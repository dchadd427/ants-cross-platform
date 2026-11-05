// The Yard, part 2: the application (docs/YARD.md). A real headless game started as the Yard (the flag --yard, the settings of a hand-made config) over a map of the Yard's size: the command line's
// words, the picture at any size and any whole scale of the window, a frame that is the map and nothing else, the pointer, the keys and the fingers through the real event loop, the zoom that stops
// one notch out, the edge scroll that waits 250 ms, the settings that stay the game's, and the refusals of a game that is not the Yard.
#include "yard_fixtures.hpp"

#include "ants_app/touch_control.hpp"

using namespace yardtest;

namespace {

/// The map's own pixel size and the tiles: 128 x 96 tiles
constexpr int32_t kMapPxW = 128 * 32;
constexpr int32_t kMapPxH = 96 * 32;

std::vector<yard::AntRecord> scene() {
    return {make_ant(101, 0, 980, 790, idle_clip_of(4)),
            make_ant(102, 0, 1020, 812, idle_clip_of(3)),
            make_ant(103, 0, 980, 836, idle_clip_of(4)),
            make_ant(201, 1, 1130, 740, walk_clip_of(3), 2),
            make_ant(202, 2, 1190, 800, action_clip_of(ActionClip::Attack, 6), 3),
            make_ant(203, 3, 1160, 806, action_clip_of(ActionClip::Hit, 2), 2),
            make_ant(204, 2, 820, 700, walk_clip_of(6), 1),
            make_ant(205, 1, 1300, 900, idle_clip_of(0))};
}

/// The world pass alone, over the engine's world: what the picture would be with no HUD at all (nothing selected, no cursor, no health mark)
Picture world_only(Application& app) {
    app.renderer().set_health_marks(kNoMarks);
    app.renderer().begin_frame();
    app.renderer().render_world(app.sim().get_world_state(), app.sim().grid(), -1, {}, false, false, -1, -1, -1, 0.0f);
    return read_output(app);
}

/// How many pixels differ between two pictures, outside a rectangle (and inside it); the rectangle is in the canvas's pixels, the pictures are the output at `scale` times them
struct Diff {
    int outside{0};
    int inside{0};
};
Diff differ_around(const Picture& a, const Picture& b, const LayoutRect& box, int32_t scale = 1) {
    Diff d;
    if (a.w != b.w || a.h != b.h) {
        d.outside = -1;
        return d;
    }
    const LayoutRect scaled{box.x * scale, box.y * scale, box.w * scale, box.h * scale};
    for (int32_t y = 0; y < a.h; ++y) {
        for (int32_t x = 0; x < a.w; ++x) {
            const uint8_t* p = a.at(x, y);
            const uint8_t* q = b.at(x, y);
            const bool same = p[0] == q[0] && p[1] == q[1] && p[2] == q[2];
            if (same) continue;
            if (scaled.contains(x, y)) ++d.inside;
            else ++d.outside;
        }
    }
    return d;
}

/// The minimap's slot with its edge (the frame pieces that the game draws around a map that is not square)
LayoutRect minimap_with_edge(const ScreenLayout& layout) {
    const LayoutRect m = layout.minimap();
    return LayoutRect{m.x - 12, m.y - 8, m.w + 24, m.h + 20};
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------
// the command line
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------

ApplicationConfig parse(std::vector<std::string> words) {
    words.insert(words.begin(), "ants");
    std::vector<char*> argv;
    for (std::string& w : words) argv.push_back(w.data());
    return Application::parse_arguments(static_cast<int>(argv.size()), argv.data());
}

void flag_cases() {
    group("F1", "--yard: the flag, and what it cannot be combined with");
    const ApplicationConfig plain = parse({"--map", "x.lvl"});
    check(!plain.yard, "no flag, no Yard: the game as it was");
    const ApplicationConfig yard = parse({"--yard", "--map", "yard.lvl", "--window-size", "390x600"});
    check(yard.yard && yard.startup_error.empty() && !yard.start_in_map_select && yard.default_map_path == "yard.lvl", "the flag, the map and no start screen");
    check(yard.has_window_size && yard.window_w == 390 && yard.window_h == 600, "--window-size gives the canvas");
    const ApplicationConfig yard_only = parse({"--yard"});
    check(yard_only.yard && yard_only.startup_error.empty() && !yard_only.start_in_map_select && !yard_only.start_menu, "the flag alone: no setup screen, no start menu");
    check(parse({"--map", "x.lvl", "--yard", "--map-select"}).start_in_map_select == false, "--map-select does not bring the setup screen back");
    for (const std::vector<std::string>& other : {std::vector<std::string>{"--host"}, {"--join", "localhost"}, {"--join-url", "ws://localhost:1/ws"}, {"--bot", "1:easy"}, {"--alone"}, {"--play"},
                                                  {"--replay", "x.antsrep"}, {"--game-mode", "187"}}) {
        std::vector<std::string> words = {"--yard"};
        words.insert(words.end(), other.begin(), other.end());
        const ApplicationConfig bad = parse(words);
        check(!bad.startup_error.empty() && bad.startup_error.find("--yard") != std::string::npos, "--yard with " + other[0] + " is refused with a word about --yard: " + bad.startup_error);
        std::vector<std::string> reversed = other;
        reversed.push_back("--yard");
        check(!parse(reversed).startup_error.empty(), "... in the other order too");
    }
    check(parse({"--start-menu", "--yard"}).startup_error.size() > 0, "the start menu and the Yard do not go together");
    check(!parse({"--yard", "--seed", "5", "--name", "x", "--aspect", "4:3", "--zoom", "1.19"}).yard == false, "the options that only set something up go with it");
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------
// start, size, window
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------

void start_cases() {
    group("A1", "a game that starts as the Yard: the map alone, nobody plays, nothing is on the screen but the world");
    YardRig r;
    if (!r.ok) return check(false, "the Yard is up");
    Application& app = r.app;
    check(app.yard_mode() && app.layout().yard && app.state() == AppState::Playing && app.match_running(), "the Yard's picture is up, as a match");
    check(app.renderer().canvas_w() == 960 && app.renderer().canvas_h() == 540 && app.picture() == LayoutRect({0, 0, 960, 540}) && app.yard_scale() == 1, "960 x 540, the picture is the whole canvas, at the scale 1");
    check_rect(app.layout().view(), LayoutRect{0, 0, 960, 540}, "the view is the whole picture");
    check(app.sim().roster_mask() == 0 && app.sim().get_world_state().ants.empty() && app.sim().get_world_state().anthills.empty(), "nobody plays: no ant, no hill");
    check(app.sim().get_world_state().width == 128 && app.sim().get_world_state().height == 96, "the world is the Yard's map: 128 x 96");
    check(!app.hud().options().chat && !app.hud().chat_takes_text() && !app.web_chat_available(), "the chat is off, and takes no text from the page");
    check(app.window_rect().w == 960 && app.window_rect().h == 540, "the window is as large as the canvas");
    check(app.zoom() == 1.0f && app.remembered_zoom() == 1.0f, "the Yard starts at the normal scale");
    check(!app.hud().is_modal_open() && !app.hud().has_selection(), "no dialog, no selection");
    const int32_t cx = app.renderer().camera().centre_world_x();
    const int32_t cy = app.renderer().camera().centre_world_y();
    check(std::abs(cx - kMapPxW / 2) <= 1 && std::abs(cy - kMapPxH / 2) <= 1, "the view begins at the middle of the map: " + std::to_string(cx) + ", " + std::to_string(cy));
    r.frames(12);
    check(app.sim().current_tick() == 0, "twelve frames later the engine has not ticked");
    check(app.is_running(), "the game runs on (a headless game that waits for its screenshot)");
}

// (one game at a time: Application::shutdown ends SDL for the whole program, so a second game must not be made while a first one lives)
void start_map_cases() {
    group("A2", "the map file may be a map of the game: its hills, start markers and ants do not count");
    YardRig game(960, 540, std::string(), maps_dir() + "GAUNTLET.LVL");
    check(game.ok && game.app.yard_mode(), "a Yard on GAUNTLET");
    check(game.app.sim().get_world_state().ants.empty() && game.app.sim().get_world_state().anthills.empty() && game.app.sim().roster_mask() == 0, "no ant, no hill, nobody plays");
    game.frames(5);
    check(game.app.sim().get_world_state().ants.empty(), "five frames later still none");
    check(game.apply(scene()) == 8, "the batch is shown");
}

void refusal_cases() {
    group("A3", "the refusals: a size or a scale that is not a Yard's changes nothing");
    YardRig r;
    if (!r.ok) return check(false, "the Yard is up");
    Application& app = r.app;
    {
        const Picture before = r.frame();
        const LayoutRect picture = app.picture();
        for (const auto& s : std::vector<std::vector<int32_t>>{{319, 240, 1}, {320, 239, 1}, {0, 0, 1}, {-5, 600, 1}, {4097, 540, 1}, {960, 4097, 1}, {960, 540, 0}, {960, 540, 4}, {960, 540, -1},
                                                              {100000, 100000, 1}}) {
            check(!app.set_yard_size(s[0], s[1], s[2]), "set_yard_size(" + std::to_string(s[0]) + ", " + std::to_string(s[1]) + ", " + std::to_string(s[2]) + ") is refused");
            check(!app.start_yard(s[0], s[1], s[2]), "start_yard with them is refused");
        }
        check(app.renderer().canvas_w() == 960 && app.renderer().canvas_h() == 540 && app.picture() == picture && app.yard_scale() == 1 && app.layout().view() == LayoutRect({0, 0, 960, 540}), "nothing changed");
        const Picture after = r.frame();
        check(before.px == after.px, "the picture is the same, pixel for pixel");
        check(app.set_yard_size(320, 240, 1) && app.set_yard_size(4096, 4096, 3) && app.set_yard_size(960, 540, 1), "the smallest, the largest and the scale 3 are accepted");
    }
}

void size_cases() {
    group("Z1", "the picture follows the page's box through five shapes, the window is the canvas times a whole scale");
    YardRig r;
    if (!r.ok) return check(false, "the Yard is up");
    Application& app = r.app;
    r.apply(scene());
    r.app.yard_set_hills({{1, 36, 20}, {2, 24, 30}});
    const struct {
        int32_t w, h, scale;
    } shapes[] = {{960, 540, 1}, {700, 400, 1}, {390, 600, 2}, {1280, 720, 1}, {480, 320, 3}, {390, 600, 1}, {320, 240, 1}, {1920, 1080, 2}};
    for (const auto& s : shapes) {
        const std::string at = std::to_string(s.w) + " x " + std::to_string(s.h) + " at " + std::to_string(s.scale);
        check(app.set_yard_size(s.w, s.h, s.scale), at + ": accepted");
        r.frames(2);
        check(app.renderer().canvas_w() == s.w && app.renderer().canvas_h() == s.h && app.yard_scale() == s.scale, at + ": the canvas and the scale");
        check(app.window_rect().w == s.w * s.scale && app.window_rect().h == s.h * s.scale, at + ": the window is " + std::to_string(s.w * s.scale) + " x " + std::to_string(s.h * s.scale));
        check(app.picture() == LayoutRect({0, 0, s.w, s.h}) && app.layout().view() == LayoutRect({0, 0, s.w, s.h}) && app.layout().yard, at + ": the picture and the view are the whole canvas");
        const ViewportCamera& cam = app.renderer().camera();
        check(cam.view_x == 0 && cam.view_y == 0 && cam.viewport_w == s.w && cam.viewport_h == s.h, at + ": the camera's view is the canvas");
        check_rect(app.layout().minimap(), LayoutRect{s.w - 133, 14, 119, 91}, at + ": the minimap");
        check(app.hud().help_button().w == 0 && app.hud().get_move_pedestal_button().w == 0, at + ": the HUD's buttons have no size");
        r.app.yard_show_own();
        r.frames(1);
        app.hud().clear_selection();
        const auto a = r.screen_of(980, 790);
        const auto b = r.screen_of(1020, 836);
        r.drag(std::max(2, a.first - 40), std::max(2, a.second - 60), b.first + 40, b.second + 20);
        check(app.hud().get_selected_ant_ids().size() == 3, at + ": a box around the three own ants selects three");
        r.frames(1);
        app.hud().clear_selection();
        app.handle_window_event(YardRig::window_event(SDL_WINDOWEVENT_LEAVE));                // (the pointer is outside: no cursor over the picture)
        const Diff d = differ_around(r.frame(), world_only(app), minimap_with_edge(app.layout()), s.scale);
        check(d.outside == 0, at + ": outside the minimap the frame is the world pass alone (" + std::to_string(d.outside) + " pixels differ)");
        check(d.inside > 100, at + ": the minimap is there (" + std::to_string(d.inside) + " pixels)");
    }
    check(app.sim().current_tick() == 0, "the engine never ticked");
    check(r.app.yard_value(0) == 1920 && r.app.yard_value(1) == 1080 && r.app.yard_value(2) == 2, "the page's probes say so");
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------
// the frame
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------

void frame_cases() {
    group("W1", "a frame is the map and its ants, and the minimap: no panel, no top bar, no status line, no chat, no frame-rate plate");
    for (const auto& size : {std::make_pair(960, 540), std::make_pair(390, 600)}) {
        YardRig r(size.first, size.second);
        if (!r.ok) return check(false, "the Yard is up");
        Application& app = r.app;
        const std::string at = std::to_string(size.first) + " x " + std::to_string(size.second);
        r.apply(scene());
        r.app.yard_set_hills({{1, 36, 20}, {2, 24, 30}});
        r.app.yard_show_own();
        r.frames(3);
        const Picture frame = r.frame();
        const Picture world = world_only(app);
        const Diff d = differ_around(frame, world, minimap_with_edge(app.layout()));
        check(d.outside == 0, at + ": every pixel outside the minimap is the world pass's (" + std::to_string(d.outside) + " differ): none of the HUD's pieces, no plate in the corner");
        check(d.inside > 100, at + ": the minimap is drawn (" + std::to_string(d.inside) + " pixels)");
        // the corner where the game draws its frame rate and its version: the world alone
        int corner_differs = 0;
        for (int32_t y = size.second - 40; y < size.second; ++y) {
            for (int32_t x = size.first - 200; x < size.first; ++x) {
                const uint8_t* p = frame.at(x, y);
                const uint8_t* q = world.at(x, y);
                corner_differs += (p[0] != q[0] || p[1] != q[1] || p[2] != q[2]) ? 1 : 0;
            }
        }
        check(corner_differs == 0, at + ": the corner is the map's (" + std::to_string(corner_differs) + " pixels differ)");
        // the world has ants and hills in it: the picture is not an empty field
        int different_from_ground = 0;
        const uint8_t* ground = frame.at(2, size.second / 2);
        for (int32_t y = 0; y < size.second; y += 2) {
            for (int32_t x = 0; x < size.first; x += 2) {
                const uint8_t* p = frame.at(x, y);
                different_from_ground += (p[0] != ground[0] || p[1] != ground[1] || p[2] != ground[2]) ? 1 : 0;
            }
        }
        check(different_from_ground > 200, at + ": there is something to see on the map (ants, hills): " + std::to_string(different_from_ground));
    }

    group("W2", "the game's own frame still has the plate and the panel: the control of the test above");
    {
        AppRig normal;
        if (!normal.ok) return check(false, "the game is up");
        normal.app.renderer().pin_animation_clock(5000);
        normal.app.hud().set_ticks_function(&fixed_clock);
        normal.app.handle_window_event(YardRig::window_event(SDL_WINDOWEVENT_LEAVE));
        normal.app.hud().dismiss_match_start_modal();
        normal.app.render_frame();
        const Picture frame = read_output(normal.app);
        const Picture world = world_only(normal.app);
        const Diff d = differ_around(frame, world, LayoutRect{16, 21, 762, 500});
        check(d.outside > 20000, "in the game the panel and the frame are outside the view: " + std::to_string(d.outside) + " pixels");
        int corner_differs = 0;
        for (int32_t y = 540 - 40; y < 540; ++y) {
            for (int32_t x = 960 - 200; x < 960; ++x) {
                const uint8_t* p = frame.at(x, y);
                const uint8_t* q = world.at(x, y);
                corner_differs += (p[0] != q[0] || p[1] != q[1] || p[2] != q[2]) ? 1 : 0;
            }
        }
        check(corner_differs > 100, "... and its corner is the panel's, with the frame-rate plate and the version: " + std::to_string(corner_differs) + " pixels");
        check(!normal.app.yard_mode() && !normal.app.layout().yard, "the game is no Yard");
    }
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------
// the pointer and the keys through the event loop
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------

void input_cases() {
    group("I1", "the mouse through the application: a drag box, a click, a right click, a hill, the minimap");
    YardRig r;
    if (!r.ok) return check(false, "the Yard is up");
    Application& app = r.app;
    r.apply(scene());
    app.yard_set_hills({{1, 36, 20}, {2, 24, 30}});
    app.renderer().camera().set_origin(700.0, 600.0, 128, 96);
    int markers = 0;
    app.hud().set_on_spawn_click_marker([&markers](int32_t, int32_t) { ++markers; });
    app.handle_window_event(YardRig::window_event(SDL_WINDOWEVENT_ENTER));
    r.frames(2);
    const auto a = r.screen_of(980, 790);
    const auto b = r.screen_of(1020, 836);
    r.drag(a.first - 40, a.second - 60, b.first + 40, b.second + 20);
    check(app.hud().get_selected_ant_ids().size() == 3 && app.yard_value(6) == 3, "a drag box around the visitor's three ants selects three");
    app.hud().clear_selection();
    r.drag(40, 60, 600, 300);
    bool only_mine = app.hud().get_selected_ant_ids().size() == 3;
    for (const uint32_t id : app.hud().get_selected_ant_ids()) only_mine = only_mine && app.yard_puppet()->is_own(id);
    check(only_mine, "a box over the others' ants as well selects the visitor's own three and no other");
    r.frames(2);
    const std::string selected_text = app.hud().status_line().text();
    check(!selected_text.empty(), "the selection has its status text: " + selected_text);

    (void)app.yard_take_intents();
    markers = 0;
    app.hud().clear_status();
    const auto ground = r.screen_of(1060, 900);
    r.click(ground.first, ground.second, SDL_BUTTON_RIGHT);
    std::vector<yard::Intent> intents = app.yard_take_intents();
    check(intents.size() == 1 && intents[0].kind == yard::IntentKind::Move && intents[0].tile_x == 1060 / 32 && intents[0].tile_y == 900 / 32 && intents[0].ants.size() == 3 && intents[0].target == 0,
          "a right click on the ground is a move of the three, to its tile");
    if (intents.size() == 1) {
        std::vector<uint16_t> named = intents[0].ants;
        std::sort(named.begin(), named.end());
        check(named == std::vector<uint16_t>({101, 102, 103}), "the ants are the visitor's three, by the server's ids");
    }
    check(markers == 1, "the order ring is spawned at the click");
    check(!app.hud().status_line().text().empty(), "the ant answers in the status line: " + app.hud().status_line().text());
    check(app.yard_take_intents().empty(), "the queue is empty after it is taken");

    markers = 0;
    const auto foe = r.screen_of(1190, 790);
    r.click(foe.first, foe.second, SDL_BUTTON_RIGHT);
    intents = app.yard_take_intents();
    check(intents.size() == 1 && intents[0].kind == yard::IntentKind::Attack && intents[0].target == 202 && intents[0].ants.size() == 3 && intents[0].tile_x == 1190 / 32 && intents[0].tile_y == 800 / 32,
          "a right click on another visitor's ant is an attack on it");
    check(markers == 1, "the ring is spawned");

    const auto hill = r.screen_of(36 * 32 + 64, 20 * 32 + 64);
    markers = 0;
    r.click(hill.first, hill.second, SDL_BUTTON_RIGHT);
    check(app.yard_take_intents().empty() && markers == 0, "a right click on a hill orders nothing");
    r.click(hill.first, hill.second);
    check(app.hud().get_selected_base_team_id() == 1 && app.hud().get_selected_ant_ids().empty(), "a click on a hill selects it");
    r.frames(1);
    const Picture with_brackets = r.frame();
    app.hud().clear_selection();
    const Picture without = r.frame();
    check(with_brackets.px != without.px, "the hill's brackets are drawn (the picture changed with the selection)");
    r.click(foe.first, foe.second);
    check(app.hud().get_selected_ant_id() == app.yard_puppet()->slot_id(202) && app.yard_value(6) == 1, "a click on another's ant inspects it");
    r.click(ground.first, ground.second, SDL_BUTTON_RIGHT);
    check(app.yard_take_intents().empty(), "with another's ant selected a right click gives no order");

    group("I2", "the minimap: a click on it takes the view there, a right click on it sends the selected ants");
    {
        app.renderer().camera().set_origin(0.0, 0.0, 128, 96);
        app.hud().clear_selection();
        r.frames(2);
        const LayoutRect mini = app.hud().minimap_rect();
        const int32_t mx = mini.x + mini.w * 3 / 4;
        const int32_t my = mini.y + mini.h * 3 / 4;
        r.motion(mx, my);
        r.button(true, SDL_BUTTON_LEFT, mx, my);
        r.frames(2);
        r.button(false, SDL_BUTTON_LEFT, mx, my);
        check(app.renderer().camera().x > 1000.0f && app.renderer().camera().y > 800.0f, "a press on the minimap moves the view at once: " + std::to_string(app.renderer().camera().x) + ", " + std::to_string(app.renderer().camera().y));
        app.yard_show_own();
        app.hud().select_all_friendly(app.sim().get_world_state());
        (void)app.yard_take_intents();
        r.click(mini.x + 30, mini.y + 30, SDL_BUTTON_RIGHT);
        const std::vector<yard::Intent> sent = app.yard_take_intents();
        check(sent.size() == 1 && sent[0].kind == yard::IntentKind::Move && sent[0].ants.size() == 3 && sent[0].tile_x >= 0 && sent[0].tile_x < 128 && sent[0].tile_y >= 0 && sent[0].tile_y < 96,
              "a right click on the minimap sends the three to that place of the map");
    }

    group("I3", "the keys through the application: Esc, Ctrl+A, N, P, S work; every other key is nothing");
    {
        r.apply(scene());
        r.frames(1);
        app.hud().clear_selection();
        r.key('a', static_cast<uint16_t>(KMOD_LCTRL));
        check(app.hud().get_selected_ant_ids().size() == 3, "Ctrl+A selects the visitor's three");
        r.key(SDLK_ESCAPE);
        check(!app.hud().has_selection(), "Esc deselects");
        r.key('n', static_cast<uint16_t>(KMOD_LCTRL));
        check(app.hud().get_selected_ant_ids().size() == 1, "Ctrl+N picks the next ant");
        app.hud().select_all_friendly(app.sim().get_world_state());
        (void)app.yard_take_intents();
        r.key('s', static_cast<uint16_t>(KMOD_LCTRL));
        const std::vector<yard::Intent> stops = app.yard_take_intents();
        check(stops.size() == 1 && stops[0].kind == yard::IntentKind::Stop && stops[0].ants.size() == 3, "Ctrl+S stops them");
        for (const SDL_Keycode k : {static_cast<SDL_Keycode>(SDLK_F1), static_cast<SDL_Keycode>(SDLK_F9), static_cast<SDL_Keycode>(SDLK_RETURN), static_cast<SDL_Keycode>('o'), static_cast<SDL_Keycode>('q'),
                                    static_cast<SDL_Keycode>('h'), static_cast<SDL_Keycode>(SDLK_SPACE)}) {
            r.key(k);
            r.key(k, static_cast<uint16_t>(KMOD_LCTRL));
        }
        check(!app.hud().is_modal_open() && !app.game_menu_is_open() && app.hud().get_chat_input().empty() && app.hud().get_selected_base_team_id() == -1, "no dialog, no menu, no chat, no hill");
        SDL_StartTextInput();
        SDL_Event text{};
        text.type = SDL_TEXTINPUT;
        std::snprintf(text.text.text, sizeof(text.text.text), "hello");
        SDL_PushEvent(&text);
        r.frames(2);
        check(app.hud().get_chat_input().empty(), "text from the page's keyboard is not taken: the chat is off");
        check(!app.web_chat_do(1, 'x') && app.hud().get_chat_input().empty(), "the page cannot type into the game's chat line");
    }
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------
// the fingers
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------

struct Finger {
    int32_t x;
    int32_t y;
};

struct Hand {
    explicit Hand(Application& a) : app(a) {
        app.set_touch_clock([this]() { return now; });
        SDL_Event stray;
        while (SDL_PollEvent(&stray)) {}
    }
    ~Hand() { app.set_touch_clock(nullptr); }
    Hand(const Hand&) = delete;
    Hand& operator=(const Hand&) = delete;

    void send(uint32_t type, int64_t finger, Finger at) {
        SDL_Event e;
        SDL_zero(e);
        e.type = type;
        e.tfinger.type = type;
        e.tfinger.timestamp = now;
        e.tfinger.touchId = 7;
        e.tfinger.fingerId = finger;
        e.tfinger.x = static_cast<float>((static_cast<double>(at.x) + static_cast<double>(app.picture().x) + 0.5) / static_cast<double>(app.renderer().canvas_w()));
        e.tfinger.y = static_cast<float>((static_cast<double>(at.y) + static_cast<double>(app.picture().y) + 0.5) / static_cast<double>(app.renderer().canvas_h()));
        SDL_PeepEvents(&e, 1, SDL_ADDEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT);
    }
    void down(int64_t finger, Finger at) { send(SDL_FINGERDOWN, finger, at); }
    void move(int64_t finger, Finger at) { send(SDL_FINGERMOTION, finger, at); }
    void up(int64_t finger, Finger at) { send(SDL_FINGERUP, finger, at); }
    void frame() { app.run_frame_with_delta(0.016f); }
    void rest(uint32_t ms) {
        while (ms > 0) {
            const uint32_t d = std::min<uint32_t>(20, ms);
            now += d;
            app.run_frame_with_delta(static_cast<float>(d) / 1000.0f);
            ms -= d;
        }
    }

    Application& app;
    uint32_t now{1000};
};

void touch_cases() {
    group("T1", "fingers through the event loop: a tap, a one-finger box, a hold, two fingers that pan and pinch");
    YardRig r(390, 600);
    if (!r.ok) return check(false, "the Yard is up");
    Application& app = r.app;
    r.apply(scene());
    app.yard_set_hills({{1, 36, 20}});
    app.renderer().camera().set_origin(800.0, 500.0, 128, 96);
    r.frames(2);
    Hand hand(app);
    const auto own = r.screen_of(980, 780);
    hand.down(1, Finger{own.first, own.second});
    hand.rest(60);
    hand.up(1, Finger{own.first, own.second});
    hand.rest(40);
    check(app.hud().get_selected_ant_ids().size() == 1 && app.yard_puppet()->is_own(app.hud().get_selected_ant_ids()[0]), "a tap on an own ant selects it");

    const auto a = r.screen_of(980, 790);
    const auto b = r.screen_of(1020, 836);
    const Finger f0{a.first - 40, a.second - 60};
    const Finger f1{b.first + 40, b.second + 20};
    hand.down(2, f0);
    hand.rest(40);
    hand.move(2, Finger{(f0.x + f1.x) / 2, (f0.y + f1.y) / 2});
    hand.rest(40);
    hand.move(2, f1);
    hand.rest(60);
    check(app.hud().is_dragging(), "a finger that moves is a rubber band");
    hand.up(2, f1);
    hand.rest(60);
    check(app.hud().get_selected_ant_ids().size() == 3, "a one-finger box around the three selects three");

    (void)app.yard_take_intents();
    const auto ground = r.screen_of(1060, 700);
    hand.down(3, Finger{ground.first, ground.second});
    hand.rest(touch::kHoldMs + 250);
    hand.up(3, Finger{ground.first, ground.second});
    hand.rest(60);
    const std::vector<yard::Intent> sent = app.yard_take_intents();
    check(sent.size() == 1 && sent[0].kind == yard::IntentKind::Move && sent[0].tile_x == 1060 / 32 && sent[0].tile_y == 700 / 32 && sent[0].ants.size() == 3, "a hold on the ground is a right click: a move of the three");

    ViewportCamera& camera = app.renderer().camera();
    app.hud().clear_selection();
    camera.set_origin(800.0, 800.0, 128, 96);
    const float cy0 = camera.y;
    hand.down(4, Finger{120, 300});
    hand.down(5, Finger{200, 300});
    hand.rest(40);
    hand.move(4, Finger{120, 380});
    hand.move(5, Finger{200, 380});
    hand.rest(40);
    hand.move(4, Finger{120, 440});
    hand.move(5, Finger{200, 440});
    hand.rest(40);
    hand.up(4, Finger{120, 440});
    hand.up(5, Finger{200, 440});
    hand.rest(60);
    check(camera.y < cy0 - 100.0f, "two fingers dragged down move the view: the world follows them (" + std::to_string(cy0) + " -> " + std::to_string(camera.y) + ")");

    const std::vector<float> levels = app.zoom_levels();
    check(levels.size() == 6 && levels.front() == 2.0f && levels.back() == zoom::series(-1), "the levels end one notch out of 1: " + zoom::level_name(levels.back()));
    camera.set_origin(800.0, 800.0, 128, 96);
    hand.down(6, Finger{195 - 50, 300});
    hand.down(7, Finger{195 + 50, 300});
    hand.rest(20);
    for (int i = 1; i <= 8; ++i) {
        hand.move(6, Finger{195 - 50 - 5 * i, 300});
        hand.move(7, Finger{195 + 50 + 5 * i, 300});
        hand.rest(16);
    }
    check(app.zoom() > 1.0f && app.zoom() <= 2.0f, "fingers spread: the view zooms in: " + zoom::level_name(app.zoom()));
    hand.up(6, Finger{100, 300});
    hand.up(7, Finger{290, 300});
    hand.rest(40);
    hand.down(6, Finger{195 - 150, 300});
    hand.down(7, Finger{195 + 150, 300});
    hand.rest(20);
    for (int i = 1; i <= 29; ++i) {
        hand.move(6, Finger{195 - 150 + 5 * i, 300});
        hand.move(7, Finger{195 + 150 - 5 * i, 300});
        hand.rest(16);
    }
    check(app.zoom() == zoom::series(-1), "a long pinch ends one notch out of 1, never further: " + zoom::level_name(app.zoom()));
    hand.up(6, Finger{195, 300});
    hand.up(7, Finger{195, 300});
    hand.rest(40);
    check(app.touch().fingers() == 0, "the fingers are lifted");
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------
// the zoom and the edge scroll
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------

void zoom_cases() {
    group("Q1", "the Yard's zoom stops one notch out of the normal scale; the game's own zoom is as it was");
    YardRig r;
    if (!r.ok) return check(false, "the Yard is up");
    Application& app = r.app;
    const std::vector<float> levels = app.zoom_levels();
    check(levels.size() == 6 && levels[0] == 2.0f && levels[4] == 1.0f && levels[5] == zoom::series(-1), "the levels: 2, 1.68, 1.41, 1.19, 1 and 0.84");
    check(zoom::series(-1) > 0.83f && zoom::series(-1) < 0.85f, "0.84 is about 0.8 of the normal scale: the level of the game's series nearest to 0.8");
    check(app.zoom_limits() == zoom::Limits{zoom::series(-1), zoom::kIn}, "the limits of the Yard: one notch out, and 2 in");
    app.handle_window_event(YardRig::window_event(SDL_WINDOWEVENT_ENTER));
    r.motion(480, 270);
    for (int i = 0; i < 12; ++i) notch(app, -1);
    check(app.zoom() == zoom::series(-1), "twelve notches out: the zoom stops at " + zoom::level_name(app.zoom()));
    r.frames(2);
    check(app.zoom() == zoom::series(-1), "and stays there after a frame");
    for (int i = 0; i < 12; ++i) notch(app, 1);
    check(app.zoom() == 2.0f, "twelve notches in: 2");
    r.button(true, SDL_BUTTON_MIDDLE, 480, 270);
    r.button(false, SDL_BUTTON_MIDDLE, 480, 270);
    check(app.zoom() == 1.0f, "the middle button is back to 1");
    app.set_zoom(0.1f, 480, 270);
    check(app.zoom() == zoom::series(-1), "a zoom that is asked for far out is the last level");
    check(!app.set_zoom(0.1f, 480, 270), "(and asking again changes nothing)");
}

void zoom_shape_cases() {
    group("Q1b", "the same levels for an upright phone's canvas, a big one and the smallest");
    {
        YardRig tall(390, 600);
        const std::vector<float> upright = tall.app.zoom_levels();
        check(upright.size() == 6 && upright.back() == zoom::series(-1), "an upright phone's canvas has the same levels");
        check(tall.app.set_yard_size(1920, 1080, 1) && tall.app.zoom_levels().back() == zoom::series(-1), "so has a big one");
        check(tall.app.set_yard_size(320, 240, 1) && tall.app.zoom_levels().back() == zoom::series(-1), "and the smallest canvas");
    }
}

void zoom_game_cases() {
    group("Q2", "the same map in the game: its zoom goes as far out as the map allows, and nothing of the Yard is in it");
    AppRig game(Aspect::Wide16x9);
    if (!game.ok) return check(false, "the game is up");
    const std::vector<float> normal = game.app.zoom_levels();
    check(game.app.zoom_limits() == zoom::Limits::any(), "the game's limits are as they were");
    check(normal == zoom::levels(game.app.zoom_fit(), zoom::Limits::any()), "the game's levels are the model's");
    check(normal.back() < zoom::series(-1) - 0.1f && normal.size() > 6, "the game zooms out further than the Yard does: " + zoom::level_name(normal.back()));
}

void edge_cases() {
    group("E1", "the edge scroll waits for the pointer: five frames of 50 ms at the edge and the view stays, the sixth moves it");
    YardRig r;
    if (!r.ok) return check(false, "the Yard is up");
    Application& app = r.app;
    ViewportCamera& camera = app.renderer().camera();
    app.handle_window_event(YardRig::window_event(SDL_WINDOWEVENT_ENTER));
    camera.set_origin(1500.0, 1000.0, 128, 96);
    r.motion(959, 270);
    const float x0 = camera.x;
    for (int frame = 1; frame <= 5; ++frame) {
        r.frames(1, 0.05f);
        check(camera.x == x0, "frame " + std::to_string(frame) + " at the east edge: the view stays");
    }
    r.frames(1, 0.05f);
    check(camera.x > x0, "the sixth frame (250 ms) moves it: " + std::to_string(x0) + " -> " + std::to_string(camera.x));
    const float x1 = camera.x;
    r.frames(1, 0.05f);
    check(camera.x > x1, "and the next one again");
    r.motion(480, 270);
    r.frames(3, 0.05f);
    const float x2 = camera.x;
    r.motion(959, 270);
    r.frames(5, 0.05f);
    check(camera.x == x2, "away and back: the dwell starts over");
    r.frames(1, 0.05f);
    check(camera.x > x2, "and the sixth moves it");

    group("E2", "the pointer that is outside the window scrolls nothing, however long it was at the edge");
    {
        camera.set_origin(1500.0, 1000.0, 128, 96);
        r.motion(959, 270);
        r.frames(3, 0.05f);
        app.handle_window_event(YardRig::window_event(SDL_WINDOWEVENT_LEAVE));
        const float x = camera.x;
        r.frames(12, 0.05f);
        check(camera.x == x, "the pointer has left the page's canvas: no scroll");
    }
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------
// the API of a page
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------

void api_cases() {
    group("B1", "the server's word and the visitor's orders: yard_apply, yard_set_hills, yard_take_intents, the probes");
    YardRig r;
    if (!r.ok) return check(false, "the Yard is up");
    Application& app = r.app;
    check(app.yard_value(3) == 0 && app.yard_value(4) == 0 && app.yard_value(5) == 0 && app.yard_value(7) == 0 && app.yard_value(11) == 0, "nothing is shown yet");
    yard::Batch batch;
    batch.tick = 4242;
    batch.ants = scene();
    check(app.yard_apply(batch) == 8, "a batch of eight ants: eight shown");
    check(app.yard_value(3) == 8 && app.yard_value(4) == 3 && app.yard_value(11) == 4242, "the probes: eight ants, three of them the visitor's, the tick");
    check(app.yard_set_hills({{1, 36, 20}, {2, 24, 30}, {0, 1, 1}}) == 2 && app.yard_value(5) == 2, "two places with a game (the one with id 0 is left out)");
    check(app.yard_value(0) == 960 && app.yard_value(1) == 540 && app.yard_value(2) == 1 && app.yard_value(8) == 100, "the canvas, the scale, the zoom times 100");
    check(app.yard_value(12) == -1 && app.yard_value(-1) == -1 && app.yard_value(1000) == -1, "any other number: -1");
    check(app.yard_show_own() && std::abs(app.yard_value(9) - 993) <= 2 && std::abs(app.yard_value(10) - 812) <= 2, "the view goes to the middle of the visitor's ants: " + std::to_string(app.yard_value(9)) + ", " + std::to_string(app.yard_value(10)));
    app.yard_center_on(2000, 1500);
    check(std::abs(app.yard_value(9) - 2000) <= 1 && std::abs(app.yard_value(10) - 1500) <= 1, "the view can be put on any world pixel");
    app.yard_center_on(-5000, 99999);
    check(app.yard_value(9) >= 0 && app.yard_value(9) < kMapPxW && app.yard_value(10) >= 0 && app.yard_value(10) < kMapPxH, "... and keeps to the map");
    batch.ants.clear();
    check(app.yard_apply(batch) == 0 && !app.yard_show_own() && app.yard_value(3) == 0, "an empty batch: no ant, no middle");
}

void selection_cases() {
    group("B4", "the application hands the HUD's selection to the puppet: what is selected never turns into a stranger");
    YardRig r;
    if (!r.ok) return check(false, "the Yard is up");
    Application& app = r.app;
    r.apply(scene());
    app.hud().select_all_friendly(app.sim().get_world_state());
    const std::vector<uint32_t> mine = app.hud().get_selected_ant_ids();
    const uint32_t dying = app.yard_puppet()->slot_id(103);
    check(mine.size() == 3 && dying != 0, "three own ants are selected");
    std::vector<yard::AntRecord> batch = scene();
    batch.erase(std::remove_if(batch.begin(), batch.end(), [](const yard::AntRecord& a) { return a.id == 103; }), batch.end());
    r.apply(batch);
    check(find_ant(app.sim().get_world_state(), dying) == nullptr, "ant 103 dies: its slot is empty");
    for (uint16_t id = 2000; id < 2153; ++id) batch.push_back(make_ant(id, static_cast<uint8_t>(1 + id % 3), 100 + (id % 60) * 60, 100 + (id / 60) * 30, idle_clip_of(4)));
    check(batch.size() == 160, "160 ants are sent for a pool of 160 slots");
    check(r.apply(batch) == 159, "one slot is still named by the selection: 159 are shown, the last of the others is left out");
    check(find_ant(app.sim().get_world_state(), dying) == nullptr, "none of them has the slot that the selection still names");
    check(app.hud().get_selected_ant_ids() == mine, "the selection is as it was");
    r.frames(2);
    r.key(SDLK_ESCAPE);
    check(app.hud().get_selected_ant_ids().empty(), "Esc deselects");
    check(r.apply(batch) == 160 && find_ant(app.sim().get_world_state(), dying) != nullptr, "when nothing is selected the slot is free again: all 160 are shown");
}

void api_game_cases() {
    group("B2", "a game that is not the Yard refuses all of it");
    AppRig game(Aspect::Wide16x9);
    if (!game.ok) return check(false, "the game is up");
    Application& g = game.app;
    yard::Batch batch;
    batch.tick = 1;
    batch.ants = scene();
    const sim::StateHash before = g.sim().state_hash();
    check(!g.yard_mode() && g.yard_puppet() == nullptr && g.yard_sink() == nullptr && g.yard_scale() == 1, "no Yard, no puppet, no sink");
    check(g.yard_apply(batch) == 0 && g.yard_set_hills({{1, 36, 20}}) == 0 && g.yard_take_intents().empty() && !g.yard_show_own(), "every call of the Yard refuses");
    check(!g.set_yard_size(960, 540, 1) && g.layout() == ScreenLayout::with_size(960, 540) && !g.layout().yard, "the size does not change, the layout is the game's");
    g.yard_center_on(100, 100);
    for (int what = -1; what < 14; ++what) check(g.yard_value(what) == -1, "probe " + std::to_string(what) + " is -1");
    check(g.sim().state_hash() == before, "the simulation was not touched");
}

void start_yard_cases() {
    group("B3", "start_yard puts a game that is up into the Yard, at any scale");
    AppRig game(Aspect::Wide16x9);
    if (!game.ok) return check(false, "the game is up");
    Application& g = game.app;
    g.hud().dismiss_match_start_modal();
    check(g.start_yard(390, 600, 2), "start_yard(390, 600, 2)");
    check(g.yard_mode() && g.layout().yard && g.renderer().canvas_w() == 390 && g.window_rect().w == 780 && g.window_rect().h == 1200 && g.yard_scale() == 2, "the Yard is up: 390 x 600 in a window of 780 x 1200");
    check(g.sim().get_world_state().ants.empty() && g.sim().roster_mask() == 0 && !g.hud().options().chat, "no ant of the match is left, no chat");
    yard::Batch batch;
    batch.tick = 1;
    batch.ants = scene();
    check(g.yard_apply(batch) == 8, "and it takes batches");
    g.render_frame();
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------
// the settings
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------

std::string file_text(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void settings_cases() {
    group("K1", "the Yard leaves the game's settings as they are: the zoom it remembers, the chat option, everything");
    const std::filesystem::path file = std::filesystem::temp_directory_path() / "ants_yard_test_settings.ini";
    const std::string original = "Participate In Chat=1\nzoom=0.5\nSound Volume=40\n";
    {
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out << original;
    }
    {
        YardRig r(960, 540, file.string());
        if (!r.ok) return check(false, "the Yard is up");
        Application& app = r.app;
        check(app.zoom() == 1.0f && app.remembered_zoom() == 1.0f, "the game's remembered zoom 0.5 is not the Yard's: it starts at 1");
        check(!app.hud().options().chat && !app.hud().chat_takes_text(), "the stored chat option is on, and the Yard has the chat off all the same");
        app.handle_window_event(YardRig::window_event(SDL_WINDOWEVENT_ENTER));
        r.motion(480, 270);
        for (int i = 0; i < 3; ++i) notch(app, -1);
        for (int i = 0; i < 2; ++i) notch(app, 1);
        app.set_zoom(1.19f, 480, 270);
        r.apply(scene());
        r.frames(10);
        r.drag(100, 100, 300, 300);
        r.click(500, 400, SDL_BUTTON_RIGHT);
        app.set_yard_size(700, 400, 1);
        r.frames(3);
        check(app.zoom() != 1.0f, "the zoom did change in the Yard (" + zoom::level_name(app.zoom()) + ")");
    }
    check(file_text(file) == original, "the settings file is as it was: not a byte written");
    {
        AppRig game(Aspect::Wide16x9, 1.0f, false, file.string());                  // the control: in the game the same wheel writes the zoom
        if (!game.ok) return check(false, "the game is up");
        game.app.hud().dismiss_match_start_modal();
        game.app.handle_window_event(YardRig::window_event(SDL_WINDOWEVENT_ENTER));
        game.app.handle_mouse_motion([] { SDL_MouseMotionEvent m{}; m.type = SDL_MOUSEMOTION; m.x = 300; m.y = 250; return m; }());
        notch(game.app, -1);
        check(file_text(file) != original && file_text(file).find("zoom=") != std::string::npos, "in the game the wheel does write the zoom: " + file_text(file));
    }
    std::error_code ec;
    std::filesystem::remove(file, ec);
}

}  // namespace

void yard_app_groups() {
    flag_cases();
    start_cases();
    start_map_cases();
    refusal_cases();
    size_cases();
    frame_cases();
    input_cases();
    touch_cases();
    zoom_cases();
    zoom_shape_cases();
    zoom_game_cases();
    edge_cases();
    api_cases();
    selection_cases();
    api_game_cases();
    start_yard_cases();
    settings_cases();
}

/// The command line as a whole: init(argc, argv) with --yard
void yard_command_line_group() {
    group("F2", "--yard through init(argc, argv): the run-time flag as a page or a player passes it");
    const std::string map = yard_map_file();
    const std::filesystem::path settings = std::filesystem::temp_directory_path() / "ants_yard_test_cmdline.ini";
    for (const bool sized : {true, false}) {
        std::vector<std::string> words = {"ants", "--headless", "--yard", "--map", map, "--settings", settings.string(), "--screenshot", "never_written.png", "--frames", "1000000"};
        if (sized) {
            words.push_back("--window-size");
            words.push_back("390x600");
        }
        std::vector<char*> argv;
        for (std::string& w : words) argv.push_back(w.data());
        Application app;
        const QuietStdout quiet;
        const bool ok = app.init(static_cast<int>(argv.size()), argv.data());
        check(ok && app.yard_mode() && app.layout().yard, std::string(sized ? "sized: " : "unsized: ") + "the Yard starts from the command line");
        if (!ok) continue;
        check(app.renderer().canvas_w() == (sized ? 390 : 960) && app.renderer().canvas_h() == (sized ? 600 : 540), "the canvas is " + std::string(sized ? "390 x 600" : "960 x 540"));
        check(app.state() == AppState::Playing && app.sim().roster_mask() == 0 && !app.hud().options().chat, "playing, nobody plays, no chat");
        for (int i = 0; i < 5; ++i) app.run_frame_with_delta(0.05f);
        check(app.sim().current_tick() == 0, "no tick");
    }
    std::error_code ec;
    std::filesystem::remove(settings, ec);

    group("F3", "a command line that the Yard cannot take stops the game before anything is opened");
    {
        std::vector<std::string> words = {"ants", "--headless", "--yard", "--bot", "1:easy", "--map", map};
        std::vector<char*> argv;
        for (std::string& w : words) argv.push_back(w.data());
        Application app;
        const QuietStdout quiet;
        check(!app.init(static_cast<int>(argv.size()), argv.data()) && !app.yard_mode(), "--yard with --bot: init refuses");
    }
}
