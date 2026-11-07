// Texture packs (texture_pack.h): Cemu-Enhanced's content hash (vectors computed with its implementation), and a
// pack tree in a temporary folder - pack priority, loose files, the title-id layout, textures.ini [hashes] and
// [filtering], packs switched off, older-hash packs, Cha-Cha's tex_ names, and textures.ini mistakes.
#include "texture_pack.h"

#include <cstdio>
#include <fstream>
#include <tuple>
#include <vector>

namespace stdfs = std::filesystem;
using namespace mh3u::gfx::texpack;

static int failures = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                 \
        }                                                               \
    } while (0)

static void dds(const stdfs::path& p, uint32_t w = 4, uint32_t h = 4) {
    stdfs::create_directories(p.parent_path());
    std::vector<uint8_t> blocks(level_bytes(71, w, h), 0x55);
    write_dds(p, 71, w, h, blocks.data(), blocks.size());
}
static std::string hx(uint64_t v) {
    char b[24];
    std::snprintf(b, sizeof b, "%016llx", static_cast<unsigned long long>(v));
    return b;
}

int main() {
    // the hash, as Cemu-Enhanced computes it
    for (auto [n, raw, cur] : {std::tuple{64u, 0xe1260bd641c1d70aull, 0x7a5c235e9ec3ef0cull},
                               std::tuple{4096u, 0xe8327ee118bfc332ull, 0xbbd6f0dd452bf45bull},
                               std::tuple{1000u, 0x680e10847c37c50aull, 0x33cd3019d14d3ca5ull}}) {
        std::vector<uint8_t> d(n);
        for (uint32_t i = 0; i < n; ++i) d[i] = static_cast<uint8_t>(i * 7 + 3);
        CHECK(content_hash_raw(d.data(), n) == raw);
        CHECK(content_hash(raw, n, 256, 0x31) == cur);
    }
    CHECK(hash_file_name(0x22fb5c1e481b3338ull, 1280, 720, 0x31) == "22fb5c1e481b3338_1280x720_fmt0031_mip00.dds");
    CHECK(hashable_format(0x31) && hashable_format(0x431) && hashable_format(0x235) && hashable_format(0x1A) &&
          hashable_format(0x41A) && !hashable_format(0x19) && !hashable_format(0x08));

    const stdfs::path root = stdfs::temp_directory_path() / "chacha_texpack_test";
    stdfs::remove_all(root);
    const uint64_t A = 0x1111, C = 0x3333, D = 0x4444, E = 0x5555, R = 0x6666;
    dds(root / hash_file_name(A, 4, 4, 0x31));                      // loose: base layer
    dds(root / "B_pack" / "deep" / hash_file_name(A, 4, 4, 0x31));  // a pack overrides it
    dds(root / "B_pack" / "sub" / "c.dds");
    dds(root / "B_pack" / "c_mip1.dds", 2, 2);
    std::ofstream(root / "B_pack" / "textures.ini") << "\xEF\xBB\xBF[hashes]\n# comment\n" << hx(C) << " = sub\\c.dds  # trailing\n"
                                                     << hx(C) << "_4x4_fmt0031_mip01 = c_mip1.dds\n"
                                                     << "nothex = sub/c.dds\n" << hx(D) << " = ../outside.dds\n"
                                                     << hx(E) << " = missing.dds\n"
                                                     << "[filtering]\n" << hx(C) << " = nearest\n" << hx(D) << " = linear\n"
                                                     << hx(E) << " = blurry\n";
    dds(root / "A_pack" / hash_file_name(C, 4, 4, 0x31));  // sorts first: wins C over B_pack's textures.ini
    std::ofstream(root / "A_pack" / "textures.ini") << "[filtering]\n" << hx(D) << " = nearest\n";
    dds(root / "0005000010118300" / "Z" / hash_file_name(E, 4, 4, 0x31));  // Cemu-Enhanced's layout
    dds(root / "0005000010118300" / "Z" / hash_file_name(R, 8, 8, 0x31));  // an older-hash pack: R is a plain hash
    dds(root / "B_pack" / "tex_4x4_DEADBEEF_edited.dds");                  // Cha-Cha's older names

    CHECK((list_packs(root) == std::vector<std::string>{"0005000010118300/Z", "A_pack", "B_pack"}));
    Library lib;
    std::vector<std::string> warnings;
    lib.scan(root, {}, &warnings);
    const auto name = [&](uint64_t k, int mip = 0) {
        const stdfs::path* p = lib.file(k, mip);
        return p ? stdfs::relative(*p, root).generic_string() : std::string("-");
    };
    CHECK(lib.resolve(A, 0, 4, 4, 0x31) == A && name(A) == "B_pack/deep/" + hash_file_name(A, 4, 4, 0x31));
    CHECK(name(C) == "A_pack/" + hash_file_name(C, 4, 4, 0x31) && name(C, 1) == "B_pack/c_mip1.dds");
    CHECK(name(E) == "0005000010118300/Z/" + hash_file_name(E, 4, 4, 0x31));
    CHECK(lib.resolve(D, 0, 4, 4, 0x31) == 0);  // only a filter, no file
    CHECK(lib.filter(C, 0) == Filter::Nearest && lib.filter(D, 0) == Filter::Nearest && lib.filter(0x9999, 0) == Filter::None);
    CHECK(lib.filter(0x9999, D) == Filter::Nearest);  // by the other hash
    CHECK(lib.conflicts() == 1);  // C in two packs (a pack over a loose file is no conflict)
    // older plain-hash packs: matched by the raw hash only for the size and format the name records
    CHECK(lib.resolve(0x7777, R, 8, 8, 0x31) == R && lib.resolve(0x7777, R, 4, 4, 0x31) == 0 && lib.resolve(0x7777, R, 8, 8, 0x33) == 0);
    CHECK(lib.has_crc_names() && lib.find(4, 4, 0xDEADBEEF) && !lib.find(4, 4, 0xDEADBEEE));
    bool bad_key = false, outside = false, missing = false, bad_filter = false;
    for (const auto& w : warnings) {
        bad_key |= w.find("'nothex' is not a texture hash") != std::string::npos;
        outside |= w.find("not inside the pack folder") != std::string::npos;
        missing |= w.find("does not exist") != std::string::npos;
        bad_filter |= w.find("unknown filter 'blurry'") != std::string::npos;
    }
    CHECK(bad_key && outside && missing && bad_filter);
    CHECK(lib.filter(E, 0) == Filter::None);

    // A_pack off: B_pack's textures.ini supplies C, and its filter for D applies
    lib.scan(root, {"A_pack"});
    CHECK(name(C) == "B_pack/sub/c.dds" && lib.filter(D, 0) == Filter::Linear);
    // B_pack off too: A falls back to the loose file
    lib.scan(root, {"A_pack", "B_pack"});
    CHECK(name(A) == hash_file_name(A, 4, 4, 0x31) && lib.resolve(C, 0, 4, 4, 0x31) == 0 && !lib.has_crc_names());
    lib.scan(root, {"A_pack", "B_pack", "0005000010118300/Z"});
    CHECK(lib.resolve(E, 0, 4, 4, 0x31) == 0 && lib.size() == 1);

    Dds d;
    CHECK(read_dds(root / "B_pack" / "c_mip1.dds", d, nullptr) && d.width == 2 && d.dxgi == 71 && d.levels == 1);
    stdfs::remove_all(root);
    std::printf(failures ? "%d failure(s)\n" : "all checks passed\n", failures);
    return failures ? 1 : 0;
}
