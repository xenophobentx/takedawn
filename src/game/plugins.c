/*
 * plugins.c - native plugins in installed mods (plugins.h,
 * docs/native-plugins.md): which may run, the approval file, the API each
 * gets, the order, the log. Loading itself is plugins_win32.c /
 * plugins_posix.c.
 *
 * The approval file (plugin-approvals.toml beside settings.toml, or
 * TAKEDAWN_PLUGIN_APPROVALS) is the player's, written by hand or by a front
 * end, read once at the start:
 *
 *     approved = ["example.hello-plugin sha256:<64 hex digits>"]
 *
 * An entry approves one mod's module with exactly those bytes. A file that is
 * not such a list approves nothing (the log says why): native code runs only
 * on an approval that could be read.
 */
#include "plugins.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform/xr_log.h"     /* XR_NOTICE */
#include "recomp_cfg.h"
#include "xr_sha256.h"
#include "modfs.h"
#include "plugins_platform.h"
#include "takedawn_plugin.h"

#define MAX_APPROVALS     256
#define MAX_APPROVAL_FILE (64 * 1024)
#define LOG_TEXT_MAX      1023

struct td_host {
    int index;                              /* the mod (modfs.h numbering) */
    char id[BO3_MOD_ID_MAX + 1];
};

typedef struct {
    bo3_plugin_state state;
    char why[400];                          /* a module path (up to BO3_PLUGIN_PATH_MAX) and the reason around it */
    char sha[65];
    int approved;                           /* approved for these bytes at this start (loaded, or refused after) */
} plugin_slot;

static plugin_slot s_slots[BO3_MODFS_MAX_MODS];
static int s_nslots, s_running;
static char s_approval_file[1100];
static char s_host_version[64];

/* What a plugin is given, and what it gave back: kept for the process (a
 * plugin keeps the pointer), one per mod slot. */
static td_host s_hosts[BO3_MODFS_MAX_MODS];
static td_host_api s_api[BO3_MODFS_MAX_MODS];
static td_plugin_info s_info[BO3_MODFS_MAX_MODS];
static int s_started[BO3_MODFS_MAX_MODS], s_order[BO3_MODFS_MAX_MODS], s_norder;
static int s_atexit;

static char (*s_approved)[BO3_MOD_ID_MAX + 1 + 8 + 64 + 1];
static int s_napproved, s_approvals_ok;

/* ---- the API a plugin gets ---- */

static void TD_CALL api_log(td_host *host, int32_t level, const char *text)
{
    char line[LOG_TEXT_MAX + 1];
    size_t n = 0;

    if (!host)
        return;
    for (; text && text[n] && n < LOG_TEXT_MAX; n++) {
        unsigned char c = (unsigned char)text[n];
        line[n] = (char)(c < 0x20 || c == 0x7F ? '?' : c);   /* one line, nothing that moves the cursor */
    }
    line[n] = 0;
    XR_NOTICE("[PLUGIN %s] %s%s\n", host->id, level == TD_LOG_ERROR ? "error: " : level == TD_LOG_WARN ? "warning: " : "",
              line);
}

static const char *TD_CALL api_mod_id(td_host *host)
{
    return host ? host->id : "";
}

static const char *TD_CALL api_host_version(td_host *host)
{
    (void)host;
    return s_host_version;
}

/* ---- the approval file ---- */

/* "<id> sha256:<64 hex digits>", as the MODS page writes it (lower case). */
static int entry_ok(const char *e)
{
    char id[BO3_MOD_ID_MAX + 1];
    const char *sp = strchr(e, ' ');
    size_t k;
    if (!sp || (size_t)(sp - e) > BO3_MOD_ID_MAX || strncmp(sp, " sha256:", 8) || strlen(sp + 8) != 64)
        return 0;
    memcpy(id, e, (size_t)(sp - e));
    id[sp - e] = 0;
    for (k = 0; k < 64; k++)
        if (!((sp[8 + k] >= '0' && sp[8 + k] <= '9') || (sp[8 + k] >= 'a' && sp[8 + k] <= 'f')))
            return 0;
    return bo3_manifest_id_ok(id);
}

