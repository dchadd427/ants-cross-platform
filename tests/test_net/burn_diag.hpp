// DIAGNOSTIC ONLY, NOT FOR MERGING: what the thread clock and the wall clock do while the tests spin on a CI runner (the Windows prediction-budget flake).
// burn_cpu records every spin; the log is written to burn_diag_<name>.txt in the current folder when the program ends, and the CI job prints it.
#pragma once

#include "ants_net/prediction.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace burn_diag {

struct Step {
    uint64_t at_ns;     // wall time since the start of the spin
    uint64_t inc_ns;    // the thread clock moved by this much
};

struct Rec {
    uint64_t req_ns{0};
    uint64_t wall_ns{0};
    uint64_t spent_ns{0};
    int outcome{0};              // 0: the spin ended as it should, 1: it gave up (the clock stood still for 100 ms of wall time)
    uint64_t started_ns{0};      // since the log was made
    std::vector<Step> steps;
};

inline uint64_t ns_between(std::chrono::steady_clock::time_point a, std::chrono::steady_clock::time_point b) {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count());
}

inline std::string timer_info() {
    std::string out;
#if defined(_WIN32)
    DWORD adjustment = 0;
    DWORD increment = 0;
    BOOL disabled = FALSE;
    if (GetSystemTimeAdjustment(&adjustment, &increment, &disabled) != 0) {
        char buf[200];
        std::snprintf(buf, sizeof buf, "GetSystemTimeAdjustment: adjustment=%lu increment=%lu (100 ns units) disabled=%d", static_cast<unsigned long>(adjustment),
                      static_cast<unsigned long>(increment), static_cast<int>(disabled));
        out += buf;
    }
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    out += " processors=" + std::to_string(static_cast<unsigned long>(si.dwNumberOfProcessors));
    LARGE_INTEGER freq;
    if (QueryPerformanceFrequency(&freq) != 0) out += " qpc_hz=" + std::to_string(static_cast<long long>(freq.QuadPart));
#else
    out = "(not Windows)";
#endif
    return out;
}

class Log {
public:
    explicit Log(std::string name) : name_(std::move(name)), t0_(std::chrono::steady_clock::now()) {
        notes_.push_back("timer: " + timer_info());
    }
    ~Log() { dump(); }

    uint64_t since_start() const { return ns_between(t0_, std::chrono::steady_clock::now()); }
    void add(Rec&& r) { recs_.push_back(std::move(r)); }
    void note(const std::string& text) { notes_.push_back(ms(since_start()) + " ms: " + text); }

    static std::string ms(uint64_t ns) {
        char buf[40];
        std::snprintf(buf, sizeof buf, "%.2f", static_cast<double>(ns) / 1e6);
        return buf;
    }

    static std::string quantiles(std::vector<uint64_t> v) {
        if (v.empty()) return "-";
        std::sort(v.begin(), v.end());
        const auto at = [&](double q) { return v[std::min(v.size() - 1, static_cast<size_t>(q * static_cast<double>(v.size())))]; };
        return "min " + ms(v.front()) + " p50 " + ms(at(0.5)) + " p90 " + ms(at(0.9)) + " p99 " + ms(at(0.99)) + " max " + ms(v.back());
    }

