// The timed events of the ants (the burn overlay's end, the combat timer, the next frame of the animation, the end of a pause) that movement_tick runs in the order of their due
// times, and the heap that finds the next one without a look at every ant (the heap is event_heap.hpp; docs/ARCHITECTURE.md, "Lookups in the tick loop").
//
// WHAT FALLS DUE. An ant has four events, each with a due time in a field of the ant and a rule for when it is pending (due_of below is the rule, the scan is the same rule
// in the shape that the original loop had):
//   Burn     burn_end_ms          pending when the ant is not removed and the field is not 0
//   Combevt  combevt_due_ms       pending when the ant is not removed and the field is not 0
//   Loco     loco.next_ms         pending when loco_action is not none, the clip is valid, the cursor is not 0 and the clip has more than one frame
//   Pause    pause_fire_ms        pending when loco_action is not none and pause_active is set
// (no event whose time is the largest 32-bit number is ever taken: the scan compared with that number as its first "best" and so does the rule). The next event is the pending one
// with the earliest time at or before the end of the tick; two events at the same time go to the ant that is earlier in the table, and two of the same ant to the earlier kind in
// the order above. The tasks (anthill queue, hatching, flower droppers) are looked at after the ants, and go before an ant event only when their time is strictly earlier.
//
// WHO CHANGES THEM. Every site that writes one of the fields above calls event_refresh afterwards (it is the one writer of the heap, as occ_move is of occ_tile): burn_overlay_start
// and burn_overlay_end, start_combat_timer and cancel_combat_timer (combat_system.cpp), remove_ant (removed, loco_action), low_hp_check (a raw write of loco_action), loco_play,
// loco_step, loco_release, start_pause and cancel_pause (movement_system.cpp), and the raw write of the combat timer in movement_tick. set_action (loco_action) and pause_fire
// (pause_active) have no call of their own: every path of the first writes loco_action and then calls cancel_pause or loco_play, and every path of the second ends in start_pause
// or set_action. loco_sync writes some of the fields too (the writes of an ant that is not alive, and of a stopped walker, which set_action follows): the pass over every ant at
// the start of movement_tick (event_catch_up) is the net under all of them, and in the shadow build it must never find anything to do. An event that is run stays in the heap while
// its handler runs: what the handler changes is set again by the site that changes it. A copy of the engine copies the heap with the ants. The shadow build checks all of it
// (lookup_shadow.hpp, event_heap_verify): the heap against the fields at the start and the end of every tick and after every copy, every selection against the scan.

#include "sim_engine_impl.hpp"

#include <limits>

