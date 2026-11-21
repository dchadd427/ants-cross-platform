// Tests of the desktop game's encrypted link (ants_net/wss.hpp over ants_net/tls.hpp): a WebSocket client over TLS to a game server's door behind a proxy. The pure parts (the URL, the upgrade request and
// its answer, the addresses that plain TCP may still go to) are fed bytes; the link itself runs over real sockets on the loopback interface against a TLS terminator that stands in for the site's proxy
// (tests/common/tls_test_server.hpp: a test authority issues its certificates, the client trusts that authority explicitly and nothing in the product skips a check) and the real WebSocket door
// of the game server (WsListener). Checked: messages in both directions in order over the whole range of sizes; nothing readable on the wire; the refusals of the certificate (an authority that is not
// trusted, the system's own roots alone, another name, an address that the certificate does not name, a certificate that has expired or is not valid yet); a server that does not speak TLS; the
// refusals of the upgrade; a server that never answers; the server's protocol errors, ping, close and fragments; a cable that is pulled; frames that are masked with fresh keys.
#include "ants_net/protocol.hpp"
#include "ants_net/tcp.hpp"
#include "ants_net/tls.hpp"
#include "ants_net/transport.hpp"
#include "ants_net/ws.hpp"
#include "ants_net/wss.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <exception>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "../common/tls_test_server.hpp"

using namespace ants::net;
using ants_test_tls::Ca;
using ants_test_tls::Identity;
using ants_test_tls::Names;
using ants_test_tls::Terminator;
using ants_test_tls::TerminatorOptions;

static int g_test_count = 0;
static int g_test_failures = 0;
static int g_assert_count = 0;

inline void run_test_case(const std::string& name, const std::function<void()>& fn) {
    if (const char* filter = std::getenv("ANTS_TEST_FILTER")) {
        if (name.find(filter) == std::string::npos) return;
    }
    ++g_test_count;
    std::cout << "  RUNNING: " << std::left << std::setw(100) << name.substr(0, 100) << " ... " << std::flush;
    const int prev = g_test_failures;
    try {
        fn();
    } catch (const std::exception& e) {
        std::cout << "FAILED! Exception: " << e.what() << "\n";
        ++g_test_failures;
        return;
    }
    if (g_test_failures == prev) std::cout << "PASS\n";
}

#include "../common/test_assert.hpp"

namespace {

using Bytes = std::vector<uint8_t>;

Bytes bytes_of(const std::string& s) { return Bytes(s.begin(), s.end()); }

struct Lcg {
    uint32_t s;
    explicit Lcg(uint32_t seed) : s(seed) {}
    uint32_t next() {
        s = s * 1664525u + 1013904223u;
        return s >> 8;
    }
};

TlsTrust trusting(const Ca& ca) {
    TlsTrust t;
    t.system_roots = false;                         // no computer's store decides a result
    t.extra_pem = ca.pem();
    return t;
}

// The pieces of the test world: a game server's door (a real WsListener whose connections echo), the TLS terminator in front of it (or a scripted one), a certificate
struct Rig {
    Ca ca;
    std::unique_ptr<Identity> id;
    std::unique_ptr<WsListener> ws;
    std::vector<std::unique_ptr<WsConnection>> served;
    std::unique_ptr<Terminator> term;
    uint32_t served_messages{0};

