// The damage and heal numbers (include/ants_app/health_changes.hpp: the rule and the tracker; src/ants_app/renderer_health.cpp: the drawing; docs/VIEW_AND_HUD.md, "Damage and heal numbers"):
//   * the tracker on hand-made snapshots: a loss is one "-N" on that ant, losses within 0.25 s merge and later ones do not, a heal is held while the ant is in the hill and shown once as "+N" when it comes
//     out (0 -> 1 -> 10 is one +10), damage and heal never merge and stand one line apart, a new ant, an ant that comes into view, an enemy on ground that is not explored and a jump of more than 10 ticks
//     make nothing, a number outlives its ant, at most 2 per sign per ant and 48 on the screen, and the opacity and the rise of a number over its 0.6 s;
//   * the tracker on the real engine: a melee hit costs 1 and a Combat Ant's 2, a bomb costs the one ant that steps on it 2, a fire wall costs 1 to each ant on its tile, drowning changes nothing, and a hurt
//     ant sent home shows one +N when it comes out;
//   * the drawing on a real renderer (the software renderer, the dummy video driver), every pixel against numbers written down here: white outlined digits for damage, pale green for a heal, their place
//     over the ant at every zoom, their one size, the rise and the fade, the number that follows its ant and the one that outlives it, the clip to the view, the z-order over the health bars, a canvas shown small
//     (twice the size at most) and Off, which changes no pixel.
// The setting's storage and its menu row are in test_game_menu; a recording played with the numbers On and Off, with the same state hash at every tick, is RA10.3 of test_replay_app.
// Usage: test_health_changes. Exit code 0 when every check passes.
#include <SDL.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "zoom_scene.hpp"

#include "ants_app/health_changes.hpp"
#include "ants_sim/sim_engine.hpp"
#include "ants_test_paths.hpp"

using namespace ants;
using namespace ants::app;
using namespace zoomtest;
namespace hc = ants::app::health;

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::fprintf(stderr, "  FAIL: %s\n", what.c_str());
    }
}

std::string num(long long v) { return std::to_string(v); }

// ---- snapshots made by hand -----------------------------------------------------------------------------------------------------------------------------------------------------------------------

sim::AntSnapshot ant(uint32_t id, uint8_t player, int hp, sim::UnitState state = sim::UnitState::Idle, int32_t tile_x = 5, int32_t tile_y = 5) {
    sim::AntSnapshot a;
    a.id = id;
    a.player_id = player;
    a.type = sim::AntType::Worker;
    a.px = tile_x * 32 + 16;
    a.py = tile_y * 32 + 16;
    a.tile_x = tile_x;
    a.tile_y = tile_y;
    a.hp = static_cast<uint16_t>(hp);
    a.max_hp = 10;
    a.state = state;
    return a;
}

sim::WorldState world_of(std::vector<sim::AntSnapshot> ants) {
    sim::WorldState w;
    w.width = 40;
    w.height = 40;
    w.ants = std::move(ants);
    for (uint8_t p = 0; p < w.player_alliances.size(); ++p) w.player_alliances[p] = p;       // (nobody is allied)
    return w;
}

/// The numbers of ant `id` that are live: a "-N" is (-N), a "+N" is (+N)
std::vector<int> live_of(const hc::ChangeTracker& t, uint32_t id) {
    std::vector<int> out;
    for (size_t i = 0; i < t.count(); ++i) {
        if (t.at(i).id == id) out.push_back(t.at(i).heal ? t.at(i).amount : -t.at(i).amount);
    }
    return out;
}

/// One look at the world at tick `tick`, the numbers' clock at `ms`, the picture of team 0
void look(hc::ChangeTracker& t, const sim::WorldState& w, uint32_t tick, int64_t ms) { t.on_tick(w, tick, ms, 0); }

