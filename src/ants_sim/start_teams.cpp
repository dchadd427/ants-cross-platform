#include "ants_sim/start_teams.hpp"

#include "ants_sim/command.hpp"
#include "ants_sim/game_strings.hpp"

namespace ants::sim {

namespace {

bool is_seat_digit(char c) noexcept { return c >= '0' && c <= '3'; }

// "Green", "Red", "Blue", "Black": the colour word of a seat (seat 0 is green, the engine's own numbering)
std::string seat_word(uint8_t seat) { return seat < MAX_PLAYERS ? std::string(strings::colour_name(static_cast<uint8_t>(3u - seat))) : std::string(); }

}  // namespace

bool parse_start_teams(std::string_view text, StartTeams& out, std::string& why) {
    if (text.size() == 3 && (text[0] == 'f' || text[0] == 'F') && (text[1] == 'f' || text[1] == 'F') && (text[2] == 'a' || text[2] == 'A')) {
        out = StartTeams{};
        return true;
    }
    if (text.size() == 3 && is_seat_digit(text[0]) && text[1] == '+' && is_seat_digit(text[2])) {
        if (text[0] == text[2]) {
            why = "a team needs two different seats.";
            return false;
        }
        out.set = true;
        out.a = static_cast<uint8_t>(text[0] - '0');
        out.b = static_cast<uint8_t>(text[2] - '0');
        return true;
    }
    why = "write ffa (free for all) or two different seats of 0 to 3 joined by +, for example 0+1.";
    return false;
}

std::string start_teams_text(const StartTeams& teams) {
    if (!teams.set) return "ffa";
    return std::string(1, static_cast<char>('0' + teams.a)) + "+" + std::string(1, static_cast<char>('0' + teams.b));
}

StartTeamsPlan plan_start_teams(const StartTeams& teams, uint8_t roster) {
    StartTeamsPlan plan;
    if (!teams.set) return plan;
    if (teams.a == teams.b) {                                                       // (parse refuses it; a message or a struct that says it is told the same)
        plan.why = "a team needs two different seats.";
        plan.short_why = plan.why;
        return plan;
    }
    const auto plays = [roster](uint8_t seat) { return seat < 4 && ((roster >> seat) & 1u) != 0; };
    for (const uint8_t seat : {teams.a, teams.b}) {
        if (!plays(seat)) {
            plan.why = "seat " + std::to_string(static_cast<unsigned>(seat)) + " does not play in this match.";
            plan.short_why = plan.why;
            return plan;
        }
    }
    std::array<uint8_t, 2> others{};
    size_t count = 0;
    for (uint8_t seat = 0; seat < 4; ++seat) {
        if (plays(seat) && seat != teams.a && seat != teams.b) {
            if (count < others.size()) others[count] = seat;
            ++count;
        }
    }
    if (count == 0) {
        plan.why = "these are the only two seats that play, and a match in which every team is allied ends at once.";
        plan.short_why = "only two seats play: a team of them would end the match at once.";
        return plan;
    }
    plan.pairs.push_back({teams.a, teams.b});
    if (count == 2) plan.pairs.push_back(others);                                   // (one other seat plays alone)
    return plan;
}

std::vector<StartTeams> local_team_choices(uint8_t own_seat, uint8_t filled_mask) {
    std::vector<StartTeams> choices{StartTeams{}};
    size_t filled = 0;
    for (uint8_t seat = 0; seat < 4; ++seat) {
        if (seat != own_seat && ((filled_mask >> seat) & 1u) != 0) ++filled;
    }
    if (own_seat >= 4 || filled < 2) return choices;
    for (uint8_t seat = 0; seat < 4; ++seat) {
        if (seat != own_seat && ((filled_mask >> seat) & 1u) != 0) choices.push_back(StartTeams{true, own_seat, seat});
    }
    return choices;
}

std::vector<StartTeams> room_team_choices(uint8_t players) {
    std::vector<StartTeams> choices{StartTeams{}};
    if (players == 3) {
        choices.push_back(StartTeams{true, 0, 1});
        choices.push_back(StartTeams{true, 0, 2});
        choices.push_back(StartTeams{true, 1, 2});
    } else if (players == 4) {
        choices.push_back(StartTeams{true, 0, 1});
        choices.push_back(StartTeams{true, 0, 2});
        choices.push_back(StartTeams{true, 0, 3});
    }
    return choices;
}

std::string start_teams_title(const StartTeams& teams, uint8_t roster) {
    if (!teams.set) return "Free for all";
    const auto plays = [roster](uint8_t seat) { return seat < 4 && ((roster >> seat) & 1u) != 0; };
    const std::string pair = seat_word(teams.a) + " + " + seat_word(teams.b);
    if (!plays(teams.a) || !plays(teams.b)) return pair;
    std::string others;
    for (uint8_t seat = 0; seat < 4; ++seat) {
        if (plays(seat) && seat != teams.a && seat != teams.b) others += (others.empty() ? "" : " + ") + seat_word(seat);
    }
    return others.empty() ? pair : pair + " against " + others;
}

void apply_start_teams(SimulationEngine& engine, const StartTeams& teams) {
    const StartTeamsPlan plan = plan_start_teams(teams, engine.roster_mask());
    for (const std::array<uint8_t, 2>& pair : plan.pairs) {
        Command invite;
        invite.type = CommandType::AllianceInvite;
        invite.issuer = pair[0];
        invite.other_player = pair[1];
        engine.apply_command(invite);
        Command accept;
        accept.type = CommandType::AllianceAccept;
        accept.issuer = pair[1];
        accept.other_player = pair[0];
        engine.apply_command(accept);
    }
}

}  // namespace ants::sim
