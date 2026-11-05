/*
 * install.c - the game's data from the player's disc, where the game finds
 * it, and the program's version. See install.h.
 */
#include "install.h"
#include "fatal.h"
#include "upscale.h"

#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>

#if defined(_WIN32)
#include <windows.h>
#include <direct.h>
#define bo3_chdir _chdir
#else
#include <unistd.h>
#define bo3_chdir chdir
#if defined(__linux__) && !defined(__ANDROID__)
#include <poll.h>
#include <sys/wait.h>
#endif
#endif

#include "disc_install.h"
#include "package.h"
#include "platform/recomp_save_dir.h"
#include "platform/recomp_exe_dir.h"
#if !defined(__ANDROID__)
#include "d3d/nv2a_pcache.h"
#endif

#include "bo3_release.h"        /* BO3_VERSION ..., made by cmake/release_info.cmake */
#ifdef BO3_HAVE_BUILD_ID
#include "bo3_build_id.h"       /* BO3_BUILD_COMMIT, BO3_TOOLKIT_COMMIT: made at build time */
#include "bo3_toolkit_id.h"
#else
#define BO3_BUILD_COMMIT "unknown"
#define BO3_TOOLKIT_COMMIT "unknown"
#endif

#define GAME_DIR  "Burnout 3 Takedown"
#define STEP      "disc"

/* ── the disc this program was made for ────────────────────────────── */

static const xr_manifest_file k_files[] = {
#include "bo3_manifest.inc"
};

/* What each build's disc is called (the manifest's "# build" line). */
#define BUILD_NAME_NTSC_U   "North America (NTSC-U) retail"
#define BUILD_NAME_PAL      "Europe (PAL, Fr/De/It) retail"
#define BUILD_NAME_PAL_ENSV "Europe/Australia (PAL, En/Es/Nl/Sv) retail"

static const xr_manifest k_manifest = {
    BO3_MANIFEST_ID,
    "Burnout 3: Takedown",
#if defined(BO3_REGION_PAL_ENSV) && BO3_REGION_PAL_ENSV
    BUILD_NAME_PAL_ENSV,
#elif defined(BO3_REGION_PAL) && BO3_REGION_PAL
    BUILD_NAME_PAL,
#else
    BUILD_NAME_NTSC_U,
#endif
    BO3_MANIFEST_TITLE_ID,
    "default.xbe",
    k_files,
    sizeof k_files / sizeof k_files[0]
};

static int same_nocase(const char *a, const char *b)
{
    for (; *a && *b; a++, b++)
        if ((*a >= 'A' && *a <= 'Z' ? *a + 32 : *a) != (*b >= 'A' && *b <= 'Z' ? *b + 32 : *b))
            return 0;
    return !*a && !*b;
}

const char *bo3_manifest_path(const char *path)
{
    unsigned i;

    for (i = 0; i < k_manifest.count; i++)
        if (same_nocase(k_files[i].path, path))
            return k_files[i].path;
    return NULL;
}

/* ── the package's other programs (package.h) ───────────────────────── */

static int file_exists(const char *p);

/* Every package carries a program for each supported disc (package.h): this
 * one and, beside it, the others, each known here by its manifest
 * (cmake/release_info.cmake, the builds of supported.json's package, in its
 * order). When the disc given to --install, the data installed, or (at a
 * start) the disc installed last is another build's, this program becomes
 * that one, with the same arguments: the setup window, a shortcut and a Steam
 * entry name the main program, whichever disc the player has. */
typedef struct {
    const char *id;                     /* supported.json: "pal-retail" */
    const char *suffix;                 /* after the main program's name: "-pal" */
    const char *user_dir;               /* its per-user folders' name (BO3_USER_DIR) */
    const char *name;                   /* the disc: "Europe (PAL, Fr/De/It) retail" */
    const char *manifest_id;
    const xr_manifest_file *files;
    unsigned count;
} bo3_package_build;

#include "bo3_package.inc"

/* Build i's disc as the installer reads it (k_manifest for this one). */
static xr_manifest pkg_manifest(int i)
{
    xr_manifest m = k_manifest;

    if (i != BO3_PACKAGE_SELF) {
        m.id = k_package[i].manifest_id;
        m.build = k_package[i].name;
        m.files = k_package[i].files;
        m.count = k_package[i].count;
    }
    return m;
}

/* The package's build whose manifest has this id; -1 for none. */
static int pkg_by_manifest(const char *id)
{
    int i;

    for (i = 0; id && i < BO3_PACKAGE_COUNT; i++)
        if (!strcmp(k_package[i].manifest_id, id))
            return i;
    return -1;
}

static int pkg_by_id(const char *id)
{
    int i;

    for (i = 0; id && i < BO3_PACKAGE_COUNT; i++)
        if (!strcmp(k_package[i].id, id))
            return i;
    return -1;
}

/* The room a whole install of build i wants: the disc and room to spare
 * for the saves, the shader cache and the title's own cache files (the PAL
 * discs are 2.97 and 2.84 GB: not 34 MB of room under 3 GB). */
static uint64_t pkg_free_needed(int i)
{
    xr_manifest m = pkg_manifest(i);
    return xr_manifest_bytes(&m) > 2600ull * 1000 * 1000 ? 3400ull * 1000 * 1000
                                                          : 3000ull * 1000 * 1000;
}
/* this program's disc's: 3 GB for the 2.48 GB North America one */
#define FREE_NEEDED pkg_free_needed(BO3_PACKAGE_SELF)

static int s_argc;
static char **s_argv;
static int s_sibling_hop;               /* this process was started by another program of the package */

void bo3_install_args(int argc, char **argv)
{
    const char *hop = getenv("TAKEDAWN_SIBLING_HOP");

    s_argc = argc;
    s_argv = argv;
    /* Only for the one step from the other program: a setup window this
     * process opens, and what that starts, may need to go across again. */
    s_sibling_hop = hop && *hop;
#if defined(_WIN32)
    _putenv("TAKEDAWN_SIBLING_HOP=");
#else
    unsetenv("TAKEDAWN_SIBLING_HOP");
#endif
}

/* Build i's program: beside this one, this one's name with its suffix in
 * place of this one's (package.h). 0 when there is none: a build of one
 * program alone, as a developer makes, or a package without it. */
static int pkg_program(int i, char *out, unsigned n)
{
    if (i < 0 || i >= BO3_PACKAGE_COUNT || i == BO3_PACKAGE_SELF)
        return 0;
    if (!bo3_package_program(recomp_exe_path(), k_package[BO3_PACKAGE_SELF].suffix,
                             k_package[i].suffix, out, n))
        return 0;
    return file_exists(out);
}

#if defined(_WIN32)
/* One argument of a command line as CommandLineToArgvW reads it back. */
static void win_quote(wchar_t *cmd, size_t cap, size_t *at, const char *arg)
{
    wchar_t w[2048];
    int i, bs = 0, n = MultiByteToWideChar(CP_UTF8, 0, arg, -1, w, 2048);

    if (n <= 0)
        n = MultiByteToWideChar(CP_ACP, 0, arg, -1, w, 2048);
    if (n <= 0)
        w[0] = 0;
#define PUT(c) do { if (*at + 1 < cap) cmd[(*at)++] = (c); } while (0)
    if (*at)
        PUT(L' ');
    PUT(L'"');
    for (i = 0; w[i]; i++) {
        if (w[i] == L'\\') {
            bs++;
            continue;
        }
        while (bs) {
            PUT(L'\\');
            if (w[i] == L'"')
                PUT(L'\\');
            bs--;
        }
        if (w[i] == L'"')
            PUT(L'\\');
        PUT(w[i]);
    }
    while (bs--) {                      /* before the closing quote: doubled */
        PUT(L'\\');
        PUT(L'\\');
    }
    PUT(L'"');
    cmd[*at] = 0;
#undef PUT
}
#endif

/* Become build i's program, with this one's arguments. Returns only when
 * that is not possible (no such program beside this one, or this process was
 * started by another program of the package: one step, never back and
 * forth); `why` is for the log. */
