#include "cheats.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

namespace mh3u::cheats {
extern const unsigned char kBundledCheats[];  // bundled_cheats.cpp (generated from assets/cheats)
extern const size_t kBundledCheatsSize;
namespace {

namespace stdfs = std::filesystem;

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::string hex8(uint32_t v) {
    char b[12];
    std::snprintf(b, sizeof b, "%08X", v);
    return b;
}

bool hex_word(const std::string& s, uint32_t& out) {
    if (s.size() != 8) return false;
    out = 0;
    for (char c : s) {
        const int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (v < 0) return false;
        out = out << 4 | static_cast<uint32_t>(v);
    }
    return true;
}

// "XXXXXXXX YYYYYYYY" (any whitespace between)
bool code_line(const std::string& line, uint32_t& x, uint32_t& y) {
    std::istringstream in(line);
    std::string a, b, rest;
    if (!(in >> a >> b) || (in >> rest)) return false;
    return hex_word(a, x) && hex_word(b, y);
}

bool blank(const std::string& s) { return trim(s).empty(); }

}  // namespace

// ---- the file -------------------------------------------------------------------------------------------------
std::vector<Cheat> parse(const std::string& text) {
    std::vector<Cheat> out;
    std::istringstream in(text);
    bool notes_open = false;
    for (std::string raw; std::getline(in, raw);) {
        const std::string line = trim(raw);
        if (notes_open) {  // {notes ... continued until the closing brace
            const size_t close = line.find('}');
            if (!out.empty()) out.back().notes += "\n" + line.substr(0, close);
            notes_open = close == std::string::npos;
            continue;
        }
        if (line.empty()) continue;
        if (line[0] == '[') {
            const size_t close = line.rfind(']');
            out.push_back({trim(line.substr(1, close == std::string::npos ? std::string::npos : close - 1)), {}, false, {}});
            continue;
        }
        if (out.empty()) continue;  // before the first [name]: belongs to no cheat
        Cheat& c = out.back();
        if (line[0] == '{') {
            const size_t close = line.find('}');
            c.notes = line.substr(1, close == std::string::npos ? std::string::npos : close - 1);
            notes_open = close == std::string::npos;
        } else if (line == "*cemu_enabled" || line == "*citra_enabled") {
            c.enabled = true;
        } else {
            c.code.push_back(line);
        }
    }
    return out;
}

std::string serialize(const std::vector<Cheat>& cheats) {
    std::ostringstream o;
    for (const Cheat& c : cheats) {
        o << '[' << c.name << "]\n";
        if (!c.notes.empty()) {
            std::string n = c.notes;
            std::replace(n.begin(), n.end(), '}', ')');  // would end the notes early when read back
            o << '{' << n << "}\n";
        }
        if (c.enabled) o << "*cemu_enabled\n";
        for (const std::string& l : c.code)
            if (!blank(l)) o << trim(l) << '\n';
        o << '\n';
    }
    return o.str();
}

stdfs::path file_for(const stdfs::path& folder, uint64_t title_id) {
    char lower[24], upper[24];
    std::snprintf(lower, sizeof lower, "%016llx.txt", static_cast<unsigned long long>(title_id));
    std::snprintf(upper, sizeof upper, "%016llX.txt", static_cast<unsigned long long>(title_id));
    std::error_code ec;
    if (!stdfs::exists(folder / lower, ec) && stdfs::exists(folder / upper, ec)) return folder / upper;
    return folder / lower;
}

std::vector<Cheat> load(const stdfs::path& file) {
    std::ifstream f(file, std::ios::binary);
    if (!f) return {};
    std::ostringstream s;
    s << f.rdbuf();
    return parse(s.str());
}

bool save(const stdfs::path& file, const std::vector<Cheat>& cheats, std::string& error) {
    std::error_code ec;
    stdfs::create_directories(file.parent_path(), ec);
    stdfs::path tmp = file;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f << serialize(cheats);
        if (!f) {
            error = "can't write " + tmp.string();
            return false;
        }
    }
    stdfs::rename(tmp, file, ec);
    if (ec) {
        stdfs::remove(tmp, ec);
        error = "can't replace " + file.string();
        return false;
    }
    return true;
}

