// Options model tests (batch 4 part 2): the original's slider, latching button and edit field (Ants.exe FUN_01011397, FUN_01010fcb with its toggle flag, FUN_010119a8),
// the settings store (FUN_0100c18f / FUN_0102950f: a value in [min, max) or the default) and the options screen built from them (FUN_0101487c). Pure logic, no
// drawing: the pictures are checked in test_hud_layout. (docs/GAME_REVERSE_ENGINEERING.md 5.51)
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "ants_app/audio_mixer.hpp"
#include "ants_app/config_store.hpp"
#include "ants_app/options_screen.hpp"
#include "ants_app/screen_controls.hpp"

using namespace ants::app;

namespace {

int g_checks = 0;
int g_failures = 0;
const char* g_group = "";

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::fprintf(stderr, "  FAIL [%s]: %s\n", g_group, what.c_str());
    }
}

// The reference model of the slider, written from the disassembly independently of the class (the script of the model is not kept in the repository)
int32_t ref_pos_from_value(int32_t v) { return std::clamp(211 + (185 * v) / 99, 211, 395); }
int32_t ref_value_from_pos(int32_t pos) { return ((std::clamp(pos, 211, 395) - 211) * 100) / 185; }

void test_slider() {
    g_group = "slider";
    std::printf("[slider] placement, hit rectangle, drag and value (FUN_01011397 / FUN_01011543 / FUN_010115ca / FUN_0101161b / FUN_01011656)\n");
    // placement of the configured value: pos = 211 + 185 v / 99, clamped to [211, 395]; the thumb's sprite is at (pos - 23, y - 1)
    int bad = 0;
    for (int32_t v = 0; v <= 100; ++v) {
        const ScreenSlider s(188, 179, v);
        if (s.position() != ref_pos_from_value(v) || s.thumb_left() != ref_pos_from_value(v) - 23 || s.thumb_top() != 178 || s.value() != ref_value_from_pos(ref_pos_from_value(v))) ++bad;
    }
    check(bad == 0, "every configured value 0 .. 100 is placed and read back as the original's formulas say");
    const ScreenSlider defaults_sound(188, 179, 100);
    const ScreenSlider defaults_music(188, 216, 65);
    const ScreenSlider defaults_scroll(188, 253, 50);
    check(defaults_sound.thumb_left() == 372 && defaults_sound.thumb_top() == 178, "Sound Volume 100: the thumb at (372, 178), the end of the track");
    check(defaults_music.thumb_left() == 309 && defaults_music.thumb_top() == 215, "Music Volume 65: the thumb at (309, 215)");
    check(defaults_scroll.thumb_left() == 281 && defaults_scroll.thumb_top() == 252, "Scroll Speed 50: the thumb at (281, 252)");
    check(defaults_sound.value() == 99, "the value 100 lies beyond the end of the track and is read back as 99 (release without a move stores 99)");
    check(defaults_music.value() == 65 && defaults_scroll.value() == 50, "65 and 50 read back unchanged");

    // the hit rectangle [188, 419) x [y, y + 20)
    ScreenSlider s(188, 179, 50);
    check(s.hit(188, 179) && s.hit(418, 198) && s.hit(300, 190), "inside: the corners and the middle");
    check(!s.hit(187, 190) && !s.hit(419, 190) && !s.hit(300, 178) && !s.hit(300, 199), "outside: one pixel beyond each edge");

    // a press starts the drag and changes nothing
    const int32_t value_before = s.value();
    const int32_t pos_before = s.position();
    check(s.on_press(400, 185), "a press at the far end of the track starts a drag");
    check(s.dragging() && s.value() == value_before && s.position() == pos_before, "the press changes neither the thumb nor the value");
    // a move sets the position (clamped) and the value (integer division by 185)
    s.on_move(320, 500);
    check(s.position() == 320 && s.value() == 58 && s.thumb_left() == 297, "a move to x = 320: pos 320, value (320 - 211) 100 / 185 = 58, thumb at 297");
    s.on_move(100, 185);
    check(s.position() == 211 && s.value() == 0 && s.thumb_left() == 188, "a move beyond the left end: the thumb stops at 188, the value is 0");
    s.on_move(639, 185);
    check(s.position() == 395 && s.value() == 99 && s.thumb_left() == 372, "a move beyond the right end: the value is 99 at most");
    check(s.on_release() && !s.dragging(), "the release runs the callback");
    check(!s.on_release(), "and only once");
    s.on_move(250, 185);
    check(s.position() == 395, "a move without a drag does nothing");

    // a press outside starts nothing
    ScreenSlider t(188, 216, 65);
    check(!t.on_press(300, 240) && !t.dragging() && !t.on_release(), "a press below the track starts no drag; its release runs no callback");

    // exhaustive: the value for every pointer x
    bad = 0;
    for (int32_t x = 0; x < 640; ++x) {
        ScreenSlider u(188, 253, 0);
        u.on_press(300, 260);
        u.on_move(x, 260);
        if (u.value() != ref_value_from_pos(x) || u.thumb_left() != std::clamp(x, 211, 395) - 23) ++bad;
    }
    check(bad == 0, "the value and the thumb for every pointer x 0 .. 639 follow the formulas");
}

