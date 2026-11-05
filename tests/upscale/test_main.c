/*
 * The menu picture upscale's pure parts (src/game/upscale_art.c, BO-311):
 * a texture dictionary and LoadScrn.bin made here, decoded; the groups; the
 * colour fill; the 2x2 reduction; the check for a cut PNG.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "upscale_art.h"
#include "upscale_tile.h"

/* The toolkit's key functions, as the game's texture-pack lookup: a stand-in
 * that only says which bytes were hashed. */
int nv2a_texpack_key(const uint8_t *guest, uint32_t color, uint32_t w, uint32_t h,
                     uint32_t pitch, const uint8_t *pal, uint32_t pal_len,
                     uint64_t *hash, uint64_t *palh)
{
    (void)pitch;
    *hash = (uint64_t)guest[0] << 32 | color << 16 | w << 8 | h;
    *palh = pal && pal_len ? 0xABCDull : 0;
    return 1;
}

void nv2a_texpack_key_name(uint64_t hash, uint64_t palh, char *out, size_t n)
{
    if (palh)
        snprintf(out, n, "%016llx-%016llx.png", (unsigned long long)hash, (unsigned long long)palh);
    else
        snprintf(out, n, "%016llx.png", (unsigned long long)hash);
}

static int s_fail, s_pass;
#define CHECK(c) do { if (c) s_pass++; else { s_fail++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static void w32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static void w16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
}

static uint32_t reg(uint32_t fmt, int lw, int lh)
{
    return fmt << 8 | 2u << 4 | 1u << 16 | (uint32_t)lw << 20 | (uint32_t)lh << 24;
}

/* A dictionary of three textures: DXT1 red, DXT1 transparent, P8 4x2. */
static size_t make_txd(uint8_t *d)
{
    uint32_t offs[3] = {0x100, 0x200, 0x300}, i;
    memset(d, 0, 0x800);
    w32(d, 0x543C0000u);
    w32(d + 8, 3);
    w32(d + 12, 16);
    for (i = 0; i < 3; i++)
        w32(d + 16 + 16 * i + 8, offs[i]);
    /* 1: DXT1 4x4, all c0 = red */
    w32(d + 0x100 + 4, 0x80);
    w32(d + 0x100 + 0x0C, reg(0x0C, 2, 2));
    strcpy((char *)d + 0x100 + 0x48, "SATMAP_US");
    w16(d + 0x180, 0xF800);
    w16(d + 0x182, 0x001F);
    w32(d + 0x184, 0);
    /* 2: DXT1 4x4, c0 < c1 and every index 3: transparent */
    w32(d + 0x200 + 4, 0x80);
    w32(d + 0x200 + 0x0C, reg(0x0C, 2, 2));
    strcpy((char *)d + 0x200 + 0x48, "Camchange1");
    w16(d + 0x280, 0x001F);
    w16(d + 0x282, 0xF800);
    w32(d + 0x284, 0xFFFFFFFFu);
    /* 3: P8 4x2: index = swizzled offset; palette k = (B k, G 1, R 10k, A 200) */
    w32(d + 0x300 + 4, 0x80);
    w32(d + 0x300 + 0x0C, reg(0x0B, 2, 1));
    strcpy((char *)d + 0x300 + 0x48, "COMPCAR1");
    for (i = 0; i < 8; i++)
        d[0x380 + i] = (uint8_t)i;
    for (i = 0; i < 256; i++) {
        uint8_t *p = d + 0x380 + 8 + 0x40 + 4 * i;
        p[0] = (uint8_t)i; p[1] = 1; p[2] = (uint8_t)(10 * i); p[3] = 200;
    }
    return 0x380 + 8 + 0x40 + 1024;
}

