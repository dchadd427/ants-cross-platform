#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <memory>

#include "ants_assets/lvl_parser.hpp"
#include "ants_sim/prng.hpp"
#include "ants_sim/match_stats.hpp"
#include "ants_sim/grid.hpp"
#include "ants_sim/ant_unit.hpp"
#include "ants_sim/combat_ai.hpp"
#include "ants_sim/physics.hpp"

namespace ants::sim {

constexpr uint32_t TICK_RATE_HZ = 20u;
constexpr uint32_t TICK_MS      = 50u;

namespace SoundID {
    constexpr uint32_t PowerUpHeal    = 1;  // powerupc.wav
    constexpr uint32_t PowerUpChime   = 2;  // powerupc2.wav
    constexpr uint32_t BombDetonate   = 4;  // bombexp.wav (22kHz, 1.14s)
    constexpr uint32_t FireBurnout    = 5;  // fireburnout.wav
    constexpr uint32_t GeneralOrders  = 13; // gantorders.wav
    constexpr uint32_t GeneralReady   = 14; // gantrdy.wav
    constexpr uint32_t GeneralCommand = 15; // gantcommand.wav
    constexpr uint32_t GeneralAttack  = 16; // gantattack.wav
    constexpr uint32_t GeneralGo      = 17; // gantgo.wav
    constexpr uint32_t ThiefReady     = 18; // theifrdy.wav
    constexpr uint32_t ThiefGo        = 19; // theifgo.wav
    constexpr uint32_t ThiefAttack    = 20; // theifattack.wav
    constexpr uint32_t ThiefDo        = 21; // theifdo.wav
    constexpr uint32_t FireReady      = 22; // firerdy.wav
    constexpr uint32_t FireGo         = 23; // firego.wav
    constexpr uint32_t FireAttack     = 24; // fireattack.wav
    constexpr uint32_t FireDo         = 25; // firedo.wav
    constexpr uint32_t CombatReady2   = 26; // combrdy2.wav
    constexpr uint32_t CombatReady1   = 27; // combrdy1.wav
    constexpr uint32_t CombatGo1      = 28; // combgo1.wav
    constexpr uint32_t CombatGo2      = 29; // combgo2.wav
    constexpr uint32_t CombatDo2      = 30; // combdo2.wav
    constexpr uint32_t CombatDo1      = 31; // combdo1.wav
    constexpr uint32_t BridgeReady    = 32; // brdgrdy.wav
    constexpr uint32_t BridgeGo       = 33; // brdggo.wav
    constexpr uint32_t BridgeAttack   = 34; // brdgat.wav
    constexpr uint32_t BridgeDo       = 35; // brdgdo.wav
    constexpr uint32_t BomberReady    = 36; // bombrdy.wav
    constexpr uint32_t BomberGo       = 37; // bombgo.wav
    constexpr uint32_t BomberAttack   = 38; // bombattack.wav
    constexpr uint32_t BomberDo       = 39; // bombdo.wav
    constexpr uint32_t PlayerDefeat   = 41; // playerout.wav (11kHz, 0.94s)
    constexpr uint32_t ChatSendA      = 45; // chatsnda.wav
    constexpr uint32_t ChatSend       = 46; // chatsnd.wav
    constexpr uint32_t AllianceBreak  = 49; // allyoff.wav
    constexpr uint32_t AllianceOn     = 50; // allyon.wav
    constexpr uint32_t AlliancePro    = 51; // allypro.wav
    constexpr uint32_t AllianceNot    = 52; // allynot.wav
    constexpr uint32_t AllianceYes    = 53; // allyyes.wav
    constexpr uint32_t BaseEnter      = 55; // sound_55.wav (anthill entrance underground)
    constexpr uint32_t VictoryFanfare = 56; // winner.wav (22kHz, 4.67s)
    constexpr uint32_t MeleeAttack    = 57; // attack.wav
    constexpr uint32_t BaseAlarmSiren = 58; // underattack.wav (2566 Hz alarm)
    constexpr uint32_t AntStop        = 61; // antstop.wav
    constexpr uint32_t CantGo         = 63; // cantgo.wav
    constexpr uint32_t FlingThumpA    = 64; // flythumpa.wav
    constexpr uint32_t FlingThumpB    = 65; // flythumpb.wav
    constexpr uint32_t FoodHarvest    = 66; // harvest.wav
    constexpr uint32_t FireBeam       = 67; // firestarta.wav
    constexpr uint32_t FireErupt      = 68; // firestartb.wav
    constexpr uint32_t FireExtinguish = 69; // fireextinguish.wav
    constexpr uint32_t StunRecover    = 70; // stun.wav
    constexpr uint32_t WaterSplash    = 71; // splash.wav
    constexpr uint32_t AntDrown       = 72; // antdrown.wav
    constexpr uint32_t BombDefuseGrab = 73; // bombdrop.wav
    constexpr uint32_t BombBodySquash = 74; // bombmuffle.wav
    constexpr uint32_t HeavyPunch     = 78; // attack2.wav
    constexpr uint32_t ShovelGravel   = 81; // shovelgravel.wav
    constexpr uint32_t ShovelWater    = 82; // shovelwater.wav
    constexpr uint32_t ThiefCrawl     = 83; // theifwhip.wav
    constexpr uint32_t ThiefDive      = 84; // steala.wav
    constexpr uint32_t ThiefRummage   = 85; // stealb.wav
    constexpr uint32_t ThiefEmerge    = 86; // stealc.wav
    constexpr uint32_t BaseScoreUp    = 87; // scoreup.wav
    constexpr uint32_t BaseScoreDn    = 88; // scoredn.wav
    constexpr uint32_t BombPick       = 90; // bombpick.wav
}

inline uint32_t get_move_voice_sound(AntType type, uint32_t variant = 0) {
    switch (type) {
        case AntType::Worker:  return (variant % 2 == 0) ? SoundID::GeneralGo : SoundID::GeneralCommand;
        case AntType::Swimmer: return SoundID::BridgeGo;
        case AntType::Fire:    return SoundID::FireGo;
        case AntType::Combat:  return (variant % 2 == 0) ? SoundID::CombatGo1 : SoundID::CombatGo2;
        case AntType::Bomber:  return SoundID::BomberGo;
        case AntType::Thief:   return SoundID::ThiefGo;
        default:               return SoundID::GeneralGo;
    }
}

inline uint32_t get_ready_voice_sound(AntType type, uint32_t variant = 0) {
    switch (type) {
        case AntType::Worker:  return (variant % 2 == 0) ? SoundID::GeneralReady : SoundID::GeneralOrders;
        case AntType::Swimmer: return SoundID::BridgeReady;
        case AntType::Fire:    return SoundID::FireReady;
        case AntType::Combat:  return (variant % 2 == 0) ? SoundID::CombatReady1 : SoundID::CombatReady2;
        case AntType::Bomber:  return SoundID::BomberReady;
        case AntType::Thief:   return SoundID::ThiefReady;
        default:               return SoundID::GeneralReady;
    }
}

inline uint32_t get_attack_voice_sound(AntType type, uint32_t variant = 0) {
    switch (type) {
        case AntType::Worker:  return SoundID::GeneralAttack;
        case AntType::Swimmer: return SoundID::BridgeAttack;
        case AntType::Fire:    return SoundID::FireAttack;
        case AntType::Combat:  return (variant % 2 == 0) ? SoundID::CombatDo1 : SoundID::CombatDo2;
        case AntType::Bomber:  return SoundID::BomberAttack;
        case AntType::Thief:   return SoundID::ThiefAttack;
        default:               return SoundID::GeneralAttack;
    }
}

inline uint32_t get_ability_voice_sound(AntType type) {
    switch (type) {
        case AntType::Swimmer: return SoundID::BridgeDo;
        case AntType::Fire:    return SoundID::FireDo;
        case AntType::Combat:  return SoundID::CombatDo2;
        case AntType::Bomber:  return SoundID::BomberDo;
        case AntType::Thief:   return SoundID::ThiefDo;
        default:               return SoundID::GeneralGo;
    }
}

namespace StringID {
    constexpr uint16_t AllianceInvitePrompt   = 1;  // "%s (%s) invites you to form a team..."
    constexpr uint16_t AllianceFormedBroadcast = 39; // "%s and %s have formed an alliance!"
    constexpr uint16_t AllianceBrokenBroadcast = 40; // "%s broke their alliance with %s!"
    constexpr uint16_t ThiefAlarmWarning      = 53; // "A ThiefAnt is at your anthill!"
    constexpr uint16_t FoodStolenStatus       = 62; // "Food stolen..."
    constexpr uint16_t AllianceDeclined       = 80; // "%s declined the alliance invitation."
}

enum class OrderType : uint8_t {
    None = 0,
    Move,
    Attack,
    PlantBomb,
    DefuseBomb,
    IgniteFire,
    ExtinguishFire,
    BuildBridge,
    DemolishBridge,
    InfiltrateAnthill,
    ReturnToBase,
    Cancel
};

struct AntOrder {
    uint32_t  ant_id{0};
    OrderType type{OrderType::None};
    int32_t   target_x{0};
    int32_t   target_y{0};
    int32_t   target_entity_id{-1};
    bool      allow_friendly_bomb{false};
};

struct PendingHatch {
    uint8_t  player_id{0};
    AntType  type{AntType::Worker};
    uint32_t ticks_remaining{0};
};

struct AudioEvent {
    uint32_t sound_id{0};
    int32_t  world_x{0};
    int32_t  world_y{0};
    uint8_t  priority{0};
    uint8_t  target_player{255}; // 255 = Broadcast, 0..3 = Target player
};

struct NewsEvent {
    uint8_t     target_player{255};
    std::string message_text;
    uint32_t    timestamp_ms{0};
    uint16_t    string_id{0};
};

struct AntSnapshot {
    uint32_t id{0};
    uint8_t  player_id{0};
    AntType  type{AntType::Worker};

