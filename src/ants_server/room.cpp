#include "ants_server/room.hpp"

#include "ants_net/clock.hpp"
#include "ants_server/secret.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <functional>
#include <limits>

namespace ants::server {

namespace {
constexpr uint32_t kMaxFailedStarts = 5;             // a room whose start is cancelled this many times (a client that cannot load the map, leavers) gives up
}  // namespace

const char* room_state_name(RoomState state) noexcept {
    switch (state) {
        case RoomState::Waiting: return "waiting";
        case RoomState::Loading: return "loading";
        case RoomState::Running: return "running";
        case RoomState::Finished: return "finished";
        case RoomState::Failed: return "failed";
    }
    return "unknown";
}

static net::HostLobby::Config lobby_config(const RoomSpec& spec) {
    net::HostLobby::Config cfg;
    cfg.host_name = "Server";
    cfg.room_code = spec.code;
    cfg.host_seat = net::kNoSeat;                    // the server plays nobody
    const uint8_t players = std::max<uint8_t>(2, std::min<uint8_t>(spec.players, sim::MAX_PLAYERS));
    cfg.min_players = spec.early_start ? uint8_t{2} : players;       // the leader may start with two; without an early start the room waits for every seat
    cfg.max_players = players;
    cfg.early_start = spec.early_start;                              // the lobby names the leader only in a room that allows it
    cfg.load_timeout_ms = spec.load_ms;
    // A room that holds seats gives every player a key (128 random bits of the operating system's generator: ants_net never reads it itself, the library is built for the web too).
    // A room that does not has none: its Welcomes carry the zero key and a Hello with a key is just a Hello.
    if (spec.reconnect) cfg.make_key = [](net::SeatKey& key) { return random_bytes(key.data(), key.size()); };
    return cfg;
}

namespace {

unsigned seats_in(uint8_t mask) noexcept {
    unsigned n = 0;
    for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) n += (mask >> seat) & 1u;
    return n;
}

ai::Level ai_level_of(net::FillLevel fill) noexcept {
    switch (fill) {
        case net::FillLevel::Easy: return ai::Level::Easy;
        case net::FillLevel::Hard: return ai::Level::Hard;
        default: return ai::Level::Medium;
    }
}

}  // namespace

// Where a bot's commands go: into the sequencer of the room's session for the bot's seat, as the commands of a person do from its connection (the verdict arrives with the turn, like every
// command's: a bot never sees it). While the match is paused for an absent player the session refuses them (a bot waits like everybody): the controller counts that as a command sent, and the
// bot finds out in its next look.
class Room::BotSink final : public sim::CommandSink {
public:
    BotSink(Room& room, uint8_t seat) : room_(room), seat_(seat) {}
    sim::CommandResult submit(const sim::Command& command) override {
        sim::CommandResult result;
        if (room_.session_ != nullptr && room_.session_->submit_bot(seat_, command)) result.status = sim::CommandResult::Status::Applied;
        return result;
    }

private:
    Room& room_;
    uint8_t seat_;
};

Room::Room(RoomSpec spec, MapEntry map, assets::LevelData level, uint32_t seed, uint32_t now_ms, net::LogBudget* log_budget)
    : spec_(std::move(spec)), map_(std::move(map)), level_(std::move(level)), seed_(seed), created_ms_(now_ms), lobby_(lobby_config(spec_)), log_budget_(log_budget) {
    spec_.players = std::max<uint8_t>(2, std::min<uint8_t>(spec_.players, sim::MAX_PLAYERS));
    retry_at_ms_ = now_ms;                                       // (not 0: the server's clock is its uptime, and a signed comparison against a stale 0 breaks after 24.8 days)
    lobby_.set_map(map_.name);
    lobby_.set_fog(spec_.fog);
    lobby_.set_before_start([this](const net::StartMsg&, uint32_t now) { record_open(now); });      // the match is fixed (the start message, the keys): its record is made before the Start is sent to anybody
    for (const ai::BotSpec& bot : spec_.bots) {                  // the room's own bots sit down before anybody comes (the lobby refuses what the specification should not have asked: fog, a full room)
        if (lobby_.add_bot(bot.seat, ai::bot_display_name(bot))) bot_specs_.push_back(bot);
    }
    std::sort(bot_specs_.begin(), bot_specs_.end(), [](const ai::BotSpec& a, const ai::BotSpec& b) { return a.seat < b.seat; });
}

Room::~Room() = default;

bool Room::expired(uint32_t now_ms) const noexcept {
    return (state_ == RoomState::Finished || state_ == RoomState::Failed) && now_ms - ended_ms_ >= spec_.keep_ms;
}

void Room::prune_connections() {
    // closed connections that nobody uses any more (a rejected Hello): the lobby and the session hold pointers to the others. The session holds the connection of a player who is coming
    // back (its link can close while its catch-up is being kept, and the session looks at it at its next pass) and the last connection of every seat: asking only the lobby would free
    // an object that the session still points at (a heap-use-after-free that AddressSanitizer shows: test_server S3.42).
    for (auto it = connections_.begin(); it != connections_.end();) {
        net::Connection* c = it->get();
        bool used = session_ != nullptr && session_->uses_connection(c);
        for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) used = used || lobby_.connection_of(seat) == c;
        if (!used && !c->is_open() && c->state() != net::Connection::State::Connecting) it = connections_.erase(it);
        else ++it;
    }
}

