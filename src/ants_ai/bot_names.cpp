#include "ants_ai/bot_names.hpp"

#include <algorithm>
#include <utility>
#include <vector>

#include "ants_ai/rng.hpp"

namespace ants::ai {

namespace {

const char* const kBotNames[] = {
#include "bot_names_data.inc"
};
constexpr size_t kNameCount = sizeof(kBotNames) / sizeof(kBotNames[0]);

constexpr uint64_t kPickSeed = 0xB07A11E5B07A11E5ull;     // (not the seat seeds of rng.hpp: a name draw must never move with them)
constexpr uint64_t kPickStep = 0x9E3779B97F4A7C15ull;

char lower(char c) noexcept { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

std::string lowered(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = lower(c);
    return out;
}

/// the five words after the comma, as bot_label writes them; "" when `tag` is none of them
const char* label_tag(std::string_view tag) {
    const std::string t = lowered(tag);
    if (t == "easy") return "Easy";
    if (t == "medium") return "Medium";
    if (t == "hard") return "Hard";
    if (t == "idle") return "idle";
    if (t == "worker") return "worker";
    return "";
}

bool starts_with(std::string_view s, std::string_view p) noexcept { return s.size() >= p.size() && s.compare(0, p.size(), p) == 0; }
bool ends_with(std::string_view s, std::string_view p) noexcept { return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0; }

// ---- the names that a person may not take (is_reserved_bot_name) --------------------------------------------------------------------------------------------------------------------
// A name is read the way a person reads it, not the way it is spelled: case, blanks, signs between letters, invisible characters and letters that are drawn like b, o or t (0 7 8 +, the Cyrillic and Greek ones,
// letters with accents, wide and bold forms) do not hide a label. Every character becomes one Glyph; everything that is not a letter, a digit or a bracket is only a break between letters.

enum class Glyph : uint8_t { Skip, Break, Open, Close, Letter };      // Skip: it is not there to the eye (a combining mark, a zero-width character); Break: a blank or a sign
struct Folded {
    Glyph glyph{Glyph::Break};
    char letter{0};                                                    // Letter: a to z or a digit (the look-alikes of b, o and t already turned into them)
};

constexpr uint32_t kBadCodePoint = 0xFFFDu;

uint32_t next_code_point(std::string_view s, size_t& i) noexcept {      // UTF-8; a byte that is no part of a character is one code point of its own (it is a break)
    const auto byte = [&](size_t at) { return static_cast<uint32_t>(static_cast<unsigned char>(s[at])); };
    const uint32_t b0 = byte(i);
    size_t length = 1;
    uint32_t cp = b0;
    if (b0 >= 0xF0 && b0 < 0xF8) { length = 4; cp = b0 & 0x07u; }
    else if (b0 >= 0xE0) { length = 3; cp = b0 & 0x0Fu; }
    else if (b0 >= 0xC0) { length = 2; cp = b0 & 0x1Fu; }
    else if (b0 >= 0x80) { ++i; return kBadCodePoint; }
    if (b0 >= 0xF8 || i + length > s.size()) { ++i; return b0 < 0x80 ? b0 : kBadCodePoint; }
    for (size_t k = 1; k < length; ++k) {
        const uint32_t b = byte(i + k);
        if ((b & 0xC0u) != 0x80u) { ++i; return kBadCodePoint; }
        cp = (cp << 6) | (b & 0x3Fu);
    }
    i += length;
    return cp;
}

Folded letter_glyph(char c) noexcept {
    Folded out;
    out.glyph = Glyph::Letter;
    out.letter = c == '0' ? 'o' : c == '7' ? 't' : c == '8' ? 'b' : c;
    return out;
}

struct Lookalike {
    uint32_t first;
    uint32_t last;
    char letter;
};
constexpr Lookalike kLookalikes[] = {
    {0x00B0, 0x00B0, 'o'}, {0x00BA, 0x00BA, 'o'}, {0x00D2, 0x00D6, 'o'}, {0x00D8, 0x00D8, 'o'}, {0x00F2, 0x00F6, 'o'}, {0x00F8, 0x00F8, 'o'},     // (accents and signs: the degree sign, the ordinal, o with a grave to a diaeresis, o with a stroke)
    {0x014C, 0x0151, 'o'}, {0x01A0, 0x01A1, 'o'}, {0x01D1, 0x01D2, 'o'}, {0x01EA, 0x01ED, 'o'}, {0x1ECC, 0x1EE3, 'o'},
    {0x039F, 0x039F, 'o'}, {0x03BF, 0x03BF, 'o'}, {0x041E, 0x041E, 'o'}, {0x043E, 0x043E, 'o'}, {0x0585, 0x0585, 'o'}, {0x1D0F, 0x1D0F, 'o'}, {0x1D52, 0x1D52, 'o'}, {0x2092, 0x2092, 'o'},      // (Greek, Cyrillic, Armenian, small capital, superscript, subscript)
    {0x0180, 0x0180, 'b'}, {0x0253, 0x0253, 'b'}, {0x0299, 0x0299, 'b'}, {0x0392, 0x0392, 'b'}, {0x03B2, 0x03B2, 'b'}, {0x0412, 0x0412, 'b'}, {0x0432, 0x0432, 'b'}, {0x042C, 0x042C, 'b'}, {0x044C, 0x044C, 'b'},
    {0x1D2E, 0x1D2E, 'b'}, {0x1D47, 0x1D47, 'b'}, {0x1E02, 0x1E07, 'b'},
    {0x0162, 0x0167, 't'}, {0x01AB, 0x01AB, 't'}, {0x01AD, 0x01AD, 't'}, {0x021A, 0x021B, 't'}, {0x03A4, 0x03A4, 't'}, {0x03C4, 0x03C4, 't'}, {0x0422, 0x0422, 't'}, {0x0442, 0x0442, 't'},    // (0x03A4: Greek capital tau)
    {0x1D1B, 0x1D1B, 't'}, {0x1D57, 0x1D57, 't'}, {0x1E6A, 0x1E6F, 't'}, {0x2020, 0x2020, 't'},
};

// The brackets that are not ASCII, as pairs (an opening one, and the closing one that follows it by one)
constexpr uint32_t kOpeners[] = {0x2329, 0x2768, 0x276A, 0x276C, 0x2770, 0x27E6, 0x27E8, 0x27EA, 0x3008, 0x300A, 0x300C, 0x300E, 0x3010, 0x3014, 0xFE59, 0xFE5B, 0xFE5D};

Folded fold_code_point(uint32_t cp) noexcept {
    Folded out;
    if (cp < 0x80u) {
        const char c = lower(static_cast<char>(cp));
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) return letter_glyph(c);
        if (c == '+') return letter_glyph('t');
        if (c == '(' || c == '[' || c == '{' || c == '<') out.glyph = Glyph::Open;
        else if (c == ')' || c == ']' || c == '}' || c == '>') out.glyph = Glyph::Close;
        return out;                                                                      // (a blank or a sign: a break)
    }
    if (cp >= 0xFF01u && cp <= 0xFF5Eu) return fold_code_point(cp - 0xFEE0u);                            // the wide forms of ASCII
    if (cp >= 0x1D400u && cp <= 0x1D6A3u) return letter_glyph(static_cast<char>('a' + (cp - 0x1D400u) % 52u % 26u));       // bold, italic, script, fraktur, double-struck, monospace letters
    if (cp >= 0x1D7CEu && cp <= 0x1D7FFu) return letter_glyph(static_cast<char>('0' + (cp - 0x1D7CEu) % 10u));
    if (cp >= 0x24B6u && cp <= 0x24CFu) return letter_glyph(static_cast<char>('a' + (cp - 0x24B6u)));      // circled letters
    if (cp >= 0x24D0u && cp <= 0x24E9u) return letter_glyph(static_cast<char>('a' + (cp - 0x24D0u)));
    if (cp == 0x00ADu || cp == 0x034Fu || (cp >= 0x0300u && cp <= 0x036Fu) || (cp >= 0x1AB0u && cp <= 0x1AFFu) || (cp >= 0x1DC0u && cp <= 0x1DFFu) || (cp >= 0x200Bu && cp <= 0x200Fu) ||
        (cp >= 0x202Au && cp <= 0x202Eu) || (cp >= 0x2060u && cp <= 0x206Fu) || (cp >= 0x20D0u && cp <= 0x20FFu) || (cp >= 0xFE00u && cp <= 0xFE0Fu) || cp == 0xFEFFu) {
        out.glyph = Glyph::Skip;
        return out;
    }
    for (const uint32_t open : kOpeners) {
        if (cp == open) out.glyph = Glyph::Open;
        else if (cp == open + 1) out.glyph = Glyph::Close;
    }
    if (out.glyph != Glyph::Break) return out;
    for (const Lookalike& l : kLookalikes) {
        if (cp >= l.first && cp <= l.last) return letter_glyph(l.letter);
    }
    return out;                                                                          // (any other character, a no-break space included: a break)
}

std::vector<Folded> fold_name(std::string_view text) {
    std::vector<Folded> out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size();) out.push_back(fold_code_point(next_code_point(text, i)));
    return out;
}

// The word bot at `at`: the letters b, o, t, in that order, with breaks and invisible characters between them and nothing else (not another letter, not a bracket); `end` is the place after the t
bool bot_word_at(const std::vector<Folded>& f, size_t at, size_t& end) noexcept {
    static const char kWord[3] = {'b', 'o', 't'};
    size_t found = 0;
    size_t i = at;
    for (; i < f.size() && found < 3; ++i) {
        if (f[i].glyph == Glyph::Letter) {
            if (f[i].letter != kWord[found]) return false;
            ++found;
        } else if (f[i].glyph == Glyph::Open || f[i].glyph == Glyph::Close) {
            return false;
        }
    }
    if (found < 3) return false;
    end = i;
    return true;
}

}  // namespace