    int32_t  px{0};
    int32_t  py{0};
    int32_t  tile_x{0};
    int32_t  tile_y{0};
    uint8_t  facing{0};

    uint16_t hp{10};
    uint16_t max_hp{10};
    uint16_t anim_state{7};
    uint16_t anim_frame{0};

    bool     is_holding{false};
    bool     had_food_at_base_entry{false};
    uint16_t held_item_id{TILE_EMPTY};
    int32_t  carried_points{0};

    bool     is_airborne{false};
    bool     is_stunned{false};
    bool     is_swimming{false};
    bool     is_underground{false};
    bool     is_drowning{false};
    bool     is_on_mud{false};
    bool     is_transforming{false};
    uint16_t transform_anim_frame{0};
    bool     on_powerup{false};
    UnitState state{UnitState::Idle};
};

struct VisualEffect {
    std::string anim_name;
    int32_t px{0};
    int32_t py{0};
    uint16_t frame{0};
    uint16_t total_frames{0};
};

struct WorldState {
    uint64_t   tick_number{0};
    uint32_t   match_time_remaining_ms{0};
    MatchState match_state{MatchState::NotStarted};

    uint32_t   width{0};
    uint32_t   height{0};

    std::vector<TileCell>    cells;
    std::vector<AntSnapshot> ants;
    std::vector<VisualEffect> effects;

