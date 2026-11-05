/*
 * upscale.c - sharper menu pictures; see upscale.h and docs/upscale-art.md.
 *
 * One run, three parts, each said with PROGRESS lines of the one step
 * "upscale"; the upscaler and its models come with the package (below):
 *
 *   read     Data/LoadScrn.bin, Data/Frontend.txd and the menus' few in
 *            Data/Global.txd (the main menu's logo, medals) of the installed game,
 *            each picture decoded to a PNG named by its texture-pack key
 *            (upscale_art.c), into <cache>/upscale/work/in-<model>.
 *   upscale  the upscaler once per model over its folder (one process: its
 *            start costs a few seconds), the progress from the files it has
 *            finished.
 *   pack     the results into a texture-pack mod, written beside the old one
 *            and renamed into place, then the work folder (the decoded disc
 *            art) is deleted.
 *
 * Stopped (the cancel file, a signal), a run leaves what the upscaler
 * finished in the work folder, and the next run starts from there.
 *
 * Android (BO-326): an app cannot start a program of its own, so the run is
 * a thread of the game (the MODS page's rows) or of the setup page, its lines
 * handed to them instead of stdout (s_sink), and the model runs in the
 * process with ncnn linked in (upscale_ncnn.cpp; the tiles, upscale_tile.c,
 * are realesrgan-ncnn-vulkan's own): the fast model only, carried in the
 * APK's assets/upscaler, on the CPU unless the game runs on the app's own
 * Turnip driver. Everything else (the disc's pictures, their keys, the pack,
 * putting it into the running game) is the desktop's code.
 */
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include "upscale.h"
#include "upscale_art.h"
#include "install.h"                    /* BO3_USER_DIR */

#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#if defined(_WIN32)
#define COBJMACROS
#include <windows.h>
#include <dxgi.h>
#include <direct.h>
#include <io.h>
#else
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif

#if defined(__ANDROID__) && defined(BO3_HAVE_NCNN)
#define BO3_UPSCALE_INPROC 1            /* the model in this process (above) */
#endif
#if defined(BO3_UPSCALE_INPROC)
#include <dlfcn.h>
#include <jni.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <SDL.h>
#include <SDL_system.h>
#include "upscale_ncnn.h"
#include "upscale_tile.h"
#endif

#include "kernel/nv2a_texpack.h"
#include "platform/recomp_save_dir.h"
#include "platform/recomp_exe_dir.h"
#include "platform/recomp_env.h"        /* recomp_host_is_steam_deck */
#include "platform/xr_log.h"            /* XR_NOTICE */

#define STEP "upscale"
/* The pack this makes: the menu pictures. (An experimental second pack of the
 * race HUD's pictures, ai-upscaled-hud, was dropped on 2026-10-30: one made
 * by a test build is deleted at the start, bo3_upscale_retire_packs.) */
struct kind {
    const char *id, *dir, *work, *name, *what;
};
#define NKINDS 1
static const struct kind k_kinds[NKINDS] = {
    { "takedawn.ai-upscaled-art", "ai-upscaled-art", "work", "Sharper menu pictures (AI upscale)",
      "The loading screens and menu pictures of your disc" },
};
/* Packs earlier test builds made that no build loads any more. */
static const char *const k_retired[] = { "ai-upscaled-hud", NULL };
static const struct kind *K = &k_kinds[0];
#define PACK_ID (K->id)
#define MAX_TEX 600

/* ── the upscaler, in the package ─────────────────────────────────────── */

/* Real-ESRGAN v0.2.5.0's realesrgan-ncnn-vulkan (MIT; on ncnn, BSD-3-Clause,
 * and on macOS MoltenVK, Apache-2.0) with RealESRGAN_x4plus and the fast
 * model (realesr-general-x4v3 at denoise 0.3; Real-ESRGAN's weights,
 * BSD-3-Clause), carried by every desktop package (tools/release, pins.env
 * UPSCALER_*): nothing is downloaded. Where:
 *
 *   Linux, Windows  <the program's folder>/upscaler/<the upscaler>, .../upscaler/models
 *   macOS           Takedawn.app/Contents/MacOS/<the upscaler>, Contents/Resources/upscaler/models
 *
 * TAKEDAWN_UPSCALE_ENGINE=<dir> or --engine <dir>: a folder with the
 * upscaler and models/ (a developer build). */
#if defined(BO3_UPSCALE_INPROC)
#define ENGINE_VERSION "ncnn in the app"
#else
#define ENGINE_VERSION "realesrgan-ncnn-vulkan-20220424"
#endif
#if defined(_WIN32)
#define ENGINE_EXE "realesrgan-ncnn-vulkan.exe"
#else
#define ENGINE_EXE "realesrgan-ncnn-vulkan"
#endif

static const char *const k_model_name[2] = { "realesrgan-x4plus", "realesr-general-dn030-x4v3" };
/* Seconds per million source pixels on an Apple M5, for the progress bar
 * and the estimate only. */
static const double k_model_cost[2] = { 25.0, 4.0 };

/* ── small things ─────────────────────────────────────────────────────── */

/* Where the run's lines go: stdout (the command), or this function (a run
 * inside the game or the setup page, Android), one line at a time without
 * its newline. */
static void (*s_sink)(void *ctx, const char *line);
static void *s_sink_ctx;

static void out(const char *fmt, ...)
{
    va_list ap;
    if (s_sink) {
        char line[1800];
        va_start(ap, fmt);
        vsnprintf(line, sizeof line, fmt, ap);
        va_end(ap);
        s_sink(s_sink_ctx, line);
        return;
    }
    va_start(ap, fmt);
    (vfprintf)(stdout, fmt, ap);
    va_end(ap);
    (fputc)('\n', stdout);
    fflush(stdout);
}

static int exists(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0;
}

static long long file_size(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 ? (long long)st.st_size : -1;
}

static int make_dir(const char *p)
{
#if defined(_WIN32)
    return _mkdir(p) == 0 || errno == EEXIST;
#else
    return mkdir(p, 0755) == 0 || errno == EEXIST;
#endif
}

/* Every folder of a path, as mkdir -p. */
static int make_dirs(const char *path)
{
    char p[1100];
    size_t i;
    if (snprintf(p, sizeof p, "%s", path) >= (int)sizeof p)
        return 0;
    for (i = 1; p[i]; i++)
        if (p[i] == '/' || p[i] == '\\') {
            char c = p[i];
#if defined(_WIN32)
            if (i == 2 && p[1] == ':')
                continue;
#endif
            p[i] = 0;
            make_dir(p);
            p[i] = c;
        }
    return make_dir(p);
}

/* A folder and what is in it. Links are removed, never followed. */
static void rmtree(const char *p)
{
    struct stat st;
    DIR *d;
    struct dirent *e;
    char sub[1200];
#if defined(_WIN32)
    if (stat(p, &st) != 0)
        return;
    if (!S_ISDIR(st.st_mode) || (GetFileAttributesA(p) & FILE_ATTRIBUTE_REPARSE_POINT)) {
        if (S_ISDIR(st.st_mode))
            _rmdir(p);
        else
            remove(p);
        return;
    }
#else
    if (lstat(p, &st) != 0)
        return;
    if (!S_ISDIR(st.st_mode)) {
        remove(p);
        return;
    }
#endif
    if ((d = opendir(p)) != NULL) {
        while ((e = readdir(d)) != NULL) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
                continue;
            if (snprintf(sub, sizeof sub, "%s/%s", p, e->d_name) < (int)sizeof sub)
                rmtree(sub);
        }
        closedir(d);
    }
#if defined(_WIN32)
    _rmdir(p);
#else
    rmdir(p);
#endif
}

static int move_file(const char *from, const char *to)
{
    FILE *a, *b;
    char buf[65536];
    size_t n;
    int ok = 1;
    remove(to);
    if (rename(from, to) == 0)
        return 1;
    /* Another volume: copied. */
    if (!(a = fopen(from, "rb")))
        return 0;
    if (!(b = fopen(to, "wb"))) {
        fclose(a);
        return 0;
    }
    while ((n = fread(buf, 1, sizeof buf, a)) > 0)
        if (fwrite(buf, 1, n, b) != n)
            ok = 0;
    fclose(a);
    if (fclose(b) != 0)
        ok = 0;
    if (ok)
        remove(from);
    else
        remove(to);
    return ok;
}

static int write_text(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    int ok;
    if (!f)
        return 0;
    ok = fputs(text, f) >= 0;
    return fclose(f) == 0 && ok;
}

static uint8_t *slurp(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    uint8_t *b = NULL;
    long size;
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) == 0 && (size = ftell(f)) > 0 && fseek(f, 0, SEEK_SET) == 0
            && (b = (uint8_t *)malloc((size_t)size + 1)) != NULL
            && fread(b, 1, (size_t)size, f) != (size_t)size) {
        free(b);
        b = NULL;
    }
    if (b) {
        b[size] = 0;                    /* a .param is read as text */
        *n = (size_t)size;
    }
    fclose(f);
    return b;
}

