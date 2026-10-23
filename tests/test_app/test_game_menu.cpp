// The settings menu that Esc opens (src/ants_app/game_menu.cpp, application_game_menu.cpp; docs/GAME_MENU.md; the page's side is tests/scripts/test_web_game_menu.py):
//   * the one table of rows: ids and names are unique, every text is plain (no markup), the JSON that the page gets is valid for a match and for a recording, in the browser and in the desktop build,
//     and a recording has no Game section;
//   * Leave game: what it does in every state (a forfeit with one opponent left, a drop out with more, the match held, a recording) and the words that ask;
//   * the three new settings (Master volume, Mute, hit point numbers): read with the original's reader, written under their own names, and the gain they make;
//   * Esc: it cancels what is open or selected first, and asks for the menu only when there was nothing, only on a first press, never over a dialog that needs its answer;
//   * what the panel changes is what the original's options window shows (the same state, stored under the original's names), and a change of Ctrl+L is remembered;
//   * while the panel is up the game takes no key, text or click, does not scroll at the edges and sees no finger; closing gives the input back after the events that were queued;
//   * Leave game goes through the quit dialog's Yes.
// Usage: test_game_menu. Exit code 0 when every check passes.
#include <SDL.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "zoom_scene.hpp"

#include "ants_app/application.hpp"
#include "ants_app/config_store.hpp"
#include "ants_app/game_menu.hpp"
#include "ants_app/hud.hpp"
#include "ants_app/screen_controls.hpp"
#include "ants_test_paths.hpp"

using namespace ants;
using namespace ants::app;
using namespace zoomtest;
namespace gm = ants::app::game_menu;

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

// ---- a small strict JSON reader: the page parses what the game says, so the test does too ---------------------------------------------------------------------------------
struct Json {
    const std::string& text;
    size_t at{0};
    explicit Json(const std::string& t) : text(t) {}
    void space() { while (at < text.size() && (text[at] == ' ' || text[at] == '\n')) ++at; }
    bool string_value(std::string* out = nullptr) {
        if (at >= text.size() || text[at] != '"') return false;
        ++at;
        while (at < text.size() && text[at] != '"') {
            if (static_cast<unsigned char>(text[at]) < 0x20) return false;
            if (text[at] == '\\') {
                ++at;
                if (at >= text.size() || (text[at] != '"' && text[at] != '\\' && text[at] != 'n' && text[at] != 'u' && text[at] != '/')) return false;
            }
            if (out != nullptr) out->push_back(text[at]);
            ++at;
        }
        if (at >= text.size()) return false;
        ++at;
        return true;
    }
    bool value() {
        space();
        if (at >= text.size()) return false;
        const char c = text[at];
        if (c == '"') return string_value();
        if (c == '{') {
            ++at;
            space();
            if (at < text.size() && text[at] == '}') { ++at; return true; }
            for (;;) {
                space();
                if (!string_value()) return false;
                space();
                if (at >= text.size() || text[at++] != ':') return false;
                if (!value()) return false;
                space();
                if (at < text.size() && text[at] == ',') { ++at; continue; }
                if (at < text.size() && text[at] == '}') { ++at; return true; }
                return false;
            }
        }
        if (c == '[') {
            ++at;
            space();
            if (at < text.size() && text[at] == ']') { ++at; return true; }
            for (;;) {
                if (!value()) return false;
                space();
                if (at < text.size() && text[at] == ',') { ++at; continue; }
                if (at < text.size() && text[at] == ']') { ++at; return true; }
                return false;
            }
        }
        const size_t start = at;
        while (at < text.size() && (text[at] == '-' || (text[at] >= '0' && text[at] <= '9'))) ++at;
        return at > start;
    }
    bool valid() { const bool ok = value(); space(); return ok && at == text.size(); }
};

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path);
    std::stringstream out;
    out << in.rdbuf();
    return out.str();
}

bool has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

