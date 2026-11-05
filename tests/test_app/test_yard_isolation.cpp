// The Yard leaves the game as it was (docs/YARD.md, "The normal game stays as it is"). What the Yard does in the HUD sits behind ScreenLayout::yard, which only ScreenLayout::map_only makes. This
// program pins what the places that carry such a gate do in the two pictures of the game itself, the original's 640 x 480 ("classic") and the 16:9 one of 960 x 540 ("wide"), as 64-bit numbers that
// were made on the code BEFORE the Yard came in and did not move when it did:
//   * "iso.<picture>.render.*": the draw calls of HUD::render (every call with all its parameters, as test_view_fingerprint pins them) in twelve states of the HUD: nothing selected, a group, a
//     bomber, the own hill, a rubber band, the chat with entries and typing, the chat off, the quit dialog, the options window, the quick help, the "get ready" dialog and a small map;
//   * "iso.<picture>.buttons": the rectangles that HUD::apply_layout places (the buttons of the top bar and the chat, the pedestals, the quit dialog's buttons) and the minimap's;
//   * "iso.<picture>.key.*": the HUD and its frame after one key (Esc, Enter, F1, F9, a letter, Ctrl or Cmd with A, H, L, N, O, P, Q, S): the keys that the Yard takes away still work as they did;
//   * "iso.<picture>.edge.*": what HUD::input_tick does to the camera in twelve ticks with the pointer at each edge of the picture, at its middle and on the minimap while it is dragged: the view
//     moves at the first tick, as it always did (the Yard's dwell is its own).
// Nothing here reads a clock (the HUD's is injected), the wall time, a file other than ants.chd, or the network. Usage: test_yard_isolation [--print | --list | --only PREFIX]. --print makes the
// table below: use it DELIBERATELY, when the HUD of the game is meant to change; a failure that you did not mean is a change of the normal game and is to be fixed in the code, not in this file.
#include "view_fingerprint_fixtures.hpp"

#include <cmath>
#include <memory>

