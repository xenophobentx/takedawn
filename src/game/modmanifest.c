/*
 * modmanifest.c - the fields of a mod.toml (modmanifest.h). The toolkit's
 * reader (src/enhance/recomp_cfg.c) does the TOML; this checks what a mod may
 * put in each field.
 */
#include "modmanifest.h"

#include <stdio.h>
#include <string.h>

#include "recomp_cfg.h"

typedef struct {
    const char *key;
    size_t off, max;
    int required;
    int (*ok)(const char *s);   /* NULL: display text, checked by text_ok */
} field;

static int id_ok(const char *s)
{
    if (!((*s >= 'a' && *s <= 'z') || (*s >= '0' && *s <= '9')))
        return 0;
    for (; *s; s++)
        if (!((*s >= 'a' && *s <= 'z') || (*s >= '0' && *s <= '9') || *s == '.' || *s == '_' || *s == '-'))
            return 0;
    return 1;
}

static int version_ok(const char *s)
{
    if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9')))
        return 0;
    for (; *s; s++)
        if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9')
                || *s == '.' || *s == '+' || *s == '-'))
            return 0;
    return 1;
}

/* The reader has checked the UTF-8; text to show must also be free of what
 * could change how it or the text after it is shown: C0 and C1 control
 * characters, DEL, the line and paragraph separators (U+2028, U+2029) and the
 * bidirectional marks, embeddings, overrides and isolates (U+061C, U+200E,
 * U+200F, U+202A-U+202E, U+2066-U+2069), escaped or not. */
static int text_ok(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    for (; *p; p++) {
        if (*p < 0x20 || *p == 0x7F)
            return 0;
        if (p[0] == 0xC2 && p[1] >= 0x80 && p[1] <= 0x9F)
            return 0;   /* U+0080-U+009F */
        if (p[0] == 0xE2 && p[1] == 0x80 && ((p[2] >= 0xA8 && p[2] <= 0xAE) || p[2] == 0x8E || p[2] == 0x8F))
            return 0;   /* U+2028-U+202E, U+200E, U+200F */
        if (p[0] == 0xD8 && p[1] == 0x9C)
            return 0;   /* U+061C */
        if (p[0] == 0xE2 && p[1] == 0x81 && p[2] >= 0xA6 && p[2] <= 0xA9)
            return 0;   /* U+2066-U+2069 */
    }
    return 1;
}

/* The reader hands strings back NUL-terminated, so a string holding U+0000
 * would come back cut short. Any \u0000 or \U00000000 in the text refuses the
 * manifest (also one in a comment: strict, and no mod needs one). */
static int has_nul_escape(const char *t, size_t n)
{
    for (size_t i = 0; i + 1 < n; i++) {
        size_t d = t[i + 1] == 'u' ? 4 : t[i + 1] == 'U' ? 8 : 0, k;
        if (t[i] != '\\' || !d || i + 2 + d > n)
            continue;
        for (k = 0; k < d && t[i + 2 + k] == '0'; k++)
            ;
        if (k == d)
            return 1;
    }
    return 0;
}

/* A known key the reader reports as missing may still be a table ([version],
 * or version.major = 1), which the reader does not report as a key. The text
 * read once more with a [key] header added says so: the header is refused
 * exactly when key already is a table. buf holds the text (n bytes) and room
 * for the header. */
static int is_table(char *buf, size_t n, const char *key)
{
    char why[160];
    recomp_cfg *cfg;

    snprintf(buf + n, BO3_MANIFEST_MAX_BYTES + 80 - n, "\n[%s]\n", key);
    cfg = recomp_cfg_load_string(buf, "mod.toml", why, sizeof why);
    buf[n] = 0;
    recomp_cfg_free(cfg);
    return cfg == NULL;
}

int bo3_manifest_id_ok(const char *id)
{
    return id && *id && strlen(id) <= BO3_MOD_ID_MAX && id_ok(id);
}

/* ---- [plugin] (docs/native-plugins.md) ---- */

/* The systems a [plugin] table may name, each with the module endings its
 * loader takes (plugins_win32.c / plugins_posix.c). */
static const struct {
    const char *key;
    const char *ends[3];
} s_platforms[] = {
    { "windows", { ".asi", ".dll", NULL } },
    { "linux",   { ".so", NULL } },
    { "macos",   { ".dylib", NULL } },
    { "android", { ".so", NULL } },
};
#if defined(_WIN32)
#define THIS_PLATFORM 0
#elif defined(__ANDROID__)
#define THIS_PLATFORM 3
#elif defined(__APPLE__)
#define THIS_PLATFORM 2
#else
#define THIS_PLATFORM 1
#endif

const char *bo3_manifest_platform(void)
{
    return s_platforms[THIS_PLATFORM].key;
}

static int lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

static int ends_nocase(const char *s, size_t n, const char *end)
{
    size_t e = strlen(end);
    if (n < e)
        return 0;
    for (size_t i = 0; i < e; i++)
        if (lower((unsigned char)s[n - e + i]) != end[i])
            return 0;
    return 1;
}

