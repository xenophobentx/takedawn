/* The mod folders' lookup (src/game/modfs.c) on folders the test makes in the
 * temporary directory: no game data, no guest. The test stands in for the
 * toolkit: it owns xbox_PathRedirectHook, puts a hook of its own there first
 * (as the soundtrack's is) and asks what the chain answers for guest paths.
 * The same cases run on every host; the ones a host cannot show (two names
 * that differ only in case on a volume that folds case, symbolic links on
 * Windows) say SKIP. */
#include "kernel.h"   /* xbox_PathRedirectHook */
#include "modfs.h"    /* the installed-mod list */

#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  include <windows.h>
#  include <direct.h>
#  include <io.h>
#  include <process.h>
#  define SETENV(k, v) _putenv_s(k, v)
#  define UNSETENV(k) _putenv_s(k, "")
#  define SEP "\\"
#  define dup _dup
#  define dup2 _dup2
#  define close _close
#  define fileno _fileno
#else
#  include <fcntl.h>
#  include <limits.h>
#  include <pthread.h>
#  include <sys/stat.h>
#  include <time.h>
#  include <unistd.h>
#  define SETENV(k, v) setenv(k, v, 1)
#  define UNSETENV(k) unsetenv(k)
#  define SEP "/"
#endif

const char *(*xbox_PathRedirectHook)(const char *xbox_path);
/* The toolkit's print gate (platform/xr_log.h, compiled into modfs.c): 0 is a
 * release build with diagnostic logging off. This file does not include it,
 * so its own lines are never dropped. */
extern int g_xr_log_diag;

static int s_fail, s_pass, s_skip;
#define CHECK(c, ...) do { if (c) s_pass++; else { s_fail++; fprintf(stderr, "FAIL line %d: ", __LINE__); \
    fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)
#define SKIP(...) do { s_skip++; printf("SKIP "); printf(__VA_ARGS__); putchar('\n'); } while (0)

/* snprintf that must fit: a path too long for the test's buffers stops the
 * test (exit 2) rather than testing a cut-off path. */
static void fit(char *buf, size_t n, const char *fmt, ...)
{
    va_list ap;
    int r;

    va_start(ap, fmt);
    r = vsnprintf(buf, n, fmt, ap);
    va_end(ap);
    if (r < 0 || (size_t)r >= n) {
        fprintf(stderr, "a path is too long for the test's buffers\n");
        exit(2);
    }
}

/* ---- the folders ---- */

static char s_root[512];
static char s_made[8192][700];  /* made, removed in reverse at the end */
static int  s_kind[8192];       /* 0 file, 1 folder, 2 link (or junction), 3 folder chmod 000 */
static int  s_nmade;

static const char *at(const char *rel)
{
    /* A few at a time: never more than 8 at() in one expression (the order
     * C evaluates arguments in is not fixed). */
    static char b[8][700];
    static int k;
    char *o = b[k++ & 7];
    fit(o, sizeof b[0], "%s" SEP "%s", s_root, rel);
#if defined(_WIN32)
    for (char *p = o; *p; p++)
        if (*p == '/')
            *p = '\\';
#endif
    return o;
}

static void remove_all(void);

static void remember(const char *path, int kind)
{
    if (s_nmade == (int)(sizeof s_made / sizeof s_made[0])) {
        fprintf(stderr, "more fixtures than the test can remove: raise s_made\n");
        exit(2);   /* remove_all (atexit) clears what was recorded */
    }
    fit(s_made[s_nmade], sizeof s_made[0], "%s", path);
    s_kind[s_nmade++] = kind;
}

/* 1 when the folder is there afterwards (made now, or already). */
static int make_dir(const char *path)
{
#if defined(_WIN32)
    int r = _mkdir(path);
#else
    int r = mkdir(path, 0755);
#endif
    if (r == 0)
        remember(path, 1);
    return r == 0 || errno == EEXIST;
}

/* rel's folders, then the file with the given text; 0 when any step fails. */
static int make_file(const char *rel, const char *text)
{
    char p[700];
    fit(p, sizeof p, "%s", at(rel));
    for (char *s = p + strlen(s_root) + 1; *s; s++) {
        if (*s == SEP[0]) {
            char c = *s;
            *s = 0;
            if (!make_dir(p))
                return 0;
            *s = c;
        }
    }
    FILE *f = fopen(p, "wb");
    if (!f)
        return 0;
    fputs(text, f);
    fclose(f);
    remember(p, 0);
    return 1;
}

/* Everything made, in reverse; also at exit (fit's exit(2), a failed start). */
static void remove_all(void)
{
    static int done;
    if (done || !s_root[0])
        return;
    done = 1;
    for (int i = s_nmade - 1; i >= 0; i--) {
#if defined(_WIN32)
        if (s_kind[i] == 0)
            DeleteFileA(s_made[i]);
        else
            RemoveDirectoryA(s_made[i]);   /* a junction goes, not what it points at */
#else
        if (s_kind[i] == 3)
            chmod(s_made[i], 0755);
        if (s_kind[i] == 1 || s_kind[i] == 3)
            rmdir(s_made[i]);
        else
            unlink(s_made[i]);
#endif
    }
#if defined(_WIN32)
    RemoveDirectoryA(s_root);
#else
    rmdir(s_root);
#endif
}

static int make_root(void)
{
#if defined(_WIN32)
    char tmp[MAX_PATH];
    DWORD n = GetTempPathA(sizeof tmp, tmp);
    if (!n || n >= sizeof tmp)
        return 0;
    fit(s_root, sizeof s_root, "%sbo3_modfs_%lu_%lu", tmp, (unsigned long)GetCurrentProcessId(),
             (unsigned long)GetTickCount());
    return _mkdir(s_root) == 0;
#else
    const char *t = getenv("TMPDIR");
    char tmpl[512], real[PATH_MAX];
    size_t n;
    fit(tmpl, sizeof tmpl, "%s", t && *t ? t : "/tmp");
    for (n = strlen(tmpl); n > 1 && tmpl[n - 1] == '/'; n--)
        tmpl[n - 1] = 0;                            /* macOS: TMPDIR ends in '/' */
    fit(tmpl + n, sizeof tmpl - n, "/bo3_modfs_XXXXXX");
    if (!mkdtemp(tmpl) || !realpath(tmpl, real))
        return 0;
    fit(s_root, sizeof s_root, "%s", real);   /* macOS: /var -> /private/var */
    return 1;
#endif
}

/* Whether the temporary directory's volume tells Aa from AA. */
static int case_sensitive(void)
{
    int sensitive;
    make_file("probe/Aa", "x");
#if defined(_WIN32)
    sensitive = GetFileAttributesA(at("probe/AA")) == INVALID_FILE_ATTRIBUTES;
#else
    struct stat st;
    sensitive = stat(at("probe/AA"), &st) != 0;
#endif
    return sensitive;
}

/* ---- asking the chain ---- */

static volatile int s_counting;
static int s_prev_calls;

/* The hook that was there before (the soundtrack's, in the game). */
static const char *prev_hook(const char *p)
{
    if (s_counting)
        s_prev_calls++;
    return p && strcmp(p, "E:\\chain") == 0 ? "PREV" : NULL;
}

static const char *ask(const char *guest)
{
    return xbox_PathRedirectHook ? xbox_PathRedirectHook(guest) : NULL;
}

/* What the file the chain names for guest holds ("" when it names none). */
static const char *read_back(const char *guest)
{
    static _Thread_local char text[64];
    const char *host = ask(guest);
    FILE *f;
    size_t n;

    text[0] = 0;
    if (!host || strcmp(host, "PREV") == 0)
        return text;
    f = fopen(host, "rb");
    if (!f)
        return "(cannot open)";
    n = fread(text, 1, sizeof text - 1, f);
    text[n] = 0;
    fclose(f);
    return text;
}

static int refused(const char *guest)
{
    const char *r = ask(guest);
    return r == NULL;   /* prev_hook answers NULL for every disc path */
}

/* What the toolkit does with an answer no path rule claims (kernel_path.c,
 * "Unrecognized Xbox path"): on Windows it converts it into a MAX_PATH wide
 * buffer with its terminator, XBOX_GUEST_CP, and does not check the result;
 * elsewhere it copies it into MAX_PATH characters. A model of that hand-off,
 * not the toolkit itself: 1 when host survives it whole. */
static int handoff_ok(const char *host)
{
#if defined(_WIN32)
    WCHAR w[MAX_PATH];
    return MultiByteToWideChar(XBOX_GUEST_CP, 0, host, -1, w, MAX_PATH) != 0;
#else
    return strlen(host) < MAX_PATH;
#endif
}

/* stderr into a file between capture_start and capture_end, then the text. */
static int s_saved_err = -1;
static int capture_start(const char *path)
{
    FILE *f;
    fflush(stderr);
    s_saved_err = dup(fileno(stderr));
    f = fopen(path, "wb");
    if (s_saved_err < 0 || !f || dup2(fileno(f), fileno(stderr)) < 0) {
        if (f)
            fclose(f);
        return 0;
    }
    fclose(f);
    remember(path, 0);
    return 1;
}

static void capture_end(const char *path, char *out, size_t n)
{
    FILE *f;
    size_t got = 0;
    fflush(stderr);
    dup2(s_saved_err, fileno(stderr));
    close(s_saved_err);
    f = fopen(path, "rb");
    if (f) {
        got = fread(out, 1, n - 1, f);
        fclose(f);
    }
    out[got] = 0;
}

static void set_mods(const char *list)
{
    if (list)
        SETENV("TAKEDAWN_MODS", list);
    else
        UNSETENV("TAKEDAWN_MODS");
    bo3_modfs_init();
}

static double now_us(void)
{
#if defined(_WIN32)
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1e6 / (double)f.QuadPart;
#else
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e6 + t.tv_nsec / 1e3;
#endif
}

/* ---- threads ---- */

#define THREADS 8
#define ROUNDS  2000
static int s_thread_bad[THREADS];

static void thread_body(int k)
{
    for (int i = 0; i < ROUNDS; i++) {
        const char *want = (i + k) % 3 == 0 ? "A static" : (i + k) % 3 == 1 ? "B only" : "";
        const char *guest = (i + k) % 3 == 0 ? "D:\\Tracks\\US\\C3_V1\\static.dat"
                          : (i + k) % 3 == 1 ? "D:\\Data\\only_b.bin" : "D:\\Data\\missing.bin";
        if (strcmp(read_back(guest), want) != 0)
            s_thread_bad[k]++;
    }
}