static void sleep_ms(unsigned ms)
{
#if defined(_WIN32)
    Sleep(ms);
#else
    struct timespec ts = { (time_t)(ms / 1000), (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
#endif
}

/* ── the folders ──────────────────────────────────────────────────────── */

static int cache_root(char *out_dir, size_t n)
{
    char base[1024];
    const char *e = getenv("TAKEDAWN_UPSCALE_CACHE");
#if defined(__ANDROID__)
    /* The app's cache folder is the system's to empty when space runs short;
     * a run's few hundred MB go beside the made mods instead
     * (<files>/takedawn/upscale). */
    const int kind = RECOMP_DIR_LOCAL;
#else
    const int kind = RECOMP_DIR_CACHE;
#endif
    if (e && *e)
        return snprintf(out_dir, n, "%s", e) < (int)n;
    if (!recomp_user_dir(kind, BO3_USER_DIR, base, sizeof base))
        return 0;
    return snprintf(out_dir, n, "%s/upscale", base) < (int)n;
}

int bo3_generated_mods_dir(char *out_dir, size_t n)
{
    char base[1024];
    const char *e = getenv("TAKEDAWN_GENERATED_MODS");
    if (e)
        return *e && snprintf(out_dir, n, "%s", e) < (int)n;
    if (!recomp_user_dir(RECOMP_DIR_LOCAL, BO3_USER_DIR, base, sizeof base))
        return 0;
    return snprintf(out_dir, n, "%s/generated-mods", base) < (int)n;
}

/* The setup window's offer was turned down: a file beside the made-mods
 * folder (not in it: that folder would become a mod folder with no mod). */
static int declined_path(char *out_path, size_t n)
{
    char gen[1024];
    return bo3_generated_mods_dir(gen, sizeof gen) && snprintf(out_path, n, "%s.upscale-declined", gen) < (int)n;
}

const char *bo3_upscale_pack_state(void)
{
    char gen[1024], p[1200];
    struct stat st;
    if (bo3_generated_mods_dir(gen, sizeof gen) && snprintf(p, sizeof p, "%s/%s/made.txt", gen, k_kinds[0].dir) < (int)sizeof p
            && stat(p, &st) == 0)
        return "made";
    if (declined_path(p, sizeof p) && stat(p, &st) == 0)
        return "declined";
    return "none";
}

void bo3_upscale_retire_packs(void)
{
    char gen[1024], dir[1200];
    struct stat st;
    int i;
    if (!bo3_generated_mods_dir(gen, sizeof gen))
        return;
    for (i = 0; k_retired[i]; i++) {
        if (snprintf(dir, sizeof dir, "%s/%s", gen, k_retired[i]) >= (int)sizeof dir || stat(dir, &st) != 0)
            continue;
        rmtree(dir);
        XR_NOTICE("[UPSCALE] removed %s: a pack earlier test builds made, no longer used\n", dir);
    }
}

/* ── the offer ────────────────────────────────────────────────────────── */

#if defined(__linux__) && !defined(__ANDROID__)
static unsigned long long read_ull(const char *path, int base)
{
    FILE *f = fopen(path, "r");
    char line[64] = "";
    if (!f)
        return 0;
    if (!fgets(line, sizeof line, f))
        line[0] = 0;
    fclose(f);
    return strtoull(line, NULL, base);
}

/* A graphics card of its own: NVIDIA, or AMD with memory of its own. amdgpu
 * names the maker of the card's memory (mem_info_vram_vendor) only on a
 * discrete card; an APU has none, whatever carve-out it reports (a Barcelo
 * laptop chip says 4 GB of "VRAM"). */
static int linux_discrete(char *what, size_t n)
{
    DIR *d = opendir("/sys/class/drm");
    struct dirent *e;
    int found = 0;
    if (!d)
        return 0;
    while (!found && (e = readdir(d)) != NULL) {
        char p[512];
        unsigned long long vendor;
        struct stat st;
        int amd_own = 0;
        if (strncmp(e->d_name, "card", 4) || strchr(e->d_name, '-'))
            continue;
        snprintf(p, sizeof p, "/sys/class/drm/%s/device/vendor", e->d_name);
        vendor = read_ull(p, 16);
        snprintf(p, sizeof p, "/sys/class/drm/%s/device/mem_info_vram_vendor", e->d_name);
        amd_own = vendor == 0x1002 && stat(p, &st) == 0;
        if (vendor == 0x10de || amd_own) {
            snprintf(what, n, "%s graphics card", vendor == 0x10de ? "NVIDIA" : "AMD");
            found = 1;
        }
    }
    closedir(d);
    return found;
}
#endif

#if defined(_WIN32)
static int windows_discrete(char *what, size_t n)
{
    typedef HRESULT (WINAPI *create_fn)(REFIID, void **);
    HMODULE dll = LoadLibraryA("dxgi.dll");
    create_fn create = dll ? (create_fn)(void *)GetProcAddress(dll, "CreateDXGIFactory1") : NULL;
    IDXGIFactory1 *f = NULL;
    IDXGIAdapter1 *a = NULL;
    UINT i;
    int found = 0;
    /* IID_IDXGIFactory1, here so that no import library is needed. */
    static const GUID iid = { 0x770aae78, 0xf26f, 0x4dba, { 0xa8, 0x29, 0x25, 0x3c, 0x83, 0xd1, 0xb3, 0x87 } };
    if (!create || FAILED(create(&iid, (void **)&f)) || !f)
        return 0;
    for (i = 0; !found && IDXGIFactory1_EnumAdapters1(f, i, &a) != DXGI_ERROR_NOT_FOUND; i++) {
        DXGI_ADAPTER_DESC1 desc;
        if (SUCCEEDED(IDXGIAdapter1_GetDesc1(a, &desc)) && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
                && desc.DedicatedVideoMemory >= ((SIZE_T)2 << 30)) {
            snprintf(what, n, "a graphics card with %u MB", (unsigned)(desc.DedicatedVideoMemory >> 20));
            found = 1;
        }
        IDXGIAdapter1_Release(a);
    }
    IDXGIFactory1_Release(f);
    return found;
}
#endif

int bo3_upscale_offer(char *quality, size_t qn, char *why, size_t wn)
{
    const char *e = getenv("TAKEDAWN_UPSCALE_OFFER");
    char what[96] = "";
    if (e && *e) {
        int v = !strncmp(e, "on", 2) ? 2 : !strncmp(e, "off", 3) ? 1 : 0;
        const char *q = strchr(e, ':');
        snprintf(quality, qn, "%s", q && !strcmp(q + 1, "fast") ? "fast" : v == 2 ? "best" : "fast");
        snprintf(why, wn, "TAKEDAWN_UPSCALE_OFFER");
        return v;
    }
#if defined(__ANDROID__)
    /* The fast model only, in the app; shown, not ticked: on the CPU it is
     * five to fifteen minutes of a handheld's battery. */
    snprintf(quality, qn, "fast");
#if defined(BO3_UPSCALE_INPROC)
    snprintf(why, wn, "Android, the fast model in the app");
    return 1;
#else
    snprintf(why, wn, "this Android build has no upscaler");
    return 0;
#endif
#elif defined(__APPLE__)
    {
        int arm = 0;
        size_t len = sizeof arm;
        if (sysctlbyname("hw.optional.arm64", &arm, &len, NULL, 0) != 0)
            arm = 0;
        snprintf(quality, qn, arm ? "best" : "fast");
        snprintf(why, wn, arm ? "Apple silicon" : "Intel Mac");
        return arm ? 2 : 1;
    }
#elif defined(_WIN32)
    if (windows_discrete(what, sizeof what)) {
        snprintf(quality, qn, "best");
        snprintf(why, wn, "%s", what);
        return 2;
    }
    snprintf(quality, qn, "fast");
    snprintf(why, wn, "integrated graphics");
    return 1;
#else
    if (recomp_host_is_steam_deck()) {
        snprintf(quality, qn, "fast");
        snprintf(why, wn, "Steam Deck");
        return 1;
    }
    if (linux_discrete(what, sizeof what)) {
        snprintf(quality, qn, "best");
        snprintf(why, wn, "%s", what);
        return 2;
    }
    snprintf(quality, qn, "fast");
    snprintf(why, wn, "integrated graphics");
    return 1;
#endif
}

#if !defined(__ANDROID__) || defined(BO3_UPSCALE_INPROC)

#if !defined(BO3_UPSCALE_INPROC)
/* ── child processes ──────────────────────────────────────────────────── */

typedef struct child {
#if defined(_WIN32)
    HANDLE proc;
#else
    pid_t pid;
#endif
    int running;
} child;

/* argv[0] is the program (a full path); stdout and stderr go to
 * log (appended). 1 started. */
static int child_start(child *c, const char *const *argv, const char *log)
{
    memset(c, 0, sizeof *c);
#if defined(_WIN32)
    {
        char cmd[8192];
        wchar_t wcmd[8192], wlog[1100];
        size_t k = 0;
        int i;
        STARTUPINFOW si;
        PROCESS_INFORMATION pi;
        SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
        HANDLE h;
        for (i = 0; argv[i]; i++) {
            const char *p;
            if (k + 4 >= sizeof cmd)
                return 0;
            if (i)
                cmd[k++] = ' ';
            cmd[k++] = '"';
            for (p = argv[i]; *p && k + 3 < sizeof cmd; p++)
                cmd[k++] = *p;          /* no argument here holds a quote */
            cmd[k++] = '"';
        }
        cmd[k] = 0;
        if (!MultiByteToWideChar(CP_UTF8, 0, cmd, -1, wcmd, 8192)
                || !MultiByteToWideChar(CP_UTF8, 0, log, -1, wlog, 1100))
            return 0;
        h = CreateFileW(wlog, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL, NULL);
        memset(&si, 0, sizeof si);
        si.cb = sizeof si;
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = INVALID_HANDLE_VALUE;
        si.hStdOutput = h;
        si.hStdError = h;
        if (!CreateProcessW(NULL, wcmd, NULL, NULL, TRUE, CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS,
                            NULL, NULL, &si, &pi)) {
            if (h != INVALID_HANDLE_VALUE)
                CloseHandle(h);
            return 0;
        }
        if (h != INVALID_HANDLE_VALUE)
            CloseHandle(h);
        CloseHandle(pi.hThread);
        c->proc = pi.hProcess;
    }
#else
    {
        pid_t pid = fork();
        if (pid < 0)
            return 0;
        if (pid == 0) {
            int fd = open(log, O_WRONLY | O_CREAT | O_APPEND, 0644), nul = open("/dev/null", O_RDONLY);
            if (fd >= 0) {
                dup2(fd, 1);
                dup2(fd, 2);
            }
            if (nul >= 0)
                dup2(nul, 0);
            /* An AppImage's own libraries are not for these programs: they
             * take the system's (the Vulkan loader). */
            if (getenv("APPDIR")) {
                unsetenv("LD_LIBRARY_PATH");
                unsetenv("LD_PRELOAD");
            }
            if (nice(5) == -1) {
                /* not lowered: no matter */
            }
            execvp(argv[0], (char *const *)argv);
            _exit(127);
        }
        c->pid = pid;
    }
#endif
    c->running = 1;
    return 1;
}

/* 1 when it has ended, with its status in *code (-1 for a signal). */
static int child_done(child *c, int *code)
{
    if (!c->running)
        return 1;
#if defined(_WIN32)
    if (WaitForSingleObject(c->proc, 0) != WAIT_OBJECT_0)
        return 0;
    {
        DWORD ec = 1;
        GetExitCodeProcess(c->proc, &ec);
        CloseHandle(c->proc);
        *code = (int)ec;
    }
#else
    {
        int st = 0;
        pid_t r = waitpid(c->pid, &st, WNOHANG);
        if (r == 0 || (r < 0 && errno == EINTR))
            return 0;
        *code = r < 0 ? -1 : WIFEXITED(st) ? WEXITSTATUS(st) : -1;
    }
#endif
    c->running = 0;
    return 1;
}

static void child_kill(child *c)
{
    int code;
    if (!c->running)
        return;
#if defined(_WIN32)
    TerminateProcess(c->proc, 1);
    WaitForSingleObject(c->proc, 5000);
#else
    kill(c->pid, SIGTERM);
    {
        int i;
        for (i = 0; i < 40 && !child_done(c, &code); i++)
            sleep_ms(50);
        if (c->running)
            kill(c->pid, SIGKILL);
    }
#endif
    while (!child_done(c, &code))
        sleep_ms(20);
}
#endif /* !BO3_UPSCALE_INPROC */

/* ── the run's state ──────────────────────────────────────────────────── */

static char s_cache[1024], s_work[1100], s_engine_exe[1300], s_models[1300], s_cancel[1200];
static volatile sig_atomic_t s_signalled;

static void on_signal(int sig)
{
    (void)sig;
    s_signalled = 1;
}

static int cancelled(void)
{
    return s_signalled || exists(s_cancel);
}

#if !defined(BO3_UPSCALE_INPROC)
/* The last lines of a log, on one line, for a FAIL. */
static void log_tail(const char *path, char *dst, size_t n)
{
    size_t len = 0, start, i, k = 0;
    uint8_t *b = slurp(path, &len);
    dst[0] = 0;
    if (!b)
        return;
    start = len > 300 ? len - 300 : 0;
    for (i = start; i < len && k + 1 < n; i++)
        dst[k++] = b[i] == '\n' || b[i] == '\r' ? ' ' : (char)b[i];
    dst[k] = 0;
    free(b);
}
#endif

/* ── the upscaler ─────────────────────────────────────────────────────── */

/* The progress of a part of the run: [p0, p1) of the bar. */
static void progress(double p0, double p1, double f, const char *text)
{
    if (f < 0) f = 0;
    if (f > 1) f = 1;
    out("PROGRESS " STEP " %.3f %s", p0 + (p1 - p0) * f, text);
}

#if !defined(BO3_UPSCALE_INPROC)
static int engine_ok(const char *exe, const char *models)
{
    char p[1300];
    int m;
    if (!exists(exe))
        return 0;
    for (m = 0; m < 2; m++) {
        snprintf(p, sizeof p, "%s/%s.bin", models, k_model_name[m]);
        if (!exists(p))
            return 0;
        snprintf(p, sizeof p, "%s/%s.param", models, k_model_name[m]);
        if (!exists(p))
            return 0;
    }
    return 1;
}

/* The upscaler and its models: a folder named on the command line or by
 * TAKEDAWN_UPSCALE_ENGINE, else the package's. 1 found (s_engine_exe,
 * s_models), 0 with code/why. */
static int find_engine(const char *engine_dir, char *code, size_t cn, char *why, size_t wn)
{
    const char *e = engine_dir && *engine_dir ? engine_dir : getenv("TAKEDAWN_UPSCALE_ENGINE");
    const char *exe = recomp_exe_path();
    char dir[1100];
    if (e && *e) {
        snprintf(s_engine_exe, sizeof s_engine_exe, "%s/" ENGINE_EXE, e);
        snprintf(s_models, sizeof s_models, "%s/models", e);
        if (engine_ok(s_engine_exe, s_models))
            return 1;
        snprintf(code, cn, "no-engine");
        snprintf(why, wn, "%s has no " ENGINE_EXE " with models/%s and models/%s", e, k_model_name[0],
                 k_model_name[1]);
        return 0;
    }
    if (exe && *exe) {
        const char *sl = strrchr(exe, '/');
#if defined(_WIN32)
        const char *bs = strrchr(exe, '\\');
        if (bs && (!sl || bs > sl))
            sl = bs;
#endif
        snprintf(dir, sizeof dir, "%.*s", sl ? (int)(sl - exe) : 0, exe);
        /* Linux and Windows: an upscaler folder beside the program. */
        snprintf(s_engine_exe, sizeof s_engine_exe, "%s/upscaler/" ENGINE_EXE, dir);
        snprintf(s_models, sizeof s_models, "%s/upscaler/models", dir);
        if (engine_ok(s_engine_exe, s_models))
            return 1;
        /* macOS: the program beside Takedawn in Contents/MacOS, the models
         * in Contents/Resources. */
        snprintf(s_engine_exe, sizeof s_engine_exe, "%s/" ENGINE_EXE, dir);
        snprintf(s_models, sizeof s_models, "%s/../Resources/upscaler/models", dir);
        if (engine_ok(s_engine_exe, s_models))
            return 1;
    }
    snprintf(code, cn, "no-engine");
    snprintf(why, wn, "this build carries no upscaler (a developer build: TAKEDAWN_UPSCALE_ENGINE=<a folder"
             " with " ENGINE_EXE " and models/>)");
    return 0;
}
#else
/* The model in memory: the APK's assets/upscaler (read through SDL, which
 * reads an Android app's assets by a relative name), or the models/ of the
 * folder named by --engine or TAKEDAWN_UPSCALE_ENGINE (a test). */
static char    *s_param;
static uint8_t *s_bin;
static size_t   s_bin_len;

static uint8_t *read_asset(const char *name, size_t *n)
{
    SDL_RWops *rw = SDL_RWFromFile(name, "rb");
    Sint64 size;
    uint8_t *b = NULL;
    if (!rw)
        return NULL;
    size = SDL_RWsize(rw);
    if (size > 0 && (b = (uint8_t *)malloc((size_t)size + 1)) != NULL) {
        if (SDL_RWread(rw, b, 1, (size_t)size) != (size_t)size) {
            free(b);
            b = NULL;
        } else {
            b[size] = 0;
            *n = (size_t)size;
        }
    }
    SDL_RWclose(rw);
    return b;
}

static int find_engine(const char *engine_dir, char *code, size_t cn, char *why, size_t wn)
{
    const char *e = engine_dir && *engine_dir ? engine_dir : getenv("TAKEDAWN_UPSCALE_ENGINE");
    const char *m = k_model_name[BO3_UPS_MODEL_GENERAL];
    char p[1300];
    size_t pn = 0;
    free(s_param);
    free(s_bin);
    s_param = NULL;
    s_bin = NULL;
    s_bin_len = 0;
    if (e && *e) {
        snprintf(p, sizeof p, "%s/models/%s.param", e, m);
        s_param = (char *)slurp(p, &pn);
        snprintf(p, sizeof p, "%s/models/%s.bin", e, m);
        s_bin = slurp(p, &s_bin_len);
    } else {
        snprintf(p, sizeof p, "upscaler/%s.param", m);
        s_param = (char *)read_asset(p, &pn);
        snprintf(p, sizeof p, "upscaler/%s.bin", m);
        s_bin = read_asset(p, &s_bin_len);
    }
    if (s_param && s_bin)
        return 1;
    snprintf(code, cn, "no-engine");
    snprintf(why, wn, "the model %s is not in %s", m, e && *e ? e : "the app's assets/upscaler");
    return 0;
}
#endif

/* ── read: the pictures of the disc ───────────────────────────────────── */

typedef struct item {
    char src[12];
    char name[33];
    char key[48];                       /* <hash>[-<pal>].png */
    int group, model, keep;
    uint32_t w, h;
} item;

static item s_items[MAX_TEX];
static int  s_nitems;
static int  s_counts[BO3_UPS_GROUPS];

static int have_key(const char *key)
{
    int i;
    for (i = 0; i < s_nitems; i++)
        if (!strcmp(s_items[i].key, key))
            return 1;
    return 0;
}

static void out_path(const item *it, char *p, size_t n)
{
    snprintf(p, n, "%s/out-%d/%s", s_work, it->model, it->key);
}

/* One picture: listed, and written as the upscaler's input unless it
 * finished it already. want_key: the key it must have (else it is skipped),
 * or NULL. 0 when a file could not be written. */
static int add_texture(const char *src, const bo3_txd_tex *t, int quality, const char *want_key, int *written)
{
    item *it;
    char inp[1200], outp[1200];
    uint8_t *rgba;
    size_t k, px;
    int g = bo3_upscale_group(src, t);
    if (s_nitems >= MAX_TEX)
        return 1;
    it = &s_items[s_nitems];
    if (g == BO3_UPS_SKIP) {
        s_counts[BO3_UPS_SKIP]++;
        return 1;
    }
    memset(it, 0, sizeof *it);
    snprintf(it->src, sizeof it->src, "%s", src);
    snprintf(it->name, sizeof it->name, "%s", t->name);
    if (!bo3_txd_pack_name(t, it->key, sizeof it->key) || have_key(it->key))
        return 1;                       /* the same picture twice: once is enough */
    if (want_key && strcmp(it->key, want_key))
        return 1;                       /* not the program this was made for */
    it->group = g;
    it->model = bo3_upscale_model(g, quality);
    it->keep = bo3_upscale_keep_scale(g);
    it->w = t->w;
    it->h = t->h;
    s_counts[g]++;
    s_nitems++;
    out_path(it, outp, sizeof outp);
    if (bo3_png_complete(outp, NULL, NULL))
        return 1;
    snprintf(inp, sizeof inp, "%s/in-%d/%s", s_work, it->model, it->key);
    if (!(rgba = bo3_txd_decode(t)))
        return 0;
    bo3_upscale_flatten(rgba, t->w, t->h);
    px = (size_t)t->w * t->h;
    for (k = 0; k < px; k++) {          /* RGBA -> BGRA for the writer */
        uint8_t r = rgba[4 * k];
        rgba[4 * k] = rgba[4 * k + 2];
        rgba[4 * k + 2] = r;
    }
    if (!nv2a_texpack_png_write(inp, rgba, t->w, t->h)) {
        free(rgba);
        return 0;
    }
    free(rgba);
    (*written)++;
    return 1;
}

/* One dictionary's pictures. 0 when a file could not be written. */
static int read_dict(const char *src, const uint8_t *d, size_t n, int quality, int *written)
{
    static bo3_txd_tex tex[512];
    int count = bo3_txd_list(d, n, tex, 512, NULL), i;
    for (i = 0; i < count; i++)
        if (!add_texture(src, &tex[i], quality, NULL, written))
            return 0;
    return 1;
}

/* 1 read, 0 failed. */
static int read_disc(const char *data_dir, int quality, char *code, size_t cn, char *why, size_t wn)
{
    char p[1200];
    uint8_t *fe, *ls;
    size_t fn = 0, ln = 0;
    int written = 0, i, m;
    static const uint8_t *dicts[256];
    static size_t lens[256];
    int nd;
    s_nitems = 0;
    memset(s_counts, 0, sizeof s_counts);
    for (m = 0; m < 2; m++) {
        snprintf(p, sizeof p, "%s/in-%d", s_work, m);
        rmtree(p);                      /* inputs are made again: they are quick */
        if (!make_dirs(p)) {
            snprintf(code, cn, "disk");
            snprintf(why, wn, "cannot make %s", p);
            return 0;
        }
        snprintf(p, sizeof p, "%s/out-%d", s_work, m);
        make_dirs(p);
    }
    snprintf(p, sizeof p, "%s/LoadScrn.bin", data_dir);
    ls = slurp(p, &ln);
    snprintf(p, sizeof p, "%s/Frontend.txd", data_dir);
    fe = slurp(p, &fn);
    if (!ls || !fe) {
        free(ls);
        free(fe);
        snprintf(code, cn, "no-game-data");
        snprintf(why, wn, "%s/%s cannot be read: install the game's data first", data_dir,
                 ls ? "Frontend.txd" : "LoadScrn.bin");
        return 0;
    }
    nd = bo3_loadscrn_dicts(ls, ln, dicts, lens, 256);
    if (nd <= 0 || bo3_txd_list(fe, fn, NULL, 0, NULL) < 0) {
        free(ls);
        free(fe);
        snprintf(code, cn, "not-this-disc");
        snprintf(why, wn, "LoadScrn.bin or Frontend.txd is not in the expected format");
        return 0;
    }
    for (i = 0; i < nd; i++)
        if (!read_dict("LoadScrn", dicts[i], lens[i], quality, &written))
            goto bad;
    if (!read_dict("Frontend", fe, fn, quality, &written))
        goto bad;
    free(ls);
    free(fe);
    /* The screens' big title lettering (CRASH NAV, SELECT REGION, ...): a
     * font atlas the program carries itself, raw DXT5 in default.xbe's .data
     * (one disc version is supported; the key is checked, so another build
     * of the program only skips it). The small-text fonts beside it are left
     * out: a model redraws small letters. */
    snprintf(p, sizeof p, "%s/../default.xbe", data_dir);
    if ((ls = slurp(p, &ln)) != NULL) {
        static const struct { const char *name; size_t off; uint32_t fmt, w, h; const char *key; } xbe_tex[] = {
            { "TitleFont", 0x3cb058, BO3_TXD_DXT5, 128, 256, "8ed622fa02acbb1f.png" }, /* region: not an address: a file offset, the same in both PAL default.xbe files (the 32 KB atlas read there is byte for byte the NTSC-U one, and the key is checked) */
        };
        size_t k;
        for (k = 0; k < sizeof xbe_tex / sizeof xbe_tex[0]; k++) {
            bo3_txd_tex t;
            memset(&t, 0, sizeof t);
            snprintf(t.name, sizeof t.name, "%s", xbe_tex[k].name);
            t.fmt = xbe_tex[k].fmt;
            t.w = xbe_tex[k].w;
            t.h = xbe_tex[k].h;
            t.px_len = (size_t)t.w * t.h;
            if (xbe_tex[k].off + t.px_len > ln)
                continue;
            t.px = ls + xbe_tex[k].off;
            if (!add_texture("XBE", &t, quality, xbe_tex[k].key, &written)) {
                free(ls);
                ls = NULL;
                fe = NULL;
                goto bad;
            }
        }
        free(ls);
        ls = NULL;
    }
    /* The menus' art in Global.txd (the main menu's logo, medals, ...). */
    snprintf(p, sizeof p, "%s/Global.txd", data_dir);
    if ((ls = slurp(p, &ln)) != NULL) {
        int ok = bo3_txd_list(ls, ln, NULL, 0, NULL) < 0 || read_dict("Global", ls, ln, quality, &written);
        free(ls);
        ls = NULL;
        if (!ok) {
            fe = NULL;
            goto bad;
        }
    }
    out("LOG " STEP " %d pictures (%d loading screens, %d car pictures, %d maps, %d other; %d skipped),"
        " %d still to upscale", s_nitems, s_counts[BO3_UPS_LOADSCREEN], s_counts[BO3_UPS_CARPICK],
        s_counts[BO3_UPS_SATMAP], s_nitems - s_counts[BO3_UPS_LOADSCREEN] - s_counts[BO3_UPS_CARPICK]
        - s_counts[BO3_UPS_SATMAP], s_counts[BO3_UPS_SKIP], written);
    return 1;
bad:
    free(ls);
    free(fe);
    snprintf(code, cn, "disk");
    snprintf(why, wn, "a picture could not be decoded or written into %s", s_work);
    return 0;
}

/* ── upscale ──────────────────────────────────────────────────────────── */

static double item_cost(const item *it)
{
    return (double)it->w * it->h / 1e6 * k_model_cost[it->model];
}

#if !defined(BO3_UPSCALE_INPROC)
/* 1 done, 0 failed, -1 cancelled. */
static int upscale(double p0, double p1, char *code, size_t cn, char *why, size_t wn)
{
    static unsigned char was_done[MAX_TEX];
    double total = 0, done_before = 0;
    int m, i;
    for (i = 0; i < s_nitems; i++)
        total += item_cost(&s_items[i]);
    for (m = 0; m < 2; m++) {
        char in[1200], od[1200], models[1200], exe[1200], logp[1200], gpu_arg[16] = "auto";
        int todo = 0, ec = 0, tile;
        double mine = 0;
        child c;
        for (i = 0; i < s_nitems; i++) {
            char op[1200];
            if (s_items[i].model != m)
                continue;
            out_path(&s_items[i], op, sizeof op);
            was_done[i] = bo3_png_complete(op, NULL, NULL);
            if (was_done[i])
                done_before += item_cost(&s_items[i]);
            else {
                todo++;
                mine += item_cost(&s_items[i]);
                remove(op);             /* a cut one from a stopped run */
            }
        }
        if (!todo)
            continue;
        snprintf(in, sizeof in, "%s/in-%d", s_work, m);
        snprintf(od, sizeof od, "%s/out-%d", s_work, m);
        snprintf(models, sizeof models, "%s", s_models);
        snprintf(exe, sizeof exe, "%s", s_engine_exe);
        snprintf(logp, sizeof logp, "%s/engine-%d.log", s_work, m);
        {
            const char *g = getenv("TAKEDAWN_UPSCALE_GPU");
            if (g && *g)
                snprintf(gpu_arg, sizeof gpu_arg, "%s", g);
        }
        /* The upscaler's own tile size first (by the card's memory). A card
         * that loses the device on it (an AMD APU under RADV did, and the
         * upscaler then writes black pictures and says nothing) gets smaller
         * tiles: shorter submissions. */
        for (tile = 0; tile < 3; tile++) {
            static const char *const tiles[3] = { NULL, "64", "32" };
            char lost_line[200] = "";
            int lost = 0;
            remove(logp);
            {
                const char *argv[20];
                int k = 0;
                argv[k++] = exe;
                argv[k++] = "-i"; argv[k++] = in;
                argv[k++] = "-o"; argv[k++] = od;
                argv[k++] = "-n"; argv[k++] = k_model_name[m];
                argv[k++] = "-m"; argv[k++] = models;
                argv[k++] = "-s"; argv[k++] = "4";
                argv[k++] = "-f"; argv[k++] = "png";
                if (strcmp(gpu_arg, "auto")) {
                    argv[k++] = "-g";
                    argv[k++] = gpu_arg;
                }
                if (tiles[tile]) {
                    argv[k++] = "-t";
                    argv[k++] = tiles[tile];
                }
                argv[k] = NULL;
                out("LOG " STEP " %s: %d pictures%s%s", k_model_name[m], todo, tiles[tile] ? ", tile " : "",
                    tiles[tile] ? tiles[tile] : "");
                if (!child_start(&c, argv, logp)) {
                    snprintf(code, cn, "engine-start");
                    snprintf(why, wn, "%s could not be started", exe);
                    return 0;
                }
            }
            for (;;) {
                double now = done_before;
                int finished = child_done(&c, &ec), made = 0;
                if (!finished && cancelled()) {
                    child_kill(&c);
                    return -1;
                }
                for (i = 0; i < s_nitems; i++) {
                    char op[1200];
                    if (s_items[i].model != m)
                        continue;
                    out_path(&s_items[i], op, sizeof op);
                    if (!was_done[i] && exists(op)) {
                        now += item_cost(&s_items[i]);
                        made++;
                    }
                }
                {
                    char t[160];
                    snprintf(t, sizeof t, "upscaling: %d of %d pictures%s", made, todo,
                             m == BO3_UPS_MODEL_GENERAL && todo < s_nitems / 2 ? " (newspapers)" : "");
                    progress(p0, p1, total > 0 ? now / total : 1, t);
                }
                if (finished)
                    break;
                sleep_ms(250);
            }
            {
                size_t len = 0;
                uint8_t *lg = slurp(logp, &len);
                if (lg) {
                    char *hit;
                    lg[len - 1] = 0;
                    if ((hit = strstr((char *)lg, "vkQueueSubmit failed")) != NULL
                            || (hit = strstr((char *)lg, "vkWaitForFences failed")) != NULL
                            || (hit = strstr((char *)lg, "vkAllocateMemory failed")) != NULL) {
                        lost = 1;
                        snprintf(lost_line, sizeof lost_line, "%.*s", (int)strcspn(hit, "\r\n"), hit);
                    }
                    free(lg);
                }
            }
            if (!lost)
                break;
            /* What it wrote this time cannot be trusted. */
            for (i = 0; i < s_nitems; i++) {
                char op[1200];
                if (s_items[i].model != m || was_done[i])
                    continue;
                out_path(&s_items[i], op, sizeof op);
                remove(op);
            }
            out("LOG " STEP " %s: the graphics device failed (%s); again with smaller tiles", k_model_name[m],
                lost_line);
            if (tile == 2) {
                snprintf(code, cn, "gpu-lost");
                snprintf(why, wn, "the graphics device failed while upscaling, also with small tiles (%s)", lost_line);
                return 0;
            }
        }
        for (i = 0; i < s_nitems; i++) {
            char op[1200];
            if (s_items[i].model != m)
                continue;
            out_path(&s_items[i], op, sizeof op);
            if (!bo3_png_complete(op, NULL, NULL)) {
                char tail[320];
                log_tail(logp, tail, sizeof tail);
                if (strstr(tail, "vkCreateInstance") || strstr(tail, "invalid gpu")
                        || strstr(tail, "no vulkan") || strstr(tail, "vkEnumeratePhysicalDevices"))
                    snprintf(code, cn, "no-gpu");
                else
                    snprintf(code, cn, "engine-failed");
                snprintf(why, wn, "the upscaler stopped (status %d) before %s: %s", ec, s_items[i].name, tail);
                return 0;
            }
        }
        done_before += mine;
    }
    return 1;
}

#else  /* BO3_UPSCALE_INPROC */

/* While this says so, the run holds between tiles (the game being played:
 * the MODS page's run, upscale rows below). NULL: never. */
static int (*s_hold)(void);
static int s_in_game;                   /* the run is the game's (not the setup page's) */

typedef struct ip_state {
    double p0, p1, total, done, cost;
    int made, todo;
} ip_state;

static int ip_poll(void *ctx, double f)
{
    ip_state *q = (ip_state *)ctx;
    char t[160];
    int held = 0;
    snprintf(t, sizeof t, "upscaling: %d of %d pictures", q->made, q->todo);
    progress(q->p0, q->p1, q->total > 0 ? (q->done + q->cost * f) / q->total : 1, t);
    while (s_hold && s_hold() && !cancelled()) {
        if (!held)
            progress(q->p0, q->p1, q->total > 0 ? (q->done + q->cost * f) / q->total : 1,
                     "paused while you play; it goes on with the settings open");
        held = 1;
        sleep_ms(250);
    }
    if (held)
        progress(q->p0, q->p1, q->total > 0 ? (q->done + q->cost * f) / q->total : 1, t);
    return cancelled();
}

/* Vulkan that failed on this device, or did not come back from its first
 * picture (the process ended inside the driver), is not tried again:
 * <cache>/vulkan-failed says why. */
static void vk_mark(char *p, size_t n, const char *name)
{
    snprintf(p, n, "%s/%s", s_cache, name);
}

static int env_int(const char *name, int def)
{
    const char *e = getenv(name);
    return e && *e ? atoi(e) : def;
}

static bo3_ncnn *ip_open(int vulkan, char *err, size_t en)
{
    char lib[1024] = "";
    Dl_info di;
    /* The game's own library: its vkGetInstanceProcAddr (the toolkit's,
     * vk_dyn.c) is the loader the game opened, so ncnn reaches the same
     * driver. */
    if (vulkan && dladdr((void *)&bo3_upscale_main, &di) && di.dli_fname)
        snprintf(lib, sizeof lib, "%s", di.dli_fname);
    return bo3_ncnn_open(s_param, s_bin, s_bin_len, vulkan, lib[0] ? lib : NULL,
                         env_int("TAKEDAWN_UPSCALE_THREADS", 0), env_int("TAKEDAWN_UPSCALE_FP16", 0), err, en);
}

static int ip_backend(char *why, size_t wn)
{
    extern const char *xr_android_vk_driver(void);
    extern int xr_android_vk_driver_failed(void);
    bo3_backend_facts f;
    char failed[1200], trying[1200];
    memset(&f, 0, sizeof f);
    f.override = getenv("TAKEDAWN_UPSCALE_BACKEND");
#if defined(BO3_NCNN_VULKAN)
    f.have_vulkan = 1;
#endif
    vk_mark(failed, sizeof failed, "vulkan-failed");
    vk_mark(trying, sizeof trying, "vulkan-trying");
    if (exists(trying)) {               /* the last try never came back */
        FILE *fp = fopen(failed, "w");
        if (fp) {
            fputs("Vulkan did not come back from its first picture (the app ended in it)\n", fp);
            fclose(fp);
        }
        remove(trying);
    }
    f.failed_before = exists(failed);
    if (s_in_game) {
        /* The setup page has no game running: the CPU there. */
        f.opened = xr_android_vk_driver();
        f.open_failed = xr_android_vk_driver_failed();
    }
    f.driver_dir = getenv("RECOMP_ANDROID_DRIVER_DIR");
    f.driver_name = getenv("RECOMP_ANDROID_DRIVER_NAME");
    f.lib_dir = getenv("RECOMP_ANDROID_LIB_DIR");
    f.bundled = "libvulkan_freedreno.so";        /* GpuDriver.BUNDLED */
    return bo3_upscale_backend_pick(&f, why, wn);
}

static void vk_failed(const char *why)
{
    char p[1200];
    FILE *f;
    vk_mark(p, sizeof p, "vulkan-failed");
    if ((f = fopen(p, "w")) != NULL) {
        fprintf(f, "%s\n", why);
        fclose(f);
    }
    vk_mark(p, sizeof p, "vulkan-trying");
    remove(p);
}

/* The model over every picture not made yet, one after the other, in this
 * process. 1 done, 0 failed, -1 cancelled. */
static int upscale(double p0, double p1, char *code, size_t cn, char *why, size_t wn)
{
    char bwhy[96], err[256] = "", trying[1200];
    ip_state q;
    bo3_ncnn *net = NULL;
    int i, b, tile = env_int("TAKEDAWN_UPSCALE_TILE", 0), first_gpu = 1;
    memset(&q, 0, sizeof q);
    q.p0 = p0;
    q.p1 = p1;
    for (i = 0; i < s_nitems; i++) {
        char op[1200];
        q.total += item_cost(&s_items[i]);
        out_path(&s_items[i], op, sizeof op);
        if (bo3_png_complete(op, NULL, NULL))
            q.done += item_cost(&s_items[i]);
        else
            q.todo++;
    }
    if (!q.todo)
        return 1;
    b = ip_backend(bwhy, sizeof bwhy);
    vk_mark(trying, sizeof trying, "vulkan-trying");
    if (b == BO3_UPS_VULKAN) {
        FILE *f = fopen(trying, "w");
        if (f)
            fclose(f);
        if (!(net = ip_open(1, err, sizeof err))) {
            out("LOG " STEP " Vulkan: %s; the CPU instead", err);
            vk_failed(err);
            b = BO3_UPS_CPU;
        }
    }
    if (!net && !(net = ip_open(0, err, sizeof err))) {
        snprintf(code, cn, "engine-failed");
        snprintf(why, wn, "%s", err);
        return 0;
    }
    out("LOG " STEP " %s: %d pictures, on the %s (%s)", k_model_name[BO3_UPS_MODEL_GENERAL], q.todo,
        bo3_ncnn_vulkan(net) ? "GPU (Vulkan)" : "CPU", bwhy);
    /* Below the game's own threads; ncnn's are made from this one, and take
     * its priority with them. */
    if (setpriority(PRIO_PROCESS, (id_t)syscall(SYS_gettid), 10) != 0) {
        /* not lowered: no matter */
    }
    for (i = 0; i < s_nitems; i++) {
        item *it = &s_items[i];
        char ip[1200], op[1200], part[1300];
        uint32_t w = 0, h = 0;
        int has_alpha = 0, rc;
        uint8_t *px, *o;
        size_t k, n;
        out_path(it, op, sizeof op);
        if (bo3_png_complete(op, NULL, NULL))
            continue;
        snprintf(ip, sizeof ip, "%s/in-%d/%s", s_work, it->model, it->key);
        if (!(px = nv2a_texpack_png_read(ip, &w, &h, &has_alpha))) {
            snprintf(code, cn, "disk");
            snprintf(why, wn, "%s could not be read", ip);
            bo3_ncnn_close(net);
            return 0;
        }
        n = (size_t)w * h;
        for (k = 0; k < n; k++) {       /* BGRA -> RGBA */
            uint8_t t = px[4 * k];
            px[4 * k] = px[4 * k + 2];
            px[4 * k + 2] = t;
        }
        if (!(o = (uint8_t *)malloc(n * 16 * 4))) {
            free(px);
            bo3_ncnn_close(net);
            snprintf(code, cn, "memory");
            snprintf(why, wn, "no memory for the %ux%u picture %s", w * 4, h * 4, it->name);
            return 0;
        }
        q.cost = item_cost(it);
        rc = bo3_ncnn_upscale(net, px, w, h, o, tile, ip_poll, &q, err, sizeof err);
        free(px);
        if (rc == -2 && bo3_ncnn_vulkan(net)) {
            /* Any Vulkan trouble: the CPU from here on, this picture again. */
            free(o);
            out("LOG " STEP " Vulkan: %s (%s); the CPU from here on", err, it->name);
            vk_failed(err);
            bo3_ncnn_close(net);
            if (!(net = ip_open(0, err, sizeof err))) {
                snprintf(code, cn, "engine-failed");
                snprintf(why, wn, "%s", err);
                return 0;
            }
            i--;
            continue;
        }
        if (rc != 0) {
            free(o);
            bo3_ncnn_close(net);
            if (rc == -1)
                return -1;
            snprintf(code, cn, rc == -3 ? "memory" : "engine-failed");
            snprintf(why, wn, "the upscaler stopped before %s: %s", it->name, err);
            return 0;
        }
        n *= 16;
        for (k = 0; k < n; k++) {       /* RGBA -> BGRA for the writer */
            uint8_t t = o[4 * k];
            o[4 * k] = o[4 * k + 2];
            o[4 * k + 2] = t;
        }
        snprintf(part, sizeof part, "%s.part", op);
        if (!nv2a_texpack_png_write(part, o, w * 4, h * 4) || rename(part, op) != 0) {
            free(o);
            remove(part);
            bo3_ncnn_close(net);
            snprintf(code, cn, "disk");
            snprintf(why, wn, "%s could not be written", op);
            return 0;
        }
        free(o);
        if (bo3_ncnn_vulkan(net) && first_gpu) {
            first_gpu = 0;
            remove(trying);             /* it came back: Vulkan works here */
        }
        q.done += q.cost;
        q.made++;
    }
    bo3_ncnn_close(net);
    remove(trying);
    return 1;
}
#endif /* BO3_UPSCALE_INPROC */

/* ── pack ─────────────────────────────────────────────────────────────── */

static long long dir_bytes(const char *p, int *files)
{
    DIR *d = opendir(p);
    struct dirent *e;
    long long sum = 0;
    if (!d)
        return 0;
    while ((e = readdir(d)) != NULL) {
        char sub[1200];
        long long s;
        if (e->d_name[0] == '.')
            continue;
        snprintf(sub, sizeof sub, "%s/%s", p, e->d_name);
        s = file_size(sub);
        if (s > 0) {
            sum += s;
            if (files)
                (*files)++;
        }
    }
    closedir(d);
    return sum;
}

/* 1 done, 0 failed. */
static int pack(const char *quality_name, double p0, double p1, char *final_dir, size_t fdn,
                char *code, size_t cn, char *why, size_t wn)
{
    char gen[1024], stage[1200], tex[1300], p[1400], text[1600];
    int i, files = 0;
    long long bytes;
    time_t now = time(NULL);
    struct tm tmv;
    if (!bo3_generated_mods_dir(gen, sizeof gen) || !make_dirs(gen)) {
        snprintf(code, cn, "disk");
        snprintf(why, wn, "no folder for made mods (TAKEDAWN_GENERATED_MODS is empty, or it cannot be made)");
        return 0;
    }
    snprintf(stage, sizeof stage, "%s/.%s.new", gen, K->dir);
    snprintf(final_dir, fdn, "%s/%s", gen, K->dir);
    rmtree(stage);
    snprintf(tex, sizeof tex, "%s/textures", stage);
    if (!make_dirs(tex)) {
        snprintf(code, cn, "disk");
        snprintf(why, wn, "cannot make %s", tex);
        return 0;
    }
    for (i = 0; i < s_nitems; i++) {
        const item *it = &s_items[i];
        char op[1200];
        out_path(it, op, sizeof op);
        snprintf(p, sizeof p, "%s/%s", tex, it->key);
        if (it->keep == 4) {
            if (!move_file(op, p))
                goto bad;
        } else {
            uint32_t w = 0, h = 0;
            int has_alpha = 0;
            uint8_t *px = nv2a_texpack_png_read(op, &w, &h, &has_alpha), *half;
            if (!px || (w & 1) || (h & 1) || !(half = (uint8_t *)malloc((size_t)w * h))) {
                free(px);
                goto bad;
            }
            bo3_upscale_half(px, w, h, half);
            free(px);
            if (!nv2a_texpack_png_write(p, half, w / 2, h / 2)) {
                free(half);
                goto bad;
            }
            free(half);
        }
        if ((i & 15) == 0)
            progress(p0, p1, (double)i / s_nitems, "making the texture pack");
    }
    /* The pictures have no alpha of their own: the game takes the disc's,
     * scaled up (docs/mod-folders.md, Alpha). */
    snprintf(p, sizeof p, "%s/pack.toml", tex);
    if (!write_text(p, "# Made by Takedawn's art upscale (docs/upscale-art.md).\n"
                       "alpha = \"original\"\ncompress = \"auto\"\n"))
        goto bad;
#if defined(_WIN32)
    localtime_s(&tmv, &now);
#else
    localtime_r(&now, &tmv);
#endif
    snprintf(text, sizeof text,
             "id = \"%s\"\n"
             "name = \"%s\"\n"
             "version = \"1\"\n"
             "author = \"made on this computer from your disc\"\n"
             "description = \"%s, upscaled 4x by Real-ESRGAN (%s quality) on %04d-%02d-%02d. Make"
             " them again or remove them in Settings, Graphics.\"\n",
             K->id, K->name, K->what, quality_name, tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday);
    snprintf(p, sizeof p, "%s/mod.toml", stage);
    if (!write_text(p, text))
        goto bad;
    snprintf(p, sizeof p, "%s/made.txt", stage);
    snprintf(text, sizeof text, "quality %s\npictures %d\nengine %s\n", quality_name, s_nitems, ENGINE_VERSION);
    if (!write_text(p, text))
        goto bad;
    rmtree(final_dir);
    if (rename(stage, final_dir) != 0) {
        snprintf(code, cn, "disk");
        snprintf(why, wn, "cannot rename %s to %s", stage, final_dir);
        return 0;
    }
    snprintf(tex, sizeof tex, "%s/textures", final_dir);
    bytes = dir_bytes(tex, &files);
    out("LOG " STEP " pack %s: %d files, %.0f MB", final_dir, files, bytes / 1048576.0);
    return 1;
bad:
    snprintf(code, cn, "disk");
    snprintf(why, wn, "the pack could not be written into %s", stage);
    rmtree(stage);
    return 0;
}

/* ── the command ──────────────────────────────────────────────────────── */

static void usage(void)
{
    fprintf(stderr,
        "usage: burnout3 --upscale-art [options]\n"
        "  Upscales the loading screens and menu pictures of the installed game 4x with\n"
        "  Real-ESRGAN, on this computer, into a texture pack the game loads (on the MODS\n"
        "  page). The upscaler comes with the program; nothing is downloaded. Progress on\n"
        "  stdout: PROTOCOL 1 / PLAN upscale / STEP upscale BEGIN|OK|SKIP|FAIL ... /\n"
        "  PROGRESS upscale <0..1> <text> / LOG upscale <text> / RESULT OK|FAIL ...\n"
        "  --quality best|fast  RealESRGAN_x4plus, or the quicker realesr-general-x4v3\n"
        "                       (default: what suits this computer, see --check)\n"
        "  --force              make the pack again even if it is there\n"
        "  --check              say whether this computer is offered it, and stop\n"
        "  --status             say whether the pack is made, and stop\n"
        "  --remove             delete the pack\n"
        "  --dismiss            the setup window does not offer it again\n"
        "  --engine <dir>       an upscaler folder of your own (" ENGINE_EXE " and models/)\n"
        "  Stop it at any time (or create the 'cancel' file in the work folder it names):\n"
        "  a later run goes on from there.\n");
}

static int read_made(const char *dir, char *quality, size_t qn)
{
    char p[1200], line[128];
    FILE *f;
    int ok = 0;
    snprintf(p, sizeof p, "%s/made.txt", dir);
    if (!(f = fopen(p, "r")))
        return 0;
    while (fgets(line, sizeof line, f))
        if (!strncmp(line, "quality ", 8)) {
            line[strcspn(line, "\r\n")] = 0;
            snprintf(quality, qn, "%s", line + 8);
            ok = 1;
        }
    fclose(f);
    return ok;
}

int bo3_upscale_main(int argc, char **argv, const char *xbe_path)
{
    char quality_name[16] = "", why_offer[96], code[48] = "", why[1500] = "", gen[1024], pack_dir[1200];
    char data_dir[1100], made_q[16];
    const char *engine_dir = NULL;
    int force = 0, check = 0, status = 0, remove_it = 0, all = 0, dismiss = 0, i, offer, quality, rc;
    time_t t0 = time(NULL);

    for (i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "--quality") && i + 1 < argc
                && (!strcmp(argv[i + 1], "best") || !strcmp(argv[i + 1], "fast")))
            snprintf(quality_name, sizeof quality_name, "%s", argv[++i]);
        else if (!strcmp(argv[i], "--force"))
            force = 1;
        else if (!strcmp(argv[i], "--check"))
            check = 1;
        else if (!strcmp(argv[i], "--status"))
            status = 1;
        else if (!strcmp(argv[i], "--remove"))
            remove_it = 1;
        else if (!strcmp(argv[i], "--all"))
            all = 1;
        else if (!strcmp(argv[i], "--dismiss"))
            dismiss = 1;
        else if (!strcmp(argv[i], "--engine") && i + 1 < argc)
            engine_dir = argv[++i];
        else if (!strcmp(argv[i], "--data") && i + 1 < argc)
            i++;                        /* main() took it */
        else {
            usage();
            return 2;
        }
    }
    if (!s_sink)
        setvbuf(stdout, NULL, _IOLBF, 0);
    s_signalled = 0;
    bo3_upscale_retire_packs();
    out("PROTOCOL 1");
    out("PLAN " STEP);
    {
        char oq[16];
        offer = bo3_upscale_offer(oq, sizeof oq, why_offer, sizeof why_offer);
        if (!quality_name[0])
            snprintf(quality_name, sizeof quality_name, "%s", oq);
    }
    quality = !strcmp(quality_name, "fast") ? BO3_UPS_FAST : BO3_UPS_BEST;
#if defined(BO3_UPSCALE_INPROC)
    if (quality != BO3_UPS_FAST)        /* the app carries the fast model only */
        out("LOG " STEP " the app has the fast model only: fast quality");
    quality = BO3_UPS_FAST;
    snprintf(quality_name, sizeof quality_name, "fast");
#endif
    if (dismiss) {
        /* The setup window's box was seen and not used: it is not offered
         * there again (Settings, Graphics still has it). */
        char p[1200];
        FILE *f;
        if (!declined_path(p, sizeof p) || !(f = fopen(p, "w"))) {
            out("RESULT FAIL " STEP " disk");
            return 1;
        }
        fputs("The setup window does not offer the menu picture upscale again (docs/upscale-art.md).\n", f);
        fclose(f);
        out("RESULT OK declined");
        return 0;
    }
    if (check) {
        out("LOG " STEP " offer %s %s %s", offer == 2 ? "on" : offer == 1 ? "off" : "no", quality_name, why_offer);
        out("RESULT OK %s", offer == 2 ? "on" : offer == 1 ? "off" : "no");
        return 0;
    }
    if (!cache_root(s_cache, sizeof s_cache) || !bo3_generated_mods_dir(gen, sizeof gen)) {
        out("STEP " STEP " FAIL disk there is no per-user folder to work in");
        out("RESULT FAIL " STEP " disk");
        return 1;
    }
    snprintf(s_work, sizeof s_work, "%s/%s", s_cache, K->work);
    snprintf(s_cancel, sizeof s_cancel, "%s/cancel", s_work);
    snprintf(pack_dir, sizeof pack_dir, "%s/%s", gen, K->dir);
    if (status || remove_it) {
        int made = read_made(pack_dir, made_q, sizeof made_q);
        if (remove_it) {
            rmtree(pack_dir);
            rmtree(s_work);
            if (all)
                rmtree(s_cache);
            out("STEP " STEP " OK removed %s%s", pack_dir, all ? " and the upscaler" : "");
            out("RESULT OK removed");
            return 0;
        }
        if (made) {
            int files = 0;
            char tex[1300];
            snprintf(tex, sizeof tex, "%s/textures", pack_dir);
            out("LOG " STEP " pack %s quality %s, %.0f MB", pack_dir, made_q, dir_bytes(tex, &files) / 1048576.0);
            out("RESULT OK made %s", made_q);
            return 0;
        }
        out("LOG " STEP " no pack in %s", pack_dir);
        out("RESULT OK none");
        return 10;
    }
    if (offer == 0 && !getenv("TAKEDAWN_UPSCALE_OFFER")) {
        out("STEP " STEP " FAIL not-offered not on this system (%s)", why_offer);
        out("RESULT FAIL " STEP " not-offered");
        return 1;
    }
    if (!force && read_made(pack_dir, made_q, sizeof made_q) && !strcmp(made_q, quality_name)) {
        out("STEP " STEP " SKIP already made (%s quality) in %s; --force makes it again", made_q, pack_dir);
        out("RESULT OK %s", pack_dir);
        return 0;
    }
    {
        const char *sl = strrchr(xbe_path, '/');
#if defined(_WIN32)
        const char *bs = strrchr(xbe_path, '\\');
        if (bs && (!sl || bs > sl))
            sl = bs;
#endif
        if (sl)
            snprintf(data_dir, sizeof data_dir, "%.*s/Data", (int)(sl - xbe_path), xbe_path);
        else
            snprintf(data_dir, sizeof data_dir, "Data");
    }
    if (!make_dirs(s_work)) {
        out("STEP " STEP " FAIL disk cannot make %s", s_work);
        out("RESULT FAIL " STEP " disk");
        return 1;
    }
    remove(s_cancel);
    /* The command's own process stops on a signal; a run inside the game
     * (s_sink) leaves the game's signals alone: the cancel file stops it. */
    if (!s_sink) {
#if !defined(_WIN32)
        signal(SIGTERM, on_signal);
        signal(SIGINT, on_signal);
        signal(SIGHUP, on_signal);
#else
        signal(SIGTERM, on_signal);
        signal(SIGINT, on_signal);
#endif
    }
    out("STEP " STEP " BEGIN Upscaling the menu pictures with Real-ESRGAN (%s quality)%s", quality_name,
        "");
    out("LOG " STEP " work %s", s_work);
    out("LOG " STEP " %s: %s", quality_name, why_offer);

    rc = find_engine(engine_dir, code, sizeof code, why, sizeof why);
    if (rc > 0) {
        progress(0.0, 0.03, 0, "reading the pictures from the game's data");
        rc = read_disc(data_dir, quality, code, sizeof code, why, sizeof why);
    }
    if (rc > 0 && cancelled())
        rc = -1;
    if (rc > 0)
        rc = upscale(0.03, 0.97, code, sizeof code, why, sizeof why);
    if (rc > 0 && cancelled())
        rc = -1;
    if (rc > 0)
        rc = pack(quality_name, 0.97, 1.0, pack_dir, sizeof pack_dir, code, sizeof code, why, sizeof why);
    if (rc < 0) {
        remove(s_cancel);
        out("STEP " STEP " FAIL cancelled stopped; what was finished is kept, and a later run goes on from there");
        out("RESULT FAIL " STEP " cancelled");
        return 1;
    }
    if (rc == 0) {
        out("STEP " STEP " FAIL %s %s", code, why);
        out("RESULT FAIL " STEP " %s", code);
        return 1;
    }
    /* The decoded disc art is not kept beside the pack. */
    rmtree(s_work);
    out("PROGRESS " STEP " 1.000 done");
    out("STEP " STEP " OK %d pictures upscaled (%s quality) into %s; %ld s; on (the MODS page turns them off)",
        s_nitems, quality_name, pack_dir, (long)(time(NULL) - t0));
    out("RESULT OK %s", pack_dir);
    return 0;
}

