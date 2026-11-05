/*
 * langpack.c - French, German, Italian, Spanish, Dutch or Swedish from the
 * European (PAL) discs, read in place of the North American (NTSC-U) disc's
 * English.
 *
 * The discs carry the same game; the two European ones have the languages
 * the North American one has not: French, German and Italian on one,
 * Spanish, Dutch and Swedish (with English) on the other. Their files are
 * the same formats under other names, and the North American executable only ever
 * asks for the English names: its text (Data\GlobalUs.bin, HeadUs.bin) is
 * chosen by region at build time, its language film (ovid\englis30.xmv) and
 * tutorial films (ovid\TrC_ENG.xmv, TrD_ENG.xmv) by number, and its DJ banks
 * (tracks\...\E_DJ*.xwb) by the prefix E_. So the language is changed here,
 * by name, and the game is left as it is: the dashboard language it reads
 * (XC_LANGUAGE) stays English.
 *
 *   Data\GlobalUs.bin       -> Languages/<disc>/Data/Global<two>.bin (Fr, Ge, It, Sp, Du, Sw)
 *   Data\HeadUs.bin         -> Languages/<disc>/Data/Head<two>.bin
 *   ovid\englis<n>.xmv      -> .../ovid/<film><n>.xmv (french, german, italia, ...)
 *   ovid\Tr{C,D}_ENG.xmv    -> .../ovid/Tr{C,D}_<three>.xmv (FRA, GER, ITA, ...)
 *   tracks\...\E_<name>.xwb -> .../tracks/.../<prefix>_<name>.xwb (F, G, I, S)
 *
 * The files are the player's own, from their European discs
 * (install.c --install-languages: <data>/Languages/FrDeIt and /EsNlSv, every
 * file checked against config/disc/pal-languages.manifest and
 * pal-languages-2.manifest; install.c copies them with the lists and
 * folders this file keeps, bo3_langpack_disc). A name is redirected only
 * when the manifest lists its counterpart, spelled as the manifest has it,
 * so a case-sensitive file system finds it. Chosen with game.language (the
 * Language row, a restart): once installed, bo3_langpack_languages offers
 * them there; the kernel still reports English (XC_LANGUAGE) to the North
 * American program, which reads the chosen language's files here. The PAL
 * programs have their own languages: nothing here acts there.
 *
 * How: xbox_PathRedirectHook (the toolkit's kernel.h), chained to the hook
 * already there, as modfs.c does; bo3_langpack_init runs before
 * bo3_modfs_init, so a mod folder's file comes first.
 *
 * The tutorial films' narration is not in the films (they are video only)
 * but in ovid\movie.xwb, the same file on both discs, which holds it in six
 * languages. The title picks the wave from a table by film number
 * (MEM32(0x383808 + film * 4), read in sub_00014FB0); the number is the
 * English film's, so its entries (TrC_ENG 11, TrD_ENG 19) are given the
 * chosen language's waves (TrC_FRA 12 ... TrD_SPA 24), once, at start.
 * Dutch and Swedish have the English waves there already.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <direct.h>
#define lp_getcwd _getcwd
#else
#include <unistd.h>
#define lp_getcwd getcwd
#endif

#include "recomp_types.h"               /* MEM32 */
#include "kernel.h"                     /* xbox_PathRedirectHook */
#include "platform/xr_log.h"
#include "platform/recomp_env.h"
#include "langpack.h"                   /* disc_install.h: xr_manifest, xr_install_marker_read */
#include "region.h"                     /* BO3_REGION_NAME, BO3_VA */
#include "bo3_release.h"                /* BO3_LANGS_ID, BO3_LANGS2_ID */

/* Each European disc's files (its manifest, the default.xbe in it only to
 * say which disc it is), its folder and what --install-languages says it
 * copies. */
static const xr_manifest_file k_files_fdi[] = {
#include "bo3_langs_manifest.inc"
};
static const xr_manifest_file k_files_esv[] = {
#include "bo3_langs2_manifest.inc"
};
static const struct {
    xr_manifest manifest;
    const char *dir, *what;
} k_discs[BO3_LANGPACK_DISCS] = {
    { { BO3_LANGS_ID, "Burnout 3: Takedown", "Europe (PAL) retail, French, German, Italian",
        BO3_LANGS_TITLE_ID, "default.xbe", k_files_fdi, sizeof k_files_fdi / sizeof k_files_fdi[0] },
      BO3_LANGPACK_DIR "/FrDeIt", "the French, German and Italian files" },
    { { BO3_LANGS2_ID, "Burnout 3: Takedown", "Europe (PAL) retail, English, Spanish, Dutch, Swedish",
        BO3_LANGS2_TITLE_ID, "default.xbe", k_files_esv, sizeof k_files_esv / sizeof k_files_esv[0] },
      BO3_LANGPACK_DIR "/EsNlSv", "the Spanish, Dutch and Swedish files" },
};

