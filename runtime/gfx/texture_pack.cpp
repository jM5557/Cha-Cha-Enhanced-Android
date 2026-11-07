#include "texture_pack.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <sstream>

namespace mh3u::gfx::texpack {
namespace {

namespace stdfs = std::filesystem;

uint64_t key(uint32_t w, uint32_t h, uint32_t crc) { return (uint64_t{w & 0xFFFF} << 48) | (uint64_t{h & 0xFFFF} << 32) | crc; }

uint32_t rd32(const uint8_t* p) { return uint32_t{p[0]} | uint32_t{p[1]} << 8 | uint32_t{p[2]} << 16 | uint32_t{p[3]} << 24; }
void wr32(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}
constexpr uint32_t fourcc(char a, char b, char c, char d) {
    return uint32_t(uint8_t(a)) | uint32_t(uint8_t(b)) << 8 | uint32_t(uint8_t(c)) << 16 | uint32_t(uint8_t(d)) << 24;
}

// "tex_<w>x<h>_<8 hex>" at the start of a file name
bool parse_name(const std::string& n, uint32_t& w, uint32_t& h, uint32_t& crc) {
    if (n.size() < 4 || (n.compare(0, 4, "tex_") != 0 && n.compare(0, 4, "TEX_") != 0)) return false;
    size_t i = 4;
    const auto num = [&](uint32_t& v) {
        const size_t s = i;
        v = 0;
        while (i < n.size() && std::isdigit(static_cast<unsigned char>(n[i])) && i - s < 5) v = v * 10 + uint32_t(n[i++] - '0');
        return i > s;
    };
    if (!num(w) || i >= n.size() || (n[i] != 'x' && n[i] != 'X')) return false;
    ++i;
    if (!num(h) || i >= n.size() || n[i] != '_') return false;
    ++i;
    if (i + 8 > n.size()) return false;
    crc = 0;
    for (size_t k = 0; k < 8; ++k) {
        const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(n[i + k])));
        if (c >= '0' && c <= '9') crc = crc << 4 | uint32_t(c - '0');
        else if (c >= 'a' && c <= 'f') crc = crc << 4 | uint32_t(c - 'a' + 10);
        else return false;
    }
    return w && h;
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}
bool is_dds(const stdfs::path& p) { return lower(p.extension().string()) == ".dds"; }

// "<hash>_<w>x<h>_fmt<hex>_mip<n>" at the start of a name (Cemu-Enhanced's dump names)
bool parse_hash_name(const std::string& n, uint64_t& hash, uint32_t& w, uint32_t& h, uint32_t& fmt, int& mip) {
    unsigned long long hh = 0;
    unsigned ww = 0, hgt = 0, ff = 0;
    int mm = 0;
    if (std::sscanf(n.c_str(), "%llx_%ux%u_fmt%x_mip%d", &hh, &ww, &hgt, &ff, &mm) != 5) return false;
    hash = hh;
    w = ww;
    h = hgt;
    fmt = ff;
    mip = mm;
    return hash != 0;
}
// a textures.ini key: a plain hash (mip 0) or a whole dump name
bool parse_key(const std::string& key, uint64_t& hash, int& mip, uint32_t& w, uint32_t& h, uint32_t& fmt) {
    if (parse_hash_name(key, hash, w, h, fmt, mip)) return true;
    std::string k = key;
    if (k.size() > 2 && k[0] == '0' && (k[1] == 'x' || k[1] == 'X')) k = k.substr(2);
    if (k.empty() || k.size() > 16 || !std::all_of(k.begin(), k.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)); }))
        return false;
    hash = std::stoull(k, nullptr, 16);
    mip = 0;
    w = h = fmt = 0;
    return hash != 0;
}
bool title_folder(const std::string& name) {
    return name.size() == 16 && std::all_of(name.begin(), name.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)); });
}
uint64_t mip_key(uint64_t hash, int mip) { return hash * 31 + static_cast<uint64_t>(mip); }

}  // namespace

