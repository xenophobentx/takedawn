/*
 * upscale_art.c - see upscale_art.h. The dictionary reader follows the
 * setup window's (tools/install/gui/steam_artwork.cpp), with the formats the
 * menu art uses: P8 (swizzled, a 256-entry BGRA palette after the indices),
 * DXT1, DXT3, DXT5.
 */
#include "upscale_art.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kernel/nv2a_texpack.h"        /* nv2a_texpack_key, nv2a_texpack_key_name */

#define TXD_MAGIC 0x543C0000u

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

/* ── dictionaries ─────────────────────────────────────────────────────── */

static size_t level0_bytes(uint32_t fmt, uint32_t w, uint32_t h)
{
    switch (fmt) {
    case BO3_TXD_P8:   return (size_t)w * h;
    case BO3_TXD_DXT1: return (size_t)w * h / 2;
    case BO3_TXD_DXT3:
    case BO3_TXD_DXT5: return (size_t)w * h;
    default:           return 0;
    }
}

int bo3_txd_list(const uint8_t *d, size_t n, bo3_txd_tex *out, int max, int *skipped)
{
    uint32_t count, table, i;
    int got = 0;
    if (skipped)
        *skipped = 0;
    if (!d || n < 16 || rd32(d) != TXD_MAGIC)
        return -1;
    count = rd32(d + 8);
    table = rd32(d + 12);
    if (count > 100000 || table > n || (size_t)count * 16 > n - table)
        return -1;
    for (i = 0; i < count; i++) {
        uint32_t off = rd32(d + table + 16 * i + 8), reg, fmt, w, h, pxo;
        size_t len;
        bo3_txd_tex t;
        if (off > n || n - off < 0x80)
            continue;
        reg = rd32(d + off + 0x0C);
        fmt = (reg >> 8) & 0xFF;
        w = 1u << ((reg >> 20) & 0xF);
        h = 1u << ((reg >> 24) & 0xF);
        pxo = off + rd32(d + off + 0x04);
        len = level0_bytes(fmt, w, h);
        /* Only swizzled 2D textures: the dimension field 2, no linear pitch. */
        if (!len || ((reg >> 4) & 0xF) != 2 || rd32(d + off + 0x10) != 0
                || (fmt != BO3_TXD_P8 && (w < 4 || h < 4))) {
            if (skipped)
                (*skipped)++;
            continue;
        }
        if (pxo < off || pxo > n || n - pxo < len
                || (fmt == BO3_TXD_P8 && n - pxo < len + 0x40 + 1024))
            continue;
        memset(&t, 0, sizeof t);
        memcpy(t.name, d + off + 0x48, 32);
        t.name[32] = 0;
        t.fmt = fmt;
        t.w = w;
        t.h = h;
        t.mips = (reg >> 16) & 0xF;
        t.px = d + pxo;
        t.px_len = len;
        t.pal = fmt == BO3_TXD_P8 ? d + pxo + len + 0x40 : NULL;
        if (got < max)
            out[got] = t;
        got++;
    }
    return got < max ? got : max;
}

int bo3_loadscrn_dicts(const uint8_t *a, size_t n, const uint8_t **dicts, size_t *lens, int max)
{
    uint32_t count, i;
    size_t base;
    int got = 0;
    if (!a || n < 8)
        return -1;
    count = rd32(a);
    if (count == 0 || count > 4096 || 8 + (size_t)count * 16 > n)
        return -1;
    base = (8 + (size_t)count * 16 + 0x7FF) & ~(size_t)0x7FF;
    for (i = 0; i < count && got < max; i++) {
        const uint8_t *e = a + 8 + 16 * i;
        size_t size = rd32(e + 8), off = base + rd32(e + 12);
        if (off > n || n - off < size || size < 16 || rd32(a + off) != TXD_MAGIC)
            continue;
        dicts[got] = a + off;
        lens[got] = size;
        got++;
    }
    return got ? got : -1;
}

/* The console's swizzle: the bits of x and y interleaved, x first, while
 * both have bits; the rest of the longer side's on top. */
static void swizzle_tables(uint32_t w, uint32_t h, uint32_t *xs, uint32_t *ys)
{
    uint32_t xb = 0, yb = 0, pos = 0, xi = 0, yi = 0, xm[16], ym[16], v, i;
    while ((1u << xb) < w) xb++;
    while ((1u << yb) < h) yb++;
    while (xi < xb || yi < yb) {
        if (xi < xb) xm[xi++] = pos++;
        if (yi < yb) ym[yi++] = pos++;
    }
    for (v = 0; v < w; v++) {
        uint32_t s = 0;
        for (i = 0; i < xb; i++) s |= ((v >> i) & 1u) << xm[i];
        xs[v] = s;
    }
    for (v = 0; v < h; v++) {
        uint32_t s = 0;
        for (i = 0; i < yb; i++) s |= ((v >> i) & 1u) << ym[i];
        ys[v] = s;
    }
}

