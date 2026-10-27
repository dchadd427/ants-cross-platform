// Test-only shadow check of the simulation's lookups (SimulationEngineImpl::find_unit, PathManager::has_request_for, the per-tile ant lists behind occ_scan and
// blast, and the event heap behind next_ant_event, and the marks of the ants that go_to refreshes). Without ANTS_SIM_LOOKUP_SHADOW (every product build) this header is empty and the lookups carry no check. The library ants_sim_shadow
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

/// The number of find_unit calls whose slot did not hold the id (they fell back to the scan; id 0 never falls back): none in a real match, so a test can show that the slot is what answers.
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
/// The ants that the start-of-tick pass looked at (it must find nothing to do on any of them: it stops the program otherwise)
inline uint64_t& lookup_shadow_event_catch_ups() noexcept {
    static thread_local uint64_t checks = 0;
    return checks;
}

/// The refreshes that go_to made from the marked ants alone (in a tick), those of them that were checked (an ant out of step that is not marked, or one left out of step, stops
/// the program), those that visited marked ants and were compared with the full refresh run from the same state, and the marked ants that they visited: so that a test can show
/// that the marks had something to do
inline uint64_t& lookup_shadow_go_to_calls() noexcept {
    static thread_local uint64_t calls = 0;
    return calls;
}
inline uint64_t& lookup_shadow_go_to_checks() noexcept {
    static thread_local uint64_t checks = 0;
    return checks;
}
inline uint64_t& lookup_shadow_go_to_compares() noexcept {
    static thread_local uint64_t compares = 0;
    return compares;
}
inline uint64_t& lookup_shadow_go_to_dirty_ants() noexcept {
    static thread_local uint64_t ants = 0;
    return ants;
}

/// The questions "does a team-mate claim this tile?" that the claim lists answered in a tick (calls), the ones whose answer was yes, those compared with the scan over every ant, the
/// checks of the whole lists against the ants' orders (at the end of a tick and after a copy), and the ants that the start-of-tick pass looked at; and the ants whose order the lists
/// had not been told of (0 after every ordinary match: the pass stops the program otherwise, unless a test that writes orders from outside counts them): so that a test can show that
/// the lists were asked, both answers were met, and nothing was put right by the net
inline uint64_t& lookup_shadow_claim_calls() noexcept {
    static thread_local uint64_t calls = 0;
    return calls;
}
inline uint64_t& lookup_shadow_claim_hits() noexcept {
    static thread_local uint64_t hits = 0;
    return hits;
}
inline uint64_t& lookup_shadow_claim_checks() noexcept {
    static thread_local uint64_t checks = 0;
    return checks;
}
inline uint64_t& lookup_shadow_claim_verifies() noexcept {
    static thread_local uint64_t verifies = 0;
    return verifies;
}
inline uint64_t& lookup_shadow_claim_catch_ups() noexcept {
    static thread_local uint64_t ants = 0;
    return ants;
}
inline uint64_t& lookup_shadow_claim_catch_up_changes() noexcept {
    static thread_local uint64_t changes = 0;
    return changes;
}

[[noreturn]] inline void lookup_shadow_fail(const char* lookup, uint32_t id) {
    std::fprintf(stderr, "lookup shadow check failed: %s(%u) differs from the linear scan\n", lookup, static_cast<unsigned>(id));
    std::abort();
}

/// The two nets that a product build has under the writers of the events (the start-of-tick pass, and the selection of the next event that finds an entry the ant does not bear
/// out) stop the program here, because in a match nothing may ever have to use them. Two tests write the fields of the events (and one the orders) from OUTSIDE the simulation on purpose (a test hook
/// that pokes an ant between two ticks, as no production code does), and set this to true for the thread: the nets then count what they put right (the counters below) and the
/// program goes on, so that the test shows that they see it and that the engine plays on as the scans do. The heap is still checked against the ants at the END of every tick: what
/// the nets left wrong stops the program. A COPY of an engine is checked strictly whatever this says (the copy's constructor verifies the heap and the lists against the ants), so a
/// test in this mode takes its copies after a tick has put its writes right, never between a write and the tick.
inline bool& lookup_shadow_count_stale_events() noexcept {
    static thread_local bool count = false;
    return count;
}
/// The ants whose events the start-of-tick pass had to put right, and the entries of the heap that a selection found the ant did not bear out (both are 0 after every ordinary match)
inline uint64_t& lookup_shadow_event_catch_up_changes() noexcept {
    static thread_local uint64_t changes = 0;
    return changes;
}
inline uint64_t& lookup_shadow_event_stale_entries() noexcept {
    static thread_local uint64_t entries = 0;
    return entries;
}
/// Counts one such thing, and stops the program unless the test above asked to have them counted
inline void lookup_shadow_stale_event(uint64_t& counter, const char* lookup, uint32_t id) {
    ++counter;
    if (!lookup_shadow_count_stale_events()) lookup_shadow_fail(lookup, id);
}

} // namespace ants::sim
#endif
