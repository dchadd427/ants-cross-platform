// Status line and message tests: the one-slot status box of Ants.exe (PostStatus FUN_0100e944 with the CLEARSTAT and TXTFLASH
// tasks), the texts that selections and ordering commands post (FUN_01027f07, FUN_0101b67b / b711 / b78a, FUN_01028a60), the
// flash flags of the world messages, and the drawing of the box (rect (481, 254) - (620, 266), colour (79, 0, 143)).
// (docs/GAME_REVERSE_ENGINEERING.md 5.41)
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "ants_app/hud.hpp"
#include "ants_app/renderer.hpp"
#include "ants_app/status_line.hpp"
#include "ants_assets/asset_archive.hpp"
#include "ants_sim/game_strings.hpp"
#include "ants_sim/sim_engine.hpp"
#include "ants_test_paths.hpp"

using namespace ants;
using namespace ants::app;

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

// Records the text draws of the HUD (with colour) and ignores everything else.
class TextRenderer : public IRenderer {
public:
    void draw_sprite(uint32_t, int32_t, int32_t, bool) override {}
    void draw_named_sprite(const std::string&, int32_t, int32_t, bool) override {}
    void fill_rect(int32_t, int32_t, int32_t, int32_t, assets::ColorRGBA) override {}
    void draw_rect(int32_t, int32_t, int32_t, int32_t, assets::ColorRGBA) override {}
    void draw_text(const std::string& text, int32_t x, int32_t y, assets::ColorRGBA color) override {
        texts.push_back({text, x, y, color});
    }
    void set_hud_team(uint8_t) override {}
    struct Text { std::string text; int32_t x; int32_t y; assets::ColorRGBA color; };
    std::vector<Text> texts;

    // The text drawn in the status box (its top-left corner is (481, 254))
    const Text* status() const {
        for (const auto& t : texts) if (t.x == 481 && t.y == 254) return &t;
        return nullptr;
    }
};

const assets::AssetArchive* g_archive = nullptr;

// Advances the HUD by `ticks` game ticks with the given world
void run(HUD& hud, const sim::WorldState& world, uint32_t ticks) {
    for (uint32_t t = 0; t < ticks; ++t) hud.update(world, 1);
}

std::string status_after_render(HUD& hud, const sim::WorldState& world) {
    TextRenderer rr;
    ViewportCamera camera;
    hud.render(rr, *g_archive, world, camera);
    const auto* s = rr.status();
    return s ? s->text : std::string();
}

// A test world with a home tile for player 0 and one hill of player 1
void make_world(sim::SimulationEngine& sim) {
    sim.init_test_world(60, 60, 7, 720000);
    sim.set_anthill(0, sim::TileCoord{2, 2});
    sim.set_anthill(1, sim::TileCoord{40, 40});
}

// ------------------------------------------------------------------------------------------------
// Part A: the status slot
// ------------------------------------------------------------------------------------------------

void test_slot_replace_and_expiry() {
    g_group = "slot";
    std::printf("[status] one slot: a post replaces the text, the text lives 5000 ms after the last non-empty post\n");
    StatusLine s;
    s.post("A");
    s.update(60);                                   // 3000 ms
    check(s.text() == "A", "A is still there after 3 s");
    s.post("B");                                    // replaces at once, no queue
    check(s.text() == "B", "B replaces A");
    s.update(99);                                   // 4950 ms after B
    check(s.text() == "B" && s.visible(), "B stays until 5000 ms (exclusive)");
    s.update(1);                                    // 5000 ms
    check(s.text().empty() && !s.visible(), "B is gone at 5000 ms after its post");
    // the same text posted again restarts the life
    s.post("C");
    s.update(80);
    s.post("C");
    s.update(80);
    check(s.text() == "C", "re-posting the same text restarts its 5 s");
}

void test_slot_empty_post() {
    g_group = "slot";
    std::printf("[status] an empty post clears at once and does not re-arm the timer\n");
    StatusLine s;
    s.post("A");
    s.update(40);
    s.post("");
    check(s.text().empty(), "the empty post clears");
    s.update(10);
    check(s.text().empty(), "and it stays empty");
    s.post("B");
    s.update(99);
    check(s.text() == "B", "a new post starts a fresh 5 s life");
    s.update(1);
    check(s.text().empty(), "which ends 5 s later");
    // clear() is the same
    s.post("D");
    s.clear();
    check(s.text().empty(), "clear() empties");
}

void test_slot_flash() {
    g_group = "slot";
    std::printf("[status] the flash flag flickers the text for 500 ms: visible steps '#.#.#.#.#.##' at 50 ms\n");
    StatusLine s;
    s.post("Can't - already have food.", true);
    std::string pattern;
    for (int step = 0; step < 12; ++step) {
        pattern += s.visible() ? '#' : '.';
        s.update(1);
    }
    check(pattern == "#.#.#.#.#.##", "flash pattern is " + pattern);
    check(!s.flashing(), "the flash is over after 500 ms");
    // a re-post restarts the flash
    s.post("x", true);
    s.update(1);
    check(!s.visible(), "step 1 of a new flash is hidden");
    s.post("x", true);
    check(s.visible() && s.flashing(), "a re-post restarts the flash at its visible first step");
    // a post without the flag ends a running flash
    s.update(2);
    s.post("plain", false);
    std::string steady;
    for (int step = 0; step < 6; ++step) { steady += s.visible() ? '#' : '.'; s.update(1); }
    check(steady == "######", "a plain post is always visible");
    // an empty post ends the flash too
    s.post("y", true);
    s.post("");
    s.post("z", false);
    check(s.visible() && !s.flashing(), "the flash of an earlier post does not carry over");
}

// ------------------------------------------------------------------------------------------------
// Part B: the status box on screen
// ------------------------------------------------------------------------------------------------