void test_toggle() {
    g_group = "toggle";
    std::printf("[toggle] the latching button: pictures, capture and the flip before the callback (FUN_01011206 / FUN_01011281 / FUN_010111d4)\n");
    using Art = ScreenToggle::Art;
    ScreenToggle t(100, 200, 50, 24);
    check(t.art() == Art::Up && !t.latched(), "it starts up");
    t.on_move(110, 210);
    check(t.art() == Art::Hover, "the pointer inside shows the hover picture");
    t.on_move(10, 10);
    check(t.art() == Art::Up, "outside: up again");
    check(t.on_press(110, 210) && t.captured() && t.art() == Art::Down, "a press inside captures and shows the down picture");
    t.on_move(115, 212);
    check(t.art() == Art::DownHover, "a move inside while captured shows down + hover");
    check(t.on_release(115, 212), "the release runs the callback");
    check(t.latched() && t.art() == Art::Down && !t.captured(), "the latch flag was flipped before it, and the button shows down");
    t.on_move(110, 210);
    check(t.art() == Art::DownHover, "a latched button under the pointer shows down + hover");
    t.on_move(0, 0);
    check(t.art() == Art::Down, "a latched button elsewhere shows down");
    // clicking the latched button flips it off; a callback that sets the latch (the options' pair) overrides that
    t.on_press(110, 210);
    check(t.on_release(110, 210) && !t.latched() && t.art() == Art::Up, "the flip of a latched button unlatches it");
    t.set_latched(true);
    check(t.latched() && t.art() == Art::Down, "set_latched(true) latches it and shows down");
    t.set_latched(false);
    check(!t.latched() && t.art() == Art::Up, "set_latched(false) shows up");

    // leaving cancels the capture for good
    ScreenToggle u(100, 200, 50, 24);
    u.on_press(110, 210);
    u.on_move(300, 300);
    check(!u.captured() && u.art() == Art::Up, "the pointer left: the capture is gone, the button is up");
    u.on_move(110, 210);
    check(!u.captured() && u.art() == Art::Hover, "coming back only hovers");
    check(!u.on_release(110, 210) && !u.latched(), "the release runs no callback");
    // a release outside after a press inside
    ScreenToggle v(100, 200, 50, 24);
    v.on_press(110, 210);
    check(!v.on_release(300, 300) && !v.latched(), "a release outside runs no callback");
    // a press outside captures nothing
    ScreenToggle w(100, 200, 50, 24);
    check(!w.on_press(10, 10) && !w.on_release(110, 210), "a press outside captures nothing; the release inside runs nothing");
}