static void test_txd(void)
{
    static uint8_t d[0x800], ls[0x2000];
    bo3_txd_tex t[8];
    size_t n = make_txd(d);
    int skipped = 0, c;
    uint8_t *px;
    char name[64];
    const uint8_t *dicts[4];
    size_t lens[4];

    c = bo3_txd_list(d, n, t, 8, &skipped);
    CHECK(c == 3 && skipped == 0);
    CHECK(!strcmp(t[0].name, "SATMAP_US") && t[0].w == 4 && t[0].h == 4 && t[0].px_len == 8);
    CHECK(t[2].fmt == BO3_TXD_P8 && t[2].w == 4 && t[2].h == 2 && t[2].pal == d + 0x380 + 8 + 0x40);
    px = bo3_txd_decode(&t[0]);
    CHECK(px && px[0] == 255 && px[1] == 0 && px[2] == 0 && px[3] == 255 && px[63] == 255);
    free(px);
    px = bo3_txd_decode(&t[1]);
    CHECK(px && px[3] == 0 && px[15 * 4 + 3] == 0);
    free(px);
    /* (x, y) is at offset x0 | y0 << 1 | x1 << 2 of the indices */
    px = bo3_txd_decode(&t[2]);
    CHECK(px && px[4 * (1 * 4 + 1)] == 30 && px[4 * (0 * 4 + 2)] == 40 && px[4 * (1 * 4 + 3)] == 70);
    CHECK(px && px[2] == 0 && px[1] == 1 && px[3] == 200);
    free(px);
    CHECK(bo3_txd_pack_name(&t[2], name, sizeof name) && strstr(name, "-000000000000abcd.png"));
    CHECK(bo3_txd_list(d + 4, n - 4, t, 8, NULL) == -1);
    CHECK(bo3_txd_list(d, 20, t, 8, NULL) == -1);

    /* LoadScrn.bin: two entries pointing at the dictionary at 0x800 and 0x1000 */
    memset(ls, 0, sizeof ls);
    w32(ls, 2);
    w32(ls + 8 + 8, (uint32_t)n);
    w32(ls + 8 + 12, 0);
    w32(ls + 24 + 8, (uint32_t)n);
    w32(ls + 24 + 12, 0x800);
    memcpy(ls + 0x800, d, n);
    memcpy(ls + 0x1000, d, n);
    CHECK(bo3_loadscrn_dicts(ls, sizeof ls, dicts, lens, 4) == 2 && dicts[1] == ls + 0x1000 && lens[0] == n);
    CHECK(bo3_loadscrn_dicts(d, n, dicts, lens, 4) == -1);
}

static void test_groups(void)
{
    bo3_txd_tex t;
    memset(&t, 0, sizeof t);
    t.w = 256; t.h = 256;
    strcpy(t.name, "GNRL0001");
    CHECK(bo3_upscale_group("LoadScrn", &t) == BO3_UPS_LOADSCREEN);
    strcpy(t.name, "SATMAP3");
    CHECK(bo3_upscale_group("Frontend", &t) == BO3_UPS_SATMAP);
    strcpy(t.name, "EU_LSAT");
    CHECK(bo3_upscale_group("Frontend", &t) == BO3_UPS_SATMAP);
    strcpy(t.name, "HDCrash");
    CHECK(bo3_upscale_group("Frontend", &t) == BO3_UPS_NEWSPAPER);
    strcpy(t.name, "HDDotsX");
    CHECK(bo3_upscale_group("Frontend", &t) == BO3_UPS_ICON);
    strcpy(t.name, "Lisa-ST1");
    CHECK(bo3_upscale_group("Frontend", &t) == BO3_UPS_POLAROID);
    strcpy(t.name, "titlescreen_logo");
    CHECK(bo3_upscale_group("Frontend", &t) == BO3_UPS_LETTERING);
    strcpy(t.name, "shadow");
    CHECK(bo3_upscale_group("Frontend", &t) == BO3_UPS_SKIP);
    t.w = 128; t.h = 64;
    strcpy(t.name, "COMPCAR7");
    CHECK(bo3_upscale_group("Frontend", &t) == BO3_UPS_CARPICK);
    t.w = 32; t.h = 32;
    strcpy(t.name, "arrow");
    CHECK(bo3_upscale_group("Frontend", &t) == BO3_UPS_SKIP);
    t.w = 256; t.h = 64;
    strcpy(t.name, "B3Logo");
    CHECK(bo3_upscale_group("Global", &t) == BO3_UPS_LETTERING);
    t.w = 128; t.h = 128;
    strcpy(t.name, "MedalGold");
    CHECK(bo3_upscale_group("Global", &t) == BO3_UPS_ICON);
    strcpy(t.name, "HUD_crash_skull");
    CHECK(bo3_upscale_group("Global", &t) == BO3_UPS_SKIP);
    strcpy(t.name, "Buttons");
    CHECK(bo3_upscale_group("Global", &t) == BO3_UPS_SKIP);
    strcpy(t.name, "pickup_boost");
    CHECK(bo3_upscale_group("Global", &t) == BO3_UPS_SKIP);
    strcpy(t.name, "coronaglow");
    CHECK(bo3_upscale_group("Global", &t) == BO3_UPS_SKIP);
    CHECK(bo3_upscale_model(BO3_UPS_LOADSCREEN, BO3_UPS_BEST) == BO3_UPS_MODEL_X4PLUS);
    CHECK(bo3_upscale_model(BO3_UPS_NEWSPAPER, BO3_UPS_BEST) == BO3_UPS_MODEL_GENERAL);
    CHECK(bo3_upscale_model(BO3_UPS_LOADSCREEN, BO3_UPS_FAST) == BO3_UPS_MODEL_GENERAL);
    CHECK(bo3_upscale_keep_scale(BO3_UPS_SATMAP) == 4 && bo3_upscale_keep_scale(BO3_UPS_CARPICK) == 4);
}

