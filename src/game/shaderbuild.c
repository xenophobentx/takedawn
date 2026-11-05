/*
 * shaderbuild.c - the shader pre-build; see shaderbuild.h and
 * docs/shader-prebuild.md.
 *
 * Three roles, one executable:
 *
 *   the build   `--build-shaders`: starts a scene process per scene, turns
 *               their marks into PROGRESS lines, keeps the state file.
 *               Runs no title.
 *   a scene     `--build-shaders-scene <name>`: the title itself, headless,
 *               on scratch saves, driven by the toolkit's in-process scene
 *               driver (xbox_autoplay.h). The GPU backend does the rest as
 *               in any run: what it compiles it appends to its key list
 *               (nv2a_pcache.h), and the SPIR-V / driver caches fill.
 *   the game    a running game starts the build as a child (the overlay's
 *               "Build shaders now") and has its backend read the key list
 *               again when it is done.
 *
 * The scenes play a new profile that is never saved, so what they can reach
 * is what a new player can: Single Event on Silver Lake, Crash at the first
 * junction. The two training films that cannot be skipped are answered with
 * the Criterion logo's file (xbox_PathRedirectHook): 3 s instead of 115.
 */
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include "shaderbuild.h"
int bo3_pcache_manifest_present(void);          /* pcmanifest.c */
#include "install.h"                    /* BO3_USER_DIR */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#if defined(_WIN32)
#include <direct.h>
#include <io.h>
#include <process.h>
#include <windows.h>
#define sb_popen(c)   sb_popen_win(c)
#define sb_pclose     sb_pclose_win
#define sb_mkdir(p)   _mkdir(p)
#define sb_rmdir      _rmdir
#define sb_dup        _dup
#define sb_fdopen     _fdopen
#define sb_dup2       _dup2
#define sb_fileno     _fileno
#else
#include <signal.h>
#include <unistd.h>
#define sb_popen(c)   popen((c), "r")
#define sb_pclose     pclose
#define sb_mkdir(p)   mkdir((p), 0755)
#define sb_rmdir      rmdir
#define sb_dup        dup
#define sb_fdopen     fdopen
#define sb_dup2       dup2
#define sb_fileno     fileno
#endif

#include "kernel.h"                     /* xbox_PathRedirectHook */
#include "xbox_autoplay.h"
#include "xbox_crash.h"                 /* g_xbox_flip_count */
#include "d3d/nv2a_pcache.h"
#include "nv2a_backend_common.h"        /* nv2a_host_opts */
#include "platform/recomp_env.h"
#include "platform/recomp_exe_dir.h"
#include "platform/host_time.h"         /* xbox_TimeWarpEnable: a scene does not wait */
#include "input/input_backend.h"        /* xbox_InputSetBlocked */
#include "usb/usb_gamepad.h"            /* usb_gamepad_live_taken */
#ifdef XBOXRECOMP_HAVE_OVERLAY
#include "xbox_shader_build.h"
#include "nv2a_texscale.h"               /* the scene fills graphics.ui_scale's cache too */
#include "platform/recomp_save_dir.h"
#include "enhance/recomp_cfg.h"
#endif

#if defined(_WIN32)
/* popen without a console window. _popen starts "cmd /c", and a window
 * program (this one is) has no console to give it, so Windows opens one for
 * every child: a black window for the build and one more for each scene.
 * This starts the child itself with CREATE_NO_WINDOW, its stdout a pipe read
 * through the returned FILE, its stdin the null device and its stderr this
 * process's own. The line is the child's command line (program first,
 * quoted). */
#define SB_CHILDREN 8
static struct { FILE *f; HANDLE proc; } s_children[SB_CHILDREN];
static pthread_mutex_t s_children_mu = PTHREAD_MUTEX_INITIALIZER;

static HANDLE inheritable_copy(HANDLE h)
{
    HANDLE out = NULL;
    if (h == NULL || h == INVALID_HANDLE_VALUE)
        return NULL;
    if (!DuplicateHandle(GetCurrentProcess(), h, GetCurrentProcess(), &out, 0, TRUE,
                         DUPLICATE_SAME_ACCESS))
        return NULL;
    return out;
}

static FILE *sb_popen_win(const char *cmdline)
{
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
    HANDLE rd = NULL, wr = NULL, in, err, list[3];
    STARTUPINFOEXA si;
    PROCESS_INFORMATION pi;
    SIZE_T attr_n = 0;
    char line[4096];
    FILE *f;
    int fd, i, nlist = 0, ok;

    if (strlen(cmdline) >= sizeof line)
        return NULL;
    memcpy(line, cmdline, strlen(cmdline) + 1);
    if (!CreatePipe(&rd, &wr, &sa, 0))
        return NULL;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    in = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                     OPEN_EXISTING, 0, NULL);
    err = inheritable_copy((HANDLE)_get_osfhandle(2));
    if (!err)
        err = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                          OPEN_EXISTING, 0, NULL);

    /* Only these three go to the child, not every inheritable handle (the
     * pipe ends of another scene starting at the same time). */
    memset(&si, 0, sizeof si);
    si.StartupInfo.cb = sizeof si;
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput = in;
    si.StartupInfo.hStdOutput = wr;
    si.StartupInfo.hStdError = err;
    list[nlist++] = wr;
    if (in != INVALID_HANDLE_VALUE)
        list[nlist++] = in;
    if (err && err != INVALID_HANDLE_VALUE)
        list[nlist++] = err;
    InitializeProcThreadAttributeList(NULL, 1, 0, &attr_n);
    si.lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)malloc(attr_n);
    ok = si.lpAttributeList
         && InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &attr_n);
    if (ok)
        ok = UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                       list, nlist * sizeof list[0], NULL, NULL);
    memset(&pi, 0, sizeof pi);
    ok = ok && CreateProcessA(NULL, line, NULL, NULL, TRUE,
                              CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, NULL, NULL,
                              &si.StartupInfo, &pi);
    if (si.lpAttributeList) {
        DeleteProcThreadAttributeList(si.lpAttributeList);
        free(si.lpAttributeList);
    }
    if (in != INVALID_HANDLE_VALUE)
        CloseHandle(in);
    if (err && err != INVALID_HANDLE_VALUE)
        CloseHandle(err);
    CloseHandle(wr);
    if (!ok) {
        CloseHandle(rd);
        return NULL;
    }
    CloseHandle(pi.hThread);
    fd = _open_osfhandle((intptr_t)rd, 0x8000 /* _O_BINARY */ | 0x0020 /* _O_NOINHERIT */);
    f = fd >= 0 ? _fdopen(fd, "rb") : NULL;
    if (!f) {
        if (fd >= 0)
            _close(fd);
        else
            CloseHandle(rd);
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hProcess);
        return NULL;
    }
    pthread_mutex_lock(&s_children_mu);
    for (i = 0; i < SB_CHILDREN && s_children[i].f; i++)
        ;
    if (i < SB_CHILDREN) {
        s_children[i].f = f;
        s_children[i].proc = pi.hProcess;
    } else {
        CloseHandle(pi.hProcess);       /* not waited for; nothing needs its status */
    }
    pthread_mutex_unlock(&s_children_mu);
    return f;
}

static int sb_pclose_win(FILE *f)
{
    HANDLE proc = NULL;
    DWORD code = 0;
    int i;

    pthread_mutex_lock(&s_children_mu);
    for (i = 0; i < SB_CHILDREN; i++)
        if (s_children[i].f == f) {
            proc = s_children[i].proc;
            s_children[i].f = NULL;
            s_children[i].proc = NULL;
        }
    pthread_mutex_unlock(&s_children_mu);
    fclose(f);
    if (!proc)
        return -1;
    WaitForSingleObject(proc, INFINITE);
    GetExitCodeProcess(proc, &code);
    CloseHandle(proc);
    return (int)code;
}
#endif

#define STEP "shaders"

/* ---- the scenes -------------------------------------------------------------- */

/* From the start of the process to the main menu, on empty saves: the two
 * logos (not skippable), the intro film (START skips it), Press START, "New
 * Profile" (the only choice that is lit), the name as offered, "save your
 * profile?" No, "Autosave will be disabled" Yes. The menus take no input
 * while they slide in, and with nothing to load they open no file to wait
 * on, so each press is given time: 150 flips.
 *
 * The first logo is a film of the disc's language (englis30.xmv on the
 * NTSC-U disc; french30, german30 or italia30 on the PAL one, which has no
 * English films), so the first file waited on is the second logo's,
 * cri_rw30.xmv, which both discs have. */