static void pkg_exec(int i, const char *why)
{
    char prog[1100];
    int k;

    if (!s_argv || s_sibling_hop || !pkg_program(i, prog, sizeof prog))
        return;
    fprintf(stderr, "[DATA] %s: the program for that disc, %s, takes over\n", why, prog);
    fflush(stdout);
    fflush(stderr);
#if defined(_WIN32)
    {
        static wchar_t cmd[32768];
        wchar_t wprog[1100];
        size_t at = 0;
        STARTUPINFOW si;
        PROCESS_INFORMATION pi;
        DWORD code = 1;
        HANDLE h[3] = { GetStdHandle(STD_INPUT_HANDLE), GetStdHandle(STD_OUTPUT_HANDLE),
                        GetStdHandle(STD_ERROR_HANDLE) };

        if (MultiByteToWideChar(CP_UTF8, 0, prog, -1, wprog, 1100) <= 0)
            return;
        win_quote(cmd, sizeof cmd / sizeof cmd[0], &at, prog);
        for (k = 1; k < s_argc; k++)
            win_quote(cmd, sizeof cmd / sizeof cmd[0], &at, s_argv[k]);
        memset(&si, 0, sizeof si);
        si.cb = sizeof si;
        for (k = 0; k < 3; k++)
            if (h[k] && h[k] != INVALID_HANDLE_VALUE)
                SetHandleInformation(h[k], HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = h[0];
        si.hStdOutput = h[1];
        si.hStdError = h[2];
        SetEnvironmentVariableW(L"TAKEDAWN_SIBLING_HOP", L"1");
        if (!CreateProcessW(wprog, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
            SetEnvironmentVariableW(L"TAKEDAWN_SIBLING_HOP", NULL);
            return;
        }
        /* The setup window waits for this process and reads its pipe: stay
         * until the other one is done and give its status. */
        WaitForSingleObject(pi.hProcess, INFINITE);
        GetExitCodeProcess(pi.hProcess, &code);
        ExitProcess(code);
    }
#else
    {
        char **args = (char **)calloc((size_t)s_argc + 1, sizeof *args);
        if (!args)
            return;
        args[0] = prog;
        for (k = 1; k < s_argc; k++)
            args[k] = s_argv[k];
        setenv("TAKEDAWN_SIBLING_HOP", "1", 1);
        execv(prog, args);
        unsetenv("TAKEDAWN_SIBLING_HOP");
        fprintf(stderr, "[DATA] %s: %s\n", prog, strerror(errno));
        free(args);
    }
#endif
}

/* The package's build whose disc `base`'s source is (an image, a folder,
 * a descriptor or an Android document tree), by its executable alone; this
 * one's first. -1 when it is none of them (another game, region or
 * revision, or not readable: the install itself then says which). */
static int source_build_of(const xr_install_opts *base)
{
    xr_install_opts o = *base;
    xr_install_result r;
    xr_manifest m;
    int i;

    o.manifest = &k_manifest;
    o.identify_only = 1;
    o.free_needed = 0;
    o.progress = NULL;
    o.log = NULL;
    if (xr_install(&o, &r) != XR_INSTALL_WRONG_VERSION)
        return r.code == XR_INSTALL_OK ? BO3_PACKAGE_SELF : -1;
    for (i = 0; i < BO3_PACKAGE_COUNT; i++) {
        if (i == BO3_PACKAGE_SELF)
            continue;
        m = pkg_manifest(i);
        o.manifest = &m;
        if (xr_install(&o, &r) == XR_INSTALL_OK)
            return i;
    }
    return -1;
}

/* --install: the same for a path. */
static int source_build(const char *source)
{
    xr_install_opts o;

    if (!source)
        return -1;
    memset(&o, 0, sizeof o);
    o.source = source;
    o.source_fd = -1;
    o.dest = ".";
    o.subdir = GAME_DIR;
    o.version = BO3_VERSION;
    return source_build_of(&o);
}

/* ── the version ───────────────────────────────────────────────────── */

const char *bo3_version_line(void)
{
    return BO3_CODENAME[0] ? BO3_APP_NAME " " BO3_VERSION " \"" BO3_CODENAME "\""
                           : BO3_APP_NAME " " BO3_VERSION;
}

const char *bo3_version_number(void)
{
    return BO3_VERSION;
}

const char *bo3_version_long(void)
{
    static char text[200];
    char game[24], toolkit[24];

    /* The commits without their branch names. */
    snprintf(game, sizeof game, "%.*s", (int)strcspn(BO3_BUILD_COMMIT, " "), BO3_BUILD_COMMIT);
    snprintf(toolkit, sizeof toolkit, "%.*s", (int)strcspn(BO3_TOOLKIT_COMMIT, " "),
             BO3_TOOLKIT_COMMIT);
    snprintf(text, sizeof text, "%s  (game %s, toolkit %s)", bo3_version_line(), game, toolkit);
    return text;
}

int bo3_version_main(void)
{
    XR_OUT("%s\n"
           "version %s\n"
           "codename %s\n"
           "game %s\n"
           "toolkit %s\n"
           "regen %s\n"
           "manifest %s (%s, %u files, %llu bytes)\n"
           "gen %s patches %s\n",
           bo3_version_line(), BO3_VERSION, BO3_CODENAME, BO3_BUILD_COMMIT, BO3_TOOLKIT_COMMIT,
           BO3_REGEN_KEY, BO3_MANIFEST_ID, BO3_MANIFEST_BUILD, k_manifest.count,
           (unsigned long long)xr_manifest_bytes(&k_manifest), BO3_GEN_TREE, BO3_GEN_PATCHES);
    return 0;
}

/* ── the per-user folders ──────────────────────────────────────────── */

static char s_dirs_notes[2600];

/* What was moved, now and again for the run log, which starts later. */
static void dirs_note(const char *fmt, ...)
{
    size_t n = strlen(s_dirs_notes);
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(s_dirs_notes + n, sizeof s_dirs_notes - n, fmt, ap);
    va_end(ap);
    (fputs)(s_dirs_notes + n, stderr);      /* a notice: always said (xr_log.h) */
}

const char *bo3_user_dirs_notes(void) { return s_dirs_notes; }

/* The folders of the port's earlier name and where each goes: saves and
 * settings; on Windows the game data's folder and the toolkit's shader
 * cache (%LOCALAPPDATA%\xboxrecomp\cache: the cache alone, the toolkit's
 * folder may hold another port's); elsewhere the cache named after the old
 * executable. 0 past the last one. */
#if !defined(__ANDROID__)
static int dir_move(int i, char *from, unsigned from_n, char *to, unsigned to_n,
                    const char **what, int *kind)
{
    static const struct { int kind; const char *old_name, *what; } k[] = {
        { RECOMP_DIR_DATA, "Burnout3", "save and settings" },
#if defined(_WIN32)
        { RECOMP_DIR_LOCAL, "Burnout3", "game data" },
        { RECOMP_DIR_CACHE, "xboxrecomp", "shader cache" },
#else
        { RECOMP_DIR_CACHE, "burnout3", "shader cache" },
#endif
    };
#if defined(BO3_REGION_PAL) && BO3_REGION_PAL
    (void)k; (void)from; (void)from_n; (void)to; (void)to_n; (void)what; (void)kind;
    return 0;                           /* the old name's folders are NTSC-U's */
#endif
    if (i < 0 || i >= (int)(sizeof k / sizeof k[0]))
        return 0;
    {
        /* Portable mode (portable.txt or a data folder beside Takedawn.exe)
         * has one folder per kind and no earlier name to move from. */
        char root[1024];
        if (recomp_portable_root(root, sizeof root))
            return 0;
    }
    *what = k[i].what;
    *kind = k[i].kind;
    return recomp_user_dir(k[i].kind, k[i].old_name, from, from_n)
        && recomp_user_dir(k[i].kind, BO3_USER_DIR, to, to_n) ? 1 : -1;
}
#endif

int bo3_user_dirs_pending(char *text, unsigned n)
{
    int count = 0;
#if !defined(__ANDROID__)
    char from[1024], to[1024];
    const char *what;
    int i, kind, rc;
    unsigned at = 0;

    if (text && n)
        text[0] = 0;
    for (i = 0; (rc = dir_move(i, from, sizeof from, to, sizeof to, &what, &kind)) != 0; i++) {
        if (rc < 0 || !recomp_dir_migrate_pending(from, to))
            continue;
        count++;
        if (text && at < n)
            at += (unsigned)snprintf(text + at, n - at, "%s%s -> %s", at ? "\n" : "", from, to);
    }
#else
    if (text && n)
        text[0] = 0;
#endif
    return count;
}

/* The folder of `kind` as it stands now: under the old name while its move
 * is pending, else under the new one. Nothing is made. */
static int user_dir_now(int kind, char *out, unsigned n)
{
#if !defined(__ANDROID__)
    char from[1024], to[1024];
    const char *what;
    int i, k, rc;

    for (i = 0; (rc = dir_move(i, from, sizeof from, to, sizeof to, &what, &k)) != 0; i++)
        if (rc > 0 && k == kind && recomp_dir_migrate_pending(from, to)) {
            snprintf(out, n, "%s", from);
            return 1;
        }
#endif
    return recomp_user_dir(kind, BO3_USER_DIR, out, n);
}

const char *bo3_save_dir_now(void)
{
    static char dir[1024];
    if (!user_dir_now(RECOMP_DIR_DATA, dir, sizeof dir))
        dir[0] = 0;
    return dir;
}

void bo3_user_dirs_names(void)
{
#if defined(_WIN32) || defined(__APPLE__)
    nv2a_pcache_set_app(BO3_USER_DIR);
#elif !defined(__ANDROID__)
    nv2a_pcache_set_app("takedawn");
#endif
}

void bo3_user_dirs_init(void)
{
#if !defined(__ANDROID__)
    char from[1024], to[1024];
    const char *what;
    int i, kind, rc;

    for (i = 0; (rc = dir_move(i, from, sizeof from, to, sizeof to, &what, &kind)) != 0; i++) {
        if (rc < 0)
            continue;
        rc = recomp_dir_migrate(from, to);
        if (rc > 0) {
            dirs_note("[DIRS] the %s folder has the port's new name: moved %s to %s\n",
                      what, from, to);
#if !defined(_WIN32)
            /* The old name stays as a link to the new folder, for what still
             * looks there: a build from before the new name, and the app the
             * source installer made, whose launcher has the old path. */
            if (kind == RECOMP_DIR_DATA && symlink(to, from) == 0)
                dirs_note("[DIRS] %s is now a link to it\n", from);
#endif
        } else if (rc < 0) {
            dirs_note("[DIRS] the %s folder %s could not be moved to %s; it is left where "
                      "it is and a new one is used\n", what, from, to);
        }
    }
#endif
    bo3_user_dirs_names();
}

/* ── where the data is ─────────────────────────────────────────────── */

static int file_exists(const char *p)
{
    FILE *f = fopen(p, "rb");
    if (f)
        fclose(f);
    return f != NULL;
}

static void default_root(char *out, unsigned n)
{
    char base[1024];

    /* Under the old name while the folders' move is still to come: a query
     * says where the data is now, the game start that follows moves it. */
    if (!user_dir_now(RECOMP_DIR_LOCAL, base, sizeof base))
        snprintf(base, sizeof base, ".");
    snprintf(out, n, "%s/Game", base);
}

/* ── the folder the player chose for the data (BO-197) ────────────── */

/* The setup window's Change... button, or --install --remember, writes the
 * chosen data root into this file in the per-user local folder (beside the
 * default Game/ folder: %LOCALAPPDATA%\Takedawn on Windows, the save folder
 * elsewhere). Its first line is the folder; the rest is a note for whoever
 * opens it. No file: the default. */
#define LOCATION_FILE "data-folder.txt"

static int location_file(char *out, unsigned n)
{
    char base[1024];

#if defined(__ANDROID__)
    (void)base; (void)out; (void)n;
    return 0;                           /* the app's own folder, always */
#else
    if (!user_dir_now(RECOMP_DIR_LOCAL, base, sizeof base))
        return 0;
    return snprintf(out, n, "%s/" LOCATION_FILE, base) < (int)n;
#endif
}

static int is_sep(char c) { return c == '/' || c == '\\'; }

/* Without the separators at its end, but "/" and "C:\" stay whole. */
static void trim_path(char *p)
{
    size_t n = strlen(p);

    while (n > 0 && (p[n - 1] == ' ' || p[n - 1] == '\t'))
        p[--n] = 0;
    while (n > 1 && is_sep(p[n - 1]) && !(n == 3 && p[1] == ':'))
        p[--n] = 0;
}

/* Two names of the same folder, as far as the text says: the separators and,
 * on Windows and macOS, the case do not count. */
static int same_path(const char *a, const char *b)
{
    char x[1100], y[1100];
    size_t i;

    snprintf(x, sizeof x, "%s", a);
    snprintf(y, sizeof y, "%s", b);
    trim_path(x);
    trim_path(y);
    for (i = 0; x[i] && y[i]; i++) {
        char c = x[i], d = y[i];
        if (is_sep(c) && is_sep(d))
            continue;
#if defined(_WIN32) || defined(__APPLE__)
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (d >= 'A' && d <= 'Z') d = (char)(d - 'A' + 'a');
#endif
        if (c != d)
            return 0;
    }
    return x[i] == y[i];
}

static int dir_exists(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 && (st.st_mode & S_IFMT) == S_IFDIR;
}

/* The saved folder; 0 when there is none (no file, an empty first line). */
/* The folder the first line of a data-folder.txt names. */
static int location_read(const char *file, char *out, unsigned n)
{
    char line[1100], *p;
    size_t len;
    FILE *f;

    if (!(f = fopen(file, "rb")))
        return 0;
    len = fread(line, 1, sizeof line - 1, f);
    fclose(f);
    line[len] = 0;
    p = line;
    if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF)
        p += 3;                         /* a byte order mark from an editor */
    p[strcspn(p, "\r\n")] = 0;
    while (*p == ' ' || *p == '\t')
        p++;
    trim_path(p);
    if (!*p)
        return 0;
    snprintf(out, n, "%s", p);
    return 1;
}

static int saved_root(char *out, unsigned n)
{
    char file[1100];

    return location_file(file, sizeof file) && location_read(file, out, n);
}

static int make_dirs(const char *path)
{
    char p[1100];
    size_t i;

    snprintf(p, sizeof p, "%s", path);
    for (i = 1; p[i]; i++) {
        if (!is_sep(p[i]) || (i == 2 && p[1] == ':'))
            continue;
        p[i] = 0;
#if defined(_WIN32)
        _mkdir(p);
#else
        mkdir(p, 0755);
#endif
        p[i] = '/';
    }
#if defined(_WIN32)
    _mkdir(p);
#else
    mkdir(p, 0755);
#endif
    return dir_exists(p);
}

/* `dest` as a whole path: the working directory in front of a relative one. */
static void absolute_path(const char *dest, char *out, unsigned n)
{
#if defined(_WIN32)
    if (!_fullpath(out, dest, n))
        snprintf(out, n, "%s", dest);
#else
    char cwd[1024];

    if (dest[0] != '/' && getcwd(cwd, sizeof cwd))
        snprintf(out, n, "%s/%s", cwd, dest);
    else
        snprintf(out, n, "%s", dest);
#endif
    trim_path(out);
}

/* --install --remember: from now on the game looks in `dest`. The default
 * root removes the file. 1 when done; else 0 with the reason in `why`. */
static int location_save(const char *dest, char *saved, unsigned saved_n, char *why, unsigned why_n)
{
    char file[1100], def[1024], base[1024];
    FILE *f;

    absolute_path(dest, saved, saved_n);
    if (!location_file(file, sizeof file)) {
        snprintf(why, why_n, "There is no per-user folder to keep the choice in.");
        return 0;
    }
    default_root(def, sizeof def);
    if (same_path(saved, def)) {
        if (remove(file) != 0 && errno != ENOENT) {
            snprintf(why, why_n, "%s could not be removed: %s", file, strerror(errno));
            return 0;
        }
        saved[0] = 0;
        return 1;
    }
    snprintf(base, sizeof base, "%s", file);
    base[strlen(base) - strlen("/" LOCATION_FILE)] = 0;
    if (!make_dirs(base) || !(f = fopen(file, "wb"))) {
        snprintf(why, why_n, "%s could not be written: %s", file, strerror(errno));
        return 0;
    }
    fprintf(f, "%s\n"
               "# The folder of %s's game data, chosen in the setup window. The game\n"
               "# reads the first line. Delete this file to use the default folder.\n",
            saved, BO3_APP_NAME);
    if (fclose(f) != 0) {
        snprintf(why, why_n, "%s could not be written: %s", file, strerror(errno));
        return 0;
    }
    return 1;
}

/* ── the setup window ──────────────────────────────────────────────── */

#if defined(_WIN32)
#define SETUP_NAME "takedawn-setup.exe"
#else
#define SETUP_NAME "takedawn-setup"
#endif

/* The setup window's program: TAKEDAWN_SETUP (a developer's build of it),
 * else "takedawn-setup" in the executable's own folder, which is where a
 * release has it (Takedawn.app/Contents/MacOS on macOS). 0: there is none. */
static int setup_program(char *out, unsigned n)
{
    const char *env = getenv("TAKEDAWN_SETUP");

    if (env && *env)
        snprintf(out, n, "%s", env);
    else
        snprintf(out, n, "%s/" SETUP_NAME, recomp_exe_dir());
    return file_exists(out);
}

/* Whether a window can be shown at all: an unattended start has said no
 * (--no-setup, --headless, TAKEDAWN_NO_SETUP), and a Linux session without
 * a display has nowhere to put one. */
static int setup_wanted(int flags)
{
    const char *no = getenv("TAKEDAWN_NO_SETUP"), *headless = getenv("RECOMP_HEADLESS");

    if ((flags & BO3_DATA_NO_SETUP) || (no && *no && strcmp(no, "0"))
        || (headless && *headless && strcmp(headless, "0")))
        return 0;
#if defined(__ANDROID__)
    return 0;
#elif !defined(_WIN32) && !defined(__APPLE__)
    {
        const char *x = getenv("DISPLAY"), *w = getenv("WAYLAND_DISPLAY");
        if (!(x && *x) && !(w && *w))
            return 0;
    }
#endif
    return 1;
}

extern void xbox_InputShutdown(void);    /* toolkit, src/input/input_core.c */

/* Hand over to the setup window: this process becomes it (the launcher that
 * started the game keeps one process to watch), and its Play button starts
 * the game again. Returns only when it could not be started. */
static void setup_open(const char *root, int named)
{
#if !defined(_WIN32) && !defined(__ANDROID__)
    char prog[1100];
    const char *self = recomp_exe_path();
    char *args[8];
    int n = 0;

    if (!setup_program(prog, sizeof prog) || !self[0])
        return;
    args[n++] = prog;
    args[n++] = (char *)"--program";
    args[n++] = (char *)self;
    if (named == 1) {                   /* --data / TAKEDAWN_DATA: the same root */
        args[n++] = (char *)"--dest";
        args[n++] = (char *)root;
    }
    args[n] = NULL;
    XR_NOTICE( "[DATA] opening the setup window: %s\n", prog);
    fflush(stdout);
    fflush(stderr);
    /* exec runs no atexit: any pad's motors off and the pads let go first
     * (nothing has started one this early; it costs nothing to be sure). */
    xbox_InputShutdown();
    execv(prog, args);
    XR_ERROR( "[DATA] the setup window could not be started (%s)\n", prog);
#elif defined(_WIN32)
    /* Windows has no exec: the window is started as a child and this process
     * waits for it and ends with its status, so whoever started the game
     * still has one process to watch. */
    char prog[1100], cmd[3400];
    const char *self = recomp_exe_path();
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    DWORD status = 0;

    if (!setup_program(prog, sizeof prog) || !self[0])
        return;
    if (named == 1)
        snprintf(cmd, sizeof cmd, "\"%s\" --program \"%s\" --dest \"%s\"", prog, self, root);
    else
        snprintf(cmd, sizeof cmd, "\"%s\" --program \"%s\"", prog, self);
    XR_NOTICE( "[DATA] opening the setup window: %s\n", prog);
    fflush(stdout);
    fflush(stderr);
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    if (!CreateProcessA(prog, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        XR_ERROR( "[DATA] the setup window could not be started (%s, error %lu)\n", prog,
                (unsigned long)GetLastError());
        return;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &status);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    exit((int)status);
#else
    (void)root; (void)named;            /* Android: its own picker activity */
#endif
}

/* ── Add Steam library art (the overlay's System page) ───────────── */

#if defined(__linux__) && !defined(__ANDROID__)
int bo3_steam_art_available(void)
{
    char prog[1100];

    return setup_program(prog, sizeof prog);
}

/* `takedawn-setup --steam-art`, which writes the library art of the Takedawn
 * shortcuts Steam already has and prints one line for the player (and, as its
 * status, 0 when it wrote any). The data root is this process's working
 * directory (bo3_data_enter went there); without a title folder in it the
 * setup program asks the game itself. */
int bo3_steam_art_run(char *line, unsigned n)
{
    char prog[1100], cwd[1100], buf[600];
    const char *self = recomp_exe_path();
    char *args[8];
    int out[2], k = 0, len = 0, status = 0;
    pid_t pid;

    snprintf(line, n, "The setup program was not found.");
    if (!setup_program(prog, sizeof prog) || !self[0] || pipe(out) != 0)
        return 0;
    args[k++] = prog;
    args[k++] = (char *)"--steam-art";
    args[k++] = (char *)"--program";
    args[k++] = (char *)self;
    if (getcwd(cwd, sizeof cwd) && strlen(cwd) < sizeof cwd - 40) {
        strcat(cwd, "/" GAME_DIR "/default.xbe");
        if (file_exists(cwd)) {
            cwd[strlen(cwd) - strlen("/" GAME_DIR "/default.xbe")] = 0;
            args[k++] = (char *)"--dest";
            args[k++] = cwd;
        }
    }
    args[k] = NULL;
    pid = fork();
    if (pid < 0) {
        close(out[0]);
        close(out[1]);
        snprintf(line, n, "Could not start the setup program.");
        return 0;
    }
    if (pid == 0) {
        dup2(out[1], 1);
        close(out[0]);
        close(out[1]);
        execv(prog, args);
        _exit(127);
    }
    close(out[1]);
    for (;;) {                          /* its one line, at most a minute */
        struct pollfd pf = { out[0], POLLIN, 0 };
        int r = poll(&pf, 1, 60000), got;

        if (r <= 0) {
            kill(pid, SIGKILL);
            len = 0;
            break;
        }
        got = (int)read(out[0], buf + len, sizeof buf - 1 - len);
        if (got <= 0)
            break;
        len += got;
        if (len >= (int)sizeof buf - 1)
            break;
    }
    close(out[0]);
    waitpid(pid, &status, 0);
    buf[len] = 0;
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
        buf[--len] = 0;
    if (len > 0) {                      /* the last line */
        char *nl = strrchr(buf, '\n');

        snprintf(line, n, "%s", nl ? nl + 1 : buf);
    } else {
        snprintf(line, n, "The setup program did not answer.");
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}
#endif

/* The command a person types to run this program again: the AppImage file
 * they hold (APPIMAGE) rather than the program inside it, which lives in a
 * mount that is gone when the program exits. */
static const char *own_command(void)
{
    static char buf[1100];
    const char *p = NULL;

#if !defined(_WIN32) && !defined(__ANDROID__)
    p = getenv("APPIMAGE");
#endif
    if (!p || !*p)
        p = recomp_exe_path()[0] ? recomp_exe_path() : "burnout3";
    if (strchr(p, ' ') && strlen(p) < sizeof buf - 3) {
        snprintf(buf, sizeof buf, "\"%s\"", p);
        return buf;
    }
    return p;
}

/* The title cannot start without its data: the setup window where there is
 * one and a person to see it; else the lines below for whoever started the
 * game, and exit status 3. */
static int setup_needed(const char *code, const char *why, const char *source, const char *root,
                        int named, int flags)
{
    if (setup_wanted(flags))
        setup_open(root, named);
    XR_OUT("PROTOCOL 1\nSTEP data FAIL %s %s\nRESULT FAIL data %s\n", code, why, code);
    fflush(stdout);
    XR_ERROR( "%s: %s\nInstall it from your own disc image or extracted disc folder:\n"
                    "  %s --install %s\n", BO3_APP_NAME, why,
            own_command(), source);
    /* A player who started the game from an icon has no terminal: say it
     * where they look (the setup window did not take over, or it would not
     * have got here). Not for an unattended start (--no-setup, --headless):
     * it has the lines above and the exit status. */
#if !defined(__ANDROID__)               /* the app's start page says it there */
    if (!(flags & BO3_DATA_NO_SETUP))
        bo3_fatal_box(BO3_APP_NAME " cannot start",
                      "%s\n\nThe game's data comes from your own Burnout 3: Takedown disc. "
                      "Install it with the setup program that comes with %s (takedawn-setup), "
                      "or from a terminal:\n\n  %s --install %s",
                      why, BO3_APP_NAME,
                      own_command(), source);
#endif
    return BO3_EXIT_NO_DATA;
}

/* ── which disc's program runs (package.h) ─────────────────────────── */

/* Build i's data root where its program looks by itself (cases 4 and 5 of
 * install.h): the folder chosen in its setup window, else its per-user
 * Game folder; this program's own as data_state has it. */
static int pkg_root(int i, char *root, unsigned n)
{
#if defined(__ANDROID__)
    (void)i; (void)root; (void)n;
    return 0;                           /* one app, one folder: the Java side picks */
#else
    char base[1024], file[1100];

    if (i == BO3_PACKAGE_SELF) {
        if (!saved_root(root, n))
            default_root(root, n);
        return 1;
    }
    if (!recomp_user_dir(RECOMP_DIR_LOCAL, k_package[i].user_dir, base, sizeof base))
        return 0;
    snprintf(file, sizeof file, "%s/" LOCATION_FILE, base);
    if (!location_read(file, root, n))
        snprintf(root, n, "%s/Game", base);
    return 1;
#endif
}

/* Build i's data is installed and whole there: its marker names its
 * manifest and its executable is in place. */
static int pkg_whole(int i, char *root, unsigned n)
{
    xr_install_marker mark;
    char there[1200];

    if (!pkg_root(i, root, n) || !xr_install_marker_read(root, &mark)
        || strcmp(mark.manifest, k_package[i].manifest_id))
        return 0;
    snprintf(there, sizeof there, "%s/" GAME_DIR "/default.xbe", root);
    return file_exists(there);
}

/* disc.txt, in the main program's per-user local folder. */
static int pkg_record_file(char *out, unsigned n)
{
#if defined(__ANDROID__)
    (void)out; (void)n;
    return 0;
#else
    char base[1024];

    if (!recomp_user_dir(RECOMP_DIR_LOCAL, k_package[0].user_dir, base, sizeof base))
        return 0;
    return snprintf(out, n, "%s/" BO3_PACKAGE_RECORD, base) < (int)n;
#endif
}

/* The build disc.txt names; -1 when there is no such file, or it names no
 * build of this package. `present`: the file is there at all. */
static int pkg_recorded(int *present)
{
    char file[1100], text[600], id[64];
    size_t len = 0;
    FILE *f;

    if (present)
        *present = 0;
    if (BO3_PACKAGE_COUNT < 2 || !pkg_record_file(file, sizeof file) || !(f = fopen(file, "rb")))
        return -1;
    len = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    text[len] = 0;
    if (present)
        *present = 1;
    return bo3_package_record_parse(text, id, sizeof id) ? pkg_by_id(id) : -1;
}

static int pkg_record(int i)
{
    char file[1100], text[600], dir[1100], *slash;
    FILE *f;
    int ok;

    if (!pkg_record_file(file, sizeof file) || !bo3_package_record_format(k_package[i].id, text, sizeof text))
        return 0;
    snprintf(dir, sizeof dir, "%s", file);
    slash = strrchr(dir, '/');
    if (slash) {
        *slash = 0;
        recomp_fs_mkdir_p(dir);
    }
    if (!(f = fopen(file, "wb")))
        return 0;
    ok = fputs(text, f) >= 0;
    ok = fclose(f) == 0 && ok;
    return ok;
}

/* Which program a start of build `self`'s program runs, and what each
 * disc's data is: whole[i] and its root in roots[i] (for the messages). */
static int pkg_choose(int self, int *whole, char roots[][1024], int *recorded, int *record_present)
{
    int i;

    *recorded = pkg_recorded(record_present);
    for (i = 0; i < BO3_PACKAGE_COUNT; i++) {
        roots[i][0] = 0;
        whole[i] = pkg_whole(i, roots[i], sizeof roots[i]);
    }
    return bo3_package_pick(BO3_PACKAGE_COUNT, self, *recorded, whole);
}

void bo3_package_start(int argc, char **argv)
{
#if !defined(__ANDROID__)
    int whole[BO3_PACKAGE_COUNT], i, recorded, present, pick;
    char roots[BO3_PACKAGE_COUNT][1024], why[1300];
    const char *env = getenv("TAKEDAWN_DATA");

    /* One step only (the program that started this one chose it); and a
     * start told where the data is (--data, TAKEDAWN_DATA, a default.xbe,
     * a checkout) runs where it is told: the data's marker says whose it
     * is (data_state). --install hands the disc to its program itself,
     * --version is this program's own. */
    if (BO3_PACKAGE_COUNT < 2 || s_sibling_hop || (env && *env))
        return;
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        size_t len = strlen(a);
        if (!strcmp(a, "--version") || !strcmp(a, "--install") || !strcmp(a, "--data")
            || !strcmp(a, "--dest") || (len > 4 && same_nocase(a + len - 4, ".xbe")))
            return;
    }
    if (file_exists(GAME_DIR "/default.xbe"))
        return;
    pick = pkg_choose(BO3_PACKAGE_SELF, whole, roots, &recorded, &present);
    if (pick == BO3_PACKAGE_SELF)
        return;
    if (whole[pick])
        snprintf(why, sizeof why, "the %s disc's data is installed (%s)%s", k_package[pick].name,
                 roots[pick], pick == recorded ? ", the disc installed last" : "");
    else
        snprintf(why, sizeof why, "the %s disc was installed last (%s), and no disc's data is whole",
                 k_package[pick].name, BO3_PACKAGE_RECORD);
    pkg_exec(pick, why);
#else
    (void)argc; (void)argv;
#endif
}

/* ── where the data is, and whether it is whole ────────────────────── */

enum { DATA_OK, DATA_CHECKOUT, DATA_MISSING };

/* The root the game would use (cases 2 to 5 of install.h) and its state.
 * `named`: 1 for --data or TAKEDAWN_DATA, 2 for the folder the player chose
 * (data-folder.txt), 0 for the default. `code`, `why` and `source` (the
 * --install arguments that would mend it) are filled for DATA_MISSING,
 * `mark` for DATA_OK. */
static int data_state(const char *data_arg, char *root, unsigned root_n, int *named,
                      xr_install_marker *mark, const char **code, char *why, unsigned why_n,
                      char *source, unsigned source_n)
{
    const char *env = getenv("TAKEDAWN_DATA");
    char there[1100];

    *named = 1;
    *code = "not-installed";
    snprintf(source, source_n, "<image-or-folder>");
    if (data_arg && *data_arg) {
        snprintf(root, root_n, "%s", data_arg);
    } else if (env && *env) {
        snprintf(root, root_n, "%s", env);
    } else if (file_exists(GAME_DIR "/default.xbe")) {
        snprintf(root, root_n, ".");
        return DATA_CHECKOUT;           /* a checkout: the data is where we stand */
    } else if (saved_root(root, root_n)) {
        *named = 2;                     /* the folder chosen in the setup window */
    } else {
        default_root(root, root_n);
        *named = 0;
    }
    snprintf(there, sizeof there, "%s/" GAME_DIR "/default.xbe", root);
    if (!xr_install_marker_read(root, mark) && file_exists(there)) {
        /* Files from before the marker (the source installer's, a copy made
         * by hand): the install checks them where they lie. */
        *code = "unverified";
        snprintf(source, source_n, "\"%s/" GAME_DIR "\" --dest \"%s\"", root, root);
        snprintf(why, why_n, "There are game files in %s, but this program has not checked "
                 "them. Installing from that folder checks them in place; nothing is copied.",
                 root);
    } else if (!xr_install_marker_read(root, mark) && *named == 2 && !dir_exists(root)) {
        snprintf(why, why_n, "The folder chosen for the game's data, %s, is not there. If it is "
                 "on a drive that is not connected, connect it and start %s again, or choose "
                 "another folder in the setup window.", root, BO3_APP_NAME);
    } else if (!xr_install_marker_read(root, mark)) {
        snprintf(why, why_n, "The game's data is not installed in %s.", root);
    } else if (strcmp(mark->manifest, BO3_MANIFEST_ID)) {
        int other = pkg_by_manifest(mark->manifest);
        if (other >= 0) {
            char said[200];
            snprintf(said, sizeof said, "the data in %s is the %s disc's", root, k_package[other].name);
            pkg_exec(other, said);
            snprintf(why, why_n, "The data in %s is the %s disc's, and this program is for the "
                     "%s disc. The program for that disc is not beside this one: install %s "
                     "again, or install your %s disc.", root, k_package[other].name, k_manifest.build,
                     BO3_APP_NAME, k_manifest.build);
        } else {
            snprintf(why, why_n, "The data in %s was installed for another build of the game "
                     "(manifest %s; this program needs %s). Install it again.", root, mark->manifest,
                     BO3_MANIFEST_ID);
        }
    } else if (!file_exists(there)) {
        snprintf(why, why_n, "The game's data in %s is not readable, or its default.xbe "
                 "is gone. Install it again.", root);
    } else {
        return DATA_OK;
    }
    return DATA_MISSING;
}

int bo3_data_enter(const char *data_arg, int xbe_named, int flags)
{
    char root[1024], why[1400], source[1200];
    const char *code;
    xr_install_marker mark;
    int named, state, quiet = flags & BO3_DATA_QUIET;
    int move = !(flags & (BO3_DATA_QUIET | BO3_DATA_NO_MOVE));

    /* The folders of the port's earlier name are moved by a start that has
     * a title to run, and only by that (and a real --install): a start
     * that ends in the setup window or in "not installed", and the bug
     * report, leave them where they are. */
    if (xbe_named) {
        if (move)
            bo3_user_dirs_init();
        return 0;
    }
    state = data_state(data_arg, root, sizeof root, &named, &mark, &code, why, sizeof why, source,
                       sizeof source);
    if (move && (state == DATA_CHECKOUT || state == DATA_OK)) {
        bo3_user_dirs_init();
        if (state == DATA_OK)           /* the per-user root may have had the old name */
            state = data_state(data_arg, root, sizeof root, &named, &mark, &code, why,
                               sizeof why, source, sizeof source);
    }
    switch (state) {
    case DATA_CHECKOUT:
        return 0;
    case DATA_OK:
        if (bo3_chdir(root) != 0) {
            snprintf(why, sizeof why, "The game's data in %s is not readable. Install it again.",
                     root);
            break;
        }
        if (!quiet)
            XR_NOTICE( "[DATA] root: %s (%s; installed by %s, manifest %s)\n", root,
                    named == 1 ? "named" : named == 2 ? "the chosen folder, " LOCATION_FILE
                    : "the per-user folder", mark.version, mark.manifest);
        return 0;
    default:
        break;
    }
    return quiet ? 0 : setup_needed(code, why, source, root, named, flags);
}

int bo3_data_status_main(int argc, char **argv)
{
    char root[1024], why[1400], source[1200], def[1024];
    const char *code, *data_arg = NULL;
    xr_install_marker mark;
    int named, i, state;

    for (i = 0; i < argc; i++)
        if ((!strcmp(argv[i], "--data") || !strcmp(argv[i], "--dest")) && i + 1 < argc)
            data_arg = argv[++i];
    state = data_state(data_arg, root, sizeof root, &named, &mark, &code, why, sizeof why, source,
                       sizeof source);
    default_root(def, sizeof def);
    XR_OUT("PROTOCOL 1\nPLAN data\n"
           "LOG data root %s\n"
           "LOG data place %s\n"
           "LOG data default %s\n"
           "LOG data disc %s, %s\n"
           "LOG data files %u\n"
           "LOG data bytes %llu\n"
           "LOG data free-needed %llu\n"
           "LOG data version %s\n"
           "LOG data build %s\n",
           root, state == DATA_CHECKOUT ? "checkout" : named == 1 ? "named"
                 : named == 2 ? "saved" : "default", def,
           k_manifest.title, k_manifest.build, k_manifest.count,
           (unsigned long long)xr_manifest_bytes(&k_manifest), (unsigned long long)FREE_NEEDED,
           bo3_version_line(), BO3_BUILD_ID);
    {
        /* Folders of the port's earlier name: reported, not moved. The game
         * start that has its data, and a real --install, move them. */
        char pending[2600], *line, *next;
        if (bo3_user_dirs_pending(pending, sizeof pending))
            for (line = pending; line && *line; line = next) {
                next = strchr(line, '\n');
                if (next)
                    *next++ = 0;
                XR_OUT("LOG data pending-move %s\n", line);
            }
    }
#if !defined(__ANDROID__)
    {
        /* The setup window's "Sharper menu pictures" box (BO-311): whether
         * it is offered here, ticked or not, and at which quality. */
        char q[16], why_up[96];
        int offer = bo3_upscale_offer(q, sizeof q, why_up, sizeof why_up);
        XR_OUT("LOG data upscale %s %s %s\n", offer == 2 ? "on" : offer == 1 ? "off" : "no", q, why_up);
        XR_OUT("LOG data upscale-pack %s\n", bo3_upscale_pack_state());
    }
#endif
    if (state == DATA_MISSING) {
        XR_OUT("STEP data FAIL %s %s\nRESULT FAIL data %s\n", code, why, code);
        return BO3_EXIT_NO_DATA;
    }
    if (state == DATA_CHECKOUT)
        XR_OUT("STEP data OK a checkout: the data is in the working directory\n");
    else
        XR_OUT("STEP data OK installed by %s, manifest %s\n", mark.version, mark.manifest);
    XR_OUT("RESULT OK %s\n", root);
    return 0;
}

/* ── enhance.toml ──────────────────────────────────────────────────── */

const char *bo3_enhance_root(void)
{
    static char root[1024];
    char file[1100];
    const char *save = getenv("RECOMP_SAVE_DIR");

#if !defined(__ANDROID__)
    /* The player's own, beside settings.toml: a release's program folder is
     * not theirs to write in (a macOS bundle, Program Files, an AppImage). */
    if (save && *save)
        snprintf(root, sizeof root, "%s", save);
    else if (!recomp_user_dir(RECOMP_DIR_DATA, BO3_USER_DIR, root, sizeof root))
        root[0] = 0;
    if (root[0]) {
        snprintf(file, sizeof file, "%s/enhance.toml", root);
        if (file_exists(file))
            return root;
    }
#else
    (void)save; (void)file;
#endif
    /* Beside the executable: a checkout's bin/, a portable copy. */
    snprintf(root, sizeof root, "%s", recomp_exe_dir());
    return root;
}

/* ── --install ─────────────────────────────────────────────────────── */

static char s_cancel_file[1100];
static volatile sig_atomic_t s_stop;
static time_t s_cancel_looked;

static void on_signal(int sig)
{
    (void)sig;
    s_stop = 1;
}

#if defined(_WIN32)
static BOOL WINAPI on_console(DWORD type)
{
    (void)type;
    s_stop = 1;
    return TRUE;
}
#endif

/* The cancel file, as the shader pre-build has one: looked for once a second. */
static int install_cancelled(void *ctx)
{
    time_t now;

    (void)ctx;
    if (s_stop)
        return 1;
    now = time(NULL);
    if (now != s_cancel_looked) {
        s_cancel_looked = now;
        if (file_exists(s_cancel_file))
            s_stop = 1;
    }
    return s_stop;
}

static void out(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
    fflush(stdout);
}

static void install_progress(void *ctx, double fraction, const char *text)
{
    (void)ctx;
    out("PROGRESS " STEP " %.3f %s", fraction, text);
}

static void install_log(void *ctx, const char *text)
{
    (void)ctx;
    out("LOG " STEP " %s", text);
}

static int install_usage(void)
{
    XR_ERROR(
        "usage: burnout3 --install <image-or-folder> [--dest <dir>] [--check]\n"
        "  Puts the game's data in place from your own disc: an Xbox disc image (.iso: an\n"
        "  xiso, or a redump with the video partition) or a folder extracted from one.\n"
        "  Only %s, %s is accepted; every file is\n"
        "  checked against the disc's manifest (%u files, %.2f GB).\n"
        "  --dest <dir>   where to (default: TAKEDAWN_DATA, else the folder chosen\n"
        "                 before, else the per-user folder)\n"
        "  --remember     the game uses <dir> from now on, with no --data: written to\n"
        "                 " LOCATION_FILE " in the per-user folder (the default folder\n"
        "                 removes it)\n"
        "  --check        only say what the disc is; nothing is written\n"
        "  stdout is the installer's line protocol:\n"
        "    PROTOCOL 1 / PLAN disc / STEP disc BEGIN|OK|SKIP|FAIL ... /\n"
        "    PROGRESS disc <0..1> <file> / LOG disc <text> / RESULT OK|FAIL ...\n"
        "  Stop it at any time (or create <dest>/install.cancel): the next run goes on\n"
        "  where it stopped.\n",
        k_manifest.title, k_manifest.build, k_manifest.count,
        (double)xr_manifest_bytes(&k_manifest) / 1e9);
    return 2;
}

int bo3_install_main(int argc, char **argv)
{
    const char *source = NULL, *dest_arg = NULL, *env = getenv("TAKEDAWN_DATA");
    char dest[1024];
    xr_install_opts o;
    xr_install_result r;
    time_t t0 = time(NULL);
    int i, check = 0, remember = 0;
    int whole_before[BO3_PACKAGE_COUNT], before = -1, recorded = -1, record_present = 0;
    const char *place;
    char def[1024];
    char roots_before[BO3_PACKAGE_COUNT][1024], switched[1600] = "";

    for (i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "--dest") && i + 1 < argc)
            dest_arg = argv[++i];
        else if (!strcmp(argv[i], "--data") && i + 1 < argc) {
            /* The data root by its other name, before or after --install:
             * where the game is told to look is where an install goes.
             * --dest wins when both are given. */
            if (!dest_arg)
                dest_arg = argv[i + 1];
            i++;
        } else if (!strcmp(argv[i], "--check"))
            check = 1;
        else if (!strcmp(argv[i], "--remember"))
            remember = 1;
        else if (argv[i][0] == '-' || source)
            return install_usage();
        else
            source = argv[i];
    }
    if (!source)
        return install_usage();
    /* Another disc of the package: its program does the install (and says so
     * on the same pipe), before this one has said a word. */
    {
        int disc = source_build(source);
        if (disc >= 0 && disc != BO3_PACKAGE_SELF) {
            char said[200];
            snprintf(said, sizeof said, "the disc is the %s disc", k_package[disc].name);
            pkg_exec(disc, said);
        }
    }
    if (!check) {
        /* A real install: the folders of the port's earlier name move now,
         * so the data goes under the new one. A --dest that names the
         * per-user root as it was called before the move (the setup window
         * passes what --data-status told it) follows it. --check moves
         * nothing. */
        char was[1024];
        default_root(was, sizeof was);
        bo3_user_dirs_init();
        default_root(dest, sizeof dest);
        if (dest_arg && !strcmp(dest_arg, was) && strcmp(was, dest))
            dest_arg = NULL;
        if (source && !strncmp(source, was, strlen(was)) && strcmp(was, dest)
            && (source[strlen(was)] == '/' || source[strlen(was)] == '\\')) {
            /* and a source inside it (files checked where they lie) */
            static char moved_source[1100];
            snprintf(moved_source, sizeof moved_source, "%s%s", dest, source + strlen(was));
            source = moved_source;
        }
    }
    place = "named";
    if (dest_arg)
        snprintf(dest, sizeof dest, "%s", dest_arg);
    else if (env && *env)
        snprintf(dest, sizeof dest, "%s", env);
    else if (saved_root(dest, sizeof dest))
        place = "saved";
    else {
        default_root(dest, sizeof dest);
        place = "default";
    }
    if (!check) {
        /* Which disc's program a start ran before this install, for the
         * message, and whether disc.txt is wanted (pkg_installed). */
        before = pkg_choose(0, whole_before, roots_before, &recorded, &record_present);
        if (!whole_before[before])
            before = -1;
    }
    snprintf(s_cancel_file, sizeof s_cancel_file, "%s/install.cancel", dest);
    remove(s_cancel_file);              /* one left by an earlier run */
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
#if defined(_WIN32)
    SetConsoleCtrlHandler(on_console, TRUE);
#endif

    memset(&o, 0, sizeof o);
    o.source = source;
    o.source_fd = -1;
    o.dest = dest;
    o.subdir = GAME_DIR;
    o.manifest = &k_manifest;
    o.version = BO3_VERSION;
    o.free_needed = FREE_NEEDED;
    o.identify_only = check;
    o.progress = install_progress;
    o.log = install_log;
    o.cancelled = install_cancelled;

    out("PROTOCOL 1");
    out("PLAN " STEP);
    if (check)
        out("STEP " STEP " BEGIN Checking the disc");
    else
        out("STEP " STEP " BEGIN Checking the disc and copying the game's data (%.1f GB)",
            (double)xr_manifest_bytes(&k_manifest) / 1e9);
    out("LOG " STEP " source %s", source);
    if (!check)
        out("LOG " STEP " destination %s", dest);
    if (remember && !check) {
        /* Before the copy: a run that stops half way is resumed there, and
         * the next start opens the setup window on that folder. */
        char saved[1100], why[1400];
        if (!location_save(dest, saved, sizeof saved, why, sizeof why)) {
            out("STEP " STEP " FAIL write-failed The game could not keep the folder chosen for its "
                "data. %s", why);
            out("RESULT FAIL " STEP " write-failed");
            return 1;
        }
        if (saved[0])
            out("LOG " STEP " remembered: the game uses %s from now on", saved);
        else
            out("LOG " STEP " remembered: the game uses its default folder");
    }
    xr_install(&o, &r);
    remove(s_cancel_file);
    if (r.code != XR_INSTALL_OK) {
        out("STEP " STEP " FAIL %s %s", xr_install_code_name(r.code), r.message);
        out("RESULT FAIL " STEP " %s", xr_install_code_name(r.code));
        return 1;
    }
    if (check) {
        /* For the setup window: the disc's program (this one) and where it
         * puts the data, which may not be where the window's first question
         * was answered (another disc's program). */
        default_root(def, sizeof def);
        out("LOG " STEP " build %s", BO3_BUILD_ID);
        out("LOG " STEP " root %s", dest);
        out("LOG " STEP " place %s", place);
        out("LOG " STEP " default %s", def);
        out("LOG " STEP " bytes %llu", (unsigned long long)xr_manifest_bytes(&k_manifest));
        out("LOG " STEP " free-needed %llu", (unsigned long long)FREE_NEEDED);
        if (r.image)
            out("STEP " STEP " OK %s, %s (a disc image, game partition at 0x%llX)",
                k_manifest.title, k_manifest.build, (unsigned long long)r.image_base);
        else
            out("STEP " STEP " OK %s, %s (an extracted folder)", k_manifest.title,
                k_manifest.build);
        out("RESULT OK %s", source);
        return 0;
    }
    {
        /* The start takes this disc from now on. disc.txt only when it
         * matters: another disc's data is whole too, or the file is there. */
        int others = 0;
        for (i = 0; i < BO3_PACKAGE_COUNT; i++)
            others += i != BO3_PACKAGE_SELF && whole_before[i];
        if ((others || record_present) && BO3_PACKAGE_COUNT > 1 && !pkg_record(BO3_PACKAGE_SELF))
            out("LOG " STEP " could not write %s: the game may start another disc's data", BO3_PACKAGE_RECORD);
        else if (others || record_present)
            out("LOG " STEP " start %s (%s)", BO3_BUILD_ID, BO3_PACKAGE_RECORD);
        if (before >= 0 && before != BO3_PACKAGE_SELF) {
            int shared = !strcmp(k_package[before].user_dir, BO3_USER_DIR);
            int over = same_path(roots_before[before], dest);
            out("LOG " STEP " switched from %s, %s %s", k_package[before].id, over ? "replaced in" : "kept in",
                roots_before[before]);
            snprintf(switched, sizeof switched,
                     ". Takedawn starts this disc (%s) from now on. %s %s disc's data %s %s; "
                     "%s. Install that disc again to go back to it",
                     k_manifest.build, over ? "It replaces the" : "The", k_package[before].name,
                     over ? "in" : "stays in", roots_before[before],
                     shared ? "the two European discs share their saves and settings"
                            : "each disc keeps its own saves and settings");
        }
        /* Another disc's data in this very folder (the two European discs share
         * one, so does a folder chosen for both): this install took its place. */
        for (i = 0; i < BO3_PACKAGE_COUNT; i++) {
            size_t at = strlen(switched);
            if (i == BO3_PACKAGE_SELF || i == before || !whole_before[i] || !same_path(roots_before[i], dest))
                continue;
            out("LOG " STEP " replaced %s in %s", k_package[i].id, dest);
            snprintf(switched + at, sizeof switched - at, ". It replaces the %s disc's data in %s",
                     k_package[i].name, dest);
        }
    }
    if (r.already)
        out("STEP " STEP " SKIP already installed (%u files, manifest %s)%s", k_manifest.count,
            k_manifest.id, switched);
    else
        out("STEP " STEP " OK %u files, %llu bytes; %u copied, %u checked in place, %u kept "
            "from an earlier run; %ld s%s", k_manifest.count,
            (unsigned long long)xr_manifest_bytes(&k_manifest), r.copied, r.verified, r.kept,
            (long)(time(NULL) - t0), switched);
    out("RESULT OK %s", dest);
    return 0;
}

