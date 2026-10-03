#pragma once

// Private engine implementation state shared by the simulation translation units
// (sim_engine.cpp, movement_system.cpp). Not part of the public ants_sim API.

#include "ants_sim/sim_engine.hpp"
#include "ants_sim/path_planner.hpp"
#include "ants_sim/movement_tables.hpp"
#include "ants_sim/effect_specs.hpp"
#include <unordered_map>
#include <memory>
#include <algorithm>
#include <cstddef>
#include <type_traits>
#include <vector>
#include <array>

namespace ants::sim {

// ---- Value semantics of the engine (SimulationEngine's copy, docs/NETWORK_PORT.md "Prediction of one's own orders") ----------------------------------------------
// SimulationEngineImpl is copied member by member, by the copy operations that the compiler writes. Every member therefore copies itself correctly or the class does not
// compile, and a member that is added later cannot be forgotten by a hand-written copy. Three members need more than the default, and each is a type of its own:
//   * the ants (AntPtr): the table owns its ants through pointers, so that an ant keeps its address while the table grows; a copy must clone them;
//   * the path managers (PathManager, path_planner.hpp): a search points into the pool of its manager;
//   * the cached world state (WorldStateCache, StaleFlag): derived from the rest of the engine, so a copy does not take it along.

/// An owning pointer to an ant of the table `ants_`. The ant lives at a stable address (the engine hands out references to ants and the table grows), and a COPY of the
/// pointer clones the ant: a copy of the engine shares no ant with the original. An assignment into a pointer that already owns an ant copies into that ant (its waypoint list
/// keeps its memory), so that a second engine that is rebuilt from the first every frame allocates nothing.
class AntPtr {
public:
    AntPtr() noexcept = default;
    AntPtr(std::unique_ptr<AntUnit> ant) noexcept : ant_(std::move(ant)) {}
    AntPtr(const AntPtr& other) : ant_(other.ant_ ? std::make_unique<AntUnit>(*other.ant_) : nullptr) {}
    AntPtr(AntPtr&&) noexcept = default;
    AntPtr& operator=(const AntPtr& other) {
        if (this == &other) return *this;
        if (!other.ant_) ant_.reset();
        else if (ant_) *ant_ = *other.ant_;
        else ant_ = std::make_unique<AntUnit>(*other.ant_);
        return *this;
    }
    AntPtr& operator=(AntPtr&&) noexcept = default;

    AntUnit* get() const noexcept { return ant_.get(); }
    AntUnit* operator->() const noexcept { return ant_.get(); }
    AntUnit& operator*() const noexcept { return *ant_; }
    explicit operator bool() const noexcept { return ant_ != nullptr; }
    friend bool operator==(const AntPtr& a, std::nullptr_t) noexcept { return a.ant_ == nullptr; }
    friend bool operator!=(const AntPtr& a, std::nullptr_t) noexcept { return a.ant_ != nullptr; }
    friend bool operator==(std::nullptr_t, const AntPtr& a) noexcept { return a.ant_ == nullptr; }
    friend bool operator!=(std::nullptr_t, const AntPtr& a) noexcept { return a.ant_ != nullptr; }

private:
    std::unique_ptr<AntUnit> ant_;
};
// The table of ants grows by push_back: it must MOVE its ants when it reallocates (a copy would clone every ant and move it to a new address)
static_assert(std::is_nothrow_move_constructible<AntPtr>::value, "a growing table of ants moves its ants");

/// The WorldState that get_world_state() builds on demand: derived from the rest of the engine, and as big as the map. A copy of the engine does not copy it (that would
/// move the whole map once more for a cache that is rebuilt when somebody asks): the copy starts with an empty one, flagged stale (StaleFlag), and an assignment into an
/// engine leaves the cache that the target holds, memory included, and flags it stale.
struct WorldStateCache : WorldState {
    WorldStateCache() = default;
    WorldStateCache(const WorldStateCache&) : WorldState() {}
    WorldStateCache& operator=(const WorldStateCache&) { return *this; }
};

/// "The cached world state is out of date": true from the start, and true again in a copy of an engine and in an engine that was assigned to (see WorldStateCache).
class StaleFlag {
public:
    StaleFlag() noexcept = default;
    StaleFlag(const StaleFlag&) noexcept {}
    StaleFlag& operator=(const StaleFlag&) noexcept { stale_ = true; return *this; }
    StaleFlag& operator=(bool stale) noexcept { stale_ = stale; return *this; }
    operator bool() const noexcept { return stale_; }

private:
    bool stale_{true};
};

class SimulationEngineImpl {
public:
    PRNG prng_{1u};
    PRNG cosmetic_prng_{0x5EEDu}; // visual-only choices (death animation); never feeds gameplay
    Grid grid_;
    MatchStatsManager stats_;
    MatchState match_state_{MatchState::NotStarted};
    uint8_t roster_mask_{0x0F};             // bit p: team p takes part (a team without a player is NULL in the original's team table: no hill, ants or eggs)
    uint8_t dropped_mask_{0};               // bit p: team p dropped out of the match (team +0x64, FUN_0100d03b)
    uint16_t quitter_{NO_QUITTER};          // the team whose quit (FUN_0101453f, one other side left) ended the match: the word of the game-over message

