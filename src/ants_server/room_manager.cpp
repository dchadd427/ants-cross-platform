#include "ants_server/room_manager.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <deque>
#include <random>

#include "ants_net/clock.hpp"
#include "ants_net/protocol.hpp"

namespace fs = std::filesystem;

namespace ants::server {

RoomManager::RoomManager(MapStore store, ServerLimits limits) : store_(std::move(store)), limits_(limits), log_budget_(limits.log_budget_bytes) {}

RoomManager::~RoomManager() = default;

RoomSpec RoomManager::default_spec() const {
    RoomSpec spec;
    spec.reconnect = limits_.reconnect;
    spec.vote_after_ms = limits_.hold_vote_ms;
    spec.max_pause_ms = limits_.max_pause_ms;
    spec.max_catch_up_ms = limits_.max_catch_up_ms;
    spec.resume_countdown_ms = limits_.resume_countdown_ms;
    spec.max_log_bytes = limits_.room_log_bytes;
    return spec;
}

std::string RoomManager::new_code() {
    // eight characters without the look-alikes (no 0 / O, 1 / I / L): easy to read out and to type
    static const char kAlphabet[] = "23456789ABCDEFGHJKMNPQRSTUVWXYZ";
    static std::mt19937 rng{std::random_device{}()};
    for (int attempt = 0; attempt < 64; ++attempt) {
        std::string code;
        for (int i = 0; i < 8; ++i) code.push_back(kAlphabet[rng() % (sizeof(kAlphabet) - 1)]);
        if (rooms_.find(code) == rooms_.end() && !restoring_has(code)) return code;
    }
    return "R" + std::to_string(++code_counter_);
}

namespace {

bool is_demo_code(const std::string& code) {
    const size_t n = std::char_traits<char>::length(kDemoRoomPrefix);
    return code.size() > n && code.compare(0, n, kDemoRoomPrefix) == 0;
}

// The ranges of a room's specification (what create_room and the control interface refuse, and what a restart record's room must keep): "" when all is well, else what is wrong, as the sentence of a 400
std::string spec_range_error(const RoomSpec& spec) {
    if (spec.wait_ms < 1000 || spec.wait_ms > 24u * 3600u * 1000u) return "wait_seconds must be 1 to 86400";
    if (spec.load_ms < 1000 || spec.load_ms > 600u * 1000u) return "load_seconds must be 1 to 600";
    if (spec.keep_ms > 24u * 3600u * 1000u) return "keep_seconds must be 0 to 86400";           // (as the control interface bounds them: a hostile record's head must not keep a failed room for ever or lift the limit of a match)
    if (spec.run_ms > 24u * 3600u * 1000u) return "max_run_seconds must be at most 86400";
    if (spec.vote_after_ms < kMinVoteAfterMs || spec.vote_after_ms > kMaxVoteAfterMs) return "hold_vote_seconds must be 5 to 3600";
    if (spec.max_pause_ms < kMinMaxPauseMs || spec.max_pause_ms > kMaxMaxPauseMs) return "max_pause_seconds must be 60 to 86400";
    if (spec.max_catch_up_ms < kMinCatchUpMs || spec.max_catch_up_ms > kMaxCatchUpLimitMs) return "max_catch_up_seconds must be 10 to 3600";
    if (spec.resume_countdown_ms > kMaxResumeCountdownMs) return "resume_countdown_seconds must be 0 to 60";
    if (spec.max_connections < spec.players || spec.max_connections > 4096) return "max_connections must be the number of players to 4096";
    if (spec.max_log_bytes < kMinLogBytes || spec.max_log_bytes > kMaxLogBytes) return "the limit of the turn log must be 1 KiB to 1 GiB";
    return std::string();
}

}  // namespace

CreateResult RoomManager::create_room(RoomSpec spec, uint32_t now_ms) {
    CreateResult r;
    auto fail = [&](int status, const std::string& why) {
        r.ok = false;
        r.http_status = status;
        r.error = why;
        return r;
    };
    if (rooms_.size() + restoring_.size() >= limits_.max_rooms) return fail(503, "the server has no room for another room");
    if (spec.players < 2 || spec.players > sim::MAX_PLAYERS) return fail(400, "players must be 2 to 4");
    if (!spec.code.empty() && (!net::valid_room_code(spec.code))) return fail(400, "the room code may hold letters, digits, '_' and '-' only (up to 32 characters)");
    if (!spec.code.empty() && (rooms_.find(spec.code) != rooms_.end() || restoring_has(spec.code))) return fail(409, "a room with this code exists");     // (a record that waits for its replay is a room)
    if (const std::string range = spec_range_error(spec); !range.empty()) return fail(400, range);
    // The bots of the room (docs/BOTS.md B6): distinct seats, a kind that exists, at least one seat left for a person, and never together with Fog of War (a bot would see through it)
    if (!spec.bots.empty()) {
        if (spec.fog) return fail(400, "bots cannot play with Fog of War: a bot would see through it");
        if (spec.bots.size() >= spec.players) return fail(400, "bots: at least one of the room's players must be a person (players " + std::to_string(spec.players) + ", bots " + std::to_string(spec.bots.size()) + ")");
        uint8_t seats = 0;
        for (const ai::BotSpec& b : spec.bots) {
            if (b.seat >= sim::MAX_PLAYERS) return fail(400, "bots: a seat is 0 to 3");
            if ((seats & (1u << b.seat)) != 0) return fail(400, "bots: seat " + std::to_string(static_cast<unsigned>(b.seat)) + " has a bot already");
            if (!ai::known_bot_kind(b.kind)) return fail(400, "bots: unknown bot '" + b.kind + "'");
            seats = static_cast<uint8_t>(seats | (1u << b.seat));
        }
    }
    MapEntry entry;
    std::string why;
    if (!store_.find(spec.map, entry, &why)) return fail(404, "map '" + spec.map + "': " + why);
    assets::LevelData level;
    if (!level.load_from_file(entry.path)) return fail(422, "the map '" + spec.map + "' cannot be played by this engine");
    if (spec.code.empty()) spec.code = new_code();
    uint32_t seed = spec.seed;
    if (!spec.has_seed) {
        std::random_device rd;
        seed = rd();
    }
    const std::string code = spec.code;
    const bool wants_replay = spec.record_replay;
    auto room = std::make_unique<Room>(std::move(spec), std::move(entry), std::move(level), seed, now_ms, &log_budget_);
    if (restart_ != nullptr && restart_->enabled()) room->set_restart_store(restart_.get());       // (a room that holds seats keeps a record from the start of its match: room.hpp)
    if (replays_ == nullptr) room->set_replay_store(nullptr, "this server keeps no replays");
    else if (!wants_replay) room->set_replay_store(nullptr, "the room was made with \"record\": false");
    else if (is_demo_code(code) && !replay_demo_) room->set_replay_store(nullptr, "this server does not keep the matches of demo rooms (--replay-demo)");
    else room->set_replay_store(replays_.get());
    rooms_.emplace(code, std::move(room));
    ++created_;
    r.ok = true;
    r.http_status = 201;
    r.code = code;
    return r;
}

namespace {

bool iequals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const auto lower = [](char c) { return static_cast<char>((c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c); };
        if (lower(a[i]) != lower(b[i])) return false;
    }
    return true;
}

// What the code of a demo room chooses: "demo-[<map>-][<n>p-]<anything>" (the team word of the code, `t01`, is read by net::room_code_teams: it names the room's teams, not the map or the players). <map> is the name of one of the allowed maps without its extension (any case; of
// several that fit, the longest name wins: a name may contain dashes itself); <n>p is the number of players, 2 to 4 ("demo-small-2p-x7k2": SMALL.LVL for two
// players). When the first word is not an allowed map, <n>p may still be the first or the second word (the page offers the six maps of the original; a server
// that allows fewer still makes the room for the players the page shows). What the code does not choose is the default (`demo_map`, `demo_players`).
struct DemoChoice {
    std::string map;
    uint8_t players;
};

bool players_word(const std::string& rest, size_t at, uint8_t& players) {
    if (rest.size() < at + 3 || rest[at] < '2' || rest[at] > '4' || (rest[at + 1] != 'p' && rest[at + 1] != 'P') || rest[at + 2] != '-') return false;
    players = static_cast<uint8_t>(rest[at] - '0');
    return true;
}

DemoChoice demo_choice_of(const std::string& code, const ServerLimits& limits) {
    DemoChoice c{limits.demo_map, limits.demo_players};
    const std::string rest = code.substr(std::char_traits<char>::length(kDemoRoomPrefix));
    const std::string* best = nullptr;
    size_t best_len = 0;
    for (const std::string& file : limits.demo_maps) {
        const size_t dot = file.rfind('.');
        const size_t len = dot == std::string::npos ? file.size() : dot;
        if (len == 0 || len <= best_len || rest.size() <= len || rest[len] != '-') continue;
        if (iequals(rest.substr(0, len), file.substr(0, len))) {
            best = &file;
            best_len = len;
        }
    }
    if (best != nullptr) {
        c.map = *best;
        players_word(rest, best_len + 1, c.players);
        return c;
    }
    if (players_word(rest, 0, c.players)) return c;
    const size_t dash = rest.find('-');
    if (dash != std::string::npos && dash > 0) players_word(rest, dash + 1, c.players);
    return c;
}

}  // namespace

