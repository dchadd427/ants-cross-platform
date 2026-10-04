#include "ants_net/cue_router.hpp"

#include <iterator>
#include <utility>

namespace ants::net {

namespace {

constexpr uint32_t kEffectOwnerBase = 0x40000000u;        // the owner ids from here on are effect sprites (explosions, dust balls), never ants

}  // namespace

// The cues of an own ant that is doing what it was ordered, which nothing another player does can change: it is refused (cantgo), it grabs food, picks up a power-up, the fire ant lights or
// puts out a fire, the bomber picks up, defuses, the swimmer shovels a bridge, the thief raids, and the bump of an own ant against another. Not in it: every blow, hit, stun, drowning, fall
// (what the other player's ants do to this one's), the explosions, the scores, the hatching, and the cues that are not an ant's (the clock, the alliances, the can't-hatch cue, the siren).
bool CueRouter::is_own_action_sound(uint32_t sound_id) noexcept {
    switch (sound_id) {
        case sim::SoundID::PowerUpHeal:
        case sim::SoundID::PowerUpChime:
        case sim::SoundID::Bump:
        case sim::SoundID::CantGo:
        case sim::SoundID::FoodHarvest:
        case sim::SoundID::FireBeam:
        case sim::SoundID::FireErupt:
        case sim::SoundID::FireExtinguish:
        case sim::SoundID::BombDefuseGrab:
        case sim::SoundID::BombBodySquash:
        case sim::SoundID::FoodGrab:
        case sim::SoundID::ShovelGravel:
        case sim::SoundID::ShovelWater:
        case sim::SoundID::ThiefDive:
        case sim::SoundID::ThiefRummage:
        case sim::SoundID::ThiefEmerge:
        case sim::SoundID::BombPick:
            return true;
        default:
            return false;
    }
}

bool CueRouter::in_class(const sim::AudioEvent& event, const OwnsAnt& owns) noexcept {
    const bool ant_owner = event.owner != 0 && event.owner < kEffectOwnerBase;
    if (event.stop) return ant_owner && owns(event.owner);
    if (!is_own_action_sound(event.sound_id)) return false;
    if (event.sound_id == sim::SoundID::Bump && event.owner == 0) return true;      // (the engine makes the bump for the viewer's own ants alone)
    return ant_owner && owns(event.owner);
}

std::vector<sim::AudioEvent> CueRouter::from_predicted(const std::vector<Prediction::PredictedAudio>& events, const OwnsAnt& owns) {
    std::vector<sim::AudioEvent> out;
    std::map<Kind, std::map<uint64_t, uint32_t>> seen;                    // this call's occurrences: kind -> tick -> how many
    for (const Prediction::PredictedAudio& pa : events) {
        if (!in_class(pa.event, owns)) continue;
        if (pa.replay) {                                                  // a tick that was run before (or caught up by a rebuild): the confirmed engine's copy is what will be heard
            ++stats_.replays_dropped;
            continue;
        }
        const Kind kind = kind_of(pa.event);
        const uint32_t n = ++seen[kind][pa.tick];
        std::multiset<uint64_t>& ticks = book_[kind];
        if (ticks.count(pa.tick) >= n) {                                  // this very tick was run and played before (the prediction was suspended and began again within its lead)
            ++stats_.repeats_dropped;
            continue;
        }
        ticks.insert(pa.tick);
        out.push_back(pa.event);
        ++stats_.played_predicted;
    }
    return out;
}

std::vector<sim::AudioEvent> CueRouter::from_confirmed(std::vector<sim::AudioEvent> events, uint64_t tick, const OwnsAnt& owns) {
    std::vector<sim::AudioEvent> out;
    out.reserve(events.size());
    for (sim::AudioEvent& e : events) {
        if (in_class(e, owns)) {
            const auto found = book_.find(kind_of(e));
            bool met = false;
            if (found != book_.end() && !found->second.empty()) {
                std::multiset<uint64_t>& ticks = found->second;
                // the nearest occurrence of the same kind that was played from the predicted engine: the same tick when the prediction was right, a few ticks off when an order of the
                // player was sealed later or earlier than it assumed
                auto later = ticks.lower_bound(tick);
                auto best = ticks.end();
                uint64_t best_distance = kMatchWindowTicks + 1;
                if (later != ticks.end() && *later - tick < best_distance) {
                    best = later;
                    best_distance = *later - tick;
                }
                if (later != ticks.begin()) {
                    auto earlier = std::prev(later);
                    if (tick - *earlier <= best_distance) {                // (on a tie the earlier one: it has waited longer)
                        best = earlier;
                        best_distance = tick - *earlier;
                    }
                }
                if (best != ticks.end() && best_distance <= kMatchWindowTicks) {
                    ticks.erase(best);
                    met = true;
                }
            }
            if (met) {
                ++stats_.duplicates_dropped;
                continue;                                                  // heard already, from the predicted engine
            }
            ++stats_.played_confirmed_own;
        }
        out.push_back(std::move(e));
    }
    // what the book holds from before the window can never be met by a later tick of the confirmed engine: it was played for something that did not happen
    for (auto it = book_.begin(); it != book_.end();) {
        std::multiset<uint64_t>& ticks = it->second;
        while (!ticks.empty() && *ticks.begin() + kMatchWindowTicks <= tick) {
            ticks.erase(ticks.begin());
            ++stats_.phantoms;
        }
        it = ticks.empty() ? book_.erase(it) : std::next(it);
    }
    return out;
}

void CueRouter::reset() {
    book_.clear();
    stats_ = Stats{};
}

size_t CueRouter::outstanding() const noexcept {
    size_t n = 0;
    for (const auto& [kind, ticks] : book_) n += ticks.size();
    return n;
}

}  // namespace ants::net
