#include "ants_net/session.hpp"

#include "ants_net/clock.hpp"

#include <algorithm>

namespace ants::net {

namespace {
constexpr uint8_t bit(uint8_t seat) noexcept { return static_cast<uint8_t>(1u << seat); }
constexpr int kMaxSealsPerUpdate = static_cast<int>(500 / kTurnMs);     // a pass that came late seals the turns it missed, up to half a second of them, then the schedule goes on
}  // namespace

// ------------------------------------------------------------------------------------------------
// HostSession
// ------------------------------------------------------------------------------------------------

HostSession::HostSession(sim::SimulationEngine& sim, Config config)
    : HostSession(sim, config, std::make_unique<LockstepRunner>(sim, config.runner)) {}

HostSession::HostSession(sim::SimulationEngine& sim, Config config, std::unique_ptr<LockstepRunner> runner)
    : cfg_(config), sim_(&sim), sequencer_(config.sequencer), runner_(std::move(runner)), delay_(config.host_player) {
    sequencer_.set_host_player(cfg_.host_player);
    // the delay of the host's own commands: the tick that applies one is read off the runner (a runner that a promoted guest brings has its old session's observer replaced)
    runner_->set_on_applied([this](const sim::Command& c) { delay_.on_applied(c, last_ms_); });
}

void HostSession::add_client(uint8_t player, Connection* connection) {
    if (started_ || player >= sim::MAX_PLAYERS || player == cfg_.host_player || connection == nullptr) return;
    clients_[player].conn = connection;
    clients_[player].present = true;
}

void HostSession::add_bot_seat(uint8_t player) {
    if (started_ || player >= sim::MAX_PLAYERS || player == cfg_.host_player || clients_[player].present) return;
    bot_seats_ = static_cast<uint8_t>(bot_seats_ | bit(player));
}

bool HostSession::submit_bot(uint8_t player, sim::Command command) {
    if (!started_ || !is_bot_seat(player)) return false;
    return sequencer_.submit(player, std::move(command));
}

void HostSession::start(uint32_t now_ms) {
    if (started_) return;
    started_ = true;
    last_ms_ = now_ms;
    next_seal_ms_ = now_ms;
    sequencer_.set_active(cfg_.host_player, true);
    for (uint8_t p = 0; p < sim::MAX_PLAYERS; ++p) {
        if (is_bot_seat(p)) sequencer_.set_active(p, true);                    // a bot's commands are accepted and its turns awaited (see run_local)
    }
    for (uint8_t p = 0; p < sim::MAX_PLAYERS; ++p) {
        if (!clients_[p].present) continue;
        sequencer_.set_active(p, true);
        clients_[p].last_heard_ms = now_ms;
        clients_[p].progress_ms = now_ms;
        clients_[p].acked_seen = sequencer_.acked(p);
    }
}

void HostSession::resume(uint32_t now_ms, uint32_t resume_turn, uint8_t old_host, const std::vector<PeerState>& survivors) {
    if (started_) return;
    started_ = true;
    last_ms_ = now_ms;
    next_seal_ms_ = now_ms;
    sequencer_.resume(resume_turn);
    sequencer_.set_host_player(cfg_.host_player);
    sequencer_.set_active(cfg_.host_player, true);
    sequencer_.set_acked(cfg_.host_player, runner_->next_turn_to_execute());
    uint8_t following = bit(cfg_.host_player);
    for (const PeerState& s : survivors) {
        if (s.seat >= sim::MAX_PLAYERS || s.seat == cfg_.host_player || s.conn == nullptr || clients_[s.seat].present) continue;
        clients_[s.seat].conn = s.conn;
        clients_[s.seat].present = true;
        clients_[s.seat].violations = 0;
        clients_[s.seat].last_heard_ms = now_ms;
        clients_[s.seat].progress_ms = now_ms;
        following = static_cast<uint8_t>(following | bit(s.seat));
        sequencer_.set_active(s.seat, true);
        sequencer_.set_acked(s.seat, s.next_execute);
        clients_[s.seat].acked_seen = sequencer_.acked(s.seat);
        ResumeMsg r;
        r.epoch = cfg_.epoch;
        r.host = cfg_.host_player;
        r.resume_turn = resume_turn;
        if (!s.conn->send(encode(r))) {
            drop(s.seat);
            continue;
        }
        send_turns(s.conn, s.next_receive, resume_turn);
    }
    // the old host and every seat of the roster that did not follow leave with the first turn (a seat that already dropped stays as it is)
    const uint8_t roster = sim_->roster_mask();
    if (old_host < sim::MAX_PLAYERS && old_host != cfg_.host_player) announce_drop(old_host);
    for (uint8_t p = 0; p < sim::MAX_PLAYERS; ++p) {
        if ((roster & bit(p)) == 0 || (following & bit(p)) != 0 || p == old_host || sim_->is_player_dropped(p)) continue;
        announce_drop(p);
    }
}

void HostSession::send_turns(Connection* conn, uint32_t from_turn, uint32_t to_turn) {
    if (conn == nullptr || from_turn >= to_turn || runner_->logged_turn(from_turn) == nullptr) return;
    for (uint32_t t = from_turn; t < to_turn; ++t) {
        const TurnMsg* m = runner_->logged_turn(t);
        if (m == nullptr || !conn->send(encode(*m))) return;
    }
}

void HostSession::submit_local(sim::Command command) {
    if (!started_ || seatless()) return;
    command.issuer = cfg_.host_player;
    if (sequencer_.submit(cfg_.host_player, command)) delay_.on_sent(command);     // (a refused command never reaches a turn: nothing to measure)
}

void HostSession::chat_local(const std::string& text, bool team) {
    if (seatless()) return;
    ChatMsg m;
    m.sender = cfg_.host_player;
    m.team = team;
    m.text = text;
    broadcast(encode(m));
    if (on_chat_) on_chat_(m);
}

void HostSession::broadcast(const std::vector<uint8_t>& msg, uint8_t except) {
    for (uint8_t p = 0; p < sim::MAX_PLAYERS; ++p) {
        if (p == except || !clients_[p].present || clients_[p].conn == nullptr) continue;
        if (!clients_[p].conn->send(msg)) drop(p);
    }
}

void HostSession::announce_drop(uint8_t player) {
    sim::Command gone;                               // FUN_0100d03b on every machine, in the same turn
    gone.type = sim::CommandType::Drop;
    gone.issuer = player;
    sequencer_.submit_system(std::move(gone));
    if (on_left_) on_left_(player);
}

void HostSession::drop(uint8_t player) {
    if (player >= sim::MAX_PLAYERS || !clients_[player].present) return;
    if (clients_[player].lagging) {                  // the others' notice about this player ends with it (the drop itself is told by the turn stream)
        clients_[player].lagging = false;
        broadcast(encode(LagMsg{player, 0}), player);
    }
    clients_[player].present = false;
    if (clients_[player].conn != nullptr && clients_[player].conn->is_open()) clients_[player].conn->close();
    sequencer_.set_active(player, false);
    announce_drop(player);
}

void HostSession::violation(uint8_t player) {
    if (++clients_[player].violations >= cfg_.violation_limit) drop(player);
}

void HostSession::report_hash(uint8_t player, uint32_t turn, const sim::StateHash& hash) {
    for (const DesyncMsg& d : sequencer_.on_hash(player, turn, hash)) {
        desyncs_.push_back(d);
        broadcast(encode(d));
        frozen_ = true;                              // two machines no longer play the same game: no more turns
    }
}

void HostSession::handle_message(uint8_t player, const std::vector<uint8_t>& msg) {
    switch (peek_type(msg)) {
        case MsgType::Command: {
            CommandMsg m;
            if (!decode(msg, m)) return violation(player);
            if (!sequencer_.submit(player, std::move(m.command))) violation(player);
            return;
        }
        case MsgType::TurnAck: {
            AckMsg m;
            if (!decode(msg, m)) return violation(player);
            sequencer_.on_ack(player, m.turn);
            return;
        }
        case MsgType::Hash: {
            HashMsg m;
            if (!decode(msg, m)) return violation(player);
            report_hash(player, m.turn, m.hash);
            return;
        }
        case MsgType::Ping: {
            PingMsg m;
            if (!decode_ping(msg.data(), msg.size(), m)) return violation(player);
            if (clients_[player].conn != nullptr) clients_[player].conn->send(encode_pong(m));
            return;
        }
        case MsgType::Leave:
            if (msg.size() != 1) return violation(player);
            drop(player);                            // a player who quits leaves like one whose connection dies
            return;
        case MsgType::StartRequest: {                // a dedicated server's leader pressed START a second time: this one crossed the Start on the wire, the match runs already
            StartRequestMsg m;
            if (!decode(msg, m) || !seatless()) return violation(player);      // (a host that holds a seat has no leader: nobody sends it one)
            // no offence at first (a second or third click), but a connection that sends more of them than a person could is flooding: each one after those is a violation
            if (++clients_[player].ignored_start_requests > kIgnoredStartRequestsAllowed) violation(player);
            return;
        }
        case MsgType::Chat: {
            ChatMsg m;
            if (!decode(msg, m)) return violation(player);
            m.sender = player;                       // the connection speaks, not the payload
            broadcast(encode(m));
            if (on_chat_) on_chat_(m);
            return;
        }
        default:
            return violation(player);                // hosts do not receive Turn, Desync, Welcome, ...
    }
}

void HostSession::poll_clients() {
    for (uint8_t p = 0; p < sim::MAX_PLAYERS; ++p) {
        Client& c = clients_[p];
        if (!c.present || c.conn == nullptr) continue;
        std::vector<uint8_t> msg;
        int budget = 256;                            // a chatty client cannot starve the rest: the rest of what it sent waits for the next update
        while (budget-- > 0 && c.present && c.conn->poll(msg)) {
            c.last_heard_ms = last_ms_;
            if (msg.size() > kMaxMessageBytes) {
                violation(p);
                continue;
            }
            if (!c.talk.take(last_ms_, cfg_.message_burst, cfg_.messages_per_second)) {      // more than a client can have to say: not even looked at
                violation(p);
                continue;
            }
            handle_message(p, msg);
        }
        if (c.present && !c.conn->is_open()) drop(p);
    }
}

uint8_t HostSession::lagging_mask() const noexcept {
    uint8_t mask = 0;
    for (uint8_t p = 0; p < sim::MAX_PLAYERS; ++p) {
        if (clients_[p].present && clients_[p].lagging) mask = static_cast<uint8_t>(mask | bit(p));
    }
    return mask;
}

void HostSession::announce_lag(uint8_t player, uint32_t behind_ms) {
    broadcast(encode(LagMsg{player, behind_ms}));                  // everybody, the player itself too: its backlog may be on the way (a slow link) and not in its own queue
}

// A host without a seat does not wait for a player that falls behind (see the head of this file): it says so to the others, and throws out the player that cannot be waited for
void HostSession::police_laggards(uint32_t now_ms) {
    for (uint8_t p = 0; p < sim::MAX_PLAYERS; ++p) {
        Client& c = clients_[p];
        if (!c.present) continue;
        const uint32_t acked = sequencer_.acked(p);
        if (acked != c.acked_seen) {                                 // an ack that moved: it runs turns
            c.acked_seen = acked;
            c.progress_ms = now_ms;
        }
        const uint32_t behind = behind_ms(p);
        const bool idle = behind > 0 && cfg_.lag_drop_idle_ms != 0 && now_ms - c.progress_ms >= cfg_.lag_drop_idle_ms;
        if (idle || (cfg_.lag_drop_behind_ms != 0 && behind >= cfg_.lag_drop_behind_ms)) {
            drop(p);                                                 // the others play on, as they did all along; the drop travels in the turn stream
            continue;
        }
        if (behind >= cfg_.lag_notice_ms) {
            if (!c.lagging || time_reached(now_ms, c.next_notice_ms)) {
                c.lagging = true;
                c.next_notice_ms = now_ms + cfg_.lag_notice_every_ms;
                announce_lag(p, behind);
            }
        } else if (c.lagging && behind <= cfg_.lag_clear_ms) {
            c.lagging = false;
            announce_lag(p, 0);
        }
    }
}

void HostSession::run_local(uint32_t dt_ms) {
    for (const LockstepRunner::Executed& e : runner_->update(dt_ms)) {
        sequencer_.on_ack(cfg_.host_player, e.turn);
        for (uint8_t p = 0; p < sim::MAX_PLAYERS; ++p) {
            if (is_bot_seat(p)) sequencer_.on_ack(p, e.turn);                  // a bot has no connection to say it: it has executed what this machine has
        }
        if (!e.has_hash) continue;
        if (seatless()) {                                 // the referee: its own engine's hash is what every client is compared with
            for (const DesyncMsg& d : sequencer_.on_referee_hash(e.turn, e.hash)) {
                desyncs_.push_back(d);
                broadcast(encode(d));
                frozen_ = true;
            }
        } else {
            report_hash(cfg_.host_player, e.turn, e.hash);
        }
    }
}

void HostSession::update(uint32_t now_ms) {
    if (!started_) return;
    const uint32_t dt = now_ms - last_ms_;
    last_ms_ = now_ms;
    delay_.on_frame(now_ms);
    poll_clients();
    for (uint8_t p = 0; p < sim::MAX_PLAYERS; ++p) {         // a peer that says nothing for a minute is gone (its ack and ping stop)
        if (clients_[p].present && now_ms - clients_[p].last_heard_ms > cfg_.silence_timeout_ms) drop(p);
    }
    // seal the turns that are due (a fixed 50 ms schedule). A host with a seat waits while a peer lags too far (the schedule slides); a host without a seat never does.
    int sealed = 0;
    while (!frozen_ && time_reached(now_ms, next_seal_ms_) && sealed < kMaxSealsPerUpdate) {
        if (!seatless() && !sequencer_.can_seal()) {
            next_seal_ms_ = now_ms;
            const uint8_t laggard = sequencer_.laggard();
            if (laggard != stall_player_) {                  // a new holder-up: the clock starts again
                stall_player_ = laggard;
                stall_since_ms_ = now_ms;
            } else if (cfg_.laggard_drop_ms != 0 && laggard < sim::MAX_PLAYERS && clients_[laggard].present && now_ms - stall_since_ms_ >= cfg_.laggard_drop_ms) {
                drop(laggard);                               // it does not execute the turns (and may still answer pings): the others play on
                stall_player_ = 255;
            }
            break;
        }
        stall_player_ = 255;
        const TurnMsg turn = sequencer_.seal();
        broadcast(encode(turn));
        runner_->on_turn(turn);
        next_seal_ms_ += kTurnMs;
        ++sealed;
    }
    if (seatless() && started_ && !frozen_) police_laggards(now_ms);
    run_local(dt);
}

// ------------------------------------------------------------------------------------------------
// ClientSession
// ------------------------------------------------------------------------------------------------

ClientSession::ClientSession(sim::SimulationEngine& sim, Config config)
    : cfg_(config), runner_(std::make_unique<LockstepRunner>(sim, config.runner)), delay_(config.player), host_seat_(config.host) {
    runner_->set_on_applied([this](const sim::Command& c) { delay_.on_applied(c, last_ms_); });    // the delay of this player's commands (latency.hpp)
}

void ClientSession::set_peer(uint8_t seat, Connection* link) {
    if (seat >= sim::MAX_PLAYERS || seat == cfg_.player || seat == host_seat_ || link == nullptr) return;
    peers_[seat] = link;
    peer_heard_[seat] = last_ms_;
}

void ClientSession::start(uint32_t now_ms) {
    started_ = true;
    last_ms_ = now_ms;
    last_heard_ms_ = now_ms;
    asked_ = false;
    next_ping_ms_ = now_ms;
    next_peer_ping_ms_ = now_ms;
    next_request_ok_ms_.fill(now_ms);                // (deadlines are set from the clock, never left at 0: see clock.hpp)
    hold_until_ms_ = now_ms;
    peer_heard_.fill(now_ms);
}

bool ClientSession::submit(sim::Command command) {
    if (!started_ || mode_ != Mode::Normal || !connected()) return false;
    command.issuer = cfg_.player;
    CommandMsg m;
    m.command = std::move(command);
    if (!conn_->send(encode(m))) return false;
    delay_.on_sent(m.command);                       // measured from now to the tick that applies it (its send time is the clock of the frame that sends it)
    return true;
}

void ClientSession::leave() {
    if (conn_ != nullptr && conn_->is_open()) {
        conn_->send(encode_leave());
        conn_->close();
    }
    for (Connection* p : peers_) {                    // the other guests see us gone at once
        if (p != nullptr && p->is_open()) p->close();
    }
}

bool ClientSession::chat(const std::string& text, bool team) {
    if (!started_ || mode_ != Mode::Normal || !connected()) return false;
    ChatMsg m;
    m.sender = cfg_.player;
    m.team = team;
    m.text = text;
    return conn_->send(encode(m));
}

uint8_t ClientSession::lagging_seat() const noexcept {
    uint8_t worst = 255;
    uint32_t worst_ms = 0;
    for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) {
        const LagNotice& n = lag_[seat];
        if (n.behind_ms == 0 || last_ms_ - n.heard_ms > kLagNoticeStaleMs) continue;     // none, or not renewed: gone
        if (n.behind_ms > worst_ms) {
            worst = seat;
            worst_ms = n.behind_ms;
        }
    }
    return worst;
}

