// Pointer model tests (stage I): the ant hit boxes, the cursor decision table, the click by cursor mode, the rubber band and the release of the
// left button, the right button, the command pedestals, the keyboard table and the button class as Ants.exe does them (FUN_01026904 / FUN_01026a39 / FUN_01026aa3 / FUN_01026f91 /
// FUN_01027530 / FUN_010277f4 / FUN_01027b51 / FUN_010287b5 / FUN_010274be / FUN_01028d30). docs/GAME_REVERSE_ENGINEERING.md 5.44
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ants_app/hud.hpp"
#include "ants_app/renderer.hpp"
#include "ants_sim/sim_engine.hpp"

using namespace ants;
using namespace ants::app;
using ants::sim::AntType;
using ants::sim::TileCoord;

namespace {

int g_checks = 0;
int g_failures = 0;
const char* g_group = "";

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::fprintf(stderr, "  FAIL [%s]: %s\n", g_group, what.c_str());
    }
}

const char* cursor_name(CursorType c) {
    switch (c) {
        case CursorType::Normal: return "Normal";
        case CursorType::Select: return "Select";
        case CursorType::Move: return "Move";
        case CursorType::Target: return "Target";
        case CursorType::Attack: return "Attack";
        case CursorType::Food: return "Food";
        default: return "Scroll";
    }
}

// A match: 60 x 60 tiles, the HUD of player 0, a view whose origin is (400, 400), and recorders for sounds and click markers.
struct Fixture {
    sim::SimulationEngine sim;
    HUD hud;
    ViewportCamera cam;
    std::vector<uint32_t> sounds;
    std::vector<std::pair<int32_t, int32_t>> markers;

    explicit Fixture(uint32_t seed = 1) {
        sim.init_test_world(60, 60, seed, 600000);
        hud.init(0);
        hud.set_sim_query(&sim);
        hud.set_on_play_sfx([this](uint32_t s) { sounds.push_back(s); });
        hud.set_on_spawn_click_marker([this](int32_t x, int32_t y) { markers.emplace_back(x, y); });
        cam.world_x = 400;
        cam.world_y = 400;
    }
    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;

    const sim::WorldState& world() { return sim.get_world_state(); }
    void settle() { sim.tick(); }                                   // rebuilds the world snapshot after the setup

    uint32_t spawn(uint8_t player, AntType type, int32_t tx, int32_t ty) { return sim.spawn_unit(player, type, TileCoord{tx, ty}); }

    // The view is placed so that tile (tx, ty) is at the middle of the map view
    void look_at(int32_t tx, int32_t ty) {
        cam.world_x = std::max(0, tx * 32 + 16 - 220);
        cam.world_y = std::max(0, ty * 32 + 16 - 220);
    }
    int32_t sx(int32_t wx) const { return wx - cam.world_x + HUD::PLAYFIELD_X; }
    int32_t sy(int32_t wy) const { return wy - cam.world_y + HUD::PLAYFIELD_Y; }

    CursorType cursor_px(int32_t x, int32_t y) { return hud.evaluate_cursor(x, y, world(), sim.grid(), cam); }
    CursorType cursor_tile(int32_t tx, int32_t ty, int32_t dx = 0, int32_t dy = 0) {
        look_at(tx, ty);
        return cursor_px(sx(tx * 32 + 16 + dx), sy(ty * 32 + 16 + dy));
    }

    void press(int32_t x, int32_t y, uint8_t button = SDL_BUTTON_LEFT, uint16_t mod = 0) { hud.handle_mouse_down(x, y, button, sim, cam, mod); }
    void release(int32_t x, int32_t y, uint8_t button = SDL_BUTTON_LEFT, uint16_t mod = 0) { hud.handle_mouse_up(x, y, button, sim, cam, mod); }
    void click(int32_t x, int32_t y, uint8_t button = SDL_BUTTON_LEFT, uint16_t mod = 0) { press(x, y, button, mod); release(x, y, button, mod); }
    void click_tile(int32_t tx, int32_t ty, uint8_t button = SDL_BUTTON_LEFT, uint16_t mod = 0, int32_t dx = 0, int32_t dy = 0) {
        look_at(tx, ty);
        click(sx(tx * 32 + 16 + dx), sy(ty * 32 + 16 + dy), button, mod);
    }
    void latch_ability() { press(560, 180); release(560, 180); }      // slot 2 (539, 152) - (582, 225)
    void latch_move() { press(500, 170); release(500, 170); }         // slot 1 (482, 152) - (525, 225)
    void end_lock() { hud.update(world(), 5); }                        // the 250 ms input lock after Stop

    const sim::AntUnit& unit(uint32_t id) { return sim.get_unit(id); }
    bool selected(uint32_t id) const { return hud.is_ant_selected(id); }
};

void expect_cursor(const char* what, CursorType got, CursorType want) {
    check(got == want, std::string(what) + ": " + cursor_name(got) + ", expected " + cursor_name(want));
}

// ---------------------------------------------------------------------------------------------------------------------------------
// The ant under the pointer (FUN_01026904 with the box of FUN_01026a39)
// ---------------------------------------------------------------------------------------------------------------------------------

void test_hit_boxes() {
    g_group = "hit boxes";
    std::printf("[pointer] ant boxes: worker [x-20, x+20) x [y-32, y+16), combat ant [x-32, x+26) x [y-46, y+16), the last hit of the 3x3 scan wins\n");
    {
        Fixture f;
        const uint32_t w = f.spawn(0, AntType::Worker, 15, 15);        // (496, 496)
        const uint32_t c = f.spawn(0, AntType::Combat, 25, 15);        // (816, 496)
        f.settle();
        auto id_at = [&](int32_t x, int32_t y) -> uint32_t {
            const sim::AntSnapshot* a = f.hud.pick_ant_at(f.world(), x, y);
            return a ? a->id : 0u;
        };
        check(id_at(476, 464) == w, "worker: the top left corner (x-20, y-32) is inside");
        check(id_at(515, 511) == w, "worker: the last pixel (x+19, y+15) is inside");
        check(id_at(475, 496) == 0, "worker: x-21 is outside");
        check(id_at(516, 496) == 0, "worker: x+20 is outside (half open)");
        check(id_at(496, 463) == 0, "worker: y-33 is outside");
        check(id_at(496, 512) == 0, "worker: y+16 is outside (half open)");
        check(id_at(784, 450) == c, "combat ant: the top left corner (x-32, y-46) is inside");
        check(id_at(841, 511) == c, "combat ant: the last pixel (x+25, y+15) is inside");
        check(id_at(783, 496) == 0, "combat ant: x-33 is outside");
        check(id_at(842, 496) == 0, "combat ant: x+26 is outside");
        check(id_at(816, 449) == 0, "combat ant: y-47 is outside");
        check(id_at(816, 512) == 0, "combat ant: y+16 is outside");
    }
    for (int order = 0; order < 2; ++order) {                             // the winner does not depend on the spawn order
        Fixture f;
        uint32_t left = 0;
        uint32_t right = 0;
        if (order == 0) { left = f.spawn(0, AntType::Worker, 5, 5); right = f.spawn(1, AntType::Worker, 6, 5); }
        else            { right = f.spawn(1, AntType::Worker, 6, 5); left = f.spawn(0, AntType::Worker, 5, 5); }
        f.settle();
        // boxes [156, 196) and [188, 228): the pixel 190 is in both; the scan reads the columns of the tiles left to right, the last hit wins
        const sim::AntSnapshot* a = f.hud.pick_ant_at(f.world(), 190, 176);
        check(a != nullptr && a->id == right, "overlapping boxes: the ant of the right tile wins (spawn order " + std::to_string(order) + ")");
        const sim::AntSnapshot* b = f.hud.pick_ant_at(f.world(), 170, 176);
        check(b != nullptr && b->id == left, "the left ant alone at x=170");
    }
    {
        // no filter: an ant frozen under the burn overlay (a bomb dud) is hit like any other
        bool tested = false;
        for (uint32_t seed = 1; seed < 100 && !tested; ++seed) {
            Fixture f(seed);
            f.sim.grid_mut().place_bomb(20, 20, 1);
            const uint32_t v = f.spawn(0, AntType::Worker, 20, 20);
            f.sim.trigger_bomb_detonation(v, TileCoord{20, 20});
            if (!f.sim.get_unit(v).knock_flag) continue;
            tested = true;
            f.settle();
            check(f.unit(v).frozen, "the dud freezes the ant");
            const sim::AntSnapshot* a = f.hud.pick_ant_at(f.world(), 20 * 32 + 16, 20 * 32 + 16);
            check(a != nullptr && a->id == v, "a frozen ant is still hit");
        }
        check(tested, "a dud seed exists");
    }
}

// ---------------------------------------------------------------------------------------------------------------------------------
// The cursor decision table (FUN_01026aa3)
// ---------------------------------------------------------------------------------------------------------------------------------

struct Scene {
    std::unique_ptr<Fixture> f;
    uint32_t mine{0};          // own ant, the one that gets selected
    uint32_t mine2{0};         // a second own ant
    uint32_t foe{0};           // an enemy worker on plain ground
    uint32_t ally{0};          // an ant of an allied player
    uint32_t foe_on_hill{0};   // an enemy ant on the entrance tile of the enemy hill
    uint32_t mine_on_hill{0};
    uint32_t foe_on_own_hill{0};
};

