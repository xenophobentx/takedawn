/*
 * install.c - the game's data from the player's disc, where the game finds
 * it, and the program's version. See install.h.
 */
#include "install.h"
#include "fatal.h"

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
/* A whole install wants this much free: the 2.48 GB of the disc and room to
 * spare for the saves, the shader cache and the title's own cache files. */
#define FREE_NEEDED (3000ull * 1000 * 1000)

/* ── the disc this program was made for ────────────────────────────── */

static const xr_manifest_file k_files[] = {
#include "bo3_manifest.inc"
};

static const xr_manifest k_manifest = {
    BO3_MANIFEST_ID,
    "Burnout 3: Takedown",
#if defined(BO3_REGION_PAL) && BO3_REGION_PAL
    "Europe (PAL) retail",
#else
    "North America (NTSC-U) retail",
#endif
    BO3_MANIFEST_TITLE_ID,
    "default.xbe",
    k_files,
    sizeof k_files / sizeof k_files[0]
};

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
static int saved_root(char *out, unsigned n)
{
    char file[1100], line[1100], *p;
    size_t len;
    FILE *f;

    if (!location_file(file, sizeof file) || !(f = fopen(file, "rb")))
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

/* ── Add Steam library art (the overlay's Advanced page) ───────────── */

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
        snprintf(why, why_n, "The data in %s was installed for another build of the game "
                 "(manifest %s; this program needs %s). Install it again.", root, mark->manifest,
                 BO3_MANIFEST_ID);
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
           "LOG data version %s\n",
           root, state == DATA_CHECKOUT ? "checkout" : named == 1 ? "named"
                 : named == 2 ? "saved" : "default", def,
           k_manifest.title, k_manifest.build, k_manifest.count,
           (unsigned long long)xr_manifest_bytes(&k_manifest), (unsigned long long)FREE_NEEDED,
           bo3_version_line());
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
    if (dest_arg)
        snprintf(dest, sizeof dest, "%s", dest_arg);
    else if (env && *env)
        snprintf(dest, sizeof dest, "%s", env);
    else if (!saved_root(dest, sizeof dest))
        default_root(dest, sizeof dest);
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
        if (r.image)
            out("STEP " STEP " OK %s, %s (a disc image, game partition at 0x%llX)",
                k_manifest.title, k_manifest.build, (unsigned long long)r.image_base);
        else
            out("STEP " STEP " OK %s, %s (an extracted folder)", k_manifest.title,
                k_manifest.build);
        out("RESULT OK %s", source);
        return 0;
    }
    if (r.already)
        out("STEP " STEP " SKIP already installed (%u files, manifest %s)", k_manifest.count,
            k_manifest.id);
    else
        out("STEP " STEP " OK %u files, %llu bytes; %u copied, %u checked in place, %u kept "
            "from an earlier run; %ld s", k_manifest.count,
            (unsigned long long)xr_manifest_bytes(&k_manifest), r.copied, r.verified, r.kept,
            (long)(time(NULL) - t0));
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
 * page) is the source "folder": its files are checked where they lie. */
#include <jni.h>

static volatile int s_jni_cancel;

typedef struct {
    JNIEnv   *env;
    jobject   self;
    jmethodID progress;
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

/* 0: nothing there. 1: a whole install for this program. 2: an install for
 * another build (another manifest). 3: game files without the marker. */
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
        state = !strcmp(mark.manifest, BO3_MANIFEST_ID) && file_exists(there) ? 1 : 2;
    else if (file_exists(there))
        state = 3;
    (*env)->ReleaseStringUTFChars(env, jroot, root);
    return state;
}

/* "<title>\n<build>\n<files>\n<bytes>\n<bytes wanted free>\n<version line>" */
JNIEXPORT jstring JNICALL
Java_io_github_xenophobentx_takedawn_SetupActivity_nativeAbout(JNIEnv *env, jclass cls)
{
    char t[600];

    (void)cls;
    snprintf(t, sizeof t, "%s\n%s\n%u\n%llu\n%llu\n%s", k_manifest.title, k_manifest.build,
             k_manifest.count, (unsigned long long)xr_manifest_bytes(&k_manifest),
             (unsigned long long)FREE_NEEDED, bo3_version_line());
    return (*env)->NewStringUTF(env, t);
}

JNIEXPORT void JNICALL
Java_io_github_xenophobentx_takedawn_SetupActivity_nativeCancel(JNIEnv *env, jclass cls)
{
    (void)env; (void)cls;
    s_jni_cancel = 1;
}

/* The install: from the descriptor of a disc image (fd >= 0), or from a
 * folder. Calls this.onProgress(double, String) as it goes. Returns
 * "<code word>\n<sentence>\n<copied> <checked in place> <kept> <already>". */
JNIEXPORT jstring JNICALL
Java_io_github_xenophobentx_takedawn_SetupActivity_nativeInstall(JNIEnv *env, jobject self,
                                                                 jint fd, jstring jfolder,
                                                                 jstring jroot)
{
    const char *root = (*env)->GetStringUTFChars(env, jroot, NULL);
    const char *folder = jfolder ? (*env)->GetStringUTFChars(env, jfolder, NULL) : NULL;
    JniInstall j;
    xr_install_opts o;
    xr_install_result r;
    char t[1400];

    memset(&j, 0, sizeof j);
    j.env = env;
    j.self = self;
    j.pct = -1;
    j.progress = (*env)->GetMethodID(env, (*env)->GetObjectClass(env, self), "onProgress",
                                     "(DLjava/lang/String;)V");
    s_jni_cancel = 0;
    memset(&o, 0, sizeof o);
    o.source = folder;
    o.source_fd = folder ? -1 : (int)fd;
    o.dest = root;
    o.subdir = GAME_DIR;
    o.manifest = &k_manifest;
    o.version = bo3_version_line();
    /* Files that are checked where they lie need no room. */
    o.free_needed = folder ? 0 : FREE_NEEDED;
    o.ctx = &j;
    o.progress = j.progress ? jni_progress : NULL;
    o.cancelled = jni_cancelled;
    xr_install(&o, &r);
    snprintf(t, sizeof t, "%s\n%s\n%u %u %u %d", xr_install_code_name(r.code), r.message,
             r.copied, r.verified, r.kept, r.already);
    if (folder)
        (*env)->ReleaseStringUTFChars(env, jfolder, folder);
    (*env)->ReleaseStringUTFChars(env, jroot, root);
    return (*env)->NewStringUTF(env, t);
}
#endif /* __ANDROID__ */
