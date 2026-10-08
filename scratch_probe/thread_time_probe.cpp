// A probe of how Windows charges CPU time to a thread that works in short passes and reads GetThreadTimes between them (what S3.71 of test_server does), with other processes on the machine
// that spin, sleep or wait on high-resolution timers (what the test programs beside it do).
//
//   probe all SECONDS                 every scenario below, three rounds, SECONDS each
//   probe scenario NAME SECONDS       the instrument beside the load of NAME (alone, burn3, hires3, sleep3, mix)
//   probe burn SECONDS                (a helper) spins
//   probe hires SECONDS               (a helper) works 40 us and waits 0.55 ms on a high-resolution timer, as ants_test::short_pause() does
//   probe sleep SECONDS               (a helper) works 40 us and std::this_thread::sleep_for(300 us), as the tests did before
//
// The instrument works in passes of a timed section (60 us, the server's update) and an untimed one (150 us, the clients). For every section it reads the thread's CPU time three ways: the
// ticks of GetThreadTimes, the cycles of QueryThreadCycleTime (which the clock interrupt does not touch) and the wall clock, and says which sections were charged much more than the thread
// could have used ("phantom") and which lasted much longer than they work ("stall").
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <realtimeapiset.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

double qpc_ms() {
    static const double freq = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return static_cast<double>(f.QuadPart);
    }();
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return static_cast<double>(t.QuadPart) * 1000.0 / freq;
}

// the same reading as tests/test_server/test_server.cpp: kernel and user time of the calling thread, milliseconds
double thread_ms() {
    FILETIME created, exited, kernel, user;
    if (!GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user)) return -1.0;
    const auto hundred_ns = [](const FILETIME& t) { return static_cast<double>((static_cast<uint64_t>(t.dwHighDateTime) << 32) | t.dwLowDateTime); };
    return (hundred_ns(kernel) + hundred_ns(user)) / 10000.0;
}

uint64_t thread_cycles() {
    ULONG64 c = 0;
    QueryThreadCycleTime(GetCurrentThread(), &c);
    return c;
}

// ---- the clock of the fix: a copy of thread_cycles_per_ms() and thread_cpu_ms() of tests/test_server/test_server.cpp -----------------------------------------------------------------

double thread_cycles_per_ms() {
    static const double rate = []() {
        double best = 0.0;
        for (int i = 0; i < 8; ++i) {
            ULONG64 begin = 0;
            ULONG64 end = 0;
            const auto t0 = std::chrono::steady_clock::now();
            if (!QueryThreadCycleTime(GetCurrentThread(), &begin)) return 0.0;
            volatile uint64_t spin = 0;
            while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(6)) spin = spin + 1;
            if (!QueryThreadCycleTime(GetCurrentThread(), &end)) return 0.0;
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            best = std::max(best, static_cast<double>(end - begin) / ms);
        }
        return best;
    }();
    return rate;
}

double fix_cpu_ms() {
    const double cycles_per_ms = thread_cycles_per_ms();
    ULONG64 cycles = 0;
    if (cycles_per_ms > 0.0 && QueryThreadCycleTime(GetCurrentThread(), &cycles)) return static_cast<double>(cycles) / cycles_per_ms;
    FILETIME created, exited, kernel, user;
    if (!GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user)) return -1.0;
    const auto hundred_ns = [](const FILETIME& t) { return static_cast<double>((static_cast<uint64_t>(t.dwHighDateTime) << 32) | t.dwLowDateTime); };
    return (hundred_ns(kernel) + hundred_ns(user)) / 10000.0;
}

volatile uint32_t g_sink = 0;

void spin_for_ms(double ms) {
    const double end = qpc_ms() + ms;
    while (qpc_ms() < end) g_sink = g_sink + 1;
}

struct Resolution {
    unsigned long min{0}, max{0}, cur{0};
};

Resolution timer_resolution() {
    using Query = LONG(NTAPI*)(PULONG, PULONG, PULONG);
    static const Query query = reinterpret_cast<Query>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryTimerResolution")));
    Resolution r;
    if (query != nullptr) query(&r.min, &r.max, &r.cur);
    return r;
}

// ---- the helpers -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------

int run_burn(double seconds) {
    spin_for_ms(seconds * 1000.0);
    return 0;
}