#define BOOT_TO_MENU \
    { XAP_WAIT_FILE, "cri_rw30.xmv", NULL, 0 }, \
    { XAP_MARK, "starting the game", NULL, 80 }, \
    { XAP_WAIT_FILE, "titles30.xmv", NULL, 0 }, \
    { XAP_PRESS_UNTIL, "start:300", "_femain.xmv", 60 }, \
    { XAP_MARK, "title screen", NULL, 140 }, \
    { XAP_WAIT_ANCHOR, NULL, NULL, 300 }, \
    { XAP_PRESS, "start:600", NULL, 150 },      /* Select Option: New Profile */ \
    { XAP_PRESS, "a:400", NULL, 150 },          /* the keyboard, cursor on Done */ \
    { XAP_PRESS, "a:400", NULL, 150 },          /* save your profile? No */ \
    { XAP_PRESS, "a:400", NULL, 150 },          /* Autosave will be disabled: No */ \
    { XAP_PRESS, "up:200", NULL, 30 },          /* Yes */ \
    { XAP_PRESS, "a:400", NULL, 240 },          /* the main menu */ \
    { XAP_MARK, "main menu", NULL, 240 }, \
    { XAP_PRESS, "down:200", NULL, 30 }, \
    { XAP_PRESS, "a:400", NULL, 120 }           /* Single Event: Select Mode */

/* Race, USA, Silver Lake Southbound (all a new profile has), the first car;
 * the garage is on screen for a while; then the race: the rolling start,
 * the accelerator, boost, a pull to the left into whatever is there (the
 * crash and its camera), and the pause menu. */
static const xap_step k_race[] = {
    BOOT_TO_MENU,
    { XAP_PRESS, "a:400", NULL, 120 },          /* Race: Select Options */
    { XAP_PRESS, "right:200", NULL, 30 },       /* Region: Random > USA */
    { XAP_PRESS, "down:200", NULL, 30 },
    { XAP_PRESS, "right:200", NULL, 30 },       /* Track: Random > Silver Lake */
    { XAP_PRESS_UNTIL, "a:400", "car1.bgv", 150 },
    { XAP_MARK, "garage", NULL, 295 },
    { XAP_WAIT_ANCHOR, NULL, NULL, 240 },
    { XAP_PRESS, "right:200", NULL, 90 },       /* another car on its stand */
    { XAP_PRESS, "left:200", NULL, 90 },
    { XAP_PRESS, "a:400", NULL, 0 },            /* the training film, the load */
    { XAP_WAIT_FILE, "c3_v1\\streamed.dat", NULL, 0 },
    { XAP_MARK, "race: track loaded", NULL, 450 },
    { XAP_WAIT_FILE, "tracks\\crash", NULL, 0 },   /* the first of the crash sound banks */
    { XAP_WAIT_ANCHOR, NULL, NULL, 300 },       /* the intro camera, GO */
    { XAP_MARK, "race: driving", NULL, 525 },
    { XAP_PRESS, "rt:9000", NULL, 540 },
    { XAP_PRESS, "rt+a:6000", NULL, 370 },      /* boost */
    { XAP_MARK, "race: boost and a crash", NULL, 730 },
    { XAP_PRESS, "rt+a+lleft:5000", NULL, 600 },/* into the scenery */
    { XAP_PRESS, "rt:4000", NULL, 250 },
    { XAP_PRESS, "start:300", NULL, 120 },      /* the pause menu */
    { XAP_MARK, "race: done", NULL, 1000 },
    { XAP_END, NULL, NULL, 0 }
};

/* Crash, the first junction, the first car: the intro flyby, the captions,
 * the countdown, the impact, the crash camera, Crash Results. */
static const xap_step k_crash[] = {
    BOOT_TO_MENU,
    { XAP_PRESS, "down:200", NULL, 30 },
    { XAP_PRESS, "down:200", NULL, 30 },
    { XAP_PRESS, "down:200", NULL, 30 },        /* Crash */
    { XAP_PRESS, "a:400", NULL, 120 },          /* Junction Selection */
    { XAP_PRESS, "down:200", NULL, 30 },        /* 1. Trailer Trash */
    { XAP_PRESS_UNTIL, "a:400", "car1.bgv", 150 },
    { XAP_MARK, "crash: garage", NULL, 245 },
    { XAP_WAIT_ANCHOR, NULL, NULL, 120 },
    { XAP_PRESS, "a:400", NULL, 0 },            /* the training film, the load */
    { XAP_WAIT_FILE, "c3_v1\\streamed.dat", NULL, 0 },
    { XAP_MARK, "crash: junction loaded", NULL, 310 },
    { XAP_WAIT_FILE, "tracks\\crash", NULL, 0 },   /* the captions (a crash sound bank) */
    { XAP_WAIT_ANCHOR, NULL, NULL, 300 },
    { XAP_PRESS, "a:400", NULL, 60 },           /* the countdown */
    { XAP_MARK, "crash: driving", NULL, 515 },
    { XAP_PRESS, "rt:20000", NULL, 0 },
    /* The impact: the title opens one of the junction's crash sound banks
     * (tracks\US\C3_v1\crash<n>.rws; which one is its choice: crash1 on
     * macOS and Linux, crash3 was seen on Windows), and it opened one while
     * loading too, so only opens from here on count. With no bank at all the
     * scene goes on when the accelerator's 20 s are over. */
    { XAP_SYNC, NULL, NULL, 0 },
    { XAP_WAIT_FILE, "c3_v1\\crash", NULL, 1500 },
    { XAP_MARK, "crash: impact", NULL, 700 },
    { XAP_WAIT_ANCHOR, NULL, NULL, 900 },       /* the crash camera, the results */
    { XAP_MARK, "crash: done", NULL, 1000 },
    { XAP_END, NULL, NULL, 0 }
};

/* ---- the coverage sweep (BO-208) ----------------------------------------------
 *
 * One visit per process: BO3_SWEEP="<mode>/<region>/<track>/<car>/<drive>"
 * (tools/headless/sweep.sh runs a list of them, in parallel, each into a key
 * list of its own, and pcache_keys.py merges the lists into the manifest).
 *
 *   mode     race, timeattack, roadrage (Single Event), crash, autorace and
 *            autoroadrage (the title's AI drives for `drive` game seconds) (Junction Selection: `track`
 *            is the junction, 1 = the first), splitrace, splitroadrage
 *            (Multiplayer), garage (only the cars)
 *   region   1 USA, 2 Europe, 3 Asia (0: the menu's Random)
 *   track    the Track row's position (right presses from Random)
 *   car      how many cars the garage is walked through (1: the first only)
 *   drive    seconds the car is driven (at the scene's speed), default 20
 *
 * The steps are made here, in memory. Unlocked in memory (BO3_UNLOCK_ALL) so
 * every region and track is in the menus; the scene plays a new profile that
 * is never saved, as the pre-build's.
 */
#define SW_MAX 400
static xap_step s_sw[SW_MAX];
static char     s_sw_txt[SW_MAX][32];
static int      s_sw_n;
static char     s_sw_name[64] = "sweep";

static const xap_step k_boot[] = { BOOT_TO_MENU, { XAP_END, NULL, NULL, 0 } };

static void sw_add(xap_op op, const char *s, const char *s2, int n)
{
    xap_step *st;
    if (s_sw_n >= SW_MAX - 1)
        return;
    st = &s_sw[s_sw_n];
    st->op = op;
    st->s = s;
    st->s2 = s2;
    st->n = n;
    s_sw_n++;
}

/* A press, its text copied (the step keeps the pointer). */
static void sw_press(const char *what, int flips)
{
    if (s_sw_n >= SW_MAX - 1)
        return;
    snprintf(s_sw_txt[s_sw_n], sizeof s_sw_txt[0], "%s", what);
    sw_add(XAP_PRESS, s_sw_txt[s_sw_n], NULL, flips);
}

static void sw_presses(const char *what, int times, int flips)
{
    while (times-- > 0)
        sw_press(what, flips);
}