/* ── the overlay's row ────────────────────────────────────────────────── */

#ifdef XBOXRECOMP_HAVE_OVERLAY
extern int  xbox_OverlayOpen(void);
static int s_h_offered;                 /* bo3_upscale_host_init: the rows are shown */

enum { H_IDLE, H_RUNNING, H_DONE, H_FAILED };
/* The rows' state, one a pack (k_kinds). */
struct hstate {
    int   state;
    float fraction;
    char  text[200];
    int   made;                         /* a pack is there: -1 not looked yet */
    char  made_q[16];
    int   force;
    int   live;                         /* the last live apply: 1 shown now, 0 needs a restart */
};
static pthread_mutex_t s_h_mu = PTHREAD_MUTEX_INITIALIZER;
static struct hstate s_h[NKINDS] = { { 0, 0, "", -1, "", 0, -1 } };

static int pack_dir_of(int k, char *dir, size_t n)
{
    char gen[1024];
    return bo3_generated_mods_dir(gen, sizeof gen) && snprintf(dir, n, "%s/%s", gen, k_kinds[k].dir) < (int)n;
}

/* A pack into the running game, or out of it (modtex.c): no restart. A pack
 * that is made is on (the mod list says so from now on, also if it was
 * switched off before). 1 when the game shows what was asked. */