/* ── Android: the setup page's installer ───────────────────────────── */

#if defined(__ANDROID__)
/* SetupActivity (android/java) in its own process (android:process=":setup"),
 * as the bug report has one: the disc image the player picked with the
 * system's file picker comes as a file descriptor, the files go under the
 * app's own files folder, which is the title's working directory there:
 *
 *   <files>/Burnout 3 Takedown/...   <files>/install.journal   <files>/.complete
 *
 * A folder put there by hand (adb push, a file manager: the way before this
 * page) is the source "folder": its files are checked where they lie. A
 * folder chosen on the page (a document tree, BO-223) is read through
 * SetupActivity.openSource, file by file, by the same installer. */
#include <jni.h>

static volatile int s_jni_cancel;

typedef struct {
    JNIEnv   *env;
    jobject   self;
    jmethodID progress;
    jmethodID open_source;
    jfieldID  src_size;
    time_t    last;
    int       pct;
} JniInstall;

static void jni_progress(void *ctx, double fraction, const char *text)
{
    JniInstall *j = (JniInstall *)ctx;
    int pct = (int)(fraction * 1000.0);
    jstring s;

    /* A file's name ten times a second is enough for a page. */
    if (pct == j->pct && time(NULL) == j->last)
        return;
    j->pct = pct;
    j->last = time(NULL);
    s = (*j->env)->NewStringUTF(j->env, text ? text : "");
    (*j->env)->CallVoidMethod(j->env, j->self, j->progress, (jdouble)fraction, s);
    if ((*j->env)->ExceptionCheck(j->env))
        (*j->env)->ExceptionClear(j->env);
    (*j->env)->DeleteLocalRef(j->env, s);
}

