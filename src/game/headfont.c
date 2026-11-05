/*
 * headfont.c - the heading font's W and the TAKEDAWN ONLINE heading; see
 * headfont.h.
 *
 * The heading font (the font list's third entry, [0x3E7BB0] on NTSC-U) is a
 * "3rev" block in the executable's .data:
 *   +0x00  "3rev"            +0x18  1 once the title has set it up
 *   +0x1C  the missing-letter box's record, also the end of every search
 *   +0x20  128 pointers: the first record to try for (code & 0x7F)
 *   +0x220 31 records of 32 bytes: u, v, w, h, x and y offset, advance (floats,
 *          fractions of the atlas), the UTF-16 code; the box (code 0) last
 *   +0x680 the atlas's texture header, +0x700 its 128x256 DXT5 texels
 * The texture header's Data (+0x684) is where the GPU reads the texels: the
 * title copies them to contiguous memory when it sets the font up (NTSC-U:
 * physical 0x3D6200), so the W goes into that copy, and into the .data
 * original too, in case the title copies it again.
 * Its offsets are made into pointers once, by the title (sub_0002EF90, which
 * sets +0x18). A lookup takes table[c & 0x7F] and steps on by 32 bytes until
 * the code matches or it reaches +0x1C's record (Ä is found from D's slot,
 * the next record). So the W record must come before the box in one run of
 * records: the 31 are copied to a heap block with the W between Ú and the
 * box, and the 128 pointers and +0x1C point into the copy.
 */
#include "headfont.h"
#include "onlinebrand.h"
#include "region.h"
#include "recomp_types.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(BO3_HAVE_GLYPH_ART) && !defined(BO3_HEADFONT_TEST)
#include "bo3_glyph_art.h"          /* bo3_glyph_headfont_w, tools/glyphs/build.py */
#define HF_HAVE_ART 1
#elif defined(BO3_HEADFONT_TEST)
extern uint32_t bo3_glyph_headfont_w[HF_W_W * HF_W_H];     /* the test's own picture */
#define HF_HAVE_ART 1
#endif

extern RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp;
extern ptrdiff_t g_xbox_mem_offset;
extern void BO3_SUB(sub_001D2879, sub_001D2B99, sub_001D2C39)(void);
#ifdef BO3_HEADFONT_TEST
extern void *bo3_headfont_test_ptr(uint32_t);
extern void bo3_headfont_test_push(uint32_t);
#define PTR8(v) ((uint8_t *)bo3_headfont_test_ptr((uint32_t)(v)))
#undef PUSH32
#define PUSH32(sp, v) do { (void)(sp); bo3_headfont_test_push((v)); } while (0)
#else
#define PTR8(v) ((uint8_t *)((uintptr_t)g_xbox_mem_offset + (uint32_t)(v)))
#endif
#define M32(v) (*(uint32_t *)PTR8(v))
#define M16(v) (*(uint16_t *)PTR8(v))

/* ── DXT5 ─────────────────────────────────────────────────────────────── */

void bo3_headfont_dxt5_block(const uint8_t alpha[16], uint8_t out[16])
{
    uint8_t lo = 255, hi = 0, pal[8];
    uint64_t bits = 0;
    int i, k;

    for (i = 0; i < 16; i++) {
        if (alpha[i] < lo) lo = alpha[i];
        if (alpha[i] > hi) hi = alpha[i];
    }
    /* a0 > a1: eight levels, both ends exact (0 stays 0, 255 stays 255). */
    pal[0] = hi;
    pal[1] = lo;
    for (k = 2; k < 8; k++)
        pal[k] = (uint8_t)(((8 - k) * hi + (k - 1) * lo + 3) / 7);
    for (i = 0; i < 16; i++) {
        int best = 0, err = 256;
        for (k = 0; k < (hi > lo ? 8 : 1); k++) {
            int e = alpha[i] > pal[k] ? alpha[i] - pal[k] : pal[k] - alpha[i];
            if (e < err) {
                err = e;
                best = k;
            }
        }
        bits |= (uint64_t)best << (3 * i);
    }
    out[0] = hi;
    out[1] = lo;
    for (i = 0; i < 6; i++)
        out[2 + i] = (uint8_t)(bits >> (8 * i));
    /* The colour half: white, both ends, every index 0. */
    out[8] = out[9] = out[10] = out[11] = 0xFF;
    out[12] = out[13] = out[14] = out[15] = 0;
}

