// The door's limits per address (docs/ADDRESS_LIMITS.md): what the room manager does with the answers of address_limits.hpp. The numbers and the windows are AddressGuard's; here are the parts that
// need the rooms and the connections: the rooms an address holds, the answers that wait, the lobbies that are closed early, and the line for the log.
#include <map>

#include "ants_net/clock.hpp"
#include "ants_server/room_manager.hpp"

namespace ants::server {

namespace {
bool ended(const Room& room) { return room.state() == RoomState::Finished || room.state() == RoomState::Failed; }
}  // namespace

size_t RoomManager::open_rooms_of(const std::string& id) const {
    if (id.empty()) return 0;
    size_t n = 0;
    for (const auto& kv : rooms_) n += kv.second->creator() == id && !ended(*kv.second) ? 1u : 0u;
    return n;
}

void RoomManager::queue_delayed(Delayed delayed) {
    size_t held_by_id = 0;
    for (const Delayed& d : delayed_) held_by_id += d.id == delayed.id ? 1u : 0u;
    if (delayed_.size() >= kWrongCodeHeldTotal || held_by_id >= kWrongCodeHeldPerAddress) {
        guard_.note_wrong_code_dropped();
        ++refused_;
        if (delayed.connection->is_open()) delayed.connection->close();
        return;
    }
    guard_.note_wrong_code_slowed();
    delayed_.push_back(std::move(delayed));
}

// A Hello for a room that does not exist. A person who mistypes a code is answered at once; so is everybody until the address has said more wrong codes in the minute than the allowance. Beyond it each answer waits
// longer, and the answer at the end of the wait is the same NoSuchRoom. A program that tries codes one after another is held to the few answers that wait; the Hellos that find no place to wait are dropped
// without one, as a link that never completed is. A Hello for a room that exists from such an address waits in the same list (hold_hello), so that the right codes are no faster than the wrong ones.
void RoomManager::answer_no_such_room(std::unique_ptr<net::Connection> connection, const std::string& id, net::RejectReason reason, uint32_t now_ms) {
    const uint32_t hold = guard_.wrong_code(id, now_ms);
    if (hold == 0) return reject(std::move(connection), reason, now_ms);
    Delayed d;
    d.connection = std::move(connection);
    d.id = id;
    d.reason = reason;
    d.due_ms = now_ms + hold;
    queue_delayed(std::move(d));
}

bool RoomManager::hold_hello(std::unique_ptr<net::Connection>& connection, const std::string& address, const std::string& id, const std::vector<uint8_t>& message, const net::HelloMsg& hello, uint32_t now_ms) {
    const uint32_t hold = guard_.hold_now(id, now_ms);
    if (hold == 0) return false;
    Delayed d;
    d.connection = std::move(connection);
    d.id = id;
    d.due_ms = now_ms + hold;
    d.hello_waits = true;
    d.address = address;
    d.message = message;
    d.hello = hello;
    queue_delayed(std::move(d));
    return true;
}

void RoomManager::release_delayed(uint32_t now_ms) {
    for (size_t i = 0; i < delayed_.size();) {
        Delayed& d = delayed_[i];
        if (!d.connection->is_open()) {                                  // (the peer gave up waiting)
            ++refused_;
            delayed_.erase(delayed_.begin() + static_cast<std::ptrdiff_t>(i));
        } else if (net::time_reached(now_ms, d.due_ms)) {
            Delayed due = std::move(d);
            delayed_.erase(delayed_.begin() + static_cast<std::ptrdiff_t>(i));
            if (due.hello_waits) route_hello(std::move(due.connection), due.address, due.message, due.hello, now_ms, true);       // (its turn has come: the door answers it as it would have)
            else reject(std::move(due.connection), due.reason, now_ms);
        } else {
            ++i;
        }
    }
}

// An address that holds more open rooms than crowded_rooms (a household or a LAN party holds a few; more than that is a program, or somebody who has forgotten rooms open) loses the lobbies in which nobody has
// joined, planned or started anything for crowded_idle_s: chat and Ready marks do not count, so one line every few minutes does not hold a room for ever. It is closed as a lobby that gives its place up is
// (its people are dropped, their pages make a room again), and an address is never taken below crowded_rooms: the rooms that remain are the ones that are used.
void RoomManager::close_crowded_lobbies(uint32_t now_ms) {
    const AddressLimits& l = guard_.limits();
    if (l.crowded_rooms == 0 || l.crowded_idle_s == 0) return;
    if (crowd_armed_ && !net::time_reached(now_ms, next_crowd_ms_)) return;
    crowd_armed_ = true;
    next_crowd_ms_ = now_ms + 1000;
    std::map<std::string, size_t> held;
    for (const auto& kv : rooms_) {
        if (!kv.second->creator().empty() && !ended(*kv.second)) ++held[kv.second->creator()];
    }
    for (auto it = rooms_.begin(); it != rooms_.end();) {
        Room& room = *it->second;
        const auto h = room.lobby_waiting() && !room.creator().empty() ? held.find(room.creator()) : held.end();
        if (h != held.end() && h->second > l.crowded_rooms && room.quiet_ms(now_ms) >= l.crowded_idle_s * 1000u) {
            room.forget(now_ms);                                           // (no word in the log: no match was begun; the counts below tell it)
            guard_.note_idle_closed();
            --h->second;
            it = rooms_.erase(it);
        } else {
            ++it;
        }
    }
}

void RoomManager::limits_upkeep(uint32_t now_ms) {
    if (upkeep_armed_ && !net::time_reached(now_ms, next_upkeep_ms_)) return;
    upkeep_armed_ = true;
    next_upkeep_ms_ = now_ms + 60u * 1000u;
    guard_.sweep(now_ms);
    const AddressRefusals& r = guard_.refusals();
    std::string line;
    const auto add = [&line](uint64_t now, uint64_t before, const char* what) {
        if (now == before) return;
        line += (line.empty() ? " " : ", ") + std::to_string(now - before) + " " + what;
    };
    add(r.connections, noted_.connections, "connections closed for coming too fast");
    add(r.rooms_rate, noted_.rooms_rate, "rooms refused (made too fast)");
    add(r.rooms_open, noted_.rooms_open, "rooms refused (too many open)");
    add(r.wrong_codes_slowed, noted_.wrong_codes_slowed, "answers to unknown room codes held back");
    add(r.wrong_codes_dropped, noted_.wrong_codes_dropped, "unknown room codes dropped");
    add(r.relay, noted_.relay, "relay credentials not given");
    add(r.idle_closed, noted_.idle_closed, "idle lobbies closed early");
    noted_ = r;
    if (!line.empty()) limit_notes_.push_back("limits per address, since the last line:" + line);
}

}  // namespace ants::server