/* The same mix through installed mods (manifests() sets the folder up). */
static int s_mod_bad[THREADS];

static void mod_body(int k)
{
    for (int i = 0; i < ROUNDS; i++) {
        const char *want = (i + k) % 3 == 0 ? "car cars" : (i + k) % 3 == 1 ? "loose static" : "id a.mod";
        const char *guest = (i + k) % 3 == 0 ? "D:\\Data\\cars.bin"
                          : (i + k) % 3 == 1 ? "D:\\Tracks\\US\\C3_V1\\static.dat" : "D:\\Data\\order.bin";
        if (strcmp(read_back(guest), want) != 0)
            s_mod_bad[k]++;
    }
}

#if defined(_WIN32)
static unsigned __stdcall thread_main(void *a) { thread_body((int)(intptr_t)a); return 0; }
static unsigned __stdcall mod_main(void *a) { mod_body((int)(intptr_t)a); return 0; }
typedef unsigned (__stdcall *thread_fn)(void *);
#else
static void *thread_main(void *a) { thread_body((int)(intptr_t)a); return NULL; }
static void *mod_main(void *a) { mod_body((int)(intptr_t)a); return NULL; }
typedef void *(*thread_fn)(void *);
#endif

static void run_threads(thread_fn fn)
{
#if defined(_WIN32)
    HANDLE t[THREADS];
    for (int k = 0; k < THREADS; k++)
        t[k] = (HANDLE)_beginthreadex(NULL, 0, fn, (void *)(intptr_t)k, 0, NULL);
    WaitForMultipleObjects(THREADS, t, TRUE, INFINITE);
    for (int k = 0; k < THREADS; k++)
        CloseHandle(t[k]);
#else
    pthread_t t[THREADS];
    for (int k = 0; k < THREADS; k++)
        pthread_create(&t[k], NULL, fn, (void *)(intptr_t)k);
    for (int k = 0; k < THREADS; k++)
        pthread_join(t[k], NULL);
#endif
}

/* ---- installed mods: subfolders with a mod.toml (modfs.h) ---- */

static const bo3_mod *mod_by_folder(const char *sub)
{
    for (int i = 0; i < bo3_modfs_mod_count(); i++) {
        const bo3_mod *m = bo3_modfs_mod(i);
        size_t fl = strlen(m->folder), sl = strlen(sub);
        if (fl > sl && !strcmp(m->folder + fl - sl, sub) && (m->folder[fl - sl - 1] == '/' || m->folder[fl - sl - 1] == '\\'))
            return m;
    }
    return NULL;
}

/* The mod in folder sub was found; usable as expected; problem holds want. */
static void mod_is(int line, const char *sub, int usable, const char *want)
{
    const bo3_mod *m = mod_by_folder(sub);
    if (m && m->usable == usable && (!want || strstr(m->problem, want)))
        s_pass++;
    else {
        s_fail++;
        fprintf(stderr, "FAIL line %d: mod %s: %s\n", line, sub, !m ? "not listed" : m->problem[0] ? m->problem : "usable");
    }
}
#define MOD_IS(sub, usable, want) mod_is(__LINE__, sub, usable, want)

#if !defined(_WIN32)
static int open_fds(void)
{
    int n = 0;
    for (int fd = 0; fd < 1024; fd++)
        if (fcntl(fd, F_GETFD) != -1)
            n++;
    return n;
}
#endif

