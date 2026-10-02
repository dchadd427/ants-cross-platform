#include "ants_net/turnlog.hpp"

#include <algorithm>

#include "ants_net/wire.hpp"

namespace ants::net {

namespace {

// What the command takes on the wire: sim::encode writes at most 255 ants (the decoder takes 32; an engine's own commands never name more than that)
size_t packed_command_bytes(const sim::Command& c) noexcept { return sim::kCommandHeaderBytes + 4 * std::min<size_t>(c.ants.size(), 255); }

}  // namespace

TurnLog::TurnLog(size_t max_bytes, LogBudget* budget) noexcept : max_bytes_(static_cast<size_t>(std::min<uint64_t>(max_bytes, kHardMaxBytes))), budget_(budget) {}

TurnLog::~TurnLog() {
    if (budget_ != nullptr) budget_->give(charged_);
}

// Everything stored is freed (swapped with empty vectors: a clear() keeps the memory) and what was charged goes back; the log serves nothing and takes nothing from now on
void TurnLog::release() noexcept {
    usable_ = false;
    if (budget_ != nullptr) budget_->give(charged_);
    charged_ = 0;
    std::vector<uint8_t>().swap(blob_);
    std::vector<uint32_t>().swap(offsets_);
}

// Room for `blob_needed` bytes of packed turns and `offsets_needed` entries of index. The vectors grow in steps (what a vector does, but chosen here, so that the budget is charged for exactly
// what is allocated): by doubling first, by a quarter when the budget cannot give that, and never by less than the first allocation (1 KiB of turns and 128 entries of index: 1.5 KiB).
// A step of a quarter keeps a log that has the budget's last bytes from copying itself at every turn. Nothing is changed when this is false.
bool TurnLog::grow(uint64_t blob_needed, uint64_t offsets_needed) noexcept {
    const uint64_t blob_cap = blob_.capacity();
    const uint64_t off_cap = offsets_.capacity();
    if (blob_needed <= blob_cap && offsets_needed <= off_cap) return true;
    const auto stepped = [](uint64_t needed, uint64_t cap, uint64_t floor, bool doubling) { return needed <= cap ? cap : std::max({needed, doubling ? cap * 2u : cap + cap / 4u, floor}); };
    for (const bool doubling : {true, false}) {
        const uint64_t new_blob = stepped(blob_needed, blob_cap, kFirstBlobBytes, doubling);
        const uint64_t new_off = stepped(offsets_needed, off_cap, kFirstOffsets, doubling);
        if (new_blob > kHardMaxBytes || new_off * sizeof(uint32_t) > kHardMaxBytes) continue;
        const uint64_t want = new_blob + new_off * sizeof(uint32_t);
        if (budget_ != nullptr && want > charged_ && !budget_->take(want - charged_)) continue;
        try {
            blob_.reserve(static_cast<size_t>(new_blob));
            offsets_.reserve(static_cast<size_t>(new_off));
        } catch (...) {                                                   // (out of memory: the same as a budget that says no)
            if (budget_ != nullptr && want > charged_) budget_->give(want - charged_);
            continue;
        }
        // a standard library may round a reservation up: what is really there is what is charged (and the budget may refuse the difference)
        const uint64_t actual = uint64_t{blob_.capacity()} + uint64_t{offsets_.capacity()} * sizeof(uint32_t);
        if (budget_ != nullptr) {
            if (actual > want && !budget_->take(actual - want)) {
                budget_->give(want - charged_);
                return false;
            }
            if (actual < want) budget_->give(want - actual);
        }
        charged_ = budget_ != nullptr ? actual : 0;
        return true;
    }
    return false;
}

bool TurnLog::append(const TurnMsg& turn) {
    if (!usable_) return false;
    if (turn.turn != turns()) {                     // a hole or a repeat: what the log holds is no longer the match
        release();
        return false;
    }
    const size_t commands = std::min(turn.commands.size(), kMaxTurnCommands);
    uint64_t need = 2;                               // the u16 count of commands
    for (size_t i = 0; i < commands; ++i) need += packed_command_bytes(turn.commands[i]);
    // every sum is made in 64 bits (a 32-bit size_t, as in a browser, must not wrap): the turn must fit in a batch of its own, in the offsets, and in the limit
    const uint64_t blob_after = uint64_t{blob_.size()} + need;
    const uint64_t bytes_after = blob_after + (uint64_t{offsets_.size()} + 1u) * sizeof(uint32_t);
    if (need > kMaxMessageBytes - kBatchHeaderBytes || blob_after > kHardMaxBytes || bytes_after > max_bytes_) {
        release();
        return false;
    }
    if (!grow(blob_after, uint64_t{offsets_.size()} + 1u)) {      // the server's memory for logs is used up (or the machine's): this log is not kept (and nothing was taken)
        release();
        return false;
    }
    offsets_.push_back(static_cast<uint32_t>(blob_.size()));
    ByteWriter w(blob_);
    w.u16(static_cast<uint16_t>(commands));
    for (size_t i = 0; i < commands; ++i) sim::encode(turn.commands[i], blob_);
    return true;
}

size_t TurnLog::bytes_between(uint32_t from, uint32_t to) const noexcept {
    to = std::min(to, turns());
    if (from >= to) return 0;
    const size_t end = to < turns() ? offsets_[to] : blob_.size();
    return end - offsets_[from];
}

uint32_t TurnLog::read(uint32_t from, uint32_t max_turns, size_t limit_bytes, std::vector<uint8_t>& out) const {
    if (from >= turns() || max_turns == 0) return 0;
    const size_t first_byte = offsets_[from];
    size_t end_byte = first_byte;
    uint32_t count = 0;
    for (uint32_t t = from; t < turns() && count < max_turns; ++t) {
        const size_t next = t + 1 < turns() ? offsets_[t + 1] : blob_.size();
        if (count > 0 && next - first_byte > limit_bytes) break;      // (the first turn always goes, however big it is)
        end_byte = next;
        ++count;
    }
    out.insert(out.end(), blob_.begin() + static_cast<std::ptrdiff_t>(first_byte), blob_.begin() + static_cast<std::ptrdiff_t>(end_byte));
    return count;
}

}  // namespace ants::net
