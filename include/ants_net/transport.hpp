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
    /// How long the message that the last successful poll() returned had been waiting to be polled, in whole milliseconds of REAL time, for a transport that can tell: the
    /// browser's WebSocket delivers a message in an event between two frames and the connection stamps it with the browser's clock then, so a game that is drawn at 30, 15 or 1
    /// frame a second (a hidden tab) can tell a Pong that has just come from one that waited for the next frame. 0 for a transport that cannot: a native socket is read when
    /// the game polls it, so what it returns is as old as the frame that polls it is late (a frame of a window at 60 Hz: up to 16.7 ms).
    virtual uint32_t last_message_age_ms() const { return 0; }
};

}  // namespace ants::net
