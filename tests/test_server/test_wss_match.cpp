// Tests of the desktop game's encrypted link in a whole online match (docs/NETWORK_PORT.md "The desktop game's encrypted link"): real NetGames join a real RoomManager through
// wss:// (TCP, TLS with a certificate that the tests' own authority issued, the WebSocket upgrade, the server's WsListener), exactly as the game reaches the site's /ws behind its proxy. The client
// trusts that authority explicitly (NetGame::set_extra_trusted_ca_pem with the system's roots off) and nothing in the product skips a check. Checked: a match from the Hello to the end, with both
// machines and the referee in one state and not one readable word on the wire; the way back over TLS (every link pulled, both machines come back by themselves with their keys and the turns they lack); a
// machine that starts from nothing with its key; the refusals (an authority that is not trusted, a certificate for another name) before any byte of the game is sent; and plain TCP, which a server's room
// may no longer use at an address of the Internet.
#include "rejoin_fixtures.hpp"
#include "../common/tls_test_server.hpp"
#include "ants_net/address.hpp"
#include "ants_net/ws.hpp"

namespace {

using ants_test_tls::Ca;
using ants_test_tls::Identity;
using ants_test_tls::Names;
using ants_test_tls::Terminator;
using ants_test_tls::TerminatorOptions;

// The site in miniature: a RoomManager behind the game server's WebSocket door (WsListener, the path /ws), behind a TLS terminator that holds a certificate for `localhost`
struct WssWorld {
    Ca ca;
    std::unique_ptr<Identity> id;
    std::unique_ptr<RoomManager> mgr;
    std::unique_ptr<net::WsListener> ws;
    std::unique_ptr<Terminator> term;
    std::vector<std::unique_ptr<Machine>> machines;
    uint32_t now{1000};
    uint32_t steps_{0};
    uint32_t accepted{0};                       // the connections that the door took in (after TLS and the upgrade)

    explicit WssWorld(const Names& names = Names{{"localhost"}, {}}) {
        id = ca.issue(names);
        mgr = std::make_unique<RoomManager>(MapStore(std::string(ORIGINAL_ASSETS_DIR) + "/Maps"), ServerLimits());
        net::WsServerOptions options;
        options.path = "/ws";
        ws = net::WsListener::listen(0, true, options);
        if (!ws) throw std::runtime_error("the door cannot listen");
        TerminatorOptions t;
        t.backend_port = ws->port();
        term = std::make_unique<Terminator>(id.get(), t);
    }
    std::string url(const std::string& host = "localhost") const { return "wss://" + host + ":" + std::to_string(term->port()) + "/ws"; }

