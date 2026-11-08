#pragma once

// What session.cpp (the host's half of the session) and session_client.cpp (the guest's half) both use. It is not part of the library's interface.

#include <cstdint>

namespace ants::net::session_detail {

inline constexpr uint8_t bit(uint8_t seat) noexcept { return static_cast<uint8_t>(1u << seat); }

}  // namespace ants::net::session_detail
