// The bench bot `gang2` (docs/BOT_PEOPLE.md, "The bench bot gang2"): a test-only opponent that plays more like a strong player than `gang` does. The scenes seat it at a rig of the real
// controller's profile (a look every 4 ticks, a reaction of 8) on the 60 by 60 field of grass with four hills and let the engine play what it orders; the enemy ants are still ants
// that nobody orders, so what the bot does is all that happens. Nothing here changes how a shipped bot plays.
//
//   AI37.1  the kind is known to the bench factory and not to make_bot, its orders are all allowed, it plays the same match twice alike, and it takes Thief and Combat power-ups in the opening
//   AI37.2  it strikes one victim until the engine sends it home: one order names every idle ant of the gang, the walking ants are told again, every blow falls on the same ant
//   AI37.3  a Thief or a Fire Ant is struck before a worker that is nearer
//   AI37.4  a gang ant with three hit points goes home, heals and comes back to strike
//   AI37.5  the Fire Ant and (with no raid open) the Thief are of the gang
//   AI37.6  a Thief raids a hill whose hole is open and not one whose hole is shut
//   AI37.7  an enemy ant that carries food is left alone
//   AI37.8  the first Combat Ant is the hunter: it walks to its post alone, and the second Combat Ant is of the gang
//   AI37.9  the nearer of two enemy ants is struck first, whichever is older
//   AI37.10 of two enemy ants at the same distance the one with fewer hit points is struck first, and an enemy ant that comes nearer later does not take the blows from the victim
//   AI37.11 with no enemy ant in reach the gang stands in front of the thief hole of the richest enemy hill
//   AI37.12 a hurt gang ant is in no strike order, the hunter goes home at 3 hit points, a Thief of the gang leaves it for a raid when a hole opens
//
// NOT COVERED: the hunter's own strike rules (an ant within three tiles, no Combat Ant, no group of three). A Combat Ant has the original's reflex: it strikes an enemy in reach by itself before
// the bot's first look, scatters the foes and is no longer idle, so no scene can show what the bot orders there (three scene designs were tried).
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "ai_test.hpp"
#include "ants_ai/arena.hpp"
#include "ants_ai/standard_bot.hpp"
#include "b41_helpers.hpp"
#include "bench_aggressor.hpp"
#include "bot_watch_people.hpp"

using namespace ai_test;
using namespace ants;
using namespace ants::ai;
using namespace b41;

namespace {

// A rig of the bench bot at seat 0 with the people numbers of the observer read after every tick
class Scene {
public:
    sim::SimulationEngine sim;
    watch::PeopleObserver seen;

    // The world must be built before start(): the rig analyses the map
    void start() { rig_ = std::make_unique<Rig>(sim, 0, Level::Hard, std::make_unique<bench::Gang2Bot>(Level::Hard), 4, 8); }
    void step() {
        rig_->tick();
        seen.tick(sim);
    }
    void run(uint64_t n) {
        for (uint64_t i = 0; i < n; ++i) step();
    }
    Rig& rig() { return *rig_; }
    uint64_t now() const { return sim.current_tick(); }
    // The blows that the bot struck
    std::vector<watch::Blow> blows_by_bot() const {
        std::vector<watch::Blow> out;
        if (!seen.started()) return out;
        for (const watch::Blow& b : seen.people().blows()) {
            if (b.striker_seat == 0) out.push_back(b);
        }
        return out;
    }

private:
    std::unique_ptr<Rig> rig_;
};

constexpr uint64_t kGangFrom = 700;            // the gang forms from this tick on

// Four workers of the bot in a block at (24..25, 24..25)
std::vector<uint32_t> block_of_workers(sim::SimulationEngine& sim, sim::AntType type = sim::AntType::Worker) {
    std::vector<uint32_t> ids;
    for (const TileCoord t : {TileCoord{24, 24}, TileCoord{25, 24}, TileCoord{24, 25}, TileCoord{25, 25}}) ids.push_back(sim.spawn_unit(0, type, t));
    return ids;
}

bool names_ant(const Rig& rig, CommandType type, uint32_t ant) {
    for (const auto& p : rig.proposed) {
        if (p.second.type == type && std::find(p.second.ants.begin(), p.second.ants.end(), ant) != p.second.ants.end()) return true;
    }
    return false;
}

size_t largest_order(const Rig& rig, CommandType type) {
    size_t most = 0;
    for (const auto& p : rig.proposed) {
        if (p.second.type == type) most = std::max(most, p.second.ants.size());
    }
    return most;
}

// A match on TREASURE of the standard bot at Hard (seat 0) against the bench bot gang2 at Hard (seat 1)
ArenaResult play_gang2_match(uint64_t ticks, uint32_t seed = 3) {
    ArenaSpec spec;
    spec.level = &level_of("TREASURE");
    spec.seed = seed;
    spec.max_ticks = ticks;
    BotSpec hard;
    hard.seat = 0;
    hard.kind = "standard";
    hard.level = Level::Hard;
    BotSpec gang2;
    gang2.seat = 1;
    gang2.kind = "gang2";
    gang2.level = Level::Hard;
    spec.bots = {hard, gang2};
    spec.extra_kinds = {"gang2"};
    spec.factory = [](const BotSpec& b) -> std::unique_ptr<Bot> {
        std::unique_ptr<Bot> bench = bench::make_bench_bot(b.kind, b.level);
        return bench != nullptr ? std::move(bench) : make_bot(b);
    };
    return play_match(spec);
}

}  // namespace

