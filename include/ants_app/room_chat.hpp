#pragma once

// The chat input of the waiting room (online rooms, protocol 11). The setup screen of the original has no chat; the remake's rooms do, and until the wide 16:9 screen has a chat box
// (MapSelectScreen::set_chat_panel) the line that a player types is shown where the screen's status line is: "Say: hello there_". This file is the MODEL of that input, with no window
// and no sockets (the application feeds it the keys and the clock), so that the tests drive it directly.
//
// The focus rule. The setup screen's own keys (S and Enter start, Q and X leave, Up and Down choose the map) belong to the screen while the input is CLOSED. T, or a click on the status
// line, OPENS the input; from then on every key goes to the input and NOTHING else: S, Q, X, Enter and the arrows do not act on the screen (a player who types "sorry, my queue" must
// not start the match or leave the room). Enter sends the line (an empty line sends nothing) and closes the input; Esc closes it without sending; both give the screen its keys back.

#include <cstddef>
#include <cstdint>
#include <string>

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
    static constexpr size_t kShownChars = 62;              // the status line holds two lines of about 40 characters: the end of the text that is shown (with the prompt)
    static constexpr uint32_t kCaretHalfPeriodMs = 150;    // as the original's edit field
    static constexpr const char* kPrompt = "Say: ";

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
    /// What the status line shows while the input is open: the prompt, the end of the text that fits and a blinking caret; `now_ms` is the clock of the application. Empty when closed.
    std::string display(uint32_t now_ms) const;

private:
    bool open_{false};
    bool swallow_{false};
    std::string text_;
};

}  // namespace ants::app
