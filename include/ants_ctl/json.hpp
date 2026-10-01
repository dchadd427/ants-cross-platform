#pragma once

// A small, strict JSON library for the control interface of the game server (see ants_ctl/http.hpp). The bodies it reads come from another program
// over HTTP, so the parser is RFC 8259 to the letter and bounded in everything: no comments, no trailing commas, no leading zeros, no NaN or Infinity,
// no byte order mark, control characters in strings and invalid UTF-8 are refused, \uXXXX escapes must form valid surrogate pairs, and a key that is
// repeated inside one object is an error (a duplicate key is the classic way to make two programs read two different documents). Depth, size, the
// number of elements of one container and the length of a key are limited (JsonLimits), and so is the text of one number (128 characters; a double
// needs 25), so no document can make the parser allocate, recurse or compute without bound.
//
// Numbers keep their kind: a literal without a fraction and exponent that fits int64 is an integer, anything else is a double ("1.0" and "1e2" are
// doubles; "-0" is the integer 0; an integer literal outside int64 becomes the nearest double, so a program that asks for an integer gets its fallback
// instead of a silently wrong value). A literal whose double would be infinite is refused. to_json prints doubles so that they parse back to the same
// bits and always with a '.' or an exponent, so that a double stays a double after a round trip.
//
// Nothing here throws on bad data: parse_json returns false with a message, and the typed accessors return a fallback (or the shared null value) when
// the value is not of the asked kind, so a handler can write `body.get("room").get("seats").as_int_or(4)` and check the result once.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ants::ctl {

class JsonValue {
public:
    enum class Type : uint8_t { Null, Bool, Int, Double, String, Array, Object };

    JsonValue() = default;                                   // null

    static JsonValue make_null() { return JsonValue(); }
    static JsonValue make_bool(bool v);
    static JsonValue make_int(int64_t v);
    /// A double. A value that is not finite (NaN, infinity) has no JSON form: to_json prints it as null.
    static JsonValue make_double(double v);
    /// A string (UTF-8; to_json replaces invalid sequences with U+FFFD, so what comes out is always valid JSON)
    static JsonValue make_string(std::string v);
    static JsonValue make_array();
    static JsonValue make_object();
    /// An array from its elements / an object from its members in order (`keys` and `values` are paired; the longer one is cut). The keys must be
    /// distinct: the parser, which uses these, has checked that already, and a caller that builds objects by hand uses set() instead.
    static JsonValue make_array_from(std::vector<JsonValue> values);
    static JsonValue make_object_from(std::vector<std::string> keys, std::vector<JsonValue> values);

    Type type() const noexcept { return type_; }
    bool is_null() const noexcept { return type_ == Type::Null; }
    bool is_bool() const noexcept { return type_ == Type::Bool; }
    bool is_int() const noexcept { return type_ == Type::Int; }
    bool is_double() const noexcept { return type_ == Type::Double; }
    bool is_number() const noexcept { return type_ == Type::Int || type_ == Type::Double; }
    bool is_string() const noexcept { return type_ == Type::String; }
    bool is_array() const noexcept { return type_ == Type::Array; }
    bool is_object() const noexcept { return type_ == Type::Object; }

    /// The value when it is of that kind, else the fallback. as_int_or takes integers only (a double, even 3.0, gives the fallback: a count of
    /// seats must be written as an integer); as_double_or takes both kinds of numbers.
    bool as_bool_or(bool fallback) const noexcept { return type_ == Type::Bool ? bool_ : fallback; }
    int64_t as_int_or(int64_t fallback) const noexcept { return type_ == Type::Int ? int_ : fallback; }
    double as_double_or(double fallback) const noexcept;
    std::string as_string_or(std::string_view fallback = {}) const;
    /// The text of a string value; empty for every other kind
    const std::string& str() const noexcept { return string_; }

    /// Elements of an array, members of an object; 0 for everything else
    size_t size() const noexcept { return values_.size(); }
    /// Element `index` of an array or value `index` of an object; the shared null value when out of range or for any other kind
    const JsonValue& at(size_t index) const noexcept;
    /// The value of member `key` of an object; the shared null value when missing or when this is not an object
    const JsonValue& get(std::string_view key) const noexcept;
    /// The member itself, or nullptr (to tell a missing member from an explicit null)
    const JsonValue* find(std::string_view key) const noexcept;
    JsonValue* find(std::string_view key) noexcept;
    bool has(std::string_view key) const noexcept { return find(key) != nullptr; }
    /// The name of member `index` of an object; empty when out of range or for any other kind
    const std::string& key_at(size_t index) const noexcept;
    /// The elements of an array / the values of an object, in order (keys() are the object's names in the same order)
    const std::vector<JsonValue>& items() const noexcept { return values_; }
    const std::vector<std::string>& keys() const noexcept { return keys_; }

    /// Appends to an array. False (and nothing happens) when this is not an array.
    bool push_back(JsonValue v);
    /// Sets member `key` of an object: replaces the value of an existing member (it keeps its place) or appends. False when this is not an object.
    bool set(std::string key, JsonValue v);

    /// The shared null value that the lookups return for what is not there
    static const JsonValue& null_value() noexcept;

    /// Same kind and same content; objects compare member by member in order; doubles compare by bits (so 0.0 and -0.0 differ)
    bool operator==(const JsonValue& other) const;
    bool operator!=(const JsonValue& other) const { return !(*this == other); }

private:
    Type type_{Type::Null};
    bool bool_{false};
    int64_t int_{0};
    double double_{0.0};
    std::string string_;
    std::vector<JsonValue> values_;       // array elements, or object values
    std::vector<std::string> keys_;       // object keys (parallel to values_), empty for an array
};

struct JsonLimits {
    size_t max_depth{16};                 // containers inside containers (a scalar is 0, "[[]]" is 2); at most 128 whatever is asked
    size_t max_bytes{64 * 1024};          // size of the whole text
    size_t max_elements{4096};            // elements of one array / members of one object
    size_t max_key_chars{1024};           // bytes of one key after unescaping (UTF-8)
};

/// Parses `text` (one JSON value, optional white space around it). On success true and `out` holds the value; on failure false, `out` is null and
/// `error` (when not null) says what was wrong and at which byte.
bool parse_json(std::string_view text, JsonValue& out, std::string* error, const JsonLimits& limits = {});

/// The compact text of a value: no white space, members in insertion order, only what must be escaped is escaped (quote, backslash, controls below
/// 0x20: \b \f \n \r \t or \u00XX; everything else, including non-ASCII, goes out as UTF-8). parse_json reads it back to an equal value. Nesting
/// deeper than 128 (only a program can build that, the parser never does) is printed as null.
std::string to_json(const JsonValue& value);

/// True when `text` is well-formed UTF-8 (no overlong forms, no surrogates, nothing above U+10FFFF)
bool is_valid_utf8(std::string_view text);

}  // namespace ants::ctl