    uint64_t current_tick_{0};
    uint32_t match_time_remaining_ms_{0};   // what the clock shows: max(0, match_clock_ms_)
    // The match clock and the CHECKGO task (Ants.exe 0x1024839, period 200 ms, first run at once): GetClock (FUN_0100fa50) is the
    // limit minus the time played and goes below 0 when the limit is over; only CHECKGO ends the match, at its first run with a
    // clock below 0 (0 - 200 ms after 0:00).
    uint32_t match_limit_ms_{0};            // map byte +0x6e * 60000
    int64_t  match_clock_ms_{0};
    int64_t  checkgo_next_ms_{0};           // clock value at which CHECKGO runs next
    uint32_t checkgo_stage_{0};             // task +0x2c: 0 one minute, 1 thirty seconds, then the 11 countdown steps
    uint32_t checkgo_threshold_ms_{61000};  // task +0x30: the clock must be below it for the next warning
    void checkgo_poll();
    /// The elimination rules of CHECKGO as the machine of team `local` runs them (Ants.exe 0x1024921 - 0x1024a66, docs 5.47): nobody has anything
    /// left, or every team that has is `local` or its ally and the alliance of `local` has the strictly best combined score.
    bool checkgo_ends_for(uint8_t local) const;
    /// Every machine of the original decides for its own team; the first that decides ends the match for all, so the shared simulation asks for each
    /// team in the match that has not dropped out.
    bool checkgo_end_rules() const;
    /// A team is alive while it has an egg, a hatch running or an ant that is not yet removed (0x1024934 - 0x10249bb)
    bool team_alive(uint8_t team) const;
    /// The drop-out win test (0x100d172, docs 5.47): after a drop, the team `local` has no live enemy when every other team that has not dropped is its ally
    bool drop_leaves_winner() const;
    /// FUN_0100c5b1: how many sides remain when `team` leaves: every other team that has not dropped, an alliance counted once, the team's own ally counted
    uint32_t other_sides(uint8_t team) const;
    MatchResult make_match_result() const;
    std::array<std::string, MAX_PLAYERS> player_names_{};
    std::string player_display_name(uint8_t p) const;    // the set name, else the colour word
    std::string player_colour_name(uint8_t p) const;     // strings 100..103; the remake's player 0..3 are green, red, blue, black
    void set_match_clock(int64_t ms);          // jumps the clock (start, test hook): CHECKGO runs at its next tick
    void set_match_clock_running(int64_t ms);  // the clock advances with the game

    std::vector<AntPtr> ants_;
    uint32_t next_ant_id_{1};

    std::vector<AudioEvent> audio_queue_;
    std::vector<NewsEvent>  news_queue_;
    // Egg hatching (Ants.exe HATCHTSK): 8000 ms after the click the newborn worker appears at the hill entrance.
    uint32_t hatch_delay_ticks_{160};
    struct HatchState {
        bool     active{false};        // player +0x0c "hatching"
        uint32_t due_ms{0};            // animation-clock time of the next attempt
        AntType  type{AntType::Worker};
    };
    std::array<HatchState, MAX_PLAYERS> hatch_{};
    std::vector<VisualEffect> active_effects_;
    std::vector<ScoreBubble> score_bubbles_;
    // FUN_0101a2aa Cloud54: the looping dust ball ("battle") on the tile of a foreign pile-up; see effect_specs.hpp
    struct BattleCloud {
        TileCoord tile{0, 0};
        uint32_t  created_ms{0};       // +0x50
        uint32_t  clear_since_ms{0};   // +0x4c: 0 while the crowd is there, else the time it was first seen gone
        uint32_t  next_sound_ms{0};    // the loop start that plays sound 3 next
        uint32_t  audio_owner{0};      // the cloud sprite owns its sounds: they stop with it
    };
    std::vector<BattleCloud> battle_clouds_;
    void spawn_battle_cloud(TileCoord tile);
    void tick_battle_clouds();