static int jni_cancelled(void *ctx)
{
    (void)ctx;
    return s_jni_cancel;
}

/* 0: nothing there. 1: a whole install of a disc of the package (the app
 * starts that disc's library). 2: an install for another build (another
 * manifest). 3: game files without the marker. */
JNIEXPORT jint JNICALL
Java_io_github_xenophobentx_takedawn_SetupActivity_nativeState(JNIEnv *env, jclass cls,
                                                               jstring jroot)
{
    const char *root = (*env)->GetStringUTFChars(env, jroot, NULL);
    char there[1200];
    xr_install_marker mark;
    int state = 0;

    (void)cls;
    snprintf(there, sizeof there, "%s/" GAME_DIR "/default.xbe", root);
    if (xr_install_marker_read(root, &mark))
        state = pkg_by_manifest(mark.manifest) >= 0 && file_exists(there) ? 1 : 2;
    else if (file_exists(there))
        state = 3;
    (*env)->ReleaseStringUTFChars(env, jroot, root);
    return state;
}

/* "<title>\n<version line>" and, for each disc of the package, a line
 * "<build id>\t<manifest id>\t<disc>\t<files>\t<bytes>\t<bytes wanted free>". */
JNIEXPORT jstring JNICALL
Java_io_github_xenophobentx_takedawn_SetupActivity_nativeAbout(JNIEnv *env, jclass cls)
{
    char t[1600];
    size_t at;
    int i;

    (void)cls;
    at = (size_t)snprintf(t, sizeof t, "%s\n%s", k_manifest.title, bo3_version_line());
    for (i = 0; i < BO3_PACKAGE_COUNT && at < sizeof t; i++) {
        xr_manifest m = pkg_manifest(i);
        at += (size_t)snprintf(t + at, sizeof t - at, "\n%s\t%s\t%s\t%u\t%llu\t%llu", k_package[i].id,
                               k_package[i].manifest_id, k_package[i].name, m.count,
                               (unsigned long long)xr_manifest_bytes(&m),
                               (unsigned long long)pkg_free_needed(i));
    }
    return (*env)->NewStringUTF(env, t);
}

