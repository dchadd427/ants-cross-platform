#pragma once

// The one-line status box of the match screen (Ants.exe PostStatus FUN_0100e944, the CLEARSTAT and TXTFLASH tasks). There is ONE
// slot: a new post replaces the text (no queue, no priority, no de-duplication), the text disappears 5000 ms after the last
// non-empty post, and a post with the flash flag flickers for 500 ms first (the text is hidden on the odd 50 ms steps). An empty
// post clears the text at once and does not re-arm the timer. The remake advances it in game ticks of 50 ms.

#include <cstdint>
#include <string>

namespace ants::app {

class StatusLine {
public:
    static constexpr uint32_t kLifeTicks = 100;   // CLEARSTAT: Schedule(0, 5000), then PostStatus("")
    static constexpr uint32_t kFlashTicks = 10;   // TXTFLASH: Add(task, 50, 50), counts 500 ms down

    /// PostStatus(text, flag): replaces the text (an identical text is only re-posted), ends a running flash, and, for a
    /// non-empty text, restarts the flash (when `flash`) and the 5 s life. An empty text just clears.
    void post(const std::string& text, bool flash = false) {
        text_ = text;
        flashing_ = false;
        flash_ticks_ = 0;
        if (text_.empty()) return;
        flashing_ = flash;
        age_ticks_ = 0;
    }

    void clear() { post(std::string(), false); }

    /// Advances the tasks by `ticks` game ticks (50 ms each), one at a time so that the flash pattern is exact.
    void update(uint32_t ticks) {
        for (uint32_t t = 0; t < ticks; ++t) {
            if (text_.empty()) return;
            ++age_ticks_;
            if (flashing_) {
                ++flash_ticks_;
                if (flash_ticks_ >= kFlashTicks) flashing_ = false;
            }
            if (age_ticks_ >= kLifeTicks) {
                text_.clear();
                flashing_ = false;
                flash_ticks_ = 0;
                return;
            }
        }
    }

    const std::string& text() const noexcept { return text_; }
    bool empty() const noexcept { return text_.empty(); }
    bool flashing() const noexcept { return flashing_; }
    uint32_t age_ticks() const noexcept { return age_ticks_; }

    /// Whether the text is drawn now: during the flash it is hidden on the odd 50 ms steps ("#.#.#.#.#.##" over the first 12 steps).
    bool visible() const noexcept {
        if (text_.empty()) return false;
        return !flashing_ || (flash_ticks_ % 2) == 0;
    }

private:
    std::string text_;
    uint32_t age_ticks_{0};
    bool flashing_{false};
    uint32_t flash_ticks_{0};
};

}  // namespace ants::app