uint8_t bo3_headfont_dxt5_alpha(const uint8_t b[16], int x, int y)
{
    uint64_t bits = 0;
    int i, k, a0 = b[0], a1 = b[1];

    for (i = 0; i < 6; i++)
        bits |= (uint64_t)b[2 + i] << (8 * i);
    k = (int)((bits >> (3 * (y * 4 + x))) & 7);
    if (k == 0) return (uint8_t)a0;
    if (k == 1) return (uint8_t)a1;
    if (a0 > a1) return (uint8_t)(((8 - k) * a0 + (k - 1) * a1) / 7);
    if (k == 6) return 0;
    if (k == 7) return 255;
    return (uint8_t)(((6 - k) * a0 + (k - 1) * a1) / 5);
}

#ifdef HF_HAVE_ART

/* ── the font ─────────────────────────────────────────────────────────── */

#define HF_FONT     BO3_VA(0x003D9CB8u, 0x003D9FD8u, 0x003DA078u)
#define HF_HEADTAB  BO3_VA(0x004D5330u, 0x004D5650u, 0x004D56F0u)   /* [[HEADTAB] + 0xC]: the heading strings */
#define HF_MAGIC    0x76657233u     /* "3rev" */
#define HF_READY    0x18u
#define HF_END      0x1Cu
#define HF_INDEX    0x20u
#define HF_RECS     0x220u
#define HF_NREC     31u
#define HF_BOX      (HF_RECS + 30u * 32u)
#define HF_TEXDATA  0x684u
#define HF_TEXFMT   0x68Cu
#define HF_ATLAS_BYTES 0x8000u
#define HF_ATLAS    0x700u
#define HF_FORMAT   0x08710F29u     /* DXT5, 128x256, one level */
#define HF_TAG      0x54414B46u     /* the heap block's tag */

/* The 31 records' codes, in the order the executable has them. */
static const uint16_t k_codes[HF_NREC] = {
    ' ', '-', 'A', 'B', 'C', 'D', 0xC4, 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
    'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 0xD6, 'X', 'Y', 'Z', 0xDA, 0
};

static int s_font;          /* 0 not yet, 1 has the W, -1 left alone */
static uint32_t s_blk;      /* the records' copy */

static void give_up(const char *why)
{
    s_font = -1;
    fprintf(stderr, "[MENU] heading font: no W added, %s; the heading stays XBOX LIVE\n", why);
}

/* The title's allocator, as onlinebrand.c calls it (the block lives as long as
 * the process). 0 when it has none. */
static uint32_t title_alloc(uint32_t bytes)
{
    uint32_t a = g_eax, c = g_ecx, d = g_edx, sp = g_esp, block;

    PUSH32(g_esp, HF_TAG);
    PUSH32(g_esp, bytes);
    PUSH32(g_esp, 0);
    BO3_SUB(sub_001D2879, sub_001D2B99, sub_001D2C39)();
    block = g_eax;
    g_eax = a; g_ecx = c; g_edx = d; g_esp = sp;
    return block;
}

/* The DXT5 block at (bx, by) of the atlas (32 blocks a row). */
static uint8_t *atlas_block(uint32_t font, int bx, int by)
{
    return PTR8(font + HF_ATLAS + (uint32_t)(by * 32 + bx) * 16u);
}

