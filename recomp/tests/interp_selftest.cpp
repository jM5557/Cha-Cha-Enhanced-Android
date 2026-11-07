// Interpreter self-test (recomp/support/interpreter.cpp): the hand-encoded self-test functions (selftest_cases.h) are
// placed in guest memory and every one is run twice from the same state - recompiled (the emitted C++ from
// emit_selftest_gen) and interpreted - and the two results must agree register for register and byte for byte.
// Then the patched-code path: a function flagged as patched runs from memory (an edited instruction, a jump into a
// code cave and back), and runs recompiled again once the flag is cleared.
#include "ppc_context.h"
#include "selftest_cases.h"

#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

namespace ppc = mh3u::ppc;
using namespace selftest;

PPC_FUNC(sub_01000000);
PPC_FUNC(sub_01001000);
PPC_FUNC(sub_01002000);
PPC_FUNC(sub_01003000);
PPC_FUNC(sub_01004000);
PPC_FUNC(sub_01005000);
PPC_FUNC(sub_01006000);
PPC_FUNC(sub_01007000);
PPC_FUNC(sub_01008000);
PPC_FUNC(sub_01009000);
PPC_FUNC(sub_0100A000);
PPC_FUNC(sub_0100D000);
PPC_FUNC(sub_0100E000);
PPC_FUNC(sub_0100F000);
PPC_FUNC(sub_01010000);
PPC_FUNC(sub_01011000);
PPC_FUNC(sub_01012000);
PPC_FUNC(sub_01013000);
PPC_FUNC(sub_01014100);
PPC_FUNC(sub_01015000);
PPC_FUNC(sub_01016000);
PPC_FUNC(import_test_hle) { ctx.r[3] = 1234; }

static int failures = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                 \
        }                                                               \
    } while (0)

constexpr uint32_t kMemSize = 0x01020000;  // test data at 0x100..0x9000, code at 0x01000000.., a cave at 0x01018000

static bool same(const ppc::Context& x, const ppc::Context& y, std::string& what) {
    char b[96];
    for (int i = 0; i < 32; ++i)
        if (x.r[i] != y.r[i]) { std::snprintf(b, sizeof b, "r%d %08X vs %08X", i, x.r[i], y.r[i]); what = b; return false; }
    for (int i = 0; i < 32; ++i)
        if (std::memcmp(&x.f[i], &y.f[i], sizeof x.f[i]) != 0) { std::snprintf(b, sizeof b, "f%d", i); what = b; return false; }
    for (int i = 0; i < 8; ++i)
        if (std::memcmp(&x.cr[i], &y.cr[i], sizeof x.cr[i]) != 0) { std::snprintf(b, sizeof b, "cr%d", i); what = b; return false; }
    if (x.lr != y.lr) { what = "lr"; return false; }
    if (x.ctr != y.ctr) { what = "ctr"; return false; }
    if (x.xer_so != y.xer_so || x.xer_ov != y.xer_ov || x.xer_ca != y.xer_ca) { what = "xer"; return false; }
    if (x.reserve_valid != y.reserve_valid) { what = "reservation"; return false; }
    return true;
}

