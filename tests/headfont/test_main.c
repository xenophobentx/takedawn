/* headfont - the heading font's W and the TAKEDAWN ONLINE heading
 * (src/game/headfont.c), on a made-up font laid out as the executable's is:
 * no byte of the game here, the records' numbers are invented.
 *   cmake --build <build> --target bo3_headfont_test && ctest -R bo3_headfont */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "recomp_types.h"
#include "region.h"
#include "headfont.h"

#define FONT 0x003D9CB8u
#define HEADTAB 0x004D5330u
#define RAM 0x80180000u
#define T 0x80182000u
#define GPU 0x001A0000u             /* physical: 0x801A0000 here */

_Alignas(4) static uint8_t font[0x700 + 128 * 256], ram[0x100000];
static uint32_t headtab, allocations;
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp;
ptrdiff_t g_xbox_mem_offset = 1;
uint32_t bo3_glyph_headfont_w[HF_W_W * HF_W_H];
void bo3_headfont_test_reset(void);

void *bo3_headfont_test_ptr(uint32_t p)
{
    if (p >= HEADTAB && p < HEADTAB + 4) return (uint8_t *)&headtab + (p - HEADTAB);
    if (p >= FONT && p < FONT + sizeof font) return font + (p - FONT);
    assert(p >= RAM && p < RAM + sizeof ram);
    return ram + (p - RAM);
}
void bo3_headfont_test_push(uint32_t n) { (void)n; }
void BO3_SUB(sub_001D2879, sub_001D2B99, sub_001D2C39)(void) { g_eax = RAM + 0x60000u + allocations++ * 0x1000u; }

static uint32_t *w32(uint32_t p) { return bo3_headfont_test_ptr(p); }
static uint16_t code_at(uint32_t rec) { return *(uint16_t *)bo3_headfont_test_ptr(rec + 0x1C); }
static float f_at(uint32_t p) { float f; memcpy(&f, bo3_headfont_test_ptr(p), 4); return f; }

static const uint16_t codes[31] = {
    ' ', '-', 'A', 'B', 'C', 'D', 0xC4, 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
    'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 0xD6, 'X', 'Y', 'Z', 0xDA, 0
};

/* The font as the title leaves it after setting it up: pointers, +0x18 = 1. */
static void make_font(int ready)
{
    uint32_t i, c;
    memset(font, 0, sizeof font);
    *w32(FONT) = 0x76657233u;
    for (i = 0; i < 31; i++) {
        uint32_t r = FONT + 0x220 + i * 32;
        float v[7] = { i / 64.0f, 0.1f, 0.15f, 38 / 256.0f, -1 / 128.0f, 0.0407f, 0.1875f };
        memcpy(bo3_headfont_test_ptr(r), v, sizeof v);
        *w32(r + 0x1C) = codes[i];
    }
    for (c = 0; c < 128; c++) {
        uint32_t k = 30;
        for (i = 0; i < 30; i++)
            if (codes[i] == c) k = i;
        *w32(FONT + 0x20 + c * 4) = (ready ? FONT : 0) + 0x220 + k * 32;
    }
    *w32(FONT + 0x1C) = (ready ? FONT : 0) + 0x220 + 30 * 32;
    *w32(FONT + 0x18) = (uint32_t)ready;
    *w32(FONT + 0x68C) = 0x08710F29u;
    /* a letter in the block left of the W's corner, which must stay */
    font[0x700 + (50 * 32 + 22) * 16] = 255;
    /* the GPU's copy of the texels, as the title makes it */
    *w32(FONT + 0x684) = GPU;
    memcpy(bo3_headfont_test_ptr(0x80000000u | GPU), font + 0x700, 0x8000);
}

/* The title's lookup (sub_0002E7A0): table[c & 0x7F], then on by 32 to the code or the end. */
static uint32_t lookup(uint16_t c)
{
    uint32_t g = *w32(FONT + 0x20 + (c & 0x7Fu) * 4), end = *w32(FONT + 0x1C);
    int steps = 0;
    while (code_at(g) != c && g != end) {
        g += 32;
        assert(++steps < 40);
    }
    return g;
}

static void str(uint32_t p, const char *s) { uint16_t *d = bo3_headfont_test_ptr(p); while ((*d++ = (uint8_t)*s++)) {} }
static int eq(uint32_t p, const char *s)
{
    const uint16_t *d = bo3_headfont_test_ptr(p);
    do { if (*d++ != (uint8_t)*s) return 0; } while (*s++);
    return 1;
}

static void make_heading_table(void)
{
    const char *h[] = { "XBOX LIVE", "CRASH", "CRASH NAV", "SINGLE EVENT" };
    uint32_t i, at = T + 16 + 4 * 4;
    headtab = T;
    *w32(T + 8) = 4;
    *w32(T + 12) = T + 16;
    for (i = 0; i < 4; i++) {
        *w32(T + 16 + i * 4) = at;
        str(at, h[i]);
        at += (uint32_t)(strlen(h[i]) + 1) * 2;
    }
}

static void test_dxt5(void)
{
    uint8_t a[16], b[16];
    int i, x, y;
    for (i = 0; i < 16; i++) a[i] = (uint8_t)(i * 17);
    bo3_headfont_dxt5_block(a, b);
    for (y = 0; y < 4; y++)
        for (x = 0; x < 4; x++) {
            int d = bo3_headfont_dxt5_alpha(b, x, y) - a[y * 4 + x];
            assert(d >= -19 && d <= 19);
        }
    assert(bo3_headfont_dxt5_alpha(b, 0, 0) == 0 && bo3_headfont_dxt5_alpha(b, 3, 3) == 255);
    assert(b[8] == 0xFF && b[9] == 0xFF && b[10] == 0xFF && b[11] == 0xFF && !b[12] && !b[15]);
    memset(a, 0, 16);
    bo3_headfont_dxt5_block(a, b);
    for (i = 0; i < 16; i++) assert(bo3_headfont_dxt5_alpha(b, i & 3, i >> 2) == 0);
    a[5] = 255;
    bo3_headfont_dxt5_block(a, b);
    for (i = 0; i < 16; i++) assert(bo3_headfont_dxt5_alpha(b, i & 3, i >> 2) == a[i]);
}

