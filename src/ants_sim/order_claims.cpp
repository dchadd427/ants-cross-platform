// The orders that claim a tile, and the lists that answer "does a team-mate claim this tile?" without a scan of every ant (docs/ARCHITECTURE.md, "Lookups in the tick loop").
//
// An ant claims a tile while it is not removed and its order (orig_order, +0xa8) is Move or Home to that tile (orig_order_tile, +0xac): can_enter refuses a tile that a team-mate
// claims (rule R6, FUN_0100f6xx family; flag kCheckClaims). The scan visited every ant of the table for each question; the lists hold exactly the claimants of each tile.
//
// WHO WRITES THEM. Every write of orig_order and orig_order_tile in the simulation is a call of set_order_kind or set_order_tile (and AntUnit::clear_path, which writes both, is called
// through clear_path_of); each ends in claim_note, which puts the ant in the list of the tile it claims, or in none. A removal (remove_ant) and add_ant call claim_note too. So the lists
// are exact after every operation of the simulation, with no healing in the query. tests/scripts/test_order_writers.py fails for a direct write anywhere in src/ants_sim, so that a
// new writer cannot be left out. What no writer can follow is a write FROM OUTSIDE (a test hook that pokes an ant through get_unit(); no production code does): the lists are used
// only inside a tick, and movement_tick starts by putting right every ant (claims_catch_up_all), so a poke is put right before the first question; outside a tick can_enter scans.

#include "sim_engine_impl.hpp"

#ifdef ANTS_SIM_LOOKUP_SHADOW
#include <vector>
#endif