static void manifests(int sensitive)
{
    static const char ok_toml[] = "id = \"%s\"\nname = \"%s\"\n";
    char t[700], list[4096];

    /* A folder with loose files and installed mods (mods/), the way the
     * Android default folder and a library folder look. */
    CHECK(make_file("lib/Tracks/US/C3_V1/static.dat", "loose static"), "setup lib loose");
    CHECK(make_file("lib/Data/loose_only.bin", "loose only"), "setup lib loose 2");
    fit(t, sizeof t, ok_toml, "car.pack", "Car Pack");
    CHECK(make_file("lib/carpack/mod.toml", t) && make_file("lib/carpack/files/Tracks/US/C3_V1/static.dat", "car static")
          && make_file("lib/carpack/files/Data/cars.bin", "car cars"), "setup carpack");
    CHECK(make_file("lib/full/mod.toml", "id = \"full.meta\"\nname = \"Full\"\nversion = \"2.1.0\"\nauthor = \"A. Author\"\n"
                                         "description = \"Everything \\u00e9.\"\nhomepage = \"https://example.org\"\n")
          && make_file("lib/full/files/Data/full.bin", "full"), "setup full");
    CHECK(make_file("lib/empty/mod.toml", "") && make_file("lib/empty/files/Data/empty.bin", "x"), "setup empty");
    CHECK(make_file("lib/noid/mod.toml", "name = \"No id\"\n") && make_file("lib/noid/files/Data/noid.bin", "x"), "setup noid");
    CHECK(make_file("lib/badid/mod.toml", "id = \"Bad/Id\"\nname = \"x\"\n"), "setup badid");
    CHECK(make_file("lib/dupkey/mod.toml", "id = \"dk\"\nid = \"dk\"\nname = \"x\"\n"), "setup dupkey");
    CHECK(make_file("lib/multiline/mod.toml", "id = \"\"\"ml\"\"\"\nname = \"x\"\n"), "setup multiline");
    CHECK(make_file("lib/badutf8/mod.toml", "id = \"u8\"\nname = \"\xC0\xAF\"\n"), "setup badutf8");
    CHECK(make_file("lib/longname/mod.toml", "id = \"ln\"\nname = \"0123456789012345678901234567890123456789012345678901234567890"
                                             "12345678901234567890123456789012345678901234567890123456789012345678\"\n"),
          "setup longname");
    {
        static char big[BO3_MANIFEST_MAX_BYTES + 64];
        int k = sprintf(big, ok_toml, "big", "Big");
        memset(big + k, '#', sizeof big - (size_t)k - 2);
        big[sizeof big - 2] = '\n';
        big[sizeof big - 1] = 0;
        CHECK(make_file("lib/oversize/mod.toml", big), "setup oversize");
    }
    fit(t, sizeof t, ok_toml, "twin", "Twin one");
    CHECK(make_file("lib/twin1/mod.toml", t) && make_file("lib/twin1/files/Data/twin.bin", "twin1"), "setup twin1");
    fit(t, sizeof t, ok_toml, "twin", "Twin two");
    CHECK(make_file("lib/twin2/mod.toml", t) && make_file("lib/twin2/files/Data/twin.bin", "twin2"), "setup twin2");
    /* Order: by id, not by folder name or listing order. */
    fit(t, sizeof t, ok_toml, "b.mod", "B");
    CHECK(make_file("lib/a_first/mod.toml", t) && make_file("lib/a_first/files/Data/order.bin", "id b.mod"), "setup a_first");
    fit(t, sizeof t, ok_toml, "a.mod", "A");
    CHECK(make_file("lib/z_last/mod.toml", t) && make_file("lib/z_last/files/Data/order.bin", "id a.mod"), "setup z_last");
    /* A mod folder named like a disc folder: not part of the loose files. */
    fit(t, sizeof t, ok_toml, "sound.pack", "Sound");
    CHECK(make_file("lib/Sound/mod.toml", t) && make_file("lib/Sound/files/Sound/x.wav", "sound mod")
          && make_file("lib/Sound/y.wav", "inside the mod folder, not files/"), "setup Sound");
    /* Case: the mod spells the disc's path in its own case. */
    fit(t, sizeof t, ok_toml, "lower.case", "Lower");
    CHECK(make_file("lib/lowercase/mod.toml", t) && make_file("lib/lowercase/files/tracks/us/c3_v1/lc.dat", "lower case"),
          "setup lowercase");
    /* A subfolder without mod.toml stays loose files; a top-level mod.toml changes nothing. */
    CHECK(make_file("lib/plain/x.bin", "plain sub"), "setup plain");
    CHECK(make_file("lib/mod.toml", "id = \"top\"\nname = \"Top\"\n"), "setup top-level mod.toml");

    set_mods(at("lib"));
    MOD_IS("carpack", 1, NULL);
    {
        const bo3_mod *m = mod_by_folder("full");
        CHECK(m && m->usable && !strcmp(m->manifest.id, "full.meta") && !strcmp(m->manifest.version, "2.1.0")
              && !strcmp(m->manifest.author, "A. Author") && !strcmp(m->manifest.description, "Everything \xc3\xa9."),
              "optional metadata kept (unknown key skipped)");
    }
    MOD_IS("empty", 0, "empty");
    MOD_IS("noid", 0, "id is missing");
    MOD_IS("badid", 0, "id must be");
    MOD_IS("dupkey", 0, "duplicate key");
    MOD_IS("multiline", 0, "mod.toml:");
    MOD_IS("badutf8", 0, "invalid UTF-8");
    MOD_IS("longname", 0, "name is longer");
    MOD_IS("oversize", 0, "larger than");
    MOD_IS("twin1", 0, "is also used by");
    MOD_IS("twin2", 0, "is also used by");
    CHECK(!mod_by_folder("plain"), "a subfolder without mod.toml is not a mod");
    CHECK(!strcmp(read_back("D:\\Data\\twin.bin"), ""), "a shared id: neither copy supplies files");
    CHECK(!strcmp(read_back("D:\\Data\\empty.bin"), "") && !strcmp(read_back("D:\\Data\\noid.bin"), ""),
          "an invalid mod supplies no files");

    /* Lookups: loose first, then mods by id; files/ only; the mod folder is
     * not a loose folder; legacy files and folders unchanged. */
    CHECK(!strcmp(read_back("D:\\Tracks\\US\\C3_V1\\static.dat"), "loose static"), "loose file wins over a mod's");
    CHECK(!strcmp(read_back("D:\\Data\\cars.bin"), "car cars"), "a mod's file");
    CHECK(!strcmp(read_back("D:\\Data\\loose_only.bin"), "loose only"), "loose files still read");
    CHECK(!strcmp(read_back("D:\\Data\\order.bin"), "id a.mod"), "two mods, same file: the lower id wins");
    CHECK(!strcmp(read_back("D:\\Sound\\x.wav"), "sound mod"), "a mod named like a disc folder: its files/ is used");
    CHECK(!strcmp(read_back("D:\\Sound\\y.wav"), ""), "the mod folder itself is not loose files");
    CHECK(!strcmp(read_back("D:\\carpack\\files\\Data\\cars.bin"), ""), "a mod's folder is not reachable as a disc path");
    CHECK(!strcmp(read_back("D:\\carpack\\mod.toml"), ""), "mod.toml is never a disc file");
    CHECK(!strcmp(read_back("D:\\plain\\x.bin"), "plain sub"), "a subfolder without mod.toml stays loose files");
    CHECK(!strcmp(read_back("D:\\mod.toml"), "id = \"top\"\nname = \"Top\"\n"), "a top-level mod.toml is just a loose file");
    CHECK(refused("D:\\Data\\..\\Data\\cars.bin") && refused("D:\\carpack\\..\\Data\\cars.bin"), "'..' still refused");
    CHECK(!strcmp(read_back("D:\\TRACKS\\US\\C3_V1\\LC.DAT"), "lower case"), "a mod's own spelling found without case");
    {
        int ids_sorted = 1, prev_usable = 1;
        for (int i = 1; i < bo3_modfs_mod_count(); i++) {
            const bo3_mod *a = bo3_modfs_mod(i - 1), *b = bo3_modfs_mod(i);
            if (a->usable && b->usable && strcmp(a->manifest.id, b->manifest.id) >= 0)
                ids_sorted = 0;
            if (!prev_usable && b->usable)
                ids_sorted = 0;
            prev_usable = b->usable;
        }
        CHECK(ids_sorted && bo3_modfs_mod(0) && bo3_modfs_mod(0)->usable, "list: usable mods by id, then the rest");
        CHECK(bo3_modfs_mod(-1) == NULL && bo3_modfs_mod(bo3_modfs_mod_count()) == NULL, "out-of-range index: NULL");
    }

    /* Repeated discovery: the same list every time, nothing left open. */
    {
        int same = 1, n0 = bo3_modfs_mod_count();
        char first[BO3_MODFS_MAX_MODS][BO3_MOD_ID_MAX + 1];
#if !defined(_WIN32)
        int fds = open_fds();
#endif
        for (int i = 0; i < n0; i++)
            fit(first[i], sizeof first[0], "%s", bo3_modfs_mod(i)->manifest.id);
        for (int k = 0; k < 50; k++) {
            set_mods(at("lib"));
            if (bo3_modfs_mod_count() != n0)
                same = 0;
            for (int i = 0; same && i < n0; i++)
                if (strcmp(first[i], bo3_modfs_mod(i)->manifest.id))
                    same = 0;
        }
        CHECK(same, "50 discoveries: the same list");
#if !defined(_WIN32)
        CHECK(open_fds() == fds, "no descriptor left open (%d before, %d after)", fds, open_fds());
#endif
    }

    /* The same folder named twice: once. */
    set_mods(at("lib"));
    int mods_once = bo3_modfs_mod_count();
    fit(list, sizeof list, "%s;%s", at("lib"), at("lib"));
    set_mods(list);
    MOD_IS("carpack", 1, NULL);
    CHECK(refused("D:\\carpack\\mod.toml") && bo3_modfs_mod_count() == mods_once,
          "the same folder named twice: its mods are not loose files of the second");

    /* Two folders: folder order first, then loose / id inside each. */
    fit(t, sizeof t, ok_toml, "zz.second", "Second folder");
    CHECK(make_file("lib2/m/mod.toml", t) && make_file("lib2/m/files/Data/cars.bin", "second folder cars"), "setup lib2");
    fit(list, sizeof list, "%s;%s", at("lib2"), at("lib"));
    set_mods(list);
    CHECK(!strcmp(read_back("D:\\Data\\cars.bin"), "second folder cars"), "the first folder named wins, mods included");
    CHECK(bo3_modfs_mod(0) && bo3_modfs_mod(0)->root == 0, "list: the first folder's mods first");

    /* Links: a mod folder, its files/ or its mod.toml reached through a link
     * is not used. */
#if defined(_WIN32)
    {
        char cmd[2048];
        fit(t, sizeof t, ok_toml, "out.side", "Outside");
        CHECK(make_file("outmod/mod.toml", t) && make_file("outmod/files/Data/out.bin", "outside mod"), "setup outmod");
        CHECK(make_dir(at("libj")), "setup libj");
        fit(cmd, sizeof cmd, "mklink /J \"%s\" \"%s\" >NUL 2>&1", at("libj/jmod"), at("outmod"));
        if (system(cmd) == 0) {
            remember(at("libj/jmod"), 2);
            set_mods(at("libj"));
            CHECK(!mod_by_folder("jmod") && !strcmp(read_back("D:\\Data\\out.bin"), ""), "a junction as a mod folder: not a mod");
        } else {
            SKIP("junction: mklink /J failed");
        }
        fit(t, sizeof t, ok_toml, "jf", "Junction files");
        CHECK(make_file("libj2/jf/mod.toml", t), "setup libj2");
        fit(cmd, sizeof cmd, "mklink /J \"%s\" \"%s\" >NUL 2>&1", at("libj2/jf/files"), at("outmod/files"));
        if (system(cmd) == 0) {
            remember(at("libj2/jf/files"), 2);
            set_mods(at("libj2"));
            MOD_IS("jf", 0, "files is a link");
            CHECK(!strcmp(read_back("D:\\Data\\out.bin"), ""), "files/ through a junction: refused");
        } else {
            SKIP("junction: mklink /J failed");
        }
        /* The same folder twice by two names (a junction to it): taken once, its
         * mods not refused as shared ids. */
        fit(cmd, sizeof cmd, "mklink /J \"%s\" \"%s\" >NUL 2>&1", at("libalias"), at("lib"));
        if (system(cmd) == 0) {
            remember(at("libalias"), 2);
            fit(list, sizeof list, "%s;%s", at("lib"), at("libalias"));
            set_mods(list);
            MOD_IS("carpack", 1, NULL);
            CHECK(!strcmp(read_back("D:\\Data\\cars.bin"), "car cars"), "a folder and a junction to it: mods used once");
        } else {
            SKIP("junction: mklink /J failed");
        }
        CHECK(make_dir(at("libd")) && make_dir(at("libd/dirtoml")) && make_dir(at("libd/dirtoml/mod.toml")), "setup dirtoml");
        set_mods(at("libd"));
        MOD_IS("dirtoml", 0, "not a regular file");
    }
#else
    {
        fit(t, sizeof t, ok_toml, "out.side", "Outside");
        CHECK(make_file("outmod/mod.toml", t) && make_file("outmod/files/Data/out.bin", "outside mod"), "setup outmod");
        CHECK(make_dir(at("libl")) && symlink(at("outmod"), at("libl/lmod")) == 0, "setup a linked mod folder");
        remember(at("libl/lmod"), 2);
        set_mods(at("libl"));
        CHECK(!mod_by_folder("lmod") && !strcmp(read_back("D:\\Data\\out.bin"), ""), "a link as a mod folder: not a mod");
        fit(t, sizeof t, ok_toml, "lf", "Linked files");
        CHECK(make_file("libl2/lf/mod.toml", t) && symlink(at("outmod/files"), at("libl2/lf/files")) == 0, "setup linked files/");
        remember(at("libl2/lf/files"), 2);
        set_mods(at("libl2"));
        MOD_IS("lf", 0, "files is a link");
        CHECK(!strcmp(read_back("D:\\Data\\out.bin"), ""), "files/ through a link: refused");
        CHECK(symlink(at("lib"), at("libalias")) == 0, "setup alias link");
        remember(at("libalias"), 2);
        fit(list, sizeof list, "%s;%s", at("lib"), at("libalias"));
        set_mods(list);
        MOD_IS("carpack", 1, NULL);
        CHECK(!strcmp(read_back("D:\\Data\\cars.bin"), "car cars"), "a folder and a link to it: mods used once");
        CHECK(make_dir(at("libl3")) && make_dir(at("libl3/lt")) && symlink(at("outmod/mod.toml"), at("libl3/lt/mod.toml")) == 0,
              "setup linked mod.toml");
        remember(at("libl3/lt/mod.toml"), 2);
        CHECK(make_dir(at("libl3/gone")) && symlink(at("nowhere/mod.toml"), at("libl3/gone/mod.toml")) == 0,
              "setup dangling mod.toml");
        remember(at("libl3/gone/mod.toml"), 2);
        CHECK(make_file("libl3/noread/mod.toml", "id = \"nr\"\nname = \"x\"\n") && chmod(at("libl3/noread/mod.toml"), 0) == 0,
              "setup unreadable mod.toml");
        set_mods(at("libl3"));
        MOD_IS("lt", 0, "not a regular file");
        MOD_IS("gone", 0, "not a regular file");
        if (geteuid() != 0)
            MOD_IS("noread", 0, "cannot be read");
        else
            SKIP("unreadable mod.toml: running as root");
        chmod(at("libl3/noread/mod.toml"), 0644);
    }
#endif

    /* files/ in other cases: one other spelling is taken; two (Files and
     * FILES, no files) refuse the mod, without blocking later mods. */
    fit(t, sizeof t, ok_toml, "case.files", "Case files");
    CHECK(make_file("libc/a/mod.toml", t) && make_file("libc/a/Files/Data/cf.bin", "Files spelling"), "setup Files");
    if (sensitive) {
        fit(t, sizeof t, ok_toml, "two.files", "Two files");
        CHECK(make_file("libc/b/mod.toml", t) && make_file("libc/b/Files/Data/x.bin", "1")
              && make_file("libc/b/FILES/Data/x.bin", "2"), "setup Files + FILES");
        fit(t, sizeof t, ok_toml, "z.later", "Later");
        CHECK(make_file("libc/z/mod.toml", t) && make_file("libc/z/files/Data/later.bin", "later mod"), "setup later");
    }
    set_mods(at("libc"));
    CHECK(!strcmp(read_back("D:\\Data\\cf.bin"), "Files spelling"), "files/ spelled Files: used");
    if (sensitive) {
        MOD_IS("b", 0, "files is spelled twice");
        CHECK(!strcmp(read_back("D:\\Data\\later.bin"), "later mod"), "a mod with two files/ does not block later mods");
    } else {
        SKIP("files/ in two spellings: this volume folds case");
    }

    /* A mod folder whose path leaves no room below files/: refused, said. */
    {
        char sub[400], rel[480];
        size_t want = MAX_PATH - strlen(at("libp")) - 18;   /* <root>\<sub>\files\ + 12 > the host limit */
        if (want > 240)
            want = 240;
        memset(sub, 'p', want);
        sub[want] = 0;
        fit(rel, sizeof rel, "libp/%s/mod.toml", sub);
        fit(t, sizeof t, ok_toml, "long.path", "Long path");
        if (make_file(rel, t)) {
            set_mods(at("libp"));
            MOD_IS(sub, 0, "too long");
        } else {
            SKIP("long mod folder name: cannot be made here");
        }
    }

    /* The file list made at the start skips mods without the file; a mod too
     * deep for the list (a folder 9 levels down) is walked instead, and still
     * supplies its files. */
    fit(t, sizeof t, ok_toml, "deep", "Deep");
    CHECK(make_file("libdeep/deep/mod.toml", t) && make_file("libdeep/deep/files/a/b/c/d/e/f/g/h/i/deep.bin", "deep file")
          && make_file("libdeep/deep/files/Data/shallow.bin", "shallow"), "setup deep");
    /* Only mod.toml spelled so makes a mod, on every system; a mod with an
     * empty files/ supplies nothing. (Its own id, so that a wrongly taken
     * MOD.TOML fails only its own check.) */
    fit(t, sizeof t, ok_toml, "upper", "Upper");
    CHECK(make_file("libdeep/upper/MOD.TOML", t) && make_file("libdeep/upper/x.bin", "upper loose"), "setup MOD.TOML");
    fit(t, sizeof t, ok_toml, "empty.files", "Empty");
    CHECK(make_file("libdeep/empty/mod.toml", t) && make_dir(at("libdeep/empty/files")), "setup empty files");
    fit(t, sizeof t, ok_toml, "deep", "Deep");
    set_mods(at("libdeep"));
    CHECK(!mod_by_folder("upper") && !strcmp(read_back("D:\\upper\\x.bin"), "upper loose"),
          "MOD.TOML: not a manifest, the folder stays loose files");
    MOD_IS("empty", 1, NULL);
    CHECK(refused("D:\\Data\\nothing.bin"), "a mod with an empty files/: nothing supplied");
    CHECK(!strcmp(read_back("D:\\a\\b\\c\\d\\e\\f\\g\\h\\i\\deep.bin"), "deep file")
          && !strcmp(read_back("D:\\Data\\shallow.bin"), "shallow"), "a mod too deep for the list: walked, files found");

    /* A reason quotes the file: printable ASCII only, one "mod.toml:". */
    CHECK(make_file("libe/esc/mod.toml", "id = \"x\"\x1b[2J\nname = \"x\"\n"), "setup ESC manifest");
    set_mods(at("libe"));
    {
        const bo3_mod *m = mod_by_folder("esc");
        CHECK(m && !m->usable && !strchr(m->problem, 0x1b) && strchr(m->problem, '?') && !strstr(m->problem, "mod.toml: mod.toml"),
              "an ESC byte in the reason is shown as '?': %s", m ? m->problem : "(none)");
    }

    /* Limits: 64 mods (the smallest folder names), and a folder with more
     * than 1024 entries is not searched for mods. The mods are made in
     * reverse name order, so whatever order the system lists them in, the
     * kept set is decided by name. */
    {
        int made = 1;
        for (int i = BO3_MODFS_MAX_MODS; i >= 0 && made; i--) {
            char sub[64], id[32];
            fit(sub, sizeof sub, "many/m%03d/mod.toml", i);
            fit(id, sizeof id, "m%03d", i);
            fit(t, sizeof t, ok_toml, id, id);
            made = make_file(sub, t);
            fit(sub, sizeof sub, "many/m%03d/files/Tracks/US/own%03d.bin", i, i);
            made = made && make_file(sub, "own");
        }
        CHECK(made, "setup 65 mods");
        set_mods(at("many"));
        CHECK(bo3_modfs_mod_count() == BO3_MODFS_MAX_MODS && mod_by_folder("m063") && !mod_by_folder("m064"),
              "65 mods: the 64 smallest names taken (%d)", bo3_modfs_mod_count());
        CHECK(!strcmp(read_back("D:\\Tracks\\US\\own063.bin"), "own"), "the 64th mod supplies files");
        {
            double t0 = now_us();
            for (int i = 0; i < 200; i++)
                ask("D:\\Data\\nothere.bin");
            double t1 = now_us();
            for (int i = 0; i < 200; i++)
                ask("D:\\Tracks\\US\\C3_V1\\nothere.dat");
            double t2 = now_us();
            printf("cost: miss, 64 mods, not under any mod's first folder %8.2f us a lookup\n", (t1 - t0) / 200);
            printf("cost: miss, 64 mods, under every mod's first folder    %8.2f us a lookup\n", (t2 - t1) / 200);
        }
        /* The cap counts across folders: the first folder's mods first. */
        fit(list, sizeof list, "%s;%s", at("lib"), at("many"));
        set_mods(list);
        {
            int in_lib = 0, in_many = 0;
            for (int i = 0; i < bo3_modfs_mod_count(); i++)
                *(bo3_modfs_mod(i)->root == 0 ? &in_lib : &in_many) += 1;
            CHECK(bo3_modfs_mod_count() == BO3_MODFS_MAX_MODS && in_lib > 0 && in_many == BO3_MODFS_MAX_MODS - in_lib
                  && mod_by_folder("m000") && !mod_by_folder("m063"),
                  "64 mods across two folders: %d + %d", in_lib, in_many);
        }
        made = 1;
        for (int i = 0; i < 1025 && made; i++) {
            char f[64];
            fit(f, sizeof f, "crowd/f%04d", i);
            made = make_file(f, "x");
        }
        fit(t, sizeof t, ok_toml, "crowded", "Crowded");
        CHECK(made && make_file("crowd/zmod/mod.toml", t) && make_file("crowd/f0000.extra", "x"), "setup crowded folder");
        set_mods(at("crowd"));
        CHECK(bo3_modfs_mod_count() == 0 && !strcmp(read_back("D:\\f0001"), "x"),
              "more than 1024 entries: no mods, loose files still read");
        made = 1;
        for (int i = 0; i < 1023 && made; i++) {
            char f[64];
            fit(f, sizeof f, "crowd2/f%04d", i);
            made = make_file(f, "x");
        }
        CHECK(made && make_file("crowd2/zmod/mod.toml", t), "setup 1024 entries");
        set_mods(at("crowd2"));
        CHECK(bo3_modfs_mod_count() == 1, "exactly 1024 entries: searched (%d mods)", bo3_modfs_mod_count());
    }

    /* Release logging: the mods taken and the ones refused are said. */
    {
        char path[700], log[16384];
        fit(path, sizeof path, "%s", at("log_mods.txt"));
        if (capture_start(path)) {
            g_xr_log_diag = 0;
            set_mods(at("lib"));
            capture_end(path, log, sizeof log);
            g_xr_log_diag = 1;
            CHECK(strstr(log, "[MOD] mod car.pack: ") && strstr(log, "[MOD] mod full.meta 2.1.0: "),
                  "release: the mods taken are said:\n%s", log);
            CHECK(strstr(log, "[MOD] mod ignored (mod.toml: id is missing): "), "release: a refused mod is said:\n%s", log);
        } else {
            SKIP("mod log: stderr could not be captured");
        }
    }

    /* Concurrent lookups through mods and loose files. */
    set_mods(at("lib"));
    {
        int bad = 0;
        run_threads(mod_main);
        for (int k = 0; k < THREADS; k++)
            bad += s_mod_bad[k];
        CHECK(bad == 0, "%d threads x %d lookups through mods: %d wrong", THREADS, ROUNDS, bad);
    }
    set_mods(NULL);
    CHECK(bo3_modfs_mod_count() == 0, "unset: no mods listed");
}