bool RoomManager::make_demo_room(const std::string& code, uint32_t now_ms) {
    if (limits_.demo_rooms == 0 || limits_.demo_map.empty()) return false;
    const std::string prefix = kDemoRoomPrefix;
    if (code.size() <= prefix.size() || code.compare(0, prefix.size(), prefix) != 0 || !net::valid_room_code(code)) return false;
    size_t demos = 0;
    for (const auto& kv : rooms_) demos += kv.first.compare(0, prefix.size(), prefix) == 0 ? 1u : 0u;
    for (const Restoring& r : restoring_) demos += r.code.compare(0, prefix.size(), prefix) == 0 ? 1u : 0u;
    while (demos >= limits_.demo_rooms) {                           // no place is free: a match that nobody has come back to for a while gives its place up (never a room with a person at it)
        if (!evict_abandoned_demo(now_ms)) return false;
        --demos;
    }
    RoomSpec spec = default_spec();                                 // (demo rooms follow the server's reconnect setting and its limits)
    spec.code = code;
    spec.max_pause_ms = std::min(spec.max_pause_ms, kDemoMaxPauseMs);      // (the cap of a match that is abandoned: a demo place is not held for half an hour)
    const DemoChoice choice = demo_choice_of(code, limits_);
    spec.map = choice.map;
    spec.players = choice.players;
    spec.teams = net::room_code_teams(code);                        // (protocol 13: a team word in the code, `demo-treasure-4p-t01-k7m2xq`: the room starts with them every time)
    spec.early_start = true;                                        // the first player in a demo room may start it with the players who are there (the page tells them)
    spec.wait_ms = limits_.demo_wait_ms;
    spec.keep_ms = 30000;
    spec.run_ms = 30u * 60u * 1000u;                                // a demo room does not hold its slot for longer than half an hour of play
    return create_room(std::move(spec), now_ms).ok;
}