    // Fog of War State
    bool fog_of_war_enabled_{false};
    std::vector<uint8_t> fog_revealed_{};
    uint8_t viewing_player_id_{0};

    void reveal_fog_tile(int32_t x, int32_t y) {
        if (x < 0 || y < 0 || static_cast<uint32_t>(x) >= grid_.width() || static_cast<uint32_t>(y) >= grid_.height()) return;
        size_t idx = static_cast<size_t>(y) * grid_.width() + static_cast<size_t>(x);
        if (idx < fog_revealed_.size() && fog_revealed_[idx] == 0) {
            fog_revealed_[idx] = 1;
            world_state_dirty_ = true;
        }
    }

    void reveal_fog_box(int32_t min_x, int32_t min_y, int32_t max_x, int32_t max_y) {
        for (int32_t y = min_y; y <= max_y; ++y) {
            for (int32_t x = min_x; x <= max_x; ++x) {
                reveal_fog_tile(x, y);
            }
        }
    }

    void update_fog_of_war() {
        if (!fog_of_war_enabled_) return;
        size_t total_tiles = static_cast<size_t>(grid_.width() * grid_.height());
        if (fog_revealed_.size() != total_tiles) {
            fog_revealed_.assign(total_tiles, 0);
            world_state_dirty_ = true;
        }

        // The original reveals only from an ant's position update (FUN_0101a93a calls FUN_01006af4 from SetPos, FUN_0101a9f8): a 13 x 13 square (+-6) around the ant's pixel
        // tile, for the viewer's own ants and its teammate's (the ally at that moment), sticky; there is no reveal around a hill and nothing is revealed when an alliance forms
        // (an allied ant reveals when it moves). Every ant remembers the tile of its last position update whoever it belongs to, so that an ant that has been standing
        // reveals nothing new when its team becomes the viewer's ally.
        for (const auto& ant : ants_) {
            if (!ant || !ant->is_alive()) continue;
            const int32_t tx = ant->pixel_x / 32;
            const int32_t ty = ant->pixel_y / 32;
            if (ant->fog_tile_x == tx && ant->fog_tile_y == ty) continue;
            ant->fog_tile_x = tx;
            ant->fog_tile_y = ty;
            if (ant->player_id == viewing_player_id_ || stats_.are_allies(viewing_player_id_, ant->player_id)) {
                reveal_fog_box(tx - 6, ty - 6, tx + 6, ty + 6);
            }
        }
    }

    // Flower droppers (FDTASK, Ants.exe 0x100fc0d): a task that polls all droppers every 3000 ms of the list scheduler. The first poll only stamps
    // the record (+0x18); a later poll posts a drop when `now - stamp > interval * 1000` (strict, 0x100fcf4) and the drop tile holds nothing on
    // layer 2 (or a power-up) and no ant; the record is stamped again at the posting, the power-up type is drawn then (FUN_01009fd8), the drop
    // effect plays 820 ms (cue 62 at 100 ms) and its last frame sets the power-up tile without looking again (0x100fe50).
    struct FlowerDropper {
        TileCoord pos;
        TileCoord drop_pos;
        uint32_t interval_s{30};        // +0x10: seconds (0: the record is off, +0xc)
        bool stamped{false};            // +0x18 != 0
        uint32_t last_ms{0};            // +0x18: the first poll, then the last posting
        bool is_dropping{false};
        bool sound_played{false};
        uint32_t drop_start_ms{0};
        uint8_t powerup_type{0};        // 0: Bomber, 1: Combat, 2: Thief, 3: Swimmer, 4: Fire
        std::array<double, 5> probabilities{0.2, 0.2, 0.2, 0.2, 0.2};
    };
    std::vector<FlowerDropper> flower_droppers_;
    uint32_t fdtask_next_ms_{0};
    void flower_dropper_poll();
    void flower_dropper_sound(FlowerDropper& d);
    void flower_dropper_land(FlowerDropper& d);

