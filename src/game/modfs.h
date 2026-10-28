/*
 * modfs.h - the mods bo3_modfs_init found (docs/mod-folders.md, Manifests),
 * and which of them are on.
 *
 * The list is made once, by bo3_modfs_init, before the title runs, and does
 * not change afterwards: readers need no lock and the pointers stay good until
 * the next bo3_modfs_init (only the unit test calls it twice). Order: the
 * folders TAKEDAWN_MODS names, in order; in each, its usable mods by id, then
 * the ones that are not usable by folder name.
 *
 * On and off: a mod is on unless the mod list (bo3_modfs_state_file, a
 * mod-list.toml beside settings.toml) names its id as off. A change is saved
 * at once and used from the next start: this session's list stays as it is.
 */
#ifndef BO3_MODFS_H
#define BO3_MODFS_H

#include "modmanifest.h"

#define BO3_MODFS_MAX_MODS 64
#define BO3_MODFS_PATH_MAX 520
#define BO3_MODFS_MAX_OFF  256      /* ids the mod list keeps as off */

typedef struct bo3_mod {
    bo3_manifest manifest;          /* all empty when the manifest was not read */
    char folder[BO3_MODFS_PATH_MAX];/* the mod folder's full host path */
    int  root;                      /* 0-based place of its folder in TAKEDAWN_MODS */
    int  usable;                    /* 1: valid, and no other mod has its id */
    int  enabled;                   /* 1: on in this session; its files are used when usable too */
    int  wins, loses;               /* files it supplies that another source has too: used / not used */
    char problem[160];              /* why not usable; "" when usable */
} bo3_mod;

void bo3_modfs_init(void);
int bo3_modfs_folder_count(void);      /* mod folders taken (0: none, the mod loader is off) */
int bo3_modfs_mod_count(void);
const bo3_mod *bo3_modfs_mod(int i);   /* NULL outside 0 .. count - 1 */

/* The mod list's file, before bo3_modfs_init. NULL or "": none (every mod on,
 * nothing saved). */
void bo3_modfs_state_file(const char *path);
/* On at the next start: what the mod list says now (1 / 0; 1 when it cannot
 * be read). Reads the list first if the start did not (no mods). */
int bo3_modfs_enabled_next(const char *id);
/* Turn a mod on or off from the next start; saved at once. 1 when saved (or
 * already so), 0 when id is not a valid id, there is no mod list file, it
 * could not be read now, the list already holds BO3_MODFS_MAX_OFF ids, or it
 * could not be written; with 0 the file and the list are as they were. Saving
 * drops entries the list skipped (not ids, more than BO3_MODFS_MAX_OFF). Any
 * thread; a lookup never waits for it. */
int bo3_modfs_set_enabled(const char *id, int on);
/* Files more than one place supplies (this session; see the log for which). */
int bo3_modfs_conflicts(void);
/* The host path of rel inside mod m's own folder (beside its mod.toml, not
 * in files), found the way a lookup finds a file: every part a plain name,
 * none a link or reparse point, a regular file at the end. 1 with out set,
 * else 0. For a plugin's module (plugins.c). */
int bo3_modfs_mod_file(const bo3_mod *m, const char *rel, char *out, size_t n);
/* Another settings file (plugins.c's approval file), read and written the
 * mod list's way. read: 1 read (text NUL-terminated), 0 no such file, -1 not
 * readable now, -2 not a plain file, larger than 64 KiB, or a path too long;
 * free(*text) whatever it returns. write: the whole file to path.tmp, flushed, then renamed
 * over path; 1 done, 0 not (path as it was). No lock: one caller at a time. */
int bo3_modfs_read_whole(const char *path, char **text, size_t *n);
int bo3_modfs_write_whole(const char *path, const char *text, size_t n);

#endif