uint64_t content_hash_raw(const uint8_t* data, size_t n) {
    // every 8-byte word (host order, as it lies in memory) mixed with its index; the tail bytes FNV-style
    uint64_t h = 0;
    const size_t words = n / 8;
    for (size_t i = 0; i < words; ++i) {
        uint64_t w;
        std::memcpy(&w, data + i * 8, 8);
        uint64_t m = (w ^ (static_cast<uint64_t>(static_cast<uint32_t>(i)) * 0x9E3779B97F4A7C15ull)) * 0xFF51AFD7ED558CCDull;
        m ^= m >> 29;
        h ^= m;
    }
    for (size_t i = words * 8; i < n; ++i) h = (h ^ data[i]) * 0x100000001B3ull;
    return h;
}

uint64_t content_hash(uint64_t h, uint32_t size_bytes, uint32_t pixel_count, uint32_t gx2_format) {
    h ^= uint64_t{pixel_count} * 0x9E3779B97F4A7C15ull;
    h ^= (uint64_t{gx2_format} + 0x165667B19E3779F9ull) * 0xC2B2AE3D27D4EB4Full;
    h ^= uint64_t{size_bytes} * 0xD6E8FEB86659FD93ull;
    h ^= h >> 33;
    h *= 0xFF51AFD7ED558CCDull;
    h ^= h >> 33;
    h *= 0xC4CEB9FE1A85EC53ull;
    h ^= h >> 33;
    return h ? h : 1;  // 0 means "no hash"
}

std::string hash_file_name(uint64_t hash, uint32_t width, uint32_t height, uint32_t gx2_format, int mip) {
    char b[96];
    std::snprintf(b, sizeof b, "%016llx_%ux%u_fmt%04x_mip%02d.dds", static_cast<unsigned long long>(hash), width, height, gx2_format, mip);
    return b;
}

bool hashable_format(uint32_t f) {
    const uint32_t base = f & 0x3F;
    return (base >= 0x31 && base <= 0x35) || f == 0x1A || f == 0x41A;  // BC1-BC5 (UNORM / SRGB / SNORM), RGBA8
}

uint32_t crc32(const uint8_t* data, size_t n) {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) c = table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

std::string file_name(uint32_t width, uint32_t height, uint32_t crc) {
    char b[64];
    std::snprintf(b, sizeof b, "tex_%ux%u_%08X.dds", width, height, crc);
    return b;
}

size_t level_bytes(uint32_t dxgi, uint32_t w, uint32_t h) {
    const size_t bw = (std::max(1u, w) + 3) / 4, bh = (std::max(1u, h) + 3) / 4;
    switch (dxgi) {
        case 71: case 72: case 80: case 81: return bw * bh * 8;                       // BC1, BC4
        case 74: case 75: case 77: case 78: case 83: case 84: case 98: case 99:       // BC2, BC3, BC5, BC7
            return bw * bh * 16;
        case 28: case 29: case 87: case 91: return size_t{std::max(1u, w)} * std::max(1u, h) * 4;  // RGBA8, BGRA8
        default: return 0;
    }
}

