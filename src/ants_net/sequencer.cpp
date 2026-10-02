#include "ants_net/sequencer.hpp"

#include <algorithm>

namespace ants::net {

namespace {
constexpr uint32_t kRefereeWindowTurns = turns_for_ms(6400);       // the referee's report of a turn is taken while the turn is at most 6.4 s old
}  // namespace

Sequencer::Sequencer(Config config) : cfg_(config) {}

void Sequencer::set_active(uint8_t player, bool active) {
    if (player >= sim::MAX_PLAYERS) return;
    active_[player] = active;
    // a player that joins is level with the sequencer: it has "executed" everything before the current turn
    if (active) acked_[player] = next_turn_;
}

bool Sequencer::submit(uint8_t player, sim::Command command) {
    if (player >= sim::MAX_PLAYERS || !active_[player]) return false;
    if (!sim::is_client_command(command.type)) return false;     // Drop and unknown types are not for clients
    if (queued_by_[player] >= cfg_.max_commands_per_turn) return false;
    command.issuer = player;                       // the connection decides who speaks, not the payload
    ++queued_by_[player];
    queue_.push_back(std::move(command));
    return true;
}

void Sequencer::submit_system(sim::Command command) {
    if (command.type == sim::CommandType::None || command.type > sim::CommandType::Last || command.issuer >= sim::MAX_PLAYERS) return;
    queue_.push_back(std::move(command));
}

void Sequencer::resume(uint32_t next_turn) {
    next_turn_ = next_turn;
    queue_.clear();
    queued_by_.fill(0);
    reports_.clear();
    have_report_.clear();
    referee_.clear();
    acked_.fill(0);
    active_.fill(false);                             // the new host activates the peers that follow it, one by one
}

void Sequencer::set_acked(uint8_t player, uint32_t next_to_execute) {
    if (player < sim::MAX_PLAYERS) acked_[player] = std::min(next_to_execute, next_turn_);
}

void Sequencer::on_ack(uint8_t player, uint32_t turn) {
    if (player >= sim::MAX_PLAYERS) return;
    // "executed turn t" means that the next turn the peer needs is t + 1; acks only move forward and never past what was sealed
    const uint32_t next_needed = std::min(turn + 1, next_turn_);
    if (next_needed > acked_[player]) acked_[player] = next_needed;
    // reports of turns everybody has passed are no longer needed
    uint32_t low = next_turn_;
    for (uint8_t p = 0; p < sim::MAX_PLAYERS; ++p) {
        if (active_[p]) low = std::min(low, acked_[p]);
    }
    for (auto it = reports_.begin(); it != reports_.end();) {
        if (it->first + 1 < low) {
            have_report_.erase(it->first);
            it = reports_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = referee_.begin(); it != referee_.end();) {            // the referee's reports of turns that everybody has passed
        if (it->first + 1 < low) it = referee_.erase(it);
        else ++it;
    }
}

std::vector<DesyncMsg> Sequencer::on_referee_hash(uint32_t turn, const sim::StateHash& hash) {
    std::vector<DesyncMsg> out;
    if (host_player_ < sim::MAX_PLAYERS) return out;                      // a host with a seat reports through on_hash like everybody else
    if (turn >= next_turn_) return out;                                   // the referee only executed turns that were sealed
    if (turn + kRefereeWindowTurns < next_turn_) return out;              // (a turn that is long past: nothing is waiting for it)
    referee_[turn] = hash;
    const auto found = reports_.find(turn);
    if (found == reports_.end()) return out;
    const auto have = have_report_.find(turn);
    for (uint8_t p = 0; p < sim::MAX_PLAYERS; ++p) {
        if (have == have_report_.end() || !have->second[p]) continue;
        if (found->second[p] != hash) {
            DesyncMsg d;
            d.turn = turn;
            d.player = p;
            d.host = hash;
            d.peer = found->second[p];
            out.push_back(d);
        }
    }
    return out;
}

std::vector<DesyncMsg> Sequencer::on_hash(uint8_t player, uint32_t turn, const sim::StateHash& hash) {
    std::vector<DesyncMsg> out;
    if (player >= sim::MAX_PLAYERS || !active_[player]) return out;
    if (turn >= next_turn_) return out;                                   // only a turn that was sealed can be reported: a hostile client cannot make the maps grow with turns of the future
    if (host_player_ >= sim::MAX_PLAYERS) {                               // a host without a seat: the referee's report is the reference
        reports_[turn][player] = hash;
        have_report_[turn][player] = true;
        const auto ref = referee_.find(turn);
        if (ref != referee_.end() && ref->second != hash) {
            DesyncMsg d;
            d.turn = turn;
            d.player = player;
            d.host = ref->second;
            d.peer = hash;
            out.push_back(d);
        }
        return out;
    }
    auto& reports = reports_[turn];
    auto& have = have_report_[turn];
    reports[player] = hash;
    have[player] = true;
    if (!have[host_player_]) return out;             // compared as soon as the host's own report of that turn is in
    const sim::StateHash& host = reports[host_player_];
    for (uint8_t p = 0; p < sim::MAX_PLAYERS; ++p) {
        if (p == host_player_ || !have[p]) continue;
        if (reports[p] != host) {
            DesyncMsg d;
            d.turn = turn;
            d.player = p;
            d.host = host;
            d.peer = reports[p];
            out.push_back(d);
        }
    }
    return out;
}

bool Sequencer::can_seal() const noexcept { return laggard() == 255; }

uint32_t Sequencer::behind_turns(uint8_t player) const noexcept {
    if (player >= sim::MAX_PLAYERS || !active_[player]) return 0;
    return next_turn_ - std::min(acked_[player], next_turn_);
}

uint8_t Sequencer::laggard() const noexcept {
    uint8_t worst = 255;
    uint32_t worst_lag = 0;
    for (uint8_t p = 0; p < sim::MAX_PLAYERS; ++p) {
        if (!active_[p]) continue;
        const uint32_t lag = next_turn_ - std::min(acked_[p], next_turn_);   // sealed turns the peer has not executed yet
        if (lag > cfg_.max_lag_turns && lag > worst_lag) {
            worst = p;
            worst_lag = lag;
        }
    }
    return worst;
}

TurnMsg Sequencer::seal() {
    TurnMsg t;
    t.turn = next_turn_++;
    sim::canonical_order(queue_);
    t.commands = std::move(queue_);
    queue_.clear();
    queued_by_.fill(0);
    return t;
}

}  // namespace ants::net