namespace ants::sim {

void SimulationEngineImpl::set_order_kind(AntUnit& a, uint8_t kind) {
    a.orig_order = kind;
    claim_note(a);
}

void SimulationEngineImpl::set_order_tile(AntUnit& a, TileCoord tile) {
    a.orig_order_tile = tile;
    claim_note(a);
}

void SimulationEngineImpl::clear_path_of(AntUnit& a) {
    a.clear_path();
    claim_note(a);
}

uint32_t SimulationEngineImpl::claim_key_of(const AntUnit& a) const noexcept {
    if (a.removed || (a.orig_order != AntUnit::kOrderMove && a.orig_order != AntUnit::kOrderHome)) return TileAntIndex::kNone;
    const size_t cell = occ_cell_index(a.orig_order_tile);
    return cell == kNoCell ? TileAntIndex::kNone : static_cast<uint32_t>(cell);
}

bool SimulationEngineImpl::claim_note(const AntUnit& a) { return claim_note(a, ant_slot(a)); }

bool SimulationEngineImpl::claim_note(const AntUnit& a, uint32_t slot) {
    if (slot == TileAntIndex::kNone) return false;                  // (not in the table: nobody asks about it)
    if (claim_ants_.cells() != occ_.size()) {                       // a grid of another size (or no lists yet): all of them again, this ant with the rest
        claim_rebuild();
        return true;
    }
    if (claim_cell_.size() <= slot) claim_cell_.resize(static_cast<size_t>(slot) + 1u, TileAntIndex::kNone);
    const uint32_t want = claim_key_of(a);
    const uint32_t have = claim_cell_[slot];
    if (want == have) return false;
    if (have != TileAntIndex::kNone) claim_ants_.unlink(have, slot);
    if (want != TileAntIndex::kNone) claim_ants_.link(want, slot);
    claim_cell_[slot] = want;
    return true;
}

void SimulationEngineImpl::claim_rebuild() {
    claim_ants_.reset(occ_.size());
    claim_cell_.assign(ants_.size(), TileAntIndex::kNone);
    for (size_t slot = 0; slot < ants_.size(); ++slot) {
        if (!ants_[slot]) continue;
        const uint32_t key = claim_key_of(*ants_[slot]);
        if (key == TileAntIndex::kNone) continue;
        claim_ants_.link(key, static_cast<uint32_t>(slot));
        claim_cell_[slot] = key;
    }
}

// The start of a tick: every ant is looked at once. A product build puts right what it finds; the shadow build stops at it, because in a match every writer has told the lists (the one
// test that writes orders from outside on purpose counts them, lookup_shadow_count_stale_events).
void SimulationEngineImpl::claims_catch_up_all() {
    if (claim_ants_.cells() != occ_.size()) claim_rebuild();
    for (size_t slot = 0; slot < ants_.size(); ++slot) {
        if (!ants_[slot]) continue;
#ifdef ANTS_SIM_LOOKUP_SHADOW
        ++lookup_shadow_claim_catch_ups();
        if (claim_note(*ants_[slot], static_cast<uint32_t>(slot))) lookup_shadow_stale_event(lookup_shadow_claim_catch_up_changes(), "claims (an order the lists were not told of)", ants_[slot]->id);
#else
        claim_note(*ants_[slot], static_cast<uint32_t>(slot));
#endif
    }
}

// The scan over every ant that can_enter made for each question (rule R6): another live ant of the team with a Move or Home order to t.
bool SimulationEngineImpl::claimed_by_mate_scan(const AntUnit& a, TileCoord t) const {
    for (const auto& up : ants_) {
        const AntUnit* x = up.get();
        if (!x || x->removed || x == &a || x->player_id != a.player_id) continue;
        if ((x->orig_order == AntUnit::kOrderMove || x->orig_order == AntUnit::kOrderHome) && x->orig_order_tile == t) return true;
    }
    return false;
}

bool SimulationEngineImpl::claimed_by_mate(const AntUnit& a, TileCoord t) {
#ifdef ANTS_SIM_LOOKUP_SHADOW
    if (lookup_shadow_linear_only()) return claimed_by_mate_scan(a, t);
#endif
    const size_t cell = occ_cell_index(t);
    if (!occ_in_tick_ || cell == kNoCell || claim_ants_.cells() != occ_.size()) return claimed_by_mate_scan(a, t);   // outside a tick, or off the grid: the scan
    bool found = false;
    for (uint32_t slot = claim_ants_.first(cell); slot != TileAntIndex::kNone; slot = claim_ants_.next(slot)) {
        const AntUnit* x = slot < ants_.size() ? ants_[slot].get() : nullptr;
        if (x && x != &a && x->player_id == a.player_id && x->orig_order_tile == t) {     // (the list holds exactly the claimants of the tile: the team is the only question left)
            found = true;
            break;
        }
    }
#ifdef ANTS_SIM_LOOKUP_SHADOW
    ++lookup_shadow_claim_calls();
    if (found) ++lookup_shadow_claim_hits();
    static thread_local uint64_t questions = 0;
    if (ants_.size() <= 2048 || (++questions & 15u) == 0) {          // (every question up to 2,048 ants: there are far fewer than other lookups' calls; every 16th above)
        ++lookup_shadow_claim_checks();
        if (found != claimed_by_mate_scan(a, t)) lookup_shadow_fail("claimed_by_mate", a.id);
        claims_verify();                                             // (and the whole lists against the orders: an ant left in a list that its order has left stops here, not only when it changes an answer)
    }
#endif
    return found;
}

#ifdef ANTS_SIM_LOOKUP_SHADOW
// Every list against the ants: each ant is in the list of the cell that its order says (claim_key_of) and in no other, the links agree, and the lists are as many as the cells of the grid
void SimulationEngineImpl::claims_verify() const {
    ++lookup_shadow_claim_verifies();
    if (claim_ants_.cells() != occ_.size()) lookup_shadow_fail("claim lists (cells)", static_cast<uint32_t>(claim_ants_.cells()));
    std::vector<uint8_t> seen(ants_.size(), 0);
    for (size_t cell = 0; cell < claim_ants_.cells(); ++cell) {
        uint32_t before = TileAntIndex::kNone;
        for (uint32_t slot = claim_ants_.first(cell); slot != TileAntIndex::kNone; before = slot, slot = claim_ants_.next(slot)) {
            if (slot >= ants_.size() || slot >= claim_ants_.slots() || seen[slot] || !ants_[slot] || claim_ants_.prev(slot) != before || claim_key_of(*ants_[slot]) != cell ||
                slot >= claim_cell_.size() || claim_cell_[slot] != cell) {
                lookup_shadow_fail("claim lists (an entry)", slot < ants_.size() && ants_[slot] ? ants_[slot]->id : 0u);
            }
            seen[slot] = 1;
        }
    }
    for (size_t i = 0; i < ants_.size(); ++i) {
        if (!ants_[i]) continue;
        const bool listed = seen[i] != 0;
        if (listed != (claim_key_of(*ants_[i]) != TileAntIndex::kNone) || (!listed && i < claim_cell_.size() && claim_cell_[i] != TileAntIndex::kNone)) lookup_shadow_fail("claim lists (a missing or extra ant)", ants_[i]->id);
    }
}
#endif

} // namespace ants::sim