void test_edit() {
    g_group = "edit";
    std::printf("[edit] the edit field: rectangle, focus, characters, maximum, caret (FUN_010119a8 / FUN_01011d86 / FUN_01011c9f / FUN_01011d0e)\n");
    ScreenEdit e(92, 370, 141, 15, "Attack!");
    check(e.hit(92, 370) && e.hit(232, 384) && !e.hit(233, 370) && !e.hit(92, 385) && !e.hit(91, 370) && !e.hit(92, 369), "the rectangle is [x, x + w) x [y, y + h)");
    check(!e.focused() && !e.on_char('a') && e.text() == "Attack!", "without the focus nothing is typed");
    check(!e.caret_visible(0) && !e.caret_visible(1000), "and there is no caret");

    e.focus(true, 1000);
    check(e.focused() && e.caret_visible(1000) && e.caret_visible(1149), "the caret shows from the focus on, for 150 ms");
    check(!e.caret_visible(1150) && !e.caret_visible(1299), "then it is gone for 150 ms");
    check(e.caret_visible(1300) && e.caret_visible(1449) && !e.caret_visible(1450), "and so on");
    e.focus(true, 5000);
    check(e.caret_visible(1300), "gaining the focus again changes nothing (the phase continues)");

    check(e.on_char('x') && e.text() == "Attack!x", "a printable character is appended");
    check(e.on_char(' ') && e.text() == "Attack!x ", "the space is one");
    check(!e.on_char(0x1f) && !e.on_char(0x7f) && !e.on_char(0x80) && e.text() == "Attack!x ", "control characters and everything above 0x7e are not");
    check(e.on_char(ScreenEdit::KEY_BACKSPACE) && e.text() == "Attack!x", "Backspace (key id 0x19) removes the last character");
    check(!e.on_char(ScreenEdit::KEY_ESCAPE) && !e.on_char(ScreenEdit::KEY_ENTER) && e.text() == "Attack!x", "Esc and Enter change nothing");
    ScreenEdit empty(0, 0, 10, 10, "");
    empty.focus(true, 0);
    check(!empty.on_char(ScreenEdit::KEY_BACKSPACE), "Backspace in an empty field changes nothing");

    // 100 characters at most
    ScreenEdit full(0, 0, 141, 15, std::string(99, 'a'));
    full.focus(true, 0);
    check(full.on_char('b') && full.text().size() == 100, "the hundredth character is taken");
    check(!full.on_char('c') && full.text().size() == 100 && full.text().back() == 'b', "the 101st is dropped");
    check(full.on_char(ScreenEdit::KEY_BACKSPACE) && full.on_char('d') && full.text().back() == 'd', "after a Backspace there is room again");
    ScreenEdit over(0, 0, 141, 15, std::string(150, 'z'));
    check(over.text().size() == 100, "a longer initial text is cut to 100");

    // focus follows the left press: the field under the pointer gets it, every other loses it
    ScreenEdit a(92, 370, 141, 15, "a");
    ScreenEdit b(92, 402, 141, 15, "b");
    a.focus(true, 0);
    a.on_press(100, 410, 10);
    b.on_press(100, 410, 10);
    check(!a.focused() && b.focused(), "a press on the second field moves the focus there");
    a.on_press(300, 300, 20);
    b.on_press(300, 300, 20);
    check(!a.focused() && !b.focused(), "a press anywhere else takes it from every field");
    // the tail flag: set by construction and by the gain of the focus, cleared by the loss of it
    ScreenEdit f(0, 0, 141, 15, "f");
    check(f.tail_aligned(), "a fresh field shows the end of a text that does not fit");
    f.focus(false, 0);
    check(f.tail_aligned(), "losing a focus it never had changes nothing");
    f.focus(true, 0);
    check(f.tail_aligned(), "gaining it: still");
    f.focus(false, 0);
    check(!f.tail_aligned(), "losing it: the beginning shows");
}

