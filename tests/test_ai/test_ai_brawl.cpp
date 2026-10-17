// AI30: the brawl of game type 187 (docs/BOTS.md, "187: the brawl"; BrawlTask). In 187 the standard bot plays the fight and nothing else: it forms packs on the enemy ants it can reach, sends its
// wounded home, orders a pack onto its victim again when the victim was thrown, and marches to the enemy hills when nobody can be struck. The worlds are made by hand (b41_helpers.hpp: four hills
// on a field of grass); whole matches run on shipped maps.
//
//   AI30.1   the view tells the game mode of the engine, a copy of it too
//   AI30.2   in 187 every level strikes enemy ants that nobody hit first, with attack orders, and sends no order of the food game; with the switch off (`brawl187`) it does what it did before
//   AI30.3   in the original's game the switch changes nothing: the same orders, to the tick
//   AI30.4   packs: at most two ants strike a victim, each victim its own pack, the wounded and the near first; the ants left over join the nearest fight (three on a victim)
//   AI30.5   a pack is ordered onto its victim again when a blow threw it to another tile, and the blows land (a lone victim loses hit points and is followed)
//   AI30.6   an ant with three hit points or fewer goes home to heal and is not sent to fight; healed, it strikes again
//   AI30.7   no walk, no order: enemy ants behind a wall and a hill that no walk reaches draw no attack and no march; the hill that a walk reaches is marched to
//   AI30.8   allies are never struck
//   AI30.9   an attack that is refused twice (the walk is not there though the analysis of the start says it is) is not ordered again for a while
//   AI30.10  whole matches on a shipped map: bit-reproducible, the commands replay into a fresh engine without any bot, and the brawl kills more than the food game's bot in 187
#include <algorithm>
#include <set>

#include "ai_test.hpp"
#include "b41_helpers.hpp"
#include "ants_ai/arena.hpp"

using namespace ai_test;
using namespace ants;
using namespace ants::ai;
using namespace b41;

namespace {

LevelPlan brawl_plan(Level level, bool brawl = true) {
    LevelPlan p = plan_for(level);
    p.brawl187 = brawl;
    return p;
}

// The 60 x 60 field of four hills in the given game type, with `mine` workers of seat 0 in a block at (x0, y0) and the enemy workers of seat 1 at the given tiles
struct Fight {
    sim::SimulationEngine sim;
    std::vector<uint32_t> mine;
    std::vector<uint32_t> foes;
    Fight(sim::GameMode mode, int mine_count, const std::vector<TileCoord>& foe_tiles, TileCoord at = TileCoord{12, 12}, uint32_t seed = 31) {
        sim.set_game_mode(mode);
        empty_field(sim, seed);
        for (int i = 0; i < mine_count; ++i) mine.push_back(sim.spawn_unit(0, sim::AntType::Worker, TileCoord{at.x + i % 4, at.y + i / 4}));
        for (const TileCoord t : foe_tiles) foes.push_back(sim.spawn_unit(1, sim::AntType::Worker, t));
    }
};

std::vector<Command> attacks_of(const Rig& rig) {
    std::vector<Command> out;
    for (const auto& e : rig.proposed) {
        if (e.second.type == CommandType::GroupAttack) out.push_back(e.second);
    }
    return out;
}

bool names(const Command& c, uint32_t ant) { return std::find(c.ants.begin(), c.ants.end(), ant) != c.ants.end(); }

}  // namespace