void test_table() {
    std::printf("[table] the one list of rows\n");
    size_t count = 0;
    const gm::Row* rows = gm::rows(count);
    check(count == 14, "fourteen rows");
    for (size_t i = 0; i < count; ++i) {
        const gm::Row& row = rows[i];
        check(gm::find_row(row.id) == &row, std::string("row ") + row.key + " is found by its id");
        for (size_t j = i + 1; j < count; ++j) check(row.id != rows[j].id && std::string(row.key) != rows[j].key, std::string("the row ") + row.key + " has an id and a name of its own");
        for (const char* p = row.key; *p != '\0'; ++p) check((*p >= 'a' && *p <= 'z') || *p == '_', std::string("the name ") + row.key + " is lower case letters and _");
        const std::string text = std::string(row.label) + row.hint + row.words;
        check(text.find('<') == std::string::npos && text.find('>') == std::string::npos && text.find('&') == std::string::npos, std::string("the words of ") + row.key + " carry no markup");
        for (const char c : text) check(static_cast<unsigned char>(c) >= 0x20 && static_cast<unsigned char>(c) < 0x7f, std::string("the words of ") + row.key + " are plain ASCII");
        if (row.kind == gm::Kind::Slider) check(row.min == 0 && row.max == 99 && row.def >= row.min, std::string("the slider ") + row.key + " runs 0 to 99, as the original's");
        if (row.kind == gm::Kind::Switch) check(std::string(row.words) == "On|Off", std::string("the switch ") + row.key + " says On and Off");
    }
    check(gm::find_row(0) == nullptr && gm::find_row(15) == nullptr && gm::find_row(-1) == nullptr, "no other id is a row");
    check(gm::find_row(gm::kMaster)->def == 100 && gm::find_row(gm::kMusic)->def == 65 && gm::find_row(gm::kEffects)->def == 100 && gm::find_row(gm::kScroll)->def == 50, "the defaults are the original's (Master 100: nothing changes)");
    check((gm::find_row(gm::kMaster)->show & gm::kShowNew) && (gm::find_row(gm::kMute)->show & gm::kShowNew) && (gm::find_row(gm::kHpNumbers)->show & gm::kShowNew) && !(gm::find_row(gm::kMusic)->show & gm::kShowNew), "exactly the three new rows carry the new tag");
    check((gm::find_row(gm::kMaster)->show & gm::kShowLive) && !(gm::find_row(gm::kMusic)->show & gm::kShowLive), "Master volume acts while it is dragged, the original's sliders at the release");

    for (const bool watching : {false, true}) {
        for (const bool web : {true, false}) {
            const std::string json = gm::describe_json(watching, web);
            Json reader(json);
            check(reader.valid(), std::string("the JSON is valid (watching ") + (watching ? "yes" : "no") + ", " + (web ? "web" : "desktop") + ")");
            check(has(json, "\"key\":\"master\"") && has(json, "\"key\":\"mute\"") && has(json, "\"key\":\"hp_numbers\"") && has(json, "\"key\":\"scroll\""), "the sound and video rows are there");
            check(has(json, "\"id\":\"controls\"") && has(json, "\"id\":\"sound\"") && has(json, "\"id\":\"video\""), "the Sound, Video and Controls sections are there");
            check(has(json, "\"key\":\"chat\"") == !watching && has(json, "\"id\":\"game\"") == !watching && has(json, "\"key\":\"version\"") == !watching, "the Game section is in a match and not in a recording");
            check(has(json, "\"key\":\"picture\"") == web && has(json, "\"key\":\"lockmouse\"") == web, "the picture's shape and the fullscreen mouse are the browser's rows");
            check(has(json, "\"key\":\"fullscreen\""), "Fullscreen is in every skin");
            check(has(json, "Forfeit and leave?") && has(json, "You will drop out.") && has(json, "This match ends. Your game is not saved.") && has(json, "Back to the matches") && has(json, "Watching a replay"), "the words of Leave game are in it");
        }
    }
    const std::string match = gm::describe_json(false, true);
    check(has(match, "\"key\":\"master\",\"sec\":\"sound\",\"kind\":\"slider\",\"label\":\"Master volume\""), "Master volume is the first row of the Sound section");
    check(match.find("\"key\":\"master\"") < match.find("\"key\":\"music\"") && match.find("\"key\":\"music\"") < match.find("\"key\":\"effects\"") && match.find("\"key\":\"effects\"") < match.find("\"key\":\"mute\""), "the Sound section is in the order of the picture");
    check(has(match, "\"hint\":\"The original's \\\"Music Volume\\\"\""), "a quote in a text is escaped");
}

