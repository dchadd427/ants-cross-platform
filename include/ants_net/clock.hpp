#pragma once

// Clocks in milliseconds are 32 bits wide and wrap (a server's clock is its uptime: after 49.7 days). Two points of time are compared through their
// DIFFERENCE, never with < or >=: `now - since >= timeout` for "how long ago", time_reached() for "has this deadline come". Both stay right across the wrap
// as long as the two points are less than 24.8 days apart. A deadline must always be set from the clock (never left at 0).

#include <cstdint>

namespace ants::net {

/// True when `now_ms` is at or after `deadline_ms` (a deadline at most 24.8 days away, in either direction)
inline bool time_reached(uint32_t now_ms, uint32_t deadline_ms) noexcept { return static_cast<int32_t>(now_ms - deadline_ms) >= 0; }

}  // namespace ants::net
