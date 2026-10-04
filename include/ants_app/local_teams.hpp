#pragma once

// The teams of a game on this computer (docs/BOTS.md, "Alliances"): `--teams` on the command line, the Teams choice of the start menu, `&teams=` of the web page's single player. Free for
// all (the default: every seat plays for itself) or ONE pair of seats that play as a team; the two other seats are a team too when both play. The match makes the teams with the original's
// own commands at its start (Application::form_start_teams): an invitation and its acceptance, so the engine's own News Flash "... are a team now!" is in the chat log.

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

/// What a match of `roster` (bit s: seat s plays) makes of the teams: the pairs that become teams in the order they are made (the chosen pair first, then the two other seats when both
/// play), or `why` (and no pair) when nothing can be made: a seat of the pair does not play, or the teams would leave nobody to play against (the original ends a match at once in which
/// every live team is allied: the last two live teams that ally win). Free for all makes nothing and says nothing.
struct LocalTeamsPlan {
    std::vector<std::array<uint8_t, 2>> pairs;
    std::string why;
};
LocalTeamsPlan plan_local_teams(const LocalTeams& teams, uint8_t roster);

/// The choices that a screen offers (the start menu's cycler, the web page's select) for a player at `own_seat` against the computer players of `filled_mask` (bit s: a bot plays at seat s):
/// free for all first, then the player with each of them, in the order of the seats. Only when two or more bots play: with one the team would be the whole match and would end it at the
/// first point (plan_local_teams refuses it), so there is nothing to choose.
std::vector<LocalTeams> local_team_choices(uint8_t own_seat, uint8_t filled_mask);

}  // namespace ants::app