static void c565(uint16_t c, int rgb[3])
{
    rgb[0] = ((c >> 11) & 31) * 255 / 31;
    rgb[1] = ((c >> 5) & 63) * 255 / 63;
    rgb[2] = (c & 31) * 255 / 31;
}

static void dxt_block(const uint8_t *b, uint32_t fmt, uint8_t px[16][4])
{
    const uint8_t *c = fmt == BO3_TXD_DXT1 ? b : b + 8;
    uint16_t c0 = rd16(c), c1 = rd16(c + 2);
    uint32_t bits = rd32(c + 4);
    int pal[4][3], k, i, four = c0 > c1 || fmt != BO3_TXD_DXT1;
    c565(c0, pal[0]);
    c565(c1, pal[1]);
    for (k = 0; k < 3; k++) {
        pal[2][k] = four ? (2 * pal[0][k] + pal[1][k]) / 3 : (pal[0][k] + pal[1][k]) / 2;
        pal[3][k] = four ? (pal[0][k] + 2 * pal[1][k]) / 3 : 0;
    }
    for (i = 0; i < 16; i++) {
        int idx = (bits >> (2 * i)) & 3;
        px[i][0] = (uint8_t)pal[idx][0];
        px[i][1] = (uint8_t)pal[idx][1];
        px[i][2] = (uint8_t)pal[idx][2];
        px[i][3] = fmt == BO3_TXD_DXT1 && !four && idx == 3 ? 0 : 255;
    }
    if (fmt == BO3_TXD_DXT3) {
        for (i = 0; i < 16; i++)
            px[i][3] = (uint8_t)(((b[i / 2] >> (4 * (i & 1))) & 15) * 17);
    } else if (fmt == BO3_TXD_DXT5) {
        int a0 = b[0], a1 = b[1], lv[8];
        uint64_t ab = 0;
        for (i = 0; i < 6; i++)
            ab |= (uint64_t)b[2 + i] << (8 * i);
        lv[0] = a0;
        lv[1] = a1;
        for (i = 0; i < 6; i++) {
            if (a0 > a1)
                lv[2 + i] = ((6 - i) * a0 + (i + 1) * a1) / 7;
            else
                lv[2 + i] = i < 4 ? ((4 - i) * a0 + (i + 1) * a1) / 5 : 0;
        }
        if (a0 <= a1) {
            lv[6] = 0;
            lv[7] = 255;
        }
        for (i = 0; i < 16; i++)
            px[i][3] = (uint8_t)lv[(ab >> (3 * i)) & 7];
    }
}

uint8_t *bo3_txd_decode(const bo3_txd_tex *t)
{
    uint8_t *out;
    uint32_t x, y;
    if (!t || !t->px || !t->w || !t->h || t->w > 4096 || t->h > 4096)
        return NULL;
    out = (uint8_t *)malloc((size_t)t->w * t->h * 4);
    if (!out)
        return NULL;
    if (t->fmt == BO3_TXD_P8) {
        uint32_t *xs = (uint32_t *)malloc(t->w * sizeof *xs), *ys = (uint32_t *)malloc(t->h * sizeof *ys);
        if (!xs || !ys || !t->pal) {
            free(xs);
            free(ys);
            free(out);
            return NULL;
        }
        swizzle_tables(t->w, t->h, xs, ys);
        for (y = 0; y < t->h; y++)
            for (x = 0; x < t->w; x++) {
                const uint8_t *p = t->pal + 4 * t->px[ys[y] + xs[x]];
                uint8_t *o = out + 4 * ((size_t)y * t->w + x);
                o[0] = p[2];
                o[1] = p[1];
                o[2] = p[0];
                o[3] = p[3];
            }
        free(xs);
        free(ys);
        return out;
    }
    if (t->fmt == BO3_TXD_DXT1 || t->fmt == BO3_TXD_DXT3 || t->fmt == BO3_TXD_DXT5) {
        uint32_t bw = t->w / 4, bh = t->h / 4, bs = t->fmt == BO3_TXD_DXT1 ? 8 : 16, bx, by, i;
        for (by = 0; by < bh; by++)
            for (bx = 0; bx < bw; bx++) {
                uint8_t px[16][4];
                dxt_block(t->px + ((size_t)by * bw + bx) * bs, t->fmt, px);
                for (i = 0; i < 16; i++)
                    memcpy(out + 4 * ((size_t)(by * 4 + i / 4) * t->w + bx * 4 + i % 4), px[i], 4);
            }
        return out;
    }
    free(out);
    return NULL;
}

