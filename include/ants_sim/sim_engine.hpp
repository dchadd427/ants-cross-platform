#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <memory>

#include "ants_assets/lvl_parser.hpp"
#include "ants_sim/prng.hpp"
#include "ants_sim/command.hpp"
#include "ants_sim/match_stats.hpp"
#include "ants_sim/grid.hpp"
#include "ants_sim/ant_unit.hpp"

namespace ants::sim {

constexpr uint32_t TICK_MS      = 50u;

namespace SoundID {
    constexpr uint32_t ButtonClick    = 0;  // buttonclick.wav
    constexpr uint32_t PowerUpHeal    = 1;  // powerupc.wav
    constexpr uint32_t PowerUpChime   = 2;  // powerupc2.wav
    constexpr uint32_t CombatNetFairy = 3;  // combatnetfairy.wav (11kHz, 0.62s, collision battle ball scuffle)
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
    constexpr uint32_t PlayerDropOut  = 41; // playerout.wav (11kHz, 0.94s, player drops out)
    constexpr uint32_t PlayerDefeat   = 42; // losers.wav (11kHz, 1.94s, match defeat sting)
    constexpr uint32_t ExitHill       = 43; // exithill.wav (hatched ant emerges from anthill)
    constexpr uint32_t Countdown      = 44; // countdwn.wav (10-second countdown tick)
    constexpr uint32_t ChatSendA      = 45; // chatsnda.wav
    constexpr uint32_t ChatSend       = 46; // chatsnd.wav
    constexpr uint32_t Bump           = 47; // bump.wav (ants bump into each other)
    constexpr uint32_t Anthill        = 48; // anthill.wav
    constexpr uint32_t AllianceBreak  = 49; // allyoff.wav
    constexpr uint32_t AllianceOn     = 50; // allyon.wav
    constexpr uint32_t AlliancePro    = 51; // allypro.wav
    constexpr uint32_t AllianceNot    = 52; // allynot.wav
    constexpr uint32_t AllianceYes    = 53; // allyyes.wav
    constexpr uint32_t ThirtySeconds  = 54; // 30sec.wav (30 seconds remaining warning)
    constexpr uint32_t OneMinute      = 55; // 1min.wav (1 minute remaining warning)
    constexpr uint32_t BaseEnter      = 55; // Legacy alias
    constexpr uint32_t VictoryFanfare = 56; // winner.wav (22kHz, 4.67s)
    constexpr uint32_t MeleeAttack    = 57; // attack.wav
    constexpr uint32_t BaseAlarmSiren = 58; // underattack.wav (2566 Hz alarm)
    constexpr uint32_t CombatAttack1  = 59; // combat1.wav (the combat ant's attack voice)
    constexpr uint32_t CombatAttack2  = 60; // combat2.wav
    constexpr uint32_t AntStop        = 61; // antstop.wav
    constexpr uint32_t PowerUpDrop    = 62; // powerdrip.wav
    constexpr uint32_t CantGo         = 63; // cantgo.wav
    constexpr uint32_t FlingThumpA    = 64; // flythumpa.wav
    constexpr uint32_t FlingThumpB    = 65; // flythumpb.wav
    constexpr uint32_t FoodHarvest    = 66; // harvest.wav
    constexpr uint32_t FireBeam       = 67; // firestarta.wav
    constexpr uint32_t FireErupt      = 68; // firestartb.wav
    constexpr uint32_t FireExtinguish = 69; // fireextinguish.wav
    constexpr uint32_t StunRecover    = 70; // stun.wav
    constexpr uint32_t Stun           = 70; // stun.wav alias
    constexpr uint32_t WaterSplash    = 71; // splash.wav
    constexpr uint32_t AntDrown       = 72; // antdrown.wav
    constexpr uint32_t BombDefuseGrab = 73; // bombdrop.wav
    constexpr uint32_t BombBodySquash = 74; // bombmuffle.wav
    constexpr uint32_t AttackAlt      = 75; // attack_alt.wav (Worker ant attack variation)
    constexpr uint32_t FoodGrab       = 77; // foodgrab.wav (Worker ant food grab / bite)
    constexpr uint32_t HeavyPunch     = 78; // attack2.wav
    constexpr uint32_t WaterAttack    = 79; // waterattack.wav
    constexpr uint32_t ShovelGravel   = 81; // shovelgravel.wav
    constexpr uint32_t ShovelWater    = 82; // shovelwater.wav
    constexpr uint32_t ThiefCrawl     = 83; // theifwhip.wav
    constexpr uint32_t ThiefWhip      = 83; // Alias for theifwhip.wav attack strike
    constexpr uint32_t ThiefDive      = 84; // steala.wav
    constexpr uint32_t ThiefRummage   = 85; // stealb.wav
    constexpr uint32_t ThiefEmerge    = 86; // stealc.wav
    constexpr uint32_t BaseScoreUp    = 87; // scoreup.wav
    constexpr uint32_t BaseScoreDn    = 88; // scoredn.wav
    constexpr uint32_t NavButtonClick = 89; // navbuttonclick.wav (11kHz, 0.13s, UI button click)
    constexpr uint32_t BombPick       = 90; // bombpick.wav
}

// The voices of the ordering commands (Ants.exe FUN_0101b5f9 ready, FUN_0101b67b go, FUN_0101b711 attack, FUN_0101b78a special).
// `variant` stands for the original's rand(): the worker's ready voice is gantorders for rand() % 3 == 0 and gantrdy otherwise,
// its go voice gantgo for an even and gantcommand for an odd number, the combat ant's voices alternate on rand() % 2.
constexpr uint32_t NoVoice = 0xFFFFu;   // the command plays no voice (worker and combat ant special orders)

inline uint32_t get_move_voice_sound(AntType type, uint32_t variant = 0) {
    switch (type) {
        case AntType::Worker:  return (variant % 2 == 0) ? SoundID::GeneralGo : SoundID::GeneralCommand;
        case AntType::Swimmer: return SoundID::BridgeGo;
        case AntType::Fire:    return SoundID::FireGo;
        case AntType::Combat:  return (variant % 2 == 0) ? SoundID::CombatGo2 : SoundID::CombatGo1;
        case AntType::Bomber:  return SoundID::BomberGo;
        case AntType::Thief:   return SoundID::ThiefGo;
        default:               return SoundID::GeneralGo;
    }
}

inline uint32_t get_ready_voice_sound(AntType type, uint32_t variant = 0) {
    switch (type) {
        case AntType::Worker:  return (variant % 3 == 0) ? SoundID::GeneralOrders : SoundID::GeneralReady;
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
        case AntType::Combat:  return (variant % 2 == 0) ? SoundID::CombatAttack1 : SoundID::CombatAttack2;
        case AntType::Bomber:  return SoundID::BomberAttack;
        case AntType::Thief:   return SoundID::ThiefAttack;
        default:               return SoundID::GeneralAttack;
    }
}

inline uint32_t get_ability_voice_sound(AntType type) {
    switch (type) {
        case AntType::Swimmer: return SoundID::BridgeDo;
        case AntType::Fire:    return SoundID::FireDo;
        case AntType::Bomber:  return SoundID::BomberDo;
        case AntType::Thief:   return SoundID::ThiefDo;
        default:               return NoVoice;   // worker and combat ant special orders are silent
    }
}

namespace StringID {
    constexpr uint16_t AllianceInvitePrompt       = 1;  // "%s (%s) invites you to form a team..."
    constexpr uint16_t AllianceFormedBroadcast     = 39; // "%s (%s) and %s (%s) are a team now!" (chat log news flash)
    constexpr uint16_t AllianceBrokenBroadcast     = 40; // "%s (%s) and %s (%s) are no longer a team!" (chat log news flash)
    constexpr uint16_t PlayerDropOut              = 46; // "%s dropped out of the game!"
    constexpr uint16_t OneMinuteRemaining         = 49; // "1 minute left in the game."
    constexpr uint16_t ThirtySecondsRemaining     = 50; // "30 seconds left in the game."
    constexpr uint16_t ThiefAlarmWarning          = 53; // "A ThiefAnt is at your anthill!"
    constexpr uint16_t TenSecondsRemaining        = 59; // "10 seconds and counting..."
    constexpr uint16_t FoodStolenStatus           = 62; // "Food stolen..."
    constexpr uint16_t AllianceDeclined           = 80; // "%s rejected teaming up" (status of the proposer)
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
    bool      special{false};             // FUN_010287b5's special flag: the classification gives the ant's ability order
};


struct AudioEvent {
    uint32_t sound_id{0};
    int32_t  world_x{0};
    int32_t  world_y{0};
    uint8_t  priority{0};
    uint8_t  target_player{255}; // 255 = Broadcast, 0..3 = Target player
    /// The sprite that started the sound (clip flag 5, "track": the sprite remembers the buffers it started): an ant id, or the id of an effect sprite (0x40000000 and up);
    /// 0 = nobody stops it (cues, UI). The sound is cut when the owner's clip is replaced or the owner is removed (FUN_0102c0db -> FUN_0102bdab, FUN_01008871).
    uint32_t owner{0};
    /// A stop request instead of a sound: every sound of `owner` that still plays is cut. It comes in order with the plays of the same tick.
    bool     stop{false};
};

/// Where a message of the original appears: the one-line status box (PostStatus) or the chat log as a "News Flash" line.
/// A Dialog event is the text of a modal question (the alliance invitation); the match screen has no dialog for it yet.
enum class NewsChannel : uint8_t { Status = 0, ChatLog = 1, Dialog = 2 };

struct NewsEvent {
    uint8_t     target_player{255};   // 255 = every player, else the one player that sees it
    std::string message_text;         // the original's text with its arguments inserted
    uint32_t    timestamp_ms{0};      // match time elapsed when it was posted (the "[m:ss]" of a News Flash)
    uint16_t    string_id{0};         // id in the original's string table (game_strings.hpp)
    bool        blink{false};         // status posted with the flash flag: a 500 ms flicker before the steady text
    NewsChannel channel{NewsChannel::Status};
};

struct AntSnapshot {
    uint32_t id{0};
    uint8_t  player_id{0};
    /// What the ant IS: its own type, or for an ant whose own type is Worker the level's default type (LVL block 3; the getter FUN_0100f9cb(ant, 0)). The sprite,
    /// the voices, the panel text and pedestal and the selection's common type all read this.
    AntType  type{AntType::Worker};
    /// The ant's own type field (+0x54): Worker until it takes a power-up, whatever the level's default is. Only a few places read the field itself (its pick box: a
    /// combat ant's is larger, 0x1026a3d).
    AntType  raw_type{AntType::Worker};

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
    int32_t  carried_points{0};

