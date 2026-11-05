/* The Language row's list for each supported disc (src/game/languages.c),
 * from the disc manifests in config/disc/ (file names, sizes and hashes;
 * no game data): only the languages the disc has, Dutch and Swedish only
 * where the program can set them. */
#include "languages.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define MAX_FILES 1024

typedef struct {
    char *path[MAX_FILES];
    int n;
} disc;

static int load(disc *d, const char *file)
{
    char line[1024];
    FILE *f = fopen(file, "r");

    d->n = 0;
    if (!f) {
        fprintf(stderr, "cannot open %s\n", file);
        return 0;
    }
    while (fgets(line, sizeof line, f) && d->n < MAX_FILES) {
        char *p;
        if (line[0] == '#')
            continue;
        line[strcspn(line, "\r\n")] = 0;
        p = strchr(line, ' ');          /* "<sha256> <size> <path>" */
        if (p)
            p = strchr(p + 1, ' ');
        if (p)
            d->path[d->n++] = strdup(p + 1);
    }
    fclose(f);
    return d->n > 0;
}

static int has(const char *path, void *ctx)
{
    disc *d = (disc *)ctx;
    int i;

    for (i = 0; i < d->n; i++)
        if (!strcasecmp(d->path[i], path))
            return 1;
    return 0;
}

static int bad;

static void names(unsigned mask, char *out, size_t n)
{
    int i;

    out[0] = 0;
    for (i = 0; i < BO3_LANG_COUNT; i++)
        if (mask & (1u << i)) {
            strncat(out, out[0] ? " " : "", n - strlen(out) - 1);
            strncat(out, bo3_language_name(i), n - strlen(out) - 1);
        }
}

static void expect(const char *what, unsigned got, const char *want)
{
    char text[256];

    names(got, text, sizeof text);
    if (strcmp(text, want)) {
        fprintf(stderr, "FAIL %s: [%s], want [%s]\n", what, text, want);
        bad = 1;
    } else {
        printf("ok   %s: %s\n", what, text);
    }
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "config/disc";
    char file[1024];
    disc d;

    snprintf(file, sizeof file, "%s/ntsc-u-retail.manifest", dir);
    if (!load(&d, file))
        return 1;
    /* English alone: no row */
    expect("NTSC-U", bo3_language_mask(has, &d, 0), "english");

    snprintf(file, sizeof file, "%s/pal-retail.manifest", dir);
    if (!load(&d, file))
        return 1;
    /* this disc has no English text */
    expect("PAL Fr/De/It", bo3_language_mask(has, &d, 0), "german french italian");

    snprintf(file, sizeof file, "%s/pal-ensv-retail.manifest", dir);
    if (!load(&d, file))
        return 1;
    expect("PAL En/Es/Nl/Sv", bo3_language_mask(has, &d, 1), "english spanish dutch swedish");
    /* a program that cannot set the title's own languages offers the others */
    expect("PAL En/Es/Nl/Sv, dashboard languages only", bo3_language_mask(has, &d, 0),
           "english spanish");

    if (bo3_language_index("dutch") != BO3_LANG_DUTCH || bo3_language_index("5") != BO3_LANG_SPANISH
        || bo3_language_index("10") != -1 || bo3_language_index("") != -1
        || bo3_language_index(NULL) != -1 || bo3_language_index("klingon") != -1) {
        fprintf(stderr, "FAIL bo3_language_index\n");
        bad = 1;
    }
    if (!bad)
        puts("PASS: each disc offers its own languages.");
    return bad;
}