void run_brawl_tests() {
    TEST_CASE("AI30.1 The View Tells The Game Mode Of The Engine, Also A Copy Of It") {
        for (const sim::GameMode mode : {sim::GameMode::HighestScore, sim::GameMode::Kills187}) {
            Fight f(mode, 2, {TileCoord{40, 12}});
            const MapInfo map(f.sim);
            const BotView view = BotView::build(f.sim, 0, &map);
            ASSERT_TRUE(view.game_mode() == mode);
            const BotView copy(view);
            ASSERT_TRUE(copy.game_mode() == mode);
        }
    } TEST_END();

    TEST_CASE("AI30.2 In 187 Every Level Strikes Enemy Ants That Nobody Hit First, With Attack Orders And No Order Of The Food Game; With The Switch Off It Does What It Did Before") {
        for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) {
            size_t brawl_attacks = 0;
            {
                Fight f(sim::GameMode::Kills187, 4, {TileCoord{30, 12}, TileCoord{31, 12}});
                Rig rig(f.sim, 0, level, std::make_unique<StandardBot>(brawl_plan(level)), 4, 8);
                uint16_t least = 10;                                                              // (a victim that is hurt walks home and heals: the lowest it fell is what shows the blows)
                for (int t = 0; t < 900; ++t) {
                    rig.tick();
                    for (const uint32_t foe : f.foes) {
                        const sim::AntSnapshot* v = snapshot_of(f.sim, foe);
                        if (v != nullptr) least = std::min<uint16_t>(least, v->hp);
                    }
                }
                const std::vector<Command> attacks = attacks_of(rig);
                brawl_attacks = attacks.size();
                ASSERT_TRUE(!attacks.empty());
                for (const Command& c : attacks) {
                    ASSERT_TRUE(c.tile_x >= 20 && c.tile_y >= 6 && c.tile_y <= 26);                   // a tile where the enemy ants stand or were thrown (a victim is thrown from tile to tile)
                    ASSERT_TRUE(c.ants.size() <= 4);
                }
                ASSERT_EQ(rig.proposed_count(CommandType::GroupSpecial), 0u);
                ASSERT_TRUE(least < 10 || level == Level::Easy);                                  // the blows land (Easy's budget is a quarter of Hard's: it may still be on its way)
            }
            {
                Fight f(sim::GameMode::Kills187, 4, {TileCoord{30, 12}, TileCoord{31, 12}});
                Rig rig(f.sim, 0, level, std::make_unique<StandardBot>(brawl_plan(level, false)), 4, 8);
                rig.run(900);
                ASSERT_TRUE(rig.proposed_count(CommandType::GroupAttack) * 4 <= brawl_attacks);   // the food game's bot strikes back at a blow, and hunts what is hurt: it starts no fight of its own

            }
        }
    } TEST_END();

    TEST_CASE("AI30.3 In The Original's Game The Switch Changes Nothing: The Same Orders To The Tick, With Food And Without") {
        for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) {
            std::vector<std::pair<uint64_t, Command>> runs[2];
            for (int on = 0; on < 2; ++on) {
                sim::SimulationEngine sim;
                empty_field(sim, 32);
                add_pile(sim, 20, 10, 8, 30);
                for (int i = 0; i < 4; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{12 + i, 12});
                sim.spawn_unit(1, sim::AntType::Worker, TileCoord{30, 12});
                Rig rig(sim, 0, level, std::make_unique<StandardBot>(brawl_plan(level, on != 0)), 4, 8);
                rig.run(600);
                runs[on] = rig.proposed;
                ASSERT_TRUE(!runs[on].empty());
            }
            ASSERT_EQ(runs[0].size(), runs[1].size());
            for (size_t i = 0; i < runs[0].size() && i < runs[1].size(); ++i) {
                ASSERT_EQ(runs[0][i].first, runs[1][i].first);
                ASSERT_TRUE(runs[0][i].second.type == runs[1][i].second.type);
                ASSERT_TRUE(runs[0][i].second.ants == runs[1][i].second.ants);
                ASSERT_EQ(runs[0][i].second.tile_x, runs[1][i].second.tile_x);
                ASSERT_EQ(runs[0][i].second.tile_y, runs[1][i].second.tile_y);
            }
        }
    } TEST_END();

    TEST_CASE("AI30.4 Packs: At Most Two Ants Strike A Victim, Each Victim Has A Pack Of Its Own, And The Ants Left Over Join The Nearest Fight, Three On A Victim") {
        {   // six ants and three victims: three packs of two, three different victims
            Fight f(sim::GameMode::Kills187, 6, {TileCoord{30, 12}, TileCoord{34, 14}, TileCoord{30, 20}});
            Rig rig(f.sim, 0, Level::Hard, std::make_unique<StandardBot>(brawl_plan(Level::Hard)), 4, 8);
            rig.run(12);
            const std::vector<Command> attacks = attacks_of(rig);
            ASSERT_EQ(attacks.size(), 3u);
            std::set<std::pair<int, int>> victims;
            std::set<uint32_t> ants;
            for (const Command& c : attacks) {
                ASSERT_EQ(c.ants.size(), 2u);
                victims.insert({c.tile_x, c.tile_y});
                for (const uint32_t a : c.ants) ants.insert(a);
            }
            ASSERT_EQ(victims.size(), 3u);
            ASSERT_EQ(ants.size(), 6u);                                                           // nobody strikes twice
        }
        {   // five ants and one victim: two pack, the others join it, three at most
            Fight f(sim::GameMode::Kills187, 5, {TileCoord{30, 12}});
            Rig rig(f.sim, 0, Level::Hard, std::make_unique<StandardBot>(brawl_plan(Level::Hard)), 4, 8);
            rig.run(12);
            const std::vector<Command> attacks = attacks_of(rig);
            ASSERT_TRUE(!attacks.empty());
            std::set<uint32_t> ants;
            for (const Command& c : attacks) {
                for (const uint32_t a : c.ants) ants.insert(a);
            }
            ASSERT_EQ(ants.size(), 3u);                                                           // the cap of a crowd; the other two wait for another victim
        }
        {   // the wounded first: two victims at the same distance, one with 4 hit points, are struck in that order
            Fight f(sim::GameMode::Kills187, 2, {TileCoord{30, 11}, TileCoord{30, 13}});
            f.sim.get_unit(f.foes[1]).hp = 4;
            Rig rig(f.sim, 0, Level::Hard, std::make_unique<StandardBot>(brawl_plan(Level::Hard)), 4, 8);
            rig.run(12);
            const std::vector<Command> attacks = attacks_of(rig);
            ASSERT_TRUE(!attacks.empty());
            ASSERT_EQ(attacks[0].tile_y, 13);
        }
    } TEST_END();

    TEST_CASE("AI30.5 A Pack Is Ordered Onto Its Victim Again When A Blow Threw It To Another Tile: A Lone Victim Is Followed, The Blows Land And It Loses Hit Points") {
        Fight f(sim::GameMode::Kills187, 3, {TileCoord{30, 30}}, TileCoord{22, 22});
        Rig rig(f.sim, 0, Level::Hard, std::make_unique<StandardBot>(brawl_plan(Level::Hard)), 4, 8);
        uint16_t least = 10;
        for (int t = 0; t < 600; ++t) {
            rig.tick();
            const sim::AntSnapshot* v = snapshot_of(f.sim, f.foes[0]);
            if (v != nullptr) least = std::min<uint16_t>(least, v->hp);
        }
        const std::vector<Command> attacks = attacks_of(rig);
        std::set<std::pair<int, int>> tiles;
        for (const Command& c : attacks) tiles.insert({c.tile_x, c.tile_y});
        ASSERT_TRUE(tiles.size() >= 4);                                                           // the victim was thrown from tile to tile and the pack named each
        ASSERT_TRUE(least <= 4);                                                                  // the blows land, one about every 40 ticks
        ASSERT_TRUE(attacks.size() <= 80);                                                        // and the orders are not a flood: a pack is ordered again only when the victim moved or an ant stands idle
    } TEST_END();

    TEST_CASE("AI30.6 An Ant With Three Hit Points Or Fewer Goes Home To Heal And Is Not Sent To Fight; Healed, It Strikes Again") {
        Fight f(sim::GameMode::Kills187, 2, {TileCoord{30, 12}});
        f.sim.get_unit(f.mine[0]).hp = 2;
        Rig rig(f.sim, 0, Level::Hard, std::make_unique<StandardBot>(brawl_plan(Level::Hard)), 4, 8);
        rig.run(24);
        const HillInfo& hill = rig.map().hill(0);
        bool home = false;
        for (const auto& e : rig.proposed) {
            if (e.second.type == CommandType::GroupMove && names(e.second, f.mine[0])) home = home || (e.second.tile_x == hill.entrance.x && e.second.tile_y == hill.entrance.y);
            if (e.second.type == CommandType::GroupAttack) ASSERT_FALSE(names(e.second, f.mine[0]));
        }
        ASSERT_TRUE(home);
        for (const auto& e : rig.proposed) ASSERT_FALSE(e.second.type == CommandType::GroupAttack && names(e.second, f.mine[0]));
        // whole again, standing where the hill sent it: it joins the fight
        f.sim.get_unit(f.mine[0]).hp = 10;
        const size_t before = attacks_of(rig).size();
        rig.run(600);
        const std::vector<Command> attacks = attacks_of(rig);
        bool joined = false;
        for (size_t i = before; i < attacks.size(); ++i) joined = joined || names(attacks[i], f.mine[0]);
        ASSERT_TRUE(joined);
    } TEST_END();

    TEST_CASE("AI30.7 No Walk, No Order: Enemy Ants Behind A Wall And A Hill That No Walk Reaches Draw No Attack And No March; The Hill That A Walk Reaches Is Marched To") {
        {   // a wall across the field between the two sides: the enemy ant and its hill are on the other side
            Fight f(sim::GameMode::Kills187, 3, {TileCoord{40, 12}});
            for (int y = 0; y < 60; ++y) f.sim.set_terrain(30, y, sim::TERRAIN_OBSTACLE);
            Rig rig(f.sim, 0, Level::Hard, std::make_unique<StandardBot>(brawl_plan(Level::Hard)), 4, 8);
            rig.run(600);
            ASSERT_EQ(rig.proposed_count(CommandType::GroupAttack), 0u);
            ASSERT_EQ(rig.proposed_count(CommandType::GroupMove), 0u);
        }
        {   // no enemy ant in sight, the hill of team 1 across a wall and the hill of team 2 on the own side: the march goes to team 2's door (the hill's tile 42), team 1 is not a goal
            sim::SimulationEngine sim;
            sim.set_game_mode(sim::GameMode::Kills187);
            empty_field(sim, 33);
            for (int y = 0; y < 60; ++y) sim.set_terrain(30, y, sim::TERRAIN_OBSTACLE);
            for (int i = 0; i < 3; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{12 + i, 12});
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(brawl_plan(Level::Hard)), 4, 8);
            rig.run(40);
            const HillInfo& near = rig.map().hill(2);
            const HillInfo& across = rig.map().hill(1);
            size_t marches = 0;
            for (const auto& e : rig.proposed) {
                if (e.second.type != CommandType::GroupMove) continue;
                ++marches;
                ASSERT_EQ(e.second.tile_x, near.tile42.x);
                ASSERT_EQ(e.second.tile_y, near.tile42.y);
                ASSERT_FALSE(e.second.tile_x == across.tile42.x && e.second.tile_y == across.tile42.y);
            }
            ASSERT_TRUE(marches > 0);
        }
    } TEST_END();

    TEST_CASE("AI30.8 Allies Are Never Struck: An Ally's Ants Stand Where The Pack Would Go, And No Attack Names Their Tile") {
        sim::SimulationEngine sim;
        sim.set_game_mode(sim::GameMode::Kills187);
        empty_field(sim, 34);
        sim.form_alliance(0, 1);
        std::vector<uint32_t> mine;
        for (int i = 0; i < 3; ++i) mine.push_back(sim.spawn_unit(0, sim::AntType::Worker, TileCoord{12 + i, 12}));
        std::set<std::pair<int, int>> friends;
        for (int i = 0; i < 3; ++i) {
            sim.spawn_unit(1, sim::AntType::Worker, TileCoord{20 + i, 14});
            friends.insert({20 + i, 14});
        }
        sim.spawn_unit(2, sim::AntType::Worker, TileCoord{14, 40});
        Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(brawl_plan(Level::Hard)), 4, 8);
        rig.run(600);
        const std::vector<Command> attacks = attacks_of(rig);
        ASSERT_TRUE(!attacks.empty());                                                            // the enemy of both is struck
        for (const Command& c : attacks) ASSERT_TRUE(friends.count({c.tile_x, c.tile_y}) == 0 && c.tile_y >= 20);
    } TEST_END();

    TEST_CASE("AI30.9 An Attack That Is Refused Twice (The Walk Does Not Exist, Though The Analysis Of The Start Says It Does) Is Not Ordered Again For A While: A Fenced Enemy Ant Draws A Few Orders, Not A Flood") {
        Fight f(sim::GameMode::Kills187, 3, {TileCoord{30, 12}});
        Rig rig(f.sim, 0, Level::Hard, std::make_unique<StandardBot>(brawl_plan(Level::Hard)), 4, 8);
        for (int dy = -1; dy <= 1; ++dy) {                                                        // the rocks come after the analysis of the map
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx != 0 || dy != 0) f.sim.set_terrain(30 + dx, 12 + dy, sim::TERRAIN_OBSTACLE);
            }
        }
        rig.run(1500);
        const size_t attacks = rig.proposed_count(CommandType::GroupAttack);
        ASSERT_TRUE(attacks >= 2);                                                                // it tried
        ASSERT_TRUE(attacks <= 14);                                                               // and gave up for a while after two refusals (about two orders in each 300 ticks)
        ASSERT_EQ(f.sim.get_unit(f.foes[0]).hp, 10);
    } TEST_END();

    TEST_CASE("AI30.10 Whole Matches On A Shipped Map: Bit-Reproducible, The Commands Replay Into A Fresh Engine Without A Bot, And The Brawl Kills More Than The Food Game's Bot In 187") {
        const auto spec_of = [&](const char* map, uint32_t seed, bool first_brawl, bool second_brawl) {
            ArenaSpec s;
            s.level = &level_of(map);
            s.seed = seed;
            s.game_mode = sim::GameMode::Kills187;
            s.record = true;
            s.factory = [first_brawl, second_brawl](const BotSpec& b) {
                const bool brawl = b.seat == 0 ? first_brawl : second_brawl;
                return std::make_unique<StandardBot>(b.level, Style::Random, [brawl](LevelPlan& p) { p.brawl187 = brawl; });
            };
            for (uint8_t seat = 0; seat < 2; ++seat) {
                BotSpec b;
                b.seat = seat;
                b.level = Level::Hard;
                s.bots.push_back(b);
            }
            return s;
        };
        {   // determinism and the proof that the match is nothing but its commands
            const ArenaSpec s = spec_of("MEDIUM", 5, true, true);
            const ArenaResult one = play_match(s);
            const ArenaResult two = play_match(s);
            ASSERT_TRUE(one.error.empty());
            ASSERT_EQ(one.hash, two.hash);
            ASSERT_TRUE(one.seats[0].kills + one.seats[1].kills > 0);
            ASSERT_EQ(one.reachable_units_left, 0u);                                              // there is no food in 187
            const ReplayResult replay = replay_commands(s, one);
            ASSERT_TRUE(replay.ok);
            ASSERT_EQ(replay.hash, one.hash);
            ArenaSpec original = s;
            original.game_mode = sim::GameMode::HighestScore;                                     // the same commands in the original's game are another match
            ASSERT_FALSE(replay_commands(original, one).ok);
        }
        {   // the brawl against the food game's bot, both seatings, three maps: it kills far more than it loses
            uint32_t brawl_kills = 0;
            uint32_t old_kills = 0;
            for (const char* map : {"TINY", "SMALL", "MEDIUM"}) {
                for (const uint32_t seed : {1u, 2u}) {
                    const ArenaResult a = play_match(spec_of(map, seed, true, false));
                    const ArenaResult b = play_match(spec_of(map, seed, false, true));
                    ASSERT_TRUE(a.error.empty() && b.error.empty());
                    brawl_kills += a.seats[0].kills + b.seats[1].kills;
                    old_kills += a.seats[1].kills + b.seats[0].kills;
                }
            }
            ASSERT_TRUE(brawl_kills >= 2 * old_kills + 6);
        }
    } TEST_END();
}
