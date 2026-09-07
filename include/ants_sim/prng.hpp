#pragma once

#include <cstdint>
#include <string>

namespace ants::sim {

/**
 * @brief Deterministic Linear Congruential Generator matching MSVC CRT rand()/srand().
 * 
 * Verified against Ants.exe disassembly at 0x10345b0 and 0x10345c0:
 *   holdrand = holdrand * 214013 + 2531011;
 *   return (holdrand >> 16) & 0x7FFF;
 */
class PRNG {
public:
    static constexpr uint32_t MULTIPLIER   = 214013u;
    static constexpr uint32_t INCREMENT    = 2531011u;
    static constexpr uint32_t MASK_15BIT   = 0x7FFFu;
    static constexpr uint16_t RAND_MAX_VAL = 32767u;

    explicit constexpr PRNG(uint32_t seed = 1u) noexcept : state_(seed) {}

    /**
     * @brief Re-seeds the generator state.
     */
    constexpr void srand(uint32_t seed) noexcept {
        state_ = seed;
    }

    /**
     * @brief Advances generator by one iteration and returns a 15-bit pseudo-random integer.
     * @return Integer in range [0, 32767].
     */
    constexpr uint16_t rand() noexcept {
        state_ = state_ * MULTIPLIER + INCREMENT;
        return static_cast<uint16_t>((state_ >> 16) & MASK_15BIT);
    }

    /**
     * @brief Alias for rand() to support MsvcPrng interface convention.
     */
    constexpr uint16_t next() noexcept {
        return rand();
    }

    /**
     * @brief Helper to generate an integer in the inclusive range [min_val, max_val].
     * Uses pure integer arithmetic to prevent floating-point divergence.
     */
    constexpr int32_t rand_range(int32_t min_val, int32_t max_val) noexcept {
        if (min_val >= max_val) return min_val;
        uint32_t range = static_cast<uint32_t>(max_val - min_val + 1);
        return min_val + static_cast<int32_t>(rand() % range);
    }

    /**
     * @brief Roll probability check: returns true if roll(1..100) <= percentage.
     */
    constexpr bool roll_chance(uint32_t percentage) noexcept {
        if (percentage == 0) return false;
        if (percentage >= 100) return true;
        return (static_cast<uint32_t>(rand() % 100u) + 1u) <= percentage;
    }

    /**
     * @brief Direct state accessors for serialization and deterministic replay verification.
     */
    constexpr uint32_t get_state() const noexcept { return state_; }
    constexpr void set_state(uint32_t state) noexcept { state_ = state; }

    /**
     * @brief Parses command-line latseed string (e.g. "latseed:12345" or "12345").
     * Returns a PRNG initialized with the parsed seed, or fallback_seed if invalid/empty.
     */
    static PRNG from_latseed(const std::string& latseed_str, uint32_t fallback_seed = 1u) noexcept {
        if (latseed_str.empty()) return PRNG(fallback_seed);
        size_t pos = latseed_str.find("latseed:");
        std::string num_str = (pos != std::string::npos) ? latseed_str.substr(pos + 8) : latseed_str;
        try {
            unsigned long s = std::stoul(num_str);
            return PRNG(static_cast<uint32_t>(s));
        } catch (...) {
            return PRNG(fallback_seed);
        }
    }

private:
    uint32_t state_{1u};
};

// Compatibility type alias for tests using MsvcPrng name
using MsvcPrng = PRNG;

} // namespace ants::sim