bool Room::rejoin(std::unique_ptr<net::Connection>& connection, const net::HelloMsg& hello, uint32_t now_ms) {
    if (connection == nullptr || !can_rejoin()) return false;
    if (connections_.size() >= spec_.max_connections) prune_connections();
    if (connections_.size() >= spec_.max_connections) {             // every connection that the room keeps is in use: it keeps no more (the caller lets this one linger, the answer arrives)
        if (connection->is_open()) {
            connection->send(net::encode(net::RejectMsg{net::RejectReason::Full}));
            connection->close();
        }
        return false;
    }
    if (!session_->accept_rejoin(connection.get(), hello, now_ms)) return false;       // the session has answered and closed it: the caller lets it linger
    connections_.push_back(std::move(connection));                  // the session points at it: the room keeps it alive (and prune_connections asks the session before it frees one)
    return true;
}

bool Room::add_connection(std::unique_ptr<net::Connection>& connection, const std::string& address, const std::vector<uint8_t>& hello, uint32_t now_ms) {
    if (connection == nullptr || state_ != RoomState::Waiting) return false;
    if (connections_.size() >= spec_.max_connections) prune_connections();
    if (connections_.size() >= spec_.max_connections) return false;
    connections_.push_back(std::move(connection));
    lobby_.add_connection(connections_.back().get(), now_ms, address, hello);
    return true;
}

void Room::fail(const std::string& reason, uint32_t now_ms) {
    if (state_ == RoomState::Finished || state_ == RoomState::Failed) return;
    state_ = RoomState::Failed;
    reason_ = reason;
    ended_ms_ = now_ms;
    end_log(now_ms);
    record_discard();                                            // the room is over: nothing to bring back after a restart
    close_connections();
}

// The match is over (or the room failed): what the turn log held and how long the match waited are kept for the status, the log itself is freed (a returning player has nothing to come back to)
void Room::end_log(uint32_t now_ms) {
    if (session_ == nullptr || log_released_) return;
    log_turns_ = session_->log().turns();
    log_bytes_ = static_cast<uint32_t>(std::min<size_t>(session_->log().bytes(), UINT32_MAX));
    log_usable_ = session_->log().usable();
    final_pause_ms_ = session_->attendance().pause_ms(now_ms);
    session_->release_log();
    log_released_ = true;
}

void Room::close(const std::string& reason, uint32_t now_ms) {
    if (state_ == RoomState::Finished) {
        close_connections();
        return;
    }
    fail(reason, now_ms);
}

void Room::close_connections() {
    connections_closed_ = true;
    for (auto& c : connections_) {
        if (c != nullptr && c->is_open()) c->close();
    }
}

// The bots that the leader's START seated are only for that start: a cancelled start (a player left, a map did not load) puts the room back as it was (otherwise a room that is "full" of bots
// would start again by itself, and a seat would be missing for the player who comes next)
void Room::unseat_fill() {
    for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) {
        if ((fill_seats_ & (1u << seat)) == 0) continue;
        lobby_.remove_bot(seat);
        bot_specs_.erase(std::remove_if(bot_specs_.begin(), bot_specs_.end(), [seat](const ai::BotSpec& b) { return b.seat == seat; }), bot_specs_.end());
    }
    fill_seats_ = 0;
}

// The controller of the match: one for the room, over the referee's own engine, called after every tick that the referee runs; each bot seat gets a sink that leads into the session. Nothing
// here is built for a room without a bot.
bool Room::start_bots(uint32_t seed, std::string& why) {
    bool any = false;
    for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) any = any || lobby_.room().slots[seat].state == net::SlotState::Bot;
    if (!any || sim_ == nullptr || session_ == nullptr) return true;
    bot_controller_ = std::make_unique<ai::BotController>(*sim_, seed);
    for (const ai::BotSpec& spec : bot_specs_) {
        if (spec.seat >= sim::MAX_PLAYERS || lobby_.room().slots[spec.seat].state != net::SlotState::Bot) continue;
        bot_sinks_.push_back(std::make_unique<BotSink>(*this, spec.seat));
        std::string error;
        if (!bot_controller_->add(spec, *bot_sinks_.back(), error)) {
            why = "the bot of seat " + std::to_string(static_cast<unsigned>(spec.seat)) + " could not sit down: " + error;
            return false;
        }
    }
    session_->runner().set_on_tick([this]() {
        if (bot_controller_ != nullptr && sim_ != nullptr) bot_controller_->on_tick(*sim_);
    });
    return true;
}

// The session of the match, as a match that starts and a match that is restored both make it: a host without a seat that holds seats when the room says so
void Room::build_session(uint32_t restart_vote_after_ms) {
    net::HostSession::Config hc;
    hc.host_player = net::kNoSeat;
    hc.start_delay_ms = net::kMatchStartDelayMs;                  // protocol 12: the first turn is sealed when the "Get ready to play!" dialog of every machine has had its 5 s (session.hpp)
    // (no waiting for a seat that falls behind: the room keeps its pace, the seat catches up alone, and one that is 60 s behind or has run nothing for 30 s is dropped:
    // the lag policy of a host without a seat, session.hpp)
    // A room that holds seats pauses the match for a player whose connection is lost (and whose key it knows) instead of dropping it: the vote, the cap and the log are the room's settings
    hc.hold_seats = spec_.reconnect;
    hc.attendance.vote_after_ms = spec_.vote_after_ms;
    hc.attendance.restart_vote_after_ms = std::max(restart_vote_after_ms, spec_.vote_after_ms);      // (after a restart: never less than a lost link's time)
    hc.attendance.max_pause_ms = spec_.max_pause_ms;
    hc.attendance.max_catch_up_ms = spec_.max_catch_up_ms;
    hc.attendance.resume_countdown_ms = spec_.resume_countdown_ms;
    hc.max_log_bytes = spec_.max_log_bytes;
    hc.log_budget = log_budget_;
    session_ = std::make_unique<net::HostSession>(*sim_, hc);
}