/* The approval file into s_approved: BO3_APPROVALS_*. quiet: nothing in the
 * log (the MODS page reads it again at each change). */
static bo3_approvals_state approvals_load(int quiet)
{
    char why[200] = "";
    recomp_cfg *cfg = NULL;
    char *text = NULL;
    size_t len = 0;
    int r, kept = 1;

    s_napproved = 0;
    s_approvals_ok = 0;
    if (!s_approval_file[0])
        return BO3_APPROVALS_NONE;
    r = bo3_modfs_read_whole(s_approval_file, &text, &len);   /* read once: what is checked is what is parsed */
    if (r == 0) {
        free(text);
        return BO3_APPROVALS_ABSENT;        /* none yet: nothing is approved */
    }
    if (r == -1)
        snprintf(why, sizeof why, "it cannot be read now");
    else if (r < 0)
        snprintf(why, sizeof why, "not a file, or larger than %d bytes", MAX_APPROVAL_FILE);
    else if (memchr(text, 0, len))
        snprintf(why, sizeof why, "a NUL byte");
    else if (!(cfg = recomp_cfg_load_string(text, s_approval_file, why, sizeof why)))
        ;                                   /* why says */
    else if (recomp_cfg_type_of(cfg, "approved") != RCFG_ARRAY && recomp_cfg_type_of(cfg, "approved") != RCFG_NONE)
        snprintf(why, sizeof why, "approved must be a list of strings");
    else {
        size_t n = recomp_cfg_array_len(cfg, "approved"), dropped = 0;
        if (!s_approved)
            s_approved = calloc(MAX_APPROVALS, sizeof *s_approved);
        for (size_t i = 0; s_approved && i < n; i++) {
            const char *e = recomp_cfg_array_string(cfg, "approved", i, "");
            size_t el = strlen(e);
            if (recomp_cfg_array_type(cfg, "approved", i) != RCFG_STRING || el >= sizeof *s_approved
                    || s_napproved >= MAX_APPROVALS) {
                dropped++;
                continue;
            }
            for (size_t k = 0; k <= el; k++)    /* ids and "sha256:" are lower case; the digits may be either */
                s_approved[s_napproved][k] = (char)(e[k] >= 'A' && e[k] <= 'Z' ? e[k] + ('a' - 'A') : e[k]);
            kept &= entry_ok(s_approved[s_napproved]);
            s_napproved++;
        }
        s_approvals_ok = s_approved != NULL;
        kept &= !dropped;
        if (dropped && !quiet)
            XR_NOTICE("[PLUGIN] approval file: %d entries not used (not a string, longer than an approval, or past %d): %s\n",
                      (int)dropped, MAX_APPROVALS, s_approval_file);
    }
    free(text);
    recomp_cfg_free(cfg);
    if (!s_approvals_ok) {
        if (!quiet)
            XR_NOTICE("[PLUGIN] approval file not used, no plugin is approved (%s): %s\n", why[0] ? why : "not TOML",
                      s_approval_file);
        return BO3_APPROVALS_BAD;
    }
    return kept ? BO3_APPROVALS_OK : BO3_APPROVALS_KEPT;
}

static int approved(const char *id, const char *sha)
{
    char want[sizeof *s_approved];

    snprintf(want, sizeof want, "%s sha256:%s", id, sha);
    for (int i = 0; s_approvals_ok && i < s_napproved; i++)
        if (!strcmp(s_approved[i], want))
            return 1;
    return 0;
}

/* ---- loading ---- */

/* atexit: each running plugin's on_shutdown, newest first, once. */
static void run_shutdowns(void)
{
    for (int k = s_norder - 1; k >= 0; k--) {
        int i = s_order[k];
        void (TD_CALL *fn)(void *) = s_info[i].on_shutdown;
        if (s_started[i] && fn) {
            s_started[i] = 0;               /* once */
            fn(s_info[i].user);
        }
    }
}