void test_rule() {
    std::printf("[rule] the tracker on hand-made snapshots\n");
    {
        hc::ChangeTracker t;
        look(t, world_of({ant(1, 1, 10), ant(2, 1, 10)}), 1, 0);
        look(t, world_of({ant(1, 1, 9), ant(2, 1, 10)}), 2, 50);
        check(live_of(t, 1) == std::vector<int>{-1} && live_of(t, 2).empty() && t.count() == 1, "10 to 9 is one -1 on that ant, and nothing on the one next to it");
    }
    {
        hc::ChangeTracker t;
        look(t, world_of({ant(1, 1, 10)}), 1, 0);
        look(t, world_of({ant(1, 1, 8)}), 2, 50);
        check(live_of(t, 1) == std::vector<int>{-2}, "10 to 8 in one tick is one -2");
        look(t, world_of({ant(1, 1, 8)}), 3, 100);
        check(t.count() == 1, "no change, no number");
    }
    for (const int gap : {100, 250}) {                                    // (0.1 s apart and exactly 0.25 s apart are the same number)
        hc::ChangeTracker t;
        look(t, world_of({ant(1, 1, 10)}), 1, 0);
        look(t, world_of({ant(1, 1, 9)}), 2, 1000);
        look(t, world_of({ant(1, 1, 8)}), 3, 1000 + gap);
        check(live_of(t, 1) == std::vector<int>{-2}, "two losses " + num(gap) + " ms apart are one -2");
    }
    for (const int gap : {251, 500}) {                                    // (just over 0.25 s and 0.5 s apart are two numbers)
        hc::ChangeTracker t;
        look(t, world_of({ant(1, 1, 10)}), 1, 0);
        look(t, world_of({ant(1, 1, 9)}), 2, 1000);
        look(t, world_of({ant(1, 1, 8)}), 3, 1000 + gap);
        check(live_of(t, 1) == std::vector<int>{-1, -1}, "two losses " + num(gap) + " ms apart are two numbers");
    }
    {
        hc::ChangeTracker t;
        look(t, world_of({ant(1, 1, 10)}), 1, 0);
        look(t, world_of({ant(1, 1, 9)}), 2, 1000);
        look(t, world_of({ant(1, 1, 8)}), 3, 1200);
        look(t, world_of({ant(1, 1, 7)}), 4, 1400);                       // (0.2 s after the last loss, 0.4 s after the first: the clock restarted at each merge)
        check(live_of(t, 1) == std::vector<int>{-3}, "a number's clock restarts when a loss is added to it");
    }
    {   // a heal waits in the hill and is one number as the ant comes out
        hc::ChangeTracker t;
        look(t, world_of({ant(1, 0, 3, sim::UnitState::Walking)}), 1, 0);
        look(t, world_of({ant(1, 0, 3, sim::UnitState::EnteringBase)}), 2, 50);
        look(t, world_of({ant(1, 0, 3, sim::UnitState::EnteringBase)}), 3, 100);
        check(t.count() == 0, "an ant in the hill shows nothing");
        look(t, world_of({ant(1, 0, 10, sim::UnitState::Walking)}), 4, 150);
        check(live_of(t, 1) == std::vector<int>{7}, "3 to 10 as the ant comes out is one +7");
        look(t, world_of({ant(1, 0, 10, sim::UnitState::Walking)}), 5, 200);
        check(t.count() == 1, "and it is shown once");
    }
    {   // the burn survivor: 0 -> 1 when it goes in, 1 -> 10 when it comes out
        hc::ChangeTracker t;
        look(t, world_of({ant(1, 0, 0, sim::UnitState::Burn)}), 1, 0);
        look(t, world_of({ant(1, 0, 1, sim::UnitState::EnteringBase)}), 2, 50);
        look(t, world_of({ant(1, 0, 10, sim::UnitState::EnteringBase)}), 3, 100);
        check(t.count() == 0, "...still in the hill: nothing");
        look(t, world_of({ant(1, 0, 10, sim::UnitState::Walking)}), 4, 150);
        check(live_of(t, 1) == std::vector<int>{10}, "0 to 1 to 10 is one +10, never +1 and then +9");
    }
    {   // damage and heal never merge, and the newer one stands one line above
        hc::ChangeTracker t;
        look(t, world_of({ant(1, 0, 10)}), 1, 0);
        look(t, world_of({ant(1, 0, 9)}), 2, 100);
        look(t, world_of({ant(1, 0, 10)}), 3, 150);                       // (a heal that does not come from the hill: shown at once, as its own number)
        check(live_of(t, 1) == std::vector<int>{-1, 1}, "a damage number and a heal number of one ant are two numbers");
        check(t.at(0).level == 0 && t.at(1).level == 1, "the newer stands one line above the older");
    }
    {   // a new ant, an ant that comes into view, a fogged ant, a jump
        hc::ChangeTracker t;
        look(t, world_of({ant(1, 1, 10)}), 1, 0);
        look(t, world_of({ant(1, 1, 10), ant(2, 1, 4)}), 2, 50);          // (a hatch, or an ant that comes into view, with fewer hit points than 10)
        look(t, world_of({ant(1, 1, 10), ant(2, 1, 4)}), 3, 100);
        check(t.count() == 0, "an id that is seen for the first time makes no number, whatever its hit points");
        look(t, world_of({ant(1, 1, 10), ant(2, 1, 3)}), 4, 150);
        check(live_of(t, 2) == std::vector<int>{-1}, "...and its next loss does");
    }
    {   // fog: an enemy on ground that is not explored makes nothing, and nothing later; your own and your ally's ants are always seen
        sim::WorldState w = world_of({ant(1, 1, 10, sim::UnitState::Idle, 5, 5), ant(2, 0, 10, sim::UnitState::Idle, 5, 5), ant(3, 2, 10, sim::UnitState::Idle, 5, 5)});
        w.fog_of_war_enabled = true;
        w.fog_revealed.assign(40u * 40u, 0);
        w.player_alliances[0] = 2;
        w.player_alliances[2] = 0;
        hc::ChangeTracker t;
        look(t, w, 1, 0);
        w.ants[0].hp = 7;
        w.ants[1].hp = 8;
        w.ants[2].hp = 6;
        look(t, w, 2, 50);
        check(live_of(t, 1).empty(), "an enemy on ground that is not explored makes no number");
        check(live_of(t, 2) == std::vector<int>{-2} && live_of(t, 3) == std::vector<int>{-4}, "your own ant and your ally's show theirs under the same fog");
        w.fog_revealed.assign(40u * 40u, 1);                               // (the enemy comes out of the fog)
        look(t, w, 3, 100);
        check(live_of(t, 1).empty(), "an enemy that comes out of the fog does not show the damage that it took in it");
        w.ants[0].hp = 6;
        look(t, w, 4, 150);
        check(live_of(t, 1) == std::vector<int>{-1}, "...and its next loss, in view, does");
    }
    for (const uint32_t gap : {10u, 11u}) {                               // (ten ticks between two looks are not a jump; eleven are)
        hc::ChangeTracker t;
        look(t, world_of({ant(1, 1, 10)}), 100, 0);
        look(t, world_of({ant(1, 1, 7)}), 100 + gap, 50);
        check(t.count() == (gap == 10u ? 1u : 0u), "a gap of " + num(gap) + " ticks between two looks is " + (gap == 10u ? "not a jump: the loss is shown" : "a jump: nothing is shown"));
        if (gap == 11u) {
            look(t, world_of({ant(1, 1, 6)}), 100 + gap + 1, 100);
            check(live_of(t, 1) == std::vector<int>{-1}, "after a jump the next loss is shown from the new starting value");
        }
    }
    {
        hc::ChangeTracker t;
        look(t, world_of({ant(1, 1, 10)}), 100, 0);
        look(t, world_of({ant(1, 1, 9)}), 101, 50);
        look(t, world_of({ant(1, 1, 5)}), 50, 100);                        // (a seek back in a replay)
        check(t.count() == 0, "a look at an earlier tick is a jump too");
    }
    {   // a number outlives its ant, at the place where the ant was last drawn
        hc::ChangeTracker t;
        look(t, world_of({ant(1, 1, 10)}), 1, 0);
        look(t, world_of({ant(1, 1, 1)}), 2, 100);
        t.note_position(1, 777, 555);
        look(t, world_of({}), 3, 150);                                     // (the dying ant is gone)
        t.expire(150);
        check(live_of(t, 1) == std::vector<int>{-9} && t.at(0).wx == 777 && t.at(0).wy == 555, "the last loss of an ant that is gone plays out where it was drawn");
        t.expire(100 + hc::kLifeMs - 1);
        check(t.count() == 1, "it is there until the end of its 0.6 s");
        t.expire(100 + hc::kLifeMs);
        check(t.count() == 0, "...and gone at the end of it");
    }
    {   // at most 2 per sign for one ant, at most 48 on the screen (the oldest goes first)
        hc::ChangeTracker t;
        look(t, world_of({ant(1, 1, 10)}), 1, 0);
        look(t, world_of({ant(1, 1, 9)}), 2, 1000);
        look(t, world_of({ant(1, 1, 8)}), 3, 1300);
        look(t, world_of({ant(1, 1, 7)}), 4, 1600);                        // (nothing was expired: three numbers would be live)
        check(live_of(t, 1) == std::vector<int>{-1, -1} && t.at(0).start_ms == 1300 && t.at(1).start_ms == 1600, "an ant has two live numbers per sign at most: the oldest goes");
        hc::ChangeTracker big;
        std::vector<sim::AntSnapshot> before, after;
        for (uint32_t i = 1; i <= 60; ++i) before.push_back(ant(i, 1, 10)), after.push_back(ant(i, 1, 9));
        look(big, world_of(before), 1, 0);
        for (uint32_t i = 0; i < 60; ++i) {                                // (one ant after the other, 10 ms apart: ant 1 is the oldest)
            std::vector<sim::AntSnapshot> w = before;
            for (uint32_t k = 0; k <= i; ++k) w[k] = after[k];
            look(big, world_of(w), 2 + i, 100 + 10 * static_cast<int64_t>(i));
        }
        bool oldest_gone = true, newest_there = false;
        for (size_t i = 0; i < big.count(); ++i) {
            if (big.at(i).id <= 12) oldest_gone = false;
            if (big.at(i).id == 60) newest_there = true;
        }
        check(big.count() == 48 && oldest_gone && newest_there, "at most 48 numbers on the screen: the 12 oldest went (" + num(static_cast<long long>(big.count())) + " live)");
    }
    {   // the opacity and the rise
        check(hc::alpha_at(0) == 255 && hc::alpha_at(300) == 255 && hc::alpha_at(301) == 254 && hc::alpha_at(450) == 128 && hc::alpha_at(599) == 1 && hc::alpha_at(600) == 0 && hc::alpha_at(-1) == 0,
              "solid for 0.3 s, then a linear fade to nothing at 0.6 s");
        int last = -1;
        bool even = true;
        for (int age = 0; age <= 600; age += 10) {
            even = even && hc::rise_at(age) >= last;
            last = hc::rise_at(age);
        }
        check(hc::rise_at(0) == 0 && hc::rise_at(300) == 4 && hc::rise_at(600) == 8 && even, "it rises 8 pixels over its life at an even pace, and never back");
    }
}

