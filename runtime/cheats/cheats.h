// Cheats: Gateway-style codes in the per-title text files Citra / Azahar use, as Cemu-Enhanced reads them, so a
// cheat file made for it works here unchanged (docs/cheats.md):
//
//   <data folder>/cheats/0005000010118300.txt
//
//   [Cheat name]
//   {Optional notes, may span lines}
//   *cemu_enabled
//   02123450 38A00000
//
// Enabled cheats run once per frame on the game's graphics thread (GX2SwapScanBuffers). Most MH3U cheats patch
// PowerPC instructions and add code in caves (0x01800000-0x01FFFFFF). The game here is recompiled ahead of time, so
// the engine compares the code region with the executable after every run and flags each function whose instructions
// differ; flagged functions run from guest memory through the interpreter (ppc_context.h, "patched code"). Turning a
// cheat off puts the original instructions back, so the function is recompiled code again.
#pragma once

#include "ppc_context.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace mh3u::cheats {

constexpr uint64_t kTitleId = 0x0005000010118300ull;  // MH3U US: the executable Cha-Cha is built from
constexpr uint32_t kCaveBase = 0x01800000;              // code caves (as Cemu maps them for patches)
constexpr uint32_t kCaveEnd = 0x02000000;

// ---- the file -----------------------------------------------------------------------------------------------
struct Cheat {
    std::string name;
    std::string notes;
    bool enabled = false;
    std::vector<std::string> code;  // code lines as written
};

std::vector<Cheat> parse(const std::string& text);
std::string serialize(const std::vector<Cheat>& cheats);
// <folder>/<title id>.txt; an existing upper-case name (as Citra writes them) is used when there is no lower-case one
std::filesystem::path file_for(const std::filesystem::path& folder, uint64_t title_id = kTitleId);
std::vector<Cheat> load(const std::filesystem::path& file);  // empty if there is none
bool save(const std::filesystem::path& file, const std::vector<Cheat>& cheats, std::string& error);  // atomic replace
// The MH3U US cheat file shipped with Cha-Cha (assets/cheats, by jM5557), written to `file` when there is none yet.
// True if it was written.
bool install_bundled(const std::filesystem::path& file);

// ---- codes ----------------------------------------------------------------------------------------------------
// Guest memory as the codes see it; values in host order (the implementation stores big-endian).
class Memory {
public:
    virtual ~Memory() = default;
    virtual bool valid(uint32_t addr, uint32_t size) = 0;
    virtual uint32_t read32(uint32_t addr) = 0;
    virtual uint16_t read16(uint32_t addr) = 0;
    virtual uint8_t read8(uint32_t addr) = 0;
    virtual void write32(uint32_t addr, uint32_t v) = 0;
    virtual void write16(uint32_t addr, uint16_t v) = 0;
    virtual void write8(uint32_t addr, uint8_t v) = 0;
};

// A cheat's code lines, compiled. Code types (big-endian Wii U memory, 28-bit addresses plus the offset register):
//   0/1/2 XXXXXXX  32/16/8-bit write            3-6  32-bit if <, >, ==, !=      7-A  16-bit if, with ignore mask
//   B  offset = [X+offset]                       C    loop the block Y times      D0 end if   D1 end loop
//   D2 end all, clear offset and data            D3   offset = Y                  D4 data += Y   D5 data = Y
//   D6/D7/D8 write data 32/16/8 to [Y+offset], offset advances                    D9/DA/DB load data 32/16/8
//   DC offset += Y                               E    copy the Y bytes on the following lines to X+offset
// DD (3DS button conditions) is rejected.
class Program {
public:
    // false with `error` naming the line (1-based in `lines`) on a code that does not parse
    bool compile(const std::vector<std::string>& lines, std::string& error);
    // One run. false when it touched unmapped memory (`error` says where); the rest of that run is skipped.
    bool run(Memory& mem, std::string* error = nullptr) const;
    bool empty() const { return ops_.empty(); }

private:
    enum class Kind : uint8_t {
        Write32, Write16, Write8, If32, If16, LoadOffset, Loop, EndIf, EndLoop, EndAll,
        SetOffset, AddData, SetData, StoreData32, StoreData16, StoreData8, LoadData32, LoadData16, LoadData8,
        AddOffset, Copy,
    };
    struct Op {
        Kind kind;
        uint8_t cmp = 0;  // If32 / If16: 0 <, 1 >, 2 ==, 3 !=
        uint32_t x = 0, y = 0;
        std::vector<uint8_t> bytes;  // Copy
    };
    std::vector<Op> ops_;
};

// ---- the engine -----------------------------------------------------------------------------------------------
class Engine {
public:
    // The guest's code as the executable has it (taken before the game runs), the function body ranges (generated
    // kFunctionRanges) and how to tell mapped memory. Without attach() the engine only keeps the list.
    void attach(uint8_t* base, std::function<bool(uint32_t addr, uint32_t size)> mapped, uint32_t code_start,
                std::vector<uint8_t> pristine_code, const ppc::FunctionRange* ranges, size_t range_count);

    // The cheat list (from the file or the MODS panel). Enabled cheats that compile run from the next frame; cheats
    // no longer running give back the original instructions they had changed.
    void set_cheats(const std::vector<Cheat>& cheats);
    void set_enabled(bool on);  // the master switch
    bool enabled() const { return enabled_; }
    void run_frame();  // once per frame, on the game's thread

    // Why a cheat does not run or stopped (compile error, unmapped address, an instruction the interpreter lacks).
    std::unordered_map<std::string, std::string> problems() const;
    size_t running() const;
    uint32_t patched_functions() const { return ppc::g_patched_functions.load(); }

private:
    class GuestMem;
    struct Active {
        std::string name;
        std::string key;  // name + code: the same cheat across set_cheats calls
        bool taken = false;
        Program program;
        std::unordered_set<uint32_t> code_words;  // code-region words it writes (put back when it stops)
        bool reported = false;
    };
    void restore_locked(const Active& gone);
    void refresh_functions_locked();  // flags for the functions owning dirty_ words
    void owners(uint32_t addr, std::vector<uint32_t>& out) const;
    bool function_differs(uint32_t entry) const;
    bool in_code(uint32_t addr) const { return addr >= code_start_ && addr - code_start_ < pristine_.size(); }

    mutable std::mutex m_;
    std::atomic<bool> enabled_{true}, has_active_{false};
    std::vector<Active> active_;
    std::unordered_map<std::string, std::string> problems_;
    uint8_t* base_ = nullptr;
    std::function<bool(uint32_t, uint32_t)> mapped_;
    uint32_t code_start_ = 0;
    std::vector<uint8_t> pristine_;
    const ppc::FunctionRange* ranges_ = nullptr;
    size_t range_count_ = 0;
    std::vector<uint32_t> max_end_;  // max end of ranges_[0..i], to bound the owner search
    std::unordered_map<uint32_t, std::vector<std::pair<uint32_t, uint32_t>>> bodies_;  // entry -> its ranges
    std::set<uint32_t> dirty_;  // code words changed since the last refresh
    std::set<uint32_t> warned_words_;
};

Engine& engine();

}  // namespace mh3u::cheats
