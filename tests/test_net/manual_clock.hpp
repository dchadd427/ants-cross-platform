// The clocks of the prediction's budget (Prediction::Config::wall_clock and cpu_clock), moved by hand: a timed block costs exactly what the work hook of the test says, on every platform and
// however busy the machine is. A spin on the real clocks cannot say it: Windows' thread clock counts in ticks of about 15.6 ms, charges a whole tick to the thread that runs when it falls and
// reads nothing for a thread that was stalled, so on a busy runner a block of 25 ms of work was charged anything from nothing to 60 ms, and the tests of the strikes failed now and then.
#pragma once

#include <cstdint>
#include <functional>

struct ManualClock {
    uint64_t wall{0};
    uint64_t cpu{0};

    void work(uint64_t ns) { move(ns, ns); }            // the thread computes: both clocks move
    void wait(uint64_t ns) { move(ns, 0); }             // it sleeps, or the process is stalled: time goes by, the CPU time does not
    void move(uint64_t wall_ns, uint64_t cpu_ns) {      // (a clock that counts in ticks reads a whole tick after a short block)
        wall += wall_ns;
        cpu += cpu_ns;
    }

    std::function<uint64_t()> wall_clock() { return [this]() { return wall; }; }
    std::function<uint64_t()> cpu_clock() { return [this]() { return cpu; }; }
    // For a Prediction::Config: the clocks of this object (it must outlive the prediction)
    template <class Config>
    void install(Config& config) {
        config.wall_clock = wall_clock();
        config.cpu_clock = cpu_clock();
    }
};
