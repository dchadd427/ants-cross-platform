#include "ants_ai/team_up.hpp"

#include <array>

namespace ants::ai {

TeamUpAnswer team_up_answer(const BotView& view, uint8_t from) noexcept {
    if (from >= sim::MAX_PLAYERS || from == view.seat()) return TeamUpAnswer::InviterGone;
    if (view.ally() < sim::MAX_PLAYERS) return TeamUpAnswer::BotHasTeammate;                 // never break an alliance
    const TeamRow& inviter = view.rows()[from];
    if (!inviter.present || inviter.dropped) return TeamUpAnswer::InviterGone;
    if (inviter.ally < sim::MAX_PLAYERS) return TeamUpAnswer::InviterHasTeammate;
    std::array<bool, sim::MAX_PLAYERS> has_ant{};
    has_ant[view.seat()] = !view.mine().empty();
    for (const AntView& a : view.others()) has_ant[a.team] = true;
    size_t live = 0;
    for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) {
        const TeamRow& row = view.rows()[t];
        if (row.present && !row.dropped && has_ant[t]) ++live;
    }
    return live >= 3 ? TeamUpAnswer::Accept : TeamUpAnswer::TwoTeamsLeft;                  // with only two live teams the alliance would unite all of them: the match would end at once
}

std::string team_up_decline_text(TeamUpAnswer answer, std::string_view bot_name) {
    switch (answer) {
        case TeamUpAnswer::TwoTeamsLeft:
            return "Bots team up only while three or more teams play.";
        case TeamUpAnswer::InviterHasTeammate:
            return "You already have a teammate.";
        case TeamUpAnswer::BotHasTeammate:
            return (bot_name.empty() ? std::string("This bot") : std::string(bot_name.substr(0, 32))) + " already has a teammate.";     // (a name is cut at 32: the chat log's body holds 100 characters)
        case TeamUpAnswer::Accept:
        case TeamUpAnswer::InviterGone:
            break;
    }
    return std::string();
}

}  // namespace ants::ai