void Room::begin_match(uint32_t now_ms) {
    const net::StartMsg& start = lobby_.start_info();
    sim_ = std::make_unique<sim::SimulationEngine>();
    sim_->set_fog_of_war_enabled(start.fog);
    sim_->init(level_, start.seed, start.roster);                // exactly what every client does with the same values
    roster_ = start.roster;
    for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) {
        if ((start.roster & (1u << seat)) == 0) continue;
        names_[seat] = start.names[seat];
        sim_->set_player_name(seat, start.names[seat]);
    }
    started_ms_ = now_ms;                                        // (run_ms counts from here: the 5 s before the first turn count, the match's pauses do not)
    build_session(restart_store_ != nullptr ? restart_store_->config().restart_vote_after_ms : net::kRestartVoteAfterMs);
    for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) {
        if (net::Connection* c = lobby_.connection_of(seat)) session_->add_client(seat, c);
        if (lobby_.room().slots[seat].state == net::SlotState::Bot) session_->add_bot_seat(seat);       // a computer player: no connection, acknowledged by the server itself
    }
    if (spec_.reconnect) {
        std::array<net::SeatKey, sim::MAX_PLAYERS> keys{};          // what the lobby gave out: the keys stay valid for the whole match (a seat that is dropped is told so by its key)
        for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) keys[seat] = lobby_.key_of(seat);
        session_->set_seat_keys(keys);
        session_->set_rejoin_start(start);                          // what a machine that starts from nothing is sent first
    }
    record_hook_up();                                               // (before the first turn can be sealed: every turn goes to the restart record before it goes to anybody)
    session_->start(now_ms);
    std::string bot_problem;
    if (!start_bots(start.seed, bot_problem)) {
        fail(bot_problem, now_ms);                                  // (cannot happen: the room refuses fog with bots and the roster holds every bot seat)
        return;
    }
    state_ = RoomState::Running;
}

void Room::finish(const std::string& reason, uint32_t now_ms) {
    if (state_ != RoomState::Running) return;
    reason_ = reason;
    state_ = RoomState::Finished;
    ended_ms_ = now_ms;
    if (session_) session_->freeze();
    end_log(now_ms);
    record_discard();                                            // the match is over: nothing to bring back after a restart
    if (sim_) {
        final_hash_ = sim_->state_hash().total;                       // (once, at the end: the hash is not worth computing at every pass)
        const sim::MatchResult result = sim_->get_world_state().match_result;
        quitter_ = result.quitter;
        for (const sim::ResultRow& r : result.rows(255)) {
            RoomRow row;
            row.first = r.first;
            row.second = r.has_second() ? r.second : uint8_t{255};
            row.name = r.first < 4 ? names_[r.first] : std::string();
            row.second_name = r.has_second() && r.second < 4 ? names_[r.second] : std::string();
            row.score = r.score;
            row.friendly_lost = r.friendly_lost;
            row.enemy_killed = r.enemy_killed;
            row.new_hatched = r.new_hatched;
            row.winner = std::find(result.winning_players.begin(), result.winning_players.end(), r.first) != result.winning_players.end();
            rows_.push_back(std::move(row));
        }
    }
}

