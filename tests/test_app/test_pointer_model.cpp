// Pointer model tests (stage I): the ant hit boxes, the cursor decision table, the click by cursor mode, the rubber band and the release of the
// left button, the right button, the command pedestals, the keyboard table and the button class as Ants.exe does them (FUN_01026904 / FUN_01026a39 / FUN_01026aa3 / FUN_01026f91 /
// FUN_01027530 / FUN_010277f4 / FUN_01027b51 / FUN_010287b5 / FUN_010274be / FUN_01028d30). docs/GAME_REVERSE_ENGINEERING.md 5.44
// Also the pointer beyond the picture of a wide window (pointer_clamp.hpp, group pillarbox; docs 5.43, "The pointer and the screen"), the ant type getter's flag 1 that the cursor and the
// right click ask (a bomber, fire ant or swimmer that is busy is a worker there: group "busy ants", docs 5.62) and the HUD on levels with a default ant type (group "default type").
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <cstring>
#include <utility>
#include <vector>

#include "ants_app/edge_scroll.hpp"
#include "ants_app/hud.hpp"
#include "ants_app/pointer_clamp.hpp"
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

    // several ants: the auto flag needs exactly one ant (panel 3), and the ability pedestal exists in panel 3 only (SetPanelMode gives slot 2 the kind 9,
    // hidden, in panel 4), so a group never has a special target, same type or not
    {
        const uint32_t b1 = f.spawn(0, AntType::Bomber, 8, 30);
        const uint32_t b2 = f.spawn(0, AntType::Bomber, 9, 30);
        const uint32_t wk = f.spawn(0, AntType::Worker, 10, 30);
        f.settle();
        f.hud.set_selected_ant_ids({b1, b2});
        expect_cursor("two bombers / bomb / auto (panel 4)", f.cursor_tile(bomb.x, bomb.y), CursorType::Move);
        f.latch_ability();
        check(!f.hud.is_ability_latched(), "two bombers: there is no ability pedestal to press");
        expect_cursor("two bombers / bomb / pressed where the pedestal is not", f.cursor_tile(bomb.x, bomb.y), CursorType::Move);
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
    // while a dialog is open the cursor code does not run, and every dialog set mode 1 (the normal arrow) when it was attached (thunk 0x101279a, v0.0.80): the pointer over
    // a dialog is the arrow whatever it was before, a map cursor or a scroll arrow; after the close the next input run decides again
    expect_cursor("primer: on the east strip the scroll arrow", f.cursor_px(639, 240), CursorType::ScrollE);
    auto over_dialog = [&](const char* name, const std::function<void()>& open, const std::function<void()>& close) {
        auto said = [&](const char* what, CursorType got, CursorType want) { expect_cursor((std::string(name) + ": " + what).c_str(), got, want); };
        said("primer (the cursor that the dialog finds when it opens)", f.cursor_px(639, 240), CursorType::ScrollE);
        open();
        said("the normal arrow over it (the pointer was on a scroll strip)", f.cursor_px(639, 240), CursorType::Normal);
        said("the normal arrow over it (anywhere)", f.cursor_px(200, 200), CursorType::Normal);
        close();
        said("after the close the strip decides again", f.cursor_px(639, 240), CursorType::ScrollE);
    };
    over_dialog("options", [&]() { f.hud.open_options(); }, [&]() { f.hud.close_options(); });
    over_dialog("quit dialog", [&]() { f.hud.open_quit_dialog(); }, [&]() { f.hud.close_quit_dialog(); });
    over_dialog("quick help", [&]() { f.hud.open_quick_help(); }, [&]() { f.hud.close_quick_help(); });
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
    // screen x = 16 + (wx - 400) (the view's origin is (16, 21)): the tile border 512 (tiles 15 | 16) is at x = 128; the row of y = 300 is tile 21
    f.markers.clear();
    f.press(127, 300);
    f.hud.handle_mouse_motion(131, 304, f.sim, f.cam);
    f.release(131, 304);
    check(f.unit(s.mine).orig_order == sim::AntUnit::kOrderMove && f.unit(s.mine).orig_order_tile == (TileCoord{16, 21}),
          "4 px: a click, executed at the release point (tile 16, 21 - the press was in tile 15)");
    check(f.markers.size() == 1 && f.markers[0].first == 400 + (131 - 16) && f.markers[0].second == 400 + (304 - 21), "the marker is at the release pixel");

    // the view is 442 x 440 at the screen pixel (16, 21) (the view window's rectangle, Ants.exe 0x100a32b): its first pixel is the camera's own world pixel, its last
    // (457, 460) is 441 / 439 further (the remake used to put it one pixel right and down)
    f.hud.select_ant(s.mine);
    f.markers.clear();
    f.click(16, 21);
    check(f.markers.size() == 1 && f.markers[0].first == 400 && f.markers[0].second == 400, "the screen pixel (16, 21) is the world pixel of the camera: (400, 400)");
    f.markers.clear();
    f.click(457, 460);
    check(f.markers.size() == 1 && f.markers[0].first == 400 + 441 && f.markers[0].second == 400 + 439, "the last pixel of the view (457, 460) is 441 / 439 further");

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
        check(g.unit(t.mine).orig_order_tile == (TileCoord{(400 + 260 - 16) / 32, (400 + 330 - 21) / 32}), "at the release point");
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
// The stored panel: a shift add or a shift drag is panel 4 for any count (FUN_01027f07 gets the mode, never a count)
// ---------------------------------------------------------------------------------------------------------------------------------

void test_shift_group() {
    g_group = "shift group";
    std::printf("[pointer] the stored panel: a shift add or shift drag is panel 4 even for one ant, and the click on an own bomb is then a group move (ISLANDS)\n");
    const TileCoord bomb{20, 20};
    auto scene = [&](Fixture& f, uint32_t& bomber, uint32_t& worker) {
        f.sim.grid_mut().place_bomb(static_cast<uint32_t>(bomb.x), static_cast<uint32_t>(bomb.y), 0);
        bomber = f.spawn(0, AntType::Bomber, 16, 20);           // (528, 656): box [508, 548) x [624, 672)
        worker = f.spawn(0, AntType::Worker, 16, 23);            // (528, 752)
        f.settle();
    };
    auto drag = [&](Fixture& f, int32_t wx1, int32_t wy1, int32_t wx2, int32_t wy2, uint16_t mod) {
        f.look_at(18, 21);
        f.press(f.sx(wx1), f.sy(wy1), SDL_BUTTON_LEFT, mod);
        f.hud.handle_mouse_motion(f.sx(wx2), f.sy(wy2), f.sim, f.cam);
        f.release(f.sx(wx2), f.sy(wy2), SDL_BUTTON_LEFT, mod);
    };
    auto status = [&](Fixture& f) {
        f.hud.update(f.world(), 1);
        return f.hud.status_line().text();
    };

    {   // the control: one bomber selected by a plain click or a plain drag is panel 3, and the bomb is its target
        Fixture f;
        uint32_t bomber = 0, worker = 0;
        scene(f, bomber, worker);
        f.click_tile(16, 20);
        check(f.selected(bomber) && f.hud.panel_mode(f.world()) == HUD::PanelMode::OneAnt, "a click selects the bomber: panel 3");
        check(status(f) == "BomberAnt selected.", "and names its type");
        expect_cursor("panel 3 / own bomb", f.cursor_tile(bomb.x, bomb.y), CursorType::Target);
        f.click_tile(bomb.x, bomb.y);
        check(f.unit(bomber).orig_order == sim::AntUnit::kOrderDefuse, "the click is the defuse");
        f.hud.clear_selection();
        drag(f, 500, 620, 560, 680, 0);
        check(f.selected(bomber) && f.hud.panel_mode(f.world()) == HUD::PanelMode::OneAnt, "a plain drag around it: panel 3");
        f.hud.clear_selection();
        drag(f, 500, 620, 560, 680, KMOD_LSHIFT);
        check(f.selected(bomber) && f.hud.panel_mode(f.world()) == HUD::PanelMode::OneAnt, "a shift drag in panel 1 is a plain drag: panel 3");
    }
    {   // shift drag around the lone selected bomber: one ant, panel 4, a group move onto the bomb
        Fixture f;
        uint32_t bomber = 0, worker = 0;
        scene(f, bomber, worker);
        f.click_tile(16, 20);
        drag(f, 500, 620, 560, 680, KMOD_LSHIFT);
        check(f.hud.get_selected_ant_ids().size() == 1 && f.selected(bomber), "still the one bomber");
        check(f.hud.panel_mode(f.world()) == HUD::PanelMode::Ants, "but the panel is 4: a shift drag adds, whatever the count");
        check(status(f) == "Ready!", "and says string 12 (\"Ready!\") like any group, not \"BomberAnt selected.\": " + status(f));
        expect_cursor("panel 4 / own bomb: a move", f.cursor_tile(bomb.x, bomb.y), CursorType::Move);
        f.latch_ability();
        check(!f.hud.is_ability_latched(), "there is no ability pedestal in panel 4");
        expect_cursor("panel 4 / own bomb / pedestal pressed", f.cursor_tile(bomb.x, bomb.y), CursorType::Move);
        f.click_tile(bomb.x, bomb.y);
        check(f.unit(bomber).orig_order == sim::AntUnit::kOrderBomb, "the click is a move onto the bomb, not the defuse");
        check(f.unit(bomber).final_dest == bomb, "and the goal stays on the bomb tile");
        for (int i = 0; i < 400 && f.sim.has_bomb_at(bomb); ++i) f.sim.tick();
        check(!f.sim.has_bomb_at(bomb) && f.sim.get_player_stats(0).bombs_defused == 0, "the bomber walked into its own bomb and set it off");
        // the next plain selection is panel 3 again
        f.click_tile(f.unit(bomber).pos.x, f.unit(bomber).pos.y);
    }
    {   // the right button follows the panel as well
        Fixture f;
        uint32_t bomber = 0, worker = 0;
        scene(f, bomber, worker);
        f.click_tile(16, 20);
        drag(f, 500, 620, 560, 680, KMOD_LSHIFT);
        f.click_tile(bomb.x, bomb.y, SDL_BUTTON_RIGHT);
        check(f.unit(bomber).orig_order == sim::AntUnit::kOrderBomb, "right button in panel 4: a move onto the bomb");
    }
    {   // shift click: joining is panel 4, leaving recounts (one left: panel 3, the automatic target cursor is back)
        Fixture f;
        uint32_t bomber = 0, worker = 0;
        scene(f, bomber, worker);
        f.click_tile(16, 20);
        f.click_tile(16, 23, SDL_BUTTON_LEFT, KMOD_LSHIFT);
        check(f.selected(bomber) && f.selected(worker) && f.hud.panel_mode(f.world()) == HUD::PanelMode::Ants, "shift click adds the worker: panel 4");
        expect_cursor("a bomber and a worker / own bomb", f.cursor_tile(bomb.x, bomb.y), CursorType::Move);
        f.click_tile(16, 23, SDL_BUTTON_LEFT, KMOD_LSHIFT);
        check(f.selected(bomber) && !f.selected(worker) && f.hud.panel_mode(f.world()) == HUD::PanelMode::OneAnt, "shift click on it again removes it: one ant left, panel 3");
        expect_cursor("the bomber alone again", f.cursor_tile(bomb.x, bomb.y), CursorType::Target);
        // the bomber leaves from panel 3: nothing is left, panel 1
        f.click_tile(16, 20, SDL_BUTTON_LEFT, KMOD_LSHIFT);
        check(f.hud.get_selected_ant_ids().empty() && f.hud.panel_mode(f.world()) == HUD::PanelMode::None, "the last ant leaves: panel 1");
    }
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
// The ant type getter with flag 1 (a busy bomber, fire ant or swimmer), and the HUD on levels with a default ant type
// ---------------------------------------------------------------------------------------------------------------------------------

// What the HUD sends, recorded and applied
class RecordingSink final : public sim::CommandSink {
public:
    explicit RecordingSink(sim::SimulationEngine& sim) : sim_(sim) {}
    sim::CommandResult submit(const sim::Command& c) override {
        log.push_back(c);
        return sim_.apply_command(c);
    }
    std::vector<sim::Command> log;

private:
    sim::SimulationEngine& sim_;
};

const sim::AntSnapshot* snapshot_of(Fixture& f, uint32_t id) {
    for (const auto& a : f.world().ants) {
        if (a.id == id) return &a;
    }
    return nullptr;
}

// FUN_0100f9cb with flag 1: the action (+0xe4) of an ant that takes an order is 0, 1 or 3
bool takes_orders_now(Fixture& f, uint32_t id) {
    const sim::AntSnapshot* a = snapshot_of(f, id);
    return a != nullptr && (a->action == 0 || a->action == 1 || a->action == 3);
}

void test_busy_ability_ants() {
    g_group = "busy ants";
    std::printf("[pointer] the cursor and the right click ask the ant type with flag 1 (FUN_010282e0(1) at 0x1026f9d, 0x1027da6, 0x1027db3): a bomber, fire ant or swimmer that is busy is a worker there, and nothing else is\n");
    // a bomber: planting a bomb is its busy action (8); walking (1) is not busy
    {
        Scene s = make_scene();
        Fixture& f = *s.f;
        f.sim.grid_mut().place_bomb(20, 20, 1);                                // an enemy bomb
        const uint32_t bomber = f.spawn(0, AntType::Bomber, 8, 30);
        f.settle();
        f.hud.select_ant(bomber);
        RecordingSink sink(f.sim);
        f.hud.set_command_sink(&sink);
        expect_cursor("a bomber at rest: a bomb is a special target", f.cursor_tile(20, 20), CursorType::Target);
        sim::Command walk;
        walk.type = sim::CommandType::GroupMove;
        walk.issuer = 0;
        walk.tile_x = 8;
        walk.tile_y = 40;
        walk.ants = {bomber};
        check(f.sim.apply_command(walk).accepted(), "the bomber is sent for a walk");
        for (int t = 0; t < 10; ++t) f.settle();
        check(snapshot_of(f, bomber)->action == 1, "it walks (action 1)");
        expect_cursor("a bomber on its way is still a bomber for the cursor: a bomb is a special target", f.cursor_tile(20, 20), CursorType::Target);
        f.sim.stop_ant(bomber);
        for (int t = 0; t < 100 && snapshot_of(f, bomber)->action != 0; ++t) f.settle();
        check(f.sim.plant_bomb(bomber, TileCoord{snapshot_of(f, bomber)->tile_x + 1, snapshot_of(f, bomber)->tile_y}, false), "the bomber starts to plant a bomb");
        f.settle();
        check(!takes_orders_now(f, bomber) && snapshot_of(f, bomber)->action == 8, "and is busy with the planting clip (action 8)");
        expect_cursor("a bomber that is planting: flag 1 answers worker, so the bomb is no special target", f.cursor_tile(20, 20), CursorType::Move);
        f.latch_ability();
        check(f.hud.is_ability_latched(), "the ability pedestal is still the bomber's (the panel asks with flag 0)");
        expect_cursor("busy and latched: plantable ground is no special target either", f.cursor_tile(18, 20), CursorType::Move);
        f.hud.unlatch_pedestals();
        sink.log.clear();
        f.click_tile(20, 20, SDL_BUTTON_RIGHT);
        check(sink.log.size() == 1 && sink.log[0].type == sim::CommandType::GroupMove, "a right click on the bomb sends the plain move for a busy bomber");
        for (int t = 0; t < 200 && !takes_orders_now(f, bomber); ++t) f.settle();
        check(takes_orders_now(f, bomber), "the planting clip ends");
        expect_cursor("at rest again: the bomb is a special target", f.cursor_tile(20, 20), CursorType::Target);
        sink.log.clear();
        f.click_tile(20, 20, SDL_BUTTON_RIGHT);
        check(sink.log.size() == 1 && sink.log[0].type == sim::CommandType::GroupSpecial, "and the right click sends the special order");
    }
    // a bomber that a dud bomb has frozen under the burn overlay (action 0xA) is busy; when the overlay ends it is stunned (action 3) and takes orders, so it is a bomber again
    {
        bool tested = false;
        for (uint32_t seed = 1; seed < 150 && !tested; ++seed) {
            Fixture f(seed);
            f.sim.grid_mut().set_anthill(0, TileCoord{2, 2});
            f.sim.grid_mut().set_anthill(1, TileCoord{40, 40});
            f.sim.grid_mut().place_bomb(20, 20, 1);                            // an enemy bomb to point at
            const uint32_t bomber = f.spawn(0, AntType::Bomber, 15, 15);
            f.sim.grid_mut().place_bomb(15, 15, 1);
            f.sim.trigger_bomb_detonation(bomber, TileCoord{15, 15});
            if (!f.sim.get_unit(bomber).knock_flag) continue;                 // a dud: the ant burns under the overlay and is then stunned (the other seeds are real blasts)
            f.settle();
            f.hud.select_ant(bomber);
            bool saw_burn = false;
            bool saw_stun = false;
            for (int t = 0; t < 300 && !saw_stun; ++t) {
                const sim::AntSnapshot* a = snapshot_of(f, bomber);
                if (a == nullptr || a->hp == 0) break;
                if (a->action == 0x0A) {
                    saw_burn = true;
                    expect_cursor("a bomber under the burn overlay (action 0xA) is busy: no special target", f.cursor_tile(20, 20), CursorType::Move);
                }
                if (a->action == 3) {
                    saw_stun = true;
                    expect_cursor("a stunned bomber (action 3) takes orders: the bomb is a special target", f.cursor_tile(20, 20), CursorType::Target);
                }
                f.settle();
            }
            tested = saw_burn && saw_stun;
        }
        check(tested, "a dud seed exists in which the bomber burns and is then stunned");
    }
    // a fire ant (igniting a wall is action 6), the pedestal latched: plantable ground
    {
        Scene s = make_scene();
        Fixture& f = *s.f;
        const uint32_t fire = f.spawn(0, AntType::Fire, 8, 30);
        f.settle();
        f.hud.select_ant(fire);
        f.latch_ability();
        expect_cursor("a fire ant at rest, the pedestal latched: plantable ground is a special target", f.cursor_tile(18, 20), CursorType::Target);
        check(f.sim.ignite_fire(fire, TileCoord{9, 30}, false), "the fire ant starts to light a wall");
        f.settle();
        check(!takes_orders_now(f, fire), "and is busy");
        expect_cursor("a fire ant that is busy: no special target (flag 1 answers worker)", f.cursor_tile(18, 20), CursorType::Move);
        check(f.hud.is_ability_latched(), "the pedestal stays latched");
    }
    // a swimmer (building a bridge is action 0x10), the pedestal latched: water
    {
        Scene s = make_scene();
        Fixture& f = *s.f;
        f.sim.set_terrain(24, 20, 2);
        f.sim.set_terrain(30, 20, 2);
        const uint32_t swimmer = f.spawn(0, AntType::Swimmer, 23, 20);
        f.settle();
        f.hud.select_ant(swimmer);
        f.latch_ability();
        expect_cursor("a swimmer at rest, the pedestal latched: water is a special target", f.cursor_tile(30, 20), CursorType::Target);
        check(f.sim.build_bridge_step(swimmer, TileCoord{24, 20}), "the swimmer starts to build a bridge");
        f.settle();
        check(!takes_orders_now(f, swimmer), "and is busy");
        expect_cursor("a swimmer that is busy: no special target", f.cursor_tile(30, 20), CursorType::Move);
        check(f.hud.is_ability_latched(), "the pedestal stays latched");
    }
    // a thief that is busy is still a thief: only the own types 1, 2 and 5 have the branch
    {
        Scene s = make_scene();
        Fixture& f = *s.f;
        const uint32_t thief = f.spawn(0, AntType::Thief, 8, 30);
        const uint32_t foe = f.spawn(1, AntType::Worker, 9, 30);
        f.settle();
        f.hud.select_ant(thief);
        expect_cursor("a thief at rest: an enemy hill is a special target", f.cursor_tile(42, 42), CursorType::Target);
        f.sim.execute_melee_attack(thief, foe);
        f.settle();
        check(!takes_orders_now(f, thief), "the thief is busy with its attack clip");
        expect_cursor("a busy thief is still a thief for the cursor", f.cursor_tile(42, 42), CursorType::Target);
    }
    // the busy rule reads the OWN type field: a worker of a level whose default is Bomber, Fire or Swimmer has the own type 0, so the getter takes its flag-0 path for it and it is that
    // type busy or not (0x100f9d5 - 0x100f9e9 jump to 0x100fa2c for an own type that is not 1, 2 or 5), while the ant of the own type 1, 2 or 5 beside it becomes a worker
    for (int k = 0; k < 3; ++k) {
        const uint16_t tile = k == 0 ? 64 : k == 1 ? 66 : 65;                          // Bomber, Fire, Swimmer
        const AntType type = k == 0 ? AntType::Bomber : k == 1 ? AntType::Fire : AntType::Swimmer;
        Scene s = make_scene();
        Fixture& f = *s.f;
        f.sim.grid_mut().set_default_ant_tile(tile);
        f.sim.set_terrain(12, 12, 2);                                                  // water for the bridge of the swimmer (and for the one of the swimmer beside it)
        f.sim.set_terrain(12, 14, 2);
        const uint32_t worker = f.spawn(0, AntType::Worker, 11, 12);                   // the default type, own type 0
        const uint32_t typed = f.spawn(0, type, 11, 14);                                // own type 1, 2 or 5
        f.settle();
        const TileCoord target = k == 2 ? TileCoord{12, 12} : TileCoord{11, 13};
        check((k == 0 ? f.sim.plant_bomb(worker, target, false) : k == 1 ? f.sim.ignite_fire(worker, target, false) : f.sim.build_bridge_step(worker, target)),
              "the worker of the default type starts its ability");
        check((k == 0 ? f.sim.plant_bomb(typed, TileCoord{11, 15}, false) : k == 1 ? f.sim.ignite_fire(typed, TileCoord{11, 15}, false) : f.sim.build_bridge_step(typed, TileCoord{12, 14})),
              "and so does the ant of the own type");
        f.settle();
        check(!takes_orders_now(f, worker) && !takes_orders_now(f, typed), "both are busy");
        sim::AntType common = AntType::Combat;
        f.hud.set_selected_ant_ids({worker});
        check(f.hud.homogeneous_type(f.world(), common, true) && common == type, "a busy worker of the default type is still that type for the cursor and the right click (own type 0: flag 0 path)");
        f.hud.set_selected_ant_ids({typed});
        check(f.hud.homogeneous_type(f.world(), common, true) && common == AntType::Worker, "a busy ant of the own type is a worker there");
        f.hud.set_selected_ant_ids({worker, typed});
        check(!f.hud.homogeneous_type(f.world(), common, true), "and the two have no common type under flag 1 (they have one under flag 0)");
        check(f.hud.homogeneous_type(f.world(), common) && common == type, "flag 0: both are of the type");
    }
    // a group: the types of the selection must agree under flag 1 too: a busy bomber and a bomber at rest are no common type
    {
        Scene s = make_scene();
        Fixture& f = *s.f;
        const uint32_t busy = f.spawn(0, AntType::Bomber, 8, 30);
        const uint32_t rest = f.spawn(0, AntType::Bomber, 8, 32);
        f.settle();
        check(f.sim.plant_bomb(busy, TileCoord{9, 30}, false), "one bomber plants");
        f.settle();
        f.hud.set_selected_ant_ids({busy, rest});
        sim::AntType type = AntType::Combat;
        check(f.hud.homogeneous_type(f.world(), type) && type == AntType::Bomber, "with flag 0 both are bombers");
        check(!f.hud.homogeneous_type(f.world(), type, true), "with flag 1 a busy bomber (a worker) and a bomber at rest have no common type");
        f.hud.set_selected_ant_ids({busy});
        check(f.hud.homogeneous_type(f.world(), type, true) && type == AntType::Worker, "a busy bomber alone is a worker with flag 1");
        check(f.hud.homogeneous_type(f.world(), type) && type == AntType::Bomber, "and a bomber with flag 0");
    }
}

void test_default_type_hud() {
    g_group = "default type";
    std::printf("[pointer] on a level with a default ant type the HUD reads what the ant IS: the ready voice (three ways to select), the ability pedestal, the special target and the right click, the rebuilt panel after a pick-up\n");
    const uint16_t tiles[5] = {62, 63, 64, 65, 66};                            // Combat, Thief, Bomber, Swimmer, Fire
    const AntType types[5] = {AntType::Combat, AntType::Thief, AntType::Bomber, AntType::Swimmer, AntType::Fire};
    const auto in_voices = [](AntType type, uint32_t sound) {
        for (uint32_t variant = 0; variant < 6; ++variant) {
            if (sim::get_ready_voice_sound(type, variant) == sound) return true;
        }
        return false;
    };
    for (int k = 0; k < 5; ++k) {
        const std::string what = std::string("default ") + (k == 0 ? "combat" : k == 1 ? "thief" : k == 2 ? "bomber" : k == 3 ? "swimmer" : "fire") + " level: ";
        // the ready voice of the ant that was selected (FUN_0101b5f9 asks the getter): by a click, by a rubber band, by Ctrl+A
        for (int how = 0; how < 3; ++how) {
            Scene s = make_scene();
            Fixture& f = *s.f;
            f.sim.grid_mut().set_default_ant_tile(tiles[k]);
            f.settle();
            f.sounds.clear();
            if (how == 0) f.click_tile(10, 10);
            else if (how == 1) f.hud.select_ants_in_rect(326, 326, 346, 346, f.world(), false);
            else f.hud.handle_key_down('a', f.sim, f.cam, KMOD_CTRL, false);
            check(f.hud.is_ant_selected(s.mine), what + "the worker is selected");
            check(f.sounds.size() == 1, what + "one voice answers");
            check(!f.sounds.empty() && in_voices(types[k], f.sounds[0]), what + "the ready voice is that of the type the ant is (" + (how == 0 ? "click" : how == 1 ? "rubber band" : "Ctrl+A") + ")");
            check(!f.sounds.empty() && !in_voices(AntType::Worker, f.sounds[0]), what + "and not the worker's");
        }
        // the ability pedestal and the special target of the type: the HUD's common type is the ant's type, not its own type field
        {
            Scene s = make_scene();
            Fixture& f = *s.f;
            f.sim.grid_mut().set_default_ant_tile(tiles[k]);
            f.sim.grid_mut().place_bomb(20, 20, 1);
            f.sim.set_fire_at({22, 20}, 3600);
            f.sim.set_terrain(24, 20, 2);
            f.settle();
            RecordingSink sink(f.sim);
            f.hud.set_command_sink(&sink);
            f.hud.select_ant(s.mine);
            sim::AntType type = AntType::Worker;
            check(f.hud.homogeneous_type(f.world(), type) && type == types[k], what + "the selection's type");
            check(f.hud.homogeneous_type(f.world(), type, true) && type == types[k], what + "the same for the orders (the own type is 0: the flag 0 path)");
            f.latch_ability();
            check(f.hud.is_ability_latched(), what + "the ability pedestal exists and latches");
            f.hud.unlatch_pedestals();
            const TileCoord target = k == 2 ? TileCoord{20, 20} : k == 4 ? TileCoord{22, 20} : k == 3 ? TileCoord{24, 20} : k == 1 ? TileCoord{42, 42} : TileCoord{18, 20};
            const bool has_target = k != 0;
            if (k == 2 || k == 1) {                                          // the bomb (bomber) and the enemy hill (thief) are special targets without the pedestal
                expect_cursor((what + "the special target without the pedestal").c_str(), f.cursor_tile(target.x, target.y), CursorType::Target);
            } else if (k != 0) {
                f.latch_ability();
                expect_cursor((what + "the special target with the pedestal latched").c_str(), f.cursor_tile(target.x, target.y), CursorType::Target);
                f.hud.unlatch_pedestals();
            } else {
                f.latch_ability();
                expect_cursor((what + "no special target for a combat ant").c_str(), f.cursor_tile(target.x, target.y), CursorType::Move);
                f.hud.unlatch_pedestals();
            }
            if (has_target && (k == 1 || k == 2)) {                          // the right click: the special order of the type, not the move of a worker
                sink.log.clear();
                f.click_tile(target.x, target.y, SDL_BUTTON_RIGHT);
                check(sink.log.size() == 1 && sink.log[0].type == sim::CommandType::GroupSpecial, what + "the right click sends the special order");
            }
        }
    }
    // a level without a default: a worker has no ability pedestal (the control of the above)
    {
        Scene s = make_scene();
        Fixture& f = *s.f;
        f.hud.select_ant(s.mine);
        f.latch_ability();
        check(!f.hud.is_ability_latched(), "a plain worker has no ability pedestal");
    }
    // a worker of a default Combat level that takes the Combat power-up is a combat ant before and after, but the pick-up rebuilds its panel (FUN_0100cd40 is the last thing the
    // pick-up does): the text of its type is posted again; a Thief power-up changes the type and posts the new text
    for (int own = 0; own < 2; ++own) {
        Scene s = make_scene();
        Fixture& f = *s.f;
        f.sim.grid_mut().set_default_ant_tile(62);
        f.sim.grid_mut().place_powerup(12, 12, own == 0 ? 4 : 3);                        // a Combat power-up, or a Thief power-up
        f.settle();
        f.hud.select_ant(s.mine);
        for (int t = 0; t < 101; ++t) f.hud.update(f.world(), 1);
        check(f.hud.status_line().text().empty(), "the selection text has expired");
        f.sim.issue_move_order(s.mine, TileCoord{12, 12});
        std::string seen;
        for (int t = 0; t < 200 && seen.empty(); ++t) {
            f.sim.tick();
            f.hud.update(f.world(), 1);
            seen = f.hud.status_line().text();
        }
        check(f.unit(s.mine).type == (own == 0 ? AntType::Combat : AntType::Thief), "the ant took the power-up");
        check(seen == (own == 0 ? "Yessir!" : "Thief here"), std::string("the panel is rebuilt after the pick-up with the text of the type the ant is: ") + seen);
    }
}

// ---------------------------------------------------------------------------------------------------------------------------------
// The whole table of the busy rule, the snapshot's action, and the pick-up that rebuilds the panel
// ---------------------------------------------------------------------------------------------------------------------------------

// A copy of a real world in which the only ant is a hand-made snapshot of the local player's: what the HUD reads of an ant is its snapshot and nothing else
sim::WorldState made_world(Fixture& f, AntType type, AntType raw_type, uint8_t action) {
    sim::WorldState w = f.world();
    w.ants.clear();
    sim::AntSnapshot a;
    a.id = 77;
    a.player_id = 0;
    a.type = type;
    a.raw_type = raw_type;
    a.action = action;
    w.ants.push_back(a);
    return w;
}

// The remake's own statement of the rule (it is not a copy of the program's code): with flag 1 an ant whose OWN type is Bomber, Fire or Swimmer answers Worker unless it is idle (0),
// walking (1) or stunned (3); every other ant, and every ant under flag 0, answers what it is
bool busy_for_orders(AntType own, uint8_t action) {
    return (own == AntType::Bomber || own == AntType::Fire || own == AntType::Swimmer) && !(action == 0 || action == 1 || action == 3);
}

// What the application does in every frame, in its order: the match ticks, then the HUD looks at it
void tick_and_update(Fixture& f) {
    f.sim.tick();
    f.hud.update(f.world(), 1);
}

// Runs until `id` starts the getpow clip (action 4: the pick-up, SetAction(4) is the first thing FUN_01020cdb does); the HUD has looked at that very tick when it returns true
bool run_to_pickup(Fixture& f, uint32_t id, int max_ticks = 400) {
    for (int t = 0; t < max_ticks; ++t) {
        tick_and_update(f);
        const sim::AntSnapshot* a = snapshot_of(f, id);
        if (a != nullptr && a->action == 4) return true;
    }
    return false;
}

// The 5 s that a posted text lives (StatusLine::kLifeTicks), with the match standing still
void let_text_expire(Fixture& f) {
    for (int t = 0; t < 101; ++t) f.hud.update(f.world(), 1);
}

// The world of the review's probes: own hill (2, 2), enemy hill (40, 40), an enemy worker far away (so that the match goes on)
void pickup_world(Fixture& f) {
    f.sim.grid_mut().set_anthill(0, TileCoord{2, 2});
    f.sim.grid_mut().set_anthill(1, TileCoord{40, 40});
    f.spawn(1, AntType::Worker, 50, 50);
}

void test_busy_table() {
    g_group = "busy table";
    std::printf("[pointer] the ant type getter with flag 1, the whole table: own type Bomber, Fire or Swimmer x every action (0 .. 0x14 and beyond), the other own types, the default-type workers, flag 0\n");
    Fixture f;
    f.settle();
    f.hud.set_selected_ant_ids({77});
    const AntType own_types[6] = {AntType::Worker, AntType::Bomber, AntType::Fire, AntType::Thief, AntType::Combat, AntType::Swimmer};
    // the actions of the original: 0 idle, 1 walking, 2 entering the hill, 3 stunned, 4 power-up, 5 harvest, 6 / 7 fire wall, 8 / 9 bomb, 0xA burn, 0xB can't go, 0xC death, 0xD raid, 0xE hit,
    // 0xF drown, 0x10 / 0x11 bridge, 0x12 attack, 0x13 blown, 0x14 hatch; the rule is a test of the three that take orders, so 0x15 .. 0x20 and 0xFF are busy as well
    int rows = 0;
    for (const AntType own : own_types) {
        for (int action = 0; action <= 0x20; ++action) {
            const std::string what = "own type " + std::to_string(static_cast<int>(own)) + ", action " + std::to_string(action) + ": ";
            const sim::WorldState w = made_world(f, own, own, static_cast<uint8_t>(action));
            AntType got = AntType::Combat;
            const bool busy = busy_for_orders(own, static_cast<uint8_t>(action));
            check(f.hud.homogeneous_type(w, got, true) && got == (busy ? AntType::Worker : own), what + (busy ? "busy: a worker for the cursor and the right click" : "answers its own type"));
            check(f.hud.homogeneous_type(w, got) && got == own, what + "flag 0 (the panel text, the ability pedestal) never filters");
            ++rows;
        }
        const sim::WorldState w = made_world(f, own, own, 0xFF);
        AntType got = AntType::Combat;
        check(f.hud.homogeneous_type(w, got, true) && got == (busy_for_orders(own, 0xFF) ? AntType::Worker : own), "an action that no ant has (0xFF) is busy for the three types, free for the others");
    }
    check(rows == 6 * 33, "every row of the table was run");
    // a worker of a level with a default type has the own type 0: it is that type whatever it does (the getter's flag-0 path)
    const AntType defaults[5] = {AntType::Combat, AntType::Thief, AntType::Bomber, AntType::Swimmer, AntType::Fire};
    for (const AntType d : defaults) {
        for (int action = 0; action <= 0x14; ++action) {
            const sim::WorldState w = made_world(f, d, AntType::Worker, static_cast<uint8_t>(action));
            AntType got = AntType::Worker;
            check(f.hud.homogeneous_type(w, got, true) && got == d, "a default-type worker is never filtered (type " + std::to_string(static_cast<int>(d)) + ", action " + std::to_string(action) + ")");
        }
    }
    // a hand-made snapshot that says nothing of its action is an ant that takes orders (its action is 0): a busy-by-default snapshot would turn every bomber of a hand-made world into a worker
    {
        sim::AntSnapshot blank;
        check(blank.action == 0, "the default action of a snapshot is 0 (idle)");
        sim::WorldState w = f.world();
        w.ants.clear();
        blank.id = 77;
        blank.player_id = 0;
        blank.type = AntType::Bomber;
        blank.raw_type = AntType::Bomber;
        w.ants.push_back(blank);
        AntType got = AntType::Worker;
        check(f.hud.homogeneous_type(w, got, true) && got == AntType::Bomber, "a hand-made bomber snapshot with no action is a bomber for the cursor and the right click");
    }
    // a mixed selection must agree under flag 1: a bomber at rest and one that is busy have no common type, two busy ones are two workers, a bomber and a thief never agree
    {
        sim::WorldState w = f.world();
        w.ants.clear();
        for (uint32_t k = 0; k < 3; ++k) {
            sim::AntSnapshot a;
            a.id = 100 + k;
            a.player_id = 0;
            a.type = k < 2 ? AntType::Bomber : AntType::Thief;
            a.raw_type = a.type;
            w.ants.push_back(a);
        }
        AntType got = AntType::Combat;
        f.hud.set_selected_ant_ids({100, 101});
        check(f.hud.homogeneous_type(w, got, true) && got == AntType::Bomber, "two bombers at rest are bombers");
        w.ants[0].action = 8;
        check(!f.hud.homogeneous_type(w, got, true), "one of them planting: no common type under flag 1");
        check(f.hud.homogeneous_type(w, got) && got == AntType::Bomber, "and a common type under flag 0");
        w.ants[1].action = 4;
        check(f.hud.homogeneous_type(w, got, true) && got == AntType::Worker, "two busy bombers are two workers, whatever they are busy with");
        f.hud.set_selected_ant_ids({100, 102});
        check(!f.hud.homogeneous_type(w, got, true) && !f.hud.homogeneous_type(w, got), "a busy bomber and a thief: no common type under either flag");
    }
}

// Runs a bomber through a situation and asks the HUD at every tick what the cursor and the right click make of it (a worker while the action is not 0, 1 or 3); `seen` collects the actions
void watch_bomber(Fixture& f, uint32_t bomber, int ticks, std::vector<bool>& seen) {
    f.hud.set_selected_ant_ids({bomber});
    for (int t = 0; t < ticks; ++t) {
        f.settle();
        const sim::AntSnapshot* a = snapshot_of(f, bomber);
        if (a == nullptr || a->hp == 0) return;
        seen[std::min<size_t>(a->action, seen.size() - 1)] = true;
        AntType got = AntType::Combat;
        const bool busy = busy_for_orders(AntType::Bomber, a->action);
        check(f.hud.homogeneous_type(f.world(), got, true) && got == (busy ? AntType::Worker : AntType::Bomber),
              "a bomber doing action " + std::to_string(a->action) + (busy ? " is a worker for the orders" : " is a bomber for the orders"));
        check(f.hud.homogeneous_type(f.world(), got) && got == AntType::Bomber, "and a bomber for the panel (flag 0)");
    }
}

// The ants in real situations: the snapshot's action is the original's +0xe4 (the engine's orig_action_of): the locomotion clip's action or, for an ant that no clip drives, the number of its state
void test_snapshot_action() {
    g_group = "snapshot action";
    std::printf("[pointer] AntSnapshot::action: the action of the locomotion clip, the state's number for an ant without a clip, and the busy rule on bombers in real situations\n");
    // the state-only fallback: an ant that no locomotion clip drives (loco_action 0xFF) has the action of its state
    {
        struct Row { sim::UnitState state; uint8_t action; };
        const Row rows[] = {
            {sim::UnitState::PoweringUp, 0x04}, {sim::UnitState::Attacking, 0x12}, {sim::UnitState::Flinch, 0x0E}, {sim::UnitState::Knockback, 0x13},
            {sim::UnitState::Stunned, 0x03}, {sim::UnitState::Burn, 0x0A}, {sim::UnitState::EnteringBase, 0x02}, {sim::UnitState::Drowning, 0x0F},
            {sim::UnitState::Dead, 0x0C}, {sim::UnitState::Infiltrating, 0x0D}, {sim::UnitState::BuildingBridge, 0x10}, {sim::UnitState::DemolishingBridge, 0x11},
            {sim::UnitState::PlantingBomb, 0x08}, {sim::UnitState::DefusingBomb, 0x09}, {sim::UnitState::PlacingFire, 0x06}, {sim::UnitState::ExtinguishingFire, 0x07},
            {sim::UnitState::HarvestingFood, 0x05}, {sim::UnitState::CantGo, 0x0B}, {sim::UnitState::Walking, 0x01}, {sim::UnitState::DivingInWater, 0x01},
            {sim::UnitState::ExitingWater, 0x01}, {sim::UnitState::Idle, 0x00}, {sim::UnitState::GuardIdle, 0x00}, {sim::UnitState::Swimming, 0x00},
        };
        for (const Row& r : rows) {
            Fixture f;
            const uint32_t id = f.spawn(0, AntType::Bomber, 10, 10);
            f.settle();                                                                   // (the snapshot is rebuilt when it is asked for after a tick, so the edit below is in it)
            sim::AntUnit& u = f.sim.get_unit(id);
            u.loco_action = sim::AntUnit::kActionNone;
            u.state = r.state;
            const sim::AntSnapshot* a = snapshot_of(f, id);
            check(a != nullptr && a->action == r.action, "state " + std::to_string(static_cast<int>(r.state)) + " without a clip is action " + std::to_string(r.action) + ": " +
                                                             std::to_string(a ? a->action : -1));
        }
        Fixture f;
        const uint32_t id = f.spawn(0, AntType::Bomber, 10, 10);
        f.settle();
        check(snapshot_of(f, id)->action == 0, "a new ant is idle (action 0)");
        f.settle();
        sim::AntUnit& u = f.sim.get_unit(id);
        u.loco_action = sim::AntUnit::kActionBlown;                                      // with a clip the clip's action wins, whatever the state says
        u.state = sim::UnitState::Idle;
        check(snapshot_of(f, id)->action == 0x13, "a locomotion clip's action is the snapshot's action");
        f.settle();
        f.sim.get_unit(id).loco_action = sim::AntUnit::kActionHatch;
        check(snapshot_of(f, id)->action == 0x14, "also the hatch clip's (0x14)");
    }
    // real situations of a bomber, one match each: the HUD's answer under flag 1 follows the action that the engine reports, tick by tick, and the situation gives the action it should
    struct Situation { const char* name; uint8_t action; };
    const Situation situations[6] = {{"blown away by a combat ant's punch", 0x13}, {"hit by a worker", 0x0E}, {"attacking", 0x12}, {"cannot go there", 0x0B},
                                     {"entering its hill", 0x02}, {"grabbing a lunchbox", 0x05}};
    for (int k = 0; k < 6; ++k) {
        Fixture f;
        pickup_world(f);
        std::vector<bool> seen(0x20, false);
        uint32_t bomber = 0;
        int ticks = 100;
        if (k < 3) {
            bomber = f.spawn(0, AntType::Bomber, 20, 20);
            const uint32_t other = f.spawn(1, k == 0 ? AntType::Combat : AntType::Worker, 21, 20);
            f.settle();
            if (k == 2) f.sim.execute_melee_attack(bomber, other); else f.sim.execute_melee_attack(other, bomber);
        } else if (k == 3) {
            for (int dx = -1; dx <= 1; ++dx) for (int dy = -1; dy <= 1; ++dy) if (dx != 0 || dy != 0) f.sim.set_terrain(30 + dx, 30 + dy, 2);      // a tile in a ring of water
            bomber = f.spawn(0, AntType::Bomber, 25, 30);
            f.settle();
            f.sim.issue_move_order(bomber, TileCoord{30, 30});
            ticks = 60;
        } else if (k == 4) {
            bomber = f.spawn(0, AntType::Bomber, 8, 8);
            f.settle();
            f.sim.join_base_queue(bomber);
            ticks = 400;
        } else {
            f.sim.grid_mut().drop_lunchbox(14, 10, 25);
            bomber = f.spawn(0, AntType::Bomber, 10, 10);
            f.settle();
            f.sim.issue_move_order(bomber, TileCoord{14, 10});
            ticks = 300;
        }
        watch_bomber(f, bomber, ticks, seen);
        check(seen[situations[k].action], std::string("a bomber ") + situations[k].name + " is in action " + std::to_string(situations[k].action) + " at some tick");
    }
}

// FUN_0100cd40 (the last act of every pick-up, 0x1020dd2): for a selected ant of the local player the panel is rebuilt: the text of the type, string 12 for a group, and both pedestals raised
void test_pickup_panel() {
    g_group = "pick-up panel";
    std::printf("[pointer] a pick-up by a selected own ant rebuilds the panel (FUN_0100cd40 at 0x1020dd2): the text of its type (string 12 for a group) and both command pedestals raised, whatever the pick-up changed\n");
    // 1. a bomber with its ability pedestal latched, or its move pedestal latched, takes a Swimmer power-up: both latches are released and the new type's text is posted
    for (int latch = 0; latch < 2; ++latch) {
        const std::string what = std::string(latch == 0 ? "ability" : "move") + " pedestal latched: ";
        Fixture f;
        pickup_world(f);
        const uint32_t bomber = f.spawn(0, AntType::Bomber, 10, 20);
        f.sim.grid_mut().place_powerup(12, 20, 5);                                          // a Swimmer power-up
        f.settle();
        f.hud.select_ant(bomber);
        let_text_expire(f);
        if (latch == 0) f.latch_ability(); else f.latch_move();
        check(latch == 0 ? f.hud.is_ability_latched() : f.hud.is_move_latched(), what + "latched before");
        f.sim.issue_move_order(bomber, TileCoord{12, 20});
        check(run_to_pickup(f, bomber), what + "the bomber takes the power-up");
        check(f.unit(bomber).type == AntType::Swimmer, what + "and is a swimmer now");
        check(!f.hud.is_ability_latched() && !f.hud.is_move_latched(), what + "the rebuilt panel raised both pedestals");
        check(f.hud.status_line().text() == "SwimmerAnt selected.", what + "and posted the text of the new type: " + f.hud.status_line().text());
        // the pick-up clip (action 4) is a busy action: the swimmer is a worker for the cursor and the right click until it ends, a swimmer after
        sim::AntType got = AntType::Combat;
        check(f.hud.homogeneous_type(f.world(), got, true) && got == AntType::Worker, what + "during the getpow clip the ant is busy for the orders");
        uint32_t after = 0;
        while (after < 100 && snapshot_of(f, bomber)->action == 4) { tick_and_update(f); ++after; }
        check(after > 3 && snapshot_of(f, bomber)->action != 4, what + "the clip ends");
        check(f.hud.homogeneous_type(f.world(), got, true) && got == AntType::Swimmer, what + "and a swimmer when it ends");
        check(f.hud.status_line().text() == "SwimmerAnt selected." && f.hud.status_line().age_ticks() == after, what + "the end of the clip posts nothing again");
        // a latch that is made after the pick-up stays (nothing rebuilds the panel again)
        f.latch_ability();
        for (int t = 0; t < 20; ++t) tick_and_update(f);
        check(f.hud.is_ability_latched(), what + "a latch made after the pick-up is not released by anything");
    }
    // 2. an ant that takes the power-up of its own type again changes nothing that can be seen, and the panel is rebuilt all the same: a Bomber takes a Bomber power-up
    {
        Fixture f;
        pickup_world(f);
        const uint32_t bomber = f.spawn(0, AntType::Bomber, 10, 20);
        f.sim.grid_mut().place_powerup(12, 20, 1);                                          // a Bomber power-up
        f.settle();
        f.hud.select_ant(bomber);
        let_text_expire(f);
        f.latch_ability();
        check(f.hud.is_ability_latched(), "same type: latched before");
        f.sim.issue_move_order(bomber, TileCoord{12, 20});
        check(run_to_pickup(f, bomber), "same type: the bomber takes the power-up");
        check(f.unit(bomber).type == AntType::Bomber, "same type: it is a bomber before and after");
        check(f.hud.status_line().text() == "BomberAnt selected.", "same type: the panel text is posted again: " + f.hud.status_line().text());
        check(!f.hud.is_ability_latched(), "same type: and the ability pedestal is raised");
    }
    // 3. the default-type cases: on a level whose default is Combat a worker (own type 0, it IS a combat ant) takes the Combat power-up, and a combat ant with the own type 4 takes it again
    for (int own = 0; own < 2; ++own) {
        const std::string what = std::string(own == 0 ? "default-type worker" : "typed combat ant") + " takes a Combat power-up on a default-Combat level: ";
        Fixture f;
        pickup_world(f);
        f.sim.grid_mut().set_default_ant_tile(62);
        const uint32_t ant = f.spawn(0, own == 0 ? AntType::Worker : AntType::Combat, 10, 20);
        f.sim.grid_mut().place_powerup(12, 20, 4);
        f.settle();
        f.hud.select_ant(ant);
        let_text_expire(f);
        f.latch_ability();
        check(f.hud.is_ability_latched(), what + "the attack pedestal is latched before");
        f.sim.issue_move_order(ant, TileCoord{12, 20});
        check(run_to_pickup(f, ant), what + "it takes the power-up");
        check(snapshot_of(f, ant)->type == AntType::Combat, what + "it is a combat ant before and after");
        check(f.hud.status_line().text() == "Yessir!", what + "the panel text is posted: " + f.hud.status_line().text());
        check(!f.hud.is_ability_latched(), what + "and the pedestal is raised");
    }
    // 4. another team's ant under inspection (panel 5) takes a power-up: nothing happens (FUN_0100cd7d: the ant's team must be the local player)
    {
        Fixture f;
        pickup_world(f);
        const uint32_t foe = f.spawn(1, AntType::Worker, 20, 20);
        f.sim.grid_mut().place_powerup(22, 20, 4);
        f.settle();
        f.hud.select_ant(foe);
        let_text_expire(f);
        check(f.hud.panel_mode(f.world()) == HUD::PanelMode::Other, "inspected enemy: panel 5");
        f.hud.post_status("Bomb dropped.");
        f.sim.issue_move_order(foe, TileCoord{22, 20});
        check(run_to_pickup(f, foe), "inspected enemy: it takes the power-up");
        check(f.unit(foe).type == AntType::Combat, "inspected enemy: and is a combat ant now");
        check(f.hud.status_line().text() == "Bomb dropped.", "inspected enemy: the text is not touched: " + f.hud.status_line().text());
    }
    // 5. an own ant that is not selected takes a power-up while another own ant is selected: nothing happens to the selected one's panel
    {
        Fixture f;
        pickup_world(f);
        const uint32_t selected = f.spawn(0, AntType::Bomber, 10, 20);
        const uint32_t other = f.spawn(0, AntType::Worker, 10, 30);
        f.sim.grid_mut().place_powerup(12, 30, 3);                                          // a Thief power-up
        f.settle();
        f.hud.select_ant(selected);
        let_text_expire(f);
        f.latch_ability();
        f.hud.post_status("Bomb dropped.");
        f.sim.issue_move_order(other, TileCoord{12, 30});
        check(run_to_pickup(f, other), "unselected own ant: it takes the power-up");
        check(f.unit(other).type == AntType::Thief, "unselected own ant: and is a thief now");
        check(f.hud.status_line().text() == "Bomb dropped.", "unselected own ant: the selected ant's panel is not rebuilt: " + f.hud.status_line().text());
        check(f.hud.is_ability_latched(), "unselected own ant: and its pedestal stays latched");
        // an ant that is selected only while its clip already runs was not selected when it took the power-up: no rebuild for it
        f.hud.select_ant(other);
        f.hud.update(f.world(), 1);
        check(snapshot_of(f, other)->action == 4, "late selection: the clip still runs");
        check(f.hud.status_line().text() == "Thief here", "late selection: the selection posts its own text: " + f.hud.status_line().text());
        f.latch_ability();
        check(f.hud.is_ability_latched(), "late selection: the thief's ability pedestal latches");
        f.hud.post_status("Bomb dropped.");
        for (int t = 0; t < 5; ++t) tick_and_update(f);
        check(f.hud.status_line().text() == "Bomb dropped." && f.hud.is_ability_latched(), "late selection: nothing is rebuilt while the clip runs on");
    }
    // 6. a group: one of two selected ants takes a power-up: string 12, and a latched move pedestal is raised (the group's panel has no ability pedestal)
    {
        Fixture f;
        pickup_world(f);
        const uint32_t a = f.spawn(0, AntType::Bomber, 10, 20);
        const uint32_t b = f.spawn(0, AntType::Worker, 10, 30);
        f.sim.grid_mut().place_powerup(12, 20, 4);                                          // a Combat power-up: panel 3 would say "Yessir!"
        f.settle();
        f.hud.set_selected_ant_ids({a, b});
        let_text_expire(f);
        f.latch_move();
        check(f.hud.is_move_latched() && f.hud.panel_mode(f.world()) == HUD::PanelMode::Ants, "group: panel 4 with the move pedestal latched");
        f.sim.issue_move_order(a, TileCoord{12, 20});
        check(run_to_pickup(f, a), "group: one of them takes the power-up");
        check(f.hud.status_line().text() == "Ready!", "group: string 12 is posted, not the text of the type: " + f.hud.status_line().text());
        check(!f.hud.is_move_latched(), "group: the move pedestal is raised");
    }
    // 7. panel 4 with a single ant (a shift selection of one: the panel is stored, not counted): string 12 as well, even for a type with a text of its own
    {
        Fixture f;
        pickup_world(f);
        const uint32_t a = f.spawn(0, AntType::Bomber, 10, 20);
        f.sim.grid_mut().place_powerup(12, 20, 4);
        f.settle();
        f.hud.select_ant(a, true);
        let_text_expire(f);
        check(f.hud.panel_mode(f.world()) == HUD::PanelMode::Ants, "panel 4 with one ant");
        f.sim.issue_move_order(a, TileCoord{12, 20});
        check(run_to_pickup(f, a), "panel 4 with one ant: it takes the power-up");
        check(f.hud.status_line().text() == "Ready!", "panel 4 with one ant says string 12, not \"Yessir!\": " + f.hud.status_line().text());
    }
    // 8. two selected ants take their power-ups in the same tick: the one text of a group, posted once for the update
    {
        Fixture f;
        pickup_world(f);
        const uint32_t a = f.spawn(0, AntType::Bomber, 10, 20);
        const uint32_t b = f.spawn(0, AntType::Fire, 10, 30);
        f.settle();
        f.hud.set_selected_ant_ids({a, b});
        let_text_expire(f);
        sim::WorldState w = f.world();
        f.hud.update(w, 1);                                                                 // both at rest: remembered
        for (auto& ant : w.ants) if (ant.id == a || ant.id == b) ant.action = 4;
        f.latch_move();
        check(f.hud.is_move_latched(), "two pick-ups: the move pedestal of the group is latched");
        f.hud.update(w, 1);
        check(f.hud.status_line().text() == "Ready!" && f.hud.status_line().age_ticks() == 0, "two pick-ups in one update: string 12, freshly posted: " + f.hud.status_line().text());
        check(!f.hud.is_move_latched(), "two pick-ups in one update: the pedestal is raised");
        f.hud.post_status("Bomb dropped.");
        f.hud.update(w, 1);                                                                 // both still in their clips: no new edge
        check(f.hud.status_line().text() == "Bomb dropped.", "and nothing more while the clips run");
    }
    // 9. the guards, on snapshots that the test edits: a dead or a drowning ant, and an ant that is not the local player's, never rebuild a panel
    for (int guard = 0; guard < 3; ++guard) {
        const std::string what = std::string(guard == 0 ? "dead own ant" : guard == 1 ? "drowning own ant" : "another team's ant") + ": ";
        Fixture f;
        pickup_world(f);
        const uint32_t a = f.spawn(0, AntType::Bomber, 10, 20);
        f.settle();
        f.hud.select_ant(a);
        let_text_expire(f);
        f.latch_ability();
        sim::WorldState w = f.world();
        f.hud.update(w, 1);                                                                 // at rest: remembered
        f.hud.post_status("Bomb dropped.");
        for (auto& ant : w.ants) {
            if (ant.id != a) continue;
            ant.action = 4;
            if (guard == 0) ant.hp = 0;
            else if (guard == 1) ant.is_drowning = true;
            else ant.player_id = 1;
        }
        f.hud.update(w, 1);
        check(f.hud.status_line().text() == "Bomb dropped.", what + "no text: " + f.hud.status_line().text());
        check(f.hud.is_ability_latched(), what + "no pedestal raised");
    }
    // the same edit with an own ant that is alive and not drowning does rebuild the panel: the guards above are the only reason that nothing happened
    {
        Fixture f;
        pickup_world(f);
        const uint32_t a = f.spawn(0, AntType::Bomber, 10, 20);
        f.settle();
        f.hud.select_ant(a);
        let_text_expire(f);
        f.latch_ability();
        sim::WorldState w = f.world();
        f.hud.update(w, 1);
        f.hud.post_status("Bomb dropped.");
        for (auto& ant : w.ants) if (ant.id == a) ant.action = 4;
        f.hud.update(w, 1);
        check(f.hud.status_line().text() == "BomberAnt selected." && !f.hud.is_ability_latched(), "the control: a live own ant that starts the getpow clip rebuilds the panel");
    }
    // the text is that of the type the ant IS (the getter's answer, AntSnapshot::type), not of the own type field (which is 0 for a worker): a worker of a default-Combat level says "Yessir!"
    {
        Fixture f;
        pickup_world(f);
        f.sim.grid_mut().set_default_ant_tile(62);
        const uint32_t a = f.spawn(0, AntType::Worker, 10, 20);
        f.settle();
        f.hud.select_ant(a);
        let_text_expire(f);
        sim::WorldState w = f.world();
        const sim::AntSnapshot* made = snapshot_of(f, a);
        check(made != nullptr && made->type == AntType::Combat && made->raw_type == AntType::Worker, "the worker of the default-Combat level is a combat ant with the own type 0");
        f.hud.update(w, 1);
        for (auto& ant : w.ants) if (ant.id == a) ant.action = 4;
        f.hud.update(w, 1);
        check(f.hud.status_line().text() == "Yessir!", "the rebuilt panel names the type the ant is, not its own type field: " + f.hud.status_line().text());
    }
    // a new match forgets what the panel remembers: an ant that is already in its clip when the first update of a fresh HUD sees it is no pick-up
    {
        Fixture f;
        pickup_world(f);
        const uint32_t a = f.spawn(0, AntType::Bomber, 10, 20);
        f.settle();
        f.hud.set_selected_ant_ids({a});
        sim::WorldState w = f.world();
        f.hud.update(w, 1);                                                                 // remembered at rest
        f.hud.init(0);
        const std::string welcome = f.hud.status_line().text();
        f.hud.set_selected_ant_ids({a});
        for (auto& ant : w.ants) if (ant.id == a) ant.action = 4;
        f.hud.update(w, 1);
        check(f.hud.status_line().text() == welcome, "a fresh HUD remembers nothing: no pick-up from a record of the last match: " + f.hud.status_line().text());
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
    // FUN_010287b5 returns 1 when any ant needed the order, whatever its GoTo answered (0x1028a07), and only the closest ant's GoTo decides the voice: a worker that is
    // ordered onto the enemy hill is refused (no voice, no text) but the pedestal still flashes with its click; a special click that goes to several ants is silent altogether
    {
        Scene t = make_scene();
        Fixture& g = *t.f;
        g.hud.select_ant(t.mine);
        g.sounds.clear();
        g.hud.clear_status();
        const uint32_t ack = g.hud.order_selected(g.sim, sim::TileCoord{41, 41}, false, false);
        check(ack == 0, "a worker's GoTo onto an enemy hill is refused: nobody acknowledges");
        check(g.hud.is_pedestal_flashing(0), "but the ant needed the order: the move pedestal flashes (the group order returned 1)");
        check(g.sounds.size() == 1 && g.sounds[0] == sim::SoundID::NavButtonClick, "with its click (89) and no voice");
        check(g.hud.status_line().text().empty(), "and no text");
        // the same click again: the ant does not carry it out (it was refused), so it still needs it; a click it already carries out is skipped altogether
        g.sounds.clear();
        const uint32_t ok = g.hud.order_selected(g.sim, sim::TileCoord{20, 20}, false, false);
        check(ok == t.mine && !g.sounds.empty() && g.sounds.front() != sim::SoundID::NavButtonClick, "an accepted move: the voice first, then the click");
        g.sounds.clear();
        const uint32_t again = g.hud.order_selected(g.sim, sim::TileCoord{20, 20}, false, false);
        check(again == 0 && g.sounds.empty(), "every ant already carries it out: nothing at all");
    }
    {
        Fixture g;
        const uint32_t b1 = g.spawn(0, AntType::Bomber, 10, 10);
        const uint32_t b2 = g.spawn(0, AntType::Bomber, 12, 10);
        g.sim.grid_mut().place_bomb(30, 12, 1);
        g.sim.grid_mut().place_bomb(32, 12, 1);
        g.settle();
        g.hud.set_selected_ant_ids({b1, b2});
        g.sounds.clear();
        g.hud.order_selected(g.sim, sim::TileCoord{30, 12}, true, false);
        check(g.sounds.size() == 1 && g.sounds[0] == sim::SoundID::NavButtonClick, "a special click that goes to two ants: no voice at all, only the pedestal click");
        g.hud.set_selected_ant_ids({b1});
        g.sounds.clear();
        g.hud.order_selected(g.sim, sim::TileCoord{32, 12}, true, false);
        check(g.sounds.size() == 2 && g.sounds[0] != sim::SoundID::NavButtonClick && g.sounds[1] == sim::SoundID::NavButtonClick,
              "and to exactly one ant that needs it: its voice, then the click");
    }
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
        // the Return button (the button class): the press captures it, the release on the button closes the quick help, a click anywhere else does nothing
        key(SDLK_F1);
        f.press(300, 200);
        f.release(300, 200);
        check(f.hud.is_quick_help_open(), "a click on the screen does not close the quick help (FUN_010145d2: only the button and the keys)");
        f.press(580, 450);
        check(f.hud.is_quick_help_open() && f.hud.quick_help_return_button().pressed(), "a press on Return captures it; nothing closes yet");
        f.release(580, 450);
        check(!f.hud.is_quick_help_open(), "the release on Return closes it");
        key(SDLK_F1);
        f.press(580, 450);
        f.hud.handle_mouse_motion(300, 200, f.sim, f.cam);
        f.hud.handle_mouse_motion(580, 450, f.sim, f.cam);
        f.release(580, 450);
        check(f.hud.is_quick_help_open(), "leaving the button cancels the capture for good: coming back and releasing closes nothing");
        f.press(580, 450);
        f.release(300, 200);
        check(f.hud.is_quick_help_open(), "a release away from the button does not close it");
        // the hit test is the rectangle of the picture that shows (0x10112e9, docs 5.52): the press must hit qh_return1 / 2 = [529, 627) x [437, 463), every move until the
        // release must stay on qh_return3 = [527, 624) x [437, 461): the click zone is the intersection [529, 624) x [437, 461)
        check(f.hud.quick_help_return_button().up_rect() == ButtonRect({529, 437, 98, 26}) && f.hud.quick_help_return_button().pressed_rect() == ButtonRect({527, 437, 97, 24}),
              "the Return button has two rectangles: the resting picture's and the pressed picture's");
        auto zone = [&](int32_t x, int32_t y) {
            key(SDLK_F1);
            f.hud.handle_mouse_motion(x, y, f.sim, f.cam);
            f.press(x, y);
            f.release(x, y);
            const bool closed = !f.hud.is_quick_help_open();
            if (f.hud.is_quick_help_open()) key(SDLK_RETURN);
            return closed;
        };
        check(zone(529, 437) && zone(623, 460) && zone(580, 450), "the click zone: (529, 437), (623, 460) and the middle close the quick help");
        check(!zone(527, 437), "(527, 437) is on the pressed picture only: no capture, nothing closes");
        check(!zone(626, 462), "(626, 462) is on the resting picture only: captured and cancelled at once, nothing closes");
        check(!zone(528, 450) && !zone(529, 461) && !zone(627, 440), "(528, 450), (529, 461) and (627, 440) close nothing");
        key(SDLK_RETURN);
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
        f.press(151, 289);                                        // the OFF toggle of the chat option (a switch acts at the release)
        f.release(151, 289);
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
    // the dialog's buttons are the button class as well: leaving a pressed button cancels its capture for good (FUN_01011281), coming back only hovers
    bool quit_asked = false;
    f.hud.set_on_quit([&]() { quit_asked = true; });
    f.press(200, 270);                                                        // Yes (180, 260) 49 x 24
    f.hud.handle_mouse_motion(400, 100, f.sim, f.cam);
    f.hud.handle_mouse_motion(200, 270, f.sim, f.cam);
    f.release(200, 270);
    check(f.hud.is_quit_dialog_open() && !quit_asked, "Yes: pressed, left and entered again, the release does nothing");
    f.press(200, 270);
    f.release(200, 270);
    check(!f.hud.is_quit_dialog_open() && quit_asked, "a plain press and release on Yes answers it");
    // [All] (532, 443, 44, 24): sends the text at the release; hidden while chat is off
    f.hud.set_chat_input("to all");
    const size_t base = f.hud.get_chat_log().size();
    f.press(545, 450);
    check(f.hud.get_chat_log().size() == base, "nothing is sent at the press");
    f.release(545, 450);
    check(f.hud.get_chat_log().size() == base + 2 && f.hud.get_chat_input().empty(), "the release sends and clears");
    f.hud.open_options();
    f.press(151, 289);
    f.release(151, 289);
    f.hud.close_options();
    f.hud.set_chat_input("");
    f.sounds.clear();
    f.press(545, 450);
    check(f.sounds.empty() && !f.hud.is_input_captured(), "chat off: [All] is hidden and does not react");
    f.release(545, 450);
}

// A pointer over a black bar of a wide window arrives at the game as a position outside the 640 x 480 picture; it is the pointer on the nearest edge pixel
// (pointer_clamp.hpp). Also the two rules around it: after which events the pointer is gone, and when the window grabs it.
void test_pillarbox() {
    g_group = "pillarbox";
    check(clamp_to_screen_x(-180) == 0 && clamp_to_screen_x(-1) == 0 && clamp_to_screen_x(0) == 0, "left of the picture: x = 0");
    check(clamp_to_screen_x(1) == 1 && clamp_to_screen_x(320) == 320 && clamp_to_screen_x(638) == 638 && clamp_to_screen_x(639) == 639, "on the picture: unchanged");
    check(clamp_to_screen_x(640) == 639 && clamp_to_screen_x(819) == 639 && clamp_to_screen_x(100000) == 639, "right of the picture: x = 639");
    check(clamp_to_screen_y(-1) == 0 && clamp_to_screen_y(0) == 0 && clamp_to_screen_y(479) == 479 && clamp_to_screen_y(480) == 479 && clamp_to_screen_y(-100000) == 0, "the same for y");
    check(clamp_to_screen_x(INT32_MIN) == 0 && clamp_to_screen_x(INT32_MAX) == 639 && clamp_to_screen_y(INT32_MIN) == 0 && clamp_to_screen_y(INT32_MAX) == 479, "any int");

    SDL_Event m{};
    m.type = SDL_MOUSEMOTION;
    m.motion.x = -50;
    m.motion.y = 700;
    m.motion.xrel = -7;
    m.motion.yrel = 9;
    m.motion.state = SDL_BUTTON_LMASK;
    m.motion.which = 3;
    clamp_pointer_event(m);
    check(m.motion.x == 0 && m.motion.y == 479, "a motion over the bottom left corner of the bars lands on the corner pixel");
    check(m.motion.xrel == -7 && m.motion.yrel == 9 && m.motion.state == SDL_BUTTON_LMASK && m.motion.which == 3u, "the motion's other fields are not touched");
    for (const Uint32 type : {static_cast<Uint32>(SDL_MOUSEBUTTONDOWN), static_cast<Uint32>(SDL_MOUSEBUTTONUP)}) {
        SDL_Event b{};
        b.type = type;
        b.button.x = 900;
        b.button.y = -5;
        b.button.button = SDL_BUTTON_RIGHT;
        b.button.clicks = 2;
        b.button.state = SDL_PRESSED;
        clamp_pointer_event(b);
        check(b.button.x == 639 && b.button.y == 0, "a button over the top right of the bars lands on the corner pixel");
        check(b.button.button == SDL_BUTTON_RIGHT && b.button.clicks == 2 && b.button.state == SDL_PRESSED, "the button's other fields are not touched");
    }
    SDL_Event k{};
    k.type = SDL_KEYDOWN;
    k.key.keysym.sym = SDLK_a;
    SDL_Event k_before = k;
    clamp_pointer_event(k);
    check(std::memcmp(&k, &k_before, sizeof k) == 0, "a key event is not touched");
    SDL_Event w{};
    w.type = SDL_MOUSEWHEEL;
    w.wheel.x = -999;
    w.wheel.y = 999;
    SDL_Event w_before = w;
    clamp_pointer_event(w);
    check(std::memcmp(&w, &w_before, sizeof w) == 0, "a wheel event (its x and y are scroll amounts) is not touched");

    // what it is for: raw positions over the bars give no scrolling at the picture's own rules (edge_scroll_step finds no strip for them), the clamped ones
    // scroll like the edge pixel
    check(edge_scroll_step(-1, 240, 50, 700, 700, 60, 60).dir == -1 && edge_scroll_step(640, 240, 50, 700, 700, 60, 60).dir == -1 &&
              edge_scroll_step(320, -1, 50, 700, 700, 60, 60).dir == -1 && edge_scroll_step(320, 480, 50, 700, 700, 60, 60).dir == -1,
          "the premise: a raw position just beside the picture (a bar on any side) is in no edge strip");
    for (const int32_t raw : {-180, -90, -1}) {
        const EdgeScroll s = edge_scroll_step(clamp_to_screen_x(raw), 240, 50, 700, 700, 60, 60);
        check(s.dx < 0 && s.dir == edge_scroll_step(0, 240, 50, 700, 700, 60, 60).dir, "over the left bar the view scrolls west like on x = 0");
    }
    for (const int32_t raw : {640, 700, 819}) {
        const EdgeScroll s = edge_scroll_step(clamp_to_screen_x(raw), 240, 50, 700, 700, 60, 60);
        check(s.dx > 0 && s.dir == edge_scroll_step(639, 240, 50, 700, 700, 60, 60).dir, "over the right bar the view scrolls east like on x = 639");
    }
    for (const int32_t raw : {-60, -1}) {
        const EdgeScroll s = edge_scroll_step(320, clamp_to_screen_y(raw), 50, 700, 700, 60, 60);
        check(s.dy < 0, "over a bar above the picture the view scrolls north");
    }
    for (const int32_t raw : {480, 560}) {
        const EdgeScroll s = edge_scroll_step(320, clamp_to_screen_y(raw), 50, 700, 700, 60, 60);
        check(s.dy > 0, "over a bar below the picture the view scrolls south");
    }

    // After which events is the pointer gone (no LEAVE follows): a release outside the window, and the lift of a finger; nothing else
    auto button = [](Uint32 type, Uint32 which) {
        SDL_Event e{};
        e.type = type;
        e.button.which = which;
        e.button.button = SDL_BUTTON_LEFT;
        return e;
    };
    const SDL_Event up_mouse = button(SDL_MOUSEBUTTONUP, 0);
    const SDL_Event up_touch = button(SDL_MOUSEBUTTONUP, SDL_TOUCH_MOUSEID);
    const SDL_Event down_mouse = button(SDL_MOUSEBUTTONDOWN, 0);
    const SDL_Event down_touch = button(SDL_MOUSEBUTTONDOWN, SDL_TOUCH_MOUSEID);
    check(pointer_gone_after(up_mouse, true), "a mouse button released outside the window: the pointer is gone");
    check(!pointer_gone_after(up_mouse, false), "a mouse button released in the window (a black bar is in the window): the pointer stays");
    check(pointer_gone_after(up_touch, false) && pointer_gone_after(up_touch, true), "a lifted finger, wherever it was: gone");
    check(!pointer_gone_after(down_mouse, true) && !pointer_gone_after(down_mouse, false) && !pointer_gone_after(down_touch, false) &&
              !pointer_gone_after(down_touch, true),
          "a press is never the end (SDL captures the pointer while a button is held: the drag goes on at the edge)");
    SDL_Event motion_out{};
    motion_out.type = SDL_MOUSEMOTION;
    SDL_Event motion_touch = motion_out;
    motion_touch.motion.which = SDL_TOUCH_MOUSEID;
    check(!pointer_gone_after(motion_out, true) && !pointer_gone_after(motion_touch, false) && !pointer_gone_after(k, true) && !pointer_gone_after(w, true),
          "a motion (SDL sends a LEAVE for an uncaptured pointer that leaves), a key or the wheel is never the end");

    // The grab that the game asks SDL for: fullscreen (SDL's or the system's), never headless; the whole table of the three inputs (SDL applies the request
    // only while the window has the focus: test 7.8e)
    int grabbing = 0;
    for (int bits = 0; bits < 8; ++bits) {
        const bool sdl_fs = (bits & 1) != 0;
        const bool os_fs = (bits & 2) != 0;
        const bool headless = (bits & 4) != 0;
        const bool want = wants_mouse_grab(sdl_fs, os_fs, headless);
        if (want) ++grabbing;
        check(want == (!headless && (sdl_fs || os_fs)), "the grab table, row " + std::to_string(bits));
    }
    check(grabbing == 3, "the grab is asked for in exactly 3 of the 8 cases: not headless, and SDL's fullscreen, the system's, or both");
    check(wants_mouse_grab(true, false, false) && wants_mouse_grab(false, true, false), "SDL's fullscreen (--fullscreen) grabs, and so does a macOS fullscreen Space");
    check(!wants_mouse_grab(false, false, false), "a window (also a maximized one: that is not fullscreen) does not grab");
    check(!wants_mouse_grab(true, true, true), "a headless run never grabs");

    // The Dock and the menu bar of macOS (the owner: they came up whenever the pointer touched the bottom or top edge of a fullscreen screen). SDL's own fullscreen already has them hidden for good
    // (its window delegate asks for FullScreen | HideDock | HideMenuBar when the window has SDL's flag), so the game asks only for the fullscreen Space that SDL's flags do not describe (the
    // green button, Cmd+Ctrl+F): the whole table of the three inputs
    int hiding = 0;
    for (int bits = 0; bits < 8; ++bits) {
        const bool sdl_fs = (bits & 1) != 0;
        const bool os_fs = (bits & 2) != 0;
        const bool headless = (bits & 4) != 0;
        const bool hide = wants_hidden_dock_and_menu_bar(sdl_fs, os_fs, headless);
        if (hide) ++hiding;
        check(hide == (!headless && os_fs && !sdl_fs), "the Dock and menu bar table, row " + std::to_string(bits));
    }
    check(hiding == 1, "the game hides them in exactly 1 of the 8 cases: a Space of the system's own (no SDL flag), not headless");
    check(wants_hidden_dock_and_menu_bar(false, true, false), "a macOS fullscreen Space entered with the green button: hidden by the game");
    check(!wants_hidden_dock_and_menu_bar(true, true, false) && !wants_hidden_dock_and_menu_bar(true, false, false), "SDL's own fullscreen needs nothing from the game (SDL's delegate hides them), with or without the Space's style mask");
    check(!wants_hidden_dock_and_menu_bar(false, false, false), "a window (also a maximized one) leaves the Dock and the menu bar alone");
    check(!wants_hidden_dock_and_menu_bar(false, true, true), "a headless run never asks the window system for anything");
    std::printf("[pillarbox] a pointer beyond the picture is on its edge pixel; a release outside the window and a lifted finger end it; fullscreen grabs; the Dock and the menu bar\n");
}

}  // namespace


int main(int argc, char* argv[]) {
    // SDL's headers rename main to SDL_main (SDL2main on Windows calls it): the signature must be this one, or the linker finds no SDL_main (the build guard in CMakeLists.txt checks it)
    (void)argc;
    (void)argv;
    test_hit_boxes();
    test_cursor_without_own_ants();
    test_cursor_with_own_ants();
    test_cursor_special_targets();
    test_cursor_misc();
    test_click_modes();
    test_click_or_drag();
    test_drag_select();
    test_shift_group();
    test_right_button();
    test_busy_ability_ants();
    test_default_type_hud();
    test_busy_table();
    test_snapshot_action();
    test_pickup_panel();
    test_pedestals();
    test_keyboard();
    test_buttons();
    test_pillarbox();
    std::printf("pointer model: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