// ---- the real engine -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------

constexpr int kTickMs = 50;

void make_world(sim::SimulationEngine& sim, uint32_t seed = 11) {
    sim.init_test_world(60, 60, seed, 720000);
    sim.set_anthill(0, sim::TileCoord{20, 20});
    sim.set_anthill(1, sim::TileCoord{50, 50});
    sim.set_player_score(0, 0);
    sim.set_player_score(1, 0);
}

/// The engine and the tracker, in step: every tick is looked at once, the clock is the tick's time
struct Run {
    sim::SimulationEngine sim;
    hc::ChangeTracker tracker;
    int64_t ms{0};
    explicit Run(uint32_t seed = 11) {
        make_world(sim, seed);
    }
    void see() { tracker.on_tick(sim.get_world_state(), static_cast<uint32_t>(sim.current_tick()), ms, 0); }
    void tick(int n = 1) {
        for (int i = 0; i < n; ++i) {
            sim.tick();
            ms += kTickMs;
            see();
        }
    }
};

/// The sum of the damage numbers (as a positive number) of an ant and how many there are
int lost(const hc::ChangeTracker& t, uint32_t id) {
    int sum = 0;
    for (size_t i = 0; i < t.count(); ++i) {
        if (t.at(i).id == id && !t.at(i).heal) sum += t.at(i).amount;
    }
    return sum;
}

