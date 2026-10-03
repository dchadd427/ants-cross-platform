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
    : cfg_(config),
      sim_(&sim),
      sequencer_(config.sequencer),
      attendance_(config.attendance),
      log_(config.max_log_bytes, config.log_budget),
      runner_(std::move(runner)),
      delay_(config.host_player) {
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
    if (!started_ || !is_bot_seat(player) || paused()) return false;         // (a bot waits, like everybody, while a seat is away: its orders would only pile up in the first turn after)
    return sequencer_.submit(player, std::move(command));
}

void HostSession::start(uint32_t now_ms) {
    if (started_) return;
    started_ = true;
    last_ms_ = now_ms;
    next_seal_ms_ = now_ms + cfg_.start_delay_ms;                              // turn 0 waits for the start dialog (protocol 12; 0: at once)
    sequencer_.set_active(cfg_.host_player, true);
    for (uint8_t p = 0; p < sim::MAX_PLAYERS; ++p) {
        if (is_bot_seat(p)) sequencer_.set_active(p, true);                    // a bot's commands are accepted and its turns awaited (see run_local)
    }
    uint8_t humans = 0;                                                        // the seats of persons (a bot's seat has no connection and is never away)
    for (uint8_t p = 0; p < sim::MAX_PLAYERS; ++p) {
        if (!clients_[p].present) continue;
        sequencer_.set_active(p, true);
        clients_[p].last_heard_ms = now_ms;
        clients_[p].progress_ms = now_ms;
        clients_[p].acked_seen = sequencer_.acked(p);
        humans = static_cast<uint8_t>(humans | bit(p));
    }
    attendance_.seat_humans(humans, now_ms);
    next_presence_ms_ = now_ms;
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
    relay_chat(m);
    if (on_chat_) on_chat_(m);                       // (the host's own line: it hears itself, as it always has)
}

bool HostSession::hears_chat(uint8_t sender, bool team, uint8_t seat) const noexcept {
    if (!team) return true;                          // a line for all: everybody
    if (seat == sender) return true;                 // the sender's own line comes back to it (the screens log it already and ignore it)
    // the sender's ally, by the host's own table now: the receiver's own filter says the same of every line that reaches it (HUD::receive_chat_message)
    return sender < sim::MAX_PLAYERS && seat < sim::MAX_PLAYERS && sim_ != nullptr && sim_->alliance_of(sender) == seat;
}

void HostSession::relay_chat(const ChatMsg& m) {
    if (!m.team) {
        broadcast(encode(m));
        return;
    }
    const std::vector<uint8_t> bytes = encode(m);
    for (uint8_t p = 0; p < sim::MAX_PLAYERS; ++p) {
        if (!hears_chat(m.sender, true, p) || !clients_[p].present || clients_[p].conn == nullptr) continue;
        if (!clients_[p].conn->send(bytes)) lose(p);       // a send that fails: the connection is gone (as in broadcast)
    }
}

void HostSession::broadcast(const std::vector<uint8_t>& msg, uint8_t except) {
    for (uint8_t p = 0; p < sim::MAX_PLAYERS; ++p) {
        if (p == except || !clients_[p].present || clients_[p].conn == nullptr) continue;
        if (!clients_[p].conn->send(msg)) lose(p);                 // a send that fails: the connection is gone
    }
}

void HostSession::announce_drop(uint8_t player) {
    sim::Command gone;                               // FUN_0100d03b on every machine, in the same turn
    gone.type = sim::CommandType::Drop;
    gone.issuer = player;
    sequencer_.submit_system(std::move(gone));
    if (on_left_) on_left_(player);
}

// The others' notice that this player lags ends with it (the drop or the loss itself is told by the turn stream or the Presence)
void HostSession::end_lag_notice(uint8_t player) {
    if (!clients_[player].lagging) return;
    clients_[player].lagging = false;
    broadcast(encode(LagMsg{player, 0}), player);
}

