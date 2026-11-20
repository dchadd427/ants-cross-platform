// A phone's keyboard for the match's chat box (Application::web_chat_*, src/ants_app/application_touch.cpp; the page's side is tests/scripts/test_web_chat.py):
//   * the box is where the HUD draws it (the log and the input box, 143 px wide), in the canvas's pixels, in every layout; it takes text only in a match, with chat on and no dialog open;
//   * what the page types lands in the game's own chat line (printable ASCII, at most 100 characters), Backspace and clear edit it, and what is not text is refused;
//   * Enter is a key press through the event queue: the line is sent and the box empties, exactly as for a keyboard (the HUD's rules: nothing when the box is empty);
//   * a dialog or the option "Participate In Chat" off closes the box to all of it, and a typed line stays what it was.
//   * the catch-up screen and the results refuse text too (the recording's clause is pinned in test_replay_app, RA7.1);
//   * the options window (open: it has the screen) has the keyboard's boxes instead: its four quick chat fields, where the window's own press would focus them, one of them with the focus, what the
//     page types shown in it at once and written to the profile by the commit (one write for one edit), the chat line left alone, Enter closing the window as a keyboard's does.
// Usage: test_web_chat. Exit code 0 when every check passes.
#include <SDL.h>

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>

#include "zoom_scene.hpp"

#include "ants_app/application.hpp"
#include "ants_app/hud.hpp"

using namespace ants;
using namespace ants::app;
using namespace zoomtest;

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::fprintf(stderr, "  FAIL: %s\n", what.c_str());
    }
}

void type(Application& app, const std::string& text) {
    for (const char c : text) app.web_chat_do(1, static_cast<unsigned char>(c));
}

bool log_has(Application& app, const std::string& text) {
    for (const std::string& line : app.hud().get_chat_log()) {
        if (line.find(text) != std::string::npos) return true;
    }
    return false;
}

void test_box() {
    std::printf("[box] where a tap opens the keyboard\n");
    for (const Aspect aspect : {Aspect::Wide16x9, Aspect::Classic4x3}) {
        AppRig rig(aspect, 1.0f, false, std::string(), true, true);
        check(rig.ok, "the match is up");
        if (!rig.ok) continue;
        Application& app = rig.app;
        const LayoutRect box = app.web_chat_box();
        const LayoutRect log = app.layout().chat_view();
        check(app.web_chat_available(), "a match with chat on takes text");
        check(box.x == app.picture().x + app.layout().right(479) && box.w == 143, "the box starts at the input box's left edge (479 right-anchored) and is 143 wide");
        check(box.y == app.picture().y + log.y, "it starts at the top of the chat log");
        check(box.bottom() == app.picture().y + app.layout().bottom(437), "it ends below the input box (437, bottom-anchored)");
        check(box.x >= 0 && box.right() <= app.canvas().width && box.bottom() <= app.canvas().height, "it is inside the canvas");
        check(box.contains(box.x + 70, box.bottom() - 8), "the input box itself is inside it");
    }
}

void test_typing() {
    std::printf("[typing] the page's characters land in the game's chat line\n");
    AppRig rig(Aspect::Wide16x9, 1.0f, false, std::string(), true, true);
    check(rig.ok, "the match is up");
    if (!rig.ok) return;
    Application& app = rig.app;
    type(app, "Rush the base!");
    check(app.hud().get_chat_input() == "Rush the base!", "characters are added to the line");
    check(app.web_chat_do(2, 0) && app.hud().get_chat_input() == "Rush the base", "Backspace takes one off");
    check(!app.web_chat_do(1, 7) && !app.web_chat_do(1, 127) && !app.web_chat_do(1, 233) && !app.web_chat_do(1, 0x1F600) && app.hud().get_chat_input() == "Rush the base", "control codes and non-ASCII are refused");
    check(!app.web_chat_do(9, 0), "an operation that does not exist does nothing");
    check(app.web_chat_do(0, 0) && app.hud().get_chat_input().empty(), "clear empties it");
    check(app.web_chat_do(2, 0) && app.hud().get_chat_input().empty(), "Backspace on an empty line is nothing");
    type(app, std::string(130, 'x'));
    check(app.hud().get_chat_input().size() == HUD::kChatInputMax, "the line holds 100 characters, as the HUD's");
}

