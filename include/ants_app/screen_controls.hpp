#pragma once

// The original's screen controls besides the plain button (screen_button.hpp): the latching button, the slider and the edit field, as pure models without
// drawing. They are the classes of Ants.exe that the options screen (and, for the edit field, the chat box) is made of; every rule below was read from the
// disassembly (docs/GAME_REVERSE_ENGINEERING.md 5.51).

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>

namespace ants::app {

/**
 * @brief The button class built with its toggle flag (constructor FUN_01010fcb, vtable 0x10025a8; mouse FUN_01011206, move FUN_01011281, FUN_010111d4).
 *
 * Four pictures: up, hover, down and "down + hover" (the fourth exists only for toggles). The state of the picture follows the pointer: inside the
 * rectangle a toggle that is neither latched nor captured shows the hover picture, every other case inside shows "down + hover"; outside it shows
 * "down" when latched and "up" otherwise, and the capture is lost for good. A press inside captures and shows "down". The release runs the callback
 * when the button is still captured, AFTER the latch flag was flipped (the options' callbacks set both latches of their pair themselves, so clicking the
 * button that is already latched leaves it latched). Every event is preceded by the move handler, which is why the rectangle test of a release needs
 * no code of its own.
 */
class ScreenToggle {
public:
    enum class Art : uint8_t { Up = 0, Hover = 1, Down = 2, DownHover = 3 };

    constexpr ScreenToggle() noexcept = default;
    constexpr ScreenToggle(int32_t x, int32_t y, int32_t w, int32_t h) noexcept : x_(x), y_(y), w_(w), h_(h) {}

    constexpr bool contains(int32_t px, int32_t py) const noexcept { return px >= x_ && px < x_ + w_ && py >= y_ && py < y_ + h_; }

    /// FUN_01011281
    void on_move(int32_t px, int32_t py) noexcept {
        if (contains(px, py)) {
            art_ = (!latched_ && !captured_) ? Art::Hover : Art::DownHover;
        } else {
            art_ = latched_ ? Art::Down : Art::Up;
            captured_ = false;
        }
    }
    /// FUN_01011206, a press: true when this button captured it
    bool on_press(int32_t px, int32_t py) noexcept {
        on_move(px, py);
        if (contains(px, py)) {
            captured_ = true;
            art_ = Art::Down;
        }
        return captured_;
    }
    /// FUN_01011206, a release: true when the callback runs. The latch flag is already flipped then; the caller's callback may set it (set_latched).
    bool on_release(int32_t px, int32_t py) noexcept {
        on_move(px, py);
        const bool fire = captured_;
        if (fire) latched_ = !latched_;
        art_ = latched_ ? Art::Down : Art::Up;
        captured_ = false;
        return fire;
    }
    /// FUN_010111d4: the latch is set and the picture follows (down for a latched button, up for the other)
    void set_latched(bool latched) noexcept {
        latched_ = latched;
        art_ = latched ? Art::Down : Art::Up;
    }
    /// A press that ends with no act (the touch was taken away): the capture is gone and nothing fires; the latch stays as it was
    void cancel_press() noexcept {
        captured_ = false;
        art_ = latched_ ? Art::Down : Art::Up;
    }