// The page's check (tests/scripts/web_game_menu_check.js) draws the rows from these two files, which are what the game says for a match and for a recording (in the browser build). They are the table's picture:
// a change of the table fails here until the files say the same (`test_game_menu --write-fixtures` writes them again), and so the page's check always runs on what the game really gives.
void test_fixtures(bool write) {
    std::printf("[fixtures] what the page's check draws from is what the game says\n");
    const std::string folder = std::string(TEST_DATA_DIR) + "/";
    const std::pair<const char*, bool> files[] = {{"game_menu_match.json", false}, {"game_menu_watch.json", true}};
    for (const auto& file : files) {
        const std::string json = gm::describe_json(file.second, true);
        if (write) {
            std::ofstream out(folder + file.first, std::ios::binary | std::ios::trunc);
            out << json << "\n";
            continue;
        }
        check(read_file(folder + file.first) == json + "\n", std::string(file.first) + " is what the game says (run test_game_menu --write-fixtures after a change of the table)");
    }
}

void test_leave() {
    std::printf("[leave] what Leave game does, and what asks\n");
    using K = gm::LeaveKind;
    check(gm::leave_kind(true, false, true, 3, true) == K::Watch && gm::leave_kind(true, true, false, 0, false) == K::Watch, "a recording or a live match that is watched: back to the list, in every state");
    check(gm::leave_kind(false, true, true, 1, true) == K::Held && gm::leave_kind(false, true, true, 3, true) == K::Held, "a match that is held: the player leaves for good, whatever is left");
    check(gm::leave_kind(false, false, true, 1, true) == K::Forfeit && gm::leave_kind(false, false, false, 1, true) == K::Forfeit, "one opponent left in a running match: a forfeit, on the network and at home");
    check(gm::leave_kind(false, false, true, 2, true) == K::DropOut && gm::leave_kind(false, false, true, 3, true) == K::DropOut, "two or more opponents in a network match: the player drops out");
    check(gm::leave_kind(false, false, false, 2, true) == K::EndsMatch && gm::leave_kind(false, false, false, 0, true) == K::EndsMatch && gm::leave_kind(false, false, true, 0, true) == K::EndsMatch, "a game at home against several, or alone: the match ends");
    check(gm::leave_kind(false, false, true, 1, false) == K::EndsMatch && gm::leave_kind(false, false, false, 1, false) == K::EndsMatch, "a match that is over is no forfeit");
    check(gm::leave_needs_confirm(K::EndsMatch) && gm::leave_needs_confirm(K::DropOut) && gm::leave_needs_confirm(K::Forfeit) && !gm::leave_needs_confirm(K::Held) && !gm::leave_needs_confirm(K::Watch), "only a leave that loses something asks");
    check(gm::match_is_held(true, true, false, false) && gm::match_is_held(true, false, true, false) && gm::match_is_held(true, false, false, true), "a network match is held by a pause, the catch-up screen or the countdown card, each alone");
    check(!gm::match_is_held(true, false, false, false) && !gm::match_is_held(false, true, true, true), "...and by nothing else; a game at home is never held");
    check(gm::needs_answer(true, false, false, false, false) && gm::needs_answer(false, true, false, false, false), "a dialog of the match or the catch-up screen needs its answer, at home too");
    check(gm::needs_answer(false, false, true, true, false) && gm::needs_answer(false, false, true, false, true), "a vote or a lost link in a network match needs its answer");
    check(!gm::needs_answer(false, false, true, false, false) && !gm::needs_answer(false, false, false, true, true), "...and nothing else does (no vote or lost link is one at home)");
    const std::string json = gm::describe_json(false, true);
    for (const K kind : {K::EndsMatch, K::DropOut, K::Forfeit}) check(has(json, "{\"kind\":" + std::to_string(static_cast<int>(kind)) + ",\"title\":"), "the question of every asking kind is in the JSON");
    check(!has(json, "{\"kind\":3") && !has(json, "{\"kind\":4"), "and none for a held match or a recording");
}