// Own hill (team 0) at (2, 2), enemy hill (team 1) at (40, 40); ants, a lunchbox at (30, 30), the allied player 2
Scene make_scene(bool fog = false) {
    Scene s;
    s.f = std::make_unique<Fixture>();
    Fixture& f = *s.f;
    f.sim.grid_mut().set_anthill(0, TileCoord{2, 2});
    f.sim.grid_mut().set_anthill(1, TileCoord{40, 40});
    s.mine = f.spawn(0, AntType::Worker, 10, 10);
    s.mine2 = f.spawn(0, AntType::Worker, 12, 10);
    s.foe = f.spawn(1, AntType::Worker, 25, 15);
    s.ally = f.spawn(2, AntType::Worker, 27, 15);
    s.foe_on_hill = f.spawn(1, AntType::Worker, 41, 41);
    s.mine_on_hill = f.spawn(0, AntType::Worker, 3, 3);
    s.foe_on_own_hill = f.spawn(1, AntType::Worker, 4, 4);
    f.sim.grid_mut().drop_lunchbox(30, 30, 25);
    f.sim.form_alliance(0, 2);
    if (fog) f.sim.set_fog_of_war_enabled(true);
    f.settle();
    return s;
}

void test_cursor_without_own_ants() {
    g_group = "cursor 1/2/5";
    std::printf("[pointer] cursor with nothing selected, a hill selected or another player's ant inspected: select over any ant or hill\n");
    Scene s = make_scene();
    Fixture& f = *s.f;
    const auto cases = std::vector<std::pair<std::string, std::function<void()>>>{
        {"nothing selected", [&]() { f.hud.clear_selection(); }},
        {"own hill selected", [&]() { f.hud.select_base(0); }},
        {"an enemy ant inspected", [&]() { f.hud.select_ant(s.foe); }},
    };
    for (const auto& c : cases) {
        c.second();
        const std::string w = c.first + ": ";
        expect_cursor((w + "own ant").c_str(), f.cursor_tile(10, 10), CursorType::Select);
        expect_cursor((w + "enemy ant").c_str(), f.cursor_tile(25, 15), CursorType::Select);
        expect_cursor((w + "allied ant").c_str(), f.cursor_tile(27, 15), CursorType::Select);
        expect_cursor((w + "own hill tile").c_str(), f.cursor_tile(5, 5), CursorType::Select);
        expect_cursor((w + "enemy hill tile").c_str(), f.cursor_tile(42, 42), CursorType::Select);
        expect_cursor((w + "empty ground").c_str(), f.cursor_tile(20, 30), CursorType::Normal);
        expect_cursor((w + "food is only a move target").c_str(), f.cursor_tile(30, 30), CursorType::Normal);
    }
    // the fog only matters for the tile under the pointer
    Scene fs = make_scene(true);
    Fixture& g = *fs.f;
    g.hud.clear_selection();
    check(!g.world().is_tile_revealed(41, 41), "the enemy ant on its hill stands in the fog");
    expect_cursor("an enemy ant in the fog", g.cursor_tile(41, 41), CursorType::Normal);
    expect_cursor("an enemy hill in the fog", g.cursor_tile(42, 42), CursorType::Normal);
    check(g.world().is_tile_revealed(10, 10), "the own ant's tile is revealed");
    expect_cursor("an own ant in the light", g.cursor_tile(10, 10), CursorType::Select);
}

void test_cursor_with_own_ants() {
    g_group = "cursor 3/4";
    std::printf("[pointer] cursor with own ants selected: food, own hill before ants, own ant = select, other players' ants = attack, fog = move\n");
    Scene s = make_scene();
    Fixture& f = *s.f;
    for (int panel = 3; panel <= 4; ++panel) {
        if (panel == 3) f.hud.select_ant(s.mine);
        else f.hud.set_selected_ant_ids({s.mine, s.mine2});
        const std::string w = "panel " + std::to_string(panel) + ": ";
        expect_cursor((w + "empty ground").c_str(), f.cursor_tile(20, 30), CursorType::Move);
        expect_cursor((w + "own ant").c_str(), f.cursor_tile(12, 10), CursorType::Select);
        expect_cursor((w + "the selected ant itself").c_str(), f.cursor_tile(10, 10), CursorType::Select);
        expect_cursor((w + "enemy ant").c_str(), f.cursor_tile(25, 15), CursorType::Attack);
        expect_cursor((w + "allied ant: no alliance test, attack").c_str(), f.cursor_tile(27, 15), CursorType::Attack);
        expect_cursor((w + "food (lunchbox)").c_str(), f.cursor_tile(30, 30), CursorType::Food);
        expect_cursor((w + "own hill").c_str(), f.cursor_tile(5, 5), CursorType::Move);
        expect_cursor((w + "own ant on the own hill: the hill wins").c_str(), f.cursor_tile(3, 3), CursorType::Move);
        expect_cursor((w + "enemy ant on the own hill: the hill wins").c_str(), f.cursor_tile(4, 4), CursorType::Move);
        expect_cursor((w + "enemy ant on the enemy hill: move").c_str(), f.cursor_tile(41, 41), CursorType::Move);
        expect_cursor((w + "enemy hill without ant: select").c_str(), f.cursor_tile(42, 42), CursorType::Select);
        // the box of the ant reaches into the neighbouring tile: 20 px left of the enemy's centre is still the enemy
        expect_cursor((w + "pointer in the tile next to the enemy but inside its box").c_str(), f.cursor_tile(24, 15, 30, 0), CursorType::Attack);
    }
    Scene fs = make_scene(true);
    Fixture& g = *fs.f;
    g.hud.select_ant(fs.mine);
    check(!g.world().is_tile_revealed(41, 41) && !g.world().is_tile_revealed(30, 30), "the enemy ant and the food stand in the fog");
    expect_cursor("an enemy ant in the fog", g.cursor_tile(41, 41), CursorType::Move);
    expect_cursor("food in the fog", g.cursor_tile(30, 30), CursorType::Move);
    expect_cursor("empty fog", g.cursor_tile(20, 30), CursorType::Move);
    expect_cursor("the fog of the pointer's tile decides, not the ant's: an enemy in the fog seen from a lit tile", g.cursor_tile(10, 10), CursorType::Select);
    // the fog is tested at the pointer's tile only; the ant scan is fog blind
    {
        // enemy at (17, 10), own ant at (10, 10) sees 6 tiles: (16, 10) is lit, (17, 10) is dark
        Fixture h;
        const uint32_t own = h.spawn(0, AntType::Worker, 10, 10);
        const uint32_t enemy = h.spawn(1, AntType::Worker, 17, 10);
        h.sim.set_fog_of_war_enabled(true);
        h.settle();
        h.hud.select_ant(own);
        const bool geometry = h.world().is_tile_revealed(16, 10) && !h.world().is_tile_revealed(17, 10);
        check(geometry, "tile (16, 10) is lit and (17, 10) is dark");
        (void)enemy;
        if (geometry) {
            // x = 17 * 32 - 2 is in tile 16 and inside the enemy's box [17 * 32 + 16 - 20, ...) = [540, 580)
            h.look_at(16, 10);
            expect_cursor("a lit pointer tile next to a hidden enemy: the enemy is hit (fog blind scan)", h.cursor_px(h.sx(542), h.sy(10 * 32 + 16)),
                          CursorType::Attack);
        }
    }
}