/* Every block the W covers holds nothing (alpha 0 everywhere). */
static int atlas_free(uint32_t font)
{
    int bx, by, x, y;

    for (by = HF_W_Y / 4; by < (HF_W_Y + HF_W_H + 3) / 4; by++)
        for (bx = HF_W_X / 4; bx < 32; bx++) {
            const uint8_t *b = atlas_block(font, bx, by);
            for (y = 0; y < 4; y++)
                for (x = 0; x < 4; x++)
                    if (bo3_headfont_dxt5_alpha(b, x, y))
                        return 0;
        }
    return 1;
}

static void atlas_write(uint32_t font)
{
    int bx, by, x, y;

    for (by = HF_W_Y / 4; by < (HF_W_Y + HF_W_H + 3) / 4; by++)
        for (bx = HF_W_X / 4; bx < 32; bx++) {
            uint8_t a[16];
            for (y = 0; y < 4; y++)
                for (x = 0; x < 4; x++) {
                    int ax = bx * 4 + x - HF_W_X, ay = by * 4 + y - HF_W_Y;
                    a[y * 4 + x] = ax >= 0 && ax < HF_W_W && ay >= 0 && ay < HF_W_H
                                   ? (uint8_t)(bo3_glyph_headfont_w[ay * HF_W_W + ax] >> 24) : 0;
                }
            bo3_headfont_dxt5_block(a, atlas_block(font, bx, by));
        }
}

static void put_f(uint32_t va, float f)
{
    memcpy(PTR8(va), &f, 4);
}

static float get_f(uint32_t va)
{
    float f;
    memcpy(&f, PTR8(va), 4);
    return f;
}

static void font_step(void)
{
    const uint32_t f = HF_FONT, recs = f + HF_RECS, box = f + HF_BOX;
    uint32_t i, end, v_rec, w_rec, gpu;

    if (s_font < 0)
        return;
    if (M32(f) != HF_MAGIC) {
        give_up("the executable has no heading font where it was expected");
        return;
    }
    if (M32(f + HF_READY) != 1)
        return;                                     /* the title has not set it up yet */
    end = M32(f + HF_END);
    if (s_font == 1) {
        if (end != s_blk + 31u * 32u)
            give_up("its table changed after the W was added");
        return;
    }
    if (end != box) {
        give_up("its table is not the title's");
        return;
    }
    for (i = 0; i < HF_NREC; i++)
        if (M16(recs + i * 32u + 0x1C) != k_codes[i] || M16(recs + i * 32u + 0x1E) != 0) {
            give_up("its letters are not the expected ones");
            return;
        }
    for (i = 0; i < 128; i++) {
        uint32_t p = M32(f + HF_INDEX + i * 4u);
        if (p < recs || p > box || (p - recs) % 32u) {
            give_up("its lookup table points outside its letters");
            return;
        }
    }
    if (M32(f + HF_INDEX + 'W' * 4u) != box || M32(f + HF_TEXFMT) != HF_FORMAT || !atlas_free(f)) {
        give_up("its W slot or its atlas is not as expected");
        return;
    }
    /* The texels the GPU reads: a copy in RAM, or the original itself. */
    gpu = M32(f + HF_TEXDATA);
    if (gpu == f + HF_ATLAS) {
        gpu = 0;
    } else if (gpu >= 0x04000000u - HF_ATLAS_BYTES
               || memcmp(PTR8(0x80000000u | gpu), PTR8(f + HF_ATLAS), HF_ATLAS_BYTES)) {
        give_up("its atlas's texels are not where the texture says");
        return;
    }
    s_blk = title_alloc(32u * 32u);
    if (!s_blk) {
        give_up("no memory");
        return;
    }
    memcpy(PTR8(s_blk), PTR8(recs), 30u * 32u);              /* space .. Ú */
    memcpy(PTR8(s_blk + 31u * 32u), PTR8(box), 32u);          /* the box, still last */
    /* The W: our cell, V's height and offsets, V's spacing at our width. */
    v_rec = recs + 24u * 32u;
    w_rec = s_blk + 30u * 32u;
    memcpy(PTR8(w_rec), PTR8(v_rec), 32u);
    put_f(w_rec + 0x00, (float)HF_W_X / 128.0f);
    put_f(w_rec + 0x04, (float)HF_W_Y / 256.0f);
    put_f(w_rec + 0x08, (float)HF_W_W / 128.0f);
    put_f(w_rec + 0x0C, (float)HF_W_H / 256.0f);
    put_f(w_rec + 0x18, get_f(v_rec + 0x18) + (float)HF_W_W / 128.0f - get_f(v_rec + 0x08));
    M32(w_rec + 0x1C) = 'W';
    atlas_write(f);
    if (gpu)
        memcpy(PTR8(0x80000000u | gpu), PTR8(f + HF_ATLAS), HF_ATLAS_BYTES);
    for (i = 0; i < 128; i++) {
        uint32_t k = (M32(f + HF_INDEX + i * 4u) - recs) / 32u;
        M32(f + HF_INDEX + i * 4u) = s_blk + (k == 30 ? 31u : k) * 32u;
    }
    M32(f + HF_INDEX + 'W' * 4u) = w_rec;
    M32(f + HF_END) = s_blk + 31u * 32u;
    s_font = 1;
    fprintf(stderr, "[MENU] heading font: W added (atlas %u,%u %ux%u, records at 0x%08X)\n",
            HF_W_X, HF_W_Y, HF_W_W, HF_W_H, s_blk);
}