/* mode "script": BO3_SWEEP=script and BO3_SWEEP_SCRIPT=<file>, one step a line
 * (# to the end of the line is a comment):
 *   boot                          the way to the main menu
 *   press <pad> <flips>           e.g. press a:400 120 (docs: xbox_autoplay.h)
 *   pressuntil <pad> <file> <n>   again every n flips until the title opens <file>
 *   file <text> [flips]           wait for a file open containing <text>
 *   race_tail | crash_tail        the chosen car, the load and a drive / a crash (as the visits)
 *   sync | wait <flips> | anchor <flips> | mark <text> [thousandths] | end
 * With BO3_SWEEP_SHOTS=1 every mark also dumps a frame to <work>/fb_flip_*.bmp. */
static char s_sw_script_txt[SW_MAX][64];
static int script_build(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[256];
    int k = 0;

    if (!f)
        return 0;
    snprintf(s_sw_name, sizeof s_sw_name, "sweep script");
    while (fgets(line, sizeof line, f) && s_sw_n < SW_MAX - 8) {
        char a[64], b[64], c[64];
        int n1 = 0, got, cam;
        char *hash = strchr(line, '#');
        if (hash)
            *hash = 0;
        a[0] = b[0] = c[0] = 0;
        got = sscanf(line, "%63s %63s %63s %63s", a, b, c, s_sw_script_txt[k + 1]);
        if (got < 1)
            continue;
        if (!strcmp(a, "boot") || !strcmp(a, "bootmain")) {
            /* bootmain stops on the main menu, before Single Event is chosen */
            const xap_step *q, *e = k_boot;
            while (e->op != XAP_END)
                e++;
            if (!strcmp(a, "bootmain"))
                e -= 2;
            for (q = k_boot; q < e; q++)
                sw_add(q->op, q->s, q->s2, q->n);
        } else if (!strcmp(a, "press") && got >= 3) {
            sw_press(b, atoi(c));
        } else if (!strcmp(a, "pressuntil") && got >= 4) {
            snprintf(s_sw_script_txt[k], 64, "%s", b);
            snprintf(s_sw_script_txt[k + 2], 64, "%s", c);
            sw_add(XAP_PRESS_UNTIL, s_sw_script_txt[k], s_sw_script_txt[k + 2],
                   atoi(s_sw_script_txt[k + 1]));
            k += 3;
        } else if (!strcmp(a, "file") && got >= 2) {
            snprintf(s_sw_script_txt[k], 64, "%s", b);
            sw_add(XAP_WAIT_FILE, s_sw_script_txt[k++], NULL, got >= 3 ? atoi(c) : 0);
        } else if (!strcmp(a, "race_tail") || !strcmp(a, "crash_tail")) {
            /* From the last Select Car: the car chosen, the load, the intro, the drive. */
            sw_add(XAP_PRESS_UNTIL, "a:400", "streamed.dat", 150);
            sw_add(XAP_MARK, "track loaded", NULL, 450);
            sw_add(XAP_WAIT_FILE, "tracks\\crash", NULL, 0);
            sw_add(XAP_WAIT_ANCHOR, NULL, NULL, 300);
            sw_add(XAP_MARK, "driving", NULL, 525);
            if (a[0] == 'r') {
                sw_add(XAP_PRESS, "rt:9000", NULL, 540);
                for (cam = 0; cam < 5; cam++)
                    sw_add(XAP_PRESS, "rt+y:300", NULL, 90);
                sw_add(XAP_PRESS, "rt+a:6000", NULL, 370);
                sw_add(XAP_PRESS, "rt+a+lleft:5000", NULL, 600);
                sw_add(XAP_PRESS, "rt:4000", NULL, 250);
                sw_add(XAP_PRESS, "start:300", NULL, 120);
            } else {
                sw_press("a:400", 60);
                sw_add(XAP_PRESS, "rt:20000", NULL, 0);
                sw_add(XAP_SYNC, NULL, NULL, 0);
                sw_add(XAP_WAIT_FILE, "crash", NULL, 1500);
                sw_add(XAP_MARK, "impact", NULL, 700);
                sw_add(XAP_WAIT_ANCHOR, NULL, NULL, 900);
            }
            sw_add(XAP_MARK, "done", NULL, 1000);
        } else if (!strcmp(a, "sync")) {
            sw_add(XAP_SYNC, NULL, NULL, 0);
        } else if (!strcmp(a, "wait") && got >= 2) {
            sw_add(XAP_WAIT_FLIPS, NULL, NULL, atoi(b));
        } else if (!strcmp(a, "anchor") && got >= 2) {
            sw_add(XAP_WAIT_ANCHOR, NULL, NULL, atoi(b));
        } else if (!strcmp(a, "mark") && got >= 2) {
            snprintf(s_sw_script_txt[k], 64, "%s", b);
            sw_add(XAP_MARK, s_sw_script_txt[k++], NULL, got >= 3 ? atoi(c) : 500 + (n1 = 0));
        } else if (!strcmp(a, "end")) {
            break;
        }
        if (k > SW_MAX - 8)
            break;
    }
    fclose(f);
    sw_add(XAP_END, NULL, NULL, 0);
    return 1;
}

/* Builds the visit's steps from BO3_SWEEP. 1 if the spec is one. */
static int sweep_build(const char *spec)
{
    char mode[24] = "";
    int region = 1, track = 1, cars = 1, drive = 20, n, i;
    const xap_step *b;

    if (!strncmp(spec, "script", 6) && getenv("BO3_SWEEP_SCRIPT"))
        return script_build(getenv("BO3_SWEEP_SCRIPT"));
    n = sscanf(spec, "%23[a-z]/%d/%d/%d/%d", mode, &region, &track, &cars, &drive);
    if (n < 1 || (!strcmp(mode, "") ))
        return 0;
    snprintf(s_sw_name, sizeof s_sw_name, "sweep %s", spec);
    for (b = k_boot; b->op != XAP_END; b++)
        sw_add(b->op, b->s, b->s2, b->n);
    if (!strcmp(mode, "splitrace") || !strcmp(mode, "splitroadrage")) {
        /* Multiplayer: two downs from Single Event (the cursor is on its
         * Select Mode now: back out is not needed, the boot ends on Single
         * Event's mode list, so this mode is reached from the main menu
         * instead: see below). */
        return 0;
    }
    if (!strcmp(mode, "crash")) {
        sw_presses("down:200", 3, 30);              /* Crash */
        sw_press("a:400", 120);                     /* Junction Selection */
        sw_presses("down:200", track < 1 ? 1 : track, 30);
    } else {
        if (!strcmp(mode, "roadrage") || !strcmp(mode, "autoroadrage"))
            sw_presses("down:200", 2, 30);
        else if (!strcmp(mode, "timeattack"))
            sw_presses("down:200", 1, 30);
        else if (strcmp(mode, "race") && strcmp(mode, "garage") && strcmp(mode, "autorace")
                 && strcmp(mode, "autoroadrage"))
            return 0;
        sw_press("a:400", 120);                     /* Select Options */
        if (region > 0) {
            sw_presses("right:200", region, 30);
            sw_press("down:200", 30);
            sw_presses("right:200", track, 30);
        }
    }
    sw_add(XAP_PRESS_UNTIL, "a:400", "car1.bgv", 150);
    sw_add(XAP_MARK, "garage", NULL, 295);
    sw_add(XAP_WAIT_ANCHOR, NULL, NULL, 240);
    for (i = 1; i < cars; i++)                      /* every car on its stand */
        sw_press("right:200", 70);
    if (!strcmp(mode, "garage")) {
        sw_add(XAP_MARK, "garage: done", NULL, 1000);
        sw_add(XAP_END, NULL, NULL, 0);
        return 1;
    }
    /* The car: again every 150 flips until the track's file is opened (a press
     * on a screen that is still sliding in is not taken; a run that logs a lot
     * is slower in real time than in flips, where this shows). */
    sw_add(XAP_PRESS_UNTIL, "a:400", "streamed.dat", 150);   /* the training film, the load */
    sw_add(XAP_MARK, "track loaded", NULL, 450);
    sw_add(XAP_WAIT_FILE, "tracks\\crash", NULL, 0);
    sw_add(XAP_WAIT_ANCHOR, NULL, NULL, 300);       /* the intro camera, GO / the captions */
    sw_add(XAP_MARK, "driving", NULL, 525);
    if (!strcmp(mode, "crash")) {
        sw_press("a:400", 60);                      /* the countdown */
        sw_add(XAP_PRESS, "rt:20000", NULL, 0);
        sw_add(XAP_SYNC, NULL, NULL, 0);
        sw_add(XAP_WAIT_FILE, "crash", NULL, 1500);
        sw_add(XAP_MARK, "impact", NULL, 700);
        sw_add(XAP_WAIT_ANCHOR, NULL, NULL, 900);   /* the crash camera, the results */
    } else if (!strncmp(mode, "auto", 4)) {
        /* The title's own AI drives (RECOMP_DEBUG=autopilot=1): a long run of
         * racing, takedowns and all, for `drive` seconds of the game's time. */
        sw_add(XAP_WAIT_FLIPS, NULL, NULL, drive * 60);
    } else {
        int flips = drive * 60;
        sw_add(XAP_PRESS, "rt:9000", NULL, flips > 900 ? 540 : flips / 2);
        /* every view the Y button cycles through (chase near and far, bumper,
         * the hood), each for a moment, the accelerator held */
        for (i = 0; i < 5; i++)
            sw_add(XAP_PRESS, "rt+y:300", NULL, 90);
        sw_add(XAP_PRESS, "rt+a:6000", NULL, 370);  /* boost */
        sw_add(XAP_MARK, "boost and a crash", NULL, 730);
        sw_add(XAP_PRESS, "rt+a+lleft:5000", NULL, 600);    /* into the scenery */
        sw_add(XAP_PRESS, "rt:4000", NULL, 250);
        sw_add(XAP_PRESS, "start:300", NULL, 120);  /* the pause menu */
    }
    sw_add(XAP_MARK, "done", NULL, 1000);
    sw_add(XAP_END, NULL, NULL, 0);
    return 1;
}

