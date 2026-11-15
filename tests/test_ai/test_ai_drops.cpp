// AI47: the power-ups that dead typed ants leave (docs/BOTS.md, "Dropped power-ups"): Medium and Hard send a worker to take back a power-up of a kind that the team had and has none of now, Hard also
// takes a Fire, Bomber or Thief power-up that lies on another team's side when it has none of the kind (the trip's contest check still decides who is first), Easy plays as it did. Hand-made worlds
// (b41_helpers.hpp: four hills on a field of 60 x 60, eight workers of seat 0 near its hill at (4, 4)); the power-up of a dead ant is the engine's own (kill_unit leaves it as a death does).
//
//   AI47.1   a Combat Ant of a Hard bot dies near its hill and a worker has the Combat power-up within 800 ticks; with the rule off nobody does; an Easy bot does not; the arena's drop tally sees the drop and its taker
//   AI47.2   Medium takes it back as well; a Fire Ant and a Bomber the same
//   AI47.3   who is not sent: a kind that the team never had (a Combat power-up that was always there), a kind that another ant of the team still has, a kind that was lost longer ago than the memory
//   AI47.4   a Bomber power-up on another team's side: Hard takes it, Medium and a Hard bot with the rule off do not; one of the start, or a Combat one, nobody takes
//   AI47.5   the plans: Easy has neither rule, Medium has the first, Hard has both; the tune keys
//   AI47.6   where no typed ant dies (TINY, ISLANDS) the rules change nothing: the same state hash and scores with and without them
#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ai_test.hpp"
#include "ants_ai/arena.hpp"
#include "ants_ai/standard_bot.hpp"
#include "b41_helpers.hpp"
#include "bot_arena_tune.hpp"

namespace {

using namespace ai_test;
using namespace ants;
using namespace ants::ai;
using namespace b41;

// Eight workers of seat 0 near its hill (nothing to harvest: they stand idle, empty-handed)
void world(sim::SimulationEngine& sim) {
    empty_field(sim);
    for (int k = 0; k < 8; ++k) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{6 + k, 10});
}

struct Outcome {
    size_t typed{0};                 // the ants of seat 0 of the kind at the end
    std::vector<DropTally::Drop> drops;
};

// A bot of seat 0 with `plan` plays `ticks`; a typed ant of `kind` of seat 0 is spawned at (12, 14), the bot looks at it for 40 ticks, then it is killed (`extra`: another ant of the kind that stays)
Outcome play(const LevelPlan& plan, Level level, sim::AntType kind, uint64_t ticks, bool extra = false, uint64_t alive = 40, const std::function<void(sim::SimulationEngine&)>& after = nullptr) {
    sim::SimulationEngine sim;
    world(sim);
    const uint32_t ant = sim.spawn_unit(0, kind, TileCoord{12, 14});
    if (extra) sim.spawn_unit(0, kind, TileCoord{14, 14});
    Rig rig(sim, 0, level, std::make_unique<StandardBot>(plan));
    DropTally tally;
    rig.after_sim_tick = [&]() {
        if (sim.current_tick() % 4 == 0) tally.scan(sim);
    };
    rig.run(alive);
    sim.kill_unit(ant);
    if (after) after(sim);
    rig.run(ticks);
    Outcome out;
    out.typed = count_type(sim, 0, kind);
    out.drops = tally.drops();
    return out;
}

LevelPlan off(LevelPlan p) {
    p.drop_retake = p.drop_contest = false;
    return p;
}

}  // namespace