    bool     is_stunned{false};
    bool     is_swimming{false};
    bool     is_drowning{false};
    bool     is_on_mud{false};
    int32_t  burn_elapsed_ms{-1};       // dud burn overlay (?bu) time since it started, -1 = none
    bool     frozen{false};             // +0xfc: the display loop skips a frozen ant (only its ?bu overlay is drawn)
    UnitState state{UnitState::Idle};
    uint8_t  target_team_id{255};

    // Original locomotion animation (idle / walk / swim / dive / climb / can't-go) currently shown, exactly
    // as the 1998 engine plays it: the ants.chd Table-4 animation, its current frame, and whether it is a
    // mirrored copy (directions SW, W, NW). loco_clip is 0x7FFE while another system animates the ant.
    uint16_t loco_clip{0x7FFE};
    uint16_t loco_frame{0};
    bool     loco_mirrored{false};
    // Milliseconds until the current clip frame ends (the sim state is at a 50 ms tick boundary; the original's player runs on
    // the real clock, so the renderer predicts the frames and displacements that end before the next tick).
    uint16_t loco_left_ms{0};
};

/**
 * @brief One locomotion event (verification trace, see SimulationEngine::set_locomotion_trace_enabled).
 */
struct LocoTraceEvent {
    enum class Kind : uint8_t {
        Step,           ///< an animation step ran (FUN_0102b997); dx/dy = displacement applied
        PathDelivered,  ///< the path manager delivered a path to the ant
        PathFailed,     ///< the path manager found no path ("Can't go there.")
    };
    Kind     kind{Kind::Step};
    uint32_t time_ms{0};     ///< animation clock (50 ms per simulation tick)
    uint32_t ant_id{0};
    int32_t  px{0};          ///< position after the event
    int32_t  py{0};
    int32_t  dx{0};
    int32_t  dy{0};
    uint16_t clip{0x7FFE};   ///< ants.chd animation running after the step
    uint16_t frame{0};       ///< its current frame
    uint8_t  action{0xFF};   ///< original action id after the step (0 idle, 1 walk, 0xB can't go)
};

struct VisualEffect {
    std::string anim_name;
    int32_t px{0};              ///< anchor pixel (tile effects use the tile top-left, Ants.exe FUN_010100ab)
    int32_t py{0};
    uint16_t frame{0};          ///< elapsed simulation ticks (elapsed_ms / 50)
    uint16_t total_frames{0};   ///< lifetime in ticks (ceil(duration_ms / 50) when duration_ms is set)
    uint32_t elapsed_ms{0};     ///< time since creation (50 ms per tick); the renderer adds the sub-tick fraction
    uint32_t duration_ms{0};    ///< sum of the Table-4 frame durations (0: legacy effect living total_frames ticks)
    int32_t  y_key{0};          ///< sort key in the y-sorted sprite list (row*32 for tile effects)
    bool     fog_gated{false};  ///< hidden while the anchor tile is unexplored
    bool     looping{false};    ///< the clip repeats until the sim removes the effect (the battle cloud)
    uint32_t audio_owner{0};    ///< the owner id of the sound this effect started (stopped when the effect ends); 0 = none
};

/**
 * @brief Floating score bubble ("+N" / "-N") shown at a player's home tile for 400 ms (Ants.exe FUN_01010560:
 * a 20-step task, one step every 20 ms, each moving the sprite 5 px up for a gain or down for a loss).
 */
struct ScoreBubble {
    int32_t  x{0};            ///< sprite position at creation: the player's home tile top-left
    int32_t  y{0};
    int32_t  amount{0};       ///< signed score change
    uint32_t elapsed_ms{0};   ///< time since creation (50 ms per tick)
};

struct FlowerDropperSnapshot {
    int32_t x{0};
    int32_t y{0};
    int32_t drop_x{0};
    int32_t drop_y{0};
    bool is_dropping{false};
    uint32_t drop_elapsed_ms{0}; // time since the drop animation started (50 ms per tick)
    uint8_t powerup_type{0}; // 0: Bomber, 1: Combat, 2: Thief, 3: Swimmer, 4: Fire
};

struct WorldState {
    uint32_t   match_time_remaining_ms{0};

