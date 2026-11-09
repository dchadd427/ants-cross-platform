// The guest's half of the session: ClientSession (the host link, the links to the other guests, the election of a new host, the way back after a lost link) and promote_to_host.
// The host's half is in session.cpp.

#include "ants_net/session.hpp"

#include "ants_net/clock.hpp"

#include <algorithm>

#include "session_detail.hpp"

namespace ants::net {

using session_detail::bit;

// ------------------------------------------------------------------------------------------------
// ClientSession
// ------------------------------------------------------------------------------------------------

ClientSession::ClientSession(sim::SimulationEngine& sim, Config config)
    : cfg_(config), sim_(&sim), runner_(std::make_unique<LockstepRunner>(sim, config.runner)), delay_(config.player), host_seat_(config.host) {
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
    reconnect_since_ms_ = now_ms;
    attempt_ms_ = now_ms;
    next_attempt_ms_ = now_ms;
    if (cfg_.rejoin) mode_ = Mode::CatchingUp;       // this machine starts from nothing: the server gives it the match (CatchUp, TurnBatch) over the link that its lobby used
}

bool ClientSession::submit(sim::Command command) {
    if (!started_ || mode_ != Mode::Normal || !connected() || paused()) return false;
    command.issuer = cfg_.player;
    CommandMsg m;
    m.command = std::move(command);
    if (!conn_->send(encode(m))) return false;
    delay_.on_sent(m.command);                       // measured from now to the tick that applies it (its send time is the clock of the frame that sends it)
    return true;
}

void ClientSession::leave() {
    left_ = true;
    if (conn_ != nullptr && conn_->is_open()) {
        conn_->send(encode_leave());
        conn_->close();
    }
    for (Connection* p : peers_) {                    // the other guests see us gone at once
        if (p != nullptr && p->is_open()) p->close();
    }
    if (reconnecting()) mode_ = Mode::Lost;           // a machine that is on its way back and quits has no more attempts to make
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
            case MsgType::Presence: {                // who is missing, the vote, the cap, the resume countdown (a server that holds seats)
                PresenceMsg p;
                if (decode(msg, p)) apply_presence(std::move(p), now_ms);
                break;
            }
            case MsgType::Reject: {                  // a newer window took the seat (Superseded), the seat was dropped, ...: the server's word is final
                RejectMsg r;
                if (cfg_.reconnect && decode(msg, r)) reject(r.reason);
                break;
            }
            default:
                break;                               // anything else is ignored by a client
        }
        if (mode_ != Mode::Normal) return;           // (a Reject ended it)
    }
    // the host is gone when its link is closed (after everything it sent was read) or it has said nothing for host_silence_ms
    if (drained && !finished_ && (!conn_->is_open() || now_ms - last_heard_ms_ > cfg_.host_silence_ms)) {
        if (cfg_.migration) begin_election(now_ms);
        else if (cfg_.reconnect && !left_ && !desynced_ && !key_is_zero(cfg_.key)) enter_reconnecting(now_ms);     // a server that holds seats: this machine is on its way back
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
    if (mode_ == Mode::Rejoining || mode_ == Mode::CatchingUp) poll_new_link(now_ms);
    if (cfg_.reconnect && mode_ == Mode::Normal) poll_host(now_ms);          // what came behind the Presence that ended a catch-up
    if ((mode_ == Mode::Reconnecting || mode_ == Mode::Rejoining) && now_ms - reconnect_since_ms_ > give_up_ms()) go_lost();      // the way back is closed for good
    if (electing()) election_tick(now_ms);
    if (mode_ == Mode::Promoted) return;             // the runner goes to the new HostSession
    if (mode_ == Mode::CatchingUp) {                 // the match so far, at full speed and silently; nothing else of the live match runs meanwhile
        catch_up_step(now_ms);
        ping_while_catching_up(now_ms);
        return;
    }
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

// A host that said nothing in the update that has just run has said nothing in the gap either (a message that arrived meanwhile waited in the link and was read: heard at that
// update's clock, which is the last of last_ms_). It is held to account only if it was ASKED before: a ping went out in an earlier update and nothing at all has come from the host
// since, so that the answer is overdue. Otherwise the host had no chance to say that it lives (a sleeping page pings nobody; a host that holds its turns is quiet and answers pings):
// the gap counts only up to kAskGraceMs short of the limit. The update that has just run sent the ping (a wake-up that hands over a gap is more than a ping period after the one before
// it), the answer, a message, is heard, and a host that stays silent has the gap held against it by the next wake-up. The unsigned arithmetic of the silence test is modular, so the
// stamp may be taken back below zero. A machine that is catching up is told the same way (it pings once a second and the server answers). The modes without a question to be asked
// count the gap in full: a new link that has said Hello and has no Welcome yet has asked its question with the Hello, and a machine that has no link is waiting for its way back.
bool ClientSession::note_gap(uint32_t gap_ms) {
    if (!started_ || gap_ms == 0) return false;
    switch (mode_) {
        case Mode::Reconnecting:                     // no link: the 31 minutes of the way back and the 2 s between the attempts are real time
            reconnect_since_ms_ -= gap_ms;
            next_attempt_ms_ -= gap_ms;
            return true;
        case Mode::Rejoining:                        // the Hello is the question: a Welcome that does not come for rejoin_timeout_ms of real time ends the attempt
            attempt_ms_ -= gap_ms;
            reconnect_since_ms_ -= gap_ms;
            return true;
        case Mode::Normal:
        case Mode::CatchingUp:
            break;
        default:
            return false;
    }
    if (last_heard_ms_ == last_ms_) return false;
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

// ---- reconnect ------------------------------------------------------------------------------------------------------------------------------------------

// A machine that catches up pings once a second like one that plays (a link that is quiet for 10 s is a link that is gone: note_gap)
void ClientSession::ping_while_catching_up(uint32_t now_ms) {
    if (mode_ != Mode::CatchingUp || !connected() || !time_reached(now_ms, next_ping_ms_)) return;
    conn_->send(encode_ping(ping_.next(now_ms)));
    next_ping_ms_ = now_ms + cfg_.ping_every_ms;
    if (!asked_) {
        asked_ = true;
        asked_ms_ = now_ms;
    }
}

// The server's word is final: dropped, no such room, a newer window took the seat, a way back that is closed. No more attempts.
void ClientSession::reject(RejectReason reason) {
    reject_ = reason;
    has_reject_ = true;
    if (conn_ != nullptr && conn_->is_open()) conn_->close();
    go_lost();
}

bool ClientSession::wants_connection(uint32_t now_ms) const noexcept {
    return mode_ == Mode::Reconnecting && !finished_ && !left_ && time_reached(now_ms, next_attempt_ms_);
}

// The first loss of the link: the give-up time counts from here, through every attempt that follows, until the machine is back
void ClientSession::enter_reconnecting(uint32_t now_ms) {
    if (conn_ != nullptr && conn_->is_open()) conn_->close();
    conn_ = nullptr;
    mode_ = Mode::Reconnecting;
    reconnect_since_ms_ = now_ms;
    attempts_ = 0;
    attempt_ms_ = now_ms;
    next_attempt_ms_ = now_ms;                       // the first attempt is due at once
    hello_pending_ = false;
    catch_known_ = false;
    caught_up_sent_ = false;
}

// The link of an attempt is gone (closed, never opened, silent): the next attempt is due 2 s after this one began, at once when it has lasted longer than that
void ClientSession::attempt_failed(uint32_t now_ms) {
    (void)now_ms;
    if (conn_ != nullptr && conn_->is_open()) conn_->close();
    conn_ = nullptr;
    mode_ = Mode::Reconnecting;
    hello_pending_ = false;
    catch_known_ = false;
    caught_up_sent_ = false;
}

void ClientSession::attach(Connection* link, uint32_t now_ms) {
    if (mode_ != Mode::Reconnecting || finished_ || left_) return;
    ++attempts_;
    attempt_ms_ = now_ms;
    next_attempt_ms_ = now_ms + cfg_.reconnect_every_ms;
    if (link == nullptr) return;                     // no link could be made: the next attempt is due then
    conn_ = link;
    mode_ = Mode::Rejoining;
    last_heard_ms_ = now_ms;
    asked_ = false;
    hello_pending_ = true;
    catch_known_ = false;
    caught_up_sent_ = false;
    if (link->is_open()) send_rejoin_hello();
}

// The Hello of a machine that comes back: the key shows who it is, the turns say what it has already (next_turn_expected: what it has received, executed or not). It goes out when the
// link is open, which a TCP link made without blocking is not yet when it is attached: a Hello written to it then is lost.
void ClientSession::send_rejoin_hello() {
    if (!hello_pending_ || conn_ == nullptr || !conn_->is_open()) return;
    HelloMsg h = cfg_.hello;
    h.version = kProtocolVersion;
    h.key = cfg_.key;
    h.have_turns = runner_->next_turn_expected();
    conn_->send(encode(h));
    hello_pending_ = false;
}

// What the server said about who is missing, the vote, the cap and the countdown. The match is HELD while a seat is missing or a countdown runs: the runner is told, so that the wait for
// turns that the server will not seal is no stall of the link (it would grow the jitter buffer of every player for a pause that has nothing to do with the link: lockstep.hpp). The cap that
// it names is what the way back is given up by.
void ClientSession::apply_presence(PresenceMsg p, uint32_t now_ms) {
    const bool was_held = paused();
    presence_ = std::move(p);
    cap_known_ = true;
    cap_s_ = presence_.cap_s;
    cap_heard_ms_ = now_ms;
    cap_paused_ = !presence_.missing.empty();
    if (paused() != was_held && runner_ != nullptr) runner_->set_held(paused());
}

uint32_t ClientSession::give_up_ms() const noexcept {
    if (!cap_known_) return cfg_.reconnect_give_up_ms;
    // the cap runs from the loss of the link, or from the last Presence when a seat was missing then (the pause was on already)
    const int64_t cap_ms = cap_s_ >= kCapSecondsMore ? int64_t{kMaxCapSeconds} * 1000 : int64_t{cap_s_} * 1000;
    const int64_t earlier = cap_paused_ ? int64_t{static_cast<int32_t>(reconnect_since_ms_ - cap_heard_ms_)} : 0;      // (how long before the loss that Presence was, wrap-safe)
    const int64_t limit = cap_ms + int64_t{kReconnectMarginMs} - std::min(earlier, cap_ms);                              // (never less than the margin)
    return static_cast<uint32_t>(std::min<int64_t>(limit, UINT32_MAX));
}

bool ClientSession::vote(uint8_t seat, bool continue_without) {
    if (mode_ != Mode::Normal || !connected() || seat >= sim::MAX_PLAYERS) return false;
    return conn_->send(encode(VoteMsg{seat, continue_without}));
}

uint8_t ClientSession::catch_up_percent() const noexcept {
    if (mode_ != Mode::CatchingUp || !catch_known_) return 0;
    if (catch_total_ == 0) return 100;               // (once every turn has run, which CaughtUp needs, it is 100 as well)
    return static_cast<uint8_t>(std::min<uint64_t>(100, uint64_t{runner_->next_turn_to_execute()} * 100u / catch_total_));
}

// The server confirmed (it sent Presence, or the first live turn): the seat is this machine's again, the match goes on
void ClientSession::begin_normal(uint32_t now_ms) {
    mode_ = Mode::Normal;
    caught_up_sent_ = false;
    catch_known_ = false;
    hello_pending_ = false;
    attempts_ = 0;
    last_heard_ms_ = now_ms;
    asked_ = false;
    next_ping_ms_ = now_ms;
}

void ClientSession::poll_new_link(uint32_t now_ms) {
    if (conn_ == nullptr) return;
    if (hello_pending_) {                            // a link that was still being made when it was attached
        std::vector<uint8_t> nothing;
        if (conn_->state() == Connection::State::Connecting) conn_->poll(nothing);
        send_rejoin_hello();
    }
    std::vector<uint8_t> msg;
    int budget = 512;
    bool drained = false;
    while (budget-- > 0 && (mode_ == Mode::Rejoining || mode_ == Mode::CatchingUp)) {
        if (!conn_->poll(msg)) {
            drained = true;
            break;
        }
        last_heard_ms_ = now_ms;
        asked_ = false;                              // whatever the server says, it lives
        handle_stream_message(msg, now_ms);
    }
    if (mode_ != Mode::Rejoining && mode_ != Mode::CatchingUp) return;
    // the link is judged after everything it said was read
    const bool never_opened = hello_pending_ && conn_->state() != Connection::State::Connecting && !conn_->is_open();
    const bool closed = drained && !hello_pending_ && !conn_->is_open();
    const bool no_welcome = mode_ == Mode::Rejoining && now_ms - attempt_ms_ > cfg_.rejoin_timeout_ms;
    const bool silent = mode_ == Mode::CatchingUp && !hello_pending_ && now_ms - last_heard_ms_ > cfg_.host_silence_ms;
    if (never_opened || closed || no_welcome || silent) attempt_failed(now_ms);
}

void ClientSession::handle_stream_message(const std::vector<uint8_t>& msg, uint32_t now_ms) {
    switch (peek_type(msg)) {
        case MsgType::Welcome: {
            WelcomeMsg w;
            if (mode_ != Mode::Rejoining || !decode(msg, w) || (w.flags & kWelcomeRejoin) == 0) return reject(RejectReason::RejoinFailed);
            cfg_.key = w.key;
            mode_ = Mode::CatchingUp;
            catch_known_ = false;
            caught_up_sent_ = false;
            next_ping_ms_ = now_ms;
            return;
        }
        case MsgType::Reject: {
            RejectMsg r;
            if (decode(msg, r)) reject(r.reason);
            return;
        }
        case MsgType::Start:                         // a server that has to tell a machine that has nothing how to load the match tells this one too when it holds no turn yet: its map is
            if (!catch_known_) conn_->send(encode(LoadedMsg{true}));            // loaded (it is the match's own), the server goes on with Begin and CatchUp
            return;
        case MsgType::CatchUp: {
            CatchUpMsg m;
            if (mode_ != Mode::CatchingUp || catch_known_ || !decode(msg, m) || m.first_turn != runner_->next_turn_expected()) return reject(RejectReason::RejoinFailed);
            catch_known_ = true;
            catch_total_ = m.total_turns;
            last_ack_ = runner_->next_turn_to_execute();
            return;
        }
        case MsgType::TurnBatch: {
            TurnBatchMsg m;
            if (mode_ != Mode::CatchingUp || !catch_known_ || caught_up_sent_ || !decode(msg, m) || m.first_turn != runner_->next_turn_expected() ||
                uint64_t{m.first_turn} + m.turns.size() > catch_total_) {      // (the stream ends where CatchUp said it does: a host cannot make this machine queue more)
                return reject(RejectReason::RejoinFailed);
            }
            for (TurnMsg& t : m.turns) {
                if (!runner_->on_catch_up_turn(std::move(t))) return reject(RejectReason::RejoinFailed);
            }
            return;
        }
        case MsgType::Presence: {
            PresenceMsg p;
            if (!decode(msg, p)) return;
            apply_presence(std::move(p), now_ms);
            if (mode_ == Mode::CatchingUp && caught_up_sent_) begin_normal(now_ms);     // the server compared the states and gave the seat back
            return;
        }
        case MsgType::Turn: {                        // the first live turn: the seat is back (what came before it is Presence, which does the same)
            TurnMsg t;
            if (mode_ == Mode::CatchingUp && caught_up_sent_ && decode(msg, t)) {
                begin_normal(now_ms);
                runner_->on_turn(std::move(t));
            }
            return;
        }
        case MsgType::Pong: {
            PingMsg p;
            if (decode_ping(msg.data(), msg.size(), p)) ping_.on_pong(p, now_ms);
            return;
        }
        case MsgType::Desync: {
            DesyncMsg d;
            if (!decode(msg, d)) return;
            desynced_ = true;
            desync_ = d;
            if (mode_ == Mode::CatchingUp) reject(RejectReason::RejoinFailed);      // the server compared this machine's state with its own and they differ: no way back
            return;
        }
        default:
            return;                                  // anything else is of no use to a machine that is coming back
    }
}

// One pass of the catch-up: the turns that have come are executed at once, silently (fast_forward), and the server is told how far this machine is (an acknowledgement that frees the window
// of the stream when the queue is empty, and at every higher percent: the others are shown the percent, and a catch-up that makes no progress is let go after 20 s); when it has run every turn
// of the stream, its state is reported (CaughtUp) and it waits for the server's verdict.
void ClientSession::catch_up_step(uint32_t now_ms) {
    (void)now_ms;
    if (!catch_known_ || caught_up_sent_ || !connected()) return;
    runner_->fast_forward(cfg_.catch_up_ticks);
    const uint32_t done = runner_->next_turn_to_execute();
    const bool all = done >= catch_total_ && runner_->at_boundary() && runner_->queued() == 0;
    if (done > last_ack_) {
        const auto percent_of = [this](uint32_t turns) { return catch_total_ == 0 ? uint64_t{100} : uint64_t{turns} * 100u / catch_total_; };
        if (percent_of(done) > percent_of(last_ack_) || runner_->queued() == 0 || all) {
            AckMsg a;
            a.turn = done - 1;
            conn_->send(encode(a));
            last_ack_ = done;
        }
    }
    if (all) {
        CaughtUpMsg m;
        m.turns = catch_total_;
        m.hash = sim_->state_hash();
        conn_->send(encode(m));
        caught_up_sent_ = true;
    }
}

}  // namespace ants::net
