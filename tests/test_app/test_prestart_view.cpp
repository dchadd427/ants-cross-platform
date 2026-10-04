// The ants before the start (a fix of v0.2.0). Since v0.2.0 the simulation waits behind the "Get ready to play!" dialog (a local game counts 5 s of real time, a match of the network ends
// the dialog with its first executed turn, and the waiting for the other players comes before that), and the picture behind the dialog showed the hit point numbers of the ants and no ants:
// the renderer draws an ant from the clip that the simulation plays on it, and the first movement tick is what starts it. The snapshot of an engine that has not ticked now shows the clip
// that tick will start (its first frame, held), and these tests draw the application's own frames to see it:
//   * LOCAL: every team's ants are drawn behind the dialog (the pixels at an ant's place are not the ground's), in both pictures (classic and 16:9) and on three maps; the ants that the
//     first tick leaves in the same pose have the same pixels in the frame after it; nothing moves while the dialog is up (no tick, the same state, the same picture at every moment of a
//     tick), and the first tick runs when the dialog is over.
//   * NETWORK: the same for a guest of a match whose first turn has not been executed yet (the dialog waits for it), and for the frame after that turn.
// Usage: test_prestart_view. Exit code 0 when every check passes.
#include <SDL.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "zoom_scene.hpp"

#include "ants_app/application.hpp"
#include "ants_app/hud.hpp"
#include "ants_app/renderer.hpp"
#include "ants_assets/asset_archive.hpp"
#include "ants_sim/sim_engine.hpp"
#include "ants_test_paths.hpp"

using namespace ants;
using namespace ants::app;
using namespace zoomtest;

namespace {

int g_checks = 0;
int g_failures = 0;
const char* g_group = "";

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        if (g_failures <= 80) std::fprintf(stderr, "  FAIL [%s]: %s\n", g_group, what.c_str());
    }
}

void group(const char* name, const char* what) {
    g_group = name;
    std::printf("[%s] %s\n", name, what);
}

/// One frame of the application's world pass over its own world (no HUD, no hit point digits), read back: `sub_tick` seconds after the last tick, with the ants or without them
Picture world_frame(Application& app, int32_t w, int32_t h, float sub_tick, bool with_ants) {
    sim::WorldState world = app.sim().get_world_state();
    if (!with_ants) world.ants.clear();
    app.renderer().set_show_hp(false);
    app.renderer().begin_frame();
    app.renderer().render_world(world, app.sim().grid(), -1, {}, false, false, -1, -1, 0, sub_tick);
    return read_canvas(app, w, h);
}

/// The rectangle of the screen (the picture's own coordinates) that the art of an ant can reach at the zoom 1: an ant stands 35 px tall above its anchor and is about 40 px wide
LayoutRect art_of(const ViewportCamera& cam, const sim::AntSnapshot& a) {
    int32_t sx = 0;
    int32_t sy = 0;
    cam.world_to_screen(a.px, a.py, sx, sy);
    return LayoutRect{sx - 22, sy - 38, 44, 52};
}

bool inside(const LayoutRect& r, const LayoutRect& view) { return r.x >= view.x && r.y >= view.y && r.right() <= view.right() && r.bottom() <= view.bottom(); }
bool overlap(const LayoutRect& a, const LayoutRect& b) { return a.x < b.right() && b.x < a.right() && a.y < b.bottom() && b.y < a.bottom(); }

/// The pixels of the rectangle that differ between two pictures
int differ_in(const Picture& a, const Picture& b, const LayoutRect& r) {
    int n = 0;
    for (int32_t y = r.y; y < r.bottom(); ++y) {
        for (int32_t x = r.x; x < r.right(); ++x) {
            if (std::memcmp(a.at(x, y), b.at(x, y), 4) != 0) ++n;
        }
    }
    return n;
}

/// The pose of an ant on the screen: what decides its sprite
bool same_pose(const sim::AntSnapshot& a, const sim::AntSnapshot& b) {
    return a.id == b.id && a.loco_clip == b.loco_clip && a.loco_frame == b.loco_frame && a.loco_mirrored == b.loco_mirrored && a.px == b.px && a.py == b.py && a.facing == b.facing &&
           a.state == b.state;
}

constexpr int kShownAtLeast = 200;          // an ant that is drawn changes at least this many pixels of its box (the least of the ants on the maps below changes about 550: --verbose prints them)
bool g_verbose = false;

