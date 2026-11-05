/*
 * modtex.c - texture packs of the mod folders; see modtex.h and
 * docs/mod-folders.md (Texture packs). The PNG files, their keys, the worker
 * and the upload are the toolkit's (nv2a_texpack.h); this hands it the
 * folders of the places that are on, in the order a file lookup uses, and
 * reads each pack.toml.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "modfs.h"
#include "modtex.h"
#include "nv2a_texpack.h"
#include "platform/xr_log.h"
#include "recomp_cfg.h"

int bo3_modtex_parse(const char *text, int *alpha, int *compress, char *why, unsigned why_n)
{
    recomp_cfg *cfg;
    const char *v;

    *alpha = NV2A_TEXPACK_ALPHA_AUTO;
    *compress = NV2A_TEXPACK_COMPRESS_AUTO;
    if (why_n)
        why[0] = 0;
    if (!text || !*text)
        return 1;
    cfg = recomp_cfg_load_string(text, "pack.toml", why, why_n);
    if (!cfg)
        return 0;
    if (recomp_cfg_type_of(cfg, "alpha") == RCFG_STRING) {
        v = recomp_cfg_string(cfg, "alpha", "auto");
        if (!strcmp(v, "original"))
            *alpha = NV2A_TEXPACK_ALPHA_ORIGINAL;
        else if (!strcmp(v, "png"))
            *alpha = NV2A_TEXPACK_ALPHA_PNG;
        else if (strcmp(v, "auto") && why_n)
            snprintf(why, why_n, "alpha = \"%.20s\" is not auto, original or png: auto is used", v);
    } else if (recomp_cfg_type_of(cfg, "alpha") != RCFG_NONE && why_n) {
        snprintf(why, why_n, "alpha is not a string: auto is used");
    }
    if (recomp_cfg_type_of(cfg, "compress") == RCFG_STRING) {
        v = recomp_cfg_string(cfg, "compress", "auto");
        if (!strcmp(v, "off"))
            *compress = NV2A_TEXPACK_COMPRESS_OFF;
        else if (strcmp(v, "auto") && why_n)
            snprintf(why, why_n, "compress = \"%.20s\" is not auto or off: auto is used", v);
    } else if (recomp_cfg_type_of(cfg, "compress") != RCFG_NONE && why_n) {
        snprintf(why, why_n, "compress is not a string: auto is used");
    }
    recomp_cfg_free(cfg);
    return !why_n || !why[0];
}

/* pack.toml beside the PNG files: at most 4 KiB, or it is not read. */
static void read_options(const char *dir, int *alpha, int *compress, const char *name)
{
    char path[BO3_MODFS_PATH_MAX + 32], text[BO3_MODTEX_PACK_FILE_MAX + 1], why[200];
    FILE *f;
    size_t n;

    *alpha = NV2A_TEXPACK_ALPHA_AUTO;
    *compress = NV2A_TEXPACK_COMPRESS_AUTO;
    if (snprintf(path, sizeof path, "%s/pack.toml", dir) >= (int)sizeof path)
        return;
    f = fopen(path, "rb");
    if (!f)
        return;
    n = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    if (n >= BO3_MODTEX_PACK_FILE_MAX) {
        XR_NOTICE("[MOD] %s: pack.toml is larger than %d bytes: defaults used\n", name,
                  BO3_MODTEX_PACK_FILE_MAX);
        return;
    }
    text[n] = 0;
    if (!bo3_modtex_parse(text, alpha, compress, why, sizeof why))
        XR_NOTICE("[MOD] %s: pack.toml: %s\n", name, why);
}

/* The packs of this start, as bo3_modtex_init handed them over. */
static char s_dirs[NV2A_TEXPACK_MAX_PACKS][BO3_MODFS_PATH_MAX];
static char s_names[NV2A_TEXPACK_MAX_PACKS][BO3_MODFS_PATH_MAX];
static struct nv2a_texpack_src s_src[NV2A_TEXPACK_MAX_PACKS];
static unsigned s_n;

