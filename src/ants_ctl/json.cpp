// The strict JSON parser and the compact serializer of the control interface (see ants_ctl/json.hpp).
#include "ants_ctl/json.hpp"

#include <algorithm>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_set>
#include <utility>

namespace ants::ctl {

namespace {

constexpr size_t kHardMaxDepth = 128;             // the parser recurses once per level: whatever JsonLimits asks, the stack stays small
constexpr size_t kMaxNumberChars = 128;           // a double needs 25 characters; the cap keeps strtod's work bounded
constexpr size_t kLinearKeyScan = 16;             // objects up to this many members are checked for a repeated key by comparing, larger ones by hashing

// The sequence of UTF-8 bytes at s[i]: its length (1 - 4) and the code point, or 0 when what is there is not valid (RFC 3629 / Unicode table 3-7:
// no overlong forms, no surrogates, nothing above U+10FFFF, no stray continuation bytes, nothing cut short)
size_t utf8_sequence(std::string_view s, size_t i, uint32_t& cp) {
    const auto at = [&](size_t k) { return static_cast<uint32_t>(static_cast<unsigned char>(s[k])); };
    const uint32_t b0 = at(i);
    if (b0 < 0x80) {
        cp = b0;
        return 1;
    }
    const auto cont = [&](size_t k, uint32_t lo, uint32_t hi) { return k < s.size() && at(k) >= lo && at(k) <= hi; };
    if (b0 >= 0xC2 && b0 <= 0xDF) {
        if (!cont(i + 1, 0x80, 0xBF)) return 0;
        cp = ((b0 & 0x1Fu) << 6) | (at(i + 1) & 0x3Fu);
        return 2;
    }
    if (b0 >= 0xE0 && b0 <= 0xEF) {
        const uint32_t lo = b0 == 0xE0 ? 0xA0u : 0x80u;       // E0: no overlong
        const uint32_t hi = b0 == 0xED ? 0x9Fu : 0xBFu;       // ED: no surrogates
        if (!cont(i + 1, lo, hi) || !cont(i + 2, 0x80, 0xBF)) return 0;
        cp = ((b0 & 0x0Fu) << 12) | ((at(i + 1) & 0x3Fu) << 6) | (at(i + 2) & 0x3Fu);
        return 3;
    }
    if (b0 >= 0xF0 && b0 <= 0xF4) {
        const uint32_t lo = b0 == 0xF0 ? 0x90u : 0x80u;       // F0: no overlong
        const uint32_t hi = b0 == 0xF4 ? 0x8Fu : 0xBFu;       // F4: nothing above U+10FFFF
        if (!cont(i + 1, lo, hi) || !cont(i + 2, 0x80, 0xBF) || !cont(i + 3, 0x80, 0xBF)) return 0;
        cp = ((b0 & 0x07u) << 18) | ((at(i + 1) & 0x3Fu) << 12) | ((at(i + 2) & 0x3Fu) << 6) | (at(i + 3) & 0x3Fu);
        return 4;
    }
    return 0;                                                   // 80 - C1 and F5 - FF never start a sequence
}

void append_utf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else {
        out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    }
}

// strtod and printf use the decimal point of the current C locale; JSON's is always '.', so the text is translated before and after
std::string locale_decimal_point() {
    const std::lconv* lc = std::localeconv();
    return lc != nullptr && lc->decimal_point != nullptr && lc->decimal_point[0] != '\0' ? std::string(lc->decimal_point) : std::string(".");
}

class Parser {
public:
    Parser(std::string_view text, const JsonLimits& limits) : text_(text), limits_(limits) {
        limits_.max_depth = std::min(limits_.max_depth, kHardMaxDepth);
    }

    bool run(JsonValue& out) {
        if (text_.size() > limits_.max_bytes) return fail_at("document too large", 0);
        skip_ws();
        if (pos_ >= text_.size()) return fail("empty document");
        if (!parse_value(out, 0)) return false;
        skip_ws();
        if (pos_ != text_.size()) return fail("unexpected data after the value");
        return true;
    }

    std::string message() const { return std::string(message_) + " at byte " + std::to_string(error_pos_); }

private:
    bool fail(const char* what) { return fail_at(what, pos_); }
    bool fail_at(const char* what, size_t pos) {
        message_ = what;
        error_pos_ = pos;
        return false;
    }

    void skip_ws() {
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r') break;
            ++pos_;
        }
    }

    static bool is_digit(char c) { return c >= '0' && c <= '9'; }

