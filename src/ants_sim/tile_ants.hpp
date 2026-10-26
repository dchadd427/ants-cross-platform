#pragma once

// The ants on each tile of the occupancy grid, as lists (docs/ARCHITECTURE.md, "Lookups in the tick loop"). Private to the simulation.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ants::sim {

/// For every cell of the occupancy grid, the ants whose `occ_tile` is that cell, as a doubly linked list through the table of ants: `head` has a cell's first ant, and
/// `next` / `prev` belong to an ant, named by its SLOT (its place in `SimulationEngineImpl::ants_`, which only grows). Adding and removing an ant is O(1); the memory is
/// 4 bytes a cell and 8 a ant, whatever the number of ants on a tile. The order inside a list is the order of arrival and means nothing: a question that depends on the
/// order of the table (occ_scan, blast) asks for the slots and orders them itself.
///
/// It holds nothing of its own: it is derived from the ants' `occ_tile`, changes exactly where an ant's `occ_tile` does (`SimulationEngineImpl::occ_move`, and `occ_refresh`
/// when the grid has another size), is never hashed and never sent, and a copy of the engine copies it with the ants (a slot is a place in the copied table, so every link
/// stays right). The copy operations are written out so that this is stated, and a copy that did not copy it fails the checks of test_engine_lookups.
class TileAntIndex {
public:
    static constexpr uint32_t kNone = 0xFFFFFFFFu;

    TileAntIndex() = default;
    TileAntIndex(const TileAntIndex&) = default;
    TileAntIndex& operator=(const TileAntIndex&) = default;
    TileAntIndex(TileAntIndex&&) noexcept = default;
    TileAntIndex& operator=(TileAntIndex&&) noexcept = default;

    /// No ant anywhere, `cells` cells
    void reset(size_t cells) {
        head_.assign(cells, kNone);
        next_.clear();
        prev_.clear();
    }

    /// The same lists in the same order (the shadow build compares two refreshes of one state)
    bool operator==(const TileAntIndex& o) const { return head_ == o.head_ && next_ == o.next_ && prev_ == o.prev_; }

    size_t cells() const noexcept { return head_.size(); }
    uint32_t first(size_t cell) const noexcept { return head_[cell]; }
    uint32_t next(uint32_t slot) const noexcept { return next_[slot]; }
    uint32_t prev(uint32_t slot) const noexcept { return prev_[slot]; }
    /// The slots that have a link (the checks of the shadow build walk them)
    size_t slots() const noexcept { return next_.size(); }

    /// The ant of this slot joins the list of the cell (it is in no list: the caller keeps that true)
    void link(size_t cell, uint32_t slot) {
        if (slot >= next_.size()) {
            next_.resize(static_cast<size_t>(slot) + 1u, kNone);
            prev_.resize(static_cast<size_t>(slot) + 1u, kNone);
        }
        const uint32_t old_first = head_[cell];
        next_[slot] = old_first;
        prev_[slot] = kNone;
        if (old_first != kNone) prev_[old_first] = slot;
        head_[cell] = slot;
    }

    /// The ant of this slot leaves the list of the cell (it is in that list: the caller keeps that true)
    void unlink(size_t cell, uint32_t slot) noexcept {
        const uint32_t n = next_[slot];
        const uint32_t p = prev_[slot];
        if (p != kNone) next_[p] = n; else head_[cell] = n;
        if (n != kNone) prev_[n] = p;
        next_[slot] = kNone;
        prev_[slot] = kNone;
    }

private:
    std::vector<uint32_t> head_;
    std::vector<uint32_t> next_;
    std::vector<uint32_t> prev_;
};

} // namespace ants::sim