bool install_bundled(const stdfs::path& file) {
    std::error_code ec;
    if (stdfs::exists(file, ec) || stdfs::exists(file_for(file.parent_path()), ec) || !kBundledCheatsSize) return false;
    stdfs::create_directories(file.parent_path(), ec);
    std::ofstream f(file, std::ios::binary);
    f.write(reinterpret_cast<const char*>(kBundledCheats), static_cast<std::streamsize>(kBundledCheatsSize));
    return static_cast<bool>(f);
}

// ---- codes ----------------------------------------------------------------------------------------------------
bool Program::compile(const std::vector<std::string>& lines, std::string& error) {
    ops_.clear();
    for (size_t i = 0; i < lines.size(); ++i) {
        if (blank(lines[i])) continue;
        const std::string at = "line " + std::to_string(i + 1) + ": ";
        uint32_t x, y;
        if (!code_line(lines[i], x, y)) {
            error = at + "expected two 8-digit hex numbers, like \"02123450 38A00000\"";
            return false;
        }
        Op op{Kind::Write32, 0, x & 0x0FFFFFFFu, y, {}};
        const uint32_t type = x >> 28;
        if (type <= 2) {
            op.kind = type == 0 ? Kind::Write32 : type == 1 ? Kind::Write16 : Kind::Write8;
        } else if (type <= 6) {
            op.kind = Kind::If32;
            op.cmp = static_cast<uint8_t>(type - 3);
        } else if (type <= 0xA) {
            op.kind = Kind::If16;
            op.cmp = static_cast<uint8_t>(type - 7);
        } else if (type == 0xB) {
            op.kind = Kind::LoadOffset;
        } else if (type == 0xC) {
            op.kind = Kind::Loop;
        } else if (type == 0xD) {
            static const Kind kD[] = {Kind::EndIf, Kind::EndLoop, Kind::EndAll, Kind::SetOffset, Kind::AddData,
                                      Kind::SetData, Kind::StoreData32, Kind::StoreData16, Kind::StoreData8,
                                      Kind::LoadData32, Kind::LoadData16, Kind::LoadData8, Kind::AddOffset};
            const uint32_t sub = (x >> 24) & 0xF;
            if (sub == 0xD) {
                error = at + "button conditions (DD) are for the 3DS and aren't supported";
                return false;
            }
            if (sub > 0xC) {
                error = at + "unknown code type D" + hex8(x).substr(1, 1);
                return false;
            }
            op.kind = kD[sub];
        } else if (type == 0xE) {
            op.kind = Kind::Copy;
            if (y == 0 || y > 0x10000) {
                error = at + "the byte count of a copy (E) code must be 1 to 0x10000";
                return false;
            }
            // the data: the next ceil(Y / 8) code lines, 8 bytes each, in order
            size_t need = (y + 7) / 8, j = i + 1;
            for (; need; --need, ++j) {
                while (j < lines.size() && blank(lines[j])) ++j;
                uint32_t p, q;
                if (j >= lines.size()) {
                    error = at + "the copy (E) code needs " + std::to_string((y + 7) / 8) + " data line(s) after it";
                    return false;
                }
                if (!code_line(lines[j], p, q)) {
                    error = "line " + std::to_string(j + 1) + ": not a data line for the copy (E) code above it";
                    return false;
                }
                for (uint32_t w : {p, q})
                    for (int sh = 24; sh >= 0; sh -= 8)
                        if (op.bytes.size() < y) op.bytes.push_back(static_cast<uint8_t>(w >> sh));
            }
            i = j - 1;
        } else {
            error = at + "unknown code type " + hex8(x).substr(0, 1);
            return false;
        }
        ops_.push_back(std::move(op));
    }
    return true;
}