bool RoomManager::evict_abandoned_demo(uint32_t now_ms) {
    const std::string prefix = kDemoRoomPrefix;
    auto best = rooms_.end();
    uint32_t best_ms = 0;
    for (auto it = rooms_.begin(); it != rooms_.end(); ++it) {
        if (it->first.compare(0, prefix.size(), prefix) != 0) continue;
        const uint32_t gone = it->second->abandoned_ms(now_ms);
        if (gone >= kDemoAbandonedMs && (best == rooms_.end() || gone > best_ms)) {     // (on a tie the first code in the map's order: the same room every time)
            best = it;
            best_ms = gone;
        }
    }
    if (best == rooms_.end()) return false;
    Room& room = *best->second;
    room.close("abandoned: nobody came back to the match, and its place was needed", now_ms);     // (Failed: the clients are dropped, the record is deleted, the log is freed)
    if (!room.end_reported()) {
        room.mark_end_reported();
        unreported_.push_back(room.status(now_ms));
    }
    rooms_.erase(best);
    return true;
}

void RoomManager::reject(std::unique_ptr<net::Connection> connection, net::RejectReason reason, uint32_t now_ms) {
    if (connection == nullptr) return;
    ++refused_;
    if (connection->is_open()) {
        connection->send(net::encode(net::RejectMsg{reason}));
        connection->close();
    }
    lingering_.push_back(Lingering{std::move(connection), now_ms});
}

void RoomManager::add_connection(std::unique_ptr<net::Connection> connection, const std::string& address, uint32_t now_ms) {
    if (connection == nullptr) return;
    if (pending_.size() >= limits_.max_pending) {
        // Too many that have not said Hello yet. The newcomer wins: the oldest of them has been silent the longest (a real client says Hello at once, and every
        // pass of update() reads the Hellos that came), so it is the one that is dropped. Otherwise 128 silent connections would keep every player out.
        ++refused_;
        pending_.front().connection->close();
        pending_.erase(pending_.begin());
    }
    pending_.push_back(Pending{std::move(connection), address, now_ms});
}

// The connection of a Hello that waits for its room to be restored. The manager pumps it (poll) to see it close, so what the peer sends after its Hello is read here and kept, a few messages at the
// most (a client that waits for its Welcome sends nothing: a peer that sends more is a flood), and handed to the room, before anything newer, when the connection is given to it.
class ParkedLink final : public net::Connection {
public:
    static constexpr size_t kMaxEarly = 8;
    explicit ParkedLink(std::unique_ptr<net::Connection> inner) : inner_(std::move(inner)) {}
    /// Reads what the peer has sent. False when it has sent more than a parked connection may hold.
    bool pump() {
        std::vector<uint8_t> message;
        while (early_.size() <= kMaxEarly && inner_->poll(message)) early_.push_back(std::move(message));
        return early_.size() <= kMaxEarly;
    }
    bool peer_gone() const { return !inner_->is_open(); }
    bool send(const std::vector<uint8_t>& message) override { return inner_->send(message); }
    bool poll(std::vector<uint8_t>& message) override {
        if (!early_.empty()) {
            message = std::move(early_.front());
            early_.pop_front();
            return true;
        }
        return inner_->poll(message);
    }
    State state() const override { return inner_->state(); }
    void close() override { inner_->close(); }
    uint32_t last_message_age_ms() const override { return inner_->last_message_age_ms(); }

private:
    std::unique_ptr<net::Connection> inner_;
    std::deque<std::vector<uint8_t>> early_;
};

bool RoomManager::restoring_has(const std::string& code) const {
    for (const Restoring& r : restoring_) {
        if (r.code == code) return true;
    }
    return false;
}

// A Hello for a room whose record waits for its replay: the connection and the Hello wait (bounded: by the door's number of connections that wait, and by the room's own number of connections, which is
// what the room would take of them: the next link of either is dropped without an answer), and go through the door when the room is restored
void RoomManager::park(std::unique_ptr<net::Connection> connection, const std::string& address, const std::vector<uint8_t>& message, const net::HelloMsg& hello, const Restoring& job, uint32_t now_ms) {
    if (parked_.size() >= limits_.max_pending) {                  // the door's bound: the link is dropped without an answer (a Reject is final for a client, a dropped link is tried again: many rooms come back at once after a restart)
        ++refused_;
        if (connection->is_open()) connection->close();
        return;
    }
    size_t of_this_room = 0;
    for (const Parked& p : parked_) of_this_room += p.code == job.code ? 1u : 0u;
    if (of_this_room >= std::max<size_t>(1, job.head.max_connections)) {      // the room's own bound (what the room would keep): dropped without an answer too, not told Full (final for a client: its key goes),
        ++refused_;                                                            // and silent Hellos of anybody who knows the code can hold these places: a dropped link is tried again
        if (connection->is_open()) connection->close();
        return;
    }
    auto link = std::make_unique<ParkedLink>(std::move(connection));
    Parked p;
    p.link = link.get();
    p.connection = std::move(link);
    p.address = address;
    p.message = message;
    p.hello = hello;
    p.code = job.code;
    p.since_ms = now_ms;
    parked_.push_back(std::move(p));
}