void test_enter() {
    std::printf("[enter] Enter sends the line as a keyboard's does\n");
    AppRig rig(Aspect::Wide16x9, 1.0f, false, std::string(), true, true);
    check(rig.ok, "the match is up");
    if (!rig.ok) return;
    Application& app = rig.app;
    const size_t before = app.hud().get_chat_log().size();
    app.web_chat_do(3, 0);
    app.run_frame_with_delta(0.016f);
    check(app.hud().get_chat_log().size() == before, "Enter on an empty line sends nothing (the chat log has the same lines)");
    type(app, "Rush the base!");
    check(app.web_chat_do(3, 0), "Enter is taken");
    check(app.hud().get_chat_input() == "Rush the base!", "the line is sent by the frame that reads the key, not at once");
    app.run_frame_with_delta(0.016f);
    check(app.hud().get_chat_input().empty(), "the frame sent it and emptied the box");
    check(log_has(app, "Rush the base!"), "the line is in the chat log");
}

void test_closed() {
    std::printf("[closed] a dialog or chat off closes the box\n");
    AppRig rig(Aspect::Wide16x9, 1.0f, false, std::string(), true, true);
    check(rig.ok, "the match is up");
    if (!rig.ok) return;
    Application& app = rig.app;
    type(app, "kept");
    app.hud().open_quit_dialog();
    check(!app.web_chat_available(), "not while the quit dialog is open");
    check(!app.web_chat_do(1, 'a') && !app.web_chat_do(2, 0) && !app.web_chat_do(0, 0) && !app.web_chat_do(3, 0), "nothing is taken then");
    check(app.hud().get_chat_input() == "kept", "and the line is as it was");
    app.hud().close_quit_dialog();
    check(app.web_chat_available(), "it is back when the dialog closes");
    app.hud().options().chat = false;
    check(!app.web_chat_available() && !app.web_chat_do(1, 'a'), "not with Participate In Chat off");
    app.hud().options().chat = true;
    app.hud().open_options();
    check(app.web_text_mode() == 1 && app.hud().get_chat_input() == "kept", "while the options window is open its fields are the boxes, and the chat line is as it was");
    check(app.web_chat_do(0, 0) && app.hud().get_chat_input() == "kept", "(an edit goes to the field with the focus, not to the chat line)");
    app.hud().close_options();
    check(app.web_text_mode() == 0 && app.hud().get_chat_input() == "kept", "the chat box is the box again when the window closes");
}

void test_options_fields() {
    std::printf("[options] the options window's four quick chat fields take the phone's keyboard\n");
    for (const Aspect aspect : {Aspect::Wide16x9, Aspect::Classic4x3}) {
        AppRig rig(aspect, 1.0f, false, std::string(), true, true);
        check(rig.ok, "the match is up");
        if (!rig.ok) continue;
        Application& app = rig.app;
        check(app.web_text_mode() == 0 && !app.web_chat_do(4, 1) && !app.web_chat_do(5, 0), "the chat box is the box before the window opens: the options operations do nothing");
        app.hud().open_options();
        check(app.web_chat_available() && app.web_text_mode() == 1, "the open window has fields that take text");
        const LayoutPoint window = app.hud().open_window_offset();
        for (size_t k = 0; k < 4; ++k) {
            const LayoutRect box = app.web_options_field(k);
            check(box.x == app.picture().x + window.x + OptionsScreen::EDIT_X[k] && box.y == app.picture().y + window.y + OptionsScreen::EDIT_Y[k] && box.w == 141 && box.h == 15, "field " + std::to_string(k) + " is where the window draws it, moved by the window's offset and the picture's");
            check(box.x >= 0 && box.right() <= app.canvas().width && box.y >= 0 && box.bottom() <= app.canvas().height, "field " + std::to_string(k) + " is inside the canvas");
            const OptionsScreen& screen = app.hud().options_screen();
            const int32_t mx = box.x + box.w / 2 - app.picture().x - window.x, my = box.y + box.h / 2 - app.picture().y - window.y;
            check(screen.edit(k).hit(mx, my) && !screen.edit(k).hit(mx, my + box.h) && !screen.edit(k).hit(mx - box.w, my), "a finger in field " + std::to_string(k) + " is a press that the window focuses it for");
        }
        check(app.web_options_field(4).w == 0 && app.web_options_field(99).h == 0, "there is no fifth field");
        check(app.hud().options_focused_edit() == std::optional<size_t>(0), "F9 has the focus when the window opens");
        check(!app.web_chat_do(4, 4) && !app.web_chat_do(4, -1), "a field that does not exist is refused");
        check(app.web_chat_do(4, 2) && app.hud().options_focused_edit() == std::optional<size_t>(2), "the page gives F11 the focus (the window's own press does the same one frame later)");
        check(app.web_text_line() == app.hud().get_quick_chat_key(2) && !app.web_text_line().empty(), "the line that the keyboard starts with is that field's text");
        app.hud().options().chat = false;
        check(app.web_chat_available(), "the fields take text with Participate In Chat off too");
        app.hud().options().chat = true;
    }
}

