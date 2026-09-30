// Golden tests of the hill actions of the original game (Ants.exe): entering the own hill (deposit and heal at the end
// of the ?h0 clip), the waiting ring (ANTHILLQ), hatching (8000 ms, one egg at a time) and the thief's raid (atcr501).
// Numbers come from the clip tables of ants.chd and the decoded routines (docs/GAME_REVERSE_ENGINEERING.md 5.35).
#include "ants_sim/sim_engine.hpp"
#include "ants_sim/movement_tables.hpp"

#include <cstdint>
#include <functional>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

using namespace ants::sim;

static int g_test_count = 0;
static int g_test_failures = 0;
static int g_assert_count = 0;

inline void run_test_case(const std::string& name, const std::function<void()>& fn) {
    ++g_test_count;
    std::cout << "  RUNNING: " << std::left << std::setw(72) << name << " ... " << std::flush;
    const int prev = g_test_failures;
    try {
        fn();
    } catch (const std::exception& e) {
        std::cout << "FAILED! Exception: " << e.what() << "\n";
        ++g_test_failures;
        return;
    }
    if (g_test_failures == prev) std::cout << "PASS\n";
}

#define TEST_CASE(name) run_test_case(name, [&]()
#define TEST_END() );
#define ASSERT_TRUE(cond) \
    do { \
        ++g_assert_count; \
        if (!(cond)) { \
            std::cout << "FAILED!\n    Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_test_failures; \
            return; \
        } \
    } while (0)
#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))
#define ASSERT_EQ(a, b) ASSERT_TRUE((a) == (b))
#define ASSERT_NEAR(a, b, tol) ASSERT_TRUE(((a) > (b) ? (a) - (b) : (b) - (a)) <= (tol))

namespace {

constexpr int kTickMs = 50;

// Two hills: team 0 at (20,20) (entrance (21,21), ring tile (19,23), tile42 (24,24), raid tile (23,22)),
// team 1 at (40,40).
void make_world(SimulationEngine& sim, uint32_t seed = 11) {
    sim.init_test_world(60, 60, seed, 720000);
    sim.set_anthill(0, TileCoord{20, 20});
    sim.set_anthill(1, TileCoord{40, 40});
    sim.set_player_score(0, 0);
    sim.set_player_score(1, 0);
}

// Ticks until the score of `player` equals `value` (true) or `max_ticks` passed (false).
bool wait_until_score(SimulationEngine& sim, uint8_t player, int32_t value, int max_ticks) {
    for (int t = 0; t < max_ticks; ++t) {
        if (sim.get_player_score(player) == value) return true;
        sim.tick();
    }
    return sim.get_player_score(player) == value;
}

bool news_has(SimulationEngine& sim, uint8_t player, uint16_t id) {
    return sim.has_news_event(player, id);
}

// First frame duration of the ?h0 clips (the first frame's time is booked twice when the clip is started inside a
// step callback, so the clip ends that much later than its nominal length).
uint32_t first_frame_ms(uint8_t type) {
    return movement::action_clip(movement::ActionClip::Enter, type, 0, false).duration(0);
}

struct EnterTimes { int start_tick{-1}; int end_tick{-1}; };

// Runs an ant of `type` standing on the entrance with the given health and food until the enter clip ends.
EnterTimes run_enter(SimulationEngine& sim, uint32_t& id, AntType type, uint16_t hp, bool food) {
    id = sim.spawn_unit(0, type, TileCoord{21, 21});
    auto& u = sim.get_unit(id);
    u.hp = hp;
    if (food) u.pick_up_food(1, 25);
    sim.join_base_queue(id);
    EnterTimes t;
    for (int tick = 0; tick < 200; ++tick) {
        sim.tick();
        const bool entering = (sim.get_unit(id).state == UnitState::EnteringBase);
        if (entering && t.start_tick < 0) t.start_tick = tick;
        if (!entering && t.start_tick >= 0) { t.end_tick = tick; break; }
    }
    return t;
}

} // namespace

int main() {
    std::cout << "=======================================================\n"
              << " Ants hill actions: enter, ring, hatch, raid\n"
              << "=======================================================\n";

    TEST_CASE("1.1 Enter clip lasts the table length plus (10 - hp) * 200 ms - 40 ms, every type") {
        struct Row { AntType type; uint32_t total_ms; };
        const Row rows[] = { {AntType::Worker, 1000}, {AntType::Bomber, 1240}, {AntType::Fire, 1240},
                             {AntType::Thief, 1000}, {AntType::Combat, 1160}, {AntType::Swimmer, 880} };
        for (const Row& r : rows) {
            for (uint16_t hp : {uint16_t{10}, uint16_t{5}, uint16_t{1}}) {
                SimulationEngine sim;
                make_world(sim);
                uint32_t id = 0;
                const EnterTimes t = run_enter(sim, id, r.type, hp, false);
                ASSERT_TRUE(t.start_tick >= 0 && t.end_tick > t.start_tick);
                const uint32_t stretch = (hp < 10) ? (10u - hp) * 200u - 40u : 0u;
                const uint32_t expect = r.total_ms + stretch + first_frame_ms(static_cast<uint8_t>(r.type));
                const uint32_t got = static_cast<uint32_t>(t.end_tick - t.start_tick) * kTickMs;
                ASSERT_NEAR(got, expect, 60u);
            }
        }
    } TEST_END();

    TEST_CASE("1.2 Food scores and the ant heals only when the clip ends, not before") {
        SimulationEngine sim;
        make_world(sim);
        uint32_t id = 0;
        id = sim.spawn_unit(0, AntType::Worker, TileCoord{21, 21});
        sim.get_unit(id).hp = 5;
        sim.get_unit(id).pick_up_food(1, 25);
        sim.join_base_queue(id);
        int start = -1, end = -1;
        for (int tick = 0; tick < 200 && end < 0; ++tick) {
            sim.tick();
            const auto& u = sim.get_unit(id);
            if (u.state == UnitState::EnteringBase) {
                if (start < 0) start = tick;
                // during the whole clip: no score, no heal, still carrying
                ASSERT_EQ(sim.get_player_score(0), 0);
                ASSERT_EQ(u.hp, 5u);
                ASSERT_TRUE(u.is_holding());
            } else if (start >= 0) {
                end = tick;
            }
        }
        ASSERT_TRUE(end > start && start >= 0);
        const auto& u = sim.get_unit(id);
        ASSERT_EQ(sim.get_player_score(0), 25);
        ASSERT_EQ(u.hp, 10u);
        ASSERT_FALSE(u.is_holding());
        ASSERT_TRUE(news_has(sim, 0, 61));                 // "Score going up..."
    } TEST_END();

    TEST_CASE("1.3 An entering ant stays a target on its tile, ignores orders and leaves for tile42") {
        SimulationEngine sim;
        make_world(sim);
        const uint32_t id = sim.spawn_unit(0, AntType::Worker, TileCoord{21, 21});
        sim.get_unit(id).pick_up_food(1, 25);
        sim.join_base_queue(id);
        for (int t = 0; t < 60 && sim.get_unit(id).state != UnitState::EnteringBase; ++t) sim.tick();
        ASSERT_EQ(sim.get_unit(id).state, UnitState::EnteringBase);
        ASSERT_TRUE(sim.has_living_ant_at(TileCoord{21, 21}));   // there is no underground: the ant keeps its tile
        AntOrder o{};
        o.ant_id = id;
        o.type = OrderType::Move;
        o.target_x = 30;
        o.target_y = 30;
        sim.issue_order(o);                                 // refused while the clip runs
        sim.tick();
        ASSERT_EQ(sim.get_unit(id).state, UnitState::EnteringBase);
        for (int t = 0; t < 200; ++t) sim.tick();
        ASSERT_EQ(sim.get_unit(id).pos.x, 24);              // tile42 = (24, 24): the carried food had no source tile
        ASSERT_EQ(sim.get_unit(id).pos.y, 24);
    } TEST_END();

    TEST_CASE("1.4 A second ant waits on the ring tile and is sent in by ANTHILLQ when the entrance is free") {
        SimulationEngine sim;
        make_world(sim);
        const uint32_t first = sim.spawn_unit(0, AntType::Worker, TileCoord{21, 28});
        const uint32_t second = sim.spawn_unit(0, AntType::Worker, TileCoord{18, 28});
        sim.get_unit(first).pick_up_food(1, 25);
        sim.get_unit(second).pick_up_food(1, 25);
        sim.join_base_queue(first);
        sim.join_base_queue(second);
        // the second order finds the entrance claimed and goes to the alternative waiting tile (19, 23)
        bool second_queued = false;
        int first_enter = -1, second_enter = -1;
        for (int t = 0; t < 400; ++t) {
            sim.tick();
            if (sim.get_unit(second).home_state == 2) second_queued = true;
            if (first_enter < 0 && sim.get_unit(first).state == UnitState::EnteringBase) first_enter = t;
            if (second_enter < 0 && sim.get_unit(second).state == UnitState::EnteringBase) second_enter = t;
        }
        // The ant that reaches the ring tile first is queued (first come first served): an ant that has not joined
        // the queue may not take the entrance while another one waits, so the other ant gives way and waits too.
        ASSERT_TRUE(second_queued);
        ASSERT_TRUE(first_enter >= 0 && second_enter >= 0);
        ASSERT_EQ(sim.get_player_score(0), 50);            // both delivered, one after the other
        const int gap = (first_enter > second_enter ? first_enter - second_enter : second_enter - first_enter) * kTickMs;
        ASSERT_TRUE(gap >= 1000);                          // the entrance is free again only after the first clip
    } TEST_END();

    TEST_CASE("1.5 A click-ordered ant is queued with priority and goes first; a new order releases the queue place") {
        SimulationEngine sim;
        make_world(sim);
        const uint32_t blocker = sim.spawn_unit(0, AntType::Worker, TileCoord{21, 21});   // stands on the entrance
        const uint32_t early = sim.spawn_unit(0, AntType::Worker, TileCoord{19, 26});
        const uint32_t clicked = sim.spawn_unit(0, AntType::Worker, TileCoord{12, 25});
        sim.get_unit(early).pick_up_food(1, 25);
        sim.get_unit(clicked).pick_up_food(1, 25);
        sim.join_base_queue(early);                      // automatic order: priority 0, time = arrival at the ring
        AntOrder o{};
        o.ant_id = clicked;
        o.type = OrderType::Move;                        // the player clicks the hill: priority 1, time 0
        o.target_x = 21;
        o.target_y = 21;
        sim.issue_order(o);
        for (int t = 0; t < 400 && sim.get_base_queue_size(0) < 2u; ++t) sim.tick();
        ASSERT_EQ(sim.get_base_queue_size(0), 2u);
        ASSERT_EQ(sim.get_unit(early).home_priority, 0);
        ASSERT_TRUE(sim.get_unit(early).home_time_ms > 0u);
        ASSERT_EQ(sim.get_unit(clicked).home_priority, 1);
        ASSERT_EQ(sim.get_unit(clicked).home_time_ms, 0u);
        ASSERT_EQ(sim.get_player_score(0), 0);           // the blocker keeps the entrance: nobody is admitted
        // the blocker leaves: the priority ant enters first although the other one has waited longer
        sim.issue_move_order(blocker, TileCoord{30, 30});
        uint32_t first_in = 0;
        for (int t = 0; t < 200 && first_in == 0; ++t) {
            sim.tick();
            first_in = sim.get_active_depositing_ant(0);
        }
        ASSERT_EQ(first_in, clicked);
        // a new order (a player stop, here a move elsewhere) takes the other ant out of the queue
        ASSERT_TRUE(sim.is_ant_in_base_queue(early));
        sim.issue_move_order(early, TileCoord{25, 40});
        ASSERT_FALSE(sim.is_ant_in_base_queue(early));
        ASSERT_EQ(sim.get_base_queue_size(0), 0u);
    } TEST_END();

    TEST_CASE("1.6 Ants that find the entrance blocked take the waiting tile and then the ring around it in scan order") {
        SimulationEngine sim;
        make_world(sim);
        sim.spawn_unit(0, AntType::Worker, TileCoord{21, 21});                            // stands on the entrance
        // one step from their future places, so that the walking ants do not get in each other's way
        const TileCoord start[4] = { {19, 24}, {17, 22}, {17, 23}, {17, 24} };
        std::vector<uint32_t> ids;
        for (int i = 0; i < 4; ++i) ids.push_back(sim.spawn_unit(0, AntType::Worker, start[i]));
        for (uint32_t id : ids) sim.join_base_queue(id);
        // waiting tile (19, 23) first, then ring 1 around it: left column top to bottom (18, 22), (18, 23), (18, 24)
        const TileCoord expect[4] = { {19, 23}, {18, 22}, {18, 23}, {18, 24} };
        for (int i = 0; i < 4; ++i) ASSERT_EQ(sim.get_unit(ids[static_cast<size_t>(i)]).orig_order_tile, expect[i]);
        for (int t = 0; t < 300 && sim.get_base_queue_size(0) < 4u; ++t) sim.tick();
        ASSERT_EQ(sim.get_base_queue_size(0), 4u);
        for (int i = 0; i < 4; ++i) ASSERT_EQ(sim.get_unit(ids[static_cast<size_t>(i)]).pos, expect[i]);
    } TEST_END();

    TEST_CASE("2.1 Hatch: checks in the original order, cost min(score, 200), 8000 ms, one at a time") {
        SimulationEngine sim;
        make_world(sim);
        sim.set_player_eggs(0, 0);
        ASSERT_EQ(static_cast<int>(sim.try_hatch(0)), static_cast<int>(SimulationEngine::HatchResult::NoEggs));
        ASSERT_TRUE(news_has(sim, 0, 16));                 // "No eggs to hatch!"
        sim.set_player_eggs(0, 3);
        sim.set_player_score(0, 150);
        sim.clear_audio_events();
        ASSERT_EQ(static_cast<int>(sim.try_hatch(0)), static_cast<int>(SimulationEngine::HatchResult::NotEnoughPoints));
        ASSERT_TRUE(news_has(sim, 0, 13));                 // "You need 200 points to hatch!"
        ASSERT_TRUE(sim.has_audio_event(61));               // canthatch = antstop.wav
        ASSERT_EQ(sim.get_player_score(0), 150);
        sim.set_player_score(0, 500);
        ASSERT_EQ(static_cast<int>(sim.try_hatch(0)), static_cast<int>(SimulationEngine::HatchResult::Started));
        ASSERT_TRUE(news_has(sim, 0, 15));                 // "Hatching a new Ant!"
        ASSERT_EQ(sim.get_player_score(0), 300);
        ASSERT_EQ(sim.get_player_eggs(0), 2u);
        ASSERT_EQ(static_cast<int>(sim.try_hatch(0)), static_cast<int>(SimulationEngine::HatchResult::AlreadyHatching));
        ASSERT_TRUE(news_has(sim, 0, 14));                 // "An Ant is already hatching!"
        ASSERT_EQ(sim.get_player_score(0), 300);
        // no ant exists during the incubation
        for (int t = 0; t < 158; ++t) sim.tick();
        ASSERT_EQ(sim.get_world_state().ants.size(), 0u);
        for (int t = 0; t < 5; ++t) sim.tick();
        ASSERT_EQ(sim.get_world_state().ants.size(), 1u);
        ASSERT_TRUE(news_has(sim, 0, 63));                 // "Ready!"
        ASSERT_TRUE(sim.has_audio_event(SoundID::ExitHill));
    } TEST_END();

    TEST_CASE("2.2 The forced hatch (last ant died) costs min(score, 200) without the 200 point rule") {
        SimulationEngine sim;
        make_world(sim);
        sim.set_player_eggs(0, 2);
        sim.set_player_score(0, 40);
        ASSERT_EQ(static_cast<int>(sim.try_hatch(0, AntType::Worker, true)), static_cast<int>(SimulationEngine::HatchResult::Started));
        ASSERT_EQ(sim.get_player_score(0), 0);
        ASSERT_EQ(sim.get_player_eggs(0), 1u);
    } TEST_END();

    TEST_CASE("2.3 The newborn appears on the entrance playing the hatch clip and then walks to tile42") {
        SimulationEngine sim;
        make_world(sim);
        sim.set_player_eggs(0, 1);
        sim.set_player_score(0, 200);
        ASSERT_EQ(static_cast<int>(sim.try_hatch(0)), static_cast<int>(SimulationEngine::HatchResult::Started));
        for (int t = 0; t < 165; ++t) sim.tick();
        ASSERT_EQ(sim.get_world_state().ants.size(), 1u);
        const auto& snap = sim.get_world_state().ants[0];
        ASSERT_EQ(snap.type, AntType::Worker);
        ASSERT_EQ(snap.hp, 10u);
        ASSERT_EQ(snap.state, UnitState::EnteringBase);
        ASSERT_EQ(snap.px, 21 * 32 + 16);
        ASSERT_EQ(snap.py, 21 * 32 + 16);
        ASSERT_EQ(snap.loco_clip, movement::action_clip(movement::ActionClip::Hatch, 0, 0, false).chd_index);
        for (int t = 0; t < 200; ++t) sim.tick();
        ASSERT_EQ(sim.get_world_state().ants[0].tile_x, 24);
        ASSERT_EQ(sim.get_world_state().ants[0].tile_y, 24);
    } TEST_END();

    TEST_CASE("2.4 A hatch waits while an own ant stands on the entrance and appears as soon as it is free") {
        SimulationEngine sim;
        make_world(sim);
        sim.set_player_eggs(0, 1);
        sim.set_player_score(0, 200);
        const uint32_t blocker = sim.spawn_unit(0, AntType::Worker, TileCoord{21, 21});
        ASSERT_EQ(static_cast<int>(sim.try_hatch(0)), static_cast<int>(SimulationEngine::HatchResult::Started));
        for (int t = 0; t < 200; ++t) sim.tick();
        ASSERT_EQ(sim.get_world_state().ants.size(), 1u);     // only the blocker: the egg still waits
        ASSERT_EQ(sim.get_pending_hatch_count(0), 1u);
        AntOrder o{};
        o.ant_id = blocker;
        o.type = OrderType::Move;
        o.target_x = 26;
        o.target_y = 26;
        sim.issue_order(o);
        int appeared = -1;
        for (int t = 0; t < 200 && appeared < 0; ++t) {
            sim.tick();
            if (sim.get_world_state().ants.size() == 2) appeared = t;
        }
        ASSERT_TRUE(appeared >= 0);
        ASSERT_EQ(sim.get_pending_hatch_count(0), 0u);
    } TEST_END();

    TEST_CASE("3.1 Raid: 3510 ms clip on the raid tile, loot min(score, 50) moves at the end, thief goes home") {
        SimulationEngine sim;
        make_world(sim);
        sim.set_player_score(1, 120);
        const uint32_t thief = sim.spawn_unit(0, AntType::Thief, TileCoord{43, 42});
        sim.clear_audio_events();
        sim.start_thief_infiltration(thief, 1);
        // the owner of the raided hill hears the anthill cue and reads the warning at the start
        ASSERT_TRUE(sim.has_audio_event(SoundID::Anthill));
        ASSERT_TRUE(news_has(sim, 1, 53));
        const auto& u = sim.get_unit(thief);
        ASSERT_EQ(u.state, UnitState::Infiltrating);
        ASSERT_EQ(u.pixel_x, 40 * 32 + 3 * 32 + 16);
        ASSERT_EQ(u.pixel_y, 40 * 32 + 2 * 32 + 16);
        int end_tick = -1;
        for (int t = 0; t < 120 && end_tick < 0; ++t) {
            sim.tick();
            if (sim.get_unit(thief).state != UnitState::Infiltrating) end_tick = t;
            else ASSERT_EQ(sim.get_player_score(1), 120);     // nothing moves before the clip ends
        }
        ASSERT_TRUE(end_tick > 0);
        ASSERT_NEAR(static_cast<uint32_t>((end_tick + 1) * kTickMs), 3510u, 100u);
        ASSERT_EQ(sim.get_player_score(1), 70);
        ASSERT_TRUE(sim.get_unit(thief).is_holding());
        ASSERT_EQ(sim.get_unit(thief).carried_points, 50u);
        ASSERT_TRUE(sim.get_unit(thief).is_thief_steal);
        ASSERT_TRUE(news_has(sim, 0, 62));                    // "Food stolen..." goes to the thief's owner
        ASSERT_EQ(sim.get_unit(thief).orig_order, AntUnit::kOrderHome);
    } TEST_END();

    TEST_CASE("3.2 Raid loot is min(victim score, 50); an empty victim leaves the thief idle and empty-handed") {
        for (int score : {30, 0}) {
            SimulationEngine sim;
            make_world(sim);
            sim.set_player_score(1, score);
            const uint32_t thief = sim.spawn_unit(0, AntType::Thief, TileCoord{43, 42});
            sim.start_thief_infiltration(thief, 1);
            for (int t = 0; t < 90; ++t) sim.tick();
            ASSERT_EQ(sim.get_player_score(1), 0);
            ASSERT_EQ(sim.get_unit(thief).is_holding(), score > 0);
            ASSERT_EQ(sim.get_unit(thief).carried_points, static_cast<uint16_t>(score));
            if (score == 0) {
                ASSERT_TRUE(sim.get_unit(thief).is_thief_steal);          // the loot flag is set even for nothing
                ASSERT_EQ(sim.get_unit(thief).state, UnitState::Idle);   // and it just stands on the raid tile
            }
        }
    } TEST_END();

    TEST_CASE("3.3 A thief that already carries food refuses the raid and goes home") {
        SimulationEngine sim;
        make_world(sim);
        sim.set_player_score(1, 100);
        const uint32_t thief = sim.spawn_unit(0, AntType::Thief, TileCoord{43, 40});
        sim.get_unit(thief).pick_up_food(1, 25);
        AntOrder o{};
        o.ant_id = thief;
        o.type = OrderType::InfiltrateAnthill;
        o.target_x = 43;
        o.target_y = 42;
        sim.issue_order(o);
        for (int t = 0; t < 120; ++t) sim.tick();
        ASSERT_TRUE(news_has(sim, 0, 17));                    // "Can't - already have food."
        ASSERT_EQ(sim.get_player_score(1), 100);
        ASSERT_EQ(sim.get_unit(thief).state == UnitState::Infiltrating, false);
    } TEST_END();

    // ---- scores are never clamped (AddScore 0x1010cc9 just adds; the loot compare at 0x101d57f is signed) ---------------------------
    TEST_CASE("3.4 Two thieves raiding a 60-point victim together: each loot is 50, fixed at arrival; the victim ends at -40") {
        SimulationEngine sim;
        make_world(sim);
        sim.set_player_score(1, 60);
        const uint32_t t1 = sim.spawn_unit(0, AntType::Thief, TileCoord{43, 42});
        const uint32_t t2 = sim.spawn_unit(0, AntType::Thief, TileCoord{43, 41});
        sim.start_thief_infiltration(t1, 1);
        sim.start_thief_infiltration(t2, 1);
        for (int t = 0; t < 90; ++t) sim.tick();
        ASSERT_EQ(sim.get_player_score(1), -40);                                        // the original keeps the real number
        ASSERT_EQ(sim.get_unit(t1).carried_points, 50u);
        ASSERT_EQ(sim.get_unit(t2).carried_points, 50u);                                // 100 points taken from a victim that had 60
    } TEST_END();

    TEST_CASE("3.5 A raid on a victim below zero takes a negative loot: the victim gets the points back, the thief carries the debt home") {
        SimulationEngine sim;
        make_world(sim);
        sim.set_player_score(1, -40);
        sim.set_player_score(0, 100);
        const uint32_t thief = sim.spawn_unit(0, AntType::Thief, TileCoord{43, 42});
        sim.start_thief_infiltration(thief, 1);                                         // loot = min(-40, 50) = -40 (signed compare)
        for (int t = 0; t < 90; ++t) sim.tick();
        ASSERT_EQ(sim.get_player_score(1), 0);                                          // AddScore(victim, -(-40))
        ASSERT_TRUE(sim.get_unit(thief).is_holding());                                  // SetHolding of a non-zero loot
        ASSERT_TRUE(sim.get_unit(thief).is_thief_steal);
        ASSERT_TRUE(wait_until_score(sim, 0, 60, 900));                                 // the deposit adds the carried -40 to the thief's team
        ASSERT_EQ(sim.get_player_score(1), 0);
    } TEST_END();

    TEST_CASE("3.6 A hill that sinks below zero shows 0 on the score box and its real value in the stats") {
        SimulationEngine sim;
        make_world(sim);
        sim.set_player_score(1, 60);
        sim.set_player_score(0, 0);
        const uint32_t t1 = sim.spawn_unit(0, AntType::Thief, TileCoord{43, 42});
        const uint32_t t2 = sim.spawn_unit(0, AntType::Thief, TileCoord{43, 41});
        sim.start_thief_infiltration(t1, 1);
        sim.start_thief_infiltration(t2, 1);
        for (int t = 0; t < 90; ++t) sim.tick();
        ASSERT_EQ(sim.get_player_stats(1).score, -40);
        ASSERT_EQ(sim.get_world_state().player_scores[1], -40);                         // the snapshot carries the real number; only the digits are clamped (FUN_01010452)
    } TEST_END();

    // ---- the waiting ring of the hill: the queue flag (+0x68) of an ant that is re-routed or blocked ---------------------
    // Ants.exe FUN_0101c4f2 REPATH: +0x68 is cleared and wasHome remembered (0x101c935), the Order is given again and the flag is restored to 1
    // (0x101cad1); a blocked last tile stops the ant and sets +0x68 = 2 whatever its order (0x101caf2). Before v0.0.52 the flag was lost on a
    // re-path and never set on a blocked ring tile, so a part of the ants of a crowd never deposited.
    TEST_CASE("4.1 An ant that is re-routed on its way to the ring keeps its queue flag and is queued at the end") {
        SimulationEngine sim;
        make_world(sim);
        sim.spawn_unit(0, AntType::Worker, TileCoord{21, 21});                       // sits on the entrance
        const uint32_t b = sim.spawn_unit(0, AntType::Worker, TileCoord{19, 30});
        sim.get_unit(b).pick_up_food(1, 25);
        sim.join_base_queue(b);
        ASSERT_EQ(sim.get_unit(b).home_state, 1);                                     // heading for the ring tile
        bool blocked_put = false;
        for (int t = 0; t < 400; ++t) {
            sim.tick();
            const auto& u = sim.get_unit(b);
            if (!blocked_put && u.pos.y <= 28) {                                      // a stationary ant appears on its path: the walk is re-planned
                sim.spawn_unit(0, AntType::Worker, TileCoord{19, 26});
                blocked_put = true;
            }
            if (u.home_state == 0) ASSERT_TRUE(false);                                // never loses the flag on the way
        }
        ASSERT_TRUE(blocked_put);
        ASSERT_EQ(sim.get_unit(b).home_state, 2);
        ASSERT_TRUE(sim.is_ant_in_base_queue(b));
    } TEST_END();

    TEST_CASE("4.2 A ring tile that is blocked when the ant arrives queues the ant where it stands") {
        SimulationEngine sim;
        make_world(sim);
        sim.spawn_unit(0, AntType::Worker, TileCoord{21, 21});                       // entrance taken
        const uint32_t b = sim.spawn_unit(0, AntType::Worker, TileCoord{19, 28});
        sim.get_unit(b).pick_up_food(1, 25);
        sim.join_base_queue(b);
        bool placed = false;
        for (int t = 0; t < 400; ++t) {
            sim.tick();
            if (!placed && sim.get_unit(b).pos.y <= 26) {                             // an enemy ant steps onto the ring tile just before it
                sim.spawn_unit(1, AntType::Worker, TileCoord{19, 23});
                placed = true;
            }
        }
        ASSERT_TRUE(placed);
        ASSERT_EQ(sim.get_unit(b).home_state, 2);
        ASSERT_TRUE(sim.is_ant_in_base_queue(b));
    } TEST_END();

    // The owner's report (v0.0.50): "I command six ants to go to the base, the first three queued up and went in, the other three cancelled their queue".
    for (int carriers : {6, 8}) {
        TEST_CASE(std::string("4.3 A click on the own hill sends ") + std::to_string(carriers) + " food carriers in: every one of them deposits") {
            SimulationEngine sim;
            make_world(sim);
            std::vector<uint32_t> ids;
            for (int i = 0; i < carriers; ++i) {
                const uint32_t id = sim.spawn_unit(0, AntType::Worker, TileCoord{22 + (i % 4) * 2, 28 + (i / 4) * 2});
                sim.get_unit(id).pick_up_food(1, 25);
                sim.get_unit(id).harvest_origin = TileCoord{45, 10};
                ids.push_back(id);
            }
            std::vector<uint32_t> group = ids;
            sim.issue_group_move_order(group, TileCoord{21, 21});
            std::vector<bool> entered(ids.size(), false);
            for (int t = 0; t < 1600; ++t) {                                          // 80 s
                sim.tick();
                for (size_t i = 0; i < ids.size(); ++i) {
                    if (sim.get_unit(ids[i]).state == UnitState::EnteringBase) entered[i] = true;
                }
            }
            for (size_t i = 0; i < ids.size(); ++i) ASSERT_TRUE(entered[i]);          // nobody is left standing on the ring
            ASSERT_EQ(sim.get_player_score(0), carriers * 25);
            for (uint32_t id : ids) ASSERT_FALSE(sim.get_unit(id).is_holding());
        } TEST_END();
    }

    TEST_CASE("4.4 A queued ant that dies no longer blocks the entrance: a fresh ant ordered home goes straight in") {
        SimulationEngine sim;
        make_world(sim);
        const uint32_t blocker = sim.spawn_unit(0, AntType::Worker, TileCoord{21, 21});
        const uint32_t q = sim.spawn_unit(0, AntType::Worker, TileCoord{19, 26});
        sim.get_unit(q).pick_up_food(1, 25);
        sim.join_base_queue(q);
        for (int t = 0; t < 200 && sim.get_unit(q).home_state != 2; ++t) sim.tick();
        ASSERT_EQ(sim.get_unit(q).home_state, 2);
        sim.kill_unit(q);
        sim.issue_move_order(blocker, TileCoord{30, 30});                             // the entrance is free now
        for (int t = 0; t < 60; ++t) sim.tick();
        const uint32_t w = sim.spawn_unit(0, AntType::Worker, TileCoord{24, 24});
        sim.get_unit(w).pick_up_food(1, 25);
        sim.issue_move_order(w, TileCoord{21, 21});
        ASSERT_EQ(sim.get_unit(w).orig_order_tile.x, 21);                             // straight to the entrance, not to the ring
        ASSERT_EQ(sim.get_unit(w).orig_order_tile.y, 21);
        ASSERT_EQ(sim.get_unit(w).home_state, 0);
    } TEST_END();

    TEST_CASE("4.5 The stale move order of a dead ant claims no tile") {
        SimulationEngine sim;
        make_world(sim);
        const uint32_t d = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        sim.issue_move_order(d, TileCoord{30, 30});
        for (int t = 0; t < 10; ++t) sim.tick();
        sim.kill_unit(d);
        const uint32_t e = sim.spawn_unit(0, AntType::Worker, TileCoord{12, 12});
        sim.issue_move_order(e, TileCoord{30, 30});
        ASSERT_EQ(sim.get_unit(e).orig_order_tile.x, 30);                             // not shifted to (29, 29)
        ASSERT_EQ(sim.get_unit(e).orig_order_tile.y, 30);
    } TEST_END();

    std::cout << "\n=======================================================\n"
              << " Total Test Cases: " << g_test_count << "\n"
              << " Total Assertions: " << g_assert_count << "\n"
              << " Failures:         " << g_test_failures << "\n"
              << "=======================================================\n";
    if (g_test_failures == 0) {
        std::cout << " >>> ALL HILL ACTION TESTS PASSED CLEANLY <<<\n";
        return 0;
    }
    std::cout << " >>> " << g_test_failures << " TEST(S) FAILED <<<\n";
    return 1;
}
