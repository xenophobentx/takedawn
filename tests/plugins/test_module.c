/*
 * test_module.c - the modules tests/plugins loads, one source built once per
 * TP_KIND_* (tests/plugins/CMakeLists.txt). Every one, the moment the system
 * loads it (DllMain / a constructor, before Takedawn can look at its
 * exports), appends "kind=<its kind> path=<what was loaded>" to
 * <TP_MARKER_DIR>/loaded: a kind that must not run must not be in that file.
 * (The path is the module's on Windows, the sealed copy's elsewhere.) What
 * each does in init:
 *
 *   GOOD, MARKER   both exports; init writes <dir>/init.<mod id> with what the
 *                  API gave it (including info's size on entry), logs a line,
 *                  returns TD_OK
 *   NOINIT         no takedawn_plugin_init
 *   NOVERSION      no takedawn_plugin_api_version
 *   NEWVERSION     says it was built for API 2
 *   INITFAIL       init writes its marker and returns TD_ERR_INIT
 *   ORDER_A / _B   init appends A / B to <dir>/order
 *   BIGINFO        says it filled a far larger td_plugin_info (as a plugin
 *                  built with a newer header may), writing only within room
 */
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE                 /* glibc declares dladdr and Dl_info only with it */
#endif
#include "takedawn_plugin.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#if defined(TP_KIND_GOOD)
#define KIND "good"
#elif defined(TP_KIND_MARKER)
#define KIND "marker"
#elif defined(TP_KIND_NOINIT)
#define KIND "noinit"
#elif defined(TP_KIND_NOVERSION)
#define KIND "noversion"
#elif defined(TP_KIND_NEWVERSION)
#define KIND "newversion"
#elif defined(TP_KIND_INITFAIL)
#define KIND "initfail"
#elif defined(TP_KIND_ORDER_A)
#define KIND "order_a"
#elif defined(TP_KIND_ORDER_B)
#define KIND "order_b"
#elif defined(TP_KIND_BIGINFO)
#define KIND "biginfo"
#else
#error "build with one TP_KIND_*"
#endif

static void append(const char *name, const char *text)
{
    const char *dir = getenv("TP_MARKER_DIR");
    char path[1024];
    FILE *f;
    if (!dir)
        return;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    f = fopen(path, "ab");
    if (f) {
        fputs(text, f);
        fclose(f);
    }
}

#if defined(_WIN32)
BOOL WINAPI DllMain(HINSTANCE inst, DWORD why, LPVOID reserved)
{
    (void)reserved;
    if (why == DLL_PROCESS_ATTACH) {
        char me[MAX_PATH + 1], line[MAX_PATH + 64];
        DWORD n = GetModuleFileNameA(inst, me, MAX_PATH);
        me[n < MAX_PATH ? n : MAX_PATH] = 0;
        snprintf(line, sizeof line, "kind=" KIND " path=%s\n", me);
        append("loaded", line);
    }
    return TRUE;
}
#else
static void marker(void) {}
__attribute__((constructor)) static void on_load(void)
{
    Dl_info d;
    char line[1200];
    snprintf(line, sizeof line, "kind=" KIND " path=%s\n",
             dladdr((void *)marker, &d) && d.dli_fname ? d.dli_fname : "?");
    append("loaded", line);
}
#endif

#if !defined(TP_KIND_NOVERSION)
TD_EXPORT uint32_t TD_CALL takedawn_plugin_api_version(void)
{
#if defined(TP_KIND_NEWVERSION)
    return 2;
#else
    return TD_PLUGIN_API_VERSION;
#endif
}
#endif

#if !defined(TP_KIND_NOINIT)
/* This loaded copy's own: how many times its init ran (1 unless the system
 * handed one library to two plugins), and the API it was given. */
static int s_inits;

#if defined(TP_KIND_GOOD)
static const td_host_api *s_api;

static void TD_CALL bye(void *user)
{
    (void)user;
    s_api->log(s_api->host, TD_LOG_INFO, "shutdown");
}
#endif

TD_EXPORT int32_t TD_CALL takedawn_plugin_init(const td_host_api *api, td_plugin_info *info)
{
    char name[128], text[512];
    const char *id = api->mod_id(api->host);
    uint32_t room = info->struct_size;
    s_inits++;
#if defined(TP_KIND_GOOD)
    s_api = api;
    info->on_shutdown = bye;            /* within the room it was given (struct_size above) */
#endif
#if defined(TP_KIND_BIGINFO)
    if (room >= sizeof *info)
        info->struct_size = 4096;           /* "I filled more": the host must read only its own fields */
#endif
#if defined(TP_KIND_ORDER_A)
    append("order", "A");
#elif defined(TP_KIND_ORDER_B)
    append("order", "B");
#endif
    snprintf(name, sizeof name, "init.%s", id);
    snprintf(text, sizeof text, "id=%s api=%u size=%u version=%s info=%u kind=" KIND " inits=%d copy=%p\n", id,
             (unsigned)api->api_version, (unsigned)api->struct_size, api->host_version(api->host), (unsigned)room, s_inits,
             (void *)&s_inits);
    append(name, text);
    api->log(api->host, TD_LOG_INFO, "test module started");
    api->log(api->host, TD_LOG_WARN, "a line with\na break and a tab\tin it");
#if defined(TP_KIND_INITFAIL)
    return TD_ERR_INIT;
#else
    return TD_OK;
#endif
}
#endif
