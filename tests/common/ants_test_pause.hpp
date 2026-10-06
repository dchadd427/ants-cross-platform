// The short pause of the tests' pump loops (a step of game time, then a moment for the kernel to deliver the loopback bytes). std::this_thread::sleep_for cannot give a few hundred
// microseconds on every runner: it lasts 0.37 ms on Linux, 1.5 to 2.7 ms on a macOS runner (timers are coalesced) and 15.6 ms on Windows (one timer tick). Here a Windows thread waits
// on a high-resolution timer (0.55 ms), a macOS thread is a time-constraint thread for the length of the pause, which makes its timers exact (0.32 ms), and Linux sleeps.
#pragma once

#include <chrono>
#include <cstdint>
#include <thread>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/thread_policy.h>
#endif

namespace ants_test {

namespace pause_detail {
#if defined(_WIN32)
struct Timer {
    HANDLE handle{CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS)};   // (null before Windows 10 1803: short_pause() then yields)
    Timer() = default;
    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;
    ~Timer() {
        if (handle != nullptr) CloseHandle(handle);
    }
};
#elif defined(__APPLE__)
// The thread asks for the time-constraint policy only while it sleeps (two calls of 1.5 us) and has its own policy again when it computes: a thread that is a time-constraint thread for good
// would keep its core against the other test programs of a run, which share three cores.
struct Realtime {
    mach_port_t self{mach_thread_self()};
    thread_time_constraint_policy_data_t policy{};
    Realtime() {
        mach_timebase_info_data_t base;
        mach_timebase_info(&base);
        const auto ticks = [&base](double ns) { return static_cast<uint32_t>(ns * base.denom / base.numer); };
        policy.period = ticks(10e6);              // 10 ms period, 0.5 ms of work, due within 1 ms: the numbers only have to be possible, the thread sleeps nearly always
        policy.computation = ticks(0.5e6);
        policy.constraint = ticks(1e6);
        policy.preemptible = 1;
    }
    Realtime(const Realtime&) = delete;
    Realtime& operator=(const Realtime&) = delete;
    ~Realtime() { mach_port_deallocate(mach_task_self(), self); }
    bool begin() { return thread_policy_set(self, THREAD_TIME_CONSTRAINT_POLICY, reinterpret_cast<thread_policy_t>(&policy), THREAD_TIME_CONSTRAINT_POLICY_COUNT) == KERN_SUCCESS; }   // (a refusal leaves a plain sleep)
    void end() {
        thread_standard_policy_data_t standard;
        standard.no_data = 0;
        thread_policy_set(self, THREAD_STANDARD_POLICY, reinterpret_cast<thread_policy_t>(&standard), THREAD_STANDARD_POLICY_COUNT);
    }
};
#endif
}  // namespace pause_detail

// Waits for about `d` (a few hundred microseconds) of real time.
inline void short_pause(std::chrono::microseconds d = std::chrono::microseconds(300)) {
#if defined(_WIN32)
    thread_local pause_detail::Timer timer;
    if (timer.handle != nullptr) {
        LARGE_INTEGER due;
        due.QuadPart = -10 * static_cast<LONGLONG>(d.count());           // 100 ns units, negative: from now
        if (SetWaitableTimer(timer.handle, &due, 0, nullptr, nullptr, FALSE) && WaitForSingleObject(timer.handle, INFINITE) == WAIT_OBJECT_0) return;
    }
    const auto until = std::chrono::steady_clock::now() + d;
    while (std::chrono::steady_clock::now() < until) std::this_thread::yield();
#elif defined(__APPLE__)
    thread_local pause_detail::Realtime realtime;
    const bool exact = realtime.begin();
    std::this_thread::sleep_for(d);
    if (exact) realtime.end();
#else
    std::this_thread::sleep_for(d);
#endif
}

}  // namespace ants_test