namespace ants::sim {

namespace {

constexpr uint32_t kNever = std::numeric_limits<uint32_t>::max();

/// Whether the event is pending, and its due time. The rule of the table above.
inline bool due_of(const AntUnit& a, AntEventKind kind, uint32_t& due) noexcept {
    switch (kind) {
        case AntEventKind::Burn:
            due = a.burn_end_ms;
            return !a.removed && due != 0 && due != kNever;
        case AntEventKind::Combevt:
            due = a.combevt_due_ms;
            return !a.removed && due != 0 && due != kNever;
        case AntEventKind::Loco:
            due = a.loco.next_ms;
            return a.loco_action != AntUnit::kActionNone && a.loco.clip.valid() && a.loco.cursor != 0 && a.loco.clip.count > 1 && due != kNever;
        case AntEventKind::Pause:
            due = a.pause_fire_ms;
            return a.loco_action != AntUnit::kActionNone && a.pause_active && due != kNever;
    }
    return false;
}

} // namespace

bool SimulationEngineImpl::event_refresh(const AntUnit& a, uint32_t slot) {
    bool changed = false;
    for (size_t k = 0; k < kAntEventKinds; ++k) {
        const AntEventKind kind = static_cast<AntEventKind>(k);
        uint32_t due = 0;
        const bool pending = due_of(a, kind, due);
        changed = event_heap_.set(slot, kind, pending, due) || changed;
    }
    return changed;
}

void SimulationEngineImpl::event_catch_up(const AntUnit& a, uint32_t slot) {
    const bool changed = event_refresh(a, slot);
#ifdef ANTS_SIM_LOOKUP_SHADOW
    ++lookup_shadow_event_catch_ups();
    if (changed) lookup_shadow_fail("event heap (an ant whose events only the start of the tick put right)", a.id);
#endif
    (void)changed;
}

void SimulationEngineImpl::event_refresh(const AntUnit& a) {
    const uint32_t slot = ant_slot(a);
    if (slot != TileAntIndex::kNone) event_refresh(a, slot);          // (an ant that is not in the table is in no scan either)
}

// The scan over every ant that movement_tick made for each event (the old loop, with the tasks left to the caller): the earliest event that is due at or before t_end, the first
// one in the order (table, kind) among equals. This is the reference of the shadow build and the whole of the engine that the test sets to the scans alone.
bool SimulationEngineImpl::next_ant_event_scan(uint32_t t_end, AntEventPick& out) {
    AntUnit* best = nullptr;
    uint32_t best_slot = 0;
    uint32_t best_t = kNever;
    AntEventKind best_kind = AntEventKind::Burn;
    uint32_t slot = 0;
    for (auto& up : ants_) {
        AntUnit* a = up.get();
        const uint32_t this_slot = slot++;
        if (a && !a->removed && a->burn_end_ms != 0 && a->burn_end_ms <= t_end && a->burn_end_ms < best_t) {
            best = a;
            best_slot = this_slot;
            best_t = a->burn_end_ms;
            best_kind = AntEventKind::Burn;
        }
        if (a && !a->removed && a->combevt_due_ms != 0 && a->combevt_due_ms <= t_end && a->combevt_due_ms < best_t) {
            best = a;
            best_slot = this_slot;
            best_t = a->combevt_due_ms;
            best_kind = AntEventKind::Combevt;
        }
        if (!a || a->loco_action == AntUnit::kActionNone) continue;
        const auto& p = a->loco;
        if (p.clip.valid() && p.cursor != 0 && p.clip.count > 1 && p.next_ms <= t_end && p.next_ms < best_t) {
            best = a;
            best_slot = this_slot;
            best_t = p.next_ms;
            best_kind = AntEventKind::Loco;
        }
        if (a->pause_active && a->pause_fire_ms <= t_end && a->pause_fire_ms < best_t) {
            best = a;
            best_slot = this_slot;
            best_t = a->pause_fire_ms;
            best_kind = AntEventKind::Pause;
        }
    }
    if (!best) return false;
    out.ant = best;
    out.slot = best_slot;
    out.time = best_t;
    out.kind = best_kind;
    return true;
}

bool SimulationEngineImpl::next_ant_event(uint32_t t_end, AntEventPick& out) {
#ifdef ANTS_SIM_LOOKUP_SHADOW
    if (lookup_shadow_linear_only()) return next_ant_event_scan(t_end, out);
#endif
    bool found = false;
    AntEventPick p;
    while (event_heap_.top(p)) {
        AntUnit* const a = p.slot < ants_.size() ? ants_[p.slot].get() : nullptr;
        uint32_t due = 0;
        if (a && due_of(*a, p.kind, due) && due == p.time) {
            p.ant = a;
            found = p.time <= t_end;                                   // the earliest event of the heap is not due yet: nothing is
            break;
        }
        // A live entry that the ant's fields do not bear out: a change of a field that no event_refresh followed. The shadow build stops here; a product build puts the ant right
        // and goes on (the entry is dead once the ant's events are set again, and a missing ant's entry is taken out).
#ifdef ANTS_SIM_LOOKUP_SHADOW
        lookup_shadow_fail("event heap (an entry that is not the ant's)", a ? a->id : 0u);
#endif
        if (a) event_refresh(*a, p.slot);
        else event_heap_.set(p.slot, p.kind, false, 0);
    }
#ifdef ANTS_SIM_LOOKUP_SHADOW
    ++lookup_shadow_event_checks();                                    // (every selection is compared with the scan over every ant)
    AntEventPick scanned;
    const bool scan_found = next_ant_event_scan(t_end, scanned);
    if (scan_found != found || (found && (scanned.ant != p.ant || scanned.time != p.time || scanned.kind != p.kind))) {
        lookup_shadow_fail("next_ant_event", scan_found ? scanned.ant->id : 0u);
    }
    if (found) ++lookup_shadow_event_picks(static_cast<size_t>(p.kind));
#endif
    if (found) out = p;
    return found;
}

#ifdef ANTS_SIM_LOOKUP_SHADOW
// The heap against the ants: for every ant of the table and each kind, the event is pending in the heap exactly when the ant's fields make it pending, with the same due time; no
// pending event belongs to a place that is not in the table; and the heap is a heap whose live entries are the pending events.
void SimulationEngineImpl::event_heap_verify() const {
    ++lookup_shadow_event_verifies();
    size_t pending = 0;
    for (size_t slot = 0; slot < ants_.size(); ++slot) {
        for (size_t k = 0; k < kAntEventKinds; ++k) {
            const AntEventKind kind = static_cast<AntEventKind>(k);
            uint32_t due = 0;
            const bool by_fields = ants_[slot] && due_of(*ants_[slot], kind, due);
            uint32_t recorded = 0;
            const bool by_heap = event_heap_.pending_at(static_cast<uint32_t>(slot), kind, recorded);
            if (by_fields != by_heap || (by_fields && due != recorded)) lookup_shadow_fail("event heap (an ant's event)", ants_[slot] ? ants_[slot]->id : 0u);
            if (by_heap) ++pending;
        }
    }
    for (size_t slot = ants_.size(); slot < event_heap_.slots(); ++slot) {
        for (size_t k = 0; k < kAntEventKinds; ++k) {
            uint32_t recorded = 0;
            if (event_heap_.pending_at(static_cast<uint32_t>(slot), static_cast<AntEventKind>(k), recorded)) lookup_shadow_fail("event heap (an event of no ant)", static_cast<uint32_t>(slot));
        }
    }
    if (pending != event_heap_.pending() || !event_heap_.consistent()) lookup_shadow_fail("event heap (the heap)", static_cast<uint32_t>(pending));
}
#endif

} // namespace ants::sim