static int h_live(int k, int on)
{
    extern int bo3_modtex_live(const char *id, const char *fallback_dir, int on);
    extern int bo3_modfs_set_enabled(const char *id, int on);
    char dir[1200];
    int keys;
    if (!pack_dir_of(k, dir, sizeof dir))
        return 0;
    if (on)
        bo3_modfs_set_enabled(k_kinds[k].id, 1);
    keys = bo3_modtex_live(k_kinds[k].id, dir, on);
    return on ? keys > 0 : keys >= 0;
}

static int h_enabled_next(int k)
{
    extern int bo3_modfs_enabled_next(const char *id);
    return bo3_modfs_enabled_next(k_kinds[k].id);
}

static void h_set(int k, int st, float fr, const char *text)
{
    pthread_mutex_lock(&s_h_mu);
    s_h[k].state = st;
    if (fr >= 0)
        s_h[k].fraction = fr;
    if (text)
        snprintf(s_h[k].text, sizeof s_h[k].text, "%s", text);
    pthread_mutex_unlock(&s_h_mu);
}

static void h_look(int k)
{
    char dir[1200];
    s_h[k].made = pack_dir_of(k, dir, sizeof dir) && read_made(dir, s_h[k].made_q, sizeof s_h[k].made_q);
}

static int h_any_running(void)
{
    int k;
    for (k = 0; k < NKINDS; k++)
        if (s_h[k].state == H_RUNNING)
            return 1;
    return 0;
}

