// The match of a NetGame: the hooks of the runner, the prediction of one's own orders, the host's controls of the room, START and what the screens ask while the match plays.
// The room, the links and the way back are in netgame.cpp.

#include "ants_net/netgame.hpp"

#include <utility>

#include "ants_sim/game_strings.hpp"

namespace ants::net {

namespace {

namespace str = ants::sim::strings;

}  // namespace

LockstepRunner* NetGame::runner() const {
    if (host_session_) return &host_session_->runner();
    if (client_session_) return &client_session_->runner();
    return nullptr;
}

void NetGame::install_hooks() {
    LockstepRunner* r = runner();
    if (r == nullptr) return;
    r->set_on_tick([this]() {
        if (prediction_) prediction_->on_tick();                // (the predicted engine runs its tick first: the application's tick hook reads the engine that is shown)
        if (on_tick_) on_tick_();
    });
    r->set_on_turn([this](const TurnMsg& turn) {
        if (prediction_) prediction_->on_turn(turn);
    });
    r->set_on_command([this](const sim::Command& c, const sim::CommandResult& res) {
        if (c.type == sim::CommandType::Drop) events_.push_back(Event{Event::Type::PlayerLeft, c.issuer});
        if (on_command_) on_command_(c, res);
    });
    r->set_on_executed([this](const TurnMsg& turn) {
        if (on_executed_) on_executed_(turn);
    });
    auto chat = [this](const ChatMsg& m) {
        if (on_chat_) on_chat_(m);
    };
    if (host_session_) host_session_->set_on_chat(chat);
    if (client_session_) {
        client_session_->set_on_chat(chat);
        client_session_->set_on_p2p([this](const std::vector<uint8_t>& message) { on_p2p_message(message); });
        client_session_->set_on_order_ack([this](const OrderAckMsg& ack) {
            if (prediction_) prediction_->on_order_ack(ack.seq, ack.open_turn);       // (protocol 30: the lead learns the lag of the player's orders from the host's answers)
        });
    }
}

// ---- hints and channels (protocol 26) -----------------------------------------------------------------------------------------------------------------

PeerLink NetGame::peer_link(uint8_t seat) const { return p2p_ == nullptr || seat >= sim::MAX_PLAYERS || seat == seat_ ? PeerLink::None : p2p_->link(seat); }
int32_t NetGame::peer_rtt_ms(uint8_t seat) const noexcept {
    if (p2p_ == nullptr || seat >= sim::MAX_PLAYERS || seat == seat_) return -1;
    const int32_t ms = p2p_->rtt_ms(seat);
    return ms < 0 ? -1 : ms;
}

// What the server sends that belongs to the channels: a computer player's order ahead of its turn (a hint, straight to the prediction), and the relay's credential, who is ready and the signalling of
// the other players (to the page's transport, which does not exist in most builds: then they are not even looked at)
void NetGame::on_p2p_message(const std::vector<uint8_t>& message) {
    switch (peek_type(message)) {
        case MsgType::Hint: {
            HintMsg h;
            if (!hints_enabled_ || !decode(message, h) || h.command.issuer == seat_) return;
            ++hint_counts_.from_server;
            if (prediction_) prediction_->hint(h.command, h.seq, h.tick);
            return;
        }
        case MsgType::IceConfig: {
            IceConfigMsg m;
            if (p2p_ != nullptr && decode(message, m) && !m.urls.empty()) p2p_->open(seat_, p2p_direct_, m);
            return;
        }
        case MsgType::P2pPeers: {
            P2pPeersMsg m;
            if (p2p_ != nullptr && decode(message, m)) p2p_->peers(m.ready, m.direct);
            return;
        }
        case MsgType::Signal: {
            SignalMsg m;
            if (p2p_ != nullptr && decode(message, m)) p2p_->signal(m.peer, m.kind, m.text);
            return;
        }
        default:
            return;
    }
}

// Once per update while the match plays: the P2pConfig that tells the server this machine can open channels (until it was taken), the page's signalling to the server, and the hints that the channels
// delivered, to the prediction. A hint whose message is not a hint, or whose issuer is not the seat of the channel it came on, is refused (the channel says who the sender is, never the payload).
void NetGame::pump_p2p() {
    if (p2p_ == nullptr || !client_session_) return;
    if (!p2p_config_sent_ && client_session_->send_p2p(encode(P2pConfigMsg{p2p_direct_}))) p2p_config_sent_ = true;
    uint8_t peer = 0;
    SignalKind kind = SignalKind::Offer;
    std::string text;
    for (int budget = 64; budget > 0 && p2p_->poll_signal(peer, kind, text); --budget) client_session_->send_p2p(encode(SignalMsg{peer, kind, text}));
    std::vector<uint8_t> raw;
    for (int budget = 128; budget > 0 && p2p_->poll_hint(peer, raw); --budget) {
        HintMsg h;
        if (!decode(raw, h) || peer >= sim::MAX_PLAYERS || h.command.issuer != peer || peer == seat_) {
            ++hint_counts_.refused;
            continue;
        }
        if (!hints_enabled_) continue;
        ++hint_counts_.from_peers;
        if (prediction_) prediction_->hint(h.command, h.seq, h.tick);
    }
}

void NetGame::set_on_tick(std::function<void()> fn) { on_tick_ = std::move(fn); }

void NetGame::set_on_command(std::function<void(const sim::Command&, const sim::CommandResult&)> fn) { on_command_ = std::move(fn); }

void NetGame::set_on_executed(std::function<void(const TurnMsg&)> fn) { on_executed_ = std::move(fn); }

// ---- the prediction of one's own orders -----------------------------------------------------------------------------------------------------------

void NetGame::make_prediction() {
    LockstepRunner* r = runner();
    if (r == nullptr || seat_ >= sim::MAX_PLAYERS) return;
    Prediction::Config pc;
    pc.seat = seat_;
    pc.budget_ns = prediction_budget_ns_;
    pc.budget_strikes = prediction_budget_strikes_;
    pc.cooldown_ticks = prediction_cooldown_ticks_;
    pc.work_hook = prediction_work_hook_;
    pc.wall_clock = prediction_wall_clock_;
    pc.cpu_clock = prediction_cpu_clock_;
    pc.first_seq = next_order_seq_;                             // (the numbers go on where the last prediction of this match stopped: an answer on its way is never taken for a new order's)
    prediction_ = std::make_unique<Prediction>(sim_, *r, pc);
}

// What an order of this player takes from the confirmed tick that it is given at to the first turn that is still open at the host, in ms: the way to the host, the wait for the seal, the way back and the jitter
// buffer, from the round trip and the buffer. It is only what the prediction takes until the host's answers to its orders have taught it the lag (Prediction::Config::learn_lead): the measured delay of the commands
// is not used (it counts up to the tick that RUNS an order, and that is the tick that the order is stamped with, so it would only measure the lead itself).
uint32_t NetGame::expected_command_delay_ms() const {
    const LockstepRunner* r = runner();
    const uint32_t buffer = r != nullptr ? r->buffer_turns() : 1u;
    return ping_ms().value_or(0u) + kTurnMs / 2u + buffer * kTurnMs;
}

void NetGame::drop_prediction() {
    if (!prediction_) return;
    next_order_seq_ = prediction_->next_seq();
    prediction_.reset();
    if (on_prediction_dropped_) on_prediction_dropped_();
}

// Once per update: there is a prediction only where it is wanted (it is made here, when the match plays and the game was not told --no-prediction, and destroyed when it is switched off: a match
// that is told not to predict never has one), and it is on only while the match simply follows the live stream. Every state in which the confirmed engine does not (a pause, a catch-up, a rejoin, a host
// change, a desync) and the application's switch turn it off; it begins again with the first tick after they are gone.
// A pause is the runner's `held` (the sessions hold it for as long as the match is paused: a seat that is away, the countdown that follows), and a host change, a rejoin and the like are a
// client session that is not in its Normal mode: the sessions' own `paused()` and `electing()` say the same thing and are not asked again.
void NetGame::refresh_prediction() {
    if (!prediction_enabled_) {
        drop_prediction();
        return;
    }
    if (!prediction_ && phase_ == Phase::Playing) make_prediction();
    if (!prediction_) return;
    bool off = app_suspends_prediction_ || phase_ != Phase::Playing || desynced();
    if (client_session_) off = off || client_session_->mode() != ClientSession::Mode::Normal || client_session_->catching_up();
    if (const LockstepRunner* r = runner()) off = off || r->held();
    prediction_->set_suspended(off);
    if (!off) prediction_->set_expected_delay_ms(expected_command_delay_ms());
}

uint64_t& NetGame::default_prediction_budget_ns() noexcept {
    static uint64_t budget = Prediction::Config{}.budget_ns;
    return budget;
}

sim::SimulationEngine& NetGame::view_engine() {
    return predicting() ? prediction_->engine() : sim_;
}

// ---- the host's controls -------------------------------------------------------------------------------------------------------------------------

void NetGame::set_map(const std::string& map_name) {
    if (role_ != Role::Host || phase_ != Phase::Room || !host_lobby_) return;
    host_lobby_->set_map(map_name);
    room_ = host_lobby_->room();
    room_.you = seat_;
}

void NetGame::set_fog(bool fog) {
    if (role_ != Role::Host || phase_ != Phase::Room || !host_lobby_) return;
    if (fog && host_lobby_->has_bot()) {
        set_notice("Bots cannot play with Fog of War.");
        return;
    }
    host_lobby_->set_fog(fog);
    room_ = host_lobby_->room();
    room_.you = seat_;
}

void NetGame::set_mode(uint8_t mode) {
    if (role_ != Role::Host || phase_ != Phase::Room || !host_lobby_) return;
    host_lobby_->set_mode(mode);
    room_ = host_lobby_->room();
    room_.you = seat_;
}

bool NetGame::add_bot(uint8_t seat, const std::string& name) {
    if (role_ != Role::Host || phase_ != Phase::Room || !host_lobby_) return false;
    if (host_lobby_->fog()) {
        set_notice("Bots cannot play with Fog of War.");
        return false;
    }
    if (!host_lobby_->add_bot(seat, name)) return false;
    room_ = host_lobby_->room();
    room_.you = seat_;
    return true;
}

void NetGame::remove_bot(uint8_t seat) {
    if (role_ != Role::Host || phase_ != Phase::Room || !host_lobby_) return;
    host_lobby_->remove_bot(seat);
    room_ = host_lobby_->room();
    room_.you = seat_;
}

size_t NetGame::fill_bots(const FillPlan& plan, std::vector<uint8_t>* seats) {
    if (!plan.any() || role_ != Role::Host || phase_ != Phase::Room || !host_lobby_) return 0;
    if (host_lobby_->fog()) {
        set_notice(kNoticeFillFog);
        refresh_status();                                            // (the status line says why at once, not with the next update)
        return 0;
    }
    size_t seated = 0;
    for (const auto& bot : plan_fill_seats(plan, host_lobby_->room(), sim::MAX_PLAYERS)) {         // (a room on the local network has four seats)
        if (!host_lobby_->add_bot(bot.first, fill_bot_name(bot.second))) break;
        if (seats != nullptr) seats->push_back(bot.first);
        ++seated;
    }
    room_ = host_lobby_->room();
    room_.you = seat_;
    return seated;
}

bool NetGame::submit_bot(uint8_t seat, const sim::Command& command) {
    if (phase_ != Phase::Playing || !host_session_ || !sim::is_client_command(command.type)) return false;
    return host_session_->submit_bot(seat, command);
}

uint64_t NetGame::bot_apply_tick(uint64_t fallback) const noexcept {
    if (phase_ != Phase::Playing || !host_session_) return fallback;
    return host_session_->turns_sealed();
}

// START needs a second player and "all players' thumbs have appeared": every guest has been measured at least once
bool NetGame::can_start() const {
    return role_ == Role::Host && phase_ == Phase::Room && host_lobby_ && host_lobby_->can_start() && host_lobby_->all_measured();
}

bool NetGame::start_match(uint32_t seed, uint64_t map_hash) {
    if (!can_start()) return false;
    // The teams (protocol 13): this machine's choice, when the seats that play can make them; else the match starts without, and everybody is told why once it has started
    uint8_t roster = 0;
    for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) roster = static_cast<uint8_t>(roster | (host_lobby_->occupied(seat) ? 1u << seat : 0u));
    sim::StartTeams teams;
    std::string no_teams;
    if (teams_.set) {
        const sim::StartTeamsPlan plan = sim::plan_start_teams(teams_, roster);
        if (plan.why.empty()) teams = teams_;
        else no_teams = std::string(kNoticeNoTeams) + plan.short_why;
    }
    if (!host_lobby_->start(seed, map_hash, now_, teams)) return false;
    phase_ = Phase::Loading;
    phase_since_ms_ = now_;
    loaded_reported_ = false;
    start_ = host_lobby_->start_info();
    events_.push_back(Event{Event::Type::StartRequested, 255});
    if (!no_teams.empty()) {
        for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) {
            if (host_lobby_->room().slots[seat].state == SlotState::Client) host_lobby_->notify(seat, no_teams);
        }
        set_notice(no_teams);
    }
    refresh_status();
    return true;
}