/* BO-208: no scene to play. The shipped manifest's keys, and the vertex
 * programs it names (found by hash in the player's own default.xbe, the
 * ones D3D assembles itself too), are built by the backend's pre-warm in this
 * process, in parallel; the title is only started far enough to bring the
 * backend up (its first pictures, the logos) and left running until that is
 * done (scene_done waits for it). The Criterion logo's film is the first
 * file waited on, as in BOOT_TO_MENU: both discs have it. */
static const xap_step k_manifest[] = {
    { XAP_WAIT_FILE, "cri_rw30.xmv", NULL, 0 },
    { XAP_MARK, "preparing graphics", NULL, 100 },
    { XAP_WAIT_FLIPS, NULL, NULL, 120 },
    { XAP_END, NULL, NULL, 0 }
};

static const struct scene {
    const char *name;
    const xap_step *steps;
    int secs;                           /* about how long it takes at SCENE_SPEED, for
                                         * the progress (the marks' thousandths too) */
    int legacy;                         /* played only when named (--scenes), not by default */
} k_scenes[] = {
    { "manifest", k_manifest, 5, 0 },
    { "race",  k_race,  30, 1 },
    { "crash", k_crash, 37, 1 },
};
#define NSCENES ((int)(sizeof k_scenes / sizeof k_scenes[0]))

/* The training films a new profile is shown before its first race and its
 * first crash event, 115 and 100 s that no button skips: the title is given
 * the 3 s Criterion logo's file under their names. */
static const char *redirect(const char *path)
{
    static const char *const from[] = { "\\ovid\\trd_eng.xmv", "\\ovid\\trc_eng.xmv" };
    size_t n = strlen(path), i, k;
    for (i = 0; i < sizeof from / sizeof from[0]; i++) {
        size_t m = strlen(from[i]);
        if (n < m)
            continue;
        for (k = 0; k < m; k++)
            if (tolower((unsigned char)path[n - m + k]) != from[i][k])
                break;
        if (k == m)
            return "D:\\ovid\\cri_rw30.xmv";
    }
    return NULL;
}

/* ---- files ----------------------------------------------------------------- */

static int exists(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0;
}

static void rmtree(const char *p)
{
    DIR *d = opendir(p);
    struct dirent *e;
    char sub[1200];
    if (!d) {
        remove(p);
        return;
    }
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        if (snprintf(sub, sizeof sub, "%s/%s", p, e->d_name) < (int)sizeof sub)
            rmtree(sub);
    }
    closedir(d);
    sb_rmdir(p);
}

static void put_env(const char *name, const char *value)
{
#if defined(_WIN32)
    _putenv_s(name, value ? value : "");
#else
    if (value)
        setenv(name, value, 1);
    else
        unsetenv(name);
#endif
}

/* The cache directory the backends use (render.shader_cache through the
 * enhancements layer, else the per-user default). 0: the cache is off. */
static int cache_dir(char *out, size_t n)
{
    return nv2a_pcache_dir(nv2a_host_opts()->shader_cache, nv2a_pcache_app(), out, n);
}

/* <cache>/prebuild: the state file, the scratch saves, the logs. */
static int work_dir(char *out, size_t n)
{
    char c[1024];
    if (!cache_dir(c, sizeof c) || snprintf(out, n, "%s/prebuild", c) >= (int)n)
        return 0;
    sb_mkdir(out);
    return exists(out);
}

/* The state file: one fact a line. "scene <name>": that scene has been
 * played to its end; "built": all of them have. ("offer never" is what an
 * earlier version wrote for a first-start question that is gone; it is kept
 * across --force and otherwise ignored.) */
static int state_has(const char *work, const char *fact)
{
    char path[1200], line[256];
    FILE *f;
    int hit = 0;
    snprintf(path, sizeof path, "%s/state.txt", work);
    if (!(f = fopen(path, "r")))
        return 0;
    while (!hit && fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        hit = !strcmp(line, fact);
    }
    fclose(f);
    return hit;
}

static void state_add(const char *work, const char *fact)
{
    char path[1200];
    FILE *f;
    if (state_has(work, fact))
        return;
    snprintf(path, sizeof path, "%s/state.txt", work);
    if ((f = fopen(path, "a")) != NULL) {
        fprintf(f, "%s\n", fact);
        fclose(f);
    }
}

static void state_clear(const char *work)
{
    char path[1200], keep = 0;
    snprintf(path, sizeof path, "%s/state.txt", work);
    keep = (char)state_has(work, "offer never");
    remove(path);
    if (keep)
        state_add(work, "offer never");
}

/* What the cache directory holds, in bytes (its own files, not the scratch). */
static unsigned long long dir_bytes(const char *dir)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    unsigned long long sum = 0;
    char p[1200];
    struct stat st;
    if (!d)
        return 0;
    while ((e = readdir(d)) != NULL)
        if (snprintf(p, sizeof p, "%s/%s", dir, e->d_name) < (int)sizeof p
                && stat(p, &st) == 0 && (st.st_mode & S_IFMT) == S_IFREG)
            sum += (unsigned long long)st.st_size;
    closedir(d);
    return sum;
}

/* ---- a scene process ---------------------------------------------------------- */

/* How fast a scene may play, in times real time (0: as fast as it goes).
 * Not as fast as it goes: the title's frames then pass in a fraction of a
 * millisecond in the logos and the menus, and what the host does beside
 * them on its own clock (a file read, a thread's wake-up) is a hundred
 * frames late where it is two at real time; a scene on a busy machine then
 * took a menu differently once in five runs. At 8 every wait of the script
 * still lasts an eighth of what it does when a person plays, and the scenes
 * take a fifth longer than with no bound (on a Mac M5; a machine that cannot
 * play them 8 times faster loses nothing). */
#define SCENE_SPEED 8u
#define SCENE_SPEED_TEXT "8"

static const struct scene *s_scene;
static char s_fb_env[1100], s_at_env[1200];
static int s_shots;
static char s_dumpat[1100];
static int s_noseed;                    /* the manifest scene and the sweeps seed nothing from the executable */
static int s_sweeping;                  /* BO-208: a coverage sweep visit: nothing seeded from the executable */
static FILE *s_out;                     /* the build's pipe (our stdout as started) */
static char  s_work[1024], s_pad[1100], s_cancel[1100];
static pthread_mutex_t s_out_mu = PTHREAD_MUTEX_INITIALIZER;
static const char *s_xbe;
static int s_seeded;

#if defined(XBOXRECOMP_HAVE_VULKAN)
extern int nvk_core_is_software(void);          /* toolkit, src/d3d/nv2a_vk_core.c */
extern const char *nvk_core_describe(void);
#endif

/* A line to the build. A write that fails means it is gone: stop. */
static void say(const char *fmt, ...)
{
    va_list ap;
    int bad;
    pthread_mutex_lock(&s_out_mu);
    va_start(ap, fmt);
    vfprintf(s_out, fmt, ap);
    va_end(ap);
    fputc('\n', s_out);
    bad = fflush(s_out) != 0 || ferror(s_out);
    pthread_mutex_unlock(&s_out_mu);
    if (bad)
        _exit(5);
}

