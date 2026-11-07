// bc_decode.h: hand-built BC1-BC5 blocks with known pixels (both BC1 modes, BC2 nibbles, BC3 / BC4 eight- and six-value
// modes, signed BC4 / BC5), and the block layout of decode().
#include "bc_decode.h"

#include <cstdio>
#include <cstring>
#include <vector>

using namespace mh3u::gfx::bc;

static int failures = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                 \
        }                                                               \
    } while (0)

static void colour(uint8_t* b, uint16_t c0, uint16_t c1, uint32_t idx) {
    b[0] = uint8_t(c0), b[1] = uint8_t(c0 >> 8), b[2] = uint8_t(c1), b[3] = uint8_t(c1 >> 8);
    for (int i = 0; i < 4; ++i) b[4 + i] = uint8_t(idx >> (8 * i));
}
static void alpha(uint8_t* b, uint8_t a0, uint8_t a1, const int idx[16]) {
    b[0] = a0, b[1] = a1;
    uint64_t bits = 0;
    for (int i = 0; i < 16; ++i) bits |= uint64_t(idx[i]) << (3 * i);
    for (int i = 0; i < 6; ++i) b[2 + i] = uint8_t(bits >> (8 * i));
}
static bool px(const uint8_t* out, int i, int r, int g, int b, int a) {
    return out[i * 4] == r && out[i * 4 + 1] == g && out[i * 4 + 2] == b && out[i * 4 + 3] == a;
}

int main() {
    uint8_t blk[16], out[64];
    // BC1, four colours (c0 > c1): red, blue, 2/3 red, 1/3 red
    colour(blk, 0xF800, 0x001F, 0 | 1 << 2 | 2 << 4 | 3 << 6);
    decode_block(1, false, blk, out, 16);
    CHECK(px(out, 0, 255, 0, 0, 255) && px(out, 1, 0, 0, 255, 255) && px(out, 2, 170, 0, 85, 255) && px(out, 3, 85, 0, 170, 255));
    CHECK(px(out, 15, 255, 0, 0, 255));  // index 0 elsewhere
    // BC1, three colours (c0 <= c1): the average, and index 3 transparent black
    colour(blk, 0x001F, 0xF800, 2 | 3 << 2);
    decode_block(1, false, blk, out, 16);
    CHECK(px(out, 0, 127, 0, 127, 255) && px(out, 1, 0, 0, 0, 0));
    // BC2: explicit alpha nibbles x 17, the colour block always in four-colour mode
    std::memset(blk, 0, 16);
    blk[0] = 0xF0;  // pixel 0 alpha 0, pixel 1 alpha 15
    blk[1] = 0x08;  // pixel 2 alpha 8
    colour(blk + 8, 0x001F, 0xF800, 3);  // c0 <= c1, but no transparent mode in BC2: index 3 = 1/3 c0 + 2/3 c1
    decode_block(2, false, blk, out, 16);
    CHECK(out[3] == 0 && out[7] == 255 && out[11] == 136);
    CHECK(px(out, 0, 170, 0, 85, 0));
    // BC3: eight interpolated alphas (a0 > a1)
    const int idx8[16] = {0, 1, 2, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    alpha(blk, 255, 0, idx8);
    colour(blk + 8, 0xFFFF, 0x0000, 0);
    decode_block(3, false, blk, out, 16);
    CHECK(out[3] == 255 && out[7] == 0 && out[11] == 219 && out[15] == 36 && out[63] == 0);
    CHECK(px(out, 0, 255, 255, 255, 255));
    // BC4: six interpolated values plus 0 and 255 (a0 <= a1)
    const int idx6[16] = {0, 1, 2, 6, 7, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    alpha(blk, 0, 250, idx6);
    decode_block(4, false, blk, out, 4);
    CHECK(out[0] == 0 && out[1] == 250 && out[2] == 50 && out[3] == 0 && out[4] == 255 && out[5] == 200);
    // signed BC4: -128 reads as -127; six-value mode ends in -127 and 127
    alpha(blk, 0x80, 0x7F, idx6);
    decode_block(4, true, blk, out, 4);
    CHECK(int8_t(out[0]) == -127 && int8_t(out[1]) == 127 && int8_t(out[3]) == -127 && int8_t(out[4]) == 127);
    // BC5: two channels, interleaved
    alpha(blk, 10, 20, idx6);
    alpha(blk + 8, 200, 100, idx8);
    decode_block(5, false, blk, out, 8);
    CHECK(out[0] == 10 && out[1] == 200 && out[2] == 20 && out[3] == 100);

    // decode(): 2 x 1 BC1 blocks -> 8 x 4 pixels, the second block to the right of the first
    std::vector<uint8_t> two(16), pixels(8 * 4 * 4);
    colour(two.data(), 0xF800, 0xF800, 0);
    colour(two.data() + 8, 0x07E0, 0x07E0, 0);
    decode(1, false, two.data(), 2, 1, pixels.data());
    CHECK(px(pixels.data(), 0, 255, 0, 0, 255) && px(pixels.data(), 4, 0, 255, 0, 255) && px(pixels.data(), 8 * 3 + 7, 0, 255, 0, 255));
    CHECK(block_bytes(1) == 8 && block_bytes(3) == 16 && pixel_bytes(4) == 1 && pixel_bytes(5) == 2);

    std::printf(failures ? "%d failure(s)\n" : "all checks passed\n", failures);
    return failures ? 1 : 0;
}