bool Program::run(Memory& mem, std::string* error) const {
    uint32_t offset = 0, data = 0, skipping = 0;  // skipping: depth of false conditions being skipped
    bool looping = false;
    size_t loop_at = 0;
    uint32_t loop_left = 0;
    uint64_t steps = 0;
    const auto unmapped = [&](uint32_t a, uint32_t n) {
        if (error) *error = "address " + hex8(a) + " (" + std::to_string(n) + " bytes) is not mapped";
        return false;
    };
    for (size_t i = 0; i < ops_.size(); ++i) {
        if (++steps > 1000000) {  // a runaway loop count must not stall the game's thread
            if (error) *error = "stopped after a million steps (check the loop count)";
            return false;
        }
        const Op& op = ops_[i];
        if (skipping) {
            if (op.kind == Kind::If32 || op.kind == Kind::If16) ++skipping;
            else if (op.kind == Kind::EndIf) --skipping;
            else if (op.kind == Kind::EndAll) {
                skipping = 0;
                looping = false;
                offset = data = 0;
            }
            continue;
        }
        const uint32_t addr = op.x + offset;
        switch (op.kind) {
            case Kind::Write32:
                if (!mem.valid(addr, 4)) return unmapped(addr, 4);
                mem.write32(addr, op.y);
                break;
            case Kind::Write16:
                if (!mem.valid(addr, 2)) return unmapped(addr, 2);
                mem.write16(addr, static_cast<uint16_t>(op.y));
                break;
            case Kind::Write8:
                if (!mem.valid(addr, 1)) return unmapped(addr, 1);
                mem.write8(addr, static_cast<uint8_t>(op.y));
                break;
            case Kind::If32:
            case Kind::If16: {
                uint32_t v, want;
                if (op.kind == Kind::If32) {
                    if (!mem.valid(addr, 4)) return unmapped(addr, 4);
                    v = mem.read32(addr);
                    want = op.y;
                } else {
                    if (!mem.valid(addr, 2)) return unmapped(addr, 2);
                    v = mem.read16(addr) & ~(op.y >> 16) & 0xFFFFu;  // the top half of Y masks bits out
                    want = op.y & 0xFFFFu;
                }
                const bool pass = op.cmp == 0 ? v < want : op.cmp == 1 ? v > want : op.cmp == 2 ? v == want : v != want;
                if (!pass) skipping = 1;
                break;
            }
            case Kind::LoadOffset:
                if (!mem.valid(addr, 4)) return unmapped(addr, 4);
                offset = mem.read32(addr);
                break;
            case Kind::Loop:  // loops don't nest: a new one replaces the current one
                looping = true;
                loop_at = i;
                loop_left = op.y ? op.y - 1 : 0;
                break;
            case Kind::EndIf: break;  // the end of a condition that held
            case Kind::EndLoop:
            case Kind::EndAll:
                if (looping && loop_left) {
                    --loop_left;
                    i = loop_at;  // the loop's next pass starts on the line after the C code
                    break;
                }
                looping = false;
                if (op.kind == Kind::EndAll) offset = data = 0;
                break;
            case Kind::SetOffset: offset = op.y; break;
            case Kind::AddOffset: offset += op.y; break;
            case Kind::AddData: data += op.y; break;
            case Kind::SetData: data = op.y; break;
            case Kind::StoreData32:
            case Kind::StoreData16:
            case Kind::StoreData8: {
                const uint32_t n = op.kind == Kind::StoreData32 ? 4 : op.kind == Kind::StoreData16 ? 2 : 1, a = op.y + offset;
                if (!mem.valid(a, n)) return unmapped(a, n);
                if (n == 4) mem.write32(a, data);
                else if (n == 2) mem.write16(a, static_cast<uint16_t>(data));
                else mem.write8(a, static_cast<uint8_t>(data));
                offset += n;
                break;
            }
            case Kind::LoadData32:
            case Kind::LoadData16:
            case Kind::LoadData8: {
                const uint32_t n = op.kind == Kind::LoadData32 ? 4 : op.kind == Kind::LoadData16 ? 2 : 1, a = op.y + offset;
                if (!mem.valid(a, n)) return unmapped(a, n);
                data = n == 4 ? mem.read32(a) : n == 2 ? mem.read16(a) : mem.read8(a);
                break;
            }
            case Kind::Copy: {
                const uint32_t n = static_cast<uint32_t>(op.bytes.size());
                if (!mem.valid(addr, n)) return unmapped(addr, n);
                for (uint32_t k = 0; k < n; ++k) mem.write8(addr + k, op.bytes[k]);
                break;
            }
        }
    }
    return true;
}