    uint32_t   width{0};
    uint32_t   height{0};

    std::vector<TileCell>    cells;
    std::vector<AntSnapshot> ants;
    std::vector<VisualEffect> effects;
    std::vector<ScoreBubble> score_bubbles;
    std::vector<FlowerDropperSnapshot> flower_droppers;

    std::array<PlayerMatchStats, MAX_PLAYERS> player_stats{};
    std::array<int32_t, MAX_PLAYERS>          player_scores{};
    std::array<uint32_t, MAX_PLAYERS>         player_eggs{};
    std::array<uint8_t, MAX_PLAYERS>          player_alliances{};
    /// [invitee]: the player whose invitation to form a team is waiting for the invitee's answer, 255 = none. The invitee's question and the
    /// proposer's waiting dialog of the match screen follow from it (docs 5.42).
    std::array<uint8_t, MAX_PLAYERS>          pending_invite_from{255, 255, 255, 255};
    std::vector<ants::assets::AnthillSpawn> anthills;
    /// The plants of the map (Block 1 flowers and clovers): world objects that the minimap shows as dots (Grid::plants)
    std::vector<MapPlant> plants;
    /// bit p: team p dropped out of the match (team +0x64): the live players counted by FUN_0100c58a are the hills whose bit is clear
    uint8_t dropped_mask{0};
    MatchResult match_result;

