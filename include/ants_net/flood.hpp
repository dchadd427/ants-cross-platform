#pragma once

// Flood control on the host's side of a connection (docs/NETWORK_PORT.md, "Flood control"). A peer that is not a game can send valid messages as fast as its line allows (a
// Ping, a TurnAck, a Hash, a Chat line, a StartRequest that is ignored: none of them is ever an offence by itself). Every connection of a host therefore has a MessageBudget
// (a token bucket): a message that finds it empty is not handled, and it is an offence (a violation: eight throw the sender out). The StartRequests that are ignored have a
// count of their own, because they are the messages that a person sends twice by accident.

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

}  // namespace ants::net