void test_options_typing() {
    std::printf("[options typing] the page's characters land in the field, the commit writes them\n");
    AppRig rig(Aspect::Wide16x9, 1.0f, false, std::string(), true, true);
    check(rig.ok, "the match is up");
    if (!rig.ok) return;
    Application& app = rig.app;
    type(app, "chat line");
    app.hud().open_options();
    const std::string was = app.hud().get_quick_chat_key(1);
    app.web_chat_do(4, 1);
    app.web_chat_do(0, 0);
    type(app, "Hold the line");
    check(app.web_text_line() == "Hold the line" && app.hud().options_screen().edit(1).text() == "Hold the line", "the characters are in the field at once");
    check(app.hud().get_quick_chat_key(1) == was, "and not yet written to the profile: the edit is one write, at the commit");
    check(app.web_chat_do(2, 0) && app.web_text_line() == "Hold the lin", "Backspace takes one off");
    check(!app.web_chat_do(1, 7) && !app.web_chat_do(1, 127) && !app.web_chat_do(1, 233) && !app.web_chat_do(1, 0x1F600) && app.web_text_line() == "Hold the lin", "control codes and non-ASCII are refused");
    check(!app.web_chat_do(9, 0), "an operation that does not exist does nothing");
    check(app.web_chat_do(5, 0) && app.hud().get_quick_chat_key(1) == "Hold the lin", "the commit writes the text to the profile's quick chat F10");
    check(app.hud().get_quick_chat_key(0) != "Hold the lin" && app.hud().get_quick_chat_key(2) != "Hold the lin", "the other fields are as they were");
    check(app.web_chat_do(0, 0) && app.web_text_line().empty() && app.web_chat_do(5, 0) && app.hud().get_quick_chat_key(1).empty(), "clearing and committing empties the field and the quick chat");
    type(app, std::string(130, 'x'));
    check(app.web_text_line().size() == OptionsState::QUICK_CHAT_MAX, "a field holds 100 characters, as the profile's");
    app.web_chat_do(5, 0);
    app.hud().close_options();
    check(app.hud().get_chat_input() == "chat line", "the chat line was never touched");
    app.hud().open_options();
    check(app.hud().get_quick_chat_key(1).size() == OptionsState::QUICK_CHAT_MAX, "the window opens again with the text that was written");
    app.hud().close_options();
}

void test_options_enter() {
    std::printf("[options enter] Enter is the window's own key\n");
    AppRig rig(Aspect::Wide16x9, 1.0f, false, std::string(), true, true);
    check(rig.ok, "the match is up");
    if (!rig.ok) return;
    Application& app = rig.app;
    app.hud().open_options();
    app.web_chat_do(4, 3);
    app.web_chat_do(0, 0);
    type(app, "Retreat");
    app.web_chat_do(5, 0);
    check(app.web_chat_do(3, 0) && app.hud().is_options_open(), "Enter is taken and goes through the queue: the window is still open until the frame reads it");
    app.run_frame_with_delta(0.016f);
    check(!app.hud().is_options_open() && app.web_text_mode() == 0, "the frame closed the window, as a keyboard's Enter does");
    check(app.hud().get_quick_chat_key(3) == "Retreat", "and the text stays");
}

void test_other_refusals() {
    std::printf("[refused] the catch-up screen and the results close the box\n");
    AppRig rig(Aspect::Wide16x9, 1.0f, false, std::string(), true, true);
    check(rig.ok, "the match is up");
    if (!rig.ok) return;
    Application& app = rig.app;
    check(app.web_chat_available(), "(it takes text first)");
    app.force_catch_up_screen_for_test(true);
    check(!app.web_chat_available() && !app.web_chat_do(1, 'a') && app.hud().get_chat_input().empty(), "not on the catch-up screen of a network match");
    app.force_catch_up_screen_for_test(false);
    check(app.web_chat_available(), "it is back when the catch-up screen goes");
    app.scorecard().show(app.sim().get_world_state().match_result, 0);
    check(app.scorecard().is_open() && !app.web_chat_available() && !app.web_chat_do(1, 'a') && app.hud().get_chat_input().empty(), "not while the results are open");
}

void test_not_a_match() {
    std::printf("[screens] the setup screen has no chat box of the match\n");
    AppRig rig(Aspect::Wide16x9, 1.0f, false, std::string(), false, true);
    check(rig.ok, "the setup screen is up");
    if (!rig.ok) return;
    check(!rig.app.web_chat_available() && !rig.app.web_chat_do(1, 'a'), "no text goes anywhere");
}

}  // namespace

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    test_box();
    test_typing();
    test_enter();
    test_closed();
    test_options_fields();
    test_options_typing();
    test_options_enter();
    test_other_refusals();
    test_not_a_match();
    std::printf("\nweb chat: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
