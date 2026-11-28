#pragma once

#include <cstddef>
#include <string>

namespace ants::app {

/// A player's name as the match screen shows it (the chat's headers, the score labels, the countdown card's rows): without the clan tag. The game holds one plain string for a name, "[TAG] Nickname"
/// (docs/HUB_NICKNAMES.md), and a tag of up to five letters takes a third of a row that is made for the original's names, so what is shown is the nickname. A tag is square brackets round two to five
/// characters that are not spaces or brackets, then one space and at least one more character: "[ANTS1] Wolfgang" is "Wolfgang"; "[x] Zoe", "[ANT]Zoe", "[ANT] " and "Pip (bot, Hard)" stay as they are.
/// The results, the replays and the pages keep the tag.
inline std::string without_clan_tag(const std::string& name) {
    if (name.size() < 5 || name[0] != '[') return name;
    size_t characters = 0;
    size_t close = std::string::npos;
    for (size_t i = 1; i < name.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(name[i]);
        if (c == ']') {
            close = i;
            break;
        }
        if (c == '[' || c == ' ') return name;
        if ((c & 0xC0) != 0x80) ++characters;                  // (every byte that is not a continuation byte begins a character)
        if (characters > 5) return name;
    }
    if (close == std::string::npos || characters < 2) return name;
    if (close + 2 >= name.size() || name[close + 1] != ' ') return name;
    return name.substr(close + 2);
}

}  // namespace ants::app