/* One line of the run: the row's state and text. *ok on its RESULT OK. */
static void h_line(int k, const char *line, int *ok)
{
    if (!strncmp(line, "PROGRESS " STEP " ", 9 + sizeof STEP)) {
        char *text = NULL;
        double fr = strtod(line + 9 + sizeof STEP, &text);
        h_set(k, H_RUNNING, (float)fr, text && *text == ' ' ? text + 1 : "");
    } else if (!strncmp(line, "STEP " STEP " FAIL ", 10 + sizeof STEP)) {
        const char *w = line + 10 + sizeof STEP;
        h_set(k, H_RUNNING, -1, !strncmp(w, "cancelled", 9) ? "stopped; the next run goes on from there"
              : !strncmp(w, "no-gpu", 6) ? "the upscaler found no Vulkan graphics device"
              : !strncmp(w, "no-engine", 9) ? "this build carries no upscaler"
              : "it did not finish; the log is in the upscale cache folder");
    } else if (!strncmp(line, "RESULT OK", 9)) {
        *ok = 1;
    }
}

/* Done: the pack into the running game at once (the title is paused while
 * the settings are open, and the packs move on a generation the renderer
 * checks at every bind), and the rows say so. */
static void h_finish(int k, int ok)
{
    int live = ok ? h_live(k, 1) : -1;
    pthread_mutex_lock(&s_h_mu);
    h_look(k);
    s_h[k].state = ok ? H_DONE : H_FAILED;
    if (ok) {
        s_h[k].fraction = 1.0f;
        s_h[k].live = live;
    }
    pthread_mutex_unlock(&s_h_mu);
}

