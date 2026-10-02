#pragma once

// The server's log of every turn of a match (docs/NETWORK_PORT.md, "Reconnect"). A player whose connection was lost, or whose page was reloaded, is given the match again from
// this log: it is cut into TurnBatch messages (protocol.hpp) and streamed to the machine that came back, which executes the turns without drawing them (LockstepRunner::fast_forward)
// and reports its state hash. Nothing else reads the log, and a match that never loses a player never needs it.
//
// A turn is stored packed, as it travels: u16 command count, then the commands in their wire form (sim::encode). One blob holds all of them and an index of 32-bit offsets finds a
// turn, so that any range can be cut out and sent as it is. An empty turn costs 2 bytes in the blob and 4 in the index (a Turn message is 7 bytes), a turn of play a few dozen. The
// decoded form would be 2.5 to 3 times bigger. With turns of 50 ms a match of 30 minutes has 36,000 turns, and the index alone is 144 KB of them; the commands of real play add a few
// hundred KB (docs/NETWORK_PORT.md has the sizes that were measured).
//
// The limit. A client may put 64 commands into every turn (Sequencer::Config::max_commands_per_turn): of 32 ants each that is 8.7 KB per player and turn, 35 KB a turn for four players,
// 1.25 GB in 30 minutes for a log that nobody could ever stream. So a log has a limit (max_bytes, 16 MiB for a room, 25 times the 651 KB of busy play): past it the log stops being usable,
// and it stays so (a log with a hole serves no replay, and a log that is not usable is never read: the room falls back to what it did before there was a way back, a player whose
// connection is lost is dropped at once). Bytes() never passes the limit: the append that would go beyond it stores nothing. The vectors may hold spare capacity, at most what their
// growth by doubling leaves, so what is allocated is never more than twice the limit. Whatever the limit, the log never holds more than 0xFFFFFFF0 bytes (the offsets are 32 bits).
//
// Pure data, no clock, no sockets; the host's session (session.hpp) appends every turn it seals, before it sends it.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "ants_net/protocol.hpp"

namespace ants::net {

class TurnLog {
public:
    /// The limit of a room's log
    static constexpr size_t kDefaultMaxBytes = 16u * 1024u * 1024u;
    /// The most that a log ever holds, whatever limit it is given: the offsets of its index are 32 bits
    static constexpr uint64_t kHardMaxBytes = 0xFFFFFFF0u;

    /// A log that holds at most `max_bytes` (the packed turns and the 4 bytes of index of each: bytes()), and never more than kHardMaxBytes
    explicit TurnLog(size_t max_bytes = kDefaultMaxBytes) noexcept;

    /// Appends turn number turns(): turns must come in order, one after the other, starting with 0. False, and the log is not usable from now on (and keeps what it had), when the
    /// turn is not the next one (a hole or a repeat), when it would take the log beyond its limit (bytes() would pass max_bytes()), or when it is too big to be sent as a batch on
    /// its own (more than kMaxMessageBytes - kBatchHeaderBytes packed: no message could carry it). Commands above kMaxTurnCommands are cut, as encode(TurnMsg) cuts them. Once the
    /// log is not usable, nothing is appended and nothing is allocated.
    bool append(const TurnMsg& turn);

    /// The turns stored: the next turn to append
    uint32_t turns() const noexcept { return static_cast<uint32_t>(offsets_.size()); }
    /// What the log holds: the packed turns and their index (4 bytes a turn). Never above max_bytes()
    size_t bytes() const noexcept { return blob_.size() + offsets_.size() * sizeof(uint32_t); }
    size_t max_bytes() const noexcept { return max_bytes_; }
    /// False once an append was refused: the log has a hole and serves no replay
    bool usable() const noexcept { return usable_; }

    /// The packed size of turns [from, to) in bytes (the index is not counted): what a stream of them costs, without its batch headers. Turns beyond turns() count for nothing.
    size_t bytes_between(uint32_t from, uint32_t to) const noexcept;
    /// Appends the packed turns from `from` on to `out`: at most `max_turns` of them, and, after the first one, at most `limit_bytes` of packed bytes in all. At least one turn is
    /// appended when there is one at `from` (and max_turns is not 0), even when that turn alone is bigger than limit_bytes. Returns how many turns were appended (0 when `from` is
    /// at or beyond turns()). The bytes are exactly what encode_turn_batch_packed() takes. Reads what is stored whether or not the log is still usable.
    uint32_t read(uint32_t from, uint32_t max_turns, size_t limit_bytes, std::vector<uint8_t>& out) const;

private:
    size_t max_bytes_;
    bool usable_{true};
    std::vector<uint8_t> blob_;
    std::vector<uint32_t> offsets_;     // offsets_[t]: where turn t starts in blob_
};

}  // namespace ants::net
