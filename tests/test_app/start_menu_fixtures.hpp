// The fixtures of the start menu tests (test_start_menu.cpp holds main(); the cases are in test_start_menu_panels.cpp and test_start_menu_hosting.cpp).
// Inline or in an anonymous namespace.
#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <memory>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include "ants_app/application.hpp"
#include "ants_app/config_store.hpp"
#include "ants_app/fps_overlay.hpp"
#include "ants_app/host_lookup.hpp"
#include "ants_app/start_menu.hpp"
#include "ants_app/text_layout.hpp"
#include "ants_net/protocol.hpp"
#include "ants_sim/sim_engine.hpp"
#include "ants_test_paths.hpp"
#include "../common/test_assert.hpp"

using namespace ants;
using namespace ants::app;

inline int g_test_count = 0;
inline int g_test_failures = 0;
inline int g_assert_count = 0;

inline void run_test_case(const std::string& name, const std::function<void()>& fn) {
    // ANTS_TEST_FILTER=text runs only the cases whose title contains the text (for working on one test and for mutation runs; the suite as run_tests.sh runs it has no filter)
    if (const char* filter = std::getenv("ANTS_TEST_FILTER")) {
        if (name.find(filter) == std::string::npos) return;
    }
    ++g_test_count;
    std::cout << "  RUNNING: " << std::left << std::setw(110) << name << " ... " << std::flush;
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


namespace fs = std::filesystem;

namespace {

// ---- helpers -------------------------------------------------------------------------------------------------------------------------------------------

struct TempDir {
    fs::path path;
    TempDir() {
        std::error_code ec;
        path = fs::temp_directory_path(ec) / ("ants_menu_model_" + std::to_string(reinterpret_cast<uintptr_t>(this)) + "_" + std::to_string(std::rand()));
        fs::create_directories(path, ec);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    std::string file(const std::string& name) const { return (path / name).string(); }
};

struct Sounds {
    std::vector<uint32_t> played;
    int count(uint32_t id) const { return static_cast<int>(std::count(played.begin(), played.end(), id)); }
};

struct Clipboard {
    std::string text;                 // what the system clipboard holds
    std::vector<std::string> writes;  // every Copy
    bool writable{true};
};

// A menu as the application sets it up: the player's name proposed, the default server, the sounds and the clipboard recorded
struct Rig {
    StartMenu menu;
    Sounds sounds;
    Clipboard clipboard;
    std::vector<MenuSetting> changes;
    explicit Rig(uint8_t own_seat = 0, const std::string& name = "Player") {
        MenuSettings s;
        s.name = name;
        menu.set_own_seat(own_seat);
        menu.set_settings(s);
        menu.set_server(ServerAddress{});
        menu.set_on_play_sfx([this](uint32_t id) { sounds.played.push_back(id); });
        menu.set_on_change([this](MenuSetting which) { changes.push_back(which); });
        menu.set_clipboard([this]() { return clipboard.text; }, [this](const std::string& text) {
            clipboard.writes.push_back(text);
            return clipboard.writable;
        });
        menu.show_main();
    }
    // A key as a person presses it: a panel has been up for longer than StartMenu::kSettleMs by then (Enter and Space ignore a press within it, Esc does not quit from the first panel;
    // the tests of that rule press with `quick_key`)
    void key(SDL_Keycode k, uint16_t mod = 0, bool repeat = false) {
        if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_SPACE || k == SDLK_ESCAPE) menu.update(static_cast<float>(StartMenu::kSettleMs + 10) / 1000.0f);
        menu.on_key(k, mod, repeat);
    }
    // A key that comes at once (no time passes in the menu's clock)
    void quick_key(SDL_Keycode k, uint16_t mod = 0, bool repeat = false) { menu.on_key(k, mod, repeat); }
    void type(const std::string& text) { menu.on_text(text); }
    MenuElement element(MenuId id) const {
        MenuElement e;
        menu.find_element(id, e);
        return e;
    }
    bool exists(MenuId id) const {
        MenuElement e;
        return menu.find_element(id, e);
    }
    void mouse_move(MenuId id) {
        const MenuElement e = element(id);
        menu.on_mouse_move(e.rect.x + e.rect.w / 2, e.rect.y + e.rect.h / 2);
    }
    // A click by the mouse: move, press, release in the middle of the control
    void click(MenuId id) {
        const MenuElement e = element(id);
        const int32_t x = e.rect.x + e.rect.w / 2;
        const int32_t y = e.rect.y + e.rect.h / 2;
        menu.on_mouse_move(x, y);
        menu.on_mouse_down(x, y, SDL_BUTTON_LEFT);
        menu.on_mouse_up(x, y, SDL_BUTTON_LEFT);
    }
    MenuRequest take() { return menu.take_request(); }
    // Goes to a panel by the first panel's entries
    void to_panel(MenuId entry) {
        menu.show_main();
        click(entry);
    }
};

inline std::string element_text(const std::vector<MenuElement>& all, MenuKind kind) {
    for (const MenuElement& e : all) {
        if (e.kind == kind) return e.text;
    }
    return std::string();
}

inline bool has_text(const std::vector<MenuElement>& all, const std::string& needle) {
    for (const MenuElement& e : all) {
        if (e.text.find(needle) != std::string::npos || e.value.find(needle) != std::string::npos) return true;
    }
    return false;
}

inline std::vector<MenuId> control_ids(const StartMenu& menu) {
    std::vector<MenuId> ids;
    for (const MenuElement& e : menu.elements()) {
        if (e.id != MenuId::None) ids.push_back(e.id);
    }
    return ids;
}

inline bool intersects(const ButtonRect& a, const ButtonRect& b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

// ---- a recording renderer ---------------------------------------------------------------------------------------------------------------------------
//
// Every call is kept. The text width is an estimate that grows with the font size (the interface's own 6 px per character is the 12 px text only), so that the long texts of the
// tests are as wide as they would be on the real renderer.
class Recorder : public IRenderer {
public:
    struct Text {
        std::string text;
        int32_t x, y;
        FontSize size;
        assets::ColorRGBA color;
    };
    struct Box {
        ButtonRect rect;
        assets::ColorRGBA color;
    };
    std::vector<Text> texts;
    std::vector<Box> fills;
    std::vector<uint32_t> sprites;

    void draw_sprite(uint32_t id, int32_t, int32_t, bool) override { sprites.push_back(id); }
    void draw_named_sprite(const std::string&, int32_t, int32_t, bool) override {}
    void fill_rect(int32_t x, int32_t y, int32_t w, int32_t h, assets::ColorRGBA c) override { fills.push_back(Box{ButtonRect{x, y, w, h}, c}); }
    void draw_rect(int32_t x, int32_t y, int32_t w, int32_t h, assets::ColorRGBA c) override { fills.push_back(Box{ButtonRect{x, y, w, h}, c}); }
    void draw_text(const std::string& text, int32_t x, int32_t y, assets::ColorRGBA c) override { draw_text(text, x, y, c, FontSize::Px12); }
    void draw_text(const std::string& text, int32_t x, int32_t y, assets::ColorRGBA c, FontSize size) override { texts.push_back(Text{text, x, y, size, c}); }
    int32_t width_scale = 1;                                                                // 2: a font twice as wide as the estimate (the cut of every one-line text is reached)
    int32_t get_text_width(const std::string& text, FontSize size) const override {
        const int32_t per = std::max<int32_t>(6, static_cast<int32_t>(size) / 2) * width_scale;
        return static_cast<int32_t>(text.size()) * per;
    }
    int32_t get_text_height(FontSize size) const override { return font_cell_height(size); }
    void set_hud_team(uint8_t) override {}
};

inline assets::AssetArchive& archive() {
    static assets::AssetArchive a;
    static bool loaded = a.load_from_file(std::string(ORIGINAL_ASSETS_DIR) + "/ants.chd");
    (void)loaded;
    return a;
}

// Panels in the states that stress the layout: the longest names, codes and messages that the screen can hold
struct Variant {
    std::string what;
    std::function<void(Rig&)> set_up;
};

inline std::string long_message() {
    return "This is a very long message that goes on and on, much longer than any box of the screen could hold, with a room code like k7m2xq and a server "
           "like a-host-with-a-very-long-name.example.org:4001 in it, so that the cut has something to cut. It does not stop here either, and neither does this sentence.";
}

inline std::vector<Variant> all_variants() {
    std::vector<Variant> v;
    v.push_back({"first panel", [](Rig& r) { r.menu.show_main(); }});
    v.push_back({"first panel with a long notice", [](Rig& r) { r.menu.show_main(long_message()); }});
    v.push_back({"first panel with an offer to rejoin", [](Rig& r) {
        r.menu.set_rejoin(RejoinOffer{"MEET-1", 2, ServerAddress{}});
        r.menu.show_main();
    }});
    v.push_back({"first panel with an offer to rejoin, the longest code, and a long notice", [](Rig& r) {
        r.menu.set_rejoin(RejoinOffer{std::string(32, 'W'), 3, ServerAddress{}});
        r.menu.show_main(long_message());
    }});
    v.push_back({"single player, nobody", [](Rig& r) { r.to_panel(MenuId::Single); }});
    v.push_back({"single player, three bots", [](Rig& r) {
        r.to_panel(MenuId::Single);
        for (MenuId id : {MenuId::Seat1, MenuId::Seat2, MenuId::Seat3}) {
            r.click(id);
            r.click(id);
            r.click(id);
        }
    }});
    v.push_back({"single player, the longest name and a refusal", [](Rig& r) {
        r.to_panel(MenuId::Single);
        r.key(SDLK_UP);
        r.type("Bot (" + std::string(40, 'W'));                                    // 32 characters, and a name that is for computer players: refused with the long line
        r.key(SDLK_RETURN);
    }});
    v.push_back({"single player, three bots, the Teams row and a refusal", [](Rig& r) {
        r.to_panel(MenuId::Single);
        for (MenuId id : {MenuId::Seat1, MenuId::Seat2, MenuId::Seat3}) r.click(id);
        r.click(MenuId::Teams);
        for (int up = 0; up < 4; ++up) r.key(SDLK_UP);                             // Teams, Black, Blue, Red, the name
        ASSERT_TRUE(r.menu.selected() == MenuId::SingleName);
        r.type("Bot (" + std::string(40, 'W'));
        r.key(SDLK_RETURN);
    }});
    v.push_back({"single player, own seat 3", [](Rig& r) {
        r.menu.set_own_seat(3);
        r.to_panel(MenuId::Single);
        r.click(MenuId::Seat0);
    }});
    v.push_back({"join, empty", [](Rig& r) { r.to_panel(MenuId::JoinWithCode); }});
    v.push_back({"join, the longest name and code", [](Rig& r) {
        r.to_panel(MenuId::JoinWithCode);
        r.key(SDLK_UP);
        r.type(std::string(40, 'W'));
        r.key(SDLK_DOWN);
        r.type(std::string(40, 'W'));
    }});
    v.push_back({"join, long error", [](Rig& r) {
        r.to_panel(MenuId::JoinWithCode);
        r.key(SDLK_RETURN);
        r.menu.take_request();
        r.menu.connection_failed(long_message());
    }});
    v.push_back({"host", [](Rig& r) { r.to_panel(MenuId::HostOnline); }});
    v.push_back({"host, the longest name and a long error", [](Rig& r) {
        r.to_panel(MenuId::HostOnline);
        r.key(SDLK_UP);
        r.type(std::string(40, 'W'));
        r.key(SDLK_DOWN);
        r.key(SDLK_RETURN);
        r.menu.take_request();
        r.menu.connection_failed(long_message());
    }});
    v.push_back({"host, four players with a bot in each seat, teams and a long error", [](Rig& r) {
        r.to_panel(MenuId::HostOnline);
        for (MenuId id : {MenuId::HostSeat1, MenuId::HostSeat2, MenuId::HostSeat3}) {
            r.click(id);
            r.click(id);
            r.click(id);
        }
        r.click(MenuId::HostTeams);
        r.click(MenuId::Host);
        r.menu.take_request();
        r.menu.connection_failed(long_message());
    }});
    v.push_back({"host, three players with teams", [](Rig& r) {
        r.to_panel(MenuId::HostOnline);
        r.click(MenuId::HostPlayers);
        r.click(MenuId::HostPlayers);
        r.click(MenuId::HostSeat2);
        r.click(MenuId::HostTeams);
        r.click(MenuId::HostTeams);
    }});
    v.push_back({"host, two players", [](Rig& r) {
        r.to_panel(MenuId::HostOnline);
        r.click(MenuId::HostPlayers);
        r.click(MenuId::HostSeat1);
    }});
    v.push_back({"the room with three bots of three levels and the teams", [](Rig& r) {
        r.to_panel(MenuId::HostOnline);
        r.click(MenuId::HostSeat1);
        r.click(MenuId::HostSeat2);
        r.click(MenuId::HostSeat2);
        r.click(MenuId::HostSeat3);
        r.click(MenuId::HostSeat3);
        r.click(MenuId::HostSeat3);
        r.click(MenuId::HostTeams);
        r.menu.show_room("k7m2xq", 1, 4);
    }});
    v.push_back({"connecting, a long server", [](Rig& r) {
        r.menu.set_server(ServerAddress{std::string(60, 'h') + ".example.org", 4001});
        r.to_panel(MenuId::JoinWithCode);
        r.key(SDLK_RETURN);
    }});
    v.push_back({"the room's code", [](Rig& r) { r.menu.show_room("k7m2xq", 1, 4); }});
    v.push_back({"the room's longest code", [](Rig& r) { r.menu.show_room(std::string(32, 'W'), 4, 4); }});
    v.push_back({"the room, copy failed", [](Rig& r) {
        r.clipboard.writable = false;
        r.menu.show_room("k7m2xq", 2, 4);
        r.key(SDLK_c, KMOD_GUI);
    }});
    return v;
}

}  // namespace

// The groups of cases, run by main() in test_start_menu.cpp in this order
void run_start_menu_panels_tests();
void run_start_menu_hosting_tests();
