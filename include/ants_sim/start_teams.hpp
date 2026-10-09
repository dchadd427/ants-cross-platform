#pragma once

// The teams that a match starts with (docs/BOTS.md, "Alliances"; docs/NETWORK_PORT.md, "Protocol 13"): free for all (the default) or ONE pair of seats as a team, the two other seats too when both play.
// One rule for every way a match starts: a game on this computer (`--teams`, the start menu's Teams row, the web page's `&teams=`), a room on the local network and a server's room (the leader's choice,
// which travels in the start data). Every engine that starts a match calls apply_start_teams with the same value right after init() and before its first tick, with the original's own commands
// (an invitation and its acceptance, applied straight to the engine), so the machines, the referee and an engine that is restored or caught up from nothing agree on every state hash.

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "ants_sim/sim_engine.hpp"

namespace ants::sim {

struct StartTeams {
    bool set{false};                   // false: free for all
    uint8_t a{0};                      // the pair: two different seats, 0 - 3 (0 green, 1 red, 2 blue, 3 black); the first seat invites, the second accepts
    uint8_t b{0};
    bool operator==(const StartTeams& o) const noexcept { return set == o.set && (!set || (a == o.a && b == o.b)); }
    bool operator!=(const StartTeams& o) const noexcept { return !(*this == o); }
};

/// "ffa" (free for all) or "A+B": two different seats, one digit 0 - 3 each, in any case. On failure `out` is untouched and `why` says what is wrong.
bool parse_start_teams(std::string_view text, StartTeams& out, std::string& why);
/// "ffa" or "A+B": what parse_start_teams reads back
std::string start_teams_text(const StartTeams& teams);

/// What a match of `roster` (bit s: seat s plays) makes of the teams: the pairs in the order they are made (the chosen pair, then the two other seats when both play), or `why` (and no pair) when a
/// seat of the pair does not play or the teams would be the whole match (the original ends a match at once in which every live team is allied). Free for all makes nothing and says nothing.
/// `short_why` is the same reason in a form that fits a line of chat with the prefix "No teams: " (the room's notice, at most kMaxChatChars in all): the seat's reason is short already, and
/// names the seat by its colour as the pages do ("Black does not play in this match."), where `why` has the digit that the command line takes ("seat 3 does not play in this match.").
struct StartTeamsPlan {
    std::vector<std::array<uint8_t, 2>> pairs;
    std::string why;
    std::string short_why;
};
StartTeamsPlan plan_start_teams(const StartTeams& teams, uint8_t roster);

/// The teams that a start asks for (protocol 13): the room's own, which the room has from the create block that made it (protocol 15; net::RoomMsg::teams()), hold for EVERY start, the automatic start of a full room included; a leader's request
/// counts only when it is what starts the match (`by_leader`) and only in a room that has none of its own. What the seats that play can make of them is plan_start_teams's business.
StartTeams start_teams_for(const StartTeams& room_own, bool by_leader, const StartTeams& asked) noexcept;

/// The choices of a screen for a player at `own_seat` against the bots of `filled_mask` (bit s: a bot at seat s): free for all, then the player with each bot; only while two or more bots play
/// (with one the team would be the whole match: plan_start_teams refuses it).
std::vector<StartTeams> local_team_choices(uint8_t own_seat, uint8_t filled_mask);
/// The choices for the room of `players` seats (2 - 4): free for all first, then the pairs 0+1 and 0+2 and 0+3 for four players, 0+1, 0+2 and 1+2 for three (the third seat plays alone); only free for all for two.
std::vector<StartTeams> room_team_choices(uint8_t players);
/// The choices for a match of the seats in `roster` (bit s: seat s plays; any seats, not the first ones): free for all first, then every way to make teams: four seats: the lowest seat with each of the others
/// (the two left are the other team), three: any two of the three (the third plays alone); only free for all for two (and for fewer or more than that). For the seats 0 to `players` - 1 it is room_team_choices(players).
std::vector<StartTeams> roster_team_choices(uint8_t roster);
/// A choice as a person reads it, from the colour words of the seats that play (`roster`): "Free for all", "Green + Red against Blue + Black", "Green + Red against Blue". A pair that `roster` cannot make
/// is written as "Green + Red" alone.
std::string start_teams_title(const StartTeams& teams, uint8_t roster);

/// Makes the teams in a started match: for each pair of plan_start_teams(teams, engine.roster_mask()) the first seat invites and the second accepts, applied with apply_command (the engine's own News Flash
/// "... are a team now!" is posted, no invitation stays open). Call it after init() and the names, before the first tick. Nothing happens for free for all and for teams that cannot be made.
void apply_start_teams(SimulationEngine& engine, const StartTeams& teams);

}  // namespace ants::sim
