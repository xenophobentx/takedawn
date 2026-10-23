/* mod.toml (src/game/modmanifest.c over the toolkit's TOML reader): what a
 * manifest may hold, every rejection the format promises, and a mutation run
 * that checks the guarantees on damaged input: nothing is read past the buffer
 * (each case is its own heap block; run it under AddressSanitizer to have that
 * checked), no field is kept from a rejected manifest, and every
 * field kept is well-formed. No files, no game data. */
#include "modmanifest.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int s_fail, s_pass;
#define CHECK(c, ...) do { if (c) s_pass++; else { s_fail++; fprintf(stderr, "FAIL line %d: ", __LINE__); \
    fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

static bo3_manifest s_m;
static char s_err[160];

static int parse(const char *t)
{
    return bo3_manifest_parse(t, strlen(t), &s_m, s_err, sizeof s_err);
}

/* Rejected, with a reason that contains want. */
static void bad(int line, const char *t, const char *want)
{
    int ok = parse(t);
    int cleared = !s_m.id[0] && !s_m.name[0] && !s_m.version[0] && !s_m.author[0] && !s_m.description[0];
    if (!ok && cleared && strstr(s_err, want))
        s_pass++;
    else {
        s_fail++;
        fprintf(stderr, "FAIL line %d: expected rejection containing \"%s\", got %s \"%s\"\n", line, want,
                ok ? "ACCEPTED" : "rejection", s_err);
    }
}
#define BAD(t, want) bad(__LINE__, t, want)

static int utf8_clean(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        int n = *p < 0x80 ? 1 : (*p & 0xE0) == 0xC0 ? 2 : (*p & 0xF0) == 0xE0 ? 3 : (*p & 0xF8) == 0xF0 ? 4 : 0;
        if (!n || *p < 0x20 || *p == 0x7F)
            return 0;
        for (int i = 1; i < n; i++)
            if ((p[i] & 0xC0) != 0x80)
                return 0;
        p += n;
    }
    return 1;
}

static void accepted(void)
{
    CHECK(parse("id = \"example.car-pack\"\nname = \"Example Car Pack\"\n"), "minimal: %s", s_err);
    CHECK(!strcmp(s_m.id, "example.car-pack") && !strcmp(s_m.name, "Example Car Pack") && !s_m.version[0]
          && !s_m.author[0] && !s_m.description[0], "minimal fields");
    CHECK(parse("# a comment\n\nid = \"a\"   # trailing\nname = \"A\"\nversion = \"1.0.0-beta+2\"\n"
                "author = \"Someone\"\ndescription = \"Two\\u00e9 \\\"words\\\" and \\\\.\"\n"),
          "all fields: %s", s_err);
    CHECK(!strcmp(s_m.version, "1.0.0-beta+2") && !strcmp(s_m.author, "Someone")
          && !strcmp(s_m.description, "Two\xc3\xa9 \"words\" and \\."), "escapes decoded: [%s]", s_m.description);
    CHECK(parse("\xEF\xBB\xBFid = \"bom\"\r\nname = \"With BOM and CRLF\"\r\n"), "BOM + CRLF: %s", s_err);
    CHECK(parse("id=\"x\"\nname=\"no spaces, no final newline\""), "no spaces / no final LF: %s", s_err);
    CHECK(parse("\t id \t= \t\"x\"\t\nname = \"tabs\"\n"), "tabs: %s", s_err);
    CHECK(parse("id = \"x\"\nname = \"x\"\nhomepage = \"https://example.org\"\nlicense = \"MIT\"\n"),
          "unknown string keys skipped: %s", s_err);
    CHECK(parse("id = \"x\"\nname = \"x\"\ntags = [\"a\", \"b\"]\npriority = 5\nbeta = true\n[future]\nkey = 1\n"),
          "unknown keys of other types and tables left alone: %s", s_err);
    CHECK(parse("id = 'lit.id'\nname = 'C:\\path\\kept'\n") && !strcmp(s_m.id, "lit.id")
          && !strcmp(s_m.name, "C:\\path\\kept"), "literal strings: %s", s_err);
    CHECK(parse("\"id\" = \"q\"\nname = \"x\"\n") && !strcmp(s_m.id, "q"), "quoted key: %s", s_err);
    CHECK(parse("id = \"x\"\nname = \"\xe6\x97\xa5\xe6\x9c\xac \xf0\x9f\x8f\x81\"\n") && utf8_clean(s_m.name),
          "UTF-8 name kept: %s", s_err);
    CHECK(parse("id = \"x\"\nname = \"\\U0001F3C1\"\n") && !strcmp(s_m.name, "\xf0\x9f\x8f\x81"), "\\U escape: %s", s_err);
    {
        char id[BO3_MOD_ID_MAX + 1], t[400];
        memset(id, 'a', BO3_MOD_ID_MAX);
        id[BO3_MOD_ID_MAX] = 0;
        snprintf(t, sizeof t, "id = \"%s\"\nname = \"n\"\n", id);
        CHECK(parse(t) && strlen(s_m.id) == BO3_MOD_ID_MAX, "id at the limit: %s", s_err);
    }
    {
        static char t[BO3_MANIFEST_MAX_BYTES + 1];
        size_t n = 0;
        n += (size_t)sprintf(t, "id = \"x\"\nname = \"n\"\n");
        while (n + 3 <= BO3_MANIFEST_MAX_BYTES)
            n += (size_t)sprintf(t + n, "#\n");   /* pad with comment lines to exactly the limit */
        while (n < BO3_MANIFEST_MAX_BYTES)
            t[n++] = '\n';
        CHECK(bo3_manifest_parse(t, n, &s_m, s_err, sizeof s_err) && n == BO3_MANIFEST_MAX_BYTES, "8192 bytes: %s", s_err);
    }
}