/* ---- the mod list: mods on and off, conflicts ---- */

static const char *read_text(const char *path)
{
    static char b[2][1024];
    static int k;
    char *o = b[k++ & 1];
    FILE *f = fopen(path, "rb");
    size_t got = 0;
    if (f) {
        got = fread(o, 1, sizeof b[0] - 1, f);
        fclose(f);
    }
    o[got] = 0;
    return f ? o : NULL;
}

static int write_text(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return 0;
    fputs(text, f);
    fclose(f);
    return 1;
}

static const bo3_mod *mod_by_id(const char *id)
{
    for (int i = 0; i < bo3_modfs_mod_count(); i++)
        if (!strcmp(bo3_modfs_mod(i)->manifest.id, id))
            return bo3_modfs_mod(i);
    return NULL;
}

/* set_mods with stderr captured into log. */
static void init_logged(const char *list, char *log, size_t n)
{
    char path[700];
    fit(path, sizeof path, "%s", at("modlist_log.txt"));
    if (capture_start(path)) {
        set_mods(list);
        capture_end(path, log, n);
    } else {
        set_mods(list);
        log[0] = 0;
    }
}

static int s_toggle_bad[THREADS];
static void toggle_body(int k)
{
    static const char *const ids[] = { "t.a", "t.b", "t.c", "t.d" };
    for (int i = 0; i < 200; i++) {
        if (k < 4) {
            if (!bo3_modfs_set_enabled(ids[k], i & 1))
                s_toggle_bad[k]++;
        } else if (strcmp(read_back("D:\\Data\\x.bin"), "one") != 0) {
            s_toggle_bad[k]++;   /* this session does not change */
        }
    }
}
#if defined(_WIN32)
static unsigned __stdcall toggle_main(void *a) { toggle_body((int)(intptr_t)a); return 0; }
#else
static void *toggle_main(void *a) { toggle_body((int)(intptr_t)a); return NULL; }
#endif

