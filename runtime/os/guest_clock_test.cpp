// The guest clock (hle.h): runs with the host clock, stands still while paused, and does not jump when it resumes -
// the time spent paused (Android: in the background) is left out, so the game's timers don't count it.
#include "hle.h"

#include <chrono>
#include <cstdio>
#include <thread>

using namespace mh3u::os;
using namespace std::chrono_literals;

static int failures = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                 \
        }                                                               \
    } while (0)

int main() {
    const int64_t t0 = guest_clock_ns();
    std::this_thread::sleep_for(50ms);
    const int64_t t1 = guest_clock_ns();
    CHECK(t1 - t0 >= 45'000'000);  // runs while not paused
    CHECK(!guest_clock_paused());

    set_guest_clock_paused(true);
    set_guest_clock_paused(true);  // twice: still one pause
    const int64_t p0 = guest_clock_ns();
    std::this_thread::sleep_for(200ms);
    const int64_t p1 = guest_clock_ns();
    CHECK(guest_clock_paused());
    CHECK(p1 == p0);  // stands still

    // a thread waiting for the pause to end
    bool woke = false;
    std::thread waiter([&] {
        wait_while_guest_paused();
        woke = true;
    });
    std::this_thread::sleep_for(20ms);
    CHECK(!woke);
    set_guest_clock_paused(false);
    waiter.join();
    CHECK(woke);
    const int64_t r0 = guest_clock_ns();
    CHECK(r0 >= p1 && r0 - p1 < 20'000'000);  // carries on from where it stopped: no 200 ms jump
    std::this_thread::sleep_for(50ms);
    CHECK(guest_clock_ns() - r0 >= 45'000'000);
    wait_while_guest_paused();  // not paused: returns at once

    std::printf(failures ? "%d failure(s)\n" : "all checks passed\n", failures);
    return failures ? 1 : 0;
}
