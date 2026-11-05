/* A texture pack's pack.toml and the folders handed to the toolkit
 * (src/game/modtex.c). Folders made in the temporary directory; no game
 * data. nv2a_texpack_set is a stub. */
#include "kernel.h"
#include "modfs.h"
#include "modtex.h"
#include "nv2a_texpack.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

const char *(*xbox_PathRedirectHook)(const char *xbox_path);
extern int g_xr_log_diag;

static int s_fail, s_pass;
#define CHECK(c, ...) do { if (c) s_pass++; else { s_fail++; fprintf(stderr, "FAIL line %d: ", __LINE__); \
    fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

static struct nv2a_texpack_src s_got[NV2A_TEXPACK_MAX_PACKS];
static char s_dir[NV2A_TEXPACK_MAX_PACKS][600], s_name[NV2A_TEXPACK_MAX_PACKS][100];
static unsigned s_ngot;
static int s_calls;

int nv2a_texpack_set(const struct nv2a_texpack_src *p, unsigned n)
{
    unsigned i;
    s_calls++;
    s_ngot = n;
    for (i = 0; i < n; i++) {
        snprintf(s_dir[i], sizeof s_dir[i], "%s", p[i].dir);
        snprintf(s_name[i], sizeof s_name[i], "%s", p[i].name);
        s_got[i] = p[i];
        s_got[i].dir = s_dir[i];
        s_got[i].name = s_name[i];
    }
    return 0;
}

static char s_root[512];

static void put(const char *rel, const char *text)
{
    char p[700], *s;
    FILE *f;
    snprintf(p, sizeof p, "%s/%s", s_root, rel);
    for (s = p + strlen(s_root) + 1; (s = strchr(s, '/')) != NULL; s++) {
        *s = 0;
        mkdir(p, 0755);
        *s = '/';
    }
    f = fopen(p, "wb");
    if (f) {
        fputs(text, f);
        fclose(f);
    }
}

static void parse(void)
{
    int a, c;
    char why[200];

    CHECK(bo3_modtex_parse("", &a, &c, why, sizeof why) && a == 0 && c == 0, "empty: defaults");
    CHECK(bo3_modtex_parse("alpha = \"original\"\n", &a, &c, why, sizeof why) && a == 1 && c == 0, "alpha original");
    CHECK(bo3_modtex_parse("alpha = \"png\"\ncompress = \"off\"\n", &a, &c, why, sizeof why) && a == 2 && c == 1, "png, off");
    CHECK(bo3_modtex_parse("alpha = \"auto\"\ncompress = \"auto\"\n", &a, &c, why, sizeof why) && a == 0 && c == 0, "auto");
    CHECK(bo3_modtex_parse("name = \"x\"\n[later]\nkey = 1\n", &a, &c, why, sizeof why) && a == 0, "unknown keys are left alone");
    CHECK(!bo3_modtex_parse("alpha = \"fill\"\n", &a, &c, why, sizeof why) && a == 0 && strstr(why, "fill"),
          "a bad value says so and keeps auto: %s", why);
    CHECK(!bo3_modtex_parse("alpha = 3\n", &a, &c, why, sizeof why) && a == 0 && strstr(why, "not a string"), "alpha a number: %s", why);
    CHECK(!bo3_modtex_parse("compress = \"bc7\"\n", &a, &c, why, sizeof why) && c == 0, "bad compress: %s", why);
    CHECK(!bo3_modtex_parse("alpha = \n", &a, &c, why, sizeof why) && a == 0 && why[0], "not TOML: %s", why);
    CHECK(!bo3_modtex_parse("a = { b = 1 }\n", &a, &c, why, sizeof why), "an inline table is refused as in mod.toml");
}