static int scene_cancelled(void *ctx)
{
    (void)ctx;
    return exists(s_cancel);
}

static void seed_one(void *ctx, const uint32_t *slots, uint32_t n)
{
    (void)ctx;
    nv2a_pcache_seed(NV2A_PC_VS, slots, n * 16u);
}

static void seed_fs(void *ctx, const void *state, uint32_t len)
{
    (void)ctx;
    nv2a_pcache_seed(NV2A_PC_FS, state, len);
}

/* The title's vertex programs and the combiner states of its pixel-shader
 * definitions, read out of its executable, into the backend's key list: its
 * pre-warm thread builds them now, and at every start with the rest. So a
 * material the scenes do not show (another track's) still has its shaders
 * built; only its pipelines are left for the first draw. Once the backend
 * is up (it comes up with the first draw). */
static void seed(void)
{
    nv2a_pcache_stat st;
    FILE *f;
    long size;
    void *b;
    size_t found, defs;

    nv2a_pcache_stat_get(&st);
    if (s_seeded || !st.active || !s_xbe || s_noseed)
        return;
    s_seeded = 1;
    if (!(f = fopen(s_xbe, "rb")))
        return;
    if (fseek(f, 0, SEEK_END) == 0 && (size = ftell(f)) > 0 && fseek(f, 0, SEEK_SET) == 0
            && (b = malloc((size_t)size)) != NULL) {
        if (fread(b, 1, (size_t)size, f) == (size_t)size) {
            found = nv2a_pcache_scan_vsh(b, (size_t)size, seed_one, NULL);
            defs = nv2a_pcache_scan_psdef(b, (size_t)size, seed_fs, NULL);
            nv2a_pcache_rescan();
            fprintf(stderr, "[SHADERBUILD] %zu vertex programs, %zu pixel shader definitions"
                    " in %s\n", found, defs, s_xbe);
            say("SB SEED %zu %zu", found, defs);
        }
        free(b);
    }
    fclose(f);
}

static void scene_mark(void *ctx, const char *text, int thousandths)
{
    (void)ctx;
    seed();
    if (s_sweeping && s_shots) {
        /* BO3_SWEEP_SHOTS=1: the frame three flips on is dumped (the backend
         * reads the flip numbers from this file: RECOMP_FB_DUMP_AT=@file). */
        FILE *df = fopen(s_dumpat, "a");
        if (df) {
            fprintf(df, "%u\n", (unsigned)(g_xbox_flip_count + 3));
            fclose(df);
        }
    }
    {
        /* BO-208: the counters at each mark, so a run says what was compiled
         * on the render thread between two of them (the acceptance check). */
        nv2a_pcache_stat ps;
        nv2a_pcache_stat_get(&ps);
        fprintf(stderr, "[SWEEP] mark '%s' flip %u: ack-thread compiles so far %u vs + %u fs + %u"
                " pipelines, pre-warm built %u + %u + %u\n", text ? text : "",
                (unsigned)g_xbox_flip_count, ps.ack[0], ps.ack[1], ps.ack[2], ps.warmed[0],
                ps.warmed[1], ps.warmed[2]);
        say("SB ACK %u %u %u warmed %u %u %u flip %u '%s'", ps.ack[0], ps.ack[1], ps.ack[2],
            ps.warmed[0], ps.warmed[1], ps.warmed[2], (unsigned)g_xbox_flip_count,
            text ? text : "");
    }
    say("SB MARK %d %s", thousandths, text ? text : "");
}

static void nap_ms(unsigned ms)
{
#if defined(_WIN32)
    Sleep(ms);
#else
    usleep(ms * 1000u);
#endif
}

static void scene_done(void *ctx, int ok, const char *why)
{
    nv2a_pcache_stat st;
    int i;
    (void)ctx;
    if (ok == 1) {
        uint32_t f0;
        /* What the scene compiled is in the lists; have the backend go over
         * them once more (the seeds, and on Vulkan its pipeline cache goes
         * to disk after a pre-warm), and give it the flips to do so. */
        seed();
        nv2a_pcache_rescan();
        for (i = 0; i < 3000; i++) {
            nv2a_pcache_stat_get(&st);
            if (!st.warming)
                break;
            nap_ms(100);
        }
        f0 = g_xbox_flip_count;
        for (i = 0; i < 50 && (uint32_t)(g_xbox_flip_count - f0) < 30u; i++)
            nap_ms(100);
        /* The 2D art the scene drew, scaled for graphics.ui_scale: the
         * worker has what is left of it written before the process goes. */
        if (nv2a_texscale_factor()) {
            struct nv2a_texscale_store_stats ts;
            int idle = nv2a_texscale_flush(60000);
            nv2a_texscale_store_stats(&ts);
            fprintf(stderr, "[SHADERBUILD] ui_scale %ux: %llu textures scaled, %llu already"
                    " in the cache, %.1f MB on disk%s\n", nv2a_texscale_factor(),
                    (unsigned long long)ts.misses, (unsigned long long)ts.hits,
                    (double)ts.dir_bytes / 1048576.0, idle ? "" : " (still working: cut short)");
        }
        nv2a_pcache_stat_get(&st);
        say("SB KEYS %u %u %u ack %u %u %u max_us %u warm_ms %u", st.listed[0], st.listed[1],
            st.listed[2], st.ack[0], st.ack[1], st.ack[2], st.ack_max_us, st.warm_ms);
        say("SB DONE ok");
        fflush(NULL);
        exit(0);
    }
    say("SB DONE %s %s", ok < 0 ? "cancelled" : "fail", why ? why : "");
    fflush(NULL);
    _exit(ok < 0 ? 4 : 3);
}

/* Once a second: a sign of life, which is also how a scene finds out that
 * the build is gone (the write fails) and stops. */
static void *scene_tick(void *arg)
{
    int told_software = 0;

    (void)arg;
    for (;;) {
        nv2a_pcache_stat st;
        nap_ms(1000);
#if defined(XBOXRECOMP_HAVE_VULKAN)
        /* The loader offered no GPU (on Linux: the user cannot open /dev/dri):
         * the scene runs on the CPU, and the build says so once. */
        if (!told_software && nvk_core_is_software()) {
            told_software = 1;
            say("SB SOFTWARE %s", nvk_core_describe());
        }
#else
        (void)told_software;
#endif
        nv2a_pcache_stat_get(&st);
        say("SB TICK %u %u", (unsigned)g_xbox_flip_count, st.ack[0] + st.ack[1] + st.ack[2]);
        if (st.hold_total)
            say("SB WARM %u %u", st.hold_done, st.hold_total);
    }
    return NULL;
}

/* graphics.ui_scale (toolkit nv2a_texscale.h): a scene runs without the
 * player's settings file, but the 2D art it draws is scaled into the cache
 * beside the shaders only if the factor reaches it. Read here, before the
 * scene's own environment replaces the player's: the variable if it is set,
 * else the key in the player's settings.toml. Off (the default): nothing. */
static void scene_ui_scale(char *out, size_t n)
{
    const char *env = getenv("RECOMP_GFX_UI_SCALE"), *over = getenv("RECOMP_SETTINGS"), *dir;
    char path[1200], err[200];
    recomp_cfg *cfg;

    out[0] = 0;
    if (env && *env) {
        snprintf(out, n, "%s", env);
        return;
    }
    if (over && !strcmp(over, "none"))
        return;
    if (over && *over) {
        snprintf(path, sizeof path, "%s", over);
    } else {
        /* Where the player's settings are, found without recomp_save_dir:
         * that one makes the folder and keeps the answer for the rest of
         * the process, and this scene's own save root (RECOMP_SAVE_DIR,
         * set below) would then never be used: the scene would run on the
         * player's save (BO-129, audit finding 8). */
        char root[1024];
        dir = getenv("RECOMP_SAVE_DIR");
        if (dir && *dir)
            snprintf(root, sizeof root, "%s", dir);
        else if (!recomp_user_dir(RECOMP_DIR_DATA, BO3_USER_DIR, root, sizeof root))
            return;
        snprintf(path, sizeof path, "%s/settings.toml", root);
    }
    cfg = recomp_cfg_load_file(path, err, sizeof err);
    if (!cfg)
        return;
    snprintf(out, n, "%s", recomp_cfg_string(cfg, "graphics.ui_scale", ""));
    recomp_cfg_free(cfg);
}