void run_gang2_tests() {
    TEST_CASE("AI37.1 The Bench Bot gang2: Known To The Bench Factory And Not To make_bot, Every Order Allowed By The Controller, A Match Played Twice Alike, And Thief And Combat Power-Ups Taken In The Opening") {
        ASSERT_FALSE(known_bot_kind("gang2"));
        BotSpec spec;
        spec.kind = "gang2";
        ASSERT_TRUE(make_bot(spec) == nullptr);
        ASSERT_EQ(bench::bench_kind_of("GANG2"), std::string("gang2"));
        ASSERT_EQ(bench::bench_kind_of("gang"), std::string("gang"));
        ASSERT_EQ(bench::bench_kind_of("gang3"), std::string());
        for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) ASSERT_EQ(std::string(bench::make_bench_bot("gang2", level)->kind()), std::string("gang2"));
        const ArenaResult first = play_gang2_match(6000);
        ASSERT_TRUE(first.error.empty());
        ASSERT_EQ(first.seats[1].runs, std::string("gang2"));
        ASSERT_EQ(first.seats[1].stats.filtered, 0u);
        ASSERT_EQ(first.seats[1].stats.rejected, 0u);
        ASSERT_TRUE(first.seats[1].banked > 0);                                                                  // it harvests
        ASSERT_TRUE(first.seats[1].attack_orders > 0);                                                           // it strikes
        const ArenaResult second = play_gang2_match(6000);
        ASSERT_EQ(first.hash, second.hash);
        const ArenaResult opening = play_gang2_match(2000);                                                      // the power-ups of the opening: Thieves first, then Combat Ants
        ASSERT_TRUE(opening.seats[1].took[static_cast<size_t>(sim::AntType::Thief)] >= 2u);
        ASSERT_TRUE(opening.seats[1].took[static_cast<size_t>(sim::AntType::Combat)] >= 1u);
    } TEST_END();

    TEST_CASE("AI37.2 One Victim Until It Goes Home: Four Workers 8 Tiles From Two Idle Enemy Workers Strike The Nearer One (The First Wins A Tie), One Order Names Every Idle Ant, The Walking Ants Are Told Again, And No Blow Falls On The Other Before The First Is Down To 1 Hit Point (The Engine Sends It Home Then)") {
        Scene s;
        empty_field(s.sim, 372);
        const std::vector<uint32_t> mine = block_of_workers(s.sim);
        const uint32_t first = s.sim.spawn_unit(1, sim::AntType::Worker, TileCoord{33, 24});
        const uint32_t second = s.sim.spawn_unit(1, sim::AntType::Worker, TileCoord{33, 33});
        s.start();
        s.run(kGangFrom + 700);
        const std::vector<watch::Blow> blows = s.blows_by_bot();
        ASSERT_TRUE(blows.size() >= 9);
        for (size_t i = 0; i < 9; ++i) ASSERT_EQ(blows[i].victim, first);                                        // (ten hit points: nine blows take it to 1)
        for (const watch::Blow& b : blows) {
            if (b.victim == second) break;
            ASSERT_EQ(b.victim, first);
        }
        ASSERT_TRUE(largest_order(s.rig(), CommandType::GroupAttack) >= 3);                                      // one order names the gang
        size_t told = 0;                                                                                         // the ants that walk to the target are told again as it moves
        for (const auto& p : s.rig().proposed) {
            if (p.first <= kGangFrom + 150 && p.second.type == CommandType::GroupAttack && std::find(p.second.ants.begin(), p.second.ants.end(), mine[0]) != p.second.ants.end()) ++told;
        }
        ASSERT_TRUE(told >= 3);
        std::map<uint32_t, uint64_t> last_named;                                                                 // an ant is named again after 12 ticks at the earliest (it has not begun the last order before)
        for (const auto& p : s.rig().proposed) {
            if (p.second.type != CommandType::GroupAttack) continue;
            for (const uint32_t id : p.second.ants) {
                const auto it = last_named.find(id);
                if (it != last_named.end()) ASSERT_TRUE(p.first >= it->second + 12);
                last_named[id] = p.first;
            }
        }
        ASSERT_TRUE(s.rig().as<bench::Gang2Bot>().strike_orders() > 0);
    } TEST_END();

    TEST_CASE("AI37.3 A Thief Or A Fire Ant Is Struck Before A Worker That Is Nearer (The Ant That Strikes The Gang Before Both)") {
        for (const sim::AntType kind : {sim::AntType::Thief, sim::AntType::Fire}) {
            Scene s;
            empty_field(s.sim, 373);
            block_of_workers(s.sim);
            s.sim.spawn_unit(1, sim::AntType::Worker, TileCoord{29, 24});                                        // 4 tiles from the gang
            const uint32_t wanted = s.sim.spawn_unit(1, kind, TileCoord{32, 27});                                // 7 tiles from it
            s.start();
            s.run(kGangFrom + 300);
            const std::vector<watch::Blow> blows = s.blows_by_bot();
            ASSERT_TRUE(!blows.empty());
            ASSERT_EQ(blows.front().victim, wanted);
        }
    } TEST_END();

    TEST_CASE("AI37.3b The Enemy Ant That Strikes One Of The Gang Is Struck Before A Thief That Is As Near") {
        Scene s;
        empty_field(s.sim, 3731);
        const std::vector<uint32_t> mine = block_of_workers(s.sim);
        const uint32_t striker = s.sim.spawn_unit(1, sim::AntType::Worker, TileCoord{26, 23});                  // next to the ant at (25, 24)
        const uint32_t thief = s.sim.spawn_unit(1, sim::AntType::Thief, TileCoord{23, 26});                     // next to the ant at (24, 25)
        s.start();
        for (uint64_t t = 0; t < kGangFrom + 300; ++t) {
            s.step();
            if (t >= kGangFrom - 20) keep_attacking(s.sim, 1, striker, mine[1]);
        }
        const std::vector<watch::Blow> blows = s.blows_by_bot();
        ASSERT_TRUE(!blows.empty());
        ASSERT_EQ(blows.front().victim, striker);
        (void)thief;
    } TEST_END();

    TEST_CASE("AI37.4 A Gang Ant With Three Hit Points Goes Home At Once (An Urgent Move Onto The Entrance), Heals And Comes Back To Strike") {
        Scene s;
        empty_field(s.sim, 374);
        const std::vector<uint32_t> mine = block_of_workers(s.sim);
        for (int i = 0; i < 12; ++i) s.sim.spawn_unit(1, sim::AntType::Worker, TileCoord{30 + 4 * (i % 4), 22 + 4 * (i / 4)});
        s.start();
        s.run(kGangFrom + 120);
        const uint32_t hurt = mine[0];
        s.sim.get_unit(hurt).hp = 3;
        const size_t before = s.rig().proposed.size();
        s.run(30);
        bool home = false;
        for (size_t i = before; i < s.rig().proposed.size(); ++i) {
            const auto& p = s.rig().proposed[i];
            home = home || (p.second.type == CommandType::GroupMove && p.second.ants == std::vector<uint32_t>{hurt} && p.second.tile_x == s.rig().map().hill(0).entrance.x &&
                            p.second.tile_y == s.rig().map().hill(0).entrance.y && s.rig().proposed_priority[i] == Priority::Urgent);
        }
        ASSERT_TRUE(home);
        const uint64_t sent_at = s.now();
        s.run(900);
        const sim::AntSnapshot* a = snapshot_of(s.sim, hurt);
        ASSERT_TRUE(a != nullptr && a->hp >= 8);                                                                 // healed in the hill
        bool struck_again = false;
        for (const watch::Blow& b : s.blows_by_bot()) struck_again = struck_again || (b.striker == hurt && b.tick > sent_at + 300);
        ASSERT_TRUE(struck_again);
    } TEST_END();

    TEST_CASE("AI37.5 The Gang Takes Every Free Ant: A Fire Ant And, With No Raid Open, A Thief Strike Beside The Workers") {
        Scene s;
        empty_field(s.sim, 375);
        const uint32_t w1 = s.sim.spawn_unit(0, sim::AntType::Worker, TileCoord{24, 24});
        const uint32_t w2 = s.sim.spawn_unit(0, sim::AntType::Worker, TileCoord{25, 24});
        const uint32_t fire = s.sim.spawn_unit(0, sim::AntType::Fire, TileCoord{24, 25});
        const uint32_t thief = s.sim.spawn_unit(0, sim::AntType::Thief, TileCoord{25, 25});
        for (int i = 0; i < 6; ++i) s.sim.spawn_unit(1, sim::AntType::Worker, TileCoord{30 + 3 * (i % 3), 22 + 4 * (i / 3)});
        s.start();
        s.run(kGangFrom + 900);
        std::set<uint32_t> strikers;
        for (const watch::Blow& b : s.blows_by_bot()) strikers.insert(b.striker);
        ASSERT_TRUE(strikers.count(fire) == 1);
        ASSERT_TRUE(strikers.count(thief) == 1);
        ASSERT_TRUE(strikers.count(w1) + strikers.count(w2) >= 1);
    } TEST_END();

    TEST_CASE("AI37.6 A Thief Raids A Hill Whose Hole Is Open, The Moment It Is, And Does Not Raid One Whose Three Tiles Are Shut By Fire Walls") {
        for (const bool shut : {false, true}) {
            Scene s;
            empty_field(s.sim, 376);
            const uint32_t thief = s.sim.spawn_unit(0, sim::AntType::Thief, TileCoord{46, 12});
            s.sim.spawn_unit(1, sim::AntType::Worker, TileCoord{56, 24});
            s.sim.set_player_score(1, 300);
            if (shut) {
                for (int dy = 1; dy <= 3; ++dy) s.sim.grid_mut().place_firewall(static_cast<uint32_t>(kFightHills[1].x + 4), static_cast<uint32_t>(kFightHills[1].y + dy), 1);
            }
            s.start();
            s.run(shut ? 600 : 400);
            if (shut) {
                ASSERT_FALSE(names_ant(s.rig(), CommandType::GroupSpecial, thief));
                ASSERT_EQ(s.sim.get_player_score(1), 300);
            } else {
                ASSERT_TRUE(names_ant(s.rig(), CommandType::GroupSpecial, thief));
                ASSERT_TRUE(s.sim.get_player_score(1) < 300);                                                    // the raid went through: loot was taken
                ASSERT_TRUE(s.rig().as<bench::Gang2Bot>().raid_orders() >= 1);
            }
        }
    } TEST_END();

    TEST_CASE("AI37.7 An Enemy Ant That Carries Food Is Left Alone (The Gang Strikes Only Ants That Carry Nothing)") {
        for (const bool carrying : {true, false}) {
            Scene s;
            empty_field(s.sim, 377);
            block_of_workers(s.sim);
            const uint32_t foe = s.sim.spawn_unit(1, sim::AntType::Worker, TileCoord{30, 24});
            if (carrying) s.sim.get_unit(foe).pick_up_food();
            s.start();
            s.run(kGangFrom + 500);
            ASSERT_EQ(largest_order(s.rig(), CommandType::GroupAttack) > 0, !carrying);
        }
    } TEST_END();

    TEST_CASE("AI37.8 The Hunter (The First Combat Ant) Is Not Of The Gang: It Walks To A Post Near The Victim's Hill Entrance Alone, And The Second Combat Ant Is Named In The Gang's Orders") {
        Scene s;
        empty_field(s.sim, 378);
        const uint32_t hunter = s.sim.spawn_unit(0, sim::AntType::Combat, TileCoord{46, 40});
        const uint32_t other = s.sim.spawn_unit(0, sim::AntType::Combat, TileCoord{24, 24});
        for (const TileCoord t : {TileCoord{30, 24}, TileCoord{30, 28}, TileCoord{34, 24}}) s.sim.spawn_unit(1, sim::AntType::Worker, t);
        s.start();
        s.run(kGangFrom + 300);
        const TileCoord door = s.rig().map().hill(1).entrance;
        bool at_post = false;
        for (const auto& p : s.rig().proposed) {
            at_post = at_post || (p.second.type == CommandType::GroupMove && p.second.ants == std::vector<uint32_t>{hunter} && std::max(std::abs(p.second.tile_x - door.x), std::abs(p.second.tile_y - door.y)) <= 6);
        }
        ASSERT_TRUE(at_post);                                                                                    // the hunter went to its post, alone
        for (const auto& p : s.rig().proposed) {
            if (p.second.type == CommandType::GroupAttack) ASSERT_TRUE(std::find(p.second.ants.begin(), p.second.ants.end(), hunter) == p.second.ants.end());
        }
        ASSERT_TRUE(names_ant(s.rig(), CommandType::GroupAttack, other));                                        // the other Combat Ant strikes with the gang
    } TEST_END();
    TEST_CASE("AI37.9 The Nearer Of Two Enemy Ants Is Struck First, Whichever Is Older (The Farther One, 8 Tiles Away, Has The Lower Number)") {
        Scene s;
        empty_field(s.sim, 379);
        block_of_workers(s.sim);
        s.sim.spawn_unit(1, sim::AntType::Worker, TileCoord{33, 24});                                            // 8 tiles from the block, in reach, the older ant
        const uint32_t near = s.sim.spawn_unit(1, sim::AntType::Worker, TileCoord{29, 24});                      // 4 tiles
        s.start();
        s.run(kGangFrom + 300);
        const std::vector<watch::Blow> blows = s.blows_by_bot();
        ASSERT_TRUE(!blows.empty());
        ASSERT_EQ(blows.front().victim, near);
    } TEST_END();

    TEST_CASE("AI37.10 At The Same Distance The Ant With Fewer Hit Points Is Struck First, And An Ant That Appears Nearer After The Third Blow Does Not Take The Blows From The Victim") {
        {
            Scene s;
            empty_field(s.sim, 3710);
            block_of_workers(s.sim);
            s.sim.spawn_unit(1, sim::AntType::Worker, TileCoord{30, 22});                                        // 5 tiles, 10 hit points, the older ant
            const uint32_t weak = s.sim.spawn_unit(1, sim::AntType::Worker, TileCoord{30, 28});                  // 5 tiles, 5 hit points
            s.sim.get_unit(weak).hp = 5;
            s.start();
            s.run(kGangFrom + 300);
            const std::vector<watch::Blow> blows = s.blows_by_bot();
            ASSERT_TRUE(!blows.empty());
            ASSERT_EQ(blows.front().victim, weak);
        }
        {
            Scene s;
            empty_field(s.sim, 3711);
            block_of_workers(s.sim);
            const uint32_t first = s.sim.spawn_unit(1, sim::AntType::Worker, TileCoord{31, 24});                 // 6 tiles
            s.start();
            s.run(kGangFrom + 20);                                                                               // the gang has its target and walks to it
            const uint32_t nearer = s.sim.spawn_unit(1, sim::AntType::Worker, TileCoord{27, 22});                // 2 tiles from the block: nearer than the victim
            s.run(700);
            const std::vector<watch::Blow> blows = s.blows_by_bot();
            ASSERT_TRUE(blows.size() >= 9);
            for (size_t i = 0; i < 9; ++i) ASSERT_EQ(blows[i].victim, first);                                    // nine blows take it to 1 hit point: no blow on the nearer one before
            for (const watch::Blow& b : blows) {
                if (b.victim == nearer) break;
                ASSERT_EQ(b.victim, first);
            }
        }
    } TEST_END();

    TEST_CASE("AI37.11 With No Enemy Ant In Reach The Gang Walks To The Tile In Front Of The Thief Hole Of The Richest Enemy Hill (Score Boxes Of 100 And 300; It Stops There Before It Meets Anyone)") {
        Scene s;
        empty_field(s.sim, 3712);
        const std::vector<uint32_t> mine = block_of_workers(s.sim);
        s.sim.spawn_unit(1, sim::AntType::Worker, TileCoord{56, 12});                                            // seat 1 (hill at (50, 4)): 100 points
        s.sim.spawn_unit(2, sim::AntType::Worker, TileCoord{6, 56});                                             // seat 2 (hill at (4, 50)): 300 points
        s.sim.set_player_score(1, 100);
        s.sim.set_player_score(2, 300);
        s.start();
        s.run(kGangFrom + 400);                                                                                  // (on the way: the walk takes about 700 ticks, and the team that loses its last ant is charged for an egg)
        const TileCoord front{kFightHills[2].x + 4 + 3, kFightHills[2].y + 2};                                   // 3 tiles in front of the middle tile of the richer hill's thief hole
        const TileCoord other{kFightHills[1].x + 4 + 3, kFightHills[1].y + 2};
        size_t moves = 0;
        for (const auto& p : s.rig().proposed) {
            if (p.first < kGangFrom || p.second.type != CommandType::GroupMove || std::find(p.second.ants.begin(), p.second.ants.end(), mine[0]) == p.second.ants.end()) continue;
            ++moves;
            ASSERT_TRUE(std::max(std::abs(p.second.tile_x - front.x), std::abs(p.second.tile_y - front.y)) <= 3);
        }
        ASSERT_TRUE(moves >= 1);
        for (const uint32_t id : mine) {
            const sim::AntSnapshot* a = snapshot_of(s.sim, id);
            ASSERT_TRUE(a != nullptr);
            ASSERT_TRUE(std::max(std::abs(a->tile_x - front.x), std::abs(a->tile_y - front.y)) < std::max(std::abs(a->tile_x - other.x), std::abs(a->tile_y - other.y)));
        }
    } TEST_END();

    TEST_CASE("AI37.12 A Gang Ant With 5 Hit Points Is In No Strike Order, A Hunter With 3 Hit Points Gets An Urgent Move Onto The Entrance, And A Thief Of The Gang Gets A Raid Order When A Hole Opens") {
        {                                                                                                        // the ant that is not healed (8 hit points to rejoin) stays out of the gang's orders
            Scene s;
            empty_field(s.sim, 3713);
            const std::vector<uint32_t> mine = block_of_workers(s.sim);
            s.sim.get_unit(mine[0]).hp = 5;
            for (const TileCoord t : {TileCoord{31, 24}, TileCoord{31, 28}}) s.sim.spawn_unit(1, sim::AntType::Worker, t);
            s.start();
            s.run(kGangFrom + 300);
            ASSERT_TRUE(names_ant(s.rig(), CommandType::GroupAttack, mine[1]));
            ASSERT_FALSE(names_ant(s.rig(), CommandType::GroupAttack, mine[0]));
        }
        {                                                                                                        // a gang ant that falls to 5 hit points stays (home at 3), one that takes food leaves the gang
            Scene s;
            empty_field(s.sim, 3716);
            const std::vector<uint32_t> mine = block_of_workers(s.sim);
            for (int i = 0; i < 8; ++i) s.sim.spawn_unit(1, sim::AntType::Worker, TileCoord{30 + 3 * (i % 3), 20 + 4 * (i / 3)});
            s.start();
            s.run(kGangFrom + 60);
            ASSERT_TRUE(names_ant(s.rig(), CommandType::GroupAttack, mine[2]));
            s.sim.get_unit(mine[2]).hp = 5;
            s.sim.get_unit(mine[3]).pick_up_food();
            const size_t before = s.rig().proposed.size();
            s.run(100);
            bool sent_home = false;
            bool hurt_named = false;
            bool carrier_named = false;
            for (size_t i = before; i < s.rig().proposed.size(); ++i) {
                const auto& p = s.rig().proposed[i];
                const bool names2 = std::find(p.second.ants.begin(), p.second.ants.end(), mine[2]) != p.second.ants.end();
                const bool names3 = std::find(p.second.ants.begin(), p.second.ants.end(), mine[3]) != p.second.ants.end();
                sent_home = sent_home || (names2 && p.second.type == CommandType::GroupMove && s.rig().proposed_priority[i] == Priority::Urgent);
                hurt_named = hurt_named || (names2 && p.second.type == CommandType::GroupAttack);
                carrier_named = carrier_named || (names3 && p.second.type == CommandType::GroupAttack);
            }
            ASSERT_FALSE(sent_home);
            ASSERT_TRUE(hurt_named);
            ASSERT_FALSE(carrier_named);
        }
        {                                                                                                        // an invitation to team up is refused (the standard bot would take it)
            Scene s;
            empty_field(s.sim, 3717);
            block_of_workers(s.sim);
            for (const int t : {1, 2, 3}) s.sim.spawn_unit(static_cast<uint8_t>(t), sim::AntType::Worker, TileCoord{30 + 3 * t, 40});
            s.start();
            s.run(2);
            Command invite;
            invite.type = CommandType::AllianceInvite;
            invite.issuer = 1;
            invite.other_player = 0;
            s.sim.apply_command(invite);
            s.run(260);
            ASSERT_EQ(s.rig().proposed_count(CommandType::AllianceDeny), 1u);
            ASSERT_EQ(s.rig().proposed_count(CommandType::AllianceAccept), 0u);
            ASSERT_TRUE(s.sim.get_ally_id(0) != 1);
        }
        {                                                                                                        // the hunter is hurt on its way: an urgent move onto the entrance
            Scene s;
            empty_field(s.sim, 3714);
            const uint32_t hunter = s.sim.spawn_unit(0, sim::AntType::Combat, TileCoord{46, 40});
            s.sim.spawn_unit(1, sim::AntType::Worker, TileCoord{56, 24});
            s.start();
            s.run(kGangFrom + 60);
            s.sim.get_unit(hunter).hp = 3;
            const size_t before = s.rig().proposed.size();
            s.run(30);
            bool home = false;
            for (size_t i = before; i < s.rig().proposed.size(); ++i) {
                const auto& p = s.rig().proposed[i];
                home = home || (p.second.type == CommandType::GroupMove && p.second.ants == std::vector<uint32_t>{hunter} && p.second.tile_x == s.rig().map().hill(0).entrance.x &&
                                p.second.tile_y == s.rig().map().hill(0).entrance.y && s.rig().proposed_priority[i] == Priority::Urgent);
            }
            ASSERT_TRUE(home);
        }
        {                                                                                                        // a Thief of the gang, in a fight, raids the moment a hole is open
            Scene s;
            empty_field(s.sim, 3715);
            const std::vector<uint32_t> workers = {s.sim.spawn_unit(0, sim::AntType::Worker, TileCoord{24, 24}), s.sim.spawn_unit(0, sim::AntType::Worker, TileCoord{25, 24}),
                                                   s.sim.spawn_unit(0, sim::AntType::Worker, TileCoord{24, 25})};
            const uint32_t thief = s.sim.spawn_unit(0, sim::AntType::Thief, TileCoord{25, 25});
            for (int i = 0; i < 8; ++i) s.sim.spawn_unit(1, sim::AntType::Worker, TileCoord{29 + 2 * (i % 4), 22 + 4 * (i / 4)});
            s.start();
            s.run(kGangFrom + 200);
            ASSERT_TRUE(names_ant(s.rig(), CommandType::GroupAttack, thief));                                    // it is of the gang, and the gang fights
            ASSERT_FALSE(names_ant(s.rig(), CommandType::GroupSpecial, thief));                                  // no hill is worth a raid yet
            s.sim.set_player_score(1, 300);
            const size_t before = s.rig().proposed.size();
            s.run(150);
            bool raid = false;
            for (size_t i = before; i < s.rig().proposed.size(); ++i) {
                const auto& p = s.rig().proposed[i];
                raid = raid || (p.second.type == CommandType::GroupSpecial && p.second.ants == std::vector<uint32_t>{thief});
            }
            ASSERT_TRUE(raid);
            (void)workers;
        }
    } TEST_END();
}