bool read_dds(const stdfs::path& file, Dds& out, std::string* error) {
    const auto fail = [&](const char* why) {
        if (error) *error = why;
        return false;
    };
    std::ifstream f(file, std::ios::binary);
    if (!f) return fail("can't open");
    std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (b.size() < 128 || rd32(b.data()) != fourcc('D', 'D', 'S', ' ')) return fail("not a DDS file");
    const uint8_t* hd = b.data() + 4;
    out.height = rd32(hd + 8);
    out.width = rd32(hd + 12);
    out.levels = std::max(1u, rd32(hd + 24));
    const uint8_t* pf = hd + 72;  // DDS_PIXELFORMAT
    const uint32_t pf_flags = rd32(pf + 4), cc = rd32(pf + 8);
    size_t at = 128;
    if (pf_flags & 4) {  // fourcc
        if (cc == fourcc('D', 'X', '1', '0')) {
            if (b.size() < 148) return fail("truncated DX10 header");
            out.dxgi = rd32(b.data() + 128);
            if (rd32(b.data() + 128 + 4) != 3 || rd32(b.data() + 128 + 12) > 1) return fail("only single 2D textures are supported");
            at = 148;
        } else if (cc == fourcc('D', 'X', 'T', '1')) out.dxgi = 71;
        else if (cc == fourcc('D', 'X', 'T', '2') || cc == fourcc('D', 'X', 'T', '3')) out.dxgi = 74;
        else if (cc == fourcc('D', 'X', 'T', '4') || cc == fourcc('D', 'X', 'T', '5')) out.dxgi = 77;
        else if (cc == fourcc('A', 'T', 'I', '1') || cc == fourcc('B', 'C', '4', 'U')) out.dxgi = 80;
        else if (cc == fourcc('A', 'T', 'I', '2') || cc == fourcc('B', 'C', '5', 'U')) out.dxgi = 83;
        else return fail("unsupported DDS format");
    } else if ((pf_flags & 0x40) && rd32(pf + 12) == 32) {  // uncompressed 32-bit RGB(A)
        const uint32_t rmask = rd32(pf + 16);
        out.dxgi = rmask == 0x000000FFu ? 28 : rmask == 0x00FF0000u ? 87 : 0;
        if (!out.dxgi) return fail("unsupported DDS channel layout");
    } else {
        return fail("unsupported DDS format");
    }
    if (!out.width || !out.height || out.width > 16384 || out.height > 16384 || !level_bytes(out.dxgi, 4, 4))
        return fail("unsupported DDS size or format");
    out.offsets.clear();
    size_t total = 0;
    uint32_t levels = 0;
    for (; levels < out.levels; ++levels) {
        const size_t n = level_bytes(out.dxgi, out.width >> levels, out.height >> levels);
        if (at + total + n > b.size()) break;  // keep the levels that are present
        out.offsets.push_back(total);
        total += n;
    }
    if (!levels) return fail("truncated DDS data");
    out.levels = levels;
    out.data.assign(b.begin() + static_cast<std::ptrdiff_t>(at), b.begin() + static_cast<std::ptrdiff_t>(at + total));
    return true;
}

bool write_dds(const stdfs::path& file, uint32_t dxgi, uint32_t width, uint32_t height, const uint8_t* data, size_t n) {
    std::array<uint8_t, 148> h{};
    wr32(&h[0], fourcc('D', 'D', 'S', ' '));
    uint8_t* hd = &h[4];
    wr32(hd + 0, 124);
    const bool block = level_bytes(dxgi, 4, 4) < 64;
    wr32(hd + 4, 0x1 | 0x2 | 0x4 | 0x1000 | (block ? 0x80000 : 0x8));  // caps, height, width, pixelformat, linear/pitch
    wr32(hd + 8, height);
    wr32(hd + 12, width);
    wr32(hd + 16, static_cast<uint32_t>(block ? level_bytes(dxgi, width, height) : size_t{width} * 4));
    wr32(hd + 24, 1);
    wr32(hd + 72, 32);
    wr32(hd + 76, 4);
    wr32(hd + 80, fourcc('D', 'X', '1', '0'));
    wr32(hd + 104, 0x1000);  // DDSCAPS_TEXTURE
    wr32(&h[128], dxgi);
    wr32(&h[132], 3);  // TEXTURE2D
    wr32(&h[140], 1);  // array size
    std::error_code ec;
    stdfs::create_directories(file.parent_path(), ec);
    std::ofstream f(file, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(h.data()), static_cast<std::streamsize>(h.size()));
    f.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(n));
    return static_cast<bool>(f);
}

