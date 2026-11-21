// The WebSocket client over TLS (see ants_net/wss.hpp): the URL, the upgrade request and its answer (pure functions the tests feed bytes), and WssConnection, which runs TCP, TLS, the upgrade and the frames
// one after the other from the game's polls. The frame codec is ws.hpp's.
#include "ants_net/wss.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <thread>

namespace ants::net {

namespace {

constexpr size_t kMaxReplyHeaders = 64;

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(const std::string& s) {
    size_t a = 0;
    size_t b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) --b;
    return s.substr(a, b - a);
}

// Whether a comma-separated header value holds the token (without regard to case): "keep-alive, Upgrade" holds "upgrade"
bool has_token(const std::string& value, const char* token) {
    size_t i = 0;
    while (i <= value.size()) {
        size_t j = value.find(',', i);
        if (j == std::string::npos) j = value.size();
        if (lower(trim(value.substr(i, j - i))) == token) return true;
        i = j + 1;
    }
    return false;
}

}  // namespace

// ------------------------------------------------------------------------------------------------
// The upgrade

std::string wss_build_request(const WssUrl& url, const std::string& key_b64) {
    const bool v6 = url.host.find(':') != std::string::npos;
    std::string r = "GET " + url.target + " HTTP/1.1\r\n";
    r += "Host: " + (v6 ? "[" + url.host + "]" : url.host) + (url.port != kWssDefaultPort ? ":" + std::to_string(url.port) : std::string()) + "\r\n";
    r += "Upgrade: websocket\r\n";
    r += "Connection: Upgrade\r\n";
    r += "Sec-WebSocket-Key: " + key_b64 + "\r\n";
    r += "Sec-WebSocket-Version: 13\r\n";
    r += "\r\n";
    return r;
}

WssReply wss_parse_reply(const std::string& bytes, const std::string& key_b64) {
    WssReply out;
    const size_t end = bytes.find("\r\n\r\n");
    if (end == std::string::npos) {
        if (bytes.size() > kWssMaxReplyBytes) {
            out.status = WssReply::Status::Refused;
            out.why = "The server's answer is too long.";
        }
        return out;
    }
    if (end + 4 > kWssMaxReplyBytes) {
        out.status = WssReply::Status::Refused;
        out.why = "The server's answer is too long.";
        return out;
    }
    const std::string head = bytes.substr(0, end);
    // the status line: HTTP/1.1 101 text
    const size_t line_end = head.find("\r\n");
    const std::string status_line = head.substr(0, line_end);
    int status = 0;
    if (status_line.size() < 12 || status_line.compare(0, 9, "HTTP/1.1 ") != 0 || !std::isdigit(static_cast<unsigned char>(status_line[9])) ||
        !std::isdigit(static_cast<unsigned char>(status_line[10])) || !std::isdigit(static_cast<unsigned char>(status_line[11])) ||
        (status_line.size() > 12 && status_line[12] != ' ')) {
        out.status = WssReply::Status::Refused;
        out.why = "The server did not answer like a game server.";
        return out;
    }
    status = (status_line[9] - '0') * 100 + (status_line[10] - '0') * 10 + (status_line[11] - '0');
    out.http_status = status;
    if (status != 101) {
        out.status = WssReply::Status::Refused;
        out.why = status == 502 || status == 503 || status == 504 ? "The game server is not reachable right now."
                                                                  : "The server did not accept the game connection (HTTP " + std::to_string(status) + ").";
        return out;
    }
    // the headers
    bool upgrade_ok = false;
    bool connection_ok = false;
    bool accept_ok = false;
    bool unwanted = false;
    size_t count = 0;
    size_t pos = line_end == std::string::npos ? head.size() : line_end + 2;
    while (pos < head.size()) {
        size_t next = head.find("\r\n", pos);
        if (next == std::string::npos) next = head.size();
        const std::string line = head.substr(pos, next - pos);
        pos = next + 2;
        if (++count > kMaxReplyHeaders) {
            out.status = WssReply::Status::Refused;
            out.why = "The server's answer has too many header lines.";
            return out;
        }
        const size_t colon = line.find(':');
        if (colon == std::string::npos || colon == 0) {
            out.status = WssReply::Status::Refused;
            out.why = "The server did not answer like a game server.";
            return out;
        }
        const std::string name = lower(line.substr(0, colon));
        const std::string value = trim(line.substr(colon + 1));
        if (name == "upgrade") upgrade_ok = upgrade_ok || has_token(value, "websocket");
        else if (name == "connection") connection_ok = connection_ok || has_token(value, "upgrade");
        else if (name == "sec-websocket-accept") accept_ok = value == ws_accept_key(key_b64);
        else if (name == "sec-websocket-extensions" || name == "sec-websocket-protocol") unwanted = true;       // (none was asked for)
    }
    if (!upgrade_ok || !connection_ok || !accept_ok || unwanted) {
        out.status = WssReply::Status::Refused;
        out.why = "The server did not accept the upgrade to a game connection.";
        return out;
    }
    out.status = WssReply::Status::Accepted;
    out.consumed = end + 4;
    return out;
}