#if defined(BO3_UPSCALE_INPROC)
/* Android: the run is a thread of the game (no program to start), its lines
 * handed to h_line. It holds between tiles while the game is played, so a
 * race is not slowed, and goes on while the settings are open or the app is
 * in the background, where a foreground service (UpscaleService) keeps the
 * process from being frozen. */
static const char *s_h_xbe = "";
static volatile int s_h_away;           /* the app is in the background */
static int s_h_ok;

static int SDLCALL h_watch(void *ud, SDL_Event *ev)
{
    (void)ud;
    if (ev->type == SDL_APP_WILLENTERBACKGROUND || ev->type == SDL_APP_DIDENTERBACKGROUND)
        s_h_away = 1;
    else if (ev->type == SDL_APP_WILLENTERFOREGROUND || ev->type == SDL_APP_DIDENTERFOREGROUND)
        s_h_away = 0;
    return 0;
}

static int h_hold(void)
{
    return !s_h_away && !xbox_OverlayOpen();
}

static void h_sink(void *ctx, const char *line)
{
    h_line((int)(intptr_t)ctx, line, &s_h_ok);
}

/* GameActivity.upscaleService(boolean): the foreground service on or off. */
static void h_service(int on)
{
    JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
    jobject act = env ? (jobject)SDL_AndroidGetActivity() : NULL;
    jclass cls;
    jmethodID m;
    if (!act)
        return;
    cls = (*env)->GetObjectClass(env, act);
    m = cls ? (*env)->GetMethodID(env, cls, "upscaleService", "(Z)V") : NULL;
    if (m)
        (*env)->CallVoidMethod(env, act, m, (jboolean)(on != 0));
    if ((*env)->ExceptionCheck(env))
        (*env)->ExceptionClear(env);
    if (cls)
        (*env)->DeleteLocalRef(env, cls);
    (*env)->DeleteLocalRef(env, act);
}