static void set(int i, bo3_plugin_state st, const char *fmt, const char *a, const char *b)
{
    s_slots[i].state = st;
    snprintf(s_slots[i].why, sizeof s_slots[i].why, fmt ? fmt : "", a ? a : "", b ? b : "");
}

/* Mod i's plugin, when it may run. */
static void load_one(int i)
{
    const bo3_mod *m = bo3_modfs_mod(i);
    const char *id = m->manifest.id;
    char path[BO3_MODFS_PATH_MAX], why[200] = "", hex[65];
    uint8_t sha[32];
    bo3_plugin_file *f;
    void *lib;
    td_plugin_api_version_fn ver_fn;
    td_plugin_init_fn init_fn;
    uint32_t ver;
    int32_t rc;
    td_plugin_info info;

    if (m->manifest.plugin_api < 1 || (unsigned)m->manifest.plugin_api > TD_PLUGIN_API_VERSION) {
        snprintf(why, sizeof why, "it needs plugin API %d; this Takedawn has 1 to %u", m->manifest.plugin_api,
                 TD_PLUGIN_API_VERSION);
        set(i, BO3_PLUGIN_REFUSED, "%s", why, NULL);
        XR_NOTICE("[PLUGIN] %s: not run: %s\n", id, why);
        return;
    }
    if (!bo3_modfs_mod_file(m, m->manifest.plugin_path, path, sizeof path)) {
        set(i, BO3_PLUGIN_REFUSED, "the module %s is not a regular file in the mod's folder (missing, a folder, "
            "or reached through a link)", m->manifest.plugin_path, NULL);
        XR_NOTICE("[PLUGIN] %s: not run: %s\n", id, s_slots[i].why);
        return;
    }
    f = bo3_plugin_sys_hold(path, sha, why, sizeof why);
    if (!f) {
        set(i, BO3_PLUGIN_REFUSED, "%s", why, NULL);
        XR_NOTICE("[PLUGIN] %s: not run: %s: %s\n", id, why, path);
        return;
    }
    xr_sha256_hex(sha, hex);
    memcpy(s_slots[i].sha, hex, sizeof hex);
    if (!approved(id, hex)) {
        bo3_plugin_sys_release(f);
        set(i, BO3_PLUGIN_NOT_APPROVED, "not approved for these bytes", NULL, NULL);
        if (s_approval_file[0])
            XR_NOTICE("[PLUGIN] %s: not run: not approved. It is native code and runs with your permissions; "
                      "to allow this module, add \"%s sha256:%s\" to approved in %s\n", id, id, hex, s_approval_file);
        else
            XR_NOTICE("[PLUGIN] %s: not run: there is no approval file (TAKEDAWN_PLUGIN_APPROVALS is empty)\n", id);
        return;
    }
    s_slots[i].approved = 1;
    XR_NOTICE("[PLUGIN] %s: loading %s (sha256 %s)\n", id, path, hex);
    lib = bo3_plugin_sys_load(f, why, sizeof why);
    bo3_plugin_sys_release(f);
    if (!lib) {
        set(i, BO3_PLUGIN_REFUSED, "%s", why, NULL);
        XR_NOTICE("[PLUGIN] %s: not run: %s\n", id, why);
        return;
    }
    /* From here the library is in the process (its own start-up code has run)
     * and stays: a refusal below means only that it is not initialised. */
    ver_fn = (td_plugin_api_version_fn)bo3_plugin_sys_symbol(lib, TD_PLUGIN_API_VERSION_SYMBOL);
    init_fn = (td_plugin_init_fn)bo3_plugin_sys_symbol(lib, TD_PLUGIN_INIT_SYMBOL);
    if (!ver_fn || !init_fn) {
        set(i, BO3_PLUGIN_REFUSED, "it does not export %s", !ver_fn ? TD_PLUGIN_API_VERSION_SYMBOL : TD_PLUGIN_INIT_SYMBOL, NULL);
        XR_NOTICE("[PLUGIN] %s: not started: %s (the library stays loaded)\n", id, s_slots[i].why);
        return;
    }
    ver = ver_fn();
    if (ver != (uint32_t)m->manifest.plugin_api) {
        snprintf(why, sizeof why, "it was built for plugin API %u, its mod.toml says %d", (unsigned)ver,
                 m->manifest.plugin_api);
        set(i, BO3_PLUGIN_REFUSED, "%s", why, NULL);
        XR_NOTICE("[PLUGIN] %s: not started: %s (the library stays loaded)\n", id, why);
        return;
    }
    s_hosts[i].index = i;
    snprintf(s_hosts[i].id, sizeof s_hosts[i].id, "%s", id);
    s_api[i].struct_size = (uint32_t)sizeof s_api[i];
    s_api[i].api_version = ver;
    s_api[i].host = &s_hosts[i];
    s_api[i].log = api_log;
    s_api[i].mod_id = api_mod_id;
    s_api[i].host_version = api_host_version;
    memset(&info, 0, sizeof info);
    info.struct_size = (uint32_t)sizeof info;   /* the room the plugin has: it writes no field past it */
    rc = init_fn(&s_api[i], &info);
    if (rc != TD_OK) {
        snprintf(why, sizeof why, "its init returned %d", (int)rc);
        set(i, BO3_PLUGIN_FAILED, "%s", why, NULL);
        XR_NOTICE("[PLUGIN] %s: not started: %s (the library stays loaded)\n", id, why);
        return;
    }
    memset(&s_info[i], 0, sizeof s_info[i]);
    if (info.struct_size >= offsetof(td_plugin_info, user) + sizeof info.user) {   /* a larger size: only ours is read */
        s_info[i].on_shutdown = info.on_shutdown;
        s_info[i].user = info.user;
    }
    s_started[i] = 1;
    if (s_norder < BO3_MODFS_MAX_MODS)
        s_order[s_norder++] = i;
    if (!s_atexit) {
        s_atexit = 1;
        atexit(run_shutdowns);
    }
    set(i, BO3_PLUGIN_RUNNING, "", NULL, NULL);
    s_running++;
    XR_NOTICE("[PLUGIN] %s: running (plugin API %u)\n", id, (unsigned)ver);
}