int bo3_txd_pack_name(const bo3_txd_tex *t, char *out, size_t n)
{
    uint64_t hash = 0, palh = 0;
    if (!nv2a_texpack_key(t->px, t->fmt, t->w, t->h, 0, t->pal, t->pal ? 256 : 0, &hash, &palh))
        return 0;
    nv2a_texpack_key_name(hash, palh, out, n);
    return 1;
}

/* ── groups ───────────────────────────────────────────────────────────── */

static int starts(const char *s, const char *p)
{
    return !strncmp(s, p, strlen(p));
}

static int ends_ci(const char *s, const char *p)
{
    size_t a = strlen(s), b = strlen(p), i;
    if (a < b)
        return 0;
    for (i = 0; i < b; i++)
        if (tolower((unsigned char)s[a - b + i]) != tolower((unsigned char)p[i]))
            return 0;
    return 1;
}

static int one_of(const char *s, const char *const *list)
{
    for (; *list; list++)
        if (!strcmp(s, *list))
            return 1;
    return 0;
}

int bo3_upscale_group(const char *src, const bo3_txd_tex *t)
{
    static const char *const satmaps[] = { "ASIA1", "AU_LSAT", "EU_LSAT", "EUROPE1", "US_LSAT", "USA1", NULL };
    static const char *const photos[] = { "advanced", "champion", "driving-skills", "easy", "expert",
                                          "World_Map", "progmap", NULL };
    static const char *const lettering[] = { "titlescreen_logo", "GPIcon", "TButton", "globe", "DD_Icons", NULL };
    const char *n = t->name;
    char up[33];
    size_t i;
    for (i = 0; i < sizeof up - 1 && n[i]; i++)
        up[i] = (char)toupper((unsigned char)n[i]);
    up[i] = 0;
    if (!strcmp(src, "LoadScrn"))
        return BO3_UPS_LOADSCREEN;
    if (!strcmp(src, "XBE"))
        return BO3_UPS_LETTERING;      /* the screens' title lettering (upscale.c names it) */
    if (!strcmp(src, "Global")) {
        /* Global.txd is mostly the race's HUD and effects (not upscaled: the
         * HUD has graphics.ui_scale's Super-xBR); of it only the
         * menus' own art: the logo on the main menu, the EA Trax logo, the
         * medals and event marks, the loading screen's car and bars. */
        static const char *const logos[] = { "B3Logo", "EATrax", NULL };
        static const char *const icons[] = { "MedalBronze", "MedalGold", "MedalSilver", "Cup", "Tick", "Cross",
                                             "loadCAR1", "loadCAR2", "loading_bars", NULL };
        if (one_of(n, logos))
            return BO3_UPS_LETTERING;
        if (one_of(n, icons))
            return BO3_UPS_ICON;
        return BO3_UPS_SKIP;
    }
    if (starts(up, "SATMAP") || one_of(n, satmaps))
        return BO3_UPS_SATMAP;
    if (one_of(n, photos))
        return BO3_UPS_PHOTO;
    if (t->w == 128 && t->h == 64 && strstr(up, "CAR"))
        return BO3_UPS_CARPICK;
    if (ends_ci(n, "-st1") || ends_ci(n, "-st2"))
        return BO3_UPS_POLAROID;
    if (starts(n, "HD") && strcmp(n, "HDDotsX"))
        return BO3_UPS_NEWSPAPER;
    if (starts(n, "PC"))
        return BO3_UPS_POSTCARD;
    if (one_of(n, lettering))
        return BO3_UPS_LETTERING;
    if (starts(n, "Camchange") || !strcmp(n, "shadow"))
        return BO3_UPS_SKIP;
    /* Icons, flags, line art: not the small ones (a model only smears them). */
    return t->w <= 32 && t->h <= 32 ? BO3_UPS_SKIP : BO3_UPS_ICON;
}

const char *bo3_upscale_group_name(int g)
{
    static const char *const names[BO3_UPS_GROUPS] = {
        "skipped", "loading screens", "satellite maps", "event photos", "car pictures", "polaroids",
        "newspapers", "postcards", "lettering", "icons"
    };
    return g >= 0 && g < BO3_UPS_GROUPS ? names[g] : "?";
}

int bo3_upscale_model(int group, int quality)
{
    if (quality == BO3_UPS_FAST || group == BO3_UPS_NEWSPAPER)
        return BO3_UPS_MODEL_GENERAL;
    return BO3_UPS_MODEL_X4PLUS;
}

int bo3_upscale_keep_scale(int group)
{
    /* Every group at the model's 4x. The satellite maps were kept at 2x for
     * memory until the owner's Deck clip (2026-10-30): the Crash Nav zoom
     * stretches the small zoom tiles over the whole screen. */
    (void)group;
    return 4;
}