void NetGame::report_loaded(bool ok, const std::string& why) {
    if (phase_ != Phase::Loading) return;
    loaded_reported_ = ok;
    if (role_ == Role::Host && host_lobby_) {
        host_lobby_->host_loaded(ok);
    } else if (role_ == Role::Client && client_lobby_) {
        client_lobby_->report_loaded(ok);
        if (!ok) {                                                // a guest that cannot load is back in the room at once (the host cancels for everybody)
            phase_ = Phase::Room;
            phase_since_ms_ = now_;
            close_peer_links();
            set_notice(why.empty() ? str::format(str::kMapFileMissing, start_.map_name) : why);
            events_.push_back(Event{Event::Type::Cancelled, seat_});
            refresh_status();
            unannounce_start_key();                               // the lobby takes no Cancel of the server's after this: the key that the Start gave goes now (last: the function that is told may end the session)
            return;
        }
    }
    refresh_status();                                             // the status line follows at once, not with the next update
}

// ---- the match -----------------------------------------------------------------------------------------------------------------------------------

sim::CommandResult NetGame::submit(const sim::Command& command) {
    sim::CommandResult result;
    if (phase_ != Phase::Playing || !sim::is_client_command(command.type)) return result;      // status Ignored
    sim::Command c = command;
    c.issuer = seat_;
    // Protocol 30: an order that the prediction will apply goes to the host with the tick that it is applied at, and the host seals it into exactly that turn when it is still open. The tick is read before the order is
    // sent (it is the state that the screen stands at now), and the order is applied to the predicted engine after it, in this call.
    const std::optional<Prediction::OrderPlan> plan = prediction_ ? prediction_->plan_order(c) : std::nullopt;
    const uint32_t at_turn = plan ? static_cast<uint32_t>(std::min<uint64_t>(plan->tick, kMaxHintTick)) : 0u;
    const uint32_t seq = plan ? plan->seq : 0u;
    uint32_t open_turn = 0;
    if (host_session_) {
        if (!host_session_->submit_local(c, at_turn, &open_turn)) return sim::CommandResult{};  // refused at once (the sequencer's flood limits): Ignored, and nothing is predicted
    } else if (!client_session_ || !client_session_->submit(c, at_turn, seq)) {
        return sim::CommandResult{};                                                            // no host to send it to (a new one is being chosen): Ignored
    }
    // The order is on its way. With the prediction on, the predicted engine applies it NOW and says what the engine says (the ant that answers, the ants that needed the order); without it,
    // the immediate feedback of the click is predict_order_ack's guess, as it was before the prediction existed.
    if (prediction_ && prediction_->submit(c, result)) {
        if (host_session_ && plan) prediction_->on_order_ack(prediction_->last_order_seq(), open_turn);   // (this machine is the host: its answer is the sequencer's own, at once)
        if (p2p_ != nullptr && p2p_config_sent_) {                                              // the other players learn the order from this machine ahead of its turn (the server's turn is what counts)
            p2p_->send_hint(encode(HintMsg{++hint_seq_, static_cast<uint32_t>(std::min<uint64_t>(prediction_->last_order_tick(), kMaxHintTick)), c}));
            ++hint_counts_.sent;
        }
        return result;
    }
    result.ack_ant = sim_.predict_order_ack(c, &result.needing_order);
    result.status = sim::CommandResult::Status::Applied;                                        // optimistic: the turn decides
    return result;
}