void test_box_geometry() {
    g_group = "box";
    std::printf("[status] the text is drawn at (481, 254) in colour (79, 0, 143), nothing invented while it is empty\n");
    sim::SimulationEngine sim;
    make_world(sim);
    HUD hud;
    hud.init(0);
    const sim::WorldState& world = sim.get_world_state();

    // the match screen starts with "Welcome to Ants!" (string 5)
    {
        TextRenderer rr;
        ViewportCamera camera;
        hud.render(rr, *g_archive, world, camera);
        const auto* s = rr.status();
        check(s && s->text == "Welcome to Ants!", "the welcome text is posted when the match screen is built");
        check(s && s->color.r == 79 && s->color.g == 0 && s->color.b == 143, "colour (79, 0, 143)");
    }
    run(hud, world, 100);                          // 5 s
    check(status_after_render(hud, world).empty(), "nothing is drawn once the welcome has expired (no idle text)");

    // a selected ant does not bring back "Ready." / "Waiting for orders." / "Enemy ant."
    const uint32_t w = sim.spawn_unit(0, sim::AntType::Worker, sim::TileCoord{10, 10});
    const uint32_t foe = sim.spawn_unit(1, sim::AntType::Worker, sim::TileCoord{12, 10});
    hud.select_ant(w);
    run(hud, sim.get_world_state(), 101);
    check(status_after_render(hud, sim.get_world_state()).empty(), "no persistent text for a selected ant");
    hud.select_ant(foe);
    run(hud, sim.get_world_state(), 1);
    check(status_after_render(hud, sim.get_world_state()).empty(), "no 'Enemy ant.' text");
    hud.select_base(0);
    run(hud, sim.get_world_state(), 1);
    check(status_after_render(hud, sim.get_world_state()).empty(), "no text for the home hill either");
}

void test_box_clip_and_flash() {
    g_group = "box";
    std::printf("[status] the text is clipped at 139 px and hidden on the odd 50 ms steps of a flash\n");
    sim::SimulationEngine sim;
    make_world(sim);
    HUD hud;
    hud.init(0);
    const sim::WorldState& world = sim.get_world_state();
    hud.post_status("This message is much longer than the 139 pixel box of the status line can show");
    const std::string shown = status_after_render(hud, world);
    check(!shown.empty() && shown.size() * 6 <= 139, "the drawn text fits 139 px (6 px per character in this renderer): " + shown);
    hud.post_status("Ouch!", true);
    check(status_after_render(hud, world) == "Ouch!", "step 0 of a flash is visible");
    run(hud, world, 1);
    check(status_after_render(hud, world).empty(), "step 1 is hidden");
    run(hud, world, 1);
    check(status_after_render(hud, world) == "Ouch!", "step 2 is visible");
}

// ------------------------------------------------------------------------------------------------
// Part C: selection texts
// ------------------------------------------------------------------------------------------------

void test_selection_texts() {
    g_group = "select";
    std::printf("[status] one own ant of each type posts 6..11, several ants 12, every deselect clears\n");
    sim::SimulationEngine sim;
    make_world(sim);
    HUD hud;
    hud.init(0);
    const sim::AntType types[6] = {sim::AntType::Worker, sim::AntType::Bomber, sim::AntType::Fire,
                                   sim::AntType::Thief, sim::AntType::Combat, sim::AntType::Swimmer};
    const char* expect[6] = {"Ready!", "BomberAnt selected.", "Where to?", "Thief here", "Yessir!", "SwimmerAnt selected."};
    uint32_t ids[6];
    for (int i = 0; i < 6; ++i) ids[i] = sim.spawn_unit(0, types[i], sim::TileCoord{10 + 3 * i, 10});
    const uint32_t foe = sim.spawn_unit(1, sim::AntType::Worker, sim::TileCoord{30, 10});
    const sim::WorldState& world = sim.get_world_state();
    for (int i = 0; i < 6; ++i) {
        hud.select_ant(ids[i]);
        run(hud, world, 1);
        check(status_after_render(hud, world) == expect[i], std::string("selecting the ") + expect[i] + " ant");
    }
    // the text of a selection lives 5 s
    run(hud, world, 99);
    check(!status_after_render(hud, world).empty(), "still there 4950 ms after the selection");
    run(hud, world, 1);
    check(status_after_render(hud, world).empty(), "gone after 5000 ms");

    // a marquee over two own ants: "Ready!" (string 12)
    hud.clear_selection();
    run(hud, world, 1);
    hud.select_ants_in_rect(10 * 32, 10 * 32 - 20, 14 * 32, 10 * 32 + 16, world, false);
    run(hud, world, 1);
    check(hud.get_selected_ant_ids().size() >= 2, "the marquee took two ants");
    check(status_after_render(hud, world) == "Ready!", "more than one ant: string 12");

    // every deselect clears: empty ground, an enemy ant, the own hill
    hud.post_status("old text");
    hud.clear_selection();
    run(hud, world, 1);
    check(status_after_render(hud, world).empty(), "a click on empty ground clears the old text");
    hud.select_ant(ids[0]);
    run(hud, world, 1);
    check(status_after_render(hud, world) == "Ready!", "worker selected");
    hud.select_ant(foe);
    run(hud, world, 1);
    check(status_after_render(hud, world).empty(), "selecting an enemy ant clears the text and posts none");
    hud.select_ant(ids[0]);
    run(hud, world, 1);
    hud.select_base(0);
    run(hud, world, 1);
    check(status_after_render(hud, world).empty(), "selecting the own hill clears the text");
}

