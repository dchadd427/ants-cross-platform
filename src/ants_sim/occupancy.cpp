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

// FUN_0100f17f: move the ant's registration from its old tile to nt.
void SimulationEngineImpl::occ_move(AntUnit& a, TileCoord nt) {
    const TileCoord old = a.occ_tile;
    const uint32_t slot = ant_slot(a);
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
        if (!up) continue;
        AntUnit& a = *up;
        if (!a.is_alive()) {
            if (a.occ_tile.x >= 0) occ_move(a, TileCoord{-1, -1});
        } else if (a.occ_tile != pixel_tile(a)) {
            occ_move(a, pixel_tile(a));
        }
    }
}

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
