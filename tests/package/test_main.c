/* Which program of the package runs (src/game/package.c, BO-341), and the
 * package table this build compiled in (bo3_package.inc): three discs, three
 * programs, each disc's default.xbe naming its own program, and each
 * program's per-user folders where its own package kept them. No game data:
 * the manifests hold names, sizes and hashes. */
#include "package.h"
#include "install.h"                    /* BO3_USER_DIR */

#include <stdio.h>
#include <string.h>

/* The toolkit's types, as far as the table needs them (disc_install.h). */
typedef struct {
    const char *path;
    unsigned long long size;
    const char *sha256;
} xr_manifest_file;

typedef struct {
    const char *id, *suffix, *user_dir, *name, *manifest_id;
    const xr_manifest_file *files;
    unsigned count;
} bo3_package_build;

#include "bo3_release.h"

static const xr_manifest_file k_files[] = {
#include "bo3_manifest.inc"
};

#include "bo3_package.inc"

static int bad;

#define CHECK(cond, ...) do { if (!(cond)) { bad++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
                                              printf(__VA_ARGS__); printf("\n"); } } while (0)

enum { NTSC, PAL, ENSV };

static int pick(int self, int recorded, int n, int p, int e)
{
    int whole[3] = { n, p, e };
    return bo3_package_pick(3, self, recorded, whole);
}

static void test_pick(void)
{
    /* nothing installed: the program started shows its own setup window */
    CHECK(pick(NTSC, -1, 0, 0, 0) == NTSC, "fresh start");
    CHECK(pick(PAL, -1, 0, 0, 0) == PAL, "fresh start of the PAL program");
    /* one disc installed: its program, from any program */
    CHECK(pick(NTSC, -1, 1, 0, 0) == NTSC, "NTSC-U user (0.2.x and earlier)");
    CHECK(pick(NTSC, -1, 0, 1, 0) == PAL, "Fr/De/It user");
    CHECK(pick(NTSC, -1, 0, 0, 1) == ENSV, "En/Es/Nl/Sv user");
    CHECK(pick(PAL, -1, 1, 0, 0) == NTSC, "the PAL program with the NTSC-U data");
    CHECK(pick(ENSV, -1, 0, 1, 0) == PAL, "the En/Es/Nl/Sv program with the Fr/De/It data");
    /* both packages used before (no record): the program started keeps its own */
    CHECK(pick(NTSC, -1, 1, 1, 0) == NTSC, "NTSC-U and PAL installed, no record");
    CHECK(pick(PAL, -1, 1, 1, 0) == PAL, "the PAL program, both installed, no record");
    CHECK(pick(NTSC, -1, 0, 1, 1) == PAL, "both PAL data (one folder: cannot be, but first wins)");
    /* the disc installed last wins */
    CHECK(pick(NTSC, PAL, 1, 1, 0) == PAL, "switched to the PAL disc");
    CHECK(pick(NTSC, NTSC, 1, 1, 0) == NTSC, "switched back to the NTSC-U disc");
    CHECK(pick(PAL, NTSC, 1, 1, 0) == NTSC, "the PAL program, NTSC-U recorded");
    CHECK(pick(NTSC, ENSV, 1, 0, 1) == ENSV, "switched to the En/Es/Nl/Sv disc");
    /* a record whose data is gone: whatever data is whole */
    CHECK(pick(NTSC, PAL, 1, 0, 0) == NTSC, "recorded PAL removed, NTSC-U still there");
    CHECK(pick(NTSC, NTSC, 0, 0, 1) == ENSV, "recorded NTSC-U removed, En/Es/Nl/Sv there");
    /* no data at all: the recorded disc's program asks for its disc */
    CHECK(pick(NTSC, ENSV, 0, 0, 0) == ENSV, "recorded, nothing whole");
    /* a record that names nothing of this package */
    CHECK(pick(NTSC, 7, 0, 1, 0) == PAL, "out-of-range record");
    CHECK(pick(NTSC, -1, 0, 0, 0) == NTSC, "no record");
    /* a package of one program */
    {
        int w = 0;
        CHECK(bo3_package_pick(1, 0, -1, &w) == 0, "one program");
    }
}

static void test_record(void)
{
    char id[64], text[600];

    CHECK(bo3_package_record_parse("pal-retail\n", id, sizeof id) && !strcmp(id, "pal-retail"), "plain");
    CHECK(bo3_package_record_parse("\xEF\xBB\xBF  pal-ensv-retail \r\n# a note\r\n", id, sizeof id)
          && !strcmp(id, "pal-ensv-retail"), "BOM, blanks, CRLF");
    CHECK(bo3_package_record_parse("# first a comment\n\n\tntsc-u-retail", id, sizeof id)
          && !strcmp(id, "ntsc-u-retail"), "comment and empty line first");
    CHECK(!bo3_package_record_parse("", id, sizeof id), "empty");
    CHECK(!bo3_package_record_parse("# only\n#comments\n", id, sizeof id), "comments only");
    CHECK(!bo3_package_record_parse("pal-retail", id, 4), "too long for the buffer");
    CHECK(bo3_package_record_format("pal-retail", text, sizeof text), "format");
    CHECK(bo3_package_record_parse(text, id, sizeof id) && !strcmp(id, "pal-retail"), "format, then parse");
}

static void program(const char *self, const char *s, const char *o, const char *want)
{
    char out[1100];
    int ok = bo3_package_program(self, s, o, out, sizeof out);

    if (!want)
        CHECK(!ok, "%s (%s -> %s): no program, got %s", self, s, o, out);
    else
        CHECK(ok && !strcmp(out, want), "%s (%s -> %s): want %s, got %s", self, s, o, want, ok ? out : "none");
}

