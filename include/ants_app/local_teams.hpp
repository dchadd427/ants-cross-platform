#pragma once

// The teams of a game on this computer (docs/BOTS.md, "Alliances"): `--teams`, the start menu's Teams row, the web page's `&teams=`. The model lives in ants_sim/start_teams.hpp since protocol 13 (a room's
// teams use it too); these are the names that a game on this computer has always used. Application::form_start_teams makes the teams at the match start with the original's own commands.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "ants_sim/start_teams.hpp"

namespace ants::app {

using LocalTeams = sim::StartTeams;
using LocalTeamsPlan = sim::StartTeamsPlan;

inline bool parse_local_teams(std::string_view text, LocalTeams& out, std::string& why) { return sim::parse_start_teams(text, out, why); }
inline std::string local_teams_text(const LocalTeams& teams) { return sim::start_teams_text(teams); }
inline LocalTeamsPlan plan_local_teams(const LocalTeams& teams, uint8_t roster) { return sim::plan_start_teams(teams, roster); }
using sim::local_team_choices;

}  // namespace ants::app
