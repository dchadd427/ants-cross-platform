#include "ants_server/map_names.hpp"

#include <algorithm>

#include "ants_ai/bot_names.hpp"

namespace ants::server::names {

namespace {

// Characters that are removed before the text is judged: invisible, zero width or only there to change how other text is drawn
bool is_invisible(char32_t c) {
    return c == 0x00AD || c == 0x034F || c == 0x061C || c == 0x115F || c == 0x1160 || c == 0x17B4 || c == 0x17B5 || (c >= 0x180B && c <= 0x180E) || (c >= 0x200B && c <= 0x200F) ||
           (c >= 0x202A && c <= 0x202E) || (c >= 0x2060 && c <= 0x2064) || (c >= 0x2066 && c <= 0x206F) || c == 0x3164 || (c >= 0xFE00 && c <= 0xFE0F) || c == 0xFEFF || c == 0xFFA0 ||
           (c >= 0xE0000 && c <= 0xE0FFF);
}

// Spaces of every width: all become one plain space
bool is_other_space(char32_t c) {
    return c == 0x00A0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200A) || c == 0x202F || c == 0x205F || c == 0x3000 || c == 0x2028 || c == 0x2029 || c == 0x0085;
}

bool is_control(char32_t c) { return c < 0x20 || c == 0x7F || (c >= 0x80 && c <= 0x9F); }

// A character that a display name or a note may hold. Everything allowed is its own NFC form and cannot combine with the character before it, so the text needs no normalization:
// printable ASCII (but not < and >), the Latin letters of U+00C0..U+024F (not the signs U+00D7 and U+00F7), the Greek and Cyrillic capitals and small letters, kana, the unified CJK
// ideographs, the Hangul syllables, and a few typographic marks. No combining mark (U+0300..U+036F, U+0483..U+0489, U+3099..U+309A ...) and no compatibility form is in the list.
bool allowed_display_char(char32_t c) {
    if (c >= 0x20 && c <= 0x7E) return c != U'<' && c != U'>';
    if (c >= 0x00C0 && c <= 0x00FF) return c != 0x00D7 && c != 0x00F7;
    return (c >= 0x0100 && c <= 0x024F) || (c >= 0x0391 && c <= 0x03A1) || (c >= 0x03A3 && c <= 0x03C9) || c == 0x0401 || (c >= 0x0410 && c <= 0x044F) || c == 0x0451 ||
           (c >= 0x3041 && c <= 0x3096) || (c >= 0x30A1 && c <= 0x30FA) || (c >= 0x4E00 && c <= 0x9FFF) || (c >= 0xAC00 && c <= 0xD7A3) || c == 0x2018 || c == 0x2019 || c == 0x201C ||
           c == 0x201D || c == 0x2013 || c == 0x2014 || c == 0x2026;
}

bool is_name_char(char32_t c) {
    if (c >= U'a' && c <= U'z') return true;
    if (c >= U'A' && c <= U'Z') return true;
    if (c >= U'0' && c <= U'9') return true;
    return c == U' ' || c == U'\'' || c == U'-' || c == U'_' || c == U'.' || c == U'!' || c == U'&' || c == U'(' || c == U')' || c == U',';
}

bool is_alnum(char32_t c) { return (c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z') || (c >= U'0' && c <= U'9'); }

std::string lower_ascii(std::string text) {
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return text;
}

// The names that Windows keeps for devices, whatever follows the first dot ("con", "Nul.x", "com1")
bool windows_reserved(const std::string& name) {
    std::string stem = lower_ascii(name.substr(0, name.find('.')));
    while (!stem.empty() && stem.back() == ' ') stem.pop_back();
    if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul" || stem == "clock$") return true;
    return stem.size() == 4 && (stem.compare(0, 3, "com") == 0 || stem.compare(0, 3, "lpt") == 0) && stem[3] >= '0' && stem[3] <= '9';
}

Cleaned fail(const char* reason) {
    Cleaned c;
    c.reason = reason;
    return c;
}

// The shared first steps: strict UTF-8, invisible characters out, other spaces to one plain space, control characters refused (a line end stays when `keep_newlines`)
bool prepare(std::string_view raw, bool keep_newlines, std::vector<char32_t>& out, const char*& reason) {
    std::vector<char32_t> decoded;
    if (!decode_utf8(raw, decoded)) {
        reason = "not_utf8";
        return false;
    }
    out.clear();
    for (size_t i = 0; i < decoded.size(); ++i) {
        char32_t c = decoded[i];
        if (is_invisible(c)) continue;
        if (c == U'\r' && i + 1 < decoded.size() && decoded[i + 1] == U'\n') continue;      // (a CRLF is one line end)
        if (c == U'\r') c = U'\n';
        if (c == U'\n' && keep_newlines) {
            out.push_back(c);
            continue;
        }
        if (c == U'\t' && keep_newlines) c = U' ';
        if (is_other_space(c)) c = U' ';
        if (is_control(c)) {
            reason = "control";
            return false;
        }
        out.push_back(c);
    }
    return true;
}

// Trims the spaces at both ends and makes every run of spaces one space (a line end ends a run too and is kept: at most two in a row)
void tidy(std::vector<char32_t>& v) {
    std::vector<char32_t> out;
    int newlines = 0;
    for (const char32_t c : v) {
        if (c == U'\n') {
            while (!out.empty() && out.back() == U' ') out.pop_back();
            if (out.empty()) continue;
            if (++newlines <= 2) out.push_back(c);
            continue;
        }
        newlines = 0;
        if (c == U' ' && (out.empty() || out.back() == U' ' || out.back() == U'\n')) continue;
        out.push_back(c);
    }
    while (!out.empty() && (out.back() == U' ' || out.back() == U'\n')) out.pop_back();
    v = std::move(out);
}

std::string encode(const std::vector<char32_t>& v) {
    std::string out;
    for (const char32_t c : v) append_utf8(out, c);
    return out;
}

// Letters of the Cyrillic and Greek alphabets that are drawn like a Latin letter (both cases; the Latin letter is in lower case). The maker's name is judged on its folded form, so "Owner" written with
// a Cyrillic O is the word "owner". Only letters that allowed_display_char lets through are listed. The browser's copy of this table is LOOK in web/front/mapmaker_submit.js.
struct Lookalike {
    char32_t from;
    char to;
};
constexpr Lookalike kLookalikes[] = {
    {0x0410, 'a'}, {0x0412, 'b'}, {0x0415, 'e'}, {0x041A, 'k'}, {0x041C, 'm'}, {0x041D, 'h'}, {0x041E, 'o'}, {0x0420, 'p'}, {0x0421, 'c'}, {0x0422, 't'}, {0x0423, 'y'}, {0x0425, 'x'},
    {0x0430, 'a'}, {0x0435, 'e'}, {0x043A, 'k'}, {0x043E, 'o'}, {0x0440, 'p'}, {0x0441, 'c'}, {0x0443, 'y'}, {0x0445, 'x'},
    {0x0391, 'a'}, {0x0392, 'b'}, {0x0395, 'e'}, {0x0396, 'z'}, {0x0397, 'h'}, {0x0399, 'i'}, {0x039A, 'k'}, {0x039C, 'm'}, {0x039D, 'n'}, {0x039F, 'o'}, {0x03A1, 'p'}, {0x03A4, 't'}, {0x03A5, 'y'}, {0x03A7, 'x'},
    {0x03B1, 'a'}, {0x03B9, 'i'}, {0x03BA, 'k'}, {0x03BD, 'v'}, {0x03BF, 'o'}, {0x03C1, 'p'}, {0x03C5, 'u'}, {0x03C7, 'x'}, {0x03C9, 'w'},
};

// The folded text: every look-alike letter is its Latin letter, ASCII letters are lower case, everything else stays
std::vector<char32_t> folded(const std::vector<char32_t>& v) {
    std::vector<char32_t> out;
    out.reserve(v.size());
    for (char32_t c : v) {
        for (const Lookalike& l : kLookalikes) {
            if (l.from == c) {
                c = static_cast<char32_t>(l.to);
                break;
            }
        }
        if (c >= U'A' && c <= U'Z') c = c - U'A' + U'a';
        out.push_back(c);
    }
    return out;
}

bool is_latin_letter(char32_t c) { return (c >= U'A' && c <= U'Z') || (c >= U'a' && c <= U'z') || (c >= 0x00C0 && c <= 0x024F); }
bool is_greek_or_cyrillic_letter(char32_t c) { return c >= 0x0391 && c <= 0x0451; }

// A word (a run of letters of the three alphabets) that holds a Latin letter and a Cyrillic or Greek one: what a look-alike name is made of
bool has_mixed_script_word(const std::vector<char32_t>& v) {
    bool latin = false, other = false;
    for (size_t i = 0; i <= v.size(); ++i) {
        const bool letter = i < v.size() && (is_latin_letter(v[i]) || is_greek_or_cyrillic_letter(v[i]));
        if (!letter) {
            if (latin && other) return true;
            latin = other = false;
            continue;
        }
        if (is_latin_letter(v[i])) latin = true;
        else other = true;
    }
    return false;
}

// The names that only the owner or the site may have, as the letters of the folded name spell them (digits, spaces and marks between them do not matter), also with "the" in front
bool reserved_maker_word(const std::vector<char32_t>& folded_name) {
    std::string skeleton;
    for (const char32_t c : folded_name) {
        if (c >= U'a' && c <= U'z') skeleton.push_back(static_cast<char>(c));
        else if (c >= 0x80) skeleton.push_back('?');
    }
    static const char* const kWords[] = {"owner", "admin", "administrator", "system", "moderator", "mod", "staff", "claude", "anthropic", "assistant", "server", "ants"};
    if (skeleton.size() > 3 && skeleton.compare(0, 3, "the") == 0) skeleton.erase(0, 3);
    for (const char* word : kWords) {
        if (skeleton == word) return true;
    }
    return false;
}

// Does the folded name read like a bot's (ai::is_reserved_bot_name: "Bot (...)", "... (bot)")
bool looks_like_bot(const std::vector<char32_t>& folded_name) {
    std::string ascii;
    for (const char32_t c : folded_name) ascii.push_back(c < 0x80 ? static_cast<char>(c) : '?');
    return ants::ai::is_reserved_bot_name(ascii);
}

// Cuts to `most` characters and drops a space that the cut leaves at the end
void cut_to(std::vector<char32_t>& v, size_t most) {
    if (v.size() > most) v.resize(most);
    while (!v.empty() && v.back() == U' ') v.pop_back();
}

// The characters of a description: see tidy_description
std::string tidy_characters(const std::vector<char32_t>& in) {
    std::vector<char32_t> v;
    for (char32_t c : in) {
        if (is_invisible(c)) continue;
        if (c == U'\t' || c == U'\n' || c == U'\r' || c == 0x0B || c == 0x0C || is_other_space(c)) c = U' ';
        if (is_control(c)) continue;
        v.push_back(allowed_display_char(c) ? c : U'?');
    }
    tidy(v);
    cut_to(v, kMaxDescriptionChars);
    return encode(v);
}

}  // namespace

bool decode_utf8(std::string_view bytes, std::vector<char32_t>& out) {
    out.clear();
    size_t i = 0;
    while (i < bytes.size()) {
        const auto b0 = static_cast<unsigned char>(bytes[i]);
        char32_t c;
        size_t extra;
        if (b0 < 0x80) { c = b0; extra = 0; }
        else if (b0 >= 0xC2 && b0 <= 0xDF) { c = b0 & 0x1Fu; extra = 1; }
        else if (b0 >= 0xE0 && b0 <= 0xEF) { c = b0 & 0x0Fu; extra = 2; }
        else if (b0 >= 0xF0 && b0 <= 0xF4) { c = b0 & 0x07u; extra = 3; }
        else return false;                                   // a continuation byte alone, 0xC0, 0xC1 (overlong) and 0xF5 and above
        if (extra > 0 && i + extra >= bytes.size()) return false;       // (the sequence is cut off)
        for (size_t k = 1; k <= extra; ++k) {
            const auto b = static_cast<unsigned char>(bytes[i + k]);
            if ((b & 0xC0) != 0x80) return false;
            c = (c << 6) | (b & 0x3Fu);
        }
        if (extra == 2 && c < 0x800) return false;           // overlong
        if (extra == 3 && (c < 0x10000 || c > 0x10FFFF)) return false;
        if (c >= 0xD800 && c <= 0xDFFF) return false;        // a surrogate
        out.push_back(c);
        i += extra + 1;
    }
    return true;
}

void append_utf8(std::string& out, char32_t c) {
    if (c < 0x80) {
        out.push_back(static_cast<char>(c));
    } else if (c < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (c >> 6)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    } else if (c < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (c >> 12)));
        out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (c >> 18)));
        out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    }
}