// The seat is gone for good: it left (Leave), was thrown out (violations, the lag policy) or its connection was lost in a room that cannot hold it. Its Drop travels in the turn stream.
void HostSession::drop(uint8_t player) {
    if (player >= sim::MAX_PLAYERS || !clients_[player].present) return;
    end_lag_notice(player);
    attendance_.dropped(player, last_ms_);           // (Dropped is final: the key still tells its owner so)
    clients_[player].present = false;
    if (clients_[player].conn != nullptr && clients_[player].conn->is_open()) clients_[player].conn->close();
    sequencer_.set_active(player, false);
    announce_drop(player);
    if (paused()) presence_dirty_ = true;            // the voters are fewer
}

// The connection of a seat is LOST (its link closed, a send failed, nothing arrived for silence_ms). A room that holds seats keeps the seat, its key valid, and pauses the match: nothing is
// sealed until the player is back, the others vote, or the cap on the pauses is reached. Everything else (a room that does not hold seats, a seat without a key, a log that is not usable
// any more: the way back would have no turns to give) drops the seat at once, as it was before there was a way back.
void HostSession::lose(uint8_t player) {
    if (player >= sim::MAX_PLAYERS || !clients_[player].present) return;
    if (!cfg_.hold_seats || !log_.usable() || key_is_zero(keys_[player])) return drop(player);
    end_lag_notice(player);
    if (!attendance_.lost(player, last_ms_)) return drop(player);
    clients_[player].present = false;
    if (clients_[player].conn != nullptr && clients_[player].conn->is_open()) clients_[player].conn->close();
    sequencer_.set_active(player, false);            // its acknowledgements no longer matter, and its commands are refused
    presence_dirty_ = true;
}