static void *h_run(void *arg)
{
    static int watching;
    int k = (int)(intptr_t)arg;
    char *argv[2];
    int argc = 0;
    if (!watching) {
        watching = 1;
        SDL_AddEventWatch(h_watch, NULL);
    }
    if (s_h[k].force)
        argv[argc++] = (char *)"--force";
    argv[argc] = NULL;
    h_service(1);
    s_h_ok = 0;
    s_hold = h_hold;
    s_in_game = 1;
    s_sink_ctx = arg;
    s_sink = h_sink;
    bo3_upscale_main(argc, argv, s_h_xbe);
    s_sink = NULL;
    s_hold = NULL;
    h_service(0);
    h_finish(k, s_h_ok);
    return NULL;
}
#else
/* The run as a child of this game, its lines read from its log file. */
static void *h_run(void *arg)
{
    int k = (int)(intptr_t)arg;
    char logp[1200], cache[1024], line[1600];
    const char *exe = recomp_exe_path();
    const char *argv[10];
    child c;
    FILE *f = NULL;
    long at = 0;
    int ec = 0, n = 0, ok = 0, stopped = 0;
    if (!exe || !*exe || !cache_root(cache, sizeof cache) || !make_dirs(cache)) {
        h_set(k, H_FAILED, 0, "could not start");
        return NULL;
    }
    snprintf(logp, sizeof logp, "%s/run.log", cache);
    remove(logp);
    argv[n++] = exe;
    argv[n++] = "--no-setup";
    argv[n++] = "--upscale-art";
    if (s_h[k].force)
        argv[n++] = "--force";
    argv[n] = NULL;
    if (!child_start(&c, argv, logp)) {
        h_set(k, H_FAILED, 0, "could not start");
        return NULL;
    }
    for (;;) {
        int done = child_done(&c, &ec);
        if (!f)
            f = fopen(logp, "r");
        if (f) {
            fseek(f, at, SEEK_SET);
            while (fgets(line, sizeof line, f)) {
                size_t len = strlen(line);
                if (!len || line[len - 1] != '\n')
                    break;              /* the rest of it next time */
                at = ftell(f);
                line[strcspn(line, "\r\n")] = 0;
                h_line(k, line, &ok);
            }
            clearerr(f);
        }
        if (done)
            break;
        /* As the shader build: it stops with the settings, so the game does
         * not race beside it. */
        if (!stopped && !xbox_OverlayOpen()) {
            char cf[1200];
            FILE *cfile;
            snprintf(cf, sizeof cf, "%s/%s/cancel", cache, k_kinds[k].work);
            if ((cfile = fopen(cf, "w")) != NULL)
                fclose(cfile);
            stopped = 1;
        }
        sleep_ms(300);
    }
    if (f)
        fclose(f);
    h_finish(k, ok);
    return NULL;
}
#endif

