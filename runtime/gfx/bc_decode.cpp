#include "bc_decode.h"

#include <algorithm>
#include <cstring>

namespace mh3u::gfx::bc {
namespace {

uint16_t rd16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | p[1] << 8); }
uint32_t rd32(const uint8_t* p) { return uint32_t{p[0]} | uint32_t{p[1]} << 8 | uint32_t{p[2]} << 16 | uint32_t{p[3]} << 24; }

void rgb565(uint16_t c, uint8_t out[3]) {
    const uint32_t r = c >> 11 & 31, g = c >> 5 & 63, b = c & 31;
    out[0] = static_cast<uint8_t>(r << 3 | r >> 2);
    out[1] = static_cast<uint8_t>(g << 2 | g >> 4);
    out[2] = static_cast<uint8_t>(b << 3 | b >> 2);
}

// BC1 colour block (8 bytes) -> RGBA. three_colour: BC1's c0 <= c1 mode (index 3 transparent black); BC2 / BC3
// always use the four-colour mode.
void colour_block(const uint8_t* b, bool allow_three, uint8_t* dst, size_t pitch) {
    const uint16_t c0 = rd16(b), c1 = rd16(b + 2);
    uint8_t pal[4][4];
    rgb565(c0, pal[0]);
    rgb565(c1, pal[1]);
    pal[0][3] = pal[1][3] = 255;
    if (c0 > c1 || !allow_three) {
        for (int ch = 0; ch < 3; ++ch) {
            pal[2][ch] = static_cast<uint8_t>((2 * pal[0][ch] + pal[1][ch] + 1) / 3);
            pal[3][ch] = static_cast<uint8_t>((pal[0][ch] + 2 * pal[1][ch] + 1) / 3);
        }
        pal[2][3] = pal[3][3] = 255;
    } else {
        for (int ch = 0; ch < 3; ++ch) pal[2][ch] = static_cast<uint8_t>((pal[0][ch] + pal[1][ch]) / 2);
        pal[2][3] = 255;
        pal[3][0] = pal[3][1] = pal[3][2] = pal[3][3] = 0;
    }
    const uint32_t idx = rd32(b + 4);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) std::memcpy(dst + y * pitch + x * 4, pal[idx >> (2 * (y * 4 + x)) & 3], 4);
}

// BC4-style 8-byte block (two endpoints, 3-bit indices) -> one channel, `stride` bytes between pixels
void alpha_block(const uint8_t* b, bool snorm, uint8_t* dst, size_t pitch, size_t stride) {
    int v[8];
    if (snorm) {
        const int a0 = std::max(-127, static_cast<int>(static_cast<int8_t>(b[0]))), a1 = std::max(-127, static_cast<int>(static_cast<int8_t>(b[1])));
        v[0] = a0;
        v[1] = a1;
        if (a0 > a1) {
            for (int i = 1; i < 7; ++i) v[i + 1] = ((7 - i) * a0 + i * a1) / 7;
        } else {
            for (int i = 1; i < 5; ++i) v[i + 1] = ((5 - i) * a0 + i * a1) / 5;
            v[6] = -127;
            v[7] = 127;
        }
    } else {
        const int a0 = b[0], a1 = b[1];
        v[0] = a0;
        v[1] = a1;
        if (a0 > a1) {
            for (int i = 1; i < 7; ++i) v[i + 1] = ((7 - i) * a0 + i * a1 + 3) / 7;
        } else {
            for (int i = 1; i < 5; ++i) v[i + 1] = ((5 - i) * a0 + i * a1 + 2) / 5;
            v[6] = 0;
            v[7] = 255;
        }
    }
    uint64_t idx = 0;
    for (int i = 0; i < 6; ++i) idx |= uint64_t{b[2 + i]} << (8 * i);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) {
            const int value = v[idx >> (3 * (y * 4 + x)) & 7];
            dst[y * pitch + x * stride] = static_cast<uint8_t>(snorm ? static_cast<uint8_t>(static_cast<int8_t>(value)) : value);
        }
}

}  // namespace

void decode_block(int kind, bool snorm, const uint8_t* b, uint8_t* dst, size_t pitch) {
    switch (kind) {
        case 1: colour_block(b, true, dst, pitch); break;
        case 2:  // explicit 4-bit alpha, then a colour block
            colour_block(b + 8, false, dst, pitch);
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 4; ++x) {
                    const int i = y * 4 + x;
                    const uint32_t a = b[i / 2] >> (4 * (i & 1)) & 15;
                    dst[y * pitch + x * 4 + 3] = static_cast<uint8_t>(a * 17);
                }
            break;
        case 3:  // interpolated alpha, then a colour block
            colour_block(b + 8, false, dst, pitch);
            alpha_block(b, false, dst + 3, pitch, 4);
            break;
        case 4: alpha_block(b, snorm, dst, pitch, 1); break;
        case 5:
            alpha_block(b, snorm, dst, pitch, 2);
            alpha_block(b + 8, snorm, dst + 1, pitch, 2);
            break;
        default: break;
    }
}

void decode(int kind, bool snorm, const uint8_t* blocks, uint32_t blocks_w, uint32_t blocks_h, uint8_t* pixels) {
    const size_t bpp = pixel_bytes(kind), pitch = size_t{blocks_w} * 4 * bpp, bb = block_bytes(kind);
    for (uint32_t by = 0; by < blocks_h; ++by)
        for (uint32_t bx = 0; bx < blocks_w; ++bx)
            decode_block(kind, snorm, blocks + (size_t{by} * blocks_w + bx) * bb, pixels + size_t{by} * 4 * pitch + size_t{bx} * 4 * bpp, pitch);
}

}  // namespace mh3u::gfx::bc