    constexpr bool latched() const noexcept { return latched_; }
    constexpr bool captured() const noexcept { return captured_; }
    constexpr Art art() const noexcept { return art_; }

private:
    int32_t x_{0};
    int32_t y_{0};
    int32_t w_{0};
    int32_t h_{0};
    bool latched_{false};
    bool captured_{false};
    Art art_{Art::Up};
};

/**
 * @brief The slider class (constructor FUN_01011397, vtable 0x10025d0; position FUN_01011543, mouse FUN_010115ca / FUN_0101161b, hit test FUN_01011656).
 *
 * The thumb's centre `pos` runs from x + 23 to x + 207 (the track is 185 px, the thumb 46 px wide). A value v of 0 .. max is placed by the constructor at
 * pos = x + 23 + (185 v) / (max - 1) (so the value 100 lies beyond the end and is clamped, and reads back as 99); the value that a position stands for is
 * ((pos - left) max) / 185. A press inside [x, x + 231) x [y, y + 20) starts the drag and changes NOTHING by itself; only a pointer move while dragging sets the
 * position (clamped to the track), and the release calls the callback with the value (the owner does that when on_release returns true). Since every event of the
 * original is preceded by a move to the pointer's position (FUN_0102737e calls FUN_0102653f first), the owner calls on_move before on_release: a click without
 * any motion therefore sets the value of the clicked position.
 */
class ScreenSlider {
public:
    static constexpr int32_t THUMB_W = 46;                  // [+0x1c] = 0x2e
    static constexpr int32_t TRACK_LEFT_OFFSET = 23;        // [+0x24] = x + 0x17
    static constexpr int32_t TRACK_RIGHT_OFFSET = 208;      // [+0x28] = x + 0xd0 (exclusive)
    static constexpr int32_t TRACK_SPAN = TRACK_RIGHT_OFFSET - TRACK_LEFT_OFFSET;    // 185
    static constexpr int32_t HIT_W = TRACK_RIGHT_OFFSET + THUMB_W / 2;               // 231: [x, x + 231)
    static constexpr int32_t HIT_H = 20;

    constexpr ScreenSlider() noexcept = default;
    /// `initial` is the configured value; the thumb's sprite row is y - 1 (the constructor stores `y - 1` at +0x20)
    constexpr ScreenSlider(int32_t x, int32_t y, int32_t initial, int32_t max = 100) noexcept : x_(x), y_(y), max_(max) {
        set_position(x_ + TRACK_LEFT_OFFSET + (TRACK_SPAN * initial) / (max_ - 1));
    }

    /// FUN_01011656: the rectangle of the track and thumb
    constexpr bool hit(int32_t px, int32_t py) const noexcept { return px >= x_ && px < x_ + HIT_W && py >= y_ && py < y_ + HIT_H; }

    /// The value that the thumb stands for ([+0x18])
    constexpr int32_t value() const noexcept { return value_; }
    /// The thumb's centre
    constexpr int32_t position() const noexcept { return pos_; }
    /// Where the thumb's sprite is drawn: (centre - 23, y - 1)
    constexpr int32_t thumb_left() const noexcept { return pos_ - THUMB_W / 2; }
    constexpr int32_t thumb_top() const noexcept { return y_ - 1; }
    constexpr bool dragging() const noexcept { return dragging_; }

    /// FUN_0101161b: while dragging the position follows the pointer's x
    constexpr void on_move(int32_t px, int32_t /*py*/) noexcept {
        if (dragging_) set_position(px);
    }
    /// FUN_010115ca, a press: true when the drag started (the value stays)
    constexpr bool on_press(int32_t px, int32_t py) noexcept {
        if (!hit(px, py)) return false;
        dragging_ = true;
        return true;
    }
    /// FUN_010115ca, a release: true when the callback runs (a drag was going on), with `value()`
    constexpr bool on_release() noexcept {
        const bool fire = dragging_;
        dragging_ = false;
        return fire;
    }

private:
    /// FUN_01011543: the centre is clamped to [x + 23, x + 207] and the value follows
    constexpr void set_position(int32_t pos) noexcept {
        const int32_t left = x_ + TRACK_LEFT_OFFSET;
        const int32_t right = x_ + TRACK_RIGHT_OFFSET;
        if (pos < left) pos = left;
        else if (pos >= right) pos = right - 1;
        pos_ = pos;
        value_ = ((pos_ - left) * max_) / TRACK_SPAN;
    }