static void test_pixels(const char *dir)
{
    uint8_t img[3 * 4] = { 10, 20, 30, 255,  99, 99, 99, 0,  30, 40, 50, 255 };
    uint8_t opaque[4] = { 1, 2, 3, 255 };
    uint8_t q[4 * 4 * 2 * 2] = { 0 }, h[4];
    char p[1024];
    FILE *f;
    uint32_t w = 0, hh = 0;
    static const uint8_t png[] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0, 0, 0, 13, 'I', 'H', 'D', 'R',
                                   0, 0, 2, 0, 0, 0, 1, 0, 8, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 'I', 'E', 'N', 'D',
                                   0xAE, 0x42, 0x60, 0x82 };

    CHECK(bo3_upscale_flatten(img, 3, 1) == 1);
    CHECK(img[4] == 20 && img[5] == 30 && img[6] == 40 && img[7] == 255);
    CHECK(bo3_upscale_flatten(opaque, 1, 1) == 0 && opaque[0] == 1);
    q[0] = 10; q[4] = 20; q[16] = 30; q[20] = 41;     /* the top-left 2x2's first channel */
    bo3_upscale_half(q, 4, 4, h);
    CHECK(h[0] == 25);

    snprintf(p, sizeof p, "%s/whole.png", dir);
    f = fopen(p, "wb");
    fwrite(png, 1, sizeof png, f);
    fclose(f);
    CHECK(bo3_png_complete(p, &w, &hh) && w == 512 && hh == 256);
    f = fopen(p, "wb");
    fwrite(png, 1, sizeof png - 6, f);
    fclose(f);
    CHECK(!bo3_png_complete(p, NULL, NULL));
}


/* ── BO-326: the in-process upscaler's tiles and its backend ─────────── */

/* A stand-in for the model that only looks at its own texel: each input
 * texel becomes a scale x scale block of the same value. A tiled run must
 * then give the same picture as one tile over the whole thing, whatever the
 * tile size, and every output texel must be written exactly once. */
static void fake_model(const float *in, size_t istride, int iw, int ih, float *out, size_t ostride, int scale)
{
    int x, y, c, dx, dy;
    for (c = 0; c < 3; c++)
        for (y = 0; y < ih; y++)
            for (x = 0; x < iw; x++)
                for (dy = 0; dy < scale; dy++)
                    for (dx = 0; dx < scale; dx++)
                        out[c * ostride + (size_t)(y * scale + dy) * iw * scale + x * scale + dx] =
                            in[c * istride + (size_t)y * iw + x];
}

static void run_tiled(const uint8_t *rgba, int w, int h, int tile, int pad, int scale, uint8_t *out)
{
    int i, n = bo3_tile_count(w, h, tile);
    for (i = 0; i < n; i++) {
        bo3_tile t;
        int iw, ih;
        size_t is, os;
        float *in, *o;
        bo3_tile_get(w, h, tile, i, &t);
        iw = t.w + 2 * pad;
        ih = t.h + 2 * pad;
        is = (size_t)iw * ih + 3;       /* a stride with a gap, as ncnn's cstep */
        os = is * scale * scale + 5;
        in = (float *)malloc(is * 3 * sizeof *in);
        o = (float *)malloc(os * 3 * sizeof *o);
        bo3_tile_input(rgba, w, h, &t, pad, in, is);
        fake_model(in, is, iw, ih, o, os, scale);
        bo3_tile_output(o, os, &t, pad, scale, out, w);
        free(in);
        free(o);
    }
}