void bo3_plugins_approval_file(const char *path)
{
    if (path && strlen(path) >= sizeof s_approval_file) {
        XR_NOTICE("[PLUGIN] approval file path too long, no plugin is approved: %.200s...\n", path);
        path = "";
    }
    snprintf(s_approval_file, sizeof s_approval_file, "%s", path ? path : "");
}

const char *bo3_plugins_off(void)
{
    const char *env = getenv("TAKEDAWN_PLUGINS");
    if (env && !strcmp(env, "0"))
        return "TAKEDAWN_PLUGINS=0";
    if (!bo3_plugin_sys_supported())
        return "this system does not load plugins";
#if defined(__APPLE__)
    if (!(env && !strcmp(env, "1")))
        return "on macOS plugins load only with TAKEDAWN_PLUGINS=1 (not yet verified there)";
#endif
    return NULL;
}

void bo3_plugins_init(const char *host_version)
{
    int declared = 0, n = bo3_modfs_mod_count();
    const char *why_off = bo3_plugins_off();

    snprintf(s_host_version, sizeof s_host_version, "%s", host_version ? host_version : "");
    memset(s_slots, 0, sizeof s_slots);
    s_nslots = n;
    s_running = 0;
    /* A second start (only the unit test makes one) begins a new list: the
     * plugins of the first stay loaded, and their on_shutdown is forgotten. */
    s_norder = 0;
    memset(s_started, 0, sizeof s_started);
    memset(s_info, 0, sizeof s_info);
    for (int i = 0; i < n; i++)
        declared += bo3_modfs_mod(i)->manifest.plugin && bo3_modfs_mod(i)->manifest.plugin_path[0];
    if (!declared) {
        for (int i = 0; i < n; i++)
            s_slots[i].state = bo3_modfs_mod(i)->manifest.plugin ? BO3_PLUGIN_NO_MODULE : BO3_PLUGIN_NONE;
        return;                             /* no mod has native code here: nothing to say */
    }
    if (why_off)
        XR_NOTICE("[PLUGIN] plugins are off (%s): no plugin is run\n", why_off);
    else
        approvals_load(0);
    for (int i = 0; i < n; i++) {
        const bo3_mod *m = bo3_modfs_mod(i);
        if (!m->manifest.plugin)
            s_slots[i].state = BO3_PLUGIN_NONE;
        else if (!m->manifest.plugin_path[0])
            s_slots[i].state = BO3_PLUGIN_NO_MODULE;
        else if (!m->usable || !m->enabled)
            s_slots[i].state = BO3_PLUGIN_OFF;   /* its module is not even opened */
        else if (why_off)
            set(i, BO3_PLUGIN_DISABLED, "%s", why_off, NULL);
        else
            load_one(i);
    }
}