static void init(void)
{
    static const char ok[] = "id = \"%s\"\nname = \"N\"\n";
    char t[100], list[1200], off[700];

    snprintf(t, sizeof t, ok, "t.one");
    put("f1/m1/mod.toml", t);
    put("f1/m1/textures/0000000000000001.png", "x");
    put("f1/m1/textures/pack.toml", "alpha = \"original\"\ncompress = \"off\"\n");
    snprintf(t, sizeof t, ok, "t.two");
    put("f1/m2/mod.toml", t);
    put("f1/m2/textures/0000000000000002.png", "x");
    put("f1/m2/textures/pack.toml", "alpha = \"bogus\"\n");
    snprintf(t, sizeof t, ok, "t.three");
    put("f1/m3/mod.toml", t);
    put("f1/m3/textures/0000000000000003.png", "x");
    snprintf(off, sizeof off, "%s/mod-list.toml", s_root);
    put("mod-list.toml", "disabled = [\"t.three\"]\n");
    snprintf(list, sizeof list, "%s/f1", s_root);
    setenv("TAKEDAWN_MODS", list, 1);
    bo3_modfs_state_file(off);
    bo3_modfs_init();
    s_calls = 0;
    bo3_modtex_init();
    CHECK(s_calls == 1 && s_ngot == 2, "two packs handed over (%u in %d calls)", s_ngot, s_calls);
    CHECK(s_ngot == 2 && !strcmp(s_name[0], "t.one") && s_got[0].alpha == NV2A_TEXPACK_ALPHA_ORIGINAL
          && s_got[0].compress == NV2A_TEXPACK_COMPRESS_OFF && strstr(s_dir[0], "m1/textures"), "t.one: its options (%s)", s_dir[0]);
    CHECK(s_ngot == 2 && !strcmp(s_name[1], "t.two") && s_got[1].alpha == NV2A_TEXPACK_ALPHA_AUTO
          && s_got[1].compress == NV2A_TEXPACK_COMPRESS_AUTO, "t.two: a bad pack.toml leaves the defaults");

    /* BO-311: packs turned on and off while the game runs, by id. */
    {
        char made[600];
        snprintf(made, sizeof made, "%s/made", s_root);
        s_calls = 0;
        CHECK(bo3_modtex_live("t.one", NULL, 0) >= 0 && s_calls == 1 && s_ngot == 1 && !strcmp(s_name[0], "t.two"),
              "t.one off live: t.two alone (%u)", s_ngot);
        CHECK(bo3_modtex_live("x.made", made, 1) >= 0 && s_ngot == 2 && !strcmp(s_name[0], "t.two")
              && !strcmp(s_name[1], "x.made") && strstr(s_dir[1], "made/textures"),
              "a pack made after the start: after the rest, t.one still off (%u)", s_ngot);
        CHECK(bo3_modtex_live("t.one", NULL, 1) >= 0 && s_ngot == 3 && !strcmp(s_name[0], "t.two")
              && !strcmp(s_name[1], "t.one") && !strcmp(s_name[2], "x.made"),
              "t.one on again: in the order first changed (%u)", s_ngot);
        CHECK(bo3_modtex_live("x.made", made, 0) >= 0 && s_ngot == 2 && !strcmp(s_name[1], "t.one"),
              "the made pack off: gone (%u)", s_ngot);
        CHECK(bo3_modtex_live("y.none", NULL, 1) == -1, "an id with no folder: nothing to do");
    }
    unsetenv("TAKEDAWN_MODS");
    bo3_modfs_init();
    s_calls = 0;
    bo3_modtex_init();
    CHECK(s_calls == 0, "no mod folder: the toolkit is not asked");
}

int main(void)
{
    char tmpl[] = "/tmp/bo3modtexXXXXXX";
    if (!mkdtemp(tmpl)) {
        fprintf(stderr, "no temporary folder\n");
        return 1;
    }
    snprintf(s_root, sizeof s_root, "%s", tmpl);
    parse();
    init();
    printf("modtex: %d passed, %d failed\n", s_pass, s_fail);
    /* the folder under /tmp stays (a few small files): the system cleans it */
    return s_fail ? 1 : 0;
}