uint32_t ClientSession::lagging_behind_ms() const noexcept {
    const uint8_t seat = lagging_seat();
    return seat < sim::MAX_PLAYERS ? lag_[seat].behind_ms : 0u;
}

bool ClientSession::peer_alive(uint8_t seat, uint32_t now_ms) const {
    return seat < sim::MAX_PLAYERS && peers_[seat] != nullptr && peers_[seat]->is_open() && now_ms - peer_heard_[seat] <= cfg_.peer_silence_ms;
}

bool ClientSession::host_alive(uint32_t now_ms) const { return mode_ == Mode::Normal && connected() && now_ms - last_heard_ms_ <= cfg_.host_alive_ms; }

uint8_t ClientSession::lowest_alive(uint32_t now_ms) const {
    for (uint8_t s = 0; s < sim::MAX_PLAYERS; ++s) {
        if (s == cfg_.player) return s;
        if (!dead_[s] && (s == hint_ || peer_alive(s, now_ms))) return s;
    }
    return cfg_.player;
}

void ClientSession::send_to_peer(uint8_t seat, const std::vector<uint8_t>& msg) {
    if (seat < sim::MAX_PLAYERS && peers_[seat] != nullptr && peers_[seat]->is_open()) peers_[seat]->send(msg);
}

void ClientSession::send_turns(Connection* link, uint32_t from_turn) {
    const uint32_t to = runner_->next_turn_expected();
    if (link == nullptr || from_turn >= to || runner_->logged_turn(from_turn) == nullptr) return;   // older than the log: nothing contiguous to give
    for (uint32_t t = from_turn; t < to; ++t) {
        const TurnMsg* m = runner_->logged_turn(t);
        if (m == nullptr || !link->send(encode(*m))) return;
    }
}