void test_type_change_text() {
    g_group = "select";
    std::printf("[status] a selected ant that takes a power-up rebuilds the panel: the text of its new type (string 12 for a group)\n");
    sim::SimulationEngine sim;
    make_world(sim);
    HUD hud;
    hud.init(0);
    const uint32_t w = sim.spawn_unit(0, sim::AntType::Worker, sim::TileCoord{10, 10});
    sim.grid_mut().place_powerup(12, 10, 1);          // a bomber power-up
    hud.select_ant(w);
    run(hud, sim.get_world_state(), 101);
    check(status_after_render(hud, sim.get_world_state()).empty(), "the selection text has expired");
    sim.issue_move_order(w, sim::TileCoord{12, 10});
    std::string seen;
    for (int t = 0; t < 120 && seen.empty(); ++t) {
        sim.tick();
        hud.update(sim.get_world_state(), 1);
        seen = hud.status_line().text();
    }
    check(seen == "BomberAnt selected.", "the type change posts the text of the new type: " + seen);

    // a group: string 12
    sim::SimulationEngine sim2;
    make_world(sim2);
    HUD hud2;
    hud2.init(0);
    const uint32_t a = sim2.spawn_unit(0, sim::AntType::Worker, sim::TileCoord{10, 10});
    const uint32_t b = sim2.spawn_unit(0, sim::AntType::Worker, sim::TileCoord{10, 13});
    sim2.grid_mut().place_powerup(12, 10, 2);         // a fire ant power-up
    hud2.select_ants_in_rect(10 * 32 - 10, 10 * 32 - 20, 11 * 32, 13 * 32 + 16, sim2.get_world_state(), false);
    (void)b;
    run(hud2, sim2.get_world_state(), 101);
    sim2.issue_move_order(a, sim::TileCoord{12, 10});
    std::string seen2;
    for (int t = 0; t < 120 && seen2.empty(); ++t) {
        sim2.tick();
        hud2.update(sim2.get_world_state(), 1);
        seen2 = hud2.status_line().text();
    }
    check(seen2 == "Ready!", "a group's panel is rebuilt with string 12: " + seen2);
}

void test_quiet_paths() {
    g_group = "select";
    std::printf("[status] shift paths post the panel text (FUN_01027f07 gets 0 as its last argument at 0x1027950 / 0x1027aae / 0x1027746): group = string 12, one ant = its type, none clears\n");
    sim::SimulationEngine sim;
    make_world(sim);
    HUD hud;
    hud.init(0);
    const uint32_t a = sim.spawn_unit(0, sim::AntType::Worker, sim::TileCoord{10, 10});
    const uint32_t b = sim.spawn_unit(0, sim::AntType::Bomber, sim::TileCoord{12, 10});
    const sim::WorldState& world = sim.get_world_state();
    hud.select_ant(a);
    run(hud, world, 1);
    check(status_after_render(hud, world) == "Ready!", "first ant");
    hud.post_status("Bomb dropped.");
    // adding with shift rebuilds the panel: several ants give string 12 (the old line is replaced)
    hud.select_ants_in_rect(12 * 32 - 10, 10 * 32 - 20, 12 * 32 + 10, 10 * 32 + 16, world, true);
    run(hud, world, 1);
    check(status_after_render(hud, world) == "Ready!" && hud.get_selected_ant_ids().size() == 2, "a shift marquee that adds an ant posts string 12");

    // shift clicks toggle: an ant leaves the selection (FUN_01027aae), the panel is rebuilt from what is left
    ViewportCamera camera;
    camera.world_x = 0;
    camera.world_y = 0;
    auto click_ant = [&](uint32_t id) {
        for (const auto& ant : world.ants) {
            if (ant.id == id) hud.pointer_click(sim, camera, HUD::PLAYFIELD_X + ant.px, HUD::PLAYFIELD_Y + ant.py - 8, true);
        }
        run(hud, world, 1);
    };
    click_ant(a);                                                        // the worker leaves: the bomber is alone (panel 3), the text of its type
    check(hud.get_selected_ant_ids().size() == 1 && hud.is_ant_selected(b), "shift on a selected ant removes it");
    check(status_after_render(hud, world) == "BomberAnt selected.", "one ant left: the text of its type (it kept the old text before)");
    click_ant(a);                                                        // joins again: two ants, string 12
    check(hud.get_selected_ant_ids().size() == 2, "shift on an unselected own ant adds it");
    check(status_after_render(hud, world) == "Ready!", "a shift click that adds posts string 12");
    click_ant(a);
    click_ant(b);                                                        // the last ant leaves: nothing selected, the line is cleared
    check(hud.get_selected_ant_ids().empty(), "shift on the last selected ant leaves nothing selected");
    check(status_after_render(hud, world).empty(), "nothing selected: the status line is cleared");
}

// ------------------------------------------------------------------------------------------------
// Part D: ordering commands
// ------------------------------------------------------------------------------------------------

struct Sfx {
    std::vector<uint32_t> ids;
    void attach(HUD& hud) { hud.set_on_play_sfx([this](uint32_t id) { ids.push_back(id); }); }
    bool has(uint32_t id) const { for (uint32_t x : ids) if (x == id) return true; return false; }
    // The voices of an order: everything but the pedestal click (89) that follows an accepted order with an unlatched pedestal (BTNPUSH)
    std::vector<uint32_t> voices() const {
        std::vector<uint32_t> v;
        for (uint32_t x : ids) if (x != sim::SoundID::NavButtonClick) v.push_back(x);
        return v;
    }
};