// ------------------------------------------------------------------------------------------------
// The connection

// What the thread that primes the roots leaves for the connection: 0 while it runs, then 1 (the system was asked: read the roots again) or 2 (nothing to ask)
struct WssConnection::Priming {
    std::atomic<int> state{0};
};

WssConnection::WssConnection(const WssUrl& url, std::unique_ptr<TlsClient> tls, uint32_t timeout_ms, const std::string& address, const TlsTrust& trust)
    : url_(url), tls_(std::move(tls)), started_(std::chrono::steady_clock::now()), timeout_ms_(timeout_ms), address_(address), trust_(trust), parser_(false, kMaxMessageBytes) {}

WssConnection::~WssConnection() { close(); }

std::unique_ptr<WssConnection> WssConnection::connect(const WssUrl& url, const std::string& address, const TlsTrust& trust, std::string& why, uint32_t timeout_ms) {
    std::unique_ptr<TcpStream> stream = TcpStream::connect(address.empty() ? url.host : address, url.port);
    if (!stream) {
        why = "The server could not be reached.";
        return nullptr;
    }
    std::unique_ptr<TlsClient> tls = TlsClient::start(std::move(stream), url.host, trust, why);
    if (!tls) return nullptr;
    std::unique_ptr<WssConnection> c(new WssConnection(url, std::move(tls), timeout_ms, address, trust));
    std::array<uint8_t, 16> key{};
    if (!tls_random(key.data(), key.size())) {
        why = "The security library has no random numbers on this computer.";
        return nullptr;
    }
    c->key_b64_ = ws_base64_encode(key.data(), key.size());
    return c;
}

void WssConnection::fail(const std::string& why) {
    if (state_ == State::Failed || state_ == State::Closed) return;
    state_ = State::Failed;
    stage_ = Stage::Done;
    if (failure_.empty()) failure_ = why;
    if (tls_) tls_->close();
}

void WssConnection::finish(State end_state) {
    state_ = end_state;
    stage_ = Stage::Done;
    if (tls_) tls_->close();
}

bool WssConnection::enqueue(WsOpcode opcode, const uint8_t* data, size_t size) {
    if (!tls_ || tls_->state() != TlsClient::State::Open) return false;
    std::array<uint8_t, 4> mask{};
    if (!tls_random(mask.data(), mask.size())) {
        fail("The security library has no random numbers on this computer.");
        return false;
    }
    const std::vector<uint8_t> frame = ws_encode_frame(opcode, data, size, true, &mask);
    return tls_->write(frame.data(), frame.size()) >= 0;
}