void ClientSession::go_lost(LostReason reason) {
    mode_ = Mode::Lost;
    lost_reason_ = reason;
    proposed_ = false;
    following_ = 255;
}

// ---- host link ---------------------------------------------------------------------------------------------------------------------------------

void ClientSession::poll_host(uint32_t now_ms) {
    if (conn_ == nullptr) return;
    std::vector<uint8_t> msg;
    int budget = 512;
    bool drained = false;
    while (budget-- > 0) {
        if (!conn_->poll(msg)) {
            drained = true;
            break;
        }
        last_heard_ms_ = now_ms;
        asked_ = false;                              // whatever the host says, it lives
        switch (peek_type(msg)) {
            case MsgType::Turn: {
                TurnMsg t;
                if (decode(msg, t)) runner_->on_turn(std::move(t));
                break;
            }
            case MsgType::Desync: {
                DesyncMsg d;
                if (decode(msg, d)) {
                    desynced_ = true;
                    desync_ = d;
                }
                break;
            }
            case MsgType::Chat: {
                ChatMsg c;
                if (decode(msg, c) && on_chat_) on_chat_(c);
                break;
            }
            case MsgType::Pong: {
                PingMsg p;
                if (decode_ping(msg.data(), msg.size(), p)) ping_.on_pong(p, now_ms, conn_->last_message_age_ms());
                break;
            }
            case MsgType::Lag: {
                LagMsg m;
                if (decode(msg.data(), msg.size(), m)) {
                    LagNotice& n = m.seat == cfg_.player ? self_lag_ : lag_[m.seat];      // (a notice about this seat is kept apart: lagging_seat() names the OTHER players)
                    n.behind_ms = m.behind_ms;
                    n.heard_ms = now_ms;
                }
                break;
            }
            default:
                break;                               // anything else is ignored by a client
        }
    }
    // the host is gone when its link is closed (after everything it sent was read) or it has said nothing for host_silence_ms
    if (drained && !finished_ && (!conn_->is_open() || now_ms - last_heard_ms_ > cfg_.host_silence_ms)) {
        if (cfg_.migration) begin_election(now_ms);
        else go_lost(!conn_->is_open() && dedicated() && runner_->backlog_ms() >= kAwayBacklogMs ? LostReason::AwayTooLong : LostReason::Connection);
        // (a server does not hand over: when it is gone the match is over for this machine. A server that closed the link of a machine that holds half a minute of the match
        // unplayed dropped it for being away: the idle rule, kLagDropIdleMs; and the turns that it sent before are all read by now)
    }
}