int run_hires(double seconds) {
    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    const double end = qpc_ms() + seconds * 1000.0;
    unsigned long long pauses = 0;
    while (qpc_ms() < end) {
        spin_for_ms(0.04);
        if (timer != nullptr) {
            LARGE_INTEGER due;
            due.QuadPart = -5500;                                   // 0.55 ms, as short_pause() waits
            SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE);
            WaitForSingleObject(timer, INFINITE);
        } else {
            std::this_thread::yield();
        }
        ++pauses;
    }
    std::printf("  helper hires: %llu pauses, timer %s\n", pauses, timer != nullptr ? "high-resolution" : "missing");
    return 0;
}

int run_sleep(double seconds) {
    const double end = qpc_ms() + seconds * 1000.0;
    unsigned long long pauses = 0;
    while (qpc_ms() < end) {
        spin_for_ms(0.04);
        std::this_thread::sleep_for(std::chrono::microseconds(300));
        ++pauses;
    }
    std::printf("  helper sleep: %llu pauses\n", pauses);
    return 0;
}

// ---- the instrument --------------------------------------------------------------------------------------------------------------------------------------------------------------------------

struct Stats {
    long sections[2]{};                    // [0] untimed, [1] timed
    double charged[2]{}, cycles[2]{}, wall[2]{};
    long bucket[2][7]{};                   // by the ticks one section was charged: 0, 1, 2, 3 to 4, 5 to 8, 9 to 15, 16 and more
    double max_charged[2]{}, max_wall[2]{};
    double min_nonzero{1e9};
    long phantom{0}, stalls{0}, migrations{0};
    double fix_total[2]{}, fix_max[2]{};
    long fix_phantom{0};
};

int bucket_of(double charged_ms) {
    if (charged_ms <= 0.0) return 0;
    const double ticks = charged_ms / 15.625;
    if (ticks < 1.5) return 1;
    if (ticks < 2.5) return 2;
    if (ticks < 4.5) return 3;
    if (ticks < 8.5) return 4;
    if (ticks < 15.5) return 5;
    return 6;
}