int main() {
    std::vector<uint8_t> mem(kMemSize);
    uint8_t* base = mem.data();
    for (const Case& c : cases()) {  // the code, as the game's code sits in guest memory
        for (size_t i = 0; i < c.words.size(); ++i) ppc::st32(base, c.addr + uint32_t(4 * i), c.words[i]);
        for (const auto& [a, w] : c.chunks)
            for (size_t i = 0; i < w.size(); ++i) ppc::st32(base, a + uint32_t(4 * i), w[i]);
    }
    const uint32_t site = kImportCall, target = 0xC0000000;  // the import stub in emit_selftest_gen's import table
    ppc::set_interpreter_branch_targets(&site, &target, 1);

    // test data the functions read (as in emit_selftest)
    const auto fill = [&](uint8_t* m) {
        const uint8_t src[] = {0xA1, 0xB2, 0xC3, 0xD4, 0x11, 0x22, 0x33, 0x44};
        std::memcpy(m + 0x100, src, sizeof src);
        ppc::stf32(m, 0x200, 3.0f);
        ppc::stf64(m, 0x208, 0.5);
        ppc::stf32(m, 0x400, 1.5f);
        ppc::stf64(m, 0x408, 2.25);
        ppc::st32(m, 0x600, 41);
        std::memcpy(m + 0x700, "Hello!", 6);
    };

    struct Run {
        uint32_t entry;
        ppc::GuestFunction native;
    };
    const Run runs[] = {{kSumLoop, sub_01000000}, {kRotMask, sub_01001000}, {kCarry64, sub_01002000},
                        {kMaxSigned, sub_01003000}, {kMemory, sub_01004000}, {kFloat, sub_01005000},
                        {kDivBy16, sub_01006000}, {kDivw, sub_01007000}, {kRecordReturn, sub_01008000},
                        {kPsLanes, sub_01009000}, {kCaller, sub_0100A000}, {kImportCall, sub_0100D000},
                        {kLrSaver, sub_0100E000}, {kSwitch, sub_0100F000}, {kIndirect, sub_01010000},
                        {kAtomicInc, sub_01011000}, {kLswi, sub_01012000}, {kMulti, sub_01013000},
                        {kMultiBefore, sub_01014100}, {kFall, sub_01015000}, {kTrapNot, sub_01016000}};

    // states: the hand-picked inputs of emit_selftest, then random registers with r1/r3/r4 pointing at test data
    std::vector<ppc::Context> states;
    const auto st = [&](std::initializer_list<std::pair<int, uint32_t>> regs) {
        ppc::Context c{};
        c.r[1] = 0x8000;
        c.lr = 0xDEAD0000;
        for (auto [i, v] : regs) c.r[i] = v;
        states.push_back(c);
    };
    st({{3, 10}});
    st({{3, 0x12345678}, {4, 0xFFFFFFFF}});
    st({{3, 1}, {4, 0xFFFFFFFF}, {5, 0}, {6, 1}});
    st({{3, uint32_t(-5)}, {4, 3}});
    st({{3, 0x100}});
    st({{3, 0x200}, {4, 0x300}});
    st({{3, uint32_t(-17)}});
    st({{3, 5}, {4, 0}});
    st({{3, 0}});
    st({{3, 1}});
    st({{3, 2}});
    st({{3, 5}});
    st({{3, 0x500}, {4, 0x400}});
    st({{3, 0x600}});
    st({{3, 0x700}});
    std::mt19937 rng(1234);
    for (int k = 0; k < 64; ++k) {
        ppc::Context c{};
        for (auto& r : c.r) r = rng();
        c.r[1] = 0x8000;
        c.r[3] = (rng() & 1) ? (rng() & 0x7F) : 0x100 + (rng() & 0x3F0);  // small counts / divisors or test data
        c.r[4] = 0x300 + (rng() & 0xF0);  // also an address for some functions
        c.lr = 0xDEAD0000;
        c.xer_ca = rng() & 1;
        for (int i = 0; i < 8; ++i) c.cr[i] = {uint8_t(rng() & 1), uint8_t(rng() & 1), uint8_t(rng() & 1), 0};
        states.push_back(c);
    }

    std::vector<uint8_t> mem2(kMemSize);
    int compared = 0;
    for (const Run& run : runs) {
        for (const ppc::Context& s : states) {
            // kSumLoop counts r3 down through CTR: keep it short
            if (run.entry == kSumLoop && (s.r[3] == 0 || s.r[3] > 200)) continue;
            // functions that use r3 / r4 as addresses need them inside the test memory
            const bool addresses = run.entry == kMemory || run.entry == kFloat || run.entry == kPsLanes ||
                                   run.entry == kAtomicInc || run.entry == kLswi;
            if (addresses && (s.r[3] >= 0xF000 || s.r[4] >= 0xF000)) continue;
            // the trap test traps (by design) when r3 == 99
            if (run.entry == kTrapNot && s.r[3] == 99) continue;
            ppc::Context a = s, b = s;
            std::memset(base, 0, 0x10000);
            fill(base);
            std::memcpy(mem2.data(), base, kMemSize);
            run.native(a, base);
            ppc::interpret(b, mem2.data(), run.entry);
            std::string what;
            if (!same(a, b, what)) {
                std::printf("FAIL %08X: recompiled and interpreted differ in %s\n", run.entry, what.c_str());
                ++failures;
            } else if (std::memcmp(base, mem2.data(), 0x10000) != 0) {
                std::printf("FAIL %08X: recompiled and interpreted wrote different memory\n", run.entry);
                ++failures;
            }
            ++compared;
        }
    }

    // patched code: kCallee (addi r3,r3,10; blr) flagged as patched runs from memory
    ppc::Context ctx{};
    ctx.r[3] = 5;
    sub_0100A000(ctx, base);
    CHECK(ctx.r[3] == 16 && ppc::g_patched_functions == 0);  // recompiled: +10 +1
    ppc::st32(base, kCallee, 0x38630014);  // addi r3,r3,20
    ctx.r[3] = 5;
    sub_0100A000(ctx, base);
    CHECK(ctx.r[3] == 16);  // not flagged: the recompiled body still runs
    ppc::set_function_patched(kCallee, true);
    CHECK(ppc::g_patched_functions == 1 && ppc::function_patched(kCallee) && !ppc::function_patched(kCaller));
    ctx.r[3] = 5;
    sub_0100A000(ctx, base);  // the recompiled caller calls the patched callee: interpreted
    CHECK(ctx.r[3] == 26);
    CHECK(ctx.r[4] == 0x0100A004);  // the tail-called function still sees the bl's return address
    // a jump into a code cave and back: kCallee: b cave | cave: addi r3,r3,100; b kCallee+4 (the blr)
    constexpr uint32_t kCave = 0x01018000;
    ppc::st32(base, kCallee, B(int32_t(kCave - kCallee)));
    ppc::st32(base, kCave, 0x38630064);
    ppc::st32(base, kCave + 4, B(int32_t((kCallee + 4) - (kCave + 4))));
    ctx.r[3] = 5;
    sub_0100A000(ctx, base);
    CHECK(ctx.r[3] == 106);
    // a call into a cave: kCallee: mflr r0; bl cave2; mtlr r0; blr | cave2: addi r3,r3,7; blr
    constexpr uint32_t kCave2 = 0x01018100;
    ppc::st32(base, kCallee, SPR(0, 8, 339));
    ppc::st32(base, kCallee + 4, B(int32_t(kCave2 - (kCallee + 4)), 1));
    ppc::st32(base, kCallee + 8, SPR(0, 8, 467));
    ppc::st32(base, kCallee + 12, BLR);
    ppc::st32(base, kCave2, 0x38630007);
    ppc::st32(base, kCave2 + 4, BLR);
    ctx.r[3] = 5;
    sub_0100A000(ctx, base);
    CHECK(ctx.r[3] == 13);
    ppc::set_function_patched(kCallee, false);
    CHECK(ppc::g_patched_functions == 0 && !ppc::function_patched(kCallee));
    ctx.r[3] = 5;
    sub_0100A000(ctx, base);
    CHECK(ctx.r[3] == 16);  // recompiled again
    CHECK(ppc::lookup_function(kCallee) != nullptr && ppc::lookup_function(kCave) == nullptr);

    std::printf("%d recompiled/interpreted runs compared\n", compared);
    std::printf(failures ? "%d failure(s)\n" : "all checks passed\n", failures);
    return failures ? 1 : 0;
}
