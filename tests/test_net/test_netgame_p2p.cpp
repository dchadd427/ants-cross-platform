// Tests of the game's side of hints and channels (NetGame, p2p_transport.hpp, docs/P2P_HINTS.md), in a match of real NetGames on the local network with a transport that the test plays.
//
// What is checked: an order that the prediction applied is sent to the other players as a hint (with the tick it was applied at and the player's own number for it); what a channel delivers goes
// to the prediction only when it is a hint of the channel's own seat; a computer player's order that the server sends goes to the prediction; a server with no relay opens nothing; and none of it
// changes what the machines run; and the round trip that the game reports for each other player's channel.
#include "netgame_fixtures.hpp"

#include <tuple>

namespace {

// The page's channels as the test plays them: what the game gave it, and what the test makes it deliver
class FakeChannels final : public P2pTransport {
public:
    struct Opened {
        uint8_t seat;
        bool direct;
        IceConfigMsg ice;
    };
    std::vector<Opened> opened;
    std::vector<std::pair<uint8_t, uint8_t>> peer_masks;
    std::vector<std::vector<uint8_t>> sent_hints;
    std::vector<std::pair<uint8_t, std::vector<uint8_t>>> to_deliver;      // (seat of the channel, message)
    std::vector<std::tuple<uint8_t, SignalKind, std::string>> to_signal;
    int closed{0};
    PeerLink state{PeerLink::None};
    std::array<int32_t, 4> rtts{{-1, -1, -1, -1}};                         // what rtt_ms says for each seat (the page's measurement)

    void open(uint8_t seat, bool direct, const IceConfigMsg& ice) override { opened.push_back(Opened{seat, direct, ice}); }
    void peers(uint8_t ready, uint8_t direct) override { peer_masks.emplace_back(ready, direct); }
    void signal(uint8_t, SignalKind, const std::string&) override {}
    void send_hint(const std::vector<uint8_t>& message) override { sent_hints.push_back(message); }
    bool poll_signal(uint8_t& to, SignalKind& kind, std::string& text) override {
        if (to_signal.empty()) return false;
        std::tie(to, kind, text) = to_signal.front();
        to_signal.erase(to_signal.begin());
        return true;
    }
    bool poll_hint(uint8_t& from, std::vector<uint8_t>& message) override {
        if (to_deliver.empty()) return false;
        from = to_deliver.front().first;
        message = to_deliver.front().second;
        to_deliver.erase(to_deliver.begin());
        return true;
    }
    PeerLink link(uint8_t) const override { return state; }
    int32_t rtt_ms(uint8_t seat) const noexcept override { return seat < rtts.size() ? rtts[seat] : -1; }
    void close() override { ++closed; }
};

// A transport that measures nothing: the answer of the interface itself
class MutePage final : public P2pTransport {
public:
    void open(uint8_t, bool, const IceConfigMsg&) override {}
    void peers(uint8_t, uint8_t) override {}
    void signal(uint8_t, SignalKind, const std::string&) override {}
    void send_hint(const std::vector<uint8_t>&) override {}
    bool poll_signal(uint8_t&, SignalKind&, std::string&) override { return false; }
    bool poll_hint(uint8_t&, std::vector<uint8_t>&) override { return false; }
    PeerLink link(uint8_t) const override { return PeerLink::Relay; }
    void close() override {}
};

// A match of a host (seat 0) and one guest (seat 1) that is running, the guest with channels of the test's
struct HintMatch {
    Table t;
    FakeChannels channels;
    bool ok{false};
    Machine* host{nullptr};
    Machine* guest{nullptr};