// ---- the engine -----------------------------------------------------------------------------------------------
// Guest memory for one cheat's run: writes that change nothing are skipped (cheats rewrite the same values every
// frame); writes into the code region are recorded, so the functions they land in can be flagged.
class Engine::GuestMem final : public Memory {
public:
    GuestMem(Engine& e, Active* who) : e_(e), who_(who) {}
    bool valid(uint32_t a, uint32_t n) override {
        return n && uint64_t{a} + n <= 0x100000000ull && e_.mapped_ && e_.mapped_(a, n);
    }
    uint32_t read32(uint32_t a) override { return ppc::ld32(e_.base_, a); }
    uint16_t read16(uint32_t a) override { return ppc::ld16(e_.base_, a); }
    uint8_t read8(uint32_t a) override { return ppc::ld8(e_.base_, a); }
    void write32(uint32_t a, uint32_t v) override {
        note(a, 4);
        if (ppc::ld32(e_.base_, a) != v) {
            ppc::st32(e_.base_, a, v);
            changed(a, 4);
        }
    }
    void write16(uint32_t a, uint16_t v) override {
        note(a, 2);
        if (ppc::ld16(e_.base_, a) != v) {
            ppc::st16(e_.base_, a, v);
            changed(a, 2);
        }
    }
    void write8(uint32_t a, uint8_t v) override {
        note(a, 1);
        if (ppc::ld8(e_.base_, a) != v) {
            ppc::st8(e_.base_, a, v);
            changed(a, 1);
        }
    }

private:
    void note(uint32_t a, uint32_t n) {
        for (uint32_t w = a & ~3u; w < a + n; w += 4)
            if (e_.in_code(w)) who_->code_words.insert(w);
    }
    void changed(uint32_t a, uint32_t n) {
        for (uint32_t w = a & ~3u; w < a + n; w += 4) {
            const bool code = e_.in_code(w), cave = w >= kCaveBase && w < kCaveEnd;
            if (code) e_.dirty_.insert(w);
            // an instruction the interpreter would stop at (harmless if the word is data the code reads)
            if ((code || cave) && !ppc::interpretable(ppc::ld32(e_.base_, w)) && e_.warned_words_.insert(w).second)
                std::fprintf(stderr, "cheats: '%s' writes %08X at %08X, which the interpreter can't run if it is reached as code\n",
                             who_->name.c_str(), ppc::ld32(e_.base_, w), w);
        }
    }
    Engine& e_;
    Active* who_;
};

Engine& engine() {
    static Engine e;
    return e;
}

void Engine::attach(uint8_t* base, std::function<bool(uint32_t, uint32_t)> mapped, uint32_t code_start,
                    std::vector<uint8_t> pristine_code, const ppc::FunctionRange* ranges, size_t range_count) {
    std::lock_guard g(m_);
    base_ = base;
    mapped_ = std::move(mapped);
    code_start_ = code_start;
    pristine_ = std::move(pristine_code);
    ranges_ = ranges;
    range_count_ = range_count;
    max_end_.resize(range_count);
    bodies_.clear();
    uint32_t m = 0;
    for (size_t i = 0; i < range_count; ++i) {
        m = std::max(m, ranges[i].end);
        max_end_[i] = m;
        bodies_[ranges[i].entry].emplace_back(ranges[i].start, ranges[i].end);
    }
}

void Engine::owners(uint32_t addr, std::vector<uint32_t>& out) const {
    const ppc::FunctionRange* end = ranges_ + range_count_;
    const auto* it = std::upper_bound(ranges_, end, addr, [](uint32_t a, const ppc::FunctionRange& r) { return a < r.start; });
    for (ptrdiff_t i = (it - ranges_) - 1; i >= 0 && max_end_[static_cast<size_t>(i)] > addr; --i)
        if (addr >= ranges_[i].start && addr < ranges_[i].end) out.push_back(ranges_[i].entry);
}

bool Engine::function_differs(uint32_t entry) const {
    auto it = bodies_.find(entry);
    if (it == bodies_.end()) return false;
    for (auto [s, e] : it->second) {
        if (!in_code(s) || !in_code(e - 1)) continue;
        if (std::memcmp(base_ + s, pristine_.data() + (s - code_start_), e - s) != 0) return true;
    }
    return false;
}