static void id_rule(void)
{
    char long_id[BO3_MOD_ID_MAX + 2];
    memset(long_id, 'a', sizeof long_id - 1);
    long_id[sizeof long_id - 1] = 0;
    CHECK(bo3_manifest_id_ok("a") && bo3_manifest_id_ok("example.car-pack_2") && bo3_manifest_id_ok("0x"),
          "bo3_manifest_id_ok: valid ids");
    CHECK(!bo3_manifest_id_ok(NULL) && !bo3_manifest_id_ok("") && !bo3_manifest_id_ok("A") && !bo3_manifest_id_ok(".a")
          && !bo3_manifest_id_ok("a b") && !bo3_manifest_id_ok("a\"b") && !bo3_manifest_id_ok(long_id),
          "bo3_manifest_id_ok: invalid ids");
    long_id[BO3_MOD_ID_MAX] = 0;
    CHECK(bo3_manifest_id_ok(long_id), "bo3_manifest_id_ok: 64 bytes");
}

static void rejected(void)
{
    BAD("", "empty");
    BAD("# only a comment\n", "id is missing");
    BAD("name = \"no id\"\n", "id is missing");
    BAD("id = \"x\"\n", "name is missing");
    BAD("id = \"\"\nname = \"x\"\n", "id is empty");
    BAD("id = 5\nname = \"x\"\n", "id must be a string");
    BAD("id = \"x\"\nname = [\"x\"]\n", "name must be a string");
    BAD("id = \"x\"\nname = \"x\"\nversion = 1.0\n", "version must be a string");
    BAD("id = \"Upper\"\nname = \"x\"\n", "id must be");
    BAD("id = \"-lead\"\nname = \"x\"\n", "id must be");
    BAD("id = \"a/b\"\nname = \"x\"\n", "id must be");
    BAD("id = \"a b\"\nname = \"x\"\n", "id must be");
    BAD("id = \"x\"\nname = \"\"\n", "name is empty");
    BAD("id = \"x\"\nname = \"x\"\nversion = \"\"\n", "version is empty");
    BAD("id = \"x\"\nname = \"x\"\nversion = \"1.0 beta\"\n", "version must be");
    BAD("id = \"x\"\nname = \"x\"\nversion = \".1\"\n", "version must be");
    BAD("id = \"x\"\nid = \"y\"\nname = \"x\"\n", "duplicate key");
    BAD("id = \"x\"\nname = \"x\"\nfoo = \"1\"\nfoo = \"2\"\n", "duplicate key");
    BAD("[mod]\nid = \"x\"\nname = \"x\"\n", "id is missing");     /* only top-level keys */
    BAD("mod.id = \"x\"\nname = \"x\"\n", "id is missing");
    BAD("id = \"\"\"x\"\"\"\nname = \"x\"\n", "mod.toml:1");         /* multi-line strings: not read */
    BAD("id \"x\"\nname = \"x\"\n", "mod.toml:1");
    BAD("= \"x\"\n", "mod.toml:1");
    BAD("id = \"x\nname = \"x\"\n", "unterminated");
    BAD("id = \"x\" y\nname = \"x\"\n", "expected end of line");
    BAD("id = \"x\"\nname = \"a\\qb\"\n", "bad escape");
    BAD("id = \"x\"\nname = \"\\u12\"\n", "mod.toml:2");
    BAD("id = \"x\"\nname = \"\\ud800\"\n", "bad unicode");
    BAD("id = \"x\"\nname = \"\\u0000\"\n", "\\u0000 escape");
    BAD("id = \"a\\u0000x\"\nname = \"x\"\n", "\\u0000 escape");      /* would cut the id to "a" */
    BAD("id = \"x\"\nname = \"x\"\ndescription = \"\\U00000000\"\n", "\\u0000 escape");
    BAD("id = \"x\"\nname = \"a\\nb\"\n", "control character");
    BAD("id = \"x\"\nname = \"tab\tin\"\n", "control character");
    BAD("id = \"x\"\nname = \"bell\\u0007\"\n", "control character");
    BAD("id = \"x\"\nname = \"del\\u007f\"\n", "control character");
    BAD("id = \"x\"\nname = \"csi\\u009b31m\"\n", "control character");                 /* C1 */
    BAD("id = \"x\"\nname = \"raw C1 \xc2\x85\"\n", "control character");
    BAD("id = \"x\"\nname = \"rlo\\u202ecba\"\n", "control character");                 /* bidi override */
    BAD("id = \"x\"\nauthor = \"iso\\u2066x\"\nname = \"x\"\n", "control character");    /* bidi isolate */
    BAD("id = \"x\"\nname = \"rlm\\u200Fx\"\n", "control character");                    /* bidi marks */
    BAD("id = \"x\"\nname = \"lrm\xE2\x80\x8Ex\"\n", "control character");
    BAD("id = \"x\"\nname = \"alm\\u061Cx\"\n", "control character");
    /* A known key written as a table is not a string, however the table is made. */
    BAD("id = \"x\"\nname = \"x\"\n[version]\n", "version must be a string");
    BAD("id = \"x\"\nname = \"x\"\nversion.major = 1\n", "version must be a string");
    BAD("id = \"x\"\nname = \"x\"\n[author]\nfirst = \"a\"\n", "author must be a string");
    BAD("name = \"x\"\n[id]\n", "id must be a string");
    CHECK(parse("id = \"x\"\nname = \"x\"\n[extra]\nversion = 1\n[versions]\n[extra.author]\n# end, no newline")
          && !s_m.version[0] && !s_m.author[0], "a table holding a known key's name, or named like one: %s", s_err);
    BAD("id = \"x\"\nname = \"x\"\ndescription = \"line\\u2028sep\"\n", "control character");
    BAD("id = \"x\"\rname = \"x\"\n", "mod.toml:1");                /* a CR alone is not a line end */
    BAD("id = \"x\"\nname = \"\xC0\xAF\"\n", "invalid UTF-8");          /* overlong '/' */
    BAD("id = \"x\"\nname = \"\xED\xA0\x80\"\n", "invalid UTF-8");      /* surrogate */
    BAD("id = \"x\"\nname = \"\xF4\x90\x80\x80\"\n", "invalid UTF-8");  /* > U+10FFFF */
    BAD("id = \"x\"\nname = \"\xE6\x97\"\n", "invalid UTF-8");          /* cut short */
    BAD("id = \"x\"\nname = \"\xFF\"\n", "invalid UTF-8");
    {
        static const char nul[] = "id = \"x\"\nna\0me = \"x\"\n";
        CHECK(!bo3_manifest_parse(nul, sizeof nul - 1, &s_m, s_err, sizeof s_err) && strstr(s_err, "NUL"), "NUL byte: %s", s_err);
    }
    {
        char id[BO3_MOD_ID_MAX + 2], t[400];
        memset(id, 'a', BO3_MOD_ID_MAX + 1);
        id[BO3_MOD_ID_MAX + 1] = 0;
        snprintf(t, sizeof t, "id = \"%s\"\nname = \"n\"\n", id);
        BAD(t, "longer than");
    }
    {
        static char t[BO3_MOD_DESC_MAX + 200];
        int k = sprintf(t, "id = \"x\"\nname = \"x\"\ndescription = \"");
        memset(t + k, 'd', BO3_MOD_DESC_MAX + 1);
        strcpy(t + k + BO3_MOD_DESC_MAX + 1, "\"\n");
        BAD(t, "longer than");
    }
    {
        static char t[BO3_MANIFEST_MAX_BYTES + 2];
        memset(t, '\n', sizeof t - 1);
        memcpy(t, "id = \"x\"\nname = \"x\"\n", 20);
        CHECK(!bo3_manifest_parse(t, BO3_MANIFEST_MAX_BYTES + 1, &s_m, s_err, sizeof s_err) && strstr(s_err, "larger"),
              "8193 bytes: %s", s_err);
    }
    CHECK(!bo3_manifest_parse(NULL, 0, &s_m, s_err, sizeof s_err), "NULL text");
    CHECK(!bo3_manifest_parse("id = \"x\"\nname = \"\"\n", 18, &s_m, NULL, 0), "no error buffer");
    {
        char tiny[8];
        CHECK(!bo3_manifest_parse("x", 1, &s_m, tiny, sizeof tiny) && strlen(tiny) < sizeof tiny, "short error buffer");
    }
}