void test_settings() {
    std::printf("[settings] the three new settings\n");
    ConfigStore store;
    gm::MenuSettings defaults;
    defaults.load(store);
    check(defaults.master == 100 && !defaults.mute && defaults.hp_numbers, "an empty file: Master 100, not muted, the numbers on");
    check(defaults.gain() == 1.0f, "which is the mix as it was");
    gm::MenuSettings s;
    s.master = 37;
    s.mute = true;
    s.hp_numbers = false;
    for (const int id : {gm::kMaster, gm::kMute, gm::kHpNumbers}) s.write(store, id);
    check(store.get_string("master", "", 8) == "37" && store.get_string("mute", "", 8) == "on" && store.get_string("hp_numbers", "", 8) == "off", "written as master=37, mute=on, hp_numbers=off");
    s.write(store, gm::kMusic);
    check(!store.has("Music Volume") && !store.has("music"), "a row that is not one of the three writes nothing");
    gm::MenuSettings back;
    back.load(store);
    check(back.master == 37 && back.mute && !back.hp_numbers, "and read back");
    check(std::string(gm::MenuSettings::key(gm::kMaster)) == "master" && std::string(gm::MenuSettings::key(gm::kMusic)).empty(), "the names");
    for (const std::string bad : {"abc", "-5", "101", "1e2", ""}) {
        ConfigStore broken;
        broken.set_string("master", bad);
        broken.set_string("mute", "yes");
        broken.set_string("hp_numbers", "maybe");
        gm::MenuSettings m;
        m.load(broken);
        check(m.master == 100 && !m.mute && m.hp_numbers, "a value that is not valid is the default: '" + bad + "'");
    }
    ConfigStore edge;
    edge.set_int("master", 0);
    gm::MenuSettings zero;
    zero.load(edge);
    check(zero.master == 0 && zero.gain() == 0.0f, "Master 0 is valid and silent");
    edge.set_int("master", 100);
    zero.load(edge);
    check(zero.master == 100 && zero.gain() == 1.0f, "Master 100 is valid and the whole volume");
    gm::MenuSettings m2;
    m2.master = 99;
    check(m2.gain() == 1.0f, "99, the end of the slider, is the whole volume too");
    m2.master = 50;
    check(m2.gain() > 0.50f && m2.gain() < 0.51f, "50 is 50 / 99");
    m2.mute = true;
    check(m2.gain() == 0.0f, "Mute is silence, whatever Master is");
}

// ---- the application -------------------------------------------------------------------------------------------------------------------------------------------------------
void press_key(Application& app, SDL_Keycode key, uint16_t mod = 0, bool repeat = false) {
    SDL_KeyboardEvent e{};
    e.type = SDL_KEYDOWN;
    e.keysym.sym = key;
    e.keysym.mod = mod;
    e.repeat = repeat ? 1 : 0;
    app.handle_key_down(e);
}

void push_key(SDL_Keycode key) {
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_KEYDOWN;
    e.key.keysym.sym = key;
    e.key.state = SDL_PRESSED;
    SDL_PushEvent(&e);
}

void push_text(const char* text) {
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_TEXTINPUT;
    std::snprintf(e.text.text, sizeof(e.text.text), "%s", text);
    SDL_PushEvent(&e);
}