    // `depth` is the number of arrays and objects that are open around this value
    bool parse_value(JsonValue& out, size_t depth) {
        if (pos_ >= text_.size()) return fail("unexpected end of input");
        const char c = text_[pos_];
        switch (c) {
            case '{': return parse_object(out, depth);
            case '[': return parse_array(out, depth);
            case '"': {
                std::string s;
                if (!parse_string(s, SIZE_MAX, "string too long")) return false;
                out = JsonValue::make_string(std::move(s));
                return true;
            }
            case 't':
                if (text_.compare(pos_, 4, "true") != 0) return fail("invalid literal");
                pos_ += 4;
                out = JsonValue::make_bool(true);
                return true;
            case 'f':
                if (text_.compare(pos_, 5, "false") != 0) return fail("invalid literal");
                pos_ += 5;
                out = JsonValue::make_bool(false);
                return true;
            case 'n':
                if (text_.compare(pos_, 4, "null") != 0) return fail("invalid literal");
                pos_ += 4;
                out = JsonValue::make_null();
                return true;
            default:
                if (c == '-' || is_digit(c)) return parse_number(out);
                return fail("unexpected character");
        }
    }

    bool parse_array(JsonValue& out, size_t depth) {
        if (depth >= limits_.max_depth) return fail("nesting too deep");
        ++pos_;                                                        // '['
        std::vector<JsonValue> values;
        skip_ws();
        if (pos_ < text_.size() && text_[pos_] == ']') {
            ++pos_;
            out = JsonValue::make_array();
            return true;
        }
        for (;;) {
            if (values.size() >= limits_.max_elements) return fail("too many array elements");
            skip_ws();
            JsonValue v;
            if (!parse_value(v, depth + 1)) return false;
            values.push_back(std::move(v));
            skip_ws();
            if (pos_ >= text_.size()) return fail("unexpected end of input");
            if (text_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (text_[pos_] == ']') {
                ++pos_;
                break;
            }
            return fail("',' or ']' expected");
        }
        out = JsonValue::make_array_from(std::move(values));
        return true;
    }

    bool parse_object(JsonValue& out, size_t depth) {
        if (depth >= limits_.max_depth) return fail("nesting too deep");
        ++pos_;                                                        // '{'
        std::vector<std::string> keys;
        std::vector<JsonValue> values;
        std::unordered_set<std::string> seen;                          // only filled for larger objects
        skip_ws();
        if (pos_ < text_.size() && text_[pos_] == '}') {
            ++pos_;
            out = JsonValue::make_object();
            return true;
        }
        for (;;) {
            skip_ws();
            if (pos_ >= text_.size()) return fail("unexpected end of input");
            if (text_[pos_] != '"') return fail("object key expected");
            if (keys.size() >= limits_.max_elements) return fail("too many object members");
            const size_t key_pos = pos_;
            std::string key;
            if (!parse_string(key, limits_.max_key_chars, "key too long")) return false;
            if (is_duplicate(keys, seen, key)) return fail_at("duplicate key", key_pos);
            skip_ws();
            if (pos_ >= text_.size() || text_[pos_] != ':') return fail("':' expected");
            ++pos_;
            skip_ws();
            JsonValue v;
            if (!parse_value(v, depth + 1)) return false;
            keys.push_back(std::move(key));
            values.push_back(std::move(v));
            skip_ws();
            if (pos_ >= text_.size()) return fail("unexpected end of input");
            if (text_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (text_[pos_] == '}') {
                ++pos_;
                break;
            }
            return fail("',' or '}' expected");
        }
        out = JsonValue::make_object_from(std::move(keys), std::move(values));
        return true;
    }

    // Small objects are checked for a repeated key by comparing with the keys so far (cheap, no allocation); once an object has kLinearKeyScan
    // members its keys go into a hash set, so that a 4096-member object costs 4096 lookups and not 8 million comparisons. The set is filled from the
    // keys when the object first reaches that size and holds every key from then on (it is empty exactly as long as it is not in use).
    static bool is_duplicate(const std::vector<std::string>& keys, std::unordered_set<std::string>& seen, const std::string& key) {
        if (keys.size() < kLinearKeyScan) return std::find(keys.begin(), keys.end(), key) != keys.end();
        if (seen.empty()) seen.insert(keys.begin(), keys.end());
        return !seen.insert(key).second;
    }

    bool parse_hex4(uint32_t& value) {
        if (text_.size() - pos_ < 4) return fail("invalid \\u escape");
        uint32_t v = 0;
        for (size_t i = 0; i < 4; ++i) {
            const char c = text_[pos_ + i];
            uint32_t d;
            if (c >= '0' && c <= '9') d = static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') d = static_cast<uint32_t>(c - 'a') + 10u;
            else if (c >= 'A' && c <= 'F') d = static_cast<uint32_t>(c - 'A') + 10u;
            else return fail_at("invalid \\u escape", pos_ + i);
            v = (v << 4) | d;
        }
        pos_ += 4;
        value = v;
        return true;
    }

    bool parse_string(std::string& out, size_t max_len, const char* too_long) {
        ++pos_;                                                        // the opening quote
        for (;;) {
            if (pos_ >= text_.size()) return fail("unterminated string");
            const unsigned char c = static_cast<unsigned char>(text_[pos_]);
            if (c == '"') {
                ++pos_;
                return true;
            }
            if (c < 0x20) return fail("control character in string");
            if (c == '\\') {
                ++pos_;
                if (pos_ >= text_.size()) return fail("unterminated string");
                const char e = text_[pos_];
                ++pos_;
                switch (e) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u': {
                        uint32_t cp = 0;
                        if (!parse_hex4(cp)) return false;
                        if (cp >= 0xD800 && cp <= 0xDBFF) {            // a high surrogate must be followed by \u and a low one
                            if (text_.size() - pos_ < 2 || text_[pos_] != '\\' || text_[pos_ + 1] != 'u') return fail("lone surrogate in \\u escape");
                            pos_ += 2;
                            uint32_t lo = 0;
                            if (!parse_hex4(lo)) return false;
                            if (lo < 0xDC00 || lo > 0xDFFF) return fail_at("lone surrogate in \\u escape", pos_ - 4);
                            cp = 0x10000u + ((cp - 0xD800u) << 10) + (lo - 0xDC00u);
                        } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                            return fail_at("lone surrogate in \\u escape", pos_ - 4);
                        }
                        append_utf8(out, cp);
                        break;
                    }
                    default: return fail_at("invalid escape", pos_ - 1);
                }
            } else if (c < 0x80) {
                out.push_back(static_cast<char>(c));
                ++pos_;
            } else {
                uint32_t cp = 0;
                const size_t len = utf8_sequence(text_, pos_, cp);
                if (len == 0) return fail("invalid UTF-8");
                out.append(text_.data() + pos_, len);
                pos_ += len;
            }
            if (out.size() > max_len) return fail(too_long);
        }
    }

    bool parse_number(JsonValue& out) {
        const size_t start = pos_;
        const bool negative = text_[pos_] == '-';
        if (negative) ++pos_;
        if (pos_ >= text_.size()) return fail("incomplete number");
        const size_t digits_at = pos_;
        if (text_[pos_] == '0') {
            ++pos_;
            if (pos_ < text_.size() && is_digit(text_[pos_])) return fail("leading zero in number");
        } else if (text_[pos_] >= '1' && text_[pos_] <= '9') {
            while (pos_ < text_.size() && is_digit(text_[pos_])) ++pos_;
        } else {
            return fail("invalid number");
        }
        const size_t int_end = pos_;
        bool is_double = false;
        if (pos_ < text_.size() && text_[pos_] == '.') {
            ++pos_;
            if (pos_ >= text_.size() || !is_digit(text_[pos_])) return fail("digit expected after the decimal point");
            while (pos_ < text_.size() && is_digit(text_[pos_])) ++pos_;
            is_double = true;
        }
        if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) ++pos_;
            if (pos_ >= text_.size() || !is_digit(text_[pos_])) return fail("digit expected in the exponent");
            while (pos_ < text_.size() && is_digit(text_[pos_])) ++pos_;
            is_double = true;
        }
        if (pos_ - start > kMaxNumberChars) return fail_at("number too long", start);

        if (!is_double) {                                              // an integer when it fits int64
            const uint64_t limit = negative ? (uint64_t{1} << 63) : (uint64_t{1} << 63) - 1;
            uint64_t acc = 0;
            bool fits = true;
            for (size_t i = digits_at; i < int_end; ++i) {
                const uint64_t d = static_cast<uint64_t>(text_[i] - '0');
                if (acc > (limit - d) / 10) {
                    fits = false;
                    break;
                }
                acc = acc * 10 + d;
            }
            if (fits) {
                int64_t v;
                if (!negative) v = static_cast<int64_t>(acc);
                else if (acc == 0) v = 0;
                else v = -static_cast<int64_t>(acc - 1) - 1;           // reaches INT64_MIN without overflowing
                out = JsonValue::make_int(v);
                return true;
            }
        }
        std::string lexeme(text_.substr(start, pos_ - start));
        const std::string point = locale_decimal_point();
        if (point != ".") {
            const size_t dot = lexeme.find('.');
            if (dot != std::string::npos) lexeme.replace(dot, 1, point);
        }
        char* end = nullptr;
        const double v = std::strtod(lexeme.c_str(), &end);
        if (end != lexeme.c_str() + lexeme.size()) return fail_at("invalid number", start);
        if (!std::isfinite(v)) return fail_at("number out of range", start);
        out = JsonValue::make_double(v);
        return true;
    }

    std::string_view text_;
    JsonLimits limits_;
    size_t pos_{0};
    const char* message_{"error"};
    size_t error_pos_{0};
};

