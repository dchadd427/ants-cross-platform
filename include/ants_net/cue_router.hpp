#pragma once

// Who plays which cue, once (docs/NETWORK_PORT.md "Prediction of one's own orders", the cues).
//
// With the prediction on, the picture is the predicted engine's: an own ant shows what it was ordered a few ticks before the confirmed engine has it. Its CUES stay the confirmed engine's,
// except for the few that belong to the player's own order and not to any other player: the cue of an own ant that is doing what it was told (the can't-go cue, the harvest, the
// power-up chimes, the fire ant's, the bomber's, the swimmer's bridge, the thief's raid cues, the bump of own ants) and the stop events that cut what that ant started. Those are played from
// the predicted engine, in step with the picture, when it first runs the tick that makes them, and the confirmed engine's copy of the same cue, which comes a few ticks later, is dropped.
// Everything else (combat, hits, deaths, explosions, scores, the alliance cues, the clock's warnings, every cue of another player's ants) stays the confirmed engine's: it is never a guess.
// A cue that was played for what then did not happen (a prediction that a turn proved wrong) can only be one of the own ant's own actions above.
//
//   - Only what the predicted engine makes when it RUNS ON from its display tick is played. A rebuild replays the ticks it had run before (and catches up whatever it has to): what a replay
//     makes is never played from here (Prediction::PredictedAudio::replay), so a correction cannot make a cue heard twice. What the corrected timeline makes later is made again by the
//     confirmed engine, and played then, late but never wrong.
//   - The confirmed engine makes every cue at the same tick the predicted engine did when the prediction was right: its copy is dropped, one for each occurrence that was played. When the
//     prediction was a little off (an order that the server sealed a few ticks later than the prediction assumed), the cue comes a few ticks apart and is matched all the same: the nearest
//     occurrence of the same kind (the same sound or stop of the same ant) within kMatchWindowTicks. An occurrence that nobody played is played, so no cue of an own ant is ever lost either.
// No clock and no sockets: the owner feeds it both streams.

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <vector>

#include "ants_net/prediction.hpp"
#include "ants_sim/sim_engine.hpp"

namespace ants::net {

class CueRouter {
public:
    /// How far apart, in ticks, the two engines' copies of one cue may stand and still be the same cue (the longest delay of an own order that the prediction follows: Config::max_lead_ticks)
    static constexpr uint64_t kMatchWindowTicks = 12;

    /// Whether an ant id belongs to the player whose orders are predicted (the owner of an event is an ant id, or the id of an effect sprite, 0x40000000 and up)
    using OwnsAnt = std::function<bool(uint32_t ant_id)>;

    struct Stats {
        uint64_t played_predicted{0};       // occurrences played from the predicted engine
        uint64_t replays_dropped{0};        // occurrences of the class that a rebuild's replay made: not played from here
        uint64_t repeats_dropped{0};        // occurrences of a tick that was run and played before, run again by a straight run (the prediction was suspended and began again within its lead)
        uint64_t duplicates_dropped{0};     // the confirmed engine's copies of what was played from the predicted engine
        uint64_t played_confirmed_own{0};   // own-class occurrences that the confirmed engine made and nobody had played (the prediction was off, late or wrong)
        uint64_t phantoms{0};               // occurrences played from the predicted engine that the confirmed engine never made (its ticks passed without)
    };

    /// True for the cues that are played from the predicted engine when an own ant makes them (the list above); a stop event of an own ant is of the class whatever it cuts
    static bool is_own_action_sound(uint32_t sound_id) noexcept;
    /// True when the event is of the class: a stop of an own ant, or an own-action cue of an own ant (the bump has no owner: only the viewer's own ants make it)
    static bool in_class(const sim::AudioEvent& event, const OwnsAnt& owns) noexcept;

    /// What the predicted engine made since the last call (Prediction::take_audio): returns the occurrences to play now, in order. Only the class is played from here, and only what the
    /// predicted engine made by running on.
    std::vector<sim::AudioEvent> from_predicted(const std::vector<Prediction::PredictedAudio>& events, const OwnsAnt& owns);
    /// What the confirmed engine made in the tick that has just run (`tick`: the index of that tick, `current_tick() - 1` after it): returns the events to play, in order. The confirmed
    /// engine has passed the tick: what the book still holds from before the window is waited for no more.
    std::vector<sim::AudioEvent> from_confirmed(std::vector<sim::AudioEvent> events, uint64_t tick, const OwnsAnt& owns);

    /// Forget everything (a match begins)
    void reset();
    const Stats& stats() const noexcept { return stats_; }
    /// The occurrences that were played from the predicted engine and that the confirmed engine has not met yet (about the lead's worth, in a running match)
    size_t outstanding() const noexcept;

private:
    struct Kind {
        uint32_t sound{0};
        uint32_t owner{0};
        bool stop{false};
        bool operator<(const Kind& other) const noexcept {
            if (stop != other.stop) return stop < other.stop;
            if (owner != other.owner) return owner < other.owner;
            return sound < other.sound;
        }
    };
    static Kind kind_of(const sim::AudioEvent& e) noexcept { return Kind{e.stop ? 0u : e.sound_id, e.owner, e.stop}; }

    std::map<Kind, std::multiset<uint64_t>> book_;      // kind -> the ticks at which an occurrence was played from the predicted engine and not yet met by the confirmed one
    Stats stats_;
};

}  // namespace ants::net
