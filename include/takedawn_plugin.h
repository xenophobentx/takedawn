/*
 * takedawn_plugin.h - the Takedawn native plugin API (docs/native-plugins.md).
 * The one file a plugin needs. Plain C, MIT, like the rest of Takedawn.
 *
 * A plugin is a shared library (Windows .asi or .dll, Linux .so, macOS .dylib)
 * in an installed mod that declares it in its mod.toml ([plugin]). It runs in
 * the game's process with the user's permissions: nothing is sandboxed, and a
 * fault in a plugin is a fault in the game. Takedawn loads only a plugin whose
 * mod is on and that the player approved for these exact bytes.
 *
 * Loading a library runs its own start-up code (DllMain, constructors) before
 * Takedawn can look at its exports: keep that code empty, and do the work in
 * takedawn_plugin_init.
 *
 * Version rules: TD_PLUGIN_API_VERSION grows when the API grows. A struct
 * only ever gains fields at its end; struct_size says which ones the other
 * side has. Nothing is ever removed from a version.
 */
#ifndef TAKEDAWN_PLUGIN_H
#define TAKEDAWN_PLUGIN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TD_PLUGIN_API_VERSION 1u

#if defined(_WIN32) && !defined(_WIN64)
#define TD_CALL __cdecl
#else
#define TD_CALL
#endif
#if defined(_WIN32)
#define TD_EXPORT __declspec(dllexport)
#else
#define TD_EXPORT __attribute__((visibility("default")))
#endif

enum {
    TD_LOG_INFO  = 0,
    TD_LOG_WARN  = 1,
    TD_LOG_ERROR = 2
};

enum {
    TD_OK       = 0,     /* takedawn_plugin_init: the plugin is running */
    TD_ERR_INIT = 1      /* takedawn_plugin_init: it could not start (say why with log first) */
};

/* One per loaded plugin; pass it back to every call. Opaque. */
typedef struct td_host td_host;

/* What Takedawn offers a plugin. Takedawn owns it; it stays valid until the
 * process ends. Calls may be made from any thread after takedawn_plugin_init
 * has been entered. */
typedef struct td_host_api {
    uint32_t struct_size;           /* sizeof (td_host_api) as Takedawn built it */
    uint32_t api_version;           /* the version in use: the plugin's own */
    td_host *host;
    /* One line to the game's log, as "[PLUGIN <mod id>] <text>". UTF-8; the
     * text is copied; control characters are shown as '?'; at most 1023
     * bytes are kept. level: TD_LOG_*. */
    void (TD_CALL *log)(td_host *host, int32_t level, const char *text);
    /* The id of the mod that declared this plugin (its mod.toml). Takedawn's
     * string, valid until the process ends. */
    const char *(TD_CALL *mod_id)(td_host *host);
    /* Takedawn's version, as --version gives it. Takedawn's string, valid
     * until the process ends. */
    const char *(TD_CALL *host_version)(td_host *host);
} td_host_api;

/* What a plugin tells Takedawn. On entry to takedawn_plugin_init, struct_size
 * is the size Takedawn made room for and every field is zero: write no field
 * that ends past that size (a newer header may have more fields than this
 * Takedawn). Set struct_size to the size the plugin filled in, or leave it;
 * Takedawn reads only fields that both sizes cover, when init returns. */
typedef struct td_plugin_info {
    uint32_t struct_size;           /* in: Takedawn's size; out: what the plugin filled in */
    /* Called once at a normal exit of the process, in reverse order of
     * loading, on the thread that ends it. Best effort: not on a crash, a
     * forced end, or the system's own quick exit paths. May be NULL. */
    void (TD_CALL *on_shutdown)(void *user);
    void *user;
} td_plugin_info;

/* Exported by every plugin (TD_EXPORT, these exact names). */

/* The API version the plugin was built for: return TD_PLUGIN_API_VERSION. It
 * must equal plugin.api in the mod.toml. Called before takedawn_plugin_init;
 * a version this Takedawn does not have means init is never called. */
typedef uint32_t (TD_CALL *td_plugin_api_version_fn)(void);

/* Start the plugin. Called once, on the game's main thread, before the game
 * itself starts. api is valid until the process ends; info is the plugin's to
 * fill in. Return TD_OK, or TD_ERR_INIT to be listed as not started (the
 * library stays loaded: Takedawn never unloads a plugin). */
typedef int32_t (TD_CALL *td_plugin_init_fn)(const td_host_api *api, td_plugin_info *info);

#define TD_PLUGIN_API_VERSION_SYMBOL "takedawn_plugin_api_version"
#define TD_PLUGIN_INIT_SYMBOL        "takedawn_plugin_init"

#ifdef __cplusplus
}
#endif

#endif /* TAKEDAWN_PLUGIN_H */