std::vector<std::string> list_packs(const stdfs::path& root) {
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& e : stdfs::directory_iterator(root, ec)) {
        if (!e.is_directory(ec)) continue;
        const auto u = e.path().filename().u8string();
        const std::string name(u.begin(), u.end());
        if (title_folder(name)) {  // Cemu-Enhanced's per-title folder: its subfolders are the packs
            std::error_code ec2;
            for (const auto& sub : stdfs::directory_iterator(e.path(), ec2))
                if (sub.is_directory(ec2)) {
                    const auto su = sub.path().filename().u8string();
                    out.push_back(name + "/" + std::string(su.begin(), su.end()));
                }
        } else {
            out.push_back(name);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

void Library::index_folder(const stdfs::path& dir, bool recurse, const std::string& pack, std::vector<std::string>* warnings) {
    const auto warn = [&](const std::string& w) {
        if (warnings && warnings->size() < 64) warnings->push_back(w);
    };
    const auto claim = [&](uint64_t key, const std::string& what) {
        auto [it, fresh] = claimed_by_.emplace(key, pack);
        if (!fresh && it->second != pack && !pack.empty()) {
            ++conflicts_;
            if (conflicts_ <= 8) warn(what + " is in '" + it->second + "' and '" + pack + "': '" + it->second + "' is used");
        }
        return fresh;
    };
    const auto add_hash = [&](uint64_t hash, int mip, const stdfs::path& file, uint32_t w, uint32_t h, uint32_t fmt) {
        if (!claim(mip_key(hash, mip), file.filename().string())) return;
        files_[hash][mip] = Entry{file, w, h, fmt};
        ++count_;
    };
    // textures.ini first: its entries beat hash-named files in the same pack
    const stdfs::path ini = dir / "textures.ini";
    std::error_code ec;
    if (stdfs::is_regular_file(ini, ec)) {
        std::ifstream f(ini, std::ios::binary);
        std::string line, section;
        int n = 0;
        std::set<std::pair<uint64_t, int>> listed;
        while (std::getline(f, line)) {
            ++n;
            if (n == 1 && line.size() >= 3 && static_cast<uint8_t>(line[0]) == 0xEF && static_cast<uint8_t>(line[1]) == 0xBB &&
                static_cast<uint8_t>(line[2]) == 0xBF)
                line.erase(0, 3);  // UTF-8 byte order mark
            if (const size_t c = line.find('#'); c != std::string::npos) line.erase(c);
            line = trim(line);
            if (line.empty() || line[0] == ';') continue;
            const std::string where = ini.filename().string() + " line " + std::to_string(n) + (pack.empty() ? "" : " (" + pack + ")") + ": ";
            if (line[0] == '[') {
                section = lower(trim(line.substr(1, line.find(']') == std::string::npos ? std::string::npos : line.find(']') - 1)));
                continue;
            }
            if (section != "hashes" && section != "filtering") continue;
            const size_t eq = line.find('=');
            if (eq == std::string::npos) {
                warn(where + "expected <hash> = <value>");
                continue;
            }
            const std::string key = trim(line.substr(0, eq));
            std::string value = trim(line.substr(eq + 1));
            if (value.size() >= 2 && value.front() == '"' && value.back() == '"') value = value.substr(1, value.size() - 2);
            uint64_t hash;
            int mip;
            uint32_t w, h, fmt;
            if (!parse_key(key, hash, mip, w, h, fmt)) {
                warn(where + "'" + key + "' is not a texture hash");
                continue;
            }
            if (section == "hashes") {
                if (value.empty()) continue;  // listed, not replaced
                std::replace(value.begin(), value.end(), '\\', '/');
                const stdfs::path rel(std::u8string(value.begin(), value.end()));
                bool inside = !rel.is_absolute() && !rel.has_root_name() && !rel.has_root_directory();
                for (const auto& part : rel)
                    if (part == "..") inside = false;
                if (!inside) {
                    warn(where + "'" + value + "' is not inside the pack folder");
                    continue;
                }
                const stdfs::path file = dir / rel;
                if (!is_dds(file) || !stdfs::is_regular_file(file, ec)) {
                    warn(where + "'" + value + (is_dds(file) ? "' does not exist" : "' is not a .dds file"));
                    continue;
                }
                if (!listed.emplace(hash, mip).second) {
                    warn(where + "'" + key + "' is listed more than once, keeping the first");
                    continue;
                }
                add_hash(hash, mip, file, w, h, fmt);
            } else {
                const std::string v = lower(value);
                Filter flt;
                if (v == "nearest" || v == "point") flt = Filter::Nearest;
                else if (v == "linear" || v == "bilinear") flt = Filter::Linear;
                else if (v == "auto" || v == "default") flt = Filter::None;
                else {
                    warn(where + "unknown filter '" + value + "' (nearest, linear or auto)");
                    continue;
                }
                filters_.try_emplace(hash, flt);  // an earlier pack's setting stands
            }
        }
    }
    // then the files named after their texture
    std::vector<stdfs::path> found;
    const auto take = [&](const stdfs::directory_entry& e) {
        std::error_code ec2;
        if (e.is_regular_file(ec2) && is_dds(e.path())) found.push_back(e.path());
    };
    if (recurse) {
        for (auto it = stdfs::recursive_directory_iterator(dir, stdfs::directory_options::skip_permission_denied, ec);
             !ec && it != stdfs::recursive_directory_iterator(); it.increment(ec))
            take(*it);
    } else {
        for (const auto& e : stdfs::directory_iterator(dir, ec)) take(e);
    }
    std::sort(found.begin(), found.end());
    for (const auto& p : found) {
        const std::string name = p.filename().string();
        uint64_t hash;
        uint32_t w, h, fmt, crc;
        int mip;
        if (parse_hash_name(name, hash, w, h, fmt, mip)) {
            add_hash(hash, mip, p, w, h, fmt);
        } else if (parse_name(name, w, h, crc)) {
            const uint64_t k = key(w, h, crc);
            if (claim(k ^ 0x8000000000000000ull, name)) {
                crc_files_[k] = p;
                ++count_;
            }
        }
    }
}

size_t Library::scan(const stdfs::path& root, const std::set<std::string>& disabled, std::vector<std::string>* warnings) {
    files_.clear();
    crc_files_.clear();
    filters_.clear();
    claimed_by_.clear();
    count_ = conflicts_ = 0;
    std::error_code ec;
    if (!stdfs::is_directory(root, ec)) return 0;
    // enabled packs in name order (the first to claim a texture keeps it), then the loose files
    for (const std::string& pk : list_packs(root))
        if (!disabled.count(pk)) index_folder(root / stdfs::path(std::u8string(pk.begin(), pk.end())), true, pk, warnings);
    for (const auto& e : stdfs::directory_iterator(root, ec)) {
        const auto u = e.path().filename().u8string();
        if (e.is_directory(ec) && title_folder(std::string(u.begin(), u.end()))) index_folder(e.path(), false, {}, warnings);
    }
    index_folder(root, false, {}, warnings);
    return count_;
}

uint64_t Library::resolve(uint64_t current, uint64_t raw, uint32_t width, uint32_t height, uint32_t gx2_format) const {
    if (auto it = files_.find(current); it != files_.end() && it->second.count(0)) return current;
    // a pack named by the older plain hash: only for a texture of the size and format its name records
    if (auto it = files_.find(raw); raw && it != files_.end() && it->second.count(0))
        for (const auto& [mip, e] : it->second)
            if (uint64_t{e.name_w} * e.name_h == uint64_t{width} * height && e.name_fmt == gx2_format) return raw;
    return 0;
}

const stdfs::path* Library::file(uint64_t k, int mip) const {
    auto it = files_.find(k);
    if (it == files_.end()) return nullptr;
    auto m = it->second.find(mip);
    return m != it->second.end() ? &m->second.path : nullptr;
}

const stdfs::path* Library::find(uint32_t width, uint32_t height, uint32_t crc) const {
    auto it = crc_files_.find(key(width, height, crc));
    return it != crc_files_.end() ? &it->second : nullptr;
}

Filter Library::filter(uint64_t current, uint64_t raw) const {
    if (filters_.empty()) return Filter::None;
    if (auto it = filters_.find(current); it != filters_.end()) return it->second;
    if (auto it = filters_.find(raw); raw && it != filters_.end()) return it->second;
    return Filter::None;
}

}  // namespace mh3u::gfx::texpack