int bo3_upscale_mods_live(const char *id, int on)
{
    int ok, k;
    for (k = 0; k < NKINDS && (!id || strcmp(id, k_kinds[k].id)); k++)
        ;
    if (k == NKINDS)
        return 0;
    ok = h_live(k, on);
    pthread_mutex_lock(&s_h_mu);
    if (s_h[k].state == H_DONE)
        s_h[k].state = H_IDLE;          /* the row says what the MODS page made it */
    pthread_mutex_unlock(&s_h_mu);
    return ok;
}

/* Row 0: the menu pictures (make / stop / again); row 1: remove them. */
static const char *h_row(int row, int activate)
{
    static char text[2 * NKINDS][220];
    int k = row / 2, sub = row % 2, st, busy;
    float fr;
    char t[200];
    const char *what = "menu pictures";
    if (row < 0 || row >= 2 * NKINDS)
        return NULL;
    pthread_mutex_lock(&s_h_mu);
    if (s_h[k].made < 0)
        h_look(k);
    st = s_h[k].state;
    fr = s_h[k].fraction;
    busy = h_any_running() && st != H_RUNNING;
    snprintf(t, sizeof t, "%s", s_h[k].text);
    pthread_mutex_unlock(&s_h_mu);

    if (activate) {
        if (sub == 0 && st == H_RUNNING) {
            char cache[1024], cf[1200];
            FILE *f;
            if (cache_root(cache, sizeof cache)) {
                snprintf(cf, sizeof cf, "%s/%s/cancel", cache, k_kinds[k].work);
                if ((f = fopen(cf, "w")) != NULL)
                    fclose(f);
            }
        } else if (sub == 0 && !busy) {
            pthread_t th;
            s_h[k].force = s_h[k].made > 0;
            h_set(k, H_RUNNING, 0, "starting");
            if (pthread_create(&th, NULL, h_run, (void *)(intptr_t)k) != 0)
                h_set(k, H_FAILED, 0, "could not start");
            else
                pthread_detach(th);
        } else if (sub == 1 && st != H_RUNNING && s_h[k].made > 0) {
            char dir[1200];
            /* Out of the game first, so no worker reads a file that goes. */
            int live = h_live(k, 0);
            if (pack_dir_of(k, dir, sizeof dir))
                rmtree(dir);
            pthread_mutex_lock(&s_h_mu);
            h_look(k);
            s_h[k].state = H_IDLE;
            snprintf(s_h[k].text, sizeof s_h[k].text, "%s", live ? "removed; the original pictures are back"
                     : "removed. Restart the game to see the original pictures");
            pthread_mutex_unlock(&s_h_mu);
        }
        return h_row(row, 0);
    }
    if (sub == 0) {
        const char *exp = "";
        if (st == H_RUNNING)
            snprintf(text[row], sizeof text[row], "Stop upscaling %s (%d%%: %.120s)", what, (int)(fr * 100 + 0.5f), t);
        else if (st == H_DONE && s_h[k].live == 0)
            snprintf(text[row], sizeof text[row], "Done. Restart the game to see the new pictures");
        else if (st == H_DONE)
            snprintf(text[row], sizeof text[row], "Upscaled %s: On. You can turn them off in the mod list", what);
        else if (busy)
            snprintf(text[row], sizeof text[row], "Upscale %s with AI%s (after the other upscale)", what, exp);
        else if (st == H_FAILED)
            snprintf(text[row], sizeof text[row], "Upscale %s with AI%s (last try: %.100s)", what, exp, t);
        else if (s_h[k].made > 0)
            snprintf(text[row], sizeof text[row], "Upscale %s again (made: %s quality, %s)", what, s_h[k].made_q,
                     h_enabled_next(k) ? "on" : "off in the mod list");
        else if (t[0])
            snprintf(text[row], sizeof text[row], "Upscale %s with AI%s (%.100s)", what, exp, t);
        else
            snprintf(text[row], sizeof text[row], "Upscale menu pictures with AI (a few minutes)");
        return text[row];
    }
    if (s_h[k].made > 0 && st != H_RUNNING) {
        snprintf(text[row], sizeof text[row], "Remove the upscaled menu pictures");
        return text[row];
    }
    return NULL;
}

void bo3_upscale_host_init(const char *xbe_path)
{
    char q[16], why[96];
#if defined(BO3_UPSCALE_INPROC)
    static char xbe[1100];
    snprintf(xbe, sizeof xbe, "%s", xbe_path ? xbe_path : "");
    s_h_xbe = xbe;
#else
    (void)xbe_path;
#endif
    if (bo3_upscale_offer(q, sizeof q, why, sizeof why) == 0)
        return;
    s_h_offered = 1;
}

/* The rows live on the overlay's Mods page (BO-327, modstab.c), beside the
 * mod list the pack joins. */
int bo3_upscale_row_count(void)
{
    return s_h_offered ? 2 * NKINDS : 0;
}

const char *bo3_upscale_row(int row, int activate)
{
    return s_h_offered ? h_row(row, activate) : NULL;
}
#else
void bo3_upscale_host_init(const char *xbe_path)
{
    (void)xbe_path;
}

int bo3_upscale_row_count(void)
{
    return 0;
}

const char *bo3_upscale_row(int row, int activate)
{
    (void)row;
    (void)activate;
    return NULL;
}

int bo3_upscale_mods_live(const char *id, int on)
{
    (void)id;
    (void)on;
    return 0;
}
#endif

#if defined(BO3_UPSCALE_INPROC)
/* ── the setup page (SetupActivity, its own process) ─────────────────── */

/* After the install the page offers the pictures once (docs/upscale-art.md):
 * the run in this process, its progress to this.onProgress(double, String)
 * as the install's. */
typedef struct jni_up {
    JNIEnv   *env;
    jobject   self;
    jmethodID progress;
    int       ok;
    char      code[48];
    char      text[600];
} jni_up;

static void jni_up_sink(void *ctx, const char *line)
{
    jni_up *j = (jni_up *)ctx;
    if (!strncmp(line, "PROGRESS " STEP " ", 9 + sizeof STEP) && j->progress) {
        char *text = NULL;
        double fr = strtod(line + 9 + sizeof STEP, &text);
        jstring t = (*j->env)->NewStringUTF(j->env, text && *text == ' ' ? text + 1 : "");
        (*j->env)->CallVoidMethod(j->env, j->self, j->progress, (jdouble)fr, t);
        if ((*j->env)->ExceptionCheck(j->env))
            (*j->env)->ExceptionClear(j->env);
        (*j->env)->DeleteLocalRef(j->env, t);
    } else if (!strncmp(line, "STEP " STEP " FAIL ", 10 + sizeof STEP)) {
        const char *w = line + 10 + sizeof STEP;
        size_t n = strcspn(w, " ");
        snprintf(j->code, sizeof j->code, "%.*s", (int)n, w);
        snprintf(j->text, sizeof j->text, "%s", w[n] ? w + n + 1 : "");
    } else if (!strncmp(line, "STEP " STEP " OK ", 8 + sizeof STEP)) {
        snprintf(j->text, sizeof j->text, "%s", line + 8 + sizeof STEP);
    } else if (!strncmp(line, "RESULT OK", 9)) {
        j->ok = 1;
    }
}

/* xbe: the installed game's default.xbe. "ok\n<sentence>" or
 * "<code>\n<sentence>" (cancelled, no-game-data, disk, ...). */
JNIEXPORT jstring JNICALL
Java_io_github_xenophobentx_takedawn_SetupActivity_nativeUpscale(JNIEnv *env, jobject self, jstring jxbe)
{
    const char *xbe = (*env)->GetStringUTFChars(env, jxbe, NULL);
    jclass cls = (*env)->GetObjectClass(env, self);
    char t[700];
    jni_up j;
    memset(&j, 0, sizeof j);
    j.env = env;
    j.self = self;
    j.progress = (*env)->GetMethodID(env, cls, "onProgress", "(DLjava/lang/String;)V");
    snprintf(j.code, sizeof j.code, "failed");
    s_hold = NULL;
    s_in_game = 0;
    s_sink_ctx = &j;
    s_sink = jni_up_sink;
    bo3_upscale_main(0, NULL, xbe);
    s_sink = NULL;
    (*env)->ReleaseStringUTFChars(env, jxbe, xbe);
    (*env)->DeleteLocalRef(env, cls);
    snprintf(t, sizeof t, "%s\n%s", j.ok ? "ok" : j.code, j.text);
    return (*env)->NewStringUTF(env, t);
}

JNIEXPORT void JNICALL
Java_io_github_xenophobentx_takedawn_SetupActivity_nativeUpscaleCancel(JNIEnv *env, jclass cls)
{
    (void)env; (void)cls;
    s_signalled = 1;
}

/* "made", "declined" or "none" (bo3_upscale_pack_state). */
JNIEXPORT jstring JNICALL
Java_io_github_xenophobentx_takedawn_SetupActivity_nativeUpscaleState(JNIEnv *env, jclass cls)
{
    (void)cls;
    return (*env)->NewStringUTF(env, bo3_upscale_pack_state());
}

/* The offer seen and not taken: not shown again (the MODS page keeps it). */
static void quiet_sink(void *ctx, const char *line)
{
    (void)ctx; (void)line;
}

JNIEXPORT void JNICALL
Java_io_github_xenophobentx_takedawn_SetupActivity_nativeUpscaleDismiss(JNIEnv *env, jclass cls)
{
    char *argv[2] = { (char *)"--dismiss", NULL };
    (void)env; (void)cls;
    s_sink = quiet_sink;
    bo3_upscale_main(1, argv, "");
    s_sink = NULL;
}
#endif /* BO3_UPSCALE_INPROC */

#else   /* Android */

int bo3_upscale_main(int argc, char **argv, const char *xbe_path)
{
    (void)argc;
    (void)argv;
    (void)xbe_path;
    out("PROTOCOL 1");
    out("PLAN " STEP);
    out("STEP " STEP " FAIL not-offered this Android build has no upscaler");
    out("RESULT FAIL " STEP " not-offered");
    return 1;
}

void bo3_upscale_host_init(const char *xbe_path)
{
    (void)xbe_path;
}

int bo3_upscale_mods_live(const char *id, int on)
{
    (void)id;
    (void)on;
    return 0;
}

int bo3_upscale_row_count(void)
{
    return 0;
}

const char *bo3_upscale_row(int row, int activate)
{
    (void)row;
    (void)activate;
    return NULL;
}

#endif