// ---- peer links --------------------------------------------------------------------------------------------------------------------------------

void ClientSession::poll_peers(uint32_t now_ms) {
    for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) {
        Connection* link = peers_[seat];
        if (link == nullptr) continue;
        std::vector<uint8_t> msg;
        int budget = 256;                            // a chatty peer cannot starve the rest
        while (budget-- > 0 && peers_[seat] == link && mode_ != Mode::Promoted && mode_ != Mode::Lost && link->poll(msg)) {
            peer_heard_[seat] = now_ms;
            if (msg.size() > kMaxMessageBytes) continue;
            handle_peer_message(seat, msg, now_ms);
        }
    }
}

void ClientSession::handle_peer_message(uint8_t seat, const std::vector<uint8_t>& msg, uint32_t now_ms) {
    switch (peek_type(msg)) {
        case MsgType::Ping: {
            PingMsg p;
            if (decode_ping(msg.data(), msg.size(), p)) send_to_peer(seat, encode_pong(p));
            return;
        }
        case MsgType::Propose: {
            ProposeMsg m;
            if (decode(msg, m)) on_propose(seat, m, now_ms);
            return;
        }
        case MsgType::Accept: {
            AcceptMsg m;
            if (!decode(msg, m) || !proposed_ || m.epoch != election_epoch() || (expected_mask_ & bit(seat)) == 0) return;
            Answer& a = answers_[seat];
            a.answered = true;
            a.accepted = true;
            a.next_receive = m.next_receive;
            a.next_execute = m.next_execute;
            return;
        }
        case MsgType::Refuse: {
            RefuseMsg m;
            if (decode(msg, m)) on_refuse(seat, m, now_ms);
            return;
        }
        case MsgType::Resume: {
            ResumeMsg m;
            if (decode(msg, m)) on_resume(seat, m, now_ms);
            return;
        }
        case MsgType::Request: {
            RequestMsg m;
            if (!decode(msg, m) || !time_reached(now_ms, next_request_ok_ms_[seat])) return;
            next_request_ok_ms_[seat] = now_ms + 250;
            send_turns(peers_[seat], m.from_turn);
            return;
        }
        case MsgType::Turn: {                        // the turns a candidate fetched from the guest that got furthest
            TurnMsg t;
            if (mode_ == Mode::Fetching && seat == fetch_from_ && decode(msg, t) && runner_->on_turn(std::move(t))) fetch_progress_ms_ = now_ms;
            return;
        }
        default:
            return;                                  // Pong and everything else: hearing from the peer was enough
    }
}