void test_esc() {
    std::printf("[esc] cancel first, ask for the menu only with nothing to cancel\n");
    AppRig rig(Aspect::Wide16x9, 1.0f, false, std::string(), true, true);
    check(rig.ok, "the match is up");
    if (!rig.ok) return;
    Application& app = rig.app;
    int asked = 0;
    app.set_on_game_menu([&]() { ++asked; });
    check(app.game_menu_get(gm::kFactWhere) == 0 && app.game_menu_get(gm::kFactNeedsAnswer) == 0, "a match, and nothing needs an answer");

    press_key(app, SDLK_ESCAPE);
    check(asked == 1, "Esc with nothing selected asks for the menu");
    press_key(app, SDLK_ESCAPE, 0, true);
    check(asked == 1, "a held Esc (a repeat) does not ask again");

    app.hud().select_base(app.local_player_id());
    check(app.hud().has_selection(), "(a hill is selected)");
    press_key(app, SDLK_ESCAPE);
    check(asked == 1 && !app.hud().has_selection(), "Esc with a selection only deselects");
    press_key(app, SDLK_ESCAPE);
    check(asked == 2, "...and the next one asks");

    const std::vector<uint32_t> own = ants_of(app.sim(), 0);
    if (!own.empty()) {
        app.hud().select_ant(own[0]);
        press_key(app, SDLK_ESCAPE);
        check(asked == 2 && !app.hud().has_selection(), "an ant that is selected is deselected, not asked about");
    }

    if (!own.empty()) {                                        // a pedestal that is latched is something to cancel, even with nothing selected (a latch outlives the selection)
        app.hud().select_ant(own[0]);
        const int32_t dx = app.layout().dx();
        ViewportCamera& cam = app.renderer().camera();
        app.hud().handle_mouse_down(485 + dx, 160, SDL_BUTTON_LEFT, app.sim(), cam);
        app.hud().handle_mouse_up(485 + dx, 160, SDL_BUTTON_LEFT, app.sim(), cam);
        app.hud().clear_selection();
        check(app.hud().is_move_latched() && !app.hud().has_selection(), "(the Move pedestal is latched and nothing is selected)");
        press_key(app, SDLK_ESCAPE);
        check(asked == 2 && !app.hud().is_move_latched(), "Esc lets a latched pedestal go and asks for nothing");
        press_key(app, SDLK_ESCAPE);
        check(asked == 3, "...and the next one asks");
        asked = 2;
    }

    app.hud().open_quit_dialog();
    check(app.game_menu_get(gm::kFactNeedsAnswer) == 1, "the quit dialog needs its answer");
    press_key(app, SDLK_ESCAPE);
    check(asked == 2 && !app.hud().is_quit_dialog_open(), "Esc closes the quit dialog and asks for nothing");
    press_key(app, SDLK_ESCAPE);
    check(asked == 3, "the Esc after it asks");

    app.hud().open_quick_help();
    press_key(app, SDLK_ESCAPE);
    check(asked == 3 && !app.hud().is_quick_help_open(), "Esc closes the quick help and asks for nothing");
    app.hud().open_options();
    press_key(app, SDLK_ESCAPE);
    check(asked == 3 && app.hud().is_options_open(), "the original's options window keeps its own Esc (it does nothing) and the menu does not open over it");
    app.hud().close_options();

    app.request_game_menu();
    check(asked == 4, "the request itself asks when nothing is open");
    app.force_catch_up_screen_for_test(true);
    app.request_game_menu();
    check(asked == 4 && app.game_menu_get(gm::kFactNeedsAnswer) == 1, "not over the catch-up screen of a lost link");
    app.force_catch_up_screen_for_test(false);
    app.scorecard().show(app.sim().get_world_state().match_result, 0);
    app.request_game_menu();
    check(asked == 4 && app.game_menu_get(gm::kFactWhere) == -1, "not over the results");
}