int run_measure(const std::string& name, int round, double seconds) {
    // the cycle counter in milliseconds: the thread spins 120 ms by the wall clock
    const uint64_t cy0 = thread_cycles();
    const double w0 = qpc_ms();
    spin_for_ms(120.0);
    const double cycles_per_ms = static_cast<double>(thread_cycles() - cy0) / (qpc_ms() - w0);
    const Resolution r0 = timer_resolution();

    Stats st;
    const double begin = qpc_ms();
    const double end = begin + seconds * 1000.0;
    double next_second = begin + 1000.0;
    int second = 0;
    double sec_charged0 = thread_ms();
    uint64_t sec_cycles0 = thread_cycles();
    unsigned last_cpu = GetCurrentProcessorNumber();
    int lumps_printed = 0;
    Resolution r_mid = r0;
    while (true) {
        for (int timed = 0; timed < 2; ++timed) {
            const double f0 = fix_cpu_ms();
            const double c0 = thread_ms();
            const uint64_t y0 = thread_cycles();
            const double t0 = qpc_ms();
            const unsigned p0 = GetCurrentProcessorNumber();
            spin_for_ms(timed == 0 ? 0.15 : 0.06);
            const double c1 = thread_ms();
            const uint64_t y1 = thread_cycles();
            const double t1 = qpc_ms();
            const unsigned p1 = GetCurrentProcessorNumber();
            const double f1 = fix_cpu_ms();
            const int kind = 1 - timed;                    // timed section: index 1 (the loop starts with the untimed one)
            const double charged = c1 - c0;
            const double cyc_ms = static_cast<double>(y1 - y0) / cycles_per_ms;
            const double wall_ms = t1 - t0;
            const double fixed_charge = f1 - f0;
            st.fix_total[kind] += fixed_charge;
            st.fix_max[kind] = std::max(st.fix_max[kind], fixed_charge);
            if (fixed_charge >= 30.0 && wall_ms < 5.0) ++st.fix_phantom;
            ++st.sections[kind];
            st.charged[kind] += charged;
            st.cycles[kind] += cyc_ms;
            st.wall[kind] += wall_ms;
            ++st.bucket[kind][bucket_of(charged)];
            st.max_charged[kind] = std::max(st.max_charged[kind], charged);
            st.max_wall[kind] = std::max(st.max_wall[kind], wall_ms);
            if (charged > 0.0) st.min_nonzero = std::min(st.min_nonzero, charged);
            if (p0 != last_cpu || p1 != p0) ++st.migrations;
            last_cpu = p1;
            const bool phantom = charged >= 30.0 && wall_ms < 5.0;
            const bool stall = wall_ms >= 30.0;
            if (phantom) ++st.phantom;
            if (stall) ++st.stalls;
            if ((phantom || stall || charged >= 62.0) && lumps_printed < 40) {
                ++lumps_printed;
                std::printf("  LUMP %s round=%d at=%.2fs section=%s charged=%.2f ms cycles=%.2f ms wall=%.2f ms cpu=%u->%u%s%s\n", name.c_str(), round, (t0 - begin) / 1000.0, timed ? "timed" : "untimed", charged, cyc_ms,
                            wall_ms, p0, p1, phantom ? " PHANTOM" : "", stall ? " STALL" : "");
            }
        }
        const double now = qpc_ms();
        if (now >= next_second) {
            ++second;
            next_second += 1000.0;
            const double c = thread_ms();
            const uint64_t y = thread_cycles();
            std::printf("  SEC %s round=%d t=%d charged=%.1f ms cycles=%.1f ms\n", name.c_str(), round, second, c - sec_charged0, static_cast<double>(y - sec_cycles0) / cycles_per_ms);
            sec_charged0 = c;
            sec_cycles0 = y;
            if (second == 5) r_mid = timer_resolution();
        }
        if (now >= end) break;
    }
    const double elapsed = (qpc_ms() - begin) / 1000.0;
    const Resolution r1 = timer_resolution();
    long lumps16[2] = {st.bucket[0][6], st.bucket[1][6]};
    std::printf("RESULT %s round=%d seconds=%.1f cycles_per_ms=%.0f min_nonzero_charge=%.3f ms\n", name.c_str(), round, elapsed, cycles_per_ms, st.min_nonzero >= 1e9 ? 0.0 : st.min_nonzero);
    for (int kind = 1; kind >= 0; --kind) {
        std::printf("RESULT %s round=%d %s sections=%ld charged_total=%.1f ms cycles_total=%.1f ms wall_total=%.1f ms max_charged=%.2f ms max_wall=%.2f ms buckets[0|1|2|3-4|5-8|9-15|16+]=%ld|%ld|%ld|%ld|%ld|%ld|%ld\n",
                    name.c_str(), round, kind ? "timed  " : "untimed", st.sections[kind], st.charged[kind], st.cycles[kind], st.wall[kind], st.max_charged[kind], st.max_wall[kind], st.bucket[kind][0],
                    st.bucket[kind][1], st.bucket[kind][2], st.bucket[kind][3], st.bucket[kind][4], st.bucket[kind][5], st.bucket[kind][6]);
    }
    std::printf("RESULT %s round=%d phantom=%ld stalls=%ld migrations=%ld timed_16plus=%ld untimed_16plus=%ld timer_resolution_100ns min/max/cur at start %lu/%lu/%lu, at 5 s %lu/%lu/%lu, at end %lu/%lu/%lu\n",
                name.c_str(), round, st.phantom, st.stalls, st.migrations, lumps16[1], lumps16[0], r0.min, r0.max, r0.cur, r_mid.min, r_mid.max, r_mid.cur, r1.min, r1.max, r1.cur);
    std::printf("RESULT %s round=%d FIX timed: total=%.1f ms max=%.3f ms | untimed: total=%.1f ms max=%.3f ms | fix_phantom=%ld\n", name.c_str(), round, st.fix_total[1], st.fix_max[1], st.fix_total[0], st.fix_max[0], st.fix_phantom);
    std::fflush(stdout);
    return 0;
}


