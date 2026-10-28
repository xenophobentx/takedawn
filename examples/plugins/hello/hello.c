/*
 * hello.c - the smallest Takedawn native plugin (docs/native-plugins.md).
 * It says hello in the game's log and does nothing else: no game state, no
 * memory, no hooks.
 *
 * Windows (clang or MinGW):  clang -shared -Iinclude hello.c -o hello.asi
 * Windows (MSVC):            cl /LD /Iinclude hello.c /Fe:hello.asi
 * Linux:                     cc -shared -fPIC -fvisibility=hidden -Iinclude hello.c -o libhello.so
 * macOS:                     cc -dynamiclib -fvisibility=hidden -Iinclude hello.c -o libhello.dylib
 */
#include "takedawn_plugin.h"

#include <stddef.h>

static const td_host_api *s_api;

static void TD_CALL bye(void *user)
{
    (void)user;
    s_api->log(s_api->host, TD_LOG_INFO, "goodbye");
}

TD_EXPORT uint32_t TD_CALL takedawn_plugin_api_version(void)
{
    return TD_PLUGIN_API_VERSION;
}

TD_EXPORT int32_t TD_CALL takedawn_plugin_init(const td_host_api *api, td_plugin_info *info)
{
    if (!api || api->struct_size < offsetof(td_host_api, host_version) + sizeof api->host_version)
        return TD_ERR_INIT;          /* an older Takedawn than this file knows: say nothing, start nothing */
    s_api = api;
    api->log(api->host, TD_LOG_INFO, "hello from API 1");
    info->struct_size = (uint32_t)sizeof *info;
    info->on_shutdown = bye;
    info->user = NULL;
    return TD_OK;
}