Cleaned clean_map_name(std::string_view raw) {
    if (raw.size() > 4 * kMaxNameChars) return fail("too_long");        // (checked before the text is decoded: a long text costs nothing)
    std::vector<char32_t> v;
    const char* reason = "";
    if (!prepare(raw, false, v, reason)) return fail(reason);
    tidy(v);
    if (v.empty()) return fail("empty");
    for (const char32_t c : v) {
        if (!is_name_char(c)) return fail("characters");
    }
    if (v.size() > kMaxNameChars) return fail("too_long");
    if (v.size() < kMinNameChars) return fail("too_short");
    if (!is_alnum(v.front()) || v.back() == U'.' || std::count_if(v.begin(), v.end(), is_alnum) < 2) return fail("characters");
    Cleaned out;
    out.text = encode(v);
    if (out.text.find("..") != std::string::npos) return fail("characters");
    if (windows_reserved(out.text)) return fail("reserved");
    out.ok = true;
    return out;
}

Cleaned clean_maker(std::string_view raw) {
    if (raw.size() > 4 * kMaxMakerChars + 64) return fail("too_long");
    std::vector<char32_t> v;
    const char* reason = "";
    if (!prepare(raw, false, v, reason)) return fail(reason);
    tidy(v);
    if (v.empty()) return fail("empty");
    for (const char32_t c : v) {
        if (!allowed_display_char(c)) return fail("characters");
    }
    if (v.size() > kMaxMakerChars) return fail("too_long");
    Cleaned out;
    out.text = encode(v);
    if (has_link_markup(out.text)) return fail("link");
    const std::vector<char32_t> look = folded(v);
    if (has_mixed_script_word(v) || reserved_maker_word(look) || looks_like_bot(look)) return fail("reserved");
    out.ok = true;
    return out;
}