    // Authentic Fog of War State
    bool fog_of_war_enabled{false};
    std::vector<uint8_t> fog_revealed{}; // size: width * height (1 = revealed, 0 = shrouded)

    bool is_tile_revealed(int32_t x, int32_t y) const noexcept {
        if (!fog_of_war_enabled) return true;
        if (x < 0 || y < 0 || static_cast<uint32_t>(x) >= width || static_cast<uint32_t>(y) >= height) return false;
        size_t idx = static_cast<size_t>(y) * width + static_cast<size_t>(x);
        return idx < fog_revealed.size() && fog_revealed[idx] != 0;
    }
};

class SimulationEngineImpl;

/// FNV-1a 64 over the deterministic gameplay state (see state_hash.cpp): `total` is what lock-step peers compare every 20 ticks, the parts
/// name the subsystem that differs after a desync. Presentation (audio and news queues, visual effects, fog, player names) is not part of it.
struct StateHash {
    uint64_t total{0};
    uint64_t engine{0};      // clocks, PRNGs, match state, hatching, CHECKGO
    uint64_t players{0};     // scores, statistics, eggs, alliances, invitations
    uint64_t grid{0};        // every cell, hills, the solid-bit mode
    uint64_t food{0};        // food objects
    uint64_t ants{0};        // every ant and the occupancy grid
    uint64_t paths{0};       // path managers, request serials
    uint64_t droppers{0};    // flower droppers
    bool operator==(const StateHash& o) const noexcept { return total == o.total; }
    bool operator!=(const StateHash& o) const noexcept { return total != o.total; }
};

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
    /// Starts a match for the teams of `roster_mask` (bit p = team p takes part). A team without a player has no hill, no starting ants and no eggs:
    /// the original keeps a NULL entry for it in its team table, which CHECKGO skips (Ants.exe 0x1024921). init(level, seed) plays with every team.
    void init(const ants::assets::LevelData& level, uint32_t random_seed, uint8_t roster_mask);
    uint8_t roster_mask() const noexcept;
    void init_test_world(uint32_t width, uint32_t height, uint32_t random_seed = 1, uint32_t match_time_ms = 720000);