/* A Windows device name as a path part (CON, PRN, AUX, NUL, CLOCK$, CONIN$,
 * CONOUT$, COM1-9, LPT1-9), with or without an ending and in any case: the
 * part up to its first dot, trailing spaces dropped, as Windows reads it.
 * Refused on every system, so a manifest means the same everywhere. */
static int device_name(const char *p, size_t len)
{
    static const char *const names[] = { "con", "prn", "aux", "nul", "clock$", "conin$", "conout$" };
    char low[8];
    size_t b = 0;

    while (b < len && p[b] != '.')
        b++;
    while (b && p[b - 1] == ' ')
        b--;
    if (b == 0 || b >= sizeof low)
        return 0;
    for (size_t i = 0; i < b; i++)
        low[i] = (char)lower((unsigned char)p[i]);
    low[b] = 0;
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
        if (!strcmp(low, names[i]))
            return 1;
    return b == 4 && (!strncmp(low, "com", 3) || !strncmp(low, "lpt", 3)) && low[3] >= '1' && low[3] <= '9';
}

/* A module path: relative, plain ASCII parts (the mod folders' own rule:
 * no "." or "..", no trailing dot or space, none of <>:"|?* or a control
 * character), not in the mod's files folder (whose contents the game reads
 * as disc files), ending as platform p's modules do. 1, or 0 with why. */
static int module_path_ok(const char *v, int p, char *why, size_t wn)
{
    size_t n = strlen(v), start = 0;
    int part = 0;

    if (!n || n > BO3_PLUGIN_PATH_MAX) {
        snprintf(why, wn, "plugin.%s must be 1 to %d bytes", s_platforms[p].key, BO3_PLUGIN_PATH_MAX);
        return 0;
    }
    for (size_t i = 0; i <= n; i++) {
        size_t len;
        if (v[i] != '/' && v[i] != '\\' && v[i])
            continue;
        len = i - start;
        if (len == 0 || (len == 1 && v[start] == '.') || (len == 2 && v[start] == '.' && v[start + 1] == '.')
                || v[i - 1] == '.' || v[i - 1] == ' ') {
            snprintf(why, wn, "plugin.%s: a part of the path is empty, . or .., or ends in a dot or a space",
                     s_platforms[p].key);
            return 0;
        }
        for (size_t k = start; k < i; k++)
            if ((unsigned char)v[k] < 0x20 || (unsigned char)v[k] > 0x7E || strchr("<>:\"|?*", v[k])) {
                snprintf(why, wn, "plugin.%s: only a relative path of plain names (no drive, no < > : \" | ? *)",
                         s_platforms[p].key);
                return 0;
            }
        if (device_name(v + start, len)) {
            snprintf(why, wn, "plugin.%s: a part of the path is a Windows device name (CON, NUL, COM1 ...)",
                     s_platforms[p].key);
            return 0;
        }
        if (part++ == 0 && len == 5 && lower((unsigned char)v[start]) == 'f' && ends_nocase(v + start, 5, "files")) {
            snprintf(why, wn, "plugin.%s: the module may not be in the files folder", s_platforms[p].key);
            return 0;
        }
        start = i + 1;
    }
    for (int e = 0; s_platforms[p].ends[e]; e++)
        if (ends_nocase(v, n, s_platforms[p].ends[e]))
            return 1;
    snprintf(why, wn, "plugin.%s must name a %s%s%s file", s_platforms[p].key, s_platforms[p].ends[0],
             s_platforms[p].ends[1] ? " or " : "", s_platforms[p].ends[1] ? s_platforms[p].ends[1] : "");
    return 0;
}

/* The [plugin] table into out, when the manifest has one. 1, or 0 with why. */
static int read_plugin(const recomp_cfg *cfg, char *buf, size_t n, bo3_manifest *out, char *why, size_t wn)
{
    int has = 0;
    long long api;

    if (recomp_cfg_type_of(cfg, "plugin") != RCFG_NONE) {
        snprintf(why, wn, "plugin must be a table ([plugin])");
        return 0;
    }
    for (size_t i = 0; i < recomp_cfg_count(cfg) && !has; i++)
        has = !strncmp(recomp_cfg_key_at(cfg, i), "plugin.", 7);
    if (!has && !is_table(buf, n, "plugin"))
        return 1;                         /* no native code: a data mod, as before */
    out->plugin = 1;
    switch (recomp_cfg_type_of(cfg, "plugin.api")) {
    case RCFG_NONE:
        snprintf(why, wn, is_table(buf, n, "plugin.api") ? "plugin.api must be a whole number" : "plugin.api is missing");
        return 0;
    case RCFG_INT:
        break;
    default:
        snprintf(why, wn, "plugin.api must be a whole number");
        return 0;
    }
    api = recomp_cfg_int(cfg, "plugin.api", 0);
    if (api < 1 || api > 65535) {
        snprintf(why, wn, "plugin.api must be from 1 to 65535");
        return 0;
    }
    out->plugin_api = (int)api;
    for (int p = 0; p < (int)(sizeof s_platforms / sizeof s_platforms[0]); p++) {
        char key[24];
        recomp_cfg_type t;
        snprintf(key, sizeof key, "plugin.%s", s_platforms[p].key);
        t = recomp_cfg_type_of(cfg, key);
        if (t == RCFG_NONE) {
            if (is_table(buf, n, key)) {
                snprintf(why, wn, "%s must be a string", key);
                return 0;
            }
            continue;
        }
        if (t != RCFG_STRING) {
            snprintf(why, wn, "%s must be a string", key);
            return 0;
        }
        if (!module_path_ok(recomp_cfg_string(cfg, key, ""), p, why, wn))
            return 0;
        if (p == THIS_PLATFORM)
            snprintf(out->plugin_path, sizeof out->plugin_path, "%s", recomp_cfg_string(cfg, key, ""));
    }
    return 1;
}