void Room::update(uint32_t now_ms) {
    if (state_ == RoomState::Failed) return;
    if (state_ == RoomState::Finished) {
        if (connections_closed_) return;
        // The match is over, but a player that was far behind when it ended (a laggard that catches up at four times the speed, up to a minute of turns) has not run the last turns
        // yet, and a client that is not answered for 10 s gives up on the server: the session goes on (it is frozen: it seals nothing) and answers pings and takes acknowledgements
        // until every player that is still here has acknowledged the last turn, but at least for the grace period and at most for kEndWaitMs.
        if (session_) session_->update(now_ms);
        const uint32_t since = now_ms - ended_ms_;
        bool level = true;
        for (uint8_t seat = 0; seat < sim::MAX_PLAYERS && session_; ++seat) level = level && (!session_->client_present(seat) || session_->behind_ms(seat) == 0);
        if (since >= kEndWaitMs || (since >= kGraceMs && level)) close_connections();
        return;
    }

    lobby_.update(now_ms);
    uint8_t asked_by = net::kNoLeader;                               // the leader who asked to start now, in this pass
    net::FillLevel asked_fill = net::FillLevel::None;                // ... and the bots that it asked for in the empty seats (protocol 11)
    for (const net::HostLobby::Event& ev : lobby_.take_events()) {
        if (ev.type == net::HostLobby::Event::Type::Cancelled || ev.type == net::HostLobby::Event::Type::LoadFailed) {
            if (state_ == RoomState::Loading && ++cancels_ >= kMaxFailedStarts) return fail("the start failed too many times (a player could not load the map or left)", now_ms);
            if (state_ == RoomState::Loading) {
                state_ = RoomState::Waiting;
                retry_at_ms_ = now_ms + kRetryMs;
                unseat_fill();                                       // the room is as it was before the START: the bots that it seated go again
                record_discard();                                    // (and it has no match to bring back)
            }
        } else if (ev.type == net::HostLobby::Event::Type::Begun && state_ == RoomState::Loading) {
            begin_match(now_ms);
        } else if (ev.type == net::HostLobby::Event::Type::LeaderStart) {
            asked_by = ev.seat;
            asked_fill = ev.fill;
        }
    }

    if (state_ == RoomState::Waiting) {
        // The leader's request counts if the room allows it and the leader is still the leader (one who left in this very pass asked for nothing). It starts the match
        // with the seats that are taken now, as the room would with all of them; a request that falls into the pause after a cancelled start is lost (START again).
        const bool full = lobby_.players() >= spec_.players;
        const bool early = spec_.early_start && asked_by != net::kNoLeader && lobby_.leader() == asked_by;
        // The fill (protocol 11): only the leader's request seats bots, in the seats that are still empty up to the players the room expects; a room that is full starts by itself with nobody
        // added. Bots and Fog of War never mix (docs/BOTS.md rule 8): the leader is told, and the match starts without bots if two people are there.
        net::FillLevel fill = early && !full ? asked_fill : net::FillLevel::None;
        if (fill != net::FillLevel::None && lobby_.fog()) {
            lobby_.notify(asked_by, net::kNoticeFillFog);
            fill = net::FillLevel::None;
        }
        std::vector<uint8_t> fill_seats;
        if (fill != net::FillLevel::None) {
            for (uint8_t seat = 0; seat < sim::MAX_PLAYERS && lobby_.players() + fill_seats.size() < spec_.players; ++seat) {
                if (lobby_.room().slots[seat].state == net::SlotState::Empty) fill_seats.push_back(seat);
            }
            if (fill_seats.empty()) fill = net::FillLevel::None;
        }
        const bool can = fill != net::FillLevel::None ? lobby_.can_start_filled() : lobby_.can_start();
        if (can && (full || early)) {
            uint8_t roster = 0;                                      // the seats that play: a map is playable for some rosters and not for others (a start marker outside the grid)
            for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) roster = static_cast<uint8_t>(roster | (lobby_.room().slots[seat].state != net::SlotState::Empty ? 1u << seat : 0u));
            for (const uint8_t seat : fill_seats) roster = static_cast<uint8_t>(roster | (1u << seat));      // (with the bots that the fill would seat)
            const assets::LevelValidation check = level_.validate(roster);
            if (!check.playable) {
                if (full) return fail("the map cannot be played by these seats: " + check.reason(), now_ms);
                // (an early start for seats that the map cannot be played by: nothing happens, the room waits for the others, it does not fail; the leader who asked for bots is told)
                if (fill != net::FillLevel::None && net::time_reached(now_ms, retry_at_ms_)) lobby_.notify(asked_by, net::kNoticeFillMap);
            } else if (net::time_reached(now_ms, retry_at_ms_)) {
                bool seated = true;
                for (const uint8_t seat : fill_seats) {
                    const ai::BotSpec bot{seat, "standard", ai_level_of(fill)};
                    if (!lobby_.add_bot(seat, ai::bot_display_name(bot))) {
                        seated = false;
                        break;
                    }
                    bot_specs_.push_back(bot);
                    fill_seats_ = static_cast<uint8_t>(fill_seats_ | (1u << seat));
                }
                if (seated && lobby_.start(seed_, map_.hash, now_ms)) {
                    state_ = RoomState::Loading;
                    lobby_.host_loaded(true);                        // the server loaded the map when it made the room (the record of the match was made by the lobby's hook before its Start went out)
                } else {
                    unseat_fill();                                   // (the lobby refused: the room is as it was)
                }
            }
        } else if (now_ms - created_ms_ >= spec_.wait_ms) {
            fail("the players did not all come (" + std::to_string(lobby_.players()) + " of " + std::to_string(spec_.players) + ")", now_ms);
            return;
        }
    }

    if (state_ == RoomState::Running && session_) {
        session_->update(now_ms);
        if (record_ != nullptr && restart_store_ != nullptr && net::time_reached(now_ms, next_sync_ms_)) {       // the record is made durable once a second (what write() leaves to the operating system)
            next_sync_ms_ = now_ms + restart_store_->config().sync_every_ms;
            if (record_->dirty() && !record_->sync()) record_stop("the disk refused a flush: " + record_->error());
        }
        last_turns_ = session_->turns_sealed();
        if (sim_) {
            last_ticks_ = static_cast<uint32_t>(sim_->current_tick());
            sim_->clear_news_events();                                   // the screen's queues: nobody on a server reads them, and they would only grow
            sim_->clear_audio_events();
        }
        if (!session_->desyncs().empty()) {
            const net::DesyncMsg& d = session_->desyncs().front();
            return fail("the simulation of seat " + std::to_string(static_cast<int>(d.player)) + " diverged at turn " + std::to_string(d.turn), now_ms);
        }
        if (sim_ && sim_->is_match_over()) {
            finish("the match ended", now_ms);
            return;
        }
        // the limit is on the time that the match ran: a pause (a seat that is away) is not play (a room that waited minutes for a player must not fail "took longer than the limit")
        const uint32_t elapsed_ms = now_ms - started_ms_;
        const uint32_t paused_ms = session_->attendance().held_ms(now_ms);      // (the pauses as they were, and the countdowns after them: the match did not run then either)
        if ((elapsed_ms > paused_ms ? elapsed_ms - paused_ms : 0u) >= spec_.run_ms) return fail("the match took longer than the room's limit", now_ms);
        // somebody is there when a player is connected or a seat is held for a player who may come back: a room whose players all lost their connection waits for them (until the cap)
        bool anybody = false;
        for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) anybody = anybody || session_->client_present(seat) || session_->seat_held(seat);
        if (!anybody) finish("everybody left", now_ms);
    }
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Restart records (restart_record.hpp)
// ---------------------------------------------------------------------------------------------------------------------------------