    void dump() {
        if (dumped_) return;
        dumped_ = true;
        std::ofstream f("burn_diag_" + name_ + ".txt");
        if (!f) return;
        f << "== burn_diag " << name_ << ": " << recs_.size() << " spins, log lasted " << ms(since_start()) << " ms\n";
        for (const std::string& n : notes_) f << "  note " << n << "\n";
        std::vector<uint64_t> reqs;
        for (const Rec& r : recs_) {
            if (std::find(reqs.begin(), reqs.end(), r.req_ns) == reqs.end()) reqs.push_back(r.req_ns);
        }
        std::sort(reqs.begin(), reqs.end());
        for (uint64_t req : reqs) {
            std::vector<uint64_t> wall, spent, gaps, incs;
            size_t n = 0, gave_up = 0, stalled = 0;
            for (const Rec& r : recs_) {
                if (r.req_ns != req) continue;
                ++n;
                gave_up += static_cast<size_t>(r.outcome == 1);
                wall.push_back(r.wall_ns);
                spent.push_back(r.spent_ns);
                if (r.wall_ns > req + 40ull * 1000ull * 1000ull) ++stalled;
                uint64_t prev = 0;
                for (const Step& s : r.steps) {
                    gaps.push_back(s.at_ns - prev);
                    incs.push_back(s.inc_ns);
                    prev = s.at_ns;
                }
            }
            f << "request " << ms(req) << " ms: n=" << n << " gave_up=" << gave_up << " stalled(wall>req+40ms)=" << stalled << "\n";
            f << "    wall  ms: " << quantiles(wall) << "\n";
            f << "    spent ms: " << quantiles(spent) << "\n";
            f << "    wall gap before each clock change, ms: " << quantiles(gaps) << "\n";
            f << "    size of each clock change, ms: " << quantiles(incs) << "\n";
        }
        // every spin that gave up or that saw something odd: a gap of 50 ms or more, a change of the clock of 20 ms or more, or a wall time far over the request
        size_t listed = 0;
        for (const Rec& r : recs_) {
            bool odd = r.outcome == 1 || r.wall_ns > r.req_ns + 40ull * 1000ull * 1000ull;
            uint64_t prev = 0;
            for (const Step& s : r.steps) {
                if (s.at_ns - prev >= 50ull * 1000ull * 1000ull || s.inc_ns >= 20ull * 1000ull * 1000ull) odd = true;
                prev = s.at_ns;
            }
            if (!odd || listed >= 60) continue;
            ++listed;
            f << "ODD at " << ms(r.started_ns) << " ms: request " << ms(r.req_ns) << " wall " << ms(r.wall_ns) << " spent " << ms(r.spent_ns) << (r.outcome == 1 ? " GAVE UP" : "") << " steps:";
            for (const Step& s : r.steps) f << " [" << ms(s.at_ns) << " +" << ms(s.inc_ns) << "]";
            f << "\n";
        }
    }

private:
    std::string name_;
    std::chrono::steady_clock::time_point t0_;
    std::vector<Rec> recs_;
    std::vector<std::string> notes_;
    bool dumped_{false};
};

inline Log*& slot() {
    static Log* log = nullptr;
    return log;
}

inline void start(const std::string& name) {
    static Log log(name);
    slot() = &log;
}

// The spin of the tests (the same loop), with a record of what the clocks did
inline void burn(uint64_t ns) {
    const uint64_t t0 = ants::net::thread_cpu_ns();
    const auto wall0 = std::chrono::steady_clock::now();
    Rec r;
    r.req_ns = ns;
    r.started_ns = slot() != nullptr ? slot()->since_start() : 0;
    uint64_t last = 0;
    for (;;) {
        const uint64_t spent = ants::net::thread_cpu_ns() - t0;
        const auto wall = std::chrono::steady_clock::now() - wall0;
        const uint64_t wall_ns = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(wall).count());
        if (spent - last >= 500000ull) {      // (a change of half a millisecond or more: Windows' clock moves in ticks, Linux's in nanoseconds, which would fill the memory)
            r.steps.push_back(Step{wall_ns, spent - last});
            last = spent;
        }
        if (spent >= ns && wall >= std::chrono::nanoseconds(static_cast<std::chrono::nanoseconds::rep>(ns))) {
            r.outcome = 0;
            r.wall_ns = wall_ns;
            r.spent_ns = spent;
            break;
        }
        if (spent == 0 && wall > std::chrono::milliseconds(100)) {
            r.outcome = 1;
            r.wall_ns = wall_ns;
            r.spent_ns = spent;
            break;
        }
    }
    if (slot() != nullptr) slot()->add(std::move(r));
}

}  // namespace burn_diag
