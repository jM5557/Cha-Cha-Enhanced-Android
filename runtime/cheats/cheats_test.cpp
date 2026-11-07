// Cheats (cheats.h): the file format, every code type, and the engine end to end - a cheat patches an instruction of
// a recompiled function (the emitter self-test functions, recomp/tests/selftest_cases.h) and jumps into a code cave;
// the function then runs interpreted, and recompiled again once the cheat is off. Also reads the bundled MH3U cheat
// file (assets/cheats): every cheat must compile, and every word its codes write into the code region or a cave must
// be an instruction the interpreter runs (or is reported as data).
// Usage: cheats_test [bundled cheat file]
#include "cheats.h"
#include "espresso.h"
#include "selftest_cases.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

PPC_FUNC(sub_0100A000);
PPC_FUNC(import_test_hle) { ctx.r[3] = 1234; }

namespace ppc = mh3u::ppc;
using namespace mh3u::cheats;

static int failures = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                 \
        }                                                               \
    } while (0)

// sparse big-endian memory for the code-type tests; [lo, hi) is mapped
struct FakeMem : Memory {
    std::map<uint32_t, uint8_t> bytes;
    uint32_t lo = 0, hi = 0xFFFFFFFF;
    bool valid(uint32_t a, uint32_t n) override { return a >= lo && uint64_t{a} + n <= hi; }
    uint8_t read8(uint32_t a) override { return bytes.count(a) ? bytes[a] : 0; }
    uint16_t read16(uint32_t a) override { return static_cast<uint16_t>(read8(a) << 8 | read8(a + 1)); }
    uint32_t read32(uint32_t a) override { return uint32_t{read16(a)} << 16 | read16(a + 2); }
    void write8(uint32_t a, uint8_t v) override { bytes[a] = v; }
    void write16(uint32_t a, uint16_t v) override { write8(a, uint8_t(v >> 8)), write8(a + 1, uint8_t(v)); }
    void write32(uint32_t a, uint32_t v) override { write16(a, uint16_t(v >> 16)), write16(a + 2, uint16_t(v)); }
};

static bool run(const std::vector<std::string>& lines, FakeMem& m, std::string* err = nullptr) {
    Program p;
    std::string e;
    if (!p.compile(lines, e)) {
        if (err) *err = e;
        return false;
    }
    return p.run(m, err);
}

static void file_format() {
    const std::string text =
        "junk before any cheat\n"
        "[First]\n{one line note}\n*cemu_enabled\n02000000 60000000\n\n"
        "[Second]\n{notes that\n  span lines}\n00000010 00000001\n"
        "[Third]\n*citra_enabled\n";
    auto cs = parse(text);
    CHECK(cs.size() == 3);
    CHECK(cs[0].name == "First" && cs[0].notes == "one line note" && cs[0].enabled && cs[0].code.size() == 1);
    CHECK(cs[1].notes == "notes that\nspan lines" && !cs[1].enabled && cs[1].code[0] == "00000010 00000001");
    CHECK(cs[2].enabled && cs[2].code.empty());
    auto again = parse(serialize(cs));
    CHECK(again.size() == 3 && again[1].notes == cs[1].notes && again[0].enabled && !again[1].enabled);
    cs[0].notes = "has } brace";
    CHECK(parse(serialize(cs))[0].notes == "has ) brace");
}

