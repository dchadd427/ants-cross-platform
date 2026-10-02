#include "ants_server/room.hpp"

#include "ants_net/clock.hpp"

#include <algorithm>

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
    return cfg;
}

Room::Room(RoomSpec spec, MapEntry map, assets::LevelData level, uint32_t seed, uint32_t now_ms)
    : spec_(std::move(spec)), map_(std::move(map)), level_(std::move(level)), seed_(seed), created_ms_(now_ms), lobby_(lobby_config(spec_)) {
    spec_.players = std::max<uint8_t>(2, std::min<uint8_t>(spec_.players, sim::MAX_PLAYERS));
    retry_at_ms_ = now_ms;                                       // (not 0: the server's clock is its uptime, and a signed comparison against a stale 0 breaks after 24.8 days)
    lobby_.set_map(map_.name);
    lobby_.set_fog(spec_.fog);
}

bool Room::expired(uint32_t now_ms) const noexcept {
    return (state_ == RoomState::Finished || state_ == RoomState::Failed) && now_ms - ended_ms_ >= spec_.keep_ms;
}

void Room::prune_connections() {
    // closed connections that nobody uses any more (a rejected Hello): the lobby and the session hold pointers to the others
    for (auto it = connections_.begin(); it != connections_.end();) {
        net::Connection* c = it->get();
        bool used = false;
        for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) used = used || lobby_.connection_of(seat) == c;
        if (!used && !c->is_open() && c->state() != net::Connection::State::Connecting) it = connections_.erase(it);
        else ++it;
    }
}

bool Room::add_connection(std::unique_ptr<net::Connection>& connection, const std::string& address, const std::vector<uint8_t>& hello, uint32_t now_ms) {
    if (connection == nullptr || state_ != RoomState::Waiting) return false;
    if (connections_.size() >= kMaxConnections) prune_connections();
    if (connections_.size() >= kMaxConnections) return false;
    connections_.push_back(std::move(connection));
    lobby_.add_connection(connections_.back().get(), now_ms, address, hello);
    return true;
}

void Room::fail(const std::string& reason, uint32_t now_ms) {
    if (state_ == RoomState::Finished || state_ == RoomState::Failed) return;
    state_ = RoomState::Failed;
    reason_ = reason;
    ended_ms_ = now_ms;
    close_connections();
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

void Room::begin_match(uint32_t now_ms) {
    const net::StartMsg& start = lobby_.start_info();
    sim_ = std::make_unique<sim::SimulationEngine>();
    sim_->set_fog_of_war_enabled(start.fog);
    sim_->init(level_, start.seed, start.roster);                // exactly what every client does with the same values
    for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) {
        if ((start.roster & (1u << seat)) == 0) continue;
        names_[seat] = start.names[seat];
        sim_->set_player_name(seat, start.names[seat]);
    }
    started_ms_ = now_ms;
    net::HostSession::Config hc;
    hc.host_player = net::kNoSeat;
    // (no waiting for a seat that falls behind: the room keeps its pace, the seat catches up alone, and one that is 60 s behind or has run nothing for 30 s is dropped:
    // the lag policy of a host without a seat, session.hpp)
    session_ = std::make_unique<net::HostSession>(*sim_, hc);
    for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) {
        if (net::Connection* c = lobby_.connection_of(seat)) session_->add_client(seat, c);
    }
    session_->start(now_ms);
    state_ = RoomState::Running;
}

void Room::finish(const std::string& reason, uint32_t now_ms) {
    if (state_ != RoomState::Running) return;
    reason_ = reason;
    state_ = RoomState::Finished;
    ended_ms_ = now_ms;
    if (session_) session_->freeze();
    if (sim_) {
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
    for (const net::HostLobby::Event& ev : lobby_.take_events()) {
        if (ev.type == net::HostLobby::Event::Type::Cancelled || ev.type == net::HostLobby::Event::Type::LoadFailed) {
            if (state_ == RoomState::Loading && ++cancels_ >= kMaxFailedStarts) return fail("the start failed too many times (a player could not load the map or left)", now_ms);
            if (state_ == RoomState::Loading) {
                state_ = RoomState::Waiting;
                retry_at_ms_ = now_ms + kRetryMs;
            }
        } else if (ev.type == net::HostLobby::Event::Type::Begun && state_ == RoomState::Loading) {
            begin_match(now_ms);
        } else if (ev.type == net::HostLobby::Event::Type::LeaderStart) {
            asked_by = ev.seat;
        }
    }

    if (state_ == RoomState::Waiting) {
        // The leader's request counts if the room allows it and the leader is still the leader (one who left in this very pass asked for nothing). It starts the match
        // with the seats that are taken now, as the room would with all of them; a request that falls into the pause after a cancelled start is lost (START again).
        const bool full = lobby_.players() >= spec_.players;
        const bool early = spec_.early_start && asked_by != net::kNoLeader && lobby_.leader() == asked_by;
        if (lobby_.can_start() && (full || early)) {
            uint8_t roster = 0;                                      // the seats that play: a map is playable for some rosters and not for others (a start marker outside the grid)
            for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) roster = static_cast<uint8_t>(roster | (lobby_.room().slots[seat].state != net::SlotState::Empty ? 1u << seat : 0u));
            const assets::LevelValidation check = level_.validate(roster);
            if (!check.playable) {
                if (full) return fail("the map cannot be played by these seats: " + check.reason(), now_ms);
                // (an early start for seats that the map cannot be played by: nothing happens, the room waits for the others, it does not fail)
            } else if (net::time_reached(now_ms, retry_at_ms_) && lobby_.start(seed_, map_.hash, now_ms)) {
                state_ = RoomState::Loading;
                lobby_.host_loaded(true);                            // the server loaded the map when it made the room
            }
        } else if (now_ms - created_ms_ >= spec_.wait_ms) {
            fail("the players did not all come (" + std::to_string(lobby_.players()) + " of " + std::to_string(spec_.players) + ")", now_ms);
            return;
        }
    }

    if (state_ == RoomState::Running && session_) {
        session_->update(now_ms);
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
        if (now_ms - started_ms_ >= spec_.run_ms) return fail("the match took longer than the room's limit", now_ms);
        bool anybody = false;
        for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) anybody = anybody || session_->client_present(seat);
        if (!anybody) finish("everybody left", now_ms);
    }
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
    s.joined = static_cast<uint8_t>(lobby_.players());
    const net::RoomMsg& room = lobby_.room();
    for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) {
        s.names[seat] = room.slots[seat].state == net::SlotState::Empty ? std::string() : room.slots[seat].name;
        if (state_ == RoomState::Running || state_ == RoomState::Finished) {
            if (!names_[seat].empty()) s.names[seat] = names_[seat];
        }
    }
    s.ticks = last_ticks_;
    s.turns = last_turns_;
    s.age_ms = now_ms - created_ms_;
    s.quitter = quitter_;
    s.rows = rows_;
    return s;
}

}  // namespace ants::server