void NetGame::show_notice(std::string text) {
    if (text.size() > kStatusNoticeChars) text = text.substr(0, kStatusNoticeChars - 3) + "...";               // (see collect_room_chat)
    set_notice(std::move(text));
}

bool NetGame::chat(const std::string& text, bool team) {
    if (text.empty()) return false;
    if (phase_ == Phase::Room || phase_ == Phase::Loading) {             // the waiting room (protocol 11): nobody has a team yet, everybody hears it
        if (host_lobby_) return host_lobby_->chat(text);
        return client_lobby_ && client_lobby_->chat(text);
    }
    if (phase_ != Phase::Playing) return false;
    const std::string body = text.size() > kMaxChatChars ? text.substr(0, kMaxChatChars) : text;
    if (host_session_) {
        host_session_->chat_local(body, team);
        return true;
    }
    return client_session_ && client_session_->chat(body, team);
}

void NetGame::freeze() {
    if (host_session_) host_session_->freeze();
    if (client_session_) client_session_->finish();              // the match is over: a host that leaves now is no loss
    forget_key();                                                // ... and the room will finish: no key opens it any more
}

bool NetGame::stalled() const {
    const LockstepRunner* r = runner();
    return phase_ == Phase::Playing && r != nullptr && r->stalled();
}

uint32_t NetGame::stalled_ms() const {
    const LockstepRunner* r = runner();
    return phase_ == Phase::Playing && r != nullptr ? r->stalled_ms() : 0u;
}