bo3_plugin_state bo3_plugins_state(int i, char *why, size_t wn, char *sha, size_t sn)
{
    if (why && wn)
        why[0] = 0;
    if (sha && sn)
        sha[0] = 0;
    if (i < 0 || i >= s_nslots)
        return BO3_PLUGIN_NONE;
    if (why && wn)
        snprintf(why, wn, "%s", s_slots[i].why);
    if (sha && sn)
        snprintf(sha, sn, "%s", s_slots[i].sha);
    return s_slots[i].state;
}

int bo3_plugins_running(void)
{
    return s_running;
}

int bo3_plugins_was_approved(int i)
{
    return i >= 0 && i < s_nslots && s_slots[i].approved;
}

/* ---- the approval file from the MODS page (modsmenu.c) ----
 * The page reads the file again at each change and writes it whole; the
 * plugins of this start stay as they are (a change is for the next start). */

bo3_approvals_state bo3_plugins_approvals(void)
{
    return approvals_load(1);
}

/* Entry e is one for id. */
static int entry_of(const char *e, const char *id)
{
    size_t n = strlen(id);
    return !strncmp(e, id, n) && e[n] == ' ';
}

int bo3_plugins_approved_for(const char *id, const char *sha)
{
    return id && sha && strlen(sha) == 64 && approved(id, sha);
}

int bo3_plugins_approvals_of(const char *id, char *sha, size_t sn)
{
    int n = 0;
    if (sha && sn)
        sha[0] = 0;
    for (int i = 0; id && s_approvals_ok && i < s_napproved; i++)
        if (entry_of(s_approved[i], id) && n++ == 0 && sha && sn && entry_ok(s_approved[i]))
            snprintf(sha, sn, "%s", s_approved[i] + strlen(id) + 8);
    return n;
}