// What a record's head holds: the room as it is, the match that is being started (the start message without the addresses of the clients: the record is no place for them) and the keys that the lobby
// gave out (secrets: they are in this file and nowhere else)
RestartHead Room::make_head() const {
    RestartHead h;
    if (restart_store_ != nullptr) h.identity = restart_store_->config().identity;
    h.code = spec_.code;
    h.map = map_.name;
    h.map_hash = map_.hash;
    h.players = spec_.players;
    h.fog = spec_.fog;
    h.early_start = spec_.early_start;
    h.wait_ms = spec_.wait_ms;
    h.load_ms = spec_.load_ms;
    h.keep_ms = spec_.keep_ms;
    h.run_ms = spec_.run_ms;
    h.vote_after_ms = spec_.vote_after_ms;
    h.max_pause_ms = spec_.max_pause_ms;
    h.max_catch_up_ms = spec_.max_catch_up_ms;
    h.resume_countdown_ms = spec_.resume_countdown_ms;
    h.max_log_bytes = spec_.max_log_bytes;
    h.max_connections = static_cast<uint32_t>(std::min<size_t>(spec_.max_connections, UINT32_MAX));
    h.bots = bot_specs_;
    h.fill_mask = fill_seats_;
    h.start = lobby_.start_info();
    for (net::Endpoint& e : h.start.endpoints) e = net::Endpoint{};      // (how the other clients reach a client: its address, which the record has no business keeping)
    for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) h.keys[seat] = lobby_.key_of(seat);
    return h;
}

void Room::record_open(uint32_t now_ms) {
    record_.reset();
    record_note_.clear();
    if (restart_store_ == nullptr || !restart_store_->enabled()) {
        record_note_ = "this server keeps no restart records";
        return;
    }
    if (!spec_.reconnect) {
        record_note_ = "the room holds no seats (reconnect is off): a restart ends its match";
        return;
    }
    std::string why;
    record_ = restart_store_->create(make_head(), why);
    if (record_ == nullptr) {
        record_note_ = "no restart record could be made: " + why;
        restart_store_->note("room " + spec_.code + ": " + record_note_ + " (a restart would end this match)");
        return;
    }
    const uint32_t every = std::max<uint32_t>(1, restart_store_->config().sync_every_ms);
    next_sync_ms_ = now_ms + static_cast<uint32_t>(std::hash<std::string>{}(spec_.code) % every);      // (the rooms' syncs are spread over the second, not made all at once)
}

// Every sealed turn goes to the record BEFORE it goes to anybody (HostSession::set_on_seal), and the referee's hash after every 20th turn is its checkpoint. The record ends at once when the
// log of the match is dead (the room can no longer give the match to a player who comes back, so a restart could not hold its seats) or the disk refuses.
void Room::record_hook_up() {
    if (record_ == nullptr || session_ == nullptr) return;
    session_->set_on_seal([this](const net::TurnMsg& turn) {
        if (record_ == nullptr) return;
        if (!session_->log().usable()) return record_stop("the turn log passed its limit or the server's memory for logs, so the match could not be given to a player who comes back: a restart would end it");
        if (!record_->append_turn(turn)) record_stop("the disk refused a write: " + record_->error());
    });
    session_->set_on_referee_hash([this](uint32_t turn, const sim::StateHash& hash) {
        if (record_ == nullptr) return;
        if (!record_->append_check(turn, hash.total)) record_stop("the disk refused a write: " + record_->error());
    });
}

// The record cannot be kept any more: it is deleted (a record that stops in the middle of a match would bring the match back at the wrong tick), the room plays on, and says why
void Room::record_stop(const std::string& note) {
    if (record_ == nullptr) return;
    record_->discard();
    record_.reset();
    record_note_ = note;
    if (restart_store_ != nullptr) restart_store_->note("room " + spec_.code + ": the restart record is gone, a restart would end this match: " + note);
}

// The room is over, or its start was cancelled: there is no match to bring back
void Room::record_discard() {
    if (record_ != nullptr) {
        record_->discard();
        record_.reset();
    }
    record_note_.clear();
}

void Room::flush_record() {
    if (record_ == nullptr) return;
    if (record_->dirty() && !record_->sync()) record_stop("the disk refused a flush: " + record_->error());
}

void Room::release_record() {
    record_.reset();
}

