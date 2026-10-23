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