/* ── the heading strings ──────────────────────────────────────────────── */

#define HF_MAX_FILE 0x1000u     /* the heading files are a few hundred bytes */
#define HF_MAX_N    64u

static uint32_t s_table, s_mark;

static void heading_step(void)
{
    uint32_t t = M32(HF_HEADTAB), count, slots, i, total = 0, changed = 0, block;
    uint32_t sizes[HF_MAX_N], offsets[HF_MAX_N];

    if (t < 0x80010000u || t > 0x84000000u - HF_MAX_FILE)
        return;
    count = M32(t + 8);
    slots = M32(t + 12);
    if (count < 1 || count > HF_MAX_N || slots != t + 16)
        return;
    if (t == s_table && M32(slots) == s_mark)
        return;                                     /* this load is done */
    for (i = 0; i < count; i++) {
        uint32_t p = M32(slots + i * 4u), delta = p - t;
        size_t n;
        sizes[i] = 0;
        if ((p & 1u) || delta < 16u + count * 4u || delta >= HF_MAX_FILE)
            return;                                 /* not relocated yet */
        n = bo3_onlinebrand_replace((const uint16_t *)PTR8(p), (HF_MAX_FILE - delta) / 2u, NULL, 0);
        if (!n || n > 64)
            continue;
        offsets[i] = total;
        sizes[i] = (uint32_t)n;
        total += (uint32_t)n * 2u;
        changed++;
    }
    if (changed) {
        block = title_alloc(total);
        if (!block)
            return;
        for (i = 0; i < count; i++) {
            uint32_t old = M32(slots + i * 4u);
            if (!sizes[i])
                continue;
            if (!bo3_onlinebrand_replace((const uint16_t *)PTR8(old), (HF_MAX_FILE - (old - t)) / 2u,
                                         (uint16_t *)PTR8(block + offsets[i]), sizes[i]))
                continue;
            M32(slots + i * 4u) = block + offsets[i];
        }
        fprintf(stderr, "[MENU] heading: replaced %u string%s (TAKEDAWN ONLINE)\n", changed,
                changed == 1 ? "" : "s");
    }
    s_table = t;
    s_mark = M32(slots);
}

#ifdef BO3_HEADFONT_TEST
void bo3_headfont_test_reset(void)
{
    s_font = 0;
    s_blk = s_table = s_mark = 0;
}
#endif

void bo3_headfont_apply(void)
{
#ifndef BO3_HEADFONT_TEST
    if (!g_xbox_mem_offset)
        return;
#endif
    font_step();
    if (s_font == 1)
        heading_step();
}

#else

void bo3_headfont_apply(void)
{
}

#endif