/// Every team's ants are drawn: for each team the camera is put on its first ant (zoom 1), and every ant that lies wholly in the view changes the picture of its box against the same
/// world without ants. Returns the number of ants that were looked at.
int check_every_team_is_drawn(Application& app, const std::string& at, int32_t cw, int32_t ch) {
    const LayoutRect view = app.layout().view();
    ViewportCamera& cam = app.renderer().camera();
    const uint32_t mw = app.sim().grid().width();
    const uint32_t mh = app.sim().grid().height();
    const std::vector<sim::AntSnapshot> ants = app.sim().get_world_state().ants;
    int looked = 0;
    int teams = 0;
    for (uint8_t team = 0; team < 4; ++team) {
        const sim::AntSnapshot* first = nullptr;
        for (const auto& a : ants) {
            if (a.player_id == team && first == nullptr) first = &a;
        }
        if (first == nullptr) continue;
        ++teams;
        cam.center_on(first->px, first->py, mw, mh);
        const Picture with = world_frame(app, cw, ch, 0.0f, true);
        const Picture without = world_frame(app, cw, ch, 0.0f, false);
        int in_view = 0;
        int mine = 0;
        int smallest = 1 << 30;
        for (const auto& a : ants) {
            const LayoutRect box = art_of(cam, a);
            if (!inside(box, view)) continue;
            ++in_view;
            if (a.player_id == team) ++mine;
            const int shown = differ_in(with, without, box);
            smallest = std::min(smallest, shown);
            check(shown >= kShownAtLeast, at + "team " + std::to_string(team) + ": the ant " + std::to_string(a.id) + " of team " + std::to_string(a.player_id) + " is drawn: " + std::to_string(shown) +
                                              " pixels of its box differ from the ground");
        }
        check(mine >= 1, at + "team " + std::to_string(team) + ": its ants are in the view (" + std::to_string(mine) + ")");
        if (g_verbose) std::printf("    %s team %d: %d ants in the view, the least changes %d pixels\n", at.c_str(), team, in_view, smallest == (1 << 30) ? -1 : smallest);
        looked += in_view;
    }
    check(teams >= 2, at + "the match has ants of at least two teams (" + std::to_string(teams) + ")");
    return looked;
}

/// The first tick leaves most ants in the pose that was shown, and every ant in the same pose has the same pixels: the frame before the first tick and the frame after it, with the camera
/// where it is (on the first ant of each team in turn). `ticked` runs the first tick (or turns) and returns when it has run.
template <typename Ticked>
void check_same_pixels_after_the_first_tick(Application& app, const std::string& at, int32_t cw, int32_t ch, Ticked ticked) {
    const LayoutRect view = app.layout().view();
    ViewportCamera& cam = app.renderer().camera();
    const uint32_t mw = app.sim().grid().width();
    const uint32_t mh = app.sim().grid().height();
    const std::vector<sim::AntSnapshot> before = app.sim().get_world_state().ants;
    // the frames of every team's view before the first tick ...
    std::array<Picture, 4> pre;
    std::array<bool, 4> have{};
    std::array<int32_t, 4> cx{};
    std::array<int32_t, 4> cy{};
    for (uint8_t team = 0; team < 4; ++team) {
        for (const auto& a : before) {
            if (a.player_id != team) continue;
            cam.center_on(a.px, a.py, mw, mh);
            cx[team] = cam.world_x;
            cy[team] = cam.world_y;
            pre[team] = world_frame(app, cw, ch, 0.0f, true);
            have[team] = true;
            break;
        }
    }
    check(ticked(), at + "the first tick (the first turn) has run");
    const std::vector<sim::AntSnapshot> after = app.sim().get_world_state().ants;
    int same = 0;
    int moved = 0;
    for (const auto& b : before) {
        for (const auto& a : after) {
            if (a.id != b.id) continue;
            if (same_pose(a, b)) ++same; else ++moved;
        }
    }
    if (g_verbose) std::printf("    %s%d of %d ants stand in the pose that was shown after the first tick, %d changed their frame\n", at.c_str(), same, static_cast<int>(before.size()), moved);
    check(same * 4 >= static_cast<int>(before.size()) * 3, at + "most ants stand in the pose that was shown (" + std::to_string(same) + " of " + std::to_string(before.size()) + ", " + std::to_string(moved) +
                                                                " changed their frame in the first tick)");
    int compared = 0;
    for (uint8_t team = 0; team < 4; ++team) {
        if (!have[team]) continue;
        cam.set_origin(static_cast<double>(cx[team]), static_cast<double>(cy[team]), mw, mh);
        check(cam.world_x == cx[team] && cam.world_y == cy[team], at + "the camera is where it was");
        const Picture post = world_frame(app, cw, ch, 0.0f, true);
        for (const auto& b : before) {
            const sim::AntSnapshot* a = nullptr;
            for (const auto& x : after) {
                if (x.id == b.id) a = &x;
            }
            if (a == nullptr || !same_pose(*a, b)) continue;
            const LayoutRect box = art_of(cam, b);
            if (!inside(box, view)) continue;
            // (an ant whose neighbour changed its frame may be covered differently: only boxes that no changed ant reaches are compared)
            bool clear = true;
            for (const auto& c : before) {
                for (const auto& x : after) {
                    if (x.id == c.id && !same_pose(x, c) && overlap(art_of(cam, c), box)) clear = false;
                }
            }
            if (!clear) continue;
            ++compared;
            const int bad = differ_in(pre[team], post, box);
            check(bad == 0, at + "team " + std::to_string(team) + ": the ant " + std::to_string(b.id) + " has the same pixels after the first tick as before it: " + std::to_string(bad) + " differ");
        }
    }
    check(compared >= 4, at + "ants were compared (" + std::to_string(compared) + ")");
}