// the checks that S3.71 now makes of its clock (a copy)
int run_fixed_check() {
    const double calib0 = qpc_ms();
    const double cpu_start = fix_cpu_ms();
    const double calibration_ms = qpc_ms() - calib0;
    const bool start_ok = cpu_start >= 0.0;
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    const double slept = fix_cpu_ms() - cpu_start;
    volatile uint64_t spin = 0;
    const auto give_up = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (fix_cpu_ms() - cpu_start < 30.0 && std::chrono::steady_clock::now() < give_up) spin = spin + 1;
    const double spun = fix_cpu_ms() - cpu_start;
    double finest_step_ms = 1.0e9;
    int steps = 0;
    const auto give_up_steps = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (double before = fix_cpu_ms(); steps < 20 && std::chrono::steady_clock::now() < give_up_steps;) {
        const double now = fix_cpu_ms();
        if (now > before) {
            finest_step_ms = std::min(finest_step_ms, now - before);
            before = now;
            ++steps;
        }
    }
    const bool ok = start_ok && slept < 30.0 && spun >= 30.0 && steps == 20 && finest_step_ms < 1.0;
    std::printf("CHECK %s: first call %.1f ms (rate %.0f cycles/ms), slept 60 ms reads %.3f ms, spun reads %.1f ms, %d steps, finest %.6f ms\\n", ok ? "OK" : "FAILED", calibration_ms, thread_cycles_per_ms(), slept, spun, steps, finest_step_ms);
    return ok ? 0 : 1;
}

// ---- the scenarios ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------

std::string self_path() {
    char buf[MAX_PATH * 2];
    const DWORD n = GetModuleFileNameA(nullptr, buf, sizeof buf);
    return std::string(buf, n);
}

int run_scenario(const std::string& name, int round, double seconds) {
    std::vector<std::string> helpers;
    if (name == "burn3") helpers = {"burn", "burn", "burn"};
    else if (name == "hires3") helpers = {"hires", "hires", "hires"};
    else if (name == "sleep3") helpers = {"sleep", "sleep", "sleep"};
    else if (name == "mix") helpers = {"burn", "hires", "hires"};
    else if (name != "alone") {
        std::fprintf(stderr, "unknown scenario %s\n", name.c_str());
        return 2;
    }
    std::vector<HANDLE> children;
    for (const std::string& h : helpers) {
        std::string cmd = "\"" + self_path() + "\" " + h + " " + std::to_string(seconds + 4.0);
        STARTUPINFOA si{};
        si.cb = sizeof si;
        PROCESS_INFORMATION pi{};
        if (!CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            std::fprintf(stderr, "cannot start a helper: %lu\n", GetLastError());
            return 3;
        }
        CloseHandle(pi.hThread);
        children.push_back(pi.hProcess);
    }
    std::printf("SCENARIO %s round=%d: %zu helpers\n", name.c_str(), round, helpers.size());
    if (!children.empty()) Sleep(2000);                          // (the helpers are all running)
    const int rc = run_measure(name, round, seconds);
    for (HANDLE c : children) {
        WaitForSingleObject(c, 20000);
        CloseHandle(c);
    }
    return rc;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: probe all|scenario|burn|hires|sleep ...\n");
        return 2;
    }
    const std::string mode = argv[1];
    if (mode == "burn") return run_burn(std::atof(argv[2]));
    if (mode == "hires") return run_hires(std::atof(argv[2]));
    if (mode == "sleep") return run_sleep(std::atof(argv[2]));
    if (mode == "scenario" && argc >= 4) return run_scenario(argv[2], 1, std::atof(argv[3]));
    if (mode == "fixedall") {
        const double seconds = std::atof(argv[2]);
        int rc = run_fixed_check();
        const char* order[2][3] = {{"alone", "burn3", "mix"}, {"mix", "burn3", "alone"}};
        for (int round = 0; round < 2; ++round) {
            for (const char* name : order[round]) {
                if (run_scenario(name, round + 1, seconds) != 0) return 1;
            }
        }
        rc |= run_fixed_check();
        return rc;
    }
    if (mode == "all") {
        const double seconds = std::atof(argv[2]);
        const char* order[3][5] = {{"alone", "burn3", "hires3", "sleep3", "mix"}, {"mix", "sleep3", "hires3", "burn3", "alone"}, {"hires3", "alone", "mix", "burn3", "sleep3"}};
        for (int round = 0; round < 3; ++round) {
            for (const char* name : order[round]) {
                if (run_scenario(name, round + 1, seconds) != 0) return 1;
            }
        }
        return 0;
    }
    std::fprintf(stderr, "usage: probe all|scenario|burn|hires|sleep ...\n");
    return 2;
}