JNIEXPORT void JNICALL
Java_io_github_xenophobentx_takedawn_SetupActivity_nativeCancel(JNIEnv *env, jclass cls)
{
    (void)env; (void)cls;
    s_jni_cancel = 1;
}

/* A document tree (the folder picker's content:// URI) has no file names,
 * so the installer asks Java for each file by its path on the disc:
 * SetupActivity.openSource(rel) returns a descriptor it detached from the
 * content resolver (-1: not in the tree, -2: not readable) and leaves the
 * file's size in its srcSize field. */
static int jni_source_open(void *ctx, const char *rel, uint64_t *size)
{
    JniInstall *j = (JniInstall *)ctx;
    jstring s = (*j->env)->NewStringUTF(j->env, rel);
    jint fd = (*j->env)->CallIntMethod(j->env, j->self, j->open_source, s);

    (*j->env)->DeleteLocalRef(j->env, s);
    if ((*j->env)->ExceptionCheck(j->env)) {
        (*j->env)->ExceptionClear(j->env);
        return -2;
    }
    if (fd >= 0)
        *size = (uint64_t)(*j->env)->GetLongField(j->env, j->self, j->src_size);
    return (int)fd;
}

/* The install: from the descriptor of a disc image (fd >= 0), from a folder
 * by path, or (tree) from the document tree SetupActivity holds; of any disc
 * of the package, with that disc's manifest (the marker then names it, and
 * the app starts its library). Calls this.onProgress(double, String) as it
 * goes. Returns "<code word>\n<sentence>\n<copied> <checked in place>
 * <kept> <already>\n<build id of the disc, or ->". */