static void test_program(void)
{
    program("/Applications/Takedawn.app/Contents/MacOS/Takedawn", "", "-pal",
            "/Applications/Takedawn.app/Contents/MacOS/Takedawn-pal");
    program("/Applications/Takedawn.app/Contents/MacOS/Takedawn-pal", "-pal", "",
            "/Applications/Takedawn.app/Contents/MacOS/Takedawn");
    program("/Applications/Takedawn.app/Contents/MacOS/Takedawn-pal", "-pal", "-ensv",
            "/Applications/Takedawn.app/Contents/MacOS/Takedawn-ensv");
    program("/tmp/.mount_TakedaXYZ/usr/bin/takedawn", "", "-ensv", "/tmp/.mount_TakedaXYZ/usr/bin/takedawn-ensv");
    program("/tmp/.mount_TakedaXYZ/usr/bin/takedawn-ensv", "-ensv", "", "/tmp/.mount_TakedaXYZ/usr/bin/takedawn");
    program("C:\\Users\\a\\AppData\\Local\\Programs\\Takedawn\\Takedawn.exe", "", "-pal",
            "C:\\Users\\a\\AppData\\Local\\Programs\\Takedawn\\Takedawn-pal.exe");
    program("C:\\Programs\\Takedawn\\TAKEDAWN-PAL.EXE", "-pal", "", "C:\\Programs\\Takedawn\\TAKEDAWN.EXE");
    program("C:\\Programs\\Takedawn\\Takedawn-ensv.exe", "-ensv", "-pal", "C:\\Programs\\Takedawn\\Takedawn-pal.exe");
    /* a developer's PAL build named burnout3: no package around it */
    program("/src/bin/burnout3", "-pal", "", NULL);
    program("/src/bin/-pal", "-pal", "", NULL);
    program("", "", "-pal", NULL);
}

/* The executables of the three retail discs (supported.json, docs/pal.md). */
static const struct { const char *id, *xbe_sha256, *suffix, *user_dir; } k_want[] = {
    { "ntsc-u-retail", "9f497acd82adb8edfb72cc31aeeb35010b45d32b13a3f1fc6540717ff24d13f9", "", "Takedawn" },
    { "pal-retail", "6f7f6b46352749f760d1c41e1c4cae1dbc9b5c5356887976c6db27327136afa2", "-pal", "Takedawn PAL" },
    { "pal-ensv-retail", "71c6923b28f8882e95bf04a37d47e838b642c65101f34ee95c32fb75b6d68911", "-ensv",
      "Takedawn PAL" },
};

static void test_table(void)
{
    int i, j;

    CHECK(BO3_PACKAGE_COUNT == 3, "the package has %d programs", BO3_PACKAGE_COUNT);
    if (BO3_PACKAGE_COUNT != 3)
        return;
    CHECK(!strcmp(k_package[BO3_PACKAGE_SELF].id, BO3_BUILD_ID), "self is %s, the build %s",
          k_package[BO3_PACKAGE_SELF].id, BO3_BUILD_ID);
    CHECK(!strcmp(k_package[BO3_PACKAGE_SELF].manifest_id, BO3_MANIFEST_ID), "self's manifest");
    CHECK(!strcmp(k_package[BO3_PACKAGE_SELF].user_dir, BO3_USER_DIR),
          "this program's folders are %s, supported.json says %s", BO3_USER_DIR, k_package[BO3_PACKAGE_SELF].user_dir);
    CHECK(!strcmp(k_package[0].suffix, ""), "the first build is the main program");
    for (i = 0; i < 3; i++) {
        const char *xbe = NULL;
        CHECK(!strcmp(k_package[i].id, k_want[i].id), "build %d is %s", i, k_package[i].id);
        CHECK(!strcmp(k_package[i].suffix, k_want[i].suffix), "%s: suffix '%s'", k_package[i].id, k_package[i].suffix);
        CHECK(!strcmp(k_package[i].user_dir, k_want[i].user_dir), "%s: folders '%s'", k_package[i].id,
              k_package[i].user_dir);
        CHECK(k_package[i].count > 700, "%s: %u files", k_package[i].id, k_package[i].count);
        for (j = 0; j < (int)k_package[i].count; j++)
            if (!strcmp(k_package[i].files[j].path, "default.xbe"))
                xbe = k_package[i].files[j].sha256;
        /* the disc -> program choice: this disc's executable is this build's */
        CHECK(xbe && !strcmp(xbe, k_want[i].xbe_sha256), "%s: default.xbe %s", k_package[i].id, xbe ? xbe : "missing");
        for (j = 0; j < i; j++) {
            CHECK(strcmp(k_package[i].manifest_id, k_package[j].manifest_id), "%s and %s share a manifest",
                  k_package[i].id, k_package[j].id);
            CHECK(strcmp(k_package[i].suffix, k_package[j].suffix), "%s and %s share a program name",
                  k_package[i].id, k_package[j].id);
        }
    }
    /* NTSC-U never shares folders with a PAL disc; the two PAL ones do (as their package did) */
    CHECK(strcmp(k_package[0].user_dir, k_package[1].user_dir), "NTSC-U and PAL folders");
    CHECK(!strcmp(k_package[1].user_dir, k_package[2].user_dir), "the PAL discs' folders");
}

int main(void)
{
    test_pick();
    test_record();
    test_program();
    test_table();
    if (bad) {
        printf("%d failed\n", bad);
        return 1;
    }
    printf("package: choice, record, program names and the table of %s (%d builds) OK\n", BO3_BUILD_ID,
           BO3_PACKAGE_COUNT);
    return 0;
}