    mutable WorldStateCache world_state_cache_;
    mutable StaleFlag       world_state_dirty_;
    // Effect sprite created at pixel (px, py); tile effects pass the tile top-left and y_key = row*32.
    // Sound ownership (tracked sounds): effect sprites get an owner id from here (ants use their own id)
    uint32_t next_audio_owner_{0x40000000u};
    uint32_t new_audio_owner() noexcept { return next_audio_owner_++; }
    void stop_audio_owner(uint32_t owner) {
        if (owner == 0) return;
        AudioEvent e;
        e.sound_id = 0xFFFFFFFFu;                   // never matches a sound id
        e.owner = owner;
        e.stop = true;
        audio_queue_.push_back(e);
    }

    void spawn_effect(const char* name, int32_t px, int32_t py, uint32_t duration_ms, int32_t y_key, bool fog_gated, uint32_t audio_owner = 0) {
        VisualEffect e;
        e.audio_owner = audio_owner;
        e.anim_name = name;
        e.px = px;
        e.py = py;
        e.duration_ms = duration_ms;
        e.total_frames = static_cast<uint16_t>((duration_ms + 49u) / 50u);
        e.y_key = y_key;
        e.fog_gated = fog_gated;
        active_effects_.push_back(std::move(e));
        world_state_dirty_ = true;
    }

    // Steps or restores the stage of a bridge tile without touching its collapse timer: in the original the timer is a separate task
    // entry (list at +0x4aec); the stage tiles change underneath it, only the completion of a build arms it (0x101ec84) and only the
    // completion of a demolish cancels it (0x101edfe); an interrupted demolish restores the completed bridge with its timer still running.
    void set_bridge_stage(int32_t x, int32_t y, uint16_t id, uint8_t owner) {
        auto& cell = grid_.get_cell_mut(static_cast<uint32_t>(x), static_cast<uint32_t>(y));
        const uint32_t timer = cell.timer_ticks;
        grid_.set_layer2(static_cast<uint32_t>(x), static_cast<uint32_t>(y), id, owner);
        cell.timer_ticks = timer;
    }

    // Fire walls and bridges only get their 180 s lifetime task while more than 180 s of match time remain
    // (Ants.exe 0x101e8e0 / 0x101ec84); later ones never burn out or collapse.
    void arm_structure_lifetime(int32_t x, int32_t y) {
        if (!grid_.in_bounds(x, y)) return;
        if (match_time_remaining_ms_ <= effect_spec::kStructureLifetimeMs) {
            grid_.get_cell_mut(static_cast<uint32_t>(x), static_cast<uint32_t>(y)).timer_ticks = 0;
        }
    }

    // Tile effect (explosion, smoke, splash): anchored at the tile top-left, sorted by row*32.
    void spawn_tile_effect(const char* name, int32_t col, int32_t row, uint32_t duration_ms, uint32_t audio_owner = 0) {
        spawn_effect(name, col * 32, row * 32, duration_ms, row * 32, true, audio_owner);
    }


    SimulationEngineImpl() = default;


    AntUnit* find_unit(uint32_t id) {
        for (auto& a : ants_) {
            if (a && a->id == id) return a.get();
        }
        return nullptr;
    }

    /// The ant type getter FUN_0100f9cb(ant, 0) (Ants.exe 0x100f9cb): the ant's own type (+0x54), and for an ant whose own type is 0 (Worker) the type of the level's
    /// default (block 3, level+0x70, through FUN_01021087: 0x100fa35 - 0x100fa47), which is 0 when the level has none. EVERY decision that the original takes through
    /// the getter uses this: the clip of an action (SetAction), the swimmer, the fire ant, the combat ant's punch and reflex, the special order, the HUD's selection
    /// type, the voices. What it reads from the field itself (+0x54) uses AntUnit::type: the pick-up and the drop of a power-up (0x1020d4b, 0x1021011), the thief's
    /// raid at an enemy hill (0x101fcb7), the combat ant's return after a failed attack step (0x101c6b0) and its hit box (0x1026a3d).
    AntType type_of(const AntUnit& a) const noexcept { return effective_type(a.type); }
    /// The same for an own type that is not stored in an ant (spawn parameters, the type of a hatch)
    AntType effective_type(AntType own) const noexcept {
        return own != AntType::Worker ? own : static_cast<AntType>(grid_.default_ant_type());
    }


