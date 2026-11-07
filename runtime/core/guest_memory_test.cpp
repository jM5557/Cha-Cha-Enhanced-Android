// GuestMemory: commit, and write tracking - a write to a watched page faults once (read-only page), the handler tells
// writes from reads (x86-64: the error code; arm64: ESR's WnR bit) and the page shows up as written; reads don't.
// Runs on x86-64 and arm64 (CI: also under qemu-aarch64), with the host's page size.
#include "guest_memory.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

using namespace mh3u::core;

static int failures = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                 \
        }                                                               \
    } while (0)

int main() {
    GuestMemory mem;
    constexpr uint32_t kA = 0x10600000, kSize = 0x40000;  // 256 KB: whole pages for 4, 16 and 64 KB hosts
    CHECK(!mem.committed(kA));
    CHECK(mem.commit(kA, kSize));
    CHECK(mem.committed(kA) && mem.committed(kA + kSize - 1));
    std::memset(mem.base() + kA, 0x11, kSize);
    mem.set_watched({{kA, kSize}});
    const auto settle = [&] {  // a few scanner passes
        const uint32_t e = mem.current_epoch();
        for (int i = 0; i < 400 && mem.current_epoch() < e + 3; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    };
    settle();
    uint32_t seen = mem.current_epoch();
    // reads of watched (read-only) pages must not count, nor fault fatally
    volatile uint8_t sum = 0;
    for (uint32_t a = kA; a < kA + kSize; a += 512) sum = static_cast<uint8_t>(sum + mem.base()[a]);
    settle();
    CHECK(!mem.written_since(kA, kSize, seen));
    seen = mem.current_epoch();
    // a write: faults once, the page counts as written; the rest of the range does not
    mem.base()[kA + kSize - 8] = 0x22;
    settle();
    CHECK(mem.base()[kA + kSize - 8] == 0x22);
    CHECK(mem.written_since(kA + kSize - 8, 1, seen));
    CHECK(!mem.written_since(kA, 0x1000, seen));
    // a second write to the same page later is seen again
    seen = mem.current_epoch();
    settle();
    mem.base()[kA + kSize - 4] = 0x33;
    settle();
    CHECK(mem.written_since(kA + kSize - 4, 1, seen));
    std::printf("%llu pages stamped in %llu passes\n", static_cast<unsigned long long>(mem.scanned_pages()),
                static_cast<unsigned long long>(mem.scans()));
    std::printf(failures ? "%d failure(s)\n" : "all checks passed\n", failures);
    return failures ? 1 : 0;
}
