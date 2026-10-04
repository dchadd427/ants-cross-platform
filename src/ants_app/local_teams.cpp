#include "ants_app/local_teams.hpp"

namespace ants::app {

namespace {

bool is_seat_digit(char c) noexcept { return c >= '0' && c <= '3'; }

}  // namespace

bool parse_local_teams(std::string_view text, LocalTeams& out, std::string& why) {
    if (text.size() == 3 && (text[0] == 'f' || text[0] == 'F') && (text[1] == 'f' || text[1] == 'F') && (text[2] == 'a' || text[2] == 'A')) {
        out = LocalTeams{};
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

std::string local_teams_text(const LocalTeams& teams) {
    if (!teams.set) return "ffa";
    return std::string(1, static_cast<char>('0' + teams.a)) + "+" + std::string(1, static_cast<char>('0' + teams.b));
}

LocalTeamsPlan plan_local_teams(const LocalTeams& teams, uint8_t roster) {
    LocalTeamsPlan plan;
    if (!teams.set) return plan;
    const auto plays = [roster](uint8_t seat) { return seat < 4 && ((roster >> seat) & 1u) != 0; };
    for (const uint8_t seat : {teams.a, teams.b}) {
        if (!plays(seat)) {
            plan.why = "seat " + std::to_string(static_cast<unsigned>(seat)) + " does not play in this match.";
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
        return plan;
    }
    plan.pairs.push_back({teams.a, teams.b});
    if (count == 2) plan.pairs.push_back(others);                                   // (one other seat plays alone)
    return plan;
}

std::vector<LocalTeams> local_team_choices(uint8_t own_seat, uint8_t filled_mask) {
    std::vector<LocalTeams> choices{LocalTeams{}};
    size_t filled = 0;
    for (uint8_t seat = 0; seat < 4; ++seat) {
        if (seat != own_seat && ((filled_mask >> seat) & 1u) != 0) ++filled;
    }
    if (own_seat >= 4 || filled < 2) return choices;
    for (uint8_t seat = 0; seat < 4; ++seat) {
        if (seat != own_seat && ((filled_mask >> seat) & 1u) != 0) choices.push_back(LocalTeams{true, own_seat, seat});
    }
    return choices;
}

}  // namespace ants::app
