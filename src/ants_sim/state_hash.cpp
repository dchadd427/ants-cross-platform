// SimulationEngine::state_hash: a hash of the deterministic gameplay state, for lock-step desync detection.
//
// Lock-step peers apply the same commands at the same ticks; if their simulations ever diverge the players see different games. Every 20 ticks
// the peers exchange this hash; a mismatch freezes the match and the per-subsystem parts say where the states differ.
//
// What is hashed: everything that can influence a later tick: the two PRNG states (the cosmetic one picks the death clip, whose length decides
// when an ant leaves its tile), the clocks, the match, hatching and CHECKGO state, every ant (all fields, in table order including the entries of
// removed ants), the occupancy grid, every cell of the map, the hills, the food objects, the scores, statistics, eggs, alliances and pending
// invitations, the queued path requests of the path managers, the flower droppers.
// What is not: what only presents the simulation (audio and news queues, visual effects, score bubbles, battle clouds, the locomotion trace),
// the per-viewer fog of war and the viewing player, and the players' display names.
//
// Every field is fed in a fixed width, little endian order (never the raw bytes of a struct, which would include padding), so the value is the
// same on every platform. FNV-1a 64 is not cryptographic; it only has to notice honest divergence.
#include "ants_sim/sim_engine.hpp"
#include "sim_engine_impl.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