// =====================================================================================================================================================
// A local match
// =====================================================================================================================================================

void test_local() {
    group("local", "a local match behind its start dialog: every team's ants are drawn, the first tick leaves their pixels as they are, and nothing moves before it");
    for (const Aspect aspect : {Aspect::Classic4x3, Aspect::Wide16x9}) {
        const bool wide = aspect == Aspect::Wide16x9;
        const int32_t cw = wide ? kWideW : kClassicW;
        const int32_t ch = wide ? kWideH : kClassicH;
        for (const char* map : {"GAUNTLET", "TREASURE", "TINY"}) {
            const std::string at = std::string(wide ? "wide" : "classic") + " " + map + ": ";
            AppRig rig(aspect, 1.0f, true);
            check(rig.ok, at + "the application is up");
            if (!rig.ok) continue;
            Application& app = rig.app;
            app.renderer().pin_animation_clock(1500);
            check(app.start_game(maps_dir() + map + ".LVL"), at + "the match starts");
            check(app.hud().is_match_start_modal_active(), at + "the start dialog is up");
            check(app.sim().current_tick() == 0, at + "the simulation has not ticked");
            check(app.zoom() == 1.0f, at + "the match is at the zoom 1");
            const int looked = check_every_team_is_drawn(app, at, cw, ch);
            check(looked >= 8, at + "ants were looked at (" + std::to_string(looked) + ")");
            check(app.sim().current_tick() == 0 && app.hud().is_match_start_modal_active(), at + "(drawing changed nothing)");

            // the first tick: the dialog is closed (as its last step does) and the first tick runs
            check_same_pixels_after_the_first_tick(app, at, cw, ch, [&]() {
                app.hud().dismiss_match_start_modal();
                app.update_simulation(0.05f);
                return app.sim().current_tick() == 1;
            });
            app.renderer().set_show_hp(true);
        }
    }
}