RoomSpec room_spec_of(const RestartHead& h) {
    RoomSpec spec;
    spec.code = h.code;
    spec.map = h.map;
    spec.fog = h.fog;
    spec.players = h.players;
    spec.early_start = h.early_start;
    for (const ai::BotSpec& b : h.bots) {
        if ((h.fill_mask & (1u << b.seat)) == 0) spec.bots.push_back(b);        // (the leader's fill is seated by Room::replay)
    }
    spec.has_seed = true;
    spec.seed = h.start.seed;
    spec.wait_ms = h.wait_ms;
    spec.load_ms = h.load_ms;
    spec.keep_ms = h.keep_ms;
    spec.run_ms = h.run_ms;
    spec.reconnect = true;                                      // (only a room that holds seats has a record)
    spec.vote_after_ms = h.vote_after_ms;
    spec.max_pause_ms = h.max_pause_ms;
    spec.max_log_bytes = static_cast<size_t>(std::min<uint64_t>(h.max_log_bytes, std::numeric_limits<size_t>::max()));
    spec.max_catch_up_ms = h.max_catch_up_ms;
    spec.resume_countdown_ms = h.resume_countdown_ms;
    spec.max_connections = h.max_connections;
    return spec;
}

std::unique_ptr<Room> Room::refused(const RestartHead& head, const std::string& reason, uint32_t turns, uint32_t now_ms) {
    RoomSpec spec = room_spec_of(head);
    spec.bots.clear();                                          // (nobody sits down in a room that is over)
    MapEntry entry;
    entry.name = head.map;
    entry.hash = head.map_hash;
    auto room = std::make_unique<Room>(std::move(spec), std::move(entry), assets::LevelData{}, head.start.seed, now_ms, nullptr);
    room->state_ = RoomState::Failed;
    room->reason_ = reason;
    room->ended_ms_ = now_ms;
    room->connections_closed_ = true;
    room->from_record_ = true;
    room->roster_ = head.start.roster;
    for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) {
        if ((head.start.roster & (1u << seat)) != 0) room->names_[seat] = head.start.names[seat];
    }
    room->last_turns_ = turns;
    room->last_ticks_ = turns;
    return room;
}

Room::ReplayResult Room::replay(const RestartLoaded& rec, uint32_t restart_vote_after_ms, const ReplayLimits& limits, std::string& why) {
    why.clear();
    const RestartHead& h = rec.head;
    if (state_ != RoomState::Waiting || session_ != nullptr || !spec_.reconnect || restart_store_ == nullptr || h.code != spec_.code || !rec.ok()) {
        why = "the room is not one that a record can be restored into";
        return ReplayResult::Refused;
    }
    const auto clock = [&limits]() { return limits.clock ? limits.clock() : restart_steady_ms(); };
    const uint32_t began = clock();
    // the bots: the specification's sat down when the room was made, the leader's fill sits down again now (the lobby refuses what the match could not have had)
    for (const ai::BotSpec& b : h.bots) {
        if ((h.fill_mask & (1u << b.seat)) == 0) continue;
        if (!lobby_.add_bot(b.seat, ai::bot_display_name(b))) {
            why = "a bot of the record cannot sit down again";
            return ReplayResult::Refused;
        }
        bot_specs_.push_back(b);
        fill_seats_ = static_cast<uint8_t>(fill_seats_ | (1u << b.seat));
    }
    std::sort(bot_specs_.begin(), bot_specs_.end(), [](const ai::BotSpec& a, const ai::BotSpec& b) { return a.seat < b.seat; });
    if (bot_specs_.size() != h.bots.size()) {
        why = "the bots of the record do not all sit down again";
        return ReplayResult::Refused;
    }
    const net::StartMsg& start = h.start;
    roster_ = start.roster;
    from_record_ = true;
    sim_ = std::make_unique<sim::SimulationEngine>();
    sim_->set_fog_of_war_enabled(start.fog);
    sim_->init(level_, start.seed, start.roster);                // exactly what begin_match does, and every client
    uint8_t bot_mask = 0;
    for (const ai::BotSpec& b : bot_specs_) bot_mask = static_cast<uint8_t>(bot_mask | (1u << b.seat));
    for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) {
        if ((start.roster & (1u << seat)) == 0) continue;
        names_[seat] = start.names[seat];
        sim_->set_player_name(seat, start.names[seat]);
    }
    build_session(restart_vote_after_ms);
    std::array<net::SeatKey, sim::MAX_PLAYERS> keys{};
    for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) keys[seat] = h.keys[seat];
    session_->set_seat_keys(keys);
    session_->set_rejoin_start(start);                           // what a machine that starts from nothing is sent first (no addresses of clients in it)
    for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) {
        if ((bot_mask & (1u << seat)) != 0) session_->add_bot_seat(seat);
    }
    // The replay: every sealed turn goes into the log (what a player who comes back is given) and into the referee's runner, which runs them 20 at a time; at every checkpoint the referee's state
    // hash must be the one that the old server had at that turn: a replay that is not the match that was played (the rules of this build are not those of the one that wrote the record) stops here.
    // Every 20 turns the caller is asked whether to go on (the server was told to stop, the time of the whole restore is used up), and the room's own cap is looked at.
    net::LockstepRunner& runner = session_->runner();
    size_t next_check = 0;
    ReplayCheck verdict = ReplayCheck::Go;
    if (!for_each_restart_turn(rec, [&](const net::TurnMsg& turn) {          // (one decoded turn at a time: a record read as Streaming holds none)
            if (!session_->restore_turn(turn)) {
                why = "the turn log cannot hold the match (its limit, or the server's memory for logs)";
                return false;
            }
            if ((turn.turn + 1) % net::kHashEveryTurns != 0) return true;
            runner.fast_forward(static_cast<uint32_t>(runner.queued()));
            if (next_check < rec.checks.size() && rec.checks[next_check].turn == turn.turn) {
                if (sim_->state_hash().total != rec.checks[next_check].hash) {
                    why = "the replay does not agree with the state hash that the record holds for turn " + std::to_string(turn.turn) + ": the rules of this build are not those that played the match";
                    return false;
                }
                ++next_check;
            }
            if (limits.check) {
                verdict = limits.check();
                if (verdict != ReplayCheck::Go) return false;
            }
            if (clock() - began > limits.budget_ms) {
                why = "the replay would take longer than the " + std::to_string(limits.budget_ms / 1000) + " s that a restore may";
                return false;
            }
            return true;
        })) {
        if (verdict == ReplayCheck::Stop) return ReplayResult::Stopped;
        if (verdict == ReplayCheck::Defer) return ReplayResult::Deferred;
        if (why.empty()) why = "the record's turns could not be read again";
        return ReplayResult::Refused;
    }
    runner.fast_forward(static_cast<uint32_t>(runner.queued()));
    if (runner.next_turn_to_execute() != rec.turn_count || runner.queued() != 0 || !runner.at_boundary() || next_check != rec.checks.size()) {
        why = "the replay of the record did not run every turn of it";
        return ReplayResult::Refused;
    }
    restored_hash_ = sim_->state_hash().total;
    // who is at the table: every seat of a person is held, except those that the match had dropped already (the engine knows who left: their Drop is in the turns)
    replay_humans_ = static_cast<uint8_t>(start.roster & ~bot_mask);
    replay_dropped_ = 0;
    for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) {
        if ((replay_humans_ & (1u << seat)) != 0 && sim_->is_player_dropped(seat)) replay_dropped_ = static_cast<uint8_t>(replay_dropped_ | (1u << seat));
    }
    replay_path_ = rec.path;
    replay_good_bytes_ = rec.good_bytes;
    replay_turns_ = rec.turn_count;
    restore_ms_ = clock() - began;
    replayed_ = true;
    return ReplayResult::Replayed;
}

