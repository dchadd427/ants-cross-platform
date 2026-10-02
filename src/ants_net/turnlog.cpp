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
    if (budget_ != nullptr) budget_->give(bytes());
}

void TurnLog::release() noexcept {
    usable_ = false;
    if (budget_ != nullptr) budget_->give(bytes());
    std::vector<uint8_t>().swap(blob_);              // (swapped with empty ones: a clear() keeps the memory)
    std::vector<uint32_t>().swap(offsets_);
}

bool TurnLog::append(const TurnMsg& turn) {
    if (!usable_) return false;
    if (turn.turn != turns()) {                     // a hole or a repeat: what the log holds is no longer the match
        usable_ = false;
        return false;
    }
    const size_t commands = std::min(turn.commands.size(), kMaxTurnCommands);
    uint64_t need = 2;                               // the u16 count of commands
    for (size_t i = 0; i < commands; ++i) need += packed_command_bytes(turn.commands[i]);
    // every sum is made in 64 bits (a 32-bit size_t, as in a browser, must not wrap): the turn must fit in a batch of its own, in the offsets, and in the limit
    const uint64_t blob_after = uint64_t{blob_.size()} + need;
    const uint64_t bytes_after = blob_after + (uint64_t{offsets_.size()} + 1u) * sizeof(uint32_t);
    if (need > kMaxMessageBytes - kBatchHeaderBytes || blob_after > kHardMaxBytes || bytes_after > max_bytes_) {
        usable_ = false;
        return false;
    }
    if (budget_ != nullptr && !budget_->take(need + sizeof(uint32_t))) {     // the server's memory for logs is used up: this log is not kept (and nothing was taken)
        usable_ = false;
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