void test_engine() {
    std::printf("[engine] every kind of damage on the real engine\n");
    for (const sim::AntType t : {sim::AntType::Worker, sim::AntType::Combat}) {
        Run r;
        const uint32_t a = r.sim.spawn_unit(0, t, sim::TileCoord{10, 10});
        const uint32_t b = r.sim.spawn_unit(1, sim::AntType::Worker, sim::TileCoord{11, 10});
        const uint32_t bystander = r.sim.spawn_unit(1, sim::AntType::Worker, sim::TileCoord{30, 30});
        r.tick(2);
        r.sim.execute_melee_attack(a, b);
        r.tick();
        const int want = t == sim::AntType::Combat ? 2 : 1;
        check(lost(r.tracker, b) == want && live_of(r.tracker, b).size() == 1u && r.tracker.count() == 1u,
              std::string("melee from a ") + (t == sim::AntType::Combat ? "Combat Ant" : "Worker") + " is one -" + num(want) + " on the victim, and nothing on the attacker or the others");
        check(live_of(r.tracker, a).empty() && live_of(r.tracker, bystander).empty(), "...nobody else shows one");
    }
    {   // a bomb: -2 on the ant that steps on it, none on its neighbours (there is no splash)
        bool tested = false;
        for (uint32_t seed = 1; seed < 40 && !tested; ++seed) {            // (the dud roll is random: find a real blast)
            Run r(seed);
            const uint32_t v = r.sim.spawn_unit(0, sim::AntType::Worker, sim::TileCoord{20, 20});
            const uint32_t n1 = r.sim.spawn_unit(0, sim::AntType::Worker, sim::TileCoord{21, 20});
            const uint32_t n2 = r.sim.spawn_unit(1, sim::AntType::Worker, sim::TileCoord{20, 21});
            r.tick(2);
            r.sim.grid_mut().place_bomb(20, 20, 0);
            r.sim.trigger_bomb_detonation(v, sim::TileCoord{20, 20}, 0, 0);
            r.tick();
            if (r.sim.get_unit(v).hp != 8u) continue;
            tested = true;
            check(lost(r.tracker, v) == 2 && live_of(r.tracker, v).size() == 1u, "a bomb is one -2 on the ant that steps on it");
            check(live_of(r.tracker, n1).empty() && live_of(r.tracker, n2).empty() && r.tracker.count() == 1u, "...and none on the ants next to it");
        }
        check(tested, "a bomb that is not a dud was found");
    }
    {   // a fire wall: -1 on every ant on its tile
        Run r;
        r.sim.set_fire_at(sim::TileCoord{12, 10}, 3600);
        r.sim.set_fire_at(sim::TileCoord{30, 30}, 3600);
        const uint32_t a = r.sim.spawn_unit(0, sim::AntType::Worker, sim::TileCoord{12, 10});
        const uint32_t b = r.sim.spawn_unit(1, sim::AntType::Worker, sim::TileCoord{30, 30});
        const uint32_t away = r.sim.spawn_unit(1, sim::AntType::Worker, sim::TileCoord{40, 10});
        r.tick(2);
        r.sim.resolve_fire_contact(a, 1, 0);
        r.sim.resolve_fire_contact(b, 1, 0);
        r.tick();
        check(live_of(r.tracker, a) == std::vector<int>{-1} && live_of(r.tracker, b) == std::vector<int>{-1} && live_of(r.tracker, away).empty() && r.tracker.count() == 2u,
              "a fire wall is one -1 on each ant on its tile, and none on one that is away from it");
    }
    {   // drowning changes no hit points: the number is the punch's, none for the drowning, and none when the ant is gone
        Run r;
        const uint32_t a = r.sim.spawn_unit(0, sim::AntType::Combat, sim::TileCoord{10, 10});
        const uint32_t b = r.sim.spawn_unit(1, sim::AntType::Worker, sim::TileCoord{11, 10});
        for (int y = 8; y <= 12; ++y) r.sim.set_terrain(15, y, sim::TERRAIN_WATER);
        r.tick(2);
        r.sim.execute_melee_attack(a, b);
        r.tick();
        check(lost(r.tracker, b) == 2, "the punch that throws an ant into the water is a -2");
        bool gone = false;
        for (int i = 0; i < 100 && !gone; ++i) {
            r.tick();
            gone = r.sim.get_unit(b).removed;
        }
        check(gone, "the ant drowned and left the snapshot");
        check(lost(r.tracker, b) == 2 && r.tracker.count() == 1u, "drowning made no number of its own (the one number is still the punch's)");
    }
    {   // a hurt ant sent home: nothing while it is in the hill, one +N as it comes out
        Run r;
        r.sim.set_anthill(0, sim::TileCoord{20, 20});
        const uint32_t id = r.sim.spawn_unit(0, sim::AntType::Worker, sim::TileCoord{21, 21});
        r.sim.get_unit(id).hp = 3;
        r.tick();
        r.sim.join_base_queue(id);
        int in_hill = 0;
        bool out = false;
        int heal_numbers = 0;
        bool shown_inside = false;
        for (int i = 0; i < 200 && !out; ++i) {
            r.tick();
            const bool inside = r.sim.get_unit(id).state == sim::UnitState::EnteringBase;
            if (inside) {
                ++in_hill;
                shown_inside = shown_inside || r.tracker.count() > 0;
            } else if (in_hill > 0) {
                out = true;
            }
        }
        for (size_t i = 0; i < r.tracker.count(); ++i) heal_numbers += r.tracker.at(i).heal ? 1 : 0;
        check(in_hill > 5 && out && !shown_inside, "an ant in the hill shows nothing for as long as it is in (" + num(in_hill) + " ticks)");
        check(live_of(r.tracker, id) == std::vector<int>{7} && heal_numbers == 1, "...and one +7 when it comes out");
        r.tick(40);
        r.tracker.expire(r.ms);
        check(r.sim.get_unit(id).hp == 10u && lost(r.tracker, id) == 0, "...and no number after that");
    }
}