// A level whose block 3 names a default ant type (Ants.exe FUN_01007025): every worker of it is that type's ant for the panel, the pedestal and the voices (the selection type
// FUN_010282e0 and the four voice pickers ask the getter FUN_0100f9cb), but its pick box is that of a worker, because FUN_01026a39 reads the own type field (+0x54): only an ant that
// took the Combat power-up has the larger box.
void test_default_type_texts() {
    g_group = "default type";
    std::printf("[status] a worker of a default-type level: the panel text and the go voice of its type, the pick box of a worker\n");
    const uint16_t tiles[5] = {62, 63, 64, 65, 66};                          // Combat, Thief, Bomber, Swimmer, Fire
    const char* select_text[5] = {"Yessir!", "Thief here", "BomberAnt selected.", "SwimmerAnt selected.", "Where to?"};
    const char* go_text[5] = {"Movin' out.", "Here I go...", "On my way.", "On my way.", "On my way."};
    const uint32_t go_lo[5] = {28, 19, 37, 33, 23};                          // combgo1 / combgo2, the thief, bomber, swimmer and fire ant voices (as in test_move_acknowledgement)
    const uint32_t go_hi[5] = {29, 19, 37, 33, 23};
    for (int i = 0; i < 5; ++i) {
        sim::SimulationEngine sim;
        make_world(sim);
        sim.grid_mut().set_default_ant_tile(tiles[i]);
        HUD hud;
        Sfx sfx;
        hud.init(0);
        sfx.attach(hud);
        const uint32_t ant = sim.spawn_unit(0, sim::AntType::Worker, sim::TileCoord{10, 10});
        hud.select_ant(ant);
        run(hud, sim.get_world_state(), 1);
        check(status_after_render(hud, sim.get_world_state()) == select_text[i], std::string("selecting a worker of the level whose default is ") + select_text[i]);
        sfx.ids.clear();
        hud.dispatch_move_order(20, 10, sim);
        check(status_after_render(hud, sim.get_world_state()) == go_text[i], std::string("the move order of that worker: ") + go_text[i]);
        check(!sfx.ids.empty() && sfx.ids[0] >= go_lo[i] && sfx.ids[0] <= go_hi[i], "the go voice of the default type");
    }
    // the pick box: a marquee that reaches only the extra margin of a combat ant's box (its own type field is Combat) takes that ant, but not a worker of a default Combat level
    for (int own = 0; own < 2; ++own) {
        sim::SimulationEngine sim;
        make_world(sim);
        if (own == 0) sim.grid_mut().set_default_ant_tile(62);
        HUD hud;
        hud.init(0);
        const uint32_t ant = sim.spawn_unit(0, own == 1 ? sim::AntType::Combat : sim::AntType::Worker, sim::TileCoord{10, 10});
        const sim::WorldState& world = sim.get_world_state();
        hud.clear_selection();
        run(hud, world, 1);
        hud.select_ants_in_rect(306, 331, 312, 341, world, false);          // x 306 .. 312: inside the combat box (left edge 304), outside the worker's (316)
        check(hud.is_ant_selected(ant) == (own == 1), own == 1 ? "a combat ant's wider box takes the marquee" : "the worker of a default Combat level has a worker's box");
        check((hud.pick_ant_at(world, 308, 336) != nullptr) == (own == 1), own == 1 ? "a click in a combat ant's extra margin hits it" : "a click in that margin misses the worker of a default Combat level");
        check(hud.pick_ant_at(world, 336, 336) != nullptr, "a click on the ant hits it either way");
    }
}

void test_move_acknowledgement() {
    g_group = "orders";
    std::printf("[status] a move order: the closest ant answers with its go voice and \"On my way.\" / \"Movin' out.\" / \"Here I go...\"\n");
    const sim::AntType types[6] = {sim::AntType::Worker, sim::AntType::Bomber, sim::AntType::Fire,
                                   sim::AntType::Thief, sim::AntType::Combat, sim::AntType::Swimmer};
    const char* text[6] = {"On my way.", "On my way.", "On my way.", "Here I go...", "Movin' out.", "On my way."};
    for (int i = 0; i < 6; ++i) {
        sim::SimulationEngine sim;
        make_world(sim);
        HUD hud;
        Sfx sfx;
        hud.init(0);
        sfx.attach(hud);
        const uint32_t ant = sim.spawn_unit(0, types[i], sim::TileCoord{10, 10});
        hud.select_ant(ant);
        run(hud, sim.get_world_state(), 1);
        sfx.ids.clear();
        hud.dispatch_move_order(20, 10, sim);
        check(status_after_render(hud, sim.get_world_state()) == text[i], std::string("move order of a ") + text[i]);
        check(sfx.voices().size() == 1, "one voice");
        check(!sfx.ids.empty() && sfx.ids.back() == sim::SoundID::NavButtonClick, "and the click of the flashing move pedestal after it");
        uint32_t expect_lo = 0;
        uint32_t expect_hi = 0;
        switch (types[i]) {
            case sim::AntType::Worker:  expect_lo = 15; expect_hi = 17; break;   // gantcommand / gantgo
            case sim::AntType::Bomber:  expect_lo = expect_hi = 37; break;
            case sim::AntType::Fire:    expect_lo = expect_hi = 23; break;
            case sim::AntType::Thief:   expect_lo = expect_hi = 19; break;
            case sim::AntType::Combat:  expect_lo = 28; expect_hi = 29; break;   // combgo1 / combgo2
            case sim::AntType::Swimmer: expect_lo = expect_hi = 33; break;
        }
        check(!sfx.ids.empty() && sfx.ids[0] >= expect_lo && sfx.ids[0] <= expect_hi, "the go voice of the type (first, the click follows)");
    }
}

