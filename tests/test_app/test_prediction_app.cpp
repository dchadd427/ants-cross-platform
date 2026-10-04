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

#include "ants_app/application.hpp"
#include "ants_app/hud.hpp"
#include "ants_assets/lvl_parser.hpp"
#include "ants_net/netgame.hpp"
#include "ants_net/protocol.hpp"
#include "ants_sim/command.hpp"
#include "ants_sim/sim_engine.hpp"
#include "ants_test_paths.hpp"

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
#include <string>
#include <thread>
#include <vector>

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

#define TEST_CASE(name) run_test_case(name, [&]()
#define TEST_END() );
#define ASSERT_TRUE(cond) \
    do { \
        ++g_assert_count; \
        if (!(cond)) { \
            std::cout << "FAILED!\n    Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_test_failures; \
            return; \
        } \
    } while (0)
#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))
#define ASSERT_EQ(a, b) ASSERT_TRUE((a) == (b))

namespace {

std::string maps_dir() { return std::string(ORIGINAL_ASSETS_DIR) + "/Maps/"; }

// The other machine of a test: a simulation and a NetGame, with the little that the application does for the room (load the map, report)
struct Peer {
    sim::SimulationEngine sim;
    net::NetGame net{sim};
    uint32_t now{1000};

    Peer() { net.set_discovery(0); }                                       // (the tests do not announce on the real network)
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
                sim.init(level, s.seed, s.roster);
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
            std::this_thread::sleep_for(std::chrono::microseconds(300));
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

// The prediction is OFF by default (opt-in); the tests that need it ask for it as `--prediction on` does
ApplicationConfig predicting_config() {
    ApplicationConfig cfg = host_config();
    cfg.prediction = true;
    cfg.prediction_given = true;
    return cfg;
}

// The application hosts a room with Bob in it, SMALL.LVL, START: both play, the application is seat 0 and Bob seat 1. Returns when the match screen is up on both.
bool begin_match(Application& app, Peer& bob, Duo& duo, const ApplicationConfig& config = predicting_config()) {
    if (!app.init(config) || !app.network_active()) return false;
    if (!bob.net.join("127.0.0.1", app.net()->listen_port(), "Bob")) return false;
    if (!duo.until([&]() { return app.net()->can_start(); }, 8000)) return false;
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

// An open ground tile 10 tiles and more from a team's hill (a goal that the path finder accepts)
bool far_goal(const sim::SimulationEngine& sim, uint8_t team, int16_t& gx, int16_t& gy) {
    const auto* hill = sim.grid().find_anthill(team);
    if (hill == nullptr) return false;
    for (int32_t d = 10; d <= 14; ++d) {
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

// The match is up and the application predicts: the dialog is over, the first turns have run
bool predicting_match(Application& app, Peer& bob, Duo& duo) {
    if (!begin_match(app, bob, duo)) return false;
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
        const sim::Grid& grid = app.sim().grid();
        const auto* hill = grid.find_anthill(0);
        ASSERT_TRUE(hill != nullptr);
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
        ASSERT_TRUE(food.x >= 0);
        const std::vector<uint32_t> mine = ants_of(app.sim(), 0);
        ASSERT_TRUE(!mine.empty());
        sim::Command go;
        go.type = sim::CommandType::GroupMove;
        go.issuer = 0;
        go.tile_x = static_cast<int16_t>(food.x);
        go.tile_y = static_cast<int16_t>(food.y);
        go.ants = {mine[0]};
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


    TEST_CASE("PA4 The Switches: Off By Default (Nothing Said, No Key: The Match Shows The Confirmed Engine And Nothing Is Predicted); --prediction on And The Settings' Key `prediction` = on Turn It On, --prediction off (And --no-prediction) Off; The Command Line Wins; Anything Else Is Refused On The Command Line And Ignored In The Settings") {
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
        ASSERT_TRUE(!c.prediction && !c.prediction_given && c.startup_error.empty());      // off by default (opt-in), the command line said nothing
        c = parse({"ants", "--prediction", "off"});
        ASSERT_TRUE(!c.prediction && c.prediction_given && c.startup_error.empty());
        c = parse({"ants", "--prediction", "on"});
        ASSERT_TRUE(c.prediction && c.prediction_given && c.startup_error.empty());
        c = parse({"ants", "--no-prediction"});
        ASSERT_TRUE(!c.prediction && c.prediction_given && c.startup_error.empty());
        c = parse({"ants", "--prediction", "maybe"});
        ASSERT_EQ(c.startup_error, std::string("--prediction needs on or off"));           // refused with the reason: the game does not start
        ASSERT_FALSE(c.prediction_given);
        c = parse({"ants", "--prediction"});
        ASSERT_EQ(c.startup_error, std::string("--prediction needs on or off"));
        c = parse({"ants", "--prediction", "off", "--prediction", "on"});
        ASSERT_TRUE(c.prediction && c.prediction_given);                                   // the last one wins
        c = parse({"ants", "--prediction", "on", "--no-prediction"});
        ASSERT_TRUE(!c.prediction && c.prediction_given);
        // the settings' key: an Application that remembers its options in a file
        const std::string settings = (std::filesystem::temp_directory_path() / ("ants_prediction_app_settings_" + std::to_string(static_cast<long long>(::getpid())) + ".ini")).string();
        const auto with_settings = [&](const std::string& line, bool given, bool given_value, bool expect_wanted) {
            {
                std::ofstream out(settings);
                out << "Sound Volume=50\n" << line << "\n";
            }
            ApplicationConfig cfg = host_config();
            cfg.settings_path = settings;
            cfg.prediction_given = given;
            cfg.prediction = given_value;
            Application app;
            if (!app.init(cfg)) return false;
            return app.prediction_wanted() == expect_wanted && app.net() != nullptr && app.net()->prediction_enabled() == expect_wanted;
        };
        ASSERT_TRUE(with_settings("prediction=off", false, false, false));                 // the key says off, the command line says nothing: off
        ASSERT_TRUE(with_settings("prediction=on", false, false, true));                   // the key turns it on
        ASSERT_TRUE(with_settings("prediction=off", true, true, true));                    // the command line (on) wins over the key
        ASSERT_TRUE(with_settings("prediction=on", true, false, false));                   // ... and --prediction off over the key's on
        ASSERT_TRUE(with_settings("prediction=maybe", false, false, false));               // a value that is no switch is reported and ignored: the default, off
        ASSERT_TRUE(with_settings("Music Volume=40", false, false, false));                // no key: off (the default)
        ASSERT_TRUE(with_settings("Music Volume=40", true, true, true));                   // --prediction on and no key: on
        std::error_code removed;
        std::filesystem::remove(settings, removed);
        // by default: the match shows the confirmed engine, nothing is predicted, an order waits for its turn, and the confirmed engines are the same
        {
            const ApplicationConfig cfg = host_config();                                   // (nothing said: the product's default)
            Application app;
            Peer bob;
            Duo duo{app, bob};
            ASSERT_TRUE(begin_match(app, bob, duo, cfg));
            ASSERT_TRUE(duo.until([&]() { return app.sim().current_tick() > 60; }, 20000));
            ASSERT_FALSE(app.prediction_wanted());
            ASSERT_FALSE(app.net()->prediction_enabled() || app.net()->predicting());
            ASSERT_TRUE(&app.view_sim() == &app.sim());
            ASSERT_TRUE(app.net()->prediction() == nullptr || (app.net()->prediction()->stats().commands_predicted == 0 && app.net()->prediction()->stats().starts == 0));      // (it never began)
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

    TEST_CASE("PA6 The Corner's \"delay\" Is What The Click Feels While The Prediction Is On: A Dash Until An Order Has Been Felt, Then The Time From The Frame That Took It To The Frame That Shows It (Less Than A Tick And A Frame), The Network's Delay (The Confirmed Engine's) When It Is Off") {
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
        ASSERT_FALSE(app.corner_delay_ms().has_value());                                    // no order has been felt: a dash (the network's delay is not what the click feels)
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
        ASSERT_TRUE(app.corner_delay_ms().has_value() && *app.corner_delay_ms() == app.felt_delay().felt_ms());
        // the number is about now only for ten seconds: the frame clock runs on (the net's clock does not, so the network's delay is still the fresh number that it was), and the corner
        // says nothing rather than the network's delay, which is not what the click feels
        for (int i = 0; i < 30; ++i) frame();                                               // (the order reaches the confirmed engine meanwhile)
        ASSERT_TRUE(app.net()->command_delay_ms().has_value());
        counter += 11ull * 1000ull * (SDL_GetPerformanceFrequency() / 1000ull);
        ASSERT_FALSE(app.corner_delay_ms().has_value());
        ASSERT_TRUE(app.net()->command_delay_ms().has_value());
        // off: the corner says what the confirmed engine's delay is (a dash while no order has been applied there; the order is by now)
        ASSERT_TRUE(duo.until([&]() { return app.net()->command_delay_ms().has_value(); }, 3000));
        app.net()->set_prediction_enabled(false);
        app.pump_network(0.0f);
        ASSERT_FALSE(app.net()->predicting());
        ASSERT_TRUE(app.corner_delay_ms().has_value() && *app.corner_delay_ms() == *app.net()->command_delay_ms());
        app.net()->freeze();
        for (int i = 0; i < 150; ++i) frame();
        ASSERT_TRUE(app.sim().state_hash() == bob.sim.state_hash());
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