/* ── pixels ───────────────────────────────────────────────────────────── */

int bo3_upscale_flatten(uint8_t *rgba, uint32_t w, uint32_t h)
{
    size_t n = (size_t)w * h, i;
    uint8_t *known;
    double mean[3] = {0, 0, 0};
    size_t nk = 0;
    int any = 0, pass;
    for (i = 0; i < n; i++)
        if (rgba[4 * i + 3] < 255)
            any = 1;
    if (!any)
        return 0;
    known = (uint8_t *)malloc(n);
    if (!known)
        return 1;
    for (i = 0; i < n; i++) {
        known[i] = rgba[4 * i + 3] > 8;
        if (known[i]) {
            mean[0] += rgba[4 * i];
            mean[1] += rgba[4 * i + 1];
            mean[2] += rgba[4 * i + 2];
            nk++;
        }
    }
    /* Grown a texel ring at a time from what is known, 64 rings at most;
     * whatever is still left takes the mean colour (grey for a texture with
     * nothing known). */
    for (pass = 0; pass < 64; pass++) {
        uint8_t *next = (uint8_t *)malloc(n);
        int grew = 0;
        uint32_t x, y;
        if (!next)
            break;
        memcpy(next, known, n);
        for (y = 0; y < h; y++)
            for (x = 0; x < w; x++) {
                size_t at = (size_t)y * w + x;
                int dx, dy, cnt = 0, acc[3] = {0, 0, 0};
                if (known[at])
                    continue;
                for (dy = -1; dy <= 1; dy++)
                    for (dx = -1; dx <= 1; dx++) {
                        uint32_t xx = (x + w + (uint32_t)dx) % w, yy = (y + h + (uint32_t)dy) % h;
                        size_t o = (size_t)yy * w + xx;
                        if ((dx || dy) && known[o]) {
                            acc[0] += rgba[4 * o];
                            acc[1] += rgba[4 * o + 1];
                            acc[2] += rgba[4 * o + 2];
                            cnt++;
                        }
                    }
                if (cnt) {
                    rgba[4 * at] = (uint8_t)((acc[0] + cnt / 2) / cnt);
                    rgba[4 * at + 1] = (uint8_t)((acc[1] + cnt / 2) / cnt);
                    rgba[4 * at + 2] = (uint8_t)((acc[2] + cnt / 2) / cnt);
                    next[at] = 1;
                    grew = 1;
                }
            }
        memcpy(known, next, n);
        free(next);
        if (!grew)
            break;
    }
    for (i = 0; i < n; i++) {
        if (!known[i]) {
            rgba[4 * i] = nk ? (uint8_t)(mean[0] / nk + 0.5) : 128;
            rgba[4 * i + 1] = nk ? (uint8_t)(mean[1] / nk + 0.5) : 128;
            rgba[4 * i + 2] = nk ? (uint8_t)(mean[2] / nk + 0.5) : 128;
        }
        rgba[4 * i + 3] = 255;
    }
    free(known);
    return 1;
}

void bo3_upscale_half(const uint8_t *in, uint32_t w, uint32_t h, uint8_t *out)
{
    uint32_t x, y, c, ow = w / 2, oh = h / 2;
    for (y = 0; y < oh; y++)
        for (x = 0; x < ow; x++)
            for (c = 0; c < 4; c++) {
                const uint8_t *a = in + 4 * ((size_t)(2 * y) * w + 2 * x) + c;
                const uint8_t *b = a + 4 * (size_t)w;
                out[4 * ((size_t)y * ow + x) + c] = (uint8_t)((a[0] + a[4] + b[0] + b[4] + 2) / 4);
            }
}

int bo3_png_complete(const char *path, uint32_t *w, uint32_t *h)
{
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    uint8_t head[24], tail[12];
    FILE *f = fopen(path, "rb");
    long size;
    int ok = 0;
    if (!f)
        return 0;
    if (fread(head, 1, sizeof head, f) == sizeof head && !memcmp(head, sig, 8)
            && !memcmp(head + 12, "IHDR", 4) && fseek(f, -12, SEEK_END) == 0
            && (size = ftell(f)) > 24 && fread(tail, 1, sizeof tail, f) == sizeof tail
            && !memcmp(tail + 4, "IEND", 4)) {
        uint32_t ww = (uint32_t)head[16] << 24 | head[17] << 16 | head[18] << 8 | head[19];
        uint32_t hh = (uint32_t)head[20] << 24 | head[21] << 16 | head[22] << 8 | head[23];
        if (w) *w = ww;
        if (h) *h = hh;
        ok = ww > 0 && hh > 0;
    }
    fclose(f);
    return ok;
}