void test_attack_and_special() {
    g_group = "orders";
    std::printf("[status] attack: \"Attack!\" with the attack voice; special orders: thief 69, fire ant 71, bomber voice only, several ants silent\n");
    {
        sim::SimulationEngine sim;
        make_world(sim);
        HUD hud;
        Sfx sfx;
        hud.init(0);
        sfx.attach(hud);
        const uint32_t combat = sim.spawn_unit(0, sim::AntType::Combat, sim::TileCoord{10, 10});
        const uint32_t foe = sim.spawn_unit(1, sim::AntType::Worker, sim::TileCoord{16, 10});
        hud.select_ant(combat);
        run(hud, sim.get_world_state(), 1);
        sfx.ids.clear();
        hud.dispatch_attack_order(foe, sim);
        check(status_after_render(hud, sim.get_world_state()) == "Attack!", "text 67");
        check(sfx.voices().size() == 1 && (sfx.voices()[0] == 59 || sfx.voices()[0] == 60), "the combat ant's attack voice (combat1 / combat2)");
    }
    {
        // a single fire ant: click on the ground = ignite: "Burn..." and firedo
        sim::SimulationEngine sim;
        make_world(sim);
        HUD hud;
        Sfx sfx;
        hud.init(0);
        sfx.attach(hud);
        const uint32_t fire = sim.spawn_unit(0, sim::AntType::Fire, sim::TileCoord{10, 10});
        hud.select_ant(fire);
        run(hud, sim.get_world_state(), 1);
        sfx.ids.clear();
        hud.order_selected(sim, sim::TileCoord{14, 10}, true, false);
        check(status_after_render(hud, sim.get_world_state()) == "Burn...", "text 71");
        check(sfx.voices().size() == 1 && sfx.voices()[0] == 25, "firedo.wav");
    }
    {
        // two fire ants: the same order is silent
        sim::SimulationEngine sim;
        make_world(sim);
        HUD hud;
        Sfx sfx;
        hud.init(0);
        sfx.attach(hud);
        const uint32_t f1 = sim.spawn_unit(0, sim::AntType::Fire, sim::TileCoord{10, 10});
        const uint32_t f2 = sim.spawn_unit(0, sim::AntType::Fire, sim::TileCoord{10, 12});
        hud.set_selected_ant_ids({f1, f2});
        hud.select_ants_in_rect(10 * 32 - 10, 10 * 32 - 20, 11 * 32, 12 * 32 + 16, sim.get_world_state(), false);
        run(hud, sim.get_world_state(), 101);
        sfx.ids.clear();
        hud.order_selected(sim, sim::TileCoord{14, 10}, true, false);
        check(status_after_render(hud, sim.get_world_state()).empty(), "no text for a special order given to two ants");
        check(sfx.voices().empty(), "and no voice (the pedestal still clicks: the order needed an ant)");
        check(sfx.has(sim::SoundID::NavButtonClick), "the click of the flashing ability pedestal");
    }
    {
        // a single bomber: plants a bomb: bombdo.wav and no text
        sim::SimulationEngine sim;
        make_world(sim);
        HUD hud;
        Sfx sfx;
        hud.init(0);
        sfx.attach(hud);
        const uint32_t bomber = sim.spawn_unit(0, sim::AntType::Bomber, sim::TileCoord{10, 10});
        hud.select_ant(bomber);
        run(hud, sim.get_world_state(), 101);
        sfx.ids.clear();
        hud.order_selected(sim, sim::TileCoord{14, 10}, true, false);
        check(status_after_render(hud, sim.get_world_state()).empty(), "no text for the bomber's special order");
        check(sfx.voices().size() == 1 && sfx.voices()[0] == 39, "bombdo.wav");
    }
    {
        // a single thief ordered onto an enemy hill: "My pleasure..." and theifdo
        sim::SimulationEngine sim;
        make_world(sim);
        HUD hud;
        Sfx sfx;
        hud.init(0);
        sfx.attach(hud);
        const uint32_t thief = sim.spawn_unit(0, sim::AntType::Thief, sim::TileCoord{30, 30});
        hud.select_ant(thief);
        run(hud, sim.get_world_state(), 101);
        sfx.ids.clear();
        hud.order_selected(sim, sim::TileCoord{41, 41}, true, false);
        check(status_after_render(hud, sim.get_world_state()) == "My pleasure...", "text 69");
        check(sfx.voices().size() == 1 && sfx.voices()[0] == 21, "theifdo.wav");
    }
}

void test_stop() {
    g_group = "orders";
    std::printf("[status] the Stop button always posts \"Stopping.\"\n");
    sim::SimulationEngine sim;
    make_world(sim);
    HUD hud;
    Sfx sfx;
    hud.init(0);
    sfx.attach(hud);
    const uint32_t ant = sim.spawn_unit(0, sim::AntType::Worker, sim::TileCoord{10, 10});
    hud.select_ant(ant);
    run(hud, sim.get_world_state(), 101);
    ViewportCamera camera;
    hud.handle_mouse_down(600, 200, SDL_BUTTON_LEFT, sim, camera);
    check(status_after_render(hud, sim.get_world_state()) == "Stopping.", "text 54");
}

// ------------------------------------------------------------------------------------------------
// Part E: world messages
// ------------------------------------------------------------------------------------------------

void test_world_message_flags() {
    g_group = "world";
    std::printf("[status] the flash flag belongs to 17, 49, 50, 53, 59 and 75; every other status is steady\n");
    for (uint16_t id = 1; id < 120; ++id) {
        const bool expect = (id == 17 || id == 49 || id == 50 || id == 53 || id == 59 || id == 75);
        check(sim::strings::blinks(id) == expect, "flash flag of string " + std::to_string(id));
    }
    check(std::string(sim::strings::text(60)) == "Got Food!", "string 60");
    check(std::string(sim::strings::text(53)) == "A ThiefAnt is at your anthill!", "string 53");
    check(sim::strings::format(5, "Ants") == "Welcome to Ants!", "string 5 with the title");
    check(sim::strings::format(39, "Ann", "Blue", "Bob", "Red") == "Ann (Blue) and Bob (Red) are a team now!", "string 39 with four arguments");
    check(std::string(sim::strings::colour_name(0)) == "Black" && std::string(sim::strings::colour_name(3)) == "Green", "colour names");
}

