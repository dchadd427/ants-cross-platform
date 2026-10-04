#pragma once

// The teams of a game on this computer (docs/BOTS.md, "Alliances"): `--teams`, the start menu's Teams row, the web page's `&teams=`. Free for all (the default) or ONE pair of seats as a team, the two
// other seats too when both play; Application::form_start_teams makes them at the match start with the original's own commands (an invitation and its acceptance).

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ants::app {

struct LocalTeams {
    bool set{false};                   // false: free for all
    uint8_t a{0};                      // the pair: two different seats, 0 - 3 (0 green, 1 red, 2 blue, 3 black)
    uint8_t b{0};
    bool operator==(const LocalTeams& o) const noexcept { return set == o.set && (!set || (a == o.a && b == o.b)); }
    bool operator!=(const LocalTeams& o) const noexcept { return !(*this == o); }
};

/// "ffa" (free for all) or "A+B": two different seats, one digit 0 - 3 each, in any case. On failure `out` is untouched and `why` says what is wrong.
bool parse_local_teams(std::string_view text, LocalTeams& out, std::string& why);
/// "ffa" or "A+B": what parse_local_teams reads back
std::string local_teams_text(const LocalTeams& teams);

/// What a match of `roster` (bit s: seat s plays) makes of the teams: the pairs in the order they are made (the chosen pair, then the two other seats when both play), or `why` (and no pair) when a
/// seat of the pair does not play or the teams would be the whole match (the original ends a match at once in which every live team is allied). Free for all makes nothing and says nothing.
struct LocalTeamsPlan {
    std::vector<std::array<uint8_t, 2>> pairs;
    std::string why;
};
LocalTeamsPlan plan_local_teams(const LocalTeams& teams, uint8_t roster);

/// The choices of a screen (the start menu's cycler, the web select) for a player at `own_seat` against the bots of `filled_mask` (bit s: a bot at seat s): free for all, then the player with each bot;
/// only while two or more bots play (with one the team would be the whole match: plan_local_teams refuses it).
std::vector<LocalTeams> local_team_choices(uint8_t own_seat, uint8_t filled_mask);

}  // namespace ants::app