    explicit HintMatch(bool with_bot = false) {
        Machine& alice = t.add("Alice");
        if (!alice.net.host(0, "Alice", true)) return;
        alice.net.set_map("TINY.LVL");
        Machine& bob = t.add("Bob");
        bob.net.set_p2p(&channels, false);
        if (!bob.net.join("127.0.0.1", alice.net.listen_port(), "Bob")) return;
        host = &alice;
        guest = &bob;
        if (!t.run_until([&]() { return bob.net.phase() == NetGame::Phase::Room && bob.net.my_seat() == 1 && alice.net.can_start(); }, 8000)) return;
        if (with_bot) {                                                                  // seat 2 is a bot
            alice.net.set_fill_bots(FillPlan(std::array<FillLevel, 4>{FillLevel::None, FillLevel::None, FillLevel::Hard, FillLevel::None}));
            if (alice.net.fill_bots(alice.net.fill_bots()) != 1u) return;
        }
        uint64_t hash = 0;
        if (!hash_file(maps_dir() + "TINY.LVL", hash) || !host->net.start_match(11, hash)) return;
        ok = t.run_until([&]() { return everybody_running(t); }, kUntilRunning);
    }
    uint32_t own_ant() { return first_ant(*guest, 1); }
};

void run_hint_netgame_tests() {
    TEST_CASE("N25.1 An Order That The Prediction Applied Is Sent To The Other Players As A Hint, With The Player's Own Numbers (1, 2, 3 ...) And The Tick That The Prediction Names For It; A Game With No Transport Sends None") {
        HintMatch m;
        ASSERT_TRUE(m.ok);
        m.t.run(1000);                                                                   // (the P2pConfig that the game says once the match plays)
        ASSERT_TRUE(m.guest->net.predicting());
        const uint32_t ant = m.own_ant();
        ASSERT_EQ(m.guest->net.submit(order(1, ant, 12, 12)).status, sim::CommandResult::Status::Applied);
        const uint64_t named_at = m.guest->net.prediction()->last_order_tick();
        m.t.run(300);
        ASSERT_EQ(m.guest->net.submit(order(1, ant, 14, 12)).status, sim::CommandResult::Status::Applied);
        m.t.run(300);
        ASSERT_EQ(m.channels.sent_hints.size(), size_t{2});
        HintMsg first, second;
        ASSERT_TRUE(decode(m.channels.sent_hints[0], first) && decode(m.channels.sent_hints[1], second));
        ASSERT_TRUE(first.seq == 1 && second.seq == 2);
        ASSERT_TRUE(first.command.issuer == 1 && first.command.type == CommandType::GroupMove && first.command.tile_x == 12 && second.command.tile_x == 14);
        ASSERT_TRUE(first.tick >= 1 && second.tick > first.tick);
        ASSERT_EQ(uint64_t{first.tick}, named_at);                                         // (the tick that the prediction names for it: the turn that the server will seal it in, or this screen's tick until it knows)
        ASSERT_EQ(m.guest->net.hint_counts().sent, uint64_t{2});
        // Hatch is not predicted, so it is not hinted
        sim::Command hatch;
        hatch.type = CommandType::Hatch;
        m.guest->net.submit(hatch);
        m.t.run(200);
        ASSERT_EQ(m.channels.sent_hints.size(), size_t{2});
        // the host's own game has no transport: nothing
        ASSERT_EQ(m.host->net.hint_counts().sent, uint64_t{0});
        // the server (here a LAN host) has no relay: the game was given an empty credential list and opened nothing
        ASSERT_TRUE(m.channels.opened.empty() && m.channels.peer_masks.empty());
    } TEST_END();

    TEST_CASE("N25.2 What A Peer's Channel Delivers Goes To The Prediction When It Is A Hint Of That Channel's Own Seat; A Message That Is Not A Hint, A Hint Of Another Seat And A Hint Of This Player's Own Seat Are Refused") {
        HintMatch m;
        ASSERT_TRUE(m.ok);
        m.t.run(1000);
        const uint32_t host_ant = first_ant(*m.guest, 0);
        const uint64_t tick = m.guest->net.prediction()->display_tick() + 2;
        const auto hint_of = [&](uint8_t issuer, uint32_t seq, uint32_t ant) {
            HintMsg h;
            h.seq = seq;
            h.tick = static_cast<uint32_t>(tick);
            h.command = order(issuer, ant, 16, 16);
            return encode(h);
        };
        m.channels.to_deliver.push_back({0, hint_of(0, 1, host_ant)});                  // seat 0's channel, seat 0's order: taken
        m.channels.to_deliver.push_back({0, hint_of(2, 2, host_ant)});                  // seat 0's channel says seat 2: refused (the channel says who the sender is, never the payload)
        m.channels.to_deliver.push_back({1, hint_of(1, 3, m.own_ant())});               // this player's own seat: refused
        m.channels.to_deliver.push_back({0, std::vector<uint8_t>{1, 2, 3}});            // not a hint
        m.channels.to_deliver.push_back({9, hint_of(0, 4, host_ant)});                  // a channel of no seat
        m.t.run(100);
        const Prediction::Stats& st = m.guest->net.prediction()->stats();
        ASSERT_EQ(m.guest->net.hint_counts().from_peers, uint64_t{1});
        ASSERT_EQ(m.guest->net.hint_counts().refused, uint64_t{4});
        ASSERT_EQ(st.hints_received, uint64_t{1});
        ASSERT_EQ(st.hints_accepted, uint64_t{1});
        // the order is not sealed by anybody: the hint is dropped by the sealed stream, and both machines are the same to the end
        m.t.run(2000);
        ASSERT_EQ(m.guest->net.prediction()->pending_hints(), size_t{0});
        ASSERT_TRUE(all_equal(m.t));
    } TEST_END();

    TEST_CASE("N25.3 A Computer Player's Order Reaches Every Player's Prediction As A Hint The Moment The Server Gives It, And Is Confirmed By The Turn That Carries It") {
        HintMatch m(true);
        ASSERT_TRUE(m.ok);
        m.t.run(1500);
        ASSERT_TRUE(m.guest->net.predicting());
        uint8_t bot = 255;                                                                   // (the seat that the START filled)
        for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) {
            if (seat >= 2 && (m.host->net.start_info().roster & (1u << seat)) != 0 && bot == 255) bot = seat;
        }
        ASSERT_TRUE(bot != 255);
        const uint32_t bot_ant = first_ant(*m.host, bot);
        ASSERT_TRUE(bot_ant != 0);
        const Prediction::Stats before = m.guest->net.prediction()->stats();
        ASSERT_TRUE(m.host->net.submit_bot(bot, order(bot, bot_ant, 20, 20)));
        m.t.run(400);
        const Prediction::Stats& st = m.guest->net.prediction()->stats();
        ASSERT_EQ(m.guest->net.hint_counts().from_server, uint64_t{1});
        ASSERT_EQ(st.hints_received, before.hints_received + 1);
        ASSERT_TRUE(st.hints_accepted + st.hints_late == before.hints_accepted + before.hints_late + 1);
        ASSERT_TRUE(st.hints_confirmed + st.hints_confirmed_moved + st.hints_late >= 1u);       // (the turn came: confirmed, or the hint came after it on a link that has no delay)
        ASSERT_EQ(m.guest->net.prediction()->pending_hints(), size_t{0});
        m.t.run(3000);
        ASSERT_TRUE(all_equal(m.t));
    } TEST_END();