bool Room::begin_restored(uint32_t now_ms, std::string& why) {
    if (!replayed_ || state_ != RoomState::Waiting || session_ == nullptr || restart_store_ == nullptr) {
        why = "the room has not been replayed";
        return false;
    }
    replayed_ = false;
    session_->start_restored(now_ms, replay_humans_, replay_dropped_);
    // The match began that long ago, as far as its limit and its age are concerned: its first turn waited for the dialog (protocol 12) and every turn is 50 ms of play. The pause that the restart is
    // does not count (Attendance::held_ms), as every pause does not. All of it from `now_ms`: the replays of the other rooms took their time before.
    const uint32_t ran_ms = net::kMatchStartDelayMs + replay_turns_ * net::kTurnMs;
    started_ms_ = now_ms - ran_ms;
    created_ms_ = started_ms_;
    retry_at_ms_ = now_ms;
    // the record goes on being written (a torn tail is cut off); when it cannot be opened the room is restored all the same, and says that a second restart would end it
    std::string open_why;
    record_ = restart_store_->reopen(replay_path_, replay_good_bytes_, replay_turns_, open_why);
    if (record_ == nullptr) {
        record_note_ = "the restart record could not be opened again: " + open_why;
        restart_store_->remove_file(replay_path_);               // (a record that is not written any more would bring the match back at the wrong tick)
        restart_store_->note("room " + spec_.code + ": " + record_note_ + " (another restart would end this match)");
    } else {
        record_hook_up();
        const uint32_t every = std::max<uint32_t>(1, restart_store_->config().sync_every_ms);
        next_sync_ms_ = now_ms + static_cast<uint32_t>(std::hash<std::string>{}(spec_.code) % every);
    }
    if (!start_bots(seed_, why)) return false;                   // (the bots start again at the restored tick: their tasks are soft, docs/BOTS.md; a bot that was running goes on from what it sees)
    state_ = RoomState::Running;
    restored_ = true;
    restored_at_ms_ = now_ms;
    restored_turns_ = replay_turns_;
    last_turns_ = session_->turns_sealed();
    last_ticks_ = static_cast<uint32_t>(sim_->current_tick());
    if (sim_->is_match_over()) finish("the match had ended when the server stopped", now_ms);
    return true;
}

// What a restart would interrupt here. A room that waits counts its persons only; a match counts while a person is in it (present or catching up: the attendance knows, a bot has no seat in it), and a
// room that a restart brought back also for kRestoredBusyWindowMs, with the seats that are held for the players who are not back yet; after that a room that nobody came back to counts nothing.
RoomBusy Room::busy(uint32_t now_ms) const {
    RoomBusy b;
    if (state_ != RoomState::Waiting && state_ != RoomState::Loading && state_ != RoomState::Running) return b;      // a room that is over holds nobody who plays
    if (state_ != RoomState::Running || session_ == nullptr) {
        b.players = static_cast<uint32_t>(lobby_.humans());
        b.match = state_ != RoomState::Waiting && b.players > 0;
        return b;
    }
    uint32_t present = 0;
    uint32_t held = 0;
    for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) {
        const net::Attendance::State st = session_->attendance().state(seat);
        if (st == net::Attendance::State::Present || st == net::Attendance::State::CatchingUp) ++present;
        else if (st == net::Attendance::State::Absent) ++held;
    }
    const bool just_restored = restored_ && now_ms - restored_at_ms_ < kRestoredBusyWindowMs;
    b.match = present > 0 || just_restored;
    b.players = present + (just_restored ? held : 0u);
    return b;
}