const xr_manifest *bo3_langpack_disc(unsigned disc, const char **dir, const char **what)
{
    if (disc >= BO3_LANGPACK_DISCS)
        return NULL;
    if (dir)
        *dir = k_discs[disc].dir;
    if (what)
        *what = k_discs[disc].what;
    return &k_discs[disc].manifest;
}

/* The setting's values after "english", and what each is called on the disc. */
static const struct {
    const char *name;   /* for the log */
    const char *two;    /* Global<two>.bin, Head<two>.bin */
    const char *three;  /* TrC_<three>.xmv */
    const char *film;   /* <film><n>.xmv */
    char        prefix; /* <prefix>_DJ*.xwb */
    unsigned    trc, trd; /* the title's numbers of its TrC_ and TrD_ films */
    int         xc;     /* game.language's choice (languages.h) */
    unsigned    disc;   /* k_discs */
} k_langs[] = {
    { "french",  "Fr", "FRA", "french", 'F', 12, 20, 3, 0 },
    { "german",  "Ge", "GER", "german", 'G', 13, 21, 2, 0 },
    { "italian", "It", "ITA", "italia", 'I', 14, 22, 5, 0 },
    { "spanish", "Sp", "SPA", "spanis", 'S', 16, 24, 4, 1 },
    /* Dutch and Swedish: game.language's own choices (languages.h; no
     * dashboard has them, so the kernel reports English, as for the others
     * here). Their disc has no speech for them: the DJ stays English (no
     * D_ or W_ banks), and the title's table already gives their tutorial
     * films the English narration (waves 5 and 4). */
    { "dutch",   "Du", "DUT", "dutch",  'D', 10, 18, 9, 1 },
    { "swedish", "Sw", "SWE", "swedis", 'W', 17, 25, 10, 1 },
};

/* The films' wave numbers in ovid\movie.xwb, by film number (sub_00014FB0),
 * and the English tutorial films' numbers with the waves they have there. */
#define FILM_WAVES   BO3_VA(0x383808u, 0x383B08u, 0x383BA8u)
#define FILM_TRC_ENG 11u
#define FILM_TRD_ENG 19u
#define WAVE_TRC_ENG 5u
#define WAVE_TRD_ENG 4u

static int s_lang = -1;                 /* index into k_langs; -1: English, nothing redirected */
static char s_root[1024];               /* <data>/Languages/<disc>, absolute */
static const char *(*s_prev)(const char *);
static _Thread_local char t_out[1200];

static int lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

/* a and b the same but for case, with '\\' and '/' alike. */
static int same_path(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        int x = *a == '\\' ? '/' : lower((unsigned char)*a);
        int y = *b == '\\' ? '/' : lower((unsigned char)*b);
        if (x != y)
            return 0;
    }
    return !*a && !*b;
}

/* p with its first n bytes like prefix (case and separators aside). */
static int starts(const char *p, const char *prefix, size_t *n)
{
    size_t i;
    for (i = 0; prefix[i]; i++) {
        int x = p[i] == '\\' ? '/' : lower((unsigned char)p[i]);
        if (!p[i] || x != lower((unsigned char)prefix[i]))
            return 0;
    }
    if (n)
        *n = i;
    return 1;
}

/* The path under the disc's root, or NULL for a path that is not the disc's. */
static const char *disc_rel(const char *p)
{
    size_t n;
    if (starts(p, "D:/", &n) || starts(p, "/Device/CdRom0/", &n))
        return p + n;
    return NULL;
}

/* The disc file `rel` is read from: the language's counterpart as the
 * manifest spells it, or NULL when it has none. */