int main(void)
{
    uint32_t i, w, before_alloc;
    uint32_t *art = bo3_glyph_headfont_w;

    test_dxt5();
    for (i = 0; i < HF_W_W * HF_W_H; i++)       /* a filled box with a soft edge */
        art[i] = (uint32_t)((i % HF_W_W) == 0 ? 0x80 : 0xFF) << 24 | 0xFFFFFFu;

    /* Not set up yet: nothing, then the W once it is. */
    make_font(0);
    make_heading_table();
    bo3_headfont_apply();
    assert(allocations == 0 && *w32(FONT + 0x1C) == 0x220 + 30 * 32);
    assert(eq(*w32(T + 16), "XBOX LIVE"));
    make_font(1);
    bo3_headfont_apply();
    assert(allocations == 2);                   /* the records, the heading */
    w = lookup('W');
    assert(code_at(w) == 'W' && w >= RAM);
    assert(fabsf(f_at(w) - HF_W_X / 128.0f) < 1e-6f && fabsf(f_at(w + 4) - HF_W_Y / 256.0f) < 1e-6f);
    assert(fabsf(f_at(w + 8) - HF_W_W / 128.0f) < 1e-6f && fabsf(f_at(w + 12) - 38 / 256.0f) < 1e-6f);
    assert(fabsf(f_at(w + 0x10) + 1 / 128.0f) < 1e-6f);                           /* V's x offset */
    assert(fabsf(f_at(w + 0x18) - (0.1875f + HF_W_W / 128.0f - 0.15f)) < 1e-6f);  /* V's spacing */
    for (i = 0; i < 30; i++) {
        uint32_t g = lookup(codes[i]);
        assert(code_at(g) == codes[i] && g >= RAM && fabsf(f_at(g) - i / 64.0f) < 1e-6f);
    }
    assert(code_at(lookup('!')) == 0 && code_at(lookup(0xD7)) == 0 && code_at(lookup(0xC1)) == 0);
    assert(lookup(0) == *w32(FONT + 0x1C));
    /* The atlas: the W's cell drawn, the neighbour block as it was. */
    {
        const uint8_t *atlas = font + 0x700;
        const uint8_t *blk = atlas + ((HF_W_Y / 4) * 32 + HF_W_X / 4) * 16;
        assert(bo3_headfont_dxt5_alpha(blk, 1, 0) == 255);
        assert(bo3_headfont_dxt5_alpha(blk, 0, 0) >= 0x70 && bo3_headfont_dxt5_alpha(blk, 0, 0) <= 0x90);
        assert(atlas[(50 * 32 + 22) * 16] == 255);
        blk = atlas + ((HF_W_Y + 36) / 4 * 32 + (HF_W_X + HF_W_W) / 4) * 16;   /* rows 236.., cols 124.. */
        assert(bo3_headfont_dxt5_alpha(blk, 1, 1) == 255 && bo3_headfont_dxt5_alpha(blk, 2, 1) == 0);
        assert(bo3_headfont_dxt5_alpha(blk, 1, 2) == 0);                       /* row 238: below it */
        assert(!memcmp(bo3_headfont_test_ptr(0x80000000u | GPU), atlas, 0x8000));  /* the GPU's copy too */
    }
    /* The heading: XBOX LIVE only. */
    assert(eq(*w32(T + 16), "TAKEDAWN ONLINE") && eq(*w32(T + 20), "CRASH") && eq(*w32(T + 28), "SINGLE EVENT"));
    /* Again: nothing more. A reload at the same address: the heading again, the font stays. */
    before_alloc = allocations;
    bo3_headfont_apply();
    assert(allocations == before_alloc);
    make_heading_table();
    bo3_headfont_apply();
    assert(allocations == before_alloc + 1 && eq(*w32(T + 16), "TAKEDAWN ONLINE"));
    assert(code_at(lookup('W')) == 'W');

    /* Another font (a letter not where it should be): left alone, and so is the heading. */
    bo3_headfont_test_reset();
    allocations = 0;
    make_font(1);
    *w32(FONT + 0x220 + 24 * 32 + 0x1C) = 'W';
    make_heading_table();
    bo3_headfont_apply();
    assert(allocations == 0 && eq(*w32(T + 16), "XBOX LIVE") && *w32(FONT + 0x1C) == FONT + 0x5E0);
    /* Something already in the W's corner of the atlas: left alone. */
    bo3_headfont_test_reset();
    make_font(1);
    font[0x700 + (52 * 32 + 30) * 16] = 9;
    bo3_headfont_apply();
    assert(allocations == 0 && *w32(FONT + 0x20 + 'W' * 4) == FONT + 0x5E0);
    /* The GPU's copy is not the atlas: left alone. */
    bo3_headfont_test_reset();
    make_font(1);
    ((uint8_t *)bo3_headfont_test_ptr(0x80000000u | GPU))[100] ^= 1;
    bo3_headfont_apply();
    assert(allocations == 0 && *w32(FONT + 0x20 + 'W' * 4) == FONT + 0x5E0);

    puts("heading font tests passed");
    return 0;
}