void Engine::refresh_functions_locked() {
    if (dirty_.empty()) return;
    std::set<uint32_t> entries;
    std::vector<uint32_t> found;
    for (uint32_t w : dirty_) {
        found.clear();
        owners(w, found);
        entries.insert(found.begin(), found.end());
    }
    dirty_.clear();
    for (uint32_t e : entries) {
        const bool differs = function_differs(e);
        if (differs != ppc::function_patched(e))
            std::printf("cheats: function %08X %s\n", e, differs ? "patched: runs interpreted" : "restored: runs recompiled");
        ppc::set_function_patched(e, differs);
    }
    std::fflush(stdout);
}

// Puts back the original instructions a cheat had written, except where another running cheat writes too (it
// rewrites its own values next frame anyway).
void Engine::restore_locked(const Active& gone) {
    if (!base_) return;
    for (uint32_t w : gone.code_words) {
        const bool shared = std::any_of(active_.begin(), active_.end(), [&](const Active& a) { return &a != &gone && a.code_words.count(w); });
        if (shared || !in_code(w)) continue;
        uint32_t orig;
        std::memcpy(&orig, pristine_.data() + (w - code_start_), 4);
        uint32_t cur;
        std::memcpy(&cur, base_ + w, 4);
        if (cur != orig) {
            std::memcpy(base_ + w, &orig, 4);
            dirty_.insert(w);
        }
    }
}

void Engine::set_cheats(const std::vector<Cheat>& cheats) {
    std::lock_guard g(m_);
    std::vector<Active> next;
    problems_.clear();
    for (const Cheat& c : cheats) {
        if (!c.enabled) continue;
        Active a;
        a.name = c.name;
        a.key = c.name;
        for (const auto& l : c.code)
            if (!blank(l)) a.key += "\n" + trim(l);
        std::string err;
        if (!a.program.compile(c.code, err)) {
            problems_[c.name] = err;
            std::fprintf(stderr, "cheats: '%s' not enabled: %s\n", c.name.c_str(), err.c_str());
            continue;
        }
        if (!a.program.empty()) next.push_back(std::move(a));
    }
    // a cheat that keeps running (same name and code) keeps the list of words it writes; the others' are put back
    std::vector<Active> old = std::move(active_);
    for (Active& n : next)
        for (Active& o : old)
            if (!o.taken && o.key == n.key) {
                n.code_words = std::move(o.code_words);
                o.code_words.clear();
                o.taken = true;
                break;
            }
    active_ = std::move(next);
    for (const Active& o : old) restore_locked(o);
    refresh_functions_locked();
    has_active_ = !active_.empty();
    std::printf("cheats: %zu of %zu running\n", active_.size(), cheats.size());
    std::fflush(stdout);
}

void Engine::set_enabled(bool on) {
    std::lock_guard g(m_);
    if (enabled_ == on) return;
    enabled_ = on;
    if (!on) {  // everything back to the original code; the list stays for when it is switched on again
        for (const Active& a : active_) restore_locked(a);
        for (Active& a : active_) a.code_words.clear();
        refresh_functions_locked();
    }
}

void Engine::run_frame() {
    if (!enabled_.load(std::memory_order_relaxed) || !has_active_.load(std::memory_order_relaxed)) return;
    std::unique_lock g(m_, std::try_to_lock);  // never wait for the panel saving the list: next frame then
    if (!g.owns_lock() || !base_) return;
    for (Active& a : active_) {
        GuestMem mem(*this, &a);
        std::string err;
        if (!a.program.run(mem, &err) && !a.reported) {
            // kept running: the address may be mapped later (a heap block the game has not allocated yet)
            a.reported = true;
            problems_[a.name] = err;
            std::fprintf(stderr, "cheats: '%s': %s\n", a.name.c_str(), err.c_str());
        }
    }
    refresh_functions_locked();
}

std::unordered_map<std::string, std::string> Engine::problems() const {
    std::lock_guard g(m_);
    return problems_;
}

size_t Engine::running() const {
    std::lock_guard g(m_);
    return active_.size();
}

}  // namespace mh3u::cheats