void bo3_modtex_init(void)
{
    unsigned n = 0;

    while (n < NV2A_TEXPACK_MAX_PACKS && bo3_modfs_texture_dir((int)n, s_dirs[n], sizeof s_dirs[n], s_names[n], sizeof s_names[n])) {
        s_src[n].dir = s_dirs[n];
        s_src[n].name = s_names[n];
        read_options(s_dirs[n], &s_src[n].alpha, &s_src[n].compress, s_names[n]);
        n++;
    }
    s_n = n;
    if (n)
        (void)nv2a_texpack_set(s_src, n);
}

/* Mods turned on or off while the game runs, by id (the packs made on this
 * computer: the menu pictures, the HUD): they take the place of what this
 * start had for them, in the order they were first changed, after the rest. */
#define LIVE_MAX 4
static struct { char id[BO3_MOD_ID_MAX + 1]; char dir[BO3_MODFS_PATH_MAX]; char tex[BO3_MODFS_PATH_MAX + 16]; int on; }
    s_live[LIVE_MAX];
static int s_nlive;
static pthread_mutex_t s_live_mu = PTHREAD_MUTEX_INITIALIZER;   /* the settings' thread, the menu's */

static int live_locked(const char *id, const char *fallback_dir, int on);

int bo3_modtex_live(const char *id, const char *fallback_dir, int on)
{
    int r;
    pthread_mutex_lock(&s_live_mu);
    r = live_locked(id, fallback_dir, on);
    pthread_mutex_unlock(&s_live_mu);
    return r;
}

static int live_locked(const char *id, const char *fallback_dir, int on)
{
    struct nv2a_texpack_src src[NV2A_TEXPACK_MAX_PACKS];
    const char *mod_dir = fallback_dir;
    unsigned n = 0, i;
    int keys, k, l;

    /* The folder as this start found it (its full path, as the packs have it). */
    for (k = 0; k < bo3_modfs_mod_count(); k++) {
        const bo3_mod *m = bo3_modfs_mod(k);
        if (m && m->usable && !strcmp(m->manifest.id, id)) {
            mod_dir = m->folder;
            break;
        }
    }
    if (!mod_dir || !*mod_dir)
        return -1;
    for (l = 0; l < s_nlive && strcmp(s_live[l].id, id); l++)
        ;
    if (l == s_nlive) {
        if (s_nlive == LIVE_MAX)
            return -1;
        s_nlive++;
    }
    snprintf(s_live[l].id, sizeof s_live[l].id, "%s", id);
    snprintf(s_live[l].dir, sizeof s_live[l].dir, "%s", mod_dir);
    snprintf(s_live[l].tex, sizeof s_live[l].tex, "%s/textures", mod_dir);
    s_live[l].on = on;

    /* This start's packs but those of the mods changed live. */
    for (i = 0; i < s_n; i++) {
        int theirs = 0;
        for (l = 0; l < s_nlive && !theirs; l++) {
            size_t ml = strlen(s_live[l].dir);
            theirs = !strncmp(s_src[i].dir, s_live[l].dir, ml)
                     && (s_src[i].dir[ml] == '/' || s_src[i].dir[ml] == '\\');
        }
        if (!theirs)
            src[n++] = s_src[i];
    }
    for (l = 0; l < s_nlive && n < NV2A_TEXPACK_MAX_PACKS; l++)
        if (s_live[l].on) {
            src[n].dir = s_live[l].tex;
            src[n].name = s_live[l].id;
            read_options(s_live[l].tex, &src[n].alpha, &src[n].compress, s_live[l].id);
            n++;
        }
    keys = nv2a_texpack_set(src, n);
    XR_NOTICE("[MOD] %s %s now: %d texture keys in use\n", id, on ? "on" : "off", keys);
    return keys;
}