    // FUN_01020de7: inside the map, no ant, not the layer-1 solid bit (FUN_0100cf0f), not water, layer 2 empty, not one of a live hill's special tiles
    bool is_valid_powerup_drop_tile(TileCoord adj) const noexcept {
        if (!grid_.in_bounds(adj)) return false;
        if (grid_.is_solid_object(adj)) return false;
        if (is_special_base_tile(adj)) return false;
        if (grid_.is_solid_obstacle(adj.x, adj.y)) return false;
        const auto& cell = grid_.get_cell(adj);
        if (!cell.is_passable()) return false;
        if (cell.terrain_type == TERRAIN_WATER || cell.surface_type == SurfaceType::Water) return false;
        if (!cell.is_empty_overlay() || cell.has_powerup() || cell.has_bomb() || cell.has_fire() || cell.has_food() || cell.has_lunchbox()) return false;

        for (const auto& ah : grid_.anthills()) {
            if (adj.x >= static_cast<int32_t>(ah.x) && adj.x < static_cast<int32_t>(ah.x) + 4 &&
                adj.y >= static_cast<int32_t>(ah.y) && adj.y < static_cast<int32_t>(ah.y) + 4) {
                return false;
            }
        }

        for (const auto& other : ants_) {
            if (!other || !other->is_alive()) continue;
            if (other->pos == adj) return false;
            TileCoord other_tile{
                (other->pixel_x >= 0) ? (other->pixel_x / 32) : ((other->pixel_x - 31) / 32),
                (other->pixel_y >= 0) ? (other->pixel_y / 32) : ((other->pixel_y - 31) / 32)
            };
            if (other_tile == adj) return false;
        }

        return true;
    }

    bool has_living_ant_at(TileCoord target) const noexcept {
        for (const auto& ant : ants_) {
            if (ant && ant->is_alive()) {
                if (ant->pos == target) return true;
                TileCoord cur_tile{
                    (ant->pixel_x >= 0) ? (ant->pixel_x / 32) : ((ant->pixel_x - 31) / 32),
                    (ant->pixel_y >= 0) ? (ant->pixel_y / 32) : ((ant->pixel_y - 31) / 32)
                };
                if (cur_tile == target) return true;
            }
        }
        return false;
    }


    // ================= Original movement system (implemented in movement_system.cpp) =================
    // Ports of the Ants.exe locomotion, order, path-manager and blocking code. Addresses in comments
    // refer to Original-Ants/Ants.exe; see docs/GAME_REVERSE_ENGINEERING.md "Movement ground truth".
    struct StepEvt {                 // FUN_0102b997 event block (ebp-0x18 .. ebp-0x4)
        int32_t  dx{0};
        int32_t  dy{0};
        uint16_t event{11111};       // next frame's event code (11111 = none)
        int32_t  status{0};          // 0 start step, 1 normal, 2 the consumed frame was the last one
    };
    struct OccCell {                 // occupancy grid cell (world+0x553c)
        int32_t ant{-1};             // registered ant id (-1 = empty)
        bool    multi{false};        // bit 8: more than one ant registered
    };

    uint32_t anim_clock_ms_{0};      // animation clock ("timeGetTime()"), advances 50 ms per sim tick
    uint32_t now_ms_{0};             // clock value of the event being processed
    std::vector<OccCell> occ_;
    std::array<PathManager, MAX_PLAYERS> path_managers_{};          // one PATHMGR per player machine
    std::unordered_map<uint32_t, uint32_t> path_request_serial_;    // ant id -> move_serial at request
    bool loco_trace_enabled_{false};
    std::vector<LocoTraceEvent> loco_trace_;
    void trace_loco(LocoTraceEvent::Kind kind, const AntUnit& a, int32_t dx, int32_t dy);

    // lifecycle / per tick
    void movement_reset();
    void movement_tick();
    void loco_sync(AntUnit& a);

    // occupancy (FUN_0100f17f / FUN_0100f2cd / FUN_0100f421 / FUN_0100f3ca)
    OccCell* occ_cell(TileCoord t) noexcept;
    void occ_move(AntUnit& a, TileCoord nt);
    void occ_refresh();
    AntUnit* occ_scan(TileCoord t, const AntUnit* exclude, uint16_t* count);
    AntUnit* occupant_at(TileCoord t);
    bool tile_occupied(TileCoord t);