void run_drops_tests() {
    TEST_CASE("AI47.1 A Combat Ant Of A Hard Bot Dies Near Its Hill: A Worker Takes The Power-Up It Left Within 800 Ticks (Nobody Does With The Rule Off, Nor An Easy Bot), And The Arena's Drop Tally Sees The Drop And Who Took It") {
        const Outcome on = play(peace_plan(Level::Hard), Level::Hard, sim::AntType::Combat, 800);
        ASSERT_EQ(on.typed, size_t{1});
        ASSERT_EQ(on.drops.size(), size_t{1});
        ASSERT_EQ(uint32_t{on.drops[0].owner}, 0u);
        ASSERT_EQ(uint32_t{on.drops[0].kind}, uint32_t{static_cast<uint8_t>(sim::AntType::Combat)});
        ASSERT_EQ(uint32_t{on.drops[0].taker}, 0u);
        ASSERT_TRUE(on.drops[0].taken > on.drops[0].tick && on.drops[0].taken < on.drops[0].tick + 800u);
        const Outcome without = play(off(peace_plan(Level::Hard)), Level::Hard, sim::AntType::Combat, 800);
        ASSERT_EQ(without.typed, size_t{0});
        ASSERT_EQ(without.drops.size(), size_t{1});
        ASSERT_EQ(without.drops[0].taken, uint64_t{0});                                           // (it lies there: nobody takes it)
        const Outcome easy = play(peace_plan(Level::Easy), Level::Easy, sim::AntType::Combat, 800);
        ASSERT_EQ(easy.typed, size_t{0});
    } TEST_END();

    TEST_CASE("AI47.2 Medium Takes Its Dropped Combat Power-Up Back As Well, And A Hard Bot Its Fire And Its Bomber") {
        ASSERT_EQ(play(peace_plan(Level::Medium), Level::Medium, sim::AntType::Combat, 1200).typed, size_t{1});
        ASSERT_EQ(play(peace_plan(Level::Hard), Level::Hard, sim::AntType::Fire, 800).typed, size_t{1});
        ASSERT_EQ(play(peace_plan(Level::Hard), Level::Hard, sim::AntType::Bomber, 800).typed, size_t{1});
        ASSERT_EQ(play(peace_plan(Level::Medium), Level::Medium, sim::AntType::Bomber, 1200).typed, size_t{1});
    } TEST_END();

    TEST_CASE("AI47.3 Who Is Not Sent: A Kind The Team Never Had, A Kind Another Ant Of The Team Still Has, A Kind That Was Lost Longer Ago Than The Memory") {
        // a Combat power-up that lies there from the start of the match, the team never had a Combat Ant
        {
            sim::SimulationEngine sim;
            world(sim);
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(peace_plan(Level::Hard)));
            sim.grid_mut().place_powerup(14, 20, static_cast<uint8_t>(sim::AntType::Combat));
            rig.run(800);
            ASSERT_EQ(count_type(sim, 0, sim::AntType::Combat), size_t{0});
        }
        // another Combat Ant stays: one is enough, no worker goes
        ASSERT_EQ(play(peace_plan(Level::Hard), Level::Hard, sim::AntType::Combat, 800, true).typed, size_t{1});
        // the memory: the kind counts as lost for drop_memory_ticks after the last ant of it was seen (0: never)
        {
            LevelPlan p = peace_plan(Level::Hard);
            p.drop_memory_ticks = 0;
            ASSERT_EQ(play(p, Level::Hard, sim::AntType::Combat, 800).typed, size_t{0});
            p.drop_memory_ticks = 4800;
            ASSERT_EQ(play(p, Level::Hard, sim::AntType::Combat, 800).typed, size_t{1});
        }
        // the kinds that the rule takes back are a mask (the tune key dkinds)
        {
            LevelPlan p = peace_plan(Level::Hard);
            p.drop_retake_kinds = static_cast<uint8_t>(1u << static_cast<unsigned>(sim::AntType::Fire));
            ASSERT_EQ(play(p, Level::Hard, sim::AntType::Combat, 800).typed, size_t{0});
        }
    } TEST_END();

    TEST_CASE("AI47.4 A Bomber Power-Up That Lies On Another Team's Side (Near The Hill Of Seat 1): A Hard Bot Takes It, A Medium Bot And A Hard Bot With The Rule Off Do Not") {
        const auto run = [](LevelPlan plan, Level level) {
            sim::SimulationEngine sim;
            world(sim);
            Rig rig(sim, 0, level, std::make_unique<StandardBot>(plan));
            sim.grid_mut().place_powerup(40, 8, static_cast<uint8_t>(sim::AntType::Bomber));
            rig.run(1200);
            return count_type(sim, 0, sim::AntType::Bomber);
        };
        ASSERT_EQ(run(peace_plan(Level::Hard), Level::Hard), size_t{1});
        ASSERT_EQ(run(peace_plan(Level::Medium), Level::Medium), size_t{0});
        LevelPlan no_contest = peace_plan(Level::Hard);
        no_contest.drop_contest = false;
        ASSERT_EQ(run(no_contest, Level::Hard), size_t{0});
        {                                                                                         // the same power-up lying there from the start of the match is another team's, not a drop: left alone
            sim::SimulationEngine sim;
            world(sim);
            sim.grid_mut().place_powerup(40, 8, static_cast<uint8_t>(sim::AntType::Bomber));
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(peace_plan(Level::Hard)));
            rig.run(1200);
            ASSERT_EQ(count_type(sim, 0, sim::AntType::Bomber), size_t{0});
        }
        {                                                                                         // only the kinds that the plan secures are taken from another side: a Combat power-up there is not
            sim::SimulationEngine sim;
            world(sim);
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(peace_plan(Level::Hard)));
            sim.grid_mut().place_powerup(40, 8, static_cast<uint8_t>(sim::AntType::Combat));
            rig.run(1200);
            ASSERT_EQ(count_type(sim, 0, sim::AntType::Combat), size_t{0});
        }
    } TEST_END();

    TEST_CASE("AI47.5 The Plans: Easy Has Neither Rule, Medium Has The First, Hard Both; The Tune Keys Set Them") {
        ASSERT_TRUE(!plan_for(Level::Easy).drop_retake && !plan_for(Level::Easy).drop_contest);
        ASSERT_TRUE(plan_for(Level::Medium).drop_retake && !plan_for(Level::Medium).drop_contest);
        ASSERT_TRUE(plan_for(Level::Hard).drop_retake && plan_for(Level::Hard).drop_contest);
        ASSERT_EQ(plan_for(Level::Hard).drop_memory_ticks, 4800u);
        ASSERT_EQ(uint32_t{plan_for(Level::Hard).drop_retake_kinds}, 0x3eu);
        LevelPlan p = plan_for(Level::Hard);
        std::string err;
        ASSERT_TRUE(apply_tune(p, "drops", 0, err) && !p.drop_retake && !p.drop_contest);
        ASSERT_TRUE(apply_tune(p, "dretake", 1, err) && p.drop_retake && !p.drop_contest);
        ASSERT_TRUE(apply_tune(p, "dcontest", 1, err) && p.drop_contest);
        ASSERT_TRUE(apply_tune(p, "dmem", 100, err) && p.drop_memory_ticks == 100u);
        ASSERT_TRUE(apply_tune(p, "dkinds", 4, err) && p.drop_retake_kinds == 4u);
    } TEST_END();

    TEST_CASE("AI47.6 Where No Typed Ant Dies (TINY, ISLANDS) The Rules Change Nothing: A Match Of Four Standard Bots, Medium And Hard, Has The Same State Hash And The Same Scores With And Without Them") {
        for (const char* map : {"TINY", "ISLANDS"}) {
            for (const Level level : {Level::Medium, Level::Hard}) {
                ArenaResult results[2];
                for (int variant = 0; variant < 2; ++variant) {
                    ArenaSpec spec;
                    spec.level = &level_of(map);
                    spec.seed = 3;
                    spec.max_ticks = 2400;
                    for (uint8_t seat = 0; seat < 4; ++seat) {
                        BotSpec b;
                        b.seat = seat;
                        b.level = level;
                        b.kind = "standard";
                        spec.bots.push_back(b);
                    }
                    spec.factory = [variant](const BotSpec& b) -> std::unique_ptr<Bot> {
                        return std::make_unique<StandardBot>(b.level, b.style, [variant](LevelPlan& p) {
                            if (variant == 1) p.drop_retake = p.drop_contest = false;
                        });
                    };
                    results[variant] = play_match(spec);
                }
                ASSERT_TRUE(results[0].error.empty() && results[1].error.empty() && results[0].ticks > 2000);
                ASSERT_TRUE(results[0].drops.empty());
                ASSERT_EQ(results[0].hash, results[1].hash);
                for (size_t i = 0; i < results[0].seats.size(); ++i) ASSERT_EQ(results[0].seats[i].score, results[1].seats[i].score);
            }
        }
    } TEST_END();
}