size_t bot_name_count() noexcept { return kNameCount; }

const char* bot_name_at(size_t index) noexcept { return index < kNameCount ? kBotNames[index] : ""; }

std::vector<std::string> pick_bot_names(uint64_t key, size_t count) {
    count = std::min(count, kNameCount);
    std::vector<std::string> out;
    std::vector<bool> used(kNameCount, false);
    const uint64_t base = mix64(key ^ kPickSeed);
    for (uint64_t draw = 1; out.size() < count; ++draw) {          // (each draw is a fresh hash, so a draw that repeats a name is skipped; count == the list's size ends too)
        const size_t index = static_cast<size_t>(mix64(base + draw * kPickStep) % kNameCount);
        if (used[index]) continue;
        used[index] = true;
        out.emplace_back(kBotNames[index]);
    }
    return out;
}

uint64_t bot_name_key(std::string_view text) noexcept {
    uint64_t h = 0xCBF29CE484222325ull;
    for (const char c : text) h = (h ^ static_cast<unsigned char>(c)) * 0x100000001B3ull;
    h = mix64(h ^ 0xB07BA3E5ull);
    return h == 0 ? 1 : h;
}

std::string bot_seat_name(uint64_t key, uint8_t seat) {
    if (key == 0 || seat >= 4) return std::string();
    return pick_bot_names(key, 4)[seat];
}