    void tick();
    /// The one entry through which a player changes the simulation (command.hpp): validates the command (issuer, ownership, ranges) and applies
    /// it. Every machine of a lock-step match applies the same commands at the same tick in canonical order.
    CommandResult apply_command(const Command& command);
    /// Hash of the deterministic gameplay state (see StateHash).
    StateHash state_hash() const;
    /// FUN_0100d03b (a team leaves the match: quit, kicked, 60 s without a sign of life): the team is marked dropped (+0x64), the cue and the News
    /// Flash of string 46 are posted (the cue unless the match is over), every ant of the team starts its death clip, its alliance ends and its
    /// egg in the incubator is lost. A team that is not in the roster or already dropped is left alone.
    /// The drop-out also ends the match at once when it leaves a team and its ally alone (Ants.exe 0x100d172: the game-over message of the machine of
    /// a team without a live enemy).
    void drop_player(uint8_t player_id);
    bool is_player_dropped(uint8_t player_id) const noexcept;
    /// FUN_0101453f, the quit dialog's Yes: with exactly one other side left the match ends and `player_id` is the quitter (its row is the last of the
    /// results); with more sides left (or none) the team drops out like a peer that left.
    void quit_player(uint8_t player_id);
    /// FUN_0100c5b1: the number of sides that remain when the team leaves (every other team that has not dropped, each alliance once, the team's own
    /// ally counted). The quit dialog asks it: exactly 1 ends the match.
    uint32_t other_sides(uint8_t player_id) const;
    /// The team whose quit ended the match (the game-over message's word), NO_QUITTER when the clock or the rules ended it
    uint16_t quitter() const noexcept;