static const char *counterpart(const char *rel)
{
    char want[512];
    size_t n, len = strlen(rel);
    const char *base;
    unsigned i;

    if (len >= sizeof want)
        return NULL;
    want[0] = 0;
    if (same_path(rel, "Data/GlobalUs.bin"))
        snprintf(want, sizeof want, "Data/Global%s.bin", k_langs[s_lang].two);
    else if (same_path(rel, "Data/HeadUs.bin"))
        snprintf(want, sizeof want, "Data/Head%s.bin", k_langs[s_lang].two);
    else if (starts(rel, "ovid/englis", &n))
        snprintf(want, sizeof want, "ovid/%s%s", k_langs[s_lang].film, rel + n);
    else if (same_path(rel, "ovid/TrC_ENG.xmv") || same_path(rel, "ovid/TrD_ENG.xmv"))
        snprintf(want, sizeof want, "ovid/Tr%c_%s.xmv", rel[7], k_langs[s_lang].three);
    else if (starts(rel, "tracks/", NULL)) {
        base = rel + len;
        while (base > rel && base[-1] != '/' && base[-1] != '\\')
            base--;
        if (lower((unsigned char)base[0]) == 'e' && base[1] == '_') {
            snprintf(want, sizeof want, "%s", rel);
            want[base - rel] = k_langs[s_lang].prefix;
        }
    }
    if (!want[0])
        return NULL;
    {
        const xr_manifest_file *f = k_discs[k_langs[s_lang].disc].manifest.files;
        unsigned nf = k_discs[k_langs[s_lang].disc].manifest.count;
        for (i = 0; i < nf; i++)
            if (same_path(f[i].path, want))
                return f[i].path;
    }
    return NULL;
}

static const char *redirect(const char *path)
{
    const char *rel = path ? disc_rel(path) : NULL;
    const char *file = rel && *rel ? counterpart(rel) : NULL;

    if (file) {
#if defined(_WIN32)
        snprintf(t_out, sizeof t_out, "\\\\?\\%s\\%s", s_root, file);
        for (char *c = t_out + 4; *c; c++)
            if (*c == '/')
                *c = '\\';
#else
        snprintf(t_out, sizeof t_out, "%s/%s", s_root, file);
#endif
        return t_out;
    }
    return s_prev ? s_prev(path) : NULL;
}

/* <data>/Languages/<disc> holds that European disc's languages (its marker
 * names this build's list). On the PAL programs: never (they have theirs). */
static int installed(unsigned disc, xr_install_marker *mark)
{
    return !strcmp(BO3_REGION_NAME, "NTSC-U") && xr_install_marker_read(k_discs[disc].dir, mark)
           && !strcmp(mark->manifest, k_discs[disc].manifest.id);
}

unsigned bo3_langpack_installed(void)
{
    xr_install_marker mark;
    unsigned i, have = 0;

    for (i = 0; i < BO3_LANGPACK_DISCS; i++)
        if (installed(i, &mark))
            have |= 1u << i;
    return have;
}

int bo3_langpack_languages(unsigned *mask, int want)
{
    unsigned i, have = bo3_langpack_installed();

    for (i = 0; i < sizeof k_langs / sizeof k_langs[0]; i++) {
        if (!(have >> k_langs[i].disc & 1))
            continue;
        *mask |= 1u << k_langs[i].xc;
        if (want == (int)k_langs[i].xc)
            s_lang = (int)i;
    }
    if (s_lang < 0)
        return want;
    /* Read from Languages; the program itself goes on in English. */
    recomp_env_set(RENV_GAME_LANGUAGE, NULL);
    return -1;
}

void bo3_langpack_init(void)
{
    xr_install_marker mark;
    char cwd[900];

    if (s_lang < 0 || !installed(k_langs[s_lang].disc, &mark))
        return;
    if (!lp_getcwd(cwd, sizeof cwd)) {
        s_lang = -1;
        return;
    }
    snprintf(s_root, sizeof s_root, "%s/%s", cwd, k_discs[k_langs[s_lang].disc].dir);
    if (xbox_PathRedirectHook != redirect) {
        s_prev = xbox_PathRedirectHook;
        xbox_PathRedirectHook = redirect;
    }
    /* The tutorials' narration: only where the table is the one this was
     * found in (the English films with their English waves). */
    if (MEM32(FILM_WAVES + FILM_TRC_ENG * 4) == WAVE_TRC_ENG
        && MEM32(FILM_WAVES + FILM_TRD_ENG * 4) == WAVE_TRD_ENG) {
        MEM32(FILM_WAVES + FILM_TRC_ENG * 4) = MEM32(FILM_WAVES + k_langs[s_lang].trc * 4);
        MEM32(FILM_WAVES + FILM_TRD_ENG * 4) = MEM32(FILM_WAVES + k_langs[s_lang].trd * 4);
    } else {
        XR_NOTICE("[LANG] the films' wave table is not as expected: the tutorials keep their "
                  "English narration\n");
    }
    XR_NOTICE("[LANG] %s, from %s (manifest %s)\n", k_langs[s_lang].name, s_root, mark.manifest);
}