static void mod_list(void)
{
    static const char ok_toml[] = "id = \"%s\"\nname = \"%s\"\n";
    char t[256], list[1600], state[700], log[16384];
    const bo3_mod *m;

    fit(t, sizeof t, ok_toml, "b.one", "One");
    CHECK(make_file("lb/m1/mod.toml", t) && make_file("lb/m1/files/Data/x.bin", "one")
          && make_file("lb/m1/files/Data/only1.bin", "only one"), "setup b.one");
    fit(t, sizeof t, ok_toml, "b.two", "Two");
    CHECK(make_file("lb/m2/mod.toml", t) && make_file("lb/m2/files/Data/x.bin", "two")
          && make_file("lb/m2/files/Data/y.bin", "two y"), "setup b.two");
    CHECK(make_file("lb/Data/y.bin", "loose y"), "setup loose y");
    fit(t, sizeof t, ok_toml, "b.three", "Three");
    CHECK(make_file("lb2/m3/mod.toml", t) && make_file("lb2/m3/files/Data/x.bin", "three"), "setup b.three");
    CHECK(make_dir(at("state")), "setup state folder");
    fit(state, sizeof state, "%s", at("state/mod-list.toml"));
    remember(state, 0);
    {
        char p[720];
        fit(p, sizeof p, "%s.bad", state);
        remember(p, 0);
        fit(p, sizeof p, "%s.tmp", state);
        remember(p, 0);
    }
    fit(list, sizeof list, "%s;%s", at("lb"), at("lb2"));
    bo3_modfs_state_file(state);

    /* No file: every mod on (as without the mod list); the order of A. */
    init_logged(list, log, sizeof log);
    CHECK(!strcmp(read_back("D:\\Data\\x.bin"), "one") && !strcmp(read_back("D:\\Data\\y.bin"), "loose y"),
          "no mod list: every mod on, loose first, then by id");
    CHECK((m = mod_by_id("b.one")) && m->enabled && m->usable, "b.one on");
    CHECK(!read_text(state), "nothing written at the start");
    CHECK(bo3_modfs_conflicts() == 2, "two files with more than one source (%d)", bo3_modfs_conflicts());
    CHECK(strstr(log, "[MOD] conflict: data/x.bin: mod b.one over mod b.two, mod b.three\n")
          && strstr(log, "[MOD] conflict: data/y.bin: folder 1 over mod b.two\n"), "conflict lines:\n%s", log);
    CHECK((m = mod_by_id("b.one")) && m->wins == 1 && m->loses == 0 && (m = mod_by_id("b.two")) && m->wins == 0
          && m->loses == 2 && (m = mod_by_id("b.three")) && m->loses == 1, "wins / loses per mod");

    /* Off: saved at once, used from the next start. */
    CHECK(bo3_modfs_set_enabled("b.one", 0), "b.one off: saved");
    CHECK(read_text(state) && strstr(read_text(state), "disabled = [\"b.one\"]"), "the file: %s", read_text(state));
    CHECK(bo3_modfs_enabled_next("b.one") == 0 && bo3_modfs_enabled_next("b.two") == 1, "next start: b.one off");
    CHECK(!strcmp(read_back("D:\\Data\\x.bin"), "one") && mod_by_id("b.one")->enabled, "this session unchanged");
    CHECK(bo3_modfs_set_enabled("b.one", 0), "off again: nothing to change, still 1");
    init_logged(list, log, sizeof log);
    CHECK(!strcmp(read_back("D:\\Data\\x.bin"), "two") && refused("D:\\Data\\only1.bin"), "after a restart: b.one's files not used");
    CHECK((m = mod_by_id("b.one")) && !m->enabled && m->usable && m->wins == 0, "b.one listed, off");
    CHECK(strstr(log, "[MOD] mod b.one: ") && strstr(log, " (off)\n"), "log says off:\n%s", log);
    CHECK(refused("D:\\m1\\mod.toml") && refused("D:\\m1\\files\\Data\\x.bin"), "an off mod's folder is still not loose files");
    CHECK(bo3_modfs_conflicts() == 2 && strstr(log, "data/x.bin: mod b.two over mod b.three"), "conflicts without b.one:\n%s", log);
    CHECK(bo3_modfs_set_enabled("b.one", 1) && strstr(read_text(state), "disabled = []"), "b.one on again: %s", read_text(state));
    set_mods(list);
    CHECK(!strcmp(read_back("D:\\Data\\x.bin"), "one"), "on again after a restart");

    /* Ids: invalid ones refused; ids of mods that are not there kept. */
    CHECK(!bo3_modfs_set_enabled("Bad Id", 0) && !bo3_modfs_set_enabled("", 0) && !bo3_modfs_set_enabled(NULL, 0),
          "an invalid id is refused");
    CHECK(bo3_modfs_set_enabled("not.installed", 0) && bo3_modfs_set_enabled("b.two", 0), "an id not installed: saved");
    set_mods(list);
    CHECK(bo3_modfs_enabled_next("not.installed") == 0 && !mod_by_id("b.two")->enabled, "kept across a restart");
    CHECK(bo3_modfs_set_enabled("b.two", 1) && strstr(read_text(state), "\"not.installed\"")
          && !strstr(read_text(state), "b.two"), "kept when the file is written again: %s", read_text(state));
    CHECK(bo3_modfs_set_enabled("not.installed", 1), "cleanup");

    /* A file that cannot be used: every mod on, a notice, and kept as .bad
     * before the first write. */
    {
        static const char *const bad[][2] = {
            { "disabled = [\"b.one\"", "not TOML" },
            { "disabled = \"b.one\"\n", "a string, not a list" },
            { "disabled = [\"b.one\\u0000x\"]\n", "\\u0000" },
            { "[disabled]\nx = 1\n", "a table" },
        };
        for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
            CHECK(write_text(state, bad[i][0]), "write bad file");
            init_logged(list, log, sizeof log);
            CHECK(!strcmp(read_back("D:\\Data\\x.bin"), "one") && mod_by_id("b.one")->enabled
                  && strstr(log, "[MOD] mod list not used ("), "%s: every mod on, a notice:\n%s", bad[i][1], log);
        }
        CHECK(bo3_modfs_set_enabled("b.three", 0), "a write after a bad file");
        {
            char p[720];
            fit(p, sizeof p, "%s.bad", state);
            CHECK(read_text(p) && !strcmp(read_text(p), bad[3][0]), "the bad file kept as .bad: %s", read_text(p));
        }
        CHECK(strstr(read_text(state), "disabled = [\"b.three\"]"), "the new file: %s", read_text(state));
        CHECK(bo3_modfs_set_enabled("b.three", 1), "cleanup");
    }
    CHECK(write_text(state, "# hand-edited\ndisabled = [\"b.one\", 5, \"Bad\", \"b.one\", \"zz.gone\"]\nother = 1\n"),
          "write a mixed file");
    init_logged(list, log, sizeof log);
    CHECK(!mod_by_id("b.one")->enabled && bo3_modfs_enabled_next("zz.gone") == 0
          && strstr(log, "[MOD] mod list: 2 entries that are not mod ids skipped"), "entries that are not ids skipped:\n%s", log);
    {
        static char big[70 * 1024];
        memset(big, '#', sizeof big - 1);
        big[0] = '\n';
        CHECK(write_text(state, big), "write a large file");
        init_logged(list, log, sizeof log);
        CHECK(mod_by_id("b.one")->enabled && strstr(log, "larger than 65536 bytes"), "larger than 64 KiB: not used:\n%s", log);
    }
    CHECK(write_text(state, "disabled = []\n"), "reset the file");
    set_mods(list);

    /* A write that fails leaves the file and the list as they were. */
    {
        char p[720];
        fit(p, sizeof p, "%s.tmp", state);
        CHECK(make_dir(p), "a folder where the temporary file goes");
        CHECK(!bo3_modfs_set_enabled("b.two", 0), "the write fails");
        CHECK(!strcmp(read_text(state), "disabled = []\n") && bo3_modfs_enabled_next("b.two") == 1,
              "file and list as they were: %s", read_text(state));
#if defined(_WIN32)
        _rmdir(p);
#else
        rmdir(p);
#endif
    }

    /* No file named: every mod on, nothing can be saved. */
    bo3_modfs_state_file(NULL);
    set_mods(list);
    CHECK(!bo3_modfs_set_enabled("b.one", 0) && mod_by_id("b.one")->enabled, "no mod list file: nothing saved");

    /* Without mods the file is not read (the no-mod path). */
    bo3_modfs_state_file(state);
    CHECK(write_text(state, "not toml at all [[["), "write a bad file");
    CHECK(make_dir(at("nomods")), "setup nomods");
    init_logged(at("nomods"), log, sizeof log);
    CHECK(!strstr(log, "mod list") && bo3_modfs_mod_count() == 0, "no mods: the mod list is not read:\n%s", log);
    init_logged(NULL, log, sizeof log);
    CHECK(!strstr(log, "mod list") && !log[0], "unset: nothing at all:\n%s", log);
    CHECK(write_text(state, "disabled = []\n"), "reset the file");

    /* Threads: four save while four look files up; this session does not
     * change, and the file ends as the last calls left it. */
    set_mods(list);
    run_threads(toggle_main);
    {
        int bad = 0;
        for (int k = 0; k < THREADS; k++)
            bad += s_toggle_bad[k];
        CHECK(!bad, "threads: %d failed saves or changed answers", bad);
        CHECK(!strstr(read_text(state), "\"t.") && bo3_modfs_enabled_next("t.a"), "threads: every id on at the end: %s",
              read_text(state));
    }

    /* Many conflicts: 32 lines, then a count. */
    fit(t, sizeof t, ok_toml, "c.first", "First");
    CHECK(make_file("lc/c1/mod.toml", t), "setup c1");
    fit(t, sizeof t, ok_toml, "c.second", "Second");
    CHECK(make_file("lc/c2/mod.toml", t), "setup c2");
    for (int i = 0; i < 40; i++) {
        char rel[64];
        fit(rel, sizeof rel, "lc/c1/files/Data/f%02d.bin", i);
        CHECK(make_file(rel, "first"), "setup c1 file");
        fit(rel, sizeof rel, "lc/c2/files/Data/f%02d.bin", i);
        CHECK(make_file(rel, "second"), "setup c2 file");
    }
    init_logged(at("lc"), log, sizeof log);
    {
        int lines = 0;
        for (const char *p = log; (p = strstr(p, "[MOD] conflict: ")); p++)
            lines++;
        CHECK(bo3_modfs_conflicts() == 40 && lines == 32 && strstr(log, "[MOD] ... and 8 more conflicts"),
              "40 conflicts: %d lines and a count", lines);
        CHECK(mod_by_id("c.first")->wins == 40 && mod_by_id("c.second")->loses == 40, "counts");
    }
    /* More files than are walked for: mod against mod still told, loose not. */
    CHECK(make_file("lc/Data/f00.bin", "loose"), "setup loose f00");
    for (int i = 0; i < 1030; i++) {
        char rel[64];
        fit(rel, sizeof rel, "lc/c2/files/Many/m%04d.bin", i);
        CHECK(make_file(rel, "m"), "setup many");
    }
    init_logged(at("lc"), log, sizeof log);
    CHECK(strstr(log, "conflicts with loose files and with mods looked up on disk not looked for")
          && strstr(log, "data/f00.bin: mod c.first over mod c.second\n") && bo3_modfs_conflicts() == 40,
          "over the walk limit:\n%.600s", log);

    /* Without mods the list is read when asked, so saving keeps its ids. */
    CHECK(write_text(state, "disabled = [\"keep.one\", \"keep.two\"]\n"), "write a list");
    set_mods(at("nomods"));
    CHECK(bo3_modfs_enabled_next("keep.one") == 0, "no mods: enabled_next reads the list");
    CHECK(bo3_modfs_set_enabled("new.id", 0) && strstr(read_text(state), "\"keep.one\", \"keep.two\", \"new.id\""),
          "no mods: a save keeps the ids already there: %s", read_text(state));
    set_mods(NULL);
    CHECK(bo3_modfs_set_enabled("new.id", 1) && strstr(read_text(state), "\"keep.two\"]"), "unset: the same: %s",
          read_text(state));

    /* A bad file and a failing write: the file stays, no .bad. */
    {
        char p[720], b[720];
        fit(p, sizeof p, "%s.tmp", state);
        fit(b, sizeof b, "%s.bad", state);
        remove(b);
        CHECK(write_text(state, "broken [[["), "write a bad file");
        set_mods(list);
        CHECK(make_dir(p), "a folder where the temporary file goes");
        CHECK(!bo3_modfs_set_enabled("b.one", 0), "the write fails");
        CHECK(!strcmp(read_text(state), "broken [[[") && !read_text(b), "the bad file stays where it was, no .bad");
#if defined(_WIN32)
        _rmdir(p);
#else
        rmdir(p);
#endif
        /* A link planted at the temporary name is not written through. */
        CHECK(write_text(at("victim.txt"), "victim"), "setup victim");
        remember(at("victim.txt"), 0);
#if defined(_WIN32)
        if (CreateHardLinkA(p, at("victim.txt"), NULL)) {
#else
        if (link(at("victim.txt"), p) == 0) {
#endif
            CHECK(bo3_modfs_set_enabled("b.one", 0), "a save with a hard link at the temporary name");
            CHECK(!strcmp(read_text(at("victim.txt")), "victim") && strstr(read_text(state), "\"b.one\""),
                  "the linked file untouched: %s", read_text(at("victim.txt")));
            CHECK(read_text(b) && !strcmp(read_text(b), "broken [[["), "the bad file kept as .bad");
        } else {
            SKIP("hard link at the temporary name: not supported here");
        }
        CHECK(write_text(state, "disabled = []\n"), "reset the file");
    }

    /* The conflict report names the place a lookup uses, also when a listed
     * file is refused by the walk (a host path too long for one mod). */
    {
        char rel[700], name[300], base[700];
        size_t room;
        fit(base, sizeof base, "%s", at("lw"));
        fit(t, sizeof t, ok_toml, "a.long", "Long");
        fit(rel, sizeof rel, "lw/%s/mod.toml", "llllllllllllllllllllllllllllllllllllllllllllllllllllllllllll");
        CHECK(make_file(rel, t), "setup a.long");
        fit(t, sizeof t, ok_toml, "b.short", "Short");
        CHECK(make_file("lw/b/mod.toml", t), "setup b.short");
        /* a.long's host path 257 characters: over the 255 a lookup takes on
         * Windows, while b's fits */
        room = 257 - (strlen(base) + 1 + 60 + sizeof "\\files\\Data\\" - 1);
        if (room > 4 && room < 200) {
            memset(name, 'n', room - 4);
            memcpy(name + room - 4, ".bin", 5);
            fit(rel, sizeof rel, "lw/%s/files/Data/%s", "llllllllllllllllllllllllllllllllllllllllllllllllllllllllllll", name);
            CHECK(make_file(rel, "long"), "setup long file");
            fit(rel, sizeof rel, "lw/b/files/Data/%s", name);
            CHECK(make_file(rel, "short"), "setup short file");
            init_logged(at("lw"), log, sizeof log);
            fit(rel, sizeof rel, "D:\\Data\\%s", name);
            if (!strcmp(read_back(rel), "short"))
                CHECK(bo3_modfs_conflicts() == 0 && !strstr(log, "[MOD] conflict"), "b.short used: no conflict told:\n%s", log);
            else
                CHECK(!strcmp(read_back(rel), "long") && bo3_modfs_conflicts() == 1 && strstr(log, "mod a.long over mod b.short"),
                      "a.long used: told so:\n%s", log);
        } else {
            SKIP("the temporary folder's path does not leave room for the long-path case");
        }
    }

    /* A mod list path too long for the buffer: none, said so. */
    {
        static char longp[1300];
        memset(longp, 'p', sizeof longp - 1);
        fit(log, sizeof log, "%s", "");
        bo3_modfs_state_file(longp);
        CHECK(!bo3_modfs_set_enabled("b.one", 0), "a path too long: no mod list");
    }

    bo3_modfs_state_file(NULL);
}

