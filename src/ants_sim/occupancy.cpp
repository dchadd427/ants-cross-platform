// The occupancy grid (world+0x553c) of the original's movement system, and the lists of the ants on each tile that answer its questions without a scan of every ant (the
// list is tile_ants.hpp; docs/ARCHITECTURE.md, "Lookups in the tick loop"). Function addresses refer to Original-Ants/Ants.exe, see docs/GAME_REVERSE_ENGINEERING.md,
// "Movement ground truth".

#include "sim_engine_impl.hpp"

#include <algorithm>
#include <limits>

namespace ants::sim {

namespace {

constexpr int32_t kTile = 32;

inline TileCoord pixel_tile(const AntUnit& a) noexcept { return TileCoord{a.pixel_x / kTile, a.pixel_y / kTile}; }   // FUN_0100ccc0 (idiv truncation)

} // namespace

// ------------------------------------------------------------------------------------------------
// Occupancy grid (world+0x553c)
// ------------------------------------------------------------------------------------------------

size_t SimulationEngineImpl::occ_cell_index(TileCoord t) const noexcept {
    if (!grid_.in_bounds(t)) return kNoCell;
    const size_t idx = static_cast<size_t>(t.y) * grid_.width() + static_cast<size_t>(t.x);
    if (idx >= occ_.size()) return kNoCell;
    return idx;
}

SimulationEngineImpl::OccCell* SimulationEngineImpl::occ_cell(TileCoord t) noexcept {
    const size_t idx = occ_cell_index(t);
    return idx == kNoCell ? nullptr : &occ_[idx];
}

uint32_t SimulationEngineImpl::ant_slot(const AntUnit& a) const noexcept {
    const size_t guess = static_cast<size_t>(a.id) - 1u;                  // the table is dense (see find_unit): the place of ant n is n - 1, checked and never trusted
    if (guess < ants_.size() && ants_[guess].get() == &a) return static_cast<uint32_t>(guess);
    for (size_t i = 0; i < ants_.size(); ++i) {
        if (ants_[i].get() == &a) return static_cast<uint32_t>(i);
    }
    return TileAntIndex::kNone;
}

// FUN_0100f2cd: every ant registered on t (team slots 0..3, then team index order); the first one found
// is the default occupant, and a later ant replaces it when it has no path and a stationary action.
// This is the scan over the whole table; occ_scan answers the same from the tile's list.
AntUnit* SimulationEngineImpl::occ_scan_scan(TileCoord t, const AntUnit* exclude, uint16_t* count) {
    uint16_t n = 0;
    AntUnit* best = nullptr;
    for (uint8_t team = 0; team < MAX_PLAYERS; ++team) {
        for (auto& up : ants_) {
            AntUnit* a = up.get();
            if (!a || a->player_id != team || a == exclude) continue;
            if (a->occ_tile != t) continue;
            ++n;
            if (!best) {
                best = a;
            } else if (a->waypoints.empty() && is_stationary_action(orig_action_of(*a))) {
                best = a;
            }
        }
    }
    if (count) *count = n;
    return best;
}

// The scan visits the ants of a tile in the order (team, place in the table) and takes the FIRST of them, then every later one that has no path and a stationary action
// replaces it: so the answer is the last such ant in that order, and the first ant when no ant is such (an ant that is both the first and the only one that is
// is the first). The list holds the same ants in no order; the order is the key (team, slot), which has no ties, and two minima / maxima give the answer.
AntUnit* SimulationEngineImpl::occ_scan(TileCoord t, const AntUnit* exclude, uint16_t* count) {
#ifdef ANTS_SIM_LOOKUP_SHADOW
    if (lookup_shadow_linear_only()) return occ_scan_scan(t, exclude, count);
#endif
    const size_t cell = occ_cell_index(t);
    if (cell == kNoCell) return occ_scan_scan(t, exclude, count);       // off the grid there is no list (nothing asks for such a tile)
    uint16_t n = 0;
    AntUnit* first = nullptr;
    uint64_t first_key = std::numeric_limits<uint64_t>::max();
    AntUnit* last_still = nullptr;
    uint64_t last_still_key = 0;
    for (uint32_t slot = tile_ants_.first(cell); slot != TileAntIndex::kNone; slot = tile_ants_.next(slot)) {
        AntUnit* a = ants_[slot].get();
        if (!a || a->player_id >= MAX_PLAYERS || a == exclude) continue;
        ++n;
        const uint64_t key = (static_cast<uint64_t>(a->player_id) << 32) | slot;
        if (key < first_key) {
            first = a;
            first_key = key;
        }
        if (key >= last_still_key && a->waypoints.empty() && is_stationary_action(orig_action_of(*a))) {
            last_still = a;
            last_still_key = key;
        }
    }
    AntUnit* const best = last_still ? last_still : first;
#ifdef ANTS_SIM_LOOKUP_SHADOW
    if (lookup_shadow_sample(ants_.size())) {
        ++lookup_shadow_scan_checks();
        uint16_t n_scan = 0;
        AntUnit* const scanned = occ_scan_scan(t, exclude, &n_scan);
        if (scanned != best || n_scan != n) lookup_shadow_fail("occ_scan (tile)", static_cast<uint32_t>(cell));
    }
#endif
    if (count) *count = n;
    return best;
}

// FUN_0100f17f: move the ant's registration from its old tile to nt. The ONE writer of occ_tile: it ends by noting whether the ant is in step (occ_note), on every path.
void SimulationEngineImpl::occ_move(AntUnit& a, TileCoord nt) {
    const uint32_t slot = ant_slot(a);
    occ_move_registration(a, nt, slot);
    occ_note(a, slot);
}

void SimulationEngineImpl::occ_move_registration(AntUnit& a, TileCoord nt, uint32_t slot) {
    const TileCoord old = a.occ_tile;
    if (old.x >= 0) {
        if (nt == old) return;
        const size_t old_index = occ_cell_index(old);
        if (old_index != kNoCell && slot != TileAntIndex::kNone) tile_ants_.unlink(old_index, slot);
        if (OccCell* c = occ_cell(old)) {
            if (c->multi) {
                uint16_t n = 0;
                AntUnit* one = occ_scan(old, &a, &n);
                c->multi = (n > 1);
                if (n == 1 && one) c->ant = static_cast<int32_t>(one->id);
                // n == 0: the original only clears the multi bit and leaves the departing ant's id.
            } else {
                c->ant = -1;
            }
        }
    }
    a.occ_tile = nt;
    if (nt.x < 0) return;
    const size_t new_index = occ_cell_index(nt);
    if (new_index != kNoCell && slot != TileAntIndex::kNone) tile_ants_.link(new_index, slot);
    if (OccCell* c = occ_cell(nt)) {
        if (c->ant < 0) c->ant = static_cast<int32_t>(a.id);
        else c->multi = true;
    }
}

// ------------------------------------------------------------------------------------------------
// Which ants occ_refresh may have to put right
// ------------------------------------------------------------------------------------------------

void SimulationEngineImpl::occ_note(const AntUnit& a) { occ_note(a, ant_slot(a)); }

void SimulationEngineImpl::occ_note(const AntUnit& a, uint32_t slot) {
    if (slot == TileAntIndex::kNone) return;                      // (not in the table: nothing refreshes it)
    const size_t word = slot >> 6;
    const uint64_t bit = uint64_t{1} << (slot & 63u);
    if (occ_in_step(a)) {
        if (word < occ_dirty_.size()) occ_dirty_[word] &= ~bit;
    } else {
        if (word >= occ_dirty_.size()) occ_dirty_.resize(word + 1, 0);
        occ_dirty_[word] |= bit;
    }
}

AntUnit* SimulationEngineImpl::add_ant(std::unique_ptr<AntUnit> unit) {
    AntUnit* const ant = unit.get();
    ants_.push_back(std::move(unit));
    occ_note(*ant);                                               // (its occ_tile is (-1, -1): out of step until the caller registers it)
    return ant;
}

// Remake: re-register ants whose pixel tile changed outside the movement system (teleports, knock-backs,
// spawns, test set-ups) and drop dead or underground ones, before occupancy is consulted by an order.
void SimulationEngineImpl::occ_refresh() {
    const size_t cells = static_cast<size_t>(grid_.width()) * grid_.height();
    if (occ_.size() != cells) {
        occ_.assign(cells, OccCell{});
        tile_ants_.reset(cells);
        for (auto& up : ants_) {
            if (up) up->occ_tile = TileCoord{-1, -1};
        }
    }
    for (auto& up : ants_) {
        if (up) occ_refresh_ant(*up);
    }
    occ_dirty_.clear();                                           // (every ant is in step now)
}

// What the refresh does to one ant: a removed ant that is still registered is unregistered, a live ant that is not registered on its pixel tile is moved there, any other ant
// is left alone (occ_in_step). Both refreshes are this, so they cannot differ in the rule, only in which ants they visit.
void SimulationEngineImpl::occ_refresh_ant(AntUnit& a) {
    if (!a.is_alive()) {
        if (a.occ_tile.x >= 0) occ_move(a, TileCoord{-1, -1});
    } else if (a.occ_tile != pixel_tile(a)) {
        occ_move(a, pixel_tile(a));
    }
}

#ifdef ANTS_SIM_LOOKUP_SHADOW
namespace {
// Everything the refresh writes, so that the shadow build can run both refreshes from one state and compare
struct OccState {
    std::vector<SimulationEngineImpl::OccCell> cells;
    std::vector<TileCoord> tiles;
    TileAntIndex lists;
    std::vector<uint64_t> dirty;
};
}
#endif

void SimulationEngineImpl::occ_refresh_for_order() {
#ifdef ANTS_SIM_LOOKUP_SHADOW
    if (lookup_shadow_linear_only()) return occ_refresh();         // (the engine of the scans alone is the code before the marks)
#endif
    if (!occ_in_tick_ || occ_.size() != static_cast<size_t>(grid_.width()) * grid_.height()) return occ_refresh();
#ifdef ANTS_SIM_LOOKUP_SHADOW
    ++lookup_shadow_go_to_calls();
    const bool sample = lookup_shadow_sample(ants_.size());
    bool any = false;
    for (const uint64_t w : occ_dirty_) any = any || w != 0;
    if (any) ++lookup_shadow_go_to_compares();
    if (sample) {                                                  // the premise: an ant that the full refresh would change is marked
        ++lookup_shadow_go_to_checks();
        for (size_t i = 0; i < ants_.size(); ++i) {
            if (ants_[i] && !occ_in_step(*ants_[i]) && !occ_marked(static_cast<uint32_t>(i))) lookup_shadow_fail("go_to refresh (an ant out of step is not marked)", ants_[i]->id);
        }
    }
    OccState before;
    if (any) {                                                     // marked ants to visit: the result is compared with the full refresh's, from the same state
        before.cells = occ_;
        before.lists = tile_ants_;
        before.dirty = occ_dirty_;
        for (const auto& up : ants_) before.tiles.push_back(up ? up->occ_tile : TileCoord{-1, -1});
    }
    uint32_t last_slot = TileAntIndex::kNone;
#endif
    for (size_t w = 0; w < occ_dirty_.size(); ++w) {
        while (occ_dirty_[w] != 0) {
            unsigned bit = 0;
            while (((occ_dirty_[w] >> bit) & 1u) == 0) ++bit;      // the lowest marked slot of the word: the order of the table
            const size_t slot = (w << 6) + bit;
            occ_dirty_[w] &= ~(uint64_t{1} << bit);                // (occ_move notes the ant again, and finds it in step)
#ifdef ANTS_SIM_LOOKUP_SHADOW
            if (last_slot != TileAntIndex::kNone && slot <= last_slot) lookup_shadow_fail("go_to refresh (the order of the table)", static_cast<uint32_t>(slot));
            last_slot = static_cast<uint32_t>(slot);
            ++lookup_shadow_go_to_dirty_ants();
#endif
            if (slot < ants_.size() && ants_[slot]) occ_refresh_ant(*ants_[slot]);
        }
    }
#ifdef ANTS_SIM_LOOKUP_SHADOW
    if (any) {
        OccState by_marks;
        by_marks.cells = occ_;
        by_marks.lists = tile_ants_;
        by_marks.dirty = occ_dirty_;
        for (const auto& up : ants_) by_marks.tiles.push_back(up ? up->occ_tile : TileCoord{-1, -1});
        occ_ = before.cells;                                       // the same state again, and the full refresh
        tile_ants_ = before.lists;
        occ_dirty_ = before.dirty;
        for (size_t i = 0; i < ants_.size(); ++i) {
            if (ants_[i]) ants_[i]->occ_tile = before.tiles[i];
        }
        occ_refresh();
        bool same = occ_.size() == by_marks.cells.size() && tile_ants_ == by_marks.lists;
        for (size_t i = 0; same && i < occ_.size(); ++i) same = occ_[i].ant == by_marks.cells[i].ant && occ_[i].multi == by_marks.cells[i].multi;
        for (size_t i = 0; same && i < ants_.size(); ++i) same = !ants_[i] || ants_[i]->occ_tile == by_marks.tiles[i];
        for (const uint64_t w : by_marks.dirty) same = same && w == 0;
        if (!same) lookup_shadow_fail("go_to refresh (the marked ants against every ant)", 0);
    } else if (sample) {
        for (const auto& up : ants_) {
            if (up && !occ_in_step(*up)) lookup_shadow_fail("go_to refresh (an ant left out of step)", up->id);
        }
    }
#endif
}

#ifdef ANTS_SIM_LOOKUP_SHADOW
// Every ant that is out of step has its mark (a mark on an ant that is in step is harmless: the visit finds nothing to do)
void SimulationEngineImpl::occ_dirty_verify() const {
    for (size_t i = 0; i < ants_.size(); ++i) {
        if (ants_[i] && !occ_in_step(*ants_[i]) && !occ_marked(static_cast<uint32_t>(i))) lookup_shadow_fail("occ marks (an ant out of step is not marked)", ants_[i]->id);
    }
}
#endif

// FUN_0100f421 (TileInfo mask 1): the tile's occupant, or nullptr.
AntUnit* SimulationEngineImpl::occupant_at(TileCoord t) {
    OccCell* c = occ_cell(t);
    if (!c) return nullptr;
    AntUnit* a = nullptr;
    if (c->multi) a = occ_scan(t, nullptr, nullptr);
    else if (c->ant >= 0) a = find_unit(static_cast<uint32_t>(c->ant));
    // Remake: ants that died or went underground since the last movement tick are unregistered lazily.
    if (a && (!a->is_alive())) return nullptr;
    return a;
}

// FUN_0100f3ca: any ant registered on the tile.
bool SimulationEngineImpl::tile_occupied(TileCoord t) {
    return occupant_at(t) != nullptr;
}

// ------------------------------------------------------------------------------------------------
// The ants that a blast throws
// ------------------------------------------------------------------------------------------------

// The scan over the whole table that blast() made: the ants that are registered on `at` and not removed, in the order of the table.
void SimulationEngineImpl::blast_victims_scan(TileCoord at, std::vector<AntUnit*>& out) {
    out.clear();
    for (auto& up : ants_) {
        AntUnit* x = up.get();
        if (x && !x->removed && x->occ_tile == at) out.push_back(x);
    }
}

// The same from the tile's list, put in the order of the table (the places of the ants, which are all different) because the throws go one after the other and the
// landing of one decides the next (`excl`, the PRNG).
void SimulationEngineImpl::blast_victims(TileCoord at, std::vector<AntUnit*>& out) {
#ifdef ANTS_SIM_LOOKUP_SHADOW
    if (lookup_shadow_linear_only()) return blast_victims_scan(at, out);
#endif
    const size_t cell = occ_cell_index(at);
    if (cell == kNoCell) return blast_victims_scan(at, out);           // off the grid there is no list
    std::vector<uint32_t> slots;
    for (uint32_t slot = tile_ants_.first(cell); slot != TileAntIndex::kNone; slot = tile_ants_.next(slot)) {
        const AntUnit* x = ants_[slot].get();
        if (x && !x->removed) slots.push_back(slot);
    }
    std::sort(slots.begin(), slots.end());
    out.clear();
    for (const uint32_t slot : slots) out.push_back(ants_[slot].get());
#ifdef ANTS_SIM_LOOKUP_SHADOW
    ++lookup_shadow_blast_checks();                                     // (a blast is rare: every one is compared)
    lookup_shadow_blast_ants() += out.size();
    std::vector<AntUnit*> scanned;
    blast_victims_scan(at, scanned);
    if (scanned != out) lookup_shadow_fail("blast (ants of the tile)", static_cast<uint32_t>(cell));
#endif
}

#ifdef ANTS_SIM_LOOKUP_SHADOW
// Every list against the ants: the lists hold exactly the ants whose occ_tile is a cell of the grid, each in the list of its own cell and once, the links agree, and the
// lists are as many as the cells of the grid (the occupancy grid and the lists change together).
void SimulationEngineImpl::tile_ants_verify() const {
    ++lookup_shadow_list_checks();
    if (tile_ants_.cells() != occ_.size()) lookup_shadow_fail("tile lists (cells)", static_cast<uint32_t>(tile_ants_.cells()));
    std::vector<uint8_t> seen(ants_.size(), 0);
    for (size_t cell = 0; cell < tile_ants_.cells(); ++cell) {
        uint32_t before = TileAntIndex::kNone;
        for (uint32_t slot = tile_ants_.first(cell); slot != TileAntIndex::kNone; before = slot, slot = tile_ants_.next(slot)) {
            if (slot >= ants_.size() || slot >= tile_ants_.slots() || seen[slot] || !ants_[slot] || tile_ants_.prev(slot) != before ||
                occ_cell_index(ants_[slot]->occ_tile) != cell) {
                lookup_shadow_fail("tile lists (an entry)", slot < ants_.size() && ants_[slot] ? ants_[slot]->id : 0u);
            }
            seen[slot] = 1;
        }
    }
    for (size_t i = 0; i < ants_.size(); ++i) {
        if (ants_[i] && !seen[i] && occ_cell_index(ants_[i]->occ_tile) != kNoCell) lookup_shadow_fail("tile lists (a missing ant)", ants_[i]->id);
    }
}
#endif

} // namespace ants::sim