// ---- the drawing ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

struct Delta {
    int n{0};
    int x0{1 << 30};
    int y0{1 << 30};
    int x1{-1};
    int y1{-1};
};

Delta delta(const Picture& a, const Picture& b) {
    Delta d;
    for (int y = 0; y < a.h; ++y) {
        for (int x = 0; x < a.w; ++x) {
            if (std::memcmp(a.at(x, y), b.at(x, y), 3) == 0) continue;
            ++d.n;
            d.x0 = std::min(d.x0, x);
            d.y0 = std::min(d.y0, y);
            d.x1 = std::max(d.x1, x);
            d.y1 = std::max(d.y1, y);
        }
    }
    return d;
}

bool is(const Picture& p, int x, int y, int r, int g, int b) { return p.at(x, y)[0] == r && p.at(x, y)[1] == g && p.at(x, y)[2] == b; }

void screen_of(PixelRig& rig, int32_t wx, int32_t wy, double zoom, int& sx, int& sy) {
    const LayoutRect v = rig.layout.view();
    sx = v.x + static_cast<int>(std::floor((static_cast<double>(wx) - static_cast<double>(rig.cam().x)) * zoom));
    sy = v.y + static_cast<int>(std::floor((static_cast<double>(wy) - static_cast<double>(rig.cam().y)) * zoom));
}

sim::AntSnapshot sprite_less(uint32_t id, uint8_t player, int32_t px, int32_t py, int hp) {
    sim::AntSnapshot a = ant(id, player, hp);
    a.px = px;
    a.py = py;
    a.tile_x = px / 32;
    a.tile_y = py / 32;
    a.loco_clip = 0x7FFE;                                          // no sprite: only the numbers (and the marks) are drawn
    return a;
}

/// A tracker with one "-1" (or the given number) made by hand on ant `id` that stands at (px, py), started at clock 0
hc::ChangeTracker with_number(uint32_t id, int32_t px, int32_t py, int amount, bool heal) {
    hc::ChangeTracker t;
    const int from = heal ? 10 - amount : 10;
    const int to = heal ? 10 : 10 - amount;
    sim::WorldState w = world_of({sprite_less(id, 0, px, py, from)});
    t.on_tick(w, 1, 0, 0);
    w.ants[0].hp = static_cast<uint16_t>(to);
    t.on_tick(w, 2, 0, 0);
    return t;
}

constexpr health::Setting kOff{health::When::Off, health::Style::Bar};
constexpr health::Setting kHurtBar{health::When::Hurt, health::Style::Bar};

/// One frame with the numbers asked for (`tracker` null is Off), at the numbers' clock `now`
Picture shoot(PixelRig& rig, hc::ChangeTracker* tracker, int64_t now, double per_css = 1.0, bool scale = false, health::Setting marks = kOff) {
    rig.renderer.set_health_changes(tracker, now, per_css, scale);
    Picture p = rig.shoot_with(marks, per_css);
    rig.renderer.set_health_changes(nullptr, 0);
    return p;
}