    /// FUN_01009fd8: the power-up type a flower dropper posts for the draw `r` (= rand() % 10000): the first type, in the order bomber, combat, thief,
    /// swimmer, fire, whose running total of trunc(p * 10000) exceeds r (types with p == 0 are skipped); 0xFF when none does (the caller then
    /// draws rand() % 5, 0x100a03f).
    static uint8_t pick_dropper_powerup(const std::array<double, 5>& probabilities, uint32_t r) noexcept;
    /// The ant that would acknowledge a group order given now (0 = none), computed like the engine picks it (the closest ant of the issuer that can
    /// take the order and is not already carrying it out, provided its order would queue a path) but without changing anything. The lock-step
    /// client uses it for the immediate feedback (voice, pedestal) of an order that only reaches the simulation a few turns later. `needed` (optional) gets the
    /// number of ants that need the order (CommandResult::needing_order).
    uint32_t predict_order_ack(const Command& command, uint32_t* needed = nullptr) const;
    /// Returns whether the order was accepted (for a move, special or attack order: GoTo returned true and a path was requested).
    bool issue_order(const AntOrder& order);
    /// The Stop button for one selected ant (the loop body of Ants.exe FUN_01028a60): an ant that accepts player orders, is
    /// not on the hill entrance or the tile above it and has a walk or a target is sent to its own tile as an ordinary move
    /// (no player flag, so a power-up under it is an obstacle and not a pick-up). Returns true when the order was given.
    bool stop_ant(uint32_t ant_id);
    /// Result of a click on the hatch pedestal (Ants.exe FUN_01010aca): the reason a click did nothing.
    enum class HatchResult : uint8_t { Started, NoEggs, AlreadyHatching, NotEnoughPoints, NotAvailable };
    /// Starts an 8000 ms incubation (cost min(score, 200), one egg); `force` skips the 200 point rule (auto-hatch).
    HatchResult try_hatch(uint8_t player_id, AntType type = AntType::Worker, bool force = false);
    bool hatch_ant(uint8_t player_id, AntType type);
    size_t get_pending_hatch_count(uint8_t player_id) const;   // 1 while an egg is incubating
    void set_hatch_delay_ticks(uint32_t ticks);                // incubation in ticks (default 160 = 8000 ms)

    // Dynamic Alliances
    void propose_alliance(uint8_t from_player, uint8_t to_player);
    /// The proposer takes the offer back (Ants.exe FUN_0100c5fa): the invitee reads "%s withdrew offer to team up".
    void withdraw_alliance_offer(uint8_t from_player, uint8_t to_player);
    /// Player names as the alliance texts print them (Ants.exe W+0x5220 + team * 0x3c); by default the colour of the team.
    void set_player_name(uint8_t player_id, const std::string& name);
    std::string get_player_name(uint8_t player_id) const;
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

    // Fog of War
    void set_fog_of_war_enabled(bool enabled);
    bool is_fog_of_war_enabled() const;
    void set_viewing_player_id(uint8_t player_id);

    std::vector<AudioEvent> poll_audio_events();
    std::vector<NewsEvent> poll_news_events();

    uint64_t current_tick() const noexcept;
    const Grid& grid() const;
    Grid& grid_mut();
    const MatchStatsManager& stats_manager() const;
    MatchStatsManager& stats_manager_mut();

    // Verification & Testing Helpers
    bool has_audio_event(uint32_t sound_id) const;
    bool has_targeted_audio_event(uint8_t player_id, uint32_t sound_id) const;
    bool has_news_event(uint8_t player_id, uint16_t string_id) const;
    void clear_audio_events();
    void clear_news_events();
    void trigger_player_dropout(uint8_t player_id, const std::string& player_name = "");
    /// Records every locomotion animation step and path delivery with its millisecond time (off by default).
    void set_locomotion_trace_enabled(bool enabled);
    const std::vector<LocoTraceEvent>& locomotion_trace() const;

