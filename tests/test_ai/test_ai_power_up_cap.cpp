// AI33: how many power-ups the bot of a gate map looks at (PowerUpTask::try_start, docs/BOTS.md "Power-ups"). Elsewhere a bot looks at the nearest three power-ups of the kind that it wants; on a gate map (its hill
// reaches no pile) it looks at every one, and each one it looks at costs a search of the whole map (a walking-cost field, kept for field_ttl_ticks), so a map of 65,536 cells and 1,000 power-ups made one look
// cost a thousand searches. It now looks at the nearest ones for as long as their searches add up to PowerUpTask::Params::gate_search_cells (never fewer than gate_min_candidates): 138 on a map of 60 x 60
// (more than every shipped map has of one kind), 12 on 200 x 200, 7 on 256 x 256.
// AI33.1 counts the searches of a gate map of 200 x 200 with 1,000 power-ups that are all too far for a trip (at most the limit, and the bot does search), AI33.2 shows that on a map of 60 x 60 the bot still gets past a hundred
// power-ups that it cannot reach to the 101st that it can, AI33.3 that a field is searched again only when its time is over.
#include "b41_helpers.hpp"

namespace {

using namespace b41;

// A field of `size` x `size` tiles with the bot's hill (seat 0) at (4, 4), eight workers of seat 0, a pile behind a wall across row 20 (fire for the first 60 tiles, the wall of the 60 x 60 field of the other tests,
// rock for the rest): the hill reaches no pile, so the bot plays a gate map. The other hills are those of the fight field (a field of 60 x 60), or all in the south, behind the wall (a larger field): then the
// whole north is of the bot's side, and it wants the kind of every power-up that lies there
void gate_world(sim::SimulationEngine& sim, uint32_t size) {
    sim.init_test_world(size, size, 3, 14400 * sim::TICK_MS);
    const int32_t edge = static_cast<int32_t>(size) - 10;
    const TileCoord south[4] = {{4, 4}, {4, edge}, {edge / 2, edge}, {edge, edge}};
    for (uint8_t p = 0; p < 4; ++p) sim.grid_mut().set_anthill(p, size == 60 ? kFightHills[p] : south[p]);
    place_crackers(sim, 12, 40, 25);
    for (int k = 0; k < 8; ++k) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{6 + k, 10});
    for (uint32_t x = 0; x < size; ++x) {
        if (x < 60) sim.set_fire_at(TileCoord{static_cast<int32_t>(x), 20}, 1000000);
        else sim.set_terrain(static_cast<int32_t>(x), 20, sim::TERRAIN_OBSTACLE);
    }
}

}  // namespace

void run_power_up_cap_tests() {
    TEST_CASE("AI33.1 A Gate Map Of 200 x 200 Tiles With 1,000 Power-Ups That Are All Too Far For A Trip: One Look Searches The Map For At Most 12 Of Them (Not For 1,000), And The Bot Does Search") {
        sim::SimulationEngine sim;
        gate_world(sim, 200);
        for (uint32_t k = 0; k < 1000; ++k) sim.grid_mut().place_powerup(static_cast<int32_t>(70 + k % 125), static_cast<int32_t>(2 + k / 125), static_cast<uint8_t>(sim::AntType::Fire));   // (in the north, 57 tiles and more from the ants: a walk to any of them is longer than a trip may be)
        Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(peace_plan(Level::Hard)));
        auto& bot = rig.as<StandardBot>();
        rig.run(60);                                                                               // (several looks: a field that was searched is kept for 100 ticks)
        ASSERT_TRUE(bot.tactics().gate_map);
        const PowerUpTask& powerups = bot.powerups();
        ASSERT_EQ(powerups.gate_candidates(200u * 200u), size_t{12});
        ASSERT_EQ(powerups.gate_candidates(65536u), size_t{7});
        ASSERT_EQ(powerups.gate_candidates(1000000000u), size_t{powerups.params().gate_min_candidates});
        ASSERT_TRUE(powerups.searches() >= 1u);
        ASSERT_TRUE(powerups.searches() <= powerups.gate_candidates(200u * 200u));
        ASSERT_EQ(powerups.started(), 0u);                                                         // (none that it looked at is near enough for a trip)
    } TEST_END();

    TEST_CASE("AI33.2 A Gate Map Of 60 x 60 Tiles: The Bot Gets Past A Hundred Power-Ups That It Cannot Reach (They Are Nearer) To The 101st, That It Can, And Sends An Ant For It (The Limit Is 138 There)") {
        sim::SimulationEngine sim;
        gate_world(sim, 60);
        for (int y = 21; y <= 24; ++y) {
            for (int x = 5; x <= 29; ++x) sim.grid_mut().place_powerup(x, y, static_cast<uint8_t>(sim::AntType::Fire));      // (100 of them below the wall of fire: nearer to the ants than the 101st)
        }
        sim.grid_mut().place_powerup(30, 3, static_cast<uint8_t>(sim::AntType::Fire));             // the one that a walk reaches, farther by the distance
        Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(peace_plan(Level::Hard)));
        auto& bot = rig.as<StandardBot>();
        ASSERT_EQ(bot.powerups().gate_candidates(60u * 60u), size_t{138});
        rig.run(200);
        ASSERT_TRUE(bot.tactics().gate_map);
        ASSERT_TRUE(bot.powerups().started() >= 1u);
        bool towards = false;
        for (const auto& go : bot.powerups().on_their_way()) towards = towards || go.second == sim::AntType::Fire;
        ASSERT_TRUE(towards || bot.powerups().taken() >= 1u);
        ASSERT_TRUE(bot.powerups().searches() >= 101u);                                           // (it searched for every one of them up to the 101st)
    } TEST_END();

    TEST_CASE("AI33.3 A Field Is Searched Again Only When Its Time Is Over: The Looks Of 90 Ticks Make No New Search Of The Power-Ups That Were Looked At, The Look After 100 Ticks Makes Them Again") {
        sim::SimulationEngine sim;
        gate_world(sim, 200);
        for (uint32_t k = 0; k < 40; ++k) sim.grid_mut().place_powerup(static_cast<int32_t>(70 + k), 5, static_cast<uint8_t>(sim::AntType::Fire));
        Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(peace_plan(Level::Hard)));
        auto& bot = rig.as<StandardBot>();
        rig.run(10);
        const uint64_t first = bot.powerups().searches();
        ASSERT_TRUE(first >= 1u);
        rig.run(80);                                                                               // (up to tick 90: the fields are younger than 100 ticks)
        ASSERT_EQ(bot.powerups().searches(), first);
        rig.run(60);                                                                               // (past tick 100 after the first look: the fields that are older than that are searched again)
        ASSERT_TRUE(bot.powerups().searches() > first);
    } TEST_END();
}
