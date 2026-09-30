#pragma once

// The host's turn sequencer. The room owner does not simulate on behalf of anybody: it only decides which commands go into which turn. Clients hand
// it their commands (the issuer is stamped from the connection, never taken from the payload); every 100 ms it seals a turn, the commands of that
// turn in canonical order, and broadcasts it. Every peer, the host included, executes the turns in sequence, so they all apply the same commands at
// the same tick. The sequencer also compares the state hashes the peers report against the host's own, and stalls sealing while a peer lags too far
// behind (flow control), so nobody is left behind by more than a few seconds.
//
// The class is pure logic without clock, sockets or simulation; the session glue (session.hpp) drives it.

#include <array>
#include <cstdint>
#include <map>
#include <vector>

#include "ants_net/protocol.hpp"

namespace ants::net {

class Sequencer {
public:
    struct Config {
        uint32_t max_lag_turns = 30;          // sealing stalls while a peer's ack is this many turns behind (3 s)
        uint32_t max_commands_per_turn = 64;  // per peer and turn: more are refused (a flooding client cannot fill a turn)
    };

    Sequencer() : Sequencer(Config{}) {}
    explicit Sequencer(Config config);

    /// A slot takes part in the match (its turns are awaited and its commands accepted) or not.
    void set_active(uint8_t player, bool active);
    bool is_active(uint8_t player) const noexcept { return player < sim::MAX_PLAYERS && active_[player]; }

    /// A command of `player` for the next turn. The issuer is set to `player` whatever the payload says. False when the player is not active, the
    /// command is not one that a client may send, or the peer already sent max_commands_per_turn commands for this turn.
    bool submit(uint8_t player, sim::Command command);

    /// The highest turn `player` has executed.
    void on_ack(uint8_t player, uint32_t turn);

    /// A state hash report of `player` after `turn`. Returns the desync found by comparing it with the host's report of the same turn (the host is
    /// `host_player`, reported through the same call), if any. Reports are kept until the turn is acknowledged by everybody.
    std::vector<DesyncMsg> on_hash(uint8_t player, uint32_t turn, const sim::StateHash& hash);
    void set_host_player(uint8_t player) noexcept { host_player_ = player; }

    /// True unless an active peer's ack is more than max_lag_turns behind the next turn.
    bool can_seal() const noexcept;
    /// The peer that holds the sequencer up (its ack is furthest behind), 255 when none does.
    uint8_t laggard() const noexcept;

    /// Seals the next turn: the queued commands in canonical order (by issuer, each issuer's commands in submission order).
    TurnMsg seal();
    uint32_t next_turn() const noexcept { return next_turn_; }
    uint32_t acked(uint8_t player) const noexcept { return player < sim::MAX_PLAYERS ? acked_[player] : 0; }
    /// Number of commands waiting for the next turn
    size_t queued() const noexcept { return queue_.size(); }

private:
    Config cfg_;
    std::array<bool, sim::MAX_PLAYERS> active_{};
    std::array<uint32_t, sim::MAX_PLAYERS> acked_{};          // highest executed turn + 1 (0 = nothing executed yet)
    std::array<uint32_t, sim::MAX_PLAYERS> queued_by_{};
    std::vector<sim::Command> queue_;
    uint32_t next_turn_{0};
    uint8_t host_player_{255};
    std::map<uint32_t, std::array<sim::StateHash, sim::MAX_PLAYERS>> reports_;
    std::map<uint32_t, std::array<bool, sim::MAX_PLAYERS>> have_report_;
};

}  // namespace ants::net
