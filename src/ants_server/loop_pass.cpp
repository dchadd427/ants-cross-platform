#include "ants_server/loop_pass.hpp"

#include <chrono>
#include <cstdio>

#include "ants_net/clock.hpp"

namespace ants::server {

namespace {

// "5.1": milliseconds with one decimal
std::string ms_text(int64_t us) {
    char text[32];
    std::snprintf(text, sizeof text, "%.1f", static_cast<double>(us) / 1000.0);
    return text;
}

// "rooms 5.1, store 21.0": the parts that took 0.05 ms or more, in the order of PassPart
std::string parts_text(const std::array<int64_t, LoopPass::kParts>& parts) {
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (parts[i] < 50) continue;
        if (!out.empty()) out += ", ";
        out += LoopPass::name(static_cast<PassPart>(i));
        out += " " + ms_text(parts[i]);
    }
    return out;
}

}  // namespace

const char* LoopPass::name(PassPart part) {
    switch (part) {
        case PassPart::Rooms: return "rooms";
        case PassPart::Starts: return "starts";
        case PassPart::Bots: return "bots";
        case PassPart::Store: return "store";
        case PassPart::History: return "history";
        case PassPart::Flush: return "flush";
        case PassPart::Doors: return "doors";
        case PassPart::Other: return "other";
        case PassPart::Count: break;
    }
    return "?";
}

int64_t LoopPass::now_us() const {
    if (clock_) return clock_();
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void SlowPassLog::hide(const LoopPass& pass) {
    ++hidden_;
    if (pass.total_us() > worst_us_) {
        worst_us_ = pass.total_us();
        worst_parts_ = pass.parts();
    }
}

std::string SlowPassLog::end(uint32_t now_ms, const LoopPass& pass) {
    const bool slow = pass.total_us() > threshold_us_;
    if (armed_ && !net::time_reached(now_ms, next_ms_)) {            // (a line was written lately: the slow passes wait to be counted)
        if (slow) hide(pass);
        return std::string();
    }
    if (hidden_ > 0) {                                               // (the time is over: the passes that were slow meanwhile are told, with the longest of them)
        const std::string line = "slow pass: " + std::to_string(hidden_) + " more in the last " + std::to_string(every_ms_ / 1000) + " s, the longest " + ms_text(worst_us_) + " ms (" + parts_text(worst_parts_) + ")";
        hidden_ = 0;
        worst_us_ = 0;
        next_ms_ = now_ms + every_ms_;
        if (slow) hide(pass);
        return line;
    }
    if (!slow) return std::string();
    armed_ = true;
    next_ms_ = now_ms + every_ms_;
    return "slow pass: " + ms_text(pass.total_us()) + " ms (" + parts_text(pass.parts()) + ")";
}

}  // namespace ants::server