static void code_types() {
    FakeMem m;
    CHECK(run({"00001000 11223344", "10001004 0000AABB", "20001006 000000CC"}, m));
    CHECK(m.read32(0x1000) == 0x11223344 && m.read16(0x1004) == 0xAABB && m.read8(0x1006) == 0xCC);
    // offset register reaches the heap
    CHECK(run({"D3000000 10000000", "00123450 0000270F"}, m) && m.read32(0x10123450) == 9999);
    // conditions: a false one skips to its D0, nested ones count
    m.write32(0x2000, 5);
    CHECK(run({"50002000 00000005", "00002004 00000001", "D0000000 00000000", "60002000 00000005", "00002008 00000001",
               "50002000 00000005", "0000200C 00000001", "D0000000 00000000", "D0000000 00000000", "00002010 00000001"}, m));
    CHECK(m.read32(0x2004) == 1 && m.read32(0x2008) == 0 && m.read32(0x200C) == 0 && m.read32(0x2010) == 1);
    CHECK(run({"30002000 00000006", "00002014 00000001", "D2000000 00000000", "40002000 00000006", "00002018 00000001",
               "D2000000 00000000"}, m));
    CHECK(m.read32(0x2014) == 1 && m.read32(0x2018) == 0);
    // 16-bit compare with a mask of bits to ignore
    m.write16(0x2100, 0x12FF);
    CHECK(run({"90002100 00FF1200", "00002104 00000001", "D0000000 00000000"}, m) && m.read32(0x2104) == 1);
    // pointer: offset = [X]
    m.write32(0x3000, 0x00004000);
    CHECK(run({"B0003000 00000000", "00000010 0000ABCD", "D2000000 00000000", "00000010 00000001"}, m));
    CHECK(m.read32(0x4010) == 0xABCD && m.read32(0x10) == 1);  // D2 cleared the offset
    // loop with data writes: D6 advances the offset
    CHECK(run({"D3000000 00005000", "D5000000 00000007", "C0000000 00000003", "D6000000 00000000", "D4000000 00000001",
               "D2000000 00000000"}, m));
    CHECK(m.read32(0x5000) == 7 && m.read32(0x5004) == 8 && m.read32(0x5008) == 9 && m.read32(0x500C) == 0);
    // loads, DC, 16/8-bit stores
    m.write32(0x6000, 0x01020304);
    CHECK(run({"D9000000 00006000", "DC000000 00000100", "D7000000 00006000", "D8000000 00006000"}, m));
    CHECK(m.read16(0x6100) == 0x0304 && m.read8(0x6102) == 0x04);
    // copy: E with 10 bytes over two data lines
    CHECK(run({"E0007000 0000000A", "11223344 55667788", "99AABBCC DDEEFF00"}, m));
    CHECK(m.read32(0x7000) == 0x11223344 && m.read16(0x7008) == 0x99AA && m.read8(0x700A) == 0);
    // errors
    std::string err;
    CHECK(!run({"DD000000 00000010"}, m, &err) && err.find("DD") != std::string::npos);
    CHECK(!run({"0200000 1"}, m, &err) && err.find("line 1") != std::string::npos);
    CHECK(!run({"E0001000 00000010", "00000000 00000000"}, m, &err) && err.find("data line") != std::string::npos);
    m.lo = 0x1000;
    CHECK(!run({"00000010 00000001"}, m, &err) && err.find("not mapped") != std::string::npos);
}