    /// SpawnAnt (0x100ef18): the ant appears at the tile. The facing is drawn as rand() % 8 for the test fixtures; the ants that a level starts with
    /// (`level_start`) draw it as the original does, rand() % 7 + 1, which is never North.
    uint32_t spawn_unit(uint8_t player_id, AntType type, TileCoord pos, bool level_start = false);
    AntUnit& get_unit(uint32_t ant_id);
    /// The ant type getter FUN_0100f9cb(ant, 0): what the ant IS (see AntSnapshot::type); AntUnit::type is the field itself.
    AntType ant_type(const AntUnit& ant) const noexcept;
    void kill_unit(uint32_t ant_id);

    void execute_melee_attack(uint32_t attacker_id, uint32_t target_id);
    /**
     * @brief Player move order through the original GoTo (Ants.exe FUN_0101fc50, "player" flag set): the ant
     * must be idle, walking or stunned (FUN_0101ff5a); it snaps to the centre of its tile, idles, and walks
     * once the path manager has delivered its path. Clicking an enemy ant attacks it, clicking a power-up
     * picks it up; an own bomb at the destination is walked onto and set off (a player order ignores bombs at its goal, FUN_010202e7 flag 0x20).
     */
    void issue_move_order(uint32_t ant_id, TileCoord dest);
    /// Move order given by a remake system (hill queue, ability approach): GoTo without the player flag.
    void issue_internal_move_order(uint32_t ant_id, TileCoord dest);
    /**
     * @brief Player group move (Ants.exe FUN_010287b5). The ants that accept orders (FUN_0101ff5a) and are
     * not already carrying out this very order are sorted by 16 x Chebyshev distance to the clicked tile
     * (exchange sort, not stable) and each gets the player GoTo to that tile; team-mate claims and the goal
     * ring scan spread them over free tiles.
     * @return the ant that acknowledges the order (the closest one, if its GoTo queued a path), or 0.
     */
    uint32_t issue_group_move_order(const std::vector<uint32_t>& ant_ids, TileCoord target, uint32_t* needed = nullptr);
    /**
     * @brief Player group special order (FUN_010287b5 with the special flag, cursor mode 4 / the latched ability pedestal): the same group
     * dispatch, but the classification (FUN_01020655) makes every ant carry out its ability at the tile: a bomber plants (defuses a bomb it
     * finds), a fire ant lights (or puts out) a fire wall, a swimmer builds (or demolishes) a bridge, a thief raids an enemy hill; worker
     * and combat ants just stop where they are.
     * @return the ant that acknowledges the order (the closest one, if its GoTo queued a path), or 0.
     */
    uint32_t issue_group_special_order(const std::vector<uint32_t>& ant_ids, TileCoord target, uint32_t* needed = nullptr);
    /// FUN_01026f91: whether a click on `tile` is a valid special order for a selection of the homogeneous type `type` of team `own_team`.
    /// `auto_flag` = false: the ability pedestal is latched (bomb tile or plantable ground for a bomber; fire wall or plantable ground for
    /// a fire ant; bridge or water for a swimmer; enemy hill for a thief); true: one ant is selected without the pedestal (only a bomb
    /// tile for a bomber and an enemy hill for a thief). Worker and combat ants never have one.
    bool is_special_target_valid(AntType type, TileCoord tile, bool auto_flag, uint8_t own_team) const;

    /**
     * @brief Player group attack (Ants.exe FUN_010287b5 with the attack flag). Ants that accept orders and already carry out an
     * attack order on this very tile are skipped (a repeated click changes nothing), the others are sorted by 16 x Chebyshev
     * distance to the clicked tile (exchange sort, not stable) and each gets the player GoTo to the tile; the classification
     * turns it into the attack order when another team's ant stands there.
     * @return the ant that acknowledges the order ("Attack!" and its voice): the closest one, if its GoTo queued a path, or 0.
     */
    uint32_t issue_group_attack_order(const std::vector<uint32_t>& ant_ids, TileCoord target, uint32_t* needed = nullptr);
    /// True while a path request of this ant is queued in the path manager (PATHMGR, one path per 50 ms).
    bool has_pending_path(uint32_t ant_id) const;

