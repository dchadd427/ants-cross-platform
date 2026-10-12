#include "ants_ai/bot_names.hpp"

#include <algorithm>
#include <utility>

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

bool is_reserved_bot_name(std::string_view text) noexcept {
    std::string s;
    for (const char c : text) {
        if (c != ' ') s.push_back(lower(c));
    }
    if (starts_with(s, "bot(")) return true;                       // the legacy form: "Bot (Hard)" and anything that starts that way
    if (!ends_with(s, ")")) return false;
    const size_t open = s.rfind('(');
    if (open == std::string::npos) return false;
    const std::string inside = s.substr(open + 1, s.size() - open - 2);                       // between the last "(" and the final ")"
    if (inside == "bot") return true;
    return starts_with(inside, "bot,") && inside.size() > 4 && inside.find(')') == std::string::npos;
}

}  // namespace ants::ai