static const field s_fields[] = {
    { "id",          offsetof(bo3_manifest, id),          BO3_MOD_ID_MAX,      1, id_ok },
    { "name",        offsetof(bo3_manifest, name),        BO3_MOD_NAME_MAX,    1, NULL },
    { "version",     offsetof(bo3_manifest, version),     BO3_MOD_VERSION_MAX, 0, version_ok },
    { "author",      offsetof(bo3_manifest, author),      BO3_MOD_AUTHOR_MAX,  0, NULL },
    { "description", offsetof(bo3_manifest, description), BO3_MOD_DESC_MAX,    0, NULL },
};

int bo3_manifest_parse(const char *text, size_t n, bo3_manifest *out, char *err, size_t errn)
{
    char buf[BO3_MANIFEST_MAX_BYTES + 80];   /* the text and is_table's header */
    char why[160];
    recomp_cfg *cfg;
    int ok = 1;

    memset(out, 0, sizeof *out);
    why[0] = 0;
    if (!text || n == 0)
        snprintf(why, sizeof why, "empty");
    else if (n > BO3_MANIFEST_MAX_BYTES)
        snprintf(why, sizeof why, "larger than %d bytes", BO3_MANIFEST_MAX_BYTES);
    else if (memchr(text, 0, n))
        snprintf(why, sizeof why, "a NUL byte");   /* the reader takes a C string */
    else if (has_nul_escape(text, n))
        snprintf(why, sizeof why, "a \\u0000 escape");   /* would end the string early */
    if (why[0]) {
        if (errn)
            snprintf(err, errn, "mod.toml: %s", why);
        return 0;
    }
    memcpy(buf, text, n);
    buf[n] = 0;
    cfg = recomp_cfg_load_string(buf, "mod.toml", why, sizeof why);
    if (!cfg) {
        if (errn)
            snprintf(err, errn, "%s", why[0] ? why : "not readable TOML");
        return 0;
    }
    for (size_t i = 0; ok && i < sizeof s_fields / sizeof s_fields[0]; i++) {
        const field *f = &s_fields[i];
        recomp_cfg_type t = recomp_cfg_type_of(cfg, f->key);
        const char *v;
        size_t len;

        if (t == RCFG_NONE) {
            if (is_table(buf, n, f->key)) {
                snprintf(why, sizeof why, "%s must be a string", f->key);
                ok = 0;
            } else if (f->required) {
                snprintf(why, sizeof why, "%s is missing", f->key);
                ok = 0;
            }
            continue;
        }
        if (t != RCFG_STRING) {
            snprintf(why, sizeof why, "%s must be a string", f->key);
            ok = 0;
            continue;
        }
        v = recomp_cfg_string(cfg, f->key, "");
        len = strlen(v);
        if (len > f->max) {
            snprintf(why, sizeof why, "%s is longer than %d bytes", f->key, (int)f->max);
            ok = 0;
        } else if (!len && (f->required || f->ok)) {
            snprintf(why, sizeof why, "%s is empty", f->key);
            ok = 0;
        } else if (f->ok ? !f->ok(v) : !text_ok(v)) {
            if (f->ok == id_ok)
                snprintf(why, sizeof why, "id must be a-z 0-9 . _ - starting with a letter or digit");
            else if (f->ok == version_ok)
                snprintf(why, sizeof why, "version must be A-Z a-z 0-9 . + - starting with a letter or digit");
            else
                snprintf(why, sizeof why, "%s has a control character", f->key);
            ok = 0;
        } else {
            memcpy((char *)out + f->off, v, len + 1);
        }
    }
    if (ok)
        ok = read_plugin(cfg, buf, n, out, why, sizeof why);
    recomp_cfg_free(cfg);
    if (!ok) {
        memset(out, 0, sizeof *out);
        if (errn)
            snprintf(err, errn, "mod.toml: %s", why);
        return 0;
    }
    if (errn)
        err[0] = 0;
    return 1;
}
