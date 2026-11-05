/*
 * upscale_tile.c - tiles and backend choice of the in-process upscaler; see
 * upscale_tile.h.
 */
#include "upscale_tile.h"

#include <stdio.h>
#include <string.h>

int bo3_tile_count(int w, int h, int tile)
{
    if (w <= 0 || h <= 0 || tile <= 0)
        return 0;
    return ((w + tile - 1) / tile) * ((h + tile - 1) / tile);
}

int bo3_tile_get(int w, int h, int tile, int i, bo3_tile *t)
{
    int xt, xi, yi;
    if (i < 0 || i >= bo3_tile_count(w, h, tile))
        return 0;
    xt = (w + tile - 1) / tile;
    xi = i % xt;
    yi = i / xt;
    t->x0 = xi * tile;
    t->y0 = yi * tile;
    t->w = (w < (xi + 1) * tile ? w : (xi + 1) * tile) - t->x0;
    t->h = (h < (yi + 1) * tile ? h : (yi + 1) * tile) - t->y0;
    return 1;
}

int bo3_tile_reflect(int x, int n)
{
    if (n <= 1)
        return 0;
    /* The shader's two steps (abs, then fold at n - 1), repeated for a
     * picture smaller than the padding (the shader reads past it there). */
    while (x < 0 || x > n - 1) {
        if (x < 0)
            x = -x;
        if (x > n - 1)
            x = (n - 1) - (x - (n - 1));
    }
    return x;
}

void bo3_tile_input(const uint8_t *rgba, int w, int h, const bo3_tile *t, int pad, float *planes,
                    size_t stride)
{
    const int iw = t->w + 2 * pad, ih = t->h + 2 * pad;
    const float norm = 1.0f / 255.0f;
    int x, y, c;
    for (y = 0; y < ih; y++) {
        int sy = bo3_tile_reflect(t->y0 - pad + y, h);
        const uint8_t *row = rgba + (size_t)sy * w * 4;
        for (x = 0; x < iw; x++) {
            int sx = bo3_tile_reflect(t->x0 - pad + x, w);
            for (c = 0; c < 3; c++)
                planes[c * stride + (size_t)y * iw + x] = (float)row[(size_t)sx * 4 + c] * norm;
        }
    }
}

static uint8_t to_u8(float v)
{
    v = v * 255.0f + 0.5f;
    if (!(v > 0.0f))                    /* also NaN */
        return 0;
    if (v >= 255.0f)
        return 255;
    return (uint8_t)v;                  /* floor of a positive value */
}

void bo3_tile_output(const float *planes, size_t stride, const bo3_tile *t, int pad, int scale,
                     uint8_t *out, int w)
{
    const int ow = (t->w + 2 * pad) * scale;
    const int cw = t->w * scale, ch = t->h * scale, crop = pad * scale;
    const size_t W = (size_t)w * scale;
    int x, y, c;
    for (y = 0; y < ch; y++) {
        uint8_t *dst = out + ((size_t)(t->y0 * scale + y) * W + (size_t)t->x0 * scale) * 4;
        const size_t src = (size_t)(y + crop) * ow + crop;
        for (x = 0; x < cw; x++) {
            for (c = 0; c < 3; c++)
                dst[x * 4 + c] = to_u8(planes[c * stride + src + x]);
            dst[x * 4 + 3] = 255;
        }
    }
}

int bo3_tile_blank(const float *planes, size_t stride, size_t n, float in_max)
{
    size_t i;
    int c;
    if (in_max < 0.05f)
        return 0;                       /* a dark picture may well come out dark */
    for (c = 0; c < 3; c++)
        for (i = 0; i < n; i++) {
            float v = planes[c * stride + i];
            if (v > 1.0f / 255.0f || v != v)
                return v != v;          /* NaN: as broken as black */
        }
    return 1;
}

/* ── which backend ───────────────────────────────────────────────────── */

static int same_dir(const char *a, const char *b)
{
    size_t na, nb;
    if (!a || !b || !*a || !*b)
        return 0;
    na = strlen(a);
    nb = strlen(b);
    while (na > 1 && a[na - 1] == '/')
        na--;
    while (nb > 1 && b[nb - 1] == '/')
        nb--;
    return na == nb && !strncmp(a, b, na);
}

static int pick(int b, char *why, size_t wn, const char *text)
{
    if (why && wn)
        snprintf(why, wn, "%s", text);
    return b;
}

int bo3_upscale_backend_pick(const bo3_backend_facts *f, char *why, size_t wn)
{
    const char *o = f->override;
    int bundled_on;
    if (o && !strcmp(o, "cpu"))
        return pick(BO3_UPS_CPU, why, wn, "TAKEDAWN_UPSCALE_BACKEND=cpu");
    if (!f->have_vulkan)
        return pick(BO3_UPS_CPU, why, wn, "this build has no Vulkan upscaler");
    if (o && !strcmp(o, "vulkan"))
        return pick(BO3_UPS_VULKAN, why, wn, "TAKEDAWN_UPSCALE_BACKEND=vulkan");
    if (f->failed_before)
        return pick(BO3_UPS_CPU, why, wn, "Vulkan failed on this device before");
    if (f->open_failed || !f->opened || !f->bundled || strcmp(f->opened, f->bundled))
        return pick(BO3_UPS_CPU, why, wn, "the game runs on the device's own graphics driver");
    /* The app's Turnip is what the loader has: asked for from the app's own
     * folder, or opened as the fallback for a package of the player's that
     * could not be (then the name asked for is another). A package of the
     * player's that happens to have the same file name is not the app's. */
    bundled_on = (f->driver_name && !strcmp(f->driver_name, f->bundled) && same_dir(f->driver_dir, f->lib_dir))
                 || (f->driver_name && strcmp(f->driver_name, f->bundled));
    if (!bundled_on)
        return pick(BO3_UPS_CPU, why, wn, "a graphics driver of the player's choosing");
    return pick(BO3_UPS_VULKAN, why, wn, "the app's own Turnip driver");
}