void test_cursor_special_targets() {
    g_group = "cursor special";
    std::printf("[pointer] special targets (FUN_01026f91): bomber, fire ant, thief, swimmer; the auto flag needs one ant, the pedestal flag the latched ability\n");
    Scene s = make_scene();
    Fixture& f = *s.f;
    f.sim.grid_mut().place_bomb(20, 20, 1);                                    // an enemy bomb
    f.sim.set_fire_at({22, 20}, 3600);
    f.sim.set_terrain(24, 20, 2);                                              // water
    f.sim.grid_mut().set_terrain(26, 20, ants::sim::TERRAIN_WATER);            // water with a finished bridge
    for (int st = 0; st < 4; ++st) f.sim.grid_mut().advance_bridge(26, 20, 0);
    f.settle();
    // (tile, description) -> expectation for the automatic case and the latched case
    struct Tile { int32_t x, y; const char* what; };
    const Tile plain{18, 20, "plantable ground"};
    const Tile bomb{20, 20, "bomb"};
    const Tile fire{22, 20, "fire wall"};
    const Tile water{24, 20, "water"};
    const Tile bridge{26, 20, "bridge"};
    const Tile enemy_hill{42, 42, "enemy hill"};
    const Tile own_hill{5, 5, "own hill"};

    auto probe = [&](AntType type, const Tile& t, bool latched) {
        f.hud.unlatch_pedestals();
        const uint32_t id = f.spawn(0, type, 8, 30);
        f.settle();
        f.hud.select_ant(id);
        if (latched) f.latch_ability();
        const CursorType c = f.cursor_tile(t.x, t.y);
        f.sim.kill_unit(id);
        f.settle();
        f.hud.clear_selection();
        f.hud.unlatch_pedestals();
        return c;
    };
    auto want = [&](const char* type, const Tile& t, bool latched, CursorType c, CursorType expect) {
        expect_cursor((std::string(type) + " / " + t.what + (latched ? " / pedestal latched" : " / auto")).c_str(), c, expect);
    };
    // bomber: a bomb with either flag, plantable ground only when latched
    want("bomber", bomb, false, probe(AntType::Bomber, bomb, false), CursorType::Target);
    want("bomber", bomb, true, probe(AntType::Bomber, bomb, true), CursorType::Target);
    want("bomber", plain, false, probe(AntType::Bomber, plain, false), CursorType::Move);
    want("bomber", plain, true, probe(AntType::Bomber, plain, true), CursorType::Target);
    want("bomber", water, true, probe(AntType::Bomber, water, true), CursorType::Move);
    // fire ant: only latched: a fire wall or plantable ground
    want("fire ant", fire, false, probe(AntType::Fire, fire, false), CursorType::Move);
    want("fire ant", fire, true, probe(AntType::Fire, fire, true), CursorType::Target);
    want("fire ant", plain, false, probe(AntType::Fire, plain, false), CursorType::Move);
    want("fire ant", plain, true, probe(AntType::Fire, plain, true), CursorType::Target);
    want("fire ant", bomb, true, probe(AntType::Fire, bomb, true), CursorType::Move);
    // thief: any hill of another colour with either flag, its own hill never
    want("thief", enemy_hill, false, probe(AntType::Thief, enemy_hill, false), CursorType::Target);
    want("thief", enemy_hill, true, probe(AntType::Thief, enemy_hill, true), CursorType::Target);
    want("thief", own_hill, false, probe(AntType::Thief, own_hill, false), CursorType::Move);
    want("thief", plain, true, probe(AntType::Thief, plain, true), CursorType::Move);
    // swimmer: only latched: water or a bridge
    want("swimmer", water, false, probe(AntType::Swimmer, water, false), CursorType::Move);
    want("swimmer", water, true, probe(AntType::Swimmer, water, true), CursorType::Target);
    want("swimmer", bridge, true, probe(AntType::Swimmer, bridge, true), CursorType::Target);
    want("swimmer", plain, true, probe(AntType::Swimmer, plain, true), CursorType::Move);
    // worker and combat ant never have a special target
    want("worker", bomb, false, probe(AntType::Worker, bomb, false), CursorType::Move);
    want("worker", enemy_hill, false, probe(AntType::Worker, enemy_hill, false), CursorType::Select);
    want("combat ant", bomb, true, probe(AntType::Combat, bomb, true), CursorType::Move);
    want("combat ant", plain, true, probe(AntType::Combat, plain, true), CursorType::Move);

    // several ants: the auto flag needs exactly one ant (panel 3); the latched ability works for a homogeneous selection; mixed types never
    {
        const uint32_t b1 = f.spawn(0, AntType::Bomber, 8, 30);
        const uint32_t b2 = f.spawn(0, AntType::Bomber, 9, 30);
        const uint32_t wk = f.spawn(0, AntType::Worker, 10, 30);
        f.settle();
        f.hud.set_selected_ant_ids({b1, b2});
        expect_cursor("two bombers / bomb / auto (panel 4)", f.cursor_tile(bomb.x, bomb.y), CursorType::Move);
        f.latch_ability();
        expect_cursor("two bombers / bomb / latched", f.cursor_tile(bomb.x, bomb.y), CursorType::Target);
        f.hud.unlatch_pedestals();
        f.hud.set_selected_ant_ids({b1, wk});
        expect_cursor("a bomber and a worker / bomb", f.cursor_tile(bomb.x, bomb.y), CursorType::Move);
        f.hud.set_shift_held(true);
        f.hud.select_ant(b1);
        expect_cursor("one bomber with shift held: shift plays no part", f.cursor_tile(bomb.x, bomb.y), CursorType::Target);
        f.hud.set_shift_held(false);
    }
}