bool has_link_markup(std::string_view cleaned_text) {
    const std::string lower = lower_ascii(std::string(cleaned_text));
    return lower.find("://") != std::string::npos || lower.find("www.") != std::string::npos || lower.find("```") != std::string::npos;
}

std::string tidy_description(std::string_view text) {
    std::vector<char32_t> v;
    if (!decode_utf8(text, v)) {
        v.clear();
        for (const char c : text) v.push_back(static_cast<unsigned char>(c));                  // (not UTF-8: Latin-1)
    }
    return tidy_characters(v);
}

std::string clean_description(std::string_view as_reported) {
    std::vector<char32_t> v;
    if (!decode_utf8(as_reported, v)) return tidy_description(as_reported);
    std::string bytes;
    for (const char32_t c : v) {
        if (c > 0xFF) return tidy_characters(v);                                                // (already characters, not the file's bytes)
        bytes.push_back(static_cast<char>(c));
    }
    std::vector<char32_t> as_text;
    if (decode_utf8(bytes, as_text)) return tidy_characters(as_text);                           // (the file's bytes are UTF-8)
    return tidy_characters(v);                                                                    // (else each byte is one Latin-1 letter)
}

Cleaned clean_note(std::string_view raw, size_t max_chars, bool required) {
    if (raw.size() > 4 * max_chars + 64) return fail("too_long");
    std::vector<char32_t> v;
    const char* reason = "";
    if (!prepare(raw, true, v, reason)) return fail(reason);
    tidy(v);
    if (v.empty()) {
        if (required) return fail("empty");
        Cleaned out;
        out.ok = true;
        return out;
    }
    for (const char32_t c : v) {
        if (c != U'\n' && !allowed_display_char(c)) return fail("characters");
    }
    if (v.size() > max_chars) return fail("too_long");
    Cleaned out;
    out.text = encode(v);
    out.ok = true;
    return out;
}

std::string name_key(std::string_view cleaned_name) { return lower_ascii(std::string(cleaned_name)); }

std::string file_name_of(std::string_view cleaned_name) { return std::string(cleaned_name) + ".lvl"; }

std::string next_free_name(std::string_view cleaned_name, const std::function<bool(const std::string& key)>& taken) {
    std::string base(cleaned_name);
    const size_t space = base.rfind(' ');
    if (space != std::string::npos && space + 1 < base.size() && std::all_of(base.begin() + static_cast<std::ptrdiff_t>(space) + 1, base.end(), [](char c) { return c >= '0' && c <= '9'; })) {
        base.erase(space);
    }
    for (int n = 2; n < 1000; ++n) {
        const std::string suffix = " " + std::to_string(n);
        std::string stem = base;
        if (stem.size() + suffix.size() > kMaxNameChars) stem.resize(kMaxNameChars - suffix.size());
        while (!stem.empty() && stem.back() == ' ') stem.pop_back();
        const std::string candidate = stem + suffix;
        if (!taken(name_key(candidate))) return candidate;
    }
    return std::string();
}

}  // namespace ants::server::names
