#pragma once

// The standard bot's answer to an invitation to team up (docs/BOTS.md, "Alliances"), as ONE pure function of what the bot's seat can see. The bot answers by it
// (StandardBot::accepts_invitation), and the application asks the same function to tell the player WHY a bot declined (one line of the chat log after the original's
// "... rejected teaming up"), so the reason that is told is the rule that decided: a text of its own would drift away from the rule.

#include <cstdint>
#include <string>
#include <string_view>

#include "ants_ai/bot_view.hpp"

namespace ants::ai {

enum class TeamUpAnswer : uint8_t {
    Accept = 0,
    TwoTeamsLeft,          // only two teams are live: the alliance would unite them all and the match would end at once (the last two live teams that ally win)
    InviterHasTeammate,    // the inviter is in a team already: accepting would break it
    BotHasTeammate,        // the bot is in a team already: a bot never breaks one
    InviterGone,           // the inviter is no team of the match, has dropped out, or is the bot itself
};

/// The answer of the bot of `view` to the invitation of team `from`. A team counts as live when it plays, has not dropped out and has an ant in sight (the bot's own ants count for itself;
/// the eggs of a team that has no ant cannot be seen, so such a team counts as gone, which only makes the bot more careful). The checks run in this order and the first that applies is the
/// reason: the bot's own team, the inviter, the number of live teams.
TeamUpAnswer team_up_answer(const BotView& view, uint8_t from) noexcept;

/// The line that the game adds to the chat log when a bot declined: why, in a few words. `bot_name` is the name of the bot's seat as the game shows it. Empty for Accept and for
/// InviterGone (nothing to explain: the player's own seat is the one that is gone).
std::string team_up_decline_text(TeamUpAnswer answer, std::string_view bot_name);

/// The worker bot (--bot N:worker, the tournaments' yardstick) declines every invitation, whatever the world looks like
inline constexpr const char* kWorkerNeverTeamsUpText = "This bot never teams up.";

}  // namespace ants::ai