void test_config_store() {
    g_group = "config";
    std::printf("[config] the settings store: the profile's validity rule, texts, file round trip (FUN_0102950f)\n");
    ConfigStore c;
    c.parse("Sound Volume=99\nMusic Volume=100\nScroll Speed=abc\nParticipate In Chat=0\nShow Quick Help at Startup=-1\nQuick Chat F9=Hello there\nQuick Chat F10=\nNoEquals\n=novalue\nBig=99999999999\n");
    check(c.get_int("Sound Volume", 100, 0, 100) == 99, "a value in [0, 100) is taken");
    check(c.get_int("Music Volume", 65, 0, 100) == 65, "100 is not below the maximum: the default");
    check(c.get_int("Scroll Speed", 50, 0, 100) == 50, "a text is not a number: the default");
    check(c.get_int("Participate In Chat", 1, 0, 1) == 0, "0 is the one valid value of a switch");
    check(c.get_int("Show Quick Help at Startup", 1, 0, 1) == 1, "a negative number has a sign: the default");
    check(c.get_int("Missing", 7, 0, 100) == 7, "a missing key: the default");
    check(c.get_int("Big", 3, 0, 100) == 3, "a number that does not fit a DWORD: the default");
    check(c.get_int("Sound Volume", 100, 50, 100) == 99 && c.get_int("Sound Volume", 100, 0, 99) == 100, "both bounds are tested: min <= v < max");
    check(c.get_string("Quick Chat F9", "x", 100) == "Hello there", "a stored text");
    check(c.get_string("Quick Chat F10", "default", 100) == "" && c.has("Quick Chat F10"), "a stored empty text is a value: the default does not come back");
    check(c.get_string("Quick Chat F11", "default", 100) == "default", "a missing text: the default");
    check(c.get_string("Quick Chat F9", "x", 5) == "Hello", "a text is cut to the maximum");
    check(!c.has("NoEquals") && !c.has(""), "lines without a name or an equal sign are ignored");

    ConfigStore d;
    d.parse("T=a\tb\x01" "c\x7f" "d \xc3\xa9 e\r\nU=x\r\n");
    check(d.get_string("T", "", 100) == "abcd  e", "only printable ASCII is kept");
    check(d.get_string("U", "", 100) == "x", "a carriage return ends a line with it");
    d.set_string("V", "tab\there");
    check(d.get_string("V", "", 100) == "tabhere", "set_string drops what is not printable");

    // serialise / parse
    ConfigStore e;
    e.set_int("Sound Volume", 42);
    e.set_string("Quick Chat F9", "Hi = there");
    ConfigStore f;
    f.parse(e.serialise());
    check(f.get_int("Sound Volume", 0, 0, 100) == 42 && f.get_string("Quick Chat F9", "", 100) == "Hi = there", "a text with an equal sign survives a round trip");

    // a file
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ants_test_options";
    std::filesystem::create_directories(dir);
    const std::string file = (dir / "settings.ini").string();
    std::filesystem::remove(file);
    ConfigStore g;
    g.set_location(file);
    check(g.load() && g.values().empty(), "a missing file is an empty store");
    g.set_int("Music Volume", 12);
    g.set_string("Quick Chat F12", "Bye");
    check(std::filesystem::exists(file), "every write is saved at once");
    ConfigStore h;
    h.set_location(file);
    check(h.load() && h.get_int("Music Volume", 65, 0, 100) == 12 && h.get_string("Quick Chat F12", "", 100) == "Bye", "a new store reads what was saved");
    ConfigStore mem;
    mem.set_int("X", 1);
    check(mem.save() && mem.get_int("X", 0, 0, 10) == 1 && mem.location().empty(), "a store without a location lives in memory");
    ConfigStore bad;
    bad.set_location((dir / "no_such_folder" / "settings.ini").string());
    check(!bad.save(), "a file that cannot be written reports it");
    std::filesystem::remove_all(dir);
}

