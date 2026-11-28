// Tests of the prediction of one's own orders in the application (include/ants_net/prediction.hpp, docs/NETWORK_PORT.md "Prediction of one's own orders"): WHAT THE MATCH SCREEN READS.
//
// In a match of the network that predicts, the screen shows the PREDICTED engine (Application::view_sim): the picture, the HUD's per-tick step, the cursor and what a click picks come from it, so
// that an order shows at once; the cues, the news, the scores and the end of the match stay the confirmed engine's. The prediction itself (exactness, corrections, derived state) is tested
// by test_prediction and the NetGame cases of test_netgame; here a headless Application plays a real match against a bare machine over the loopback interface and the tests ask what the
// application does with the two engines:
//   - the frame that is drawn is the predicted engine's (the same frame, drawn from the confirmed engine, differs where the order has moved an ant);
//   - a click picks the ant where the predicted engine has it (which is not where the confirmed engine has it);
//   - an order given with the right button is in the predicted engine in the same call and in the confirmed one a few ticks later;
//   - the HUD's per-tick step reads the predicted engine (the alliance that a turn in hand has made shows in the HUD before the confirmed engine has run that turn);
//   - the switch (the user's, the application's) gives the confirmed engine back at once.
// Real sockets on the loopback interface; one Application per test (SDL is initialised once per process).
#include <SDL.h>

#include "slide_cause.hpp"
#include "ants_app/application.hpp"
#include "ants_app/health_changes.hpp"
#include "ants_app/hud.hpp"
#include "ants_app/others_view.hpp"
#include "ants_app/view_glide.hpp"
#include "ants_assets/lvl_parser.hpp"
#include "ants_net/netgame.hpp"
#include "ants_net/protocol.hpp"
#include "ants_sim/command.hpp"
#include "ants_sim/sim_engine.hpp"
#include "ants_test_paths.hpp"
#include "../common/ants_test_pause.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include "../common/test_assert.hpp"

#if defined(_WIN32)
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

using namespace ants;
using namespace ants::app;


static int g_test_count = 0;
static int g_test_failures = 0;
static int g_assert_count = 0;