    void issue(const Names& names, int from_days = -1, int to_days = 30) { id = ca.issue(names, from_days, to_days); }
    void bridge(const WsServerOptions& options = WsServerOptions(), mbedtls_ssl_protocol_version max = MBEDTLS_SSL_VERSION_TLS1_3) {
        ws = WsListener::listen(0, true, options);
        TerminatorOptions o;
        o.backend_port = ws->port();
        o.max_version = max;
        term.reset(new Terminator(id.get(), o));
    }
    void script(std::function<std::string(std::string&)> fn, bool tls = true, bool close_after = false) {
        TerminatorOptions o;
        o.script = std::move(fn);
        o.use_tls = tls;
        o.close_after_script = close_after;
        term.reset(new Terminator(tls ? id.get() : nullptr, o));
    }
    void pump() {
        if (ws) {
            while (auto c = ws->accept()) served.push_back(std::move(c));
            for (auto& c : served) {
                Bytes m;
                while (c->poll(m)) {
                    ++served_messages;
                    c->send(m);                                              // the door's connections echo what they get
                }
            }
        }
        if (term) term->pump();
    }
    uint16_t port() const { return term->port(); }
    std::string url(const std::string& host = "localhost", const std::string& path = "/ws") const { return "wss://" + host + ":" + std::to_string(port()) + path; }
};

// A client link and what it has received
struct Link {
    std::unique_ptr<WssConnection> c;
    std::deque<Bytes> got;
    std::string why;
    void step() {
        if (!c) return;
        Bytes m;
        while (c->poll(m)) got.push_back(std::move(m));
    }
};

bool run_until(Rig& rig, Link& link, const std::function<bool()>& pred, uint32_t ms = 10000) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (!pred()) {
        if (std::chrono::steady_clock::now() > end) return false;
        rig.pump();
        link.step();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

// Starts a link to the rig's terminator (by its numeric address, with the URL's name for the certificate) and waits until it is no longer connecting
bool open_link(Rig& rig, Link& link, const std::string& url, const TlsTrust& trust, uint32_t timeout_ms = 10000) {
    WssUrl u;
    std::string why;
    if (!parse_wss_url(url, u, why)) return false;
    link.c = WssConnection::connect(u, "127.0.0.1", trust, link.why, timeout_ms);
    if (!link.c) return false;
    return run_until(rig, link, [&]() { return link.c->state() != Connection::State::Connecting; }, timeout_ms + 5000);
}

// The server side of a scripted door: answers the upgrade of a client properly, then sends `after` (frames) at once and collects what the client sends
struct Door {
    bool upgraded{false};
    std::string request_seen;
    std::string frames_in;                          // the client's bytes after the upgrade
    std::string after;                              // sent right behind the 101
    std::string extra_header;                       // a header line (with its CRLF) added to the 101, for the tests of what the client refuses
    bool wrong_accept{false};
    std::string operator()(std::string& inbox) {
        if (upgraded) {
            frames_in += inbox;
            inbox.clear();
            return std::string();
        }
        const WsHandshakeResult r = ws_parse_handshake(inbox, WsServerOptions());
        if (r.status == WsHandshakeResult::Status::NeedMore) return std::string();
        request_seen = inbox.substr(0, r.consumed);
        frames_in = inbox.substr(r.consumed);
        inbox.clear();
        if (r.status != WsHandshakeResult::Status::Accepted) return r.response;
        upgraded = true;
        std::string answer = r.response;
        if (wrong_accept) {                                                     // an accept value for another key
            const size_t at = answer.find("Sec-WebSocket-Accept: ");
            if (at != std::string::npos) answer[at + 22] = answer[at + 22] == 'A' ? 'B' : 'A';
        }
        if (!extra_header.empty()) answer.insert(answer.size() - 2, extra_header);
        return answer + after;
    }
};

Bytes server_frame(WsOpcode op, const Bytes& payload, bool fin = true) { return ws_encode_frame(op, payload, fin, nullptr); }
std::string as_string(const Bytes& b) { return std::string(b.begin(), b.end()); }

// The frames in a stream of bytes that a client wrote (every frame must be masked): returns false when a frame is unmasked or the bytes do not end at a frame border
bool client_frames(const std::string& stream, std::vector<std::pair<uint8_t, Bytes>>& out) {
    WsFrameParser parser(true);
    parser.feed(reinterpret_cast<const uint8_t*>(stream.data()), stream.size());
    Bytes payload;
    uint16_t status = 0;
    for (;;) {
        const WsFrameParser::Result r = parser.next(payload, status);
        if (r == WsFrameParser::Result::NeedMore) return parser.buffered() == 0;
        if (r == WsFrameParser::Result::Error) return false;
        out.emplace_back(static_cast<uint8_t>(r == WsFrameParser::Result::Message ? 0x2 : r == WsFrameParser::Result::Pong ? 0xA : r == WsFrameParser::Result::Ping ? 0x9 : 0x8), payload);
        payload.clear();
    }
}

}  // namespace

int main() {
    std::cout << "\n=======================================================\n [SUITE] Network port: the encrypted link of the desktop game (WebSocket over TLS)\n"
                 "=======================================================\n";

    // ============================================================================================
    // The pure parts

    TEST_CASE("WSS1.1 A wss URL is taken apart (host in lower case, port, path and query) and written in one form; everything that is not a plain wss address is refused, ws:// with a reason of its own") {
        const struct {
            const char* text;
            const char* host;
            uint16_t port;
            const char* target;
            const char* canonical;
        } good[] = {
            {"wss://beta.playants.org/ws", "beta.playants.org", 443, "/ws", "wss://beta.playants.org/ws"},
            {"WSS://Beta.PlayAnts.org:443/ws?x=1", "beta.playants.org", 443, "/ws?x=1", "wss://beta.playants.org/ws?x=1"},
            {"wss://example.test", "example.test", 443, "/", "wss://example.test/"},
            {"wss://example.test?room=1", "example.test", 443, "/?room=1", "wss://example.test/?room=1"},
            {"wss://example.test:8443/a/b", "example.test", 8443, "/a/b", "wss://example.test:8443/a/b"},
            {"wss://127.0.0.1:9/ws", "127.0.0.1", 9, "/ws", "wss://127.0.0.1:9/ws"},
            {"wss://[::1]:4443/ws", "::1", 4443, "/ws", "wss://[::1]:4443/ws"},
            {"wss://[2001:DB8::7]/ws", "2001:db8::7", 443, "/ws", "wss://[2001:db8::7]/ws"},
        };
        for (const auto& g : good) {
            WssUrl u;
            std::string why;
            ASSERT_TRUE(parse_wss_url(g.text, u, why));
            ASSERT_EQ(u.host, std::string(g.host));
            ASSERT_EQ(u.port, g.port);
            ASSERT_EQ(u.target, std::string(g.target));
            ASSERT_EQ(wss_url_text(u), std::string(g.canonical));
            WssUrl again;
            ASSERT_TRUE(parse_wss_url(wss_url_text(u), again, why) && again == u);                  // (what is written reads back as the same address)
        }
        const char* bad[] = {"", "wss://", "wss:///ws", "wss://:443/ws", "wss://exa mple.test/", "wss://example.test:0/", "wss://example.test:65536/", "wss://example.test:/", "wss://example.test:44x/",
                             "wss://user@example.test/", "wss://example.test/a b", "wss://example.test/a#frag", "wss://example.test/\x01", "wss://example.test/\xC3\xA9", "wss://-example.test/",
                             "wss://.example.test/", "wss://[::1/ws", "wss://[]/ws", "wss://[zz::1]/ws", "wss://exa_mple.test/", "https://example.test/ws", "example.test", "tcp://example.test:4001",
                             "ws://example.test/ws", "WS://example.test/ws", "wss:example.test"};
        for (const char* b : bad) {
            WssUrl u;
            std::string why;
            ASSERT_FALSE(parse_wss_url(b, u, why));
            ASSERT_FALSE(why.empty());
        }
        WssUrl plain;
        std::string why_plain;
        std::string why_other;
        WssUrl other;
        ASSERT_FALSE(parse_wss_url("ws://example.test/ws", plain, why_plain));
        ASSERT_FALSE(parse_wss_url("https://example.test/ws", other, why_other));
        ASSERT_NE(why_plain, why_other);                                                            // (a plain ws:// address says that it is unencrypted)
        WssUrl long_url;
        std::string why_long;
        ASSERT_TRUE(parse_wss_url("wss://example.test/" + std::string(480, 'a'), long_url, why_long));
        ASSERT_FALSE(parse_wss_url("wss://example.test/" + std::string(600, 'a'), long_url, why_long));
    } TEST_END();

    TEST_CASE("WSS1.2 The upgrade request is one that the game server's own door accepts, and the door's answer is accepted by the client: the accept key, one byte at a time, the frames behind it; every other answer is refused") {
        WssUrl url;
        std::string why;
        ASSERT_TRUE(parse_wss_url("wss://beta.example.test:8443/ws?r=1", url, why));
        Bytes key(16);
        for (size_t i = 0; i < key.size(); ++i) key[i] = static_cast<uint8_t>(i * 7 + 1);
        const std::string key_b64 = ws_base64_encode(key.data(), key.size());
        const std::string request = wss_build_request(url, key_b64);
        WsServerOptions options;
        options.path = "/ws";
        const WsHandshakeResult accepted = ws_parse_handshake(request, options);                 // the door that the browsers use takes it
        ASSERT_TRUE(accepted.status == WsHandshakeResult::Status::Accepted && accepted.consumed == request.size());
        WsServerOptions with_origins;
        with_origins.allowed_origins = {"https://other.example"};
        ASSERT_EQ(ws_parse_handshake(request, with_origins).status, WsHandshakeResult::Status::Accepted);      // (no Origin: only browsers send one, the list defends against pages of other sites)
        // the answer, whole and one byte at a time, with frames behind it
        const std::string frames = as_string(server_frame(WsOpcode::Binary, bytes_of("hi")));
        const std::string answer = accepted.response;
        const WssReply whole = wss_parse_reply(answer + frames, key_b64);
        ASSERT_TRUE(whole.status == WssReply::Status::Accepted && whole.consumed == answer.size() && whole.http_status == 101);
        for (size_t n = 0; n < answer.size(); ++n) ASSERT_TRUE(wss_parse_reply(answer.substr(0, n), key_b64).status == WssReply::Status::NeedMore);
        ASSERT_TRUE(wss_parse_reply(answer, key_b64).status == WssReply::Status::Accepted);
        // an answer for another key, a missing header, an extension, a subprotocol, another status
        const std::string other_key = ws_base64_encode(Bytes(16, 9).data(), 16);
        ASSERT_TRUE(wss_parse_reply(answer, other_key).status == WssReply::Status::Refused);
        const auto without = [&](const std::string& header) {
            std::string a = answer;
            const size_t at = a.find(header);
            const size_t end = a.find("\r\n", at);
            a.erase(at, end + 2 - at);
            return a;
        };
        ASSERT_TRUE(wss_parse_reply(without("Upgrade:"), key_b64).status == WssReply::Status::Refused);
        ASSERT_TRUE(wss_parse_reply(without("Connection:"), key_b64).status == WssReply::Status::Refused);
        ASSERT_TRUE(wss_parse_reply(without("Sec-WebSocket-Accept:"), key_b64).status == WssReply::Status::Refused);
        std::string with_extension = answer;
        with_extension.insert(with_extension.size() - 2, "Sec-WebSocket-Extensions: permessage-deflate\r\n");
        ASSERT_TRUE(wss_parse_reply(with_extension, key_b64).status == WssReply::Status::Refused);
        std::string with_protocol = answer;
        with_protocol.insert(with_protocol.size() - 2, "Sec-WebSocket-Protocol: ants\r\n");
        ASSERT_TRUE(wss_parse_reply(with_protocol, key_b64).status == WssReply::Status::Refused);
        std::string upgrade_only = answer;                                                          // a header that says something else than websocket
        upgrade_only.replace(upgrade_only.find("websocket"), 9, "h2c");
        ASSERT_TRUE(wss_parse_reply(upgrade_only, key_b64).status == WssReply::Status::Refused);
        for (const int status : {200, 301, 400, 403, 404, 426, 500, 502, 503, 504}) {
            const WssReply r = wss_parse_reply("HTTP/1.1 " + std::to_string(status) + " Whatever\r\nContent-Length: 0\r\n\r\n", key_b64);
            ASSERT_TRUE(r.status == WssReply::Status::Refused && r.http_status == status && !r.why.empty());
        }
        ASSERT_TRUE(wss_parse_reply("HTTP/1.0 101 Switching\r\n\r\n", key_b64).status == WssReply::Status::Refused);
        ASSERT_TRUE(wss_parse_reply("garbage\r\n\r\n", key_b64).status == WssReply::Status::Refused);
        ASSERT_TRUE(wss_parse_reply("HTTP/1.1 101 x\r\nno colon here\r\n\r\n", key_b64).status == WssReply::Status::Refused);
        ASSERT_TRUE(wss_parse_reply(std::string(kWssMaxReplyBytes + 10, 'x'), key_b64).status == WssReply::Status::Refused);       // too long without its blank line
        std::string long_answer = "HTTP/1.1 101 x\r\n";
        for (int i = 0; i < 100; ++i) long_answer += "X-" + std::to_string(i) + ": 1\r\n";
        ASSERT_TRUE(wss_parse_reply(long_answer + "\r\n", key_b64).status == WssReply::Status::Refused);                      // too many header lines
    } TEST_END();

    TEST_CASE("WSS1.3 Plain TCP is for this machine and the local network only: loopback, private, link-local, shared (VPN) and unspecified addresses are local, every other address and every text that is no address is not") {
        for (const char* a : {"127.0.0.1", "127.255.255.254", "10.0.0.1", "10.255.255.255", "172.16.0.1", "172.31.255.255", "192.168.0.1", "192.168.255.255", "169.254.1.1", "100.64.0.1", "100.127.255.255",
                              "0.0.0.0", "::1", "::", "fe80::1", "fe80::1%2", "febf::1", "fc00::1", "fd12:3456::1", "::ffff:192.168.1.1", "::ffff:127.0.0.1"}) {
            ASSERT_TRUE(is_local_address(a));
        }
        for (const char* a : {"8.8.8.8", "1.1.1.1", "172.15.255.255", "172.32.0.1", "11.0.0.1", "9.255.255.255", "192.167.0.1", "192.169.0.1", "169.253.1.1", "169.255.0.1", "100.63.255.255", "100.128.0.1",
                              "126.255.255.255", "128.0.0.1", "203.0.113.9", "2001:db8::1", "2606:4700::1111", "fec0::1", "fe7f::1", "fb00::1", "fe::1", "::ffff:8.8.8.8", "::2", "1::1", "", "localhost",
                              "beta.playants.org", "256.1.1.1", "1.2.3", "fe80::1%"}) {
            ASSERT_FALSE(is_local_address(a));
        }
    } TEST_END();

    TEST_CASE("WSS1.4 The system's trusted roots are read where the platform has a store (macOS, Windows, a Linux with a bundle file): many of them, not none; and the random numbers of the handshake are random") {
        const size_t roots = system_root_count();
        bool has_store = true;
#if !defined(__APPLE__) && !defined(_WIN32)
        has_store = false;
        for (const char* path : {"/etc/ssl/certs/ca-certificates.crt", "/etc/pki/tls/certs/ca-bundle.crt", "/etc/ssl/ca-bundle.pem", "/etc/pki/tls/cacert.pem", "/etc/ssl/cert.pem", "/usr/local/etc/ssl/cert.pem"}) {
            if (std::ifstream(path).good()) has_store = true;
        }
#endif
        if (has_store) ASSERT_GE(roots, size_t{10});
        std::array<uint8_t, 32> a{};
        std::array<uint8_t, 32> b{};
        ASSERT_TRUE(tls_random(a.data(), a.size()) && tls_random(b.data(), b.size()));
        const std::array<uint8_t, 32> zero{};
        ASSERT_TRUE(a != b && a != zero && b != zero);
        ASSERT_TRUE(tls_random(nullptr, 0));
    } TEST_END();

    // ============================================================================================
    // The link

    TEST_CASE("WSS2.1 A client that trusts the server's authority opens the link (TLS, then the upgrade) and exchanges messages with the game server's door in order, whatever their size: 0 bytes to the limit, 1000 random ones, then the close") {
        Rig rig;
        rig.issue(Names{{"localhost"}, {}});
        rig.bridge();
        Link link;
        ASSERT_TRUE(open_link(rig, link, rig.url(), trusting(rig.ca)));
        ASSERT_EQ(link.c->state(), Connection::State::Open);
        ASSERT_TRUE(link.c->tls_failure() == TlsFailure::None && link.c->failure_text().empty());
        ASSERT_FALSE(link.c->tls_description().empty());
        ASSERT_EQ(rig.term->handshakes, 1u);
        Lcg rng(77);
        std::vector<Bytes> sent;
        for (const size_t size : {size_t{0}, size_t{1}, size_t{125}, size_t{126}, size_t{127}, size_t{4096}, size_t{65535}, size_t{65536}, kMaxMessageBytes}) {
            Bytes m(size);
            for (uint8_t& b : m) b = static_cast<uint8_t>(rng.next());
            sent.push_back(m);
        }
        for (int i = 0; i < 1000; ++i) {
            Bytes m(rng.next() % 2000);
            for (uint8_t& b : m) b = static_cast<uint8_t>(rng.next());
            sent.push_back(m);
        }
        size_t next_send = 0;
        ASSERT_TRUE(run_until(rig, link, [&]() {
            while (next_send < sent.size() && link.c->send(sent[next_send])) ++next_send;
            return link.got.size() == sent.size();
        }, 30000));
        for (size_t i = 0; i < sent.size(); ++i) ASSERT_TRUE(link.got[i] == sent[i]);                // in order, byte for byte
        ASSERT_EQ(rig.served_messages, static_cast<uint32_t>(sent.size()));
        ASSERT_FALSE(link.c->send(Bytes(kMaxMessageBytes + 1)));                                     // (too big for the protocol: not sent, the link stays)
        ASSERT_EQ(link.c->state(), Connection::State::Open);
        link.c->close();
        ASSERT_EQ(link.c->state(), Connection::State::Closed);
        ASSERT_FALSE(link.c->send(bytes_of("late")));
        ASSERT_TRUE(run_until(rig, link, [&]() { return rig.served.size() == 1 && !rig.served[0]->is_open(); }));      // (the door saw a close frame, not a dead socket)
        ASSERT_EQ(rig.served[0]->state(), Connection::State::Closed);
    } TEST_END();

    TEST_CASE("WSS2.2 The link runs over TLS 1.2 as well as 1.3, and a link by an IP address works when the certificate names that address") {
        for (const mbedtls_ssl_protocol_version max : {MBEDTLS_SSL_VERSION_TLS1_2, MBEDTLS_SSL_VERSION_TLS1_3}) {
            Rig rig;
            rig.issue(Names{{"localhost"}, {"127.0.0.1"}});
            rig.bridge(WsServerOptions(), max);
            Link link;
            ASSERT_TRUE(open_link(rig, link, rig.url(), trusting(rig.ca)));
            ASSERT_EQ(link.c->state(), Connection::State::Open);
            ASSERT_TRUE(link.c->tls_description().find(max == MBEDTLS_SSL_VERSION_TLS1_2 ? "1.2" : "1.3") != std::string::npos);
            ASSERT_TRUE(link.c->send(bytes_of("over this version")));
            ASSERT_TRUE(run_until(rig, link, [&]() { return link.got.size() == 1; }));
            ASSERT_TRUE(link.got[0] == bytes_of("over this version"));
        }
        Rig rig;
        rig.issue(Names{{"localhost"}, {"127.0.0.1", "::1"}});
        rig.bridge();
        Link by_address;
        ASSERT_TRUE(open_link(rig, by_address, rig.url("127.0.0.1"), trusting(rig.ca)));
        ASSERT_EQ(by_address.c->state(), Connection::State::Open);
    } TEST_END();

    TEST_CASE("WSS2.3 Nothing readable travels in the clear: the first bytes on the wire are a TLS record, and a distinctive message and its echo appear in neither direction of it") {
        Rig rig;
        rig.issue(Names{{"localhost"}, {}});
        rig.bridge();
        Link link;
        ASSERT_TRUE(open_link(rig, link, rig.url(), trusting(rig.ca)));
        const std::string secret = "SeatKey-ZebraQuokka-0123456789-chat-line-that-nobody-on-the-path-may-read";
        ASSERT_TRUE(link.c->send(bytes_of(secret)));
        ASSERT_TRUE(run_until(rig, link, [&]() { return link.got.size() == 1; }));
        ASSERT_TRUE(link.got[0] == bytes_of(secret));
        ASSERT_TRUE(rig.term->raw_in().size() > 100 && rig.term->raw_out().size() > 100);
        ASSERT_TRUE(static_cast<uint8_t>(rig.term->raw_in()[0]) == 0x16 && static_cast<uint8_t>(rig.term->raw_in()[1]) == 0x03);        // a TLS handshake record
        for (const std::string& wire : {rig.term->raw_in(), rig.term->raw_out()}) {
            ASSERT_TRUE(wire.find("ZebraQuokka") == std::string::npos);
            ASSERT_TRUE(wire.find("Upgrade") == std::string::npos && wire.find("websocket") == std::string::npos && wire.find("GET /ws") == std::string::npos);
        }
    } TEST_END();

    TEST_CASE("WSS3.1 The certificate is always checked: an authority that the client does not trust, and the system's own roots alone, refuse a server whose certificate a test authority issued; the server never receives a byte of the game") {
        Rig rig;
        rig.issue(Names{{"localhost"}, {}});
        rig.bridge();
        Ca stranger("Another Authority");
        {
            Link link;
            ASSERT_TRUE(open_link(rig, link, rig.url(), trusting(stranger)));
            ASSERT_EQ(link.c->state(), Connection::State::Failed);
            ASSERT_EQ(link.c->tls_failure(), TlsFailure::Untrusted);
            ASSERT_FALSE(link.c->failure_text().empty());
            ASSERT_FALSE(link.c->send(bytes_of("must not leave")));
            Bytes m;
            ASSERT_FALSE(link.c->poll(m));
        }
        {
            Link link;                                                                              // what the game does: the system's roots, nothing added
            WssUrl u;
            std::string why;
            ASSERT_TRUE(parse_wss_url(rig.url(), u, why));
            link.c = WssConnection::connect(u, "127.0.0.1", TlsTrust(), link.why);
            if (system_root_count() == 0) {
                ASSERT_TRUE(link.c == nullptr && !link.why.empty());                                // (a computer that has no roots cannot make the link at all)
            } else {
                ASSERT_TRUE(link.c != nullptr);
                ASSERT_TRUE(run_until(rig, link, [&]() { return link.c->state() != Connection::State::Connecting; }));
                ASSERT_EQ(link.c->state(), Connection::State::Failed);
                ASSERT_EQ(link.c->tls_failure(), TlsFailure::Untrusted);
            }
        }
        {
            Link link;                                                                              // nothing trusted at all
            WssUrl u;
            std::string why;
            ASSERT_TRUE(parse_wss_url(rig.url(), u, why));
            TlsTrust nothing;
            nothing.system_roots = false;
            link.c = WssConnection::connect(u, "127.0.0.1", nothing, link.why);
            ASSERT_TRUE(link.c == nullptr && !link.why.empty());
            TlsTrust garbage;
            garbage.system_roots = false;
            garbage.extra_pem = "-----BEGIN CERTIFICATE-----\nnot a certificate\n-----END CERTIFICATE-----\n";
            link.c = WssConnection::connect(u, "127.0.0.1", garbage, link.why);
            ASSERT_TRUE(link.c == nullptr && !link.why.empty());
        }
        for (int i = 0; i < 30; ++i) {
            rig.pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        ASSERT_EQ(rig.term->app_bytes_up, 0u);                                                      // no request, no frame, nothing reached the server
        ASSERT_EQ(rig.term->handshakes, 0u);
    } TEST_END();

    TEST_CASE("WSS3.2 The certificate must be for the name that was asked for: another name, an address that it does not name, a certificate for the address when the name was given: all refused; the right name is accepted") {
        {
            Rig rig;
            rig.issue(Names{{"other.example.test"}, {}});                                           // a certificate that is trusted, for another site
            rig.bridge();
            Link link;
            ASSERT_TRUE(open_link(rig, link, rig.url("localhost"), trusting(rig.ca)));
            ASSERT_EQ(link.c->state(), Connection::State::Failed);
            ASSERT_EQ(link.c->tls_failure(), TlsFailure::WrongName);
            ASSERT_EQ(rig.term->app_bytes_up, 0u);
        }
        {
            Rig rig;
            rig.issue(Names{{"localhost"}, {}});                                                    // it names localhost, the link goes to 127.0.0.1 by its address
            rig.bridge();
            Link link;
            ASSERT_TRUE(open_link(rig, link, rig.url("127.0.0.1"), trusting(rig.ca)));
            ASSERT_EQ(link.c->state(), Connection::State::Failed);
            ASSERT_EQ(link.c->tls_failure(), TlsFailure::WrongName);
        }
        {
            Rig rig;
            rig.issue(Names{{}, {"127.0.0.1"}});                                                    // it names the address only, the link asks for localhost
            rig.bridge();
            Link link;
            ASSERT_TRUE(open_link(rig, link, rig.url("localhost"), trusting(rig.ca)));
            ASSERT_EQ(link.c->state(), Connection::State::Failed);
            ASSERT_EQ(link.c->tls_failure(), TlsFailure::WrongName);
        }
        {
            Rig rig;
            rig.issue(Names{{"*.example.test"}, {}});                                               // a wildcard covers one label: sub.example.test, not example.test or a.b.example.test
            rig.bridge();
            Link link;
            ASSERT_TRUE(open_link(rig, link, rig.url("sub.example.test"), trusting(rig.ca)));
            ASSERT_EQ(link.c->state(), Connection::State::Open);
            Link two;
            ASSERT_TRUE(open_link(rig, two, rig.url("a.b.example.test"), trusting(rig.ca)));
            ASSERT_EQ(two.c->tls_failure(), TlsFailure::WrongName);
            Link bare;
            ASSERT_TRUE(open_link(rig, bare, rig.url("example.test"), trusting(rig.ca)));
            ASSERT_EQ(bare.c->tls_failure(), TlsFailure::WrongName);
        }
    } TEST_END();

    TEST_CASE("WSS3.3 A certificate that has expired, or is not valid yet, is refused") {
        for (const std::pair<int, int>& window : {std::pair<int, int>{-10, -1}, std::pair<int, int>{1, 10}}) {
            Rig rig;
            rig.issue(Names{{"localhost"}, {}}, window.first, window.second);
            rig.bridge();
            Link link;
            ASSERT_TRUE(open_link(rig, link, rig.url(), trusting(rig.ca)));
            ASSERT_EQ(link.c->state(), Connection::State::Failed);
            ASSERT_EQ(link.c->tls_failure(), TlsFailure::Dates);
            ASSERT_EQ(rig.term->app_bytes_up, 0u);
        }
    } TEST_END();

    TEST_CASE("WSS3.4 A server that does not speak TLS, and a plain WebSocket door where a secure one was asked for, are refused without hanging: nothing is sent that a listener could read") {
        {
            Rig rig;
            rig.script([](std::string& inbox) {
                inbox.clear();
                return std::string("HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nhi");
            }, false);
            Link link;
            ASSERT_TRUE(open_link(rig, link, rig.url(), TlsTrust{false, rig.ca.pem()}));
            ASSERT_EQ(link.c->state(), Connection::State::Failed);
            ASSERT_EQ(link.c->tls_failure(), TlsFailure::Protocol);
        }
        {
            Rig rig;                                                                                // the game server's own door, without the proxy
            rig.ws = WsListener::listen(0, true);
            Link link;
            WssUrl u;
            std::string why;
            ASSERT_TRUE(parse_wss_url("wss://localhost:" + std::to_string(rig.ws->port()) + "/ws", u, why));
            link.c = WssConnection::connect(u, "127.0.0.1", trusting(rig.ca), link.why, 5000);
            ASSERT_TRUE(link.c != nullptr);
            ASSERT_TRUE(run_until(rig, link, [&]() { return link.c->state() != Connection::State::Connecting; }));
            ASSERT_EQ(link.c->state(), Connection::State::Failed);
            ASSERT_EQ(link.c->tls_failure(), TlsFailure::Protocol);
            ASSERT_TRUE(rig.served.empty());                                                        // (the door never took a connection in: what it read was not an upgrade)
        }
        {
            WssUrl u;
            std::string why;
            ASSERT_TRUE(parse_wss_url("wss://localhost:1/ws", u, why));                              // nothing listens on this port
            TlsTrust t;
            t.system_roots = false;
            t.extra_pem = Ca().pem();
            Link link;
            link.c = WssConnection::connect(u, "127.0.0.1", t, link.why, 5000);
            if (link.c) {
                Rig rig;
                ASSERT_TRUE(run_until(rig, link, [&]() { return link.c->state() != Connection::State::Connecting; }));
                ASSERT_EQ(link.c->state(), Connection::State::Failed);
                ASSERT_EQ(link.c->tls_failure(), TlsFailure::Unreachable);
            }
        }
    } TEST_END();

    TEST_CASE("WSS4.1 The refusals of the upgrade end the link: a path that the door does not serve (404) and an answer of the proxy when the game server is down (502); the TLS link itself was fine") {
        {
            Rig rig;
            rig.issue(Names{{"localhost"}, {}});
            WsServerOptions options;
            options.path = "/ws";
            rig.bridge(options);
            Link link;
            ASSERT_TRUE(open_link(rig, link, rig.url("localhost", "/elsewhere"), trusting(rig.ca)));
            ASSERT_EQ(link.c->state(), Connection::State::Failed);
            ASSERT_EQ(link.c->refused_status(), 404);
            ASSERT_EQ(link.c->tls_failure(), TlsFailure::None);
            ASSERT_FALSE(link.c->failure_text().empty());
            Link good;
            ASSERT_TRUE(open_link(rig, good, rig.url("localhost", "/ws"), trusting(rig.ca)));
            ASSERT_EQ(good.c->state(), Connection::State::Open);
        }
        {
            Rig rig;
            rig.issue(Names{{"localhost"}, {}});
            rig.script([](std::string& inbox) {
                if (inbox.find("\r\n\r\n") == std::string::npos) return std::string();
                inbox.clear();
                return std::string("HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            });
            Link link;
            ASSERT_TRUE(open_link(rig, link, rig.url(), trusting(rig.ca)));
            ASSERT_EQ(link.c->state(), Connection::State::Failed);
            ASSERT_EQ(link.c->refused_status(), 502);
        }
        {
            Rig rig;                                                                                // a 101 with the wrong accept key, and one with an extension that nobody asked for
            rig.issue(Names{{"localhost"}, {}});
            Door door;
            door.wrong_accept = true;
            rig.script(std::ref(door));
            Link link;
            ASSERT_TRUE(open_link(rig, link, rig.url(), trusting(rig.ca)));
            ASSERT_EQ(link.c->state(), Connection::State::Failed);
            Rig rig2;
            rig2.issue(Names{{"localhost"}, {}});
            Door door2;
            door2.extra_header = "Sec-WebSocket-Extensions: permessage-deflate\r\n";
            rig2.script(std::ref(door2));
            Link link2;
            ASSERT_TRUE(open_link(rig2, link2, rig2.url(), trusting(rig2.ca)));
            ASSERT_EQ(link2.c->state(), Connection::State::Failed);
        }
    } TEST_END();

    TEST_CASE("WSS4.2 A server that never answers (it takes the connection and says nothing, with TLS done or not) is given up on when the time is out: the link fails by itself") {
        Rig rig;
        rig.issue(Names{{"localhost"}, {}});
        rig.script([](std::string& inbox) {                                                         // TLS is made, the request is read, no answer ever comes
            inbox.clear();
            return std::string();
        });
        Link link;
        const auto start = std::chrono::steady_clock::now();
        ASSERT_TRUE(open_link(rig, link, rig.url(), trusting(rig.ca), 400));
        ASSERT_EQ(link.c->state(), Connection::State::Failed);
        ASSERT_TRUE(std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(390));
        ASSERT_TRUE(std::chrono::steady_clock::now() - start < std::chrono::seconds(8));
        Rig silent;                                                                                  // a listener that completes TCP and sends nothing: the TLS handshake never finishes
        silent.issue(Names{{"localhost"}, {}});
        auto raw = TcpListener::listen(0, true);
        ASSERT_TRUE(raw != nullptr);
        WssUrl u;
        std::string why;
        ASSERT_TRUE(parse_wss_url("wss://localhost:" + std::to_string(raw->port()) + "/ws", u, why));
        Link link2;
        link2.c = WssConnection::connect(u, "127.0.0.1", trusting(silent.ca), link2.why, 400);
        ASSERT_TRUE(link2.c != nullptr);
        std::vector<std::unique_ptr<TcpStream>> held;
        ASSERT_TRUE(run_until(silent, link2, [&]() {
            while (auto s = raw->accept_stream()) held.push_back(std::move(s));
            return link2.c->state() != Connection::State::Connecting;
        }));
        ASSERT_EQ(link2.c->state(), Connection::State::Failed);
    } TEST_END();

    TEST_CASE("WSS5.1 The server's frames: a message in fragments with a ping between them is reassembled, the ping is answered with a masked pong that carries the same bytes, and the server's close frame is answered and ends the link") {
        Rig rig;
        rig.issue(Names{{"localhost"}, {}});
        Door door;
        const Bytes whole = bytes_of("a message in three fragments");
        door.after = as_string(server_frame(WsOpcode::Binary, Bytes(whole.begin(), whole.begin() + 8), false)) + as_string(server_frame(WsOpcode::Ping, bytes_of("are you there"))) +
                     as_string(server_frame(WsOpcode::Continuation, Bytes(whole.begin() + 8, whole.begin() + 20), false)) +
                     as_string(server_frame(WsOpcode::Continuation, Bytes(whole.begin() + 20, whole.end()), true)) + as_string(server_frame(WsOpcode::Binary, bytes_of("then one more")));
        rig.script(std::ref(door));
        Link link;
        ASSERT_TRUE(open_link(rig, link, rig.url(), trusting(rig.ca)));
        ASSERT_EQ(link.c->state(), Connection::State::Open);
        ASSERT_TRUE(run_until(rig, link, [&]() { return link.got.size() == 2; }));
        ASSERT_TRUE(link.got[0] == whole && link.got[1] == bytes_of("then one more"));
        ASSERT_TRUE(run_until(rig, link, [&]() { return !door.frames_in.empty(); }));
        std::vector<std::pair<uint8_t, Bytes>> frames;
        ASSERT_TRUE(client_frames(door.frames_in, frames));                                         // (every frame the client wrote is masked: the strict parser took them)
        ASSERT_TRUE(frames.size() == 1 && frames[0].first == 0xA && frames[0].second == bytes_of("are you there"));
        Rig rig2;                                                                                    // a second server for the close: it sends a close frame at once
        rig2.issue(Names{{"localhost"}, {}});
        Door door2;
        door2.after = as_string(server_frame(WsOpcode::Close, ws_close_payload(1001, "going away")));
        rig2.script(std::ref(door2));
        Link link2;
        ASSERT_TRUE(open_link(rig2, link2, rig2.url(), trusting(rig2.ca)));
        ASSERT_TRUE(run_until(rig2, link2, [&]() { return link2.c->state() == Connection::State::Closed; }));
        ASSERT_TRUE(run_until(rig2, link2, [&]() { return !door2.frames_in.empty(); }));
        std::vector<std::pair<uint8_t, Bytes>> reply;
        ASSERT_TRUE(client_frames(door2.frames_in, reply));
        ASSERT_TRUE(reply.size() == 1 && reply[0].first == 0x8);                                    // the close was echoed
        ASSERT_FALSE(link2.c->send(bytes_of("late")));
    } TEST_END();

    TEST_CASE("WSS5.2 A server that breaks the rules ends the link: a masked frame, a text frame, a message over the limit, a reserved bit, a close frame with a bad status; the client says why with a close frame of its own") {
        struct Case {
            const char* name;
            std::string frames;
        };
        const std::array<uint8_t, 4> mask{{1, 2, 3, 4}};
        std::vector<Case> cases;
        cases.push_back({"masked", as_string(ws_encode_frame(WsOpcode::Binary, bytes_of("x"), true, &mask))});
        cases.push_back({"text", as_string(server_frame(WsOpcode::Text, bytes_of("x")))});
        cases.push_back({"too big", as_string(server_frame(WsOpcode::Binary, Bytes(kMaxMessageBytes + 1, 0)))});
        std::string reserved = as_string(server_frame(WsOpcode::Binary, bytes_of("x")));
        reserved[0] = static_cast<char>(static_cast<uint8_t>(reserved[0]) | 0x40u);
        cases.push_back({"reserved bit", reserved});
        cases.push_back({"bad close status", as_string(server_frame(WsOpcode::Close, Bytes{0x03, 0xED}))});             // 1005 may not travel
        cases.push_back({"unknown opcode", std::string("\x83\x00", 2)});
        for (const Case& c : cases) {
            Rig rig;
            rig.issue(Names{{"localhost"}, {}});
            Door door;
            door.after = c.frames;
            rig.script(std::ref(door));
            Link link;
            ASSERT_TRUE(open_link(rig, link, rig.url(), trusting(rig.ca)));
            ASSERT_TRUE(run_until(rig, link, [&]() { return link.c->state() == Connection::State::Failed; }));
            ASSERT_TRUE(link.got.empty());
            ASSERT_TRUE(run_until(rig, link, [&]() { return !door.frames_in.empty(); }));
            std::vector<std::pair<uint8_t, Bytes>> reply;
            ASSERT_TRUE(client_frames(door.frames_in, reply));
            ASSERT_TRUE(!reply.empty() && reply.back().first == 0x8);
        }
    } TEST_END();

    TEST_CASE("WSS5.3 Every frame the client writes is masked, with a key of its own: the same message sent many times is masked with several different keys") {
        Rig rig;
        rig.issue(Names{{"localhost"}, {}});
        Door door;
        rig.script(std::ref(door));
        Link link;
        ASSERT_TRUE(open_link(rig, link, rig.url(), trusting(rig.ca)));
        for (int i = 0; i < 40; ++i) ASSERT_TRUE(link.c->send(bytes_of("the same message every time")));
        ASSERT_TRUE(run_until(rig, link, [&]() { return door.frames_in.size() >= 40 * (6 + 27); }));
        std::set<uint32_t> keys;
        size_t at = 0;
        const std::string& s = door.frames_in;
        for (int i = 0; i < 40; ++i) {
            ASSERT_TRUE((static_cast<uint8_t>(s[at + 1]) & 0x80u) != 0);                              // the mask bit
            ASSERT_EQ(static_cast<uint8_t>(s[at + 1]) & 0x7Fu, 27u);
            keys.insert((uint32_t{static_cast<uint8_t>(s[at + 2])} << 24) | (uint32_t{static_cast<uint8_t>(s[at + 3])} << 16) | (uint32_t{static_cast<uint8_t>(s[at + 4])} << 8) | uint32_t{static_cast<uint8_t>(s[at + 5])});
            at += 6 + 27;
        }
        ASSERT_GE(keys.size(), size_t{20});
    } TEST_END();

    TEST_CASE("WSS6.1 A cable that is pulled (every link dropped without a goodbye) ends the client's link with a failure or a close, never a hang, and a new link to the same server opens at once") {
        Rig rig;
        rig.issue(Names{{"localhost"}, {}});
        rig.bridge();
        Link link;
        ASSERT_TRUE(open_link(rig, link, rig.url(), trusting(rig.ca)));
        ASSERT_TRUE(link.c->send(bytes_of("before")));
        ASSERT_TRUE(run_until(rig, link, [&]() { return link.got.size() == 1; }));
        rig.term->cut_all();
        ASSERT_TRUE(run_until(rig, link, [&]() { return link.c->state() == Connection::State::Failed || link.c->state() == Connection::State::Closed; }));
        ASSERT_FALSE(link.c->send(bytes_of("after")));
        Link again;
        ASSERT_TRUE(open_link(rig, again, rig.url(), trusting(rig.ca)));
        ASSERT_EQ(again.c->state(), Connection::State::Open);
        ASSERT_TRUE(again.c->send(bytes_of("back")));
        ASSERT_TRUE(run_until(rig, again, [&]() { return again.got.size() == 1; }));
    } TEST_END();

    TEST_CASE("WSS6.2 A burst of 6000 messages from the server arrives whole and in order when the game takes them one at a time (more than the link keeps: it stops reading and goes on as the game takes them)") {
        Rig rig;
        rig.issue(Names{{"localhost"}, {}});
        Door door;
        std::string flood;
        for (int i = 0; i < 6000; ++i) flood += as_string(server_frame(WsOpcode::Binary, Bytes(200, static_cast<uint8_t>(i))));
        door.after = flood;
        rig.script(std::ref(door));
        Link link;
        ASSERT_TRUE(open_link(rig, link, rig.url(), trusting(rig.ca)));
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (link.got.size() < 6000 && std::chrono::steady_clock::now() < end) {
            rig.pump();
            Bytes m;
            if (link.c->poll(m)) link.got.push_back(std::move(m));
        }
        ASSERT_EQ(link.got.size(), size_t{6000});
        for (size_t i = 0; i < link.got.size(); ++i) ASSERT_TRUE(link.got[i].size() == 200 && link.got[i][0] == static_cast<uint8_t>(i));      // all of them, in order
        ASSERT_EQ(link.c->state(), Connection::State::Open);
    } TEST_END();

    std::cout << "\n=======================================================\n";
    std::cout << " Encrypted link: " << g_test_count << " test cases, " << g_assert_count << " assertions, " << g_test_failures << " failures\n";
    std::cout << "=======================================================\n";
    if (g_test_count == 0) {
        std::cout << "\n no test ran: the filter ANTS_TEST_FILTER matches no test of this suite\n";
        return 1;
    }
    return g_test_failures == 0 ? 0 : 1;
}