RoomStatus Room::status(uint32_t now_ms) const {
    RoomStatus s;
    s.code = spec_.code;
    s.map = map_.name;
    s.fog = spec_.fog;
    s.expected = spec_.players;
    s.early_start = spec_.early_start;
    s.leader = state_ == RoomState::Waiting || state_ == RoomState::Loading ? lobby_.leader() : uint8_t{255};      // (the lead means something until the match runs)
    s.ignored_start_requests = lobby_.ignored_start_requests() + (session_ ? session_->ignored_start_requests() : 0u);       // (the late ones of a running match are the session's)
    s.state = state_;
    s.reason = reason_;
    s.joined = static_cast<uint8_t>(from_record_ ? seats_in(roster_) : lobby_.players());       // (a room made from a record has no lobby that knows its players: the roster of its match does)
    s.bot_controller = bot_controller_ != nullptr;
    s.bot_start_hold = bot_controller_ != nullptr ? bot_controller_->start_hold() : 0u;
    if (bot_controller_ != nullptr) {
        for (const ai::BotSpec& b : bot_specs_) s.bot_decisions += bot_controller_->stats(b.seat).decisions;
    }
    for (const ai::BotSpec& b : bot_specs_) {
        RoomStatus::Bot row;
        row.seat = b.seat;
        row.kind = b.kind;
        row.level = ai::level_name(b.level);
        row.name = ai::bot_display_name(b);
        row.fill = b.seat < sim::MAX_PLAYERS && (fill_seats_ & (1u << b.seat)) != 0;
        s.bots.push_back(std::move(row));
    }
    const net::RoomMsg& room = lobby_.room();
    for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) {
        s.names[seat] = room.slots[seat].state == net::SlotState::Empty ? std::string() : room.slots[seat].name;
        if (state_ == RoomState::Running || state_ == RoomState::Finished || from_record_) {
            if (!names_[seat].empty()) s.names[seat] = names_[seat];
        }
    }
    s.ticks = last_ticks_;
    s.referee_hash = final_hash_;
    s.turns = last_turns_;
    s.age_ms = now_ms - created_ms_;
    s.quitter = quitter_;
    s.rows = rows_;
    s.reconnect = spec_.reconnect;
    s.vote_after_ms = spec_.vote_after_ms;
    s.max_pause_ms = spec_.max_pause_ms;
    s.max_catch_up_ms = spec_.max_catch_up_ms;
    s.resume_countdown_ms = spec_.resume_countdown_ms;
    s.connections = static_cast<uint32_t>(connections_.size());
    s.record_kept = record_ != nullptr;
    s.record_bytes = record_ != nullptr ? record_->bytes() : 0u;
    s.record_note = record_note_;
    if (record_ == nullptr && record_note_.empty()) {
        s.record_note = state_ == RoomState::Waiting ? "the room has not started: a room is kept from the start of its match on" : "the room is over";
        if (state_ == RoomState::Waiting && restart_store_ == nullptr) s.record_note = "this server keeps no restart records";
    }
    s.restored = restored_;
    s.restored_turns = restored_turns_;
    s.restore_ms = restore_ms_;
    s.restored_hash = restored_hash_;
    if (session_) {
        const net::Attendance& a = session_->attendance();
        const bool running = state_ == RoomState::Running;
        s.rejoins = a.rejoins();
        s.drops_by_vote = a.drops_by_vote();
        s.drops_by_cap = a.drops_by_cap();
        s.rejoins_refused = a.rejoins_refused();
        s.catch_up_expired = a.catch_up_expired();
        s.streamed_bytes = a.streamed_bytes();
        if (log_released_) {                                        // the match is over: what the log held at the end
            s.log_turns = log_turns_;
            s.log_bytes = log_bytes_;
            s.log_usable = log_usable_;
            s.paused_s = final_pause_ms_ / 1000u;
        } else {
            s.log_turns = session_->log().turns();
            s.log_bytes = static_cast<uint32_t>(std::min<size_t>(session_->log().bytes(), UINT32_MAX));
            s.log_usable = session_->log().usable();
            s.paused_s = a.pause_ms(now_ms) / 1000u;
        }
        if (running) {                                              // who is missing now, and the vote (the status is the server's view: no seat is the viewer)
            const net::PresenceMsg p = a.presence_for(255, now_ms);
            s.paused = session_->paused();
            s.resume_s = a.resume_s(now_ms);
            for (const net::PresenceMsg::Entry& e : p.missing) {
                RoomStatus::Absent row;
                row.seat = e.seat;
                row.name = names_[e.seat];
                row.catching_up = e.state == net::PresenceMsg::State::CatchingUp;
                row.away_s = a.away_ms(e.seat, now_ms) / 1000u;
                row.progress = e.progress;
                s.absent.push_back(std::move(row));
            }
            s.vote_seat = p.vote_seat;
            s.votes_continue = p.votes_continue;
            s.voters = p.voters;
        }
    }
    return s;
}

}  // namespace ants::server
