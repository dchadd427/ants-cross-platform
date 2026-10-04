#include "ants_server/room_manager.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <random>
#include <set>

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
        if (rooms_.find(code) == rooms_.end() && deferred_.count(code) == 0) return code;
    }
    return "R" + std::to_string(++code_counter_);
}

namespace {

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
    if (rooms_.size() + deferred_.size() >= limits_.max_rooms) return fail(503, "the server has no room for another room");
    if (spec.players < 2 || spec.players > sim::MAX_PLAYERS) return fail(400, "players must be 2 to 4");
    if (!spec.code.empty() && (!net::valid_room_code(spec.code))) return fail(400, "the room code may hold letters, digits, '_' and '-' only (up to 32 characters)");
    if (!spec.code.empty() && (rooms_.find(spec.code) != rooms_.end() || deferred_.count(spec.code) != 0)) return fail(409, "a room with this code exists");     // (a record that waits for its first player is a room)
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
    auto room = std::make_unique<Room>(std::move(spec), std::move(entry), std::move(level), seed, now_ms, &log_budget_);
    if (restart_ != nullptr && restart_->enabled()) room->set_restart_store(restart_.get());       // (a room that holds seats keeps a record from the start of its match: room.hpp)
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

// What the code of a demo room chooses: "demo-[<map>-][<n>p-]<anything>". <map> is the name of one of the allowed maps without its extension (any case; of
// several that fit, the longest name wins: a name may contain dashes itself); <n>p is the number of players, 2 to 4 ("demo-small-2p-x7k2": SMALL.LVL for two
// players). When the first word is not an allowed map, <n>p may still be the first or the second word (the page offers the six maps of the original; a server
// that allows fewer still makes the room for the players the page shows). What the code does not choose is the default (`demo_map`, `demo_players`).
struct DemoChoice {
    std::string map;
    uint8_t players;
};

bool is_demo_code(const std::string& code) {
    const size_t n = std::char_traits<char>::length(kDemoRoomPrefix);
    return code.size() > n && code.compare(0, n, kDemoRoomPrefix) == 0;
}

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
    for (const auto& kv : deferred_) demos += kv.first.compare(0, prefix.size(), prefix) == 0 ? 1u : 0u;
    if (demos >= limits_.demo_rooms) return false;
    RoomSpec spec = default_spec();                                 // (demo rooms follow the server's reconnect setting and its limits)
    spec.code = code;
    const DemoChoice choice = demo_choice_of(code, limits_);
    spec.map = choice.map;
    spec.players = choice.players;
    spec.early_start = true;                                        // the first player in a demo room may start it with the players who are there (the page tells them)
    spec.wait_ms = limits_.demo_wait_ms;
    spec.keep_ms = 30000;
    spec.run_ms = 30u * 60u * 1000u;                                // a demo room does not hold its slot for longer than half an hour of play
    return create_room(std::move(spec), now_ms).ok;
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

void RoomManager::update(uint32_t now_ms) {
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
        bool done = false;
        if (p.connection->poll(msg)) {
            done = true;
            net::HelloMsg hello;
            net::RejectReason reason = net::RejectReason::BadRequest;
            bool good = false;
            if (net::peek_type(msg) == net::MsgType::Hello && net::decode_hello_prefix(msg.data(), msg.size(), hello)) {
                if (hello.version != net::kProtocolVersion) reason = net::RejectReason::VersionMismatch;       // another version's layout is not read further
                else if (!net::decode(msg, hello)) reason = net::RejectReason::BadRequest;
                else good = true;
            }
            Room* room = nullptr;
            bool rejoin = false;                                       // a player who comes back to a match that runs (a Hello with the key of a seat)
            if (good) {
                auto it = hello.room.empty() ? rooms_.end() : rooms_.find(hello.room);
                const bool ended = it != rooms_.end() && (it->second->state() == RoomState::Finished || it->second->state() == RoomState::Failed);
                if (ended && is_demo_code(hello.room) && limits_.demo_rooms > 0) {
                    // A demo room that is over is forgotten at once when somebody comes back to its code (a late friend, a reload, a rematch with the same
                    // link): its end is reported, and the Hello makes a new room below
                    if (!it->second->end_reported()) {
                        it->second->mark_end_reported();
                        unreported_.push_back(it->second->status(now_ms));
                    }
                    rooms_.erase(it);
                    it = rooms_.end();
                }
                if (it == rooms_.end() && !hello.room.empty() && restore_deferred(hello.room, now_ms)) it = rooms_.find(hello.room);      // (a record that the restore did not reach: its first Hello brings the room back)
                if (it == rooms_.end() && !hello.room.empty() && make_demo_room(hello.room, now_ms)) it = rooms_.find(hello.room);
                if (it == rooms_.end()) {
                    good = false;
                    reason = net::RejectReason::NoSuchRoom;
                } else if (it->second->state() == RoomState::Finished || it->second->state() == RoomState::Failed) {
                    good = false;
                    reason = net::RejectReason::NoSuchRoom;                // the room is over: "the match has already started" would be wrong
                } else if (!it->second->accepting()) {
                    if (!net::key_is_zero(hello.key) && it->second->can_rejoin()) {
                        room = it->second.get();                      // the session of the match decides (a key that fits no seat is told MatchRunning there, as here)
                        rejoin = true;
                    } else {
                        good = false;
                        reason = net::RejectReason::MatchRunning;
                    }
                } else {
                    room = it->second.get();
                }
            }
            std::unique_ptr<net::Connection> connection = std::move(p.connection);
            const std::string address = p.address;
            pending_.erase(pending_.begin() + static_cast<std::ptrdiff_t>(i));
            if (good && rejoin) {
                if (!room->rejoin(connection, hello, now_ms)) {        // the session has answered (a Reject) and closed it: it stays a moment so that the answer arrives
                    ++refused_;
                    lingering_.push_back(Lingering{std::move(connection), now_ms});
                }
                continue;
            }
            if (good && room->add_connection(connection, address, msg, now_ms)) continue;        // the room owns it now
            reject(std::move(connection), good ? net::RejectReason::Full : reason, now_ms);
            continue;
        }
        if (!p.connection->is_open() || now_ms - p.since_ms >= limits_.hello_timeout_ms) done = true;
        if (done) {
            ++refused_;
            if (p.connection->is_open()) p.connection->close();
            pending_.erase(pending_.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            ++i;
        }
    }
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

std::vector<std::string> RoomManager::take_notices() {
    return restart_ != nullptr ? restart_->take_notes() : std::vector<std::string>();
}

namespace {

std::string seconds_text(uint64_t ms) { return std::to_string(ms / 1000) + " s"; }

std::string hex16(uint64_t v) {
    static const char kHex[] = "0123456789abcdef";
    std::string out(16, '0');
    for (int i = 0; i < 16; ++i) out[static_cast<size_t>(15 - i)] = kHex[(v >> (4 * i)) & 0xFu];
    return out;
}

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

RoomManager::Verdict RoomManager::judge_and_replay(const std::string& path, uint32_t now_ms, const std::function<uint32_t()>& clock, const std::function<Room::ReplayCheck()>& check,
                                                   const std::set<std::string>& waiting, RestoreItem& item, Replayed& out) {
    const RestartConfig& cfg = restart_->config();
    const uint32_t entered = clock();
    item = RestoreItem{};
    item.file = fs::path(path).filename().string();
    // One record: read (and checked all through), judged, and either made a room again (replayed: it begins when the caller says) or made a failed room with the reason. Its file is deleted unless the room
    // that it became goes on writing it, or the record was only put off (Stopped, Deferred).
    RestartLoaded rec = read_restart_record(path, cfg.max_record_bytes, RestartRead::Streaming);       // (checked and counted, the turns are not kept: the replay decodes them one at a time)
    if (!rec.ok()) {
        item.outcome = RestoreItem::Outcome::Unreadable;
        item.note = std::string(read_status_name(rec.status)) + ": " + rec.why;
        restart_->remove_file(path);
        restart_->note("restart record " + item.file + " was not restored: " + item.note);
        return Verdict::Unreadable;
    }
    const RestartHead& head = rec.head;
    item.code = head.code;
    item.turns = rec.turn_count;
    if (rooms_.find(head.code) != rooms_.end() || waiting.count(head.code) != 0 || deferred_.count(head.code) != 0) {   // (two records for one code cannot be: the name holds a hash of the code; a room made before the restore can)
        item.outcome = RestoreItem::Outcome::Unreadable;
        item.note = "a room with the code " + head.code + " exists already";
        restart_->remove_file(path);
        restart_->note("restart record " + item.file + " was not restored: " + item.note);
        return Verdict::Unreadable;
    }
    // The things that make a match impossible to go on with. What each says is what the status shows for the room, and the log.
    std::string refusal;
    if (!head.identity.same_rules_as(cfg.identity)) {
        refusal = "ended by a restart of the server: the match was started with network protocol " + std::to_string(head.identity.protocol) + " (" + head.identity.game_version + "), this server speaks protocol " +
                  std::to_string(cfg.identity.protocol) + " (" + cfg.identity.game_version + "), and a match cannot go on across a change of the rules";
    }
    RoomSpec spec = room_spec_of(head);
    MapEntry entry;
    assets::LevelData level;
    if (refusal.empty()) {
        const std::string range = spec_range_error(spec);
        if (!range.empty()) refusal = "ended by a restart of the server: the record holds a room that this server would not make (" + range + ")";
    }
    if (refusal.empty()) {
        std::error_code ec;
        const auto age = fs::file_time_type::clock::now() - fs::last_write_time(path, ec);
        const uint64_t age_ms = ec || age.count() < 0 ? 0u : static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(age).count());
        if (age_ms > cfg.max_age_ms) refusal = "ended by a restart of the server: the record was last written " + seconds_text(age_ms) + " ago and its players have given up";
    }
    if (refusal.empty() && rooms_.size() + waiting.size() + deferred_.size() >= limits_.max_rooms) refusal = "ended by a restart of the server: this server has no room for another room";
    if (refusal.empty()) {
        std::string why;
        if (!store_.find(head.map, entry, &why)) refusal = "ended by a restart of the server: the map " + head.map + " is not on this server (" + why + ")";
        else if (entry.hash != head.map_hash) refusal = "ended by a restart of the server: the map file " + head.map + " is not the one that the match was played on";
        else if (!level.load_from_file(entry.path)) refusal = "ended by a restart of the server: the map " + head.map + " cannot be played by this engine";
        else if (!level.validate(head.start.roster).playable) refusal = "ended by a restart of the server: the map cannot be played by the seats of the match";
    }
    if (refusal.empty()) {
        out.room = std::make_unique<Room>(std::move(spec), std::move(entry), std::move(level), head.start.seed, now_ms, &log_budget_);
        out.room->set_restart_store(restart_.get());
        std::string why;
        Room::ReplayLimits limits;
        limits.budget_ms = cfg.replay_budget_ms;
        limits.clock = clock;
        limits.check = check;
        Room::ReplayResult result = Room::ReplayResult::Refused;
        try {
            result = out.room->replay(rec, cfg.restart_vote_after_ms, limits, why);
        } catch (const std::exception& e) {
            why = std::string("the replay failed (") + e.what() + ")";
        }
        if (result == Room::ReplayResult::Replayed) {
            out.head = head;
            out.item = item;
            out.path = path;
            return Verdict::Replayed;
        }
        out.room.reset();                                               // (it holds a half-built match: it goes, with its log)
        if (result == Room::ReplayResult::Stopped) return Verdict::Stopped;
        if (result == Room::ReplayResult::Deferred) return Verdict::Deferred;
        refusal = "ended by a restart of the server: " + why;
    }
    // not restored: the room is there as a failed room with the reason, and the record goes
    item.outcome = RestoreItem::Outcome::Ended;
    item.note = refusal;
    restart_->remove_file(path);
    if (rooms_.size() + waiting.size() + deferred_.size() < limits_.max_rooms) rooms_.emplace(head.code, Room::refused(head, refusal, item.turns, now_ms + (clock() - entered)));       // (the rooms that were replayed have their places)
    restart_->note("room " + head.code + " was not restored: " + refusal);
    return Verdict::Ended;
}

// A room that was replayed begins: its clocks start now, the report and the log say what came back
void RoomManager::begin_replayed(Replayed& replayed, uint32_t now_ms, RestoreReport& report) {
    const RestartConfig& cfg = restart_->config();
    RestoreItem& item = replayed.item;
    const RestartHead& head = replayed.head;
    std::string why;
    if (!replayed.room->begin_restored(now_ms, why)) {                  // (cannot be: the bots sat down in the replay)
        const std::string refusal = "ended by a restart of the server: " + why;
        replayed.room.reset();
        item.outcome = RestoreItem::Outcome::Ended;
        item.note = refusal;
        restart_->remove_file(replayed.path);
        if (rooms_.size() < limits_.max_rooms) rooms_.emplace(head.code, Room::refused(head, refusal, item.turns, now_ms));
        restart_->note("room " + head.code + " was not restored: " + refusal);
        report.items.push_back(item);
        return;
    }
    const RoomStatus st = replayed.room->status(now_ms);
    item.outcome = RestoreItem::Outcome::Restored;
    item.replay_ms = st.restore_ms;
    item.note = "restored: " + std::to_string(item.turns) + " turns (" + seconds_text(uint64_t{item.turns} * net::kTurnMs) + " of play) replayed in " + std::to_string(st.restore_ms) + " ms, state hash " + hex16(st.restored_hash) + ", " +
                std::to_string(st.absent.size()) + " seat(s) waiting for their players" + (st.state == RoomState::Finished ? "; the match had ended" : std::string()) +
                (head.identity.game_version != cfg.identity.game_version || head.identity.build_id != cfg.identity.build_id ? "; the record was written by " + head.identity.game_version + " build " + head.identity.build_id : std::string());
    restart_->note("room " + head.code + " " + item.note);
    rooms_.emplace(head.code, std::move(replayed.room));
    report.items.push_back(item);
}

RestoreReport RoomManager::restore_rooms(uint32_t now_ms, const std::function<bool()>& should_stop) {
    RestoreReport report;
    if (restart_ == nullptr || !restart_->enabled()) return report;
    const RestartConfig& cfg = restart_->config();
    const std::function<uint32_t()> clock = cfg.clock_ms ? cfg.clock_ms : std::function<uint32_t()>(restart_steady_ms);
    const uint32_t began = clock();
    const auto out_of_time = [&]() { return clock() - began >= cfg.restore_budget_ms; };
    // the newest first: the match that was played a moment ago before one that had been waiting (ties by name, so that the order is the same on every run)
    std::vector<std::pair<fs::file_time_type, std::string>> found;
    for (const std::string& path : restart_->records()) {
        std::error_code ec;
        const fs::file_time_type written = fs::last_write_time(path, ec);
        found.emplace_back(ec ? fs::file_time_type::min() : written, path);
    }
    std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first != b.first ? a.first > b.first : a.second < b.second; });
    std::vector<Replayed> ready;                                        // replayed, not begun: they all begin together at the end, with a clock that starts then
    std::set<std::string> waiting;
    const auto check = [&]() {
        if (should_stop && should_stop()) return Room::ReplayCheck::Stop;
        return out_of_time() ? Room::ReplayCheck::Defer : Room::ReplayCheck::Go;
    };
    size_t at = 0;
    for (; at < found.size(); ++at) {
        if (should_stop && should_stop()) {
            report.stopped = true;
            break;
        }
        if (out_of_time()) break;                                       // the rest is deferred
        RestoreItem item;
        Replayed replayed;
        const Verdict verdict = judge_and_replay(found[at].second, now_ms + (clock() - began), clock, check, waiting, item, replayed);
        if (verdict == Verdict::Replayed) {
            waiting.insert(replayed.head.code);
            ready.push_back(std::move(replayed));
        } else if (verdict == Verdict::Stopped) {
            report.stopped = true;
            break;
        } else if (verdict == Verdict::Deferred) {
            break;                                                      // this record and the rest are put off
        } else {
            report.items.push_back(item);
        }
    }
    if (report.stopped) {                                               // everything stays as it is: the rooms that were replayed are thrown away (no record was opened again), nothing is deleted
        report.left_on_disk = (found.size() - at) + ready.size();
        restart_->note("the restore was stopped by the server's stop: " + std::to_string(report.left_on_disk) + " restart record(s) are left on disk as they were");
        return report;
    }
    for (size_t i = at; i < found.size(); ++i) {                        // what the budget did not reach waits for its first player
        RestoreItem item;
        item.file = fs::path(found[i].second).filename().string();
        std::string code;
        if (!restart_->code_of_path(found[i].second, code) || rooms_.count(code) != 0 || waiting.count(code) != 0 || deferred_.count(code) != 0) {
            restart_->note("restart record " + item.file + " was not reached by the restore's " + seconds_text(cfg.restore_budget_ms) + " and its name does not say a room: it is left on disk");
            continue;
        }
        deferred_[code] = found[i].second;
        item.code = code;
        item.outcome = RestoreItem::Outcome::Deferred;
        item.note = "deferred: the restore's " + seconds_text(cfg.restore_budget_ms) + " are used up; the room is restored when its first player comes";
        restart_->note("room " + code + " " + item.note);
        report.items.push_back(item);
    }
    const uint32_t fresh = now_ms + (clock() - began);                  // the rooms begin NOW: the time that the replays took is not part of any room's pause
    for (Replayed& replayed : ready) begin_replayed(replayed, fresh, report);
    return report;
}

bool RoomManager::restore_deferred(const std::string& code, uint32_t& now_ms) {
    const auto found = deferred_.find(code);
    if (found == deferred_.end() || restart_ == nullptr) return false;
    const std::string path = found->second;
    deferred_.erase(found);
    const RestartConfig& cfg = restart_->config();
    const std::function<uint32_t()> clock = cfg.clock_ms ? cfg.clock_ms : std::function<uint32_t()>(restart_steady_ms);
    const uint32_t began = clock();
    RestoreItem item;
    Replayed replayed;
    if (judge_and_replay(path, now_ms, clock, nullptr, std::set<std::string>(), item, replayed) == Verdict::Replayed) {
        const uint32_t fresh = now_ms + (clock() - began);
        RestoreReport scratch;
        begin_replayed(replayed, fresh, scratch);
        now_ms = fresh;                                                 // (the rest of this pass goes on from the time that the replay took)
    }
    return rooms_.find(code) != rooms_.end();
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
    if (restart_ != nullptr) restart_->retry_stale();             // (a record whose delete failed: one more try before the server goes)
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