int main(void)
{
    char list[4096];
    int sensitive;

    if (!make_root()) {
        fprintf(stderr, "cannot make a temporary folder\n");
        return 1;
    }
    atexit(remove_all);
    sensitive = case_sensitive();
    printf("folders under %s (%s volume)\n", s_root, sensitive ? "case-sensitive" : "case-folding");

    /* Every fixture is checked: a case whose files are not there would pass
     * for the wrong reason. */
    CHECK(make_file("modA/Tracks/US/C3_V1/static.dat", "A static"), "setup modA");
    CHECK(make_dir(at("modA/Tracks/dirfile")), "setup modA/Tracks/dirfile");
    CHECK(make_file("modB/Tracks/US/C3_V1/static.dat", "B static"), "setup modB");
    CHECK(make_file("modB/Data/only_b.bin", "B only"), "setup modB/Data");
    CHECK(make_file("lower/tracks/us/c3_v1/lower.dat", "lower"), "setup lower");
    CHECK(make_file("outside/secret.dat", "outside"), "setup outside");
    CHECK(make_file("plainfile", "not a folder"), "setup plainfile");
    CHECK(make_file("modN/Data/x.dat", "N plain"), "setup modN");

    /* 1. Unset or empty: no hook at all, whatever was there stays. */
    set_mods(NULL);
    CHECK(xbox_PathRedirectHook == NULL, "unset: no hook installed");
    set_mods("");
    CHECK(xbox_PathRedirectHook == NULL, "empty: no hook installed");
    xbox_PathRedirectHook = prev_hook;
    set_mods(NULL);
    CHECK(xbox_PathRedirectHook == prev_hook, "unset: the earlier hook is left alone");
    set_mods(at("missing"));
    CHECK(xbox_PathRedirectHook == prev_hook, "only a missing folder: no hook installed");

    /* 2. Several folders, the first one named wins; a missing folder and a
     *    file named as one are skipped. */
    {
        static const char *const names[] = { "missing", "plainfile", "modA", "modB", "lower" };
        list[0] = 0;
        for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
            fit(list + strlen(list), sizeof list - strlen(list), "%s%s", i ? ";" : "", at(names[i]));
    }
    set_mods(list);
    CHECK(xbox_PathRedirectHook != prev_hook && xbox_PathRedirectHook != NULL, "set: the hook is installed");
    CHECK(!strcmp(read_back("D:\\Tracks\\US\\C3_V1\\static.dat"), "A static"), "first folder wins");
    CHECK(!strcmp(read_back("\\Device\\CdRom0\\Tracks\\US\\C3_V1\\static.dat"), "A static"),
          "\\Device\\CdRom0\\ is the disc too");
    CHECK(!strcmp(read_back("d:\\Tracks\\US\\C3_V1\\static.dat"), "A static"), "d:\\ as well as D:\\");
    CHECK(!strcmp(read_back("D:\\Data\\only_b.bin"), "B only"), "a file only the second folder has");
    CHECK(!strcmp(read_back("D:\\Data/only_b.bin"), "B only"), "'/' in the guest path is a separator");
    {
        const char *h = ask("D:\\Tracks\\US\\C3_V1\\static.dat");
        CHECK(h && handoff_ok(h), "the path handed back fits the toolkit's buffer: %s", h ? h : "(none)");
    }

    /* 3. Case: the folder's own spelling, from any spelling the title uses. */
    CHECK(!strcmp(read_back("d:\\tracks\\us\\c3_v1\\STATIC.DAT"), "A static"), "mixed case finds modA's file");
    CHECK(!strcmp(read_back("D:\\TRACKS\\US\\C3_V1\\LOWER.DAT"), "lower"), "upper case finds a lower-case tree");
    {
        const char *h = ask("D:\\TRACKS\\Us\\C3_v1\\Lower.dat");
        CHECK(h && strstr(h, sensitive ? SEP "tracks" SEP "us" SEP "c3_v1" SEP "lower.dat" : "ower.dat"),
              "the host path has the folder's spelling: %s", h ? h : "(none)");
    }

    /* 4. Missing files, and everything that is not a disc path, go down the
     *    chain; a redirected one does not. */
    s_counting = 1;
    s_prev_calls = 0;
    CHECK(refused("D:\\Tracks\\US\\C3_V1\\missing.dat") && s_prev_calls == 1, "missing file: the disc's (chain asked)");
    CHECK(ask("E:\\chain") && !strcmp(ask("E:\\chain"), "PREV"), "a non-disc path reaches the earlier hook");
    CHECK(refused("T:\\Tracks\\US\\C3_V1\\static.dat"), "T:\\ is not the disc");
    CHECK(refused("D:\\"), "the disc's root itself");
    CHECK(refused(NULL), "no path");
    s_prev_calls = 0;
    ask("D:\\Tracks\\US\\C3_V1\\static.dat");
    CHECK(s_prev_calls == 0, "a redirected path is not passed on");
    s_counting = 0;

    /* 5. Names that are refused, the same on every host. Real files stand
     *    behind the non-ASCII and the control-character name (where the file
     *    system takes them), so only the name rule refuses those. */
    if (!make_file("modA/Tracks/US/C3_V1/st\xc3\xa9.dat", "non-ASCII name"))
        SKIP("a file with a non-ASCII name could not be made here");
#if !defined(_WIN32)
    if (!make_file("modA/Tracks/US/C3_V1/stat\x01ic.dat", "control name"))
        SKIP("a file with a control character in its name could not be made here");
#endif
    {
        static const char *const bad[] = {
            "D:\\..\\outside\\secret.dat",
            "D:\\Tracks\\..\\..\\outside\\secret.dat",
            "D:\\Tracks\\US\\..\\US\\C3_V1\\static.dat",
            "D:\\.\\Tracks\\US\\C3_V1\\static.dat",
            "D:\\Tracks\\\\US\\C3_V1\\static.dat",
            "D:\\\\Tracks\\US\\C3_V1\\static.dat",
            "D:\\/Tracks/US/C3_V1/static.dat",
            "D:\\Tracks\\US\\C3_V1\\static.dat\\",
            "D:\\C:\\Windows\\win.ini",
            "D:\\Tracks\\US\\C3_V1\\static.dat:stream",
            "D:\\Tracks\\US\\C3_V1\\static.dat.",
            "D:\\Tracks\\US\\C3_V1\\static.dat ",
            "D:\\Tracks\\US\\C3_V1\\*.dat",
            "D:\\Tracks\\US\\C3_V1\\stat?c.dat",
            "D:\\Tracks\\US\\C3_V1\\st\"atic.dat",
            "D:\\Tracks\\US\\C3_V1\\st<atic.dat",
            "D:\\Tracks\\US\\C3_V1\\st|atic.dat",
            "D:\\Tracks\\US\\C3_V1\\stat\x01ic.dat",
            "D:\\Tracks\\US\\C3_V1\\st\xc3\xa9.dat",
            "D:\\Tracks",
            "D:\\Tracks\\dirfile",
            "D:\\Tracks\\US\\C3_V1",
        };
        for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++)
            CHECK(refused(bad[i]), "refused: %s", bad[i]);
    }
    {
        char longpart[400], deep[1200];
        memset(longpart, 'a', 300);
        longpart[300] = 0;
        fit(deep, sizeof deep, "D:\\%s", longpart);
        CHECK(refused(deep), "a 300-character name (longer than any file system allows)");
    }

    /* 5b. The longest host path handed back: on Windows 255 characters (the
     *     toolkit puts "\\?\" in front and converts into MAX_PATH wide
     *     characters with the terminator), elsewhere MAX_PATH - 1. Real files
     *     just under, at and just over it: the ones over are refused (the
     *     disc's file is read), the others found and fit the hand-off. */
    {
#if defined(_WIN32)
        const int limit = MAX_PATH - 5, prefix = 4;
#else
        const int limit = MAX_PATH - 1, prefix = 0;
#endif
        char dir[700], name[300], rel[400], guest[400], text[32];
        int lengths[4], made[4];
        fit(dir, sizeof dir, "%s", at("len/Data"));
        for (int i = 0; i < 4; i++) {
            int t = limit - 1 + i, k = t - (int)strlen(dir) - 1;   /* host = dir + SEP + name */
            lengths[i] = t;
            made[i] = 0;
            if (k < 8 || k > 250)
                continue;
            memset(name, 'n', (size_t)k - 4);
            memcpy(name + k - 4, ".bin", 5);
            fit(rel, sizeof rel, "len/Data/%s", name);
            fit(text, sizeof text, "len %d", t);
            made[i] = make_file(rel, text);
        }
        set_mods(at("len"));
        for (int i = 0; i < 4; i++) {
            int t = lengths[i], k = t - (int)strlen(dir) - 1;
            const char *h;
            if (!made[i]) {
                SKIP("host path of %d characters: its file could not be made here", t);
                continue;
            }
            memset(name, 'n', (size_t)k - 4);
            memcpy(name + k - 4, ".bin", 5);
            fit(guest, sizeof guest, "D:\\Data\\%s", name);
            fit(text, sizeof text, "len %d", t);
            h = ask(guest);
            if (t <= limit)
                CHECK(h && (int)strlen(h) == t + prefix && handoff_ok(h) && !strcmp(read_back(guest), text),
                      "host path of %d characters (limit %d): found, fits the hand-off", t, limit);
            else
                CHECK(!h, "host path of %d characters (limit %d): refused, not handed on cut off (%s)",
                      t, limit, h ? h : "none");
        }
    }

    /* 6. Links below a folder are refused; a folder reached through a link
     *    is fine. */