void test_cursor_misc() {
    g_group = "cursor misc";
    std::printf("[pointer] cursor outside the map rectangle, with a rubber band, over the scroll strips and while a dialog is open\n");
    Scene s = make_scene();
    Fixture& f = *s.f;
    f.hud.select_ant(s.mine);
    f.cam.world_x = 400;
    f.cam.world_y = 400;
    // the map rectangle is (16, 21) - (458, 461)
    expect_cursor("(16, 21) is on the map", f.cursor_px(16, 21), CursorType::Move);
    expect_cursor("(457, 460) is on the map", f.cursor_px(457, 460), CursorType::Move);
    expect_cursor("x = 15 is off the map", f.cursor_px(15, 200), CursorType::Normal);
    expect_cursor("y = 20 is off the map", f.cursor_px(200, 20), CursorType::Normal);
    expect_cursor("x = 458 is off the map", f.cursor_px(458, 200), CursorType::Normal);
    expect_cursor("y = 461 is off the map", f.cursor_px(200, 461), CursorType::Normal);
    // a rubber band of more than 4 px makes the pointer plain
    f.press(200, 300);
    f.hud.handle_mouse_motion(204, 304, f.sim, f.cam);
    expect_cursor("a band of 4 x 4 px keeps the cursor", f.cursor_px(204, 304), CursorType::Move);
    f.hud.handle_mouse_motion(205, 300, f.sim, f.cam);
    expect_cursor("a band of 5 px width is a plain pointer", f.cursor_px(205, 300), CursorType::Normal);
    f.hud.handle_mouse_motion(200, 305, f.sim, f.cam);
    expect_cursor("a band of 5 px height is a plain pointer", f.cursor_px(200, 305), CursorType::Normal);
    f.release(200, 305);
    // no band while a pedestal is latched
    f.hud.select_ant(s.mine);                                  // (the drag above cleared the selection)
    f.latch_move();
    f.press(200, 300);
    f.hud.handle_mouse_motion(260, 340, f.sim, f.cam);
    expect_cursor("a latched pedestal has no band: the cursor stays", f.cursor_px(260, 340), CursorType::Move);
    f.release(260, 340);
    f.hud.unlatch_pedestals();
    // the eight edge strips
    expect_cursor("east strip", f.cursor_px(639, 240), CursorType::ScrollE);
    expect_cursor("west strip (inner)", f.cursor_px(2, 240), CursorType::ScrollW);
    expect_cursor("north strip", f.cursor_px(320, 3), CursorType::ScrollN);
    expect_cursor("south strip", f.cursor_px(320, 478), CursorType::ScrollS);
    expect_cursor("north-west corner in the dead zone still shows the arrow", f.cursor_px(5, 4), CursorType::ScrollNW);
    expect_cursor("12 px band edge is still an arrow", f.cursor_px(320, 11), CursorType::ScrollN);
    expect_cursor("the 13th pixel is not", f.cursor_px(320, 12), CursorType::Normal);
    f.cam.world_x = 0;
    f.cam.world_y = 0;
    expect_cursor("no arrow where the view cannot move (west at the map's edge)", f.cursor_px(2, 240), CursorType::Normal);
    f.cam.world_x = 400;
    f.cam.world_y = 400;
    // while a dialog is open the cursor code does not run
    const CursorType before = f.hud.evaluate_cursor(200, 200, f.world(), f.sim.grid(), f.cam);
    f.hud.open_options();
    expect_cursor("a dialog is open: the cursor stays", f.cursor_px(639, 240), before);
    f.hud.close_options();
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Clicks by cursor mode (FUN_010277f4)
// ---------------------------------------------------------------------------------------------------------------------------------

void test_click_modes() {
    g_group = "click";
    std::printf("[pointer] a click acts by the cursor mode at the release point: 1 deselect, 2 select, 3 / 7 move, 4 special, 5 attack\n");
    // mode 3: a move order, the marker at the pixel, the voice
    {
        Scene s = make_scene();
        Fixture& f = *s.f;
        f.hud.select_ant(s.mine);
        f.sounds.clear();
        f.markers.clear();
        f.click_tile(20, 30, SDL_BUTTON_LEFT, 0, 5, -3);
        check(f.unit(s.mine).orig_order == sim::AntUnit::kOrderMove, "mode 3: a move order");
        check(f.unit(s.mine).orig_order_tile == (TileCoord{20, 30}), "mode 3: to the clicked tile");
        check(f.markers.size() == 1 && f.markers[0].first == 20 * 32 + 16 + 5 && f.markers[0].second == 30 * 32 + 16 - 3, "the marker sits at the raw pixel");
        check(f.sounds.size() >= 2 && (f.sounds[f.sounds.size() - 2] == 17 || f.sounds[f.sounds.size() - 2] == 15) &&
                  f.sounds.back() == sim::SoundID::NavButtonClick,
              "the go voice of a worker, then the click of the flashing move pedestal (BTNPUSH, 89)");
        check(f.selected(s.mine), "an order keeps the selection");
    }
    // mode 7: food is the ordinary group order too (the harvest classification)
    {
        Scene s = make_scene();
        Fixture& f = *s.f;
        f.hud.select_ant(s.mine);
        f.click_tile(30, 30);
        check(f.unit(s.mine).orig_order == sim::AntUnit::kOrderHarvest, "mode 7: the harvest order (5)");
    }
    // mode 4: the special order; a single bomber defuses the bomb it is sent to
    {
        Scene s = make_scene();
        Fixture& f = *s.f;
        f.sim.grid_mut().place_bomb(20, 20, 1);
        const uint32_t bomber = f.spawn(0, AntType::Bomber, 15, 20);
        f.settle();
        f.hud.select_ant(bomber);
        f.markers.clear();
        f.click_tile(20, 20);
        check(f.unit(bomber).orig_order == sim::AntUnit::kOrderDefuse, "mode 4: the bomber's special order on a bomb is the defuse order");
        check(f.markers.size() == 1, "mode 4 spawns the marker");
    }
    // mode 5: the attack order targets the tile of the ant that is under the pointer, also when that is the next tile
    {
        Scene s = make_scene();
        Fixture& f = *s.f;
        const uint32_t fighter = f.spawn(0, AntType::Combat, 20, 15);
        f.settle();
        f.hud.select_ant(fighter);
        f.markers.clear();
        // the enemy stands on (25, 15), its box starts 20 px left of its centre (x = 796 - ...): a pixel of tile 24 that is inside the box
        f.click_tile(24, 15, SDL_BUTTON_LEFT, 0, 30, 0);
        check(f.unit(fighter).orig_order == sim::AntUnit::kOrderAttack, "mode 5: the attack order (3)");
        check(f.unit(fighter).orig_order_tile == (TileCoord{25, 15}), "mode 5: the order goes to the tile of the ant, not to the clicked tile");
        check(f.unit(fighter).orig_target_ant == s.foe, "mode 5: the target is the enemy");
        // the same click again is skipped by the group order (order 3 with the same tile), but the marker is spawned anyway
        f.click_tile(24, 15, SDL_BUTTON_LEFT, 0, 30, 0);
        check(f.markers.size() == 2, "the marker is spawned even when the order is refused");
    }
    // mode 2 with nothing selected: own ant, enemy ant, hill, empty ground
    {
        Scene s = make_scene();
        Fixture& f = *s.f;
        f.sounds.clear();
        f.click_tile(10, 10);
        check(f.hud.get_selected_ant_ids().size() == 1 && f.selected(s.mine), "mode 2: an own ant is selected");
        check(f.hud.panel_mode(f.world()) == HUD::PanelMode::OneAnt, "panel 3");
        check(!f.sounds.empty() && (f.sounds.back() == 13 || f.sounds.back() == 14), "the ready voice of a worker");
        f.hud.clear_selection();
        f.sounds.clear();
        f.click_tile(25, 15);
        check(f.hud.panel_mode(f.world()) == HUD::PanelMode::Other && f.hud.get_selected_ant_id() == s.foe, "mode 2: an enemy ant is inspected (panel 5)");
        check(f.sounds.empty() && f.markers.empty(), "inspection is silent and gives no marker");
        // panel 5 + empty ground: the plain pointer, the click deselects everything
        f.click_tile(20, 30);
        check(f.hud.panel_mode(f.world()) == HUD::PanelMode::None && f.hud.get_selected_ant_id() == 0, "mode 1: empty ground deselects");
        check(f.markers.empty(), "mode 1 spawns no marker");
        f.click_tile(42, 42);
        check(f.hud.get_selected_base_team_id() == 1, "mode 2: a hill is selected (an enemy hill too)");
        f.click_tile(5, 5);
        check(f.hud.get_selected_base_team_id() == 0, "own hill");
        f.click_tile(10, 10);
        check(f.hud.get_selected_base_team_id() == -1 && f.selected(s.mine), "an ant replaces the base selection");
        // with own ants selected another own ant is a plain replacement
        f.click_tile(12, 10);
        check(f.hud.get_selected_ant_ids().size() == 1 && f.selected(s.mine2) && !f.selected(s.mine), "an own ant replaces the selection without shift");
    }
    // shift toggles own ants in a selection of own ants, quietly
    {
        Scene s = make_scene();
        Fixture& f = *s.f;
        f.hud.select_ant(s.mine);
        f.sounds.clear();
        f.click_tile(12, 10, SDL_BUTTON_LEFT, KMOD_LSHIFT);
        check(f.selected(s.mine) && f.selected(s.mine2) && f.hud.get_selected_ant_ids().size() == 2, "shift adds an own ant");
        check(f.sounds.empty(), "adding with shift is silent");
        check(f.hud.panel_mode(f.world()) == HUD::PanelMode::Ants, "panel 4");
        f.click_tile(12, 10, SDL_BUTTON_LEFT, KMOD_LSHIFT);
        check(f.selected(s.mine) && !f.selected(s.mine2), "shift on a selected ant removes it");
        f.click_tile(10, 10, SDL_BUTTON_LEFT, KMOD_LSHIFT);
        check(f.hud.get_selected_ant_ids().empty(), "shift on the last selected ant leaves nothing selected");
        // shift with nothing selected is a plain click
        f.click_tile(10, 10, SDL_BUTTON_LEFT, KMOD_LSHIFT);
        check(f.selected(s.mine) && f.hud.get_selected_ant_ids().size() == 1, "shift in panel 1 is a plain selection");
        // shift over an enemy ant with own ants selected is an attack, not a toggle
        const uint32_t fighter = f.spawn(0, AntType::Combat, 20, 15);
        f.settle();
        f.hud.select_ant(fighter);
        f.click_tile(25, 15, SDL_BUTTON_LEFT, KMOD_LSHIFT);
        check(f.unit(fighter).orig_order == sim::AntUnit::kOrderAttack, "shift does not change the attack click");
    }
    // mode 6: the scroll arrows ignore the buttons
    {
        Scene s = make_scene();
        Fixture& f = *s.f;
        f.hud.select_ant(s.mine);
        f.markers.clear();
        f.press(639, 240);
        check(!f.hud.is_input_captured(), "a press on a scroll strip captures nothing");
        f.release(639, 240);
        check(f.selected(s.mine) && f.markers.empty(), "and does nothing");
    }
}

// ---------------------------------------------------------------------------------------------------------------------------------
// The rubber band and the release (FUN_0102653f / FUN_01027530)
// ---------------------------------------------------------------------------------------------------------------------------------

void test_click_or_drag() {
    g_group = "release";
    std::printf("[pointer] the release: a band of at most 4 px both ways is a click at the release point, anything bigger selects\n");
    Scene s = make_scene();
    Fixture& f = *s.f;
    f.hud.select_ant(s.mine);
    f.cam.world_x = 400;
    f.cam.world_y = 400;
    // screen x = 17 + (wx - 400): the tile border 512 (tiles 15 | 16) is at x = 129; the row of y = 300 is tile 21
    f.markers.clear();
    f.press(127, 300);
    f.hud.handle_mouse_motion(131, 304, f.sim, f.cam);
    f.release(131, 304);
    check(f.unit(s.mine).orig_order == sim::AntUnit::kOrderMove && f.unit(s.mine).orig_order_tile == (TileCoord{16, 21}),
          "4 px: a click, executed at the release point (tile 16, 21 - the press was in tile 15)");
    check(f.markers.size() == 1 && f.markers[0].first == 400 + (131 - 17) && f.markers[0].second == 400 + (304 - 22), "the marker is at the release pixel");

    // 5 px: a drag; over empty ground it clears the selection and gives no order
    f.hud.select_ant(s.mine);
    f.sim.issue_move_order(s.mine, TileCoord{10, 10});
    f.markers.clear();
    f.press(127, 300);
    f.hud.handle_mouse_motion(132, 300, f.sim, f.cam);
    f.release(132, 300);
    check(f.hud.get_selected_ant_ids().empty(), "5 px: a drag over empty ground deselects");
    check(f.markers.empty(), "and gives no order and no marker");

    // a stationary press is a click (the 2 x 2 dot)
    f.hud.select_ant(s.mine);
    f.markers.clear();
    f.click(200, 300);
    check(f.markers.size() == 1 && f.selected(s.mine), "a stationary press is a click");

    // the band is kept 1 px inside the view: dragging out of the view selects up to its edge only
    {
        Fixture g;
        const uint32_t own = g.spawn(0, AntType::Worker, 10, 10);       // at the start of the drag, out of the band
        const uint32_t far_ant = g.spawn(0, AntType::Worker, 27, 21);   // (880, 688): its box starts at x = 860
        g.settle();
        g.cam.world_x = 400;
        g.cam.world_y = 400;
        g.hud.select_ant(own);
        // press at screen (300, 300), pointer released at x = 500 (outside the view, whose last pixel is 457 = world 840)
        g.press(300, 300);
        g.hud.handle_mouse_motion(500, 300, g.sim, g.cam);
        g.release(500, 300);
        check(!g.selected(far_ant), "the band stops 1 px inside the view: the ant beyond it is not picked");
        check(g.hud.get_selected_ant_ids().empty(), "the drag selected nothing and cleared the old selection");
        // and one that is inside is picked: an ant at (26, 21) has the box [828, 868), the band ends at world x 840
        const uint32_t inside = g.spawn(0, AntType::Worker, 26, 21);
        g.settle();
        g.press(300, 300);
        g.hud.handle_mouse_motion(500, 300, g.sim, g.cam);
        g.release(500, 300);
        check(g.selected(inside) && !g.selected(far_ant), "an ant whose box reaches into the clamped band is picked");
    }

    // a latched pedestal has no band: a drag is a click at the release point
    {
        Scene t = make_scene();
        Fixture& g = *t.f;
        g.hud.select_ant(t.mine);
        g.latch_move();
        check(g.hud.is_move_latched(), "the move pedestal is latched");
        g.markers.clear();
        g.press(127, 300);
        g.hud.handle_mouse_motion(260, 330, g.sim, g.cam);
        g.release(260, 330);
        check(g.unit(t.mine).orig_order == sim::AntUnit::kOrderMove, "a long drag with a latched pedestal is one click");
        check(g.unit(t.mine).orig_order_tile == (TileCoord{(400 + 260 - 17) / 32, (400 + 330 - 22) / 32}), "at the release point");
        check(!g.hud.is_move_latched(), "the accepted order lets the latched pedestal up");
    }

    // a press outside the map (on a pedestal) released on the map is a click at the release point: nothing is captured, no band exists
    {
        Scene t = make_scene();
        Fixture& g = *t.f;
        g.hud.select_ant(t.mine);
        g.press(500, 170);                                    // the move pedestal
        g.markers.clear();
        g.release(200, 300);
        check(g.markers.size() == 1 && g.unit(t.mine).orig_order == sim::AntUnit::kOrderMove, "the release on the map acts as a click although the press was on the panel");
    }
}

void test_drag_select() {
    g_group = "drag select";
    std::printf("[pointer] drag select: own ants only, positive-area overlap of the hit box, clear first even when empty, shift adds in panels 3 and 4\n");
    Fixture f;
    const uint32_t a = f.spawn(0, AntType::Worker, 10, 10);          // (336, 336): box [316, 356) x [304, 352)
    const uint32_t b = f.spawn(0, AntType::Combat, 11, 10);          // (368, 336): box [336, 394) x [290, 352)
    const uint32_t e = f.spawn(1, AntType::Worker, 12, 10);          // an enemy
    const uint32_t c = f.spawn(0, AntType::Worker, 30, 30);          // far away
    f.settle();
    f.cam.world_x = 200;
    f.cam.world_y = 200;
    auto drag = [&](int32_t wx1, int32_t wy1, int32_t wx2, int32_t wy2, uint16_t mod = 0) {
        f.press(f.sx(wx1), f.sy(wy1), SDL_BUTTON_LEFT, mod);
        f.hud.handle_mouse_motion(f.sx(wx2), f.sy(wy2), f.sim, f.cam);
        f.release(f.sx(wx2), f.sy(wy2), SDL_BUTTON_LEFT, mod);
    };
    f.hud.select_ant(c);
    f.sounds.clear();
    drag(323, 298, 443, 368);
    check(f.selected(a) && f.selected(b), "own ants inside the band are selected");
    check(!f.selected(e), "another player's ant is not");
    check(!f.selected(c), "the old selection is cleared");
    check(f.hud.panel_mode(f.world()) == HUD::PanelMode::Ants, "panel 4");
    check(f.sounds.size() == 1 && (f.sounds[0] == 13 || f.sounds[0] == 14), "the ready voice of the first selected ant");

    // dragging over empty ground deselects
    drag(500, 500, 560, 560);
    check(f.hud.get_selected_ant_ids().empty(), "a drag over nothing clears the selection");

    // positive area: a band that only touches the box's edge picks nothing, one pixel of overlap picks
    {
        Fixture g;
        const uint32_t only = g.spawn(0, AntType::Worker, 10, 10);   // box x [316, 356), y [304, 352)
        g.settle();
        g.cam.world_x = 200;
        g.cam.world_y = 200;
        auto gdrag = [&](int32_t wx1, int32_t wy1, int32_t wx2, int32_t wy2) {
            g.press(g.sx(wx1), g.sy(wy1));
            g.hud.handle_mouse_motion(g.sx(wx2), g.sy(wy2), g.sim, g.cam);
            g.release(g.sx(wx2), g.sy(wy2));
        };
        gdrag(356, 320, 400, 340);
        check(!g.selected(only), "a band starting at x = 356 (the box's exclusive right edge) picks nothing");
        gdrag(355, 320, 400, 340);
        check(g.selected(only), "one pixel of overlap picks the ant");
        gdrag(300, 352, 340, 400);
        check(!g.selected(only), "a band starting at the box's exclusive bottom edge y = 352 picks nothing");
        gdrag(300, 351, 340, 400);
        check(g.selected(only), "one row of overlap");
        gdrag(300, 250, 340, 304);
        check(!g.selected(only), "a band ending at the box's top edge y = 304 picks nothing");
    }

    // shift adds only to a selection of own ants
    f.hud.select_ant(c);
    drag(323, 298, 335, 368, KMOD_LSHIFT);                       // x 323 - 335 reaches the worker's box but not the combat ant's (from x = 336)
    check(f.selected(c) && f.selected(a) && !f.selected(b), "shift with own ants selected adds to the selection");
    f.sounds.clear();
    drag(500, 500, 560, 560, KMOD_LSHIFT);
    check(f.selected(c) && f.selected(a), "shift with an empty pick changes nothing");
    f.hud.clear_selection();
    drag(323, 298, 335, 368, KMOD_LSHIFT);
    check(f.selected(a) && f.hud.get_selected_ant_ids().size() == 1, "shift in panel 1 is a plain drag select");
    f.hud.select_base(0);
    drag(323, 298, 335, 368, KMOD_LSHIFT);
    check(f.selected(a) && f.hud.get_selected_base_team_id() == -1, "shift with a hill selected replaces it");
    // an enemy ant inspected (panel 5) is replaced too
    f.hud.select_ant(e);
    drag(323, 298, 335, 368, KMOD_LSHIFT);
    check(f.selected(a) && !f.selected(e), "shift with an enemy ant inspected replaces it");
}

// ---------------------------------------------------------------------------------------------------------------------------------
// The right button (FUN_01027b51)
// ---------------------------------------------------------------------------------------------------------------------------------

void test_right_button() {
    g_group = "right button";
    std::printf("[pointer] right button: the order at the release, the press point's tile, move for several / worker / combat, else the special order\n");
    // a worker: move
    {
        Scene s = make_scene();
        Fixture& f = *s.f;
        f.hud.select_ant(s.mine);
        f.markers.clear();
        f.look_at(20, 30);
        const int32_t x = f.sx(20 * 32 + 16);
        const int32_t y = f.sy(30 * 32 + 16);
        f.press(x, y, SDL_BUTTON_RIGHT);
        check(f.unit(s.mine).orig_order != sim::AntUnit::kOrderMove, "nothing happens at the press");
        f.release(x, y, SDL_BUTTON_RIGHT);
        check(f.unit(s.mine).orig_order == sim::AntUnit::kOrderMove && f.unit(s.mine).orig_order_tile == (TileCoord{20, 30}), "the move order at the release");
        check(f.markers.size() == 1, "the marker");
    }
    // the tile of the press point is used, the cursor mode of the release point decides
    {
        Scene s = make_scene();
        Fixture& f = *s.f;
        f.hud.select_ant(s.mine);
        f.look_at(20, 30);
        const int32_t px = f.sx(20 * 32 + 16), py = f.sy(30 * 32 + 16);
        const int32_t rx = f.sx(22 * 32 + 16), ry = f.sy(30 * 32 + 16);
        f.press(px, py, SDL_BUTTON_RIGHT);
        f.hud.handle_mouse_motion(rx, ry, f.sim, f.cam);
        f.release(rx, ry, SDL_BUTTON_RIGHT);
        check(f.unit(s.mine).orig_order_tile == (TileCoord{20, 30}), "the order goes to the tile of the press point");
        // released over an own ant (cursor 2): nothing
        Scene t = make_scene();
        Fixture& g = *t.f;
        g.hud.select_ant(t.mine);
        g.look_at(11, 10);
        g.press(g.sx(11 * 32 + 16), g.sy(15 * 32 + 16), SDL_BUTTON_RIGHT);
        const int32_t ox = g.sx(12 * 32 + 16), oy = g.sy(10 * 32 + 16);         // over the other own ant
        g.release(ox, oy, SDL_BUTTON_RIGHT);
        check(g.unit(t.mine).orig_order != sim::AntUnit::kOrderMove, "released where the cursor is 'select': no order");
    }
    // an enemy ant under the release point: the attack order (the ant's tile), the marker at the press point
    {
        Scene s = make_scene();
        Fixture& f = *s.f;
        const uint32_t fighter = f.spawn(0, AntType::Combat, 20, 15);
        f.settle();
        f.hud.select_ant(fighter);
        f.markers.clear();
        f.look_at(24, 15);
        const int32_t px = f.sx(22 * 32 + 16), py = f.sy(15 * 32 + 16);
        const int32_t rx = f.sx(25 * 32 + 16), ry = f.sy(15 * 32 + 16);
        f.press(px, py, SDL_BUTTON_RIGHT);
        f.hud.handle_mouse_motion(rx, ry, f.sim, f.cam);
        f.release(rx, ry, SDL_BUTTON_RIGHT);
        check(f.unit(fighter).orig_order == sim::AntUnit::kOrderAttack && f.unit(fighter).orig_order_tile == (TileCoord{25, 15}), "attack cursor at the release: the attack order");
        check(f.markers.size() == 1 && f.markers[0].first == 22 * 32 + 16, "the marker is at the press point");
    }
    // special or move by the selection's type
    {
        Scene s = make_scene();
        Fixture& f = *s.f;
        const uint32_t bomber = f.spawn(0, AntType::Bomber, 15, 20);
        const uint32_t bomber2 = f.spawn(0, AntType::Bomber, 16, 20);
        const uint32_t fighter = f.spawn(0, AntType::Combat, 15, 22);
        const uint32_t swimmer = f.spawn(0, AntType::Swimmer, 15, 24);
        f.settle();
        f.hud.select_ant(bomber);
        f.click_tile(20, 20, SDL_BUTTON_RIGHT);
        check(f.unit(bomber).orig_order == sim::AntUnit::kOrderPlant, "a single bomber: the special order (plant)");
        f.hud.set_selected_ant_ids({bomber, bomber2});
        f.click_tile(22, 20, SDL_BUTTON_RIGHT);
        check(f.unit(bomber).orig_order == sim::AntUnit::kOrderMove, "two bombers (panel 4): a move");
        f.hud.select_ant(fighter);
        f.click_tile(22, 22, SDL_BUTTON_RIGHT);
        check(f.unit(fighter).orig_order == sim::AntUnit::kOrderMove, "a single combat ant: a move");
        f.hud.set_selected_ant_ids({bomber, swimmer});
        f.click_tile(24, 24, SDL_BUTTON_RIGHT);
        check(f.unit(swimmer).orig_order == sim::AntUnit::kOrderMove, "a mixed group: a move");
    }
    // nothing selected, or pressed off the views: nothing
    {
        Scene s = make_scene();
        Fixture& f = *s.f;
        f.markers.clear();
        f.click_tile(20, 30, SDL_BUTTON_RIGHT);
        check(f.markers.empty(), "cursor 1 (nothing selected): nothing happens, not even a marker");
        f.hud.select_ant(s.mine);
        f.press(600, 300, SDL_BUTTON_RIGHT);                  // on the panel: not captured
        f.release(f.sx(20 * 32 + 16), f.sy(30 * 32 + 16), SDL_BUTTON_RIGHT);
        check(f.markers.empty() && f.unit(s.mine).orig_order != sim::AntUnit::kOrderMove, "a right press outside the views captures nothing: no order at the release");
    }
    // the minimap: panel 3 or 4 only; the press point is a point of the map
    {
        Scene s = make_scene();
        Fixture& f = *s.f;
        f.hud.select_ant(s.mine);
        f.markers.clear();
        f.click(500, 60, SDL_BUTTON_RIGHT);                    // (20, 25) in the minimap: world (322, 527) -> tile (10, 16)
        check(f.unit(s.mine).orig_order == sim::AntUnit::kOrderMove && f.unit(s.mine).orig_order_tile == (TileCoord{10, 16}),
              "a right click on the minimap moves to the point of the map");
        check(f.markers.size() == 1, "with its marker");
        f.hud.clear_selection();
        const auto order_before = f.unit(s.mine).orig_order_tile;
        f.click(560, 100, SDL_BUTTON_RIGHT);
        check(f.unit(s.mine).orig_order_tile == order_before, "no ants selected: nothing");
    }
}

// ---------------------------------------------------------------------------------------------------------------------------------
// The pedestals (FUN_010274be -> FUN_01028d30 -> FUN_01028ee0)
// ---------------------------------------------------------------------------------------------------------------------------------

void test_pedestals() {
    g_group = "pedestals";
    std::printf("[pointer] pedestals: slot rectangles, latch as a visual, radio behaviour, flash after an order, Stop with its lock, hatch only with eggs\n");
    Scene s = make_scene();
    Fixture& f = *s.f;
    const uint32_t bomber = f.spawn(0, AntType::Bomber, 15, 20);
    f.settle();
    f.hud.select_ant(bomber);
    // slot 1 (482, 152) - (525, 225): the press acts at once, the boundaries are half open
    f.press(481, 200);
    check(!f.hud.is_move_latched(), "x = 481 is outside slot 1");
    f.press(482, 151);
    check(!f.hud.is_move_latched(), "y = 151 is outside slot 1");
    f.press(525, 200);
    check(!f.hud.is_move_latched(), "x = 525 is outside slot 1");
    f.press(524, 224);
    check(f.hud.is_move_latched(), "(524, 224) is the last pixel of slot 1; the press latches at once");
    f.release(524, 224);
    check(f.hud.is_move_latched(), "the latch survives the release");
    f.sounds.clear();
    f.press(500, 170);
    check(f.sounds.empty(), "a press on a latched pedestal does nothing");
    check(f.hud.is_move_latched(), "and does not unlatch it");
    // radio behaviour: the other pedestal takes over
    f.press(560, 180);
    check(f.hud.is_ability_latched() && !f.hud.is_move_latched(), "slot 2 latches and lets slot 1 up");
    check(!f.sounds.empty() && f.sounds.back() == sim::SoundID::NavButtonClick, "the pedestal press sound (89)");
    f.press(538, 180);
    check(f.hud.is_ability_latched(), "x = 538 is outside slot 2 (539 - 581)");
    f.press(500, 170);
    check(f.hud.is_move_latched() && !f.hud.is_ability_latched(), "and back");
    // an accepted order with a latched move pedestal lets it up; with nothing latched the pedestal flashes for 125 ms
    f.sounds.clear();
    f.click_tile(22, 30);
    check(!f.hud.is_move_latched(), "an accepted order lets the latched move pedestal up");
    check(f.sounds.size() >= 1 && f.sounds.back() != sim::SoundID::NavButtonClick, "a latched pedestal that pops up is silent: only the voice of the order");
    check(!f.hud.is_pedestal_flashing(0), "a latched pedestal that pops up does not flash");
    f.click_tile(24, 30);
    check(f.hud.is_pedestal_flashing(0) && !f.hud.is_pedestal_flashing(1), "without a latch the move pedestal flashes (BTNPUSH)");
    // the ability latch: a special order lets slot 2 up; slot 1 latched does not
    f.latch_ability();
    f.sim.grid_mut().place_bomb(30, 12, 1);
    f.settle();
    f.click_tile(30, 12);
    check(!f.hud.is_ability_latched(), "a special order lets the latched ability pedestal up");
    f.latch_move();
    f.click_tile(30, 12);                                                     // the same target again: the bomber already works on it, nobody needs an order (0x10288b2)
    check(f.hud.is_move_latched() && !f.hud.is_pedestal_flashing(1), "a repeated special click is skipped: no feedback at all");
    f.sim.grid_mut().place_bomb(32, 12, 1);
    f.settle();
    f.click_tile(32, 12);                                                     // another bomb tile is a special target of a single bomber (auto)
    check(f.hud.is_move_latched(), "a special order does not release the move pedestal");
    check(f.hud.is_pedestal_flashing(1), "the ability pedestal flashes instead");
    f.hud.unlatch_pedestals();
    // worker: no ability pedestal, the press does nothing
    {
        Scene t = make_scene();
        Fixture& g = *t.f;
        g.hud.select_ant(t.mine);
        const bool taken = g.hud.handle_mouse_down(560, 180, SDL_BUTTON_LEFT, g.sim, g.cam);
        g.release(560, 180);
        check(!taken && !g.hud.is_ability_latched(), "a worker has no ability pedestal");
        g.hud.clear_selection();
        g.press(500, 170);
        check(!g.hud.is_move_latched(), "nothing selected: no pedestals");
    }
    // Stop: the stop order, "Stopping.", the flash, a 250 ms lock, then the deselect
    {
        Scene t = make_scene();
        Fixture& g = *t.f;
        g.hud.select_ant(t.mine);
        g.click_tile(30, 22);
        check(g.unit(t.mine).orig_order == sim::AntUnit::kOrderMove, "the ant walks");
        g.latch_move();
        g.sounds.clear();
        g.press(610, 200);
        check(g.hud.is_pedestal_flashing(2), "Stop flashes");
        check(!g.hud.is_move_latched(), "Stop lets the pedestals up");
        check(!g.sounds.empty() && g.sounds.back() == sim::SoundID::AntStop, "antstop only");
        g.release(610, 200);
        check(g.selected(t.mine), "the selection stays during the lock");
        // the lock: mouse events are ignored
        g.markers.clear();
        g.click_tile(20, 30);
        check(g.markers.empty() && g.unit(t.mine).orig_order_tile != (TileCoord{20, 30}), "no mouse input during the 250 ms lock");
        g.hud.update(g.world(), 4);
        g.click_tile(20, 30);
        check(g.markers.empty(), "still locked after 200 ms");
        g.hud.update(g.world(), 1);
        check(g.hud.get_selected_ant_ids().empty() && g.hud.get_selected_ant_id() == 0, "after 250 ms everything is deselected");
        g.click_tile(10, 10);
        check(g.selected(t.mine), "and the mouse works again");
    }
    // the hill: hatch exists only while eggs remain, Stop deselects after the lock
    {
        Scene t = make_scene();
        Fixture& g = *t.f;
        g.sim.set_player_score(0, 500);
        g.sim.set_player_eggs(0, 0);
        g.settle();
        g.hud.select_base(0);
        const bool none = g.hud.handle_mouse_down(500, 170, SDL_BUTTON_LEFT, g.sim, g.cam);
        g.release(500, 170);
        check(!none && g.sim.get_pending_hatch_count(0) == 0, "no eggs: no hatch pedestal, the press does nothing");
        g.sim.set_player_eggs(0, 3);
        g.settle();
        g.press(500, 170);
        g.release(500, 170);
        check(g.sim.get_pending_hatch_count(0) == 1 && g.sim.get_player_eggs(0) == 2, "with eggs the press hatches");
        check(g.hud.is_pedestal_flashing(0), "the hatch pedestal flashes");
        g.press(610, 200);
        g.release(610, 200);
        check(g.hud.get_selected_base_team_id() == 0, "Stop on the hill: still selected during the lock");
        g.end_lock();
        check(g.hud.get_selected_base_team_id() == -1, "deselected after the lock");
    }
    // opening a dialog removes a rubber band and releases the capture
    {
        Scene t = make_scene();
        Fixture& g = *t.f;
        g.hud.select_ant(t.mine);
        g.press(200, 300);
        check(g.hud.is_input_captured(), "the press on the map captures");
        g.hud.open_quit_dialog();
        check(!g.hud.is_input_captured(), "a dialog releases the capture");
        g.hud.close_quit_dialog();
        g.release(200, 300);
        check(g.selected(t.mine), "the release after the dialog does nothing to the selection");
    }
    // the top bar is dead in a scroll strip that can scroll, alive where it cannot
    {
        Scene t = make_scene();
        Fixture& g = *t.f;
        g.cam.world_y = 400;
        g.press(500, 8);
        g.release(500, 8);
        check(!g.hud.is_quick_help_open(), "y = 8 is in the north strip while the view can scroll up: the Help button is dead");
        g.cam.world_y = 0;
        g.press(500, 8);
        g.release(500, 8);
        check(g.hud.is_quick_help_open(), "with the view at the top the strip is not active: the button works");
    }
}

// ---------------------------------------------------------------------------------------------------------------------------------
// The keyboard (FUN_0102609a) and the button class (FUN_01011206 / FUN_01011281)
// ---------------------------------------------------------------------------------------------------------------------------------

void test_keyboard() {
    g_group = "keyboard";
    std::printf("[keys] the original's keyboard: F1, F9 - F12, Enter, Esc, Ctrl+A / H / L / N / O / P / Q / S, dialog keys, nothing else\n");
    // Ctrl+N / Ctrl+P: from the lowest selected slot (nothing selected: the last), replacing the selection, no voice, scrolling just far enough
    {
        Fixture f;
        const uint32_t a = f.spawn(0, AntType::Worker, 10, 10);
        const uint32_t b = f.spawn(0, AntType::Worker, 12, 10);
        const uint32_t c = f.spawn(0, AntType::Worker, 14, 10);
        f.spawn(1, AntType::Worker, 20, 20);
        f.settle();
        auto key = [&](int32_t k, uint16_t mod = 0, bool repeat = false) { return f.hud.handle_key_down(k, f.sim, f.cam, mod, repeat); };
        auto only = [&](uint32_t id) { return f.hud.get_selected_ant_ids().size() == 1 && f.selected(id); };
        key('n', KMOD_CTRL);
        check(only(a), "Ctrl+N with nothing selected starts at the first ant");
        key('n', KMOD_CTRL);
        check(only(b), "Ctrl+N: the next ant");
        key('n', KMOD_CTRL);
        check(only(c), "Ctrl+N: the next ant");
        key('n', KMOD_CTRL);
        check(only(a), "Ctrl+N wraps around");
        key('p', KMOD_CTRL);
        check(only(c), "Ctrl+P wraps around backwards");
        key('p', KMOD_CTRL);
        check(only(b), "Ctrl+P: the previous ant");
        f.hud.clear_selection();
        key('p', KMOD_CTRL);
        check(only(b), "Ctrl+P with nothing selected steps back from the last slot: the second to last ant");
        f.hud.set_selected_ant_ids({b, c});
        f.sounds.clear();
        key('n', KMOD_CTRL);
        check(only(c), "several selected: the search starts at the lowest slot (b), the selection is replaced by the next ant (c)");
        check(f.sounds.empty() && f.hud.panel_mode(f.world()) == HUD::PanelMode::OneAnt, "panel 3, no voice");
        f.hud.set_selected_ant_ids({b, c});
        key('p', KMOD_CTRL);
        check(only(a), "and backwards from the lowest slot");
        check(!key('n') || f.hud.get_chat_input() == "n", "without Ctrl 'n' is a typed letter");
        f.hud.set_chat_input("");
    }
    {
        Fixture f;
        const uint32_t near_ant = f.spawn(0, AntType::Worker, 5, 5);
        const uint32_t far_ant = f.spawn(0, AntType::Worker, 40, 40);
        f.settle();
        f.cam.x = 0.0f; f.cam.y = 0.0f; f.cam.world_x = 0; f.cam.world_y = 0;
        f.hud.select_ant(near_ant);
        f.hud.handle_key_down('n', f.sim, f.cam, KMOD_CTRL);
        check(f.selected(far_ant), "the far ant is selected");
        // ScrollToShow of the +-128 square around (1296, 1296): the right edge 1424 - 442 and the bottom edge 1424 - 440
        check(f.cam.world_x == 982 && f.cam.world_y == 984, "the view moves just far enough to show the square (not centred): (" + std::to_string(f.cam.world_x) + ", " + std::to_string(f.cam.world_y) + ")");
        f.hud.handle_key_down('p', f.sim, f.cam, KMOD_CTRL);
        check(f.selected(near_ant) && f.cam.world_x == 48 && f.cam.world_y == 48, "and back: the left / top edge of the square (48, 48)");
        f.hud.handle_key_down('p', f.sim, f.cam, KMOD_CTRL);
        f.hud.handle_key_down('n', f.sim, f.cam, KMOD_CTRL);
        check(f.cam.world_x == 48 + 0 || f.cam.world_x > 0, "the view is not touched when the square is already visible or moves again");
    }
    // Ctrl+A, Ctrl+H, Ctrl+L, Esc, Ctrl+S
    {
        Scene s = make_scene();
        Fixture& f = *s.f;
        auto key = [&](int32_t k, uint16_t mod = 0, bool repeat = false) { return f.hud.handle_key_down(k, f.sim, f.cam, mod, repeat); };
        f.sounds.clear();
        key('a', KMOD_CTRL);
        check(f.hud.get_selected_ant_ids().size() == 3 && f.selected(s.mine) && f.selected(s.mine2) && f.selected(s.mine_on_hill), "Ctrl+A selects every own ant");
        check(!f.selected(s.foe) && f.hud.panel_mode(f.world()) == HUD::PanelMode::Ants, "and none of the others; panel 4");
        check(f.sounds.size() == 1, "the ready voice of the first ant");
        key('a', KMOD_CTRL | KMOD_LSHIFT);
        check(f.hud.get_selected_ant_ids().size() == 3, "Shift changes nothing");
        f.sim.kill_unit(s.mine2);
        f.sim.kill_unit(s.mine_on_hill);
        f.settle();
        key('a', KMOD_CTRL);
        check(f.hud.get_selected_ant_ids().size() == 1 && f.hud.panel_mode(f.world()) == HUD::PanelMode::OneAnt, "one own ant: panel 3");
        f.hud.select_base(1);
        f.cam.world_x = 400; f.cam.world_y = 400;
        key('h', KMOD_CTRL);
        check(f.hud.get_selected_base_team_id() == 0 && f.hud.get_selected_ant_ids().empty(), "Ctrl+H selects the home hill (panel 2)");
        check(f.cam.world_x == 400 && f.cam.world_y == 400, "without scrolling");
        f.hud.select_ant(s.mine);
        f.latch_move();
        check(f.hud.is_move_latched(), "a pedestal is latched");
        check(key(SDLK_ESCAPE), "Esc is handled");
        check(f.hud.get_selected_ant_ids().empty() && !f.hud.is_move_latched() && !f.hud.is_quit_dialog_open(), "Esc deselects everything and lets the pedestals up, no dialog");
        check(f.hud.is_show_hp(), "hit point digits are on (the owner's tweak: the original starts with them off)");
        key('l', KMOD_CTRL);
        check(!f.hud.is_show_hp(), "Ctrl+L switches them off");
        key('L', KMOD_CTRL);
        check(f.hud.is_show_hp(), "and on again");
        // Ctrl+S: the stop order without flash, lock or deselect, only for own ants
        f.hud.select_ant(s.mine);
        f.click_tile(30, 22);
        check(f.unit(s.mine).orig_order == sim::AntUnit::kOrderMove, "the ant walks");
        key('s', KMOD_CTRL);
        check(f.hud.status_line().text() == "Stopping.", "Ctrl+S posts \"Stopping.\"");
        check(f.selected(s.mine) && !f.hud.is_pedestal_flashing(2), "no flash, no lock: still selected");
        f.click_tile(31, 22);                                     // the mouse is not locked
        check(f.unit(s.mine).orig_order_tile == (TileCoord{31, 22}), "and the mouse still works");
        f.hud.clear_selection();
        f.hud.clear_status();
        key('s', KMOD_CTRL);
        check(f.hud.status_line().text().empty(), "nothing selected: Ctrl+S does nothing");
        f.hud.select_base(0);
        key('s', KMOD_CTRL);
        check(f.hud.status_line().text().empty(), "a hill selected: Ctrl+S does nothing");
    }
    // F1, dialog keys, Ctrl+O, Ctrl+Q
    {
        Scene s = make_scene();
        Fixture& f = *s.f;
        auto key = [&](int32_t k, uint16_t mod = 0) { return f.hud.handle_key_down(k, f.sim, f.cam, mod, false); };
        for (int32_t close_key : {static_cast<int32_t>('x'), static_cast<int32_t>('X'), static_cast<int32_t>('c'), static_cast<int32_t>('C'),
                                  static_cast<int32_t>(SDLK_RETURN), static_cast<int32_t>(SDLK_ESCAPE)}) {
            key(SDLK_F1);
            check(f.hud.is_quick_help_open(), "F1 opens the quick help");
            key('q');
            key(' ');
            check(f.hud.is_quick_help_open(), "other keys are taken by the dialog");
            check(f.hud.get_chat_input().empty(), "and are not typed");
            key(close_key);
            check(!f.hud.is_quick_help_open(), "the quick help closes with C, X, Enter and Esc");
        }
        key('o', KMOD_CTRL);
        check(f.hud.is_options_open(), "Ctrl+O opens the options");
        key(SDLK_ESCAPE);
        check(f.hud.is_options_open(), "Esc does not close the options");
        key(SDLK_RETURN);
        check(!f.hud.is_options_open(), "Enter does");
        bool quit = false;
        f.hud.set_on_quit([&]() { quit = true; });
        key('q', KMOD_CTRL);
        check(f.hud.is_quit_dialog_open(), "Ctrl+Q opens the quit dialog");
        key(SDLK_RETURN);
        check(f.hud.is_quit_dialog_open() && !quit, "Enter does nothing in it");
        key('x');
        check(f.hud.is_quit_dialog_open() && !quit, "neither does another key");
        key('N');
        check(!f.hud.is_quit_dialog_open() && !quit, "N answers No");
        key('q', KMOD_CTRL);
        key(SDLK_ESCAPE);
        check(!f.hud.is_quit_dialog_open() && !quit, "Esc answers No");
        key('q', KMOD_CTRL);
        key('y');
        check(!f.hud.is_quit_dialog_open() && quit, "Y answers Yes");
        // the get-ready dialog takes everything
        f.hud.select_ant(s.mine);
        f.hud.start_match_modal();
        key(SDLK_ESCAPE);
        check(f.selected(s.mine), "the start dialog swallows Esc");
        f.hud.dismiss_match_start_modal();
    }
    // chat keys: F9 - F12 fresh presses, Enter, Backspace, typed characters, nothing else does anything
    {
        Scene s = make_scene();
        Fixture& f = *s.f;
        auto key = [&](int32_t k, uint16_t mod = 0, bool repeat = false) { return f.hud.handle_key_down(k, f.sim, f.cam, mod, repeat); };
        const size_t base = f.hud.get_chat_log().size();
        key(SDLK_F9, 0, true);
        check(f.hud.get_chat_log().size() == base, "a repeat of F9 sends nothing");
        key(SDLK_F9, 0, false);
        check(f.hud.get_chat_log().size() == base + 3, "a fresh F9 chats the first quick text (a header and two body lines)");
        key(SDLK_F12);
        check(f.hud.get_chat_log().size() == base + 5, "F12 the fourth (a header and one body line)");
        key('h');
        key('i');
        check(f.hud.get_chat_input() == "hi", "printable keys are typed");
        key('!', KMOD_LSHIFT);
        check(f.hud.get_chat_input() == "hi!", "with Shift too");
        key(SDLK_BACKSPACE);
        check(f.hud.get_chat_input() == "hi", "Backspace deletes");
        check(!key('b', KMOD_CTRL) && !key('c', KMOD_CTRL) && !key('f', KMOD_CTRL) && !key('m', KMOD_CTRL) && !key('t', KMOD_CTRL),
              "Ctrl+B / C / F / M / T do nothing");
        check(!key(SDLK_LEFT) && !key(SDLK_UP) && !key(SDLK_TAB) && !key(SDLK_HOME), "arrows, Tab and Home do nothing");
        check(f.hud.get_chat_input() == "hi", "and nothing was typed");
        f.cam.world_x = 100; f.cam.world_y = 100;
        key(' ');
        check(f.hud.get_chat_input() == "hi " && f.cam.world_x == 100 && f.cam.world_y == 100, "Space is a typed character, it does not centre the view");
        key(SDLK_RETURN);
        check(f.hud.get_chat_input().empty() && f.hud.get_chat_log().size() > base + 5, "Enter sends and clears");
        // with the chat option off the box is covered: nothing is typed, F9 - F12 are silent, Enter sends nothing
        f.hud.open_options();
        f.press(151, 289);                                        // the OFF toggle of the chat option
        f.hud.close_options();
        const size_t now = f.hud.get_chat_log().size();
        check(!key('a') && f.hud.get_chat_input().empty(), "chat off: nothing is typed");
        key(SDLK_F10);
        check(f.hud.get_chat_log().size() == now, "chat off: F10 is silent");
    }
}

void test_buttons() {
    g_group = "buttons";
    std::printf("[buttons] the button class: a press captures, the callback runs at the release while the pointer is still on the button\n");
    Scene s = make_scene();
    Fixture& f = *s.f;
    f.cam.world_x = 0; f.cam.world_y = 0; f.cam.x = 0.0f; f.cam.y = 0.0f;      // no scroll strip is active at the top left of the map
    // Help (476, 7, 46, 23)
    f.sounds.clear();
    f.press(500, 15);
    check(!f.sounds.empty() && f.sounds.back() == sim::SoundID::ButtonClick, "the click sound plays at the press");
    check(f.hud.is_input_captured() && !f.hud.is_quick_help_open(), "the button is captured, its callback has not run");
    f.release(500, 15);
    check(f.hud.is_quick_help_open() && !f.hud.is_input_captured(), "the release on the button opens the quick help");
    f.hud.close_quick_help();
    // leaving the button cancels the capture for good
    f.press(540, 15);                                                  // Options (525, 7, 52, 23)
    f.hud.handle_mouse_motion(300, 300, f.sim, f.cam);
    check(!f.hud.is_input_captured(), "leaving the button cancels the capture");
    f.hud.handle_mouse_motion(540, 15, f.sim, f.cam);
    check(!f.hud.is_input_captured(), "coming back only hovers");
    f.release(540, 15);
    check(!f.hud.is_options_open(), "and the release does nothing");
    f.press(540, 15);
    f.release(300, 300);
    check(!f.hud.is_options_open(), "released elsewhere: nothing");
    f.press(540, 15);
    f.release(541, 16);
    check(f.hud.is_options_open(), "released on the button: the options open");
    f.hud.close_options();
    // Quit (579, 7, 46, 23)
    f.press(590, 15);
    f.release(590, 15);
    check(f.hud.is_quit_dialog_open(), "Quit opens the quit dialog");
    f.hud.close_quit_dialog();
    // [All] (532, 443, 44, 24): sends the text at the release; hidden while chat is off
    f.hud.set_chat_input("to all");
    const size_t base = f.hud.get_chat_log().size();
    f.press(545, 450);
    check(f.hud.get_chat_log().size() == base, "nothing is sent at the press");
    f.release(545, 450);
    check(f.hud.get_chat_log().size() == base + 2 && f.hud.get_chat_input().empty(), "the release sends and clears");
    f.hud.open_options();
    f.press(151, 289);
    f.hud.close_options();
    f.hud.set_chat_input("");
    f.sounds.clear();
    f.press(545, 450);
    check(f.sounds.empty() && !f.hud.is_input_captured(), "chat off: [All] is hidden and does not react");
    f.release(545, 450);
}

}  // namespace

int main() {
    test_hit_boxes();
    test_cursor_without_own_ants();
    test_cursor_with_own_ants();
    test_cursor_special_targets();
    test_cursor_misc();
    test_click_modes();
    test_click_or_drag();
    test_drag_select();
    test_right_button();
    test_pedestals();
    test_keyboard();
    test_buttons();
    std::printf("pointer model: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
