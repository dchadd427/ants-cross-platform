#pragma once

#include <cstdint>

namespace ants::app {

/// A rectangle of the screen (right and bottom edge excluded): the bounds of one picture of a button
struct ButtonRect {
    int32_t x{0};
    int32_t y{0};
    int32_t w{0};
    int32_t h{0};

    constexpr bool contains(int32_t px, int32_t py) const noexcept { return px >= x && px < x + w && py >= y && py < y + h; }
    constexpr bool operator==(const ButtonRect& o) const noexcept { return x == o.x && y == o.y && w == o.w && h == o.h; }
    constexpr bool operator!=(const ButtonRect& o) const noexcept { return !(*this == o); }
};

/**
 * @brief The original's button class (Ants.exe constructor FUN_01010fcb, mouse FUN_01011206, move FUN_01011281, hit test FUN_010112e9; docs 5.45, 5.52).
 *
 * The hit test is the bounding rectangle of the picture the button shows NOW (the union of its parts' rectangles, offset by their dx, dy; the animation's own "box"
 * is never used): the resting and the hovering picture (`up`) while the button is not captured, the pressed picture (`pressed`) while it is. That makes three rules:
 *  - a press captures the button only when it hits the `up` rectangle; the pressed picture shows and the sound that it carries plays;
 *  - every move (and every button event is preceded by one: FUN_0102737e calls FUN_0102653f first; the 50 ms INPUT task also sends the pointer to the top
 *    window whenever its queue is empty) is tested against the picture that shows now: inside = hover (or pressed while captured), outside = up and the capture
 *    is gone for good (coming back only hovers); after a capture the very same input run sends a move against the PRESSED rectangle, so a press in a strip that
 *    belongs to the resting picture only (the pressed picture is smaller) is cancelled at once (it still played the sound: inferred);
 *  - the callback runs at the RELEASE and only while still captured: with a stationary pointer the click zone is the intersection of the two rectangles.
 * A picture of the same size for every state (`ScreenButton(x, y, w, h)`) is the old single rectangle; the buttons whose pictures differ pass both.
 */
class ScreenButton {
public:
    constexpr ScreenButton() noexcept = default;
    constexpr ScreenButton(int32_t x, int32_t y, int32_t w, int32_t h) noexcept : up_{x, y, w, h}, pressed_{x, y, w, h} {}
    constexpr ScreenButton(ButtonRect up, ButtonRect pressed) noexcept : up_(up), pressed_(pressed) {}

    /// The picture of the resting / hovering state and that of the pressed state
    constexpr const ButtonRect& up_rect() const noexcept { return up_; }
    constexpr const ButtonRect& pressed_rect() const noexcept { return pressed_; }
    /// FUN_010112e9: is the point on the picture that shows now?
    constexpr bool contains(int32_t px, int32_t py) const noexcept { return (captured_ ? pressed_ : up_).contains(px, py); }

    /// The pointer moved (FUN_01011281): inside = hover (pressed while captured), outside = up and a capture is gone for good
    void on_move(int32_t px, int32_t py) noexcept {
        inside_ = contains(px, py);
        if (!inside_) captured_ = false;
    }
    /// The left button went down (FUN_01011206): true when this button captured it (the pressed picture was put on, its sound plays). The move that ends the
    /// input run follows at once, now against the pressed picture's rectangle.
    bool on_press(int32_t px, int32_t py) noexcept {
        on_move(px, py);
        if (!inside_) return false;
        captured_ = true;
        on_move(px, py);
        return true;
    }
    /// The left button went up: true when the callback runs (the capture is still there)
    bool on_release(int32_t px, int32_t py) noexcept {
        on_move(px, py);
        const bool fire = captured_;
        captured_ = false;
        on_move(px, py);                                   // the picture is the resting one again: hover when the pointer is on it
        return fire;
    }
    /// The screen closes or the button is taken away: nothing stays pressed
    void reset() noexcept {
        inside_ = false;
        captured_ = false;
    }

    constexpr bool hovered() const noexcept { return inside_; }
    constexpr bool pressed() const noexcept { return captured_; }

private:
    ButtonRect up_{};
    ButtonRect pressed_{};
    bool inside_{false};
    bool captured_{false};
};

} // namespace ants::app