    // animation stepping (FUN_0102c0db / FUN_0102b95f / FUN_0102b997 / FUN_0101ee84)
    void loco_play(AntUnit& a, const movement::MotionClip& clip, uint8_t dir, uint16_t evt5_ms = 0);
    void loco_update(AntUnit& a, uint32_t now);
    int  loco_step(AntUnit& a, uint32_t now);
    void loco_on_step(AntUnit& a, StepEvt& e);

    // actions and walking (FUN_0101ad02 / FUN_0101b8cb / ARRIVE / FUN_0101ccaf / FUN_01021664)
    void set_action(AntUnit& a, uint8_t action, uint8_t dir, int16_t terr_a, int16_t terr_b, bool flag);
    void walk_step(AntUnit& a, StepEvt& e);
    void arrive(AntUnit& a, StepEvt& e, TileCoord cur);
    void path_complete(AntUnit& a, StepEvt& e);
    void stop_sync(AntUnit& a);
    void stop_at(AntUnit& a, TileCoord t);
    void start_pause(AntUnit& a);
    void cancel_pause(AntUnit& a);
    void pause_fire(AntUnit& a);
    void enter_cant_go(AntUnit& a);
    void attack_clip_end(AntUnit& a);
    void loco_release(AntUnit& a);

    // tile entry and passability (FUN_0101c4f2 / FUN_0101f780 / FUN_01020951)
    int  try_enter_tile(AntUnit& a, TileCoord nt);
    bool can_enter(const AntUnit& a, TileCoord t, uint32_t flags);
    bool can_enter_attack_step(const AntUnit& a, TileCoord t, bool last);
    uint32_t step_cost(const AntUnit& a, TileCoord from, TileCoord to);

    // orders (FUN_0101fc50 / FUN_01020655 / FUN_010202e7 / FUN_0101ff5a / FUN_010287b5 / FUN_0100cba4)
    // a player order ignores the bombs at its goal (FUN_010202e7 gets flag 0x20 when the order is the player's), so a move may end on an own bomb
    bool go_to(AntUnit& a, TileCoord t, bool user_cmd, bool special);
    void classify_order(AntUnit& a, TileCoord t, bool special, bool user_cmd);
    bool adjust_goal(AntUnit& a, TileCoord& t, bool user_cmd);
    bool can_take_user_order(const AntUnit& a) const noexcept;
    bool group_click_skips(const AntUnit& a, TileCoord target, bool special, bool attack) const noexcept;
    void deliver_path(uint32_t ant_id, const std::vector<TileCoord>& path);
    bool has_pending_path(uint32_t ant_id) const noexcept;

    // abilities: validators, approach tile and the action starts / ends (movement_system.cpp, ability_system.cpp)
    bool valid_ground(TileCoord t, bool stationary_only);                       // FUN_0101d762
    bool valid_water(TileCoord t, bool stationary_only);                        // FUN_0101d6d6
    bool valid_bomb(TileCoord t) const;                                         // FUN_0101d7f9
    bool approach_tile(const AntUnit& a, TileCoord& t);                         // FUN_01020128
    bool occupied_stationary(TileCoord t);                                      // FUN_0100f3e8
    bool ability_arrive(AntUnit& a, uint8_t order);                             // FUN_0101ccaf cases 6..9, 0xd, 0xe (true: started)
    void start_plant(AntUnit& a, TileCoord target, TileCoord approach);         // FUN_01021915 (msg 0xd)
    void end_plant(AntUnit& a, bool cancel);                                    // FUN_0101e433
    void start_defuse(AntUnit& a, TileCoord target, TileCoord approach);        // FUN_010219e8 (msg 0xe)
    void end_defuse(AntUnit& a, bool cancel);                                   // FUN_0101e599
    void start_ignite(AntUnit& a, TileCoord target, TileCoord approach);        // FUN_010210fa (msg 0xb)
    void end_ignite(AntUnit& a, bool cancel);                                   // FUN_0101e798
    void start_extinguish(AntUnit& a, TileCoord target, TileCoord approach);    // FUN_010211f2 (msg 0xc)
    void end_extinguish(AntUnit& a, bool cancel);                               // FUN_0101e97b
    void start_bridge_build(AntUnit& a, TileCoord target, TileCoord approach);  // FUN_010212a3 (msg 0x19)
    void bridge_build_pass_end(AntUnit& a);                                     // step callback case 0x10 (0x101f2b7)
    void end_bridge_build(AntUnit& a, bool cancel);                             // FUN_0101eaec
    void start_bridge_demolish(AntUnit& a, TileCoord target, TileCoord approach);   // FUN_0102137b (msg 0x1a)
    void bridge_demolish_pass_end(AntUnit& a);                                  // step callback case 0x11 (0x101f401)
    void end_bridge_demolish(AntUnit& a, bool cancel);                          // FUN_0101ecdf
    void ability_clip_end(AntUnit& a);                                          // step callback cases 6..9 at the last frame
    void end_walk_to_idle(AntUnit& a);                                          // SetActionDefault(0) + SetPath(0)
    void powerup_pickup(AntUnit& a, TileCoord tile);                            // FUN_01020cdb (msg 9): action 4, type change
    void set_holding(AntUnit& a, uint16_t amount, TileCoord tile);              // FUN_0101ac8c
    void start_harvest(AntUnit& a, TileCoord food_tile, uint16_t amount);       // FUN_0102178a (msg 0xa): action 5
    void end_harvest(AntUnit& a);                                               // FUN_0101e342: cleanup of action 5
    void harvest_clip_end(AntUnit& a);                                          // step callback case 5 at the last frame