void ClientSession::on_propose(uint8_t seat, const ProposeMsg& m, uint32_t now_ms) {
    if (m.candidate != seat || m.epoch != election_epoch() || mode_ == Mode::Lost || mode_ == Mode::Promoted) return;   // a link speaks for itself
    if (mode_ == Mode::Normal) {
        if (host_alive(now_ms)) {                    // the host lives for us: the sender lost its own link to it
            send_to_peer(seat, encode(RefuseMsg{election_epoch(), host_seat_}));
            return;
        }
        begin_election(now_ms);                      // the sender's word that the host is gone agrees with what we see
    }
    dead_[seat] = false;                             // it is talking to us: it lives
    if (following_ != 255 && following_ != seat && !dead_[following_] && peer_alive(following_, now_ms)) {
        send_to_peer(seat, encode(RefuseMsg{election_epoch(), following_}));      // one candidate per election
        return;
    }
    const uint8_t low = lowest_alive(now_ms);
    if (low < seat) {                                // a lower seat lives (perhaps this one): the lowest wins
        send_to_peer(seat, encode(RefuseMsg{election_epoch(), low}));
        return;
    }
    if (proposed_) withdraw(seat);
    following_ = seat;
    mode_ = Mode::Following;
    follow_start_ms_ = now_ms;
    AcceptMsg a;
    a.epoch = election_epoch();
    a.next_receive = runner_->next_turn_expected();
    a.next_execute = runner_->next_turn_to_execute();
    send_to_peer(seat, encode(a));
}