void test_nothing_moves() {
    group("still", "behind the dialog nothing moves: no tick, the same state, the same picture at every moment of a tick; the first tick runs when the dialog is over");
    AppRig rig(Aspect::Wide16x9, 1.0f, true);
    check(rig.ok, "the application is up");
    if (!rig.ok) return;
    Application& app = rig.app;
    app.renderer().pin_animation_clock(1500);
    check(app.start_game(maps_dir() + "GAUNTLET.LVL"), "the match starts");
    const int32_t cw = kWideW;
    const int32_t ch = kWideH;
    const LayoutRect view = app.layout().view();
    app.renderer().camera().center_on(app.sim().get_world_state().ants.front().px, app.sim().get_world_state().ants.front().py, app.sim().grid().width(), app.sim().grid().height());
    const std::vector<sim::AntSnapshot> start = app.sim().get_world_state().ants;
    const sim::StateHash hash0 = app.sim().state_hash();
    const Picture still = world_frame(app, cw, ch, 0.0f, true);
    int steps = 0;
    // 4.4 s of the dialog in steps that do not fit the tick: the frame clock of a real machine
    for (; steps < 340; ++steps) {
        app.update_simulation(0.013f);
        if (steps % 37 == 0) {
            check(app.sim().current_tick() == 0 && app.hud().is_match_start_modal_active(), "step " + std::to_string(steps) + ": no tick, the dialog is up");
            const sim::StateHash h = app.sim().state_hash();
            check(h.total == hash0.total, "step " + std::to_string(steps) + ": the state is the same");
        }
    }
    const std::vector<sim::AntSnapshot> now = app.sim().get_world_state().ants;
    bool same = now.size() == start.size();
    for (size_t i = 0; same && i < now.size(); ++i) same = same_pose(now[i], start[i]) && now[i].hp == start[i].hp && now[i].loco_left_ms == start[i].loco_left_ms;
    check(same, "after 4.4 s of the dialog every ant is as it was (position, facing, clip, frame, hit points)");
    // every moment of the tick that the frame clock passes through: the renderer is given the time since the last tick, and the pose is held
    for (const float sub : {0.0f, 0.004f, 0.011f, 0.019f, 0.027f, 0.036f, 0.044f, 0.0499f}) {
        const Picture p = world_frame(app, cw, ch, sub, true);
        const int bad = differ(p, still, view);
        check(bad == 0, "the picture " + std::to_string(static_cast<int>(sub * 1000.0f)) + " ms into a tick is the picture at 0: " + std::to_string(bad) + " pixels differ");
    }
    // the dialog's last steps, then the first tick
    int more = 0;
    while (app.hud().is_match_start_modal_active() && more < 200) {
        check(app.sim().current_tick() == 0, "the dialog is up: no tick (" + std::to_string(more) + ")");
        app.update_simulation(0.05f);
        ++more;
    }
    check(!app.hud().is_match_start_modal_active() && app.sim().current_tick() == 0, "the dialog is over and the simulation has not ticked yet");
    check(more >= 1 && more <= 12, "the dialog's steps are counted in real time: " + std::to_string(more) + " more 50 ms steps after 4.4 s");
    app.update_simulation(0.05f);
    check(app.sim().current_tick() == 1, "the first tick runs on the step after the dialog's last");
}

// =====================================================================================================================================================
// A match of the network
// =====================================================================================================================================================

void test_network() {
    group("net", "a guest of a match of the network: the ants are drawn while its dialog waits for the first turn, and the frame after that turn has the same pixels");
    for (const Aspect aspect : {Aspect::Wide16x9, Aspect::Classic4x3}) {
        const bool wide = aspect == Aspect::Wide16x9;
        const std::string at = std::string("guest, ") + (wide ? "wide" : "classic") + ": ";
        const QuietStdout quiet;
        SDL_Init(SDL_INIT_VIDEO);
        Peer host;
        Application app;
        check(join_and_start(app, host, network_config(1.0f, false, aspect)), at + "the guest and the host are in the match on TINY");
        if (app.state() != AppState::Playing) continue;
        check(app.network_active() && !app.net()->is_host(), at + "it is a match of the network and this machine is a guest");
        check(app.hud().is_match_start_modal_active(), at + "the guest's dialog is up");
        check(app.sim().current_tick() == 0, at + "the guest has not executed a turn");
        check(host.sim.current_tick() == 0, at + "the host has not either");
        app.renderer().pin_animation_clock(1500);
        const int32_t cw = wide ? kWideW : kClassicW;
        const int32_t ch = wide ? kWideH : kClassicH;
        const int looked = check_every_team_is_drawn(app, at, cw, ch);
        check(looked >= 4, at + "ants were looked at (" + std::to_string(looked) + ")");
        check(app.sim().current_tick() == 0 && app.hud().is_match_start_modal_active(), at + "(drawing changed nothing)");
        Duo duo{app, host};
        // the first turn is sealed 5 s after the match began (game time of the two machines)
        check_same_pixels_after_the_first_tick(app, at, cw, ch, [&]() { return duo.until([&]() { return app.sim().current_tick() >= 1; }, 9000); });
        check(!app.hud().is_match_start_modal_active(), at + "the dialog is gone with the first turn");
        app.renderer().set_show_hp(true);
        app.return_to_map_select();
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string only;                                           // --only NAME: run one group (local, still, net)
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--only") == 0 && i + 1 < argc) {
            only = argv[++i];
        } else if (std::strcmp(argv[i], "--verbose") == 0) {
            g_verbose = true;
        } else {
            std::fprintf(stderr, "usage: test_prestart_view [--only local|still|net] [--verbose]\n");
            return 2;
        }
    }
    const auto run = [&only](const char* name) { return only.empty() || only == name; };
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);                      // nothing is shown or heard
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    SDL_Init(SDL_INIT_VIDEO);
    if (run("local")) test_local();
    if (run("still")) test_nothing_moves();
    if (run("net")) test_network();
    std::printf("\nprestart view: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