uint8_t NetGame::laggard() const { return host_session_ ? host_session_->laggard() : uint8_t{255}; }

std::optional<NetGame::LagNotice> NetGame::lag_notice() const {
    if (phase_ != Phase::Playing || !client_session_) return std::nullopt;
    const uint8_t seat = client_session_->lagging_seat();
    if (seat >= sim::MAX_PLAYERS) return std::nullopt;
    return LagNotice{seat, client_session_->lagging_behind_ms()};
}

bool NetGame::catching_up() const { return phase_ == Phase::Playing && client_session_ && client_session_->catching_up(); }

std::optional<uint32_t> NetGame::self_lag_behind_ms() const {
    if (phase_ != Phase::Playing || !client_session_) return std::nullopt;
    const uint32_t ms = client_session_->self_lag_behind_ms();
    if (ms == 0) return std::nullopt;
    return ms;
}

bool NetGame::desynced() const {
    if (host_session_) return !host_session_->desyncs().empty();
    if (client_session_) return client_session_->desynced();
    return false;
}

uint32_t NetGame::sub_tick_ms() const {
    const LockstepRunner* r = runner();
    return r != nullptr ? r->sub_tick_ms() : 0u;
}


bool NetGame::electing() const { return client_session_ && client_session_->electing(); }

std::string NetGame::match_notice() const { return phase_ == Phase::Playing && now_ < notice_until_ms_ ? notice_ : std::string(); }

uint32_t NetGame::turns_executed() const {
    const LockstepRunner* r = runner();
    return r != nullptr ? r->next_turn_to_execute() : 0u;
}

}  // namespace ants::net
