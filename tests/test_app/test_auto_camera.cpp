// The Auto camera's model (include/ants_app/auto_camera.hpp, docs/REPLAYS.md "The camera"), alone: made-up cues and worlds in, the middle of the view out, no SDL, no window, no engine. The
// application's own tests (test_replay_app, RA8) check the camera on a real recording and what the viewer does to it; this program checks every decision of the model:
//   * what counts as action (AC1: the cues, the hits and the deaths that the world shows, what is ignored) and how much each weighs (AC2: a kill beats hits, a team fight beats a lone scuffle
//     of the same blows and beats a single-sided one, a lone hit is nothing, events fade);
//   * where it looks (AC3: the camera locks on the place with the most action, centred on it; AC4: it follows the ants of the action as they move);
//   * when it moves on (AC5: not before the dwell is over, AC6: only for a place clearly busier, AC7: and lets go when the action dies out; AC8: no strobing between two places);
//   * how it moves (AC9: a critically damped spring, no snap, no overshoot, a speed limit; AC10: a zoom of the viewer's is followed by a glide, not a jump);
//   * the viewer's hand (AC11: Manual wins and the button comes back), the speed of the replay (AC12), the Fog of War (AC13), determinism and forgetting (AC14).
// Usage: test_auto_camera. ANTS_TEST_FILTER=text runs only the cases whose title contains the text. Exit code 0 when every check passes.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "ants_app/auto_camera.hpp"
#include "ants_sim/sim_engine.hpp"

static int g_test_count = 0;
static int g_test_failures = 0;
static int g_assert_count = 0;

