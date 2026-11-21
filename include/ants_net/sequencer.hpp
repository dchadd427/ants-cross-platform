#pragma once

// The host's turn sequencer. The room owner does not simulate on behalf of anybody: it only decides which commands go into which turn. Clients hand
// it their commands (the issuer is stamped from the connection, never taken from the payload); every 50 ms it seals a turn, the commands of that
// turn in canonical order, and broadcasts it. Every peer, the host included, executes the turns in sequence, so they all apply the same commands at
// the same tick. The sequencer also compares the state hashes the peers report against the host's own, and stalls sealing while a peer lags too far
// behind (flow control), so nobody is left behind by more than a few seconds.
//
// What a seat may have sealed is limited here and nowhere else (docs/NETWORK_PORT.md, "The order budget"): a turn takes 64 commands of a seat, a seat has a budget of orders (a store that fills at a
// steady pace: the clock is the sealed turns, 20 a second), and an order that names no ant that the seat could order is not sealed. What is over is dropped, counted and marked, never made a
// violation and never put into a turn, so every machine runs the same turns.
//
// The class is pure logic without clock, sockets or simulation; the session glue (session.hpp) drives it.

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <set>
#include <vector>

#include "ants_net/protocol.hpp"

namespace ants::net {

/// What the referee of a dedicated server makes of the seats whose reported state differs from its own (Sequencer::judge_hashes)
struct HashVerdict {
    enum class Kind : uint8_t {
        Nothing,    ///< no report differs, or the seats that may still say which state is right have not, or the turn is let go (nobody who was at the table for it can say)
        Drop,       ///< the seats of `seats` differ and at least one seat agrees with the referee: those are out of the match, the others go on
        Fail        ///< every seat that could say differs: nobody vouches for the referee, which may be the one that is wrong; nobody is blamed and the match stops
    };
    Kind kind{Kind::Nothing};
    std::vector<DesyncMsg> seats;      ///< Drop, Fail: one for each seat that differs: the turn, the seat, the referee's hash (host) and the seat's (peer)
    uint8_t agree{0};                  ///< Drop: the seats (a bit for each) that report the referee's state for that turn
};

/// What became of a command that a seat handed in (Sequencer::submit_checked)
enum class Submit : uint8_t {
    Queued,        ///< it is in the next turn or waits for a later one
    OverBudget,    ///< the seat has used up its budget (Config): dropped, counted, the seat is marked (Sequencer::heavy_seats); a seat with a budget never has a command refused for the commands that wait (a flood is dropped, not an offence)
    NoAnts,        ///< the vetter says that the order names no ant that the seat can order: dropped and counted
    Refused        ///< not a command that this seat may send (the seat is not at the table, a command that only the sequencer makes) or the seat has too many waiting: a client that sends one is at fault
};

class Sequencer {
public:
    struct Config {
        uint32_t max_lag_turns = 3 * kTurnsPerSecond;   // sealing stalls while a peer's ack is this many turns behind (3 s); a host without a seat never waits (session.hpp)
        uint32_t max_commands_per_turn = 64;  // per peer and turn: what does not fit waits for the next turns (a flooding client cannot fill a turn)
        uint32_t max_carried_commands = 16 * 64;  // per peer: the commands that wait for a later turn (sixteen turns' worth at the default: a click on a big selection is up to 48 commands): a stuck uplink that is let go delivers
                                                  // all the orders of the player at once (15 a second for 5 s are 76), which are NOT a flood and must not cost the player its
                                                  // seat; beyond this many waiting commands a peer is refused
        // The budgets of a seat. Each is a store that holds `*_burst` commands and earns back `*_per_second` of them every second of sealed turns (kTurnsPerSecond turns); a command that is sealed takes one
        // from the store of its kind, and a command that finds it empty is dropped (a test that wants no budget gives 0 in either number of a store).
        // Orders, the commands that carry an ant list (group orders and Stop: the only ones that can be padded, 8 + 4 n bytes each, and the ones that a click on a big selection sends as one command for
        // every 32 ants): 6,144 and 12 a second. An honest player does not meet it: the busiest second of the recordings measured is 6 commands, the busiest minute 110; the biggest play that a person can
        // make is the HUD's click, ceil(ants / 32) commands (docs/CONTROLS.md), so an army of 200 ants clicked 4 times a second for five minutes is 8,400 commands and a fight of 100 ants clicked 12 times a
        // second is 48 a second: the store holds 6,144 and earns 12 a second (9,744 commands in five minutes, 6,864 in a minute). And what one seat can make of it is bounded in time: 6,144 + 12 x 7,200 are
        // 92,544 commands in the longest match (2 hours), at most 137 bytes each in the turn log: 12 MiB, three quarters of its 16 MiB.
        uint32_t order_burst = 6144;
        uint32_t orders_per_second = 12;
        // Control commands, the others (Hatch, the alliance commands, a second Quit): a store of their own, 512 and 8 a second, so that a seat that has used up its orders can still say them and a seat
        // that says them as fast as it can cannot fill the log (9 bytes each, 64 a turn were 11 KB a second). The first Quit of a seat is never charged: a seat that is over a budget can always leave.
        uint32_t control_burst = 512;
        uint32_t controls_per_second = 8;
    };

    Sequencer() : Sequencer(Config{}) {}
    explicit Sequencer(Config config);

    /// A slot takes part in the match (its turns are awaited and its commands accepted) or not.
    void set_active(uint8_t player, bool active);
    bool is_active(uint8_t player) const noexcept { return player < sim::MAX_PLAYERS && active_[player]; }