void ClientSession::on_refuse(uint8_t seat, const RefuseMsg& m, uint32_t now_ms) {
    if (m.epoch != election_epoch()) return;
    if (following_ == seat) {                        // the candidate we accepted withdrew (a lower seat lives): choose again
        following_ = 255;
        mode_ = Mode::Electing;
        wait_start_ms_ = now_ms;
        return;
    }
    if (!proposed_ || (expected_mask_ & bit(seat)) == 0) return;
    Answer& a = answers_[seat];
    a.answered = true;
    a.accepted = false;
    a.lowest = m.lowest;
}

void ClientSession::on_resume(uint8_t seat, const ResumeMsg& m, uint32_t now_ms) {
    if (!electing() || m.host != seat || m.epoch != election_epoch() || following_ != seat) return;
    if (m.resume_turn < runner_->next_turn_expected()) return go_lost();       // the new host lacks turns that we hold: no common history
    conn_ = peers_[seat];                            // the link to the new host is the host link from now on
    peers_[seat] = nullptr;
    host_seat_ = seat;
    epoch_ = m.epoch;
    mode_ = Mode::Normal;
    following_ = 255;
    proposed_ = false;
    expected_mask_ = 0;
    last_heard_ms_ = now_ms;
    asked_ = false;
    next_ping_ms_ = now_ms;
}

// ---- the election --------------------------------------------------------------------------------------------------------------------------------

void ClientSession::begin_election(uint32_t now_ms) {
    if (conn_ != nullptr && conn_->is_open()) conn_->close();
    conn_ = nullptr;                                 // nothing more is read from the old host: the turns we hold now are the history we offer
    old_host_ = host_seat_;
    dead_.fill(false);
    if (old_host_ < sim::MAX_PLAYERS) dead_[old_host_] = true;
    answers_.fill(Answer{});
    proposed_ = false;
    expected_mask_ = 0;
    following_ = 255;
    hint_ = 255;
    host_refusals_ = 0;
    hold_until_ms_ = now_ms;
    mode_ = Mode::Electing;
    elect_start_ms_ = now_ms;
    wait_start_ms_ = now_ms;
}

bool ClientSession::all_answered(uint32_t) const {
    for (uint8_t s = 0; s < sim::MAX_PLAYERS; ++s) {
        if ((expected_mask_ & bit(s)) == 0 || answers_[s].answered) continue;
        if (peers_[s] != nullptr && peers_[s]->is_open()) return false;
    }
    return true;
}

void ClientSession::propose(uint32_t now_ms) {
    proposed_ = true;
    proposal_ms_ = now_ms;
    expected_mask_ = 0;
    answers_.fill(Answer{});
    const std::vector<uint8_t> msg = encode(ProposeMsg{election_epoch(), cfg_.player});
    for (uint8_t s = 0; s < sim::MAX_PLAYERS; ++s) {
        if (s == cfg_.player || peers_[s] == nullptr || dead_[s] || !peer_alive(s, now_ms)) continue;
        if (peers_[s]->send(msg)) expected_mask_ = static_cast<uint8_t>(expected_mask_ | bit(s));
        else dead_[s] = true;
    }
}

void ClientSession::withdraw(uint8_t in_favour_of) {
    const std::vector<uint8_t> msg = encode(RefuseMsg{election_epoch(), in_favour_of});
    for (uint8_t s = 0; s < sim::MAX_PLAYERS; ++s) {
        if ((expected_mask_ & bit(s)) != 0) send_to_peer(s, msg);
    }
    proposed_ = false;
    expected_mask_ = 0;
    answers_.fill(Answer{});
}

