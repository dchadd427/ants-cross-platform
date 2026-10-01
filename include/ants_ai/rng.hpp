#pragma once

// The random numbers of a bot seat. A bot never draws from the simulation's generator (that would change the game), never reads a clock and never
// uses std::random_device or a std::*_distribution (their results differ between libstdc++, libc++ and the Microsoft library): a seat owns a
// splitmix64 generator whose seed comes from the match seed, the seat and the kind of bot, so the same match is reproduced bit for bit everywhere.

#include <cstdint>
#include <string_view>

namespace ants::ai {

/// The finalizer of splitmix64: a bijective scramble of 64 bits
constexpr uint64_t mix64(uint64_t z) noexcept {
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

/// The seed of the generator of one seat: the same (match seed, seat, kind) always gives the same seed, any difference gives another one
constexpr uint64_t seat_seed(uint64_t match_seed, uint8_t seat, std::string_view kind) noexcept {
    uint64_t h = mix64(match_seed ^ 0xB07B07B07B07ull);
    h = mix64(h ^ (static_cast<uint64_t>(seat) + 1u) * 0x100000001B3ull);
    for (const char c : kind) h = mix64(h ^ static_cast<uint64_t>(static_cast<unsigned char>(c)));
    return h;
}

class BotRng {
public:
    explicit BotRng(uint64_t seed = 1) noexcept : state_(seed) {}
    uint64_t next() noexcept {
        state_ += 0x9E3779B97F4A7C15ull;
        return mix64(state_);
    }
    /// A number in [0, n); 0 when n is 0. (The remainder is biased by less than n / 2^64: of no importance for a delay in ticks.)
    uint32_t below(uint32_t n) noexcept { return n == 0 ? 0u : static_cast<uint32_t>(next() % n); }

private:
    uint64_t state_;
};

}  // namespace ants::ai