namespace ants::sim {

namespace {

class Fnv {
public:
    void byte(uint8_t b) noexcept {
        h_ ^= b;
        h_ *= 0x100000001b3ULL;
    }
    void u8(uint8_t v) noexcept { byte(v); }
    void boolean(bool v) noexcept { byte(v ? 1u : 0u); }
    void u16(uint16_t v) noexcept {
        byte(static_cast<uint8_t>(v & 0xFFu));
        byte(static_cast<uint8_t>((v >> 8) & 0xFFu));
    }
    void u32(uint32_t v) noexcept {
        for (int i = 0; i < 4; ++i) byte(static_cast<uint8_t>((v >> (8 * i)) & 0xFFu));
    }
    void u64(uint64_t v) noexcept {
        for (int i = 0; i < 8; ++i) byte(static_cast<uint8_t>((v >> (8 * i)) & 0xFFu));
    }
    void i16(int16_t v) noexcept { u16(static_cast<uint16_t>(v)); }
    void i32(int32_t v) noexcept { u32(static_cast<uint32_t>(v)); }
    void i64(int64_t v) noexcept { u64(static_cast<uint64_t>(v)); }
    void f64(double d) noexcept {
        uint64_t bits = 0;
        std::memcpy(&bits, &d, sizeof(bits));
        u64(bits);
    }
    void size(size_t n) noexcept { u64(static_cast<uint64_t>(n)); }
    void tile(const TileCoord& t) noexcept {
        i32(t.x);
        i32(t.y);
    }
    uint64_t value() const noexcept { return h_; }

private:
    uint64_t h_{0xcbf29ce484222325ULL};
};

void hash_cell(Fnv& h, const TileCell& c) {
    h.u16(c.terrain_id);
    h.u8(c.terrain_type);
    h.u8(static_cast<uint8_t>(c.surface_type));
    h.u16(c.flags);
    h.u16(c.interactive_id);
    h.u8(c.interactive_owner);
    h.u32(c.timer_ticks);
    h.i16(c.anchor_x);
    h.i16(c.anchor_y);
    h.boolean(c.is_food);
    h.boolean(c.is_mud);
    h.boolean(c.is_powerup);
    h.u8(c.powerup_type);
    h.boolean(c.is_obstacle_overlay);
    h.boolean(c.is_thief_only);
    h.boolean(c.is_corridor_team_locked);
    h.boolean(c.is_base_hole);
    h.u8(c.base_owner_team);
    h.i32(c.occupant_ant_id);
    h.boolean(c.static_solid);
}

void hash_ant(Fnv& h, const AntUnit& a) {
    h.u32(a.id);
    h.u8(a.player_id);
    h.u8(static_cast<uint8_t>(a.team));
    h.u8(a.target_team_id);
    h.u8(static_cast<uint8_t>(a.type));
    h.u8(static_cast<uint8_t>(a.state));
    h.u8(static_cast<uint8_t>(a.death_status));
    h.u16(a.hp);
    h.u16(a.max_hp);
    h.tile(a.pos);
    h.i32(a.pixel_x);
    h.i32(a.pixel_y);
    h.u8(static_cast<uint8_t>(a.facing));
    h.i32(a.fx_x);
    h.i32(a.fx_y);
    h.u8(a.holding);
    h.u16(a.carried_food);
    h.u16(a.carried_points);
    h.u16(a.anim_subitem);
    h.u16(a.anim_tick);
    h.boolean(a.is_on_mud);
    h.boolean(a.was_in_water);
    h.boolean(a.in_water);
    h.size(a.waypoints.size());
    for (const TileCoord& w : a.waypoints) h.tile(w);
    h.size(a.current_waypoint_idx);
    h.tile(a.final_dest);
    h.tile(a.harvest_origin);
    h.boolean(a.is_thief_steal);
    h.tile(a.ability_target);
    h.boolean(a.allow_friendly_bomb);
    // combat
    h.boolean(a.engaged);
    h.boolean(a.frozen);
    h.u32(a.pending_victim);
    h.u8(a.pending_range);
    h.u8(a.pending_dir);
    h.tile(a.pending_tile);
    h.u8(a.killer_team);
    h.u32(a.last_order_ms);
    h.boolean(a.knock_flag);
    h.tile(a.flight_tile);
    h.boolean(a.removed);
    h.u32(a.burn_end_ms);
    h.boolean(a.auto_engage);
    h.u8(a.ae_order);
    h.tile(a.ae_target);
    h.u8(a.ae_home_state);
    h.u32(a.combevt_due_ms);
    // locomotion (the clip is identified by its table index; the frame pointer is not hashed)
    h.u16(a.loco.clip.chd_index);
    h.u16(a.loco.clip.count);
    h.boolean(a.loco.clip.mirrored);
    h.u8(a.loco.clip.flags);
    h.boolean(a.loco.clip.valid());
    h.u16(a.loco.cursor);
    h.u32(a.loco.next_ms);
    h.u8(a.loco.dir);
    h.u32(a.loco.serial);
    h.u16(a.loco.evt5_ms);
    h.u32(a.loco.sound_mask);
    h.u8(a.loco_action);
    h.boolean(a.dive_flag);
    h.boolean(a.pause_active);
    h.u32(a.pause_fire_ms);
    h.u8(a.pause_saved_action);
    h.u8(a.pause_saved_dir);
    h.u8(a.orig_order);
    h.tile(a.orig_order_tile);
    h.u8(a.home_state);
    h.u8(a.home_priority);
    h.u32(a.home_time_ms);
    h.u32(static_cast<uint32_t>(a.raid_amount));
    h.u8(a.orig_target_team);
    h.u32(a.orig_target_ant);
    h.tile(a.orig_special_tile);
    h.u16(a.orig_b4);
    h.i32(a.orig_food_id);
    h.tile(a.orig_food_tile);
    h.u16(a.harvest_amount);
    h.u32(a.move_serial);
    h.tile(a.occ_tile);
    h.boolean(a.arrived_this_tick);
}

}  // namespace

StateHash SimulationEngine::state_hash() const {
    const SimulationEngineImpl& e = *impl_;
    StateHash out;

    // ---- engine: PRNGs, clocks, match, hatching, CHECKGO ----
    {
        Fnv h;
        h.u32(e.prng_.get_state());
        h.u32(e.cosmetic_prng_.get_state());
        h.u8(static_cast<uint8_t>(e.match_state_));
        h.u8(e.roster_mask_);
        h.u8(e.dropped_mask_);
        h.u32(e.quitter_);
        h.u64(e.current_tick_);
        h.u32(e.match_limit_ms_);
        h.i64(e.match_clock_ms_);
        h.i64(e.checkgo_next_ms_);
        h.u32(e.checkgo_stage_);
        h.u32(e.checkgo_threshold_ms_);
        h.u32(e.next_ant_id_);
        h.u32(e.hatch_delay_ticks_);
        for (const auto& hs : e.hatch_) {
            h.boolean(hs.active);
            h.u32(hs.due_ms);
            h.u8(static_cast<uint8_t>(hs.type));
        }
        h.u32(e.anim_clock_ms_);
        h.u32(e.now_ms_);
        h.u32(e.anthillq_next_ms_);
        h.u32(e.fdtask_next_ms_);
        for (int64_t t : e.last_attacked_ms_) h.i64(t);
        out.engine = h.value();
    }

    // ---- players: statistics, eggs, alliances, invitations ----
    {
        Fnv h;
        for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
            const PlayerMatchStats& s = e.stats_.get_player_stats(p);
            h.i32(s.score);
            h.u32(s.friendly_lost);
            h.u32(s.enemy_killed);
            h.u32(s.ants_hatched);
            h.u32(s.new_hatched);
            h.u32(s.food_deposited);
            h.u32(s.food_stolen);
            h.u32(s.food_lost);
            h.u32(s.bombs_planted);
            h.u32(s.bombs_defused);
            h.u32(s.fires_lit);
            h.u32(s.bridges_built);
            h.u32(e.stats_.get_egg_count(p));
            h.u8(e.stats_.get_alliance(p));
            const AllianceInvite& inv = e.stats_.get_pending_invite(p);
            h.u8(inv.from_player);
            h.u8(inv.to_player);
            h.u32(inv.expiry_tick);
            h.boolean(inv.active);
        }
        out.players = h.value();
    }