void ClientSession::resolve(uint32_t now_ms) {
    bool host_refused = false;
    uint8_t defer = 255;
    for (uint8_t s = 0; s < sim::MAX_PLAYERS; ++s) {
        if ((expected_mask_ & bit(s)) == 0) continue;
        const Answer& a = answers_[s];
        if (!a.answered) {                           // it never answered: it is gone
            dead_[s] = true;
            continue;
        }
        if (a.accepted) continue;
        if (a.lowest == old_host_) {                 // the host lives for that guest: it has not noticed yet that the host is gone, or only we lost it
            host_refused = true;
            continue;
        }
        if (a.lowest < cfg_.player) {
            if (a.lowest == hint_) dead_[a.lowest] = true;      // we already waited for that seat once and it never proposed
            else defer = std::min(defer, a.lowest);
        } else {
            dead_[s] = true;                         // it follows a seat above ours that cannot see us: not ours
        }
    }
    if (host_refused) {                              // ask again in a moment; when the others keep saying that their host lives, it is we who lost it
        if (++host_refusals_ >= cfg_.host_refusals_limit) return go_lost();
        withdraw(cfg_.player);
        hold_until_ms_ = now_ms + cfg_.retry_ms;
        return;
    }
    if (defer != 255) {                              // a lower seat lives: it takes over, we wait for its proposal
        withdraw(defer);
        hint_ = defer;
        wait_start_ms_ = now_ms;
        return;
    }
    plan_fetch(now_ms);
}

void ClientSession::plan_fetch(uint32_t now_ms) {
    uint32_t target = runner_->next_turn_expected();
    uint8_t source = 255;
    for (uint8_t s = 0; s < sim::MAX_PLAYERS; ++s) {
        const bool follows = (expected_mask_ & bit(s)) != 0 && answers_[s].accepted && !dead_[s] && peers_[s] != nullptr && peers_[s]->is_open();
        if (follows && answers_[s].next_receive > target) {
            target = answers_[s].next_receive;
            source = s;
        }
    }
    if (source == 255) return promote();             // nobody is ahead of us: the history is complete
    fetch_from_ = source;
    fetch_target_ = target;
    fetch_progress_ms_ = now_ms;
    mode_ = Mode::Fetching;
    RequestMsg r;
    r.from_turn = runner_->next_turn_expected();
    send_to_peer(source, encode(r));
}

void ClientSession::fetch_tick(uint32_t now_ms) {
    if (runner_->next_turn_expected() >= fetch_target_) return promote();
    Connection* src = fetch_from_ < sim::MAX_PLAYERS ? peers_[fetch_from_] : nullptr;
    if (src == nullptr || !src->is_open() || now_ms - fetch_progress_ms_ > cfg_.fetch_timeout_ms) {
        dead_[fetch_from_] = true;                   // its extra turns are lost with it: plan again without it
        plan_fetch(now_ms);
    }
}

void ClientSession::promote() {
    promotion_ = Promotion{};
    promotion_.epoch = election_epoch();
    promotion_.old_host = old_host_;
    promotion_.resume_turn = runner_->next_turn_expected();
    for (uint8_t s = 0; s < sim::MAX_PLAYERS; ++s) {
        const bool follows = (expected_mask_ & bit(s)) != 0 && answers_[s].accepted && !dead_[s] && peers_[s] != nullptr && peers_[s]->is_open();
        if (!follows) continue;
        PeerState p;
        p.seat = s;
        p.conn = peers_[s];
        p.next_receive = answers_[s].next_receive;
        p.next_execute = answers_[s].next_execute;
        promotion_.survivors.push_back(p);
    }
    mode_ = Mode::Promoted;
}

void ClientSession::election_tick(uint32_t now_ms) {
    if (now_ms - elect_start_ms_ > cfg_.election_limit_ms) return go_lost();
    switch (mode_) {
        case Mode::Electing: {
            // our turn when no lower seat lives, or when the one we waited for did not propose: then we ask everybody (a guest whose host lives
            // answers so, and this machine learns that it is the one that lost the host)
            const bool my_turn = (lowest_alive(now_ms) == cfg_.player || now_ms - wait_start_ms_ >= cfg_.elect_timeout_ms) && time_reached(now_ms, hold_until_ms_);
            if (my_turn) {
                if (!proposed_) propose(now_ms);
                if (proposed_ && (all_answered(now_ms) || now_ms - proposal_ms_ >= cfg_.accept_timeout_ms)) resolve(now_ms);
            }
            break;
        }
        case Mode::Following:
            if (following_ == 255 || dead_[following_] || !peer_alive(following_, now_ms) ||
                now_ms - follow_start_ms_ > cfg_.accept_timeout_ms + 2 * cfg_.fetch_timeout_ms) {
                if (following_ != 255) dead_[following_] = true;   // it never resumed
                following_ = 255;
                mode_ = Mode::Electing;
                wait_start_ms_ = now_ms;
            }
            break;
        case Mode::Fetching:
            fetch_tick(now_ms);
            break;
        default:
            break;
    }
}

// ---- every frame -------------------------------------------------------------------------------------------------------------------------------

