/*
 * plugins_platform.h - the part of native plugins each system does its own
 * way (plugins_win32.c, plugins_posix.c): hashing a module while it is held,
 * loading it, finding its exports. plugins.c decides everything else.
 */
#ifndef BO3_PLUGINS_PLATFORM_H
#define BO3_PLUGINS_PLATFORM_H

#include <stddef.h>
#include <stdint.h>

/* 1 when this build can load plugins at all. */
int bo3_plugin_sys_supported(void);

/* The module at path (a full path plugins.c has checked), opened and held
 * until bo3_plugin_sys_release: on Windows nobody can write, replace or
 * delete it meanwhile; elsewhere it is copied while it is hashed and the
 * copy is loaded (Linux: a sealed memfd; macOS: a private file, checked
 * again after loading).
 * Its SHA-256 into sha. NULL, with why, when it cannot be read or is not a
 * regular file. */
typedef struct bo3_plugin_file bo3_plugin_file;
bo3_plugin_file *bo3_plugin_sys_hold(const char *path, uint8_t sha[32], char *why, size_t wn);
/* Load the held module: the system runs the library's own start-up code here.
 * NULL, with why, when it does not load, when what was loaded is not the
 * file that was hashed (it stays loaded then), or when the system hands back
 * a library it already gave another plugin (nothing new was loaded). Never
 * unloaded. */
void *bo3_plugin_sys_load(bo3_plugin_file *f, char *why, size_t wn);
void bo3_plugin_sys_release(bo3_plugin_file *f);
/* An export of a loaded module, or NULL. */
void *bo3_plugin_sys_symbol(void *lib, const char *name);

#endif