namespace {

struct Picture {
    const char* name;
    const ScreenLayout* layout;        // nullptr: the original's 640 x 480
    int32_t width;
    int32_t height;
};

const Picture kPictures[] = {{"classic", nullptr, 640, 480}, {"wide", &kWide, 960, 540}};

std::unique_ptr<Scene> make_scene(const assets::AssetArchive& arc, const Picture& pic, uint32_t map = kMap) {
    return std::make_unique<Scene>(arc, 0, map, map, pic.layout);
}

std::string name_of(const Picture& pic, const std::string& what) { return std::string("iso.") + pic.name + "." + what; }

uint64_t rounded(float v) { return static_cast<uint64_t>(static_cast<int64_t>(std::lround(static_cast<double>(v) * 100.0))); }

// ---- HUD::render

void render_scenarios(const assets::AssetArchive& arc, const Picture& pic) {
    const auto shoot = [&](const char* what, const std::function<void(Scene&)>& setup, uint32_t map = kMap, uint32_t at_ms = 10000) {
        auto s = make_scene(arc, pic, map);
        setup(*s);
        s->shoot(name_of(pic, std::string("render.") + what), at_ms);
    };
    shoot("idle", [](Scene&) {});
    shoot("group", [](Scene& s) { s.hud.set_selected_ant_ids({1, 2, 3}); s.tick(0); });
    shoot("bomber", [](Scene& s) { s.hud.select_ant(3); s.tick(0); });
    shoot("hill", [](Scene& s) { s.hud.select_base(0); s.tick(0); });
    shoot("band", [](Scene& s) {
        s.look(300, 300);
        s.move(100, 100);
        s.press(100, 100);
        s.move(260, 220);
    });
    shoot("chat", [](Scene& s) {
        s.hud.add_chat_entry("Alice", "Hello everybody, this is a rather long message that has to wrap over several lines of the log window.", false, 3);
        s.hud.add_chat_entry("Bob", "Team only: attack the red hill together!", true, 2);
        s.hud.add_news_flash(65000, "Black dropped out of the game!");
        s.hud.set_chat_input("Typing a message");
    });
    shoot("chat_off", [](Scene& s) { s.hud.options().chat = false; s.hud.set_chat_input("left over"); });
    shoot("quit_dialog", [](Scene& s) { s.hud.open_quit_dialog(); });
    shoot("options", [](Scene& s) { s.hud.open_options(); });
    shoot("quick_help", [](Scene& s) { s.hud.open_quick_help(); });
    shoot("get_ready", [](Scene& s) { s.hud.start_match_modal(); }, kMap, 1500);
    shoot("small_map", [](Scene& s) { s.hud.set_selected_ant_ids({1}); s.tick(0); }, 24);
}

// ---- HUD::apply_layout

void buttons_scenario(const assets::AssetArchive& arc, const Picture& pic) {
    auto s = make_scene(arc, pic);
    const HUD& hud = s->hud;
    Fnv64 h;
    uint64_t n = 0;
    const auto add = [&](const UIButton& b) {
        h.i32(b.x);
        h.i32(b.y);
        h.i32(b.w);
        h.i32(b.h);
        ++n;
    };
    for (const UIButton* b : {&hud.help_button(), &hud.options_button(), &hud.quit_button(), &hud.stop_button(), &hud.send_to_button(), &hud.team_button(), &hud.hatch_button(),
                              &hud.team_up_button(), &hud.quit_yes_button(), &hud.quit_no_button(), &hud.get_move_pedestal_button(), &hud.get_ability_pedestal_button()}) {
        add(*b);
    }
    const LayoutRect rects[] = {hud.minimap_rect(), hud.layout().view(), hud.layout().chat_view(), hud.layout().panel_fill()};
    for (const LayoutRect& r : rects) {
        h.i32(r.x);
        h.i32(r.y);
        h.i32(r.w);
        h.i32(r.h);
        ++n;
    }
    record(name_of(pic, "buttons"), Measure{h.value(), n});
}

// ---- HUD::handle_key_down

struct KeyCase {
    const char* name;
    int32_t key;
    uint16_t mod;
};

const KeyCase kKeys[] = {
    {"esc", SDLK_ESCAPE, 0},
    {"enter", SDLK_RETURN, 0},
    {"f1", SDLK_F1, 0},
    {"f9", SDLK_F9, 0},
    {"letter", 'x', 0},
    {"ctrl_a", 'a', static_cast<uint16_t>(KMOD_LCTRL)},
    {"ctrl_h", 'h', static_cast<uint16_t>(KMOD_LCTRL)},
    {"ctrl_l", 'l', static_cast<uint16_t>(KMOD_LCTRL)},
    {"ctrl_n", 'n', static_cast<uint16_t>(KMOD_LCTRL)},
    {"ctrl_o", 'o', static_cast<uint16_t>(KMOD_LCTRL)},
    {"ctrl_p", 'p', static_cast<uint16_t>(KMOD_LCTRL)},
    {"ctrl_q", 'q', static_cast<uint16_t>(KMOD_LCTRL)},
    {"ctrl_s", 's', static_cast<uint16_t>(KMOD_LCTRL)},
    {"ctrl_x", 'x', static_cast<uint16_t>(KMOD_LCTRL)},
    {"cmd_a", 'a', static_cast<uint16_t>(KMOD_LGUI)},
};

Measure key_measure(const assets::AssetArchive& arc, const Picture& pic, const KeyCase& kc) {
    auto s = make_scene(arc, pic);
    for (int32_t i = 0; i < 5; ++i) s->sim.spawn_unit(0, sim::AntType::Worker, sim::TileCoord{12 + 2 * i, 12});      // (what Ctrl+A, N, P and S act on: the engine's own ants)
    s->hud.set_selected_ant_ids({1, 2});
    s->hud.set_chat_input("hi");
    s->look(300, 300);
    {
        FrameRecorder first;                                         // (the pedestals' chains start at t = 0, as Scene::shoot does)
        g_now_ms = 0;
        s->hud.render(first, arc, s->world, s->camera);
    }
    s->hud.handle_key_down(kc.key, s->sim, s->camera, kc.mod, false);
    g_now_ms = 10000;
    FrameRecorder rec;
    s->hud.render(rec, arc, s->world, s->camera);
    Fnv64 h;
    const Measure frame = rec.result();
    h.u32(static_cast<uint32_t>(frame.hash & 0xFFFFFFFFu));
    h.u32(static_cast<uint32_t>(frame.hash >> 32));
    h.u32(static_cast<uint32_t>(frame.count));
    h.u32(static_cast<uint32_t>(s->hud.get_selected_ant_ids().size()));
    for (uint32_t id : s->hud.get_selected_ant_ids()) h.u32(id);
    h.u32(s->hud.get_selected_ant_id());
    h.i32(s->hud.get_selected_base_team_id());
    h.str(s->hud.get_chat_input());
    h.flag(s->hud.is_quit_dialog_open());
    h.flag(s->hud.is_quick_help_open());
    h.flag(s->hud.is_options_open());
    h.flag(s->hud.is_modal_open());
    h.u32(static_cast<uint32_t>(s->hud.ant_health().when));                // (Ctrl+L: the health marks, which the HUD's own frame does not show)
    h.u32(static_cast<uint32_t>(s->hud.ant_health().style));
    h.u32(static_cast<uint32_t>(rounded(s->camera.x)));
    h.u32(static_cast<uint32_t>(rounded(s->camera.y)));
    h.u32(static_cast<uint32_t>(s->sim.get_world_state().ants.size()));
    return Measure{h.value(), frame.count};
}

void key_scenarios(const assets::AssetArchive& arc, const Picture& pic) {
    for (const KeyCase& kc : kKeys) {
        const std::string name = name_of(pic, std::string("key.") + kc.name);
        if (!wanted(name)) continue;
        record(name, key_measure(arc, pic, kc));
    }
}

// ---- HUD::input_tick

struct EdgePoint {
    const char* name;
    int32_t fx;        // the pointer, as a fraction (in 100ths) of the picture's width and height
    int32_t fy;
};

const EdgePoint kEdgePoints[] = {{"n", 50, 0}, {"ne", 100, 0}, {"e", 100, 50}, {"se", 100, 100}, {"s", 50, 100}, {"sw", 0, 100}, {"w", 0, 50}, {"nw", 0, 0}, {"middle", 50, 50}};

Measure edge_measure(const assets::AssetArchive& arc, const Picture& pic, const EdgePoint& p) {
    auto s = make_scene(arc, pic);
    s->look(700, 700);
    const int32_t x = std::min(pic.width - 1, p.fx * pic.width / 100);
    const int32_t y = std::min(pic.height - 1, p.fy * pic.height / 100);
    Fnv64 h;
    for (int32_t tick = 0; tick < 12; ++tick) {
        h.flag(s->hud.input_tick(s->camera, kMap, kMap, x, y));
        h.u32(static_cast<uint32_t>(rounded(s->camera.x)));
        h.u32(static_cast<uint32_t>(rounded(s->camera.y)));
    }
    return Measure{h.value(), 12};
}

Measure radar_measure(const assets::AssetArchive& arc, const Picture& pic) {
    auto s = make_scene(arc, pic);
    s->look(100, 100);
    const LayoutRect mini = s->hud.minimap_rect();
    s->press(mini.x + mini.w / 4, mini.y + mini.h / 4);
    Fnv64 h;
    for (int32_t tick = 0; tick < 12; ++tick) {
        h.flag(s->hud.input_tick(s->camera, kMap, kMap, mini.x + mini.w * 3 / 4, mini.y + mini.h * 3 / 4));
        h.u32(static_cast<uint32_t>(rounded(s->camera.x)));
        h.u32(static_cast<uint32_t>(rounded(s->camera.y)));
    }
    return Measure{h.value(), 12};
}

void edge_scenarios(const assets::AssetArchive& arc, const Picture& pic) {
    for (const EdgePoint& p : kEdgePoints) {
        const std::string name = name_of(pic, std::string("edge.") + p.name);
        if (!wanted(name)) continue;
        record(name, edge_measure(arc, pic, p));
    }
    if (wanted(name_of(pic, "edge.radar"))) record(name_of(pic, "edge.radar"), radar_measure(arc, pic));
}

// ---- the fingerprints can tell states apart (a table of equal numbers would pin nothing)

void self_check(const assets::AssetArchive& arc) {
    for (const Picture& pic : kPictures) {
        const Measure idle = [&] { auto s = make_scene(arc, pic); FrameRecorder r; g_now_ms = 10000; s->hud.render(r, arc, s->world, s->camera); return r.result(); }();
        const Measure group = [&] { auto s = make_scene(arc, pic); s->hud.set_selected_ant_ids({1, 2, 3}); FrameRecorder r; g_now_ms = 10000; s->hud.render(r, arc, s->world, s->camera); return r.result(); }();
        check(idle.hash != group.hash, std::string(pic.name) + ": a selection changes the frame");
        check(idle.count > 40, std::string(pic.name) + ": the HUD draws its panel (calls: " + std::to_string(idle.count) + ")");
        check(edge_measure(arc, pic, kEdgePoints[0]).hash != edge_measure(arc, pic, kEdgePoints[8]).hash, std::string(pic.name) + ": the edge scroll moves the view, the middle does not");
        check(key_measure(arc, pic, kKeys[0]).hash != key_measure(arc, pic, kKeys[5]).hash, std::string(pic.name) + ": Esc and Ctrl+A leave different states");
    }
    const Measure classic = [&] { auto s = make_scene(arc, kPictures[0]); FrameRecorder r; g_now_ms = 10000; s->hud.render(r, arc, s->world, s->camera); return r.result(); }();
    const Measure wide = [&] { auto s = make_scene(arc, kPictures[1]); FrameRecorder r; g_now_ms = 10000; s->hud.render(r, arc, s->world, s->camera); return r.result(); }();
    check(classic.hash != wide.hash, "the two pictures are told apart");
}

// BEGIN GOLDEN TABLE
// (made on the code as it was before the Yard came in, then run again after it did: 76 fingerprints, none moved)
const Golden kGoldens[] = {
    {"iso.classic.render.idle", 0xaa58258cae06214c, 80},
    {"iso.classic.render.group", 0x4190e311b8d7b4b2, 85},
    {"iso.classic.render.bomber", 0x2a8dd8d8f7ea0aaa, 88},
    {"iso.classic.render.hill", 0x9033f768ea584d71, 93},
    {"iso.classic.render.band", 0xec91dc7ca77d34ba, 81},
    {"iso.classic.render.chat", 0x6238693b15bab46a, 88},
    {"iso.classic.render.chat_off", 0x873be8bf49eba50d, 81},
    {"iso.classic.render.quit_dialog", 0xb223ea350257db2f, 104},
    {"iso.classic.render.options", 0x90929d9ea096b478, 304},
    {"iso.classic.render.quick_help", 0x9e37c41fa167033e, 114},
    {"iso.classic.render.get_ready", 0x28465aecc9b30ccf, 106},
    {"iso.classic.render.small_map", 0x9e0bfefe13631d6e, 85},
    {"iso.classic.buttons", 0xd471e62d133567ac, 16},
    {"iso.classic.key.esc", 0xae52e4e3eada53e8, 81},
    {"iso.classic.key.enter", 0x320ed479367ba57c, 87},
    {"iso.classic.key.f1", 0x9c178a6699ba2305, 120},
    {"iso.classic.key.f9", 0x6cc3bd5ec244746b, 89},
    {"iso.classic.key.letter", 0xd3c6759d573eddc4, 86},
    {"iso.classic.key.ctrl_a", 0xfb4478046bc5c86e, 86},
    {"iso.classic.key.ctrl_h", 0xd981efce8df2a913, 95},
    {"iso.classic.key.ctrl_l", 0x7dbe7ef72d1be50d, 86},
    {"iso.classic.key.ctrl_n", 0xd983d44c02398cef, 86},
    {"iso.classic.key.ctrl_o", 0xd9c01263faeb967d, 310},
    {"iso.classic.key.ctrl_p", 0xeece271df5ac6bf2, 87},
    {"iso.classic.key.ctrl_q", 0x48147acd21e8892b, 110},
    {"iso.classic.key.ctrl_s", 0xd237fc6ce52231d0, 86},
    {"iso.classic.key.ctrl_x", 0x2dce46b7cf524d0f, 86},
    {"iso.classic.key.cmd_a", 0xfb4478046bc5c86e, 86},
    {"iso.classic.edge.n", 0x4d53eedf8bab73fd, 12},
    {"iso.classic.edge.ne", 0x0088261e3c2ad472, 12},
    {"iso.classic.edge.e", 0x153c586bfb736d3e, 12},
    {"iso.classic.edge.se", 0x1bb9e3f50c806839, 12},
    {"iso.classic.edge.s", 0xe9a6814a51a4e596, 12},
    {"iso.classic.edge.sw", 0x369e1311696c102a, 12},
    {"iso.classic.edge.w", 0x68a56148975915f9, 12},
    {"iso.classic.edge.nw", 0xffc8bc1ca6218141, 12},
    {"iso.classic.edge.middle", 0x0873574063b87f55, 12},
    {"iso.classic.edge.radar", 0xed33134b5b0676c4, 12},
    {"iso.wide.render.idle", 0x7a90cfaad34abb92, 96},
    {"iso.wide.render.group", 0x4db4114addd05802, 101},
    {"iso.wide.render.bomber", 0xf8c1bd03046f8ce5, 104},
    {"iso.wide.render.hill", 0xfc8d102c83550430, 109},
    {"iso.wide.render.band", 0x5a6745fc13ba8664, 97},
    {"iso.wide.render.chat", 0x35865c5434cf61dd, 108},
    {"iso.wide.render.chat_off", 0x59a04eb818a210c4, 97},
    {"iso.wide.render.quit_dialog", 0xdf73f3d91f954a2e, 122},
    {"iso.wide.render.options", 0x79616d4bc655cc19, 365},
    {"iso.wide.render.quick_help", 0x436c19acc3031fd1, 134},
    {"iso.wide.render.get_ready", 0xabd866816bf8c489, 124},
    {"iso.wide.render.small_map", 0x667935f90ca629ab, 101},
    {"iso.wide.buttons", 0x8ae711c92d92cbde, 16},
    {"iso.wide.key.esc", 0x3a640b1269dd7fd5, 97},
    {"iso.wide.key.enter", 0xc638aab8c3f95491, 103},
    {"iso.wide.key.f1", 0x64ff00581c4f34b9, 140},
    {"iso.wide.key.f9", 0xc4b711fd126c205e, 105},
    {"iso.wide.key.letter", 0x9d95401c2a6bc516, 102},
    {"iso.wide.key.ctrl_a", 0xb136944608b84ef6, 102},
    {"iso.wide.key.ctrl_h", 0xd30963de233b74b6, 111},
    {"iso.wide.key.ctrl_l", 0x58283d69113d9875, 102},
    {"iso.wide.key.ctrl_n", 0x8e73c538cba741d1, 102},
    {"iso.wide.key.ctrl_o", 0x3996afe880437122, 371},
    {"iso.wide.key.ctrl_p", 0xfd16bc7a712d8323, 103},
    {"iso.wide.key.ctrl_q", 0x1c991e1b891160e0, 128},
    {"iso.wide.key.ctrl_s", 0x9b6b3a1ac6a251aa, 102},
    {"iso.wide.key.ctrl_x", 0xb5bc7ad7dfa12577, 102},
    {"iso.wide.key.cmd_a", 0xb136944608b84ef6, 102},
    {"iso.wide.edge.n", 0x4d53eedf8bab73fd, 12},
    {"iso.wide.edge.ne", 0x363c945aa97c6e36, 12},
    {"iso.wide.edge.e", 0x368fc06adef4f39a, 12},
    {"iso.wide.edge.se", 0xe2d1937d32b1d2f1, 12},
    {"iso.wide.edge.s", 0xe9a6814a51a4e596, 12},
    {"iso.wide.edge.sw", 0x369e1311696c102a, 12},
    {"iso.wide.edge.w", 0x68a56148975915f9, 12},
    {"iso.wide.edge.nw", 0xffc8bc1ca6218141, 12},
    {"iso.wide.edge.middle", 0x0873574063b87f55, 12},
    {"iso.wide.edge.radar", 0x4bf6605f1d891fb8, 12},
};
// END GOLDEN TABLE

}  // namespace

