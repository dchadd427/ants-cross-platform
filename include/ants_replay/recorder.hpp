#pragma once

// The recorder: what a machine keeps of a match while it runs, written as a replay file (replay.hpp) when the match ends. It only watches: it is told every command that the engine is given
// and every tick that the engine runs, and it reads the engine's state hash now and then; it never changes the engine, so a recorded match plays out exactly as an unrecorded one.
//
// Two ways to feed it, one for each place that every game passes:
//  * a lock-step match (a network game, the room's referee): LockstepRunner::set_on_executed calls on_turn for every turn that the runner runs, the live ones and the catch-up's, so a machine
//    that comes back to its match records it from its first turn;
//  * a game on one machine: the places that give the engine a command (the HUD's sink, the computer players' sink, the quit) call on_command, and the place that runs a tick calls on_tick.
//    A command given before the first tick is turn 0, one given after n ticks is turn n: the same numbering as the runner's.
//
// A recording that cannot be a replay (a turn that never arrived, more than kMaxFileBytes) is dropped: finish() returns nothing and says why. A command that the engine turns down without
// changing anything (no known type, an ant order with no ant or with too many) is left out and counted, and does not spoil the recording. A command that the engine would apply but that the
// wire form cannot hold (one without an ant list that names ants) is not left out: it spoils the recording, because a file without it would not play the match that was played.

#include <cstdint>
#include <string>
#include <vector>

#include "ants_net/protocol.hpp"
#include "ants_replay/replay.hpp"
#include "ants_sim/sim_engine.hpp"

namespace ants::replay {

class Recorder {
public:
    explicit Recorder(Header head);

    /// A command that the engine is given now, before the tick of turn `turns()`. In a lock-step match on_turn does this; a game on one machine calls it itself.
    void on_command(const sim::Command& command);
    /// A tick has run (a game on one machine): the turn is over. The engine is read for its state hash every `hash_period` turns.
    void on_tick(const sim::SimulationEngine& engine);
    /// A turn of the lock-step runner has run: its commands, then its tick. A turn that is not the next one (the recording missed one) spoils the recording.
    void on_turn(const net::TurnMsg& turn, const sim::SimulationEngine& engine);

    /// The ticks that the recording has seen
    uint32_t turns() const noexcept { return turns_; }
    /// The commands that it holds
    size_t commands() const noexcept { return replay_.commands.size(); }
    /// The commands that it holds from the seat of the machine that records (the head's recorder_seat): what the person at this machine did, the quit included. Nobody else's: the computer
    /// players' orders and the other machines' are in `commands()` but not here. 0 when the head names no seat.
    size_t own_commands() const noexcept { return own_; }
    /// The commands that it left out because the engine turns them down (they would change nothing)
    uint32_t skipped_commands() const noexcept { return skipped_; }
    /// Why the recording cannot be a replay ("" while it can)
    const std::string& failure() const noexcept { return failure_; }
    bool finished() const noexcept { return finished_; }

    /// Ends the recording: the file's bytes, or empty when there is nothing to write (no turn ran, the recording failed, it is finished already); `error` says why. The engine is read for
    /// the final state hash and whether the rules ended the match. Call it after the last command and the last tick of the match. Whatever the recorder is told afterwards is ignored.
    std::vector<uint8_t> finish(const sim::SimulationEngine& engine, std::string& error);

    /// The replay as it stands (for the tests)
    const Replay& replay() const noexcept { return replay_; }

private:
    void fail(const std::string& why);

    Replay replay_;
    uint32_t turns_{0};
    uint32_t skipped_{0};
    size_t own_{0};                    // of the commands held, those of the recorder's seat
    size_t bytes_{0};                  // what the commands will take in the file, about
    std::string failure_;
    bool finished_{false};
};

}  // namespace ants::replay