void test_values() {
    std::printf("[values] the panel and the original's options window show the same state\n");
    const std::filesystem::path file = std::filesystem::temp_directory_path() / ("ants_game_menu_" + std::to_string(static_cast<long long>(SDL_GetTicks())) + ".ini");
    {
        AppRig rig(Aspect::Wide16x9, 1.0f, false, file.string(), true, true);
        check(rig.ok, "the match is up");
        if (!rig.ok) return;
        Application& app = rig.app;
        check(app.game_menu_get(gm::kMaster) == 100 && app.game_menu_get(gm::kMute) == 0 && app.game_menu_get(gm::kHpNumbers) == 1, "the new settings start as they always were");
        check(app.game_menu_get(gm::kMusic) == 65 && app.game_menu_get(gm::kEffects) == 100 && app.game_menu_get(gm::kScroll) == 50 && app.game_menu_get(gm::kChat) == 1 && app.game_menu_get(gm::kQuickHelpAtStart) == 1, "the original's start as the original's");
        check(app.game_menu_get(gm::kFullscreen) == -1 && app.game_menu_get(gm::kPicture) == -1 && app.game_menu_get(gm::kLockMouse) == -1 && app.game_menu_get(99) == -1 && app.game_menu_get(0) == -1, "the page's rows and what is no row are not the game's");

        check(app.game_menu_set(gm::kMaster, 41) && app.game_menu_get(gm::kMaster) == 41, "Master volume is taken");
        check(app.game_menu_set(gm::kMaster, 400) && app.game_menu_get(gm::kMaster) == 100 && app.game_menu_set(gm::kMaster, -4) && app.game_menu_get(gm::kMaster) == 0, "...and held to 0 .. 100");
        check(std::abs(app.audio_mixer().output_gain() - 0.0f) < 1e-6f, "(Master 0 is silence in the mixer)");
        app.game_menu_set(gm::kMaster, 41);
        check(std::abs(app.audio_mixer().output_gain() - app.menu_settings().gain()) < 1e-6f && app.audio_mixer().output_gain() > 0.4f, "Master volume reaches the mixer");
        check(app.game_menu_set(gm::kMute, 1) && app.audio_mixer().output_gain() == 0.0f, "Mute silences the mixer");
        check(app.game_menu_set(gm::kMute, 0) && std::abs(app.audio_mixer().output_gain() - app.menu_settings().gain()) < 1e-6f, "...and Off gives Master back");
        check(app.game_menu_set(gm::kMute, 1) && app.game_menu_get(gm::kMute) == 1 && app.game_menu_get(gm::kMaster) == 41, "Mute is on and Master stays where it was");
        check(app.game_menu_get(gm::kMusic) == 65 && app.game_menu_get(gm::kEffects) == 100, "...and so do the original's two volumes");
        check(app.game_menu_set(gm::kMusic, 30) && app.game_menu_set(gm::kEffects, 77) && app.game_menu_set(gm::kScroll, 12), "the original's sliders are taken");
        check(app.hud().get_music_volume() == 30 && app.hud().get_sound_volume() == 77 && app.hud().get_scroll_speed() == 12, "in the options state that the original's window reads");
        check(app.game_menu_set(gm::kChat, 0) && !app.hud().is_chat_enabled() && app.game_menu_set(gm::kQuickHelpAtStart, 0) && !app.hud().is_quick_help_enabled(), "the two switches too");
        check(app.game_menu_set(gm::kHpNumbers, 0) && !app.hud().is_show_hp() && app.game_menu_get(gm::kHpNumbers) == 0, "the hit point numbers go off");
        check(!app.game_menu_set(gm::kFullscreen, 1) && !app.game_menu_set(gm::kVersion, 1) && !app.game_menu_set(77, 1), "what is not the game's is refused");
        app.hud().open_options();
        check(app.hud().options_screen().slider(0).value() == ScreenSlider(188, 179, 77).value() && app.hud().options_screen().slider(1).value() == ScreenSlider(188, 216, 30).value() &&
                  app.hud().options_screen().slider(2).value() == ScreenSlider(188, 253, 12).value(), "the original's window opens on the same sliders (77, 30 and 12 as its thumbs stand for them)");
        app.hud().close_options();
        app.run_frame_with_delta(0.016f);
        press_key(app, SDLK_l, KMOD_LCTRL);
        check(app.hud().is_show_hp(), "(Ctrl+L turns the numbers on again)");
        app.run_frame_with_delta(0.016f);
        check(app.menu_settings().hp_numbers, "...and the frame after it remembers that");
        press_key(app, SDLK_l, KMOD_LCTRL);
        app.run_frame_with_delta(0.016f);
        check(!app.menu_settings().hp_numbers, "...and off");
        const std::string saved = read_file(file);
        check(has(saved, "master=41\n") && has(saved, "mute=on\n") && has(saved, "hp_numbers=off\n"), "the file has master=41, mute=on, hp_numbers=off");
        check(has(saved, "Music Volume=30\n") && has(saved, "Sound Volume=77\n") && has(saved, "Scroll Speed=12\n") && has(saved, "Participate In Chat=0\n") && has(saved, "Show Quick Help at Startup=0\n"), "the original's keep their own names");
    }
    {
        AppRig again(Aspect::Wide16x9, 1.0f, false, file.string(), true, true);
        check(again.ok, "the game starts again");
        if (again.ok) {
            Application& app = again.app;
            check(app.game_menu_get(gm::kMaster) == 41 && app.game_menu_get(gm::kMute) == 1 && app.game_menu_get(gm::kHpNumbers) == 0 && !app.hud().is_show_hp(), "Master, Mute and the numbers come back");
            check(app.game_menu_get(gm::kMusic) == 30 && app.game_menu_get(gm::kScroll) == 12, "and the original's");
        }
    }
    std::error_code ignored;
    std::filesystem::remove(file, ignored);
}