// The handshakes of TCP and TLS, then the upgrade. True when the frames flow (the link is Open); false while it is still being made or after it failed.
bool WssConnection::pump_handshakes() {
    if (stage_ == Stage::Done) return false;
    if (stage_ != Stage::Frames && std::chrono::steady_clock::now() - started_ > std::chrono::milliseconds(timeout_ms_)) {
        fail("The server did not answer in time.");
        return false;
    }
    if (stage_ == Stage::Priming) {
        const int answer = priming_->state.load();
        if (answer == 0) return false;                                          // (the system is still being asked; the time limit above runs)
        priming_.reset();
        if (answer != 1 || !restart_link()) {
            fail(tls_->error());                                                // nothing to learn, or no new link: the link fails as it did
            return false;
        }
        return false;
    }
    if (stage_ == Stage::Tls) {
        switch (tls_->advance()) {
            case TlsClient::State::Handshaking: return false;
            case TlsClient::State::Failed:
                if (start_priming()) return false;
                fail(tls_->error());
                return false;
            case TlsClient::State::Closed: fail("The server closed the connection before the secure link was made."); return false;
            case TlsClient::State::Open: break;
        }
        const std::string request = wss_build_request(url_, key_b64_);
        if (tls_->write(reinterpret_cast<const uint8_t*>(request.data()), request.size()) < 0) {
            fail(tls_->error().empty() ? "The secure link to the server broke." : tls_->error());
            return false;
        }
        stage_ = Stage::Upgrade;
    }
    if (stage_ == Stage::Upgrade) {
        tls_->advance();                                                        // (pushes the request on)
        uint8_t buf[2048];
        for (int rounds = 0; rounds < 8 && stage_ == Stage::Upgrade; ++rounds) {
            const long n = tls_->read(buf, sizeof(buf));
            if (n < 0) {
                fail(tls_->error().empty() ? "The server closed the connection before it accepted the game connection." : tls_->error());
                return false;
            }
            if (n == 0) break;
            reply_.append(reinterpret_cast<const char*>(buf), static_cast<size_t>(n));
            const WssReply r = wss_parse_reply(reply_, key_b64_);
            if (r.status == WssReply::Status::Refused) {
                refused_status_ = r.http_status;
                fail(r.why);
                return false;
            }
            if (r.status == WssReply::Status::Accepted) {
                if (r.consumed < reply_.size()) parser_.feed(reinterpret_cast<const uint8_t*>(reply_.data()) + r.consumed, reply_.size() - r.consumed);      // (frames that came with the answer)
                reply_.clear();
                reply_.shrink_to_fit();
                stage_ = Stage::Frames;
                state_ = State::Open;
            }
        }
        if (stage_ == Stage::Upgrade) {
            const WssReply r = wss_parse_reply(reply_, key_b64_);              // (an answer that is too long without its blank line)
            if (r.status == WssReply::Status::Refused) {
                refused_status_ = r.http_status;
                fail(r.why);
            }
            return false;
        }
    }
    return stage_ == Stage::Frames;
}

// A chain that failed only because its root is not trusted is given to the system once (on a thread of its own: Windows may go to the network), and the link is made again when the answer is in
bool WssConnection::start_priming() {
    if (primed_ || !trust_.system_roots || tls_->failure() != TlsFailure::Untrusted) return false;
    const std::vector<std::string> chain = tls_->presented_chain();
    if (chain.empty()) return false;
    primed_ = true;
    priming_ = std::make_shared<Priming>();
    std::thread([state = priming_, chain]() { state->state.store(prime_system_roots(chain) ? 1 : 2); }).detach();
    stage_ = Stage::Priming;
    started_ = std::chrono::steady_clock::now();                                // (the time limit of the link starts again: the system may take a while)
    return true;
}

// A new TCP and TLS link to the same address, with the roots that the system has now
bool WssConnection::restart_link() {
    reload_system_roots();
    std::unique_ptr<TcpStream> stream = TcpStream::connect(address_.empty() ? url_.host : address_, url_.port);
    if (!stream) return false;
    std::string why;
    std::unique_ptr<TlsClient> next = TlsClient::start(std::move(stream), url_.host, trust_, why);
    if (!next) return false;
    tls_ = std::move(next);
    stage_ = Stage::Tls;
    started_ = std::chrono::steady_clock::now();
    return true;
}