void ClientSession::update(uint32_t now_ms) {
    if (!started_ || mode_ == Mode::Promoted || runner_ == nullptr) return;
    const uint32_t dt = now_ms - last_ms_;
    last_ms_ = now_ms;
    delay_.on_frame(now_ms);
    if (mode_ == Mode::Normal) poll_host(now_ms);    // first: a host whose link has closed must be known as gone before a peer's proposal is judged
    poll_peers(now_ms);
    if (mode_ == Mode::Normal) poll_host(now_ms);    // and again: what the new host sent behind its Resume, or what arrived meanwhile
    if (electing()) election_tick(now_ms);
    if (mode_ == Mode::Promoted) return;             // the runner goes to the new HostSession
    const std::vector<LockstepRunner::Executed> ran = runner_->update(dt);
    if (!ran.empty() && mode_ == Mode::Normal && connected()) {
        for (const LockstepRunner::Executed& e : ran) {      // the hashes first: the host drops its reference of a turn that everybody has acknowledged
            if (!e.has_hash) continue;
            HashMsg h;
            h.turn = e.turn;
            h.hash = e.hash;
            conn_->send(encode(h));
        }
        AckMsg a;                                            // one ack per frame, for the last turn it ran: an ack says "I have executed up to this turn"
        a.turn = ran.back().turn;
        conn_->send(encode(a));
    }
    if (dedicated()) {                                       // "Catching up..." from 3 s behind until within 1 s (the host does not wait: the backlog is run down at up to 4x)
        const uint32_t backlog = runner_->backlog_ms();
        if (!catching_up_ && backlog >= kLagNoticeMs) catching_up_ = true;
        else if (catching_up_ && backlog <= kLagClearMs) catching_up_ = false;
    }
    if (mode_ == Mode::Normal && connected() && time_reached(now_ms, next_ping_ms_)) {
        conn_->send(encode_ping(ping_.next(now_ms)));
        next_ping_ms_ = now_ms + cfg_.ping_every_ms;
        if (!asked_) {                                 // the host is asked whether it lives: the oldest question that nothing has answered yet (note_gap)
            asked_ = true;
            asked_ms_ = now_ms;
        }
    }
    if (mode_ != Mode::Lost && time_reached(now_ms, next_peer_ping_ms_)) {      // keeps the links to the other guests alive and measured
        PingMsg p;
        p.nonce = 0;
        p.sent_ms = now_ms;
        const std::vector<uint8_t> ping = encode_ping(p);
        for (uint8_t s = 0; s < sim::MAX_PLAYERS; ++s) {
            if (peers_[s] != nullptr && peers_[s]->is_open()) peers_[s]->send(ping);
        }
        next_peer_ping_ms_ = now_ms + cfg_.ping_every_ms;
    }
}

// A host that said nothing in the update that has just run has said nothing in the gap either (a message that arrived meanwhile waited in the link and was read: heard at
// that update's clock, which is the last of last_ms_). It is held to account only if it was ASKED before: a ping went out in an earlier update and nothing at all has come from
// the host since, so that the answer is overdue. Otherwise the host had no chance to say that it lives (a sleeping page pings nobody; a host that holds its turns is quiet
// and answers pings): the gap counts only up to kAskGraceMs short of the limit. The update that has just run sent the ping (a wake-up that hands over a gap is more than
// a ping period after the one before it), the answer, a message, is heard, and a host that stays silent has the gap held against it by the next wake-up. The unsigned
// arithmetic of the silence test is modular, so the stamp may be taken back below zero.
bool ClientSession::note_gap(uint32_t gap_ms) {
    if (!started_ || mode_ != Mode::Normal || gap_ms == 0 || last_heard_ms_ == last_ms_) return false;
    uint32_t credit = gap_ms;
    const bool asked_before = asked_ && static_cast<int32_t>(last_ms_ - asked_ms_) > 0;
    if (!asked_before) {
        const uint32_t silent_ms = last_ms_ - last_heard_ms_;
        const uint32_t room = cfg_.host_silence_ms > silent_ms + kAskGraceMs ? cfg_.host_silence_ms - silent_ms - kAskGraceMs : 0u;
        credit = std::min(gap_ms, room);
    }
    last_heard_ms_ -= credit;
    return true;
}

std::unique_ptr<HostSession> promote_to_host(ClientSession& client, sim::SimulationEngine& sim, HostSession::Config config, uint32_t now_ms,
                                             const std::function<void(HostSession&)>& prepare) {
    if (!client.promoted()) return nullptr;
    std::unique_ptr<LockstepRunner> runner = client.release_runner();
    if (runner == nullptr) return nullptr;
    const ClientSession::Promotion& p = client.promotion();
    config.host_player = client.player();
    config.epoch = p.epoch;
    auto host = std::make_unique<HostSession>(sim, config, std::move(runner));
    if (prepare) prepare(*host);                     // hooks are in place before the first drop is announced
    host->resume(now_ms, p.resume_turn, p.old_host, p.survivors);
    return host;
}

}  // namespace ants::net