int bo3_shaderbuild_scene_env(const char *scene)
{
    const char *work = getenv("BO3_SHADERBUILD_DIR");
    char saves[1100], log[1100], ui_scale[16];
    int i;

    scene_ui_scale(ui_scale, sizeof ui_scale);
    for (i = 0; i < NSCENES; i++)
        if (!strcmp(k_scenes[i].name, scene))
            s_scene = &k_scenes[i];
    if (!strcmp(scene, "manifest"))
        s_noseed = 1;
    if (!strcmp(scene, "sweep") && getenv("BO3_SWEEP") && sweep_build(getenv("BO3_SWEEP"))) {
        static struct scene sw;
        sw.name = s_sw_name;
        sw.steps = s_sw;
        sw.secs = 60;
        s_scene = &sw;
        s_sweeping = 1;
        s_noseed = 1;
        if (!strncmp(getenv("BO3_SWEEP"), "auto", 4)) {
            const char *dbg = getenv("RECOMP_DEBUG");
            char buf[160];
            snprintf(buf, sizeof buf, "%s%sautopilot=1", dbg ? dbg : "", dbg && *dbg ? "," : "");
            put_env("RECOMP_DEBUG", buf);
        }
        put_env("BO3_UNLOCK_ALL", "1");
        if (getenv("BO3_SWEEP_SHOTS") && work && *work) {
            char fb[1100], at[1200];
            s_shots = 1;
            snprintf(s_dumpat, sizeof s_dumpat, "%s/dumpat.txt", work);
            snprintf(fb, sizeof fb, "%s/fb_", work);
            snprintf(at, sizeof at, "@%s", s_dumpat);
            { FILE *z = fopen(s_dumpat, "w"); if (z) fclose(z); }
            snprintf(s_fb_env, sizeof s_fb_env, "%s", fb);
            snprintf(s_at_env, sizeof s_at_env, "%s", at);
        }
    }
    if (!s_scene || !work || !*work) {
        XR_ERROR( "--build-shaders-scene is started by --build-shaders\n");
        return 0;
    }
    snprintf(s_work, sizeof s_work, "%s", work);
    snprintf(s_pad, sizeof s_pad, "%s/pad.txt", work);
    snprintf(s_cancel, sizeof s_cancel, "%s/cancel", work);
    snprintf(saves, sizeof saves, "%s/saves", work);
    snprintf(log, sizeof log, "%s/%s.log", work, scene);

    /* The build reads our stdout; everything the game prints goes to the
     * scene's log. */
    s_out = sb_fdopen(sb_dup(sb_fileno(stdout)), "w");
    if (!s_out)
        return 0;
    if (freopen(log, "w", stdout))
        sb_dup2(sb_fileno(stdout), sb_fileno(stderr));
#if !defined(_WIN32)
    signal(SIGPIPE, SIG_IGN);           /* a write to a build that is gone fails instead */
#endif

    /* Scratch saves, empty: the profile screens are then the same on every
     * machine, and the player's profile is not opened. */
    rmtree(saves);
    sb_mkdir(saves);
    {
        FILE *f = fopen(s_pad, "w");
        if (f)
            fclose(f);
    }
    put_env("RECOMP_STDIO_LOG", NULL);
    put_env("RECOMP_PAD_SCRIPT", NULL);
    put_env("RECOMP_FB_DUMP", s_shots ? s_fb_env : NULL);
    put_env("RECOMP_FB_DUMP_AT", s_shots ? s_at_env : NULL);
    put_env("RECOMP_HEADLESS", "1");
    put_env("RECOMP_SAVE_DIR", saves);
    put_env("RECOMP_SAVE_BACKUPS", "0");
    put_env("RECOMP_SETTINGS", "none");
    put_env("RECOMP_GFX_UI_SCALE", ui_scale[0] ? ui_scale : NULL);
    put_env("RECOMP_PAD_LIVE", s_pad);
    put_env("RECOMP_AUDIO_MUTE", "1");
    put_env("RECOMP_INPUT_RUMBLE", "0");
    put_env("RECOMP_OVERLAY", "0");
    xbox_PathRedirectHook = redirect;

    /* Nobody watches a scene, so it does not wait for the clock: a frame
     * that is done goes to its vblank at once, on every clock the title
     * reads (the toolkit's host_time.h). The build says how fast at most
     * (BO3_SHADERBUILD_SPEED: times real time, "max" for no bound, 1 for
     * real time as a game plays). Only here: no setting reaches this. */
    {
        const char *sp = getenv("BO3_SHADERBUILD_SPEED");
        unsigned speed = sp && *sp ? (!strcmp(sp, "max") ? 0u : (unsigned)strtoul(sp, NULL, 10))
                                   : SCENE_SPEED;
        if (speed != 1) {
            xbox_TimeWarpEnable(speed);
            fprintf(stderr, "[SHADERBUILD] the scene does not wait for the clock (at most %u"
                    " times real time; 0: no bound)\n", speed);
        }
    }
    return 1;
}

void bo3_shaderbuild_scene_start(const char *xbe_path)
{
    static xap_run run;
    pthread_t th;

    s_xbe = xbe_path;
    xbox_InputSetBlocked(1);            /* the player's pad is not the scene's */
    run.steps = s_scene->steps;
    run.pad_file = s_pad;
    run.pad_taken = usb_gamepad_live_taken;
    run.wait_timeout_s = 150;
    run.mark = scene_mark;
    run.done = scene_done;
    run.cancelled = scene_cancelled;
    if (!xbox_AutoplayStart(&run)) {
        say("SB DONE fail no driver thread");
        _exit(3);
    }
    if (pthread_create(&th, NULL, scene_tick, NULL) == 0)
        pthread_detach(th);
    say("SB START %s", s_scene->name);
}

/* ---- the build ---------------------------------------------------------------- */

static void out(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    /* The build's own output, not logging: through the real functions,
     * whatever the print gate says (xr_log.h). */
    (vfprintf)(stdout, fmt, ap);
    va_end(ap);
    (fputc)('\n', stdout);
    fflush(stdout);
}

/* The executable, quoted for the shell popen runs (Windows: the command
 * line CreateProcess takes). */
static int exe_command(char *out_cmd, size_t n, const char *args)
{
    const char *exe = recomp_exe_path();
    if (!exe || !*exe)
        return 0;
#if defined(_WIN32)
    /* The child's own command line (sb_popen_win starts it without cmd). */
    return snprintf(out_cmd, n, "\"%s\" %s", exe, args) < (int)n;
#else
    {
        size_t k = 0;
        const char *p;
        if (n < 8)
            return 0;
        out_cmd[k++] = '\'';
        for (p = exe; *p && k + 8 < n; p++) {
            if (*p == '\'') {
                memcpy(out_cmd + k, "'\\''", 4);
                k += 4;
            } else {
                out_cmd[k++] = *p;
            }
        }
        out_cmd[k++] = '\'';
        out_cmd[k] = 0;
        return snprintf(out_cmd + k, n - k, " %s", args) < (int)(n - k);
    }
#endif
}

/* One scene in its own process. 1 done, 0 failed (why), -1 cancelled.
 * Progress lines for the span [p0, p1] of the whole build. */
