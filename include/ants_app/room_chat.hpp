#pragma once

// The chat input of the waiting room (online rooms, protocol 11). The setup screen of the original has no chat; the remake's rooms do. The wide 16:9 setup screen's chat box (a later step)
// will use this model too; until then the line that a player types is shown where the screen's status line is: "Say: hello there_" (MapSelectScreen draws it as a typed line, in the two lines
// of 14 px that the label holds, and the end of a long line shows). This file is the MODEL of that input, with no window and no sockets (the application feeds it the keys and the clock), so
// that the tests drive it directly.
//
// The focus rule. The setup screen's own keys (S and Enter start, Q and X leave, Up and Down choose the map) belong to the screen while the input is CLOSED. T, or a click on the status
// line, OPENS the input; from then on every key goes to the input and NOTHING else: S, Q, X, Enter and the arrows do not act on the screen (a player who types "sorry, my queue" must
// not start the match or leave the room). Enter sends the line (an empty line sends nothing) and closes the input; Esc closes it without sending; both give the screen its keys back, but not at
// once: the application keeps START from a key that comes within kGuardMs of the closing (a second Enter, or the S of a habit, must not start the match that a line was just sent in).
// A click while the input is open closes it (it sends nothing) and is no click on the screen: a touch screen has no Esc.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "ants_net/flood.hpp"
#include "ants_net/lobby.hpp"

#if defined(__has_include)
  #if __has_include(<SDL.h>)
    #include <SDL.h>
  #elif __has_include(<SDL2/SDL.h>)
    #include <SDL2/SDL.h>
  #endif
#else
  #include <SDL2/SDL.h>
#endif

namespace ants::app {

class RoomChatInput {
public:
    /// What a key did while the input was open
    enum class Result : uint8_t { None, Send, Closed };

    static constexpr size_t kMaxChars = 100;               // the match's chat line (net::kMaxChatChars)
    static constexpr uint32_t kCaretHalfPeriodMs = 150;    // as the original's edit field
    static constexpr const char* kPrompt = "Say: ";
    /// After the input has closed (Enter, Esc, a click) the screen's START keys (Enter, the keypad's Enter, S) do nothing for this long (milliseconds of the application's clock)
    static constexpr double kGuardMs = 400.0;

    /// Opens the input with an empty line. `from_key`: it was the T key (the key press is followed by a text input event for the same key, which must not become the first character of the
    /// line: the next text event of "t" or "T" is dropped, until end_of_frame()); a click on the status line passes false.
    void open(bool from_key);
    /// Closes it and forgets the line (the room is left, the match begins, Esc)
    void close() noexcept;
    bool is_open() const noexcept { return open_; }

    /// A key press while open: Enter is Send (the line is in text(); take_line() hands it out and closes the input), Esc is Closed, Backspace deletes the last character (Ctrl / Cmd with it
    /// clears the line). Every other key does nothing here and nothing at all on the screen. Not open: None, and the caller gives the key to the screen.
    Result on_key(SDL_Keycode key, uint16_t modifiers);
    /// Typed text (SDL's text input): printable ASCII is added up to kMaxChars, anything else is dropped. Not open: ignored.
    void on_text(const std::string& text);
    /// The end of a frame: a text event of the key that opened the input comes with that key press, in the same batch of events, so nothing is dropped after the frame
    void end_of_frame() noexcept { swallow_ = false; }

    const std::string& text() const noexcept { return text_; }
    /// The line, and the input closed (called after Send); empty when there is none
    std::string take_line();
    /// Whether the caret is shown now (it blinks; `now_ms` is the clock of the application). False when the input is closed.
    bool caret(uint32_t now_ms) const noexcept { return open_ && (now_ms / kCaretHalfPeriodMs) % 2 == 0; }

private:
    bool open_{false};
    bool swallow_{false};
    std::string text_;
};

/// What of the lines that arrive in a room goes to the program's log (stderr; the browser build's console): the lines are test hooks' food (the end-to-end checks read "Room chat: Name: text"),
/// not a chat window, and a room that is flooded must not flood the log: at most kBurst lines at once and then kPerSecond a second; the lines beyond that are counted, and one line says how
/// many were left out ("(N more lines were not logged)") as soon as the budget allows it. (The rooms' own chat budget, net::ChatBudget, already holds the rate of an honest
/// room down to a line a second a player; this is the second wall, for a server that relays more.)
class RoomChatLog {
public:
    static constexpr uint32_t kBurst = 20;
    static constexpr uint32_t kPerSecond = 10;
    /// The lines to print for `lines` that arrived at `now_ms` (the application's clock), the left-out count included; each is the text after "Room chat: " ("(room)" for the room's own notice)
    std::vector<std::string> take(const std::vector<net::ChatLine>& lines, uint32_t now_ms);
    /// How many lines were left out since the last report
    uint64_t left_out() const noexcept { return left_out_; }

private:
    net::MessageBudget budget_;
    uint64_t left_out_{0};
};

}  // namespace ants::app