void test_events_reach_the_status_line() {
    g_group = "world";
    std::printf("[status] a world message reaches the local player's status line with its flash flag, others' do not\n");
    sim::SimulationEngine sim;
    make_world(sim);
    HUD hud;
    hud.init(0);
    run(hud, sim.get_world_state(), 101);
    // a worker of player 0 carrying food is ordered onto food: "Can't - already have food." flashes
    const uint32_t w0 = sim.spawn_unit(0, sim::AntType::Worker, sim::TileCoord{10, 10});
    sim::AntUnit& a = sim.get_unit(w0);
    a.holding = 1;
    a.carried_food = 1;
    a.carried_points = 25;
    sim::FoodObject o;
    o.row = 10;
    o.col = 13;
    o.units = 2;
    o.value = 25;
    o.remaining = 2;
    o.thresholds = {2, 0};
    o.stage_tiles = {372, 0x7FFE};
    sim.grid_mut().add_food_object(std::move(o));
    sim.clear_news_events();
    sim.issue_move_order(w0, sim::TileCoord{13, 10});
    bool seen = false;
    for (int t = 0; t < 200 && !seen; ++t) {
        sim.tick();
        hud.poll_sim_events(sim);
        hud.update(sim.get_world_state(), 1);
        seen = hud.status_line().text() == "Can't - already have food.";
    }
    check(seen, "text 17 reached the status line");
    check(hud.status_line().flashing() || hud.status_line().age_ticks() > 0, "posted with the flash flag");

    // the same message of player 1's ant is not shown to player 0
    HUD hud2;
    hud2.init(0);
    run(hud2, sim.get_world_state(), 101);
    sim::SimulationEngine sim2;
    make_world(sim2);
    const uint32_t w1 = sim2.spawn_unit(1, sim::AntType::Worker, sim::TileCoord{10, 10});
    sim::AntUnit& b = sim2.get_unit(w1);
    b.holding = 1;
    b.carried_food = 1;
    b.carried_points = 25;
    sim::FoodObject o2;
    o2.row = 10;
    o2.col = 13;
    o2.units = 2;
    o2.value = 25;
    o2.remaining = 2;
    o2.thresholds = {2, 0};
    o2.stage_tiles = {372, 0x7FFE};
    sim2.grid_mut().add_food_object(std::move(o2));
    sim2.issue_move_order(w1, sim::TileCoord{13, 10});
    for (int t = 0; t < 200; ++t) {
        sim2.tick();
        hud2.poll_sim_events(sim2);
        hud2.update(sim2.get_world_state(), 1);
    }
    check(hud2.status_line().text().empty(), "player 1's message does not reach player 0");
}


void test_hatch_texts() {
    g_group = "world";
    std::printf("[status] hatching: 16 without eggs, 14 while one hatches, 13 (with the canthatch cue) below 200 points, 15 when accepted, 63 at the emergence\n");
    sim::SimulationEngine sim;
    make_world(sim);
    sim.set_player_eggs(0, 0);
    sim.set_player_score(0, 500);
    auto texts = [&](std::vector<uint16_t>& ids) {
        ids.clear();
        for (const auto& n : sim.poll_news_events()) if (n.target_player == 0) ids.push_back(n.string_id);
    };
    std::vector<uint16_t> ids;
    sim.clear_news_events();
    check(sim.try_hatch(0, sim::AntType::Worker) == sim::SimulationEngine::HatchResult::NoEggs, "no eggs");
    texts(ids);
    check(ids.size() == 1 && ids[0] == 16, "text 16 \"No eggs to hatch!\" (checked first)");

    sim.set_player_eggs(0, 3);
    sim.set_player_score(0, 100);
    sim.clear_audio_events();
    check(sim.try_hatch(0, sim::AntType::Worker) == sim::SimulationEngine::HatchResult::NotEnoughPoints, "not enough points");
    texts(ids);
    check(ids.size() == 1 && ids[0] == 13, "text 13 \"You need 200 points to hatch!\"");
    check(sim.has_targeted_audio_event(0, sim::SoundID::AntStop), "13 plays the canthatch cue (antstop.wav, 61)");

    sim.set_player_score(0, 500);
    check(sim.try_hatch(0, sim::AntType::Worker) == sim::SimulationEngine::HatchResult::Started, "accepted");
    texts(ids);
    check(ids.size() == 1 && ids[0] == 15, "text 15 \"Hatching a new Ant!\"");
    check(sim.get_player_score(0) == 300, "the cost is 200 points");
    check(sim.try_hatch(0, sim::AntType::Worker) == sim::SimulationEngine::HatchResult::AlreadyHatching, "one at a time");
    texts(ids);
    check(ids.size() == 1 && ids[0] == 14, "text 14 \"An Ant is already hatching!\"");

    // the newborn appears 8000 ms after the click: text 63 and the exithill cue
    sim.clear_audio_events();
    bool ready = false;
    int ms = 0;
    for (; ms < 9000 && !ready; ms += 50) {
        sim.tick();
        for (const auto& n : sim.poll_news_events()) if (n.target_player == 0 && n.string_id == 63) ready = true;
    }
    check(ready, "text 63 \"Ready!\" when the ant emerges");
    check(ms >= 8000 && ms <= 8200, "8000 ms after the click");
    check(sim.has_audio_event(sim::SoundID::ExitHill), "exithill.wav");
}


// ------------------------------------------------------------------------------------------------
// Part F: alliance texts, News Flash lines and the chat log
// ------------------------------------------------------------------------------------------------

struct Collected {
    std::vector<sim::NewsEvent> news;
    std::vector<sim::AudioEvent> audio;
};

Collected drain(sim::SimulationEngine& sim) {
    Collected c;
    c.news = sim.poll_news_events();
    c.audio = sim.poll_audio_events();
    return c;
}

const sim::NewsEvent* find_news(const Collected& c, uint16_t id, uint8_t target = 255) {
    for (const auto& n : c.news) if (n.string_id == id && n.target_player == target) return &n;
    return nullptr;
}

bool has_audio(const Collected& c, uint32_t id, uint8_t target) {
    for (const auto& a : c.audio) if (a.sound_id == id && a.target_player == target) return true;
    return false;
}

