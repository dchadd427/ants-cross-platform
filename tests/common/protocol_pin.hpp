// The network protocol number that the layout tests describe, in ONE place: a test that says "these are the layouts of protocol N" asserts that net::kProtocolVersion is this number, so a change of the protocol
// is a change of include/ants_net/protocol.hpp, web/front/lobby_net.js (PROTOCOL), this number and the golden bytes of the Hello (tests/data/lobby_messages.txt, which the encoder test prints again with
// ANTS_WRITE_LOBBY_MESSAGES=1), and no other line of a test. test_lockstep N2.1 holds the number itself to the protocol's history.
#pragma once

#include <cstdint>

namespace ants_test {
inline constexpr uint16_t kPinnedProtocol = 28;
}  // namespace ants_test