inline void run_test_case(const std::string& name, const std::function<void()>& fn) {
    if (const char* filter = std::getenv("ANTS_TEST_FILTER")) {
        if (name.find(filter) == std::string::npos) return;
    }
    ++g_test_count;
    std::cout << "  RUNNING: " << std::left << std::setw(100) << name.substr(0, 100) << " ... " << std::flush;
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

#include "../common/test_assert.hpp"

using namespace ants;
using namespace ants::app;

namespace {

using Pt = AutoCamera::Point;
namespace S = sim::SoundID;
using Cam = AutoCamera;

constexpr double kFrame = 50.0;                    // the viewer's frame in the tests (a tick at 1x): milliseconds

sim::AudioEvent cue(uint32_t sound, int32_t x, int32_t y, uint32_t owner = 0, uint8_t target = 255) {
    sim::AudioEvent e;
    e.sound_id = sound;
    e.world_x = x;
    e.world_y = y;
    e.priority = 1;
    e.target_player = target;
    e.owner = owner;
    return e;
}

bool is_near(const Pt& a, double x, double y, double tol = 1.0) { return std::fabs(a.x - x) <= tol && std::fabs(a.y - y) <= tol; }
double dist(const Pt& a, const Pt& b) { return std::hypot(a.x - b.x, a.y - b.y); }

/// A model, the world it watches, and the view that it moves. `run` plays the viewer's time in 50 ms frames: the world is looked at (a tick), then the frame is given; the view goes where the model
/// last put it.
struct Rig {
    Cam cam;
    sim::WorldState world;
    Pt at{100.0, 100.0};
    bool moved{false};
    double biggest_step{0.0};                      // the most that the middle of the view moved in one frame
    std::vector<Pt> path;                          // the middle after every frame

    sim::AntSnapshot& ant(uint32_t id, uint8_t team, int32_t x, int32_t y, uint16_t hp = 200) {
        sim::AntSnapshot a;
        a.id = id;
        a.player_id = team;
        a.px = x;
        a.py = y;
        a.hp = hp;
        a.max_hp = 200;
        world.ants.push_back(a);
        return world.ants.back();
    }
    sim::AntSnapshot* find(uint32_t id) {
        for (sim::AntSnapshot& a : world.ants) {
            if (a.id == id) return &a;
        }
        return nullptr;
    }
    void see() { cam.observe_world(world); }
    /// The ant strikes (its cue names it and the place)
    void blow(uint32_t id) {
        const sim::AntSnapshot* a = find(id);
        cam.observe_cue(cue(S::MeleeAttack, a->px, a->py, id));
    }
    /// The ant loses a hit point (the next look at the world sees it)
    void hurt(uint32_t id, uint16_t points = 1) { find(id)->hp = static_cast<uint16_t>(find(id)->hp - points); }
    /// The ant is killed
    void kill(uint32_t id) {
        find(id)->hp = 0;
        find(id)->state = sim::UnitState::Dead;
    }
    void walk(uint32_t id, double dx, double dy) {
        find(id)->px += static_cast<int32_t>(std::lround(dx));
        find(id)->py += static_cast<int32_t>(std::lround(dy));
    }
    void frame(bool zoomed = false) {
        see();
        Pt out;
        const Pt before = at;
        moved = cam.step(kFrame, at, zoomed, out);
        if (moved) at = out;
        biggest_step = std::max(biggest_step, dist(before, at));
        path.push_back(at);
    }
    void run(double ms, bool zoomed = false) {
        for (double t = 0.0; t < ms; t += kFrame) frame(zoomed && t == 0.0);
    }
    Pt target() const {
        Pt t;
        return cam.target(t) ? t : Pt{-1.0, -1.0};
    }
    Pt hot() const {
        Pt h{-1.0, -1.0};
        cam.hot_spot(h);
        return h;
    }
};

/// A stream of one kind of cue at one place, `per_second` of them (given a frame at a time)
struct Stream {
    double per_second;
    uint32_t sound;
    int32_t x;
    int32_t y;
    double owed{0.0};
    void give(Rig& r) {
        owed += per_second * kFrame / 1000.0;
        for (; owed >= 1.0; owed -= 1.0) r.cam.observe_cue(cue(sound, x, y));
    }
};

/// A fight of ants (ids, two sides) round a place: each of them strikes and is hit every `period` ms
struct Fight {
    std::vector<uint32_t> ids;
    double period{500.0};
    double owed{0.0};
    void give(Rig& r) {
        owed += kFrame;
        if (owed < period) return;
        owed -= period;
        for (const uint32_t id : ids) {
            r.blow(id);
            r.hurt(id);
        }
    }
};

/// A place that engages the camera: two bombs go off there (a bomb alone is 10, the camera locks from 12)
void boom(Rig& r, int32_t x, int32_t y) {
    for (int i = 0; i < 2; ++i) r.cam.observe_cue(cue(S::BombDetonate, x, y));
}

/// Six ants, three of each of two sides, round (x, y), ids from `first`
std::vector<uint32_t> melee(Rig& r, uint32_t first, int32_t x, int32_t y, int count = 6) {
    std::vector<uint32_t> ids;
    for (int i = 0; i < count; ++i) {
        const uint32_t id = first + static_cast<uint32_t>(i);
        r.ant(id, static_cast<uint8_t>(i % 2), x + (i - count / 2) * 20, y + (i % 2) * 24);
        ids.push_back(id);
    }
    return ids;
}

}  // namespace

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    std::cout << "=== The Auto camera ===\n";

    TEST_CASE("AC1.1 What is action: the cues of the engine (blows of every kind, a brawl, thefts, fires, bombs, defusing, a hill's score, base alarms, drowning, power-ups, bridges, food) count; a stop, a cue of no meaning and a seat without a hill do not") {
        Rig r;
        r.cam.set_hills({Pt{300.0, 300.0}, Pt{1700.0, 300.0}, Pt{300.0, 1700.0}, Pt{1700.0, 1700.0}});
        r.ant(1, 0, 1000, 1000);
        for (const uint32_t sound : {S::MeleeAttack, S::AttackAlt, S::HeavyPunch, S::WaterAttack, S::ThiefWhip}) {          // the blows of every kind of ant
            Rig one;
            one.ant(1, 0, 1000, 1000);
            one.see();
            one.cam.observe_cue(cue(sound, 1000, 1000, 1));
            ASSERT_EQ(one.cam.events(), 1u);
        }
        const std::vector<uint32_t> meaningful = {S::CombatNetFairy, S::ThiefDive, S::ThiefEmerge, S::BaseScoreDn, S::BaseScoreUp, S::FireBeam, S::FireErupt, S::FireBurnout, S::BombDetonate,
                                                  S::BombDefuseGrab, S::BombPick, S::AntDrown, S::PowerUpDrop, S::ShovelGravel, S::ShovelWater, S::FoodGrab, S::FoodHarvest};
        for (const uint32_t sound : meaningful) {
            Rig one;
            one.cam.observe_cue(cue(sound, 1000, 1000));
            ASSERT_EQ(one.cam.events(), 1u);
        }
        Rig none;
        none.cam.set_hills({Pt{300.0, 300.0}});
        for (const uint32_t sound : {S::ButtonClick, S::Bump, S::FlingThumpA, S::FlingThumpB, S::ExitHill, S::StunRecover, S::BombBodySquash, S::Countdown}) none.cam.observe_cue(cue(sound, 1000, 1000));
        sim::AudioEvent stop = cue(S::BombDetonate, 1000, 1000);
        stop.stop = true;
        none.cam.observe_cue(stop);
        none.cam.observe_cue(cue(S::BaseAlarmSiren, 0, 0, 0, 1));                    // (the first seat past the one hill: no such seat, and nothing is read beyond the list)
        none.cam.observe_cue(cue(S::BaseAlarmSiren, 0, 0, 0, 5));                    // (no such seat)
        none.cam.observe_cue(cue(S::BaseAlarmSiren, 0, 0, 0, 255));                  // (a cue for everyone has no hill)
        ASSERT_EQ(none.cam.events(), 0u);
        // a cue for a seat (a base alarm, a raid) is at that seat's hill
        r.cam.observe_cue(cue(S::BaseAlarmSiren, 0, 0, 0, 2));
        r.cam.observe_cue(cue(S::BaseAlarmSiren, 0, 0, 0, 3));
        r.cam.observe_cue(cue(S::Anthill, 0, 0, 0, 3));
        r.run(200.0);
        ASSERT_TRUE(is_near(r.hot(), 1700.0, 1700.0, 2.0));                              // (seat 3's hill: an alarm and a raid there weigh more than seat 2's alarm)
    } TEST_END();

    TEST_CASE("AC1.2 What the world shows: an ant whose hit points fell was hit, an ant that is dead was killed (once), an ant in the water is not a death, and the first look at a world makes nothing") {
        Rig r;
        r.ant(1, 0, 500, 600);
        r.ant(2, 1, 900, 100);
        r.ant(3, 1, 700, 700);
        r.ant(4, 0, 800, 800);
        r.see();
        ASSERT_EQ(r.cam.events(), 0u);
        r.hurt(1);
        r.see();
        ASSERT_EQ(r.cam.events(), 1u);                                               // a hit
        r.see();
        ASSERT_EQ(r.cam.events(), 1u);                                               // (the same hit is not told twice)
        r.find(4)->hp = 250;                                                         // (more hit points: a power-up, not an event)
        r.see();
        ASSERT_EQ(r.cam.events(), 1u);
        r.kill(2);                                                                   // a blow that kills: the hit and the death
        r.see();
        ASSERT_EQ(r.cam.events(), 3u);
        ASSERT_EQ(r.cam.deaths(), 1u);
        r.see();
        r.see();
        ASSERT_EQ(r.cam.events(), 3u);                                               // (it lies there for a moment: nothing more)
        r.find(3)->is_drowning = true;                                               // an ant in the water: its own cue, not a death (this one was hit to 0 before it fell: the water has it)
        r.find(3)->state = sim::UnitState::Drowning;
        r.find(3)->hp = 0;
        r.see();
        r.world.ants.erase(r.world.ants.begin() + 2);                               // ... and then it is gone
        r.see();
        ASSERT_EQ(r.cam.events(), 3u);
        ASSERT_EQ(r.cam.deaths(), 1u);
        r.world.ants.erase(r.world.ants.begin());                                    // an ant that is gone without a death (it entered a hill) is nothing either
        r.see();
        ASSERT_EQ(r.cam.events(), 3u);
        r.run(100.0);
        ASSERT_TRUE(is_near(r.hot(), 900.0, 100.0, 1.0));                               // (the kill is where it lay)
        {   // an ant with no hit points is dead, and so is one that the engine has in the Dead state, whichever the world says first
            Rig d;
            d.ant(1, 0, 500, 500);
            d.ant(2, 1, 900, 900);
            d.see();
            d.find(1)->hp = 0;
            d.find(2)->state = sim::UnitState::Dead;
            d.see();
            ASSERT_EQ(d.cam.deaths(), 2u);
        }
        r.cam.reset();                                                               // after a jump the ants of the world before it are not compared with
        r.world.ants.clear();
        r.ant(1, 0, 500, 600, 100);
        r.see();
        r.world.ants[0].hp = 50;
        r.cam.reset();
        r.see();
        ASSERT_EQ(r.cam.events(), 0u);
    } TEST_END();

    TEST_CASE("AC1.3 The camera keeps the latest 400 events (a bound for its work): the oldest go, the newest stay") {
        Rig r;
        for (int i = 0; i < 50; ++i) r.cam.observe_cue(cue(S::BombDetonate, 1000, 1000));                     // the oldest: 50 bombs
        for (int i = 0; i < 400; ++i) r.cam.observe_cue(cue(S::ShovelWater, 3000, 3000));                    // the newest: 400 shovels, a little each
        ASSERT_EQ(r.cam.events(), Cam::kMostEvents);
        r.run(100.0);
        ASSERT_TRUE(is_near(r.hot(), 3000.0, 3000.0, 2.0));                          // (the bombs are gone: the shovels are the place; had the newest gone, the bombs would be it)
    } TEST_END();

    TEST_CASE("AC1.4 The camera looks at the places every kEvalMs, not every frame: an event is a place at the second 50 ms frame, not the first") {
        Rig r;
        boom(r, 1000, 1000);
        r.frame();
        Pt h;
        ASSERT_FALSE(r.cam.hot_spot(h));
        ASSERT_FALSE(r.cam.locked());
        r.frame();
        ASSERT_TRUE(r.cam.hot_spot(h));
        ASSERT_TRUE(r.cam.locked());
    } TEST_END();

    TEST_CASE("AC2.1 The weights: a kill beats hits, a bomb beats a blow, a fire a theft and a death each take the camera from the low ones (a power-up, a bridge, a base alarm, an ant in the water, food), and a lone hit is nothing") {
        Rig lone;                                                                    // a lone hit is nothing at all; a hit and a blow are a scuffle that no one needs to see
        lone.ant(1, 0, 500, 500);
        lone.ant(2, 1, 530, 500);
        lone.see();
        lone.hurt(1);
        lone.run(300.0);
        Pt h;
        ASSERT_FALSE(lone.cam.hot_spot(h));
        lone.blow(2);
        lone.run(300.0);
        ASSERT_FALSE(lone.cam.locked());
        ASSERT_FALSE(lone.moved);
        const std::vector<uint32_t> high = {S::ThiefDive, S::FireBeam, S::FireErupt, S::BombDetonate, S::BombDefuseGrab, S::BaseScoreDn};
        const std::vector<std::pair<uint32_t, uint8_t>> low = {{S::PowerUpDrop, 255}, {S::ShovelGravel, 255}, {S::ShovelWater, 255}, {S::AntDrown, 255}, {S::BaseScoreUp, 255}, {S::FoodGrab, 255},
                                                                {S::BaseAlarmSiren, 0}, {S::Anthill, 0}};
        for (const uint32_t sound : high) {
            for (const auto& [lesser, whose] : low) {
                Rig r;
                r.cam.set_hills({Pt{100.0, 100.0}});
                r.cam.observe_cue(cue(lesser, whose == 255 ? 4000 : 0, whose == 255 ? 4000 : 0, 0, whose));        // (elsewhere, at the same moment)
                r.cam.observe_cue(cue(sound, 2000, 2000));
                r.run(100.0);
                ASSERT_TRUE(is_near(r.hot(), 2000.0, 2000.0, 2.0));
            }
            Rig alone;
            alone.cam.observe_cue(cue(sound, 2000, 2000));
            alone.run(100.0);
            ASSERT_TRUE(alone.cam.hot_spot(h));                                      // (and each is enough to be a place)
        }
        for (const auto& [lesser, whose] : low) {                                    // a death is high, too
            Rig r;
            r.cam.set_hills({Pt{100.0, 100.0}});
            r.ant(1, 0, 500, 600);
            r.see();
            r.cam.observe_cue(cue(lesser, whose == 255 ? 4000 : 0, whose == 255 ? 4000 : 0, 0, whose));
            r.kill(1);
            r.run(100.0);
            ASSERT_TRUE(is_near(r.hot(), 500.0, 600.0, 2.0));
        }
        Rig kill_or_hits;                                                            // a kill is worth more than three hits, at once
        kill_or_hits.ant(1, 0, 1000, 1000);
        kill_or_hits.ant(2, 1, 1030, 1000);
        kill_or_hits.ant(3, 0, 3000, 3000);
        kill_or_hits.ant(4, 1, 3030, 3000);
        kill_or_hits.see();
        for (int i = 0; i < 3; ++i) kill_or_hits.hurt(1);
        kill_or_hits.kill(3);
        kill_or_hits.run(100.0);
        ASSERT_TRUE(is_near(kill_or_hits.hot(), 3000.0, 3000.0, 2.0));
        Rig bomb_or_blows;                                                           // a bomb is worth more than a blow, a hit, a blow and a hit
        bomb_or_blows.ant(1, 0, 1000, 1000);
        bomb_or_blows.ant(2, 1, 1030, 1000);
        bomb_or_blows.see();
        bomb_or_blows.blow(1);
        bomb_or_blows.hurt(2);
        bomb_or_blows.blow(2);
        bomb_or_blows.hurt(1);
        bomb_or_blows.cam.observe_cue(cue(S::BombDetonate, 3000, 3000));
        bomb_or_blows.run(100.0);
        ASSERT_TRUE(is_near(bomb_or_blows.hot(), 3000.0, 3000.0, 2.0));
    } TEST_END();

    TEST_CASE("AC2.2 A fight of several ants of two sides beats a lone scuffle with the same blows, and beats a bigger one-sided event; ants of one side only are not a fight") {
        // place A: five ants hit once each (raw 5 hits); place B: a bomb and one more hit (raw more than A's); the team fight outweighs them
        const auto place = [](bool two_sides) {
            Rig r;
            for (uint32_t i = 0; i < 5; ++i) r.ant(10 + i, static_cast<uint8_t>(two_sides ? i % 2 : 0), 1000 + static_cast<int32_t>(i) * 25, 1000);
            r.ant(20, 0, 3000, 3000);
            r.see();
            for (uint32_t i = 0; i < 5; ++i) r.hurt(10 + i);
            r.cam.observe_cue(cue(S::BombDetonate, 3000, 3000));
            r.hurt(20);
            r.run(100.0);
            return r.hot();
        };
        ASSERT_TRUE(is_near(place(true), 1060.0, 1000.0, 40.0));                       // (two sides: the fight is the busiest place, though the bomb's place has more raw weight)
        ASSERT_TRUE(is_near(place(false), 3000.0, 3000.0, 2.0));                       // (one side only: no bonus, the bomb wins)
        // ants of two sides that carry food are not a fight: five of them with five grabs each (raw 10) lose to a bomb and a hit (12), as the bonus would not have let them
        {
            Rig c;
            for (uint32_t i = 0; i < 5; ++i) c.ant(10 + i, static_cast<uint8_t>(i % 2), 1000 + static_cast<int32_t>(i) * 25, 1000);
            c.see();
            for (uint32_t i = 0; i < 5; ++i) {
                for (int n = 0; n < 5; ++n) c.cam.observe_cue(cue(S::FoodGrab, 1000 + static_cast<int32_t>(i) * 25, 1000, 10 + i));
            }
            c.cam.observe_cue(cue(S::BombDetonate, 3000, 3000));
            c.ant(20, 0, 3000, 3000);
            c.see();
            c.hurt(20);
            c.run(100.0);
            ASSERT_TRUE(is_near(c.hot(), 3000.0, 3000.0, 2.0));
        }
        // the ants that strike are fighters too: three strike and three are hit (raw 9) beat a bomb and a hit (12) because six fighters of two sides are a fight
        {
            Rig c;
            for (uint32_t i = 0; i < 6; ++i) c.ant(10 + i, static_cast<uint8_t>(i % 2), 1000 + static_cast<int32_t>(i) * 25, 1000);
            c.ant(20, 0, 3000, 3000);
            c.see();
            for (uint32_t i = 0; i < 6; i += 2) c.blow(10 + i);
            for (uint32_t i = 1; i < 6; i += 2) c.hurt(10 + i);
            c.cam.observe_cue(cue(S::BombDetonate, 3000, 3000));
            c.hurt(20);
            c.run(100.0);
            ASSERT_TRUE(is_near(c.hot(), 1060.0, 1000.0, 60.0));
        }
        // a duel that goes on is a duel: two ants that strike and are hit ten times each are two fighters (a bonus of 12%), not twenty; a bomb in ten beats the duel
        {
            Rig c;
            c.ant(1, 0, 1000, 1000);
            c.ant(2, 1, 1030, 1000);
            c.ant(20, 0, 3000, 3000);
            c.see();
            for (int i = 0; i < 5; ++i) {                                            // five rounds: raw 30, and two fighters
                c.blow(1);
                c.blow(2);
                c.hurt(1);
                c.hurt(2);
                c.see();
            }
            for (int i = 0; i < 5; ++i) c.cam.observe_cue(cue(S::BombDetonate, 3000, 3000));
            c.run(100.0);
            ASSERT_TRUE(is_near(c.hot(), 3000.0, 3000.0, 2.0));
        }
        // a side that is not known when the blow comes (the ant is not in the world yet) is known when the world has it: five such ants (raw 15) beat a bomb and three more (19) only as a fight
        {
            Rig c;
            for (uint32_t i = 0; i < 5; ++i) c.ant(10 + i, static_cast<uint8_t>(i % 2), 1000 + static_cast<int32_t>(i) * 25, 1000);
            for (uint32_t i = 0; i < 5; ++i) c.blow(10 + i);                          // (before any look at the world)
            c.cam.observe_cue(cue(S::BombDetonate, 3000, 3000));
            for (int i = 0; i < 3; ++i) c.cam.observe_cue(cue(S::BombPick, 3000, 3000));
            c.see();
            for (uint32_t i = 0; i < 5; ++i) c.hurt(10 + i);
            c.run(100.0);
            ASSERT_TRUE(is_near(c.hot(), 1060.0, 1000.0, 60.0));
        }
        // the ants of a fight that are killed and gone are still its fighters while their events last: five of two sides killed (raw 80) beat ten bombs (100) only as a fight
        {
            Rig c;
            for (uint32_t i = 0; i < 5; ++i) c.ant(10 + i, static_cast<uint8_t>(i % 2), 1000 + static_cast<int32_t>(i) * 25, 1000);
            c.see();
            for (uint32_t i = 0; i < 5; ++i) c.kill(10 + i);
            c.see();
            c.world.ants.clear();
            c.see();
            for (int i = 0; i < 10; ++i) c.cam.observe_cue(cue(S::BombDetonate, 3000, 3000));
            c.run(100.0);
            ASSERT_TRUE(is_near(c.hot(), 1060.0, 1000.0, 60.0));
        }
        // twenty fighters of two sides are worth no more than eleven: the bonus stops at kCrowdCap more (2.2 times), so ten bombs (100) beat their 40 raw (88), and would not beat 3.28 times
        {
            Rig c;
            for (uint32_t i = 0; i < 20; ++i) c.ant(100 + i, static_cast<uint8_t>(i % 2), 1000 + static_cast<int32_t>(i) * 10, 1000);
            c.see();
            for (uint32_t i = 0; i < 20; ++i) c.hurt(100 + i);
            for (int i = 0; i < 10; ++i) c.cam.observe_cue(cue(S::BombDetonate, 3000, 3000));
            c.run(100.0);
            ASSERT_TRUE(is_near(c.hot(), 3000.0, 3000.0, 2.0));
        }
        // the bonus grows with the fighters up to a cap: ten ants of two sides beat a fight of four with the same hits each
        Rig r;
        for (uint32_t i = 0; i < 10; ++i) r.ant(100 + i, static_cast<uint8_t>(i % 2), 1000 + static_cast<int32_t>(i) * 15, 1000);
        for (uint32_t i = 0; i < 4; ++i) r.ant(200 + i, static_cast<uint8_t>(i % 2), 3000 + static_cast<int32_t>(i) * 15, 3000);
        for (uint32_t i = 0; i < 3; ++i) r.ant(300 + i, static_cast<uint8_t>(i % 2), 3000 + static_cast<int32_t>(i) * 15, 3040);
        r.see();
        for (uint32_t i = 0; i < 10; ++i) r.hurt(100 + i);
        for (uint32_t i = 0; i < 4; ++i) r.hurt(200 + i, 2);                        // (the same weight in all: 10 hits against 8 + 2 from the three more)
        r.hurt(300);
        r.hurt(301);
        r.hurt(302);
        r.hurt(300);
        r.hurt(301);
        r.hurt(302);
        r.run(100.0);
        ASSERT_TRUE(is_near(r.hot(), 1070.0, 1000.0, 60.0));
    } TEST_END();

    TEST_CASE("AC2.3 Events fade: a place is a place while its events are fresh and nothing once kWindowMs has gone by") {
        Rig r;
        r.cam.observe_cue(cue(S::BombDetonate, 1000, 1000));
        Pt h;
        r.run(1000.0);
        ASSERT_TRUE(r.cam.hot_spot(h));
        r.run(2000.0);                                                               // 3 s: 4 of 10 left, still above the quiet level
        ASSERT_TRUE(r.cam.hot_spot(h));
        r.run(1000.0);                                                               // 4 s: 2 of 10 left
        ASSERT_FALSE(r.cam.hot_spot(h));
        r.run(1200.0);
        ASSERT_EQ(r.cam.events(), 0u);                                               // (and the event is gone)
        // five bombs close together outweigh one that is older, however old
        Rig b;
        b.cam.observe_cue(cue(S::BombDetonate, 1000, 1000));
        b.run(2500.0);
        b.cam.observe_cue(cue(S::BombDetonate, 3000, 3000));
        b.run(100.0);
        ASSERT_TRUE(is_near(b.hot(), 3000.0, 3000.0, 2.0));
    } TEST_END();

    TEST_CASE("AC3.1 The camera locks on the place with the most action and centres on it: nothing below the engage level moves it, a kill does, and the middle of the place is the middle of its action") {
        Rig quiet;
        quiet.cam.observe_cue(cue(S::ShovelWater, 1000, 1000));
        quiet.cam.observe_cue(cue(S::PowerUpDrop, 1000, 1000));
        quiet.cam.observe_cue(cue(S::FoodGrab, 1000, 1000));
        quiet.run(3000.0);
        ASSERT_FALSE(quiet.cam.locked());
        ASSERT_FALSE(quiet.moved);
        ASSERT_TRUE(is_near(quiet.at, 100.0, 100.0, 0.0));                              // (it stays where it is)
        Pt t;
        ASSERT_FALSE(quiet.cam.target(t));

        Rig r;
        r.ant(1, 0, 1000, 2000);
        r.ant(2, 1, 1040, 2000);
        r.see();
        r.kill(1);
        r.run(100.0);
        ASSERT_TRUE(r.cam.locked());
        ASSERT_TRUE(is_near(r.target(), 1000.0, 2000.0, 25.0));
        ASSERT_TRUE(r.moved);                                                        // (it moves)
        ASSERT_FALSE(is_near(r.at, 1000.0, 2000.0, 100.0));                             // (not there yet: it glides)
        r.run(4000.0);
        ASSERT_TRUE(is_near(r.at, r.target().x, r.target().y, 2.0));                    // (arrived: the middle of the view is the place)
        r.run(50.0);
        ASSERT_FALSE(r.moved);                                                       // (and the view is left alone)
        // two places: the busier one, and the middle of its events (weighed by how fresh and how heavy they are)
        Rig two;
        two.cam.observe_cue(cue(S::BombDetonate, 5000, 5000));
        two.cam.observe_cue(cue(S::BombDetonate, 5100, 5000));
        two.cam.observe_cue(cue(S::PowerUpDrop, 500, 500));
        two.run(200.0);
        ASSERT_TRUE(is_near(two.target(), 5050.0, 5000.0, 4.0));
        ASSERT_EQ(two.cam.locks(), 1u);
    } TEST_END();

    TEST_CASE("AC4.1 The camera follows the ants of the action: a fight that walks across the map keeps the camera on it, with the same lock, and ants that walk away in a lull are followed too") {
        Rig r;
        Fight f;
        f.ids = melee(r, 1, 1000, 1000);
        r.see();
        for (int i = 0; i < 400; ++i) {                                              // 20 s, the fight walks east at 60 px/s: 1200 px
            f.give(r);
            for (const uint32_t id : f.ids) r.walk(id, 3.0, 0.0);
            r.frame();
        }
        double sum_x = 0.0;
        for (const uint32_t id : f.ids) sum_x += r.find(id)->px;
        const double centre_x = sum_x / static_cast<double>(f.ids.size());
        ASSERT_GT(centre_x, 2000.0);                                                 // (the fight has moved on)
        ASSERT_EQ(r.cam.locks(), 1u);                                                // (it is the one lock)
        ASSERT_TRUE(r.cam.locked());
        ASSERT_NEAR(r.target().x, centre_x, 120.0);
        ASSERT_NEAR(r.at.x, centre_x, 200.0);                                        // (the view is on it, a little behind: the spring)
        ASSERT_NEAR(r.at.y, 1010.0, 60.0);
        // a lull: the ants stop fighting and walk 300 px away in 3 s; no new event comes. The place that the camera watches is where the ants are
        Rig lull;
        for (uint32_t i = 0; i < 6; ++i) lull.ant(1 + i, static_cast<uint8_t>(i % 2), 1000 + static_cast<int32_t>(i) * 20, 1000 + static_cast<int32_t>(i % 2) * 24);
        lull.see();
        for (uint32_t id = 1; id <= 6; ++id) {
            lull.blow(id);
            lull.hurt(id);
        }
        lull.run(300.0);
        ASSERT_TRUE(lull.cam.locked());
        for (int i = 0; i < 60; ++i) {
            for (uint32_t id = 1; id <= 6; ++id) lull.walk(id, 0.0, 5.0);
            lull.frame();
        }
        ASSERT_NEAR(lull.target().y, 1012.0 + 300.0, 70.0);
        ASSERT_EQ(lull.cam.locks(), 1u);
    } TEST_END();

    TEST_CASE("AC5.1 The dwell: once locked the camera stays kDwellMs, though a place twice as busy begins at once, and goes there when the dwell is over") {
        Rig r;
        r.ant(1, 0, 1000, 1000);
        r.ant(2, 1, 1030, 1000);
        r.see();
        r.kill(1);
        r.run(100.0);
        ASSERT_TRUE(r.cam.locked());
        ASSERT_TRUE(is_near(r.target(), 1000.0, 1000.0, 25.0));
        const double locked_at = r.cam.now_ms();
        Stream big{8.0, S::BombPick, 5000, 4000};                                    // a far busier place, from 2 s on
        double since = 0.0;
        bool early = false;
        bool on_b = false;
        for (int i = 0; i < 400; ++i) {
            if (r.cam.now_ms() - locked_at >= 1900.0) big.give(r);
            r.frame();
            since = r.cam.now_ms() - locked_at;
            if (since < Cam::kDwellMs - 200.0 && !is_near(r.target(), 1000.0, 1000.0, 25.0)) early = true;
            if (since > Cam::kDwellMs + 1500.0 && is_near(r.target(), 5000.0, 4000.0, 40.0)) on_b = true;
            if (since > Cam::kDwellMs + 1500.0) break;
        }
        ASSERT_FALSE(early);                                                         // (not a step before the dwell is over)
        ASSERT_TRUE(on_b);                                                           // (and then the busier place)
        ASSERT_EQ(r.cam.locks(), 2u);
    } TEST_END();

    TEST_CASE("AC6.1 Hysteresis: a place 1.25 times as busy does not take the camera, however long it burns; a place 1.75 times as busy does (kClearlyBusier is 1.5)") {
        for (const double ratio : {1.25, 1.75}) {
            Rig r;
            Stream a{4.0, S::BombPick, 1000, 1000};
            Stream b{4.0 * ratio, S::BombPick, 4000, 3000};
            for (int i = 0; i < 160; ++i) {                                         // 8 s of the first place alone: the camera is on it
                a.give(r);
                r.frame();
            }
            ASSERT_TRUE(r.cam.locked());
            ASSERT_TRUE(is_near(r.target(), 1000.0, 1000.0, 10.0));
            for (int i = 0; i < 600; ++i) {                                          // 30 s of both
                a.give(r);
                b.give(r);
                r.frame();
            }
            if (ratio < Cam::kClearlyBusier) {
                ASSERT_EQ(r.cam.locks(), 1u);
                ASSERT_TRUE(is_near(r.target(), 1000.0, 1000.0, 10.0));
            } else {
                ASSERT_EQ(r.cam.locks(), 2u);
                ASSERT_TRUE(is_near(r.target(), 4000.0, 3000.0, 10.0));
            }
        }
    } TEST_END();

    TEST_CASE("AC6.2 A fight that is cooling is not taken from by a place that has only just engaged: it needs 1.5 times the engage level, not 1.5 times the cooled score; a place that is clearly busier still takes it") {
        for (const double rate : {1.9, 4.0}) {
            Rig r;
            Stream a{8.0, S::BombPick, 1000, 1000};
            Stream b{rate, S::BombPick, 4000, 3000};
            for (int i = 0; i < 160; ++i) {                                          // 8 s of a busy place: locked, steady score about 60
                a.give(r);
                r.frame();
            }
            ASSERT_TRUE(r.cam.locked());
            for (int i = 0; i < 80; ++i) {                                           // 4 s: the first place stops, the second one burns (steady 14 at 1.9 a second, 30 at 4)
                b.give(r);
                r.frame();
            }
            if (rate < 3.0) {
                ASSERT_EQ(r.cam.locks(), 1u);                                        // (the first place has cooled to a fifth, and still holds the camera)
                ASSERT_TRUE(is_near(r.target(), 1000.0, 1000.0, 15.0));
            } else {
                ASSERT_EQ(r.cam.locks(), 2u);
                ASSERT_TRUE(is_near(r.target(), 4000.0, 3000.0, 15.0));
            }
        }
    } TEST_END();

    TEST_CASE("AC7.1 When the action dies out the camera lets go after the dwell and the quiet time, the view stays, a smaller thing elsewhere is not followed, and the next real action takes it") {
        Rig r;
        r.ant(1, 0, 1000, 1000);
        r.ant(2, 1, 1030, 1000);
        r.see();
        r.kill(1);
        r.run(1000.0);
        ASSERT_TRUE(r.cam.locked());
        r.run(4000.0);                                                               // 5 s: the kill (16 at first with its hit) is less than the quiet level from 4.1 s on; the dwell is not over yet
        ASSERT_TRUE(r.cam.locked());
        r.ant(3, 0, 3000, 3000);
        r.ant(4, 1, 3030, 3000);
        r.see();
        r.hurt(3);
        r.blow(4);                                                                   // a scuffle elsewhere, more than the dying action but less than the engage level: not worth a move
        r.run(1400.0);                                                               // 6.4 s: not yet 3 s of quiet (it began at 4.1 s)
        ASSERT_TRUE(r.cam.locked());
        ASSERT_EQ(r.cam.locks(), 1u);
        r.run(1400.0);                                                               // 7.8 s: the dwell (7 s) and the quiet time (until 7.1 s) are over
        ASSERT_FALSE(r.cam.locked());                                                // let go
        const Pt rest = r.at;
        ASSERT_TRUE(is_near(rest, 1000.0, 1000.0, 5.0));
        r.run(2000.0);                                                               // (the scuffle is long gone)
        ASSERT_FALSE(r.cam.locked());
        ASSERT_EQ(r.cam.locks(), 1u);
        ASSERT_TRUE(is_near(r.at, rest.x, rest.y, 0.0));
        r.kill(3);                                                                   // a kill: the next real action
        r.run(200.0);
        ASSERT_TRUE(r.cam.locked());
        ASSERT_TRUE(is_near(r.target(), 3000.0, 3000.0, 30.0));
        ASSERT_EQ(r.cam.locks(), 2u);
    } TEST_END();

    TEST_CASE("AC7.2 The camera does not let go while the action goes on, however long: a brawl that keeps hitting holds it for a minute") {
        Rig r;
        Fight f;
        f.ids = melee(r, 1, 1500, 1500, 4);
        f.period = 400.0;
        r.see();
        for (int i = 0; i < 1200; ++i) {
            f.give(r);
            r.frame();
        }
        ASSERT_TRUE(r.cam.locked());
        ASSERT_EQ(r.cam.locks(), 1u);
        ASSERT_GT(r.cam.dwell_ms(), 59000.0);
    } TEST_END();

    TEST_CASE("AC7.3 A lull in the fight is not the end of it: a quiet spell shorter than kDieOutMs does not let go, and does not count towards the next one") {
        Rig r;
        Stream a{8.0, S::BombPick, 1000, 1000};
        for (int i = 0; i < 80; ++i) {                                               // 4 s of action, a score of about 55 at the end
            a.give(r);
            r.frame();
        }
        ASSERT_TRUE(r.cam.locked());
        r.run(5900.0);                                                               // ... and a lull: the score falls under the quiet level 3.9 s after the last event, and is under it for 2 s of this
        ASSERT_TRUE(r.cam.locked());
        for (int i = 0; i < 400; ++i) {                                              // the action begins again, and goes on for 20 s
            a.give(r);
            r.frame();
        }
        ASSERT_TRUE(r.cam.locked());
        ASSERT_EQ(r.cam.locks(), 1u);
    } TEST_END();

    TEST_CASE("AC7.4 More action beside the one that is watched is the same place: it is followed, not a new lock (the dwell does not begin again)") {
        Rig r;
        Stream a{2.0, S::BombPick, 1000, 1000};
        Stream b{4.0, S::BombPick, 1200, 1000};
        Stream c{4.0, S::BombPick, 1400, 1000};
        a.owed = 0.5;
        for (int i = 0; i < 120; ++i) {                                              // 6 s at the first place alone
            a.give(r);
            r.frame();
        }
        ASSERT_TRUE(r.cam.locked());
        ASSERT_TRUE(is_near(r.target(), 1000.0, 1000.0, 10.0));
        for (int i = 0; i < 120; ++i) {                                              // 6 s with two more streams a tile or six along (a good deal busier together)
            a.give(r);
            b.give(r);
            c.give(r);
            r.frame();
        }
        ASSERT_EQ(r.cam.locks(), 1u);
        ASSERT_GT(r.target().x, 1100.0);                                             // (the camera goes along with it)
        ASSERT_GT(r.cam.dwell_ms(), 8000.0);
    } TEST_END();

    TEST_CASE("AC8.1 No strobing: two places that take turns at being the busier one do not make the camera shuttle; it stays kDwellMs at least on each place it moves to") {
        Rig r;
        std::vector<double> lock_times;
        uint32_t seen = 0;
        for (int i = 0; i < 2400; ++i) {                                             // 2 minutes, 6 s of one place and then 6 s of the other, each at 12 events a second
            const bool first = (i / 120) % 2 == 0;
            Stream s{12.0, S::BombPick, first ? 1000 : 4000, first ? 1000 : 3000};
            s.owed = 0.5;
            s.give(r);
            r.frame();
            if (r.cam.locks() != seen) {
                seen = r.cam.locks();
                lock_times.push_back(r.cam.now_ms());
            }
        }
        ASSERT_GE(lock_times.size(), 3u);                                            // (it did go from place to place)
        for (size_t i = 1; i < lock_times.size(); ++i) ASSERT_GE(lock_times[i] - lock_times[i - 1], Cam::kDwellMs - 1.0);
        ASSERT_LE(lock_times.size(), 2400u * 50u / 5000u + 1u);                      // (never more than a lock every dwell)
        // the camera is never in two minds: its path has no turn-back within a second of a lock (a move to one place, not a shake)
        double reversals = 0.0;
        for (size_t i = 2; i < r.path.size(); ++i) {
            const double ax = r.path[i - 1].x - r.path[i - 2].x;
            const double bx = r.path[i].x - r.path[i - 1].x;
            if (ax * bx < 0.0 && std::fabs(ax) > 3.0 && std::fabs(bx) > 3.0) ++reversals;
        }
        ASSERT_LE(reversals, 4.0);
    } TEST_END();

    TEST_CASE("AC9.1 The move is a critically damped spring: it starts gently (no snap), is never faster than kMaxSpeed, never goes past the place, and arrives; a trip across the map takes seconds") {
        Rig r;
        boom(r, 1100, 100);                          // a short trip, 1000 px to the east
        r.run(100.0);
        ASSERT_TRUE(r.cam.locked());
        Pt first = r.path.front();
        ASSERT_LT(dist(first, Pt{100.0, 100.0}), 25.0);                              // the first frames are gentle
        double speed_peak = 0.0;
        bool past = false;
        for (int i = 0; i < 200; ++i) {
            r.frame();
            speed_peak = std::max(speed_peak, r.biggest_step);
            if (r.at.x > 1100.0 + 0.5) past = true;
        }
        ASSERT_FALSE(past);                                                          // (no overshoot)
        ASSERT_TRUE(is_near(r.at, 1100.0, 100.0, 1.0));
        ASSERT_LE(speed_peak, Cam::kMaxSpeed * kFrame / 1000.0 + 0.01);
        for (size_t i = 3; i < r.path.size(); ++i) {                                 // the speed changes smoothly: no frame is much faster than the one before
            const double v0 = r.path[i - 2].x < 1100.0 ? r.path[i - 1].x - r.path[i - 2].x : 0.0;
            const double v1 = r.path[i].x - r.path[i - 1].x;
            ASSERT_LE(std::fabs(v1 - v0), 0.35 * Cam::kMaxSpeed * kFrame / 1000.0);
        }
        Rig trip;                                                                     // across the map: the speed limit decides, and it still arrives, without a snap
        boom(trip, 6000, 100);
        trip.run(100.0);
        trip.run(1000.0);
        ASSERT_FALSE(is_near(trip.at, 6000.0, 100.0, 1000.0));                           // (a second in: far from there)
        ASSERT_LE(trip.biggest_step, Cam::kMaxSpeed * kFrame / 1000.0 + 0.01);
        trip.run(6000.0);
        ASSERT_TRUE(is_near(trip.at, 6000.0, 100.0, 1.0));
        // from a view that is still moving (the button's trip to the last action, cut short by an action just ahead) the new move goes on from that speed: no jump, and not past the place
        Rig turn;
        turn.cam.take();
        turn.cam.observe_cue(cue(S::PowerUpDrop, 3000, 100));
        turn.cam.set_mode(CameraMode::Auto);
        turn.run(1000.0);
        const Pt mid = turn.at;
        ASSERT_GT(mid.x, 300.0);                                                     // (on its way, fast)
        ASSERT_LT(mid.x, 2900.0);
        const double ahead = mid.x + 150.0;
        boom(turn, static_cast<int32_t>(ahead), 100);
        double most = 0.0;
        for (int i = 0; i < 120; ++i) {
            turn.frame();
            most = std::max(most, turn.at.x);
        }
        ASSERT_TRUE(turn.cam.locked());
        ASSERT_LE(most, ahead + 1.0);                                                // (it is stopped at the place, it does not run past it)
        ASSERT_TRUE(is_near(turn.at, ahead, 100.0, 1.0));
    } TEST_END();

    TEST_CASE("AC9.2 Taking the camera stops its speed: after the button the new trip begins from rest, not on with the old speed") {
        Rig r;
        boom(r, 4000, 100);                                                          // a long trip east, fast after a second
        r.run(1000.0);
        const Pt mid = r.at;
        ASSERT_GT(mid.x, 500.0);
        ASSERT_TRUE(r.cam.take());
        r.run(5500.0);                                                               // (Manual: it did not coast, and the bombs have faded)
        ASSERT_TRUE(is_near(r.at, mid.x, mid.y, 0.0));
        r.cam.observe_cue(cue(S::PowerUpDrop, 100, 1500));                            // the last thing that happened: back west
        r.cam.set_mode(CameraMode::Auto);
        r.run(150.0);
        ASSERT_TRUE(r.moved);
        ASSERT_LE(r.at.x, mid.x + 0.5);                                              // (west and south from the first frame: not on east first)
        ASSERT_GT(r.at.y, mid.y);
    } TEST_END();

    TEST_CASE("AC10.1 A zoom of the viewer's moves the middle of the view: the camera goes on from where the view is and glides back to the place, with no jump; in Manual nothing is moved") {
        Rig r;
        for (int i = 0; i < 2; ++i) r.cam.observe_cue(cue(S::FireBeam, 1500, 1800));      // (two fires: one alone is not enough to be locked on)
        for (int i = 0; i < 2; ++i) r.cam.observe_cue(cue(S::FireErupt, 1500, 1800));
        r.run(5000.0);
        ASSERT_TRUE(is_near(r.at, 1500.0, 1800.0, 2.0));
        r.at = Pt{1500.0 + 140.0, 1800.0 + 90.0};                                    // (the zoom around the pointer moved the middle of the view)
        const Pt shifted = r.at;
        r.frame(true);
        ASSERT_TRUE(r.moved);
        ASSERT_LT(dist(r.at, shifted), 12.0);                                        // (a step of the glide, not a jump back)
        r.run(4000.0);
        ASSERT_TRUE(is_near(r.at, 1500.0, 1800.0, 2.0));                                // (and it is back)
        r.run(50.0);
        ASSERT_FALSE(r.moved);
        r.cam.take();
        r.at = Pt{900.0, 900.0};
        r.frame(true);
        ASSERT_FALSE(r.moved);
        Rig early;                                                                   // a zoom before anything has happened has no place to go back to
        early.run(50.0, true);
        ASSERT_FALSE(early.moved);
    } TEST_END();

    TEST_CASE("AC11.1 The viewer's hand: taking the camera makes it Manual (nothing moves it whatever happens, the busiest place is still known), and the button comes back to the busiest place now, else the last thing that happened, else stays") {
        Rig r;
        boom(r, 1000, 2000);
        r.run(1000.0);
        ASSERT_TRUE(r.cam.mode() == CameraMode::Auto);
        ASSERT_TRUE(r.cam.locked());
        ASSERT_TRUE(r.cam.take());
        ASSERT_FALSE(r.cam.take());                                                  // (it is Manual already: nothing to tell)
        ASSERT_TRUE(r.cam.mode() == CameraMode::Manual);
        ASSERT_FALSE(r.cam.locked());
        r.at = Pt{4000.0, 4000.0};                                                   // (the viewer put the view there)
        boom(r, 2500, 3000);
        r.cam.observe_cue(cue(S::MeleeAttack, 3000, 500));                           // (the last thing that happened)
        r.run(100.0);
        ASSERT_TRUE(is_near(r.hot(), 2500.0, 3000.0, 2.0));                             // (the hints go on in Manual)
        ASSERT_FALSE(r.cam.locked());                                                // (but it does not lock on them)
        for (int i = 0; i < 100; ++i) {                                              // (five seconds, and the events have faded)
            r.run(50.0);
            ASSERT_FALSE(r.moved);
        }
        ASSERT_TRUE(is_near(r.at, 4000.0, 4000.0, 0.0));
        // the busiest place now, when there is one
        Rig busy;
        busy.cam.take();
        busy.at = Pt{4000.0, 4000.0};
        boom(busy, 2500, 3000);
        busy.cam.observe_cue(cue(S::MeleeAttack, 3000, 500));
        busy.run(100.0);
        busy.cam.set_mode(CameraMode::Auto);
        busy.run(100.0);
        ASSERT_TRUE(busy.moved);
        ASSERT_TRUE(busy.cam.locked());
        ASSERT_TRUE(is_near(busy.target(), 2500.0, 3000.0, 2.0));
        busy.run(3000.0);
        ASSERT_TRUE(is_near(busy.at, 2500.0, 3000.0, 2.0));
        ASSERT_TRUE(busy.cam.holding() || busy.cam.dwell_ms() >= Cam::kDwellMs);
        // else the last thing that happened, when nothing is big enough
        Rig last;
        last.cam.take();
        last.at = Pt{4000.0, 4000.0};
        last.cam.observe_cue(cue(S::PowerUpDrop, 800, 900));
        last.run(100.0);
        last.cam.set_mode(CameraMode::Auto);
        last.run(100.0);
        ASSERT_TRUE(last.moved);
        ASSERT_FALSE(last.cam.locked());
        ASSERT_TRUE(is_near(last.target(), 800.0, 900.0, 2.0));
        last.run(5000.0);
        ASSERT_TRUE(is_near(last.at, 800.0, 900.0, 2.0));
        // with nothing ever to go to, Auto stays where it is
        Rig none;
        none.cam.take();
        none.cam.set_mode(CameraMode::Auto);
        none.run(2000.0);
        ASSERT_FALSE(none.moved);
        ASSERT_TRUE(is_near(none.at, 100.0, 100.0, 0.0));
        // Auto while it is Auto changes nothing
        Rig same;
        boom(same, 1000, 2000);
        same.run(5000.0);
        const double locks = same.cam.locks();
        same.cam.set_mode(CameraMode::Auto);
        same.run(500.0);
        ASSERT_EQ(same.cam.locks(), locks);
    } TEST_END();

    TEST_CASE("AC12.1 At 4x and 8x only the big events count (a kill, a theft, a fire, a bomb): a blow, a hit, a power-up and a bridge are ignored there, and count again at 1x and 2x") {
        Rig r;
        r.ant(1, 0, 1000, 1000);
        r.ant(2, 1, 1030, 1000);
        r.see();
        r.cam.set_speed(4.0f);
        r.blow(1);
        r.hurt(2);
        r.cam.observe_cue(cue(S::PowerUpDrop, 1000, 1000));
        r.cam.observe_cue(cue(S::ShovelWater, 1000, 1000));
        r.cam.observe_cue(cue(S::BaseScoreUp, 1000, 1000));
        r.see();
        ASSERT_EQ(r.cam.events(), 0u);
        for (const uint32_t big : {S::ThiefDive, S::FireBeam, S::FireErupt, S::BombDetonate, S::BombDefuseGrab, S::BaseScoreDn}) r.cam.observe_cue(cue(big, 1000, 1000));
        r.kill(1);
        r.see();
        ASSERT_EQ(r.cam.events(), 7u);                                               // (the six, and the death; the hit that came with it is not big)
        r.cam.set_speed(8.0f);
        r.blow(2);
        ASSERT_EQ(r.cam.events(), 7u);
        r.cam.set_speed(2.0f);
        r.blow(2);
        ASSERT_EQ(r.cam.events(), 8u);
    } TEST_END();

    TEST_CASE("AC13.1 The Fog of War: only what is on a revealed tile is seen (a replay has none, and sees everything)") {
        Rig r;
        r.world.width = 100;
        r.world.height = 100;
        r.world.fog_of_war_enabled = true;
        r.world.fog_revealed.assign(100 * 100, 0);
        for (int ty = 10; ty < 20; ++ty) {
            for (int tx = 10; tx < 20; ++tx) r.world.fog_revealed[static_cast<size_t>(ty * 100 + tx)] = 1;      // tiles 10..19 are revealed (world pixels 320..639)
        }
        r.ant(1, 0, 400, 400);                                                       // seen
        r.ant(2, 1, 430, 400);
        r.ant(3, 0, 2000, 2000);                                                     // not seen
        r.ant(4, 1, 2030, 2000);
        r.see();
        r.kill(3);                                                                   // a death in the dark
        r.cam.observe_cue(cue(S::BombDetonate, 2000, 2000));                         // a bomb in the dark
        r.see();
        ASSERT_EQ(r.cam.events(), 0u);
        r.run(500.0);
        ASSERT_FALSE(r.cam.locked());
        ASSERT_FALSE(r.moved);
        Pt h;
        ASSERT_FALSE(r.cam.hot_spot(h));
        r.kill(1);                                                                   // a death in the light
        r.cam.observe_cue(cue(S::BombDetonate, 500, 500));
        r.run(200.0);
        ASSERT_TRUE(r.cam.locked());
        ASSERT_TRUE(is_near(r.target(), 450.0, 440.0, 70.0));
        // the same events in a world with no fog are all seen
        Rig open;
        open.ant(3, 0, 2000, 2000);
        open.ant(4, 1, 2030, 2000);
        open.see();
        open.kill(3);
        open.cam.observe_cue(cue(S::BombDetonate, 2000, 2000));
        open.run(200.0);
        ASSERT_TRUE(open.cam.locked());
        ASSERT_TRUE(is_near(open.target(), 2000.0, 2000.0, 30.0));
        // a place that goes dark is no longer watched
        Rig dark;
        dark.world.width = 100;
        dark.world.height = 100;
        dark.world.fog_of_war_enabled = true;
        dark.world.fog_revealed.assign(100 * 100, 1);
        dark.ant(1, 0, 1000, 1000);
        dark.ant(2, 1, 1030, 1000);
        dark.see();
        dark.kill(1);
        dark.run(100.0);
        ASSERT_TRUE(dark.cam.hot_spot(h));
        dark.world.fog_revealed.assign(100 * 100, 0);
        dark.run(100.0);
        ASSERT_FALSE(dark.cam.hot_spot(h));
    } TEST_END();

    TEST_CASE("AC14.1 Same match, same camera: two models fed the same cues and worlds at the same times give the same views, frame by frame, and a reset forgets everything but the mode") {
        const auto play = []() {
            Rig r;
            std::vector<uint32_t> a;
            std::vector<uint32_t> b;
            for (uint32_t i = 0; i < 40; ++i) r.ant(1 + i, static_cast<uint8_t>(i % 3), 500 + static_cast<int32_t>((i * 97) % 2400), 500 + static_cast<int32_t>((i * 211) % 2400), 100);
            r.see();
            for (int i = 0; i < 900; ++i) {
                if (i % 7 == 0) {
                    const uint32_t id = 1 + static_cast<uint32_t>((i * 13) % 40);
                    r.blow(id);
                    r.hurt(id);
                }
                if (i % 53 == 0) r.cam.observe_cue(cue(S::BombDetonate, 600 + (i * 37) % 2000, 700 + (i * 91) % 2000));
                if (i % 97 == 0) r.kill(1 + static_cast<uint32_t>((i * 7) % 40));
                for (sim::AntSnapshot& ant : r.world.ants) {
                    ant.px += static_cast<int32_t>(ant.id % 5) - 2;
                    ant.py += static_cast<int32_t>(ant.id % 3) - 1;
                }
                r.frame();
            }
            return r;
        };
        const Rig x = play();
        const Rig y = play();
        ASSERT_EQ(x.path.size(), y.path.size());
        bool same = true;
        for (size_t i = 0; i < x.path.size(); ++i) same = same && x.path[i].x == y.path[i].x && x.path[i].y == y.path[i].y;
        ASSERT_TRUE(same);
        ASSERT_EQ(x.cam.locks(), y.cam.locks());
        ASSERT_GE(x.cam.locks(), 2u);                                                // (and it was not a quiet match)
        // forgetting
        Rig g;
        g.ant(1, 0, 1000, 1000);
        g.ant(2, 1, 1030, 1000);
        g.see();
        g.kill(1);
        g.run(300.0);
        ASSERT_TRUE(g.cam.locked());
        g.cam.reset();
        ASSERT_FALSE(g.cam.locked());
        ASSERT_EQ(g.cam.locks(), 0u);
        ASSERT_EQ(g.cam.events(), 0u);
        ASSERT_TRUE(g.cam.mode() == CameraMode::Auto);
        Rig f = play();
        f.cam.take();
        f.cam.reset();
        ASSERT_EQ(f.cam.events(), 0u);
        ASSERT_EQ(f.cam.deaths(), 0u);
        ASSERT_EQ(f.cam.locks(), 0u);
        ASSERT_FALSE(f.cam.locked());
        Pt t;
        ASSERT_FALSE(f.cam.target(t));
        ASSERT_FALSE(f.cam.hot_spot(t));
        ASSERT_TRUE(f.cam.mode() == CameraMode::Manual);                             // (the mode is the viewer's choice)
    } TEST_END();

    TEST_CASE("AC15.1 The numbers that the documents and the changelog quote (docs/REPLAYS.md \"The camera\", CHANGELOG.md): a change of one is a change of those texts") {
        ASSERT_EQ(Cam::kDwellMs, 7000.0);                                            // "stays at least 7 seconds"
        ASSERT_EQ(Cam::kClearlyBusier, 1.5);                                         // "1.5 times as busy"
        ASSERT_EQ(Cam::kEngageScore, 12.0);                                          // "a score of 12"
        ASSERT_EQ(Cam::kQuietScore, 3.0);                                            // "under a score of 3 for 3 seconds"
        ASSERT_EQ(Cam::kDieOutMs, 3000.0);
        ASSERT_EQ(Cam::kWindowMs, 5000.0);                                           // "fades away over 5 seconds"
        ASSERT_EQ(Cam::kRadius, 224.0);                                              // "seven tiles"
        ASSERT_EQ(Cam::kCrowd, 0.12);                                                // "12% more for every fighter after the first, up to 2.2 times"
        ASSERT_EQ(Cam::kCrowdCap, 10);
        ASSERT_EQ(Cam::kMaxSpeed, 1400.0);                                           // "at most 1400 world pixels a second"
        ASSERT_EQ(Cam::kSmoothMs, 600.0);                                            // "about 0.6 s to arrive"
        ASSERT_EQ(Cam::kEvalMs, 100.0);                                              // "every tenth of a second"
        ASSERT_EQ(Cam::kBigOnlySpeed, 4.0f);                                         // "from 4x up only the big events count"
        ASSERT_EQ(Cam::kDeath, 14.0f);                                               // "a kill 14, a bomb 10, a fire 5 for each of its two cues, a theft or a hill losing points 6, a hit 2, a blow 1, banked food 2"
        ASSERT_EQ(Cam::kBomb, 10.0f);
        ASSERT_EQ(Cam::kFire, 5.0f);
        ASSERT_EQ(Cam::kTheft, 6.0f);
        ASSERT_EQ(Cam::kScoreDown, 6.0f);
        ASSERT_EQ(Cam::kDefuse, 6.0f);
        ASSERT_EQ(Cam::kHit, 2.0f);
        ASSERT_EQ(Cam::kBlow, 1.0f);
        ASSERT_EQ(Cam::kBank, 2.0f);
        ASSERT_EQ(Cam::kMostEvents, 400u);
    } TEST_END();

    std::cout << "\n" << g_test_count << " test cases, " << g_assert_count << " assertions, " << g_test_failures << " failures\n";
    return g_test_failures == 0 ? 0 : 1;
}
