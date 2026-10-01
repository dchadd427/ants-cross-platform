#include "ants_net/session.hpp"

#include <algorithm>

namespace ants::net {

namespace {
constexpr uint8_t bit(uint8_t seat) noexcept { return static_cast<uint8_t>(1u << seat); }
}  // namespace

// ------------------------------------------------------------------------------------------------
// HostSession
// ------------------------------------------------------------------------------------------------

HostSession::HostSession(sim::SimulationEngine& sim, Config config)
    : HostSession(sim, config, std::make_unique<LockstepRunner>(sim, config.runner)) {}

HostSession::HostSession(sim::SimulationEngine& sim, Config config, std::unique_ptr<LockstepRunner> runner)
    : cfg_(config), sim_(&sim), sequencer_(config.sequencer), runner_(std::move(runner)) {
    sequencer_.set_host_player(cfg_.host_player);
}

void HostSession::add_client(uint8_t player, Connection* connection) {
    if (started_ || player >= sim::MAX_PLAYERS || player == cfg_.host_player || connection == nullptr) return;
    clients_[player].conn = connection;
    clients_[player].present = true;
}

void HostSession::start(uint32_t now_ms) {
    if (started_) return;
    started_ = true;
    last_ms_ = now_ms;
    next_seal_ms_ = now_ms;
    sequencer_.set_active(cfg_.host_player, true);
    for (uint8_t p = 0; p < sim::MAX_PLAYERS; ++p) {
        if (!clients_[p].present) continue;
        sequencer_.set_active(p, true);
        clients_[p].last_heard_ms = now_ms;
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
        following = static_cast<uint8_t>(following | bit(s.seat));
        sequencer_.set_active(s.seat, true);
        sequencer_.set_acked(s.seat, s.next_execute);
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
    sequencer_.submit(cfg_.host_player, std::move(command));
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
        int budget = 256;                            // a chatty client cannot starve the rest
        while (budget-- > 0 && c.present && c.conn->poll(msg)) {
            c.last_heard_ms = last_ms_;
            if (msg.size() > kMaxMessageBytes) {
                violation(p);
                continue;
            }
            handle_message(p, msg);
        }
        if (c.present && !c.conn->is_open()) drop(p);
    }
}

void HostSession::run_local(uint32_t dt_ms) {
    for (const LockstepRunner::Executed& e : runner_->update(dt_ms)) {
        sequencer_.on_ack(cfg_.host_player, e.turn);
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
    poll_clients();
    for (uint8_t p = 0; p < sim::MAX_PLAYERS; ++p) {         // a peer that says nothing for a minute is gone (its ack and ping stop)
        if (clients_[p].present && now_ms - clients_[p].last_heard_ms > cfg_.silence_timeout_ms) drop(p);
    }
    // seal the turns that are due (a fixed 100 ms schedule; while a peer lags too far the game waits and the schedule slides)
    int sealed = 0;
    while (!frozen_ && now_ms >= next_seal_ms_ && sealed < 5) {
        if (!sequencer_.can_seal()) {
            next_seal_ms_ = now_ms;
            break;
        }
        const TurnMsg turn = sequencer_.seal();
        broadcast(encode(turn));
        runner_->on_turn(turn);
        next_seal_ms_ += kTurnMs;
        ++sealed;
    }
    run_local(dt);
}

// ------------------------------------------------------------------------------------------------
// ClientSession
// ------------------------------------------------------------------------------------------------

ClientSession::ClientSession(sim::SimulationEngine& sim, Config config)
    : cfg_(config), runner_(std::make_unique<LockstepRunner>(sim, config.runner)), host_seat_(config.host) {}

void ClientSession::set_peer(uint8_t seat, Connection* link) {
    if (seat >= sim::MAX_PLAYERS || seat == cfg_.player || seat == host_seat_ || link == nullptr) return;
    peers_[seat] = link;
    peer_heard_[seat] = last_ms_;
}

void ClientSession::start(uint32_t now_ms) {
    started_ = true;
    last_ms_ = now_ms;
    last_heard_ms_ = now_ms;
    next_ping_ms_ = now_ms;
    next_peer_ping_ms_ = now_ms;
    peer_heard_.fill(now_ms);
}

bool ClientSession::submit(sim::Command command) {
    if (!started_ || mode_ != Mode::Normal || !connected()) return false;
    command.issuer = cfg_.player;
    CommandMsg m;
    m.command = std::move(command);
    return conn_->send(encode(m));
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

void ClientSession::go_lost() {
    mode_ = Mode::Lost;
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
                if (decode_ping(msg.data(), msg.size(), p) && p.nonce == ping_nonce_) rtt_ms_ = now_ms - p.sent_ms;
                break;
            }
            default:
                break;                               // anything else is ignored by a client
        }
    }
    // the host is gone when its link is closed (after everything it sent was read) or it has said nothing for host_silence_ms
    if (drained && !finished_ && (!conn_->is_open() || now_ms - last_heard_ms_ > cfg_.host_silence_ms)) {
        if (cfg_.migration) begin_election(now_ms);
        else go_lost();                              // a server does not hand over: when it is gone the match is over for this machine
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
            if (!decode(msg, m) || now_ms < next_request_ok_ms_[seat]) return;
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
    hold_until_ms_ = 0;
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
            const bool my_turn = (lowest_alive(now_ms) == cfg_.player || now_ms - wait_start_ms_ >= cfg_.elect_timeout_ms) && now_ms >= hold_until_ms_;
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
    if (mode_ == Mode::Normal) poll_host(now_ms);    // first: a host whose link has closed must be known as gone before a peer's proposal is judged
    poll_peers(now_ms);
    if (mode_ == Mode::Normal) poll_host(now_ms);    // and again: what the new host sent behind its Resume, or what arrived meanwhile
    if (electing()) election_tick(now_ms);
    if (mode_ == Mode::Promoted) return;             // the runner goes to the new HostSession
    for (const LockstepRunner::Executed& e : runner_->update(dt)) {
        if (mode_ != Mode::Normal || !connected()) continue;
        AckMsg a;
        a.turn = e.turn;
        conn_->send(encode(a));
        if (e.has_hash) {
            HashMsg h;
            h.turn = e.turn;
            h.hash = e.hash;
            conn_->send(encode(h));
        }
    }
    if (mode_ == Mode::Normal && connected() && now_ms >= next_ping_ms_) {
        PingMsg p;
        p.nonce = ++ping_nonce_;
        p.sent_ms = now_ms;
        conn_->send(encode_ping(p));
        next_ping_ms_ = now_ms + cfg_.ping_every_ms;
    }
    if (mode_ != Mode::Lost && now_ms >= next_peer_ping_ms_) {      // keeps the links to the other guests alive and measured
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
