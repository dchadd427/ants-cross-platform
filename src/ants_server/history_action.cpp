#include "ants_server/history_action.hpp"

#include <algorithm>

#include "ants_ctl/json.hpp"

namespace ants::server {

namespace {

using ctl::JsonValue;
using replay::MatchStats;

constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";     // (the 64 letters of history_deaths.cpp)

int letter_value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '-') return 62;
    if (c == '_') return 63;
    return -1;
}

constexpr MatchStats::Action kKinds[MatchStats::kActionKinds] = {MatchStats::Action::Death, MatchStats::Action::Bomb, MatchStats::Action::Theft, MatchStats::Action::Fire, MatchStats::Action::Hit};

}  // namespace

std::string make_history_action(const MatchStats& st) {
    if (!st.ok) return std::string();
    uint64_t events = 0;
    for (size_t k = 0; k < MatchStats::kActionKinds; ++k) {
        for (const uint32_t n : st.action[k]) events += n;
    }
    if (events == 0) return std::string();
    // the buckets of 5 seconds that the match has (the last tick of a match that ends on a bucket's edge is counted in its last bucket), and how many of them one bucket of the field holds
    const uint64_t base = std::max<uint64_t>((static_cast<uint64_t>(st.turns) + MatchStats::kSampleTurns - 1) / MatchStats::kSampleTurns, 1);
    uint32_t factor = 1;
    while ((base + factor - 1) / factor > kActionMaxBuckets) factor *= 2;
    const size_t n = static_cast<size_t>((base + factor - 1) / factor);
    JsonValue o = JsonValue::make_object();
    o.set("v", JsonValue::make_int(kActionVersion));
    o.set("s", JsonValue::make_int(static_cast<int64_t>(kActionBaseSeconds) * factor));
    o.set("n", JsonValue::make_int(static_cast<int64_t>(n)));
    for (const MatchStats::Action kind : kKinds) {
        const std::vector<uint32_t>& series = st.action[static_cast<size_t>(kind)];
        std::vector<uint64_t> sums(n, 0);
        for (size_t b = 0; b < series.size(); ++b) sums[static_cast<size_t>(std::min<uint64_t>(b, base - 1) / factor)] += series[b];
        std::string letters;
        letters.reserve(n);
        for (const uint64_t sum : sums) letters.push_back(kAlphabet[std::min<uint64_t>(sum, kActionMaxCount)]);
        o.set(MatchStats::action_name(kind), JsonValue::make_string(letters));
    }
    return ctl::to_json(o);
}

bool parse_history_action(const std::string& text, ActionView& out) {
    out = ActionView();
    JsonValue v;
    ctl::JsonLimits limits;
    limits.max_bytes = 2u * kActionMaxBytes;
    limits.max_elements = 16;
    limits.max_depth = 2;
    if (!ctl::parse_json(text, v, nullptr, limits) || !v.is_object() || v.get("v").as_int_or(0) != kActionVersion) return false;
    const int64_t s = v.get("s").as_int_or(0), n = v.get("n").as_int_or(0);
    if (s < static_cast<int64_t>(kActionBaseSeconds) || s > static_cast<int64_t>(kActionMaxSeconds) || s % kActionBaseSeconds != 0 || n < 1 || n > static_cast<int64_t>(kActionMaxBuckets)) return false;
    for (const MatchStats::Action kind : kKinds) {
        const JsonValue& letters = v.get(MatchStats::action_name(kind));
        if (!letters.is_string() || letters.str().size() != static_cast<size_t>(n)) return false;
        std::vector<uint8_t>& counts = out.counts[static_cast<size_t>(kind)];
        counts.reserve(static_cast<size_t>(n));
        for (const char c : letters.str()) {
            const int value = letter_value(c);
            if (value < 0) return false;
            counts.push_back(static_cast<uint8_t>(value));
        }
    }
    out.seconds = static_cast<uint32_t>(s);
    out.buckets = static_cast<uint32_t>(n);
    return true;
}

}  // namespace ants::server