void test_input() {
    std::printf("[input] the game takes nothing while the panel is up\n");
    AppRig rig(Aspect::Wide16x9, 1.0f, false, std::string(), true, true);
    check(rig.ok, "the match is up");
    if (!rig.ok) return;
    Application& app = rig.app;
    push_text("a");
    app.run_frame_with_delta(0.016f);
    check(app.hud().get_chat_input() == "a", "(a typed letter is the chat line's)");
    app.web_chat_do(0, 0);
    // the map scrolls when the pointer is at the left edge of the screen (the control of the check below)
    const auto edge = [&]() {
        app.renderer().camera().set_origin(450.0, 500.0, app.sim().grid().width(), app.sim().grid().height());
        SDL_MouseMotionEvent m{};
        m.type = SDL_MOUSEMOTION;
        m.x = 0;
        m.y = app.layout().view().y + app.layout().view().h / 2;
        app.handle_mouse_motion(m);
        const float before = app.renderer().camera().x;
        for (int i = 0; i < 10; ++i) app.run_frame_with_delta(0.05f);
        return app.renderer().camera().x - before;
    };
    check(edge() < 0.0f, "(the map scrolls left with the pointer on the left edge)");

    const LayoutRect view = app.layout().view();
    app.hud().handle_mouse_down(view.x + 100, view.y + 100, SDL_BUTTON_LEFT, app.sim(), app.renderer().camera());
    check(app.hud().is_dragging(), "(a rubber band is begun with the left button)");
    app.game_menu_set_open(true);
    check(app.game_menu_is_open(), "the panel is up");
    check(!app.hud().is_dragging(), "what the button began before it is let go, without its act");
    push_text("b");
    push_key(SDLK_b);
    push_key(SDLK_ESCAPE);
    push_key(SDLK_l);
    app.run_frame_with_delta(0.016f);
    check(app.hud().get_chat_input().empty(), "no letter reaches the chat line");
    check(app.hud().is_show_hp() == (app.game_menu_get(gm::kHpNumbers) == 1), "and no hotkey (the numbers are as they were)");
    const bool had_numbers = app.hud().is_show_hp();
    push_key(SDLK_l);
    SDL_Event ctrl_l;
    SDL_zero(ctrl_l);
    ctrl_l.type = SDL_KEYDOWN;
    ctrl_l.key.keysym.sym = SDLK_l;
    ctrl_l.key.keysym.mod = KMOD_LCTRL;
    SDL_PushEvent(&ctrl_l);
    app.run_frame_with_delta(0.016f);
    check(app.hud().is_show_hp() == had_numbers, "Ctrl+L does not turn the numbers");

    check(edge() == 0.0f, "the map does not scroll at the edge while the panel is up");
    const auto finger = [&](uint32_t type) {
        SDL_Event e;
        SDL_zero(e);
        e.type = type;
        e.tfinger.type = type;
        e.tfinger.timestamp = SDL_GetTicks();
        e.tfinger.touchId = 7;
        e.tfinger.fingerId = 1;
        e.tfinger.x = 0.5f;
        e.tfinger.y = 0.5f;
        SDL_PushEvent(&e);
        app.run_frame_with_delta(0.016f);
    };
    finger(SDL_FINGERDOWN);
    check(app.touch().fingers() == 0, "a finger that goes down on the panel is not the game's");
    finger(SDL_FINGERUP);

    app.game_menu_set_open(false);
    check(app.game_menu_is_open(), "closing waits for the events that are queued");
    push_text("c");
    push_key(SDLK_c);
    app.run_frame_with_delta(0.016f);
    check(!app.game_menu_is_open() && app.hud().get_chat_input().empty(), "the key that closed the panel and the ones with it are still the panel's");
    push_text("d");
    app.run_frame_with_delta(0.016f);
    check(app.hud().get_chat_input() == "d", "the next frame's text is the game's again");
    check(edge() < 0.0f, "and the map scrolls at the edge again");
    finger(SDL_FINGERDOWN);
    check(app.touch().fingers() == 1, "(the control: a finger is the game's again)");
    finger(SDL_FINGERUP);
}

