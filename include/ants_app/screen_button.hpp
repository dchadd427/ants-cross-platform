#pragma once

#include <cstdint>

namespace ants::app {

/**
 * @brief The original's button class (Ants.exe constructor FUN_01010fcb, mouse FUN_01011206, move FUN_01011281; docs 5.45, 5.50).
 *
 * A press inside the rectangle captures the button: its pressed picture shows (and the sound that the pressed animation carries plays at once). The callback
 * runs at the RELEASE, and only while the button is still captured, that is when the pointer never left the rectangle since the press: leaving cancels the capture
 * for good (coming back only hovers). Every event is preceded by the button's move handler, so the position of the release needs no test of its own.
 * The state is the original's: up, hover (the pointer is inside), pressed (captured).
 */
class ScreenButton {
public:
    constexpr ScreenButton() noexcept = default;
    constexpr ScreenButton(int32_t x, int32_t y, int32_t w, int32_t h) noexcept : x_(x), y_(y), w_(w), h_(h) {}

    constexpr bool contains(int32_t px, int32_t py) const noexcept { return px >= x_ && px < x_ + w_ && py >= y_ && py < y_ + h_; }

    /// The pointer moved (FUN_01011281): inside = hover, outside = up, and a capture is gone for good
    void on_move(int32_t px, int32_t py) noexcept {
        inside_ = contains(px, py);
        if (!inside_) captured_ = false;
    }
    /// The left button went down (FUN_01011206): true when this button captured it (the pressed picture shows, its sound plays)
    bool on_press(int32_t px, int32_t py) noexcept {
        on_move(px, py);
        captured_ = inside_;
        return captured_;
    }
    /// The left button went up: true when the callback runs (the capture is still there)
    bool on_release(int32_t px, int32_t py) noexcept {
        on_move(px, py);
        const bool fire = captured_;
        captured_ = false;
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
    int32_t x_{0};
    int32_t y_{0};
    int32_t w_{0};
    int32_t h_{0};
    bool inside_{false};
    bool captured_{false};
};

} // namespace ants::app
