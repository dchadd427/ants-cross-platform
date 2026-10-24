// The fixtures that the parts of the pointer model test share (test_pointer_model.cpp holds main(); the cases are in test_pointer_model.cpp and test_pointer_model_big.cpp). Inline, so that
// every part is a self-contained translation unit that counts into the same totals.
#pragma once
#include <cstdint>
#include <cstdio>
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

inline int g_checks = 0;
inline int g_failures = 0;
inline const char* g_group = "";

inline void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::fprintf(stderr, "  FAIL [%s]: %s\n", g_group, what.c_str());
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

void test_big_selection();      // test_pointer_model_big.cpp
