#include "ants_net/session.hpp"

#include "ants_net/clock.hpp"

#include <algorithm>

#include "session_detail.hpp"

namespace ants::net {

using session_detail::bit;

namespace {
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

// A turn of the record of a match that is being restored: into the log (what a returning player is given) and into the runner (what the referee executes), in order
bool HostSession::restore_turn(const TurnMsg& turn) {
    if (started_ || !cfg_.hold_seats || turn.turn != restored_turns_) return false;
    if (!log_.append(turn)) return false;                // the log is full or the server's budget is used up (the log is dead now): this match cannot be given to anybody again
    if (!runner_->on_catch_up_turn(turn)) return false;
    ++restored_turns_;
    return true;
}

void HostSession::start_restored(uint32_t now_ms, uint8_t humans, uint8_t dropped) {
    if (started_) return;
    started_ = true;
    last_ms_ = now_ms;
    next_seal_ms_ = now_ms;                              // (the match is paused: the schedule slides until the players are back)
    sequencer_.resume(restored_turns_);                  // the next turn is the one after the record's last; nothing is queued, nobody is active
    sequencer_.set_host_player(cfg_.host_player);
    for (uint8_t p = 0; p < sim::MAX_PLAYERS; ++p) {
        if (is_bot_seat(p)) sequencer_.set_active(p, true);        // a bot's seat is level with the match (the host acknowledges every turn for it)
    }
    attendance_.seat_restored(humans, dropped, now_ms);
    for (uint8_t p = 0; p < sim::MAX_PLAYERS; ++p) {   // a seat without a key can never be taken back: it is gone, as a lost link without a key is (its Drop is sealed after the pause)
        if ((humans & bit(p)) != 0 && (dropped & bit(p)) == 0 && key_is_zero(keys_[p]) && attendance_.dropped(p, now_ms)) announce_drop(p);
    }
    next_presence_ms_ = now_ms;
    presence_dirty_ = true;
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

void HostSession::announce_drop(uint8_t player, bool as_quit) {
    sim::Command gone;                               // FUN_0100d03b on every machine, in the same turn (as_quit: FUN_0101453f, the quit, which ranks the seat last and ends a match of two sides)
    gone.type = as_quit ? sim::CommandType::Quit : sim::CommandType::Drop;
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
void HostSession::drop(uint8_t player, bool as_quit) {
    if (player >= sim::MAX_PLAYERS || !clients_[player].present) return;
    end_lag_notice(player);
    attendance_.dropped(player, last_ms_);           // (Dropped is final: the key still tells its owner so)
    clients_[player].present = false;
    if (clients_[player].conn != nullptr && clients_[player].conn->is_open()) clients_[player].conn->close();
    sequencer_.set_active(player, false);
    announce_drop(player, as_quit);
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
            if (cfg_.start_delay_ms != 0 && sequencer_.next_turn() == 0) return;     // no turn is sealed yet in a match that waits for its start dialog (protocol 12): an honest client sends nothing (its dialog takes every
                                                     // click and key until its first turn has run), and a command now would run at the first tick, ahead of every person's: discarded, no violation
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
        case MsgType::SeatMove: {                    // the leader's press on a player's row crossed the Start on the wire (protocol 14): the colors are fixed now
            SeatMoveMsg m;
            if (!decode(msg, m) || !seatless()) return violation(player);      // (a host that holds a seat has no leader: nobody sends it one)
            if (++clients_[player].ignored_seat_moves > kIgnoredSeatMovesAllowed) violation(player);       // the same ladder as the StartRequest's: free at first, a violation each beyond it
            return;
        }
        case MsgType::Chat: {
            ChatMsg m;
            if (!decode(msg, m)) return violation(player);
            m.sender = player;                       // the connection speaks, not the payload
            // The chat budget (flood.hpp; a remake protection, the original has no limit on chat): a line beyond it is dropped, and a client that keeps on beyond it is flooding
            const bool guest = player < sim::MAX_PLAYERS && ((cfg_.guest_seats >> player) & 1u) != 0;
            const ChatBudget::Verdict verdict = guest ? clients_[player].chat.take(last_ms_, kGuestChatRate) : clients_[player].chat.take(last_ms_);
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
        if (seatless() && on_referee_hash_) on_referee_hash_(e.turn, e.hash);         // (the restart record's checkpoint)
        if (seatless()) sequencer_.on_referee_hash(e.turn, e.hash);                   // the referee: its own engine's hash is what every client is compared with (judge_hashes)
        else report_hash(cfg_.host_player, e.turn, e.hash);
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
        if (on_seal_) on_seal_(turn);                        // the restart record, also before it is sent: no client may run a turn that the record lacks (set_on_seal)
        broadcast(encode(turn));
        runner_->on_turn(turn);
        next_seal_ms_ += kTurnMs;
        ++sealed;
    }
    if (seatless() && started_ && !frozen_) police_laggards(now_ms);
    run_local(dt);
    if (seatless()) judge_hashes();
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
    // The seat's budgets (attendance.hpp): the catch-up time of this absence, twelve accepted Hellos a minute, three times the log's size streamed in ten minutes. Past any of them the
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
                    conn->send(encode_begin(0));                  // (protocol 21: a machine that is given a match that runs has no countdown: its Begin tells 0)
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

}  // namespace ants::net