/* Deterministic mutations of valid manifests: every result must keep the
 * promises in modmanifest.h. Each case is parsed from its own heap block of
 * exactly its length, so a read past the end is a sanitizer report. */
static uint32_t s_rng = 12345;
static uint32_t rnd(void)
{
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}

static void mutations(void)
{
    static const char *const seeds[] = {
        "id = \"example.car-pack\"\nname = \"Example Car Pack\"\nversion = \"1.0.0\"\n",
        "# c\r\nid = \"a-b_c.d\" # x\r\nname = \"\\u00e9t\\u00e9\"\r\nauthor = \"\\\\\\\"\"\r\ndescription = \"text\"\r\n",
        "\xEF\xBB\xBFid=\"z\"\nname=\"\xe6\x97\xa5\"\nfoo = \"bar\"",
    };
    static const unsigned char alphabet[] = "\"\\=#[]'.\n\r\t uU0aZ\x80\xC3\xA9\xEF\xBB\xBF\xF0\x9F\x8F\x81";
    int runs = 0, accepted_n = 0, bad_guarantee = 0;

    for (int s = 0; s < 3; s++) {
        size_t sl = strlen(seeds[s]);
        for (int it = 0; it < 20000; it++) {
            size_t n = sl + 4;
            unsigned char *b = malloc(n);
            if (!b)
                abort();
            memcpy(b, seeds[s], sl);
            n = sl;
            for (int k = 1 + (int)(rnd() % 4); k > 0; k--) {
                uint32_t op = rnd() % 3, pos = n ? rnd() % (uint32_t)n : 0;
                unsigned char c = alphabet[rnd() % (sizeof alphabet - 1)];
                if (op == 0 && n)
                    b[pos] = c;                                       /* replace */
                else if (op == 1 && n) {
                    memmove(b + pos, b + pos + 1, n - pos - 1);       /* delete */
                    n--;
                } else if (n < sl + 4) {
                    memmove(b + pos + 1, b + pos, n - pos);           /* insert */
                    b[pos] = c;
                    n++;
                }
            }
            {
                unsigned char *exact = malloc(n ? n : 1);
                if (!exact)
                    abort();
                memcpy(exact, b, n);
                free(b);
                runs++;
                if (bo3_manifest_parse((const char *)exact, n, &s_m, s_err, sizeof s_err)) {
                    accepted_n++;
                    if (!s_m.id[0] || !s_m.name[0] || !utf8_clean(s_m.name) || !utf8_clean(s_m.author)
                            || !utf8_clean(s_m.description) || strlen(s_m.id) > BO3_MOD_ID_MAX
                            || strspn(s_m.id, "abcdefghijklmnopqrstuvwxyz0123456789._-") != strlen(s_m.id)
                            || strspn(s_m.version, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789.+-")
                               != strlen(s_m.version))
                        bad_guarantee++;
                } else if (s_m.id[0] || s_m.name[0] || s_m.version[0] || s_m.author[0] || s_m.description[0]
                           || !s_err[0]) {
                    bad_guarantee++;
                }
                free(exact);
            }
        }
    }
    CHECK(bad_guarantee == 0, "mutations: %d of %d broke a guarantee", bad_guarantee, runs);
    printf("mutations: %d parsed, %d accepted, guarantees kept\n", runs, accepted_n);
}

int main(void)
{
    accepted();
    rejected();
    id_rule();
    mutations();
    printf("modmanifest: %d passed, %d failed\n", s_pass, s_fail);
    return s_fail ? 1 : 0;
}