// ---- serializer ----

// The shortest of %.15g, %.16g, %.17g that reads back as the same double (17 digits always does), always with a '.' or an exponent
std::string format_double(double v) {
    if (!std::isfinite(v)) return "null";
    char buf[48];
    for (int precision = 15; precision <= 17; ++precision) {
        std::snprintf(buf, sizeof(buf), "%.*g", precision, v);
        const double back = std::strtod(buf, nullptr);
        if (std::memcmp(&back, &v, sizeof(double)) == 0) break;
    }
    std::string s(buf);
    const std::string point = locale_decimal_point();
    if (point != ".") {
        const size_t at = s.find(point);
        if (at != std::string::npos) s.replace(at, point.size(), ".");
    }
    if (s.find_first_of(".eE") == std::string::npos) s += ".0";
    return s;
}

void write_string(const std::string& s, std::string& out) {
    static const char kHex[] = "0123456789abcdef";
    out.push_back('"');
    size_t i = 0;
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c >= 0x80) {
            uint32_t cp = 0;
            const size_t len = utf8_sequence(s, i, cp);
            if (len == 0) {
                out.append("\xEF\xBF\xBD");                          // invalid UTF-8 would make invalid JSON: U+FFFD instead
                ++i;
            } else {
                out.append(s, i, len);
                i += len;
            }
            continue;
        }
        switch (c) {
            case '"': out.append("\\\""); break;
            case '\\': out.append("\\\\"); break;
            case '\b': out.append("\\b"); break;
            case '\f': out.append("\\f"); break;
            case '\n': out.append("\\n"); break;
            case '\r': out.append("\\r"); break;
            case '\t': out.append("\\t"); break;
            default:
                if (c < 0x20) {
                    out.append("\\u00");
                    out.push_back(kHex[c >> 4]);
                    out.push_back(kHex[c & 0xF]);
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
        ++i;
    }
    out.push_back('"');
}

