// AI41: a seat that is cut off (docs/BOTS.md, "Cut off"): the hill reaches no food on foot at the start of the match, so the economy has nothing to do. The detector (Tactics::cut_food,
// cut_foes, cut_open, cut_off: decided at the first look from the analysis of the start), the stall detector that fires for such a seat, the Swimmer that a seat enters a contest for, and the
// expedition that plans from the island where the ants stand with the crew it has.
//
//   AI41.1  the plans: the detector is on at every level (a slower level notices it later and is declared stalled later), off in a plan made by hand; the numbers
//   AI41.2  the detector on the shipped maps and on the community maps that cut a seat off: food and foes on foot, who plays the cut-off play (a map of the gate goals does not), the scarce Swimmers
//   AI41.3  a cut-off seat that banks nothing is stalled after cutoff_stall_ticks (no food that its hill can reach would never have fired the detector), a seat that is not cut off is not
//   AI41.4  a cut-off seat enters the contest for a Swimmer: two bots whose ants stand as near to the same Swimmer, in a world whose only food is in a lake, take it between them
//   AI41.5  Island in the Middle, four Hard bots: three ants each and a Bomber at the hill, the Swimmers in the middle of the lake: every seat takes a Swimmer and scores (the crew of three was refused)
//   AI41.6  Little Islands, four Hard bots: four of the five start ants stand on the island next to the hill's: every seat plans from where its ants stand, takes a Swimmer and scores
//   AI41.7  a map whose hill reaches food is played as it was: the same state hash with the cut-off play on and off, on the five maps with food on foot
//   AI41.8  a bot alone on a map with no food at all is never cut off
//   AI41.9  Anarky: the Bomber power-up of a seat's island lies at the end of a one-wide way that its crew waits in: the ant that cannot reach it hands the duty to the nearest ant, and most seats score
//   AI41.10 Stranded: the start ants of all four seats stand on the same small island: a flight whose landing an ant of another team holds is left alone for a while, and a seat gets its Swimmer
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ai_test.hpp"
#include "ants_ai/arena.hpp"
#include "ants_ai/standard_bot.hpp"
#include "ants_ai/tactics.hpp"
#include "b41_helpers.hpp"

using namespace ai_test;
using namespace b41;
using namespace ants;
using namespace ants::ai;

namespace {

const assets::LevelData& community(const std::string& file) {
    static std::vector<std::pair<std::string, assets::LevelData>> cache;
    for (auto& e : cache) {
        if (e.first == file) return e.second;
    }
    cache.emplace_back();
    cache.back().first = file;
    if (!cache.back().second.load_from_file(std::string(ANTS_SOURCE_DIR) + "/Community-Maps/" + file)) std::cout << "  (cannot load " << file << ")\n";
    return cache.back().second;
}

struct Seen {
    bool food{false}, foes{false}, open{false}, off{false}, scarce{false};
};

struct Played {
    ArenaResult result;
    Seen seen[4];
    bool seen_ok{false};
};

// Four Hard bots (or the seats of `seats`) on a map; `tune` is applied to the plan of every bot; the detector is read at tick 8 (every seat has looked: seat s looks first on tick 1 + s)
Played play(const assets::LevelData& level, uint32_t seed, uint64_t ticks, const std::function<void(LevelPlan&)>& tune, uint8_t seats = 0x0F, Level bot_level = Level::Hard) {
    Played out;
    std::vector<StandardBot*> bots(4, nullptr);
    ArenaSpec spec;
    spec.level = &level;
    spec.seed = seed;
    spec.max_ticks = ticks;
    for (uint8_t s = 0; s < 4; ++s) {
        if (((seats >> s) & 1u) == 0) continue;
        BotSpec b;
        b.seat = s;
        b.kind = "standard";
        b.level = bot_level;
        spec.bots.push_back(b);
    }
    spec.factory = [&](const BotSpec& b) -> std::unique_ptr<Bot> {
        auto bot = std::make_unique<StandardBot>(bot_level, Style::Random, tune);
        bots[b.seat] = bot.get();
        return bot;
    };
    spec.on_tick = [&](const sim::SimulationEngine& sim) {
        if (sim.current_tick() != 8) return;
        for (uint8_t s = 0; s < 4; ++s) {
            if (bots[s] == nullptr) continue;
            const Tactics& t = bots[s]->tactics();
            out.seen[s] = Seen{t.cut_food, t.cut_foes, t.cut_open, t.cut_off, t.swim_scarce};
        }
        out.seen_ok = true;
    };
    out.result = play_match(spec);
    return out;
}

const auto kOff = [](LevelPlan& p) { p.cutoff = false; };
const auto kOn = [](LevelPlan&) {};

}  // namespace

