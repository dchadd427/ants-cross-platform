#pragma once

// The host's turn sequencer. The room owner does not simulate on behalf of anybody: it only decides which commands go into which turn. Clients hand
// it their commands (the issuer is stamped from the connection, never taken from the payload); every 50 ms it seals a turn, the commands of that
// turn in canonical order, and broadcasts it. Every peer, the host included, executes the turns in sequence, so they all apply the same commands at
// the same tick. The sequencer also compares the state hashes the peers report against the host's own, and stalls sealing while a peer lags too far
// behind (flow control), so nobody is left behind by more than a few seconds.
//
// The class is pure logic without clock, sockets or simulation; the session glue (session.hpp) drives it.

#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <set>
#include <vector>

#include "ants_net/protocol.hpp"

namespace ants::net {

/// What the referee of a dedicated server makes of the seats whose reported state differs from its own (Sequencer::judge_hashes)
struct HashVerdict {
    enum class Kind : uint8_t {
        Nothing,    ///< no report differs, or the seats that may still say which state is right have not
        Drop,       ///< the seats of `seats` differ and at least one seat agrees with the referee: those are out of the match, the others go on
        Fail        ///< every seat that could say differs: nobody vouches for the referee, which may be the one that is wrong; nobody is blamed and the match stops
    };
    Kind kind{Kind::Nothing};
    std::vector<DesyncMsg> seats;      ///< Drop, Fail: one for each seat that differs: the turn, the seat, the referee's hash (host) and the seat's (peer)
};

class Sequencer {
public:
    struct Config {
        uint32_t max_lag_turns = 3 * kTurnsPerSecond;   // sealing stalls while a peer's ack is this many turns behind (3 s); a host without a seat never waits (session.hpp)
        uint32_t max_commands_per_turn = 64;  // per peer and turn: what does not fit waits for the next turns (a flooding client cannot fill a turn)
        uint32_t max_carried_commands = 16 * 64;  // per peer: the commands that wait for a later turn (sixteen turns' worth at the default: a click on a big selection is up to 48 commands): a stuck uplink that is let go delivers
                                                  // all the orders of the player at once (15 a second for 5 s are 76), which are NOT a flood and must not cost the player its
                                                  // seat; beyond this many waiting commands a peer is refused
    };

    Sequencer() : Sequencer(Config{}) {}
    explicit Sequencer(Config config);

    /// A slot takes part in the match (its turns are awaited and its commands accepted) or not.
    void set_active(uint8_t player, bool active);
    bool is_active(uint8_t player) const noexcept { return player < sim::MAX_PLAYERS && active_[player]; }

    /// A command of `player` for the next turn. The issuer is set to `player` whatever the payload says. False when the player is not active, the command is not one that a
    /// client may send, or the peer already has max_carried_commands commands waiting beyond the max_commands_per_turn of this turn. A command that does not fit into the turn
    /// goes into one of the next turns (the first that has room for it, behind the older commands of the same player: the order in which a player sent its commands is kept).
    /// The sequencer alone decides which turn carries a command, so every machine runs the same turns.
    bool submit(uint8_t player, sim::Command command);
    /// A command that the sequencer itself creates: the drop-out of a peer that left or fell silent (`command.issuer` is that peer). It goes into
    /// the next turn whatever the state of the peer, so that every machine drops the team at the same tick. Clients cannot send these (submit
    /// refuses every type that is not is_client_command).
    void submit_system(sim::Command command);

    /// The highest turn `player` has executed.
    void on_ack(uint8_t player, uint32_t turn);

    /// A machine that takes over as host (host migration): the next turn it seals is `next_turn`, whatever was queued or reported before is forgotten,
    /// nobody is active any more (the survivors are activated again, one by one), and `set_acked` tells how far each has executed.
    void resume(uint32_t next_turn);
    void set_acked(uint8_t player, uint32_t next_to_execute);

