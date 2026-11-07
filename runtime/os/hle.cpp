#include "hle.h"

#include "exp_heap.h"

#include <chrono>
#include <condition_variable>
#include <mutex>

namespace mh3u::os {

namespace {
std::mutex g_clock_m;
std::condition_variable g_clock_cv;
int64_t g_paused_total = 0;  // ns spent paused before the current pause
int64_t g_pause_start = -1;  // host ns (since boot) when the current pause began, -1 = running
int64_t host_ns() {
    static const auto boot = std::chrono::steady_clock::now();  // on first use: safe from other files' initialisers
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - boot).count();
}
}  // namespace

int64_t guest_clock_ns() {
    std::lock_guard g(g_clock_m);
    return (g_pause_start >= 0 ? g_pause_start : host_ns()) - g_paused_total;  // stands still while paused
}

void set_guest_clock_paused(bool paused) {
    {
        std::lock_guard g(g_clock_m);
        if (paused && g_pause_start < 0) {
            g_pause_start = host_ns();
        } else if (!paused && g_pause_start >= 0) {
            g_paused_total += host_ns() - g_pause_start;
            g_pause_start = -1;
        } else {
            return;
        }
    }
    if (!paused) g_clock_cv.notify_all();
}

bool guest_clock_paused() {
    std::lock_guard g(g_clock_m);
    return g_pause_start >= 0;
}

void wait_while_guest_paused() {
    std::unique_lock l(g_clock_m);
    g_clock_cv.wait(l, [] { return g_pause_start < 0; });
}

Runtime& rt() {
    static Runtime r;
    return r;
}

uint32_t hle_function_address(ppc::GuestFunction fn) {
    static std::mutex m;
    static uint32_t next = core::layout::kHleFunctionBase;
    std::lock_guard g(m);
    uint32_t a = next;
    next += 4;
    ppc::register_indirect(a, fn);
    return a;
}

}  // namespace mh3u::os
