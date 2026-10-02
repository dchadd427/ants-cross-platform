#pragma once

// The age of a received message in whole milliseconds, from two readings of one clock (a transport that stamps each message when it arrives and reads the clock again when
// the game takes it: Connection::last_message_age_ms). Pure arithmetic in a header of its own so that it is tested on every platform although the only transport that uses it
// is the browser's (wasm_ws.cpp, with emscripten_get_now() in milliseconds as a double).

#include <cstdint>

namespace ants::net {

/// `now_ms` - `arrived_ms` rounded to the nearest millisecond; 0 when the clock did not move or went back (or a reading is not a number), the most a 32-bit count holds when it
/// is more than that
inline uint32_t message_age_ms(double now_ms, double arrived_ms) noexcept {
    const double waited = now_ms - arrived_ms;
    if (!(waited > 0.0)) return 0;                                  // (also false for NaN)
    if (waited >= 4294967295.0) return UINT32_MAX;
    return static_cast<uint32_t>(waited + 0.5);
}

}  // namespace ants::net