    // ---- grid: every cell, the hills ----
    {
        Fnv h;
        h.u32(e.grid_.width());
        h.u32(e.grid_.height());
        h.boolean(e.grid_.exact_solid_bits());
        h.size(e.grid_.cells().size());
        for (const TileCell& c : e.grid_.cells()) hash_cell(h, c);
        h.size(e.grid_.anthills().size());
        for (const auto& a : e.grid_.anthills()) {
            h.u16(a.tile_id);
            h.u16(a.y);
            h.u16(a.x);
            h.u8(a.team_id);
        }
        out.grid = h.value();
    }

    // ---- food objects ----
    {
        Fnv h;
        h.size(e.grid_.food_objects().size());
        for (const FoodObject& f : e.grid_.food_objects()) {
            h.u16(f.row);
            h.u16(f.col);
            h.u16(f.units);
            h.u16(f.value);
            h.u16(f.remaining);
            h.size(f.thresholds.size());
            for (uint16_t t : f.thresholds) h.u16(t);
            h.size(f.stage_tiles.size());
            for (uint16_t t : f.stage_tiles) h.u16(t);
        }
        out.food = h.value();
    }

    // ---- ants and the occupancy grid ----
    {
        Fnv h;
        h.size(e.ants_.size());
        for (const auto& up : e.ants_) {
            if (!up) {
                h.u8(0);
                continue;
            }
            h.u8(1);
            hash_ant(h, *up);
        }
        h.size(e.occ_.size());
        for (const auto& c : e.occ_) {
            h.i32(c.ant);
            h.boolean(c.multi);
        }
        out.ants = h.value();
    }

    // ---- path managers and the request serials ----
    {
        Fnv h;
        for (const PathManager& pm : e.path_managers_) {
            h.size(pm.pending());
            pm.for_each_request([&h](uint32_t ant_id, TileCoord start, TileCoord goal, bool has_grid, bool finished) {
                h.u32(ant_id);
                h.tile(start);
                h.tile(goal);
                h.boolean(has_grid);
                h.boolean(finished);
            });
        }
        std::vector<std::pair<uint32_t, uint32_t>> serials(e.path_request_serial_.begin(), e.path_request_serial_.end());
        std::sort(serials.begin(), serials.end());
        h.size(serials.size());
        for (const auto& s : serials) {
            h.u32(s.first);
            h.u32(s.second);
        }
        out.paths = h.value();
    }

    // ---- flower droppers ----
    {
        Fnv h;
        h.size(e.flower_droppers_.size());
        for (const auto& fd : e.flower_droppers_) {
            h.tile(fd.pos);
            h.tile(fd.drop_pos);
            h.u32(fd.interval_s);
            h.boolean(fd.stamped);
            h.u32(fd.last_ms);
            h.boolean(fd.is_dropping);
            h.boolean(fd.sound_played);
            h.u32(fd.drop_start_ms);
            h.u8(fd.powerup_type);
            for (double p : fd.probabilities) h.f64(p);
        }
        out.droppers = h.value();
    }

    Fnv total;
    total.u64(out.engine);
    total.u64(out.players);
    total.u64(out.grid);
    total.u64(out.food);
    total.u64(out.ants);
    total.u64(out.paths);
    total.u64(out.droppers);
    out.total = total.value();
    return out;
}

}  // namespace ants::sim