static int run_scene(const struct scene *sc, const char *work, double p0, double p1,
                     char *why, size_t why_n, unsigned keys[3], unsigned ack[4], int *software)
{
    char cmd[2400], args[128], line[512], cancel[1200];
    FILE *f;
    int done = 0, last = -1;
    double warm_f = 0;

    snprintf(cancel, sizeof cancel, "%s/cancel", work);
    snprintf(args, sizeof args, "--build-shaders-scene %s", sc->name);
    if (!exe_command(cmd, sizeof cmd, args) || !(f = sb_popen(cmd))) {
        snprintf(why, why_n, "cannot start %s", recomp_exe_path());
        return 0;
    }
    snprintf(why, why_n, "the scene's process ended early (see %s/%s.log)", work, sc->name);
    while (fgets(line, sizeof line, f)) {
        int th;
        line[strcspn(line, "\r\n")] = 0;
        if (!strncmp(line, "SB MARK ", 8)) {
            char *text = NULL;
            th = (int)strtol(line + 8, &text, 10);
            if (th != last && p0 + (p1 - p0) * (th / 1000.0) >= warm_f) {
                last = th;
                warm_f = p0 + (p1 - p0) * (th / 1000.0);
                out("PROGRESS " STEP " %.3f %s", p0 + (p1 - p0) * (th / 1000.0),
                    text && *text == ' ' ? text + 1 : sc->name);
            }
        } else if (!strncmp(line, "SB WARM ", 8)) {
            unsigned d = 0, t = 0;
            sscanf(line + 8, "%u %u", &d, &t);
            if (t && d < t) {
                char txt[64];
                double f = p0 + (p1 - p0) * 0.9 * ((double)d / t);
                snprintf(txt, sizeof txt, "Preparing graphics %u/%u", d, t);
                if (f >= warm_f) {      /* never back: the marks have their own scale */
                    warm_f = f;
                    out("PROGRESS " STEP " %.3f %s", f, txt);
                }
            }
        } else if (!strncmp(line, "SB KEYS ", 8)) {
            unsigned wms = 0;
            sscanf(line + 8, "%u %u %u ack %u %u %u max_us %u warm_ms %u", &keys[0], &keys[1], &keys[2],
                   &ack[0], &ack[1], &ack[2], &ack[3], &wms);
            if (wms > 1)
                out("LOG " STEP " the graphics were built in %.1f s", wms / 1000.0);
        } else if (!strncmp(line, "SB SOFTWARE", 11)) {
            if (!*software) {
                *software = 1;
                out("WARN " STEP " no-gpu %s", line[11] == ' ' ? line + 12 : "software renderer");
            }
        } else if (!strncmp(line, "SB SEED ", 8)) {
            unsigned v = 0, d = 0;
            sscanf(line + 8, "%u %u", &v, &d);
            out("LOG " STEP " %u vertex programs and %u pixel shader definitions found in the"
                " game's executable", v, d);
        } else if (!strncmp(line, "SB DONE ", 8)) {
            if (!strncmp(line + 8, "ok", 2)) {
                done = 1;
            } else if (!strncmp(line + 8, "cancelled", 9)) {
                done = -1;
            } else {
                done = 0;
                snprintf(why, why_n, "%s", line + 8 + (strncmp(line + 8, "fail ", 5) ? 0 : 5));
            }
        }
    }
    sb_pclose(f);
    if (done == 0 && exists(cancel))
        done = -1;
    return done;
}

static void usage(void)
{
    XR_ERROR(
        "usage: burnout3 --build-shaders [options]\n"
        "  Plays the reference scenes headless and leaves the shaders and pipelines they\n"
        "  need compiled, so the first races do not stop to compile them. Optional: the\n"
        "  game works without it. Progress on stdout, one line each:\n"
        "    PROTOCOL 1 / PLAN shaders / STEP shaders BEGIN|OK|SKIP|FAIL ... /\n"
        "    PROGRESS shaders <0..1> <text> / LOG shaders <text> / RESULT OK|FAIL ...\n"
        "  --cache-dir <dir>  the shader cache to fill (default: the game's own, as\n"
        "                     render.shader_cache or the per-user cache directory)\n"
        "  --scenes a,b       only these scenes (race, crash)\n"
        "  --force            play scenes again that an earlier build finished\n"
        "  --status           say what is built and stop (no scene is played)\n"
        "  --speed <n>|max    how fast the scenes may play, in times real time: 1 is a\n"
        "                     game's own pace (4 minutes), max has no bound (default: %s)\n"
        "  Stop it at any time (or create <cache>/prebuild/cancel): a later run goes on\n"
        "  with the scenes that are left.\n", SCENE_SPEED ? SCENE_SPEED_TEXT : "max");
}

int bo3_shaderbuild_main(int argc, char **argv, const char *xbe_path)
{
    char cdir[1024], work[1100], path[1200], why[400] = "";
    const char *only = NULL, *speed = NULL;
    int force = 0, status = 0, i, todo = 0, total = 0, secs_all = 0, secs_done = 0;
    unsigned keys[3] = {0, 0, 0}, ack[4] = {0, 0, 0, 0};
    int software = 0;                   /* a scene told us it runs on the CPU */
    time_t t0 = time(NULL);

    for (i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "--scenes") && i + 1 < argc)
            only = argv[++i];
        else if (!strcmp(argv[i], "--cache-dir") && i + 1 < argc)
            i++;                        /* main() put it in the environment */
        else if (!strcmp(argv[i], "--data") && i + 1 < argc)
            i++;                        /* main() took it: the data root, after the word too */
        else if (!strcmp(argv[i], "--force"))
            force = 1;
        else if (!strcmp(argv[i], "--status"))
            status = 1;
        else if (!strcmp(argv[i], "--speed") && i + 1 < argc
                 && (!strcmp(argv[i + 1], "max") || strtoul(argv[i + 1], NULL, 10) >= 1))
            speed = argv[++i];
        else {
            usage();
            return 2;
        }
    }
    setvbuf(stdout, NULL, _IOLBF, 0);
    out("PROTOCOL 1");
    out("PLAN " STEP);
    if (!cache_dir(cdir, sizeof cdir) || !work_dir(work, sizeof work)) {
        out("STEP " STEP " FAIL cache-off the shader cache is off (render.shader_cache = none)"
            " or its directory cannot be made");
        out("RESULT FAIL " STEP " cache-off");
        return 1;
    }
    if (!exists(xbe_path)) {
        out("STEP " STEP " FAIL no-game-data %s is not here: run it from the folder that holds"
            " the game's files", xbe_path);
        out("RESULT FAIL " STEP " no-game-data");
        return 1;
    }
    if (force)
        state_clear(work);
    for (i = 0; i < NSCENES; i++) {
        char fact[64];
        int want = only ? strstr(only, k_scenes[i].name) != NULL
                        : (bo3_pcache_manifest_present() ? !strcmp(k_scenes[i].name, "manifest")
                                                         : k_scenes[i].legacy);
        snprintf(fact, sizeof fact, "scene %s", k_scenes[i].name);
        if (!want)
            continue;
        total++;
        secs_all += k_scenes[i].secs;
        if (!state_has(work, fact))
            todo++;
        else
            secs_done += k_scenes[i].secs;
    }
    if (status) {
        out("LOG " STEP " cache %s", cdir);
        out("LOG " STEP " %d of %d scenes built, %llu bytes on disk", total - todo, total,
            dir_bytes(cdir));
        out("RESULT OK %s", cdir);
        return todo ? 10 : 0;
    }
    if (!todo) {
        out("STEP " STEP " SKIP already built (%d scenes, %llu bytes in %s); --force builds"
            " again", total, dir_bytes(cdir), cdir);
        out("RESULT OK %s", cdir);
        return 0;
    }
    {
        /* The scenes' seconds are those of SCENE_SPEED; at real time they
         * take four times as long. */
        int secs = (secs_all - secs_done) * (speed && !strcmp(speed, "1") ? 4 : 1);
        if (!only && bo3_pcache_manifest_present())
            out("STEP " STEP " BEGIN Preparing graphics: building every shader and pipeline the"
                " game needs, without a window or sound (a few seconds)");
        else
            out("STEP " STEP " BEGIN Building shaders: the game plays %d scene%s by itself,"
                " without a window or sound (about %d minute%s)", todo, todo == 1 ? "" : "s",
                (secs + 59) / 60, secs > 60 ? "s" : "");
    }
    out("LOG " STEP " cache %s", cdir);
    snprintf(path, sizeof path, "%s/cancel", work);
    remove(path);
    put_env("BO3_SHADERBUILD_DIR", work);
    put_env("RECOMP_RENDER_SHADER_CACHE", cdir);
    put_env("BO3_SHADERBUILD_SPEED", speed);    /* none: the scenes' own */

    for (i = 0; i < NSCENES; i++) {
        const struct scene *sc = &k_scenes[i];
        char fact[64];
        double p0, p1;
        int rc, tries;
        snprintf(fact, sizeof fact, "scene %s", sc->name);
        if ((only ? !strstr(only, sc->name)
                  : (bo3_pcache_manifest_present() ? strcmp(sc->name, "manifest") : !sc->legacy))
                || state_has(work, fact))
            continue;
        p0 = (double)secs_done / secs_all;
        p1 = (double)(secs_done + sc->secs) / secs_all;
        out("PROGRESS " STEP " %.3f %s", p0, sc->name);
        /* A scene that lost its way (a press a menu did not take) is played
         * once more; what it compiled before that is kept either way. */
        for (tries = 0, rc = 0; tries < 2 && rc == 0; tries++) {
            if (tries)
                out("LOG " STEP " %s: %s; once more", sc->name, why);
            rc = run_scene(sc, work, p0, p1, why, sizeof why, keys, ack, &software);
        }
        if (rc != 1) {
            const char *code = rc < 0 ? "cancelled" : "scene-failed";
            snprintf(path, sizeof path, "%s/saves", work);
            rmtree(path);
            snprintf(path, sizeof path, "%s/cancel", work);
            remove(path);
            out("STEP " STEP " FAIL %s %s: %s; what was compiled so far is kept, and a later"
                " run goes on from here", code, sc->name, rc < 0 ? "stopped" : why);
            out("RESULT FAIL " STEP " %s", code);
            return 1;
        }
        state_add(work, fact);
        secs_done += sc->secs;
        /* The scene's log (the whole run's, tens of megabytes) is only kept
         * when the scene failed. */
        snprintf(path, sizeof path, "%s/%s.log", work, sc->name);
        remove(path);
        out("LOG " STEP " %s: done; the list holds %u vertex, %u fragment, %u pipelines"
            " (%u compiled in this scene)", sc->name, keys[0], keys[1], keys[2],
            ack[0] + ack[1] + ack[2]);
    }
    snprintf(path, sizeof path, "%s/saves", work);
    rmtree(path);
    snprintf(path, sizeof path, "%s/pad.txt", work);
    remove(path);
    if (!only)
        state_add(work, "built");
    out("PROGRESS " STEP " 1.000 done");
    out("STEP " STEP " OK %u vertex, %u fragment, %u pipelines; %llu bytes in %s; %ld s",
        keys[0], keys[1], keys[2], dir_bytes(cdir), cdir, (long)(time(NULL) - t0));
    out("RESULT OK %s", cdir);
    return 0;
}

