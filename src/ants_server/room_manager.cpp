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
        ++refused_;
        connection->close();                                   // too many that have not said Hello: no answer, no record
        return;
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
                const auto it = hello.room.empty() ? rooms_.end() : rooms_.find(hello.room);
                if (it == rooms_.end()) {
                    good = false;
                    reason = net::RejectReason::NoSuchRoom;
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
        if (it->second->expired(now_ms)) it = rooms_.erase(it);
        else ++it;
    }
}

std::vector<RoomStatus> RoomManager::take_ended(uint32_t now_ms) {
    std::vector<RoomStatus> out;
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