    TEST_CASE("N25.5 A Game That Is Told Not To Take Hints Ignores Them, From A Channel And From The Server: Nothing Is Counted And The Prediction Is Never Given One") {
        HintMatch m(true);
        ASSERT_TRUE(m.ok);
        m.guest->net.set_hints_enabled(false);
        m.t.run(1500);
        uint8_t bot = 255;
        for (uint8_t seat = 2; seat < sim::MAX_PLAYERS; ++seat) {
            if ((m.host->net.start_info().roster & (1u << seat)) != 0 && bot == 255) bot = seat;
        }
        ASSERT_TRUE(bot != 255);
        ASSERT_TRUE(m.host->net.submit_bot(bot, order(bot, first_ant(*m.host, bot), 20, 20)));
        HintMsg h;
        h.seq = 1;
        h.tick = static_cast<uint32_t>(m.guest->net.prediction()->display_tick() + 2);
        h.command = order(0, first_ant(*m.guest, 0), 16, 16);
        m.channels.to_deliver.push_back({0, encode(h)});
        m.t.run(500);
        ASSERT_EQ(m.guest->net.hint_counts().from_server + m.guest->net.hint_counts().from_peers, uint64_t{0});
        ASSERT_EQ(m.guest->net.prediction()->stats().hints_received, uint64_t{0});
        m.t.run(2000);
        ASSERT_TRUE(all_equal(m.t));
    } TEST_END();