static void engine_end_to_end() {
    using namespace selftest;
    constexpr uint32_t kEnd = 0x01020000;
    std::vector<uint8_t> mem(kCaveEnd);  // up to the end of the cave region
    uint8_t* base = mem.data();
    std::vector<ppc::FunctionRange> ranges;
    for (const Case& c : cases()) {
        for (size_t i = 0; i < c.words.size(); ++i) ppc::st32(base, c.addr + uint32_t(4 * i), c.words[i]);
        ranges.push_back({c.addr, c.addr + uint32_t(4 * c.words.size()), c.addr});
        for (const auto& [a, w] : c.chunks) {
            for (size_t i = 0; i < w.size(); ++i) ppc::st32(base, a + uint32_t(4 * i), w[i]);
            ranges.push_back({a, a + uint32_t(4 * w.size()), c.addr});
        }
    }
    std::sort(ranges.begin(), ranges.end(), [](auto& x, auto& y) { return x.start < y.start; });
    std::vector<uint8_t> pristine(base + 0x01000000, base + kEnd);
    Engine e;
    e.attach(base, [&](uint32_t a, uint32_t n) { return uint64_t{a} + n <= mem.size(); }, 0x01000000, pristine, ranges.data(), ranges.size());

    auto cs = parse(
        "[Callee adds 20]\n*cemu_enabled\n0100B000 38630014\n"
        "[Callee jumps into a cave]\n"  // b 0x01800000 | cave: addi r3,r3,100; b 0x0100B004 (the blr)
        "0100B000 48000000\n01800000 38630064\n01800004 4B80B000\n");
    // the branches: patch in the real displacements
    cs[1].code[0] = "0100B000 " + [] { char b[12]; std::snprintf(b, sizeof b, "%08X", B(int32_t(0x01800000 - kCallee))); return std::string(b); }();
    cs[1].code[2] = "01800004 " + [] { char b[12]; std::snprintf(b, sizeof b, "%08X", B(int32_t((kCallee + 4) - 0x01800004))); return std::string(b); }();
    ppc::Context ctx{};
    const auto call = [&] {
        ctx.r[3] = 5;
        sub_0100A000(ctx, base);
        return ctx.r[3];
    };
    CHECK(call() == 16);
    e.set_cheats(cs);
    CHECK(e.running() == 1 && call() == 16);  // nothing written before the first frame
    e.run_frame();
    CHECK(ppc::function_patched(kCallee) && !ppc::function_patched(kCaller) && call() == 26);
    e.run_frame();  // unchanged writes: still patched, nothing else flagged
    CHECK(ppc::g_patched_functions == 1 && call() == 26);
    cs[0].enabled = false;
    cs[1].enabled = true;
    e.set_cheats(cs);  // the first cheat's instruction is put back at once
    CHECK(ppc::ld32(base, kCallee) == 0x3863000A && !ppc::function_patched(kCallee) && call() == 16);
    e.run_frame();
    CHECK(ppc::function_patched(kCallee) && call() == 106);
    e.set_enabled(false);  // master switch: original code, recompiled again
    CHECK(ppc::ld32(base, kCallee) == 0x3863000A && ppc::g_patched_functions == 0 && call() == 16);
    e.run_frame();
    CHECK(call() == 16);
    e.set_enabled(true);
    e.run_frame();
    CHECK(call() == 106);
    cs[1].enabled = false;
    e.set_cheats(cs);
    CHECK(ppc::g_patched_functions == 0 && call() == 16 && e.running() == 0);
    // a cheat whose code does not parse is reported, not run
    auto bad = parse("[Broken]\n*cemu_enabled\n0100B000 zz\n");
    e.set_cheats(bad);
    CHECK(e.running() == 0 && e.problems().count("Broken"));
}

// The bundled file: everything compiles; words written into code / caves decode as instructions the interpreter has
static void bundled(const char* path) {
    const auto cs = load(path);
    if (cs.empty()) {
        std::printf("bundled cheat file %s not found or empty\n", path);
        ++failures;
        return;
    }
    size_t words = 0, unknown = 0, data = 0;
    for (const Cheat& c : cs) {
        Program p;
        std::string err;
        if (!p.compile(c.code, err)) {
            std::printf("FAIL bundled '%s': %s\n", c.name.c_str(), err.c_str());
            ++failures;
            continue;
        }
        uint32_t offset = 0;
        for (const std::string& l : c.code) {
            uint32_t x = 0, y = 0;
            if (std::sscanf(l.c_str(), "%8x %8x", &x, &y) != 2) continue;
            if ((x >> 24) == 0xD3) offset = y;
            if ((x >> 24) == 0xD2) offset = 0;
            if ((x >> 28) != 0) continue;
            const uint32_t a = (x & 0x0FFFFFFF) + offset;
            if (a < kCaveBase || a >= 0x10000000) continue;  // data
            ++words;
            if (ppc::interpretable(y)) continue;
            if (!ppc::decode(y).valid()) {
                ++data;  // not an instruction at all: a constant the cave code reads (a timer, a count)
                continue;
            }
            ++unknown;
            std::printf("FAIL '%s' writes %08X at %08X: %s, which the interpreter doesn't run\n", c.name.c_str(), y, a,
                        ppc::format(ppc::decode(y), a).c_str());
        }
    }
    std::printf("bundled: %zu cheats compile; %zu words written to code or caves: %zu instructions the interpreter runs, %zu data, "
                "%zu unsupported\n", cs.size(), words, words - data - unknown, data, unknown);
    if (unknown) ++failures;
}

int main(int argc, char** argv) {
    file_format();
    code_types();
    engine_end_to_end();
    if (argc > 1) bundled(argv[1]);
    std::printf(failures ? "%d failure(s)\n" : "all checks passed\n", failures);
    return failures ? 1 : 0;
}