static void test_tiles(void)
{
    static const int sizes[][2] = { { 512, 256 }, { 128, 64 }, { 200, 200 }, { 201, 13 }, { 37, 450 } };
    static const int tiles[] = { 200, 64, 32, 7 };
    bo3_tile t;
    size_t s, k;
    int ti, i, ok;

    /* realesrgan-ncnn-vulkan's cut: 512 x 256 in tiles of 200 = 3 x 2, the
     * last column 112 wide, the last row 56 high. */
    CHECK(bo3_tile_count(512, 256, 200) == 6);
    CHECK(bo3_tile_get(512, 256, 200, 2, &t) && t.x0 == 400 && t.y0 == 0 && t.w == 112 && t.h == 200);
    CHECK(bo3_tile_get(512, 256, 200, 5, &t) && t.x0 == 400 && t.y0 == 200 && t.w == 112 && t.h == 56);
    CHECK(!bo3_tile_get(512, 256, 200, 6, &t) && !bo3_tile_get(512, 256, 200, -1, &t));
    CHECK(bo3_tile_count(0, 10, 200) == 0 && bo3_tile_count(10, 10, 0) == 0);
    CHECK(bo3_tile_count(1024, 512, 200) == 18);

    /* The mirror: -1 -> 1, -10 -> 10, n -> n - 2 (the edge texel once). */
    CHECK(bo3_tile_reflect(-1, 100) == 1 && bo3_tile_reflect(-10, 100) == 10);
    CHECK(bo3_tile_reflect(100, 100) == 98 && bo3_tile_reflect(109, 100) == 89);
    CHECK(bo3_tile_reflect(5, 100) == 5 && bo3_tile_reflect(0, 1) == 0 && bo3_tile_reflect(-7, 1) == 0);
    ok = 1;
    for (i = -40; i < 60; i++) {        /* a picture smaller than the padding */
        int r = bo3_tile_reflect(i, 3);
        ok = ok && r >= 0 && r < 3;
    }
    CHECK(ok);

    /* Values in and out: 255 -> 1.0 -> 255, 0.5/255 rounds up, below 0 and
     * NaN are 0, above 1 is 255. */
    {
        uint8_t px[4 * 4] = { 255, 128, 0, 7,  1, 2, 3, 4,  5, 6, 7, 8,  9, 10, 11, 12 };
        float in[3 * 4 * 4];
        bo3_tile one = { 0, 0, 2, 2 };
        uint8_t o[4 * 4];
        float planes[3 * 4];
        bo3_tile_input(px, 2, 2, &one, 0, in, 4);
        CHECK(in[0] == 1.0f && in[4] == 128.0f / 255.0f && in[8] == 0.0f);
        planes[0] = 1.0f;  planes[1] = -0.2f;  planes[2] = 2.0f;  planes[3] = 0.5f / 255.0f;
        planes[4] = 0.0f / 0.0f; planes[5] = 0.4999f / 255.0f; planes[6] = 1.0f; planes[7] = 1.0f;
        planes[8] = 0.0f;  planes[9] = 0.0f;   planes[10] = 0.0f; planes[11] = 0.0f;
        memset(o, 0x55, sizeof o);
        bo3_tile_output(planes, 4, &one, 0, 1, o, 2);
        CHECK(o[0] == 255 && o[4] == 0 && o[8] == 255 && o[12] == 1);
        CHECK(o[1] == 0 && o[5] == 0 && o[3] == 255 && o[15] == 255);
    }

    /* Tiled = whole, for every size and tile, with prepadding 10 and 4x. */
    for (s = 0; s < sizeof sizes / sizeof sizes[0]; s++) {
        int w = sizes[s][0], h = sizes[s][1];
        size_t px = (size_t)w * h, opx = px * 16;
        uint8_t *src = (uint8_t *)malloc(px * 4), *whole = (uint8_t *)malloc(opx * 4), *tiled = (uint8_t *)malloc(opx * 4);
        for (k = 0; k < px * 4; k++)
            src[k] = (uint8_t)(k * 2654435761u >> 13);
        run_tiled(src, w, h, w > h ? w : h, 10, 4, whole);
        ok = 1;
        for (k = 0; k < opx; k++) {     /* the stand-in is the identity per texel */
            size_t x = k % ((size_t)w * 4), y = k / ((size_t)w * 4), sk = (y / 4 * w + x / 4) * 4;
            ok = ok && whole[k * 4] == src[sk] && whole[k * 4 + 1] == src[sk + 1] && whole[k * 4 + 2] == src[sk + 2]
                 && whole[k * 4 + 3] == 255;
        }
        CHECK(ok);
        for (ti = 0; ti < (int)(sizeof tiles / sizeof tiles[0]); ti++) {
            memset(tiled, 0x5A, opx * 4);
            run_tiled(src, w, h, tiles[ti], 10, 4, tiled);
            CHECK(!memcmp(tiled, whole, opx * 4));
        }
        free(src);
        free(whole);
        free(tiled);
    }

    /* The tile's input around an inner edge: the picture's own texels, not a
     * mirror (only the picture's outer edge is mirrored). */
    {
        uint8_t src[40 * 30 * 4];
        float in[3 * 30 * 30];
        bo3_tile_get(40, 30, 10, 1, &t);        /* x0 10, y0 0 */
        for (k = 0; k < sizeof src; k++)
            src[k] = (uint8_t)k;
        bo3_tile_input(src, 40, 30, &t, 10, in, 30 * 30);
        /* input (0, 10) is picture (0, 0): x0 - pad = 0 */
        CHECK(in[10 * 30 + 0] == src[0] / 255.0f);
        /* input (0, 0) is picture (0, row 10 mirrored: -10 -> 10) */
        CHECK(in[0] == src[(10 * 40 + 0) * 4] / 255.0f);
    }

    /* A black output from a lit input is broken; from a dark one it is not. */
    {
        float z[3 * 8], lit[3 * 8];
        memset(z, 0, sizeof z);
        memcpy(lit, z, sizeof z);
        lit[13] = 0.3f;
        CHECK(bo3_tile_blank(z, 8, 8, 0.9f) && !bo3_tile_blank(z, 8, 8, 0.01f));
        CHECK(!bo3_tile_blank(lit, 8, 8, 0.9f));
        z[3] = 0.0f / 0.0f;
        CHECK(bo3_tile_blank(z, 8, 8, 0.9f));   /* NaN is as broken as black */
    }
}