void test_options_state() {
    g_group = "options state";
    std::printf("[options] the settings of the options screen: defaults, load rules, write (FUN_0100a2c9, FUN_0101487c)\n");
    OptionsState st;
    check(st.sound_volume == 100 && st.music_volume == 65 && st.scroll_speed == 50 && st.chat && st.quick_help, "defaults: sound 100, music 65, scroll 50, chat on, quick help on");
    check(st.quick_chat[0] == "Now you are in for it!" && st.quick_chat[1] == "Let me be!" && st.quick_chat[2] == "Attack!" && st.quick_chat[3] == "Do you want to ally?",
          "the quick chats are the strings 18 - 21");
    check(std::string(OptionsState::key(OptionSetting::SoundVolume)) == "Sound Volume" && std::string(OptionsState::key(OptionSetting::MusicVolume)) == "Music Volume" &&
              std::string(OptionsState::key(OptionSetting::ScrollSpeed)) == "Scroll Speed" && std::string(OptionsState::key(OptionSetting::Chat)) == "Participate In Chat" &&
              std::string(OptionsState::key(OptionSetting::QuickHelp)) == "Show Quick Help at Startup" && std::string(OptionsState::key(OptionSetting::QuickChat1)) == "Quick Chat F9" &&
              std::string(OptionsState::key(OptionSetting::QuickChat4)) == "Quick Chat F12",
          "the profile names are the original's string table entries");

    ConfigStore c;
    c.parse("Sound Volume=100\nMusic Volume=7\nScroll Speed=99\nParticipate In Chat=0\nShow Quick Help at Startup=1\nQuick Chat F9=mine\nQuick Chat F10=\n");
    st.load(c);
    check(st.sound_volume == 100, "a stored Sound Volume of 100 is invalid and the default is 100");
    check(st.music_volume == 7 && st.scroll_speed == 99, "valid numbers are taken");
    check(!st.chat, "Participate In Chat = 0: off");
    check(st.quick_help, "Show Quick Help at Startup = 1: invalid, the default is on");
    check(st.quick_chat[0] == "mine" && st.quick_chat[1].empty() && st.quick_chat[2] == "Attack!", "texts: stored, stored empty, default");
    ConfigStore d;
    d.parse("Participate In Chat=2\nQuick Chat F12=" + std::string(150, 'q') + "\n");
    OptionsState s2;
    s2.load(d);
    check(s2.chat && s2.quick_chat[3].size() == 100, "a stored 2 is not 0: on; a long text is cut to 100");

    ConfigStore w;
    OptionsState s3;
    s3.sound_volume = 33;
    s3.music_volume = 44;
    s3.scroll_speed = 55;
    s3.chat = false;
    s3.quick_help = false;
    s3.quick_chat[2] = "Go go";
    for (auto setting : {OptionSetting::SoundVolume, OptionSetting::MusicVolume, OptionSetting::ScrollSpeed, OptionSetting::Chat, OptionSetting::QuickHelp, OptionSetting::QuickChat3}) {
        s3.write(w, setting);
    }
    OptionsState s4;
    s4.load(w);
    check(s4.sound_volume == 33 && s4.music_volume == 44 && s4.scroll_speed == 55 && !s4.chat && !s4.quick_help && s4.quick_chat[2] == "Go go" && s4.quick_chat[0] == "Now you are in for it!",
          "what the callbacks write is what the next start reads");
}

void test_sound_law() {
    g_group = "sound law";
    std::printf("[sound] the Sound Volume option as DirectSound hundredths of a dB (FUN_0102d803): 25 (v - 100), 0 = mute\n");
    check(AudioMixer::attenuation_centibels(0, 100) == -10000, "0 is the mute: -10000");
    check(AudioMixer::attenuation_centibels(1, 100) == -2475 && AudioMixer::attenuation_centibels(50, 100) == -1250 && AudioMixer::attenuation_centibels(65, 100) == -875 &&
              AudioMixer::attenuation_centibels(99, 100) == -25 && AudioMixer::attenuation_centibels(100, 100) == 0,
          "1, 50, 65, 99, 100: -2475, -1250, -875, -25, 0");
    // the music: the device volume word is v * 0xffff / 100 (FUN_0100e714)
    check((65 * 0xffff) / 100 == 42597 && (100 * 0xffff) / 100 == 0xffff && (0 * 0xffff) / 100 == 0, "the music word of 65 is 42597, of 100 0xffff");
}

struct Recorder {
    std::vector<OptionSetting> changes;
};