int bo3_plugins_set_approval(const char *id, const char *sha)
{
    char want[sizeof *s_approved], *text;
    size_t cap, n;
    int keep = 0, mine = 0, same = 0, ok;
    bo3_approvals_state st;

    if (!id || !bo3_manifest_id_ok(id))
        return 0;
    if (sha) {
        snprintf(want, sizeof want, "%s sha256:%s", id, sha);
        if (!entry_ok(want))
            return 0;                       /* 64 lower-case hex digits, nothing else */
    }
    st = approvals_load(1);
    if (st != BO3_APPROVALS_ABSENT && st != BO3_APPROVALS_OK) {
        XR_NOTICE("[PLUGIN] %s: approval not changed: the approval file %s: %s\n", id,
                  st == BO3_APPROVALS_NONE ? "is off (TAKEDAWN_PLUGIN_APPROVALS is empty)"
                  : st == BO3_APPROVALS_KEPT ? "has entries this page does not write back, so it is left as it is"
                  : "cannot be read, or is not an approval file, so it is left as it is", s_approval_file);
        return 0;
    }
    for (int i = 0; i < s_napproved; i++) {
        if (!entry_of(s_approved[i], id))
            keep++;
        else {
            mine++;
            same += sha && !strcmp(s_approved[i], want);
        }
    }
    if (sha ? mine == 1 && same : !mine)
        return 1;                           /* already so: nothing to write */
    if (keep + (sha != NULL) > MAX_APPROVALS) {
        XR_NOTICE("[PLUGIN] %s: not approved: the approval file already holds %d approvals\n", id, MAX_APPROVALS);
        return 0;
    }
    cap = 160 + (size_t)(keep + 1) * (sizeof *s_approved + 4);
    if (!(text = malloc(cap)))
        return 0;
    n = (size_t)snprintf(text, cap, "# Takedawn: native plugins allowed to run, each for exactly these bytes "
                                    "(docs/native-plugins.md). Read at start.\napproved = [");
    keep = 0;
    for (int i = 0; i < s_napproved; i++)
        if (!entry_of(s_approved[i], id))
            n += (size_t)snprintf(text + n, cap - n, "%s\"%s\"", keep++ ? ", " : "", s_approved[i]);
    if (sha)
        n += (size_t)snprintf(text + n, cap - n, "%s\"%s\"", keep ? ", " : "", want);
    n += (size_t)snprintf(text + n, cap - n, "]\n");
    ok = n < cap && n <= MAX_APPROVAL_FILE && bo3_modfs_write_whole(s_approval_file, text, n);
    free(text);
    /* read back: the file must now say exactly that */
    if (ok && (approvals_load(1) != BO3_APPROVALS_OK
               || (sha ? !approved(id, sha) || bo3_plugins_approvals_of(id, NULL, 0) != 1
                       : bo3_plugins_approvals_of(id, NULL, 0) != 0)))
        ok = 0;
    if (!ok)
        XR_NOTICE("[PLUGIN] %s: approval file not written (%s): %s\n", id, sha ? "approval" : "removal", s_approval_file);
    else if (sha)
        XR_NOTICE("[PLUGIN] %s: approved from the next start for sha256 %s: %s\n", id, sha, s_approval_file);
    else
        XR_NOTICE("[PLUGIN] %s: approval removed from the next start (a running plugin runs until Takedawn exits): %s\n",
                  id, s_approval_file);
    return ok;
}

int bo3_plugins_hash_module(int i, char sha[65], char *why, size_t wn)
{
    const bo3_mod *m = bo3_modfs_mod(i);
    char path[BO3_MODFS_PATH_MAX], w[200] = "";
    uint8_t d[32];
    bo3_plugin_file *f;

    sha[0] = 0;
    if (!m || !m->usable || !m->manifest.plugin || !m->manifest.plugin_path[0]) {
        snprintf(why, wn, "no module for this system");
        return 0;
    }
    if (!bo3_modfs_mod_file(m, m->manifest.plugin_path, path, sizeof path)) {
        snprintf(why, wn, "the module %s is not a regular file in the mod's folder (missing, a folder, or reached "
                 "through a link)", m->manifest.plugin_path);
        XR_NOTICE("[PLUGIN] %s: not hashed: %s\n", m->manifest.id, why);
        return 0;
    }
    f = bo3_plugin_sys_hold(path, d, w, sizeof w);   /* read and hashed as a start does; never loaded */
    if (!f) {
        snprintf(why, wn, "%s", w);
        XR_NOTICE("[PLUGIN] %s: not hashed: %s: %s\n", m->manifest.id, w, path);
        return 0;
    }
    bo3_plugin_sys_release(f);
    xr_sha256_hex(d, sha);
    return 1;
}
