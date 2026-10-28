/*
 * plugins.h - native plugins in installed mods (docs/native-plugins.md).
 *
 * A mod that declares [plugin] in its mod.toml brings a shared library. At
 * the start, after bo3_modfs_init, bo3_plugins_init loads the plugins that may
 * run, once, on the main thread, before the game itself starts, in the mods'
 * order (modfs.h). A plugin runs only when all of these hold:
 *
 *   - plugins are not turned off (TAKEDAWN_PLUGINS=0) and this system loads
 *     them (Windows, Linux; macOS only with TAKEDAWN_PLUGINS=1; never Android);
 *   - its mod is usable and on (the mod list, modfs.h);
 *   - its mod.toml names a module for this system, for a plugin API version
 *     this Takedawn has;
 *   - the module is a regular file in the mod's folder, reached without a
 *     link or reparse point;
 *   - the approval file lists the mod's id with the SHA-256 of the module's
 *     bytes as they are now (changed bytes need a new approval).
 *
 * A plugin is never unloaded; a change takes effect at the next start. The
 * list is made once and does not change afterwards: readers need no lock.
 */
#ifndef BO3_PLUGINS_H
#define BO3_PLUGINS_H

#include <stddef.h>

typedef enum {
    BO3_PLUGIN_NONE = 0,        /* the mod declares no native code */
    BO3_PLUGIN_NO_MODULE,       /* [plugin] without a module for this system */
    BO3_PLUGIN_OFF,             /* the mod is off or not usable */
    BO3_PLUGIN_DISABLED,        /* plugins off (TAKEDAWN_PLUGINS) or not loaded on this system */
    BO3_PLUGIN_NOT_APPROVED,    /* not in the approval file for these bytes */
    BO3_PLUGIN_REFUSED,         /* checked and not run: why says */
    BO3_PLUGIN_FAILED,          /* loaded, its init returned an error */
    BO3_PLUGIN_RUNNING          /* loaded and initialised */
} bo3_plugin_state;

/* The approval file, before bo3_plugins_init. NULL or "": none (no plugin
 * can be approved). */
void bo3_plugins_approval_file(const char *path);
/* Load the plugins that may run (above). host_version: Takedawn's version
 * for td_host_api.host_version. The unit test calls it more than once. */
void bo3_plugins_init(const char *host_version);
/* Mod i's plugin (modfs.h numbering): its state, why (when REFUSED / FAILED /
 * NOT_APPROVED, else "") into why, and the module's SHA-256 as 64 hex digits
 * into sha ("" when not hashed). Either buffer may be NULL. */
bo3_plugin_state bo3_plugins_state(int i, char *why, size_t wn, char *sha, size_t sn);
/* Plugins running in this session. */
int bo3_plugins_running(void);
/* Why no plugin can run in this process (TAKEDAWN_PLUGINS, this system), or
 * NULL when they can. */
const char *bo3_plugins_off(void);
/* 1 when mod i's module was approved for its bytes at this start (it was
 * then loaded: RUNNING, or REFUSED / FAILED after loading). */
int bo3_plugins_was_approved(int i);

/* The approval file for the MODS page (modsmenu.c): it says what the next
 * start uses; nothing here changes this start's plugins. No lock: one
 * caller (the page, on the game's thread), after bo3_plugins_init. */
typedef enum {
    BO3_APPROVALS_NONE = 0,     /* no approval file (TAKEDAWN_PLUGIN_APPROVALS is empty) */
    BO3_APPROVALS_ABSENT,       /* not made yet: nothing approved; the page may make it */
    BO3_APPROVALS_OK,           /* read; the page may change it */
    BO3_APPROVALS_KEPT,         /* read and used, but with entries the page would not write back: left as it is */
    BO3_APPROVALS_BAD           /* not readable now, or not an approval file: nothing approved; left as it is */
} bo3_approvals_state;
/* Read the approval file again. */
bo3_approvals_state bo3_plugins_approvals(void);
/* From the last read: 1 when id is approved for exactly sha (64 hex digits). */
int bo3_plugins_approved_for(const char *id, const char *sha);
/* From the last read: how many entries name id; the first one's hash into sha
 * ("" when none, or not as the page writes it). sha may be NULL. */
int bo3_plugins_approvals_of(const char *id, char *sha, size_t sn);
/* id approved from the next start for exactly sha (64 lower-case hex digits),
 * its other entries removed; or, sha NULL, every entry for id removed. The
 * file is read again, written whole (bo3_modfs_write_whole) and read back. 1
 * done (or already so); 0 not done (the file as it was) or not confirmed by
 * the read-back (the file may have been written). Never written when it is
 * NONE, KEPT or BAD, or when id or sha is not well-formed. Only approved is
 * written back: comments and other keys in the file are not kept. */
int bo3_plugins_set_approval(const char *id, const char *sha);
/* Mod i's module (usable, with a module for this system) read and hashed the
 * way a start does, never loaded: 1 with its SHA-256 (64 hex digits) in sha,
 * else 0 with why (the log says it too). */
int bo3_plugins_hash_module(int i, char sha[65], char *why, size_t wn);

#endif
