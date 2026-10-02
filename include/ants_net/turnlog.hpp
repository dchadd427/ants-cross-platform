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
// A log that is not usable is DEAD: it frees what it holds at once (turns() and bytes() are 0 from then on) and gives it back to the budget: nothing can be streamed from it any more, and
// a room that was made to fill its log up (a hostile client) holds nothing of it afterwards.
//
// The server's budget. A server holds the logs of many rooms, and each of them may grow to its own limit: a LogBudget is the memory that all the logs of a server may take together
// (ants_server: 256 MiB). A log takes from the budget what its vectors have ALLOCATED (their capacity, not the bytes in them: capacity_bytes(); a vector that grows by doubling holds up to
// twice what it stores, so counting the size would let the logs take up to twice the budget) and gives all of it back when it is destroyed, released or dead. The log grows its vectors
// itself, by doubling, and an append whose growth the budget refuses tries a growth of a quarter before it gives up; an append that the budget refuses stores nothing and makes the
// log dead, exactly as the log's own limit does, so a server that is full of logs falls back to dropping a lost seat at once in the rooms that cannot log (and says so in their status) and
// never grows beyond the budget.
//
// Pure data, no clock, no sockets; the host's session (session.hpp) appends every turn it seals, before it sends it.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "ants_net/protocol.hpp"

namespace ants::net {

/// The memory that the logs of a server may take together (see above). One thread, like everything here: plain numbers.
class LogBudget {
public:
    explicit LogBudget(uint64_t limit_bytes) noexcept : limit_(limit_bytes) {}
    /// Takes `bytes` from the budget; false (and nothing is taken) when less than that is left
    bool take(uint64_t bytes) noexcept {
        if (bytes > limit_ - used_) return false;
        used_ += bytes;
        return true;
    }
    /// Gives `bytes` back (never more than was taken)
    void give(uint64_t bytes) noexcept { used_ = bytes >= used_ ? 0 : used_ - bytes; }
    uint64_t used() const noexcept { return used_; }
    uint64_t limit() const noexcept { return limit_; }

private:
    uint64_t limit_;
    uint64_t used_{0};
};

class TurnLog {
public:
    /// The limit of a room's log
    static constexpr size_t kDefaultMaxBytes = 16u * 1024u * 1024u;
    /// The most that a log ever holds, whatever limit it is given: the offsets of its index are 32 bits
    static constexpr uint64_t kHardMaxBytes = 0xFFFFFFF0u;
    /// The first allocation of a log: 1 KiB of packed turns and 128 entries of index (512 bytes). A budget of less than 1536 bytes keeps no log at all
    static constexpr uint64_t kFirstBlobBytes = 1024;
    static constexpr uint64_t kFirstOffsets = 128;

    /// A log that holds at most `max_bytes` (the packed turns and the 4 bytes of index of each: bytes()), and never more than kHardMaxBytes. With a `budget` (a server's, shared by the logs
    /// of all its rooms; it must outlive the log) everything that the log stores is taken from it, and given back when the log is destroyed or released.
    explicit TurnLog(size_t max_bytes = kDefaultMaxBytes, LogBudget* budget = nullptr) noexcept;
    ~TurnLog();
    TurnLog(const TurnLog&) = delete;               // (a copy would give the budget back twice)
    TurnLog& operator=(const TurnLog&) = delete;

    /// Appends turn number turns(): turns must come in order, one after the other, starting with 0. False, and the log is dead from now on (it frees everything that it held and gives it
    /// back to the budget), when the turn is not the next one (a hole or a repeat), when it would take the log beyond its limit (bytes() would pass max_bytes()), when the budget refuses
    /// the growth that it needs, or when the turn is too big to be sent as a batch on its own (more than kMaxMessageBytes - kBatchHeaderBytes packed: no message could carry it).
    /// Commands above kMaxTurnCommands are cut, as encode(TurnMsg) cuts them. Once the log is not usable, nothing is appended and nothing is allocated.
    bool append(const TurnMsg& turn);

    /// The turns stored: the next turn to append (0 again after release())
    uint32_t turns() const noexcept { return static_cast<uint32_t>(offsets_.size()); }
    /// What the log holds: the packed turns and their index (4 bytes a turn). Never above max_bytes()
    size_t bytes() const noexcept { return blob_.size() + offsets_.size() * sizeof(uint32_t); }
    /// What the log has allocated, and takes from the budget: the capacity of the two vectors (at least bytes(), at most twice it and a little more). 0 when the log is dead or released
    size_t capacity_bytes() const noexcept { return blob_.capacity() + offsets_.capacity() * sizeof(uint32_t); }
    size_t max_bytes() const noexcept { return max_bytes_; }
    /// False once an append was refused: the log has a hole and serves no replay
    bool usable() const noexcept { return usable_; }

    /// The packed size of turns [from, to) in bytes (the index is not counted): what a stream of them costs, without its batch headers. Turns beyond turns() count for nothing.
    size_t bytes_between(uint32_t from, uint32_t to) const noexcept;
    /// Appends the packed turns from `from` on to `out`: at most `max_turns` of them, and, after the first one, at most `limit_bytes` of packed bytes in all. At least one turn is
    /// appended when there is one at `from` (and max_turns is not 0), even when that turn alone is bigger than limit_bytes. Returns how many turns were appended (0 when `from` is
    /// at or beyond turns()). The bytes are exactly what encode_turn_batch_packed() takes. Reads what is stored whether or not the log is still usable.
    uint32_t read(uint32_t from, uint32_t max_turns, size_t limit_bytes, std::vector<uint8_t>& out) const;

    /// The match is over (or the log is dead): everything stored is freed and given back to the budget, and the log is not usable (it holds nothing: turns() and bytes() are 0, a read
    /// gives nothing, an append is refused)
    void release() noexcept;

private:
    bool grow(uint64_t blob_needed, uint64_t offsets_needed) noexcept;       // makes room for that many bytes of blob and entries of index, charging the budget: false if it cannot

    size_t max_bytes_;
    LogBudget* budget_;
    uint64_t charged_{0};               // the capacity of the vectors, as taken from the budget (when there is one)
    bool usable_{true};
    std::vector<uint8_t> blob_;
    std::vector<uint32_t> offsets_;     // offsets_[t]: where turn t starts in blob_
};

}  // namespace ants::net