    TEST_CASE("N25.4 With A Transport The Game Closes It When It Ends And Reports How It Is Connected To Each Other Player; Without One Every Player Is Not Connected") {
        HintMatch m;
        ASSERT_TRUE(m.ok);
        ASSERT_TRUE(m.guest->net.peer_link(0) == PeerLink::None);
        m.channels.state = PeerLink::Relay;
        ASSERT_TRUE(m.guest->net.peer_link(0) == PeerLink::Relay && m.guest->net.peer_link(2) == PeerLink::Relay);
        ASSERT_TRUE(m.guest->net.peer_link(1) == PeerLink::None && m.guest->net.peer_link(4) == PeerLink::None);   // (not to oneself, not to a seat that is not one)
        ASSERT_TRUE(m.host->net.peer_link(1) == PeerLink::None);
        m.channels.state = PeerLink::Direct;
        ASSERT_TRUE(m.guest->net.peer_link(0) == PeerLink::Direct);
        m.guest->net.leave();
        ASSERT_EQ(m.channels.closed, 1);
    } TEST_END();
    TEST_CASE("N25.6 The Game Stamps Its Orders (Protocol 30): Every Hint Carries The Tick That The Order Was Stamped With, The Host Answers Every Order, And The Host Executes The Order At That Tick When It Is Still Open (A Lead That The Answers Have Raised Covers The Lag)") {
        HintMatch m;
        ASSERT_TRUE(m.ok);
        m.t.run(2500);                                                                   // (turns arrive)
        ASSERT_TRUE(m.guest->net.predicting());
        const uint32_t ant = m.own_ant();
        std::vector<uint64_t> stamped;
        for (uint32_t j = 0; j < 18; ++j) {
            ASSERT_EQ(m.guest->net.submit(order(1, ant, static_cast<int16_t>(10 + j), 12)).status, sim::CommandResult::Status::Applied);
            stamped.push_back(m.guest->net.prediction()->last_order_tick());
            m.t.run(230 + 31 * (j % 3));                                                 // (the orders are given at different moments of the tick)
        }
        m.t.run(500);
        ASSERT_EQ(m.channels.sent_hints.size(), stamped.size());
        for (size_t i = 0; i < stamped.size(); ++i) {
            HintMsg h;
            ASSERT_TRUE(decode(m.channels.sent_hints[i], h));
            ASSERT_EQ(uint64_t{h.tick}, stamped[i]);                                     // (the hint names the stamp)
        }
        ASSERT_EQ(m.guest->net.prediction()->stats().order_acks, uint64_t{18});          // (the host answered every order)
        size_t at_stamp = 0;
        for (size_t i = 0; i < stamped.size(); ++i) {
            for (const auto& executed : m.host->applied) {
                if (executed.second.issuer == 1 && executed.second.tile_x == static_cast<int16_t>(10 + i) && executed.first == stamped[i]) ++at_stamp;
            }
        }
        ASSERT_TRUE(at_stamp >= 14u);                                                    // (the first orders may be late, before the answers have taught the lead)
    } TEST_END();

    TEST_CASE("N25.7 The Game Reports The Round Trip Over The Channel To Each Other Player In Milliseconds; No Number (-1) Without A Transport, For The Player's Own Seat, For A Seat That Is Not One, For A Channel With No Answer, And From A Transport That Measures Nothing") {
        HintMatch m;
        ASSERT_TRUE(m.ok);
        ASSERT_EQ(m.guest->net.peer_rtt_ms(0), -1);                      // (the page has no answer yet)
        m.channels.rtts = {{42, 77, 130, 0}};
        ASSERT_TRUE(m.guest->net.peer_rtt_ms(0) == 42 && m.guest->net.peer_rtt_ms(2) == 130 && m.guest->net.peer_rtt_ms(3) == 0);
        ASSERT_EQ(m.guest->net.peer_rtt_ms(1), -1);                      // (the guest is seat 1: there is no channel to oneself, whatever the page says)
        ASSERT_TRUE(m.guest->net.peer_rtt_ms(4) == -1 && m.guest->net.peer_rtt_ms(255) == -1);
        m.channels.rtts = {{-5, -1, 20, -1}};
        ASSERT_TRUE(m.guest->net.peer_rtt_ms(0) == -1 && m.guest->net.peer_rtt_ms(3) == -1 && m.guest->net.peer_rtt_ms(2) == 20);   // (every negative number is the same: none)
        ASSERT_EQ(m.host->net.peer_rtt_ms(1), -1);                       // (the host has no transport)
        MutePage mute;
        ASSERT_EQ(static_cast<const P2pTransport&>(mute).rtt_ms(0), -1);
    } TEST_END();