bool WssConnection::drain_parser() {
    std::vector<uint8_t> payload;
    uint16_t status = 0;
    for (;;) {
        switch (parser_.next(payload, status)) {
            case WsFrameParser::Result::NeedMore:
                return true;
            case WsFrameParser::Result::Message:
                inbox_bytes_ += payload.size();
                messages_.push_back(std::move(payload));
                payload = std::vector<uint8_t>();
                break;
            case WsFrameParser::Result::Ping:
                if (!enqueue(WsOpcode::Pong, payload.data(), payload.size())) {
                    fail("The secure link to the server broke.");
                    return false;
                }
                break;
            case WsFrameParser::Result::Pong:
                break;
            case WsFrameParser::Result::Close: {
                // the server's status is echoed (nothing, if it sent none), then the link is closed
                const std::vector<uint8_t> body = status == kWsCloseNoStatus ? std::vector<uint8_t>() : ws_close_payload(status);
                enqueue(WsOpcode::Close, body.data(), body.size());
                if (tls_) tls_->flush();
                finish(State::Closed);
                return false;
            }
            case WsFrameParser::Result::Error: {
                // the server broke the protocol: say with which status, then close
                const std::vector<uint8_t> body = ws_close_payload(status);
                enqueue(WsOpcode::Close, body.data(), body.size());
                if (tls_) tls_->flush();
                fail("The server broke the rules of the game connection.");
                return false;
            }
        }
    }
}

void WssConnection::pump() {
    if (state_ == State::Closed || state_ == State::Failed || !tls_) return;
    if (!pump_handshakes()) return;
    if (!drain_parser()) return;
    tls_->advance();
    if (tls_->state() == TlsClient::State::Failed) return fail(tls_->error());
    uint8_t buf[16384];
    for (int rounds = 0; rounds < 16; ++rounds) {                               // bounded per pump so that a flooding peer cannot stall the game
        if (messages_.size() >= TcpConnection::kMaxInboxMessages || inbox_bytes_ >= TcpConnection::kMaxInboxBytes) break;      // not read until the game has taken what is here
        const long n = tls_->read(buf, sizeof(buf));
        if (n > 0) {
            parser_.feed(buf, static_cast<size_t>(n));
            if (!drain_parser()) return;
        } else if (n == 0) {
            break;
        } else if (tls_->state() == TlsClient::State::Failed) {
            return fail(tls_->error());
        } else {                                                                // the server closed the link without a close frame
            return finish(State::Closed);
        }
    }
}

bool WssConnection::send(const std::vector<uint8_t>& message) {
    if (message.size() > kMaxMessageBytes) return false;
    if (state_ != State::Open) {
        if (state_ != State::Connecting) return false;
        pump();                                                                 // (the game sends from the moment it has a link: the handshakes may be done by now)
        if (state_ != State::Open) return false;
    }
    if (!enqueue(WsOpcode::Binary, message.data(), message.size())) {
        if (state_ == State::Open) fail(tls_ && !tls_->error().empty() ? tls_->error() : "The secure link to the server broke.");
        return false;
    }
    pump();
    return state_ == State::Open;
}

bool WssConnection::poll(std::vector<uint8_t>& message) {
    pump();
    if (messages_.empty()) return false;
    message = std::move(messages_.front());
    messages_.pop_front();
    inbox_bytes_ -= message.size();
    return true;
}

void WssConnection::close() {
    if (state_ == State::Open && tls_) {
        const std::vector<uint8_t> body = ws_close_payload(kWsCloseNormal);
        enqueue(WsOpcode::Close, body.data(), body.size());                     // goodbye, best effort: nothing is waited for
        tls_->flush();
    }
    if (state_ != State::Failed && state_ != State::Closed) finish(State::Closed);
    else if (tls_) tls_->close();
}

}  // namespace ants::net