void test_drawing(const assets::AssetArchive& arc) {
    std::printf("[drawing] white and green outlined numbers over the ants, pixel for pixel\n");
    PixelRig rig(arc, "MEDIUM", kWideW, kWideH);
    check(rig.ok, "the rig is up");
    if (!rig.ok) return;
    rig.look(500.0, 400.0, 1.0f);
    rig.world.ants = {sprite_less(1, 0, 600, 500, 9)};
    int sx = 0, sy = 0;
    screen_of(rig, 600, 500, 1.0, sx, sy);
    const Picture none = shoot(rig, nullptr, 0);
    {   // Off: no pixel of the picture differs, with or without numbers that are waiting
        hc::ChangeTracker t = with_number(1, 600, 500, 1, false);
        check(delta(none, shoot(rig, nullptr, 0)).n == 0, "Off: the frame is the frame without the feature, pixel for pixel");
        check(t.count() == 1, "...whatever the tracker holds");
    }
    {   // a damage number, "-1", at its start: white digits, a near black outline round them
        hc::ChangeTracker t = with_number(1, 600, 500, 1, false);
        const Picture on = shoot(rig, &t, 0);
        const Delta d = delta(none, on);
        // the glyphs' bottom is 2 pixels over where the bar's top would be (the zoom 1 bar is 3 high, 31 over the ant: its top is at sy - 34): rows sy - 46 .. sy - 37, the outline one more each side
        check(d.n > 30 && d.x0 == sx - 7 && d.x1 == sx + 6 && d.y0 == sy - 47 && d.y1 == sy - 36, "-1 stands centred over the ant, 2 pixels over the bar's place: " + num(d.x0 - sx) + ".." + num(d.x1 - sx) + " x " + num(d.y0 - sy) + ".." + num(d.y1 - sy));
        bool minus = true;
        for (int x = sx - 6; x <= sx - 2; ++x) minus = minus && is(on, x, sy - 41, 255, 255, 255);
        check(minus, "the minus is white, 5 pixels wide, at row 5 of the glyph");
        bool one = is(on, sx + 3, sy - 46, 255, 255, 255) && is(on, sx + 3, sy - 38, 255, 255, 255);
        for (int x = sx + 1; x <= sx + 5; ++x) one = one && is(on, x, sy - 37, 255, 255, 255);
        check(one, "the 1 is white, its foot 5 pixels wide on the glyph's last row (digits take 7 pixels each)");
        const uint8_t* ground = none.at(sx - 4, sy - 42);
        const uint8_t* ring = on.at(sx - 4, sy - 42);                       // (the pixel over the minus: the outline, near black at about 85 %)
        bool at_85 = true;                                                   // (black at 217 / 255 over the ground: the ground keeps 38 / 255 of itself, give or take the rounding)
        for (int c = 0; c < 3; ++c) at_85 = at_85 && std::abs(static_cast<int>(ring[c]) - static_cast<int>(ground[c]) * 38 / 255) <= 2;
        check(at_85 && ground[1] > 40 && ring[1] > 0, "the outline is near black at about 85 % over the ground (ground " + num(ground[1]) + " -> " + num(ring[1]) + ")");
        check(std::memcmp(on.at(sx - 4, sy - 49), none.at(sx - 4, sy - 49), 3) == 0 && std::memcmp(on.at(sx + 10, sy - 41), none.at(sx + 10, sy - 41), 3) == 0, "...and one pixel wide: nothing beyond it");
    }
    {   // a heal number, "+3": pale green
        hc::ChangeTracker t = with_number(1, 600, 500, 3, true);
        const Picture on = shoot(rig, &t, 0);
        const Delta d = delta(none, on);
        check(d.n > 30 && d.x0 == sx - 7 && d.x1 == sx + 7 && d.y0 == sy - 47 && d.y1 == sy - 36, "+3 stands where -1 stood (its bar-free box is the same height): " + num(d.x0 - sx) + ".." + num(d.x1 - sx) + " x " + num(d.y0 - sy) + ".." + num(d.y1 - sy));
        bool plus = is(on, sx - 4, sy - 41, 132, 236, 120) && is(on, sx - 7 + 3, sy - 43, 132, 236, 120) && is(on, sx - 7 + 3, sy - 39, 132, 236, 120);
        for (int x = sx - 6; x <= sx - 2; ++x) plus = plus && is(on, x, sy - 41, 132, 236, 120);
        check(plus, "the plus is pale green (132, 236, 120): a cross of 5 x 5 pixels, in the hand of the minus");
    }
    {   // the rise and the fade
        int top_age[3] = {0, 0, 0};
        const int ages[3] = {0, 300, 580};
        for (int i = 0; i < 3; ++i) {
            hc::ChangeTracker t = with_number(1, 600, 500, 1, false);
            const Delta d = delta(none, shoot(rig, &t, ages[i]));
            top_age[i] = d.y0;
        }
        check(top_age[0] == sy - 47 && top_age[1] == sy - 47 - 4 && top_age[2] == sy - 47 - 8, "it rises 8 pixels over its life: 0, 4 and 8 at 0, 0.3 and 0.58 s (" + num(top_age[0] - sy) + ", " + num(top_age[1] - sy) + ", " + num(top_age[2] - sy) + ")");
        hc::ChangeTracker t = with_number(1, 600, 500, 1, false);
        const Picture solid = shoot(rig, &t, 300);
        hc::ChangeTracker t2 = with_number(1, 600, 500, 1, false);
        const Picture half = shoot(rig, &t2, 450);
        const int ys = sy - 41 - 4;
        const int yh = sy - 41 - 6;                                           // (the rise at 0.45 s is 6)
        const uint8_t* g = none.at(sx - 4, yh);
        const uint8_t* p = half.at(sx - 4, yh);
        check(is(solid, sx - 4, ys, 255, 255, 255), "solid at 0.3 s");
        check(p[0] > g[0] + 20 && p[0] < 245 && p[1] > g[1] + 20 && p[1] < 245, "half faded at 0.45 s: the minus is between the ground (" + num(g[1]) + ") and white (" + num(p[1]) + ")");
        hc::ChangeTracker t3 = with_number(1, 600, 500, 1, false);
        t3.expire(600);
        check(delta(none, shoot(rig, &t3, 600)).n == 0 && t3.count() == 0, "at 0.6 s it is gone");
    }
    {   // the place at other zooms: one size, 31 world pixels over the ant scaled by the zoom, 2 over the bar of that zoom
        struct Z { float zoom; int head; int bar; };
        for (const Z& z : {Z{2.0f, 62, 5}, Z{0.5f, 16, 2}, Z{1.5f, 47, 5}, Z{0.7f, 22, 3}}) {
            rig.look(500.0, 400.0, z.zoom);
            int zx = 0, zy = 0;
            screen_of(rig, 600, 500, z.zoom, zx, zy);
            const Picture zn = shoot(rig, nullptr, 0);
            hc::ChangeTracker t = with_number(1, 600, 500, 10, false);
            const Picture on = shoot(rig, &t, 0);
            const Delta d = delta(zn, on);
            const int bottom = zy - z.head - z.bar - 2;                      // (the glyphs' bottom edge)
            check(d.n > 30 && d.x1 - d.x0 == 21 && d.y1 == bottom && d.y0 == bottom - 11 && d.x0 == zx - 10, "zoom " + std::to_string(z.zoom) + ": -12 is the same 22 x 12 pixel box, its bottom " + num(bottom) + ": " + num(d.x0 - zx) + ".." + num(d.x1 - zx) + " x " + num(d.y0) + ".." + num(d.y1) + " (want " + num(bottom - 11) + ".." + num(bottom));
        }
        rig.look(500.0, 400.0, 1.0f);
    }
    {   // it follows its ant, and it outlives it
        hc::ChangeTracker t = with_number(1, 600, 500, 1, false);
        rig.world.ants = {sprite_less(1, 0, 640, 520, 9)};
        int mx = 0, my = 0;
        screen_of(rig, 640, 520, 1.0, mx, my);
        const Picture none_moved = shoot(rig, nullptr, 0);
        const Delta d = delta(none_moved, shoot(rig, &t, 0));
        check(d.x0 == mx - 7 && d.y0 == my - 47, "the number stands over the ant as it is drawn now (it moved 40 and 20 pixels)");
        rig.world.ants.clear();
        const Picture empty = shoot(rig, nullptr, 0);
        const Delta gone = delta(empty, shoot(rig, &t, 100));
        check(gone.n > 30 && gone.x0 == mx - 7 && gone.y0 == my - 47 - 1, "an ant that is gone leaves its number where it was last drawn (the rise is 1 at 0.1 s)");
        rig.world.ants = {sprite_less(1, 0, 600, 500, 9)};
    }
    {   // a safety net behind the tracker's gate: the ant is still in the game but the picture does not draw it (fog), so its number is not drawn either, and one that is drawn or gone is as before
        rig.look(500.0, 400.0, 1.0f);
        hc::ChangeTracker t = with_number(1, 600, 500, 1, false);
        rig.world.fog_of_war_enabled = true;
        rig.world.fog_revealed.assign(static_cast<size_t>(rig.world.width) * rig.world.height, 0);
        rig.world.ants = {sprite_less(1, 1, 600, 500, 9)};                      // (an enemy of team 0 on ground that is not explored)
        const Picture fogged_none = shoot(rig, nullptr, 0);
        check(delta(fogged_none, shoot(rig, &t, 0)).n == 0, "an ant that the fog hides has no number drawn, though the tracker holds one for it");
        rig.world.fog_revealed.assign(static_cast<size_t>(rig.world.width) * rig.world.height, 1);
        const Picture seen_none = shoot(rig, nullptr, 0);
        check(delta(seen_none, shoot(rig, &t, 0)).n > 30, "...and the same ant on explored ground has it (the hiding is only for a frame in which the ant is not drawn)");
        rig.world.fog_revealed.assign(static_cast<size_t>(rig.world.width) * rig.world.height, 0);
        shoot(rig, nullptr, 0);
        shoot(rig, &t, 0);                                                      // (hidden again: noted)
        rig.world.ants.clear();
        const Picture gone_none = shoot(rig, nullptr, 0);
        check(delta(gone_none, shoot(rig, &t, 100)).n == 0, "an ant that was hidden and is now gone shows nothing either (its last place was a hidden one)");
        rig.world.fog_of_war_enabled = false;
        rig.world.ants = {sprite_less(1, 0, 600, 500, 9)};
    }
    {   // it is clipped to the view: an ant at the top edge, its number would stand over the HUD's part of the canvas (at the zoom 1 and in a zoomed pass)
        struct Edge { float zoom; float under; };                              // (how many screen pixels under the view's top edge the ant stands: the number's box reaches over it)
        for (const Edge& e : {Edge{1.0f, 40.0f}, Edge{2.0f, 75.0f}, Edge{0.5f, 25.0f}}) {
            const float zoom = e.zoom;
            rig.look(500.0, 400.0, zoom);
            const int32_t span = static_cast<int32_t>(e.under / zoom);
            rig.world.ants = {sprite_less(1, 0, 600, static_cast<int32_t>(rig.cam().y) + span, 9)};
            const Picture edge_none = shoot(rig, nullptr, 0);
            hc::ChangeTracker t = with_number(1, 600, static_cast<int32_t>(rig.cam().y) + span, 1, false);
            const Picture on = shoot(rig, &t, 0);
            check(delta(edge_none, on).n > 0 && outside_not_black(on, rig.layout.view()) == outside_not_black(edge_none, rig.layout.view()), "zoom " + std::to_string(zoom) + ": a number at the edge of the view is cut by it: nothing outside the view changes");
        }
        rig.look(500.0, 400.0, 1.0f);
        rig.world.ants = {sprite_less(1, 0, 600, 500, 9)};
    }
    {   // it is over the health marks (a bar of a neighbour): the z-order is the bars first, then the numbers
        rig.world.ants = {sprite_less(1, 0, 600, 500, 9), sprite_less(2, 1, 600, 486, 5)};
        const Picture bars = shoot(rig, nullptr, 0, 1.0, false, kHurtBar);
        hc::ChangeTracker t = with_number(1, 600, 500, 1, false);
        const Picture on = shoot(rig, &t, 0, 1.0, false, kHurtBar);
        check(!is(bars, sx + 3, sy - 46, 255, 255, 255) && is(on, sx + 3, sy - 46, 255, 255, 255), "a number is drawn over a health bar of a neighbour, not under it");
        rig.world.ants = {sprite_less(1, 0, 600, 500, 9)};
    }
    {   // a canvas that is shown small grows the number with the health numbers' rule, to twice at most; the native build keeps one size
        for (const double per_css : {1.0, 1.24, 2.0, 2.5, 3.9}) {
            hc::ChangeTracker t = with_number(1, 600, 500, 1, false);
            const Delta d = delta(shoot(rig, nullptr, 0, per_css, true), shoot(rig, &t, 0, per_css, true));
            const int scale = per_css < 1.25 ? 1 : 2;
            const int bar = per_css > 2.0 ? 5 : 3;                          // (the zoom 1 bar: 3 high, 5 on a canvas that is shown small)
            check(d.y0 == sy - 31 - bar - 13 * scale && d.y1 == d.y0 + 12 * scale - 1, "...and it stands 2 (x" + num(scale) + ") pixels over the bar of that canvas, which is " + num(bar) + " high: rows " + num(d.y0 - sy) + ".." + num(d.y1 - sy));
            check(d.x1 - d.x0 + 1 == 14 * scale && d.y1 - d.y0 + 1 == 12 * scale, "a canvas shown at " + std::to_string(per_css) + " picture pixels per CSS pixel: the number is x" + num(scale) + " (" + num(d.x1 - d.x0 + 1) + " x " + num(d.y1 - d.y0 + 1) + ")");
        }
        hc::ChangeTracker t = with_number(1, 600, 500, 1, false);
        const Delta native = delta(shoot(rig, nullptr, 0, 3.9, false), shoot(rig, &t, 0, 3.9, false));
        check(native.x1 - native.x0 + 1 == 14 && native.y1 - native.y0 + 1 == 12, "the native build keeps the one size on any canvas");
    }
    {   // two numbers on one ant: a heal over a damage, a line (12 pixels) apart
        hc::ChangeTracker t;
        sim::WorldState w = world_of({sprite_less(1, 0, 600, 500, 10)});
        t.on_tick(w, 1, 0, 0);
        w.ants[0].hp = 9;
        t.on_tick(w, 2, 0, 0);
        w.ants[0].hp = 10;
        t.on_tick(w, 3, 0, 0);
        const Delta d = delta(none, shoot(rig, &t, 0));
        check(d.y0 == sy - 47 - 12 && d.y1 == sy - 36, "a damage and a heal number of one ant: the newer stands one line (12 pixels) above the older (" + num(d.y0 - sy) + ".." + num(d.y1 - sy) + ")");
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    test_rule();
    test_engine();
    SDL_Init(SDL_INIT_VIDEO);
    assets::AssetArchive arc;
    if (!arc.load_chd(std::string(ORIGINAL_ASSETS_DIR) + "/ants.chd")) {
        std::fprintf(stderr, "cannot open ants.chd\n");
        return 2;
    }
    test_drawing(arc);
    std::printf("\ndamage and heal numbers: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