    TEST_CASE("N25.8 A Hosting Game Stamps Its Own Orders Too (The Sequencer Is Its Own Host: Its Answer Is Given At Once): Every Order Of The Host's Player Is Answered, The Prediction Learns The Lag From It, And The Order Is Executed At The Tick That It Was Stamped With Once The Lead Covers The Lag") {
        HintMatch m;
        ASSERT_TRUE(m.ok);
        m.t.run(2500);
        ASSERT_TRUE(m.host->net.predicting());
        const uint32_t ant = first_ant(*m.host, 0);
        std::vector<uint64_t> stamped;
        for (uint32_t j = 0; j < 14; ++j) {
            ASSERT_EQ(m.host->net.submit(order(0, ant, static_cast<int16_t>(10 + j), 12)).status, sim::CommandResult::Status::Applied);
            stamped.push_back(m.host->net.prediction()->last_order_tick());
            m.t.run(230 + 31 * (j % 3));
        }
        m.t.run(500);
        const Prediction::Stats& st = m.host->net.prediction()->stats();
        ASSERT_EQ(st.order_acks, uint64_t{14});                                          // (every order was answered, at once)
        ASSERT_TRUE(m.host->net.prediction()->learned_lag_ticks() >= 1u);                // (and the lag is learned from the answers)
        size_t at_stamp = 0;
        for (size_t i = 0; i < stamped.size(); ++i) {
            for (const auto& executed : m.host->applied) {
                if (executed.second.issuer == 0 && executed.second.tile_x == static_cast<int16_t>(10 + i) && executed.first == stamped[i]) ++at_stamp;
            }
        }
        ASSERT_TRUE(at_stamp >= 11u);                                                    // (the first orders may be late, before the answers have taught the lead)
    } TEST_END();

    TEST_CASE("N25.9 The Numbers Of The Orders Go On When The Prediction Is Made Again In The Same Match (Switched Off And On): The Answer That Was On Its Way When It Went Off Is About An Order Of The Old Prediction, And No Order Of The New One Has Its Number; Only The Orders Of The Match Are Counted") {
        HintMatch m;
        ASSERT_TRUE(m.ok);
        m.t.run(2500);
        ASSERT_TRUE(m.guest->net.predicting());
        const uint32_t ant = m.own_ant();
        for (int16_t j = 0; j < 3; ++j) {
            ASSERT_EQ(m.guest->net.submit(order(1, ant, static_cast<int16_t>(10 + j), 12)).status, sim::CommandResult::Status::Applied);
            m.t.run(200);
        }
        ASSERT_EQ(m.guest->net.prediction()->last_order_seq(), 3u);
        m.guest->net.set_prediction_enabled(false);                                      // (the player switches it off: the prediction is gone, and so is its count)
        m.t.run(300);
        ASSERT_TRUE(m.guest->net.prediction() == nullptr);
        ASSERT_EQ(m.guest->net.submit(order(1, ant, 20, 12)).status, sim::CommandResult::Status::Applied);       // (an order with no prediction carries no number)
        m.t.run(200);
        m.guest->net.set_prediction_enabled(true);
        m.t.run(600);
        ASSERT_TRUE(m.guest->net.predicting());
        ASSERT_EQ(m.guest->net.submit(order(1, ant, 21, 12)).status, sim::CommandResult::Status::Applied);
        ASSERT_EQ(m.guest->net.prediction()->last_order_seq(), 4u);                      // the number after the last one of the old prediction, not 1 again
        m.t.run(400);
        ASSERT_EQ(m.guest->net.prediction()->stats().order_acks, uint64_t{1});          // (and the host's answer to it is the only one that this prediction took)
    } TEST_END();
}

}  // namespace

void run_p2p_netgame_tests() { run_hint_netgame_tests(); }
