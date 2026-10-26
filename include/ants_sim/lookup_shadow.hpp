// Test-only shadow check of the simulation's lookups (SimulationEngineImpl::find_unit, PathManager::has_request_for, the per-tile ant lists behind occ_scan and
// blast, and the event heap behind next_ant_event). Without ANTS_SIM_LOOKUP_SHADOW (every product build) this header is empty and the lookups carry no check. The library ants_sim_shadow
// (tests/test_sim/CMakeLists.txt) compiles the simulation with it: every call then also runs the linear scan that the lookup replaced and stops the program when the two
// answers differ; the lists themselves are rebuilt from the ants and compared as well; and a test can switch a thread to the scans alone (the old lookups) to play one
// match with each and compare the state hashes.
#pragma once

#ifdef ANTS_SIM_LOOKUP_SHADOW
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace ants::sim {

/// True: the lookups of this thread answer with the scan alone (the code before the index). The default is false.
inline bool& lookup_shadow_linear_only() noexcept {
    static thread_local bool linear_only = false;
    return linear_only;
}

/// The number of calls that were compared with the scan, so that a test can show that it checked something.
inline uint64_t& lookup_shadow_checks() noexcept {
    static thread_local uint64_t checks = 0;
    return checks;
}

/// The calls of occ_scan and of blast that were compared with their scans, the ants that those blasts threw, and the checks of the whole lists against the ants: so that a
/// test can show that piles of ants were met
inline uint64_t& lookup_shadow_scan_checks() noexcept {
    static thread_local uint64_t checks = 0;
    return checks;
}
inline uint64_t& lookup_shadow_blast_checks() noexcept {
    static thread_local uint64_t checks = 0;
    return checks;
}
inline uint64_t& lookup_shadow_blast_ants() noexcept {
    static thread_local uint64_t ants = 0;
    return ants;
}
inline uint64_t& lookup_shadow_list_checks() noexcept {
    static thread_local uint64_t checks = 0;
    return checks;
}

/// The number of find_unit calls whose slot did not hold the id (they fell back to the scan; id 0 is not counted): none in a real match, so a test can show that the slot is what answers.
inline uint64_t& lookup_shadow_fallbacks() noexcept {
    static thread_local uint64_t fallbacks = 0;
    return fallbacks;
}

/// Whether this call is compared with the scan. Every call is, while the scan is short (up to kFullCheckSize ants or queued requests); with more, every 64th call, so that a
/// match of thousands of ants costs about twice the scan engine, not a hundred times (the answers of the calls in between are the same code).
constexpr size_t kFullCheckSize = 256;
inline bool lookup_shadow_sample(size_t size) noexcept {
    static thread_local uint64_t calls = 0;
    return size <= kFullCheckSize || (++calls & 63u) == 0;
}

/// The selections of the next ant event that were compared with the scan over every ant (EVERY selection is, whatever the size of the table: the scan costs what the engine of
/// the twin that runs on scans alone costs), and the checks of the whole heap against the ants' fields (at the start and end of every tick and after every copy): so that a test
/// can show that the heap was asked and checked, not only that nothing differed
inline uint64_t& lookup_shadow_event_checks() noexcept {
    static thread_local uint64_t checks = 0;
    return checks;
}
/// The events that the heap gave (in the compared selections), by kind (AntEventKind: burn, combat timer, animation frame, pause): so that a test can show that every kind was met
inline uint64_t& lookup_shadow_event_picks(size_t kind) noexcept {
    static thread_local uint64_t picks[4] = {0, 0, 0, 0};
    return picks[kind & 3u];
}
inline uint64_t& lookup_shadow_event_verifies() noexcept {
    static thread_local uint64_t checks = 0;
    return checks;
}
/// The ants that the start-of-tick pass looked at (it must find nothing to do on every one of them: it stops the program otherwise)
inline uint64_t& lookup_shadow_event_catch_ups() noexcept {
    static thread_local uint64_t checks = 0;
    return checks;
}

[[noreturn]] inline void lookup_shadow_fail(const char* lookup, uint32_t id) {
    std::fprintf(stderr, "lookup shadow check failed: %s(%u) differs from the linear scan\n", lookup, static_cast<unsigned>(id));
    std::abort();
}

} // namespace ants::sim
#endif