void run_cutoff_tests() {
    TEST_CASE("AI41.1 The Plans: The Cut-Off Play Is On At Easy, Medium And Hard (Not In A Plan Made By Hand), A Slower Level Notices Later And Is Declared Stalled Later; The Numbers") {
        const LevelPlan none;
        ASSERT_FALSE(none.cutoff);
        for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) ASSERT_TRUE(plan_for(level).cutoff);
        ASSERT_EQ(plan_for(Level::Hard).cutoff_notice_ticks, 0u);
        ASSERT_EQ(plan_for(Level::Medium).cutoff_notice_ticks, 160u);
        ASSERT_EQ(plan_for(Level::Easy).cutoff_notice_ticks, 400u);
        ASSERT_EQ(plan_for(Level::Hard).cutoff_crew_min, 1u);
        ASSERT_EQ(plan_for(Level::Medium).cutoff_crew_min, 1u);
        ASSERT_EQ(plan_for(Level::Easy).cutoff_crew_min, 1u);
        ASSERT_EQ(plan_for(Level::Hard).cutoff_stall_ticks, 3600u);
        ASSERT_EQ(plan_for(Level::Medium).cutoff_stall_ticks, 4800u);
        ASSERT_EQ(plan_for(Level::Easy).cutoff_stall_ticks, 7200u);
        for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) {
            const LevelPlan p = plan_for(level);
            ASSERT_EQ(p.cutoff_stuck_ticks, 800u);
            ASSERT_EQ(p.cutoff_retry_ticks, 200u);
            ASSERT_EQ(p.cutoff_bomb_wait, 160u);
        }
        const Tactics fresh;
        ASSERT_FALSE(fresh.cut_food || fresh.cut_foes || fresh.cut_off || fresh.cut_open || fresh.swim_scarce);
    } TEST_END();

    TEST_CASE("AI41.2 The Detector: No Food On Foot On ISLANDS, Island In The Middle, Little Islands, Anarky, Stranded (Every Seat; Their Foes Are Over Water Too); Food On Foot On TINY, MEDIUM, TREASURE, GAUNTLET And SMALL; The Maps Of The Gate Goals (!AntFreaks, POPcOrN, HELLRAISER) Are Cut Off Too But Do Not Play The Cut-Off Play; The Swimmers Are Scarce Where There Are No More Than Teams (Island In The Middle: Four For Four Teams)")
    {
        struct Case {
            const char* name;
            bool shipped;
            bool food, foes, open, scarce;
        };
        const Case cases[] = {
            {"TINY", true, false, false, false, false},
            {"MEDIUM", true, false, false, false, false},
            {"TREASURE", true, false, false, false, false},
            {"GAUNTLET", true, false, false, false, false},
            {"SMALL", true, false, false, false, false},
            {"ISLANDS", true, true, true, true, false},
            {"ISLAND IN THE MIDDLE.lvl", false, true, true, true, true},
            {"Little Islands.lvl", false, true, true, true, false},
            {"Anarky.lvl", false, true, true, true, false},
            {"Stranded.lvl", false, true, true, true, false},
            {"!AntFreaks.lvl", false, true, true, false, false},
            {"POPcOrN.lvl", false, true, false, false, false},
            {"HELLRAISER.lvl", false, true, true, false, false},
        };
        for (const Case& c : cases) {
            const assets::LevelData* level = c.shipped ? &level_of(c.name) : &community(c.name);
            const Played p = play(*level, 1, 12, kOn);
            ASSERT_TRUE(p.seen_ok);
            for (uint8_t s = 0; s < 4; ++s) {
                ASSERT_EQ(p.seen[s].food, c.food);
                ASSERT_EQ(p.seen[s].open, c.open);
                ASSERT_EQ(p.seen[s].off, c.open);                                                       // (Hard notices at once)
                if (c.food) ASSERT_EQ(p.seen[s].foes, c.foes);
                if (c.open) ASSERT_EQ(p.seen[s].scarce, c.scarce);
            }
        }
        // the plan can switch it off
        const Played off = play(community("Little Islands.lvl"), 1, 12, kOff);
        for (uint8_t s = 0; s < 4; ++s) ASSERT_FALSE(off.seen[s].open || off.seen[s].off);
    } TEST_END();

    TEST_CASE("AI41.3 The Stall Of A Cut-Off Seat: A Hard Bot Alone On Island In The Middle With No Expedition And No Ferry Banks Nothing And Is Stalled Once After 3,600 Ticks (The Detector Would Never Have Fired With No Food That The Hill Reaches); With The Cut-Off Play Off It Is Not; A Bot Alone On TINY Is Not; Medium After 4,800")
    {
        const auto no_work = [](bool cutoff) {
            return [cutoff](LevelPlan& p) {
                p.island_expedition = false;
                p.island_ferry = false;
                p.cutoff = cutoff;
            };
        };
        const Played on = play(community("ISLAND IN THE MIDDLE.lvl"), 1, 4300, no_work(true), 0x01);
        ASSERT_EQ(on.result.seats[0].stalls, 1u);
        ASSERT_EQ(on.result.seats[0].score, 0);
        const Played before = play(community("ISLAND IN THE MIDDLE.lvl"), 1, 3500, no_work(true), 0x01);                 // not yet: 3,600 ticks without a point
        ASSERT_EQ(before.result.seats[0].stalls, 0u);
        const Played off = play(community("ISLAND IN THE MIDDLE.lvl"), 1, 4300, no_work(false), 0x01);
        ASSERT_EQ(off.result.seats[0].stalls, 0u);
        const Played tiny = play(level_of("TINY"), 1, 4300, [](LevelPlan& p) { p.stall_ticks = 3600; }, 0x01);
        ASSERT_EQ(tiny.result.seats[0].stalls, 0u);                                                             // (a worker that harvests: it banks, and no point has been missing for 3,600 ticks)
        // the level's own number: Medium declares the stall after 4,800 ticks, not 3,600
        const Played medium_early = play(community("ISLAND IN THE MIDDLE.lvl"), 1, 4300, no_work(true), 0x01, Level::Medium);
        ASSERT_EQ(medium_early.result.seats[0].stalls, 0u);
        const Played medium_late = play(community("ISLAND IN THE MIDDLE.lvl"), 1, 5200, no_work(true), 0x01, Level::Medium);
        ASSERT_EQ(medium_late.result.seats[0].stalls, 1u);
    } TEST_END();

    TEST_CASE("AI41.4 A Cut-Off Seat Enters The Contest For A Swimmer: Two Bots Whose Ants Stand As Near To The Same Swimmer Power-Up (The Only Food Is In A Lake) Take It Between Them Within 300 Ticks; With The Cut-Off Play Off Each Gives It Up To The Other For Ever")
    {
        for (const bool cutoff : {true, false}) {
            sim::SimulationEngine sim;
            empty_field(sim, 3);
            for (int y = 40; y <= 50; ++y) {
                for (int x = 20; x <= 34; ++x) sim.set_terrain(x, y, sim::TERRAIN_WATER);
            }
            place_crackers(sim, 27, 45, 25);                                                                     // a pile in the middle of the lake: nobody reaches it on foot
            sim.grid_mut().place_powerup(27, 5, 5);                                                              // the Swimmer lies halfway between the hills of seats 0 and 1
            sim.spawn_unit(0, sim::AntType::Worker, tc(26, 5));
            sim.spawn_unit(1, sim::AntType::Worker, tc(28, 5));
            RecordingSink sink(sim, true);
            BotController ctl(sim, 3);
            for (uint8_t seat = 0; seat < 2; ++seat) {
                BotSpec b;
                b.seat = seat;
                b.kind = "standard";
                b.level = Level::Hard;
                std::string why;
                ASSERT_TRUE(ctl.add(b, std::make_unique<StandardBot>(Level::Hard, Style::Aggressive, [cutoff](LevelPlan& p) { p.cutoff = cutoff; }), sink, why));
            }
            for (int t = 0; t < 300; ++t) {
                sim.tick();
                ctl.on_tick(sim);
                sim.clear_news_events();
                sim.clear_audio_events();
            }
            uint32_t swimmers = 0;
            for (const auto& a : sim.get_world_state().ants) swimmers += a.type == sim::AntType::Swimmer ? 1u : 0u;
            if (cutoff) {
                ASSERT_EQ(swimmers, 1u);
                ASSERT_FALSE(sim.grid().has_powerup_at(tc(27, 5)));
            } else {
                ASSERT_EQ(swimmers, 0u);
                ASSERT_TRUE(sim.grid().has_powerup_at(tc(27, 5)));
            }
        }
    } TEST_END();

    TEST_CASE("AI41.5 Island In The Middle: Four Hard Bots (Three Ants Each, One Bomber At The Hill, Four Swimmers In The Middle Of The Lake) Every Seat Takes One Swimmer And Scores At Least 600 In The Whole Match, Seeds 1 To 3; With The Cut-Off Play Off Nobody Scores (A Crew Of Three And A Bomber Was Asked For)")
    {
        const assets::LevelData& level = community("ISLAND IN THE MIDDLE.lvl");
        for (uint32_t seed = 1; seed <= 3; ++seed) {
            const Played on = play(level, seed, 0, kOn);
            ASSERT_TRUE(on.result.error.empty());
            for (uint8_t s = 0; s < 4; ++s) {
                ASSERT_EQ(on.result.seats[s].took[static_cast<size_t>(sim::AntType::Swimmer)], 1u);
                ASSERT_TRUE(on.result.seats[s].score >= 600);
            }
        }
        const Played off = play(level, 1, 0, kOff);
        for (uint8_t s = 0; s < 4; ++s) ASSERT_EQ(off.result.seats[s].score, 0);
    } TEST_END();

    TEST_CASE("AI41.6 Little Islands: Four Hard Bots (Four Of The Five Start Ants Of A Seat Stand On The Island Next To The Hill's) Every Seat Plans From Where Its Ants Stand, Takes A Swimmer And Scores At Least 600 In The Whole Match, Seeds 1, 3 And 5; With The Cut-Off Play Off Nobody Does")
    {
        const assets::LevelData& level = community("Little Islands.lvl");
        for (const uint32_t seed : {1u, 3u, 5u}) {
            const Played on = play(level, seed, 0, kOn);
            ASSERT_TRUE(on.result.error.empty());
            for (uint8_t s = 0; s < 4; ++s) {
                ASSERT_TRUE(on.result.seats[s].took[static_cast<size_t>(sim::AntType::Swimmer)] >= 1u);
                ASSERT_TRUE(on.result.seats[s].score >= 600);
                ASSERT_TRUE(on.result.seats[s].expedition.swimmers_taken >= 1u);
            }
        }
        const Played off = play(level, 1, 0, kOff);
        for (uint8_t s = 0; s < 4; ++s) ASSERT_EQ(off.result.seats[s].score, 0);
    } TEST_END();

    TEST_CASE("AI41.7 A Map Whose Hill Reaches Food Is Played As It Was: TINY, MEDIUM, TREASURE, GAUNTLET And SMALL, Four Hard Bots, 3,000 Ticks, The Same State Hash With The Cut-Off Play On And Off")
    {
        for (const char* name : {"TINY", "MEDIUM", "TREASURE", "GAUNTLET", "SMALL"}) {
            const Played on = play(level_of(name), 2, 3000, kOn);
            const Played off = play(level_of(name), 2, 3000, kOff);
            ASSERT_TRUE(on.result.error.empty() && off.result.error.empty());
            ASSERT_EQ(on.result.hash, off.result.hash);
            for (uint8_t s = 0; s < 4; ++s) ASSERT_EQ(on.result.seats[s].stalls, off.result.seats[s].stalls);
        }
    } TEST_END();

    TEST_CASE("AI41.8 No Food At All Is No Cut-Off: A Bot Alone On A Field With No Pile Is Not Cut Off (The Game Type 187 Has No Food Either); A Pile That Its Hill Reaches Makes It Not Cut Off Even With Another Pile In A Lake")
    {
        {
            sim::SimulationEngine sim;
            empty_field(sim, 5);
            sim.spawn_unit(0, sim::AntType::Worker, tc(10, 10));
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(Level::Hard, Style::Aggressive));
            rig.run(20);
            ASSERT_FALSE(rig.as<StandardBot>().tactics().cut_food);
            ASSERT_FALSE(rig.as<StandardBot>().tactics().cut_open);
        }
        {
            sim::SimulationEngine sim;
            empty_field(sim, 5);
            for (int y = 40; y <= 50; ++y) {
                for (int x = 20; x <= 34; ++x) sim.set_terrain(x, y, sim::TERRAIN_WATER);
            }
            place_crackers(sim, 27, 45, 25);                                                                     // in the lake
            place_crackers(sim, 12, 8, 25);                                                                      // on the grass in front of the hill
            sim.spawn_unit(0, sim::AntType::Worker, tc(10, 10));
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(Level::Hard, Style::Aggressive));
            rig.run(20);
            ASSERT_FALSE(rig.as<StandardBot>().tactics().cut_food);
        }
        {
            sim::SimulationEngine sim;
            empty_field(sim, 5);
            for (int y = 40; y <= 50; ++y) {
                for (int x = 20; x <= 34; ++x) sim.set_terrain(x, y, sim::TERRAIN_WATER);
            }
            place_crackers(sim, 27, 45, 25);                                                                     // the only pile, in the lake
            sim.spawn_unit(0, sim::AntType::Worker, tc(10, 10));
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(Level::Hard, Style::Aggressive));
            rig.run(20);
            ASSERT_TRUE(rig.as<StandardBot>().tactics().cut_food);
            ASSERT_TRUE(rig.as<StandardBot>().tactics().cut_open);
        }
    } TEST_END();

    TEST_CASE("AI41.9 Anarky: A Crew That Waits In The One-Wide Way To The Bomber Power-Up Does Not Shut It: Four Hard Bots, Seed 2, At Least Two Seats Take A Swimmer And Score 600 Or More; With The Cut-Off Play Off Nobody Scores")
    {
        const assets::LevelData& level = community("Anarky.lvl");
        const Played on = play(level, 2, 0, kOn);
        ASSERT_TRUE(on.result.error.empty());
        uint32_t scoring = 0;
        for (uint8_t s = 0; s < 4; ++s) scoring += on.result.seats[s].score >= 600 ? 1u : 0u;
        ASSERT_TRUE(scoring >= 2u);
        const Played off = play(level, 2, 0, kOff);
        for (uint8_t s = 0; s < 4; ++s) ASSERT_EQ(off.result.seats[s].score, 0);
    } TEST_END();

    TEST_CASE("AI41.10 Stranded: Every Seat Starts With Two Bombers On The Same Small Island: A Flight Whose Landing Or Bomb Tile Another Team Holds Is Left Alone For A While; Four Hard Bots, Seed 2, At Least Two Seats Score 600 Or More; With The Cut-Off Play Off Nobody Does")
    {
        const assets::LevelData& level = community("Stranded.lvl");
        const Played on = play(level, 2, 0, kOn);
        ASSERT_TRUE(on.result.error.empty());
        uint32_t scoring = 0;
        for (uint8_t s = 0; s < 4; ++s) scoring += on.result.seats[s].score >= 600 ? 1u : 0u;
        ASSERT_TRUE(scoring >= 2u);
        const Played off = play(level, 2, 0, kOff);
        for (uint8_t s = 0; s < 4; ++s) ASSERT_EQ(off.result.seats[s].score, 0);
    } TEST_END();
}