void test_alliance_texts() {
    g_group = "alliance";
    std::printf("[chat] alliance: invitation dialog, 81 / 80 / 82 for the proposer and invitee, 75 flashing, News Flash lines 39 / 40 and the cues\n");
    sim::SimulationEngine sim;
    make_world(sim);
    sim.set_player_name(0, "Ann");
    sim.set_player_name(1, "Bob");
    check(sim.get_player_name(2) == "Blue" && sim.get_player_name(3) == "Black", "unnamed players print their colour");

    // propose: the invitee gets the modal question (string 1) and the allypro cue, nobody gets a status
    sim.clear_news_events();
    sim.clear_audio_events();
    sim.propose_alliance(0, 1);
    Collected c = drain(sim);
    const sim::NewsEvent* q = find_news(c, 1, 1);
    check(q && q->channel == sim::NewsChannel::Dialog, "the invitation is a dialog for the invitee");
    check(q && q->message_text == "Ann (Green) and Bob (Red) are a team now!" ? false : true, "");
    check(q && q->message_text == "Ann (Green) invites you to form a team.  Would you like to accept?", "text of string 1: " + (q ? q->message_text : std::string("none")));
    check(has_audio(c, sim::SoundID::AlliancePro, 1), "allypro.wav (51) for the invitee");

    // withdraw: the invitee reads string 82
    sim.propose_alliance(0, 1);
    sim.clear_news_events();
    sim.withdraw_alliance_offer(0, 1);
    c = drain(sim);
    const sim::NewsEvent* w = find_news(c, 82, 1);
    check(w && w->message_text == "Ann withdrew offer to team up" && !w->blink, "82 to the invitee: " + (w ? w->message_text : std::string("none")));

    // accept: 81 to the proposer, cue allyon for everybody, News Flash 39 in the chat log, 75 flashing for everybody
    sim.propose_alliance(0, 1);
    sim.clear_news_events();
    sim.clear_audio_events();
    sim.accept_alliance(1, 0);
    c = drain(sim);
    const sim::NewsEvent* acc = find_news(c, 81, 0);
    check(acc && acc->message_text == "Bob accepted teaming up" && acc->channel == sim::NewsChannel::Status, "81 to the proposer: " + (acc ? acc->message_text : std::string("none")));
    const sim::NewsEvent* flash = find_news(c, 39);
    check(flash && flash->channel == sim::NewsChannel::ChatLog && flash->message_text == "Ann (Green) and Bob (Red) are a team now!",
          "News Flash 39: " + (flash ? flash->message_text : std::string("none")));
    const sim::NewsEvent* made = find_news(c, 75);
    check(made && made->message_text == "A team has been made." && made->blink && made->channel == sim::NewsChannel::Status, "75 flashes for everybody");
    check(has_audio(c, sim::SoundID::AllianceOn, 255), "allyon.wav (50) for everybody");
    check(has_audio(c, sim::SoundID::AllianceYes, 1), "allyyes.wav (53) is the answering player's own cue");

    // break: allyoff always, the News Flash 40 with the breaker and its old ally
    sim.clear_news_events();
    sim.clear_audio_events();
    sim.break_alliance(0);
    c = drain(sim);
    const sim::NewsEvent* off = find_news(c, 40);
    check(off && off->channel == sim::NewsChannel::ChatLog && off->message_text == "Ann (Green) and Bob (Red) are no longer a team!",
          "News Flash 40: " + (off ? off->message_text : std::string("none")));
    check(has_audio(c, sim::SoundID::AllianceBreak, 255), "allyoff.wav (49)");
    sim.clear_news_events();
    sim.clear_audio_events();
    sim.break_alliance(0);                                  // no ally any more: the cue plays, no line
    c = drain(sim);
    check(find_news(c, 40) == nullptr, "no line without an old ally");
    check(has_audio(c, sim::SoundID::AllianceBreak, 255), "but the allyoff cue plays");

    // deny: 80 to the proposer with allynot
    sim.propose_alliance(2, 3);
    sim.clear_news_events();
    sim.clear_audio_events();
    sim.deny_alliance(3, 2);
    c = drain(sim);
    const sim::NewsEvent* den = find_news(c, 80, 2);
    check(den && den->message_text == "Black rejected teaming up", "80 to the proposer: " + (den ? den->message_text : std::string("none")));
    check(has_audio(c, sim::SoundID::AllianceNot, 2), "allynot.wav (52) for the proposer");
    check(has_audio(c, sim::SoundID::AllianceNot, 3), "allynot.wav (52) for the decliner too (0x1023c53)");

    // an invitee that already has a team is told what accepting costs (string 2)
    sim.form_alliance(0, 1);
    sim.clear_news_events();
    sim.propose_alliance(2, 0);
    c = drain(sim);
    const sim::NewsEvent* q2 = find_news(c, 2, 0);
    check(q2 && q2->message_text.find("This will remove you from the team you have with Bob (Red)") != std::string::npos, "string 2 names the team that would end");

    // a drop-out is a News Flash line (the sound is covered by test 12.68)
    sim.clear_news_events();
    sim.trigger_player_dropout(2);
    c = drain(sim);
    const sim::NewsEvent* drop = find_news(c, 46);
    check(drop && drop->channel == sim::NewsChannel::ChatLog && drop->message_text == "Blue dropped out of the game!", "46: " + (drop ? drop->message_text : std::string("none")));
}

void test_chat_log_format() {
    g_group = "chat";
    std::printf("[chat] the log: News Flash headers, team-coloured names, bodies wrapped and indented, limits\n");
    HUD hud;
    hud.init(0);
    const auto& log = hud.get_chat_log();
    check(log.size() >= 2 && log[0] == "[0:00] News Flash:", "the start message is a News Flash entry: header \"[0:00] News Flash:\"");
    check(hud.get_chat_line_colour(0) == 4, "news flash header colour (79, 0, 143)");
    check(log.size() >= 2 && log[1] == "Game started! Go get" && hud.get_chat_line_colour(1) == 5, "the body wraps at 21 characters: " + (log.size() > 1 ? log[1] : std::string()));

    hud.add_news_flash(75000, "x");
    check(log[log.size() - 2] == "[1:15] News Flash:", "header time is the match time played");

    hud.set_player_name("Ann");
    const size_t before = log.size();
    hud.add_chat_entry("Ann", "hello there", false, 2);
    check(log.size() == before + 2 && log[before] == "Ann:" && log[before + 1] == "hello there", "header line and body line");
    check(hud.get_chat_line_colour(before) == 2 && hud.get_chat_line_colour(before + 1) == 5, "the header takes the team colour, the body (7, 11, 15)");
    hud.add_chat_entry("Ann", "to my friend", true, 3);
    check(log[log.size() - 2] == "Ann (To Teammate):", "team text: \"(To Teammate):\"");

    // a body holds at most 100 characters
    const size_t b2 = log.size();
    hud.add_chat_entry("Ann", std::string(150, 'a') + " tail", false, 0);
    size_t chars = 0;
    for (size_t i = b2 + 1; i < log.size(); ++i) chars += log[i].size();
    check(chars <= 100, "a body is cut at 100 characters");

    // the input box holds 100 characters (the chat box is always active: no focus needed)
    hud.handle_text_input(std::string(130, 'b'));
    check(hud.get_chat_input().size() == 100, "the input box holds 100 characters");
}

