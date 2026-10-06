// The desktop start menu, as a model (include/ants_app/start_menu.hpp): navigation by the keys and the mouse, the text fields, the seats of the single-player panel and the
// rule-8 line, the join and host panels, the room's code and the clipboard, the servers, the room codes and the names that the menu accepts, the remembered settings, where
// every control lies on the 640 x 480 screen, what is drawn and how long texts are cut, and which command-line options show or skip the menu. No window, no sockets: the
// application's paths against a real server are in test_start_menu_app.
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

using namespace ants;
using namespace ants::app;

static int g_test_count = 0;
static int g_test_failures = 0;
static int g_assert_count = 0;

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

std::string element_text(const std::vector<MenuElement>& all, MenuKind kind) {
    for (const MenuElement& e : all) {
        if (e.kind == kind) return e.text;
    }
    return std::string();
}

bool has_text(const std::vector<MenuElement>& all, const std::string& needle) {
    for (const MenuElement& e : all) {
        if (e.text.find(needle) != std::string::npos || e.value.find(needle) != std::string::npos) return true;
    }
    return false;
}

std::vector<MenuId> control_ids(const StartMenu& menu) {
    std::vector<MenuId> ids;
    for (const MenuElement& e : menu.elements()) {
        if (e.id != MenuId::None) ids.push_back(e.id);
    }
    return ids;
}

bool intersects(const ButtonRect& a, const ButtonRect& b) {
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
        assets::ColorRGBA colour;
    };
    struct Box {
        ButtonRect rect;
        assets::ColorRGBA colour;
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

assets::AssetArchive& archive() {
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

std::string long_message() {
    return "This is a very long message that goes on and on, much longer than any box of the screen could hold, with a room code like demo-gauntlet-4p-abcdef and a server "
           "like a-host-with-a-very-long-name.example.org:4001 in it, so that the cut has something to cut. It does not stop here either, and neither does this sentence.";
}

std::vector<Variant> all_variants() {
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
        r.menu.show_room("demo-small-4p-abcdef", 1, 4);
    }});
    v.push_back({"connecting, a long server", [](Rig& r) {
        r.menu.set_server(ServerAddress{std::string(60, 'h') + ".example.org", 4001});
        r.to_panel(MenuId::JoinWithCode);
        r.key(SDLK_RETURN);
    }});
    v.push_back({"the room's code", [](Rig& r) { r.menu.show_room("demo-small-4p-abcdef", 1, 4); }});
    v.push_back({"the room's longest code", [](Rig& r) { r.menu.show_room(std::string(32, 'W'), 4, 4); }});
    v.push_back({"the room, copy failed", [](Rig& r) {
        r.clipboard.writable = false;
        r.menu.show_room("demo-small-4p-abcdef", 2, 4);
        r.key(SDLK_c, KMOD_GUI);
    }});
    return v;
}

}  // namespace

