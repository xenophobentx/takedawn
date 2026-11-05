/*
 * upscale_tile.h - the plain C half of the in-process upscaler (BO-326,
 * docs/upscale-art.md, Android): how a picture is cut into tiles for the
 * model, what each tile's input is, how its output goes back into the
 * picture, and which backend runs it. No ncnn, no files, no threads: unit
 * tested in tests/upscale. upscale_ncnn.cpp runs the model on what this
 * makes.
 *
 * The tiles are the ones realesrgan-ncnn-vulkan cuts (its realesrgan.cpp,
 * MIT), so that a picture made in the app matches the desktop's: tiles of
 * `tile` source pixels from the top left, the last ones in a row or column
 * shorter, each given to the model with `pad` pixels more on every side
 * (prepadding 10 for the realesr models) taken from the picture mirrored at
 * its edges (its preproc shader: abs(), then (w-1) - abs(x - (w-1)), the
 * edge texel not repeated), values / 255. From the model's output (scale
 * times the padded tile) the middle scale * tile is kept, as v * 255 + 0.5
 * rounded down and clamped (its postproc shader).
 */
#ifndef BO3_UPSCALE_TILE_H
#define BO3_UPSCALE_TILE_H

#include <stddef.h>
#include <stdint.h>

typedef struct bo3_tile {
    int x0, y0;                 /* the tile's own part, in source pixels */
    int w, h;
} bo3_tile;

/* How many tiles a w x h picture has at this tile size (> 0). */
int bo3_tile_count(int w, int h, int tile);

/* Tile i (row by row from the top left) into *t. 0 when i is out of range. */
int bo3_tile_get(int w, int h, int tile, int i, bo3_tile *t);

/* x mirrored into 0..n-1 without repeating the edge texel (reflect-101); a
 * picture narrower than the mirror needs is folded again until it fits. */
int bo3_tile_reflect(int x, int n);

/* The model's input for t: three planes of (t->w + 2 pad) x (t->h + 2 pad)
 * floats (R, G, B; each plane `stride` floats apart, a row right after the
 * other) from the RGBA8 picture rgba (w x h; alpha is not read). */
void bo3_tile_input(const uint8_t *rgba, int w, int h, const bo3_tile *t, int pad, float *planes,
                    size_t stride);

/* The model's output for t (three planes of scale * (t->w + 2 pad) by
 * scale * (t->h + 2 pad) floats, `stride` apart) into the RGBA8 picture out
 * (scale * w wide): its middle, alpha 255. */
void bo3_tile_output(const float *planes, size_t stride, const bo3_tile *t, int pad, int scale,
                     uint8_t *out, int w);

/* 1 when a model's output of n floats per plane is black where its input was
 * not: what a lost graphics device leaves (BO-311 saw black pictures with a
 * status of 0 on an AMD APU). in_max: the largest input value. */
int bo3_tile_blank(const float *planes, size_t stride, size_t n, float in_max);

/* ── which backend ───────────────────────────────────────────────────── */

enum { BO3_UPS_CPU = 0, BO3_UPS_VULKAN = 1 };

typedef struct bo3_backend_facts {
    const char *override;       /* TAKEDAWN_UPSCALE_BACKEND: "cpu", "vulkan", "auto", NULL */
    int         have_vulkan;    /* this build has ncnn's Vulkan path */
    const char *opened;         /* the driver the game's loader opened (xr_android_vk_driver) */
    int         open_failed;    /* the chosen driver could not be opened */
    const char *driver_dir;     /* RECOMP_ANDROID_DRIVER_DIR, the one asked for */
    const char *driver_name;    /* RECOMP_ANDROID_DRIVER_NAME */
    const char *lib_dir;        /* RECOMP_ANDROID_LIB_DIR: the app's native libraries */
    const char *bundled;        /* the file name of the app's own Turnip */
    int         failed_before;  /* Vulkan failed or did not come back on this device before */
} bo3_backend_facts;

/* BO3_UPS_VULKAN only when the game runs on the app's own Turnip (the
 * stock Adreno drivers are not trusted with compute, BO-140) and Vulkan has
 * not failed here before; else BO3_UPS_CPU. TAKEDAWN_UPSCALE_BACKEND=cpu
 * always wins; =vulkan asks for it on any driver (tests). why: a few words
 * for the log. */
int bo3_upscale_backend_pick(const bo3_backend_facts *f, char *why, size_t wn);

#endif