    /// A state hash report of `player` after `turn`. A host with a seat: returns the desync found by comparing it with the host's report of the same turn (the host is
    /// `host_player`, reported through the same call), if any. A host without a seat (a dedicated server): nothing is returned, the report is kept for judge_hashes() against
    /// the referee's; only a hashed turn (kHashEveryTurns) is kept, and the first report of a seat for a turn is its report (a repeat adds nothing). Reports are kept until
    /// the turn is acknowledged by everybody.
    std::vector<DesyncMsg> on_hash(uint8_t player, uint32_t turn, const sim::StateHash& hash);
    void set_host_player(uint8_t player) noexcept { host_player_ = player; }
    /// A host without a seat (a dedicated server) plays no player but runs the match on its own engine: its report is the reference (judge_hashes() weighs the seats' against it)
    void on_referee_hash(uint32_t turn, const sim::StateHash& hash);
    /// A host without a seat: the referee's word on the seats of `seated` (a bit for each: the people who are at the table now) whose report of a turn differs from the referee's; `held` are the seats
    /// that are away and may come back (a lost link, in a room that holds seats). The decision depends on the reports and the acknowledgements alone, never on the clock or on which report came first.
    /// For the oldest turn that has such a seat and an answer:
    ///   - a seat of `seated` agrees with the referee: the seats that differ are to be dropped (Drop), the referee has a witness;
    ///   - no seat agrees and a seat that has not executed the turn yet may still report it: Nothing, the answer is awaited (a seat that is stuck is dropped by the lag policy);
    ///   - no seat agrees and none can, but a seat is away or came to the table after the turn was sealed (it was not there to run it, and says nothing of it; it can say of the turns that follow):
    ///     Nothing, the turn is let go and the next hashed turn decides;
    ///   - no seat agrees and none can: Fail. The referee may be the one that is wrong (a bug that only its machine has), so nobody is blamed.
    /// A turn is answered once; a seat that is not in `seated` (gone, held, a bot, never reported) is no witness and is not judged. Call it until it says Nothing.
    HashVerdict judge_hashes(uint8_t seated, uint8_t held = 0);

    /// True unless an active peer's ack is more than max_lag_turns behind the next turn.
    bool can_seal() const noexcept;
    /// The peer that holds the sequencer up (its ack is furthest behind), 255 when none does.
    uint8_t laggard() const noexcept;
    /// The sealed turns that `player` has not executed yet (as far as its acks tell: they travel); 0 for a slot that is not active
    uint32_t behind_turns(uint8_t player) const noexcept;

    /// Seals the next turn: the queued commands in canonical order (by issuer, each issuer's commands in submission order).
    TurnMsg seal();
    uint32_t next_turn() const noexcept { return next_turn_; }
    uint32_t acked(uint8_t player) const noexcept { return player < sim::MAX_PLAYERS ? acked_[player] : 0; }
    /// Number of commands waiting for the next turn
    size_t queued() const noexcept { return queue_.size(); }
    /// Number of commands of `player` that did not fit into the next turn and wait for a later one
    size_t carried(uint8_t player) const noexcept { return player < sim::MAX_PLAYERS ? carry_[player].size() : 0u; }
    /// Hash reports kept until every peer has passed their turn (diagnostics and the tests of the bound)
    size_t pending_reports() const noexcept { return reports_.size() + referee_.size(); }

private:
    Config cfg_;
    std::array<bool, sim::MAX_PLAYERS> active_{};
    std::array<uint32_t, sim::MAX_PLAYERS> acked_{};          // highest executed turn + 1 (0 = nothing executed yet)
    std::array<uint32_t, sim::MAX_PLAYERS> joined_{};         // the turn at which the slot last became active (0: at the start): it was not at the table for the turns before it
    std::array<uint32_t, sim::MAX_PLAYERS> queued_by_{};
    std::vector<sim::Command> queue_;
    std::array<std::deque<sim::Command>, sim::MAX_PLAYERS> carry_;   // per player: the commands beyond max_commands_per_turn, in the order they came
    uint32_t next_turn_{0};
    uint8_t host_player_{255};
    std::map<uint32_t, std::array<sim::StateHash, sim::MAX_PLAYERS>> reports_;
    std::map<uint32_t, std::array<bool, sim::MAX_PLAYERS>> have_report_;
    std::map<uint32_t, sim::StateHash> referee_;               // the seatless host's own reports, by turn
    std::set<uint32_t> suspect_;                               // the seatless host's turns on which a report differs from the referee's and judge_hashes() has not answered yet
};

}  // namespace ants::net