    /// The question that decides whether an order is sealed at all: asked for every command that a seat hands in, with the issuer set; it may take ants out of the command (and must say false when none is left).
    /// The session gives the host's engine's answer (HostSession: ants that another player owns, ants that cannot exist yet and repeats are taken out: what the engine would not carry out anyway).
    /// It must depend only on the state of the host: what is sealed is what every machine runs.
    using Vetter = std::function<bool(uint8_t player, sim::Command& command)>;
    void set_vetter(Vetter vetter) { vetter_ = std::move(vetter); }

    /// A command of `player` for the next turn. The issuer is set to `player` whatever the payload says. Refused when the player is not active, the command is not one that a
    /// client may send, or the peer already has max_carried_commands commands waiting beyond the max_commands_per_turn of this turn. NoAnts when the vetter turns it down, OverBudget when the
    /// seat has none left in the budget of its kind of command (Config::order_burst for the commands that carry ants, control_burst for the rest; a seat's first Quit has none to take). A command that does not fit into the turn
    /// goes into one of the next turns (the first that has room for it, behind the older commands of the same player: the order in which a player sent its commands is kept).
    /// The sequencer alone decides which turn carries a command, so every machine runs the same turns.
    Submit submit_checked(uint8_t player, sim::Command command);
    /// submit_checked() as a yes or no: true when the command was queued
    bool submit(uint8_t player, sim::Command command) { return submit_checked(player, std::move(command)) == Submit::Queued; }
    /// Commands of `player` that were dropped because a budget was used up, and commands that were dropped because the vetter found no ant in them (for the whole match: they are never reset); the seats that
    /// have gone over their budget at least once (a bit for each seat): the "heavy order rate" mark of the status, the replay's head and the history
    uint32_t dropped_over_budget(uint8_t player) const noexcept { return player < sim::MAX_PLAYERS ? over_budget_[player] : 0u; }
    uint32_t dropped_no_ants(uint8_t player) const noexcept { return player < sim::MAX_PLAYERS ? no_ants_[player] : 0u; }
    uint8_t heavy_seats() const noexcept { return heavy_; }
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
    /// the turn is acknowledged by everybody (a report of a turn that everybody has acknowledged is not kept).
    std::vector<DesyncMsg> on_hash(uint8_t player, uint32_t turn, const sim::StateHash& hash);
    void set_host_player(uint8_t player) noexcept { host_player_ = player; }
    /// A host without a seat (a dedicated server) plays no player but runs the match on its own engine: its report is the reference (judge_hashes() weighs the seats' against it)
    void on_referee_hash(uint32_t turn, const sim::StateHash& hash);
    /// A host without a seat: the referee's word on the seats of `seated` (a bit for each: the people who are at the table now) whose report of a turn differs from the referee's; `held` are the seats
    /// that are away and may come back (a lost link, in a room that holds seats). The decision depends on the reports and the acknowledgements alone, never on the clock. A turn is judged when the referee's
    /// report and a seat's are both in before every seat has passed it (the reports of a turn that all have passed are forgotten): a report of either kind that comes later is not kept and judges nothing,
    /// the next hashed turn does. For the oldest turn that has such a seat and an answer:
    ///   - a seat of `seated` agrees with the referee: the seats that differ are to be dropped (Drop), the referee has a witness;
    ///   - no seat agrees and a seat that has not executed the turn yet may still report it: Nothing, the answer is awaited (a seat that is stuck is dropped by the lag policy);
    ///   - no seat agrees and none can, but a seat is away or came to the table after the turn was sealed (it was not there to run it, and says nothing of it; it can say of the turns that follow):
    ///     Nothing, the turn is let go and the next hashed turn decides;
    ///   - no seat agrees and none can: Fail. The referee may be the one that is wrong (a bug that only its machine has), so nobody is blamed.
    /// A report is answered once (a seat that reports a turn after it was answered gets its own verdict); a seat that is not in `seated` (gone, held, a bot, never reported) is no witness and is not judged.
    /// Call it until it says Nothing.
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
    Vetter vetter_;
    std::array<uint32_t, sim::MAX_PLAYERS> budget_milli_{};     // the orders that each seat has left, in thousandths: full at the start, one turn's earnings added at every seal
    std::array<uint32_t, sim::MAX_PLAYERS> control_milli_{};    // ... and the control commands
    std::array<bool, sim::MAX_PLAYERS> quit_sent_{};            // the seat's first Quit has been given (it was never charged; a later one is a control command)
    std::array<uint32_t, sim::MAX_PLAYERS> over_budget_{};
    std::array<uint32_t, sim::MAX_PLAYERS> no_ants_{};
    uint8_t heavy_{0};
    uint32_t next_turn_{0};
    uint8_t host_player_{255};
    std::map<uint32_t, std::array<sim::StateHash, sim::MAX_PLAYERS>> reports_;
    std::map<uint32_t, std::array<bool, sim::MAX_PLAYERS>> have_report_;
    std::map<uint32_t, sim::StateHash> referee_;               // the seatless host's own reports, by turn
    uint32_t passed_{0};                                       // the lowest acknowledgement of the seats at the table at the last acknowledgement (it never goes down): the reports of the turns before passed_ - 1 are forgotten, and a report of one is not kept
    std::set<uint32_t> suspect_;                               // the seatless host's turns on which a report differs from the referee's and judge_hashes() has not answered yet
};

}  // namespace ants::net