// The room of these Hellos is a room now (restored, or a failed room with the reason): each Hello goes through the door as any other does (the rejoin, MatchRunning, NoSuchRoom as the room's state says)
void RoomManager::release_parked(const std::string& code, uint32_t now_ms) {
    std::vector<Parked> mine;
    for (size_t i = 0; i < parked_.size();) {
        if (parked_[i].code == code) {
            mine.push_back(std::move(parked_[i]));
            parked_.erase(parked_.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            ++i;
        }
    }
    for (Parked& p : mine) route_hello(std::move(p.connection), p.address, p.message, p.hello, now_ms);
}

void RoomManager::route_hello(std::unique_ptr<net::Connection> connection, const std::string& address, const std::vector<uint8_t>& message, const net::HelloMsg& hello, uint32_t now_ms) {
    net::RejectReason reason = net::RejectReason::BadRequest;
    bool good = true;
    Room* room = nullptr;
    bool rejoin = false;                                           // a player who comes back to a match that runs (a Hello with the key of a seat)
    auto it = hello.room.empty() ? rooms_.end() : rooms_.find(hello.room);
    const bool ended = it != rooms_.end() && (it->second->state() == RoomState::Finished || it->second->state() == RoomState::Failed);
    // A Hello that shows a key is a player who comes back to a match: never a newcomer. It does not make a demo room and does not replace an ended one (the match of its key is over: NoSuchRoom,
    // and its machine lets the key go); the review found a stale key that opened a new, empty room of the same code and left the player alone in it.
    const bool keyed = !net::key_is_zero(hello.key);
    if (ended && !keyed && is_demo_code(hello.room) && limits_.demo_rooms > 0) {
        // A demo room that is over is forgotten at once when somebody comes back to its code (a late friend, a reload, a rematch with the same
        // link): its end is reported, and the Hello makes a new room below
        if (!it->second->end_reported()) {
            it->second->mark_end_reported();
            unreported_.push_back(it->second->status(now_ms));
        }
        rooms_.erase(it);
        it = rooms_.end();
    }
    if (it == rooms_.end() && !hello.room.empty()) {              // a record that waits for its replay: the room is not there yet, and the Hello waits for it
        for (const Restoring& r : restoring_) {
            if (r.code != hello.room) continue;
            park(std::move(connection), address, message, hello, r, now_ms);
            return;
        }
    }
    if (it == rooms_.end() && !keyed && !hello.room.empty() && make_demo_room(hello.room, now_ms)) it = rooms_.find(hello.room);
    if (it == rooms_.end()) {
        good = false;
        reason = net::RejectReason::NoSuchRoom;
    } else if (it->second->state() == RoomState::Finished || it->second->state() == RoomState::Failed) {
        good = false;
        reason = net::RejectReason::NoSuchRoom;                    // the room is over: "the match has already started" would be wrong
    } else if (!it->second->accepting()) {
        if (!net::key_is_zero(hello.key) && it->second->can_rejoin()) {
            room = it->second.get();                              // the session of the match decides (a key that fits no seat is told MatchRunning there, as here)
            rejoin = true;
        } else {
            good = false;
            reason = net::RejectReason::MatchRunning;
        }
    } else {
        room = it->second.get();
    }
    if (good && rejoin) {
        if (!room->rejoin(connection, hello, now_ms)) {            // the session has answered (a Reject) and closed it: it stays a moment so that the answer arrives
            ++refused_;
            lingering_.push_back(Lingering{std::move(connection), now_ms});
        }
        return;
    }
    if (good && room->add_connection(connection, address, message, now_ms)) return;        // the room owns it now
    reject(std::move(connection), good ? net::RejectReason::Full : reason, now_ms);
}

void RoomManager::update(uint32_t now_ms) {
    if (replays_ != nullptr) replays_->update();                   // (the replays that are too old are deleted once an hour)
    // 0. a record whose delete failed is tried again every 10 s (its room is over: a restart before it is deleted would bring the room back)
    if (restart_ != nullptr) {
        constexpr uint32_t kStaleRetryMs = 10000;
        if (restart_->stale_count() == 0) {
            stale_armed_ = false;
        } else if (!stale_armed_) {
            stale_armed_ = true;
            next_stale_retry_ms_ = now_ms + kStaleRetryMs;
        } else if (net::time_reached(now_ms, next_stale_retry_ms_)) {
            next_stale_retry_ms_ = now_ms + kStaleRetryMs;
            restart_->retry_stale();
        }
    }
    // 1. connections that have not said Hello yet
    for (size_t i = 0; i < pending_.size();) {
        Pending& p = pending_[i];
        std::vector<uint8_t> msg;
        if (p.connection->poll(msg)) {
            net::HelloMsg hello;
            net::RejectReason reason = net::RejectReason::BadRequest;
            bool good = false;
            if (net::peek_type(msg) == net::MsgType::Hello && net::decode_hello_prefix(msg.data(), msg.size(), hello)) {
                if (hello.version != net::kProtocolVersion) reason = net::RejectReason::VersionMismatch;       // another version's layout is not read further
                else if (!net::decode(msg, hello)) reason = net::RejectReason::BadRequest;
                else good = true;
            }
            std::unique_ptr<net::Connection> connection = std::move(p.connection);
            const std::string address = p.address;
            pending_.erase(pending_.begin() + static_cast<std::ptrdiff_t>(i));
            if (good) route_hello(std::move(connection), address, msg, hello, now_ms);
            else reject(std::move(connection), reason, now_ms);
            continue;
        }
        if (!p.connection->is_open() || now_ms - p.since_ms >= limits_.hello_timeout_ms) {
            ++refused_;
            if (p.connection->is_open()) p.connection->close();
            pending_.erase(pending_.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            ++i;
        }
    }
    // 1b. Hellos that wait for their room to be restored: a connection that closes (or sends more than a client that waits for its answer does) is dropped, and so is one that has waited too long (a
    // client gives up on a Hello after 10 s and makes a new one: the parked connections of a room that restores for a long time are the clients' retries)
    for (size_t i = 0; i < parked_.size();) {
        Parked& p = parked_[i];
        const bool flood = !p.link->pump();
        if (flood || p.link->peer_gone() || now_ms - p.since_ms >= limits_.park_timeout_ms) {
            ++refused_;
            if (p.link->is_open()) p.link->close();
            parked_.erase(parked_.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            ++i;
        }
    }
    // 1c. the records that wait for their replay: a slice of work (a room that is restored takes the Hellos that waited for it)
    restore_step(now_ms);
    // 2. rejected connections wait a moment so that their answer is delivered, then they go
    for (size_t i = 0; i < lingering_.size();) {
        if (now_ms - lingering_[i].since_ms >= limits_.reject_linger_ms || !lingering_[i].connection->is_open()) lingering_.erase(lingering_.begin() + static_cast<std::ptrdiff_t>(i));
        else ++i;
    }
    // 3. the rooms
    for (auto it = rooms_.begin(); it != rooms_.end();) {
        it->second->update(now_ms);
        if (it->second->expired(now_ms)) {
            if (!it->second->end_reported()) {                     // (a room with no keep time ends and expires in one pass: its end is still reported)
                it->second->mark_end_reported();
                unreported_.push_back(it->second->status(now_ms));
            }
            it = rooms_.erase(it);
        } else {
            ++it;
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Restart records
// ---------------------------------------------------------------------------------------------------------------------------------

bool RoomManager::enable_restart_records(RestartConfig config, std::string& why) {
    restart_.reset();
    if (config.dir.empty()) return true;
    auto store = std::make_unique<RestartStore>(std::move(config));
    if (!store->prepare(why)) return false;
    restart_ = std::move(store);
    return true;
}

bool RoomManager::enable_replays(ReplayConfig config, bool include_demo, std::string& why) {
    replays_.reset();
    replay_demo_ = include_demo;
    if (config.dir.empty()) return true;
    auto store = std::make_unique<ReplayStore>(std::move(config));
    if (!store->prepare(why)) return false;
    replays_ = std::move(store);
    return true;
}

std::vector<std::string> RoomManager::take_notices() {
    std::vector<std::string> notes = restart_ != nullptr ? restart_->take_notes() : std::vector<std::string>();
    if (replays_ != nullptr) {
        for (std::string& line : replays_->take_notes()) notes.push_back(std::move(line));
    }
    return notes;
}

namespace {

std::string seconds_text(uint64_t ms) { return std::to_string(ms / 1000) + " s"; }

std::string hex16(uint64_t v) {
    static const char kHex[] = "0123456789abcdef";
    std::string out(16, '0');
    for (int i = 0; i < 16; ++i) out[static_cast<size_t>(15 - i)] = kHex[(v >> (4 * i)) & 0xFu];
    return out;
}

// The crash-loop guard: while a piece of a record's replay runs, its marker is on disk (RestartStore::mark_replaying); a piece that returns takes it off. A server that stops in the middle of one (a crash of the
// engine on the match's turns: they were written before they were played, so the replay meets them again) leaves the marker, and the next start does not replay that record.
class ReplayMark {
public:
    ReplayMark(RestartStore* store, std::string path) : store_(store), path_(std::move(path)) { store_->mark_replaying(path_); }
    ~ReplayMark() { store_->unmark_replaying(path_); }
    ReplayMark(const ReplayMark&) = delete;
    ReplayMark& operator=(const ReplayMark&) = delete;

private:
    RestartStore* store_;
    std::string path_;
};

const char* read_status_name(RestartLoaded::Status status) {
    switch (status) {
        case RestartLoaded::Status::Ok: return "ok";
        case RestartLoaded::Status::Unreadable: return "unreadable";
        case RestartLoaded::Status::TooBig: return "too big";
        case RestartLoaded::Status::NotARecord: return "not a record";
        case RestartLoaded::Status::UnknownFormat: return "another format";
        case RestartLoaded::Status::Corrupt: return "corrupt";
    }
    return "unknown";
}

}  // namespace

// "" when the match's map is on this server, is the file that the match was played on, loads, and can be played by the seats of the match; else the refusal
std::string RoomManager::map_refusal(const RestartHead& head, MapEntry& entry, assets::LevelData& level) const {
    std::string why;
    if (!store_.find(head.map, entry, &why)) return "ended by a restart of the server: the map " + head.map + " is not on this server (" + why + ")";
    if (entry.hash != head.map_hash) return "ended by a restart of the server: the map file " + head.map + " is not the one that the match was played on";
    if (!level.load_from_file(entry.path)) return "ended by a restart of the server: the map " + head.map + " cannot be played by this engine";
    if (!level.validate(head.start.roster).playable) return "ended by a restart of the server: the map cannot be played by the seats of the match";
    return std::string();
}

// A record that cannot be restored: the room is there as a failed room with the reason (its code is taken until its keep time is over) when the server has a place for it (a job of the queue has its own
// place: it takes it from the queue), and the record is put by for a day (it was read: the owner may want it back)
void RoomManager::refuse_record(const std::string& path, const RestartHead& head, RestoreItem item, const std::string& refusal, bool has_place, uint32_t now_ms) {
    item.code = head.code;
    item.outcome = RestoreItem::Outcome::Ended;
    item.note = refusal;
    restart_->refuse_file(path);
    if (has_place) rooms_.emplace(head.code, Room::refused(head, refusal, item.turns, now_ms));
    restart_->note("room " + head.code + " was not restored: " + refusal);
    report_.items.push_back(std::move(item));
}

// One record, read (and checked all through) and judged: it is deleted when it cannot be read, made a failed room with the reason when its match cannot go on, and otherwise queued for its replay
// (the cheap part of a restore: what is judged here is the head and the file; the engine is not made, no turn is run, and the record's bytes are not kept)
void RoomManager::judge_record(const std::string& path, uint32_t now_ms) {
    const RestartConfig& cfg = restart_->config();
    RestoreItem item;
    item.file = fs::path(path).filename().string();
    if (restart_->was_replaying(path)) {                                // the last run stopped in the middle of this record's replay: it is put by unread and not replayed again
        item.outcome = RestoreItem::Outcome::Unreadable;
        item.note = "the server stopped in the middle of this record's replay (a crash or a kill): it is not replayed again";
        restart_->refuse_file(path);
        restart_->note("restart record " + item.file + " was not restored: " + item.note);
        report_.items.push_back(std::move(item));
        return;
    }
    RestartLoaded rec = read_restart_record(path, cfg.max_record_bytes, RestartRead::Streaming);       // (checked and counted: the turns are decoded again, one at a time, when the replay runs)
    if (!rec.ok()) {
        item.outcome = RestoreItem::Outcome::Unreadable;
        item.note = std::string(read_status_name(rec.status)) + ": " + rec.why;
        restart_->remove_file(path);
        restart_->note("restart record " + item.file + " was not restored: " + item.note);
        report_.items.push_back(std::move(item));
        return;
    }
    const RestartHead& head = rec.head;
    item.code = head.code;
    item.turns = rec.turn_count;
    if (rooms_.find(head.code) != rooms_.end() || restoring_has(head.code)) {   // (two records for one code cannot be: the name holds a hash of the code; a room made before the restore can)
        item.outcome = RestoreItem::Outcome::Unreadable;
        item.note = "a room with the code " + head.code + " exists already";
        restart_->refuse_file(path);
        restart_->note("restart record " + item.file + " was not restored: " + item.note);
        report_.items.push_back(std::move(item));
        return;
    }
    // The things that make a match impossible to go on with. What each says is what the status shows for the room, and the log.
    std::string refusal;
    if (!head.identity.same_rules_as(cfg.identity)) {
        refusal = "ended by a restart of the server: the match was started with network protocol " + std::to_string(head.identity.protocol) + " (" + head.identity.game_version + "), this server speaks protocol " +
                  std::to_string(cfg.identity.protocol) + " (" + cfg.identity.game_version + "), and a match cannot go on across a change of the rules";
    }
    if (refusal.empty()) {
        const std::string range = spec_range_error(room_spec_of(head));
        if (!range.empty()) refusal = "ended by a restart of the server: the record holds a room that this server would not make (" + range + ")";
    }
    if (refusal.empty()) {
        std::error_code ec;
        const auto age = fs::file_time_type::clock::now() - fs::last_write_time(path, ec);
        const uint64_t age_ms = ec || age.count() < 0 ? 0u : static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(age).count());
        if (age_ms > cfg.max_age_ms) refusal = "ended by a restart of the server: the record was last written " + seconds_text(age_ms) + " ago and its players have given up";
    }
    if (refusal.empty() && rooms_.size() + restoring_.size() >= limits_.max_rooms) refusal = "ended by a restart of the server: this server has no room for another room";
    if (refusal.empty()) {
        MapEntry entry;
        assets::LevelData level;
        refusal = map_refusal(head, entry, level);
    }
    if (!refusal.empty()) {
        refuse_record(path, head, std::move(item), refusal, rooms_.size() + restoring_.size() < limits_.max_rooms, now_ms);
        return;
    }
    Restoring job;
    job.code = head.code;
    job.path = path;
    job.head = head;
    job.item = std::move(item);
    restoring_.push_back(std::move(job));                               // (its code is taken: create_room says 409, a Hello for it waits)
}

RestoreReport RoomManager::restore_rooms(uint32_t now_ms, const std::function<bool()>& should_stop) {
    report_ = RestoreReport{};
    if (restart_ == nullptr || !restart_->enabled()) return report_;
    // the newest first: the match that was played a moment ago before one that had been waiting (ties by name, so that the order is the same on every run)
    std::vector<std::pair<fs::file_time_type, std::string>> found;
    for (const std::string& path : restart_->records()) {
        std::error_code ec;
        const fs::file_time_type written = fs::last_write_time(path, ec);
        found.emplace_back(ec ? fs::file_time_type::min() : written, path);
    }
    std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first != b.first ? a.first > b.first : a.second < b.second; });
    size_t at = 0;
    for (; at < found.size(); ++at) {
        if (should_stop && should_stop()) {
            report_.stopped = true;
            break;
        }
        judge_record(found[at].second, now_ms);
    }
    if (report_.stopped) {                                              // everything that waits stays as it is: nothing is deleted, no room is made
        report_.left_on_disk = (found.size() - at) + restoring_.size();
        restoring_.clear();
        restart_->note("the restore was stopped by the server's stop: " + std::to_string(report_.left_on_disk) + " restart record(s) are left on disk as they were");
        return report_;
    }
    report_.queued = restoring_.size();
    if (!restoring_.empty()) {
        restart_->note("restore: " + std::to_string(restoring_.size()) + " restart record(s) wait for their replay, which runs in slices of " + std::to_string(restart_->config().restore_slice_ms) +
                       " ms while the server serves; a Hello for such a room waits for it");
    }
    return report_;
}

// The queue's next job: a room for which a Hello waits (a player is waiting), else the newest record (the queue's order). A room whose replay has begun and whose player's Hello is no longer there
// waits its turn like any other: the order is the one rule.
size_t RoomManager::next_to_restore() const {
    for (size_t i = 0; i < restoring_.size(); ++i) {
        for (const Parked& p : parked_) {
            if (p.code == restoring_[i].code) return i;
        }
    }
    return 0;
}

// A job that cannot go on: the record is refused with the reason, the room is a failed room, the half-built one goes
RoomManager::JobEnd RoomManager::end_job(Restoring& job, const std::string& refusal, uint32_t now_ms) {
    job.room.reset();
    job.rec.reset();
    refuse_record(job.path, job.head, job.item, refusal, true, now_ms);
    return JobEnd::Ended;
}

// One piece of work on a job, of at most `slice_ms` (a room may run up to 20 turns past it): its first piece reads the record again and makes the room, the others replay turns. The job's end (the room
// is restored, or refused with the reason) is decided here; the caller takes it out of the queue.
RoomManager::JobEnd RoomManager::advance(Restoring& job, uint32_t slice_ms, const std::function<uint32_t()>& clock, uint32_t now_ms) {
    const RestartConfig& cfg = restart_->config();
    const ReplayMark mark(restart_.get(), job.path);                    // (on disk while this piece of work runs)
    std::string why;
    try {
        if (job.room == nullptr) {
            job.rec = std::make_unique<RestartLoaded>(read_restart_record(job.path, cfg.max_record_bytes, RestartRead::Streaming));
            if (!job.rec->ok() || job.rec->head.code != job.code || job.rec->turn_count != job.item.turns) {      // (it was good when it was judged: somebody has changed the file since)
                RestoreItem item = job.item;
                item.outcome = RestoreItem::Outcome::Unreadable;
                const bool readable = job.rec->ok();                    // (a record that cannot be read is deleted, as at the start; one that can be and is not the one that was judged is put by, like any refused record)
                item.note = readable ? "the record has changed since it was judged" : std::string(read_status_name(job.rec->status)) + ": " + job.rec->why;
                job.rec.reset();
                if (readable) restart_->refuse_file(job.path);
                else restart_->remove_file(job.path);
                restart_->note("restart record " + item.file + " was not restored: " + item.note);
                report_.items.push_back(std::move(item));
                return JobEnd::Unreadable;
            }
            MapEntry entry;
            assets::LevelData level;
            const std::string refusal = map_refusal(job.rec->head, entry, level);
            if (!refusal.empty()) return end_job(job, refusal, now_ms);
            job.room = std::make_unique<Room>(room_spec_of(job.rec->head), std::move(entry), std::move(level), job.rec->head.start.seed, now_ms, &log_budget_);
            job.room->set_restart_store(restart_.get());
            if (job.room->begin_replay(*job.rec, cfg.restart_vote_after_ms, why) != Room::ReplayBegin::Ready) return end_job(job, "ended by a restart of the server: " + why, now_ms);
            return JobEnd::More;
        }
        const Room::ReplayStep step = job.room->replay_step(slice_ms, clock, why);
        if (step == Room::ReplayStep::More) return JobEnd::More;
        if (step == Room::ReplayStep::Refused) return end_job(job, "ended by a restart of the server: " + why, now_ms);
    } catch (const std::exception& e) {
        return end_job(job, std::string("ended by a restart of the server: the replay failed (") + e.what() + ")", now_ms);
    }
    // The replay is the match. The room begins now: its clocks (the wall-clock limit, the vote's wait, the pause cap) start at this pass, not when the restore began and not when other rooms were replayed.
    job.rec.reset();                                                    // (the room has what it needs of the record: its bytes go)
    if (!job.room->begin_restored(now_ms, why)) return end_job(job, "ended by a restart of the server: " + why, now_ms);       // (cannot be: the bots sat down in the replay)
    const RoomStatus st = job.room->status(now_ms);
    RestoreItem item = job.item;
    item.outcome = RestoreItem::Outcome::Restored;
    item.replay_ms = st.restore_ms;
    item.note = "restored: " + std::to_string(item.turns) + " turns (" + seconds_text(uint64_t{item.turns} * net::kTurnMs) + " of play) replayed in " + std::to_string(st.restore_ms) + " ms, state hash " + hex16(st.restored_hash) + ", " +
                std::to_string(st.absent.size()) + " seat(s) waiting for their players" + (st.state == RoomState::Finished ? "; the match had ended" : std::string()) +
                (job.head.identity.game_version != cfg.identity.game_version || job.head.identity.build_id != cfg.identity.build_id ? "; the record was written by " + job.head.identity.game_version + " build " + job.head.identity.build_id : std::string());
    restart_->note("room " + job.code + " " + item.note);
    rooms_.emplace(job.code, std::move(job.room));
    report_.items.push_back(std::move(item));
    return JobEnd::Restored;
}

// Every pass of the server spends at most RestartConfig::restore_slice_ms on the records that wait (a pass does at least one piece of work: the restore always goes on). A job that ends leaves the queue,
// and the Hellos that waited for its room go through the door.
void RoomManager::restore_step(uint32_t now_ms) {
    if (restoring_.empty() || restart_ == nullptr) return;
    const RestartConfig& cfg = restart_->config();
    const std::function<uint32_t()> clock = cfg.clock_ms ? cfg.clock_ms : std::function<uint32_t()>(restart_steady_ms);
    const uint32_t began = clock();
    for (;;) {
        const size_t at = next_to_restore();
        const uint32_t spent = clock() - began;
        const JobEnd end = advance(restoring_[at], spent >= cfg.restore_slice_ms ? 0u : cfg.restore_slice_ms - spent, clock, now_ms);
        if (end != JobEnd::More) {
            const std::string code = restoring_[at].code;
            restoring_.erase(restoring_.begin() + static_cast<std::ptrdiff_t>(at));
            release_parked(code, now_ms);
        }
        if (restoring_.empty() || clock() - began >= cfg.restore_slice_ms) break;
    }
}

size_t RoomManager::shutdown(uint32_t now_ms) {
    size_t kept = 0;
    for (auto& kv : rooms_) {
        Room& room = *kv.second;
        if (room.keeps_record()) {
            room.flush_record();                                  // (a record that the disk refuses to make durable is gone: its room does not count)
            if (room.keeps_record()) {
                ++kept;
                room.release_record();                            // the file stays as it is: a record is deleted when its room is over, never because the server stops
            }
            continue;
        }
        room.close("closed by the owner", now_ms);                // (a room without a record ends with the server, as it always did)
    }
    if (restart_ != nullptr && !restoring_.empty()) {             // the records that still wait for their replay (or are half replayed) are left on disk as they were: no room of theirs is begun, nothing is written
        restart_->note("the restore was stopped by the server's stop: " + std::to_string(restoring_.size()) + " restart record(s) are left on disk as they were");
        restoring_.clear();
    }
    parked_.clear();                                              // (the Hellos that waited for them: their connections close with the server)
    if (restart_ != nullptr) restart_->retry_stale();             // (a record whose delete failed: one more try before the server goes)
    if (replays_ != nullptr) replays_->report_repeats();          // (the count of a line that kept coming: told before the server goes)
    return kept;
}

std::vector<RoomStatus> RoomManager::take_ended(uint32_t now_ms) {
    std::vector<RoomStatus> out;
    out.swap(unreported_);
    for (auto& kv : rooms_) {
        Room& room = *kv.second;
        if ((room.state() == RoomState::Finished || room.state() == RoomState::Failed) && !room.end_reported()) {
            room.mark_end_reported();
            out.push_back(room.status(now_ms));
        }
    }
    return out;
}

bool RoomManager::status(const std::string& code, RoomStatus& out, uint32_t now_ms) const {
    const auto it = rooms_.find(code);
    if (it == rooms_.end()) return false;
    out = it->second->status(now_ms);
    return true;
}

BusyCounts RoomManager::busy(uint32_t now_ms) const {
    BusyCounts counts;
    for (const auto& entry : rooms_) {
        const RoomBusy b = entry.second->busy(now_ms);                       // (a room that is over counts nothing; a restored room that nobody has come back to, after a few minutes, counts nothing either)
        counts.players += b.players;
        if (b.match) ++counts.matches;
    }
    for (const Restoring& r : restoring_) {                                  // a record that waits for its replay is a match that a restart brought back and that is not back yet: it counts as a restored room
        uint8_t bots = 0;                                                    // does for its first minutes, with its persons as people (a restart now would end it again; the persons who dropped out before the
        for (const ai::BotSpec& b : r.head.bots) bots = static_cast<uint8_t>(bots | (1u << b.seat));     // restart are not known before the replay: they count, an upper bound)
        const uint8_t persons = static_cast<uint8_t>(r.head.start.roster & ~bots);
        for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) counts.players += (persons & (1u << seat)) != 0 ? 1u : 0u;
        ++counts.matches;
    }
    return counts;
}

std::vector<RoomStatus> RoomManager::list(uint32_t now_ms) const {
    std::vector<RoomStatus> out;
    out.reserve(rooms_.size());
    for (const auto& kv : rooms_) out.push_back(kv.second->status(now_ms));
    return out;
}

bool RoomManager::close_room(const std::string& code, uint32_t now_ms) {
    const auto it = rooms_.find(code);
    if (it == rooms_.end()) return false;
    it->second->close("closed by the owner", now_ms);
    return true;
}

}  // namespace ants::server
