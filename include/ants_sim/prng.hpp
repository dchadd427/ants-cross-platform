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
     * @brief Direct state accessors for serialization and deterministic replay verification.
     */
    constexpr uint32_t get_state() const noexcept { return state_; }

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