#if defined(_WIN32)
    {
        char cmd[1600];
        CHECK(make_dir(at("modJ")), "setup modJ");
        fit(cmd, sizeof cmd, "mklink /J \"%s\" \"%s\" >NUL 2>&1", at("modJ/Tracks"), at("outside"));
        if (system(cmd) == 0) {
            remember(at("modJ/Tracks"), 2);
            set_mods(at("modJ"));
            /* Positive control: through the junction the file is there. */
            CHECK(GetFileAttributesA(at("modJ/Tracks/secret.dat")) != INVALID_FILE_ATTRIBUTES,
                  "setup: the junction leads to a real file");
            CHECK(refused("D:\\Tracks\\secret.dat"), "a junction below the folder");
        } else {
            SKIP("junction: mklink /J failed");
        }
        SKIP("symbolic links, pipes, unreadable folders: POSIX cases");
    }

    /* Only folders on a drive letter. The same folder by its drive path is
     * the positive control; written as a network share or with \\?\ or \\.\
     * it would be handed back as "\\?\\\..." and name nothing, so it is
     * ignored and the disc's file is read. */
    {
        static const char *const pre[] = { "\\\\?\\", "\\\\.\\", "//?/" };
        char p[900];
        set_mods(at("modA"));
        CHECK(!strcmp(read_back("D:\\Tracks\\US\\C3_V1\\static.dat"), "A static"), "modA by its drive path");
        for (size_t i = 0; i < sizeof pre / sizeof pre[0]; i++) {
            fit(p, sizeof p, "%s%s", pre[i], at("modA"));
            set_mods(p);
            CHECK(refused("D:\\Tracks\\US\\C3_V1\\static.dat"), "ignored: %s", p);
        }
        fit(p, sizeof p, "\\\\localhost\\%c$%s", at("modA")[0], at("modA") + 2);
        set_mods(p);
        CHECK(refused("D:\\Tracks\\US\\C3_V1\\static.dat"), "ignored (network share): %s", p);
        set_mods("\\\\no-such-host.invalid\\share\\mods");
        CHECK(refused("D:\\Tracks\\US\\C3_V1\\static.dat"), "ignored (network share, without asking the network)");
        fit(p, sizeof p, "\\\\localhost\\%c$%s;%s", at("modA")[0], at("modA") + 2, at("modB"));
        set_mods(p);
        CHECK(!strcmp(read_back("D:\\Tracks\\US\\C3_V1\\static.dat"), "B static"),
              "an ignored share does not stop the next folder");
    }
#else
    {
        struct stat st;
        int ok;
        CHECK(make_dir(at("modL")) && make_dir(at("modL/Data")), "setup modL");
        CHECK(make_file("modL/Data/real.dat", "L real"), "setup modL/Data/real.dat");
        fit(list, sizeof list, "%s", at("modL"));
        /* Each link is made, then shown to reach a real file when followed
         * (what the lookup would return without the rule), then refused. */
        ok = symlink(at("outside"), at("modL/Tracks")) == 0;
        if (ok)
            remember(at("modL/Tracks"), 2);
        if (!ok) {
            SKIP("a folder link: symlink failed (%s)", strerror(errno));
        } else {
            set_mods(list);
            CHECK(stat(at("modL/Tracks/secret.dat"), &st) == 0, "setup: the folder link leads to a real file");
            CHECK(refused("D:\\Tracks\\secret.dat"), "a folder link below the folder");
            CHECK(refused("D:\\tracks\\secret.dat"), "a folder link found by case");
        }
        ok = symlink(at("outside/secret.dat"), at("modL/Data/link.dat")) == 0;
        if (ok)
            remember(at("modL/Data/link.dat"), 2);
        if (!ok) {
            SKIP("a file link: symlink failed (%s)", strerror(errno));
        } else {
            set_mods(list);
            CHECK(stat(at("modL/Data/link.dat"), &st) == 0 && S_ISREG(st.st_mode), "setup: the file link leads to a file");
            CHECK(refused("D:\\Data\\link.dat"), "a file link (absolute)");
        }
        ok = symlink("../../outside/secret.dat", at("modL/Data/rel.dat")) == 0;
        if (ok)
            remember(at("modL/Data/rel.dat"), 2);
        if (!ok) {
            SKIP("a relative file link: symlink failed (%s)", strerror(errno));
        } else {
            set_mods(list);
            CHECK(stat(at("modL/Data/rel.dat"), &st) == 0 && S_ISREG(st.st_mode), "setup: the relative link leads to a file");
            CHECK(refused("D:\\Data\\rel.dat"), "a file link (relative, ../..)");
        }
        ok = mkfifo(at("modL/Data/pipe.dat"), 0644) == 0;
        if (ok)
            remember(at("modL/Data/pipe.dat"), 0);
        if (!ok) {
            SKIP("a pipe: mkfifo failed (%s)", strerror(errno));
        } else {
            set_mods(list);
            CHECK(lstat(at("modL/Data/pipe.dat"), &st) == 0 && S_ISFIFO(st.st_mode), "setup: the pipe is there");
            CHECK(refused("D:\\Data\\pipe.dat"), "a pipe (and the lookup did not block on it)");
        }
        set_mods(list);
        CHECK(!strcmp(read_back("D:\\Data\\real.dat"), "L real"), "a real file beside them");
        ok = symlink(at("modA"), at("rootlink")) == 0;
        if (ok)
            remember(at("rootlink"), 2);
        if (!ok) {
            SKIP("a folder named through a link: symlink failed (%s)", strerror(errno));
        } else {
            set_mods(at("rootlink"));
            CHECK(!strcmp(read_back("D:\\Tracks\\US\\C3_V1\\static.dat"), "A static"), "a folder named through a link");
            {
                const char *h = ask("D:\\Tracks\\US\\C3_V1\\static.dat");
                CHECK(h && strstr(h, SEP "modA" SEP), "the link is resolved once, at the start: %s", h ? h : "(none)");
            }
        }

        CHECK(make_file("modP/Data/x.dat", "P"), "setup modP");
        if (geteuid() == 0) {
            SKIP("unreadable folder: running as root, permissions do not stop it");
        } else if (chmod(at("modP/Data"), 0) == 0) {
            for (int i = s_nmade - 1; i >= 0; i--)
                if (!strcmp(s_made[i], at("modP/Data")))
                    s_kind[i] = 3;
            set_mods(at("modP"));
            CHECK(refused("D:\\Data\\x.dat"), "an unreadable folder: refused, no crash");
            CHECK(refused("D:\\data\\x.dat"), "an unreadable folder by case: refused");
            chmod(at("modP/Data"), 0755);
        }

        /* A '\' in a folder's name: the same tree as modN, which is found. */
        if (!make_file("mod\\x/Data/x.dat", "backslash")) {
            SKIP("a folder with a '\\': it could not be made (%s)", strerror(errno));
        } else {
            set_mods(at("modN"));
            CHECK(!strcmp(read_back("D:\\Data\\x.dat"), "N plain"), "the plain twin modN is found");
            set_mods(at("mod\\x"));
            CHECK(refused("D:\\Data\\x.dat"), "a folder with a '\\' is ignored");
        }
    }