    // hill, hatch and raid actions (action_system.cpp)
    bool action_cleanup(AntUnit& a, uint8_t old_action, uint8_t new_action);   // SetAction old-action cleanup (table 0x101b48f)
    void enter_hill(AntUnit& a);                       // message 7 handler FUN_01021494
    void enter_clip_end(AntUnit& a);                   // step callback, actions 2 / 0x14, last frame
    void cleanup_enter(AntUnit& a);                    // FUN_0101e165: deposit and heal
    bool raid_arrive(AntUnit& a, StepEvt& e);          // FUN_0101ccaf case 0xb (true: the raid started)
    void start_raid(AntUnit& a, uint8_t victim, int32_t amount);    // FUN_0102184e
    void raid_clip_end(AntUnit& a);                    // step callback, actions 5 / 0xd, last frame
    void cleanup_raid(AntUnit& a);                     // FUN_0101e27f: loot transfer
    void anthillq_run();                               // ANTHILLQ task, every 200 ms
    void hatch_run(uint8_t team);                      // HATCHTSK (0x1025072)
    void add_score(uint8_t player, int32_t amount);    // FUN_01010cc9
    SimulationEngine::HatchResult hatch_request(uint8_t player, AntType type, bool force);   // FUN_01010aca
    /// PostStatus of the original for one player (255 = everybody): the text of `string_id` with `%s` filled by a..d, the flash flag
    /// of that id. The remake posts to the player that the original's "local player" test would pass.
    void post_news(uint8_t player, uint16_t string_id, const std::string& a = {}, const std::string& b = {},
                   const std::string& c = {}, const std::string& d = {});
    /// A "News Flash" line of the chat log (AddNewsFlash 0x100e9bb).
    void post_news_flash(uint16_t string_id, const std::string& a = {}, const std::string& b = {},
                         const std::string& c = {}, const std::string& d = {});
    TileCoord team_tile42(uint8_t team) const noexcept;
    TileCoord team_ring_tile(uint8_t team) const noexcept;
    uint32_t anthillq_next_ms_{200};