    std::array<PlayerMatchStats, MAX_PLAYERS> player_stats{};
    std::array<int32_t, MAX_PLAYERS>          player_scores{};
    std::array<uint32_t, MAX_PLAYERS>         player_eggs{};
    std::array<uint8_t, MAX_PLAYERS>          player_alliances{};

    std::vector<ants::assets::AnthillSpawn> anthills;
    MatchResult match_result;
};

class SimulationEngineImpl;

class SimulationEngine {
public:
    SimulationEngine();
    ~SimulationEngine();

    SimulationEngine(const SimulationEngine&) = delete;
    SimulationEngine& operator=(const SimulationEngine&) = delete;
    SimulationEngine(SimulationEngine&&) noexcept;
    SimulationEngine& operator=(SimulationEngine&&) noexcept;

    // Core Lifecycle
    void init(const ants::assets::LevelData& level, uint32_t random_seed);
    void init_test_world(uint32_t width, uint32_t height, uint32_t random_seed = 1, uint32_t match_time_ms = 720000);
    void reset();

    void tick();
    void issue_order(const AntOrder& order);
    bool hatch_ant(uint8_t player_id, AntType type);
    size_t get_pending_hatch_count(uint8_t player_id) const;
    void set_hatch_delay_ticks(uint32_t ticks);

    // Dynamic Alliances
    void propose_alliance(uint8_t from_player, uint8_t to_player);
    void respond_alliance(uint8_t responding_player, uint8_t proposing_player, bool accept);
    void accept_alliance(uint8_t responding_player, uint8_t proposing_player);
    void deny_alliance(uint8_t responding_player, uint8_t proposing_player);
    void break_alliance(uint8_t player_id);
    void break_alliance(uint8_t p1, uint8_t p2);
    void form_alliance(uint8_t p1, uint8_t p2);

    // State Inspection
    const WorldState& get_world_state() const;
    uint32_t get_match_time_remaining_ms() const;
    void set_match_time_remaining_ms(uint32_t ms);
    bool is_match_over() const;
    PlayerMatchStats get_player_stats(uint8_t player_id) const;

    std::vector<AudioEvent> poll_audio_events();
    std::vector<NewsEvent> poll_news_events();