const Golden* find_golden(const std::string& name) {
    for (const Golden& g : kGoldens) {
        if (name == g.name) return &g;
    }
    return nullptr;
}

int main(int argc, char** argv) {
    bool list_only = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--print") g_print = true;
        else if (a == "--list") list_only = true;
        else if (a == "--only" && i + 1 < argc) g_only_prefix = argv[++i];
        else {
            std::fprintf(stderr, "usage: test_yard_isolation [--print | --list | --only PREFIX]\n");
            return 2;
        }
    }
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);                      // nothing is shown or heard
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 2;
    }
    assets::AssetArchive arc;
    if (!arc.load_chd(std::string(ORIGINAL_ASSETS_DIR) + "/ants.chd")) {
        std::fprintf(stderr, "cannot open ants.chd\n");
        return 2;
    }

    if (g_only_prefix.empty() && !g_print && !list_only) self_check(arc);
    for (const Picture& pic : kPictures) {
        render_scenarios(arc, pic);
        buttons_scenario(arc, pic);
        key_scenarios(arc, pic);
        edge_scenarios(arc, pic);
    }

    SDL_Quit();
    if (list_only) {
        for (const auto& m : g_measured) std::printf("%s\n", m.first.c_str());
        return 0;
    }
    if (g_print) {
        std::printf("    // %zu fingerprints\n", g_measured.size());
        for (const auto& m : g_measured) {
            std::printf("    {\"%s\", %s, %llu},\n", m.first.c_str(), hex64(m.second.hash).c_str(), static_cast<unsigned long long>(m.second.count));
        }
        return g_failures == 0 ? 0 : 1;
    }
    if (g_only_prefix.empty()) {
        for (const Golden& g : kGoldens) {
            ++g_checks;
            if (std::find(g_seen.begin(), g_seen.end(), g.name) == g_seen.end()) {
                ++g_failures;
                std::fprintf(stderr, "  FAIL [%s]: a golden number without a scenario\n", g.name);
            }
        }
    }
    std::printf("\nyard isolation: %zu fingerprints, %d checks, %d failures\n", g_measured.size(), g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
