#include "ants_net/latency.hpp"

#include <utility>

namespace ants::net {

// ------------------------------------------------------------------------------------------------
// PingMeter
// ------------------------------------------------------------------------------------------------

PingMsg PingMeter::next(uint32_t now_ms) noexcept {
    if (++nonce_ == 0) ++nonce_;                                   // never 0 (after 2^32 pings)
    InFlight& slot = in_flight_[nonce_ % kInFlight];
    slot.sent_ms = now_ms;
    slot.open = true;
    PingMsg ping;
    ping.nonce = nonce_;
    ping.sent_ms = now_ms;
    return ping;
}

bool PingMeter::on_pong(const PingMsg& pong, uint32_t now_ms, uint32_t age_ms) noexcept {
    // one of the last eight pings (`nonce_ - pong.nonce` wraps to a huge number for a nonce that was never sent, so this also refuses the future)
    if (pong.nonce == 0 || nonce_ - pong.nonce >= kInFlight) return false;
    InFlight& slot = in_flight_[pong.nonce % kInFlight];
    if (!slot.open || slot.sent_ms != pong.sent_ms) return false;
    const uint32_t elapsed = now_ms - pong.sent_ms;                // from the frame that sent the ping to the frame that read the answer
    if (static_cast<int32_t>(elapsed) < 0) return false;           // an answer that arrives before its question went out
    slot.open = false;
    if (age_ms > elapsed) return false;                            // it came before the frame clock says the ping went out: the clock was cut (see the declaration), nothing is measured
    answers_.add(elapsed - age_ms);                                // the round trip: what the answer waited in the queue of a page that is drawn less often than the link answers is not the link's
    last_answer_ms_ = now_ms - age_ms;
    return true;
}

// ------------------------------------------------------------------------------------------------
// CommandDelayMeter
// ------------------------------------------------------------------------------------------------

void CommandDelayMeter::on_sent(const sim::Command& command) {
    if (own_seat_ >= sim::MAX_PLAYERS) return;
    Pending p;
    p.command = command;
    p.command.issuer = own_seat_;                                  // the host stamps the issuer of what it seals: this is what the turn will say
    pending_.push_back(std::move(p));
    if (pending_.size() > kMaxPending) pending_.pop_front();
}

void CommandDelayMeter::on_frame(uint32_t now_ms) {
    for (Pending& p : pending_) {
        if (p.stamped) continue;
        p.sent_ms = now_ms;
        p.stamped = true;
    }
    while (!pending_.empty() && now_ms - pending_.front().sent_ms > kForgetAfterMs) pending_.pop_front();
}

bool CommandDelayMeter::on_applied(const sim::Command& command, uint32_t now_ms) {
    if (own_seat_ >= sim::MAX_PLAYERS || command.issuer != own_seat_) return false;
    for (auto it = pending_.begin(); it != pending_.end(); ++it) {
        if (!it->stamped || it->command != command) continue;
        delays_.add(now_ms - it->sent_ms);
        last_applied_ms_ = now_ms;
        pending_.erase(pending_.begin(), it + 1);                  // the host keeps the order of one player's commands: whatever was sent before this one and is still here was lost
        return true;
    }
    return false;
}

}  // namespace ants::net
