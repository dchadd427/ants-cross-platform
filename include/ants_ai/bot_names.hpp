#pragma once

// The names of the computer players (docs/BOTS.md, "Names"): a list of about 250 short names, the picker that draws distinct ones from a key, and the label that a bot is shown
// under ("Pip (bot, Hard)"), with the parser and the reserved-name rule that go with it. PR 1 of the series: nothing calls these functions yet; the game still names its bots
// "Bot (Level)" (bot_display_name in bot.hpp). web/front/bot_names.js is the same code in JavaScript: tests/scripts/test_bot_names.py runs both over 10,000 keys and compares.
//
// Pure functions of their arguments: no clock, no global state, no std::random_device and no std::*_distribution, so every machine and both languages give the same answer.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "ants_ai/bot.hpp"

namespace ants::ai {

/// The list (tools/bot_names.txt, made into src/ants_ai/bot_names_data.inc by tools/gen_bot_names.py): 3 to 9 ASCII letters each, no two the same in any case.
size_t bot_name_count() noexcept;
/// The name at `index` (below bot_name_count()); "" beyond it
const char* bot_name_at(size_t index) noexcept;

/// `count` DISTINCT names for a key (a value that the room fixes once, so every client reads the same names). The i-th draw is a fresh hash of (key, i); a draw that gives a name
/// the key has already given is skipped, so pick_bot_names(key, 2) is the first two of pick_bot_names(key, 3). `count` is cut to bot_name_count().
std::vector<std::string> pick_bot_names(uint64_t key, size_t count);

/// The longest name that bot_label takes (the whole label, name + " (bot, Medium)", stays within the 32 characters of a player name on the wire)
inline constexpr size_t kBotLabelNameMax = 16;

/// Whether `name` can be the name of a label: 1 to kBotLabelNameMax printable ASCII characters, no parenthesis, no blank at either end
bool valid_bot_label_name(std::string_view name) noexcept;

/// The full label of a bot: "Pip (bot, Easy|Medium|Hard)"; the two test kinds of --bot say what they are, in small letters: "Pip (bot, idle)", "Pip (bot, worker)".
/// `tag` is "easy", "medium", "hard", "idle" or "worker" in any case. A name that is not valid_bot_label_name gives the legacy label ("Bot (Hard)", "Bot (Idle)"), so a bot
/// is never shown without a name; a tag that is none of the five gives "".
std::string bot_label(std::string_view name, std::string_view tag);
std::string bot_label(std::string_view name, Level level);
/// The label of a seat's spec: "idle" and "worker" kinds by their kind, every other kind by its level (as bot_display_name does)
std::string bot_label(std::string_view name, const BotSpec& spec);

/// What parse_bot_label finds
struct BotLabel {
    std::string name;      // "Pip"; empty for the legacy form
    std::string tag;       // "easy", "medium", "hard", "idle", "worker" (small letters); empty for the short form "Pip (bot)"
    bool legacy{false};    // "Bot (Hard)": the label of every match before the names, still read
};

/// Reads a label back: "Pip (bot, Hard)", "Pip (bot, idle)", the short form "Pip (bot)" and the five legacy labels "Bot (Easy|Medium|Hard|Idle|Worker)" (exactly these). The
/// spelling must be exactly that of bot_label (a tag in other case, a blank too many or a name with a parenthesis is refused). False leaves `out` as it was.
bool parse_bot_label(std::string_view text, BotLabel& out);

/// The short form for a narrow place (the HUD's label is cut at 15 characters): "Pip (bot)" for a label in the new form, `text` unchanged for anything else
std::string bot_short_label(std::string_view text);

/// Whether a PERSON may not be called `text`: a name that starts with "Bot (" (the legacy form) or ends with "(bot)" or "(bot, ...)" (the new form, any words between the comma
/// and the parenthesis). Blanks and case are ignored. Nothing calls it yet; the six places that guard the names today (docs/BOTS.md) take it over in a later change.
bool is_reserved_bot_name(std::string_view text) noexcept;

}  // namespace ants::ai