void test_options_screen() {
    g_group = "options screen";
    std::printf("[screen] the options screen: controls, events, callbacks at the release, Enter, no click outside (FUN_0101487c, FUN_01014f12)\n");
    OptionsScreen screen;
    Recorder rec;
    screen.set_on_change([&](OptionSetting s) { rec.changes.push_back(s); });
    check(!screen.is_open(), "closed at first");
    screen.on_press(400, 440, 0);
    screen.on_release(400, 440);
    check(rec.changes.empty(), "a closed screen takes no events");

    screen.open(1000);
    check(screen.is_open(), "open");
    check(screen.slider(0).value() == 99 && screen.slider(0).thumb_left() == 372, "the Sound slider stands at the end of its track");
    check(screen.slider(1).value() == 65 && screen.slider(2).value() == 50, "Music 65, Scroll 50");
    check(screen.chat_on().latched() && !screen.chat_off().latched() && screen.help_on().latched() && !screen.help_off().latched(), "both switches are on");
    check(screen.edit(0).focused() && !screen.edit(1).focused() && !screen.edit(2).focused() && !screen.edit(3).focused(), "the F9 field has the focus");
    check(screen.edit(0).text() == "Now you are in for it!" && screen.edit(3).text() == "Do you want to ally?", "the fields hold the quick chats");
    check(screen.edit(0).x() == 92 && screen.edit(0).y() == 370 && screen.edit(1).x() == 92 && screen.edit(1).y() == 402 && screen.edit(2).x() == 302 && screen.edit(2).y() == 370 &&
              screen.edit(3).x() == 302 && screen.edit(3).y() == 402 && screen.edit(0).w() == 141 && screen.edit(0).h() == 15,
          "fields: (92, 370), (92, 402), (302, 370), (302, 402), 141 x 15");

    // sliders: nothing happens before the release
    screen.on_press(300, 222, 1100);                             // the Music slider's track (y 216 .. 235)
    screen.on_move(250, 222);
    screen.on_move(330, 500);
    check(rec.changes.empty() && screen.state().music_volume == 65, "press and drag change nothing");
    check(screen.slider(1).value() == 64 && screen.slider(1).position() == 330, "the thumb follows the pointer (pos 330: value 119 100 / 185 = 64)");
    screen.on_release(330, 500);
    check(rec.changes.size() == 1 && rec.changes[0] == OptionSetting::MusicVolume && screen.state().music_volume == 64, "the release applies the value once");
    rec.changes.clear();

    // every event is preceded by the move (FUN_0102737e -> FUN_0102653f): the release moves the thumb to the pointer even when no motion came between. A plain click
    // therefore sets the value of the clicked position, and the far end of the track is 99: the Sound Volume 100 of the start cannot be set again
    screen.on_press(400, 185, 1200);
    screen.on_release(400, 185);
    check(rec.changes.size() == 1 && rec.changes[0] == OptionSetting::SoundVolume && screen.state().sound_volume == 99, "a click at the far end of the Sound track: 99");
    rec.changes.clear();
    screen.on_press(300, 185, 1250);
    screen.on_release(300, 185);
    check(rec.changes.size() == 1 && screen.state().sound_volume == 48 && screen.slider(0).thumb_left() == 277, "a click without a move sets the value of the clicked position: (300 - 211) 100 / 185 = 48");
    rec.changes.clear();
    // a press beside the track
    screen.on_press(300, 210, 1300);
    screen.on_release(300, 210);
    check(rec.changes.empty(), "a click between the tracks does nothing");

    // the Scroll Speed slider
    screen.on_press(304, 260, 1400);
    screen.on_move(211, 260);
    screen.on_release(211, 260);
    check(rec.changes.size() == 1 && rec.changes[0] == OptionSetting::ScrollSpeed && screen.state().scroll_speed == 0, "Scroll Speed: dragged to the left end: 0");
    rec.changes.clear();

    // switches: nothing at the press, both latches at the release
    screen.on_press(160, 300, 1500);                              // chat OFF
    check(rec.changes.empty() && screen.state().chat, "a press on Chat OFF changes nothing yet");
    screen.on_release(160, 300);
    check(rec.changes.size() == 1 && rec.changes[0] == OptionSetting::Chat && !screen.state().chat, "the release switches the chat off");
    check(screen.chat_off().latched() && !screen.chat_on().latched(), "Chat OFF is latched, Chat ON is not");
    rec.changes.clear();
    screen.on_press(160, 300, 1600);
    screen.on_release(160, 300);
    check(screen.chat_off().latched() && !screen.chat_on().latched() && !screen.state().chat, "clicking the latched OFF again leaves it latched");
    rec.changes.clear();
    screen.on_press(110, 300, 1700);                              // chat ON
    screen.on_move(300, 300);                                     // leaves
    screen.on_release(300, 300);
    check(rec.changes.empty() && !screen.state().chat, "a press that leaves the button before the release does nothing");
    screen.on_press(110, 300, 1800);
    screen.on_release(110, 300);
    check(screen.state().chat && screen.chat_on().latched() && !screen.chat_off().latched(), "Chat ON switches it on again");
    rec.changes.clear();
    screen.on_press(420, 300, 1900);                              // quick help OFF
    screen.on_release(420, 300);
    check(rec.changes.size() == 1 && rec.changes[0] == OptionSetting::QuickHelp && !screen.state().quick_help && screen.help_off().latched() && !screen.help_on().latched(),
          "Quick Help OFF");
    rec.changes.clear();
    screen.on_press(370, 300, 2000);                              // quick help ON
    screen.on_release(370, 300);
    check(screen.state().quick_help && screen.help_on().latched() && !screen.help_off().latched(), "Quick Help ON");
    rec.changes.clear();

    // edit fields: the presses on the sliders and switches took the focus from F9 (a press anywhere but on a field does); a press on the field gives it back
    check(!screen.edit(0).focused(), "the presses on the other controls took the focus from F9");
    screen.on_text("!!");
    check(rec.changes.empty(), "so nothing is typed");
    screen.on_press(150, 377, 2100);
    screen.on_release(150, 377);
    check(screen.edit(0).focused() && screen.is_open(), "a press on F9 gives it the focus");
    screen.on_text("!!");
    check(rec.changes.size() == 2 && rec.changes[0] == OptionSetting::QuickChat1 && screen.state().quick_chat[0] == "Now you are in for it!!!", "every typed character changes the quick chat at once");
    rec.changes.clear();
    screen.on_key(ScreenEdit::KEY_BACKSPACE);
    check(rec.changes.size() == 1 && screen.state().quick_chat[0] == "Now you are in for it!!", "Backspace");
    rec.changes.clear();
    screen.on_key(ScreenEdit::KEY_ESCAPE);
    check(screen.is_open() && rec.changes.empty(), "Esc does nothing");
    // a click on the F10 field moves the focus there (the press only)
    screen.on_press(200, 410, 3000);
    check(!screen.edit(0).focused() && screen.edit(1).focused(), "a press on F10 focuses it and takes the focus from F9");
    screen.on_release(200, 410);
    screen.on_text("ab");
    check(screen.state().quick_chat[1] == "Let me be!ab" && screen.state().quick_chat[0] == "Now you are in for it!!", "the text goes to the focused field only");
    rec.changes.clear();
    // a click anywhere else takes the focus away
    screen.on_press(300, 330, 3100);
    screen.on_release(300, 330);
    check(!screen.edit(1).focused(), "a click on the window takes the focus from every field");
    screen.on_text("zz");
    check(rec.changes.empty() && screen.state().quick_chat[1] == "Let me be!ab", "nothing is typed then");
    // clicks outside the window's controls never close it
    screen.on_press(5, 5, 3200);
    screen.on_release(5, 5);
    screen.on_press(630, 470, 3300);
    screen.on_release(630, 470);
    check(screen.is_open(), "a click outside the card does not close the screen");

    // Return: the release closes it, and only on the button
    screen.on_press(400, 440, 3400);
    check(screen.return_button().pressed() && screen.is_open(), "a press on Return only captures it");
    screen.on_move(10, 10);
    screen.on_release(10, 10);
    check(screen.is_open(), "a press that leaves the button before the release does not close it");
    screen.on_press(400, 440, 3500);
    screen.on_release(400, 440);
    check(!screen.is_open(), "the release on Return closes the screen");

    // Enter closes it whatever has the focus, typing included
    screen.open(4000);
    screen.on_text("x");
    check(screen.state().quick_chat[0] == "Now you are in for it!!x", "reopened: the F9 field has the focus again and takes text");
    screen.on_key(ScreenEdit::KEY_ENTER);
    check(!screen.is_open() && screen.state().quick_chat[0] == "Now you are in for it!!x", "Enter closes it; the text stays");
    screen.open(5000);
    screen.on_press(300, 330, 5100);
    screen.on_key(ScreenEdit::KEY_ENTER);
    check(!screen.is_open(), "Enter closes it with no field focused");
    // the screen is built from the state each time
    screen.state().sound_volume = 30;
    screen.state().chat = false;
    screen.open(6000);
    check(screen.slider(0).value() == 30 && screen.chat_off().latched() && !screen.chat_on().latched(), "reopened from the changed settings");
}

}  // namespace

int main() {
    test_slider();
    test_toggle();
    test_edit();
    test_config_store();
    test_options_state();
    test_sound_law();
    test_options_screen();
    std::printf("\noptions: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