// A seat that the attendance dropped (a won vote, the cap on the pauses): its Drop goes into the first turn that is sealed after the pause, the same tick on every machine. A player of
// that seat who is coming back right now is told that it is out, and so is one that is connected (a seat that flapped and was voted out while it was back). The key stays: whoever shows it
// later is told "dropped", not "the match has started".
void HostSession::finish_drop(uint8_t seat) {
    for (size_t i = 0; i < rejoiners_.size();) {
        if (rejoiners_[i].seat != seat) {
            ++i;
            continue;
        }
        Connection* c = rejoiners_[i].conn;
        if (c != nullptr && c->is_open()) {
            c->send(encode(RejectMsg{RejectReason::Dropped}));
            c->close();
        }
        rejoiners_.erase(rejoiners_.begin() + static_cast<std::ptrdiff_t>(i));
    }
    if (seat < sim::MAX_PLAYERS && clients_[seat].present) {
        Client& c = clients_[seat];
        end_lag_notice(seat);
        c.present = false;
        if (c.conn != nullptr && c.conn->is_open()) {
            c.conn->send(encode(RejectMsg{RejectReason::Dropped}));
            c.conn->close();
        }
        sequencer_.set_active(seat, false);
    }
    announce_drop(seat);
    presence_dirty_ = true;
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
            if (paused()) return;                    // the match waits for a seat: a click now would be carried out at a moment that nobody expects, and a pause is long (a player who
                                                     // clicks on would reach the limit of commands per turn, which no turn resets): discarded, no violation
            if (!sequencer_.submit(player, std::move(m.command))) violation(player);
            return;
        }
        case MsgType::Vote: {
            VoteMsg m;
            if (!decode(msg, m) || !cfg_.hold_seats) return violation(player);        // (only a room that holds seats has a vote: for any other host it is a message that hosts do not receive)
            // a vote that does not fit (no vote is open, another seat is its subject, the voter is not connected) crossed a change of state on the wire: ignored, no offence
            if (attendance_.vote(player, m.seat, m.continue_without, last_ms_)) presence_dirty_ = true;
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
            // The chat budget (flood.hpp; a remake protection, the original has no limit on chat): a line beyond it is dropped, and a client that keeps on beyond it is flooding
            const ChatBudget::Verdict verdict = clients_[player].chat.take(last_ms_);
            if (verdict == ChatBudget::Verdict::Offence) return violation(player);
            if (verdict == ChatBudget::Verdict::Drop) return;
            relay_chat(m);
            // the host's own screen is a receiver like the others: a host with a seat hears a team line of two others no more than a guest does; a host without a seat (the referee) hears every line (nothing installs a log for it yet)
            if (on_chat_ && (seatless() || hears_chat(m.sender, m.team, cfg_.host_player))) on_chat_(m);
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
        if (c.present && !c.conn->is_open()) lose(p);
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
    const bool waiting_for_a_seat = paused();
    for (uint8_t p = 0; p < sim::MAX_PLAYERS; ++p) {
        Client& c = clients_[p];
        if (!c.present) continue;
        const uint32_t acked = sequencer_.acked(p);
        if (acked != c.acked_seen) {                                 // an ack that moved: it runs turns
            c.acked_seen = acked;
            c.progress_ms = now_ms;
        }
        if (waiting_for_a_seat) {                                    // nothing is sealed while a seat is away: nobody falls further behind, a runner that holds its last turns waits for more
                                                                     // (it runs a turn once the buffer is full again), and the time of a pause is nobody's lag: the clocks start again afterwards
            c.progress_ms = now_ms;
            continue;
        }
        const uint32_t behind = behind_ms(p);
        if (behind == 0) c.progress_ms = now_ms;                     // nothing waits for it, so no idle time runs: the seconds before the first turn of a match (nothing is sealed yet, protocol 12) are nobody's lag
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
    // a peer that says nothing for a minute is gone (its ack and ping stop); in a room that holds seats, for silence_ms: nothing at all for that long is a half-open link (a power cut, a
    // frozen machine), the connection is lost, and the seat is held
    const uint32_t silence = cfg_.hold_seats ? cfg_.silence_ms : cfg_.silence_timeout_ms;
    for (uint8_t p = 0; p < sim::MAX_PLAYERS; ++p) {
        if (clients_[p].present && now_ms - clients_[p].last_heard_ms > silence) lose(p);
    }
    if (cfg_.hold_seats) {
        for (const uint8_t seat : attendance_.update(now_ms)) finish_drop(seat);        // a catch-up that went nowhere is let go, a vote that was won, the cap: the seats that are dropped
        pump_rejoiners(now_ms);
        const bool paused_now = paused();                    // (a pause, and the countdown that follows it)
        const bool voting = attendance_.vote_subject(now_ms) != 255;       // (a seat that flaps is put to the vote while the match runs: the players are told when it opens and when it closes, and at every vote)
        if (presence_dirty_ || paused_now != was_paused_ || voting != was_voting_ || (paused_now && time_reached(now_ms, next_presence_ms_))) {
            send_presence();
            next_presence_ms_ = now_ms + cfg_.presence_every_ms;
            presence_dirty_ = false;
            was_paused_ = paused_now;
            was_voting_ = voting;
        }
        if (paused_now) {                                    // nothing is sealed while a seat is away: the schedule slides, as it does behind a laggard
            next_seal_ms_ = now_ms;
            stall_player_ = 255;
        }
    }
    // seal the turns that are due (a fixed 50 ms schedule). A host with a seat waits while a peer lags too far (the schedule slides); a host without a seat never does. A seat that
    // is lost in the middle of a pass (a send fails) pauses the match at once: the condition is read again for every turn.
    int sealed = 0;
    while (!frozen_ && !paused() && time_reached(now_ms, next_seal_ms_) && sealed < kMaxSealsPerUpdate) {
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
        if (cfg_.hold_seats) log_.append(turn);              // before it is sent: the log is the match (a log that is full stops being usable, and a lost seat is dropped at once from then on)
        broadcast(encode(turn));
        runner_->on_turn(turn);
        next_seal_ms_ += kTurnMs;
        ++sealed;
    }
    if (seatless() && started_ && !frozen_) police_laggards(now_ms);
    run_local(dt);
}

// ---- reconnect: Presence, the door for a returning player, the stream of the log ---------------------------------------------------------------------------

bool HostSession::uses_connection(const Connection* c) const noexcept {
    if (c == nullptr) return false;
    for (const Client& cl : clients_) {
        if (cl.conn == c) return true;                       // (a seat that is held keeps the pointer too: nothing that it points at may be freed)
    }
    for (const Rejoiner& r : rejoiners_) {
        if (r.conn == c) return true;
    }
    return false;
}

// Who is missing, per recipient (the vote that a player has cast is its own). A failed send is not handled here: the next poll sees the closed link (nothing recursive).
void HostSession::send_presence() {
    if (!cfg_.hold_seats) return;
    for (uint8_t p = 0; p < sim::MAX_PLAYERS; ++p) {
        if (!clients_[p].present || clients_[p].conn == nullptr || !clients_[p].conn->is_open()) continue;
        clients_[p].conn->send(encode(attendance_.presence_for(p, last_ms_)));
    }
}

bool HostSession::accept_rejoin(Connection* conn, const HelloMsg& hello, uint32_t now_ms) {
    if (conn == nullptr) return false;
    const auto refuse = [&](RejectReason why) {
        if (conn->is_open()) {
            conn->send(encode(RejectMsg{why}));
            conn->close();
        }
        return false;
    };
    if (!started_ || !cfg_.hold_seats) return refuse(RejectReason::MatchRunning);
    // The key decides the seat. Every seat is compared, in constant time, whatever the first comparison said: how long the answer takes says nothing about how much of a guess was
    // right (a zero key never matches a seat, not even one that has none). A key that fits no seat is answered like a Hello without a key: nothing is revealed.
    uint8_t seat = 255;
    for (uint8_t p = 0; p < sim::MAX_PLAYERS; ++p) {
        const bool fits = key_matches(keys_[p], hello.key);
        seat = fits ? p : seat;
    }
    if (seat == 255) return refuse(RejectReason::MatchRunning);
    const Attendance::State state = attendance_.state(seat);
    if (state == Attendance::State::Dropped) return refuse(RejectReason::Dropped);
    if (state == Attendance::State::Empty) return refuse(RejectReason::MatchRunning);
    if (!log_.usable()) return refuse(RejectReason::RejoinFailed);
    const uint32_t total = log_.turns();
    if (hello.have_turns > total) return refuse(RejectReason::BadRequest);                  // more turns than were ever sealed
    if (hello.have_turns == 0 && !have_rejoin_start_) return refuse(RejectReason::RejoinFailed);     // a machine with nothing cannot be told how to load the match
    // The seat's budgets (attendance.hpp): the catch-up time of this absence, three accepted Hellos a minute, three times the log's size streamed in ten minutes. Past any of them the
    // answer is RejoinFailed, the seat stays as it was (held: the vote and the cap apply), and nothing of an attempt that may be in progress is touched. A Hello that is accepted is the
    // attempt (a second Hello of an attempt that goes on does not give it a new stall clock).
    if (!attendance_.returning(seat, now_ms, log_.bytes_between(hello.have_turns, total), log_.bytes())) return refuse(RejectReason::RejoinFailed);
    // The newer link wins over whatever the seat had: a connection that this host has not found dead yet (a Wi-Fi switch leaves one for up to 10 s) or a second window with the same key
    // (a duplicated tab). The old link is told that it was replaced, so that its window stops trying, and it is closed; an earlier attempt of the same seat that is still catching up too.
    if (clients_[seat].present) {
        Client& old = clients_[seat];
        end_lag_notice(seat);
        if (old.conn != nullptr && old.conn->is_open()) {
            old.conn->send(encode(RejectMsg{RejectReason::Superseded}));
            old.conn->close();
        }
        old.present = false;
        sequencer_.set_active(seat, false);
    }
    for (size_t i = 0; i < rejoiners_.size();) {
        if (rejoiners_[i].seat != seat) {
            ++i;
            continue;
        }
        Connection* earlier = rejoiners_[i].conn;
        if (earlier != nullptr && earlier != conn && earlier->is_open()) {
            earlier->send(encode(RejectMsg{RejectReason::Superseded}));
            earlier->close();
        }
        rejoiners_.erase(rejoiners_.begin() + static_cast<std::ptrdiff_t>(i));
    }
    uint8_t players = 0;
    for (uint8_t p = 0; p < sim::MAX_PLAYERS; ++p) players = static_cast<uint8_t>(players + ((sim_->roster_mask() >> p) & 1u));
    WelcomeMsg w;
    w.player = seat;
    w.players = std::max<uint8_t>(players, 2);               // (the Welcome says 2 .. 4: the seats of the roster)
    w.key = keys_[seat];
    w.flags = kWelcomeRejoin;
    conn->send(encode(w));
    Rejoiner r;
    r.conn = conn;
    r.seat = seat;
    r.total = total;
    if (hello.have_turns == 0) {                             // a machine that starts from nothing loads the match first (Start, Loaded, Begin) and is given the turns after that
        r.stage = Rejoiner::Stage::AwaitLoaded;
        conn->send(encode(rejoin_start_));
    } else {
        r.stage = Rejoiner::Stage::Streaming;
        r.next = hello.have_turns;
        r.acked = hello.have_turns;
        conn->send(encode(CatchUpMsg{hello.have_turns, total}));
    }
    rejoiners_.push_back(std::move(r));
    presence_dirty_ = true;
    return true;
}

// The returning connections, one pass: what they say, the stream of turns paced by what they have executed, and the verdict when one says that it has them all
void HostSession::pump_rejoiners(uint32_t now_ms) {
    for (size_t i = 0; i < rejoiners_.size();) {
        Rejoiner& r = rejoiners_[i];
        enum class Fate : uint8_t { Going, Back, Failed, Left } fate = Fate::Going;
        Connection* const conn = r.conn;
        const uint8_t seat = r.seat;
        if (attendance_.state(seat) != Attendance::State::CatchingUp || !log_.usable()) fate = Fate::Failed;       // the attendance decides (a catch-up that went nowhere is let go, the seat was dropped): over
        std::vector<uint8_t> msg;
        int budget = 128;
        while (budget-- > 0 && fate == Fate::Going && conn->poll(msg)) {
            if (msg.size() > kMaxMessageBytes || !r.talk.take(now_ms, cfg_.message_burst, cfg_.messages_per_second)) {
                if (++r.violations >= cfg_.violation_limit) fate = Fate::Failed;
                continue;
            }
            switch (peek_type(msg)) {
                case MsgType::Loaded: {
                    LoadedMsg m;
                    if (r.stage != Rejoiner::Stage::AwaitLoaded || !decode(msg, m)) {
                        fate = Fate::Failed;
                        break;
                    }
                    if (!m.ok) {                             // the map of that machine is not the match's: there is no way back for it
                        conn->send(encode(RejectMsg{RejectReason::RejoinFailed}));
                        fate = Fate::Failed;
                        break;
                    }
                    conn->send(encode_begin());
                    r.stage = Rejoiner::Stage::Streaming;
                    r.next = 0;
                    r.acked = 0;
                    conn->send(encode(CatchUpMsg{0, r.total}));
                    break;
                }
                case MsgType::TurnAck: {
                    AckMsg m;
                    if (!decode(msg, m)) {
                        if (++r.violations >= cfg_.violation_limit) fate = Fate::Failed;
                        break;
                    }
                    r.acked = std::max(r.acked, std::min(m.turn + 1, r.next));      // (never more than was sent: a client cannot open the window by acknowledging what it was never given)
                    const uint8_t percent = r.total == 0 ? uint8_t{100} : static_cast<uint8_t>((uint64_t{r.acked} * 100u) / r.total);
                    attendance_.progress(seat, percent, now_ms);
                    break;
                }
                case MsgType::CaughtUp: {
                    CaughtUpMsg m;
                    if (r.stage != Rejoiner::Stage::Streaming || !decode(msg, m) || m.turns != r.total) {
                        fate = Fate::Failed;
                        break;
                    }
                    r.claim = m;
                    r.verify = true;
                    break;
                }
                case MsgType::Ping: {
                    PingMsg m;
                    if (decode_ping(msg.data(), msg.size(), m)) conn->send(encode_pong(m));
                    else if (++r.violations >= cfg_.violation_limit) fate = Fate::Failed;
                    break;
                }
                case MsgType::Leave:                         // a player who quits while it is coming back has left for good
                    fate = Fate::Left;
                    break;
                case MsgType::Command:                       // a client that has just said CaughtUp may already believe that it plays: what it says until the host has compared its state
                case MsgType::Hash:                          // (and whatever it sent before it knew that the match is paused) is not for anybody yet and no offence
                case MsgType::Chat:
                case MsgType::Vote:
                    break;
                default:
                    if (++r.violations >= cfg_.violation_limit) fate = Fate::Failed;
                    break;
            }
        }
        if (fate == Fate::Going && !conn->is_open()) fate = Fate::Failed;
        // the stream: batches of turns, as many as the window of bytes (what the player has not executed yet) allows
        while (fate == Fate::Going && r.stage == Rejoiner::Stage::Streaming && r.next < r.total && log_.bytes_between(r.acked, r.next) < cfg_.stream_window_bytes) {
            std::vector<uint8_t> packed;
            const uint32_t n = log_.read(r.next, static_cast<uint32_t>(kMaxBatchTurns), kBatchBytes, packed);
            if (n == 0) break;
            if (!conn->send(encode_turn_batch_packed(r.next, n, packed.data(), packed.size()))) fate = Fate::Failed;
            attendance_.charge_stream(seat, now_ms, packed.size());        // (what a key holder is given is counted: attendance.hpp)
            r.next += n;
        }
        // the player says it has them all: the referee has executed what it sealed or holds the last of it in its queue (the match is paused, so the number cannot grow), and the states are compared
        if (fate == Fate::Going && r.verify) {
            if (!(runner_->at_boundary() && runner_->next_turn_to_execute() == r.total && runner_->queued() == 0)) {
                // A runner that has not started (fewer turns queued than its buffer asks for) or whose buffer was rebuilt after a stall keeps up to `buffer` turns for the next ones, which
                // do not come while the match waits: the referee runs them now, as it would have (fast_forward is the same turns in the same order)
                if (runner_->at_boundary()) runner_->fast_forward(static_cast<uint32_t>(runner_->queued()));
            }
            if (runner_->at_boundary() && runner_->next_turn_to_execute() == r.total && runner_->queued() == 0) {
                const sim::StateHash mine = sim_->state_hash();
                if (mine == r.claim.hash) {
                    fate = Fate::Back;
                } else {                                     // that machine's state is not the match's: it alone is told, the room does not fail, and the seat stays away
                    DesyncMsg d;
                    d.turn = r.total;
                    d.player = seat;
                    d.host = mine;
                    d.peer = r.claim.hash;
                    conn->send(encode(d));
                    fate = Fate::Failed;
                }
            }
        }
        switch (fate) {
            case Fate::Going:
                ++i;
                continue;
            case Fate::Back: {
                Client& c = clients_[seat];
                c.conn = conn;
                c.present = true;
                c.violations = 0;
                c.last_heard_ms = now_ms;
                c.talk = r.talk;
                c.lagging = false;
                c.next_notice_ms = now_ms;
                sequencer_.set_active(seat, true);           // level with the sequencer: it has executed everything that was sealed
                c.acked_seen = sequencer_.acked(seat);
                c.progress_ms = now_ms;
                attendance_.caught_up(seat, now_ms);
                break;
            }
            case Fate::Left:
                rejoiners_.erase(rejoiners_.begin() + static_cast<std::ptrdiff_t>(i));
                if (conn->is_open()) conn->close();
                attendance_.dropped(seat, now_ms);           // (final: its Drop goes into the first turn after the pause)
                announce_drop(seat);
                presence_dirty_ = true;
                continue;
            case Fate::Failed:
                if (conn->is_open()) conn->close();
                attendance_.catch_up_failed(seat, now_ms);   // (a no-op when the attendance gave up on it already)
                break;
        }
        rejoiners_.erase(rejoiners_.begin() + static_cast<std::ptrdiff_t>(i));
        presence_dirty_ = true;
    }
}

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
