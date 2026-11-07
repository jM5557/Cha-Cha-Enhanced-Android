// BC1-BC5 (DXT1/3/5, ATI1/2) decoding on the CPU, for GPUs without BC texture support - most phone GPUs (Adreno,
// Mali) on Android. The game's textures are mostly BC1 / BC3; such a texture is decoded when it is uploaded and
// sampled as RGBA8 (BC1-3), R8 (BC4) or RG8 (BC5). Blocks are little-endian, as in guest memory and DDS files.
#pragma once

#include <cstddef>
#include <cstdint>

namespace mh3u::gfx::bc {

// kind: 1 BC1, 2 BC2, 3 BC3, 4 BC4, 5 BC5. snorm: BC4 / BC5 signed (output bytes are int8).
constexpr uint32_t block_bytes(int kind) { return kind == 1 || kind == 4 ? 8 : 16; }
constexpr uint32_t pixel_bytes(int kind) { return kind <= 3 ? 4 : kind == 4 ? 1 : 2; }

// One block -> 4x4 pixels at dst, rows `pitch` bytes apart.
void decode_block(int kind, bool snorm, const uint8_t* block, uint8_t* dst, size_t pitch);
// blocks_w x blocks_h blocks (row-major, tightly packed) -> (4 * blocks_w) x (4 * blocks_h) pixels, tightly packed.
void decode(int kind, bool snorm, const uint8_t* blocks, uint32_t blocks_w, uint32_t blocks_h, uint8_t* pixels);

}  // namespace mh3u::gfx::bc