void write_value(const JsonValue& v, std::string& out, size_t depth) {
    switch (v.type()) {
        case JsonValue::Type::Null: out.append("null"); break;
        case JsonValue::Type::Bool: out.append(v.as_bool_or(false) ? "true" : "false"); break;
        case JsonValue::Type::Int: out.append(std::to_string(v.as_int_or(0))); break;
        case JsonValue::Type::Double: out.append(format_double(v.as_double_or(0.0))); break;
        case JsonValue::Type::String: write_string(v.str(), out); break;
        case JsonValue::Type::Array:
            if (depth >= kHardMaxDepth) {
                out.append("null");
                break;
            }
            out.push_back('[');
            for (size_t i = 0; i < v.size(); ++i) {
                if (i > 0) out.push_back(',');
                write_value(v.at(i), out, depth + 1);
            }
            out.push_back(']');
            break;
        case JsonValue::Type::Object:
            if (depth >= kHardMaxDepth) {
                out.append("null");
                break;
            }
            out.push_back('{');
            for (size_t i = 0; i < v.size(); ++i) {
                if (i > 0) out.push_back(',');
                write_string(v.key_at(i), out);
                out.push_back(':');
                write_value(v.at(i), out, depth + 1);
            }
            out.push_back('}');
            break;
    }
}

}  // namespace

// ---- JsonValue ----

JsonValue JsonValue::make_bool(bool v) {
    JsonValue j;
    j.type_ = Type::Bool;
    j.bool_ = v;
    return j;
}

JsonValue JsonValue::make_int(int64_t v) {
    JsonValue j;
    j.type_ = Type::Int;
    j.int_ = v;
    return j;
}