bool valid_bot_label_name(std::string_view name) noexcept {
    if (name.empty() || name.size() > kBotLabelNameMax || name.front() == ' ' || name.back() == ' ') return false;
    for (const char c : name) {
        if (c < 0x20 || c > 0x7E || c == '(' || c == ')') return false;
    }
    return true;
}

std::string bot_label(std::string_view name, std::string_view tag) {
    const std::string word = label_tag(tag);
    if (word.empty()) return std::string();
    if (!valid_bot_label_name(name)) {
        const std::string legacy = word == "idle" ? "Idle" : word == "worker" ? "Worker" : word;
        return "Bot (" + legacy + ")";
    }
    return std::string(name) + " (bot, " + word + ")";
}

std::string bot_label(std::string_view name, Level level) { return bot_label(name, std::string_view(level_name(level))); }

std::string bot_label(std::string_view name, const BotSpec& spec) {
    if (spec.kind == "idle" || spec.kind == "worker") return bot_label(name, std::string_view(spec.kind));
    return bot_label(name, spec.level);
}

std::string bot_room_label(uint64_t key, const BotSpec& spec) {
    if (key == 0) return bot_display_name(spec);
    return bot_label(bot_seat_name(key, spec.seat), spec);
}

bool parse_bot_label(std::string_view text, BotLabel& out) {
    static const char* const kLegacy[] = {"Bot (Easy)", "Bot (Medium)", "Bot (Hard)", "Bot (Idle)", "Bot (Worker)"};
    for (const char* legacy : kLegacy) {
        if (text == legacy) {
            BotLabel found;
            found.legacy = true;
            found.tag = lowered(std::string_view(legacy).substr(5, std::string_view(legacy).size() - 6));
            out = std::move(found);
            return true;
        }
    }
    if (!ends_with(text, ")")) return false;
    const size_t open = text.rfind(" (bot");
    if (open == std::string_view::npos) return false;
    const std::string_view name = text.substr(0, open);
    const std::string_view inside = text.substr(open + 5, text.size() - open - 6);          // what stands between "(bot" and ")"
    BotLabel found;
    found.name = std::string(name);
    if (!inside.empty()) {
        if (!starts_with(inside, ", ")) return false;
        const std::string_view word = inside.substr(2);
        for (const char* ok : {"Easy", "Medium", "Hard", "idle", "worker"}) {
            if (word == ok) found.tag = lowered(word);
        }
        if (found.tag.empty()) return false;
    }
    if (!valid_bot_label_name(name)) return false;
    out = std::move(found);
    return true;
}

std::string bot_short_label(std::string_view text) {
    BotLabel parsed;
    if (parse_bot_label(text, parsed) && !parsed.legacy) return parsed.name + " (bot)";
    return std::string(text);
}

bool is_reserved_bot_name(std::string_view text) {
    const std::vector<Folded> f = fold_name(text);
    const size_t n = f.size();
    // "Bot (...)": the word bot at the start, then an opening bracket (blanks and signs may stand between)
    size_t end = 0;
    if (bot_word_at(f, 0, end)) {
        while (end < n && (f[end].glyph == Glyph::Skip || f[end].glyph == Glyph::Break)) ++end;
        if (end < n && f[end].glyph == Glyph::Open) return true;
    }
    // "... (bot)", "... (bot, Hard)": an opening bracket, the word bot, and then no letter (a bracket, a comma, a blank, a sign: whatever follows, the name reads as a computer player's label)
    for (size_t i = 0; i < n; ++i) {
        if (f[i].glyph != Glyph::Open || !bot_word_at(f, i + 1, end)) continue;
        while (end < n && f[end].glyph == Glyph::Skip) ++end;
        if (end < n && f[end].glyph != Glyph::Letter) return true;
    }
    return false;
}

}  // namespace ants::ai
