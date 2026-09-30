#include "ants_net/session.hpp"

#include <algorithm>

namespace ants::net {

// ------------------------------------------------------------------------------------------------
// HostSession
// ------------------------------------------------------------------------------------------------

HostSession::HostSession(sim::SimulationEngine& sim, Config config)
    : cfg_(config), sequencer_(config.sequencer), runner_(sim, config.runner) {
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
        if (clients_[p].present) sequencer_.set_active(p, true);
    }
}

void HostSession::submit_local(sim::Command command) {
    if (!started_) return;
    sequencer_.submit(cfg_.host_player, std::move(command));
}

void HostSession::chat_local(const std::string& text, bool team) {
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

void HostSession::drop(uint8_t player) {
    if (player >= sim::MAX_PLAYERS || !clients_[player].present) return;
    clients_[player].present = false;
    if (clients_[player].conn != nullptr && clients_[player].conn->is_open()) clients_[player].conn->close();
    sequencer_.set_active(player, false);
    if (on_left_) on_left_(player);
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
    for (const LockstepRunner::Executed& e : runner_.update(dt_ms)) {
        sequencer_.on_ack(cfg_.host_player, e.turn);
        if (e.has_hash) report_hash(cfg_.host_player, e.turn, e.hash);
    }
}

void HostSession::update(uint32_t now_ms) {
    if (!started_) return;
    const uint32_t dt = now_ms - last_ms_;
    last_ms_ = now_ms;
    poll_clients();
    // seal the turns that are due (a fixed 100 ms schedule; while a peer lags too far the game waits and the schedule slides)
    int sealed = 0;
    while (!frozen_ && now_ms >= next_seal_ms_ && sealed < 5) {
        if (!sequencer_.can_seal()) {
            next_seal_ms_ = now_ms;
            break;
        }
        const TurnMsg turn = sequencer_.seal();
        broadcast(encode(turn));
        runner_.on_turn(turn);
        next_seal_ms_ += kTurnMs;
        ++sealed;
    }
    run_local(dt);
}

// ------------------------------------------------------------------------------------------------
// ClientSession
// ------------------------------------------------------------------------------------------------

ClientSession::ClientSession(sim::SimulationEngine& sim, Config config) : cfg_(config), runner_(sim, config.runner) {}

void ClientSession::start(uint32_t now_ms) {
    started_ = true;
    last_ms_ = now_ms;
    next_ping_ms_ = now_ms;
}

bool ClientSession::submit(sim::Command command) {
    if (!started_ || !connected()) return false;
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
}

bool ClientSession::chat(const std::string& text, bool team) {
    if (!started_ || !connected()) return false;
    ChatMsg m;
    m.sender = cfg_.player;
    m.team = team;
    m.text = text;
    return conn_->send(encode(m));
}

void ClientSession::update(uint32_t now_ms) {
    if (!started_ || conn_ == nullptr) return;
    const uint32_t dt = now_ms - last_ms_;
    last_ms_ = now_ms;
    std::vector<uint8_t> msg;
    int budget = 512;
    while (budget-- > 0 && conn_->poll(msg)) {
        switch (peek_type(msg)) {
            case MsgType::Turn: {
                TurnMsg t;
                if (decode(msg, t)) runner_.on_turn(std::move(t));
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
    for (const LockstepRunner::Executed& e : runner_.update(dt)) {
        if (!connected()) break;
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
    if (connected() && now_ms >= next_ping_ms_) {
        PingMsg p;
        p.nonce = ++ping_nonce_;
        p.sent_ms = now_ms;
        conn_->send(encode_ping(p));
        next_ping_ms_ = now_ms + cfg_.ping_every_ms;
    }
}

}  // namespace ants::net