JsonValue JsonValue::make_double(double v) {
    JsonValue j;
    j.type_ = Type::Double;
    j.double_ = v;
    return j;
}

JsonValue JsonValue::make_string(std::string v) {
    JsonValue j;
    j.type_ = Type::String;
    j.string_ = std::move(v);
    return j;
}

JsonValue JsonValue::make_array() {
    JsonValue j;
    j.type_ = Type::Array;
    return j;
}

JsonValue JsonValue::make_object() {
    JsonValue j;
    j.type_ = Type::Object;
    return j;
}

JsonValue JsonValue::make_array_from(std::vector<JsonValue> values) {
    JsonValue j;
    j.type_ = Type::Array;
    j.values_ = std::move(values);
    return j;
}

JsonValue JsonValue::make_object_from(std::vector<std::string> keys, std::vector<JsonValue> values) {
    const size_t n = std::min(keys.size(), values.size());
    keys.erase(keys.begin() + static_cast<std::ptrdiff_t>(n), keys.end());
    values.erase(values.begin() + static_cast<std::ptrdiff_t>(n), values.end());
    JsonValue j;
    j.type_ = Type::Object;
    j.keys_ = std::move(keys);
    j.values_ = std::move(values);
    return j;
}

double JsonValue::as_double_or(double fallback) const noexcept {
    if (type_ == Type::Double) return double_;
    if (type_ == Type::Int) return static_cast<double>(int_);
    return fallback;
}

std::string JsonValue::as_string_or(std::string_view fallback) const {
    return type_ == Type::String ? string_ : std::string(fallback);
}

const JsonValue& JsonValue::null_value() noexcept {
    static const JsonValue kNull;
    return kNull;
}

const JsonValue& JsonValue::at(size_t index) const noexcept {
    return index < values_.size() ? values_[index] : null_value();
}

const JsonValue* JsonValue::find(std::string_view key) const noexcept {
    for (size_t i = 0; i < keys_.size(); ++i) {
        if (keys_[i] == key) return &values_[i];
    }
    return nullptr;
}

JsonValue* JsonValue::find(std::string_view key) noexcept {
    for (size_t i = 0; i < keys_.size(); ++i) {
        if (keys_[i] == key) return &values_[i];
    }
    return nullptr;
}

const JsonValue& JsonValue::get(std::string_view key) const noexcept {
    const JsonValue* v = find(key);
    return v != nullptr ? *v : null_value();
}

const std::string& JsonValue::key_at(size_t index) const noexcept {
    static const std::string kEmpty;
    return index < keys_.size() ? keys_[index] : kEmpty;
}

bool JsonValue::push_back(JsonValue v) {
    if (type_ != Type::Array) return false;
    values_.push_back(std::move(v));
    return true;
}

bool JsonValue::set(std::string key, JsonValue v) {
    if (type_ != Type::Object) return false;
    if (JsonValue* existing = find(key)) {
        *existing = std::move(v);
        return true;
    }
    keys_.push_back(std::move(key));
    values_.push_back(std::move(v));
    return true;
}

bool JsonValue::operator==(const JsonValue& other) const {
    if (type_ != other.type_) return false;
    switch (type_) {
        case Type::Null: return true;
        case Type::Bool: return bool_ == other.bool_;
        case Type::Int: return int_ == other.int_;
        case Type::Double: return std::memcmp(&double_, &other.double_, sizeof(double)) == 0;
        case Type::String: return string_ == other.string_;
        case Type::Array: return values_ == other.values_;
        case Type::Object: return keys_ == other.keys_ && values_ == other.values_;
    }
    return false;
}

// ---- free functions ----

bool parse_json(std::string_view text, JsonValue& out, std::string* error, const JsonLimits& limits) {
    out = JsonValue();
    Parser parser(text, limits);
    JsonValue parsed;
    if (!parser.run(parsed)) {
        if (error != nullptr) *error = parser.message();
        return false;
    }
    if (error != nullptr) error->clear();
    out = std::move(parsed);
    return true;
}

std::string to_json(const JsonValue& value) {
    std::string out;
    write_value(value, out, 0);
    return out;
}

bool is_valid_utf8(std::string_view text) {
    size_t i = 0;
    while (i < text.size()) {
        uint32_t cp = 0;
        const size_t len = utf8_sequence(text, i, cp);
        if (len == 0) return false;
        i += len;
    }
    return true;
}

}  // namespace ants::ctl