/* ---- the running game: the overlay's action and the first start's offer ------- */

#ifdef XBOXRECOMP_HAVE_OVERLAY

static pthread_mutex_t s_host_mu = PTHREAD_MUTEX_INITIALIZER;
static int   s_host_state;              /* xbox_shader_build_state */
static float s_host_fraction;
static char  s_host_text[160];
static char  s_host_work[1100];
static int   s_host_finishing;          /* the build ended: the backend's rescan is running */
static char  s_host_args[160];          /* BO3_SHADERBUILD_TEST's command line */

static void host_set(int state, float fraction, const char *text)
{
    pthread_mutex_lock(&s_host_mu);
    s_host_state = state;
    if (fraction >= 0)
        s_host_fraction = fraction;
    if (text)
        snprintf(s_host_text, sizeof s_host_text, "%s", text);
    pthread_mutex_unlock(&s_host_mu);
}

/* The build as a child, its lines read here. */
static void *host_run(void *arg)
{
    char cmd[2400], line[512];
    FILE *f;
    int ok = 0;
    (void)arg;
    /* "Build shaders again" plays the scenes again; a build that was
     * stopped goes on with the ones that are left. */
    if (!exe_command(cmd, sizeof cmd, s_host_args[0] ? s_host_args
                                      : state_has(s_host_work, "built")
                                      ? "--build-shaders --force" : "--build-shaders")
            || !(f = sb_popen(cmd))) {
        host_set(XBOX_SHADERS_FAILED, 0, "could not start the build");
        return NULL;
    }
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!strncmp(line, "PROGRESS " STEP " ", 10 + sizeof STEP - 1)) {
            char *text = NULL;
            double fr = strtod(line + 10 + sizeof STEP - 1, &text);
            host_set(XBOX_SHADERS_RUNNING, (float)fr, text && *text == ' ' ? text + 1 : "");
        } else if (!strncmp(line, "STEP " STEP " FAIL ", 11 + sizeof STEP - 1)) {
            /* A line the overlay's status row has room for. */
            const char *why = line + 11 + sizeof STEP - 1;
            host_set(XBOX_SHADERS_RUNNING, -1, !strncmp(why, "cancelled", 9)
                     ? "stopped; the next build goes on from there"
                     : "a scene failed; its log is in the cache folder's prebuild/");
        } else if (!strncmp(line, "RESULT OK", 9)) {
            ok = 1;
        }
    }
    sb_pclose(f);
    if (ok) {
        /* The backend reads its lists again and builds what is new to it on
         * its pre-warm thread (on Metal that compiles in this process once
         * more, some seconds): "built" is said when that is done too. */
        nv2a_pcache_rescan();
        pthread_mutex_lock(&s_host_mu);
        s_host_finishing = 1;
        s_host_fraction = 0.99f;
        snprintf(s_host_text, sizeof s_host_text, "loading them into the running game");
        pthread_mutex_unlock(&s_host_mu);
    } else {
        pthread_mutex_lock(&s_host_mu);
        s_host_state = XBOX_SHADERS_FAILED;
        pthread_mutex_unlock(&s_host_mu);
    }
    return NULL;
}

static int host_start(void)
{
    pthread_t th;
    char cancel[1200];
    pthread_mutex_lock(&s_host_mu);
    if (s_host_state == XBOX_SHADERS_RUNNING || !s_host_work[0]) {
        pthread_mutex_unlock(&s_host_mu);
        return 0;
    }
    s_host_state = XBOX_SHADERS_RUNNING;
    s_host_fraction = 0;
    snprintf(s_host_text, sizeof s_host_text, "starting");
    pthread_mutex_unlock(&s_host_mu);
    snprintf(cancel, sizeof cancel, "%s/cancel", s_host_work);
    remove(cancel);
    /* BO-110: the HDR variants too, only where they can be used: the
     * display can show HDR (the Display page has the row) or HDR is on. */
    put_env("RECOMP_PCACHE_HDR",
            nv2a_host_hdr_potential() > 1.0f || nv2a_host_hdr_active() ? "1" : NULL);
    if (pthread_create(&th, NULL, host_run, NULL) != 0) {
        host_set(XBOX_SHADERS_FAILED, 0, "could not start the build");
        return 0;
    }
    pthread_detach(th);
    return 1;
}

static void host_cancel(void)
{
    char cancel[1200];
    FILE *f;
    if (!s_host_work[0])
        return;
    snprintf(cancel, sizeof cancel, "%s/cancel", s_host_work);
    if ((f = fopen(cancel, "w")) != NULL)
        fclose(f);
}

static int host_poll(float *fraction, char *text, unsigned n)
{
    int st;
    pthread_mutex_lock(&s_host_mu);
    if (s_host_finishing) {
        nv2a_pcache_stat pst;
        nv2a_pcache_stat_get(&pst);
        if (!pst.warming) {
            s_host_finishing = 0;
            s_host_state = XBOX_SHADERS_DONE;
            s_host_fraction = 1.0f;
            snprintf(s_host_text, sizeof s_host_text, "built");
        }
    }
    st = s_host_state;
    if (fraction)
        *fraction = s_host_fraction;
    if (text && n)
        snprintf(text, n, "%s", s_host_text);
    pthread_mutex_unlock(&s_host_mu);
    if (st == XBOX_SHADERS_IDLE && s_host_work[0] && state_has(s_host_work, "built"))
        st = XBOX_SHADERS_DONE;
    return st;
}

void bo3_shaderbuild_host_init(int headless)
{
    static xbox_shader_build_ops ops;
    const char *test = getenv("BO3_SHADERBUILD_TEST");

    (void)headless;
    if (!work_dir(s_host_work, sizeof s_host_work))
        s_host_work[0] = 0;             /* the cache is off: nothing to build into */
    ops.start = host_start;
    ops.cancel = host_cancel;
    ops.poll = host_poll;
    ops.cost = "Plays a race and a crash event by itself, a minute or two, and uses a few MB"
               " of disk. Afterwards the first races don't hitch while shaders compile.";
    xbox_OverlaySetShaderBuild(&ops);
    /* BO3_SHADERBUILD_TEST=start[:<options>]: what the overlay's action
     * does, without a window to press it in (a headless test of this path). */
    if (test && !strncmp(test, "start", 5)) {
        if (test[5] == ':')             /* start:<options>, e.g. start:--scenes race */
            snprintf(s_host_args, sizeof s_host_args, "--build-shaders %s", test + 6);
        host_start();
    }
}

#else

void bo3_shaderbuild_host_init(int headless)
{
    (void)headless;
}

#endif