void test_acts() {
    std::printf("[acts] what the panel asks of the game\n");
    {
        AppRig rig(Aspect::Wide16x9, 1.0f, false, std::string(), true, true);
        check(rig.ok, "the match is up");
        if (!rig.ok) return;
        Application& app = rig.app;
        check(app.game_menu_get(gm::kFactLeave) == static_cast<int>(gm::leave_kind(false, false, false, app.sim().other_sides(app.local_player_id()), true)), "Leave game's kind is the model's, from the match as it is");
        check(app.game_menu_do(gm::kActQuickHelp) && app.hud().is_quick_help_open(), "Quick help opens the original's quick help");
        check(!app.game_menu_do(gm::kActOptionsWindow) && !app.hud().is_options_open(), "...and nothing opens over a dialog that is open");
        app.hud().close_quick_help();
        check(app.game_menu_do(gm::kActOptionsWindow) && app.hud().is_options_open(), "the options window opens");
        app.hud().close_options();
        check(!app.game_menu_do(7) && !app.game_menu_do(0), "an act that does not exist does nothing");
        app.game_menu_set_open(true);
        check(app.game_menu_do(gm::kActLeave) && !app.game_menu_is_open(), "Leave game is taken");
        check(!app.is_running() || app.sim().is_match_over(), "...and is the quit dialog's Yes: the program ends, or the match ends for a forfeit");
    }
    {
        AppRig rig(Aspect::Wide16x9, 1.0f, false, std::string(), true, true);
        check(rig.ok, "the match is up (one opponent left)");
        if (!rig.ok) return;
        Application& app = rig.app;
        app.sim().drop_player(2);
        app.sim().drop_player(3);
        check(app.sim().other_sides(app.local_player_id()) == 1 && app.game_menu_get(gm::kFactLeave) == static_cast<int>(gm::LeaveKind::Forfeit), "Leave game's kind is a forfeit");
        app.game_menu_set_open(true);
        check(app.game_menu_do(gm::kActLeave) && !app.game_menu_is_open(), "Leave game is taken");
        check(app.sim().is_match_over() && app.is_running(), "...and is the quit dialog's Yes: the Quit command ends the match (the results are up), the program does not end");
    }
    {
        AppRig rig(Aspect::Wide16x9, 1.0f, false, std::string(), false, true);
        check(rig.ok, "the setup screen is up");
        if (!rig.ok) return;
        check(rig.app.game_menu_get(gm::kFactWhere) == -1 && !rig.app.game_menu_do(gm::kActLeave) && !rig.app.game_menu_do(gm::kActQuickHelp), "no match, no menu: nothing is done");
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    const bool write = argc > 1 && std::string(argv[1]) == "--write-fixtures";
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    test_fixtures(write);
    if (write) return 0;
    test_table();
    test_leave();
    test_settings();
    test_esc();
    test_values();
    test_input();
    test_acts();
    std::printf("\ngame menu: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