#endif

    /* A folder whose path is not plain ASCII, with a real file in it: the
     * same tree as modN (found), so only the folder's name stops it. */
    {
#if defined(_WIN32)
        const char *na = "mod\xe9";           /* the ANSI code page's e-acute */
#else
        const char *na = "mod\xc3\xa9";       /* UTF-8 */
#endif
        char rel[64];
        fit(rel, sizeof rel, "%s/Data/x.dat", na);
        if (!make_file(rel, "non-ASCII")) {
            SKIP("a non-ASCII folder: it could not be made here");
        } else {
            set_mods(at("modN"));
            CHECK(!strcmp(read_back("D:\\Data\\x.dat"), "N plain"), "the plain twin modN is found");
            set_mods(at(na));
            CHECK(refused("D:\\Data\\x.dat"), "a non-ASCII folder is ignored");
        }
    }

    /* 7. Two names that differ only in case: the one as written, else none. */
    if (sensitive) {
        make_file("amb/Data/x.bin", "amb Data");
        make_file("amb/DATA/x.bin", "amb DATA");
        make_file("amb/Exact/Y.bin", "Y");
        make_file("amb/Exact/y.bin", "y");
        fit(list, sizeof list, "%s;%s", at("amb"), at("modB"));
        set_mods(list);
        CHECK(refused("D:\\data\\x.bin"), "Data and DATA for data: refused");
        CHECK(refused("D:\\data\\only_b.bin"), "...and not taken from a later folder either");
        CHECK(!strcmp(read_back("D:\\Data\\x.bin"), "amb Data"), "Data as written");
        CHECK(!strcmp(read_back("D:\\DATA\\x.bin"), "amb DATA"), "DATA as written");
        CHECK(!strcmp(read_back("D:\\Exact\\y.bin"), "y") && !strcmp(read_back("D:\\Exact\\Y.bin"), "Y"),
              "files: as written");
        CHECK(refused("D:\\exact\\Y.BIN"), "Y.bin and y.bin for Y.BIN: refused");
        {
            int same = 1;
            for (int i = 0; i < 200; i++)
                same &= refused("D:\\data\\x.bin") && !strcmp(read_back("D:\\Data\\x.bin"), "amb Data");
            CHECK(same, "the same answer every time");
        }
    } else {
        SKIP("names that differ only in case: this volume folds case");
    }

    /* 8. The list: ';' only, empty items, trailing separators, a relative
     *    folder, more than eight. */
    fit(list, sizeof list, ";;%s" SEP SEP ";;", at("modB"));
    set_mods(list);
    CHECK(!strcmp(read_back("D:\\Tracks\\US\\C3_V1\\static.dat"), "B static"), "empty items and trailing separators");
    fit(list, sizeof list, "%s:%s", at("modA"), at("modB"));
    set_mods(list);
    CHECK(refused("D:\\Tracks\\US\\C3_V1\\static.dat"), "':' does not separate folders");
    {
        char here[1024];
#if defined(_WIN32)
        if (_getcwd(here, sizeof here) && _chdir(s_root) == 0) {
            set_mods("modB");
            _chdir(here);
#else
        if (getcwd(here, sizeof here) && chdir(s_root) == 0) {
            set_mods("modB");
            if (chdir(here) != 0)
                perror("chdir");
#endif
            CHECK(!strcmp(read_back("D:\\Data\\only_b.bin"), "B only"), "a relative folder, taken at the start");
        }
    }
    list[0] = 0;
    for (int i = 0; i < 8; i++)
        fit(list + strlen(list), sizeof list - strlen(list), "%s;", at("modB"));
    fit(list + strlen(list), sizeof list - strlen(list), "%s", at("modA"));
    set_mods(list);
    CHECK(!strcmp(read_back("D:\\Tracks\\US\\C3_V1\\static.dat"), "B static"), "a ninth folder is ignored");
    /* The limit itself: eight folders, then a ninth that alone has the file.
     * A folder named again keeps its place among the eight. */
    CHECK(make_file("nine/f9/Data/ninth.bin", "ninth"), "setup nine");
    list[0] = 0;
    for (int i = 1; i <= 8; i++) {
        char d[32];
        snprintf(d, sizeof d, "nine/f%d", i);
        CHECK(make_dir(at(d)), "setup %s", d);
        fit(list + strlen(list), sizeof list - strlen(list), "%s;", at(d));
    }
    fit(list + strlen(list), sizeof list - strlen(list), "%s", at("nine/f9"));
    set_mods(list);
    CHECK(refused("D:\\Data\\ninth.bin"), "eight folders, then a ninth: ignored");
    list[0] = 0;
    for (int i = 0; i < 8; i++)
        fit(list + strlen(list), sizeof list - strlen(list), "%s;", at("nine/f1"));
    fit(list + strlen(list), sizeof list - strlen(list), "%s", at("nine/f9"));
    set_mods(list);
    CHECK(refused("D:\\Data\\ninth.bin"), "one folder named eight times, then a ninth: ignored");
    fit(list, sizeof list, "%s;%s", at("nine/f1"), at("nine/f9"));
    set_mods(list);
    CHECK(!strcmp(read_back("D:\\Data\\ninth.bin"), "ninth"), "the ninth folder's file, when it is the second");

    /* 9. Threads: each gets its own answer. */
    fit(list, sizeof list, "%s;%s", at("modA"), at("modB"));
    set_mods(list);
    run_threads(thread_main);
    {
        int bad = 0;
        for (int k = 0; k < THREADS; k++)
            bad += s_thread_bad[k];
        CHECK(bad == 0, "%d threads x %d lookups: %d wrong", THREADS, ROUNDS, bad);
    }

    /* 10. What a lookup costs (not checked: printed for the record). */
    {
        static const char *const kind[] = { "hit, as written", "hit, by case", "miss, first part",
                                             "miss, last part" };
        static const char *const guest[] = { "D:\\Tracks\\US\\C3_V1\\static.dat", "D:\\TRACKS\\us\\c3_v1\\Static.Dat",
                                             "D:\\Sound\\DSP\\B3XboxFx.bin", "D:\\Tracks\\US\\C3_V1\\enviro.dat" };
        fit(list, sizeof list, "%s;%s;%s", at("modA"), at("modB"), at("lower"));
        set_mods(list);
        for (int g = 0; g < 4; g++) {
            double t0 = now_us();
            for (int i = 0; i < 2000; i++)
                ask(guest[g]);
            printf("cost: %-17s %6.2f us a lookup (3 folders)\n", kind[g], (now_us() - t0) / 2000);
        }
    }

    /* 11. What a release build says. The print gate drops plain stderr lines
     *     while diagnostic logging is off (g_xr_log_diag 0, what xr_log_init
     *     sets for a release build): the folders taken and ignored must still
     *     be there (XR_NOTICE), the per-file lines only with diagnostics. */
    {
        static char log[8192];
        char path[700];
        fit(path, sizeof path, "%s", at("log_release.txt"));
        fit(list, sizeof list, "%s;%s", at("missing"), at("modA"));
        g_xr_log_diag = 0;
        if (!capture_start(path)) {
            g_xr_log_diag = 1;
            SKIP("release log: stderr could not be captured");
        } else {
            set_mods(list);
            ask("D:\\Tracks\\US\\C3_V1\\static.dat");
#if defined(_WIN32)
            set_mods("\\\\no-such-host.invalid\\share\\mods");
#endif
            capture_end(path, log, sizeof log);
            g_xr_log_diag = 1;
            CHECK(strstr(log, "[MOD] ignored (not a folder): "), "release: an ignored folder is said:\n%s", log);
            CHECK(strstr(log, "[MOD] folder 1: "), "release: the folder taken is said:\n%s", log);
#if defined(_WIN32)
            CHECK(strstr(log, "[MOD] ignored (not on a drive letter"), "release: a network share is said:\n%s", log);
#endif
            CHECK(!strstr(log, "static.dat -> "), "release: no per-file line without diagnostics:\n%s", log);
        }
        fit(path, sizeof path, "%s", at("log_diag.txt"));
        if (capture_start(path)) {
            set_mods(list);   /* a new start: the log-once list is empty again */
            ask("D:\\Tracks\\US\\C3_V1\\static.dat");
            capture_end(path, log, sizeof log);
            CHECK(strstr(log, "[MOD] folder 1: ") && strstr(log, "static.dat -> "),
                  "diagnostics on: the folder and the per-file line:\n%s", log);
        } else {
            SKIP("diagnostic log: stderr could not be captured");
        }
    }

    manifests(sensitive);
    mod_list();

    remove_all();
    printf("modfs: %d passed, %d failed, %d skipped\n", s_pass, s_fail, s_skip);
    return s_fail ? 1 : 0;
}