    bool validate_cardinal_placement(TileCoord from, TileCoord to) const;
    bool plant_bomb(uint32_t ant_id, TileCoord target, bool instant = true);
    bool defuse_bomb(uint32_t ant_id, TileCoord target, bool instant = true);
    bool ignite_fire(uint32_t ant_id, TileCoord target, bool instant = true);
    bool extinguish_fire(uint32_t ant_id, TileCoord target, bool instant = true);
    bool build_bridge_step(uint32_t ant_id, TileCoord target);
    bool demolish_bridge_step(uint32_t ant_id, TileCoord target);

    bool can_unit_traverse(AntType type, TileCoord pos, uint8_t ant_team = 255, bool is_entering_or_leaving = false) const;

    bool has_bomb_at(TileCoord pos) const;
    bool has_fire_at(TileCoord pos) const;
    bool has_living_ant_at(TileCoord pos) const;
    void trigger_bomb_detonation(uint32_t ant_id, TileCoord bomb_pos, int32_t incoming_dx = 0, int32_t incoming_dy = 0);
    uint32_t get_fire_timer(TileCoord pos) const;
    void set_fire_at(TileCoord pos, uint32_t timer_ticks);

    bool has_bridge_at(TileCoord pos) const;
    int  get_bridge_stage(TileCoord pos) const;
    void set_bridge_at(TileCoord pos, int stage, uint32_t timer_ticks);

    void set_terrain(int32_t x, int32_t y, uint8_t terrain_type);
    void set_tile_flags(int32_t x, int32_t y, uint16_t flags);
    void set_anthill(uint8_t team_id, TileCoord pos);

    /// Hill waiting ring (ANTHILLQ): Order(home). The ant walks to the entrance or waits on the ring tiles.
    void join_base_queue(uint32_t ant_id);
    void leave_base_queue(uint32_t ant_id);
    bool is_ant_in_base_queue(uint32_t ant_id) const;              // heading for or waiting at the ring
    uint32_t get_active_depositing_ant(uint8_t player_id) const;   // the ant of the team playing the enter clip, or 0
    size_t get_base_queue_size(uint8_t player_id) const;           // ants waiting at the ring

    /// The thief starts its raid now (test hook): atcr501 on the raid tile, loot = min(victim score, 50).
    void start_thief_infiltration(uint32_t ant_id, uint8_t target_team_id);

    bool has_lunchbox_at(TileCoord pos) const;
    uint32_t get_lunchbox_points(TileCoord pos) const;

    void apply_knockback(uint32_t ant_id, int32_t from_px, int32_t from_py, int32_t min_tiles, int32_t max_tiles);
    void resolve_fire_contact(uint32_t ant_id, int32_t incoming_dx, int32_t incoming_dy);
    /// Test hook: Blast(0, 7) on the ants that stand on `tile` (the pile-up dispersal of WalkStep block C).
    void blast_tile_for_test(TileCoord tile);

    int32_t get_display_score(uint8_t player_id) const;
    int32_t get_player_score(uint8_t player_id) const;
    void set_player_score(uint8_t player_id, int32_t score);

    uint32_t get_player_eggs(uint8_t player_id) const;
    void set_player_eggs(uint8_t player_id, uint32_t eggs);

    uint32_t get_player_hatched(uint8_t player_id) const;
    uint8_t get_ally_id(uint8_t player_id) const;

    void record_player_stat(uint8_t player_id, StatType stat, uint32_t value);

private:
    uint32_t group_order(const std::vector<uint32_t>& ant_ids, TileCoord target, bool special, uint32_t* needed);
    std::unique_ptr<SimulationEngineImpl> impl_;
};

} // namespace ants::sim
