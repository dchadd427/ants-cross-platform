#pragma once

// Flood control on the host's side of a connection (docs/NETWORK_PORT.md, "Flood control"). A peer that is not a game can send valid messages as fast as its line allows (a
// Ping, a TurnAck, a Hash, a Chat line, a StartRequest that is ignored: none of them is ever an offence by itself). Every connection of a host therefore has a MessageBudget
// (a token bucket): a message that finds it empty is not handled, and it is an offence (a violation: eight throw the sender out). The StartRequests that are ignored have a
// count of their own, because they are the messages that a person sends twice by accident. A chat line has a budget of its own as well (ChatBudget): the general budget lets a connection
// say a thousand lines a second, which is no talk, and every one of them would be relayed to everybody in the room.

#include <algorithm>
#include <cstdint>

namespace ants::net {

/// What a real client says to a host in a second (turns of 50 ms, since protocol 8): an acknowledgement for every frame that ran a turn (20 a second, and never more than one per turn
/// that runs: 80 while it catches up at 4x), a state hash for every 20th turn (1; 4 while it catches up), a ping, its commands (a person gives a few; the sequencer takes 64 a turn from one
/// player, which is 1280 a second: a client at that rate is no person and meets the budget first) and now and then a line of chat. Measured with the real sessions over a simulated
/// link (test_lockstep N2.39), the busiest second of a client held 27 messages in steady play, 48 for a person who gives 20 orders a second, 89 for a window that catches up at 4x after a
/// freeze of 10 s; and an uplink that was stuck for 25 s (the longest that a server's room tolerates: a player with no acknowledgement for 30 s is dropped) delivers 637 at once. The budget
/// is 1000 messages a second and a burst of as many (a connection that was quiet may say a second's worth at once): some 10 to 40 times above what an honest client says in a second, and
/// the burst holds the backlog of a stuck uplink.
inline constexpr uint32_t kMessagesPerSecond = 1000;
inline constexpr uint32_t kMessageBurst = 1000;

/// A StartRequest that cannot be honoured (the sender does not lead, the room cannot start, the match is loading or running already) is no offence: a leader clicks START twice, a
/// player of a slow link three times. Up to this many of them from one connection cost nothing; each one after them is a violation, like any message that a guest may not send.
inline constexpr uint32_t kIgnoredStartRequestsAllowed = 16;

/// The colour moves of a room's leader (protocol 14, SeatMove). One that cannot be done (the sender does not lead, the room is loading or running, the player has left, somebody holds the colour) is no
/// offence at first, as for a StartRequest: up to kIgnoredSeatMovesAllowed of them per connection cost nothing, each one after those is a violation. One that can be done is shown to the whole room (the Room
/// message, a notice to each player that was moved), so a person's presses have a budget like a chat's (ChatBudget): a burst of kSeatMoveBurst, then kSeatMovesPerSecond a second (a finger presses a
/// row twice a second, and the screen waits for the room's answer before it presses again); a move beyond it is dropped, kSeatMoveExcessBurst more are tolerated, and every one after that is a violation.
inline constexpr uint32_t kIgnoredSeatMovesAllowed = 16;
inline constexpr uint32_t kSeatMoveBurst = 6;
inline constexpr uint32_t kSeatMovesPerSecond = 4;
inline constexpr uint32_t kSeatMoveExcessBurst = 12;

/// The lines of chat that one connection may say (a remake protection: the original has no limit, but nobody types more than a line a second for long). A line is relayed to everybody, so
/// a flood of them is a flood for the whole room: a burst of kChatBurst lines, then kChatPerSecond a second. A line beyond that is DROPPED (not relayed, not logged, no answer): a person
/// who pastes six lines loses the sixth. It costs nothing at first, but a connection that goes on saying more than the budget allows is flooding: kChatExcessBurst lines beyond it are tolerated and that allowance
/// is refilled at the budget's own rate, so a talker above TWO lines a second (twice the budget) uses it up (four a second: in ten seconds) and every line after that is a violation, like any
/// message that a client may not send, and eight throw the sender out; two lines a second, half of them dropped, is tolerated for ever. Used by the waiting room
/// (HostLobby) and by the match (HostSession); the host's own lines (its own player's) are not limited: it is not a connection.
inline constexpr uint32_t kChatBurst = 5;
inline constexpr uint32_t kChatPerSecond = 1;
inline constexpr uint32_t kChatExcessBurst = 20;

/// A token bucket for the messages of one connection. The clock is the host's millisecond clock, 32 bits wide, which wraps: only differences of it are used. The bucket is
/// full when the first message comes.
class MessageBudget {
public:
    /// Takes one message from the budget at `now_ms`. False when the connection has used it up: the message is not to be handled and counts as a violation.
    bool take(uint32_t now_ms, uint32_t burst = kMessageBurst, uint32_t per_second = kMessagesPerSecond) noexcept {
        const uint64_t capacity = uint64_t{burst} * 1000u;                // (tokens are counted in thousandths, so that a refill of a millisecond is exact)
        if (!primed_) {
            primed_ = true;
            thousandths_ = capacity;
        } else {
            thousandths_ = std::min<uint64_t>(capacity, thousandths_ + uint64_t{now_ms - last_ms_} * per_second);
        }
        last_ms_ = now_ms;
        if (thousandths_ < 1000u) return false;
        thousandths_ -= 1000u;
        return true;
    }

private:
    uint64_t thousandths_{0};
    uint32_t last_ms_{0};
    bool primed_{false};
};

/// The budget of the chat lines of one connection (see kChatBurst): Relay when the line is within it, Drop when it is beyond it (and the excess is still tolerated), Offence when the
/// connection has gone on beyond it for long: the line is dropped and counts as a violation.
class ChatBudget {
public:
    enum class Verdict : uint8_t { Relay, Drop, Offence };
    Verdict take(uint32_t now_ms, uint32_t burst = kChatBurst, uint32_t per_second = kChatPerSecond, uint32_t excess_burst = kChatExcessBurst) noexcept {
        if (lines_.take(now_ms, burst, per_second)) return Verdict::Relay;
        return excess_.take(now_ms, excess_burst, per_second) ? Verdict::Drop : Verdict::Offence;
    }

private:
    MessageBudget lines_;
    MessageBudget excess_;
};

}  // namespace ants::net
