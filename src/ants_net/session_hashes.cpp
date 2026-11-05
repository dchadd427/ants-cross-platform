#include "ants_net/session.hpp"

namespace ants::net {

// What a dedicated server does with the state hashes that its clients report (session.hpp, "A seat whose state differs from the referee's"). The rule that says who differs, who agrees and
// when the answer is final is Sequencer::judge_hashes; this is what the host does with the answer. It is called after every pass, so whatever moves it (a report, an acknowledgement, a seat that
// leaves the table) is seen in the pass that brings it.
void HostSession::judge_hashes() {
    while (!frozen_) {                                   // (a frozen match is over, or stopped by a verdict already: nothing is left to protect)
        uint8_t seated = 0;                              // the people at the table: a seat that is held, a bot's and an empty one are not (they send no hashes)
        uint8_t held = 0;                                // the seats that are away (a lost link, a machine that is coming back): they may be back and say of the turns that follow
        for (uint8_t p = 0; p < sim::MAX_PLAYERS; ++p) {
            if (clients_[p].present && clients_[p].conn != nullptr) seated = static_cast<uint8_t>(seated | (1u << p));
            if (seat_held(p)) held = static_cast<uint8_t>(held | (1u << p));
        }
        const HashVerdict verdict = sequencer_.judge_hashes(seated, held);
        if (verdict.kind == HashVerdict::Kind::Nothing) return;
        if (verdict.kind == HashVerdict::Kind::Drop) {
            for (const DesyncMsg& d : verdict.seats) drop_diverged(d);
            continue;                                    // (the next turn that has an answer, if any)
        }
        for (const DesyncMsg& d : verdict.seats) {       // no seat agrees with the referee: it may be the one that is wrong. Nobody is dropped; every seat that differs is told, and the match stops
            desyncs_.push_back(d);
            broadcast(encode(d));
        }
        frozen_ = true;
    }
}

// The seat is out for good, as one that quits is: it is told alone (a Desync, as a machine that comes back with a different state is), then its connection is closed, its Quit goes into the next turn (on
// every machine alike: the team's ants die, its alliance ends, "<name> dropped out of the game!"; with one other side left the match ends and the seat's row is the last) and its key is told "dropped".
void HostSession::drop_diverged(const DesyncMsg& d) {
    if (d.player >= sim::MAX_PLAYERS || !clients_[d.player].present) return;
    Connection* const conn = clients_[d.player].conn;
    if (conn != nullptr && conn->is_open()) conn->send(encode(d));
    ++diverged_drops_;
    drop(d.player, true);
}

}  // namespace ants::net
