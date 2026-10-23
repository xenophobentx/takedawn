/*
 * modfs.h - the mods bo3_modfs_init found (docs/mod-folders.md, Manifests).
 *
 * The list is made once, by bo3_modfs_init, before the title runs, and does
 * not change afterwards: readers need no lock and the pointers stay good until
 * the next bo3_modfs_init (only the unit test calls it twice). Order: the
 * folders TAKEDAWN_MODS names, in order; in each, its usable mods by id, then
 * the ones that are not usable by folder name.
 */
#ifndef BO3_MODFS_H
#define BO3_MODFS_H

#include "modmanifest.h"

#define BO3_MODFS_MAX_MODS 64
#define BO3_MODFS_PATH_MAX 520

typedef struct bo3_mod {
    bo3_manifest manifest;          /* all empty when the manifest was not read */
    char folder[BO3_MODFS_PATH_MAX];/* the mod folder's full host path */
    int  root;                      /* 0-based place of its folder in TAKEDAWN_MODS */
    int  usable;                    /* 1: its files/ replace disc files */
    char problem[160];              /* why not usable; "" when usable */
} bo3_mod;

void bo3_modfs_init(void);
int bo3_modfs_mod_count(void);
const bo3_mod *bo3_modfs_mod(int i);   /* NULL outside 0 .. count - 1 */

#endif