    // combat, flights, bombs and death (combat_system.cpp)
    static uint8_t dir_from_to(TileCoord from, TileCoord to) noexcept;         // FUN_01017531 / FUN_01017560
    static TileCoord step_toward(TileCoord t, uint8_t dir, int32_t r) noexcept; // FUN_0101d9f7
    bool landing_ok(TileCoord k) const;
    uint8_t knock_dir(TileCoord target, TileCoord attacker, int32_t range) const;   // FUN_0101d8ed
    TileCoord pick_landing(const AntUnit& self, TileCoord from, int32_t range, bool cardinal, bool* excl, bool use_rand);   // FUN_0101df5d
    void take_hit(AntUnit& t, uint8_t source_team);                             // FUN_01021627
    bool can_be_attacked_from(const AntUnit& t, TileCoord attacker_tile) const; // FUN_0101cb0c
    void on_attacked(uint8_t team);                                             // FUN_01010a03
    void start_engaged(AntUnit& t, uint8_t dir);                                // FUN_01020c70
    void start_melee(AntUnit& t, AntUnit& a, TileCoord t_tile, uint8_t kdir, uint8_t range);   // FUN_01010245
    void hit_frame(AntUnit& t, TileCoord tile, uint8_t kdir, uint8_t range);    // FUN_01010335
    void deliver_pending_hit(AntUnit& a);                                       // FUN_0101c1e2
    void start_knock_flight(AntUnit& t, uint8_t kdir, uint8_t range, TileCoord tile);   // FUN_0101de7e
    void handle_melee_message(AntUnit& t, AntUnit& a, TileCoord t_tile, uint8_t kdir, bool combat);   // msg 8, FUN_01022ca1
    bool melee_contact(AntUnit& a, AntUnit& t);                                 // TryEnterTile branch A
    bool low_hp_check(AntUnit& a);                                              // FUN_0101dded (flag 1)
    bool stun_or_die(AntUnit& a);                                               // FUN_0102151a (flag 1)
    bool stun_end(AntUnit& a);                                                  // FUN_0102151a (flag 0)
    void blast_hit(AntUnit& a, TileCoord at, TileCoord dest, uint8_t dmg, uint8_t src);   // FUN_0101c221
    void blast(AntUnit& a, uint8_t dmg, uint8_t src, bool pile_up);             // FUN_0101c34c (the dust ball only for the walk step's pile-up block)
    void drown(AntUnit& a, TileCoord t);                                        // FUN_0101c2e2
    void water_landing(AntUnit& a, TileCoord t);                                // FUN_0101e6b3
    void bridge_gone_scan(TileCoord t);                                         // FUN_0100f8bf (after the bridge tile became water)
    void bomb_trigger(AntUnit& a, TileCoord at);                                // path completion case 0xA
    void bomb_victim(AntUnit& a, TileCoord at, TileCoord to);                   // FUN_01021a6f
    void burn_overlay_start(AntUnit& a);                                        // FUN_01021c68
    void burn_overlay_end(AntUnit& a);                                          // callback 0x1021dda
    void finish_death(AntUnit& a);                                              // FUN_01020f89 / Kill 0x1020ff6
    void drop_powerup(AntUnit& a, TileCoord tile, uint8_t type);                // FUN_01020e6e
    void remove_ant(AntUnit& a);                                                // FUN_0100cd9f
    void cancel_combat_timer(AntUnit& a);                                       // FUN_0101c152
    void start_combat_timer(AntUnit& a, uint32_t ms);                           // FUN_0101c184
    bool resume_after_auto_engage(AntUnit& a);                                  // FUN_0101dd6f
    bool can_auto_engage(const AntUnit& a) const;                               // FUN_0101c0d5
    bool find_enemy(const AntUnit& a, int32_t radius, TileCoord& tile);         // FUN_0101dbec (+ FUN_0101db55)
    void attack_tile(AntUnit& a, TileCoord target);                             // FUN_0101da6f
    bool auto_engage_check(AntUnit& a, int32_t radius, bool at_arrival);        // the hooks at 0x101b9a8 and 0x101bd67
    void apply_path(AntUnit& a, const std::vector<TileCoord>& path);            // the part of message 6 that installs a path
    std::array<int64_t, MAX_PLAYERS> last_attacked_ms_{{-1000000, -1000000, -1000000, -1000000}};   // player +0x4c

    // helpers
    uint8_t  orig_action_of(const AntUnit& a) const noexcept;
    static bool is_stationary_action(uint8_t action) noexcept;
    void set_position(AntUnit& a, int32_t x, int32_t y);
    void set_idle_label(AntUnit& a);
    void set_walking_label(AntUnit& a);
    int32_t food_object_at(TileCoord t) const noexcept;
    TileCoord team_entrance(uint8_t team) const noexcept;
    bool is_special_base_tile(TileCoord t) const noexcept;
    bool team_dropped(uint8_t team) const noexcept { return team < MAX_PLAYERS && (dropped_mask_ & (1u << team)) != 0; }

    // The end of the match. The winner / loser sting is not a simulation event: the original plays ONE cue per machine when the results rows are
    // built (0x1015a4a: winner when the local team or its ally is the top row, else losers); the application plays it when the scorecard opens.
    void handle_game_over(uint16_t quitter = NO_QUITTER) {
        match_state_ = MatchState::GameOver;
        match_time_remaining_ms_ = 0;
        quitter_ = quitter;
        world_state_dirty_ = true;
    }
};

} // namespace ants::sim
