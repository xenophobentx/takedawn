/*
 * upscale_art.h - the parts of the art upscale (upscale.h, BO-311) that need
 * no network, no process and no GPU: reading the disc's texture dictionaries,
 * decoding a texture to RGBA, its texture-pack name, which pictures are
 * upscaled with which model, the colour fill under transparent texels, a 2x2
 * reduction. Unit tested in tests/upscale.
 */
#ifndef BO3_UPSCALE_ART_H
#define BO3_UPSCALE_ART_H

#include <stddef.h>
#include <stdint.h>

/* ── the disc's texture dictionaries ─────────────────────────────────── */

/* NV2A colour formats the art uses (the format byte of the texture's
 * format register). */
#define BO3_TXD_P8    0x0Bu
#define BO3_TXD_DXT1  0x0Cu
#define BO3_TXD_DXT3  0x0Eu
#define BO3_TXD_DXT5  0x0Fu

typedef struct bo3_txd_tex {
    char           name[33];
    uint32_t       fmt;         /* NV2A colour format */
    uint32_t       w, h;        /* level 0 */
    uint32_t       mips;
    const uint8_t *px;          /* level 0 as the console stores it */
    size_t         px_len;      /* bytes of level 0 */
    const uint8_t *pal;         /* P8: 256 BGRA entries; else NULL */
} bo3_txd_tex;

/* The textures of a Criterion texture dictionary (Frontend.txd, or one of
 * LoadScrn.bin's): up to max entries into out, of the formats above only (the
 * others are counted in *skipped when it is not NULL). The pointers point into
 * d. Returns how many, -1 when d is not a dictionary. */
int bo3_txd_list(const uint8_t *d, size_t n, bo3_txd_tex *out, int max, int *skipped);

/* LoadScrn.bin: an index of the loading screens' dictionaries. Up to max of
 * them into dicts/lens (pointing into a). Returns how many, -1 when a is not
 * that file. */
int bo3_loadscrn_dicts(const uint8_t *a, size_t n, const uint8_t **dicts, size_t *lens, int max);

/* The texture's level 0 as w x h RGBA8 (malloc'd), or NULL. */
uint8_t *bo3_txd_decode(const bo3_txd_tex *t);

/* The texture-pack file name the game looks the texture up by
 * ("<hash>.png", "<hash>-<palette>.png" for P8; docs/mod-folders.md). 1 when
 * the format has one. */
int bo3_txd_pack_name(const bo3_txd_tex *t, char *out, size_t n);

/* ── which pictures, which model ─────────────────────────────────────── */

enum {
    BO3_UPS_SKIP = 0,           /* not upscaled: noise, solid, tiny icons */
    BO3_UPS_LOADSCREEN,
    BO3_UPS_SATMAP,             /* satellite maps: upscaled 4x, kept at 2x (memory) */
    BO3_UPS_PHOTO,              /* event photos and the world map */
    BO3_UPS_CARPICK,
    BO3_UPS_POLAROID,
    BO3_UPS_NEWSPAPER,          /* small body text: the fast model, whatever the quality */
    BO3_UPS_POSTCARD,
    BO3_UPS_LETTERING,          /* the title logo and the few lettered icons */
    BO3_UPS_ICON,
    BO3_UPS_GROUPS
};
/* src: "LoadScrn" or "Frontend". */
int         bo3_upscale_group(const char *src, const bo3_txd_tex *t);
const char *bo3_upscale_group_name(int group);

enum { BO3_UPS_BEST = 0, BO3_UPS_FAST = 1 };
#define BO3_UPS_MODEL_X4PLUS  0     /* RealESRGAN_x4plus */
#define BO3_UPS_MODEL_GENERAL 1     /* realesr-general-x4v3, denoise 0.3 */
/* The model a group is upscaled with at a quality. */
int bo3_upscale_model(int group, int quality);
/* The scale the pack keeps: 4 for every group (it was 2 for the satellite
 * maps). */
int bo3_upscale_keep_scale(int group);

/* ── pixels ──────────────────────────────────────────────────────────── */

/* The colour of texels whose alpha is at most 8 replaced by the mean of known
 * neighbours, grown outwards (so a model sees no hard edge there); alpha is set
 * to 255 everywhere. Returns 1 when the texture had any alpha below 255. */
int bo3_upscale_flatten(uint8_t *rgba, uint32_t w, uint32_t h);

/* A 2x2 average (per channel, rounded) of a 4-channel image: out is
 * (w/2) x (h/2). w and h even. */
void bo3_upscale_half(const uint8_t *in, uint32_t w, uint32_t h, uint8_t *out);

/* A PNG file that is complete: the signature, IHDR and an IEND at the end.
 * Its size into *w, *h. 1 when so. (An engine stopped in the middle of a
 * write leaves a cut file.) */
int bo3_png_complete(const char *path, uint32_t *w, uint32_t *h);

#endif