    uint64_t current_tick() const noexcept;
    const Grid& grid() const;
    Grid& grid_mut();
    const PRNG& prng() const;
    PRNG& prng_mut();
    const MatchStatsManager& stats_manager() const;
    MatchStatsManager& stats_manager_mut();

    // Verification & Testing Helpers
    bool has_audio_event(uint32_t sound_id) const;
    bool has_targeted_audio_event(uint8_t player_id, uint32_t sound_id) const;
    bool has_news_event(uint8_t player_id, uint16_t string_id) const;
    void clear_audio_events();
    void clear_news_events();

    uint32_t spawn_unit(uint8_t player_id, AntType type, TileCoord pos);
    AntUnit& get_unit(uint32_t ant_id);
    const AntUnit& get_unit(uint32_t ant_id) const;
    void kill_unit(uint32_t ant_id);

    void execute_melee_attack(uint32_t attacker_id, uint32_t target_id);
    void issue_move_order(uint32_t ant_id, TileCoord dest, bool allow_friendly_bomb = false);

    bool validate_cardinal_placement(TileCoord from, TileCoord to) const;
    bool plant_bomb(uint32_t ant_id, TileCoord target, bool instant = true);
    bool defuse_bomb(uint32_t ant_id, TileCoord target);
    bool ignite_fire(uint32_t ant_id, TileCoord target, bool instant = true);
    bool extinguish_fire(uint32_t ant_id, TileCoord target);
    bool build_bridge_step(uint32_t ant_id, TileCoord target);
    bool demolish_bridge_step(uint32_t ant_id, TileCoord target);
    bool interrupt_transformation(uint32_t ant_id);
    void set_unit_transformation_interrupted(uint32_t ant_id, bool interrupted);

    bool can_unit_traverse(AntType type, TileCoord pos) const;

    bool has_bomb_at(TileCoord pos) const;
    bool has_fire_at(TileCoord pos) const;
    uint32_t get_fire_timer(TileCoord pos) const;
    void set_fire_at(TileCoord pos, uint32_t timer_ticks);

    bool has_bridge_at(TileCoord pos) const;
    int  get_bridge_stage(TileCoord pos) const;
    void set_bridge_at(TileCoord pos, int stage, uint32_t timer_ticks);

    void set_terrain(int32_t x, int32_t y, uint8_t terrain_type);
    void set_tile_flags(int32_t x, int32_t y, uint16_t flags);
    void set_anthill(uint8_t team_id, TileCoord pos);

    TileCoord assign_queue_slot(uint8_t team_id, TileCoord from_pos);
    void release_queue_slot(TileCoord slot);
    void clear_reserved_queue_slots();
    bool is_queue_slot_reserved(TileCoord slot) const;

    void join_base_queue(uint32_t ant_id);
    void leave_base_queue(uint32_t ant_id);
    bool is_ant_in_base_queue(uint32_t ant_id) const;
    uint32_t get_active_depositing_ant(uint8_t player_id) const;
    size_t get_base_queue_size(uint8_t player_id) const;
    TileCoord get_base_queue_slot(uint8_t player_id, size_t index) const;
    void send_ant_straight_into_base(uint32_t ant_id);
    void dispatch_next_base_queue(uint8_t player_id);

    void step_base_entry_animation(uint32_t ant_id, uint16_t target_frame);

    void start_thief_infiltration(uint32_t ant_id, uint8_t target_team_id);
    void step_thief_animation(uint32_t ant_id, uint16_t target_frame);
    void execute_thief_loot(uint32_t ant_id, uint8_t target_team_id);

    bool has_lunchbox_at(TileCoord pos) const;
    uint32_t get_lunchbox_points(TileCoord pos) const;

    void apply_knockback(uint32_t ant_id, int32_t from_px, int32_t from_py, int32_t min_tiles, int32_t max_tiles);
    void resolve_fire_contact(uint32_t ant_id, int32_t incoming_dx, int32_t incoming_dy);

    int32_t get_display_score(uint8_t player_id) const;
    int32_t get_player_score(uint8_t player_id) const;
    void set_player_score(uint8_t player_id, int32_t score);

    uint32_t get_player_eggs(uint8_t player_id) const;
    void set_player_eggs(uint8_t player_id, uint32_t eggs);

    uint32_t get_player_hatched(uint8_t player_id) const;
    uint8_t get_ally_id(uint8_t player_id) const;

    void record_player_stat(uint8_t player_id, StatType stat, uint32_t value);

private:
    std::unique_ptr<SimulationEngineImpl> impl_;
};

} // namespace ants::sim
