#pragma once
// Texture replacement packs, compatible with Cemu-Enhanced's custom textures (docs/texture-packs.md).
//
// <data>/texture_replacements/            the root
//   <pack>/...                            each folder is a pack, switched on and off in MODS -> TEXTURES
//   <title id>/<pack>/...                 Cemu-Enhanced's layout (load/textures/<title id>/<pack>) works as copied
//   *.dds                                 loose files: always on, any pack overrides them
// Inside a pack (subfolders are fine):
//   <hash16>_<w>x<h>_fmt<XXXX>_mip<NN>.dds   named after the texture's content hash, as Cemu-Enhanced and our dumps
//                                            name them (a mip00 file with a full chain covers every level)
//   tex_<w>x<h>_<CRC32>[anything].dds        Cha-Cha's older names: CRC32 of the texture's level 0 in guest memory
//   textures.ini                             [hashes] <hash16 or whole dump name> = file.dds (any name, inside the
//                                            pack), [filtering] <hash16> = nearest | linear | auto
// Where enabled packs provide the same texture, the pack whose folder name sorts first wins; in a pack, textures.ini
// beats a hash-named file.
//
// The content hash covers every byte of the texture's level 0 as it sits in guest memory (GX2-tiled, the surface's
// image size), mixed with its pixel count, GX2 surface format and size, exactly as Cemu-Enhanced computes it, so
// packs move between the two unchanged. Packs made with Cemu-Enhanced's earlier plain content hash are still matched
// (only for a texture of the size and format their file names record).
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace mh3u::gfx::texpack {

uint32_t crc32(const uint8_t* data, size_t n);
std::string file_name(uint32_t width, uint32_t height, uint32_t crc);  // tex_256x256_1A2B3C4D.dds

// Cemu-Enhanced's content hash: the plain one (packs made before it mixed the surface in) and the current one.
uint64_t content_hash_raw(const uint8_t* data, size_t n);
uint64_t content_hash(uint64_t raw, uint32_t size_bytes, uint32_t pixel_count, uint32_t gx2_format);
// <hash16>_<w>x<h>_fmt<XXXX>_mip<NN>.dds, the dump / pack name
std::string hash_file_name(uint64_t hash, uint32_t width, uint32_t height, uint32_t gx2_format, int mip = 0);
// Formats the hash names cover (as in Cemu-Enhanced): the block-compressed ones and RGBA8.
bool hashable_format(uint32_t gx2_format);

enum class Filter : uint8_t { None = 0, Nearest = 1, Linear = 2 };

// A DDS image: DXGI format, levels back to back (block-compressed or 32-bit texels).
struct Dds {
    uint32_t dxgi = 0, width = 0, height = 0, levels = 0;
    std::vector<uint8_t> data;
    std::vector<size_t> offsets;  // per level into data
};
// Bytes of one level, or 0 for a format we don't handle. Block formats count 4x4 blocks.
size_t level_bytes(uint32_t dxgi, uint32_t width, uint32_t height);
bool read_dds(const std::filesystem::path& file, Dds& out, std::string* error);
bool write_dds(const std::filesystem::path& file, uint32_t dxgi, uint32_t width, uint32_t height, const uint8_t* data, size_t n);

// The packs below a root: "<folder>" or "<title id>/<folder>", sorted (which is also their priority).
std::vector<std::string> list_packs(const std::filesystem::path& root);

class Library {
public:
    // Indexes the enabled packs (all but `disabled`) and the loose files. Problems (a bad textures.ini line, a
    // missing file) go to `warnings`. Returns the number of files indexed.
    size_t scan(const std::filesystem::path& root, const std::set<std::string>& disabled = {},
                std::vector<std::string>* warnings = nullptr);
    // The key a texture is found under (its current hash, or the plain one when only an older pack has it), 0 if no
    // pack replaces it.
    uint64_t resolve(uint64_t current, uint64_t raw, uint32_t width, uint32_t height, uint32_t gx2_format) const;
    const std::filesystem::path* file(uint64_t key, int mip) const;
    // Cha-Cha's older tex_<w>x<h>_<crc> names
    const std::filesystem::path* find(uint32_t width, uint32_t height, uint32_t crc) const;
    bool has_crc_names() const { return !crc_files_.empty(); }
    // [filtering] for a texture, by either of its hashes
    Filter filter(uint64_t current, uint64_t raw) const;
    size_t size() const { return count_; }
    bool empty() const { return !count_ && filters_.empty(); }
    size_t filters() const { return filters_.size(); }
    // textures provided by more than one enabled pack (the first keeps it)
    size_t conflicts() const { return conflicts_; }

private:
    struct Entry {
        std::filesystem::path path;
        uint32_t name_w = 0, name_h = 0, name_fmt = 0;  // what a hash file name says about the game texture
    };
    void index_folder(const std::filesystem::path& dir, bool recurse, const std::string& pack, std::vector<std::string>* warnings);
    std::unordered_map<uint64_t, std::map<int, Entry>> files_;  // hash -> mip -> file
    std::unordered_map<uint64_t, std::filesystem::path> crc_files_;
    std::unordered_map<uint64_t, Filter> filters_;
    std::unordered_map<uint64_t, std::string> claimed_by_;  // hash/mip or crc key -> pack (conflicts)
    size_t count_ = 0, conflicts_ = 0;
};

}  // namespace mh3u::gfx::texpack