    Machine& add_machine(const std::string& name) {
        machines.push_back(std::make_unique<Machine>(name, static_cast<uint32_t>(machines.size() + 1) * 7919u));
        return *machines.back();
    }
    // A machine that joins `room` through wss: it trusts the tests' authority and nothing else
    Machine& join(const std::string& name, const std::string& room, const net::SeatKey& key = net::SeatKey{}, uint8_t want_seat = 255, const Ca* trusted = nullptr, const std::string& host = "localhost") {
        Machine& m = add_machine(name);
        m.net.set_system_roots_trusted_for_test(false);
        m.net.set_extra_trusted_ca_pem(trusted != nullptr ? trusted->pem() : ca.pem());
        if (!m.net.join_url(url(host), name, want_seat, room, std::string(), key, "127.0.0.1")) throw std::runtime_error("join_url failed: " + m.net.join_refusal());
        return m;
    }
    void pump() {
        while (auto c = ws->accept()) {
            ++accepted;
            mgr->add_connection(std::move(c), "127.0.0.1", now);
        }
        term->pump();
        mgr->update(now);
        for (auto& m : machines) m->frame(now);
    }
    void run(uint32_t ms) {
        for (uint32_t elapsed = 0; elapsed < ms; elapsed += 10) {
            now += 10;
            pump();
            if ((++steps_ & 15u) == 0) ants_test::short_pause();
            else std::this_thread::yield();
        }
    }
    bool run_until(const std::function<bool()>& cond, uint32_t max_ms) {
        for (uint32_t elapsed = 0; elapsed < max_ms; elapsed += 10) {
            if (cond()) return true;
            run(10);
        }
        for (int i = 0; i < 2000 && !cond(); ++i) {                    // (the game clock is virtual and the sockets are real: the kernel may be late; the clock stands still meanwhile)
            pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return cond();
    }
    RoomStatus status(const std::string& code) const {
        RoomStatus s;
        mgr->status(code, s, now);
        return s;
    }
    bool running(std::initializer_list<Machine*> ms) const {
        for (const Machine* m : ms) {
            if (m->net.phase() != NetGame::Phase::Playing || m->ticks < 40) return false;
        }
        return true;
    }
    bool finished(const std::string& code) const {
        for (const RoomStatus& s : mgr->list(now)) {
            if (s.code == code && s.state == RoomState::Finished) return true;
        }
        return false;
    }
    RoomStatus ended(const std::string& code) const {
        for (const RoomStatus& s : mgr->list(now)) {
            if (s.code == code && s.state == RoomState::Finished) return s;
        }
        return RoomStatus{};
    }
};

// Nothing a person typed or was given appears in the bytes that crossed the wire
bool wire_is_private(const Terminator& t, const std::vector<std::string>& words) {
    for (const std::string& w : words) {
        if (t.raw_in().find(w) != std::string::npos || t.raw_out().find(w) != std::string::npos) return false;
    }
    return !t.raw_in().empty() && !t.raw_out().empty();
}

// A connection that is never made: a stand-in for the link that a test does not want to open
class Pending final : public net::Connection {
public:
    bool send(const std::vector<uint8_t>&) override { return false; }
    bool poll(std::vector<uint8_t>&) override { return false; }
    State state() const override { return State::Connecting; }
    void close() override {}
};

}  // namespace

void run_wss_match_tests() {
    TEST_CASE("WM1.1 A whole online match over wss: two NetGames join a server's room through TLS and the WebSocket door, play, and end in one state with the referee; the keys they were given name the server by its written URL; nothing readable crossed the wire") {
        WssWorld w;
        ASSERT_TRUE(w.mgr->create_room(held_spec("WSS-1"), w.now).ok);
        Machine& a = w.join("ZebraAnnWss", "WSS-1");
        Machine& b = w.join("QuokkaBobWss", "WSS-1");
        ASSERT_TRUE(w.run_until([&]() { return w.running({&a, &b}); }, 16000 + kPre));
        w.run(6000);
        ASSERT_TRUE(w.status("WSS-1").state == RoomState::Running && !w.status("WSS-1").paused);
        ASSERT_EQ(w.accepted, 2u);
        ASSERT_EQ(w.term->handshakes, 2u);
        ASSERT_TRUE(a.keys_given.size() == 1 && b.keys_given.size() == 1);
        ASSERT_EQ(a.keys_given[0].server, w.url());                                            // the rejoin key says where the room is: the URL, in its one written form
        ASSERT_EQ(a.keys_given[0].server, net::wss_url_text(net::WssUrl{"localhost", w.term->port(), "/ws"}));
        ASSERT_TRUE(a.net.join_target().url == w.url() && a.net.join_target().resolved == "127.0.0.1" && a.net.join_target().address.empty());
        a.quit();
        ASSERT_TRUE(w.run_until([&]() { return w.finished("WSS-1") && a.sim.is_match_over() && b.sim.is_match_over(); }, 30000));
        w.run(1000);
        const RoomStatus end = w.ended("WSS-1");
        ASSERT_TRUE(end.state == RoomState::Finished && end.referee_hash != 0);
        ASSERT_TRUE(all_equal({&a, &b}) && a.sim.state_hash().total == end.referee_hash);       // both machines and the referee: one state
        ASSERT_TRUE(hashes_agree(a, b));
        ASSERT_FALSE(a.net.desynced() || b.net.desynced());
        ASSERT_TRUE(a.events.size() > 2 && b.events.size() > 2);
        ASSERT_TRUE(wire_is_private(*w.term, {"ZebraAnnWss", "QuokkaBobWss", "WSS-1", "Upgrade", "websocket"}));
        ASSERT_TRUE(w.term->raw_in().size() > 2 && static_cast<uint8_t>(w.term->raw_in()[0]) == 0x16 && static_cast<uint8_t>(w.term->raw_in()[1]) == 0x03);       // (a TLS handshake record opens the wire)
    } TEST_END();

    TEST_CASE("WM1.2 The way back over TLS: every link is pulled in the middle of a match, both machines make new links by themselves (TCP, TLS, the upgrade, the Hello with the key, the turns they lack), the match goes on and ends in one state") {
        WssWorld w;
        ASSERT_TRUE(w.mgr->create_room(held_spec("WSS-2"), w.now).ok);
        Machine& a = w.join("Ann", "WSS-2");
        Machine& b = w.join("Bob", "WSS-2");
        ASSERT_TRUE(w.run_until([&]() { return w.running({&a, &b}); }, 16000 + kPre));
        w.run(3000);
        ASSERT_EQ(w.term->handshakes, 2u);
        const uint32_t cut_tick = w.status("WSS-2").ticks;
        w.term->cut_all();                                                                      // the cable, for both
        ASSERT_TRUE(w.run_until([&]() { return w.status("WSS-2").paused; }, 4000));
        ASSERT_EQ(a.net.phase(), NetGame::Phase::Playing);                                      // the machines do not leave the match
        ASSERT_EQ(b.net.phase(), NetGame::Phase::Playing);
        ASSERT_TRUE(w.run_until([&]() { return w.status("WSS-2").rejoins >= 2 && !w.status("WSS-2").paused && !a.net.pause_info().reconnecting && !b.net.pause_info().reconnecting &&
                                               !a.net.pause_info().catching_up && !b.net.pause_info().catching_up; }, 40000));
        ASSERT_GE(w.term->handshakes, 4u);                                                      // two new TLS links, each with its certificate checked again
        ASSERT_GE(w.accepted, 4u);
        ASSERT_EQ(a.count(NetGame::Event::Type::Rejoined), size_t{1});
        ASSERT_EQ(b.count(NetGame::Event::Type::Rejoined), size_t{1});
        ASSERT_FALSE(a.saw(NetGame::Event::Type::HostLeft) || b.saw(NetGame::Event::Type::HostLeft) || a.saw(NetGame::Event::Type::Failed) || b.saw(NetGame::Event::Type::Failed));
        w.run(3000);
        ASSERT_TRUE(w.status("WSS-2").ticks > cut_tick + 20);                                   // the match runs again
        a.quit();
        ASSERT_TRUE(w.run_until([&]() { return w.finished("WSS-2") && a.sim.is_match_over() && b.sim.is_match_over(); }, 30000));
        w.run(1000);
        const RoomStatus end = w.ended("WSS-2");
        ASSERT_TRUE(all_equal({&a, &b}) && a.sim.state_hash().total == end.referee_hash && end.referee_hash != 0);
        ASSERT_FALSE(a.net.desynced() || b.net.desynced());
    } TEST_END();

    TEST_CASE("WM1.3 A machine that starts from nothing (a game started again) joins with its key over wss: the room gives it its seat and the match from the server's log, and it is back in the match") {
        WssWorld w;
        ASSERT_TRUE(w.mgr->create_room(held_spec("WSS-3"), w.now).ok);
        Machine& a = w.join("Ann", "WSS-3");
        Machine& b = w.join("Bob", "WSS-3");
        ASSERT_TRUE(w.run_until([&]() { return w.running({&a, &b}); }, 16000 + kPre));
        w.run(8000);
        ASSERT_EQ(b.keys_given.size(), size_t{1});
        const net::SeatKey key = b.keys_given[0].key;
        const uint8_t seat = b.net.my_seat();
        ASSERT_FALSE(net::key_is_zero(key));
        w.machines.erase(w.machines.begin() + 1);                                               // Bob's game is gone, with its links and no goodbye
        ASSERT_TRUE(w.run_until([&]() { return w.status("WSS-3").paused; }, 4000));
        Machine& b2 = w.join("Bob", "WSS-3", key, seat);
        ASSERT_TRUE(w.run_until([&]() { return b2.net.phase() == NetGame::Phase::Playing && !w.status("WSS-3").paused; }, 40000));
        ASSERT_EQ(b2.net.my_seat(), seat);
        ASSERT_TRUE(b2.keys_given.size() == 1 && net::key_matches(b2.keys_given[0].key, key));
        ASSERT_EQ(b2.keys_given[0].server, w.url());
        const RoomStatus s = w.status("WSS-3");
        ASSERT_TRUE(s.state == RoomState::Running && s.rejoins == 1 && s.absent.empty());
        ASSERT_TRUE(b2.net.turns_executed() > 100);                                             // the match so far was given to it
        w.run(3000);
        ASSERT_TRUE(hashes_agree(a, b2));
        ASSERT_FALSE(a.net.desynced() || b2.net.desynced());
    } TEST_END();

    TEST_CASE("WM2.1 A server whose certificate is not trusted, or is for another name, is refused before one byte of the game is sent: the machine fails with the reason, the door takes in nobody") {
        WssWorld w;
        ASSERT_TRUE(w.mgr->create_room(held_spec("WSS-4"), w.now).ok);
        Ca stranger("Another Authority");
        Machine& m = w.join("Mallory", "WSS-4", net::SeatKey{}, 255, &stranger);               // it trusts another authority than the one that issued the server's certificate
        ASSERT_TRUE(w.run_until([&]() { return m.net.phase() == NetGame::Phase::Failed; }, 8000));
        ASSERT_EQ(m.net.fail_reason(), NetGame::FailReason::Unreachable);
        ASSERT_FALSE(m.net.link_failure_text().empty());
        Machine& n = w.join("Nina", "WSS-4", net::SeatKey{}, 255, nullptr, "127.0.0.1");        // the certificate names localhost, the URL gives an address
        ASSERT_TRUE(w.run_until([&]() { return n.net.phase() == NetGame::Phase::Failed; }, 8000));
        ASSERT_EQ(n.net.fail_reason(), NetGame::FailReason::Unreachable);
        ASSERT_FALSE(n.net.link_failure_text().empty());
        w.run(300);
        ASSERT_EQ(w.accepted, 0u);                                                              // the door never saw either of them
        ASSERT_EQ(w.term->app_bytes_up, 0u);                                                    // not a byte of the game went through TLS
        ASSERT_EQ(w.term->handshakes, 0u);
        ASSERT_FALSE(m.saw(NetGame::Event::Type::StartRequested) || n.saw(NetGame::Event::Type::StartRequested));
        Machine& good = w.join("Gina", "WSS-4");                                                // the right authority and name: the same server takes her in
        ASSERT_TRUE(w.run_until([&]() { return good.net.phase() == NetGame::Phase::Room; }, 8000));
        ASSERT_EQ(w.accepted, 1u);
    } TEST_END();

    TEST_CASE("WM2.2 join_url takes only a usable wss:// address and makes no TCP link of a wss address: a refused address says why, an address that does not resolve does not join") {
        sim::SimulationEngine sim;
        NetGame net(sim);
        net.set_discovery(0);
        for (const char* url : {"ws://example.test/ws", "http://example.test/ws", "example.test", "wss://", "wss://exa mple.test/ws", "wss://example.test:99999/ws"}) {
            ASSERT_FALSE(net.join_url(url, "Ann", 255, "ROOM", ""));
            ASSERT_FALSE(net.join_refusal().empty());
            ASSERT_EQ(net.phase(), NetGame::Phase::Off);
        }
        net.set_resolver_for_test([](const std::string&) { return std::string(); });
        ASSERT_FALSE(net.join_url("wss://nowhere.example.test/ws", "Ann", 255, "ROOM", ""));
        ASSERT_FALSE(net.join_refusal().empty());
        ASSERT_EQ(net.phase(), NetGame::Phase::Off);
        net.set_resolver_for_test([](const std::string& host) { return host == "game.example.test" ? std::string("203.0.113.9") : std::string(); });
        net.set_link_maker_for_test([]() { return std::unique_ptr<net::Connection>(new Pending()); });          // (nothing is connected to: the test looks at what join_url keeps)
        ASSERT_TRUE(net.join_url("WSS://Game.Example.Test/ws?x=1", "Ann", 255, "ROOM", ""));
        ASSERT_EQ(net.join_target().url, std::string("wss://game.example.test/ws?x=1"));         // the written form (the key of a rejoin)
        ASSERT_TRUE(net.join_target().resolved == "203.0.113.9" && net.join_target().address.empty() && net.join_target().port == 0);
        ASSERT_EQ(net.phase(), NetGame::Phase::Connecting);
    } TEST_END();

    TEST_CASE("WM3.1 Plain TCP is for this machine and the local network: a server's room (it has a code, so it carries a seat key) is refused at an address of the Internet and joined at a local one; a direct host (no code) is joined by its address as ever") {
        sim::SimulationEngine sim;
        const auto resolver = [](const std::string& host) {
            if (host == "public.example.test") return std::string("203.0.113.9");
            if (host == "lan.example.test") return std::string("192.168.1.20");
            if (host == "v6.example.test") return std::string("2001:db8::7");
            if (host == "loop.example.test") return std::string("127.0.0.1");
            return std::string();
        };
        for (const char* host : {"public.example.test", "v6.example.test"}) {
            NetGame net(sim);
            net.set_discovery(0);
            net.set_resolver_for_test(resolver);
            net.set_link_maker_for_test([]() { return std::unique_ptr<net::Connection>(new Pending()); });
            ASSERT_FALSE(net.join(host, 4001, "Ann", 255, "ROOM-1", ""));
            ASSERT_FALSE(net.join_refusal().empty());
            ASSERT_EQ(net.phase(), NetGame::Phase::Off);
            ASSERT_TRUE(net.join(host, 4001, "Ann", 255, std::string(), ""));                    // no code: a direct host
            ASSERT_TRUE(net.join_refusal().empty());
        }
        for (const char* host : {"lan.example.test", "loop.example.test"}) {
            NetGame net(sim);
            net.set_discovery(0);
            net.set_resolver_for_test(resolver);
            net.set_link_maker_for_test([]() { return std::unique_ptr<net::Connection>(new Pending()); });
            ASSERT_TRUE(net.join(host, 4001, "Ann", 255, "ROOM-1", ""));
            ASSERT_EQ(net.phase(), NetGame::Phase::Connecting);
        }
    } TEST_END();
}

int main() {
    NetGame::default_prediction_budget_ns() = UINT64_MAX;      // (a busy machine stalls the test process now and then: that is not the prediction's cost, and no test is to lose its prediction to it)
    std::cout << "\n=======================================================\n [SUITE] Network port: the encrypted link in a whole match (real sockets, TLS, a real server)\n"
                 "=======================================================\n";
    run_wss_match_tests();
    std::cout << "\n=======================================================\n Total Test Cases: " << g_test_count << "\n Total Assertions: " << g_assert_count
              << "\n Failed:           " << g_test_failures << "\n=======================================================\n";
    if (g_test_count == 0) {                                      // (a misspelt or forgotten filter must not turn the suite green)
        std::cout << "\n no test ran: the filter ANTS_TEST_FILTER matches no test of this suite\n";
        return 1;
    }
    return g_test_failures == 0 ? 0 : 1;
}
