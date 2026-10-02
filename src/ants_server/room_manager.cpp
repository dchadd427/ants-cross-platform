#include "ants_server/room_manager.hpp"

#include <algorithm>
#include <random>

#include "ants_net/protocol.hpp"

namespace ants::server {

RoomManager::RoomManager(MapStore store, ServerLimits limits) : store_(std::move(store)), limits_(limits) {}

std::string RoomManager::new_code() {
    // eight characters without the look-alikes (no 0 / O, 1 / I / L): easy to read out and to type
    static const char kAlphabet[] = "23456789ABCDEFGHJKMNPQRSTUVWXYZ";
    static std::mt19937 rng{std::random_device{}()};
    for (int attempt = 0; attempt < 64; ++attempt) {
        std::string code;
        for (int i = 0; i < 8; ++i) code.push_back(kAlphabet[rng() % (sizeof(kAlphabet) - 1)]);
        if (rooms_.find(code) == rooms_.end()) return code;
    }
    return "R" + std::to_string(++code_counter_);
}

CreateResult RoomManager::create_room(RoomSpec spec, uint32_t now_ms) {
    CreateResult r;
    auto fail = [&](int status, const std::string& why) {
        r.ok = false;
        r.http_status = status;
        r.error = why;
        return r;
    };
    if (rooms_.size() >= limits_.max_rooms) return fail(503, "the server has no room for another room");
    if (spec.players < 2 || spec.players > sim::MAX_PLAYERS) return fail(400, "players must be 2 to 4");
    if (!spec.code.empty() && (!net::valid_room_code(spec.code))) return fail(400, "the room code may hold letters, digits, '_' and '-' only (up to 32 characters)");
    if (!spec.code.empty() && rooms_.find(spec.code) != rooms_.end()) return fail(409, "a room with this code exists");
    if (spec.wait_ms < 1000 || spec.wait_ms > 24u * 3600u * 1000u) return fail(400, "wait_seconds must be 1 to 86400");
    if (spec.load_ms < 1000 || spec.load_ms > 600u * 1000u) return fail(400, "load_seconds must be 1 to 600");
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
    rooms_.emplace(code, std::make_unique<Room>(std::move(spec), std::move(entry), std::move(level), seed, now_ms));
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
    if (demos >= limits_.demo_rooms) return false;
    RoomSpec spec;
    spec.code = code;
    const DemoChoice choice = demo_choice_of(code, limits_);
    spec.map = choice.map;
    spec.players = choice.players;
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
                if (it == rooms_.end() && !hello.room.empty() && make_demo_room(hello.room, now_ms)) it = rooms_.find(hello.room);
                if (it == rooms_.end()) {
                    good = false;
                    reason = net::RejectReason::NoSuchRoom;
                } else if (it->second->state() == RoomState::Finished || it->second->state() == RoomState::Failed) {
                    good = false;
                    reason = net::RejectReason::NoSuchRoom;                // the room is over: "the match has already started" would be wrong
                } else if (!it->second->accepting()) {
                    good = false;
                    reason = net::RejectReason::MatchRunning;
                } else {
                    room = it->second.get();
                }
            }
            std::unique_ptr<net::Connection> connection = std::move(p.connection);
            const std::string address = p.address;
            pending_.erase(pending_.begin() + static_cast<std::ptrdiff_t>(i));
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