static jstring jni_install(JNIEnv *env, jobject self, jint fd, jstring jfolder, jstring jroot,
                           int tree)
{
    const char *root = (*env)->GetStringUTFChars(env, jroot, NULL);
    const char *folder = jfolder ? (*env)->GetStringUTFChars(env, jfolder, NULL) : NULL;
    jclass cls = (*env)->GetObjectClass(env, self);
    JniInstall j;
    xr_install_opts o;
    xr_install_result r;
    char t[1400];

    memset(&j, 0, sizeof j);
    j.env = env;
    j.self = self;
    j.pct = -1;
    j.progress = (*env)->GetMethodID(env, cls, "onProgress", "(DLjava/lang/String;)V");
    if (tree) {
        j.open_source = (*env)->GetMethodID(env, cls, "openSource", "(Ljava/lang/String;)I");
        j.src_size = (*env)->GetFieldID(env, cls, "srcSize", "J");
    }
    s_jni_cancel = 0;
    memset(&o, 0, sizeof o);
    o.source = folder;
    o.source_fd = folder || tree ? -1 : (int)fd;
    if (tree && j.open_source && j.src_size)
        o.source_open = jni_source_open;
    o.dest = root;
    o.subdir = GAME_DIR;
    o.manifest = &k_manifest;
    o.version = bo3_version_line();
    /* Files that are checked where they lie need no room. */
    o.free_needed = folder ? 0 : FREE_NEEDED;
    o.ctx = &j;
    o.progress = j.progress ? jni_progress : NULL;
    o.cancelled = jni_cancelled;
    if (tree && !o.source_open) {
        snprintf(t, sizeof t, "internal\nThe setup page cannot read folders.\n0 0 0 0\n-");
    } else {
        int disc = source_build_of(&o);
        xr_manifest m;
        if (disc < 0)
            disc = BO3_PACKAGE_SELF;    /* none of them: this one's install says what it is */
        m = pkg_manifest(disc);
        o.manifest = &m;
        if (!folder)
            o.free_needed = pkg_free_needed(disc);
        xr_install(&o, &r);
        snprintf(t, sizeof t, "%s\n%s\n%u %u %u %d\n%s", xr_install_code_name(r.code), r.message,
                 r.copied, r.verified, r.kept, r.already,
                 r.code == XR_INSTALL_OK ? k_package[disc].id : "-");
    }
    if (folder)
        (*env)->ReleaseStringUTFChars(env, jfolder, folder);
    (*env)->ReleaseStringUTFChars(env, jroot, root);
    return (*env)->NewStringUTF(env, t);
}

JNIEXPORT jstring JNICALL
Java_io_github_xenophobentx_takedawn_SetupActivity_nativeInstall(JNIEnv *env, jobject self,
                                                                 jint fd, jstring jfolder,
                                                                 jstring jroot)
{
    return jni_install(env, self, fd, jfolder, jroot, 0);
}

/* The folder the player picked with ACTION_OPEN_DOCUMENT_TREE. */
JNIEXPORT jstring JNICALL
Java_io_github_xenophobentx_takedawn_SetupActivity_nativeInstallTree(JNIEnv *env, jobject self,
                                                                     jstring jroot)
{
    return jni_install(env, self, -1, NULL, jroot, 1);
}
#endif /* __ANDROID__ */
