#pragma once

// Framed, non-blocking TCP for LAN and development (native builds only: a browser cannot open raw sockets and uses WebRTC / WebSocket).
// A message travels as u32 length (little endian) + payload; a length above kMaxMessageBytes fails the connection, so a hostile peer cannot make the
// receiver allocate or read without bound. Nothing blocks: connect, send and receive make progress whenever the game polls. The messages that wait to be polled are bounded as
// well (kMaxInboxMessages, kMaxInboxBytes): when the game does not take them as fast as a peer sends them, the socket is not read, and TCP holds the sender back.

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "ants_net/transport.hpp"

namespace ants::net {

class TcpConnection final : public Connection {
public:
    /// Starts a connection to host:port (a numeric address or a name; the name lookup itself may block briefly). Returns nullptr when the address
    /// cannot be used at all. The state is Connecting until the handshake completes, then Open, or Failed.
    static std::unique_ptr<TcpConnection> connect(const std::string& host, uint16_t port);

    ~TcpConnection() override;
    TcpConnection(const TcpConnection&) = delete;
    TcpConnection& operator=(const TcpConnection&) = delete;

    bool send(const std::vector<uint8_t>& message) override;
    bool poll(std::vector<uint8_t>& message) override;
    State state() const override { return state_; }
    /// Closes this side. What was received and not polled is dropped with it (nobody reads it any more); what a peer sent before IT closed is delivered, as before.
    void close() override;

    /// The peer's address as text (diagnostics)
    const std::string& peer() const noexcept { return peer_; }
    /// Bytes waiting to be written (diagnostics)
    size_t backlog() const noexcept { return out_.size(); }
    /// Received messages that wait to be polled, and the bytes that were read but are not parsed into messages yet (diagnostics and the tests of the bound)
    size_t inbox() const noexcept { return messages_.size(); }
    size_t buffered() const noexcept { return in_.size(); }
    /// The most messages (and bytes of them) that wait to be polled. At half of either the socket is not read any more until the game has taken what is here; at the full
    /// size not even the bytes that were read are parsed. A flooding peer is held back by TCP instead of growing this process (the WebSocket connection has the same bound).
    static constexpr size_t kMaxInboxMessages = 4096;
    static constexpr size_t kMaxInboxBytes = 1024 * 1024;

private:
    friend class TcpListener;
    TcpConnection(int fd, bool connecting, std::string peer);
    void pump();
    void fail();

    int fd_{-1};
    State state_{State::Connecting};
    std::string peer_;
    std::vector<uint8_t> in_;                   // received bytes not yet parsed into messages
    std::vector<uint8_t> out_;                  // framed bytes not yet written
    std::deque<std::vector<uint8_t>> messages_;
    size_t inbox_bytes_{0};                     // the payload bytes of messages_
};

class TcpListener final {
public:
    /// Listens on `port` (0 = any free port; see port()). `loopback_only` accepts only connections from this machine. nullptr on failure.
    static std::unique_ptr<TcpListener> listen(uint16_t port, bool loopback_only = false);
    ~TcpListener();
    TcpListener(const TcpListener&) = delete;
    TcpListener& operator=(const TcpListener&) = delete;

    /// The next incoming connection (already Open), or nullptr when none is waiting
    std::unique_ptr<TcpConnection> accept();
    uint16_t port() const noexcept { return port_; }

private:
    TcpListener(int fd, uint16_t port) : fd_(fd), port_(port) {}
    int fd_;
    uint16_t port_;
};

}  // namespace ants::net