static void test_backend(void)
{
    const char *lib = "/data/app/x/lib/arm64", *tu = "libvulkan_freedreno.so";
    bo3_backend_facts f;
    char why[96];

    /* The app's own Turnip, asked for from its own folder: Vulkan. */
    memset(&f, 0, sizeof f);
    f.have_vulkan = 1;
    f.opened = tu;
    f.driver_dir = "/data/app/x/lib/arm64/";
    f.driver_name = tu;
    f.lib_dir = lib;
    f.bundled = tu;
    CHECK(bo3_upscale_backend_pick(&f, why, sizeof why) == BO3_UPS_VULKAN);
    /* ... but not when Vulkan failed here before, nor in a build without it,
     * nor when the player said cpu. */
    f.failed_before = 1;
    CHECK(bo3_upscale_backend_pick(&f, why, sizeof why) == BO3_UPS_CPU && strstr(why, "before"));
    f.failed_before = 0;
    f.have_vulkan = 0;
    CHECK(bo3_upscale_backend_pick(&f, why, sizeof why) == BO3_UPS_CPU);
    f.have_vulkan = 1;
    f.override = "cpu";
    CHECK(bo3_upscale_backend_pick(&f, why, sizeof why) == BO3_UPS_CPU);
    f.override = "auto";
    CHECK(bo3_upscale_backend_pick(&f, why, sizeof why) == BO3_UPS_VULKAN);
    /* The device's own driver (stock Adreno, BO-140): the CPU. */
    f.opened = "system";
    f.driver_dir = NULL;
    f.driver_name = NULL;
    CHECK(bo3_upscale_backend_pick(&f, why, sizeof why) == BO3_UPS_CPU && strstr(why, "own"));
    f.opened = NULL;
    CHECK(bo3_upscale_backend_pick(&f, why, sizeof why) == BO3_UPS_CPU);
    /* =vulkan asks for it on any driver. */
    f.override = "vulkan";
    CHECK(bo3_upscale_backend_pick(&f, why, sizeof why) == BO3_UPS_VULKAN);
    f.override = NULL;
    /* A package of the player's with the same file name, from its own folder:
     * not the app's. */
    f.opened = tu;
    f.driver_dir = "/sdcard/Android/data/x/files/driver/turnip-25";
    f.driver_name = tu;
    CHECK(bo3_upscale_backend_pick(&f, why, sizeof why) == BO3_UPS_CPU);
    /* A package of the player's that could not be opened, and the app's own
     * Turnip opened in its place: Vulkan. */
    f.driver_name = "vulkan.custom.so";
    CHECK(bo3_upscale_backend_pick(&f, why, sizeof why) == BO3_UPS_VULKAN);
    /* A package of the player's that opened: not ours. */
    f.opened = "vulkan.custom.so";
    CHECK(bo3_upscale_backend_pick(&f, why, sizeof why) == BO3_UPS_CPU);
    /* The loader says the chosen driver failed and nothing took its place. */
    f.opened = tu;
    f.driver_dir = lib;
    f.driver_name = tu;
    f.open_failed = 1;
    CHECK(bo3_upscale_backend_pick(&f, why, sizeof why) == BO3_UPS_CPU);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    test_txd();
    test_groups();
    test_pixels(dir);
    test_tiles();
    test_backend();
    printf("%d passed, %d failed\n", s_pass, s_fail);
    return s_fail ? 1 : 0;
}