void test_chat_rendering() {
    g_group = "chat";
    std::printf("[chat] the log is drawn from (482, 299): headers in team colours, bodies in (7, 11, 15) 10 px to the right, one pixel between the entries\n");
    sim::SimulationEngine sim;
    make_world(sim);
    HUD hud;
    hud.init(0);
    hud.add_chat_entry("Ann", "hi", false, 2);
    TextRenderer rr;
    ViewportCamera camera;
    hud.render(rr, *g_archive, sim.get_world_state(), camera);
    bool header = false, body = false, news = false;
    for (const auto& t : rr.texts) {
        if (t.text == "Ann:" && t.x == 482 && t.color.r == 119 && t.color.g == 0 && t.color.b == 0) header = true;
        if (t.text == "hi" && t.x == 492 && t.color.r == 7 && t.color.g == 11 && t.color.b == 15) body = true;
        if (t.text == "[0:00] News Flash:" && t.x == 482 && t.color.r == 79 && t.color.g == 0 && t.color.b == 143) news = true;
    }
    check(header, "header at x = 482 in the red team colour (119, 0, 0)");
    check(body, "body at x = 482 + 10 in (7, 11, 15) (MoveTo(body, 10, y), 0x1012607)");
    check(news, "the news flash header in (79, 0, 143)");
    // The entries are stacked from y = 299: the news flash is a 12 px header and a body of two 12 px lines (36 px), the next entry starts one pixel below it (37 px)
    int32_t y_news = -1, y_header = -1, y_body = -1;
    for (const auto& t : rr.texts) {
        if (t.text == "[0:00] News Flash:") y_news = t.y;
        if (t.text == "Ann:") y_header = t.y;
        if (t.text == "hi") y_body = t.y;
    }
    check(y_news == 299, "the first header is at y = 299");
    check(y_header == 299 + 37, "the next entry starts one pixel below the news flash (36 px + 1)");
    check(y_body == y_header + 12, "the body is one header line below its header");
}

void test_chat_gate_and_filter() {
    g_group = "chat";
    std::printf("[chat] the option \"Participate In Chat\" gates sending and receiving; team text reaches the sender and the sender's allies only; F9 - F12 go to all\n");
    sim::SimulationEngine sim;
    make_world(sim);
    const sim::WorldState& world = sim.get_world_state();
    HUD hud;
    hud.init(0);
    const auto& log = hud.get_chat_log();

    check(std::string(hud.get_quick_chat_key(0)) == "Now you are in for it!" && std::string(hud.get_quick_chat_key(3)) == "Do you want to ally?", "the quick chat defaults are strings 18 - 21");
    size_t before = log.size();
    hud.trigger_quick_chat(2);
    check(log.size() == before + 2 && log[before + 1] == "Attack!", "F11 chats the default text");
    check(log[before].find("(To Teammate)") == std::string::npos, "quick chat always goes to all");

    // receive: team text from a stranger is dropped, from an ally shown
    before = log.size();
    hud.receive_chat_message(1, "Bob", "psst", true, world);
    check(log.size() == before, "a team message of a player who is not my ally is not shown");
    hud.receive_chat_message(1, "Bob", "for everybody", false, world);
    check(log.size() == before + 2 && log[before] == "Bob:", "text to all is shown");

    sim.form_alliance(0, 1);
    const sim::WorldState& world2 = sim.get_world_state();
    hud.receive_chat_message(1, "Bob", "team plan", true, world2);
    check(log[log.size() - 2] == "Bob (To Teammate):" && log[log.size() - 1] == "team plan", "my ally's team text is shown with \"(To Teammate):\"");
    check(hud.get_chat_line_colour(log.size() - 2) == 2, "in the colour of Bob's team (red)");

    // the option off: nothing is sent, nothing is received, quick chat is silent
    ViewportCamera camera;
    hud.open_options();
    hud.handle_mouse_down(151, 289, SDL_BUTTON_LEFT, sim, camera);      // the OFF toggle of the chat option (a switch acts at the release)
    hud.handle_mouse_up(151, 289, SDL_BUTTON_LEFT, sim, camera);
    hud.close_options();
    check(!hud.is_chat_enabled(), "chat switched off");
    before = log.size();
    hud.set_chat_input("hello");
    hud.send_chat_message();
    hud.trigger_quick_chat(0);
    hud.receive_chat_message(1, "Bob", "anyone?", false, world2);
    check(log.size() == before && hud.get_chat_input().empty(), "with the option off nothing is sent or received");
}

}  // namespace

int main(int argc, char* argv[]) {
    // SDL's headers rename main to SDL_main (SDL2main on Windows calls it): the signature must be this one, or the linker finds no SDL_main (the build guard in CMakeLists.txt checks it)
    (void)argc;
    (void)argv;
    assets::AssetArchive arc;
    if (!arc.load_chd(std::string(ORIGINAL_ASSETS_DIR) + "/ants.chd")) {
        std::fprintf(stderr, "cannot load ants.chd\n");
        return 2;
    }
    g_archive = &arc;
    test_slot_replace_and_expiry();
    test_slot_empty_post();
    test_slot_flash();
    test_box_geometry();
    test_box_clip_and_flash();
    test_selection_texts();
    test_default_type_texts();
    test_type_change_text();
    test_quiet_paths();
    test_move_acknowledgement();
    test_attack_and_special();
    test_stop();
    test_world_message_flags();
    test_events_reach_the_status_line();
    test_hatch_texts();
    test_alliance_texts();
    test_chat_log_format();
    test_chat_rendering();
    test_chat_gate_and_filter();
    std::printf("\nstatus messages: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
