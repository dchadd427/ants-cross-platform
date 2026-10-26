#pragma once

// The due times of the ants' timed events as a heap (docs/ARCHITECTURE.md, "Lookups in the tick loop"). Private to the simulation.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ants::sim {

class AntUnit;

/// The four timed events of an ant, in the order in which the scan of movement_tick looked at them for one ant (so that the order of the kinds is the tie-break between two
/// events of the same ant that fall due at the same time).
enum class AntEventKind : uint8_t { Burn = 0, Combevt = 1, Loco = 2, Pause = 3 };
constexpr size_t kAntEventKinds = 4;

/// The earliest event of the ants: whose it is, what it is, when it falls due
struct AntEventPick {
    AntUnit* ant{nullptr};
    uint32_t slot{0};
    uint32_t time{0};
    AntEventKind kind{AntEventKind::Burn};
};

/// The events that are due, ordered by (time, slot, kind): the order of the scan that this replaced, which looked at the ants in the order of the table, at the four events of an
/// ant in the order of AntEventKind, and took the first of the earliest. The key has no ties (an ant has one event of each kind), so the heap's answer is the scan's.
///
/// It is a binary heap with lazy deletion. Every (ant slot, kind) has a RECORD: whether the event is pending, when it falls due, and a version that changes whenever the record
/// does. `set` is the only writer of the records and the heap: it pushes an entry (time, slot, kind, version) when an event becomes pending or its time changes, and never
/// looks for the old entry: an entry whose version is not its record's version is dead (the version also changes when an event stops being pending), and `top` throws dead
/// entries away when they reach the top. So the heap holds one live entry for each pending event, plus dead ones that leave when their time comes (and the heap is made
/// again from the records when the dead ones outnumber the live).
///
/// It holds nothing of its own: the records say what the ants' fields say (SimulationEngineImpl::event_refresh writes them from the fields, after every change of the fields),
/// it is never hashed and never sent, and a copy of the engine copies it with the ants (a slot is a place in the copied table). The copy operations are written out so that this
/// is stated, and a copy that did not copy it fails the checks of test_engine_lookups.
class AntEventHeap {
public:
    AntEventHeap() = default;
    AntEventHeap(const AntEventHeap&) = default;
    AntEventHeap& operator=(const AntEventHeap&) = default;
    AntEventHeap(AntEventHeap&&) noexcept = default;
    AntEventHeap& operator=(AntEventHeap&&) noexcept = default;

    /// No event of any ant
    void reset() {
        heap_.clear();
        recs_.clear();
        live_ = 0;
    }

    /// The event `kind` of the ant in this slot is pending and falls due at `due` (`pending`), or is not pending. THE writer: a call that changes nothing changes nothing, and
    /// returns false; true: the heap changed.
    bool set(uint32_t slot, AntEventKind kind, bool pending, uint32_t due) {
        const size_t k = static_cast<size_t>(kind);
        const uint8_t bit = static_cast<uint8_t>(1u << k);
        if (slot >= recs_.size()) {
            if (!pending) return false;
            recs_.resize(static_cast<size_t>(slot) + 1u);
        }
        Record& r = recs_[slot];
        const bool was = (r.live & bit) != 0;
        if (!pending) {
            if (!was) return false;
            r.live = static_cast<uint8_t>(r.live & ~bit);
            ++r.version[k];                                       // the entry of the event that is gone is dead now
            --live_;
            return true;
        }
        if (was && r.due[k] == due) return false;                 // already pending at that time, its entry is in the heap
        if (!was) {
            r.live = static_cast<uint8_t>(r.live | bit);
            ++live_;
        }
        r.due[k] = due;
        ++r.version[k];                                           // the entry of the old time (if any) is dead now
        heap_.push_back(Entry{due, slot, r.version[k], static_cast<uint8_t>(k)});
        std::push_heap(heap_.begin(), heap_.end(), later);
        if (heap_.size() > 4u * static_cast<size_t>(live_) + 4096u) rebuild();
        return true;
    }

    /// The earliest pending event, put in `out` (false: none); it stays in the heap, and is the earliest until `set` changes it (the handler of an event changes the ant, which
    /// sets its events again). The dead entries at the top are thrown away on the way.
    bool top(AntEventPick& out) {
        while (!heap_.empty()) {
            const Entry& e = heap_.front();
            if (alive(e)) {
                out.slot = e.slot;
                out.time = e.time;
                out.kind = static_cast<AntEventKind>(e.kind);
                return true;
            }
            std::pop_heap(heap_.begin(), heap_.end(), later);
            heap_.pop_back();
        }
        return false;
    }

    // What the checks of the shadow build look at
    size_t slots() const noexcept { return recs_.size(); }
    size_t pending() const noexcept { return live_; }
    size_t entries() const noexcept { return heap_.size(); }
    /// Whether the event is pending, and when it falls due
    bool pending_at(uint32_t slot, AntEventKind kind, uint32_t& due) const noexcept {
        if (slot >= recs_.size()) return false;
        const size_t k = static_cast<size_t>(kind);
        if ((recs_[slot].live & (1u << k)) == 0) return false;
        due = recs_[slot].due[k];
        return true;
    }
    /// The heap is a heap, and its live entries are exactly the pending events, each with the time of its record
    bool consistent() const {
        if (!std::is_heap(heap_.begin(), heap_.end(), later)) return false;
        size_t alive_entries = 0;
        for (const Entry& e : heap_) {
            if (!alive(e)) continue;
            ++alive_entries;
            if (recs_[e.slot].due[e.kind] != e.time) return false;
        }
        return alive_entries == live_;
    }

private:
    struct Entry {
        uint32_t time;
        uint32_t slot;
        uint32_t version;
        uint8_t kind;
    };
    struct Record {
        uint32_t due[kAntEventKinds]{};
        uint32_t version[kAntEventKinds]{};
        uint8_t live{0};                                          // bit k: the event of kind k is pending
    };

    /// std::push_heap makes a max-heap of what `less` calls less: "later" is the order of the scan reversed, so the front is the earliest
    static bool later(const Entry& a, const Entry& b) noexcept {
        if (a.time != b.time) return a.time > b.time;
        if (a.slot != b.slot) return a.slot > b.slot;
        return a.kind > b.kind;
    }
    bool alive(const Entry& e) const noexcept {
        return e.slot < recs_.size() && recs_[e.slot].version[e.kind] == e.version;
    }
    /// The heap again from the records: one live entry for each pending event, no dead ones
    void rebuild() {
        heap_.clear();
        for (size_t slot = 0; slot < recs_.size(); ++slot) {
            const Record& r = recs_[slot];
            for (size_t k = 0; k < kAntEventKinds; ++k) {
                if ((r.live & (1u << k)) != 0) heap_.push_back(Entry{r.due[k], static_cast<uint32_t>(slot), r.version[k], static_cast<uint8_t>(k)});
            }
        }
        std::make_heap(heap_.begin(), heap_.end(), later);
    }

    std::vector<Entry> heap_;
    std::vector<Record> recs_;
    uint32_t live_{0};
};

} // namespace ants::sim