    int32_t x_{0};
    int32_t y_{0};
    int32_t max_{100};
    int32_t pos_{0};
    int32_t value_{-1};
    bool dragging_{false};
};

/**
 * @brief The edit field (constructor FUN_010119a8, vtable 0x1002648; characters FUN_01011d86, mouse FUN_01011c9f, focus FUN_01011d0e).
 *
 * A one-line box (x, y, w, h) that holds at most `max_chars` (100) characters. While it has the focus it takes the printable characters 0x20 .. 0x7e (appended
 * when the text is shorter than the maximum; a full box drops them) and Backspace (the key id 0x19), and every change calls the owner's callback with the new
 * text at once. A left press anywhere gives the focus to the field under the pointer and takes it from every other field. The caret is an underscore after
 * the text that shows from the moment of the focus and toggles every 150 ms while the field has it.
 */
class ScreenEdit {
public:
    static constexpr int32_t KEY_BACKSPACE = 0x19;          // the original's key ids (docs 5.45)
    static constexpr int32_t KEY_ENTER = 0x18;
    static constexpr int32_t KEY_ESCAPE = 0x1a;
    static constexpr uint32_t CARET_HALF_PERIOD_MS = 150;   // the caret task (FUN_01011d0e posts it with 0x96)

    ScreenEdit() = default;
    ScreenEdit(int32_t x, int32_t y, int32_t w, int32_t h, std::string text, size_t max_chars = 100) : x_(x), y_(y), w_(w), h_(h), max_(max_chars), text_(std::move(text)) {
        if (text_.size() > max_) text_.resize(max_);
    }

    /// FUN_01011d77: the rectangle [x, x + w) x [y, y + h)
    bool hit(int32_t px, int32_t py) const noexcept { return px >= x_ && px < x_ + w_ && py >= y_ && py < y_ + h_; }

    const std::string& text() const noexcept { return text_; }
    int32_t x() const noexcept { return x_; }
    int32_t y() const noexcept { return y_; }
    int32_t w() const noexcept { return w_; }
    int32_t h() const noexcept { return h_; }
    bool focused() const noexcept { return focused_; }

    /// FUN_01011d0e: the focus changes (no change, no effect: the caret keeps its phase). The text's "tail" flag (the label's +0x18: a text wider than the box is
    /// right aligned, so that its end shows) is set by the constructor and by every gain of the focus, and cleared by every loss of it.
    void focus(bool on, uint32_t now_ms) noexcept {
        if (on == focused_) return;
        focused_ = on;
        tail_aligned_ = on;
        if (on) focus_ms_ = now_ms;
    }
    /// True: a text that does not fit shows its end (right aligned); false: its beginning
    bool tail_aligned() const noexcept { return tail_aligned_; }
    /// FUN_01011c9f, a left press: the field under the pointer gets the focus, any other loses it
    void on_press(int32_t px, int32_t py, uint32_t now_ms) noexcept { focus(hit(px, py), now_ms); }

    /// Whether the caret is drawn: from the focus on it shows for 150 ms, is gone for 150 ms, and so on
    bool caret_visible(uint32_t now_ms) const noexcept { return focused_ && ((now_ms - focus_ms_) / CARET_HALF_PERIOD_MS) % 2 == 0; }

    /// FUN_01011d86: a key id of the original. True when the text changed (the owner's callback runs with it)
    bool on_char(int32_t key_id) {
        if (!focused_) return false;
        if (key_id >= 0x20 && key_id <= 0x7e) {
            if (text_.size() >= max_) return false;
            text_.push_back(static_cast<char>(key_id));
            return true;
        }
        if (key_id == KEY_BACKSPACE && !text_.empty()) {
            text_.pop_back();
            return true;
        }
        return false;
    }

private:
    int32_t x_{0};
    int32_t y_{0};
    int32_t w_{0};
    int32_t h_{0};
    size_t max_{100};
    std::string text_;
    bool focused_{false};
    bool tail_aligned_{true};
    uint32_t focus_ms_{0};
};

}  // namespace ants::app