int main(int argc, char* argv[]) {
    // SDL's headers rename main to SDL_main (SDL2main on Windows calls it): the signature must be this one, or the linker finds no SDL_main (the build guard in CMakeLists.txt checks it)
    (void)argc;
    (void)argv;
    std::cout << "=== Start menu ===\n";

    TEST_CASE("M1.1 First panel: the entries in their order, the first selected; Up and Down wrap round, Tab and Shift+Tab do the same; Esc is Quit and asks nothing else") {
        Rig r;
        ASSERT_EQ(r.menu.panel(), MenuPanel::Main);
        const std::vector<MenuId> expected = {MenuId::Single, MenuId::JoinWithCode, MenuId::HostOnline, MenuId::Quit};
        ASSERT_TRUE(control_ids(r.menu) == expected);
        ASSERT_EQ(element_text(r.menu.elements(), MenuKind::Title), std::string("Welcome to Ants!"));
        ASSERT_EQ(r.element(MenuId::Single).text, std::string("Single player"));
        ASSERT_EQ(r.element(MenuId::JoinWithCode).text, std::string("Join with a code"));
        ASSERT_EQ(r.element(MenuId::HostOnline).text, std::string("Host an online match"));
        ASSERT_EQ(r.element(MenuId::Quit).text, std::string("Quit"));
        ASSERT_EQ(r.menu.selected(), MenuId::Single);
        r.key(SDLK_DOWN);
        ASSERT_EQ(r.menu.selected(), MenuId::JoinWithCode);
        r.key(SDLK_DOWN);
        r.key(SDLK_DOWN);
        ASSERT_EQ(r.menu.selected(), MenuId::Quit);
        r.key(SDLK_DOWN);
        ASSERT_EQ(r.menu.selected(), MenuId::Single);                                      // wraps to the top
        r.key(SDLK_UP);
        ASSERT_EQ(r.menu.selected(), MenuId::Quit);                                        // and to the bottom
        r.key(SDLK_TAB);
        ASSERT_EQ(r.menu.selected(), MenuId::Single);
        r.key(SDLK_TAB, KMOD_SHIFT);
        ASSERT_EQ(r.menu.selected(), MenuId::Quit);
        ASSERT_FALSE(r.menu.has_request());                                                // moving asks for nothing
        r.key(SDLK_ESCAPE);
        ASSERT_TRUE(r.take().type == MenuRequest::Type::Quit);                             // Esc on the first panel: Quit, no question
        ASSERT_FALSE(r.menu.has_request());
        ASSERT_EQ(r.menu.panel(), MenuPanel::Main);
    } TEST_END();

    TEST_CASE("M1.2 First panel: Enter on an entry opens its panel (Quit asks to quit); a key that is held (repeat) does not act twice; Space acts like Enter on a button") {
        Rig r;
        r.key(SDLK_RETURN);
        ASSERT_EQ(r.menu.panel(), MenuPanel::Single);
        r.key(SDLK_ESCAPE);
        ASSERT_EQ(r.menu.panel(), MenuPanel::Main);
        ASSERT_EQ(r.menu.selected(), MenuId::Single);                                      // the entry that was left is the selection again
        r.key(SDLK_DOWN);
        r.key(SDLK_RETURN);
        ASSERT_EQ(r.menu.panel(), MenuPanel::Join);
        r.key(SDLK_ESCAPE);
        ASSERT_EQ(r.menu.selected(), MenuId::JoinWithCode);
        r.key(SDLK_DOWN);
        r.key(SDLK_RETURN);
        ASSERT_EQ(r.menu.panel(), MenuPanel::Host);
        r.key(SDLK_ESCAPE);
        ASSERT_EQ(r.menu.selected(), MenuId::HostOnline);
        r.key(SDLK_DOWN);
        r.key(SDLK_RETURN, 0, true);                                                       // a held Enter: ignored
        ASSERT_FALSE(r.menu.has_request());
        r.key(SDLK_SPACE);
        ASSERT_TRUE(r.take().type == MenuRequest::Type::Quit);
        r.key(SDLK_ESCAPE, 0, true);                                                       // a held Esc: ignored
        ASSERT_FALSE(r.menu.has_request());
        r.key(SDLK_KP_ENTER);
        ASSERT_TRUE(r.take().type == MenuRequest::Type::Quit);
    } TEST_END();

    TEST_CASE("M1.3 Mouse: the pointer selects a button it is over; a click is a press and a release on the same button (the click sound at the press); leaving the button, another button at the release and the right button do nothing") {
        Rig r;
        r.mouse_move(MenuId::HostOnline);
        ASSERT_EQ(r.menu.selected(), MenuId::HostOnline);
        r.menu.on_mouse_move(5, 5);                                                        // over nothing: the selection stays
        ASSERT_EQ(r.menu.selected(), MenuId::HostOnline);
        const MenuElement e = r.element(MenuId::JoinWithCode);
        const int32_t x = e.rect.x + 4;
        const int32_t y = e.rect.y + 4;
        r.menu.on_mouse_move(x, y);
        ASSERT_TRUE(r.menu.on_mouse_down(x, y, SDL_BUTTON_LEFT));
        ASSERT_EQ(r.sounds.count(sim::SoundID::ButtonClick), 1);                           // the click of the pressed button
        ASSERT_TRUE(r.element(MenuId::JoinWithCode).pressed);
        ASSERT_EQ(r.menu.panel(), MenuPanel::Main);                                        // nothing happens at the press
        r.menu.on_mouse_up(x, y, SDL_BUTTON_LEFT);
        ASSERT_EQ(r.menu.panel(), MenuPanel::Join);                                        // the action comes with the release
        r.menu.show_main();
        // pressed, then the pointer leaves the button: no action at the release even when it comes back
        r.menu.on_mouse_down(x, y, SDL_BUTTON_LEFT);
        r.menu.on_mouse_move(5, 5);
        ASSERT_FALSE(r.element(MenuId::JoinWithCode).pressed);
        r.menu.on_mouse_move(x, y);
        r.menu.on_mouse_up(x, y, SDL_BUTTON_LEFT);
        ASSERT_EQ(r.menu.panel(), MenuPanel::Main);
        // pressed on one button, released on another: nothing
        r.menu.on_mouse_down(x, y, SDL_BUTTON_LEFT);
        const MenuElement q = r.element(MenuId::Quit);
        r.menu.on_mouse_up(q.rect.x + 2, q.rect.y + 2, SDL_BUTTON_LEFT);
        ASSERT_FALSE(r.menu.has_request());
        ASSERT_EQ(r.menu.panel(), MenuPanel::Main);
        // the pointer over a field does not take the focus from the field that has it (typing would go to the wrong one); a click does
        r.menu.show_main();
        r.click(MenuId::JoinWithCode);
        ASSERT_EQ(r.menu.selected(), MenuId::Code);
        r.mouse_move(MenuId::Name);
        ASSERT_EQ(r.menu.selected(), MenuId::Code);
        r.mouse_move(MenuId::Join);
        ASSERT_EQ(r.menu.selected(), MenuId::Join);                                        // (a button is selected by the pointer)
        r.menu.show_main();
        r.menu.on_mouse_move(5, 5);
        // the right button never presses
        ASSERT_FALSE(r.menu.on_mouse_down(x, y, SDL_BUTTON_RIGHT));
        r.menu.on_mouse_up(x, y, SDL_BUTTON_RIGHT);
        ASSERT_EQ(r.menu.panel(), MenuPanel::Main);
        // a click on the background does nothing, a click on Quit asks for it
        ASSERT_FALSE(r.menu.on_mouse_down(3, 3, SDL_BUTTON_LEFT));
        r.click(MenuId::Quit);
        ASSERT_TRUE(r.take().type == MenuRequest::Type::Quit);
    } TEST_END();

    TEST_CASE("M2.1 Single player: one row per other seat in the colour's own words (own seat green: Red, Blue, Black; own seat blue: Green, Red, Black), every row Empty, no fog line, Continue is the selection and asks for no bots") {
        for (uint8_t own = 0; own < 4; ++own) {
            Rig r(own);
            r.to_panel(MenuId::Single);
            ASSERT_EQ(r.menu.panel(), MenuPanel::Single);
            const std::vector<MenuElement> all = r.menu.elements();
            size_t rows = 0;
            std::vector<std::string> colours;
            for (const MenuElement& e : all) {
                if (e.kind == MenuKind::Cycler) {
                    ++rows;
                    ASSERT_EQ(e.value, std::string("Empty"));
                }
                if (e.kind == MenuKind::Portrait) colours.push_back(std::to_string(e.team));
            }
            ASSERT_EQ(rows, static_cast<size_t>(3));
            ASSERT_EQ(colours.size(), static_cast<size_t>(3));
            // the seat of the player has no row, the other three do (their portraits are in their colours)
            std::vector<std::string> want;
            for (uint8_t s = 0; s < 4; ++s) {
                if (s != own) want.push_back(std::to_string(s));
            }
            ASSERT_TRUE(colours == want);
            ASSERT_FALSE(r.exists(static_cast<MenuId>(static_cast<uint8_t>(MenuId::Seat0) + own)));
            ASSERT_FALSE(has_text(all, "fog of war"));
            ASSERT_TRUE(has_text(all, StartMenu::kNoBotsLine));                            // nobody seated: the panel says that this is the original's game (L7)
            const uint8_t first_other = own == 0 ? uint8_t{1} : uint8_t{0};
            ASSERT_EQ(r.menu.selected(), static_cast<MenuId>(static_cast<uint8_t>(MenuId::Seat0) + first_other));   // the first INPUT, never a button that acts (M2)
            ASSERT_TRUE(r.menu.bots().empty());
        }
        Rig r0(0);
        r0.to_panel(MenuId::Single);
        ASSERT_TRUE(has_text(r0.menu.elements(), "Red") && has_text(r0.menu.elements(), "Blue") && has_text(r0.menu.elements(), "Black") && !has_text(r0.menu.elements(), "Green ants"));
        ASSERT_TRUE(has_text(r0.menu.elements(), "You play the green ants."));
        Rig r2(2);
        r2.to_panel(MenuId::Single);
        ASSERT_TRUE(has_text(r2.menu.elements(), "Green") && has_text(r2.menu.elements(), "Red") && has_text(r2.menu.elements(), "Black"));
        ASSERT_TRUE(has_text(r2.menu.elements(), "You play the blue ants."));
        r0.click(MenuId::Continue);                                                        // Continue with nobody: the original's single-player game
        const MenuRequest request = r0.take();
        ASSERT_TRUE(request.type == MenuRequest::Type::Single && request.bots.empty());
    } TEST_END();

    TEST_CASE("M2.2 Single player: a row cycles Empty, Easy, Medium, Hard and round by click, Enter and Right (Left goes back); the bots are the standard bot of the level at their seat as --bot SEAT:LEVEL would give them; the rule-8 line shows while any seat has a bot") {
        Rig r;
        r.to_panel(MenuId::Single);
        r.key(SDLK_DOWN);
        r.key(SDLK_DOWN);                                                                  // the last row (Black, seat 3): the first row is selected on arrival
        ASSERT_EQ(r.menu.selected(), MenuId::Seat3);
        ASSERT_EQ(r.element(MenuId::Seat3).value, std::string("Empty"));
        r.key(SDLK_RIGHT);
        ASSERT_EQ(r.element(MenuId::Seat3).value, std::string("Easy bot"));
        r.key(SDLK_RETURN);
        ASSERT_EQ(r.element(MenuId::Seat3).value, std::string("Medium bot"));
        r.click(MenuId::Seat3);
        ASSERT_EQ(r.element(MenuId::Seat3).value, std::string("Hard bot"));
        r.key(SDLK_RIGHT);
        ASSERT_EQ(r.element(MenuId::Seat3).value, std::string("Empty"));                   // round
        r.key(SDLK_LEFT);
        ASSERT_EQ(r.element(MenuId::Seat3).value, std::string("Hard bot"));                // and back round
        r.key(SDLK_LEFT);
        ASSERT_EQ(r.element(MenuId::Seat3).value, std::string("Medium bot"));
        ASSERT_TRUE(has_text(r.menu.elements(), "Bots play without fog of war."));         // rule 8: a bot never sees through the fog
        r.click(MenuId::Seat1);                                                            // Red: Easy
        const std::vector<ai::BotSpec> bots = r.menu.bots();
        ASSERT_EQ(bots.size(), static_cast<size_t>(2));
        ASSERT_TRUE(bots[0].seat == 1 && bots[0].kind == "standard" && bots[0].level == ai::Level::Easy);
        ASSERT_TRUE(bots[1].seat == 3 && bots[1].kind == "standard" && bots[1].level == ai::Level::Medium);
        ai::BotSpec parsed;
        std::string why;
        ASSERT_TRUE(ai::parse_bot_spec("1:easy", parsed, why) && parsed.seat == bots[0].seat && parsed.kind == bots[0].kind && parsed.level == bots[0].level);
        ASSERT_TRUE(ai::parse_bot_spec("3:medium", parsed, why) && parsed.seat == bots[1].seat && parsed.kind == bots[1].kind && parsed.level == bots[1].level);
        ASSERT_EQ(ai::bot_display_name(bots[1]), std::string("Bot (Medium)"));
        r.click(MenuId::Continue);
        const MenuRequest request = r.take();
        ASSERT_TRUE(request.type == MenuRequest::Type::Single && request.bots.size() == 2 && request.bots[0].seat == 1 && request.bots[1].seat == 3);
        // empty again: the line goes
        r.click(MenuId::Seat1);
        r.click(MenuId::Seat1);
        r.click(MenuId::Seat1);
        r.click(MenuId::Seat3);
        ASSERT_EQ(r.element(MenuId::Seat3).value, std::string("Hard bot"));
        r.click(MenuId::Seat3);
        ASSERT_TRUE(r.menu.bots().empty());
        ASSERT_FALSE(has_text(r.menu.elements(), "fog of war"));
        // every change is reported (the owner writes it to the settings file), a move of the selection is not
        const size_t before = r.changes.size();
        r.key(SDLK_UP);
        ASSERT_EQ(r.changes.size(), before);
        r.click(MenuId::Seat1);
        ASSERT_TRUE(r.changes.size() == before + 1 && r.changes.back() == MenuSetting::Bots);
        // Back (and Esc) return to the first panel with the choice kept
        r.key(SDLK_ESCAPE);
        ASSERT_EQ(r.menu.panel(), MenuPanel::Main);
        r.click(MenuId::Single);
        ASSERT_EQ(r.element(MenuId::Seat1).value, std::string("Easy bot"));
    } TEST_END();

    TEST_CASE("M2.3 Single player: a bot never sits at the player's own seat, whatever the seat is (the choice stored for that seat is not used)") {
        for (uint8_t own = 0; own < 4; ++own) {
            Rig r(own);
            MenuSettings s;
            s.name = "Player";
            for (auto& seat : s.seats) seat = SeatChoice::Hard;                            // a stored choice for every seat
            r.menu.set_settings(s);
            const std::vector<ai::BotSpec> bots = r.menu.bots();
            ASSERT_EQ(bots.size(), static_cast<size_t>(3));
            for (const ai::BotSpec& b : bots) ASSERT_TRUE(b.seat != own && b.seat < 4 && b.level == ai::Level::Hard);
        }
    } TEST_END();

    TEST_CASE("M2.4 Single player: a Teams choice appears under the seats once two or more of them have a bot (with fewer there is nothing to choose: the panel is the one that it always was), cycles Free for all, You + each bot and round by click, Enter and Left / Right, stands between the seats and Continue in the Tab order, and Continue asks for the teams as --teams would give them") {
        Rig r;
        r.to_panel(MenuId::Single);
        ASSERT_FALSE(r.exists(MenuId::Teams));                                             // nobody seated
        const ButtonRect continue_none = r.element(MenuId::Continue).rect;
        r.click(MenuId::Seat1);                                                            // Red: Easy
        ASSERT_FALSE(r.exists(MenuId::Teams));                                             // one bot: a team would be the whole match (it would end at the first point), nothing to choose
        ASSERT_TRUE(r.element(MenuId::Continue).rect.y == continue_none.y && r.element(MenuId::Continue).rect.h == continue_none.h);
        ASSERT_TRUE(r.menu.teams() == LocalTeams{});
        ASSERT_TRUE(r.menu.team_choices().size() == 1);
        r.click(MenuId::Seat3);                                                            // Black: Easy: two bots
        ASSERT_TRUE(r.exists(MenuId::Teams));
        ASSERT_EQ(r.element(MenuId::Teams).value, std::string("Free for all"));
        ASSERT_TRUE(r.element(MenuId::Teams).kind == MenuKind::Cycler);
        ASSERT_TRUE(has_text(r.menu.elements(), "Teams") && has_text(r.menu.elements(), StartMenu::kBotsLine));
        // the Tab order: the name, the rows, Teams, Continue, Back
        const std::vector<MenuId> ids = control_ids(r.menu);
        ASSERT_TRUE((ids == std::vector<MenuId>{MenuId::SingleName, MenuId::Seat1, MenuId::Seat2, MenuId::Seat3, MenuId::Teams, MenuId::Continue, MenuId::Back}));
        // the controls lie inside the panel, clear of each other, and under the seats
        {
            const std::vector<MenuElement> all = r.menu.elements();
            for (size_t i = 0; i < all.size(); ++i) {
                ASSERT_TRUE(all[i].rect.x >= 16 && all[i].rect.y >= 16 && all[i].rect.x + all[i].rect.w <= StartMenu::kWidth - 16 && all[i].rect.y + all[i].rect.h <= StartMenu::kHeight - 16);
                for (size_t j = i + 1; j < all.size(); ++j) ASSERT_FALSE(intersects(all[i].rect, all[j].rect));
            }
            ASSERT_TRUE(r.element(MenuId::Teams).rect.y >= r.element(MenuId::Seat3).rect.y + r.element(MenuId::Seat3).rect.h);
            ASSERT_TRUE(r.element(MenuId::Continue).rect.y >= r.element(MenuId::Teams).rect.y + r.element(MenuId::Teams).rect.h);
        }
        // the cycle: by click, Enter, Right, Left
        const size_t before = r.changes.size();
        r.click(MenuId::Teams);
        ASSERT_EQ(r.element(MenuId::Teams).value, std::string("You + Red"));
        ASSERT_TRUE(r.changes.size() == before + 1 && r.changes.back() == MenuSetting::Teams);
        ASSERT_TRUE(r.menu.teams() == LocalTeams({true, 0, 1}));
        ASSERT_EQ(r.menu.selected(), MenuId::Teams);
        r.key(SDLK_RETURN);
        ASSERT_EQ(r.element(MenuId::Teams).value, std::string("You + Black"));
        ASSERT_TRUE(r.menu.teams() == LocalTeams({true, 0, 3}));
        r.key(SDLK_RIGHT);
        ASSERT_EQ(r.element(MenuId::Teams).value, std::string("Free for all"));            // round
        ASSERT_TRUE(r.menu.teams() == LocalTeams{});
        r.key(SDLK_LEFT);
        ASSERT_EQ(r.element(MenuId::Teams).value, std::string("You + Black"));             // and back round
        r.key(SDLK_LEFT);
        ASSERT_EQ(r.element(MenuId::Teams).value, std::string("You + Red"));
        r.key(SDLK_UP);                                                                    // a move of the selection is no change
        const size_t after = r.changes.size();
        r.key(SDLK_DOWN);
        ASSERT_EQ(r.changes.size(), after);
        // Continue asks for the bots and the teams
        r.click(MenuId::Continue);
        MenuRequest request = r.take();
        ASSERT_TRUE(request.type == MenuRequest::Type::Single && request.bots.size() == 2 && request.bots[0].seat == 1 && request.bots[1].seat == 3);
        ASSERT_TRUE(request.teams == LocalTeams({true, 0, 1}));
        // the choice survives a change of the other seats that keeps its bot, and Back and the way in again
        r.click(MenuId::Seat3);                                                            // Black: Medium
        ASSERT_EQ(r.element(MenuId::Teams).value, std::string("You + Red"));
        r.key(SDLK_ESCAPE);
        r.click(MenuId::Single);
        ASSERT_EQ(r.element(MenuId::Teams).value, std::string("You + Red"));
        // a third bot: one more choice
        r.click(MenuId::Seat2);
        ASSERT_TRUE(r.menu.team_choices().size() == 4);
        r.click(MenuId::Teams);
        ASSERT_EQ(r.element(MenuId::Teams).value, std::string("You + Blue"));
        r.click(MenuId::Teams);
        ASSERT_EQ(r.element(MenuId::Teams).value, std::string("You + Black"));
        // the team's bot leaves (its row goes round to Empty): free for all again, written (the file never says a team with a seat that has no bot), and with one bot left the row is gone
        r.click(MenuId::Seat3);                                                            // Black: Hard (still a bot: the choice stays)
        ASSERT_EQ(r.element(MenuId::Teams).value, std::string("You + Black"));
        r.click(MenuId::Seat3);                                                            // Black: Empty
        ASSERT_TRUE(r.menu.settings().teams == LocalTeams{});
        ASSERT_TRUE(r.changes.size() >= 2 && r.changes.back() == MenuSetting::Teams && r.changes[r.changes.size() - 2] == MenuSetting::Bots);       // (the seat, then the team that went with it)
        ASSERT_EQ(r.element(MenuId::Teams).value, std::string("Free for all"));            // (Red and Blue are bots: still a choice)
        r.click(MenuId::Seat2);
        r.click(MenuId::Seat2);
        r.click(MenuId::Seat2);                                                            // Blue: Empty: one bot left
        ASSERT_FALSE(r.exists(MenuId::Teams));
        r.click(MenuId::Continue);
        request = r.take();
        ASSERT_TRUE(request.bots.size() == 1 && request.teams == LocalTeams{});
    } TEST_END();

    TEST_CASE("M2.5 Single player: the Teams choice is the player's seat with a bot, whatever the seat is (own seat blue: You + Green, You + Red), a team that is stored for seats that have no bots is no choice (free for all, and the cycler goes on from there), the words of the cycler, and the store keeps the choice under `teams`") {
        {
            Rig r(2);
            r.to_panel(MenuId::Single);
            r.click(MenuId::Seat0);                                                        // Green
            r.click(MenuId::Seat3);                                                        // Black
            ASSERT_TRUE(r.exists(MenuId::Teams));
            r.click(MenuId::Teams);
            ASSERT_EQ(r.element(MenuId::Teams).value, std::string("You + Green"));
            ASSERT_TRUE(r.menu.teams() == LocalTeams({true, 2, 0}));
            r.click(MenuId::Teams);
            ASSERT_EQ(r.element(MenuId::Teams).value, std::string("You + Black"));
            ASSERT_TRUE(r.menu.teams() == LocalTeams({true, 2, 3}));
            r.click(MenuId::Continue);
            const MenuRequest request = r.take();
            ASSERT_TRUE(request.teams == LocalTeams({true, 2, 3}) && request.bots.size() == 2 && request.bots[0].seat == 0 && request.bots[1].seat == 3);
        }
        ASSERT_EQ(StartMenu::teams_text(LocalTeams{}, 0), std::string("Free for all"));
        ASSERT_EQ(StartMenu::teams_text(LocalTeams({true, 0, 1}), 0), std::string("You + Red"));
        ASSERT_EQ(StartMenu::teams_text(LocalTeams({true, 0, 2}), 0), std::string("You + Blue"));
        ASSERT_EQ(StartMenu::teams_text(LocalTeams({true, 0, 3}), 0), std::string("You + Black"));
        ASSERT_EQ(StartMenu::teams_text(LocalTeams({true, 2, 0}), 2), std::string("You + Green"));
        {   // a stored team whose seat has no bot is no choice: the panel says free for all and the first click goes to the first choice (not to the one after the stored one)
            Rig r;
            MenuSettings s;
            s.name = "Player";
            s.seats = {SeatChoice::Empty, SeatChoice::Empty, SeatChoice::Hard, SeatChoice::Hard};
            s.teams = LocalTeams({true, 0, 1});
            r.menu.set_settings(s);
            ASSERT_TRUE(r.menu.teams() == LocalTeams{});
            r.to_panel(MenuId::Single);
            ASSERT_EQ(r.element(MenuId::Teams).value, std::string("Free for all"));
            r.click(MenuId::Teams);
            ASSERT_EQ(r.element(MenuId::Teams).value, std::string("You + Blue"));
            // and the same for a team of a seat that is not the player's partner at all (not 0 + N for the player at seat 0)
            s.teams = LocalTeams({true, 2, 3});
            r.menu.set_settings(s);
            ASSERT_TRUE(r.menu.teams() == LocalTeams{});
        }
        {   // the store: written under `teams` as ffa or A+B, read back; anything else is free for all
            TempDir temp;
            {
                ConfigStore store;
                store.set_location(temp.file("settings.ini"));
                MenuSettings s;
                s.write(store, MenuSetting::Teams);
                ASSERT_EQ(store.get_string("teams", "?", 99), std::string("ffa"));
                s.teams = LocalTeams({true, 0, 3});
                s.write(store, MenuSetting::Teams);
                ASSERT_EQ(store.get_string("teams", "?", 99), std::string("0+3"));
                ASSERT_FALSE(store.has("bots") || store.has("name"));                      // only that one
            }
            ConfigStore later;
            later.set_location(temp.file("settings.ini"));
            ASSERT_TRUE(later.load());
            MenuSettings back;
            back.load(later);
            ASSERT_TRUE(back.teams == LocalTeams({true, 0, 3}));
            for (const char* junk : {"teams=\n", "teams=0+0\n", "teams=0+9\n", "teams=red\n", "teams=0+1+2\n", "teams=ffa\n", ""}) {
                ConfigStore bad;
                bad.parse(junk);
                MenuSettings d;
                d.teams = LocalTeams({true, 1, 2});                                         // (what load() does not read it must not keep)
                d.load(bad);
                ASSERT_TRUE(d.teams == LocalTeams{});
            }
            ConfigStore padded;
            padded.parse("teams= 0+2 \n");
            MenuSettings p;
            p.load(padded);
            ASSERT_TRUE(p.teams == LocalTeams({true, 0, 2}));
        }
        {   // the menu writes a change of the Teams choice at once and only that one
            TempDir temp;
            ConfigStore store;
            store.set_location(temp.file("settings.ini"));
            Rig r;
            r.menu.set_on_change([&](MenuSetting which) { r.menu.settings().write(store, which); });
            r.to_panel(MenuId::Single);
            r.click(MenuId::Seat1);
            r.click(MenuId::Seat2);
            ASSERT_FALSE(store.has("teams"));                                              // (a seat is `bots`)
            r.click(MenuId::Teams);
            ASSERT_EQ(store.get_string("teams", "?", 99), std::string("0+1"));
            r.click(MenuId::Seat1);
            r.click(MenuId::Seat1);
            r.click(MenuId::Seat1);                                                        // Red: Empty: the team is gone, and so is the stored word
            ASSERT_EQ(store.get_string("teams", "?", 99), std::string("ffa"));
            ASSERT_EQ(store.get_string("bots", "?", 99), std::string("off,off,easy,off"));   // (the seats by colour: green is the player's, red is empty again, blue is Easy)
        }
    } TEST_END();

    TEST_CASE("M2.6 Single player: the player's name is a field of the panel, the same text as Join's and Host's (one remembered name), proposed as the settings have it, focused by the keyboard (Up from the first row: its text is selected, so typing replaces it) or by a click (nothing selected), edited like the others (printable ASCII, 32 at most), Enter in it and Continue ask for the game with the cleaned name, a name that Join would refuse is refused on the field with the same words (shown in the rule's place until the next key, with the can't-go cue, asking for nothing), and the name is written when the game is asked for") {
        TempDir temp;
        ConfigStore store;
        store.set_location(temp.file("settings.ini"));
        Rig r(0, "Maya");
        r.menu.set_on_change([&](MenuSetting which) {
            r.changes.push_back(which);
            r.menu.settings().write(store, which);
        });
        r.to_panel(MenuId::Single);
        ASSERT_TRUE(r.exists(MenuId::SingleName));
        ASSERT_TRUE(r.element(MenuId::SingleName).kind == MenuKind::Field);
        ASSERT_EQ(r.element(MenuId::SingleName).text, std::string("Maya"));                // the name that the settings have (the owner puts --name there too)
        ASSERT_TRUE(has_text(r.menu.elements(), "Your name"));
        ASSERT_EQ(r.menu.selected(), MenuId::Seat1);                                       // arriving: the first row, as it was (never a field that takes typing, never a button that acts)
        // by the keyboard: Up from the first row selects the field and its text
        r.key(SDLK_UP);
        ASSERT_EQ(r.menu.selected(), MenuId::SingleName);
        ASSERT_TRUE(r.element(MenuId::SingleName).all_selected);
        r.type("Zed");
        ASSERT_EQ(r.menu.name(), std::string("Zed"));                                      // typing replaced "Maya"
        ASSERT_FALSE(r.element(MenuId::SingleName).all_selected);
        ASSERT_EQ(r.element(MenuId::SingleName).text, std::string("Zed"));
        r.type(std::string(60, 'x'));
        ASSERT_EQ(r.menu.name().size(), StartMenu::kNameMax);                              // the limit of the room's protocol, as on the other panels
        r.key(SDLK_BACKSPACE);
        ASSERT_EQ(r.menu.name().size(), StartMenu::kNameMax - 1);
        r.type("\x01\xc3\xa9");                                                            // a control character and an accent are not typed ...
        ASSERT_EQ(r.menu.name().size(), StartMenu::kNameMax - 1);
        ASSERT_TRUE(has_text(r.menu.elements(), StartMenu::kRefusedCharsText));            // ... and the panel says why (in the rule's place)
        r.type("y");                                                                       // the next key clears the line
        ASSERT_FALSE(has_text(r.menu.elements(), StartMenu::kRefusedCharsText));
        ASSERT_TRUE(has_text(r.menu.elements(), StartMenu::kNoBotsLine));
        ASSERT_FALSE(store.has("name"));                                                   // (nothing is written at every key)
        r.key(SDLK_DOWN);                                                                  // the field is left: now it is
        ASSERT_EQ(r.menu.selected(), MenuId::Seat1);
        ASSERT_EQ(store.get_string("name", "", 99), "Zed" + std::string(28, 'x') + "y");
        ASSERT_TRUE(r.changes.back() == MenuSetting::Name);
        // one name for the three panels
        r.key(SDLK_ESCAPE);
        r.to_panel(MenuId::JoinWithCode);
        ASSERT_EQ(r.menu.name(), "Zed" + std::string(28, 'x') + "y");
        ASSERT_EQ(r.element(MenuId::Name).text, r.menu.name());
        r.key(SDLK_UP);
        r.type("Ruth");
        r.key(SDLK_ESCAPE);
        r.click(MenuId::Single);
        ASSERT_EQ(r.element(MenuId::SingleName).text, std::string("Ruth"));                // what was typed on Join is the single player's name
        r.to_panel(MenuId::HostOnline);
        ASSERT_EQ(r.element(MenuId::HostName).text, std::string("Ruth"));
        // by the mouse: a click gives the field the focus and selects nothing (typing goes on at the end); the pointer over it selects nothing
        r.to_panel(MenuId::Single);
        r.mouse_move(MenuId::SingleName);
        ASSERT_EQ(r.menu.selected(), MenuId::Seat1);
        r.click(MenuId::SingleName);
        ASSERT_EQ(r.menu.selected(), MenuId::SingleName);
        ASSERT_FALSE(r.element(MenuId::SingleName).all_selected);
        r.type("!");
        ASSERT_EQ(r.menu.name(), std::string("Ruth!"));
        // Tab order: the name, then the rows, Continue, Back (and round)
        r.key(SDLK_TAB);
        ASSERT_EQ(r.menu.selected(), MenuId::Seat1);
        r.key(SDLK_TAB, KMOD_SHIFT);
        ASSERT_EQ(r.menu.selected(), MenuId::SingleName);
        // Enter in the field asks for the game: with the cleaned name (the blanks at both ends go), the bots and the teams of the panel; and it is written
        r.key(SDLK_a, KMOD_CTRL);                                                          // (select the text: the next typing replaces it)
        r.type("  Dave  ");
        r.key(SDLK_RETURN);
        MenuRequest request = r.take();
        ASSERT_TRUE(request.type == MenuRequest::Type::Single && request.bots.empty() && request.teams == LocalTeams{});
        ASSERT_EQ(request.name, std::string("Dave"));
        ASSERT_EQ(store.get_string("name", "", 99), std::string("  Dave  "));              // (the field's text, as the other panels write it; the settings clean it when they are read)
        // Continue asks as well, with the same name; a second ask with nothing new writes nothing new
        const size_t writes = r.changes.size();
        r.click(MenuId::Seat1);
        r.click(MenuId::Seat3);
        r.changes.clear();
        r.click(MenuId::Continue);
        request = r.take();
        ASSERT_TRUE(request.type == MenuRequest::Type::Single && request.bots.size() == 2 && request.name == "Dave");
        ASSERT_TRUE(std::find(r.changes.begin(), r.changes.end(), MenuSetting::Name) == r.changes.end());
        (void)writes;
        // a name that Join would refuse is refused here with the same words, on the field, with the cue, and nothing is asked
        for (const auto& refused : std::vector<std::pair<std::string, std::string>>{{"", "Type your name first."},
                                                                                  {"   ", "Type your name first."},
                                                                                  {"Bot (Hard)", "A name that starts with \"Bot (\" is for computer players. Please choose another name."},
                                                                                  {" bot(x", "A name that starts with \"Bot (\" is for computer players. Please choose another name."}}) {
            Rig b(0, "Player");
            b.to_panel(MenuId::Single);
            b.key(SDLK_UP);
            b.key(SDLK_BACKSPACE);                                                         // (the selected text goes)
            b.type(refused.first);
            const int cues = b.sounds.count(sim::SoundID::CantGo);
            b.click(MenuId::Continue);
            ASSERT_FALSE(b.menu.has_request());
            ASSERT_EQ(b.menu.panel(), MenuPanel::Single);
            ASSERT_EQ(b.menu.message(), refused.second);
            ASSERT_TRUE(has_text(b.menu.elements(), refused.second));
            ASSERT_EQ(b.menu.selected(), MenuId::SingleName);
            ASSERT_EQ(b.sounds.count(sim::SoundID::CantGo), cues + 1);
            std::string clean;
            std::string why;
            ASSERT_FALSE(check_player_name(refused.first, clean, why));                    // the Join panel's rule says the same
            ASSERT_EQ(why, refused.second);
            b.key(SDLK_RETURN);                                                            // Enter in the field is the same ask, refused the same
            ASSERT_FALSE(b.menu.has_request());
            ASSERT_EQ(b.sounds.count(sim::SoundID::CantGo), cues + 2);
            b.type("Bob");                                                                 // the next key takes the line away, and a good name goes through
            ASSERT_TRUE(b.menu.message().empty());
            b.click(MenuId::Continue);
            const MenuRequest ok = b.take();
            ASSERT_TRUE(ok.type == MenuRequest::Type::Single && ok.name == "Bob");
        }
        // Esc leaves the panel and writes what was typed (a name that is not asked for yet is still kept)
        Rig e(0, "Player");
        e.menu.set_on_change([&](MenuSetting which) { e.menu.settings().write(store, which); });
        e.to_panel(MenuId::Single);
        e.key(SDLK_UP);
        e.type("Esme");
        e.key(SDLK_ESCAPE);
        ASSERT_EQ(e.menu.panel(), MenuPanel::Main);
        ASSERT_EQ(store.get_string("name", "", 99), std::string("Esme"));
    } TEST_END();

    TEST_CASE("M3.1 Join: the name and the code fields take printable characters up to 32 and Backspace; Up, Down and Tab move between the fields and the buttons; the keyboard selects the text of a field (the next typing replaces it), a click does not") {
        Rig r(0, "Player");
        r.to_panel(MenuId::JoinWithCode);
        ASSERT_EQ(r.menu.panel(), MenuPanel::Join);
        ASSERT_EQ(r.menu.selected(), MenuId::Code);                                        // the name is there already: the code is next
        r.type("abc");
        ASSERT_EQ(r.menu.code(), std::string("abc"));
        r.key(SDLK_BACKSPACE);
        ASSERT_EQ(r.menu.code(), std::string("ab"));
        r.type(std::string(60, 'x'));
        ASSERT_EQ(r.menu.code().size(), static_cast<size_t>(32));                          // cut at the room code's limit
        ASSERT_EQ(r.menu.code().substr(0, 2), std::string("ab"));
        r.key(SDLK_UP);                                                                    // to the name: its text is selected
        ASSERT_EQ(r.menu.selected(), MenuId::Name);
        ASSERT_TRUE(r.element(MenuId::Name).all_selected);
        r.type("Dave");
        ASSERT_EQ(r.menu.name(), std::string("Dave"));                                     // typing replaced "Player"
        ASSERT_FALSE(r.element(MenuId::Name).all_selected);
        r.type(std::string(60, 'y'));
        ASSERT_EQ(r.menu.name().size(), StartMenu::kNameMax);                              // the name's limit is what a Hello carries
        r.key(SDLK_BACKSPACE);
        ASSERT_EQ(r.menu.name().size(), StartMenu::kNameMax - 1);
        r.type("\x01\x7f\n\t\xc3\xa9é");                                                  // control characters and anything that is not ASCII are not typed
        ASSERT_EQ(r.menu.name().size(), StartMenu::kNameMax - 1);                          // (none of those got in)
        r.key(SDLK_TAB);
        ASSERT_EQ(r.menu.selected(), MenuId::Code);
        ASSERT_FALSE(r.element(MenuId::Code).all_selected && r.menu.code().empty());
        r.key(SDLK_DOWN);
        ASSERT_EQ(r.menu.selected(), MenuId::Join);
        r.type("zzz");                                                                     // text with no field has nowhere to go
        ASSERT_EQ(r.menu.code().substr(0, 2), std::string("ab"));
        r.key(SDLK_DOWN);
        ASSERT_EQ(r.menu.selected(), MenuId::Back);
        r.key(SDLK_DOWN);
        ASSERT_EQ(r.menu.selected(), MenuId::Name);                                        // wraps
        // a click gives the focus without selecting the text
        r.menu.show_main();
        r.click(MenuId::JoinWithCode);
        r.click(MenuId::Name);
        ASSERT_EQ(r.menu.selected(), MenuId::Name);
        ASSERT_FALSE(r.element(MenuId::Name).all_selected);
        r.type("!");
        ASSERT_EQ(r.menu.name(), std::string("Dave" + std::string(27, 'y') + "!"));        // appended at the end (31 characters were there)
    } TEST_END();

    TEST_CASE("M3.2 Join: Ctrl+V and Cmd+V paste the clipboard into the field that has the focus (line ends dropped, the code trimmed, cut to the field's length, never case-changed); Ctrl+A selects, Ctrl+Backspace clears; pasting where there is no field does nothing") {
        Rig r;
        r.to_panel(MenuId::JoinWithCode);
        r.clipboard.text = "  Demo-Small-X7K2\r\n";
        r.key(SDLK_v, KMOD_GUI);
        ASSERT_EQ(r.menu.code(), std::string("Demo-Small-X7K2"));                          // trimmed, and the case is the server's: never changed
        r.key(SDLK_a, KMOD_CTRL);                                                          // Ctrl+A: select all
        ASSERT_TRUE(r.element(MenuId::Code).all_selected);
        r.clipboard.text = "second";
        r.key(SDLK_v, KMOD_CTRL);                                                          // pasting replaces the selection
        ASSERT_EQ(r.menu.code(), std::string("second"));
        r.key(SDLK_v, KMOD_CTRL);                                                          // and appends when nothing is selected
        ASSERT_EQ(r.menu.code(), std::string("secondsecond"));
        r.clipboard.text = std::string(80, 'q');
        r.key(SDLK_v, KMOD_GUI);
        ASSERT_EQ(r.menu.code().size(), static_cast<size_t>(32));
        r.key(SDLK_BACKSPACE, KMOD_CTRL);                                                  // Ctrl+Backspace: clear the field
        ASSERT_TRUE(r.menu.code().empty());
        r.key(SDLK_UP);                                                                    // the name: pasted text keeps its inner blanks
        r.clipboard.text = "Mary Ann\n";
        r.key(SDLK_v, KMOD_GUI);
        ASSERT_EQ(r.menu.name(), std::string("Mary Ann"));
        r.key(SDLK_DOWN);
        r.key(SDLK_DOWN);                                                                  // on Join: no field
        ASSERT_EQ(r.menu.selected(), MenuId::Join);
        r.clipboard.text = "nowhere";
        r.key(SDLK_v, KMOD_GUI);
        ASSERT_TRUE(r.menu.code().empty() && r.menu.name() == "Mary Ann");
        // no clipboard at all: the key does nothing
        StartMenu bare;
        bare.set_settings(MenuSettings{});
        bare.show_main();
        bare.on_key(SDLK_DOWN, 0);
        bare.on_key(SDLK_RETURN, 0);
        bare.on_key(SDLK_v, KMOD_GUI);
        ASSERT_TRUE(bare.code().empty());
    } TEST_END();

    TEST_CASE("M3.3 Join: a name that looks like a computer player's is refused on the panel with the reason, whatever its blanks and case; so are an empty name and a bad or empty code; nothing is asked of the application, the panel stays, the can't-go cue plays") {
        const std::vector<std::string> bot_names = {"Bot (Hard)", "bot (x)", "BOT (Easy)", " Bot (x)", "B o t (x)", "bot(x)", "Bot (", "  bOt(  "};
        for (const std::string& name : bot_names) {
            Rig r;
            r.to_panel(MenuId::JoinWithCode);
            r.key(SDLK_UP);
            r.type(name);
            r.key(SDLK_TAB);
            r.type("abc-1");
            r.key(SDLK_RETURN);                                                            // Enter on the code field: Join
            ASSERT_FALSE(r.menu.has_request());
            ASSERT_EQ(r.menu.panel(), MenuPanel::Join);
            ASSERT_TRUE(r.menu.message().find("Bot (") != std::string::npos);
            ASSERT_EQ(r.menu.selected(), MenuId::Name);                                    // the focus goes to the name that is wrong
            ASSERT_EQ(r.sounds.count(sim::SoundID::CantGo), 1);
            ASSERT_TRUE(has_text(r.menu.elements(), "computer players"));
            r.type("Dave");                                                                // typing the better name clears the message
            ASSERT_TRUE(r.menu.message().empty());
        }
        const std::vector<std::string> fine = {"Robot", "Bobby", "bo t", "Bot", "Bot Smith", "bot-9", "Abot (x)"};
        for (const std::string& name : fine) {
            Rig r;
            r.to_panel(MenuId::JoinWithCode);
            r.key(SDLK_UP);
            r.type(name);
            r.key(SDLK_TAB);
            r.type("abc-1");
            r.key(SDLK_RETURN);
            const MenuRequest request = r.take();
            ASSERT_TRUE(request.type == MenuRequest::Type::Join);
            ASSERT_EQ(request.name, name);
        }
        Rig empty;
        empty.to_panel(MenuId::JoinWithCode);
        empty.key(SDLK_UP);
        empty.key(SDLK_BACKSPACE);                                                         // the selected "Player" is deleted
        ASSERT_TRUE(empty.menu.name().empty());
        empty.key(SDLK_TAB);
        empty.type("abc");
        empty.key(SDLK_RETURN);
        ASSERT_FALSE(empty.menu.has_request());
        ASSERT_EQ(empty.menu.panel(), MenuPanel::Join);
        ASSERT_TRUE(has_text(empty.menu.elements(), "name"));
        Rig blank;
        blank.to_panel(MenuId::JoinWithCode);
        blank.key(SDLK_UP);
        blank.type("   ");
        blank.key(SDLK_TAB);
        blank.type("abc");
        blank.key(SDLK_RETURN);
        ASSERT_FALSE(blank.menu.has_request());                                            // a name of blanks is no name
        // the code
        const std::vector<std::string> bad_codes = {"", "  ", "has space", "bad!", "a/b", "\xc3\xbc\xc3\xaf", "dot.dot", "q?"};
        for (const std::string& code : bad_codes) {
            Rig r;
            r.to_panel(MenuId::JoinWithCode);
            r.type(code);
            r.key(SDLK_RETURN);
            if (r.menu.has_request()) std::cout << "\n    the code '" << code << "' was accepted as '" << r.menu.take_request().room << "'";
            ASSERT_FALSE(r.menu.has_request());
            ASSERT_EQ(r.menu.panel(), MenuPanel::Join);
            ASSERT_FALSE(r.menu.message().empty());
            ASSERT_EQ(r.menu.selected(), MenuId::Code);
            ASSERT_EQ(r.sounds.count(sim::SoundID::CantGo), 1);
        }
    } TEST_END();

    TEST_CASE("M3.4 Join: a good name and code ask the application to join (the name and the code cleaned, the code's case kept) and the panel shows 'Connecting to <server>...'; a failure comes back to the panel with its line, a cancel without") {
        Rig r;
        r.menu.set_server(ServerAddress{"play.example.org", 4010});
        r.to_panel(MenuId::JoinWithCode);
        r.key(SDLK_UP);
        r.type("  Dave Smith ");
        r.key(SDLK_TAB);
        r.type("Demo-Small-X7K2");
        r.click(MenuId::Join);
        const MenuRequest request = r.take();
        ASSERT_TRUE(request.type == MenuRequest::Type::Join);
        ASSERT_EQ(request.name, std::string("Dave Smith"));
        ASSERT_EQ(request.room, std::string("Demo-Small-X7K2"));
        ASSERT_EQ(r.menu.panel(), MenuPanel::Connecting);
        ASSERT_TRUE(has_text(r.menu.elements(), "Connecting to play.example.org:4010..."));
        ASSERT_EQ(r.menu.selected(), MenuId::None);                                        // nothing is preselected here: an Enter that comes twice must not cancel the attempt (M2)
        ASSERT_EQ(r.menu.connect_origin(), MenuPanel::Join);
        r.key(SDLK_TAB);
        ASSERT_EQ(r.menu.selected(), MenuId::Cancel);                                      // Tab (or the pointer) reaches the button
        // Esc and the Cancel button both ask to cancel (the application cancels, then says so)
        r.key(SDLK_ESCAPE);
        ASSERT_TRUE(r.take().type == MenuRequest::Type::Cancel);
        r.click(MenuId::Cancel);
        ASSERT_TRUE(r.take().type == MenuRequest::Type::Cancel);
        r.menu.connection_cancelled();
        ASSERT_EQ(r.menu.panel(), MenuPanel::Join);
        ASSERT_TRUE(r.menu.message().empty());
        ASSERT_EQ(r.menu.selected(), MenuId::Join);                                        // the button to try again is lit, not a field
        ASSERT_EQ(r.menu.name(), std::string("  Dave Smith "));                            // the fields are as the player left them
        ASSERT_EQ(r.menu.code(), std::string("Demo-Small-X7K2"));
        // a failure
        r.key(SDLK_RETURN);
        r.take();
        r.menu.connection_failed("There is no room with that code.");
        ASSERT_EQ(r.menu.panel(), MenuPanel::Join);
        ASSERT_EQ(r.menu.message(), std::string("There is no room with that code."));
        ASSERT_TRUE(has_text(r.menu.elements(), "There is no room with that code."));
        ASSERT_EQ(r.menu.selected(), MenuId::Join);
        r.key(SDLK_ESCAPE);                                                                // Back: the first panel, the error is gone (it is not carried to the first panel as a notice)
        ASSERT_EQ(r.menu.panel(), MenuPanel::Main);
        ASSERT_TRUE(r.menu.message().empty());
        ASSERT_FALSE(has_text(r.menu.elements(), "There is no room with that code."));
        r.click(MenuId::JoinWithCode);
        ASSERT_TRUE(r.menu.message().empty());
        // Enter in the name field goes on to the code field and joins nothing; Enter in the code field joins
        Rig e;
        e.to_panel(MenuId::JoinWithCode);
        e.key(SDLK_UP);
        e.type("Dave");
        e.key(SDLK_TAB);
        e.type("demo-1");
        e.key(SDLK_UP);                                                                    // back to the name, with a code written already
        ASSERT_EQ(e.menu.selected(), MenuId::Name);
        e.key(SDLK_RETURN);
        ASSERT_EQ(e.menu.selected(), MenuId::Code);
        ASSERT_FALSE(e.menu.has_request());
        ASSERT_EQ(e.menu.panel(), MenuPanel::Join);
        e.key(SDLK_RETURN);
        const MenuRequest joined = e.take();
        ASSERT_TRUE(joined.type == MenuRequest::Type::Join && joined.room == "demo-1" && joined.name == "Dave");
    } TEST_END();

    TEST_CASE("M4.1 Host: the map goes round the six maps of the original game in the page's order and the players round 2, 3, 4 (Left, Right, Enter, click); the request carries the map, the players and the cleaned name") {
        Rig r;
        r.to_panel(MenuId::HostOnline);
        ASSERT_EQ(r.menu.panel(), MenuPanel::Host);
        ASSERT_EQ(r.menu.selected(), MenuId::HostMap);                                     // the first input is selected on arrival, not the Host button (M2)
        const std::vector<std::string> maps = {"Tiny", "Small", "Medium", "Gauntlet", "Treasure", "Islands"};
        ASSERT_EQ(r.element(MenuId::HostMap).value, std::string("Treasure"));              // the panel opens on the default map (M4.4); the walk below starts from the first of the list
        r.key(SDLK_RIGHT);                                                                 // Treasure -> Islands
        r.key(SDLK_RIGHT);                                                                 // Islands -> Tiny: the list goes round
        for (int lap = 0; lap < 2; ++lap) {
            for (size_t i = 0; i < maps.size(); ++i) {
                ASSERT_EQ(r.element(MenuId::HostMap).value, maps[i]);
                r.key(SDLK_RIGHT);
            }
        }
        r.key(SDLK_LEFT);
        ASSERT_EQ(r.element(MenuId::HostMap).value, std::string("Islands"));               // Left from the first goes to the last
        r.key(SDLK_RETURN);
        ASSERT_EQ(r.element(MenuId::HostMap).value, std::string("Tiny"));
        r.key(SDLK_DOWN);
        ASSERT_EQ(r.menu.selected(), MenuId::HostPlayers);
        ASSERT_EQ(r.element(MenuId::HostPlayers).value, std::string("4 players"));         // the page's default
        r.key(SDLK_RIGHT);
        ASSERT_EQ(r.element(MenuId::HostPlayers).value, std::string("2 players"));
        r.click(MenuId::HostPlayers);
        ASSERT_EQ(r.element(MenuId::HostPlayers).value, std::string("3 players"));
        r.key(SDLK_LEFT);
        r.key(SDLK_LEFT);
        ASSERT_EQ(r.element(MenuId::HostPlayers).value, std::string("4 players"));
        r.key(SDLK_LEFT);
        ASSERT_EQ(r.element(MenuId::HostPlayers).value, std::string("3 players"));
        r.key(SDLK_DOWN);                                                                  // the Red seat's bot at START (protocol 13: M4.3)
        ASSERT_EQ(r.menu.selected(), MenuId::HostSeat1);
        ASSERT_EQ(r.element(MenuId::HostSeat1).value, std::string("Leave empty"));
        r.key(SDLK_DOWN);                                                                  // the Blue seat (a room of three has it)
        ASSERT_EQ(r.menu.selected(), MenuId::HostSeat2);
        r.key(SDLK_DOWN);                                                                  // the teams (three players have the choice: M4.5)
        ASSERT_EQ(r.menu.selected(), MenuId::HostTeams);
        r.key(SDLK_DOWN);                                                                  // the name
        ASSERT_EQ(r.menu.selected(), MenuId::HostName);
        ASSERT_TRUE(r.element(MenuId::HostName).all_selected);
        r.type(" Maya ");
        r.key(SDLK_RIGHT);                                                                 // no cycler here: nothing changes
        ASSERT_EQ(r.element(MenuId::HostMap).value, std::string("Tiny"));
        r.key(SDLK_DOWN);
        r.key(SDLK_RETURN);
        const MenuRequest request = r.take();
        ASSERT_TRUE(request.type == MenuRequest::Type::Host);
        ASSERT_TRUE(request.map == 0 && request.players == 3 && request.name == "Maya" && request.fill == net::FillLevel::None && !request.teams.set);
        ASSERT_EQ(r.menu.panel(), MenuPanel::Connecting);
        ASSERT_EQ(r.menu.connect_origin(), MenuPanel::Host);
        r.menu.connection_failed("The server is busy.");
        ASSERT_EQ(r.menu.panel(), MenuPanel::Host);                                        // back where it came from
        ASSERT_TRUE(has_text(r.menu.elements(), "The server is busy."));
        ASSERT_EQ(r.element(MenuId::HostMap).value, std::string("Tiny"));                  // the choice is kept
        // a name that looks like a bot's is refused here too
        r.key(SDLK_UP);
        r.type("Bot (Easy)");
        r.key(SDLK_RETURN);                                                                // Enter in the name field hosts
        ASSERT_FALSE(r.menu.has_request());
        ASSERT_EQ(r.menu.panel(), MenuPanel::Host);
        ASSERT_TRUE(has_text(r.menu.elements(), "Bot ("));
        // the changes of the map and the players are reported for the settings file
        ASSERT_TRUE(std::count(r.changes.begin(), r.changes.end(), MenuSetting::HostMap) >= 13);
        ASSERT_TRUE(std::count(r.changes.begin(), r.changes.end(), MenuSetting::HostPlayers) >= 5);
        ASSERT_TRUE(std::count(r.changes.begin(), r.changes.end(), MenuSetting::Name) >= 1);
    } TEST_END();

    TEST_CASE("M4.2 Host: the room's panel shows the code in large letters and the players that are in; Copy puts the code on the clipboard (the call is checked) and says Copied! for two seconds; Ctrl+C / Cmd+C copies too; a clipboard that fails says so and shows the code to write down") {
        Rig r;
        r.menu.show_room("demo-small-3p-abc234", 1, 3);
        ASSERT_EQ(r.menu.panel(), MenuPanel::Room);
        ASSERT_EQ(element_text(r.menu.elements(), MenuKind::Code), std::string("demo-small-3p-abc234"));
        ASSERT_TRUE(has_text(r.menu.elements(), "Your room:"));
        ASSERT_TRUE(has_text(r.menu.elements(), "Players in the room: 1 of 3"));
        r.menu.set_room_players(2, 3);
        ASSERT_TRUE(has_text(r.menu.elements(), "Players in the room: 2 of 3"));
        ASSERT_EQ(r.menu.selected(), MenuId::EnterRoom);
        const std::vector<MenuId> expected = {MenuId::Copy, MenuId::EnterRoom, MenuId::Back};
        ASSERT_TRUE(control_ids(r.menu) == expected);
        ASSERT_EQ(r.element(MenuId::Copy).text, std::string("Copy"));
        r.click(MenuId::Copy);
        ASSERT_TRUE(r.clipboard.writes.size() == 1 && r.clipboard.writes[0] == "demo-small-3p-abc234");
        ASSERT_EQ(r.element(MenuId::Copy).text, std::string("Copied!"));
        r.menu.update(1.0f);
        ASSERT_EQ(r.element(MenuId::Copy).text, std::string("Copied!"));
        r.menu.update(1.5f);
        ASSERT_EQ(r.element(MenuId::Copy).text, std::string("Copy"));                      // two seconds later: the word is back
        r.key(SDLK_c, KMOD_CTRL);
        ASSERT_EQ(r.clipboard.writes.size(), static_cast<size_t>(2));
        r.key(SDLK_c, KMOD_GUI);
        ASSERT_EQ(r.clipboard.writes.size(), static_cast<size_t>(3));
        ASSERT_FALSE(r.menu.has_request());
        // Enter continues to the room, Esc and Back leave it
        r.key(SDLK_DOWN);                                                                  // (the clicks above moved the selection to Copy)
        ASSERT_EQ(r.menu.selected(), MenuId::EnterRoom);
        r.key(SDLK_RETURN);
        ASSERT_TRUE(r.take().type == MenuRequest::Type::EnterRoom);
        r.key(SDLK_ESCAPE);
        ASSERT_TRUE(r.take().type == MenuRequest::Type::LeaveRoom);
        r.click(MenuId::Back);
        ASSERT_TRUE(r.take().type == MenuRequest::Type::LeaveRoom);
        // the room is gone: back to the Host panel with the reason
        r.menu.room_left("The connection to the server was lost.");
        ASSERT_EQ(r.menu.panel(), MenuPanel::Host);
        ASSERT_TRUE(has_text(r.menu.elements(), "The connection to the server was lost."));
        // a clipboard that cannot be written
        Rig bad;
        bad.clipboard.writable = false;
        bad.menu.show_room("demo-tiny-2p-qqqqqq", 1, 2);
        bad.click(MenuId::Copy);
        ASSERT_EQ(bad.clipboard.writes.size(), static_cast<size_t>(1));
        ASSERT_FALSE(bad.menu.copied());
        ASSERT_TRUE(has_text(bad.menu.elements(), "Copy failed") && element_text(bad.menu.elements(), MenuKind::Code) == "demo-tiny-2p-qqqqqq");   // (the code stands in full in its box)
        bad.clipboard.writable = true;                                                      // the clipboard comes back: the failure line goes with the next copy that works
        bad.click(MenuId::Copy);
        ASSERT_TRUE(bad.menu.copied());
        ASSERT_FALSE(has_text(bad.menu.elements(), "Copy failed"));
        bad.menu.show_room("demo-tiny-2p-rrrrrr", 1, 2);                                    // a new room: "Copied!" of the old one is not carried over
        ASSERT_FALSE(bad.menu.copied());
        ASSERT_EQ(bad.element(MenuId::Copy).text, std::string("Copy"));
        // no clipboard at all
        StartMenu none;
        none.set_settings(MenuSettings{});
        none.show_room("code", 1, 2);
        none.on_key(SDLK_c, KMOD_GUI);
        ASSERT_FALSE(none.copied());
    } TEST_END();

    TEST_CASE("M4.3 Host: a row for each seat after the leader's (Red, Blue and Black, as many as the room has) goes round Leave empty, Easy bot, Medium bot, Hard bot (Left, Right, Enter, click), each on its own, and is remembered; the request of Host carries the levels of the seats that the room has; the room's panel says what START will do; every control of both panels lies inside the screen and no two overlap") {
        using L = net::FillLevel;
        // the words of a seat's choice
        ASSERT_EQ(std::string(fill_seat_text(L::None)), std::string("Leave empty"));
        ASSERT_EQ(std::string(fill_seat_text(L::Easy)), std::string("Easy bot"));
        ASSERT_EQ(std::string(fill_seat_text(L::Medium)), std::string("Medium bot"));
        ASSERT_EQ(std::string(fill_seat_text(L::Hard)), std::string("Hard bot"));
        // what the room's panel says START will do: the same level in every seat of the room keeps the one sentence of protocol 11, any other plan names its seats (the short form when the long one is
        // longer than the line), the seats beyond the room and the leader's own seat are nobody's business
        ASSERT_EQ(fill_choice_sentence(L::Medium), std::string("Empty seats will be Medium bots."));
        ASSERT_EQ(fill_choice_sentence(L::Easy), std::string("Empty seats will be Easy bots."));
        ASSERT_EQ(fill_choice_sentence(L::Hard), std::string("Empty seats will be Hard bots."));
        ASSERT_EQ(fill_choice_sentence(L::None), std::string("Empty seats stay empty."));
        const auto plan = [](L red, L blue, L black) {
            net::FillPlan p;
            p.level[1] = red;
            p.level[2] = blue;
            p.level[3] = black;
            return p;
        };
        ASSERT_EQ(fill_choice_sentence(plan(L::Easy, L::None, L::Hard)), std::string("At START: Red gets an Easy bot, Black a Hard bot."));
        ASSERT_EQ(fill_choice_sentence(plan(L::None, L::Medium, L::None)), std::string("At START: Blue gets a Medium bot."));
        ASSERT_EQ(fill_choice_sentence(plan(L::Medium, L::Medium, L::Medium)), std::string("Empty seats will be Medium bots."));
        ASSERT_EQ(fill_choice_sentence(plan(L::Easy, L::Medium, L::Hard)), std::string("At START: Red Easy, Blue Medium, Black Hard."));
        ASSERT_EQ(fill_choice_sentence(plan(L::Hard, L::Easy, L::Easy), 2), std::string("Empty seats will be Hard bots."));            // (a room of two has the Red seat only)
        ASSERT_EQ(fill_choice_sentence(plan(L::None, L::Easy, L::Easy), 2), std::string("Empty seats stay empty."));
        ASSERT_EQ(fill_choice_sentence(plan(L::Easy, L::Hard, L::Hard), 3), std::string("At START: Red gets an Easy bot, Blue a Hard bot."));
        net::FillPlan own;
        own.level[0] = L::Hard;
        ASSERT_EQ(fill_choice_sentence(own), std::string("Empty seats stay empty."));
        for (size_t i = 0; i < 64; ++i) {                                                  // every plan: one line of the room's panel (520 px: the estimate of Recorder is 9 px a character at 18 px)
            const net::FillPlan p = plan(static_cast<L>(i % 4), static_cast<L>((i / 4) % 4), static_cast<L>(i / 16));
            for (int players = 2; players <= 4; ++players) ASSERT_TRUE(fill_choice_sentence(p, players).size() * 9 <= 520);
        }
        Rig r;
        r.to_panel(MenuId::HostOnline);
        // the rows: a label and a cycler for each seat after the leader's, in the order of the keys; the old single choice is gone
        const std::vector<MenuId> four = {MenuId::HostMap, MenuId::HostPlayers, MenuId::HostSeat1, MenuId::HostSeat2, MenuId::HostSeat3, MenuId::HostTeams, MenuId::HostName, MenuId::Host, MenuId::Back};
        ASSERT_TRUE(control_ids(r.menu) == four);
        for (const char* label : {"Red at START", "Blue at START", "Black at START", "Teams", "Map", "Players", "Your name"}) ASSERT_TRUE(has_text(r.menu.elements(), label));
        ASSERT_FALSE(has_text(r.menu.elements(), "Empty seats at START"));
        for (const MenuId id : {MenuId::HostSeat1, MenuId::HostSeat2, MenuId::HostSeat3}) {
            ASSERT_EQ(r.element(id).kind, MenuKind::Cycler);
            ASSERT_EQ(r.element(id).value, std::string("Leave empty"));                    // nothing is seated unless the player chose it
        }
        // a room of three has two seat rows after the leader's and a room of two has one (the Teams row needs three)
        {
            Rig three;
            three.to_panel(MenuId::HostOnline);
            three.click(MenuId::HostPlayers);                                              // 4 -> 2
            const std::vector<MenuId> two_players = {MenuId::HostMap, MenuId::HostPlayers, MenuId::HostSeat1, MenuId::HostName, MenuId::Host, MenuId::Back};
            ASSERT_TRUE(control_ids(three.menu) == two_players);
            ASSERT_TRUE(has_text(three.menu.elements(), "Red at START") && !has_text(three.menu.elements(), "Blue at START") && !has_text(three.menu.elements(), "Black at START"));
            three.click(MenuId::HostPlayers);                                              // 2 -> 3
            const std::vector<MenuId> three_players = {MenuId::HostMap, MenuId::HostPlayers, MenuId::HostSeat1, MenuId::HostSeat2, MenuId::HostTeams, MenuId::HostName, MenuId::Host, MenuId::Back};
            ASSERT_TRUE(control_ids(three.menu) == three_players);
            ASSERT_TRUE(has_text(three.menu.elements(), "Blue at START") && !has_text(three.menu.elements(), "Black at START"));
        }
        r.key(SDLK_DOWN);
        r.key(SDLK_DOWN);
        ASSERT_EQ(r.menu.selected(), MenuId::HostSeat1);
        const char* const words[4] = {"Leave empty", "Easy bot", "Medium bot", "Hard bot"};
        for (int lap = 0; lap < 2; ++lap) {
            for (const char* word : words) {
                ASSERT_EQ(r.element(MenuId::HostSeat1).value, std::string(word));
                r.key(SDLK_RIGHT);
            }
        }
        r.key(SDLK_LEFT);
        ASSERT_EQ(r.element(MenuId::HostSeat1).value, std::string("Hard bot"));            // Left from the first goes to the last
        r.key(SDLK_RETURN);
        ASSERT_EQ(r.element(MenuId::HostSeat1).value, std::string("Leave empty"));         // Enter goes on, round
        r.click(MenuId::HostSeat1);
        ASSERT_EQ(r.element(MenuId::HostSeat1).value, std::string("Easy bot"));
        ASSERT_EQ(r.element(MenuId::HostSeat2).value, std::string("Leave empty"));         // each row is its own
        ASSERT_EQ(r.element(MenuId::HostSeat3).value, std::string("Leave empty"));
        ASSERT_TRUE(std::count(r.changes.begin(), r.changes.end(), MenuSetting::HostFill) >= 11);       // every change is reported for the settings file
        r.key(SDLK_RIGHT);                                                                 // Red: Medium bot
        r.key(SDLK_DOWN);
        ASSERT_EQ(r.menu.selected(), MenuId::HostSeat2);
        for (int i = 0; i < 3; ++i) r.key(SDLK_RIGHT);                                     // Blue: Hard bot
        r.key(SDLK_DOWN);
        ASSERT_EQ(r.menu.selected(), MenuId::HostSeat3);
        r.click(MenuId::HostSeat3);                                                        // Black: Easy bot
        ASSERT_TRUE(r.menu.settings().host_fill.level[0] == L::None && r.menu.settings().host_fill.level[1] == L::Medium && r.menu.settings().host_fill.level[2] == L::Hard &&
                    r.menu.settings().host_fill.level[3] == L::Easy);
        // the request carries the levels of the seats that the room has (never the leader's seat, never a seat beyond the room), and a failed attempt keeps them
        r.key(SDLK_DOWN);                                                                  // the teams
        r.key(SDLK_DOWN);                                                                  // the name
        r.key(SDLK_RETURN);
        const MenuRequest request = r.take();
        ASSERT_TRUE(request.type == MenuRequest::Type::Host && request.players == 4 && request.fill == plan(L::Medium, L::Hard, L::Easy));
        r.menu.connection_failed("The server is busy.");
        ASSERT_EQ(r.element(MenuId::HostSeat1).value, std::string("Medium bot"));
        ASSERT_EQ(r.element(MenuId::HostSeat2).value, std::string("Hard bot"));
        ASSERT_EQ(r.element(MenuId::HostSeat3).value, std::string("Easy bot"));
        r.click(MenuId::HostPlayers);                                                      // 2 players: the Red seat only
        r.click(MenuId::Host);
        const MenuRequest two = r.take();
        ASSERT_TRUE(two.type == MenuRequest::Type::Host && two.players == 2 && two.fill == plan(L::Medium, L::None, L::None));
        r.menu.connection_failed("The server is busy.");
        r.click(MenuId::HostPlayers);                                                      // 3 players
        r.click(MenuId::Host);
        const MenuRequest three = r.take();
        ASSERT_TRUE(three.type == MenuRequest::Type::Host && three.players == 3 && three.fill == plan(L::Medium, L::Hard, L::None));
        r.menu.connection_failed("The server is busy.");
        r.click(MenuId::HostPlayers);                                                      // back to 4: the Black seat's choice was kept all along
        ASSERT_EQ(r.element(MenuId::HostSeat3).value, std::string("Easy bot"));
        // the room's panel says what START will do
        r.menu.show_room("demo-tiny-4p-abc234", 1, 4);
        ASSERT_TRUE(has_text(r.menu.elements(), "At START: Red Medium, Blue Hard, Black Easy."));
        ASSERT_FALSE(has_text(r.menu.elements(), "Empty seats stay empty."));
        r.menu.show_room("demo-tiny-2p-abc234", 1, 2);
        ASSERT_TRUE(has_text(r.menu.elements(), "Empty seats will be Medium bots."));      // (the room of two has the Red seat only)
        Rig uniform;
        uniform.to_panel(MenuId::HostOnline);
        for (const MenuId id : {MenuId::HostSeat1, MenuId::HostSeat2, MenuId::HostSeat3}) {
            uniform.click(id);
            uniform.click(id);
        }
        uniform.menu.show_room("demo-tiny-4p-abc234", 1, 4);
        ASSERT_TRUE(has_text(uniform.menu.elements(), "Empty seats will be Medium bots."));
        Rig none;
        none.menu.show_room("demo-tiny-4p-abc234", 1, 4);
        ASSERT_TRUE(has_text(none.menu.elements(), "Empty seats stay empty.") && !has_text(none.menu.elements(), "bots."));
        // the settings file: written under `host_fill` (one word when every seat is the same, else four), read back; an old file's one word is every seat after the leader's; anything else is the default
        {
            TempDir temp;
            ConfigStore store;
            store.set_location(temp.file("settings.ini"));
            MenuSettings s;
            s.host_fill = plan(L::Easy, L::None, L::Hard);
            s.write(store, MenuSetting::HostFill);
            ASSERT_EQ(store.get_string("host_fill", "", 99), std::string("none,easy,none,hard"));
            MenuSettings back;
            back.load(store);
            ASSERT_TRUE(back.host_fill == s.host_fill);
            MenuSettings same;
            same.host_fill = net::FillPlan(L::Hard);
            same.write(store, MenuSetting::HostFill);
            ASSERT_EQ(store.get_string("host_fill", "", 99), std::string("hard"));
            back.load(store);
            ASSERT_TRUE(back.host_fill == plan(L::Hard, L::Hard, L::Hard));                // (the leader's seat gets nothing)
            store.set_string("host_fill", "  MEDIUM ");
            back.load(store);
            ASSERT_TRUE(back.host_fill == plan(L::Medium, L::Medium, L::Medium));          // (any case, blanks cut)
            store.set_string("host_fill", " Easy , none ,HARD, none");
            back.load(store);
            ASSERT_TRUE(back.host_fill == plan(L::None, L::Hard, L::None));                // (four words: the first is the leader's seat and counts for nothing)
            for (const char* bad : {"loud", "easy,hard", "easy,easy,easy,easy,easy", "easy,,easy,easy", "none,none,none,loud", ""}) {
                store.set_string("host_fill", bad);
                back.load(store);
                ASSERT_FALSE(back.host_fill.any());
            }
            MenuSettings fresh;
            ASSERT_FALSE(fresh.host_fill.any());                                           // nothing is seated unless the player chose it
        }
        // the geometry, in every state: every control and every line inside the screen, no two controls over each other, the label of each row beside its control and the caption under the last
        // of the rows
        for (int players = 2; players <= 4; ++players) {
            for (const MenuPanel panel : {MenuPanel::Host, MenuPanel::Room}) {
                Rig g;
                g.to_panel(MenuId::HostOnline);
                while (g.menu.settings().host_players != players) g.click(MenuId::HostPlayers);
                for (const MenuId id : {MenuId::HostSeat1, MenuId::HostSeat2, MenuId::HostSeat3}) {
                    if (g.exists(id)) g.click(id);
                }
                if (g.exists(MenuId::HostTeams)) g.click(MenuId::HostTeams);
                if (panel == MenuPanel::Room) g.menu.show_room("demo-tiny-4p-abc234", 1, players);
                const std::vector<MenuElement> all = g.menu.elements();
                for (size_t i = 0; i < all.size(); ++i) {
                    ASSERT_TRUE(all[i].rect.x >= 16 && all[i].rect.y >= 16 && all[i].rect.x + all[i].rect.w <= 624 && all[i].rect.y + all[i].rect.h <= 464);
                    for (size_t j = i + 1; j < all.size(); ++j) {
                        if (all[i].id != MenuId::None && all[j].id != MenuId::None) ASSERT_FALSE(intersects(all[i].rect, all[j].rect));
                    }
                }
            }
        }
        for (int players = 2; players <= 4; ++players) {
            Rig g;
            g.to_panel(MenuId::HostOnline);
            while (g.menu.settings().host_players != players) g.click(MenuId::HostPlayers);
            const std::vector<MenuElement> all = g.menu.elements();
            const struct { const char* label; MenuId id; } rows[] = {{"Map", MenuId::HostMap},         {"Players", MenuId::HostPlayers}, {"Red at START", MenuId::HostSeat1},
                                                                    {"Blue at START", MenuId::HostSeat2}, {"Black at START", MenuId::HostSeat3}, {"Teams", MenuId::HostTeams},
                                                                    {"Your name", MenuId::HostName}};
            MenuElement last;                                                              // the last row of the choices (the name is no choice)
            for (const auto& row : rows) {
                if (!g.exists(row.id)) continue;
                MenuElement label;
                for (const MenuElement& e : all) {
                    if (e.text == row.label && e.id == MenuId::None) label = e;
                }
                const MenuElement control = g.element(row.id);
                ASSERT_EQ(label.text, std::string(row.label));
                ASSERT_TRUE(label.rect.x + label.rect.w <= control.rect.x && label.rect.y < control.rect.y + control.rect.h && control.rect.y < label.rect.y + label.rect.h);
                if (row.id != MenuId::HostName) last = control;
            }
            Recorder rec;                                                                  // every label is drawn whole (a text that is cut would lose its end)
            render_start_menu(rec, archive(), g.menu);
            for (const auto& row : rows) {
                if (!g.exists(row.id)) continue;
                bool whole = false;
                for (const Recorder::Text& t : rec.texts) whole = whole || t.text == row.label;
                ASSERT_TRUE(whole);
            }
            // the caption under the rows (the panel says what the standard bot does, it gathers food, raids and fights back, and must not promise more): exact words, dim, between the last row and the name,
            // drawn whole and 14 px
            ASSERT_EQ(std::string(fill_choice_caption()), std::string("Bots gather food, raid and fight back."));
            MenuElement caption;
            MenuElement name_label;
            for (const MenuElement& e : all) {
                if (e.text == fill_choice_caption()) caption = e;
                if (e.text == "Your name") name_label = e;
            }
            ASSERT_EQ(caption.text, std::string(fill_choice_caption()));
            ASSERT_TRUE(caption.id == MenuId::None && caption.tone == MenuTone::Dim && caption.font == FontSize::Px14 && caption.centered);
            ASSERT_TRUE(caption.rect.y >= last.rect.y + last.rect.h && caption.rect.y + caption.rect.h <= name_label.rect.y);                    // under the last row, above the name
            ASSERT_TRUE(caption.rect.x <= last.rect.x && caption.rect.x + caption.rect.w >= last.rect.x + last.rect.w);                            // centred under the whole control
            bool caption_drawn = false;
            for (const Recorder::Text& t : rec.texts) caption_drawn = caption_drawn || (t.text == fill_choice_caption() && t.size == FontSize::Px14);
            ASSERT_TRUE(caption_drawn);
        }
        Rig room_panel;
        room_panel.menu.show_room("demo-tiny-4p-abc234", 1, 4);
        ASSERT_FALSE(has_text(room_panel.menu.elements(), fill_choice_caption()));                                                       // (only the panel that has the choice)
    } TEST_END();

    TEST_CASE("M4.5 Host: the Teams row of a room of three or four players goes round the room's choices, free for all first (Left, Right, Enter, click), and is remembered; the request carries it; a room of two has no such row; a choice that the room does not offer any more is free for all again; the room's panel says the teams") {
        const LocalTeams ffa{};
        const LocalTeams green_red{true, 0, 1};
        const LocalTeams green_blue{true, 0, 2};
        const LocalTeams green_black{true, 0, 3};
        const LocalTeams red_blue{true, 1, 2};
        // the words of a choice: the colour words of the seats that play, the pair first
        ASSERT_EQ(StartMenu::host_teams_text(ffa, 4), std::string("Free for all"));
        ASSERT_EQ(StartMenu::host_teams_text(green_red, 4), std::string("Green + Red against Blue + Black"));
        ASSERT_EQ(StartMenu::host_teams_text(green_blue, 4), std::string("Green + Blue against Red + Black"));
        ASSERT_EQ(StartMenu::host_teams_text(green_black, 4), std::string("Green + Black against Red + Blue"));
        ASSERT_EQ(StartMenu::host_teams_text(green_red, 3), std::string("Green + Red against Blue"));
        ASSERT_EQ(StartMenu::host_teams_text(red_blue, 3), std::string("Red + Blue against Green"));
        ASSERT_EQ(room_teams_sentence(green_red, 4), std::string("Room teams: Green + Red against Blue + Black."));        // (they are the room's: its code names them)
        ASSERT_EQ(room_teams_sentence(ffa, 4), std::string());
        ASSERT_EQ(room_teams_sentence(green_black, 3), std::string());                         // (a choice that the room does not offer is no teams)
        ASSERT_EQ(room_teams_sentence(green_red, 2), std::string());
        Rig r;
        r.to_panel(MenuId::HostOnline);
        ASSERT_TRUE(r.exists(MenuId::HostTeams) && r.element(MenuId::HostTeams).kind == MenuKind::Cycler);
        ASSERT_EQ(r.element(MenuId::HostTeams).value, std::string("Free for all"));            // free for all until the leader chooses
        for (int i = 0; i < 5; ++i) r.key(SDLK_DOWN);                                          // the map, the players, the three seats, the teams
        ASSERT_EQ(r.menu.selected(), MenuId::HostTeams);
        const char* const four[4] = {"Free for all", "Green + Red against Blue + Black", "Green + Blue against Red + Black", "Green + Black against Red + Blue"};
        for (int lap = 0; lap < 2; ++lap) {
            for (const char* word : four) {
                ASSERT_EQ(r.element(MenuId::HostTeams).value, std::string(word));
                r.key(SDLK_RIGHT);
            }
        }
        r.key(SDLK_LEFT);
        ASSERT_EQ(r.element(MenuId::HostTeams).value, std::string(four[3]));                   // Left from the first goes to the last
        r.key(SDLK_RETURN);
        ASSERT_EQ(r.element(MenuId::HostTeams).value, std::string(four[0]));
        r.click(MenuId::HostTeams);
        ASSERT_EQ(r.element(MenuId::HostTeams).value, std::string(four[1]));
        ASSERT_TRUE(std::count(r.changes.begin(), r.changes.end(), MenuSetting::HostTeams) >= 11);
        // the request carries it
        r.click(MenuId::Host);
        const MenuRequest request = r.take();
        ASSERT_TRUE(request.type == MenuRequest::Type::Host && request.teams == green_red && request.players == 4);
        r.menu.connection_failed("The server is busy.");
        ASSERT_EQ(r.element(MenuId::HostTeams).value, std::string(four[1]));                   // (a failed attempt keeps it)
        // a room of three offers its own choices: the pairs 0+1, 0+2 and 1+2 (the third seat plays alone); the choice that was made stays while the room offers it
        r.mouse_move(MenuId::HostPlayers);
        r.key(SDLK_LEFT);                                                                      // 4 -> 3: Green + Red against Blue is one of its choices
        ASSERT_EQ(r.element(MenuId::HostTeams).value, std::string("Green + Red against Blue"));
        ASSERT_TRUE(r.menu.settings().host_teams == green_red);
        r.key(SDLK_RIGHT);                                                                     // 3 -> 4: and one of the four's
        ASSERT_EQ(r.element(MenuId::HostTeams).value, std::string(four[1]));
        r.click(MenuId::HostPlayers);                                                          // 4 -> 2: no teams, the choice is gone
        ASSERT_FALSE(r.exists(MenuId::HostTeams));
        ASSERT_FALSE(r.menu.settings().host_teams.set);
        r.click(MenuId::Host);
        ASSERT_TRUE(r.take().teams == ffa);
        r.menu.connection_failed("The server is busy.");
        r.click(MenuId::HostPlayers);                                                          // 2 -> 3
        const char* const three[4] = {"Free for all", "Green + Red against Blue", "Green + Blue against Red", "Red + Blue against Green"};
        for (const char* word : three) {
            ASSERT_EQ(r.element(MenuId::HostTeams).value, std::string(word));
            r.click(MenuId::HostTeams);
        }
        r.click(MenuId::HostTeams);
        r.click(MenuId::HostTeams);
        r.click(MenuId::HostTeams);                                                            // Red + Blue against Green
        ASSERT_EQ(r.element(MenuId::HostTeams).value, std::string(three[3]));
        r.click(MenuId::HostPlayers);                                                          // 3 -> 4: the pair 1+2 is not a choice of a room of four
        ASSERT_EQ(r.element(MenuId::HostTeams).value, std::string("Free for all"));
        ASSERT_FALSE(r.menu.settings().host_teams.set);
        // the room's panel says the teams where a message would stand (a message takes the place)
        Rig room;
        room.to_panel(MenuId::HostOnline);
        room.click(MenuId::HostTeams);
        room.click(MenuId::HostTeams);                                                         // Green + Blue against Red + Black
        room.menu.show_room("demo-tiny-4p-abc234", 1, 4);
        const size_t with_teams = room.menu.elements().size();
        ASSERT_TRUE(has_text(room.menu.elements(), "Room teams: Green + Blue against Red + Black."));
        room.menu.show_room("demo-tiny-2p-abc234", 1, 2);                                      // (a room of two has no teams)
        ASSERT_FALSE(has_text(room.menu.elements(), "Room teams:"));
        ASSERT_EQ(room.menu.elements().size() + 1, with_teams);
        Rig plain;
        plain.menu.show_room("demo-tiny-4p-abc234", 1, 4);
        ASSERT_EQ(plain.menu.elements().size() + 1, with_teams);                               // free for all: the panel is what it was
        room.clipboard.writable = false;
        room.menu.show_room("demo-tiny-4p-abc234", 1, 4);
        room.key(SDLK_c, KMOD_GUI);
        ASSERT_TRUE(has_text(room.menu.elements(), "Copy failed") && !has_text(room.menu.elements(), "Room teams:"));
        // the settings file: written under `host_teams` (ffa or the pair), read back, anything else is free for all
        {
            TempDir temp;
            ConfigStore store;
            store.set_location(temp.file("settings.ini"));
            MenuSettings s;
            s.host_teams = green_blue;
            s.write(store, MenuSetting::HostTeams);
            ASSERT_EQ(store.get_string("host_teams", "", 99), std::string("0+2"));
            MenuSettings back;
            back.load(store);
            ASSERT_TRUE(back.host_teams == green_blue);
            ASSERT_FALSE(back.teams.set);                                                      // (the single-player panel's choice is its own key)
            s.host_teams = ffa;
            s.write(store, MenuSetting::HostTeams);
            ASSERT_EQ(store.get_string("host_teams", "", 99), std::string("ffa"));
            back.load(store);
            ASSERT_FALSE(back.host_teams.set);
            store.set_string("host_teams", " 1+2 ");
            back.load(store);
            ASSERT_TRUE(back.host_teams == red_blue);
            for (const char* bad : {"1+1", "0+4", "01", "x", ""}) {
                store.set_string("host_teams", bad);
                back.load(store);
                ASSERT_FALSE(back.host_teams.set);
            }
            MenuSettings fresh;
            ASSERT_FALSE(fresh.host_teams.set);
        }
        // a choice that is stored for a room that does not offer it counts as free for all (the panel shows what the request will carry)
        Rig stale;
        MenuSettings stored;
        stored.name = "Player";
        stored.host_players = 4;
        stored.host_teams = red_blue;
        stale.menu.set_settings(stored);
        stale.to_panel(MenuId::HostOnline);
        ASSERT_EQ(stale.element(MenuId::HostTeams).value, std::string("Free for all"));
        stale.click(MenuId::Host);
        ASSERT_TRUE(stale.take().teams == ffa);
    } TEST_END();

    TEST_CASE("M4.6 Host: the Teams row's choice is a word of the room's code (demo-<map>-<n>p-t01-<six characters>: the room makes the teams for every start, the one when it fills up too): every map and every choice of a room of three or four, nothing for free for all, for a room of two and for a choice that the room does not offer; the code is what the server and every game read (net::room_code_teams), valid and within the 32 characters; the Host button's request carries the choice and the code made from it names it") {
        const char* keys[] = {"tiny", "small", "medium", "gauntlet", "treasure", "islands"};
        const auto counter = []() {
            auto n = std::make_shared<uint32_t>(0);
            return std::function<uint32_t()>([n]() { return *n += 977u; });
        };
        size_t words = 0;
        for (size_t m = 0; m < kMenuMapCount; ++m) {
            for (int players = 2; players <= 4; ++players) {
                for (const sim::StartTeams& choice : sim::room_team_choices(static_cast<uint8_t>(players))) {
                    const std::string plain = make_room_code(menu_map(m), players, counter());                       // (what the code was before: the same random characters)
                    const std::string code = make_room_code(menu_map(m), players, counter(), choice);
                    ASSERT_TRUE(net::valid_room_code(code) && code.size() <= 27 && code.size() <= net::kMaxRoomCodeChars);
                    ASSERT_TRUE(net::room_code_teams(code) == choice);
                    if (!choice.set) {
                        ASSERT_EQ(code, plain);                                                                         // free for all: the code is what it was
                        continue;
                    }
                    ++words;
                    const std::string word = std::string("t") + static_cast<char>('0' + choice.a) + static_cast<char>('0' + choice.b);
                    const std::string head = std::string("demo-") + keys[m] + "-" + std::to_string(players) + "p-";
                    ASSERT_EQ(code, head + word + "-" + plain.substr(head.size()));                                     // the word between the player count and the six characters, nothing else changed
                    ASSERT_EQ(code.size(), plain.size() + 4);
                }
            }
        }
        ASSERT_EQ(words, kMenuMapCount * 6);                                                                           // three choices for three players, three for four, on each of the six maps
        // a choice that the room does not offer is no word: a room of two, a seat that the room does not have, the same team written from the other end
        const LocalTeams green_red{true, 0, 1};
        const std::vector<std::pair<int, LocalTeams>> not_offered = {
            {2, green_red}, {3, LocalTeams{true, 0, 3}}, {3, LocalTeams{true, 2, 3}}, {4, LocalTeams{true, 1, 2}}, {4, LocalTeams{true, 2, 3}}, {4, LocalTeams{true, 1, 0}},
            {4, LocalTeams{true, 0, 0}}, {0, green_red},                                                               // (0 players is a room of two)
        };
        for (const auto& c : not_offered) {
            const std::string plain = make_room_code(menu_map(4), c.first, counter());
            const std::string code = make_room_code(menu_map(4), c.first, counter(), c.second);
            ASSERT_EQ(code, plain);
            ASSERT_FALSE(net::room_code_teams(code).set);
        }
        ASSERT_EQ(make_room_code(menu_map(4), 9, counter(), green_red), make_room_code(menu_map(4), 4, counter(), green_red));      // (the players are clamped to 2 .. 4 first: nine is four)
        ASSERT_EQ(make_room_code(menu_map(4), 4, []() { return 0u; }, LocalTeams{true, 0, 3}), std::string("demo-treasure-4p-t03-aaaaaa"));      // the form of the code, by name
        ASSERT_EQ(make_room_code(menu_map(1), 3, []() { return 0u; }, LocalTeams{true, 1, 2}), std::string("demo-small-3p-t12-aaaaaa"));
        // the Host button: the request carries the choice, and the code made from it names it
        Rig r;
        r.to_panel(MenuId::HostOnline);
        r.click(MenuId::HostTeams);
        r.click(MenuId::HostTeams);
        r.click(MenuId::HostTeams);                                                                                    // Green + Black against Red + Blue
        ASSERT_EQ(r.element(MenuId::HostTeams).value, std::string("Green + Black against Red + Blue"));
        r.click(MenuId::Host);
        const MenuRequest request = r.take();
        ASSERT_TRUE(request.type == MenuRequest::Type::Host && request.teams == LocalTeams({true, 0, 3}) && request.players == 4);
        const std::string code = make_room_code(menu_map(static_cast<size_t>(request.map)), request.players, []() { return 0u; }, request.teams);
        ASSERT_EQ(code, std::string("demo-treasure-4p-t03-aaaaaa"));
        ASSERT_TRUE(net::room_code_teams(code) == LocalTeams({true, 0, 3}));
        r.menu.connection_failed("The server is busy.");
        r.click(MenuId::HostPlayers);                                                                                  // 4 -> 2: the choice is gone from the request, so from the code
        r.click(MenuId::Host);
        const MenuRequest two = r.take();
        ASSERT_TRUE(two.players == 2 && !two.teams.set);
        ASSERT_FALSE(net::room_code_teams(make_room_code(menu_map(static_cast<size_t>(two.map)), two.players, []() { return 0u; }, two.teams)).set);
    } TEST_END();

    TEST_CASE("M4.4 Host: the map is Treasure until a choice is stored (the list keeps the page's order, so Tiny, its first entry, is not the default) and a stored choice wins: a new menu, an empty store, a store with a word that is no map, a store with each of the six (any case); the panel shows it, the request carries it, the room's code names it, a change is stored") {
        // the default is Treasure, the fifth of the six; the list is the page's (by size), so the first entry is Tiny
        ASSERT_EQ(kDefaultMenuMap, 4);
        ASSERT_EQ(std::string(menu_map(static_cast<size_t>(kDefaultMenuMap)).key), std::string("treasure"));
        ASSERT_EQ(std::string(menu_map(0).key), std::string("tiny"));
        ASSERT_EQ(std::string(menu_map(kMenuMapCount).key), std::string("treasure"));      // an index that is not in the list gives the default map too
        ASSERT_EQ(std::string(menu_map(99).key), std::string("treasure"));
        {
            StartMenu fresh;
            ASSERT_EQ(fresh.settings().host_map, 4);
            MenuSettings plain;
            ASSERT_EQ(plain.host_map, 4);
        }
        // nothing stored: the panel shows Treasure, the request, the code and the settings key follow it, a change is stored
        {
            Rig r;
            r.to_panel(MenuId::HostOnline);
            ASSERT_EQ(r.menu.selected(), MenuId::HostMap);
            ASSERT_EQ(r.element(MenuId::HostMap).value, std::string("Treasure"));
            ASSERT_EQ(r.menu.settings().host_map, 4);
            for (const MenuId next : {MenuId::HostPlayers, MenuId::HostSeat1, MenuId::HostSeat2, MenuId::HostSeat3, MenuId::HostTeams, MenuId::HostName, MenuId::Host}) {
                r.key(SDLK_DOWN);                                                          // (the players, the three seats after the leader's, the teams, the name, Host)
                ASSERT_EQ(r.menu.selected(), next);
            }
            r.key(SDLK_RETURN);
            const MenuRequest request = r.take();
            ASSERT_TRUE(request.type == MenuRequest::Type::Host && request.map == 4 && request.players == 4);
            ASSERT_EQ(std::string(menu_map(static_cast<size_t>(request.map)).key), std::string("treasure"));
            const std::string code = make_room_code(menu_map(static_cast<size_t>(request.map)), request.players, []() { return 0u; });
            ASSERT_EQ(code, std::string("demo-treasure-4p-aaaaaa"));                       // a room whose code names Treasure: the server makes it on TREASURE.LVL
            r.menu.connection_failed("The server is busy.");
            ASSERT_EQ(r.element(MenuId::HostMap).value, std::string("Treasure"));          // (and a failed attempt keeps it)
            TempDir temp;
            ConfigStore store;
            store.set_location(temp.file("settings.ini"));
            r.menu.settings().write(store, MenuSetting::HostMap);
            ASSERT_EQ(store.get_string("host_map", "", 99), std::string("treasure"));
        }
        // the settings that come from a store: what is not one of the six words is the default, and a stored map wins, Tiny and Islands (the two ends of the list) included
        const auto shown_with = [](const std::string& ini, int& index) {
            ConfigStore store;
            store.parse(ini);
            MenuSettings settings;
            settings.load(store);
            index = settings.host_map;
            Rig r;
            r.menu.set_settings(settings);
            r.to_panel(MenuId::HostOnline);
            return r.element(MenuId::HostMap).value;
        };
        int index = -1;
        ASSERT_EQ(shown_with("", index), std::string("Treasure"));
        ASSERT_TRUE(index == 4);
        ASSERT_EQ(shown_with("host_map=\n", index), std::string("Treasure"));
        ASSERT_TRUE(index == 4);
        ASSERT_EQ(shown_with("host_map=bogus\n", index), std::string("Treasure"));
        ASSERT_TRUE(index == 4);
        ASSERT_EQ(shown_with("host_map=tiny\n", index), std::string("Tiny"));
        ASSERT_TRUE(index == 0);
        ASSERT_EQ(shown_with("host_map=SMALL\n", index), std::string("Small"));
        ASSERT_TRUE(index == 1);
        ASSERT_EQ(shown_with("host_map=Medium\n", index), std::string("Medium"));
        ASSERT_TRUE(index == 2);
        ASSERT_EQ(shown_with("host_map=gauntlet\n", index), std::string("Gauntlet"));
        ASSERT_TRUE(index == 3);
        ASSERT_EQ(shown_with("host_map=treasure\n", index), std::string("Treasure"));
        ASSERT_TRUE(index == 4);
        ASSERT_EQ(shown_with("host_map=islands\n", index), std::string("Islands"));
        ASSERT_TRUE(index == 5);
    } TEST_END();

    TEST_CASE("M5.1 Servers: HOST, HOST:PORT, IPv4, [IPv6] and [IPv6]:PORT are read (the port 4001 when none is given); anything else is refused with a reason; what the screen shows reads back") {
        struct Good { const char* text; const char* host; uint16_t port; };
        const Good good[] = {
            {"beta.playants.org", "beta.playants.org", 4001}, {"beta.playants.org:4001", "beta.playants.org", 4001}, {"localhost:1", "localhost", 1},
            {"127.0.0.1:65535", "127.0.0.1", 65535}, {"  example.org:80  ", "example.org", 80}, {"my_host-1.local", "my_host-1.local", 4001},
            {"[::1]:4010", "::1", 4010}, {"[::1]", "::1", 4001}, {"::1", "::1", 4001}, {"fe80::1:2", "fe80::1:2", 4001}, {"a", "a", 4001},
        };
        for (const Good& g : good) {
            ServerAddress s;
            std::string why;
            ASSERT_TRUE(parse_server(g.text, s, why));
            ASSERT_TRUE(s.host == g.host && s.port == g.port);
            ServerAddress back;
            ASSERT_TRUE(parse_server(server_label(s), back, why) && back == s);            // the label is a server again
        }
        ASSERT_EQ(server_label(ServerAddress{}), std::string("beta.playants.org:4001"));
        ASSERT_EQ(server_label(ServerAddress{"::1", 4010}), std::string("[::1]:4010"));
        const char* bad[] = {"", "   ", "host:", "host:0", "host:65536", "host:99999999", "host:-1", "host:12a", "host name", "ho$t", ".host", "-host", "a..b", "[::1", "[::1]x",
                             "[::1]:", "[host]:1", "[]", "http://x", "a:b:c:zz", "host:4001:1", "tab\thost", "[1:2]", "[abc]", "host:4294967297", "host:99999999999999999999", "host:4294967296", "host:1 2"};
        for (const char* text : bad) {
            ServerAddress s;
            std::string why;
            ASSERT_FALSE(parse_server(text, s, why));
            ASSERT_FALSE(why.empty());
        }
        ServerAddress long_host;
        std::string why;
        ASSERT_TRUE(parse_server(std::string(253, 'a'), long_host, why));
        ASSERT_FALSE(parse_server(std::string(254, 'a'), long_host, why));
        std::string v6_45 = "1";                                                              // an IPv6 text is at most 45 characters (the longest, an IPv4-mapped one)
        while (v6_45.size() < 45) v6_45 += (v6_45.size() % 2 == 1) ? ":" : "1";
        ASSERT_EQ(v6_45.size(), size_t{45});
        ASSERT_TRUE(parse_server("[" + v6_45 + "]", long_host, why));
        ASSERT_FALSE(parse_server("[" + v6_45 + "1]", long_host, why));
        ASSERT_TRUE(parse_server("host:65535", long_host, why) && long_host.port == 65535);
        ASSERT_TRUE(parse_server("host:00080", long_host, why) && long_host.port == 80);
        ASSERT_TRUE(parse_server("host:000000000000000000001", long_host, why) && long_host.port == 1);          // leading zeros are no harm, and no wrap-round either way
        ASSERT_FALSE(parse_server("host name", long_host, why));
        ASSERT_TRUE(why.find("blank") != std::string::npos);                                 // a blank or a character that is no address character is named as such
        ASSERT_FALSE(parse_server("caf\xc3\xa9.org", long_host, why));
        ASSERT_TRUE(why.find("blank") != std::string::npos);
    } TEST_END();

    TEST_CASE("M5.2 Room codes of a hosted match: demo-<map>-<n>p-<six characters> from the page's alphabet (read from web/lobby.html), at most 23 characters, a valid room code for every map and size, different every time") {
        std::string page_alphabet;
        {
            std::ifstream page(std::string(ANTS_SOURCE_DIR) + "/web/lobby.html");           // (by the source folder, not the working directory: it is read wherever the test is run from)
            ASSERT_TRUE(page.good());                                                      // a page that cannot be read, or in which the variable is renamed, FAILS the test (it used to skip it)
            std::stringstream text;
            text << page.rdbuf();
            const std::string html = text.str();
            const size_t at = html.find("var chars = '");
            ASSERT_TRUE(at != std::string::npos);
            page_alphabet = html.substr(at + 13, html.find('\'', at + 13) - (at + 13));
        }
        ASSERT_FALSE(page_alphabet.empty());
        ASSERT_EQ(page_alphabet, std::string(kRoomCodeAlphabet));                          // the same alphabet as web/lobby.html (the page is in the repository)
        ASSERT_EQ(std::string(kRoomCodeAlphabet).size(), static_cast<size_t>(31));
        ASSERT_EQ(kRoomCodeRandomChars, static_cast<size_t>(6));
        const char* keys[] = {"tiny", "small", "medium", "gauntlet", "treasure", "islands"};
        uint32_t counter = 0;
        const std::function<uint32_t()> next = [&counter]() { return counter += 977u; };
        std::vector<std::string> seen;
        for (size_t m = 0; m < kMenuMapCount; ++m) {
            ASSERT_EQ(std::string(menu_map(m).key), std::string(keys[m]));
            for (int players = 2; players <= 4; ++players) {
                const std::string code = make_room_code(menu_map(m), players, next);
                ASSERT_TRUE(code.rfind(std::string("demo-") + keys[m] + "-" + std::to_string(players) + "p-", 0) == 0);
                ASSERT_TRUE(net::valid_room_code(code));
                ASSERT_TRUE(code.size() <= 23);
                const std::string tail = code.substr(code.size() - 6);
                for (char c : tail) ASSERT_TRUE(std::string(kRoomCodeAlphabet).find(c) != std::string::npos);
                ASSERT_TRUE(std::find(seen.begin(), seen.end(), code) == seen.end());
                seen.push_back(code);
            }
        }
        ASSERT_EQ(make_room_code(menu_map(1), 9, next).substr(0, 14), std::string("demo-small-4p-"));   // the players are clamped to 2 .. 4
        ASSERT_EQ(make_room_code(menu_map(1), 0, next).substr(0, 14), std::string("demo-small-2p-"));
        // the map index of a key, any case
        ASSERT_TRUE(menu_map_index("ISLANDS") == 5 && menu_map_index("tiny") == 0 && menu_map_index("nope") == -1);
        // random bits that are all ones and all zeros: the first and the last character of the alphabet, never one beyond
        ASSERT_EQ(make_room_code(menu_map(0), 2, []() { return 0u; }).substr(13), std::string("aaaaaa"));
        ASSERT_EQ(make_room_code(menu_map(0), 2, []() { return 0xFFFFFFFFu; }).back(), kRoomCodeAlphabet[0xFFFFFFFFu % 31u]);
    } TEST_END();

    TEST_CASE("M5.3 Names and codes: a name is cleaned (blanks at both ends, printable ASCII, 32 characters) and refused when empty or when it looks like a bot's; a code is cleaned and refused when empty, too long or not letters, digits, - and _; the case of a code is never touched") {
        std::string clean;
        std::string why;
        ASSERT_TRUE(check_player_name("  Ann  Lee  ", clean, why) && clean == "Ann  Lee");
        ASSERT_TRUE(check_player_name(std::string(50, 'a'), clean, why) && clean.size() == 32);
        ASSERT_TRUE(check_player_name(std::string(31, 'a') + " tail", clean, why) && clean == std::string(31, 'a'));      // cut at 32, and the blank that the cut leaves at its end goes
        ASSERT_FALSE(check_player_name("   ", clean, why));
        ASSERT_TRUE(why.find("name") != std::string::npos);
        ASSERT_TRUE(check_player_name("caf\xc3\xa9", clean, why) && clean == "caf");        // only printable ASCII is kept
        ASSERT_FALSE(check_player_name("", clean, why));
        ASSERT_FALSE(check_player_name("\x01\x02", clean, why));
        ASSERT_FALSE(check_player_name("Bot (x)", clean, why));
        ASSERT_TRUE(why.find("Bot (") != std::string::npos);
        ASSERT_TRUE(looks_like_bot_name("b o T(") && !looks_like_bot_name("bo") && !looks_like_bot_name("Bot") && !looks_like_bot_name("xbot("));
        ASSERT_TRUE(check_room_code("  ab-C_9 ", clean, why) && clean == "ab-C_9");
        ASSERT_TRUE(check_room_code("DEMO-SMALL-X7K2", clean, why) && clean == "DEMO-SMALL-X7K2");
        ASSERT_TRUE(check_room_code(std::string(32, 'z'), clean, why));
        ASSERT_FALSE(check_room_code(std::string(33, 'z'), clean, why));
        ASSERT_TRUE(why.find("at most 32") != std::string::npos);                          // the reason names the limit, not the alphabet
        ASSERT_FALSE(check_room_code("", clean, why));
        ASSERT_TRUE(why.find("code") != std::string::npos);
        ASSERT_FALSE(check_room_code("a b", clean, why));
        ASSERT_TRUE(why.find("letters, digits") != std::string::npos);
        ASSERT_FALSE(check_room_code("a.b", clean, why));
        ASSERT_FALSE(check_room_code("\xc3\xa9", clean, why));
        ASSERT_EQ(StartMenu::kNameMax, net::kMaxNameChars);
        ASSERT_EQ(StartMenu::kCodeMax, net::kMaxRoomCodeChars);
    } TEST_END();

    TEST_CASE("M6.1 Settings: the name, the bots of the four seats, the host's map and players, and the server are written to the store under the remake's keys and read back; a bad value is the default; the server is read and never written by the menu") {
        TempDir temp;
        {
            ConfigStore store;
            store.set_location(temp.file("settings.ini"));
            MenuSettings s;
            s.name = "Maya";
            s.seats = {SeatChoice::Empty, SeatChoice::Easy, SeatChoice::Medium, SeatChoice::Hard};
            s.host_map = 4;
            s.host_players = 2;
            for (MenuSetting which : {MenuSetting::Name, MenuSetting::Bots, MenuSetting::HostMap, MenuSetting::HostPlayers}) s.write(store, which);
            ASSERT_EQ(store.get_string("name", "", 99), std::string("Maya"));
            ASSERT_EQ(store.get_string("bots", "", 99), std::string("off,easy,medium,hard"));
            ASSERT_EQ(store.get_string("host_map", "", 99), std::string("treasure"));
            ASSERT_EQ(store.get_int("host_players", 0, 0, 10), 2);
            ASSERT_FALSE(store.has("server"));                                             // the menu never writes the server
        }
        {   // a new store on the same file, as a restart reads it (and the original's own entries beside)
            std::ofstream out(temp.file("settings.ini"), std::ios::app);
            out << "server=play.example.org:4010\nSound Volume=30\n";
        }
        ConfigStore later;
        later.set_location(temp.file("settings.ini"));
        ASSERT_TRUE(later.load());
        MenuSettings back;
        back.load(later);
        ASSERT_EQ(back.name, std::string("Maya"));
        ASSERT_TRUE(back.seats[0] == SeatChoice::Empty && back.seats[1] == SeatChoice::Easy && back.seats[2] == SeatChoice::Medium && back.seats[3] == SeatChoice::Hard);
        ASSERT_TRUE(back.host_map == 4 && back.host_players == 2);
        ASSERT_EQ(back.server, std::string("play.example.org:4010"));
        ASSERT_EQ(later.get_int("Sound Volume", 100, 0, 100), 30);
        // anything that is not a valid value is the default
        ConfigStore junk;
        junk.parse("name=\nbots=sideways,EASY,,hard,extra\nhost_map=bogus\nhost_players=7\nserver=  \n");
        ConfigStore one_player;
        one_player.parse("host_players=1\n");
        MenuSettings o;
        o.load(one_player);
        ASSERT_EQ(o.host_players, 4);                                                      // a match for one player is no room
        MenuSettings d;
        d.load(junk);
        ASSERT_TRUE(d.name.empty() && d.server.empty());
        ASSERT_TRUE(d.seats[0] == SeatChoice::Empty && d.seats[1] == SeatChoice::Easy && d.seats[2] == SeatChoice::Empty && d.seats[3] == SeatChoice::Hard);
        ASSERT_TRUE(d.host_map == 4 && d.host_players == 4);                               // "bogus" is no map: the default, Treasure (index 4: M4.4)
        {   // what the menu is given is brought into range
            StartMenu menu;
            MenuSettings given;
            given.host_players = 9;
            given.host_map = 99;
            menu.set_settings(given);
            ASSERT_TRUE(menu.settings().host_players == 4 && menu.settings().host_map == 5);
            given.host_players = 1;
            given.host_map = -3;
            menu.set_settings(given);
            ASSERT_TRUE(menu.settings().host_players == 2 && menu.settings().host_map == 0);
        }
        ConfigStore none;
        MenuSettings n;
        n.load(none);
        ASSERT_TRUE(n.name.empty() && n.server.empty() && n.host_map == 4 && n.host_players == 4);     // nothing stored: Treasure
        for (SeatChoice c : n.seats) ASSERT_TRUE(c == SeatChoice::Empty);
        // a name that was stored too long or with odd characters is cleaned
        ConfigStore long_name;
        long_name.parse("name=" + std::string(60, 'n') + "\n");
        MenuSettings l;
        l.load(long_name);
        ASSERT_EQ(l.name.size(), static_cast<size_t>(32));
    } TEST_END();

    TEST_CASE("M6.2 Settings: the menu writes a change at once and only that one (typing a name writes `name`, a seat writes `bots`), and the values it was given come back unchanged") {
        TempDir temp;
        ConfigStore store;
        store.set_location(temp.file("settings.ini"));
        Rig r(0, "Zed");
        MenuSettings s = r.menu.settings();
        int name_writes = 0;
        r.menu.set_on_change([&](MenuSetting which) {
            name_writes += which == MenuSetting::Name ? 1 : 0;
            r.menu.settings().write(store, which);
        });
        r.to_panel(MenuId::JoinWithCode);
        r.key(SDLK_UP);
        r.type("Ruth");
        ASSERT_FALSE(store.has("name"));                                                   // not at every key: the file is written when the field is left (a keystroke used to write it)
        r.key(SDLK_BACKSPACE);
        ASSERT_FALSE(store.has("name"));
        r.key(SDLK_TAB);                                                                   // the field is left: now it is written
        ASSERT_EQ(store.get_string("name", "", 99), std::string("Rut"));
        ASSERT_FALSE(store.has("bots") || store.has("host_map") || store.has("host_players"));
        r.key(SDLK_UP);                                                                    // back in the field (its text selected)
        r.key(SDLK_BACKSPACE);                                                             // ... cleared
        ASSERT_EQ(store.get_string("name", "", 99), std::string("Rut"));                   // not written yet
        r.menu.show_main();                                                                // a panel changes: it is
        ASSERT_EQ(store.get_string("name", "", 99), std::string(""));
        ASSERT_EQ(name_writes, 2);                                                         // one write each time, never one per key (seven keys were pressed)
        r.to_panel(MenuId::JoinWithCode);
        ASSERT_EQ(r.menu.selected(), MenuId::Name);                                        // the name is empty: it is the first empty field
        r.type("Rut");
        r.menu.flush();                                                                    // what the application does when the program ends
        ASSERT_EQ(store.get_string("name", "", 99), std::string("Rut"));
        r.menu.flush();
        ASSERT_EQ(name_writes, 3);                                                         // nothing new to write: no write
        r.menu.show_main();
        ASSERT_EQ(name_writes, 3);
        r.click(MenuId::Single);
        r.click(MenuId::Seat2);
        ASSERT_EQ(store.get_string("bots", "", 99), std::string("off,off,easy,off"));
        ASSERT_EQ(store.get_string("name", "", 99), std::string("Rut"));
        (void)s;
    } TEST_END();

    TEST_CASE("M7.1 Layout: on every panel, in the states that stress it (the longest names, codes, errors and servers), every element lies inside the frame of the 640 x 480 screen and no two elements overlap; the controls are never under the frame-rate plate") {
        for (const Variant& v : all_variants()) {
            Rig r;
            v.set_up(r);
            const std::vector<MenuElement> all = r.menu.elements();
            ASSERT_FALSE(all.empty());
            for (size_t i = 0; i < all.size(); ++i) {
                const ButtonRect& a = all[i].rect;
                const bool inside = a.x >= 16 && a.y >= 16 && a.x + a.w <= 624 - 1 && a.y + a.h <= 464 - 1 && a.w > 0 && a.h > 0;
                if (!inside) std::cout << "\n    [" << v.what << "] element " << i << " (" << a.x << "," << a.y << " " << a.w << "x" << a.h << ") is not inside the frame";
                ASSERT_TRUE(inside);
                ASSERT_TRUE(a.y + a.h <= FPS_OVERLAY_TOP);                                // the frame-rate plate and the version (bottom right) are never covered
                for (size_t j = i + 1; j < all.size(); ++j) {
                    const bool clash = intersects(a, all[j].rect);
                    if (clash) std::cout << "\n    [" << v.what << "] elements " << i << " and " << j << " overlap";
                    ASSERT_FALSE(clash);
                }
            }
        }
    } TEST_END();

    TEST_CASE("M7.2 Layout: the controls of every panel can be reached by the mouse at their middle and nowhere else is a control; the hit test agrees with the rectangles, and a pixel outside every rectangle is no control") {
        for (const Variant& v : all_variants()) {
            Rig r;
            v.set_up(r);
            const std::vector<MenuElement> all = r.menu.elements();
            for (const MenuElement& e : all) {
                if (e.id == MenuId::None) continue;
                ASSERT_EQ(r.menu.control_at(e.rect.x + e.rect.w / 2, e.rect.y + e.rect.h / 2), e.id);
                ASSERT_EQ(r.menu.control_at(e.rect.x, e.rect.y), e.id);
                ASSERT_EQ(r.menu.control_at(e.rect.x + e.rect.w - 1, e.rect.y + e.rect.h - 1), e.id);
                ASSERT_TRUE(r.menu.control_at(e.rect.x - 1, e.rect.y - 1) != e.id);
                ASSERT_TRUE(r.menu.control_at(e.rect.x + e.rect.w, e.rect.y + e.rect.h) != e.id);
            }
            for (const MenuElement& e : all) {                                              // the pointer over a button or a row selects it; over a field it does not (a click does)
                if (e.id == MenuId::None || e.kind == MenuKind::Field) continue;
                r.menu.on_mouse_move(e.rect.x + e.rect.w / 2, e.rect.y + e.rect.h / 2);
                ASSERT_EQ(r.menu.selected(), e.id);
            }
            ASSERT_EQ(r.menu.control_at(0, 0), MenuId::None);
            ASSERT_EQ(r.menu.control_at(639, 479), MenuId::None);
            ASSERT_EQ(r.menu.control_at(-5, 300), MenuId::None);
        }
    } TEST_END();

    TEST_CASE("M8.1 Drawing: every panel is drawn with the background of the original and every text of it lies inside the element that owns it; long texts are cut (the last line that fits ends with '...'), a name or code that is wider than its field is cut to it, nothing is drawn off the screen") {
        for (const int32_t scale : {1, 2}) {                                               // the estimate of the font, and a font twice as wide: the single lines are cut at their box
            for (const Variant& v : all_variants()) {
                Rig r;
                v.set_up(r);
                Recorder rec;
                rec.width_scale = scale;
                render_start_menu(rec, archive(), r.menu);
                ASSERT_FALSE(rec.sprites.empty());                                         // the sm_screen background is made of sprites
                const std::vector<MenuElement> all = r.menu.elements();
                for (const Recorder::Text& t : rec.texts) {
                    const int32_t w = rec.get_text_width(t.text, t.size);
                    const int32_t h = font_cell_height(t.size);
                    bool inside_some = false;
                    for (const MenuElement& e : all) {
                        if (t.x >= e.rect.x && t.y >= e.rect.y && t.x + w <= e.rect.x + e.rect.w && t.y + h <= e.rect.y + e.rect.h) inside_some = true;
                    }
                    if (!inside_some) std::cout << "\n    [" << v.what << ", width x" << scale << "] the text \"" << t.text << "\" at (" << t.x << "," << t.y << ") " << w << "x" << h << " is outside every element";
                    ASSERT_TRUE(inside_some);
                    ASSERT_TRUE(t.x >= 0 && t.y >= 0 && t.x + w <= StartMenu::kWidth && t.y + h <= StartMenu::kHeight);
                }
                for (const Recorder::Box& b : rec.fills) {
                    ASSERT_TRUE(b.rect.x >= 0 && b.rect.y >= 0 && b.rect.x + b.rect.w <= StartMenu::kWidth + 1 && b.rect.y + b.rect.h <= StartMenu::kHeight + 1);
                }
            }
        }
        // the long error is cut with "..." where the box ends
        Rig r;
        r.to_panel(MenuId::JoinWithCode);
        r.key(SDLK_RETURN);
        r.menu.take_request();
        r.menu.connection_failed(long_message());
        Recorder rec;
        render_start_menu(rec, archive(), r.menu);
        bool cut = false;
        for (const Recorder::Text& t : rec.texts) {
            if (t.size == FontSize::Px18 && t.text.size() > 3 && t.text.compare(t.text.size() - 3, 3, "...") == 0) cut = true;
        }
        ASSERT_TRUE(cut);
        // a short message is not cut
        Rig s;
        s.menu.show_main("The connection was lost.");
        Recorder rec2;
        render_start_menu(rec2, archive(), s.menu);
        bool whole = false;
        for (const Recorder::Text& t : rec2.texts) whole = whole || t.text == "The connection was lost.";
        ASSERT_TRUE(whole);
        // the 32 characters of the widest name show their END in the field that has the focus (the original's edit field) and never run out of it
        Rig w;
        w.to_panel(MenuId::JoinWithCode);
        w.key(SDLK_UP);
        w.type(std::string(40, 'W'));
        Recorder rec3;
        render_start_menu(rec3, archive(), w.menu);
        const MenuElement name = w.element(MenuId::Name);
        for (const Recorder::Text& t : rec3.texts) {
            if (t.text.find("WW") == std::string::npos) continue;
            ASSERT_TRUE(t.x >= name.rect.x && t.x + rec3.get_text_width(t.text, t.size) <= name.rect.x + name.rect.w);
        }
    } TEST_END();

    TEST_CASE("M8.2 Drawing: the code of a room is drawn in the largest size that fits its box (35 px for the page's codes, smaller for the longest ones), the buttons carry their words, the selected one is lit, the title is the panel's") {
        Rig r;
        r.menu.show_room("demo-small-4p-abcdef", 1, 4);
        Recorder a;
        render_start_menu(a, archive(), r.menu);
        bool big = false;
        for (const Recorder::Text& t : a.texts) {
            if (t.text == "demo-small-4p-abcdef") big = t.size == FontSize::Px35;
        }
        ASSERT_TRUE(big);
        r.menu.show_room(std::string(32, 'W'), 1, 4);
        Recorder b;
        render_start_menu(b, archive(), r.menu);
        bool smaller = false;
        for (const Recorder::Text& t : b.texts) {
            if (t.text == std::string(32, 'W')) smaller = t.size != FontSize::Px35;
        }
        ASSERT_TRUE(smaller);
        Rig m;
        Recorder c;
        render_start_menu(c, archive(), m.menu);
        std::vector<std::string> words;
        for (const Recorder::Text& t : c.texts) words.push_back(t.text);
        for (const char* w : {"Welcome to Ants!", "Single player", "Join with a code", "Host an online match", "Quit"}) {
            ASSERT_TRUE(std::find(words.begin(), words.end(), std::string(w)) != words.end());
        }
        // the lit button has a lighter face than the others (the box fills before its label)
        const MenuElement lit = m.element(MenuId::Single);
        const MenuElement dull = m.element(MenuId::Quit);
        assets::ColorRGBA lit_face{}, dull_face{};
        for (const Recorder::Box& box : c.fills) {
            if (box.rect.x == lit.rect.x + 1 && box.rect.y == lit.rect.y + 1 && box.rect.w == lit.rect.w - 2 && box.rect.h == lit.rect.h - 2) lit_face = box.colour;
            if (box.rect.x == dull.rect.x + 1 && box.rect.y == dull.rect.y + 1 && box.rect.w == dull.rect.w - 2 && box.rect.h == dull.rect.h - 2) dull_face = box.colour;
        }
        ASSERT_TRUE(lit_face.g > dull_face.g);
    } TEST_END();

    TEST_CASE("M9.1 Command line: an option that starts a match, a room, a test run or a screenshot shows no menu (each of them alone); --start-menu forces it, also with --headless and --screenshot; the options that only set something up do not skip it") {
        const auto menu_for = [](std::vector<std::string> args) {
            std::vector<std::string> full = {"ants"};
            full.insert(full.end(), args.begin(), args.end());
            std::vector<char*> arg_ptrs;
            for (std::string& a : full) arg_ptrs.push_back(a.data());
            arg_ptrs.push_back(nullptr);
            return Application::parse_arguments(static_cast<int>(full.size()), arg_ptrs.data());
        };
#if defined(__EMSCRIPTEN__)
        ASSERT_FALSE(menu_for({}).start_menu);                                              // the web build never shows it
#else
        ASSERT_TRUE(menu_for({}).start_menu);                                               // a native game started with nothing on the command line
        // the options that choose a mode: each skips the menu (today's start exactly)
        const std::vector<std::vector<std::string>> skipping = {
            {"--map", "Original-Ants/Maps/TINY.LVL"}, {"--map-select"}, {"--play"}, {"--host"}, {"--host", "4002"}, {"--join", "127.0.0.1:4001"}, {"--join-url", "ws://x/ws"}, {"--room", "abc"},
            {"--token", "t"}, {"--seat", "1"}, {"--bot", "1:easy"}, {"--alone"}, {"--headless"}, {"--screenshot", "x.png"}, {"--player", "1"}, {"-pnum:1"}, {"-pnum=2"}, {"--select-ant", "3"},
            {"--select-base", "1"}, {"--open-options"}, {"--scorecard"}};
        for (const auto& args : skipping) {
            const ApplicationConfig c = menu_for(args);
            if (c.start_menu) std::cout << "\n    the menu was not skipped by " << args[0];
            ASSERT_FALSE(c.start_menu);
        }
        // the options that only set something up: the menu comes
        const std::vector<std::vector<std::string>> setting_up = {
            {"--name", "Bob"}, {"--settings", "s.ini"}, {"--seed", "5"}, {"--fullscreen"}, {"--frames", "9"}, {"--show-grid"}, {"--team-name", "1", "Bob"}, {"-N1Bob"}, {"--title", "T"},
            {"--window-pos", "10,10"}, {"--window-size", "800,600"}, {"--grid", "2x2"}, {"--cell", "1"}, {"--display", "0"}, {"--audio-focus"}, {"--port", "4005"}, {"--loopback"},
            {"--lan-port", "5000"}, {"--no-lan"}, {"--start-when", "2"}, {"--server", "play.example.org:4001"}, {"--unknown-option"}};
        for (const auto& args : setting_up) {
            const ApplicationConfig c = menu_for(args);
            if (!c.start_menu) std::cout << "\n    the menu was skipped by " << args[0];
            ASSERT_TRUE(c.start_menu);
            ASSERT_TRUE(c.startup_error.empty());
        }
        // --start-menu forces it: also headless and with a screenshot (the tests and the screenshots), with a seat and with bots (they set the menu's rows)
        for (const auto& args : std::vector<std::vector<std::string>>{{"--start-menu"}, {"--headless", "--start-menu"}, {"--start-menu", "--headless", "--screenshot", "x.png"},
                                                                       {"--start-menu", "--player", "2"}, {"--start-menu", "--bot", "1:hard"}, {"--map-select", "--start-menu"}}) {
            const ApplicationConfig c = menu_for(args);
            ASSERT_TRUE(c.start_menu);
            ASSERT_TRUE(c.startup_error.empty());
        }
        // the menu comes first and chooses the match: an option that starts a match or a room at once cannot be combined with it
        for (const auto& args : std::vector<std::vector<std::string>>{{"--start-menu", "--map", "x.lvl"}, {"--start-menu", "--host"}, {"--start-menu", "--join", "h:1"},
                                                                       {"--join-url", "ws://x", "--start-menu"}, {"--start-menu", "--open-options"}, {"--start-menu", "--scorecard"}}) {
            const ApplicationConfig c = menu_for(args);
            ASSERT_FALSE(c.startup_error.empty());
            ASSERT_TRUE(c.startup_error.find("--start-menu") != std::string::npos);
        }
        // a hand-made config has no menu (the game as it was)
        ASSERT_FALSE(ApplicationConfig{}.start_menu);
#endif
    } TEST_END();

    TEST_CASE("M9.3 Command line: --play is the setup screen's own START at its first visit: no flag, no play_at_once; with it (either order of --map) the game keeps the setup screen's way, the map and the bots are the ones named, nothing else changes") {
        const auto parse = [](std::vector<std::string> args) {
            std::vector<std::string> full = {"ants"};
            full.insert(full.end(), args.begin(), args.end());
            std::vector<char*> arg_ptrs;
            for (std::string& a : full) arg_ptrs.push_back(a.data());
            arg_ptrs.push_back(nullptr);
            return Application::parse_arguments(static_cast<int>(full.size()), arg_ptrs.data());
        };
        const std::string tiny = "Original-Ants/Maps/TINY.LVL";
        ASSERT_FALSE(parse({}).play_at_once);
        ASSERT_FALSE(parse({"--map", tiny}).play_at_once);                                  // (the direct start of the tests and the screenshots: no flag)
        ASSERT_FALSE(parse({"--map", tiny}).start_in_map_select);                           // ... and it still starts the match at once, without the screens
        ApplicationConfig c = parse({"--play"});
        ASSERT_TRUE(c.play_at_once && c.start_in_map_select && c.default_map_path.empty() && c.startup_error.empty());
        for (const auto& args : std::vector<std::vector<std::string>>{{"--map", tiny, "--play"}, {"--play", "--map", tiny}}) {
            c = parse(args);
            ASSERT_TRUE(c.play_at_once);
            ASSERT_TRUE(c.start_in_map_select);                                             // (--map would have turned it off: the order of the two does not matter)
            ASSERT_EQ(c.default_map_path, tiny);
            ASSERT_TRUE(c.startup_error.empty());
        }
        c = parse({"--map", tiny, "--play", "--bot", "1:medium", "--bot", "2:medium", "--bot", "3:medium", "--name", "Bob"});
        ASSERT_TRUE(c.play_at_once && c.bots.size() == 3 && c.player_name == "Bob" && c.net_role == ApplicationConfig::NetRole::None);
#if !defined(__EMSCRIPTEN__)
        ASSERT_FALSE(c.start_menu);                                                          // --play is a mode: no start menu in front of it
#endif
    } TEST_END();

    TEST_CASE("M9.3b Command line: --alone is a game for one: off by default (a game of this machine without --bot plays all four colonies), a mode of its own (no start menu) that --play and --map leave as they are, and it is refused with --bot and with --start-menu, in either order, with the reason") {
        const auto parse = [](std::vector<std::string> args) {
            std::vector<std::string> full = {"ants"};
            full.insert(full.end(), args.begin(), args.end());
            std::vector<char*> arg_ptrs;
            for (std::string& a : full) arg_ptrs.push_back(a.data());
            arg_ptrs.push_back(nullptr);
            return Application::parse_arguments(static_cast<int>(full.size()), arg_ptrs.data());
        };
        const std::string tiny = "Original-Ants/Maps/TINY.LVL";
        ASSERT_FALSE(parse({}).alone);
        ASSERT_FALSE(parse({"--map", tiny, "--play"}).alone);                                 // (the original's single player: no option, no change)
        ApplicationConfig c = parse({"--map", tiny, "--play", "--alone", "--name", "Bob"});
        ASSERT_TRUE(c.alone && c.play_at_once && c.bots.empty() && c.startup_error.empty());
        ASSERT_TRUE(c.player_name == "Bob" && c.default_map_path == tiny && c.start_in_map_select);
        ASSERT_TRUE(c.net_role == ApplicationConfig::NetRole::None);
        c = parse({"--alone"});
        ASSERT_TRUE(c.alone && c.startup_error.empty());
#if !defined(__EMSCRIPTEN__)
        ASSERT_FALSE(c.start_menu);                                                            // --alone chooses the match's seats: no start menu in front of it (--map and --play do not need it: M9.1 has it alone)
        // the menu's Single player chooses who plays, so the two cannot be combined (the menu is not asked what the option has decided); the reason is told, in either order
        for (const auto& args : std::vector<std::vector<std::string>>{{"--start-menu", "--alone"}, {"--alone", "--start-menu"}, {"--headless", "--alone", "--start-menu"}}) {
            c = parse(args);
            ASSERT_EQ(c.startup_error, std::string("--start-menu cannot be combined with --alone: the menu's Single player chooses who plays."));
        }
        c = parse({"--start-menu", "--map", "x.lvl", "--alone"});
        ASSERT_TRUE(c.startup_error.find("--map") != std::string::npos);                        // (the first mistake is the one that is told)
#endif
        c = parse({"--alone", "--bot", "1:easy"});
        ASSERT_EQ(c.startup_error, std::string("--alone cannot be used with --bot: a game for one has no other player."));
        c = parse({"--bot", "1:easy", "--alone"});
        ASSERT_EQ(c.startup_error, std::string("--alone cannot be used with --bot: a game for one has no other player."));
        c = parse({"--bot", "9", "--alone"});
        ASSERT_TRUE(c.startup_error.find("--bot 9") == 0);                                     // (the first mistake is the one that is told)
    } TEST_END();

    TEST_CASE("M9.2 Command line: --server is read as HOST[:PORT] (it overrides the settings key and the default); a bad one is a start error with the reason and the text; a missing value too; the rig of start_game.sh shows no menu") {
        const auto parse = [](std::vector<std::string> args) {
            std::vector<std::string> full = {"ants"};
            full.insert(full.end(), args.begin(), args.end());
            std::vector<char*> arg_ptrs;
            for (std::string& a : full) arg_ptrs.push_back(a.data());
            arg_ptrs.push_back(nullptr);
            return Application::parse_arguments(static_cast<int>(full.size()), arg_ptrs.data());
        };
        ApplicationConfig ok = parse({"--server", "play.example.org:4010"});
        ASSERT_TRUE(ok.startup_error.empty());
        ASSERT_EQ(ok.server, std::string("play.example.org:4010"));
        ASSERT_EQ(parse({}).server, std::string(""));
        const std::vector<std::string> bad = {"bad host", "host:99999", "host:0", "[::1", "", "ho$t"};
        for (const std::string& text : bad) {
            const ApplicationConfig c = parse({"--server", text});
            if (c.startup_error.empty()) std::cout << "\n    '" << text << "' was accepted";
            ASSERT_FALSE(c.startup_error.empty());
            ASSERT_TRUE(c.startup_error.find("--server") != std::string::npos);
            if (!text.empty()) ASSERT_TRUE(c.startup_error.find(text) != std::string::npos);
        }
        const ApplicationConfig missing = parse({"--server"});
        ASSERT_TRUE(missing.startup_error.find("--server needs") != std::string::npos);
        // the command lines that start_game.sh prints (the four-window rig: window 0 hosts, the others join and ask for their seat; --single is a plain game)
        const std::vector<std::vector<std::string>> rig = {
            {"--name", "Antonio", "--title", "Ants - Green (Antonio)", "--grid", "2x2", "--cell", "1", "--audio-focus", "--no-lan", "--host", "4001", "--loopback"},
            {"--name", "Buzz", "--title", "Ants - Red (Buzz)", "--grid", "2x2", "--cell", "2", "--audio-focus", "--no-lan", "--join", "127.0.0.1:4001", "--seat", "1"},
            {"--name", "Clover", "--title", "Ants - Blue (Clover)", "--grid", "2x2", "--cell", "3", "--audio-focus", "--no-lan", "--join", "127.0.0.1:4001", "--seat", "2"},
            {"--name", "Dot", "--title", "Ants - Black (Dot)", "--grid", "2x2", "--cell", "0", "--audio-focus", "--no-lan", "--join", "127.0.0.1:4001", "--seat", "3"}};
        for (const auto& args : rig) ASSERT_FALSE(parse(args).start_menu);
    } TEST_END();

    TEST_CASE("M10.1 The name lookup: an address is answered at once and never goes to a resolver, a name goes to the resolver on a worker thread (Pending until it answers), a failure carries its reason, cancel abandons a lookup whose late answer then goes nowhere, and a new lookup is not confused by the old one") {
        const auto wait_for = [](HostLookup& lookup) {
            for (int i = 0; i < 4000 && lookup.poll() == HostLookup::State::Pending; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            return lookup.state();
        };
        ASSERT_TRUE(HostLookup::is_numeric("127.0.0.1") && HostLookup::is_numeric("::1") && HostLookup::is_numeric("192.168.1.20") && HostLookup::is_numeric("fe80::1"));
        ASSERT_FALSE(HostLookup::is_numeric("localhost") || HostLookup::is_numeric("beta.playants.org") || HostLookup::is_numeric("example.org") || HostLookup::is_numeric("300.1.1.1") ||
                     HostLookup::is_numeric("not an address"));
        {   // an address: Done at once, the resolver is never asked
            const auto called = std::make_shared<std::atomic<int>>(0);
            const HostLookup::Resolver resolver = [called](const std::string&, std::string&, std::string&) {
                ++*called;
                return false;
            };
            HostLookup lookup;
            ASSERT_TRUE(lookup.state() == HostLookup::State::Idle);
            lookup.start("127.0.0.1", resolver);
            ASSERT_TRUE(lookup.state() == HostLookup::State::Done && lookup.poll() == HostLookup::State::Done && lookup.address() == "127.0.0.1");
            lookup.start("::1", resolver);
            ASSERT_TRUE(lookup.poll() == HostLookup::State::Done && lookup.address() == "::1");
            ASSERT_EQ(called->load(), 0);
        }
        {   // a name: Pending until the worker has answered, then Done with the address
            const auto gate = std::make_shared<std::atomic<bool>>(false);
            HostLookup lookup;
            lookup.start("play.example.test", [gate](const std::string& host, std::string& address, std::string&) {
                while (!*gate) std::this_thread::sleep_for(std::chrono::milliseconds(1));
                address = host == "play.example.test" ? "10.1.2.3" : "wrong host";
                return true;
            });
            ASSERT_TRUE(lookup.poll() == HostLookup::State::Pending);
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            ASSERT_TRUE(lookup.poll() == HostLookup::State::Pending);                          // the window would stay alive: poll returns at once
            *gate = true;
            ASSERT_TRUE(wait_for(lookup) == HostLookup::State::Done);
            ASSERT_EQ(lookup.address(), std::string("10.1.2.3"));
        }
        {   // a failure with its reason
            HostLookup lookup;
            lookup.start("nope.example.test", [](const std::string&, std::string&, std::string& error) {
                error = "no such name";
                return false;
            });
            ASSERT_TRUE(wait_for(lookup) == HostLookup::State::Failed);
            ASSERT_TRUE(lookup.error() == "no such name" && lookup.address().empty());
        }
        {   // cancel: Idle at once, the late answer goes nowhere (not even into the next lookup)
            const auto gate = std::make_shared<std::atomic<bool>>(false);
            const auto answers = std::make_shared<std::atomic<int>>(0);
            HostLookup lookup;
            lookup.start("slow.example.test", [gate, answers](const std::string&, std::string& address, std::string&) {
                while (!*gate) std::this_thread::sleep_for(std::chrono::milliseconds(1));
                ++*answers;
                address = "1.2.3.4";
                return true;
            });
            lookup.cancel();
            ASSERT_TRUE(lookup.state() == HostLookup::State::Idle && lookup.address().empty());
            *gate = true;
            for (int i = 0; i < 2000 && answers->load() == 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            ASSERT_EQ(answers->load(), 1);                                                     // the abandoned worker did answer ...
            ASSERT_TRUE(lookup.poll() == HostLookup::State::Idle && lookup.address().empty()); // ... into nothing
            // a lookup that replaces a running one: only the new answer counts
            const auto gate2 = std::make_shared<std::atomic<bool>>(false);
            lookup.start("first.example.test", [gate2](const std::string&, std::string& address, std::string&) {
                while (!*gate2) std::this_thread::sleep_for(std::chrono::milliseconds(1));
                address = "1.1.1.1";
                return true;
            });
            lookup.start("second.example.test", [](const std::string&, std::string& address, std::string&) {
                address = "2.2.2.2";
                return true;
            });
            ASSERT_TRUE(wait_for(lookup) == HostLookup::State::Done);
            *gate2 = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            ASSERT_TRUE(lookup.poll() == HostLookup::State::Done && lookup.address() == "2.2.2.2");
        }
        {   // the system's resolver finds this machine by its usual name, and what it finds is an address
            std::string address;
            std::string error;
            ASSERT_TRUE(HostLookup::system_resolve("localhost", address, error));
            ASSERT_TRUE(HostLookup::is_numeric(address));
            ASSERT_EQ(address, std::string("127.0.0.1"));                                      // a name that has both is answered with its IPv4 address: the game's server listens on IPv4
        }
    } TEST_END();


    // ---- the review fixes of the start menu -------------------------------------------------------------------------------------------------------------

    TEST_CASE("M10.2 The name lookup when its thread cannot be started: Failed at once with a reason of its own (start_failed), never an exception out of the menu; an address needs no thread; a launcher that works runs the job; the lookup polled while its worker answers is race free (run under TSan too)") {
        const HostLookup::Launcher none = [](std::function<void()>) { throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again)); };
        const HostLookup::Resolver found = [](const std::string&, std::string& address, std::string&) {
            address = "10.0.0.1";
            return true;
        };
        {
            HostLookup lookup;
            lookup.start("nothread.example.test", found, none);
            ASSERT_TRUE(lookup.state() == HostLookup::State::Failed && lookup.start_failed());
            ASSERT_EQ(lookup.error(), std::string("could not start the lookup"));
            ASSERT_TRUE(lookup.address().empty());
            ASSERT_TRUE(lookup.poll() == HostLookup::State::Failed);                         // (and it stays so: nothing is pending)
            lookup.cancel();
            ASSERT_TRUE(lookup.state() == HostLookup::State::Idle && !lookup.start_failed());
            lookup.start("127.0.0.1", found, none);                                           // an address is never looked up: no thread is needed
            ASSERT_TRUE(lookup.state() == HostLookup::State::Done && !lookup.start_failed() && lookup.address() == "127.0.0.1");
            lookup.start("nothread.example.test", found, none);                               // the next start begins from nothing
            ASSERT_TRUE(lookup.start_failed());
            int launched = 0;
            const HostLookup::Launcher own_thread = [&launched](std::function<void()> job) {
                ++launched;
                std::thread(std::move(job)).detach();
            };
            lookup.start("thread.example.test", found, own_thread);
            ASSERT_FALSE(lookup.start_failed());
            ASSERT_EQ(launched, 1);
            HostLookup::State state = HostLookup::State::Pending;
            for (int i = 0; i < 5000 && (state = lookup.poll()) == HostLookup::State::Pending; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            ASSERT_TRUE(state == HostLookup::State::Done && lookup.address() == "10.0.0.1");
        }
        {   // the main thread polls in a tight loop while the worker stores its answer: poll() takes the lock (without it ThreadSanitizer reports the race on the state)
            for (int round = 0; round < 300; ++round) {
                HostLookup lookup;
                lookup.start("race.example.test", found);
                HostLookup::State state = HostLookup::State::Pending;
                for (long spins = 0; spins < 50000000L && (state = lookup.poll()) == HostLookup::State::Pending; ++spins) {
                }
                ASSERT_TRUE(state == HostLookup::State::Done && lookup.address() == "10.0.0.1");
            }
        }
    } TEST_END();

    TEST_CASE("M11.1 Enter pressed twice in a hurry acts once: a new panel preselects the first INPUT (never Continue, Host or Cancel), and Enter and Space within StartMenu::kSettleMs of a panel's appearing do nothing, Esc does not quit from the first panel then; a held key never acts") {
        // Single player: Enter on the entry opens the panel; the second Enter must not start the game (Continue used to be selected) and must not change a row either
        {
            Rig r;
            r.key(SDLK_RETURN);
            ASSERT_EQ(r.menu.panel(), MenuPanel::Single);
            ASSERT_EQ(r.menu.selected(), MenuId::Seat1);                                   // the first row, not Continue
            r.quick_key(SDLK_RETURN);
            ASSERT_FALSE(r.menu.has_request());
            ASSERT_EQ(r.menu.seat(1), SeatChoice::Empty);                                  // (nor does it cycle the row it lands on)
            r.quick_key(SDLK_SPACE);
            ASSERT_FALSE(r.menu.has_request());
            ASSERT_EQ(r.menu.seat(1), SeatChoice::Empty);
            r.menu.update(0.29f);                                                          // 290 ms: still the same gesture
            r.quick_key(SDLK_KP_ENTER);
            ASSERT_EQ(r.menu.seat(1), SeatChoice::Empty);
            r.menu.update(0.02f);                                                          // 310 ms: a person's own press
            r.quick_key(SDLK_RETURN);
            ASSERT_EQ(r.menu.seat(1), SeatChoice::Easy);
            r.quick_key(SDLK_SPACE);
            ASSERT_EQ(r.menu.seat(1), SeatChoice::Medium);
            r.key(SDLK_RETURN, 0, true);                                                   // a held key never acts, however old the panel is
            ASSERT_EQ(r.menu.seat(1), SeatChoice::Medium);
            r.key(SDLK_DOWN);
            r.key(SDLK_DOWN);
            r.key(SDLK_DOWN);
            ASSERT_EQ(r.menu.selected(), MenuId::Continue);                                // Continue is one Down away from the last row, and Enter on it still starts
            r.key(SDLK_RETURN);
            ASSERT_TRUE(r.take().type == MenuRequest::Type::Single);
        }
        // Host an online match: the Host panel's selection is the map, so the second Enter makes no room
        {
            Rig r;
            r.key(SDLK_DOWN);
            r.key(SDLK_DOWN);
            r.key(SDLK_RETURN);
            ASSERT_EQ(r.menu.panel(), MenuPanel::Host);
            ASSERT_EQ(r.menu.selected(), MenuId::HostMap);
            r.quick_key(SDLK_RETURN);
            ASSERT_FALSE(r.menu.has_request());
            ASSERT_EQ(r.menu.panel(), MenuPanel::Host);
            ASSERT_EQ(r.element(MenuId::HostMap).value, std::string("Treasure"));          // the map is not cycled by it (the panel opens on the default map: Treasure)
            r.key(SDLK_RETURN);
            ASSERT_EQ(r.element(MenuId::HostMap).value, std::string("Islands"));           // a press of its own is
        }
        // Join: the Enter that joins is followed by a second one while "Connecting" shows: it must not cancel the attempt (Cancel used to be selected)
        {
            Rig r(0, "Dave");
            r.to_panel(MenuId::JoinWithCode);
            r.type("room-one");
            r.key(SDLK_RETURN);
            ASSERT_TRUE(r.take().type == MenuRequest::Type::Join);
            ASSERT_EQ(r.menu.panel(), MenuPanel::Connecting);
            r.quick_key(SDLK_RETURN);
            ASSERT_FALSE(r.menu.has_request());
            ASSERT_EQ(r.menu.panel(), MenuPanel::Connecting);
            r.quick_key(SDLK_ESCAPE);                                                      // Esc cancels at once (it does not wait for the panel to settle)
            ASSERT_TRUE(r.take().type == MenuRequest::Type::Cancel);
            r.menu.connection_cancelled();                                                 // and once more, to see that Enter and Space do nothing here however late they come
            r.key(SDLK_RETURN);
            ASSERT_TRUE(r.take().type == MenuRequest::Type::Join);
            ASSERT_EQ(r.menu.panel(), MenuPanel::Connecting);
            r.key(SDLK_RETURN);                                                            // nothing is selected on this panel
            r.key(SDLK_SPACE);
            ASSERT_FALSE(r.menu.has_request());
        }
        // Esc, Esc: leaves the panel and does not quit the program with the second press
        {
            Rig r;
            r.to_panel(MenuId::Single);
            r.quick_key(SDLK_ESCAPE);                                                      // (the panel is new; Esc goes back from every panel but the first)
            ASSERT_EQ(r.menu.panel(), MenuPanel::Main);
            r.quick_key(SDLK_ESCAPE);
            ASSERT_FALSE(r.menu.has_request());                                            // the same gesture: no Quit
            r.menu.update(0.31f);
            r.quick_key(SDLK_ESCAPE);
            ASSERT_TRUE(r.take().type == MenuRequest::Type::Quit);
        }
        // a failure and a room arrive on their own time: an Enter that comes right then is not a decision about the new panel
        {
            Rig r(0, "Dave");
            r.to_panel(MenuId::JoinWithCode);
            r.type("room-one");
            r.key(SDLK_RETURN);
            r.take();
            r.menu.connection_failed("There is no room with that code.");
            ASSERT_EQ(r.menu.selected(), MenuId::Join);
            r.quick_key(SDLK_RETURN);
            ASSERT_FALSE(r.menu.has_request());                                            // (no new attempt)
            r.key(SDLK_RETURN);
            ASSERT_TRUE(r.take().type == MenuRequest::Type::Join);
            r.menu.show_room("demo-tiny-2p-abcdef", 1, 2);
            r.quick_key(SDLK_RETURN);
            ASSERT_FALSE(r.menu.has_request());
            r.key(SDLK_RETURN);
            ASSERT_TRUE(r.take().type == MenuRequest::Type::EnterRoom);
        }
        // the same on the Host panel's failure and on the first panel after a lost game
        {
            Rig r;
            r.to_panel(MenuId::HostOnline);
            for (int down = 0; down < 6; ++down) r.key(SDLK_DOWN);                         // (the players, the three seats after the leader's, the teams, the name: Enter in it hosts)
            ASSERT_EQ(r.menu.selected(), MenuId::HostName);
            r.key(SDLK_RETURN);
            ASSERT_TRUE(r.take().type == MenuRequest::Type::Host);
            r.menu.connection_failed("The server is busy.");
            r.quick_key(SDLK_RETURN);
            ASSERT_FALSE(r.menu.has_request());
            r.menu.show_main("The connection was lost.");
            r.quick_key(SDLK_ESCAPE);
            ASSERT_FALSE(r.menu.has_request());
            r.quick_key(SDLK_RETURN);
            ASSERT_EQ(r.menu.panel(), MenuPanel::Main);                                    // (the entry that was selected does not open either)
        }
        // the menu as it first appears: the Enter or Esc that ended the loading screen is not an answer to it
        {
            StartMenu m;
            m.set_settings(MenuSettings{});
            m.show_main();
            m.on_key(SDLK_ESCAPE, 0, false);
            ASSERT_FALSE(m.has_request());
            m.on_key(SDLK_RETURN, 0, false);
            ASSERT_EQ(m.panel(), MenuPanel::Main);
            m.update(StartMenu::kSettleMs / 1000.0f + 0.001f);
            ASSERT_TRUE(m.settled());
            m.on_key(SDLK_RETURN, 0, false);
            ASSERT_EQ(m.panel(), MenuPanel::Single);
        }
        // the mouse is not slowed by it (a click is two events, and its rest is the application's rule: test_start_menu_app)
        {
            Rig r;
            r.click(MenuId::Single);
            r.click(MenuId::Continue);
            ASSERT_TRUE(r.take().type == MenuRequest::Type::Single);
        }
    } TEST_END();

    TEST_CASE("M11.2 The Join panel's code field after a Back or a finished game: the text that is there is selected and typing replaces it (it used to append), like arriving by Tab; the first empty field has the focus") {
        Rig r(0, "Dave");
        r.to_panel(MenuId::JoinWithCode);
        ASSERT_EQ(r.menu.selected(), MenuId::Code);                                        // the name is there: the code is the first empty field
        ASSERT_FALSE(r.element(MenuId::Code).all_selected);                                // (empty: nothing to select)
        r.type("room-one");
        r.key(SDLK_ESCAPE);                                                                // Back ...
        r.click(MenuId::JoinWithCode);                                                     // ... and in again: the old code stands there, selected
        ASSERT_EQ(r.menu.selected(), MenuId::Code);
        ASSERT_EQ(r.menu.code(), std::string("room-one"));
        ASSERT_TRUE(r.element(MenuId::Code).all_selected);
        r.type("room-two");
        ASSERT_EQ(r.menu.code(), std::string("room-two"));                                 // replaced, not "room-oneroom-two"
        // by the keyboard it is the same, and so is the way back after a game (show_main with a notice) and after a failed attempt
        r.key(SDLK_ESCAPE);
        r.key(SDLK_RETURN);                                                                // (the entry that was left is selected again)
        ASSERT_EQ(r.menu.panel(), MenuPanel::Join);
        ASSERT_TRUE(r.element(MenuId::Code).all_selected);
        r.menu.show_main("The connection was lost.");
        r.click(MenuId::JoinWithCode);
        ASSERT_TRUE(r.element(MenuId::Code).all_selected);
        r.type("room-three");
        ASSERT_EQ(r.menu.code(), std::string("room-three"));
        r.key(SDLK_UP);                                                                    // Tab and Up agree with the arrival: the name's text is selected too
        ASSERT_TRUE(r.element(MenuId::Name).all_selected);
        r.key(SDLK_TAB);
        ASSERT_TRUE(r.element(MenuId::Code).all_selected);
        // no name yet: the name is the first empty field, and the code that is there is not the focus
        Rig e(0, "");
        e.to_panel(MenuId::JoinWithCode);
        ASSERT_EQ(e.menu.selected(), MenuId::Name);
        // a failed attempt keeps what was typed and lights the button, as before
        Rig f(0, "Dave");
        f.to_panel(MenuId::JoinWithCode);
        f.type("room-x");
        f.key(SDLK_RETURN);
        f.take();
        f.menu.connection_failed("Nope.");
        ASSERT_EQ(f.menu.selected(), MenuId::Join);
        ASSERT_EQ(f.menu.code(), std::string("room-x"));
        f.key(SDLK_ESCAPE);
        f.click(MenuId::JoinWithCode);
        f.type("room-y");
        ASSERT_EQ(f.menu.code(), std::string("room-y"));
    } TEST_END();

    TEST_CASE("M11.3 A character that the fields cannot take is said, never dropped in silence: 'Jose-acute CJK' leaves 'Jos ' and the line 'Only letters A-Z, digits and simple punctuation.'; so does a paste; a line end of a paste and typing that is all good say nothing; the next good key takes the line away") {
        Rig r(0, "");
        r.to_panel(MenuId::JoinWithCode);
        ASSERT_EQ(r.menu.selected(), MenuId::Name);
        r.type("Jos\xC3\xA9 \xE6\x9D\x8E");                                                // "José 李" as SDL's text input delivers it (UTF-8)
        ASSERT_EQ(r.menu.name(), std::string("Jos "));
        ASSERT_EQ(r.menu.message(), std::string(StartMenu::kRefusedCharsText));
        ASSERT_TRUE(has_text(r.menu.elements(), "Only letters A-Z, digits and simple punctuation"));
        r.type("e");                                                                       // the next key takes the line away
        ASSERT_EQ(r.menu.name(), std::string("Jos e"));
        ASSERT_TRUE(r.menu.message().empty());
        r.type("abc");                                                                     // ASCII only: no line
        ASSERT_TRUE(r.menu.message().empty());
        r.type("\xC3\xA9");                                                                // only a refused character: nothing typed, the line shows
        ASSERT_EQ(r.menu.name(), std::string("Jos eabc"));
        ASSERT_EQ(r.menu.message(), std::string(StartMenu::kRefusedCharsText));
        r.key(SDLK_TAB);                                                                   // the code field takes the same
        r.key(SDLK_BACKSPACE);
        r.type("caf\xC3\xA9");
        ASSERT_EQ(r.menu.code(), std::string("caf"));
        ASSERT_EQ(r.menu.message(), std::string(StartMenu::kRefusedCharsText));
        r.key(SDLK_BACKSPACE);                                                             // Backspace takes the line away too
        ASSERT_TRUE(r.menu.message().empty());
        r.type("\x01");                                                                    // a control character is refused as well
        ASSERT_EQ(r.menu.message(), std::string(StartMenu::kRefusedCharsText));
        // a paste: an accent in the clipboard says it, a line end or a tab does not
        r.clipboard.text = "demo-ti\xC3\xA9ny";
        r.key(SDLK_a, KMOD_CTRL);
        r.key(SDLK_v, KMOD_CTRL);
        ASSERT_EQ(r.menu.code(), std::string("demo-tiny"));
        ASSERT_EQ(r.menu.message(), std::string(StartMenu::kRefusedCharsText));
        r.clipboard.text = "room-9\r\n";
        r.key(SDLK_a, KMOD_CTRL);
        r.key(SDLK_v, KMOD_CTRL);
        ASSERT_EQ(r.menu.code(), std::string("room-9"));
        ASSERT_TRUE(r.menu.message().empty());
        r.clipboard.text = "\t\n";
        r.key(SDLK_v, KMOD_CTRL);
        ASSERT_EQ(r.menu.code(), std::string("room-9"));
        ASSERT_TRUE(r.menu.message().empty());
        // the Host panel's name field too (its line stands where the server's note was)
        Rig h(0, "");
        h.to_panel(MenuId::HostOnline);
        for (int down = 0; down < 6; ++down) h.key(SDLK_DOWN);                             // (map, players, the three seats after the leader's, the teams, then the name)
        ASSERT_EQ(h.menu.selected(), MenuId::HostName);
        h.type("\xE6\x9D\x8E");
        ASSERT_EQ(h.menu.name(), std::string(""));
        ASSERT_TRUE(has_text(h.menu.elements(), "Only letters A-Z, digits and simple punctuation"));
        // the line fits its box in the drawing (it is a notice of one line at 18 px)
        Recorder rec;
        render_start_menu(rec, archive(), h.menu);
        bool whole = false;
        for (const Recorder::Text& t : rec.texts) whole = whole || t.text == StartMenu::kRefusedCharsText;
        ASSERT_TRUE(whole);
    } TEST_END();

    TEST_CASE("M11.4 What the panels say: the single-player panel says what its seats mean in both states (nobody: the original's game, the other colours stand still; a bot: no fog of war, empty seats have no ants), a bot choice that is stored for the player's own seat is not a bot; the copy failure is one line that is not cut") {
        Rig r;
        r.to_panel(MenuId::Single);
        ASSERT_TRUE(has_text(r.menu.elements(), "No bots: the original single-player game (the other colours stand still)."));
        ASSERT_FALSE(has_text(r.menu.elements(), "Bots play without fog of war"));
        r.click(MenuId::Seat2);
        ASSERT_TRUE(has_text(r.menu.elements(), "Bots play without fog of war. Empty seats have no ants."));
        ASSERT_FALSE(has_text(r.menu.elements(), "No bots:"));
        r.click(MenuId::Seat2);
        r.click(MenuId::Seat2);
        r.click(MenuId::Seat2);
        ASSERT_TRUE(has_text(r.menu.elements(), "No bots:"));                              // back to nobody: back to the other line
        // a stored choice for the own seat: no row, no bot, the panel says "No bots" (the line follows the bots that will play, not the stored words)
        for (uint8_t own = 0; own < 4; ++own) {
            Rig s(own);
            MenuSettings st;
            st.name = "Player";
            st.seats = {SeatChoice::Empty, SeatChoice::Empty, SeatChoice::Empty, SeatChoice::Empty};
            st.seats[own] = SeatChoice::Hard;
            s.menu.set_settings(st);
            ASSERT_FALSE(s.menu.any_bot());
            ASSERT_TRUE(s.menu.bots().empty());
            s.to_panel(MenuId::Single);
            ASSERT_TRUE(has_text(s.menu.elements(), "No bots:") && !has_text(s.menu.elements(), "Bots play without"));
        }
        // both lines fit their box at the font's estimate (no '...'), and at a font twice as wide they are cut to their box and not beyond it
        for (const bool with_bot : {false, true}) {
            Rig d;
            d.to_panel(MenuId::Single);
            if (with_bot) d.click(MenuId::Seat1);
            Recorder rec;
            render_start_menu(rec, archive(), d.menu);
            const std::string want = with_bot ? StartMenu::kBotsLine : StartMenu::kNoBotsLine;
            std::string drawn;
            for (const Recorder::Text& t : rec.texts) {
                if (t.size == FontSize::Px18 && (want.find(t.text) != std::string::npos || t.text.find("...") != std::string::npos) && t.y >= 290 && t.y < 332) {
                    if (!drawn.empty()) drawn += " ";                                       // (a wrapped line ends where the blank was)
                    drawn += t.text;
                }
            }
            ASSERT_EQ(drawn, want);                                                        // all of it, however many lines it takes
        }
        // the copy failure: one line that says what to do, the code stands in full in its own box above it, and neither is cut
        Rig bad;
        bad.clipboard.writable = false;
        bad.menu.show_room("demo-gauntlet-4p-abcdef", 1, 4);
        bad.click(MenuId::Copy);
        ASSERT_TRUE(has_text(bad.menu.elements(), "Copy failed. Write down the code above."));
        ASSERT_EQ(element_text(bad.menu.elements(), MenuKind::Code), std::string("demo-gauntlet-4p-abcdef"));
        Recorder rc;
        render_start_menu(rc, archive(), bad.menu);
        bool line_whole = false;
        bool code_whole = false;
        for (const Recorder::Text& t : rc.texts) {
            line_whole = line_whole || t.text == "Copy failed. Write down the code above.";
            code_whole = code_whole || t.text == "demo-gauntlet-4p-abcdef";
        }
        ASSERT_TRUE(line_whole && code_whole);
    } TEST_END();

    TEST_CASE("M11.5 A release belongs to a press that began on the panel that is up: a button that is held while the panel changes under it does nothing at its release, over whatever button of the new panel lies there") {
        Rig r;
        const MenuElement quit = r.element(MenuId::Quit);
        const int32_t x = quit.rect.x + quit.rect.w / 2;
        const int32_t y = quit.rect.y + quit.rect.h / 2;
        r.menu.on_mouse_move(x, y);
        ASSERT_TRUE(r.menu.on_mouse_down(x, y, SDL_BUTTON_LEFT));
        r.menu.show_room("demo-tiny-2p-abcdef", 1, 2);                                    // the room appears while the button is down ...
        MenuElement copy;
        ASSERT_TRUE(r.menu.find_element(MenuId::Copy, copy) && copy.rect.contains(x, y));  // ... and its Copy button lies where Quit was
        ASSERT_FALSE(r.element(MenuId::Copy).pressed);
        r.menu.on_mouse_up(x, y, SDL_BUTTON_LEFT);
        ASSERT_TRUE(r.clipboard.writes.empty());                                           // nothing was copied
        ASSERT_FALSE(r.menu.has_request());                                                // nothing was asked for
        r.click(MenuId::Copy);                                                             // a click that begins on the panel does
        ASSERT_EQ(r.clipboard.writes.size(), static_cast<size_t>(1));
        // the same with a button that the next panel has too, at the same place: the Join panel's Back is held while a room that was left puts the player on the Host panel, whose Back lies under it
        // (the Host panel of a room of three players: the rows above Host and Back are as many as the room has, and only this one has its Back under the Join panel's)
        Rig j(0, "Dave");
        j.to_panel(MenuId::HostOnline);
        j.click(MenuId::HostPlayers);
        j.click(MenuId::HostPlayers);
        ASSERT_EQ(j.menu.settings().host_players, 3);
        j.to_panel(MenuId::JoinWithCode);
        const MenuElement back = j.element(MenuId::Back);
        const int32_t bx = back.rect.x + back.rect.w / 2;
        const int32_t by = back.rect.y + back.rect.h - 4;
        j.menu.on_mouse_move(bx, by);
        ASSERT_TRUE(j.menu.on_mouse_down(bx, by, SDL_BUTTON_LEFT));
        j.menu.room_left("The connection to the server was lost.");                       // the Host panel appears under the held button
        MenuElement host_back;
        ASSERT_TRUE(j.menu.find_element(MenuId::Back, host_back) && host_back.rect.contains(bx, by));
        j.menu.on_mouse_up(bx, by, SDL_BUTTON_LEFT);
        ASSERT_EQ(j.menu.panel(), MenuPanel::Host);                                        // the release did not press the other panel's Back
        ASSERT_EQ(j.menu.message(), std::string("The connection to the server was lost."));
        j.click(MenuId::Back);                                                             // a click that begins on this panel does
        ASSERT_EQ(j.menu.panel(), MenuPanel::Main);
    } TEST_END();


    // ---- the first panel's "Rejoin your match (CODE)" (a key of a running match is in the application's store, rejoin_store.hpp) ----

    TEST_CASE("M12.1 Rejoin offer: with none the first panel is exactly what it was; with one there is one more button, \"Rejoin your match (CODE)\", above Single player in the gap under the title, and nothing else of the panel moves (the default selection stays Single player: a panel never preselects a button that acts)") {
        Rig plain;
        const std::vector<MenuElement> before = plain.menu.elements();
        ASSERT_FALSE(plain.menu.rejoin().has_value());
        ASSERT_FALSE(plain.exists(MenuId::Rejoin));
        Rig r;
        r.menu.set_rejoin(RejoinOffer{"MEET-1", 2, ServerAddress{"play.example.org", 4001}});
        r.menu.show_main();
        const std::vector<MenuElement> after = r.menu.elements();
        ASSERT_EQ(after.size(), before.size() + 1);
        size_t found = 0;
        for (const MenuElement& e : after) {
            if (e.id == MenuId::Rejoin) {
                ++found;
                ASSERT_EQ(e.text, std::string("Rejoin your match (MEET-1)"));
                ASSERT_TRUE(e.kind == MenuKind::Button && e.font == FontSize::Px18 && e.centered);
                continue;
            }
            bool same = false;                                                          // every other element is where it was, with the same words
            for (const MenuElement& b : before) {
                if (b.id == e.id && b.kind == e.kind && b.text == e.text && b.rect.x == e.rect.x && b.rect.y == e.rect.y && b.rect.w == e.rect.w && b.rect.h == e.rect.h) same = true;
            }
            ASSERT_TRUE(same);
        }
        ASSERT_EQ(found, size_t{1});
        const MenuElement rejoin = r.element(MenuId::Rejoin);
        const MenuElement single = r.element(MenuId::Single);
        MenuElement title;
        for (const MenuElement& e : after) {
            if (e.kind == MenuKind::Title) title = e;
        }
        ASSERT_TRUE(rejoin.rect.y >= title.rect.y + title.rect.h && rejoin.rect.y + rejoin.rect.h <= single.rect.y);       // between the title and Single player
        ASSERT_TRUE(rejoin.rect.x + rejoin.rect.w / 2 == single.rect.x + single.rect.w / 2);                                 // centred over it
        const std::vector<MenuId> expected = {MenuId::Rejoin, MenuId::Single, MenuId::JoinWithCode, MenuId::HostOnline, MenuId::Quit};
        ASSERT_TRUE(control_ids(r.menu) == expected);
        ASSERT_EQ(r.menu.selected(), MenuId::Single);
        // the 16:9 menu moves the button with the group that the others are in (+160, +30)
        r.menu.set_wide_layout(true);
        const MenuElement wide = r.element(MenuId::Rejoin);
        ASSERT_TRUE(wide.rect.x == rejoin.rect.x + StartMenu::kWideDx && wide.rect.y == rejoin.rect.y + StartMenu::kWideMiddleDy);
        ASSERT_TRUE(r.element(MenuId::Single).rect.y == single.rect.y + StartMenu::kWideMiddleDy);
        // taking the offer away gives the panel back
        r.menu.set_wide_layout(false);
        r.menu.set_rejoin(std::nullopt);
        ASSERT_FALSE(r.exists(MenuId::Rejoin));
        ASSERT_EQ(r.menu.elements().size(), before.size());
    } TEST_END();

    TEST_CASE("M12.2 Rejoin offer: Up from Single player selects it (Down from Quit wraps to it), Enter or Space on it asks the application to rejoin that room, seat and server (the panel's name, cleaned; \"Player\" when it is not a name that the server would take) and the panel says \"Rejoining your match in room CODE...\"; the settling rule holds for it like for every button") {
        Rig r(0, "Dave");
        r.menu.set_rejoin(RejoinOffer{"MEET-1", 2, ServerAddress{"play.example.org", 4444}});
        r.menu.show_main();
        r.key(SDLK_UP);
        ASSERT_EQ(r.menu.selected(), MenuId::Rejoin);
        r.key(SDLK_UP);
        ASSERT_EQ(r.menu.selected(), MenuId::Quit);                                      // (the wrap)
        r.key(SDLK_DOWN);
        ASSERT_EQ(r.menu.selected(), MenuId::Rejoin);
        r.menu.show_main();                                                              // a fresh panel (the first panel keeps the selection that it had): Enter in a hurry does nothing
        ASSERT_EQ(r.menu.selected(), MenuId::Rejoin);
        r.quick_key(SDLK_RETURN);
        ASSERT_FALSE(r.menu.has_request());
        ASSERT_EQ(r.menu.panel(), MenuPanel::Main);
        r.key(SDLK_RETURN);
        ASSERT_EQ(r.menu.panel(), MenuPanel::Connecting);
        ASSERT_TRUE(r.menu.has_request());
        const MenuRequest q = r.take();
        ASSERT_TRUE(q.type == MenuRequest::Type::Rejoin && q.room == "MEET-1" && q.seat == 2 && q.name == "Dave");
        ASSERT_TRUE(q.server.host == "play.example.org" && q.server.port == 4444);
        ASSERT_TRUE(has_text(r.menu.elements(), "Rejoining your match in room MEET-1..."));
        ASSERT_EQ(r.menu.connect_origin(), MenuPanel::Main);
        // Space acts like Enter on a button; a name that the server would rename (a bot's) is not sent
        Rig s(0, "Bot (Hard)");
        s.menu.set_rejoin(RejoinOffer{"R2", 0, ServerAddress{}});
        s.menu.show_main();
        s.key(SDLK_UP);
        s.key(SDLK_SPACE);
        const MenuRequest q2 = s.take();
        ASSERT_TRUE(q2.type == MenuRequest::Type::Rejoin && q2.name == "Player" && q2.room == "R2" && q2.seat == 0);
        // nothing offered, nothing to ask: Up from Single player is Quit, and the other entries ask nothing
        Rig n;
        n.key(SDLK_UP);
        ASSERT_EQ(n.menu.selected(), MenuId::Quit);
    } TEST_END();

    TEST_CASE("M12.3 Rejoin offer: a click is a press and a release on the button (the click sound at the press); leaving the button before the release cancels it; the clicks of the other entries are what they were") {
        Rig r;
        r.menu.set_rejoin(RejoinOffer{"MEET-1", 1, ServerAddress{}});
        r.menu.show_main();
        const MenuElement b = r.element(MenuId::Rejoin);
        const int32_t x = b.rect.x + b.rect.w / 2;
        const int32_t y = b.rect.y + b.rect.h / 2;
        r.menu.on_mouse_move(x, y);
        ASSERT_EQ(r.menu.selected(), MenuId::Rejoin);
        ASSERT_TRUE(r.menu.on_mouse_down(x, y, SDL_BUTTON_LEFT));
        ASSERT_TRUE(r.element(MenuId::Rejoin).pressed);
        ASSERT_EQ(r.sounds.count(sim::SoundID::ButtonClick), 1);
        ASSERT_FALSE(r.menu.has_request());                                              // the action is at the release
        const MenuElement single = r.element(MenuId::Single);
        r.menu.on_mouse_move(single.rect.x + 5, single.rect.y + 5);                      // the pointer leaves the button: it is no longer pressed
        r.menu.on_mouse_up(single.rect.x + 5, single.rect.y + 5, SDL_BUTTON_LEFT);
        ASSERT_FALSE(r.menu.has_request());
        ASSERT_EQ(r.menu.panel(), MenuPanel::Main);
        r.click(MenuId::Rejoin);
        ASSERT_EQ(r.menu.panel(), MenuPanel::Connecting);
        const MenuRequest q = r.take();
        ASSERT_TRUE(q.type == MenuRequest::Type::Rejoin && q.room == "MEET-1" && q.seat == 1);
        r.menu.connection_cancelled();
        r.click(MenuId::Single);                                                         // the others work as they did
        ASSERT_EQ(r.menu.panel(), MenuPanel::Single);
    } TEST_END();

    TEST_CASE("M12.4 Rejoin offer: a rejoin that fails comes back to the FIRST panel with the reason (not to Join or Host), a cancel comes back to it with nothing said, and Esc on \"Rejoining...\" cancels; the offer is whatever the owner gives next") {
        Rig r;
        r.menu.set_rejoin(RejoinOffer{"MEET-1", 2, ServerAddress{}});
        r.menu.show_main();
        r.key(SDLK_UP);
        r.key(SDLK_RETURN);
        r.take();
        r.menu.connection_failed("Sorry, you have been dropped from the game.");
        ASSERT_EQ(r.menu.panel(), MenuPanel::Main);
        ASSERT_EQ(r.menu.message(), std::string("Sorry, you have been dropped from the game."));
        ASSERT_TRUE(has_text(r.menu.elements(), "Sorry, you have been dropped from the game."));
        ASSERT_TRUE(r.exists(MenuId::Rejoin));                                           // (the owner takes the offer away when the key was let go of)
        r.menu.set_rejoin(std::nullopt);
        ASSERT_FALSE(r.exists(MenuId::Rejoin));
        // Cancel and Esc
        Rig c;
        c.menu.set_rejoin(RejoinOffer{"MEET-1", 2, ServerAddress{}});
        c.menu.show_main();
        c.key(SDLK_UP);
        c.key(SDLK_RETURN);
        c.take();
        ASSERT_EQ(c.menu.panel(), MenuPanel::Connecting);
        c.key(SDLK_ESCAPE);
        ASSERT_EQ(c.take().type, MenuRequest::Type::Cancel);
        c.menu.connection_cancelled();
        ASSERT_EQ(c.menu.panel(), MenuPanel::Main);
        ASSERT_TRUE(c.menu.message().empty());
        ASSERT_TRUE(c.exists(MenuId::Rejoin) && c.exists(MenuId::Single));
        // a Join after it still fails back to Join (the origin is the panel that asked)
        c.to_panel(MenuId::JoinWithCode);
        c.type("Dave");
        c.key(SDLK_DOWN);
        c.type("ROOM-9");
        c.key(SDLK_RETURN);
        c.take();
        c.menu.connection_failed("The room is full.");
        ASSERT_EQ(c.menu.panel(), MenuPanel::Join);
        // the selection that stood on the button does not stay on a button that is gone
        Rig d;
        d.menu.set_rejoin(RejoinOffer{"MEET-1", 2, ServerAddress{}});
        d.menu.show_main();
        d.key(SDLK_UP);
        ASSERT_EQ(d.menu.selected(), MenuId::Rejoin);
        d.menu.set_rejoin(std::nullopt);
        ASSERT_EQ(d.menu.selected(), MenuId::Single);
        d.menu.show_main();
        ASSERT_EQ(d.menu.selected(), MenuId::Single);
    } TEST_END();

    std::cout << "\nstart menu model: " << g_test_count << " tests, " << g_assert_count << " assertions, " << g_test_failures << " failures\n";
    if (g_test_count == 0) {                                      // (a misspelt or forgotten filter must not turn the suite green)
        std::cout << "\n no test ran: the filter ANTS_TEST_FILTER matches no test of this suite\n";
        return 1;
    }
    return g_test_failures == 0 ? 0 : 1;
}