inline void run_test_case(const std::string& name, const std::function<void()>& fn) {
    // ANTS_TEST_FILTER=text runs only the cases whose title contains the text (for working on one test and for mutation runs; run_tests.sh clears it)
    if (const char* filter = std::getenv("ANTS_TEST_FILTER")) {
        if (name.find(filter) == std::string::npos) return;
    }
    ++g_test_count;
    std::cout << "  RUNNING: " << std::left << std::setw(120) << name << " ... " << std::flush;
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


namespace {

std::string maps_dir() { return std::string(ORIGINAL_ASSETS_DIR) + "/Maps/"; }

// The other machine of a test: a simulation and a NetGame, with the little that the application does for the room (load the map, report)
struct Peer {
    sim::SimulationEngine sim;
    net::NetGame net{sim};
    uint32_t now{1000};

    Peer() {                                                               // (the other machine does not predict: a match in which one machine predicts and the other does not stays identical)
        net.set_discovery(0);                                              // (the tests do not announce on the real network)
        net.set_prediction_enabled(false);
    }
    void update() {
        net.update(now);
        for (const auto& ev : net.take_events()) {
            if (ev.type != net::NetGame::Event::Type::StartRequested) continue;
            const net::StartMsg& s = net.start_info();
            ants::assets::LevelData level;
            uint64_t hash = 0;
            const bool ok = level.load_lvl(maps_dir() + s.map_name) && net::hash_file(maps_dir() + s.map_name, hash) && hash == s.map_hash;
            if (ok) {
                sim.set_fog_of_war_enabled(s.fog);
                sim.set_game_mode(s.game_mode());                         // (the mode of the Start, before the first init: what the application does)
                sim.init(level, s.seed, s.roster);
                sim::apply_start_teams(sim, s.teams());
            }
            net.report_loaded(ok);
        }
    }
};

// Steps the application and the peer together in 10 ms of game time
struct Duo {
    Application& app;
    Peer& peer;
    void step(uint32_t ms) {
        for (uint32_t t = 0; t < ms; t += 10) {
            app.pump_network(0.010f);
            app.update_simulation(0.010f);
            peer.now += 10;
            peer.update();
            ants_test::short_pause();
        }
    }
    bool until(const std::function<bool()>& cond, uint32_t max_ms) {
        for (uint32_t t = 0; t < max_ms; t += 10) {
            if (cond()) return true;
            step(10);
        }
        for (int i = 0; i < 2000 && !cond(); ++i) {                        // (the game clock is virtual but the sockets are real: a late kernel gets real time, the clock standing still)
            app.pump_network(0.0f);
            peer.update();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return cond();
    }
};

ApplicationConfig host_config() {
    ApplicationConfig cfg;
    cfg.headless = true;
    cfg.start_in_map_select = true;
    cfg.lan_port = 0;                                                      // the tests do not announce on the real network
    cfg.net_role = ApplicationConfig::NetRole::Host;
    cfg.net_port = 0;
    cfg.net_loopback_only = true;
    cfg.player_name = "Alice";
    return cfg;
}

// The application hosts a room with Bob in it, SMALL.LVL, START: both play, the application is seat 0 and Bob seat 1. Returns when the match screen is up on both.
bool begin_match(Application& app, Peer& bob, Duo& duo, const ApplicationConfig& config = host_config(), bool fog = false) {
    if (!app.init(config) || !app.network_active()) return false;
    if (!bob.net.join("127.0.0.1", app.net()->listen_port(), "Bob")) return false;
    if (!duo.until([&]() { return app.net()->can_start(); }, 8000)) return false;
    if (fog) app.net()->set_fog(true);
    int32_t small_index = -1;
    for (size_t i = 0; i < app.map_select().get_maps().size(); ++i) {
        if (app.map_select().get_maps()[i].filename == "SMALL.LVL") small_index = static_cast<int32_t>(i);
    }
    if (small_index < 0) return false;
    app.map_select().set_selected_index(small_index);
    duo.step(200);
    app.map_select().handle_key_down(SDLK_RETURN);
    return duo.until([&]() { return app.state() == AppState::Playing && bob.net.phase() == net::NetGame::Phase::Playing; }, 8000);
}

std::vector<uint32_t> ants_of(const sim::SimulationEngine& sim, uint8_t seat) {
    std::vector<uint32_t> out;
    for (const auto& a : sim.get_world_state().ants) {
        if (a.player_id == seat && a.hp > 0) out.push_back(a.id);
    }
    return out;
}

const sim::AntSnapshot* ant_in(const sim::SimulationEngine& sim, uint32_t id) {
    for (const auto& a : sim.get_world_state().ants) {
        if (a.id == id) return &a;
    }
    return nullptr;
}

// An open ground tile 10 tiles and more from a team's hill (a goal that the path finder accepts), or `nearest` to `farthest` tiles from it
bool far_goal(const sim::SimulationEngine& sim, uint8_t team, int16_t& gx, int16_t& gy, int32_t nearest = 10, int32_t farthest = 14) {
    const auto* hill = sim.grid().find_anthill(team);
    if (hill == nullptr) return false;
    for (int32_t d = nearest; d <= farthest; ++d) {
        for (int32_t dy = -d; dy <= d; ++dy) {
            for (int32_t dx = -d; dx <= d; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != d) continue;
                const sim::TileCoord t{static_cast<int32_t>(hill->x) + 2 + dx, static_cast<int32_t>(hill->y) + 2 + dy};
                if (!sim.grid().in_bounds(t) || sim.grid().is_solid_obstacle(t.x, t.y) || sim.grid().is_solid_object(t) ||
                    sim.grid().terrain_class_at(t) == sim::movement::kTerrainWater) {
                    continue;
                }
                gx = static_cast<int16_t>(t.x);
                gy = static_cast<int16_t>(t.y);
                return true;
            }
        }
    }
    return false;
}

void mouse_button(Application& app, uint32_t type, uint8_t button, int32_t x, int32_t y) {
    SDL_MouseButtonEvent b{};
    b.type = type;
    b.button = button;
    b.state = type == SDL_MOUSEBUTTONDOWN ? SDL_PRESSED : SDL_RELEASED;
    b.clicks = 1;
    b.x = x;
    b.y = y;
    app.handle_mouse_button(b);
}

void click(Application& app, uint8_t button, int32_t x, int32_t y) {
    mouse_button(app, SDL_MOUSEBUTTONDOWN, button, x, y);
    mouse_button(app, SDL_MOUSEBUTTONUP, button, x, y);
}

// The camera is put so that world point (wx, wy) is where the picture shows it at (sx, sy) of the playfield: the centre of the view minus the offset
bool place_camera(Application& app, int32_t wx, int32_t wy, int32_t off_x, int32_t off_y) {
    const sim::Grid& grid = app.sim().grid();
    app.renderer().camera().center_on(wx + off_x, wy + off_y, grid.width(), grid.height());
    int32_t sx = 0;
    int32_t sy = 0;
    return app.renderer().camera().world_to_screen(wx, wy, sx, sy);
}

// The pixels of a rectangle of the canvas (the frame that render_frame has just drawn). SDL scales the canvas into the window (a headless window may be a multiple of it), and reads the
// window's pixels: the rectangle is taken in the canvas's own coordinates and read at the scale of the window
std::vector<uint8_t> canvas_region(Application& app, int32_t x, int32_t y, int32_t w, int32_t h) {
    SDL_Renderer* sr = app.renderer().get_sdl_renderer();
    int32_t out_w = 0;
    int32_t out_h = 0;
    if (SDL_GetRendererOutputSize(sr, &out_w, &out_h) != 0 || out_w <= 0) return {};
    const int32_t scale = std::max(1, out_w / std::max(1, app.renderer().canvas_w()));
    SDL_Rect rect{x * scale, y * scale, w * scale, h * scale};
    std::vector<uint8_t> px(static_cast<size_t>(rect.w) * static_cast<size_t>(rect.h) * 4u, 0);
    if (SDL_RenderReadPixels(sr, &rect, SDL_PIXELFORMAT_RGBA32, px.data(), rect.w * 4) != 0) px.clear();
    return px;
}

// The order that sends the player's first ant to the food that stands nearest to its hill (it walks there and harvests: the own-action cues of the cue router)
bool food_order(Application& app, sim::Command& go) {
    const sim::Grid& grid = app.sim().grid();
    const auto* hill = grid.find_anthill(0);
    if (hill == nullptr) return false;
    sim::TileCoord food{-1, -1};
    int32_t best = 1 << 30;
    for (int32_t y = 0; y < static_cast<int32_t>(grid.height()); ++y) {
        for (int32_t x = 0; x < static_cast<int32_t>(grid.width()); ++x) {
            if (grid.food_object_at_cell(sim::TileCoord{x, y}) < 0) continue;
            const int32_t d = std::max(std::abs(x - static_cast<int32_t>(hill->x)), std::abs(y - static_cast<int32_t>(hill->y)));
            if (d < best) {
                best = d;
                food = sim::TileCoord{x, y};
            }
        }
    }
    const std::vector<uint32_t> mine = ants_of(app.sim(), 0);
    if (food.x < 0 || mine.empty()) return false;
    go = sim::Command{};
    go.type = sim::CommandType::GroupMove;
    go.issuer = 0;
    go.tile_x = static_cast<int16_t>(food.x);
    go.tile_y = static_cast<int16_t>(food.y);
    go.ants = {mine[0]};
    return true;
}

// The match is up and the application predicts: the dialog is over, the first turns have run
bool predicting_match(Application& app, Peer& bob, Duo& duo, const ApplicationConfig& config = host_config()) {
    if (!begin_match(app, bob, duo, config)) return false;
    if (!duo.until([&]() { return app.net()->predicting() && app.sim().current_tick() > 40; }, 20000)) return false;
    duo.step(500);
    return app.net()->predicting();
}

// Walks the player's first ant away from the others with an order, until the predicted engine has it at least `gap` pixels from where the confirmed one has it. Returns the ant (0: failed)
uint32_t walk_one_ant_ahead(Application& app, Peer& bob, Duo& duo, int32_t gap) {
    (void)bob;
    const std::vector<uint32_t> mine = ants_of(app.sim(), 0);
    if (mine.empty()) return 0;
    const uint32_t ant = mine[0];
    int16_t gx = 0;
    int16_t gy = 0;
    if (!far_goal(app.sim(), 0, gx, gy)) return 0;
    const sim::AntSnapshot* start = ant_in(app.view_sim(), ant);
    if (start == nullptr) return 0;
    // select the ant with a click on it (the camera is put on it first), then order it with the right button on the open ground (the goal and the ant both in the picture)
    int32_t sx = 0;
    int32_t sy = 0;
    if (!place_camera(app, start->px, start->py, 0, 0)) return 0;
    if (!app.renderer().camera().world_to_screen(start->px, start->py, sx, sy)) return 0;
    click(app, SDL_BUTTON_LEFT, sx, sy);
    if (app.hud().get_selected_ant_id() != ant) return 0;
    const int32_t goal_wx = gx * 32 + 16;
    const int32_t goal_wy = gy * 32 + 16;
    if (!place_camera(app, goal_wx, goal_wy, (start->px - goal_wx) / 2, (start->py - goal_wy) / 2)) return 0;
    int32_t gsx = 0;
    int32_t gsy = 0;
    if (!app.renderer().camera().world_to_screen(goal_wx, goal_wy, gsx, gsy)) return 0;
    click(app, SDL_BUTTON_RIGHT, gsx, gsy);
    const auto separated = [&]() {
        const sim::AntSnapshot* p = ant_in(app.view_sim(), ant);
        const sim::AntSnapshot* c = ant_in(app.sim(), ant);
        if (p == nullptr || c == nullptr) return false;
        return std::max(std::abs(p->px - c->px), std::abs(p->py - c->py)) >= gap;
    };
    return duo.until(separated, 3000) ? ant : 0u;
}


// The ants of two pictures are the same ant: every field that the picture, the pointer and the panel read
bool same_ant(const sim::AntSnapshot& a, const sim::AntSnapshot& b) {
    return a.id == b.id && a.player_id == b.player_id && a.type == b.type && a.raw_type == b.raw_type && a.px == b.px && a.py == b.py && a.tile_x == b.tile_x && a.tile_y == b.tile_y && a.facing == b.facing &&
           a.hp == b.hp && a.max_hp == b.max_hp && a.anim_state == b.anim_state && a.anim_frame == b.anim_frame && a.is_holding == b.is_holding && a.carried_points == b.carried_points &&
           a.is_stunned == b.is_stunned && a.is_swimming == b.is_swimming && a.is_drowning == b.is_drowning && a.is_on_mud == b.is_on_mud && a.burn_elapsed_ms == b.burn_elapsed_ms && a.frozen == b.frozen &&
           a.state == b.state && a.action == b.action && a.target_team_id == b.target_team_id && a.loco_clip == b.loco_clip && a.loco_frame == b.loco_frame && a.loco_mirrored == b.loco_mirrored &&
           a.loco_left_ms == b.loco_left_ms;
}

const sim::AntSnapshot* ant_of_world(const sim::WorldState& world, uint32_t id) {
    for (const auto& a : world.ants) {
        if (a.id == id) return &a;
    }
    return nullptr;
}

// What the picture of others = confirmed is made of, checked against the two engines themselves: every ant of another player is the confirmed engine's ant (and no other player's ant that the confirmed engine
// does not have), every own ant the shown engine's. Returns how many other players' ants stand elsewhere in the shown engine (the predicted engine is ahead: the picture must not show that)
bool picture_is_made_of_both(Application& app, uint8_t me, size_t& others_ahead) {
    const sim::WorldState& world = app.shown_world();
    const sim::WorldState& shown = app.view_sim().get_world_state();
    const sim::WorldState& confirmed = app.sim().get_world_state();
    for (const sim::AntSnapshot& a : world.ants) {
        const sim::AntSnapshot* from = ant_of_world(a.player_id == me ? shown : confirmed, a.id);
        if (from == nullptr || !same_ant(a, *from)) return false;
    }
    for (const sim::AntSnapshot& a : shown.ants) {
        if (a.player_id == me && ant_of_world(world, a.id) == nullptr) return false;
    }
    for (const sim::AntSnapshot& a : confirmed.ants) {
        if (a.player_id == me) continue;
        const sim::AntSnapshot* drawn = ant_of_world(world, a.id);
        if (drawn == nullptr) return false;
        const sim::AntSnapshot* ahead = ant_of_world(shown, a.id);
        if (ahead != nullptr && (ahead->px != a.px || ahead->py != a.py)) ++others_ahead;
    }
    return world.fog_of_war_enabled == confirmed.fog_of_war_enabled && world.fog_revealed == confirmed.fog_revealed && world.cells.size() == shown.cells.size() &&
           world.match_time_remaining_ms == shown.match_time_remaining_ms;
}

}  // namespace

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    net::NetGame::default_prediction_budget_ns() = UINT64_MAX;      // (a busy machine stalls the test process now and then: that is not the prediction's cost, and no test is to lose its prediction to it)
    std::cout << "\n=======================================================\n [PREDICTION IN THE APPLICATION] what the match screen reads\n=======================================================\n";

    TEST_CASE("PA1 The Match Screen Shows The Predicted Engine: The Order Of A Right Click Is In It In The Same Call (Not In The Confirmed Engine), The Frame Drawn Is Its Picture, A Click Picks An Ant Where It Stands, And The Switch Gives The Confirmed Engine Back At Once") {
        Application app;
        Peer bob;
        Duo duo{app, bob};
        ASSERT_TRUE(predicting_match(app, bob, duo));
        // the engine that is shown stands ahead of the confirmed one by the lead, and is the NetGame's view
        ASSERT_TRUE(&app.view_sim() != &app.sim());
        ASSERT_TRUE(&app.view_sim() == &app.net()->view_engine());
        ASSERT_EQ(app.view_sim().current_tick(), app.sim().current_tick() + app.net()->prediction()->lead_ticks());
        // an order of a right click: in the shown engine at once (the click's own call), in the confirmed one only when its turn has run
        const std::vector<uint32_t> mine = ants_of(app.sim(), 0);
        ASSERT_TRUE(!mine.empty());
        const uint32_t ant = mine[0];
        ASSERT_TRUE(app.sim().get_unit(ant).orig_order != sim::AntUnit::kOrderMove);
        const uint64_t predicted_before = app.net()->prediction()->stats().commands_predicted;
        const uint32_t walking = walk_one_ant_ahead(app, bob, duo, 2);
        ASSERT_EQ(walking, ant);                                                           // (it was ordered, and the shown engine had it walking a few pixels ahead of the confirmed one)
        ASSERT_EQ(app.net()->prediction()->stats().commands_predicted, predicted_before + 1);
        ASSERT_EQ(app.view_sim().get_unit(ant).orig_order, sim::AntUnit::kOrderMove);
        ASSERT_TRUE(app.sim().get_unit(ant).orig_order == sim::AntUnit::kOrderMove);       // (by now the order has been sealed and run: that is how the ant came to walk in the confirmed engine)
        // The host stops sealing and the turns in hand are run: from here no tick runs, so that two frames are the same frame of the same two engines (a tick between them would move the ant
        // in the confirmed engine and tell nothing of what the screen reads)
        app.net()->freeze();
        duo.step(400);
        ASSERT_TRUE(app.net()->predicting() && &app.view_sim() != &app.sim());
        // the pixels: the frame that is drawn is the shown engine's. The same frame drawn from the confirmed engine differs where the ant stands (the HUD and the cursor do not enter: the
        // regions are those of the ant in both engines, away from the pointer that rests at the middle of the screen)
        const sim::AntSnapshot* p = ant_in(app.view_sim(), ant);
        const sim::AntSnapshot* c = ant_in(app.sim(), ant);
        ASSERT_TRUE(p != nullptr && c != nullptr);
        const int32_t pwx = p->px;
        const int32_t pwy = p->py;
        const int32_t cwx = c->px;
        const int32_t cwy = c->py;
        ASSERT_TRUE(pwx != cwx || pwy != cwy);
        ASSERT_TRUE(place_camera(app, pwx, pwy, 110, 90));                                 // the ant is up and left of the middle of the view, where the pointer rests
        int32_t psx = 0, psy = 0, csx = 0, csy = 0;
        ASSERT_TRUE(app.renderer().camera().world_to_screen(pwx, pwy, psx, psy));
        ASSERT_TRUE(app.renderer().camera().world_to_screen(cwx, cwy, csx, csy));
        const int32_t rx = std::min(psx, csx) - 30;
        const int32_t ry = std::min(psy, csy) - 44;
        const int32_t rw = std::abs(psx - csx) + 60;
        const int32_t rh = std::abs(psy - csy) + 70;
        // 1. click on the shown ant at the edge of its box away from where the confirmed engine has it: the shown engine picks it (the confirmed one has nothing there)
        const int32_t dx = pwx - cwx;
        const int32_t dy = pwy - cwy;
        int32_t click_x = psx;
        int32_t click_y = psy;
        if (std::abs(dx) >= std::abs(dy)) click_x = psx + (dx > 0 ? 19 : -20);
        else click_y = psy + (dy > 0 ? 15 : -32);
        const auto deselect = [&]() { app.hud().handle_key_down(SDLK_ESCAPE, app.sim(), app.renderer().camera()); };
        const auto point_at = [&](int32_t x, int32_t y) {
            SDL_MouseMotionEvent motion{};
            motion.type = SDL_MOUSEMOTION;
            motion.x = x;
            motion.y = y;
            app.handle_mouse_motion(motion);
        };
        deselect();
        ASSERT_EQ(app.hud().get_selected_ant_id(), 0u);
        click(app, SDL_BUTTON_LEFT, click_x, click_y);
        ASSERT_EQ(app.hud().get_selected_ant_id(), ant);
        // 2. the cursor is the shown engine's too: the pointer over the shown ant is the selection cursor
        deselect();
        point_at(click_x, click_y);
        app.render_frame();
        ASSERT_TRUE(app.hud().current_cursor() == CursorType::Select);
        // 3. the pixels: the frame that is drawn is the shown engine's. Nothing is selected and the pointer is far from the ant (the same user interface in both frames), so that what differs
        // between this frame and the one from the confirmed engine is the ant. A frame drawn again is the same frame.
        deselect();
        const LayoutRect view = app.layout().view();
        point_at(view.x + 30, view.y + view.h - 30);
        app.render_frame();
        const std::vector<uint8_t> shown = canvas_region(app, rx, ry, rw, rh);
        app.render_frame();
        ASSERT_TRUE(!shown.empty() && canvas_region(app, rx, ry, rw, rh) == shown);
        // 4. the switch gives the confirmed engine back at once (the next update of the net): the same frame, from the other engine, differs where the ant stands
        app.net()->set_prediction_enabled(false);
        app.pump_network(0.0f);
        ASSERT_FALSE(app.net()->predicting());
        ASSERT_TRUE(&app.view_sim() == &app.sim());
        app.render_frame();
        ASSERT_TRUE(canvas_region(app, rx, ry, rw, rh) != shown);
        // 5. and the click that picked the ant now finds nothing, the cursor over that place is the plain one
        point_at(click_x, click_y);
        app.render_frame();
        ASSERT_TRUE(app.hud().current_cursor() == CursorType::Normal);
        deselect();
        click(app, SDL_BUTTON_LEFT, click_x, click_y);
        ASSERT_EQ(app.hud().get_selected_ant_id(), 0u);
        duo.step(1500);
        ASSERT_TRUE(app.sim().state_hash() == bob.sim.state_hash());                       // the confirmed engines are the same engine, whatever was shown
        ASSERT_FALSE(app.net()->desynced() || bob.net.desynced());
    } TEST_END();

    TEST_CASE("PA2 The HUD's Step Of Every Tick Reads The Shown Engine: An Alliance That A Turn In Hand Makes Is In The HUD (is_on_team) Before The Confirmed Engine Has Run That Turn, Not Later") {
        Application app;
        Peer bob;
        Duo duo{app, bob};
        ASSERT_TRUE(predicting_match(app, bob, duo));
        const auto allied = [](const sim::SimulationEngine& e, uint8_t seat) {
            const sim::WorldState& w = e.get_world_state();
            return seat < w.player_alliances.size() && w.player_alliances[seat] < sim::MAX_PLAYERS && w.player_alliances[seat] != seat;
        };
        ASSERT_FALSE(allied(app.sim(), 0) || app.hud().is_on_team());
        sim::Command invite;                                                               // Bob asks the application's team to team up ...
        invite.type = sim::CommandType::AllianceInvite;
        invite.issuer = 1;
        invite.other_player = 0;
        ASSERT_EQ(bob.net.submit(invite).status, sim::CommandResult::Status::Applied);
        duo.step(1000);                                                                    // (sealed, run everywhere)
        ASSERT_FALSE(allied(app.sim(), 0));
        sim::Command accept;                                                               // ... and the application's team accepts
        accept.type = sim::CommandType::AllianceAccept;
        accept.issuer = 0;
        accept.other_player = 1;
        ASSERT_EQ(app.net()->submit(accept).status, sim::CommandResult::Status::Applied);
        int window = 0;                                                                    // steps at which the shown engine is allied and the confirmed one not yet
        int late = 0;                                                                      // ... at which the HUD did not know
        for (int i = 0; i < 300 && !allied(app.sim(), 0); ++i) {
            duo.step(10);
            if (allied(app.view_sim(), 0) && !allied(app.sim(), 0)) {
                ++window;
                if (!app.hud().is_on_team()) ++late;
            }
        }
        ASSERT_TRUE(allied(app.sim(), 0));
        ASSERT_TRUE(window >= 3);                                                          // (the prediction stands at least a tick ahead: a window of several 10 ms steps)
        ASSERT_EQ(late, 0);
        duo.step(200);
        ASSERT_TRUE(app.hud().is_on_team());
        size_t team_news = 0;                                                              // the chat log's news flash of the new team: the confirmed engine's news, polled once
        for (const std::string& line : app.hud().get_chat_log()) team_news += line.find("are a team now") != std::string::npos ? 1u : 0u;
        ASSERT_EQ(team_news, 1u);
        app.net()->freeze();
        duo.step(2000);
        ASSERT_TRUE(app.sim().state_hash() == bob.sim.state_hash());
        ASSERT_FALSE(app.net()->desynced() || bob.net.desynced());
    } TEST_END();


    TEST_CASE("PA3 The Cues Of An Own Ant's Own Action Are Heard Once, From The Predicted Engine, In Step With The Picture: An Own Worker Sent To Food Harvests It, The Cues Are Played From The Predicted Engine A Few Ticks Before The Confirmed Engine Makes Them, And The Confirmed Engine's Copies Are Dropped (No Second Sound)") {
        Application app;
        Peer bob;
        Duo duo{app, bob};
        ASSERT_TRUE(predicting_match(app, bob, duo));
        // the food nearest to the player's hill, and an own worker
        sim::Command go;
        ASSERT_TRUE(food_order(app, go));
        const size_t channels_before = app.audio_mixer().active_channel_count();
        ASSERT_EQ(app.net()->submit(go).status, sim::CommandResult::Status::Applied);
        const net::CueRouter::Stats before = app.cue_router().stats();
        for (int i = 0; i < 3000 && app.cue_router().stats().played_predicted == before.played_predicted; ++i) duo.step(10);       // (the ant walks to the food and harvests it)
        ASSERT_TRUE(app.cue_router().stats().played_predicted > before.played_predicted);
        // heard now, from the predicted engine: one more sound is playing, and the confirmed engine has not made the cue yet (nothing has been met)
        ASSERT_EQ(app.audio_mixer().active_channel_count(), channels_before + 1);
        ASSERT_EQ(app.cue_router().stats().duplicates_dropped, before.duplicates_dropped);
        ASSERT_EQ(app.cue_router().stats().played_confirmed_own, before.played_confirmed_own);
        const uint64_t heard_at = app.sim().current_tick();
        // a second later the confirmed engine has made the same cues; they are dropped, and never a second sound is playing
        bool met = false;
        for (int i = 0; i < 100; ++i) {
            duo.step(10);
            ASSERT_TRUE(app.audio_mixer().active_channel_count() <= channels_before + 1);
            met = met || app.cue_router().stats().duplicates_dropped > before.duplicates_dropped;
        }
        ASSERT_TRUE(met);
        const net::CueRouter::Stats& st = app.cue_router().stats();
        ASSERT_TRUE(app.sim().current_tick() > heard_at + app.net()->prediction()->lead_ticks());
        ASSERT_EQ(st.duplicates_dropped, st.played_predicted);                              // every cue that was played from the predicted engine was met by the confirmed engine's copy
        ASSERT_EQ(st.played_confirmed_own, 0u);                                             // ... and the confirmed engine played no cue of the ant's own action itself
        ASSERT_EQ(st.phantoms, 0u);
        ASSERT_EQ(app.audio_mixer().active_channel_count(), channels_before);               // (the stop that cuts the sound came from the predicted engine too, and was met)
        app.net()->freeze();
        duo.step(1500);
        ASSERT_TRUE(app.sim().state_hash() == bob.sim.state_hash());
        ASSERT_FALSE(app.net()->desynced() || bob.net.desynced());
    } TEST_END();


    TEST_CASE("PA4 The Switch: A Match Predicts Unless The Command Line Says --prediction off Or --no-prediction (The Last Word Wins, A Value That Is No Switch Is Refused With The Reason); Told Not To, The Match Shows The Confirmed Engine And Nothing Is Predicted") {
        bool on = false;
        ASSERT_TRUE(parse_switch("on", on) && on);
        ASSERT_TRUE(parse_switch("OFF", on) && !on);
        ASSERT_TRUE(parse_switch("Yes", on) && on);
        ASSERT_TRUE(parse_switch("no", on) && !on);
        ASSERT_TRUE(parse_switch("true", on) && on);
        ASSERT_TRUE(parse_switch("False", on) && !on);
        ASSERT_TRUE(parse_switch("1", on) && on);
        ASSERT_TRUE(parse_switch("0", on) && !on);
        on = true;
        ASSERT_FALSE(parse_switch("maybe", on) || parse_switch("", on) || parse_switch("onn", on) || parse_switch("2", on));
        ASSERT_TRUE(on);                                                                   // (untouched by a text that is no switch)
        std::vector<std::string> args;
        std::vector<char*> st;
        const auto parse = [&](std::vector<std::string> a) {
            args = std::move(a);
            st.clear();
            for (std::string& x : args) st.push_back(x.data());
            st.push_back(nullptr);
            return Application::parse_arguments(static_cast<int>(args.size()), st.data());
        };
        ApplicationConfig c = parse({"ants"});
        ASSERT_TRUE(c.prediction && c.startup_error.empty());                              // on by default: the command line said nothing
        c = parse({"ants", "--prediction", "off"});
        ASSERT_TRUE(!c.prediction && c.startup_error.empty());
        c = parse({"ants", "--prediction", "on"});
        ASSERT_TRUE(c.prediction && c.startup_error.empty());
        c = parse({"ants", "--no-prediction"});
        ASSERT_TRUE(!c.prediction && c.startup_error.empty());
        c = parse({"ants", "--prediction", "maybe"});
        ASSERT_EQ(c.startup_error, std::string("--prediction needs on or off"));           // refused with the reason: the game does not start
        c = parse({"ants", "--prediction"});
        ASSERT_EQ(c.startup_error, std::string("--prediction needs on or off"));
        c = parse({"ants", "--prediction", "off", "--prediction", "on"});
        ASSERT_TRUE(c.prediction);                                                         // the last one wins
        c = parse({"ants", "--prediction", "on", "--no-prediction"});
        ASSERT_TRUE(!c.prediction);
        // the application gives its NetGame what the configuration says, and nothing else decides: a key `prediction` in the settings file is not read
        const std::string settings = (std::filesystem::temp_directory_path() / ("ants_prediction_app_settings_" + std::to_string(static_cast<long long>(::getpid())) + ".ini")).string();
        const auto with_settings = [&](const std::string& line, bool predict) {
            {
                std::ofstream out(settings);
                out << "Sound Volume=50\n" << line << "\n";
            }
            ApplicationConfig cfg = host_config();
            cfg.settings_path = settings;
            cfg.prediction = predict;
            Application app;
            if (!app.init(cfg)) return false;
            return app.net() != nullptr && app.net()->prediction_enabled() == predict;
        };
        ASSERT_TRUE(with_settings("Music Volume=40", true));
        ASSERT_TRUE(with_settings("prediction=off", true));                                // (a key of an earlier version: ignored, the match predicts)
        ASSERT_TRUE(with_settings("prediction=on", false));                                // ... and does not make a match predict that the command line told not to
        std::error_code removed;
        std::filesystem::remove(settings, removed);
        // told not to: the match shows the confirmed engine, nothing is predicted, an order waits for its turn, and the confirmed engines are the same
        {
            ApplicationConfig cfg = host_config();
            cfg.prediction = false;
            Application app;
            Peer bob;
            Duo duo{app, bob};
            ASSERT_TRUE(begin_match(app, bob, duo, cfg));
            ASSERT_TRUE(duo.until([&]() { return app.sim().current_tick() > 60; }, 20000));
            ASSERT_FALSE(app.net()->prediction_enabled() || app.net()->predicting());
            ASSERT_TRUE(&app.view_sim() == &app.sim());
            ASSERT_TRUE(app.net()->prediction() == nullptr);                               // there is no prediction at all, not a suspended one: a match that is told not to predict pays nothing
            ASSERT_TRUE(app.cue_router().idle());                                          // ... and the cue router has nothing to do
            sim::Command go;
            go.type = sim::CommandType::GroupMove;
            go.issuer = 0;
            go.tile_x = 10;
            go.tile_y = 10;
            go.ants = {ants_of(app.sim(), 0)[0]};
            ASSERT_EQ(app.net()->submit(go).status, sim::CommandResult::Status::Applied);        // (the answer is the guess of predict_order_ack, as before)
            app.net()->freeze();
            duo.step(1500);
            ASSERT_TRUE(app.sim().state_hash() == bob.sim.state_hash());
        }
        // by default: the match predicts (the application's NetGame is asked to, and the screen shows the predicted engine)
        {
            Application app;
            Peer bob;
            Duo duo{app, bob};
            ASSERT_TRUE(begin_match(app, bob, duo, host_config()));
            ASSERT_TRUE(duo.until([&]() { return app.net()->predicting() && app.sim().current_tick() > 60; }, 20000));
            ASSERT_TRUE(app.net()->prediction_enabled());
            ASSERT_TRUE(&app.view_sim() == &app.net()->view_engine() && &app.view_sim() != &app.sim());
            app.net()->freeze();
            duo.step(1500);
            ASSERT_TRUE(app.sim().state_hash() == bob.sim.state_hash());
        }
    } TEST_END();

    TEST_CASE("PA9 The Frame Draws An Ant That A Rebuild Moved Where It Was Shown And Slides It: With A Slide Of 40 px Left Over The Ant Is Not At The Engine's Place Any More, It Is 40 px Further, And When The Slide Is Over The Frame Is The Plain One Again; While It Slides The Pointer Picks It Where It Is Drawn, Not Where The Engine Has It") {
        Application app;
        Peer bob;
        Duo duo{app, bob};
        ASSERT_TRUE(predicting_match(app, bob, duo));
        const std::vector<uint32_t> mine = ants_of(app.sim(), 0);
        ASSERT_FALSE(mine.empty());
        app.net()->freeze();                                                               // (no tick runs between the frames: they are one frame of one engine)
        duo.step(400);
        const uint32_t ant = mine[0];
        const sim::AntSnapshot* shown = ant_in(app.view_sim(), ant);
        ASSERT_TRUE(shown != nullptr);
        const int32_t wx = shown->px;
        const int32_t wy = shown->py;
        ASSERT_TRUE(place_camera(app, wx, wy, 110, 90));
        int32_t sx = 0;
        int32_t sy = 0;
        ASSERT_TRUE(app.renderer().camera().world_to_screen(wx, wy, sx, sy));
        const LayoutRect view = app.layout().view();
        SDL_MouseMotionEvent motion{};
        motion.type = SDL_MOUSEMOTION;
        motion.x = view.x + 30;
        motion.y = view.y + view.h - 30;
        app.handle_mouse_motion(motion);                                                   // (the pointer far from the ant)
        const int32_t kSlide = 40;
        const std::vector<uint8_t> before_old = (app.render_frame(), canvas_region(app, sx - 12, sy - 12, 24, 24));
        const std::vector<uint8_t> before_new = canvas_region(app, sx + kSlide - 12, sy - 12, 24, 24);
        app.renderer().view_glide().add(ant, kSlide, 0);                                    // the ant was shown 40 px to the right of where the engine has it
        app.render_frame();
        const std::vector<uint8_t> during_old = canvas_region(app, sx - 12, sy - 12, 24, 24);
        const std::vector<uint8_t> during_new = canvas_region(app, sx + kSlide - 12, sy - 12, 24, 24);
        ASSERT_FALSE(before_old.empty() || before_new.empty());
        ASSERT_TRUE(during_old != before_old);                                             // the ant has left the engine's place ...
        ASSERT_TRUE(during_new != before_new);                                             // ... and stands 40 px to the right
        app.renderer().update_transient_effects(1.0f);                                      // (a second of frames: the slide is over)
        app.render_frame();
        ASSERT_TRUE(canvas_region(app, sx - 12, sy - 12, 24, 24) == before_old);
        ASSERT_TRUE(canvas_region(app, sx + kSlide - 12, sy - 12, 24, 24) == before_new);
        // The pointer picks the ant where it is DRAWN while it slides: a click on the drawn ant selects it and the cursor over it is the selection cursor; a click where the engine has it, which
        // the picture shows empty, does not. (The points are ones that no other ant's box holds: the ants stand close together at the hill.)
        const sim::WorldState world = app.view_sim().get_world_state();
        const auto in_box = [](const sim::AntSnapshot& a, int32_t x, int32_t y) {
            const bool combat = a.raw_type == sim::AntType::Combat;
            return x >= a.px - (combat ? 32 : 20) && x < a.px + (combat ? 26 : 20) && y >= a.py - (combat ? 46 : 32) && y < a.py + 16;
        };
        const auto free_of_others = [&](int32_t x, int32_t y) {
            for (const auto& a : world.ants) {
                if (a.id != ant && in_box(a, x, y)) return false;
            }
            return true;
        };
        const auto deselect = [&]() { app.hud().handle_key_down(SDLK_ESCAPE, app.sim(), app.renderer().camera()); };
        bool tried = false;
        const std::pair<int32_t, int32_t> slides[] = {{48, 0}, {-48, 0}, {0, 56}, {0, -56}, {48, 56}, {-48, -56}, {48, -56}, {-48, 56}};
        for (const auto& slide : slides) {
            const int32_t drawn_x = wx + slide.first;                                       // a point of the ant's box as it is drawn, and one of its box where the engine has it
            const int32_t drawn_y = wy + slide.second - 8;
            const int32_t engine_x = wx;
            const int32_t engine_y = wy - 8;
            if (!free_of_others(drawn_x, drawn_y) || !free_of_others(engine_x, engine_y)) continue;
            tried = true;
            app.renderer().view_glide().clear();
            app.renderer().view_glide().add(ant, slide.first, slide.second);
            app.render_frame();
            deselect();
            click(app, SDL_BUTTON_LEFT, sx + slide.first, sy + slide.second - 8);
            ASSERT_EQ(app.hud().get_selected_ant_id(), ant);                                // picked where it is drawn
            deselect();
            motion.x = sx + slide.first;
            motion.y = sy + slide.second - 8;
            app.handle_mouse_motion(motion);
            app.render_frame();
            ASSERT_TRUE(app.hud().current_cursor() == CursorType::Select);
            click(app, SDL_BUTTON_LEFT, sx, sy - 8);
            ASSERT_EQ(app.hud().get_selected_ant_id(), 0u);                                 // ... and not where the engine has it: the picture shows nothing there
            app.renderer().view_glide().clear();                                            // without a slide the same click picks it where the engine has it
            app.render_frame();
            click(app, SDL_BUTTON_LEFT, sx, sy - 8);
            ASSERT_EQ(app.hud().get_selected_ant_id(), ant);
            deselect();
            break;
        }
        ASSERT_TRUE(tried);
        duo.step(1500);
        ASSERT_TRUE(app.sim().state_hash() == bob.sim.state_hash());                        // (drawing only: the confirmed engines are the same)
    } TEST_END();

    TEST_CASE("PA10 In The Plain Picture (--others predicted) The Frame Never Draws A Sliding Ant Of Another Team On Ground That The Fog Covers: With Fog Of War An Enemy Ant At The Edge Of The Explored Ground Whose Slide Began Two Tiles Into The Fog Is Drawn With The Part Of The Slide That Stays On Explored Ground (The Same Frame As That Shorter Slide, Nothing Where The Whole Slide Would Put It), And A Slide Over Explored Ground Is Drawn Whole") {
        // (the plain picture, --others predicted, expires with v0.22.3 at the latest: this test goes with it, AGENTS.md rule 9)
        Application app;
        Peer bob;
        Duo duo{app, bob};
        ApplicationConfig plain_picture = host_config();
        plain_picture.others_confirmed = false;                                             // (the plain picture, --others predicted: it is the one in which another player's ant slides at all)
        ASSERT_TRUE(begin_match(app, bob, duo, plain_picture, true));
        ASSERT_TRUE(duo.until([&]() { return app.net()->predicting() && app.sim().current_tick() > 40; }, 20000));
        ASSERT_TRUE(app.sim().is_fog_of_war_enabled() && app.view_sim().is_fog_of_war_enabled());
        const auto drawn_enemy = [&]() -> const sim::AntSnapshot* {                          // an ant of Bob's that the picture draws: it stands on ground that the application's team has explored
            const sim::WorldState& world = app.view_sim().get_world_state();
            for (const auto& a : world.ants) {
                if (a.player_id == 1 && a.hp > 0 && health::ant_drawn(world, 0, a)) return &a;
            }
            return nullptr;
        };
        ASSERT_TRUE(drawn_enemy() == nullptr);                                              // (the two hills are far apart: Bob's ants stand under the fog)
        // an own ant walks to Bob's hill; the match is stopped at the frame in which the first of Bob's ants comes out of the fog: that ant stands at the edge of the explored ground
        const std::vector<uint32_t> mine = ants_of(app.sim(), 0);
        ASSERT_FALSE(mine.empty());
        int16_t gx = 0;
        int16_t gy = 0;
        ASSERT_TRUE(far_goal(app.sim(), 1, gx, gy, 3, 6));
        sim::Command go;
        go.type = sim::CommandType::GroupMove;
        go.issuer = 0;
        go.tile_x = gx;
        go.tile_y = gy;
        go.ants = {mine[0]};
        ASSERT_EQ(app.net()->submit(go).status, sim::CommandResult::Status::Applied);
        ASSERT_TRUE(duo.until([&]() { return drawn_enemy() != nullptr; }, 60000));
        app.net()->freeze();                                                                // (no tick runs between the frames from here)
        duo.step(400);
        ASSERT_TRUE(app.net()->predicting());
        const sim::AntSnapshot* found = drawn_enemy();
        ASSERT_TRUE(found != nullptr);
        const sim::AntSnapshot enemy = *found;
        const sim::WorldState world = app.view_sim().get_world_state();
        // a slide of two tiles that began on covered ground, and what the model keeps of it (view_glide.hpp: the largest quarter that stays on explored ground)
        int32_t fog_dx = 0;
        int32_t fog_dy = 0;
        int32_t kept_dx = 0;
        int32_t kept_dy = 0;
        bool have = false;
        const std::pair<int32_t, int32_t> two_tiles[] = {{64, 0}, {-64, 0}, {0, 64}, {0, -64}};
        for (const auto& d : two_tiles) {
            const int32_t tx = (enemy.px + d.first) / 32;
            const int32_t ty = (enemy.py + d.second) / 32;
            if (tx < 0 || ty < 0 || tx >= static_cast<int32_t>(world.width) || ty >= static_cast<int32_t>(world.height) || world.is_tile_revealed(tx, ty)) continue;
            ViewGlide model;
            model.add(enemy.id, d.first, d.second);
            const sim::AntSnapshot kept = glided_ant(model, world, 0, enemy);
            fog_dx = d.first;
            fog_dy = d.second;
            kept_dx = kept.px - enemy.px;
            kept_dy = kept.py - enemy.py;
            have = true;
            break;
        }
        ASSERT_TRUE(have);                                                                  // (the ant stands within two tiles of the fog)
        ASSERT_TRUE(kept_dx != fog_dx || kept_dy != fog_dy);                                // (the whole slide would draw it on covered ground: the model cuts it)
        ASSERT_TRUE(place_camera(app, enemy.px, enemy.py, 0, 0));
        int32_t sx = 0;
        int32_t sy = 0;
        ASSERT_TRUE(app.renderer().camera().world_to_screen(enemy.px, enemy.py, sx, sy));
        const LayoutRect view = app.layout().view();
        SDL_MouseMotionEvent motion{};
        motion.type = SDL_MOUSEMOTION;
        motion.x = view.x + 30;
        motion.y = view.y + view.h - 30;
        app.handle_mouse_motion(motion);                                                   // (the pointer far from the ant)
        // the frame, with a slide of the enemy ant: the ant and two tiles around it on every side
        const auto frame_with = [&](int32_t dx, int32_t dy) {
            app.renderer().view_glide().clear();
            if (dx != 0 || dy != 0) app.renderer().view_glide().add(enemy.id, dx, dy);
            app.render_frame();
            return canvas_region(app, sx - 96, sy - 96, 192, 192);
        };
        const auto fog_spot = [&]() { return canvas_region(app, sx + fog_dx - 12, sy + fog_dy - 20, 24, 24); };      // (where the whole slide would draw the ant: its body stands above its position)
        const std::vector<uint8_t> plain = frame_with(0, 0);
        const std::vector<uint8_t> plain_spot = fog_spot();
        ASSERT_FALSE(plain.empty() || plain_spot.empty());
        ASSERT_TRUE(frame_with(0, 0) == plain);                                             // (a frame drawn again is the same frame)
        const std::vector<uint8_t> slid = frame_with(fog_dx, fog_dy);                       // the slide that began two tiles into the fog ...
        ASSERT_TRUE(fog_spot() == plain_spot);                                              // ... draws nothing where the fog covers the ground
        ASSERT_TRUE(slid == frame_with(kept_dx, kept_dy));                                  // ... and is the frame of the part that stays on explored ground (none of it: the plain frame)
        if (kept_dx == 0 && kept_dy == 0) ASSERT_TRUE(slid == plain);
        // the same slide of an own ant is drawn whole (the player sees his own ants wherever they are), and so is an enemy's slide over explored ground
        const sim::AntSnapshot* own = ant_in(app.view_sim(), mine[0]);
        ASSERT_TRUE(own != nullptr);
        ViewGlide whole;
        whole.add(own->id, fog_dx, fog_dy);
        ASSERT_EQ(glided_ant(whole, world, 0, *own).px, own->px + fog_dx);
        ASSERT_EQ(glided_ant(whole, world, 0, *own).py, own->py + fog_dy);
        const int32_t back_tx = (enemy.px - fog_dx / 2) / 32;
        const int32_t back_ty = (enemy.py - fog_dy / 2) / 32;
        if (world.is_tile_revealed(back_tx, back_ty)) {                                     // (a tile towards the application's ant: explored)
            const std::vector<uint8_t> back = frame_with(-fog_dx / 2, -fog_dy / 2);
            ASSERT_TRUE(back != plain);                                                     // the ant is drawn a tile from where the engine has it
            ViewGlide over;
            over.add(enemy.id, -fog_dx / 2, -fog_dy / 2);
            ASSERT_EQ(glided_ant(over, world, 0, enemy).px, enemy.px - fog_dx / 2);
            ASSERT_EQ(glided_ant(over, world, 0, enemy).py, enemy.py - fog_dy / 2);
        }
        app.renderer().view_glide().clear();
        duo.step(1500);
        ASSERT_TRUE(app.sim().state_hash() == bob.sim.state_hash());                        // (drawing only: the confirmed engines are the same)
        ASSERT_FALSE(app.net()->desynced() || bob.net.desynced());
    } TEST_END();

    TEST_CASE("PA12 Others = Confirmed (The Default) Draws The Other Player's Ants From The Confirmed Engine And The Player's Own From The Predicted One: With Bob's Ant Walking And The Two Engines A Lead Apart, The World Of The Frame Has Bob's Ant Where The Confirmed Engine Has It, Alice's Ants Where The Predicted Engine Has Them, The Frame Shows Bob's Ant At The Confirmed Engine's Place (The Frame Of The Confirmed Engine Alone Is The Same There, The Plain Predicted Frame Is Not), And --others predicted Draws Everything From The Predicted Engine As It Did") {
        Application app;
        Peer bob;
        Duo duo{app, bob};
        ASSERT_TRUE(predicting_match(app, bob, duo));
        const std::vector<uint32_t> bobs = ants_of(app.sim(), 1);
        ASSERT_FALSE(bobs.empty());
        const uint32_t walker = bobs[0];
        int16_t gx = 0;
        int16_t gy = 0;
        ASSERT_TRUE(far_goal(app.sim(), 1, gx, gy));
        sim::Command go;
        go.type = sim::CommandType::GroupMove;
        go.issuer = 1;
        go.tile_x = gx;
        go.tile_y = gy;
        go.ants.assign(1u, walker);
        bob.net.submit(go);
        const auto apart = [&]() {                                                         // Bob's ant walks and the predicted engine has it ahead of the confirmed one
            const sim::AntSnapshot* p = ant_in(app.view_sim(), walker);
            const sim::AntSnapshot* c = ant_in(app.sim(), walker);
            return p != nullptr && c != nullptr && std::max(std::abs(p->px - c->px), std::abs(p->py - c->py)) >= 4;
        };
        ASSERT_TRUE(duo.until(apart, 8000));
        app.net()->freeze();                                                               // (no tick between the frames: they are one frame of one pair of engines)
        duo.step(400);
        ASSERT_TRUE(app.net()->predicting() && apart());
        // the default: the world is made of both engines (Bob's ant is behind the predicted engine's)
        ASSERT_TRUE(app.others_view_active());
        // the plain picture (--others predicted): the world of the frame is the shown engine's own, nothing is made
        app.set_others_confirmed(false);
        ASSERT_FALSE(app.others_view_active());
        const uint64_t made_before_plain = app.others_worlds_made();
        ASSERT_TRUE(&app.shown_world() == &app.view_sim().get_world_state());
        ASSERT_EQ(app.others_worlds_made(), made_before_plain);
        const sim::AntSnapshot p0 = *ant_in(app.view_sim(), walker);
        const sim::AntSnapshot c0 = *ant_in(app.sim(), walker);
        ASSERT_TRUE(place_camera(app, c0.px, c0.py, 110, 90));
        int32_t psx = 0, psy = 0, csx = 0, csy = 0;
        ASSERT_TRUE(app.renderer().camera().world_to_screen(p0.px, p0.py, psx, psy));
        ASSERT_TRUE(app.renderer().camera().world_to_screen(c0.px, c0.py, csx, csy));
        const int32_t rx = std::min(psx, csx) - 30;
        const int32_t ry = std::min(psy, csy) - 44;
        const int32_t rw = std::abs(psx - csx) + 60;
        const int32_t rh = std::abs(psy - csy) + 70;
        const LayoutRect view = app.layout().view();
        SDL_MouseMotionEvent motion{};
        motion.type = SDL_MOUSEMOTION;
        motion.x = view.x + 30;
        motion.y = view.y + view.h - 30;
        app.handle_mouse_motion(motion);                                                   // (the pointer far from the ant, nothing selected: the interface is the same in every frame)
        app.render_frame();
        const std::vector<uint8_t> plain_predicted = canvas_region(app, rx, ry, rw, rh);
        ASSERT_FALSE(plain_predicted.empty());
        // the mode on: the world is made of both engines
        app.set_others_confirmed(true);
        ASSERT_TRUE(app.others_view_active());
        size_t ahead = 0;
        ASSERT_TRUE(picture_is_made_of_both(app, 0, ahead));
        ASSERT_TRUE(ahead >= 1);                                                           // (the test is not empty: the predicted engine has Bob's ant ahead, and the picture does not)
        const sim::WorldState& world = app.shown_world();
        ASSERT_TRUE(&world != &app.view_sim().get_world_state());
        ASSERT_TRUE(same_ant(*ant_of_world(world, walker), c0));                           // Bob's ant: the confirmed engine's, whole
        ASSERT_FALSE(same_ant(*ant_of_world(world, walker), p0));
        for (const uint32_t mine : ants_of(app.sim(), 0)) ASSERT_TRUE(same_ant(*ant_of_world(world, mine), *ant_in(app.view_sim(), mine)));      // Alice's: the predicted engine's, whole
        uint64_t made = app.others_worlds_made();
        ASSERT_EQ(&app.shown_world(), &world);                                             // (asked again in the same moment: the same world, not made again)
        ASSERT_EQ(app.others_worlds_made(), made);
        app.render_frame();
        const std::vector<uint8_t> on = canvas_region(app, rx, ry, rw, rh);
        ASSERT_TRUE(on != plain_predicted);                                                // the frame is not the predicted engine's where Bob's ant walks ...
        app.render_frame();
        ASSERT_TRUE(canvas_region(app, rx, ry, rw, rh) == on);                             // (a frame drawn again is the same frame)
        // the pointer reads the world that is drawn: with an own ant selected, the cursor at the edge of the box that the predicted engine has Bob's ant in, away from the box of the ant as it is drawn,
        // is the plain move cursor in this picture (nothing is drawn there) and the attack cursor in the plain one
        const std::vector<uint32_t> alice = ants_of(app.sim(), 0);
        ASSERT_FALSE(alice.empty());
        app.hud().select_ant(alice[0]);
        const int32_t away_x = p0.px - c0.px;
        const int32_t away_y = p0.py - c0.py;
        SDL_MouseMotionEvent edge{};
        edge.type = SDL_MOUSEMOTION;
        edge.x = std::abs(away_x) >= std::abs(away_y) ? psx + (away_x > 0 ? 19 : -20) : psx;
        edge.y = std::abs(away_x) >= std::abs(away_y) ? psy : psy + (away_y > 0 ? 15 : -32);
        app.handle_mouse_motion(edge);
        app.render_frame();
        ASSERT_TRUE(app.hud().current_cursor() == CursorType::Move);
        app.set_others_confirmed(false);
        app.render_frame();
        ASSERT_TRUE(app.hud().current_cursor() == CursorType::Attack);
        app.set_others_confirmed(true);
        app.hud().handle_key_down(SDLK_ESCAPE, app.sim(), app.renderer().camera());        // (deselected, and the pointer back where it rested: the same interface as in the frames before)
        app.handle_mouse_motion(motion);
        app.render_frame();
        ASSERT_TRUE(canvas_region(app, rx, ry, rw, rh) == on);
        made = app.others_worlds_made();
        // ... it is the confirmed engine's: with the prediction off (the screen is the confirmed engine alone) the same place of the frame is the same picture, Alice's ants being far from it
        app.net()->set_prediction_enabled(false);
        app.pump_network(0.0f);
        ASSERT_FALSE(app.net()->predicting());
        ASSERT_TRUE(&app.view_sim() == &app.sim() && !app.others_view_active());
        ASSERT_TRUE(&app.shown_world() == &app.sim().get_world_state());                  // (nothing to compose: the picture is the one engine)
        app.render_frame();
        ASSERT_TRUE(canvas_region(app, rx, ry, rw, rh) == on);
        ASSERT_EQ(app.others_worlds_made(), made);
        app.set_others_confirmed(false);
        duo.step(1500);
        ASSERT_TRUE(app.sim().state_hash() == bob.sim.state_hash());                       // (drawing only: the confirmed engines are the same)
        ASSERT_FALSE(app.net()->desynced() || bob.net.desynced());
    } TEST_END();

    TEST_CASE("PA17 The Player's Own Ants Are Drawn As They Are Today In Both Pictures, Pixel For Pixel: With An Own Ant Walking (The Two Engines Have It Apart, A Lead Of Walking) The Frame Of Others = Confirmed Has It Where The Predicted Engine Has It, The Plain Frame Is The Same There, And It Is Not Where The Confirmed Engine Has It") {
        Application app;
        Peer bob;
        Duo duo{app, bob};
        ASSERT_TRUE(predicting_match(app, bob, duo));
        const uint32_t walker = walk_one_ant_ahead(app, bob, duo, 4);
        ASSERT_TRUE(walker != 0);
        app.net()->freeze();                                                               // (no tick between the frames)
        duo.step(400);
        ASSERT_TRUE(app.net()->predicting());
        const sim::AntSnapshot p = *ant_in(app.view_sim(), walker);
        const sim::AntSnapshot c = *ant_in(app.sim(), walker);
        ASSERT_TRUE(p.px != c.px || p.py != c.py);
        ASSERT_TRUE(place_camera(app, p.px, p.py, 110, 90));
        int32_t psx = 0, psy = 0, csx = 0, csy = 0;
        ASSERT_TRUE(app.renderer().camera().world_to_screen(p.px, p.py, psx, psy));
        ASSERT_TRUE(app.renderer().camera().world_to_screen(c.px, c.py, csx, csy));
        const int32_t rx = std::min(psx, csx) - 30;
        const int32_t ry = std::min(psy, csy) - 44;
        const int32_t rw = std::abs(psx - csx) + 60;
        const int32_t rh = std::abs(psy - csy) + 70;
        const LayoutRect view = app.layout().view();
        SDL_MouseMotionEvent motion{};
        motion.type = SDL_MOUSEMOTION;
        motion.x = view.x + 30;
        motion.y = view.y + view.h - 30;
        app.handle_mouse_motion(motion);
        app.hud().handle_key_down(SDLK_ESCAPE, app.sim(), app.renderer().camera());        // (nothing selected: the interface is the same in every frame)
        app.set_others_confirmed(false);
        app.render_frame();
        const std::vector<uint8_t> plain = canvas_region(app, rx, ry, rw, rh);
        app.set_others_confirmed(true);
        app.render_frame();
        const std::vector<uint8_t> on = canvas_region(app, rx, ry, rw, rh);
        ASSERT_FALSE(plain.empty());
        ASSERT_TRUE(on == plain);                                                          // the own ant is drawn where the predicted engine has it, in both
        const sim::WorldState& world = app.shown_world();
        ASSERT_TRUE(same_ant(*ant_of_world(world, walker), p) && !same_ant(*ant_of_world(world, walker), c));
        app.net()->set_prediction_enabled(false);                                          // and without the prediction the same place of the frame shows the ant where the confirmed engine has it
        app.pump_network(0.0f);
        app.render_frame();
        ASSERT_TRUE(canvas_region(app, rx, ry, rw, rh) != plain);
        duo.step(1500);
        ASSERT_TRUE(app.sim().state_hash() == bob.sim.state_hash());
    } TEST_END();

    TEST_CASE("PA13 Over A Whole Match No Other Player's Ant Is Ever Drawn Ahead Of The Confirmed Engine Or Anywhere It Is Not, Alice's Own Are Always The Predicted Engine's, The World That The Frame And The Pointer Share Is The One That A Fresh Composition Of The Two Engines Would Make At Every Step (Never A Stale One), And The Confirmed Engines End Identical: Two People Order Their Ants About For 40 s, With Fog Of War On And Off") {
        for (const bool fog : {false, true}) {
            Application app;
            Peer bob;
            Duo duo{app, bob};
            ApplicationConfig cfg = host_config();
            cfg.others_confirmed = true;
            ASSERT_TRUE(begin_match(app, bob, duo, cfg, fog));
            ASSERT_TRUE(duo.until([&]() { return app.net()->predicting() && app.sim().current_tick() > 40; }, 20000));
            ASSERT_TRUE(app.others_view_active());
            std::mt19937 rng(fog ? 9u : 4u);
            const auto order = [&](bool alice) {
                const std::vector<uint32_t> ants = alice ? ants_of(app.sim(), 0) : ants_of(bob.sim, 1);
                if (ants.empty()) return;
                sim::Command c;
                c.type = rng() % 5u == 0u ? sim::CommandType::Stop : sim::CommandType::GroupMove;
                c.issuer = alice ? 0 : 1;
                c.tile_x = static_cast<int16_t>(4 + rng() % 30u);
                c.tile_y = static_cast<int16_t>(4 + rng() % 30u);
                c.ants.assign(1u, ants[rng() % ants.size()]);
                if (alice) app.net()->submit(c);
                else bob.net.submit(c);
            };
            size_t ahead_total = 0;
            size_t checks = 0;
            uint64_t elapsed = 0;
            while (elapsed < 40000) {
                if (elapsed % 250 == 0) order(elapsed % 500 == 0);
                duo.step(10);
                elapsed += 10;
                if (!app.others_view_active()) continue;
                size_t ahead = 0;
                ASSERT_TRUE(picture_is_made_of_both(app, 0, ahead));
                ahead_total += ahead;
                ++checks;
                // never stale: the world that stands is the world that a fresh composition of the two engines makes now
                OthersView fresh;
                const sim::WorldState& made = fresh.compose_now(app.view_sim().get_world_state(), app.sim().get_world_state(), 0);
                const sim::WorldState& stands = app.shown_world();
                ASSERT_EQ(stands.ants.size(), made.ants.size());
                for (size_t i = 0; i < made.ants.size(); ++i) ASSERT_TRUE(same_ant(stands.ants[i], made.ants[i]));
                ASSERT_TRUE(stands.effects.size() == made.effects.size() && stands.score_bubbles.size() == made.score_bubbles.size() && stands.player_scores == made.player_scores);
                if (fog) {
                    for (const sim::AntSnapshot& a : stands.ants) {                          // (the gate of the picture, asked of the world that is drawn: what it draws of another player is on ground that the confirmed engine has explored)
                        if (a.player_id != 0 && health::ant_drawn(stands, 0, a)) ASSERT_TRUE(app.sim().get_world_state().is_tile_revealed(a.tile_x, a.tile_y));
                    }
                }
            }
            ASSERT_TRUE(checks > 3000);
            ASSERT_TRUE(ahead_total > 100);                                                // (the test is not empty: the predicted engine had other players' ants ahead of the confirmed one in many steps, and the picture had none)
            app.net()->freeze();
            duo.step(1500);
            ASSERT_TRUE(app.sim().state_hash() == bob.sim.state_hash());                   // (nothing of it reached the simulation)
            ASSERT_FALSE(app.net()->desynced() || bob.net.desynced());
        }
    } TEST_END();

    TEST_CASE("PA14 No Other Player's Ant Ever Slides In The Mode Of Others = Confirmed: A Turn Of Bob's That Moves His Ant In The Predicted Engine Slides It In The Plain Picture And Not In This One (It Is Drawn Where The Confirmed Engine Has It, Never Corrected), While The Player's Own Ants Slide When The Prediction Stops Or Begins As They Did; Both Pictures Are Drawn From The Same Match") {
        size_t slid_plain = 0;
        for (const bool others : {false, true}) {
            Application app;
            Peer bob;
            Duo duo{app, bob};
            ApplicationConfig cfg = host_config();
            cfg.others_confirmed = others;
            ASSERT_TRUE(begin_match(app, bob, duo, cfg));
            ASSERT_TRUE(duo.until([&]() { return app.net()->predicting() && app.sim().current_tick() > 40; }, 20000));
            const std::vector<uint32_t> bobs = ants_of(app.sim(), 1);
            ASSERT_FALSE(bobs.empty());
            const sim::AntSnapshot* start = ant_in(app.sim(), bobs[0]);
            ASSERT_TRUE(start != nullptr);
            ASSERT_TRUE(place_camera(app, start->px, start->py, 0, 0));
            size_t slid_bobs = 0;
            shaped::SlideCause cause;                                                      // (the one slide that an ant of Bob's can have is the hop of his own ant's snap: the engine moves it in the same frame)
            uint64_t elapsed = 0;
            std::mt19937 rng(5);
            while (elapsed < 12000) {
                if (elapsed % 400 == 0) {                                                  // Bob steers a walking ant: a turn that the predicted engine did not have, so that it is rebuilt and moves his ant
                    sim::Command c;
                    c.type = sim::CommandType::GroupMove;
                    c.issuer = 1;
                    c.tile_x = static_cast<int16_t>(start->tile_x - 6 + static_cast<int>(rng() % 5u));
                    c.tile_y = static_cast<int16_t>(start->tile_y + 2 + static_cast<int>(rng() % 6u));
                    c.ants.assign(1u, bobs[rng() % std::min<size_t>(bobs.size(), 4u)]);
                    bob.net.submit(c);
                }
                duo.step(10);
                elapsed += 10;
                if (elapsed % 30 != 0) continue;
                app.render_frame();                                                        // (the frame is where the shifts of a rebuild are taken up)
                for (const uint32_t id : bobs) {
                    int32_t dx = 0, dy = 0;
                    const bool slid = app.renderer().view_glide().offset_of(id, dx, dy);
                    if (slid) ++slid_bobs;
                    const sim::AntSnapshot* drawn = nullptr;
                    for (const sim::AntSnapshot& a : app.shown_world().ants) {
                        if (a.id == id) drawn = &a;
                    }
                    ASSERT_TRUE(drawn != nullptr);
                    bool began = false;
                    const bool unexplained = cause.unexplained(*drawn, dx, dy, slid, began);
                    if (others) {
                        ASSERT_FALSE(unexplained);                                         // not one frame of one ant of Bob's slides, but by the hop of its own snap: no correction is ever drawn
                    }
                }
            }
            if (!others) slid_plain = slid_bobs;
            if (others) {
                // the player's own ants slide when the prediction stops, as they do in the plain picture; Bob's still do not (they are where they were drawn: the confirmed engine's)
                const std::vector<uint32_t> mine = ants_of(app.sim(), 0);
                ASSERT_FALSE(mine.empty());
                app.renderer().view_glide().clear();
                const std::vector<uint32_t> own_ants = mine;
                const sim::AntSnapshot* p = ant_in(app.view_sim(), own_ants[0]);
                ASSERT_TRUE(p != nullptr);
                sim::Command walk;                                                         // the player's own ant walks, so that the two engines have it apart
                walk.type = sim::CommandType::GroupMove;
                walk.issuer = 0;
                int16_t gx = 0, gy = 0;
                ASSERT_TRUE(far_goal(app.sim(), 0, gx, gy));
                walk.tile_x = gx;
                walk.tile_y = gy;
                walk.ants = own_ants;
                app.net()->submit(walk);
                duo.step(900);
                app.render_frame();
                app.renderer().view_glide().clear();
                app.set_page_hidden(true);                                                  // the prediction is suspended: the screen goes from the predicted engine back to the confirmed one, and the own ants are moved by the lead
                app.pump_network(0.0f);
                ASSERT_FALSE(app.net()->predicting());
                ASSERT_TRUE(app.net()->prediction() != nullptr);
                app.render_frame();
                size_t own_slid = 0;
                size_t bob_slid = 0;
                for (const uint32_t id : own_ants) {
                    int32_t dx = 0, dy = 0;
                    own_slid += app.renderer().view_glide().offset_of(id, dx, dy) ? 1u : 0u;
                }
                for (const uint32_t id : bobs) {
                    int32_t dx = 0, dy = 0;
                    bob_slid += app.renderer().view_glide().offset_of(id, dx, dy) ? 1u : 0u;
                }
                ASSERT_TRUE(own_slid >= 1);                                                // the player's own ant is carried to where the confirmed engine has it ...
                ASSERT_EQ(bob_slid, 0u);                                                   // ... and Bob's ants, which the picture drew from the confirmed engine all the time, did not move at all
            }
            app.net()->freeze();
            duo.step(1500);
            ASSERT_TRUE(app.sim().state_hash() == bob.sim.state_hash());
        }
        ASSERT_TRUE(slid_plain >= 3);                                                      // (the plain picture does slide Bob's ants when his turns arrive: the test is not empty)
    } TEST_END();

    TEST_CASE("PA15 With Fog Of War Nothing Of Another Player Is Drawn Earlier Than The Confirmed Engine Has It Where The Player Can See: An Ant Of Bob's That Walks Into The Ground That Alice Has Explored Is Drawn By The Plain Picture (The Predicted Engine Has It There A Lead Sooner) And Not Yet By This One, Which Waits For The Confirmed Engine; The Pointer Finds Nothing Where Nothing Is Drawn, And An Attack Aimed At The Predicted Tile Is Only Aimed There When The Picture Would Draw The Ant There") {
        Application app;
        Peer bob;
        Duo duo{app, bob};
        ApplicationConfig cfg = host_config();
        cfg.others_confirmed = true;
        ASSERT_TRUE(begin_match(app, bob, duo, cfg, true));
        ASSERT_TRUE(duo.until([&]() { return app.net()->predicting() && app.sim().current_tick() > 40; }, 20000));
        ASSERT_TRUE(app.sim().is_fog_of_war_enabled());
        // Bob's ant walks to Alice's hill; the match is stopped at the first frame in which the predicted engine has it on ground that the confirmed engine has explored and the confirmed engine does not
        const std::vector<uint32_t> bobs = ants_of(app.sim(), 1);
        ASSERT_FALSE(bobs.empty());
        int16_t gx = 0;
        int16_t gy = 0;
        ASSERT_TRUE(far_goal(app.sim(), 0, gx, gy, 3, 6));
        sim::Command go;
        go.type = sim::CommandType::GroupMove;
        go.issuer = 1;
        go.tile_x = gx;
        go.tile_y = gy;
        go.ants.assign(1u, bobs[0]);
        bob.net.submit(go);
        const uint32_t walker = bobs[0];
        const auto crossing = [&]() {
            const sim::WorldState& shown = app.view_sim().get_world_state();
            const sim::WorldState& confirmed = app.sim().get_world_state();
            const sim::AntSnapshot* p = ant_of_world(shown, walker);
            const sim::AntSnapshot* c = ant_of_world(confirmed, walker);
            return p != nullptr && c != nullptr && health::ant_drawn(shown, 0, *p) && !health::ant_drawn(confirmed, 0, *c);
        };
        ASSERT_TRUE(duo.until(crossing, 90000));
        app.net()->freeze();                                                               // (no tick between the frames from here)
        const sim::AntSnapshot p = *ant_in(app.view_sim(), walker);
        const sim::AntSnapshot c = *ant_in(app.sim(), walker);
        // the plain picture would draw the ant; this one does not (the ant of the world that is drawn stands on ground that is covered)
        const sim::WorldState& world = app.shown_world();
        const sim::AntSnapshot* in_world = ant_of_world(world, walker);
        ASSERT_TRUE(in_world != nullptr && same_ant(*in_world, c));
        ASSERT_FALSE(health::ant_drawn(world, 0, *in_world));
        ASSERT_TRUE(health::ant_drawn(app.view_sim().get_world_state(), 0, p));
        ASSERT_FALSE(world.is_tile_revealed(c.tile_x, c.tile_y));
        ASSERT_TRUE(world.is_tile_revealed(p.tile_x, p.tile_y));                             // (the veil is the confirmed engine's in both: the predicted tile is explored)
        // nothing is there for the pointer: with an own ant selected the cursor over the ant (the picture draws nothing there) is the plain move cursor, where the plain picture shows the ant under the attack
        // cursor at the place that the predicted engine has it
        const std::vector<uint32_t> alice = ants_of(app.sim(), 0);
        ASSERT_FALSE(alice.empty());
        app.hud().select_ant(alice[0]);
        ASSERT_TRUE(place_camera(app, p.px, p.py, 40, 30));
        int32_t csx = 0, csy = 0, psx = 0, psy = 0;
        ASSERT_TRUE(app.renderer().camera().world_to_screen(c.px, c.py - 8, csx, csy));
        ASSERT_TRUE(app.renderer().camera().world_to_screen(p.px, p.py - 8, psx, psy));
        ASSERT_TRUE(app.hud().evaluate_cursor(csx, csy, world, app.view_sim().grid(), app.renderer().camera()) == CursorType::Move);       // (the pointer's own tile is covered: the cursor ignores the ant, as the original's does)
        ASSERT_TRUE(app.hud().evaluate_cursor(psx, psy, app.view_sim().get_world_state(), app.view_sim().grid(), app.renderer().camera()) == CursorType::Attack);
        // the aim: where the picture would draw the ant at the predicted tile the order is aimed there, where it would not it is not
        sim::TileCoord tile{c.tile_x, c.tile_y};
        ASSERT_TRUE(aim_at_predicted_ant(app.view_sim().get_world_state(), 0, c, tile));     // (the predicted tile is explored: a drawn ant of the confirmed engine would be aimed there)
        ASSERT_TRUE(tile.x == p.tile_x && tile.y == p.tile_y);
        duo.step(1500);
        ASSERT_TRUE(app.sim().state_hash() == bob.sim.state_hash());
    } TEST_END();

    TEST_CASE("PA16 The Switch: Others = Confirmed Is On Unless The Command Line Says --others predicted (The Last Word Wins, A Value That Is No Choice Is Refused With The Reason), It Does Nothing Without The Prediction (Told Not To, Or In A Game Of One Machine), And Off It Changes Nothing: The Frame, The HUD And The Glide Are The Plain Ones, No World Is Made And The Match Ends In The Same State") {
        // (the plain picture, --others predicted, expires with v0.22.3 at the latest: this test goes with it, AGENTS.md rule 9)
        std::vector<std::string> args;
        std::vector<char*> st;
        const auto parse = [&](std::vector<std::string> a) {
            args = std::move(a);
            st.clear();
            for (std::string& x : args) st.push_back(x.data());
            st.push_back(nullptr);
            return Application::parse_arguments(static_cast<int>(args.size()), st.data());
        };
        ApplicationConfig c = parse({"ants"});
        ASSERT_TRUE(c.others_confirmed && c.startup_error.empty());         // on by default: the command line said nothing
        c = parse({"ants", "--others", "confirmed"});
        ASSERT_TRUE(c.others_confirmed && c.startup_error.empty());
        c = parse({"ants", "--others", "predicted"});
        ASSERT_TRUE(!c.others_confirmed && c.startup_error.empty());
        c = parse({"ants", "--others", "maybe"});
        ASSERT_EQ(c.startup_error, std::string("--others needs confirmed or predicted"));
        c = parse({"ants", "--others"});
        ASSERT_EQ(c.startup_error, std::string("--others needs confirmed or predicted"));
        c = parse({"ants", "--others", "predicted", "--others", "confirmed"});
        ASSERT_TRUE(c.others_confirmed);                                                   // the last one wins
        c = parse({"ants", "--others", "confirmed", "--others", "predicted", "--no-prediction"});
        ASSERT_TRUE(!c.others_confirmed && !c.prediction);                                 // (the two switches are independent: the picture is the plain one, and there is no prediction to draw)
        // --others predicted: a whole match is the plain picture, the HUD is not told of another world, no world is made, and Bob's ants slide as before
        {
            ApplicationConfig cfg = host_config();
            cfg.others_confirmed = false;
            Application app;
            Peer bob;
            Duo duo{app, bob};
            ASSERT_TRUE(predicting_match(app, bob, duo, cfg));
            ASSERT_FALSE(app.others_view_active());
            for (int i = 0; i < 40; ++i) {
                duo.step(50);
                ASSERT_TRUE(&app.shown_world() == &app.view_sim().get_world_state());
                app.render_frame();
            }
            ASSERT_EQ(app.others_worlds_made(), 0u);
            app.net()->freeze();
            duo.step(1500);
            ASSERT_TRUE(app.sim().state_hash() == bob.sim.state_hash());
        }
        // the default: a match that predicts is drawn from both engines, and nothing but the picture differs (the confirmed engines end alike)
        {
            Application app;
            Peer bob;
            Duo duo{app, bob};
            ASSERT_TRUE(predicting_match(app, bob, duo));
            ASSERT_TRUE(app.others_view_active());
            for (int i = 0; i < 40; ++i) {
                duo.step(50);
                ASSERT_TRUE(&app.shown_world() != &app.view_sim().get_world_state());
                app.render_frame();
            }
            ASSERT_TRUE(app.others_worlds_made() > 0u);
            app.net()->freeze();
            duo.step(1500);
            ASSERT_TRUE(app.sim().state_hash() == bob.sim.state_hash());
        }
        // asked for (the default), with the prediction told off: nothing is made either (the picture is the confirmed engine alone)
        {
            ApplicationConfig cfg = host_config();
            cfg.prediction = false;
            Application app;
            Peer bob;
            Duo duo{app, bob};
            ASSERT_TRUE(begin_match(app, bob, duo, cfg));
            ASSERT_TRUE(duo.until([&]() { return app.sim().current_tick() > 60; }, 20000));
            ASSERT_FALSE(app.net()->predicting() || app.others_view_active());
            for (int i = 0; i < 20; ++i) {
                duo.step(50);
                ASSERT_TRUE(&app.shown_world() == &app.sim().get_world_state());
                app.render_frame();
            }
            ASSERT_EQ(app.others_worlds_made(), 0u);
            app.net()->freeze();
            duo.step(1500);
            ASSERT_TRUE(app.sim().state_hash() == bob.sim.state_hash());
        }
    } TEST_END();

    TEST_CASE("PA18 The World Of Both Engines Is Let Go Of When A Match Ends And When A Match Begins, So That No Match Is Ever Shown The One Before's World For A Moment Whatever Its Stamp: It Stands, Shared By Every Ask Of A Moment, Until The Match Is Over (Or, For A New Match, Begins), And The Next Ask Makes It Again") {
        {
            Application app;
            Peer bob;
            Duo duo{app, bob};
            ASSERT_TRUE(predicting_match(app, bob, duo));
            ASSERT_TRUE(app.others_view_active());
            app.shown_world();
            ASSERT_TRUE(app.others_world_standing());                                      // the world of this moment is kept ...
            const uint64_t made = app.others_worlds_made();
            app.shown_world();
            ASSERT_EQ(app.others_worlds_made(), made);                                     // ... and every ask of the moment shares it
            sim::Command quit;                                                             // Bob leaves: the match is over
            quit.type = sim::CommandType::Quit;
            quit.issuer = 1;
            ASSERT_EQ(bob.net.submit(quit).status, sim::CommandResult::Status::Applied);
            ASSERT_TRUE(duo.until([&]() { return app.sim().is_match_over(); }, 15000));
            ASSERT_FALSE(app.others_world_standing());                                     // the end let go of it
        }
        {
            Application app;                                                               // a match begins (a game that this machine starts, after a match of the network that was left): the world
            Peer bob;                                                                      // that the match before made is not the new match's
            Duo duo{app, bob};
            ASSERT_TRUE(predicting_match(app, bob, duo));
            app.shown_world();
            ASSERT_TRUE(app.others_world_standing());
            app.return_to_map_select();                                                    // (a match that is left ends no match: nothing lets go of the world here)
            ASSERT_TRUE(app.others_world_standing());
            ASSERT_TRUE(app.start_game(maps_dir() + "SMALL.LVL"));
            ASSERT_FALSE(app.others_world_standing());                                     // the start let go of it
        }
    } TEST_END();

    TEST_CASE("PA5 A Hidden Page Shows Nothing: The Prediction Is Suspended While It Is Hidden (The Wake-Ups Step The Match, No Picture Needs The Predicted Engine) And Begins Again, With The Next Tick, When The Page Is Shown") {
        Application app;
        Peer bob;
        Duo duo{app, bob};
        ASSERT_TRUE(predicting_match(app, bob, duo));
        const uint64_t starts = app.net()->prediction()->stats().starts;
        app.set_page_hidden(true);
        duo.step(200);
        ASSERT_FALSE(app.net()->predicting());                                              // suspended at the next pump of the net
        ASSERT_TRUE(&app.view_sim() == &app.sim());
        duo.step(500);
        ASSERT_EQ(app.net()->prediction()->stats().starts, starts);                         // (not begun again meanwhile)
        app.set_page_hidden(false);
        ASSERT_TRUE(duo.until([&]() { return app.net()->predicting(); }, 2000));
        ASSERT_EQ(app.net()->prediction()->stats().starts, starts + 1);
        ASSERT_TRUE(&app.view_sim() != &app.sim());
        app.net()->freeze();
        duo.step(1500);
        ASSERT_TRUE(app.sim().state_hash() == bob.sim.state_hash());
        ASSERT_FALSE(app.net()->desynced() || bob.net.desynced());
    } TEST_END();

    TEST_CASE("PA6 The Delay Of An Order (The Browser Measurement's) Is What The Click Feels While The Prediction Is On: Nothing Until An Order Has Been Felt, Then The Time From The Frame That Took It To The Frame That Shows It (Less Than A Tick And A Frame), The Network's Delay (The Confirmed Engine's) When It Is Off") {
        Application app;
        Peer bob;
        Duo duo{app, bob};
        uint64_t counter = 1000;                                                            // a virtual clock of frames: 10 ms for every frame that is run below
        const uint64_t per_frame = SDL_GetPerformanceFrequency() / 100;
        app.set_clock([&]() { return counter; });
        ASSERT_TRUE(predicting_match(app, bob, duo));
        // a frame as the window runs it (the input is handled at its start, then the net, the simulation and the drawing), the peer alongside
        const auto frame = [&]() {
            counter += per_frame;
            app.pump_network(0.010f);
            app.update_simulation(0.010f);
            app.render_frame();
            bob.now += 10;
            bob.update();
        };
        for (int i = 0; i < 20; ++i) frame();
        ASSERT_TRUE(app.net()->predicting());
        ASSERT_FALSE(app.order_delay_ms().has_value());                                    // no order has been felt: nothing (the network's delay is not what the click feels)
        // select an own ant and order it with the right button
        const std::vector<uint32_t> mine = ants_of(app.sim(), 0);
        ASSERT_TRUE(!mine.empty());
        const sim::AntSnapshot* ant = ant_in(app.view_sim(), mine[0]);
        ASSERT_TRUE(ant != nullptr);
        int16_t gx = 0, gy = 0;
        ASSERT_TRUE(far_goal(app.sim(), 0, gx, gy));
        int32_t sx = 0, sy = 0;
        ASSERT_TRUE(place_camera(app, ant->px, ant->py, 0, 0));
        ASSERT_TRUE(app.renderer().camera().world_to_screen(ant->px, ant->py, sx, sy));
        counter += per_frame;                                                               // (the input of a frame is handled at its start)
        click(app, SDL_BUTTON_LEFT, sx, sy);
        ASSERT_EQ(app.hud().get_selected_ant_id(), mine[0]);
        ASSERT_EQ(app.felt_delay().pending(), size_t{0});                                   // (a selection is no order)
        const int32_t goal_wx = gx * 32 + 16;
        const int32_t goal_wy = gy * 32 + 16;
        ASSERT_TRUE(place_camera(app, goal_wx, goal_wy, (ant->px - goal_wx) / 2, (ant->py - goal_wy) / 2));
        int32_t gsx = 0, gsy = 0;
        ASSERT_TRUE(app.renderer().camera().world_to_screen(goal_wx, goal_wy, gsx, gsy));
        frame();
        const uint64_t predicted_before = app.net()->prediction()->stats().commands_predicted;
        counter += per_frame;
        click(app, SDL_BUTTON_RIGHT, gsx, gsy);                                             // the frame that takes the order ...
        ASSERT_EQ(app.net()->prediction()->stats().commands_predicted, predicted_before + 1);
        ASSERT_EQ(app.felt_delay().pending(), size_t{1});
        ASSERT_FALSE(app.felt_delay().measured());
        for (int i = 0; i < 12 && !app.felt_delay().measured(); ++i) frame();               // ... and the frame that shows it: the predicted engine's next tick, then a drawn frame
        ASSERT_TRUE(app.felt_delay().measured());
        ASSERT_EQ(app.felt_delay().samples(), size_t{1});
        ASSERT_EQ(app.felt_delay().pending(), size_t{0});
        ASSERT_TRUE(app.felt_delay().felt_ms() <= 60);                                      // within a tick (50 ms) and the frame that draws it
        ASSERT_TRUE(app.order_delay_ms().has_value() && *app.order_delay_ms() == app.felt_delay().felt_ms());
        // the number is about now only for ten seconds: the frame clock runs on (the net's clock does not, so the network's delay is still the fresh number that it was), and it
        // says nothing rather than the network's delay, which is not what the click feels
        for (int i = 0; i < 30; ++i) frame();                                               // (the order reaches the confirmed engine meanwhile)
        ASSERT_TRUE(app.net()->command_delay_ms().has_value());
        counter += 11ull * 1000ull * (SDL_GetPerformanceFrequency() / 1000ull);
        ASSERT_FALSE(app.order_delay_ms().has_value());
        ASSERT_TRUE(app.net()->command_delay_ms().has_value());
        // off: it is what the confirmed engine's delay is (nothing while no order has been applied there; the order is by now)
        ASSERT_TRUE(duo.until([&]() { return app.net()->command_delay_ms().has_value(); }, 3000));
        app.net()->set_prediction_enabled(false);
        app.pump_network(0.0f);
        ASSERT_FALSE(app.net()->predicting());
        ASSERT_TRUE(app.order_delay_ms().has_value() && *app.order_delay_ms() == *app.net()->command_delay_ms());
        app.net()->freeze();
        for (int i = 0; i < 150; ++i) frame();
        ASSERT_TRUE(app.sim().state_hash() == bob.sim.state_hash());
    } TEST_END();

    TEST_CASE("PA7 The Cue Router Runs Only While There Is A Prediction Or A Cue That It Played Still Waits For Its Copy: A Match That Is Told Not To Predict Never Runs It (An Own Ant's Cues Are The Confirmed Engine's, As They Were); Switched Off At Run Time While The Predicted Engine's Cue Waits For Its Copy, It Still Drops That Copy (No Second Sound) And Is Idle Again After It") {
        {                                                                                  // a match that is told not to predict: nothing is routed
            ApplicationConfig cfg = host_config();
            cfg.prediction = false;
            Application app;
            Peer bob;
            Duo duo{app, bob};
            ASSERT_TRUE(begin_match(app, bob, duo, cfg));
            ASSERT_TRUE(duo.until([&]() { return app.sim().current_tick() > 60; }, 20000));
            ASSERT_TRUE(app.net()->prediction() == nullptr && app.cue_router().idle());
            sim::Command go;
            ASSERT_TRUE(food_order(app, go));
            ASSERT_EQ(app.net()->submit(go).status, sim::CommandResult::Status::Applied);
            const uint32_t ant = go.ants[0];
            ASSERT_TRUE(duo.until([&]() {                                                  // it harvests: the engine made the cues of the ant's own action
                const sim::AntSnapshot* a = ant_in(app.sim(), ant);
                return a != nullptr && (a->is_holding || a->carried_points > 0);
            }, 30000));
            duo.step(300);
            const net::CueRouter::Stats& st = app.cue_router().stats();                    // (a router that ran would have counted them: the confirmed engine's cues of an own ant's own action)
            ASSERT_EQ(st.played_predicted + st.replays_dropped + st.repeats_dropped + st.duplicates_dropped + st.played_confirmed_own + st.phantoms, uint64_t{0});
            ASSERT_TRUE(app.cue_router().idle());
            app.net()->freeze();
            duo.step(1500);
            ASSERT_TRUE(app.sim().state_hash() == bob.sim.state_hash());
        }
        {                                                                                  // switched off at run time with a cue that waits for its copy
            Application app;
            Peer bob;
            Duo duo{app, bob};
            ASSERT_TRUE(predicting_match(app, bob, duo));
            sim::Command go;
            ASSERT_TRUE(food_order(app, go));
            const size_t channels_before = app.audio_mixer().active_channel_count();
            ASSERT_EQ(app.net()->submit(go).status, sim::CommandResult::Status::Applied);
            for (int i = 0; i < 3000 && app.cue_router().stats().played_predicted == 0; ++i) duo.step(10);
            const uint64_t played = app.cue_router().stats().played_predicted;
            ASSERT_TRUE(played > 0);
            ASSERT_EQ(app.audio_mixer().active_channel_count(), channels_before + 1);      // (heard from the predicted engine, which is ahead: the confirmed engine has not made it yet)
            ASSERT_EQ(app.cue_router().stats().duplicates_dropped, 0u);
            app.net()->set_prediction_enabled(false);
            app.pump_network(0.0f);
            ASSERT_TRUE(app.net()->prediction() == nullptr);                               // the prediction is gone ...
            ASSERT_FALSE(app.cue_router().idle());                                         // ... and the cue that it played still waits for its copy
            for (int i = 0; i < 300 && !app.cue_router().idle(); ++i) {                    // the confirmed engine makes the same cues a few ticks later: they are the copies
                duo.step(10);
                ASSERT_TRUE(app.audio_mixer().active_channel_count() <= channels_before + 1);          // (never a second sound)
            }
            ASSERT_TRUE(app.cue_router().idle());
            const net::CueRouter::Stats& st = app.cue_router().stats();
            ASSERT_EQ(st.duplicates_dropped, played);                                      // every cue that was played from the predicted engine met its copy
            ASSERT_EQ(st.phantoms, 0u);
            app.net()->freeze();
            duo.step(1500);
            ASSERT_TRUE(app.sim().state_hash() == bob.sim.state_hash());
            ASSERT_FALSE(app.net()->desynced() || bob.net.desynced());
        }
    } TEST_END();

    TEST_CASE("PA8 A Prediction That Is Switched Off At Run Time Is Destroyed, And The HUD's Special-Target Question Goes Back To The Confirmed Engine At Once (A Pointer To The Predicted Engine Never Outlives It): The Question Is Asked Of The Confirmed Engine Afterwards, Before Any Frame Has Set It Anew") {
        Application app;
        Peer bob;
        Duo duo{app, bob};
        ASSERT_TRUE(predicting_match(app, bob, duo));
        const std::vector<uint32_t> mine = ants_of(app.sim(), 0);
        ASSERT_TRUE(!mine.empty());
        const sim::AntSnapshot* ant = ant_in(app.view_sim(), mine[0]);
        ASSERT_TRUE(ant != nullptr);
        const int32_t ant_wx = ant->px;
        const int32_t ant_wy = ant->py;
        int32_t sx = 0, sy = 0;
        ASSERT_TRUE(place_camera(app, ant_wx, ant_wy, 0, 0));
        ASSERT_TRUE(app.renderer().camera().world_to_screen(ant_wx, ant_wy, sx, sy));
        click(app, SDL_BUTTON_LEFT, sx, sy);                                               // an own ant is selected: the cursor over open ground asks the engine about a special target
        ASSERT_EQ(app.hud().get_selected_ant_id(), mine[0]);
        app.render_frame();
        const sim::SimulationEngine* predicted = &app.view_sim();
        ASSERT_TRUE(predicted != &app.sim() && app.hud().sim_query() == predicted);       // (the frame asks the engine that it shows)
        int16_t gx = 0, gy = 0;
        ASSERT_TRUE(far_goal(app.sim(), 0, gx, gy));
        const int32_t goal_wx = gx * 32 + 16;
        const int32_t goal_wy = gy * 32 + 16;
        ASSERT_TRUE(place_camera(app, goal_wx, goal_wy, 0, 0));
        int32_t gsx = 0, gsy = 0;
        ASSERT_TRUE(app.renderer().camera().world_to_screen(goal_wx, goal_wy, gsx, gsy));
        app.net()->set_prediction_enabled(false);
        app.pump_network(0.0f);
        ASSERT_TRUE(app.net()->prediction() == nullptr);                                   // destroyed, not suspended
        // the cursor over open ground with the ant selected asks the query about a special target, with no frame drawn since (the first thing is the read: under a sanitizer a query that still
        // pointed at the destroyed engine is a heap-use-after-free right here)
        const CursorType cursor = app.hud().evaluate_cursor(gsx, gsy, app.sim().get_world_state(), app.sim().grid(), app.renderer().camera());
        ASSERT_TRUE(cursor == CursorType::Move || cursor == CursorType::Target);           // open ground, an own ant selected
        ASSERT_TRUE(app.hud().sim_query() == &app.sim());                                  // at once: no frame has been drawn since
        app.render_frame();
        ASSERT_TRUE(app.hud().sim_query() == &app.sim());
        app.net()->freeze();
        duo.step(1500);
        ASSERT_TRUE(app.sim().state_hash() == bob.sim.state_hash());
        ASSERT_FALSE(app.net()->desynced() || bob.net.desynced());
    } TEST_END();

    std::cout << "\n=======================================================\n"
              << " PREDICTION APPLICATION TEST SUMMARY\n=======================================================\n"
              << " Total Test Cases: " << g_test_count << "\n Total Assertions: " << g_assert_count << "\n Passed:           " << (g_test_count - g_test_failures) << "\n Failed:           "
              << g_test_failures << "\n=======================================================\n";
    if (g_test_count == 0) {                                                                // (a misspelt filter must not turn the suite green)
        std::cout << "\n no test ran: the filter ANTS_TEST_FILTER matches no test of this suite\n";
        return 1;
    }
    return g_test_failures == 0 ? 0 : 1;
}
