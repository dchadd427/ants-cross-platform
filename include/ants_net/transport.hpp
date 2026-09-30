#pragma once

// One message-oriented, reliable, ordered connection between two machines. WebRTC data channels, WebSocket and framed TCP all fit; so does the
// in-memory link of the tests. Nothing here blocks: the game polls it from its main loop (a WebAssembly build has no threads).

#include <cstdint>
#include <vector>

namespace ants::net {

class Connection {
public:
    enum class State : uint8_t { Connecting, Open, Closed, Failed };

    virtual ~Connection() = default;
    /// Queues one whole message (at most kMaxMessageBytes). False when the connection is not open.
    virtual bool send(const std::vector<uint8_t>& message) = 0;
    /// The next received message, in the order it was sent; false when nothing is waiting.
    virtual bool poll(std::vector<uint8_t>& message) = 0;
    virtual State state() const = 0;
    virtual void close() = 0;
    bool is_open() const { return state() == State::Open; }
};

}  // namespace ants::net
