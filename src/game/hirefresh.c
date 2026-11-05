/*
 * hirefresh.c - pictures between the simulation's steps.
 *
 * graphics.high_refresh (overlay Display page, "High refresh rate";
 * RECOMP_GFX_HIGH_REFRESH): frames at the display's refresh rate, the
 * simulation at its own 60 steps a second. Active while the game is in its
 * running state (the game object's state, 0x4D53B4, is 5: the front end,
 * races, the pause menu) with one player; the other states (loads, the
 * stage's set-up and hand-overs) step once a frame by their own code
 * (Update's jump table, 0x16CC8) and keep flips at 60. The title's own
 * 30 Hz phases (the half rate, asked 0x4D538D or in force 0x4D538E: crash
 * cameras without graphics.crash_60fps, split screen without
 * graphics.split_60fps), split screen at any rate (two players,
 * 0x73A1C0) and the front end (no stage mode object, 0x4D5370 + 0x1B8:
 * its showroom draws several cars through the same frames in a picture)
 * run as shipped: the title's steps and its interval, on the vblank. The rate is the display's the window is on (the toolkit's
 * xbox_DisplayRate: its measured refresh, else the display mode's), or
 * RECOMP_FLIP_PACING=host:<Hz> for a run without a display; the toolkit
 * holds each flip to a host grid at it, on the display's refreshes
 * (xbox_FlipHostRate, flip_grid.h). Thirteen wraps (recomp/recomp_manual.c).
 *
 * 1. The steps of a frame. CB3Game::Update (sub_000165F0) runs the steps
 *    in the word at 0x4D53AC (game object 0x4A71A0 + 0x2E20C), which the
 *    stepper sub_001B58E0 at the end of the previous frame chose: at least
 *    1 (the call site's minimum, 0x16C2D), so a host flipping faster than
 *    60 runs the game fast. The step loop itself (0x16AB6) skips a count
 *    of 0. The title's clock cannot simply be given a minimum of 0: its
 *    period (timer 0x4D5338 + 0x20, 733.33 MHz ticks) is 1/60 s in a stage
 *    but 1/30 s in the front end, where the minimum alone makes 60 steps a
 *    second (with a minimum of 0 the menus ran at 30.00), it counts no time
 *    before its first step (0x1B58FB), and a paused race holds it (+0x34),
 *    when it returns the minimum every frame: the pause menu, which runs in
 *    the steps (the stage's update is skipped while a player has paused,
 *    0x16BA1), ran at 142 steps a second at 175 Hz. So the title's stepper
 *    runs as shipped and its count is not used: at the start of Update the
 *    count is the steps that bring the simulation up to the picture's time,
 *    0 when it is there already, at most 4. The menus' mode 2 keeps the lag
 *    beyond the cap, as the title does; the others drop it.
 *
 *    The picture's time is the host time the frame will be shown at: the
 *    edge of the toolkit's grid its swap will be held to while the machine
 *    keeps up (xbox_FlipHostGridState: the last swap's edge, the period, the
 *    swaps seen; the title's own swaps, counted at sub_00350DB0, say how
 *    many frames are on their way), and when it does not, the refresh the
 *    frame will reach, learned from when the last ones were shown
 *    (picture_ns). Each picture is then as many refreshes after the one
 *    before as the display shows it later, and the simulation is stepped up to the
 *    frame's flip, not to when Update ran: the blend below is at most one
 *    step behind the newest step, the one step of delay an interpolation
 *    cannot avoid, and nothing more (no frame of lag from the wall clock).
 *
 * 2. The picture between two steps. RenderWare 3.7's _rwFrameSyncDirty
 *    (sub_001E15E0; RwCameraBeginUpdate, sub_001D91B0, calls it before the
 *    device builds the view from the camera frame's LTM) walks the dirty
 *    roots at 0x75935C (each frame's +0x08 link) and brings their LTMs
 *    (+0x50, an RwMatrix: right, flags, up, at, pos) up to date. After it,
 *    the LTM of every frame under those roots is taken as the frame's state
 *    at the global step count (0x4A1EB4), the one before kept; and every
 *    frame that moved at the last step gets, in place, its two states
 *    blended at the picture's time, between the last two steps. A frame's
 *    state is taken once a step: the first sync after the steps is the
 *    truth, and a later one in the same picture can be an LTM rebuilt from
 *    a parent that already holds a blend, which must not be put back as
 *    the true value. Position by lerp, the axes by lerp and Gram-Schmidt
 *    at their lerped lengths. A move of more than SNAP_DIST a step or an
 *    axis turned more than ~45 degrees (a camera cut, a reset, a frame
 *    whose address was reused) is not blended. Everything the frame draws
 *    or reads after that (the camera's view, the atomics, a particle placed
 *    on a car) sees the blend; the view's own view matrix, which the rival
 *    marker projects with, is rebuilt from the camera's blend (blend_view).
 *    At the start of the next Update, before any
 *    step, the blends are put back to the true LTMs, so the simulation
 *    never reads one: skipped where the LTM no longer holds what was
 *    written (the frame is gone or was synced again).
 *
 * 3. The HUD (sub_000535E0, stdcall, its one argument the time to advance
 *    by) is updated by the one-player stages' step (sub_0001A130,
 *    sub_0001AC30: vtable + 8, called once a step from CB3Game::Update's
 *    step loop) with one step's time, MEMF(0x4AE1FC): once a step, so it
 *    runs as shipped. Its time used to be scaled by the picture's time
 *    since the last over 1/60 s, which with pictures between the steps
 *    gave it 60/Hz of its time (BO-275/BO-280: 2076 calls over 2075 steps
 *    at 120, 34.6 s given, 21.6 s after the scaling).
 *
 * 4. What else the title advances once a picture instead of once a step
 *    (the BO-275 audit), each put to once a step: the game's random
 *    generator (game + 0x2AE38, 0x4D1FD8) and the profile's time played
 *    (*(0x3C838C) + 0x18), both at the top of CB3Game::Update (bo3_update);
 *    the race audio manager sub_0013EF40 (bo3_race_audio) and with it the
 *    audio world's update sub_001CA530 (bo3_audio_world: the two hand the
 *    streamed sounds over to each other, BO-297); the effects clock at
 *    0x5A9A00 that the race-end flash runs on (bo3_fx_clock).
 *
 * Not blended: what is not on a frame (particles, skid marks, the
 * deformed car body's vertices), which moves once a step.
 */
#include "hirefresh.h"
#include "hraudit.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

#include "recomp_types.h"
#include "race_module.h"
#include "platform/recomp_env.h"
#include "kernel/kernel_pacing.h"
#include "platform/host_time.h"
#include "region.h"

#define STEP_S       (1.0 / 60.0)
#define TIMER        BO3_VA(0x4D5338u, 0x4D5658u, 0x4D56F8u)     /* the stepper's timer object */
#define FRAME_STEPS  BO3_VA(0x4D53ACu, 0x4D56CCu, 0x4D576Cu)     /* steps this frame's Update runs */
#define HALF_ASKED   BO3_VA(0x4D538Du, 0x4D56ADu, 0x4D574Du)     /* the half rate asked for */
#define HALF_RATE    BO3_VA(0x4D538Eu, 0x4D56AEu, 0x4D574Eu)     /* the half rate in force */
#define PLAYERS      BO3_VA(0x73A1C0u, 0x73A4D0u, 0x73A570u)     /* players at once: 2 in split screen */
#define STAGE        BO3_VA(0x4D5370u, 0x4D5690u, 0x4D5730u)     /* the stage; its mode object at + 0x1B8, 0 in the front end */
#define STEP_COUNT   BO3_VA(0x4A1EB4u, 0x4A21D4u, 0x4A2274u)     /* global step counter */
#define GAME_STATE   BO3_VA(0x4D53B4u, 0x4D56D4u, 0x4D5774u)     /* the game object's state, 5 running */
#define DIRTY_HEAD   BO3_VA(0x75935Cu, 0x75967Cu, 0x75971Cu)     /* RenderWare's dirty frame list */
#define RACERS       BO3_VA(0x64B38Cu, 0x64B69Cu, 0x64B73Cu)     /* the racer table, 0x30 a racer */
#define RACERS_N     BO3_VA(0x73A19Cu, 0x73A4ACu, 0x73A54Cu)
#define FRAME_LTM    0x50u
#define FRAME_CHILD  0x98u
#define FRAME_NEXT   0x9Cu

#define SNAP_DIST    8.0f          /* world units a step */
#define SNAP_DOT     0.7f          /* cos of the largest turn a step */

#define REC_N        32768u        /* power of two */
#define WALK_MAX     4096u

typedef struct {
    uint32_t key;                  /* the frame's address, 0 free */
    uint32_t sp, sc;               /* the step counts of prev and cur */
    uint32_t put_gen, put;         /* its s_put entry, while put_gen is s_put_gen */
    uint32_t veh;                  /* a vehicle's matrix (blend_matrix), not a frame: apply() leaves it */
    uint32_t snap_gen;             /* snap is this picture's, while snap_gen is s_put_gen */
    float    snap[16];             /* a vehicle's matrix after the steps, before anything was drawn */
    float    prev[16], cur[16];
} rec_t;

static rec_t    s_rec[REC_N];
static uint32_t s_live[REC_N];     /* records whose cur is the last step's */
static unsigned s_live_n;
static uint32_t s_live_step;
static struct { uint32_t va; float wrote[16], wrote1[16], cur[16], snap[16]; } s_put[REC_N];
static unsigned s_put_n;
static uint32_t s_put_gen = 1;
static unsigned s_rec_used;

static int      s_active;          /* this frame's steps are ours */
static double   s_rate;            /* the flips' rate while active, Hz */
static int      s_rendering;       /* a camera has begun since Update */
static double   s_lag;             /* the picture's time past the step before the last, s (0, STEP_S] */
static double   s_ahead;           /* the picture's time past the last step, s */
static uint64_t s_pic_ns;          /* the last picture's time, host ns */
static double   s_pic_dt;          /* the picture's time since the last, s */
static uint32_t s_swaps;           /* the title's swaps issued (sub_00350DB0) */
static int      s_in_update;       /* inside the title's Update (its steps, then the render) */
static uint32_t s_upd_steps;       /* the steps this Update runs */
static uint32_t s_updates;         /* Updates so far, active or not: a picture's number */

/* What a diagnostic log says every 10 s, only when something was off: frames
 * found elsewhere within a step, and the pictures between steps whose view
 * camera was not blended (the world then moves only at the steps). */
enum { OWN_CAR, OWN_RIVAL, OWN_TRAFFIC, OWN_CAMERA, OWN_VIEW, OWN_FRAME, OWN_N };
static const char *const k_own[OWN_N] = { "car", "rival", "traffic", "camera", "view", "frame" };
static struct {
    unsigned ovr[OWN_N], owned[OWN_N];
    unsigned skips, pics, cam_off, upd[5], periods;
    unsigned cube_drawn, cube_skipped, cube_moved;  /* the reflection cube (bo3_reflection_cube) */
    uint32_t skip_va, cam_va;
    float    skip_d;
    double   t0;
} s_diag;
static uint32_t s_steps_end;       /* the step count once this Update's steps are done */

/* The diagnostic lines (diagnostic logging on): to stderr, and to
 * <save dir>/logs/hirefresh.log as well, which keeps the whole session's
 * (the run log keeps a head and a tail only). The last run's is kept as
 * hirefresh.prev.log. At most 1 MB a run. */
const char *xbox_crash_log_dir(void);

static void hr_log(const char *fmt, ...)
{
    static FILE *f;
    static int tried;
    static long n;
    va_list ap;

    if (!XR_DIAG_ON())
        return;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    if (!tried) {
        const char *d = xbox_crash_log_dir();
        tried = 1;
        if (d && *d) {
            char a[1200], b[1200];
            snprintf(a, sizeof a, "%s/hirefresh.log", d);
            snprintf(b, sizeof b, "%s/hirefresh.prev.log", d);
            remove(b);
            rename(a, b);
            f = fopen(a, "w");
        }
    }
    if (f && n < (1L << 20)) {
        va_start(ap, fmt);
        n += (vfprintf)(f, fmt, ap);
        va_end(ap);
        fflush(f);
    }
}

static int env_on(recomp_env_id id)
{
    const char *v = recomp_env(id);
    return v && *v && strcmp(v, "0") != 0;
}

/* The rate to draw at, 0 for the title's own: the setting on, a display
 * faster than 60, the game in its running state in a stage (not the front
 * end) with one player, and the title not in its half rate. */
static double wanted_rate(void)
{
    static double env_hz = -1.0;
    double hz;

    if (!env_on(RENV_GFX_HIGH_REFRESH) || MEM32(GAME_STATE) != 5)
        return 0.0;
    if (MEM8(HALF_ASKED) || MEM8(HALF_RATE) || MEM32(PLAYERS) >= 2)
        return 0.0;
    /* The front end as shipped: its showroom (Select Car) draws several
     * cars through the same frames within one picture, which a blend
     * between two steps' states puts in each other's place (BO-241). */
    {
        uint32_t st = MEM32(STAGE);
        if (st < 0x10000u || st >= 0x08000000u || !MEM32(st + 0x1B8u))
            return 0.0;
    }
    if (env_hz < 0.0) {
        const char *e = recomp_env(RENV_FLIP_PACING);
        env_hz = e && !strncmp(e, "host:", 5) ? atof(e + 5) : 0.0;
    }
    hz = env_hz > 0.0 ? env_hz : xbox_DisplayRate();
    return hz > 61.0 && hz <= 1000.0 ? hz : 0.0;
}

static double host_s(void)
{
#ifdef _WIN32
    LARGE_INTEGER c, f;
    QueryPerformanceCounter(&c);
    QueryPerformanceFrequency(&f);
    return (double)c.QuadPart / (double)f.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
#endif
}

static float *ltm(uint32_t frame)
{
    return (float *)(uintptr_t)XBOX_PTR(frame + FRAME_LTM);
}

/* ── 1. The steps ─────────────────────────────────────────── */

static void campt_restore(void);

static FILE *dbg_file(void);
static int owner_of(uint32_t mat);
static rec_t *rec_find(uint32_t va);
static void restore_blends(void)
{
    unsigned i;

    campt_restore();

    for (i = 0; i < s_put_n; i++) {
        float *m = ltm(s_put[i].va);
        if (!memcmp(m, s_put[i].wrote, sizeof s_put[i].wrote)
                || !memcmp(m, s_put[i].wrote1, sizeof s_put[i].wrote1)) {
            memcpy(m, s_put[i].cur, sizeof s_put[i].cur);
        }
        /* A vehicle left with something else than the steps' snapshot: the
         * title changed it after the steps (after our blend, or before and
         * nothing of ours since) and owns it; it stays, counted. */
        {
            rec_t *rv = rec_find(s_put[i].va);
            if (rv && rv->veh && memcmp(m, s_put[i].snap, sizeof s_put[i].snap))
                s_diag.owned[owner_of(s_put[i].va + FRAME_LTM)]++;
        }
        if (memcmp(m, s_put[i].cur, sizeof s_put[i].cur) && dbg_file())
            fprintf(dbg_file(), "X %u %08x %.4f %.4f %.4f vs %.4f %.4f %.4f d %.4f\n", s_swaps, s_put[i].va + FRAME_LTM,
                    m[12], m[13], m[14], s_put[i].cur[12], s_put[i].cur[13], s_put[i].cur[14],
                    sqrtf((m[12] - s_put[i].cur[12]) * (m[12] - s_put[i].cur[12])
                          + (m[13] - s_put[i].cur[13]) * (m[13] - s_put[i].cur[13])
                          + (m[14] - s_put[i].cur[14]) * (m[14] - s_put[i].cur[14])));
    }
    s_put_n = 0;
    s_put_gen++;
}

/* The picture's time: when the frame being built will be on the glass.
 *
 * The toolkit holds each swap to the grid's next edge (xbox_FlipHostGridState:
 * the last swap's edge, the period, the swaps seen); the swaps the title has
 * issued beyond those are on their way, each an edge later. That is when a
 * frame is shown while the machine keeps up. When it does not (the Deck at
 * 90 Hz: the executor's 9-10 ms and the title's own work), a frame comes to
 * the executor after its edge and is shown a refresh or more later than that
 * edge said, with the positions of the earlier time; the next ones in
 * flight with it. Each picture then moves the cars by the wrong amount for
 * the time it is on screen (0.2 to 0.8 of a step a picture after a crash
 * camera, where 0.67 is even).
 *
 * So the time is learned from when frames were shown: the toolkit says, by
 * swap, when each reached the display (the presenter's present_wait times;
 * without them the hand-over: its edge, or the edge it reached when it came
 * more than half a period late; xbox_FlipHostShownAt). From the last frames
 * the time from the start of Update to the glass (median of 9), and the
 * glass's offset from the edges (median of the last 16 frames handed over
 * on time). The
 * frame's glass time is then the refresh nearest now + that time, never
 * before the last frame shown + a refresh for each frame since, nor before
 * the picture before. The picture's time is that less the
 * offset: while the machine keeps up, exactly the edge as before.
 * BO3_HIREFRESH_PREDICT=edge: the edge alone (the A/B arm). */
#define PRED_N   32u               /* power of two */
#define HIST_L   9u
#define HIST_D   16u
static struct { uint32_t sw; uint64_t upd, pic; } s_pred[PRED_N];
static struct {
    int64_t  l[HIST_L], d[HIST_D];   /* glass - Update; glass - edge, on-time frames */
    unsigned nl, nd, il, id, nall;
    int64_t  dmin;                   /* glass - edge, the least of all frames */
    int      real;                   /* the source: display times */
    uint32_t last_sw;                /* the last swap whose glass is known */
    uint64_t last_shown;
    double   d0;                     /* the glass's offset in force, ns */
    int      d0_set;
    uint64_t last_p;                 /* the last picture's glass time */
    uint32_t last_p_sw;
} s_lrn;
/* BO3_HIREFRESH_STATS: each picture's time against the edge its swap got and
 * against when it was shown (less the offset), and the frames on their way. */
static struct {
    unsigned n, off, nmin, nmax, gn, goff, grefr;
    double off_max_ms, goff_max_ms;
} s_pst = { 0, 0, 99, 0, 0, 0, 0, 0.0, 0.0 };
static FILE *trace_file(void);
static void marks_probe(void);

static int predict_edge_only(void)
{
    static int v = -1;
    if (v < 0) {
        const char *e = getenv("BO3_HIREFRESH_PREDICT");
        v = e && !strcmp(e, "edge");
        if (v)
            fprintf(stderr, "[HIREFRESH] picture time: the predicted edge alone"
                            " (BO3_HIREFRESH_PREDICT=edge)\n");
    }
    return v;
}

static void learn_reset(void)
{
    int real = s_lrn.real;
    memset(&s_lrn, 0, sizeof s_lrn);
    s_lrn.real = real;
}

static int cmp_i64(const void *a, const void *b)
{
    int64_t x = *(const int64_t *)a, y = *(const int64_t *)b;
    return x < y ? -1 : x > y;
}

/* The glass times that came in since: into the histories and the stats. */
static void learn(uint64_t per)
{
    unsigned i;

    for (i = 0; i < PRED_N; i++) {
        uint64_t shown, edge;
        int real, ontime;
        if (!s_pred[i].sw || !xbox_FlipHostShownAt(s_pred[i].sw, &shown, &edge, &real))
            continue;
        ontime = real & 2;
        real &= 1;
        if (real != s_lrn.real) {
            /* Display times came (or went): another clock, learned anew. */
            s_lrn.real = real;
            learn_reset();
        }
        {
            double d = (double)((int64_t)(s_pred[i].pic - edge)) / 1e6;
            if (d < 0) d = -d;
            s_pst.n++;
            if (d > 0.5) s_pst.off++;
            if (d > s_pst.off_max_ms) s_pst.off_max_ms = d;
        }
        if (s_lrn.d0_set) {
            double g = ((double)shown - s_lrn.d0 - (double)s_pred[i].pic) / 1e6;
            if (g < 0) g = -g;
            s_pst.gn++;
            if (g > 0.5) s_pst.goff++;
            if (per && g > (double)per / 2e6) s_pst.grefr++;
            if (g > s_pst.goff_max_ms) s_pst.goff_max_ms = g;
        }
        {
            FILE *f = trace_file();
            if (f)
                fprintf(f, "G %u %llu %llu %llu %.0f %d\n", s_pred[i].sw,
                        (unsigned long long)s_pred[i].pic, (unsigned long long)shown,
                        (unsigned long long)edge, s_lrn.d0, real);
        }
        s_lrn.l[s_lrn.il] = (int64_t)(shown - s_pred[i].upd);
        s_lrn.il = (s_lrn.il + 1) % HIST_L;
        if (s_lrn.nl < HIST_L) s_lrn.nl++;
        if (ontime) {
            s_lrn.d[s_lrn.id] = (int64_t)(shown - edge);
            s_lrn.id = (s_lrn.id + 1) % HIST_D;
            if (s_lrn.nd < HIST_D) s_lrn.nd++;
        }
        if (!s_lrn.nall++ || (int64_t)(shown - edge) < s_lrn.dmin)
            s_lrn.dmin = (int64_t)(shown - edge);
        if ((int32_t)(s_pred[i].sw - s_lrn.last_sw) > 0 || !s_lrn.last_sw) {
            s_lrn.last_sw = s_pred[i].sw;
            s_lrn.last_shown = shown;
        }
        s_pred[i].sw = 0;
    }
    if (s_lrn.nd || s_lrn.nall >= HIST_D) {
        /* The offset: the median of the last 16 frames handed over on time
         * (none yet: the least of all), moved to by at most 0.05 ms a
         * picture, so the clock never jumps. */
        int64_t mn = s_lrn.dmin;
        if (s_lrn.nd) {
            int64_t d[HIST_D];
            memcpy(d, s_lrn.d, sizeof d);
            qsort(d, s_lrn.nd, sizeof *d, cmp_i64);
            mn = d[s_lrn.nd / 2];
        }
        if (!s_lrn.d0_set) {
            s_lrn.d0 = (double)mn;
            s_lrn.d0_set = 1;
        } else {
            double t = (double)mn - s_lrn.d0;
            if (t > 5e4) t = 5e4;
            if (t < -5e4) t = -5e4;
            s_lrn.d0 += t;
        }
    }
}

/* The host time the frame being built will flip at. Unknown (the grid's
 * first frames): now. */
static uint64_t picture_ns(uint64_t now)
{
    uint32_t seen, sw = s_swaps + 1u;
    uint64_t due, per, e;
    int32_t n;

    if (!xbox_FlipHostGridState(&seen, &due, &per)) {
        s_pred[sw % PRED_N].sw = 0;
        return now;
    }
    learn(per);
    n = (int32_t)(sw - seen);
    if ((unsigned)n < s_pst.nmin) s_pst.nmin = (unsigned)n;
    if (n > 0 && (unsigned)n > s_pst.nmax) s_pst.nmax = (unsigned)n;
    if (n < 1)
        n = 1;
    if (n > 4)
        n = 4;
    e = due + (uint64_t)n * per;
    e = e > now ? e : now;
    if (!predict_edge_only() && s_lrn.nl >= 3 && s_lrn.d0_set && s_lrn.last_sw
            && (int32_t)(sw - s_lrn.last_sw) > 0 && (int32_t)(sw - s_lrn.last_sw) <= 16) {
        int64_t l[HIST_L], x, k;
        uint64_t m = sw - s_lrn.last_sw, chain = s_lrn.last_shown + m * per, p;
        memcpy(l, s_lrn.l, sizeof l);
        qsort(l, s_lrn.nl, sizeof *l, cmp_i64);
        x = (int64_t)now + l[s_lrn.nl / 2] - (int64_t)s_lrn.last_shown;
        k = x >= 0 ? (x + (int64_t)per / 2) / (int64_t)per : 0;
        p = s_lrn.last_shown + (uint64_t)k * per;
        if (p < chain)
            p = chain;
        /* Never before the picture before. Not a refresh after it: when
         * that one was put a refresh too late, this one would be too, and
         * every one after (the frames still go at their edges). */
        if (s_lrn.last_p_sw == s_swaps && p < s_lrn.last_p)
            p = s_lrn.last_p;
        if (p > chain + 4 * per)
            p = chain + 4 * per;
        s_lrn.last_p = p;
        s_lrn.last_p_sw = sw;
        e = (uint64_t)((double)p - s_lrn.d0);
    } else {
        s_lrn.last_p = 0;
    }
    s_pred[sw % PRED_N].sw = sw;
    s_pred[sw % PRED_N].upd = now;
    s_pred[sw % PRED_N].pic = e;
    return e;
}

static void diag_swap(void);
static void dbg_swap(void);
static int s_dbg_car_drawn;

void bo3_swap(bo3_hirefresh_fn title_fn)
{
    s_swaps++;
    dbg_swap();
    marks_probe();
    if (s_active)
        diag_swap();
    /* The picture is built: the blends back to the true states (again at
     * the next Update, for what syncs or draws in between). */
    if (s_active)
        restore_blends();
    title_fn();
}

/* debug (CAMREL): at each Update, before any step, after the blends are put
 * back: the step count and player 1's car and the view's camera as the
 * simulation holds them: "S <step> <active> <car x y z> <car at x z> <camera x y z> <camera at x z>". */
static void hit_update(void);

static void dbg_truth(void)
{
    static uint32_t last = ~0u;
    FILE *f = dbg_file();
    uint32_t cam = MEM32(BO3_VA(0x4D6200u, 0x4D6520u, 0x4D65C0u) + 0x58u), frame, step = MEM32(STEP_COUNT);
    const float *c = (const float *)(uintptr_t)XBOX_PTR(BO3_VA(0x0073A1D0u, 0x0073A4E0u, 0x0073A580u) + 0x10u), *m;
    if (!f || step == last || MEM32(GAME_STATE) != 5)
        return;
    last = step;
    if (cam < 0x10000u || cam >= 0x08000000u || (frame = MEM32(cam + 4u)) < 0x10000u || frame >= 0x08000000u)
        return;
    m = ltm(frame);
    fprintf(f, "S %u %d %.4f %.4f %.4f %.5f %.5f %.4f %.4f %.4f %.5f %.5f\n", step, s_active,
            c[12], c[13], c[14], c[8], c[10], m[12], m[13], m[14], m[8], m[10]);
}

/* 4. The top of CB3Game::Update (0x165F0) steps two things once an Update,
 * whatever its steps: the game's random generator (game 0x4A71A0 +
 * 0x2AE38/+0x2AE3C: x = (x << 16) + (x >> 16, arithmetic) + y, then
 * y += x; drawn from by sub_0005EC40) and the profile's time played
 * (*(0x3C838C) + 0x18 += game + 0x705C, one step's time; saved by
 * sub_00018650). At 60 an Update is a step. Here: an Update with no step
 * leaves both as they were (nothing else touched them in such a picture,
 * BO-275), one with n steps advances them n times. */
#define GAME_RNG     BO3_VA(0x4D1FD8u, 0x4D22F8u, 0x4D2398u)     /* game + 0x2AE38: x, then y */
#define PLAYED_PTR   BO3_VA(0x3C838Cu, 0x3C86ACu, 0x3C874Cu)     /* + 0x18: the profile's time played */
#define PLAYED_DT    BO3_VA(0x4AE1FCu, 0x4AE51Cu, 0x4AE5BCu)     /* game + 0x705C: what it gains an Update */
static struct { uint32_t x, y, played_at; float played; int have; } s_pu;

static uint32_t played_at(void)
{
    uint32_t p = MEM32(PLAYED_PTR);
    return p >= 0x10000u && p < 0x04000000u ? p + 0x18u : 0u;
}

static void rng_step(void)
{
    uint32_t x = MEM32(GAME_RNG), y = MEM32(GAME_RNG + 4u);
    x = (x << 16) + ((uint32_t)((int32_t)x >> 16) + y);
    MEM32(GAME_RNG) = x;
    MEM32(GAME_RNG + 4u) = x + y;
}

static void per_update_before(int steps)
{
    uint32_t pa = played_at();
    int k;
    s_pu.have = 0;
    if (steps == 0) {
        s_pu.x = MEM32(GAME_RNG);
        s_pu.y = MEM32(GAME_RNG + 4u);
        s_pu.played_at = pa;
        if (pa)
            s_pu.played = MEMF(pa);
        s_pu.have = 1;
        return;
    }
    for (k = 1; k < steps; k++) {     /* the title adds the last one */
        rng_step();
        if (pa)
            MEMF(pa) = MEMF(PLAYED_DT) + MEMF(pa);
    }
}

static void per_update_after(int steps)
{
    if (steps != 0 || !s_pu.have)
        return;
    MEM32(GAME_RNG) = s_pu.x;
    MEM32(GAME_RNG + 4u) = s_pu.y;
    if (s_pu.played_at && s_pu.played_at == played_at())
        MEMF(s_pu.played_at) = s_pu.played;
    s_pu.have = 0;
}

/* BO3_LOCKSTEP=1, a test switch (gate G2 of docs/research/perf-plan-fable.md,
 * BO-282.2), off unless set and only with High refresh off: two runs of a
 * scripted race compute the same race, so two builds can be compared by
 * their results and saves. Without it a run is not repeatable: the menus'
 * presses and the loads take as many pictures as the machine needs, the
 * game's random generator (GAME_RNG above) is stepped once an Update, and a
 * picture the host was late with runs two steps in one Update (the
 * generator once). With it every Update runs exactly one step (the
 * simulation is paced by the pictures, not the clock), and the generator is
 * held at a fixed seed until player 1's race begins (the stage running,
 * the car's phase 1, the rolling start), so the race starts from the same
 * numbers. Nothing else is touched. */
#define LS_CAR1      BO3_VA(0x0073A1D0u, 0x0073A4E0u, 0x0073A580u)   /* player 1's CB3PlayerCar (autopilot.c) */
#define LS_PHASE     0x134Cu       /* 1 before the GO, 2 in play, 3 finished */
#define LS_SEED_X    0x2545F491u
#define LS_SEED_Y    0x6C078965u

static int lockstep_on(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("BO3_LOCKSTEP");
        on = e && *e == '1';
        if (on)
            fprintf(stderr, "[LOCKSTEP] on: one step an Update, the random generator held until the race\n");
    }
    return on;
}

static void lockstep_update(void)
{
    static int live;
    uint32_t mod = MEM32(STAGE), ph = MEM32(LS_CAR1 + LS_PHASE);
    int now = mod >= 0x10000u && mod < 0x04000000u && bo3_race_module_vt(MEM32(mod)) && ph >= 1u && ph <= 3u;

    MEM32(FRAME_STEPS) = 1u;
    if (!now) {
        MEM32(GAME_RNG) = LS_SEED_X;
        MEM32(GAME_RNG + 4u) = LS_SEED_Y;
    }
    if (now != live)
        fprintf(stderr, "[LOCKSTEP] race %s at step %u\n", now ? "begins" : "over", MEM32(STEP_COUNT));
    live = now;
}

void bo3_update(bo3_hirefresh_fn title_fn)
{
    uint64_t now = xbox_HostNowNs(), pic;
    double rate = wanted_rate(), dt;
    int steps, was = s_active, max = 4;

    s_updates++;
    restore_blends();
    bo3_hraudit_begin();
    dbg_truth();
    hit_update();
    s_rendering = 0;
    xbox_FlipHostRate(rate);
    if ((rate > 0.0) != (s_rate > 0.0))
        hr_log("[HIREFRESH] %s (%.1f Hz)\n", rate > 0.0 ? "on" : "off", rate);
    if (rate <= 0.0) {
        s_active = 0;
        s_rate = 0.0;
        if (lockstep_on())
            lockstep_update();
        title_fn();
        return;
    }
    s_rate = rate;
    pic = picture_ns(now);
    dt = was && pic >= s_pic_ns ? (double)(pic - s_pic_ns) / 1e9 : 0.0;
    if (!was || dt > 0.25) {
        if (!was)
            learn_reset();
        /* One step at once, the picture at it; then the picture's clock. */
        s_ahead = STEP_S;
        dt = STEP_S;
    } else {
        s_ahead += dt;
    }
    s_pic_ns = pic;
    s_pic_dt = dt;
    steps = s_ahead > 0.0 ? (int)ceil(s_ahead / STEP_S - 1e-6) : 0;
    if (steps < 0)
        steps = 0;
    if (steps > max) {
        steps = max;
        if (MEM32(TIMER) != 2)
            s_ahead = steps * STEP_S;       /* the lag beyond the cap dropped */
    }
    s_ahead -= steps * STEP_S;
    /* The picture's time past the step before the last, in (0, 1] steps:
     * 1 is the last step itself (no blend). */
    s_lag = s_ahead + STEP_S;
    if (s_lag > STEP_S)
        s_lag = STEP_S;
    if (s_lag < 0.0)
        s_lag = 0.0;
    if (bo3_hraudit_noblend())
        s_lag = STEP_S;                     /* BO-275 audit: drawn at the last step */
    MEM32(FRAME_STEPS) = (uint32_t)steps;
    s_active = 1;
    s_steps_end = MEM32(STEP_COUNT) + (uint32_t)steps;
    s_upd_steps = (uint32_t)steps;
    s_diag.upd[steps > 4 ? 4 : steps]++;
    xbox_PictureSteps((uint32_t)steps);   /* BO-282.6/.7: the walker's per-picture view */
    s_in_update = 1;
    bo3_hraudit_picture(steps);
    per_update_before(steps);
    title_fn();
    per_update_after(steps);
    s_in_update = 0;
}

/* ── 2. The picture between steps ─────────────────────────── */

/* BO3_HIREFRESH_STATS=1: a line a second of what was blended. */
static struct { unsigned syncs, blended, live; double umin, umax, t0; uint32_t step0; } s_stat
    = { 0, 0, 0, 1.0, 0.0, 0.0, 0 };

static void stats(uint32_t step)
{
    static int on = -1;
    double now;

    if (on < 0)
        on = getenv("BO3_HIREFRESH_STATS") != NULL;
    if (!on)
        return;
    s_stat.syncs++;
    if (s_live_n > s_stat.live)
        s_stat.live = s_live_n;
    now = host_s();
    if (s_stat.t0 == 0.0) {
        s_stat.t0 = now;
        s_stat.step0 = step;
    }
    if (now - s_stat.t0 >= 1.0) {
        fprintf(stderr, "[HIREFRESH] %.0f syncs/s, %u steps, moving frames <= %u, blends %u,"
                        " u %.2f..%.2f, records %u; picture time vs its flip edge: %u checked,"
                        " %u off by > 0.5 ms (max %.3f ms), frames on their way %u..%u;"
                        " vs the glass (%s, offset %.2f ms): %u checked, %u off by > 0.5 ms,"
                        " %u by half a refresh or more (max %.3f ms)\n",
                s_stat.syncs / (now - s_stat.t0),
                step - s_stat.step0, s_stat.live, s_stat.blended,
                s_stat.blended ? s_stat.umin : 0.0, s_stat.umax, s_rec_used,
                s_pst.n, s_pst.off, s_pst.off_max_ms, s_pst.nmin == 99 ? 0 : s_pst.nmin,
                s_pst.nmax, s_lrn.real ? "display" : "hand-over", s_lrn.d0 / 1e6,
                s_pst.gn, s_pst.goff, s_pst.grefr, s_pst.goff_max_ms);
        s_pst.n = s_pst.off = s_pst.nmax = s_pst.gn = s_pst.goff = s_pst.grefr = 0;
        s_pst.nmin = 99;
        s_pst.off_max_ms = s_pst.goff_max_ms = 0.0;
        memset(&s_stat, 0, sizeof s_stat);
        s_stat.umin = 1.0;
        s_stat.t0 = now;
        s_stat.step0 = step;
    }
}

static rec_t *rec_get(uint32_t va)
{
    uint32_t h = (va >> 2) * 2654435761u, i, n;

    for (n = 0; n < REC_N; n++) {
        i = (h + n) & (REC_N - 1);
        if (s_rec[i].key == va)
            return &s_rec[i];
        if (s_rec[i].key == 0) {
            if (s_rec_used >= REC_N / 4 * 3)
                return NULL;
            s_rec_used++;
            s_rec[i].key = va;
            return &s_rec[i];
        }
    }
    return NULL;
}

/* Frames come and go with every stage: the table is started again when
 * three quarters full, at a step's first sync (a picture of no blends). */
#define VEH_N 1024u
static uint32_t s_veh[VEH_N];      /* the vehicles' records (indices into s_rec) */
static unsigned s_veh_n;

static void rec_reset_if_full(void)
{
    if (s_rec_used < REC_N / 4 * 3 - WALK_MAX)
        return;
    memset(s_rec, 0, sizeof s_rec);
    s_rec_used = 0;
    s_live_n = 0;
    s_veh_n = 0;
}

/* The vehicles' matrices as the steps left them, at the picture's first
 * frame sync (after the steps and the title's per-frame updates, before
 * anything is drawn): what is put back after the picture, whatever the
 * title writes into them while it draws (BO-241). */
static void snapshot_vehicles(void)
{
    unsigned i;

    for (i = 0; i < s_veh_n; i++) {
        rec_t *r = &s_rec[s_veh[i]];
        if (!r->key || !r->veh)
            continue;
        memcpy(r->snap, ltm(r->key), sizeof r->snap);
        r->snap_gen = s_put_gen;
    }
}

static float dot3(const float *a, const float *b);


/* The matrix in place is the record's object: the blend written into it, or
 * within SNAP_DIST of its take at this step (a rebuild from a blended parent
 * is off by less than a step's move). Not so: the frame holds something else
 * for now, which is not blended. */
static int at_take(const rec_t *r, const float *m)
{
    float d[3] = { m[12] - r->cur[12], m[13] - r->cur[13], m[14] - r->cur[14] };

    if (r->put_gen == s_put_gen && !memcmp(m, s_put[r->put].wrote, sizeof r->cur))
        return 1;
    return d[0] * d[0] + d[1] * d[1] + d[2] * d[2] <= SNAP_DIST * SNAP_DIST;
}

static void capture_from(uint32_t va, const float *m, uint32_t step);

static void capture(uint32_t va, uint32_t step)
{
    capture_from(va, ltm(va), step);
}

/* The take of a record at this step from m (a frame's LTM in place, or a
 * vehicle's matrix as the steps left it). */
static void capture_from(uint32_t va, const float *m, uint32_t step)
{
    rec_t *r = rec_get(va);

    if (!r)
        return;
    /* A root synced again without its LTMs (0x1E1595) still holds the
     * blend written into it: that is no new state. */
    if (r->put_gen == s_put_gen && !memcmp(m, s_put[r->put].wrote, sizeof r->cur))
        return;
    /* Taken at this step already: the first take after the steps is the
     * truth. A later sync in the same picture can rebuild this LTM from a
     * parent that holds a blend; taken as the truth, it would be what the
     * blends are put back to. */
    if (r->sc == step && (r->sc || r->sp)) {
        /* Somewhere else entirely within the same step: one frame used for
         * several objects in a picture, or moved by something other than the
         * steps. That take is left as it is (at_take(), below); the record
         * and the takes where the frame is back are not touched. (This was
         * once a mark for good, "shared": the record outlives its frame, so
         * a frame later made at the same address, the view's camera among
         * them, inherited it and was never blended again: the world at the
         * step under a blended car, BO-241.) */
        if (!at_take(r, m)) {
            s_diag.skips++;
            s_diag.skip_va = va;
            s_diag.skip_d = sqrtf((m[12] - r->cur[12]) * (m[12] - r->cur[12])
                                  + (m[13] - r->cur[13]) * (m[13] - r->cur[13])
                                  + (m[14] - r->cur[14]) * (m[14] - r->cur[14]));
        }
        return;
    }
    if (r->sc == 0 && r->sp == 0) {
        memcpy(r->prev, m, sizeof r->prev);
        r->sp = step;
    } else {
        memcpy(r->prev, r->cur, sizeof r->prev);
        r->sp = r->sc;
    }
    if (s_live_n < REC_N)
        s_live[s_live_n++] = (uint32_t)(r - s_rec);
    memcpy(r->cur, m, sizeof r->cur);
    r->sc = step;
}

static void walk(uint32_t root, uint32_t step)
{
    uint32_t stack[64], f;
    unsigned sp = 0, n = 0;

    capture(root, step);
    if ((f = MEM32(root + FRAME_CHILD)) != 0)
        stack[sp++] = f;
    while (sp && n++ < WALK_MAX) {
        f = stack[--sp];
        capture(f, step);
        if (MEM32(f + FRAME_NEXT) && sp < 64)
            stack[sp++] = MEM32(f + FRAME_NEXT);
        if (MEM32(f + FRAME_CHILD) && sp < 64)
            stack[sp++] = MEM32(f + FRAME_CHILD);
    }
}

static float dot3(const float *a, const float *b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

/* m = prev..cur at u: lerped axes made orthogonal again (Gram-Schmidt in
 * the order right, up, at) at their lerped lengths; 0 when it should snap. */
static int blend(const rec_t *r, float u, float *m)
{
    float ax[3][3], len[3];
    int a, k;

    {
        float d[3] = { r->cur[12] - r->prev[12], r->cur[13] - r->prev[13],
                       r->cur[14] - r->prev[14] };
        float lim = SNAP_DIST * (float)(r->sc - r->sp);
        if (dot3(d, d) > lim * lim)
            return 0;
    }
    for (a = 0; a < 3; a++) {
        const float *p = r->prev + a * 4, *c = r->cur + a * 4;
        float lp = sqrtf(dot3(p, p)), lc = sqrtf(dot3(c, c));
        if (lp < 1e-6f || lc < 1e-6f || dot3(p, c) < SNAP_DOT * lp * lc)
            return 0;
        for (k = 0; k < 3; k++)
            ax[a][k] = p[k] + (c[k] - p[k]) * u;
        len[a] = lp + (lc - lp) * u;
    }
    for (a = 0; a < 3; a++) {
        int b;
        float l;
        for (b = 0; b < a; b++) {
            float d = dot3(ax[a], ax[b]);
            for (k = 0; k < 3; k++)
                ax[a][k] -= d * ax[b][k];
        }
        l = sqrtf(dot3(ax[a], ax[a]));
        if (l < 1e-6f)
            return 0;
        for (k = 0; k < 3; k++)
            ax[a][k] /= l;
    }
    memcpy(m, r->cur, 16 * sizeof *m);    /* flags and pads as they are */
    for (a = 0; a < 3; a++)
        for (k = 0; k < 3; k++)
            m[a * 4 + k] = ax[a][k] * len[a];
    for (k = 0; k < 3; k++)
        m[12 + k] = r->prev[12 + k] + (r->cur[12 + k] - r->prev[12 + k]) * u;
    return 1;
}

/* m = the record's state at the picture's time (between the last two
 * steps); 0 when it did not move at the last step or should snap. */
static int blend_now(const rec_t *r, uint32_t step, float *m)
{
    double t = (double)step - 1.0 + s_lag / STEP_S, u;

    if (r->sc != step || r->sp >= r->sc || r->sc - r->sp > 4)
        return 0;
    u = (t - (double)r->sp) / (double)(r->sc - r->sp);
    if (u >= 1.0)
        return 0;
    if (u < 0.0)
        u = 0.0;
    if (!blend(r, (float)u, m))
        return 0;
    s_stat.blended++;
    if (u < s_stat.umin) s_stat.umin = u;
    if (u > s_stat.umax) s_stat.umax = u;
    return 1;
}

static void apply(rec_t *r, uint32_t step)
{
    float m[16];
    unsigned k;

    /* A vehicle's matrix is blended by its own path (blend_matrix), which
     * knows what to put back. Here, at a later sync of the picture, it was
     * blended again in place and put back to what the draw found in it:
     * a car drawing its wobble (below) had its wobble put back as its true
     * matrix, so the simulation went on from it (BO-241). */
    if (r->veh || !at_take(r, ltm(r->key)) || !blend_now(r, step, m))
        return;
    if (r->put_gen == s_put_gen) {
        k = r->put;
    } else {
        if (s_put_n == REC_N)
            return;
        k = s_put_n++;
        r->put_gen = s_put_gen;
        r->put = k;
        s_put[k].va = r->key;
        memcpy(s_put[k].wrote1, m, sizeof m);
    }
    memcpy(s_put[k].cur, r->cur, sizeof r->cur);
    memcpy(ltm(r->key), m, sizeof m);
    memcpy(s_put[k].wrote, m, sizeof m);
}

static rec_t *rec_find(uint32_t va)
{
    uint32_t h = (va >> 2) * 2654435761u, i, n;

    for (n = 0; n < REC_N; n++) {
        i = (h + n) & (REC_N - 1);
        if (s_rec[i].key == va)
            return &s_rec[i];
        if (s_rec[i].key == 0)
            return NULL;
    }
    return NULL;
}

/* o = a b, 4x4, row vectors (p' = p M: RenderWare's and the view's order). */
static void mat_mul(const float *a, const float *b, float *o)
{
    int r, c;
    for (r = 0; r < 4; r++)
        for (c = 0; c < 4; c++)
            o[r * 4 + c] = a[r * 4] * b[c] + a[r * 4 + 1] * b[4 + c]
                         + a[r * 4 + 2] * b[8 + c] + a[r * 4 + 3] * b[12 + c];
}

/* An RwMatrix as a 4x4 affine: the flags and pads read as 0 0 0 1. */
static void mat_affine(const float *m, float *o)
{
    memcpy(o, m, 16 * sizeof *o);
    o[3] = o[7] = o[11] = 0.0f;
    o[15] = 1.0f;
}

/* o = the inverse of the affine m; 0 when singular. */
static int mat_affine_inv(const float *m, float *o)
{
    float a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6],
          g = m[8], h = m[9], i = m[10], det;
    int k;

    det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (fabsf(det) < 1e-12f)
        return 0;
    det = 1.0f / det;
    o[0] = (e * i - f * h) * det; o[1] = (c * h - b * i) * det; o[2] = (b * f - c * e) * det;
    o[4] = (f * g - d * i) * det; o[5] = (a * i - c * g) * det; o[6] = (c * d - a * f) * det;
    o[8] = (d * h - e * g) * det; o[9] = (b * g - a * h) * det; o[10] = (a * e - b * d) * det;
    for (k = 0; k < 3; k++)
        o[12 + k] = -(m[12] * o[k] + m[13] * o[4 + k] + m[14] * o[8 + k]);
    o[3] = o[7] = o[11] = 0.0f;
    o[15] = 1.0f;
    return 1;
}

/* The view being drawn (the race's 0x4D6200; 0 outside the stage's render,
 * sub_000134D0): its view matrix (+ 0x00, 4x4, the camera's inverse times
 * the view window, sub_001C8870) and its RwCamera (+ 0x58). sub_001C8A60
 * builds both from the camera the steps left (+ 0x40): the camera frame's
 * modelling matrix, and the view matrix from the same, before the frame
 * sync. The frame is then blended in the sync like any frame, so the world
 * is drawn from the blended camera, but the view matrix is still the last
 * step's. What projects a point with it draws at the step's camera: the
 * rival marker (sub_0018F060: the triangle, the position number, the
 * label, at the car's projected point; its size and the triangle/number
 * switch from that point's depth) sat up to a step's camera move off its
 * car, by a different amount in each picture: the number, near, shook
 * (BO-241). So the view matrix follows its camera's blend: V' = B^-1 M V
 * (M the camera's true LTM, B its blend: the view window part, M V, is
 * kept), put back with the blends. */
#define VIEW_PTR     BO3_VA(0x4D6520u, 0x4D6840u, 0x4D68E0u)
#define VIEW_CAMERA  0x58u

static void blend_view(void)
{
    uint32_t v = MEM32(VIEW_PTR), cam, frame;
    float *vm, *lm, m[16], b[16], bi[16], a[16], nv[16];
    unsigned k;
    rec_t *r;

    if (v < 0x10000u || v >= 0x08000000u)
        return;
    cam = MEM32(v + VIEW_CAMERA);
    if (cam < 0x10000u || cam >= 0x08000000u)
        return;
    frame = MEM32(cam + 4u);
    if (frame < 0x10000u || frame >= 0x08000000u)
        return;
    r = rec_find(frame);
    lm = ltm(frame);
    /* The camera blended in this picture, its blend still in place. */
    if (!r || r->put_gen != s_put_gen || memcmp(lm, s_put[r->put].wrote, sizeof r->cur))
        return;
    vm = (float *)(uintptr_t)XBOX_PTR(v);
    {
        rec_t *rv = rec_get(v - FRAME_LTM);
        if (!rv)
            return;
        /* Done for this picture already, unless the view was built again. */
        if (rv->put_gen == s_put_gen && !memcmp(vm, s_put[rv->put].wrote, sizeof rv->cur))
            return;
        mat_affine(s_put[r->put].cur, m);
        mat_affine(lm, b);
        if (!mat_affine_inv(b, bi))
            return;
        mat_mul(m, vm, a);
        mat_mul(bi, a, nv);
        if (rv->put_gen == s_put_gen) {
            k = rv->put;
        } else {
            if (s_put_n == REC_N)
                return;
            k = s_put_n++;
            rv->put_gen = s_put_gen;
            rv->put = k;
            s_put[k].va = v - FRAME_LTM;
            memcpy(s_put[k].wrote1, nv, sizeof nv);
        }
        memcpy(s_put[k].cur, vm, sizeof s_put[k].cur);
        memcpy(vm, nv, sizeof nv);
        memcpy(s_put[k].wrote, nv, sizeof nv);
    }
}

/* BO3_HIREFRESH_VIEW=step: the view matrix left at the step's camera, as
 * before (the A/B arm). */
static int view_at_step(void)
{
    static int v = -1;
    if (v < 0) {
        const char *e = getenv("BO3_HIREFRESH_VIEW");
        v = e && !strcmp(e, "step") ? 1 : e && !strcmp(e, "ab") ? 2 : 0;
        if (v)
            fprintf(stderr, "[HIREFRESH] view matrix: the step's camera (BO3_HIREFRESH_VIEW=%s)\n",
                    v == 1 ? "step" : "ab, every other 60 steps");
    }
    return v == 2 ? (int)((MEM32(STEP_COUNT) / 60u) & 1u) : v;
}

/* At the swap, the blends still in place: was the view's camera blended in a
 * picture between steps? A diagnostic line every 10 s when a camera was not
 * (in more than a tenth of them) or frames were skipped (above). */
static void diag_swap(void)
{
    uint32_t cam = MEM32(BO3_VA(0x4D6200u, 0x4D6520u, 0x4D65C0u) + VIEW_CAMERA), frame;
    double now = host_s();
    rec_t *r;

    if (s_lag < STEP_S * 0.999 && cam >= 0x10000u && cam < 0x08000000u
            && (frame = MEM32(cam + 4u)) >= 0x10000u && frame < 0x08000000u) {
        r = rec_find(frame);
        s_diag.pics++;
        if (!(r && r->put_gen == s_put_gen && !memcmp(ltm(frame), s_put[r->put].wrote, sizeof r->cur))) {
            s_diag.cam_off++;
            s_diag.cam_va = frame;
        }
    }
    if (s_diag.t0 == 0.0)
        s_diag.t0 = now;
    if (now - s_diag.t0 < 10.0)
        return;
    /* Every 10 s when something was off, else every 60 s. */
    {
        unsigned k, any = 0;
        for (k = 0; k < OWN_N; k++)
            any += s_diag.ovr[k] + s_diag.owned[k];
        if (any) {
            char b[400];
            int n = 0;
            for (k = 0; k < OWN_N; k++)
                if (s_diag.ovr[k])
                    n += snprintf(b + n, sizeof b - (size_t)n, " %s %u", k_own[k], s_diag.ovr[k]);
            n += snprintf(b + n, sizeof b - (size_t)n, ";%s title-owned at the put-back:", n ? "" : " none;");
            for (k = 0; k < OWN_N; k++)
                if (s_diag.owned[k])
                    n += snprintf(b + n, sizeof b - (size_t)n, " %s %u", k_own[k], s_diag.owned[k]);
            hr_log("[HIREFRESH] 10 s: drawn from a matrix the title put there to draw (blended apart, the steps' put back):%s\n", b);
        }
    }
    if (s_diag.skips || s_diag.cam_off * 10u > s_diag.pics || ++s_diag.periods >= 6) {
        hr_log("[HIREFRESH] 10 s: Updates by steps 0/1/2/3/4+ %u/%u/%u/%u/%u; %u frames found elsewhere"
               " within a step (last %08x, %.1f units); the view's camera not blended in %u of %u"
               " pictures between steps (frame %08x); reflection cube drawn %u, kept %u"
               " (from another point %u)\n",
               s_diag.upd[0], s_diag.upd[1], s_diag.upd[2], s_diag.upd[3], s_diag.upd[4],
               s_diag.skips, s_diag.skip_va, s_diag.skip_d, s_diag.cam_off, s_diag.pics, s_diag.cam_va,
               s_diag.cube_drawn, s_diag.cube_skipped, s_diag.cube_moved);
        s_diag.periods = 0;
    }
    {
        unsigned keep = s_diag.periods;
        memset(&s_diag, 0, sizeof s_diag);
        s_diag.periods = keep;
    }
    s_diag.t0 = now;
}

static void blend_racers(uint32_t step);

static void step_changed(uint32_t step)
{
    if (step != s_live_step) {
        s_live_n = 0;
        s_live_step = step;
        rec_reset_if_full();
    }
}

/* BO3_HIREFRESH_POKE=<swap> (test aid): in that picture, right after the
 * first sync, the view's camera frame is taken a second time 20 units away
 * (what a cut or a second use of the frame within one step looks like), then
 * put back as it was. */
static void poke(uint32_t step)
{
    static long at = -2;
    uint32_t cam, frame;
    float *m, keep[16];
    rec_t *r;

    if (at == -2) {
        const char *e = getenv("BO3_HIREFRESH_POKE");
        at = e && *e ? atol(e) : -1;
    }
    if (at < 0 || s_swaps + 1u < (uint32_t)at)
        return;
    at = -1;
    cam = MEM32(BO3_VA(0x4D6200u, 0x4D6520u, 0x4D65C0u) + 0x58u);
    if (cam < 0x10000u || cam >= 0x08000000u || (frame = MEM32(cam + 4u)) < 0x10000u || frame >= 0x08000000u)
        return;
    m = ltm(frame);
    memcpy(keep, m, sizeof keep);
    m[12] += 20.0f;
    capture(frame, step);
    if ((r = rec_find(frame)) != NULL)
        apply(r, step);
    memcpy(m, keep, sizeof keep);
    hr_log("[HIREFRESH] poke: the camera frame %08x taken 20 units away at step %u\n", frame, step);
}

void bo3_frame_sync_dirty(bo3_hirefresh_fn title_fn)
{
    uint32_t roots[WALK_MAX], l, step;
    unsigned n = 0, i;

    if (!s_active) {
        title_fn();
        return;
    }
    for (l = MEM32(DIRTY_HEAD); l != DIRTY_HEAD && l && n < WALK_MAX; l = MEM32(l))
        roots[n++] = l - 8;
    title_fn();

    step = MEM32(STEP_COUNT);
    /* A sync while the steps still run (the simulation's own): its LTMs are
     * of a step the picture is not at, and the next step may cut. Taken, it
     * would be the step's first take, and the picture's own one, after the
     * cut, would count as "elsewhere within the step". Not ours. */
    if (s_in_update && (int32_t)(step - s_steps_end) < 0)
        return;
    step_changed(step);
    if (!s_rendering)
        snapshot_vehicles();
    s_rendering = 1;
    for (i = 0; i < n; i++)
        walk(roots[i], step);
    if (!s_active) {
        stats(step);
        return;
    }

    for (i = 0; i < s_live_n; i++)
        apply(&s_rec[s_live[i]], step);
    poke(step);
    if (!view_at_step())
        blend_view();
    blend_racers(step);
    stats(step);
}

/* RwFrameGetLTM (sub_001DD280, cdecl, the frame): what the render
 * pipelines ask for. The cars are posed by writing their LTMs, not through
 * the dirty list, so they are taken and blended here, when drawn. Only
 * while a picture is drawn: the simulation's own calls get the truth. */
void bo3_frame_get_ltm(bo3_hirefresh_fn title_fn)
{
    uint32_t frame = MEM32(g_esp + 4), step;
    rec_t *r;

    title_fn();
    if (!s_active || !s_rendering || !frame)
        return;
    step = MEM32(STEP_COUNT);
    step_changed(step);
    capture(frame, step);
    r = rec_get(frame);
    if (r)
        apply(r, step);
}

/* sub_000303D0 (stdcall: draw context, matrix, model), the vehicles' draw:
 * CB3RaceCar::Draw (sub_0018DE00) and CB3TrafficSystem::Draw (sub_001A4710)
 * hand it the vehicle's own world matrix (car + 0x10, the RwMatrix layout;
 * the LOD test reads its position at + 0x30). The vehicles are not frames:
 * they are taken and blended here. The blend stays in the matrix for the
 * rest of the picture, as a frame's does, and is put back at the swap (and
 * at the next Update, before any step): what the title draws on the
 * vehicle after its body reads that matrix too. The player car's lights
 * (sub_00187C70: brake, head and tail lights, esi the matrix) and boost
 * flames (sub_001871E0, from car + 0x10) right after the body
 * (sub_0017F730, 0x18DF94), the traffic's lights after all their bodies
 * (0x1A52DB, 0x1A531E). With the matrix put back after the body alone,
 * those were drawn at the last step while the body was up to a step behind:
 * at speed the glow sat up to a metre inside the car and its own body hid
 * it in the pictures between steps, a flicker (BO-241). Where the title
 * writes the matrix itself after the draw (the shake, + 0x19AB, 0x18DEF2:
 * its own copy put back) the blend is not there any more and nothing is
 * put back. */
/* BO3_HIREFRESH_TRACE=<file>: player 1's car as drawn, once a picture
 * (host time, step count, position), to see the motion between steps. */
/* Also "G <swap> <picture ns> <glass ns> <edge ns> <offset ns> <real>" as
 * each frame's glass time is learned. */
static FILE *trace_file(void)
{
    static FILE *f;
    static int on = -1;

    if (on < 0) {
        const char *e = getenv("BO3_HIREFRESH_TRACE");
        on = e && *e && (f = fopen(e, "w")) != NULL;
        if (on)
            setvbuf(f, NULL, _IOLBF, 0);
    }
    return on ? f : NULL;
}

static void trace_car(uint32_t mat, const float *m)
{
    static uint32_t last_flip_step, last_sw;
    FILE *f = trace_file();
    double now;

    if (!f || mat != BO3_VA(0x0073A1D0u, 0x0073A4E0u, 0x0073A580u) + 0x10u)
        return;
    now = host_s();
    if (last_sw == s_swaps && last_flip_step == MEM32(STEP_COUNT))
        return;              /* the same picture's other passes */
    last_sw = s_swaps;
    last_flip_step = MEM32(STEP_COUNT);
    /* time, step, position; the frame's swap and its picture time */
    fprintf(f, "%.6f %u %.4f %.4f %.4f %u %llu\n", now, MEM32(STEP_COUNT), m[12], m[13], m[14],
            s_swaps + 1u, (unsigned long long)s_pic_ns);
}

/* debug (CAMREL): why player 1's car was not blended at its last take: 0
 * blended, 1 no record, 2 elsewhere (at_take), 3 not taken at this step or
 * no step pair, 4 at the step (u >= 1), 5 moved too far, 6 turned too far. */
static int s_dbg_car_why;
static int why_not(const rec_t *r, const float *g, uint32_t step)
{
    double t = (double)step - 1.0 + s_lag / STEP_S;
    int a;
    if (!r) return 1;
    if (!at_take(r, g)) return 2;
    if (r->sc != step || r->sp >= r->sc || r->sc - r->sp > 4) return 3;
    if ((t - (double)r->sp) / (double)(r->sc - r->sp) >= 1.0) return 4;
    {
        float d[3] = { r->cur[12] - r->prev[12], r->cur[13] - r->prev[13], r->cur[14] - r->prev[14] };
        float lim = SNAP_DIST * (float)(r->sc - r->sp);
        if (dot3(d, d) > lim * lim) return 5;
    }
    for (a = 0; a < 3; a++) {
        const float *p = r->prev + a * 4, *c = r->cur + a * 4;
        float lp = sqrtf(dot3(p, p)), lc = sqrtf(dot3(c, c));
        if (lp < 1e-6f || lc < 1e-6f || dot3(p, c) < SNAP_DOT * lp * lc) return 6;
    }
    return 0;
}

/* Who a blended matrix is (the diagnostic counters): player 1's car (or
 * player 2's), a rival's car (the racer table), another vehicle (traffic),
 * the view's camera frame, the view matrix, any other frame (debris and
 * parts among them). */
static int owner_of(uint32_t mat)
{
    uint32_t n = MEM32(RACERS_N), i, cam;
    rec_t *r;

    if (mat == BO3_VA(0x0073A1D0u, 0x0073A4E0u, 0x0073A580u) + 0x10u || mat == BO3_VA(0x0073A1D0u, 0x0073A4E0u, 0x0073A580u) + 0x27E0u + 0x10u)
        return OWN_CAR;
    if (n > 8)
        n = 8;
    for (i = 0; i < n; i++) {
        uint32_t obj = MEM32(RACERS + i * 0x30u);
        if (obj >= 0x10000u && obj < 0x08000000u && MEM32(obj + 0x204u) == mat)
            return OWN_RIVAL;
    }
    if (mat == BO3_VA(0x4D6200u, 0x4D6520u, 0x4D65C0u))
        return OWN_VIEW;
    cam = MEM32(BO3_VA(0x4D6200u, 0x4D6520u, 0x4D65C0u) + 0x58u);
    if (cam >= 0x10000u && cam < 0x08000000u && MEM32(cam + 4u) + FRAME_LTM == mat)
        return OWN_CAMERA;
    r = rec_find(mat - FRAME_LTM);
    return r && r->veh ? OWN_TRAFFIC : OWN_FRAME;
}

/* A vehicle's matrix (the RwMatrix at mat) taken at this step and blended
 * in place to the picture's time, put back with the blends; 0 when it is
 * not blended. capture() keys an LTM at frame + 0x50; this one is the
 * matrix. A second call in the same picture finds the blend there and
 * keeps it.
 *
 * What is taken, and put back after the picture, is the matrix as the steps
 * left it (snapshot_vehicles, at the picture's first sync), never what the
 * title put in it to draw. A race car after a slam draws its body rocked
 * (sub_0018DA00, from the car's step: a decaying turn, about 2.5 swings in
 * 1.25 s, in a copy at car + 0x1930 with + 0x19AB set; CB3RaceCar::Draw
 * puts the copy in car + 0x10 for the body and the lights and its own
 * matrix back after). That copy, taken as the car's matrix, was put back as
 * the truth and the simulation stepped on from it: the car fishtailed
 * (BO-241). A matrix found holding something else than the steps left is
 * an override for the draw: it is blended under a record of its own (the
 * key + 1) and counted; the put-back value stays the snapshot. */
static int blend_matrix(uint32_t mat, uint32_t step)
{
    uint32_t key = mat - FRAME_LTM;
    float m[16], *g = (float *)(uintptr_t)XBOX_PTR(mat);
    const float *truth;
    unsigned k;
    int ours;
    rec_t *r = rec_get(key), *o;

    if (!r)
        return 0;
    if (!r->veh) {
        r->veh = 1;
        if (s_veh_n < VEH_N)
            s_veh[s_veh_n++] = (uint32_t)(r - s_rec);
    }
    ours = r->put_gen == s_put_gen && !memcmp(g, s_put[r->put].wrote, sizeof r->cur);
    if (r->snap_gen != s_put_gen) {
        /* Not seen at this picture's first sync (drawn before it, or new):
         * the matrix as it is now, the truth when it holds our blend. */
        memcpy(r->snap, ours ? s_put[r->put].cur : g, sizeof r->snap);
        r->snap_gen = s_put_gen;
    }
    truth = r->snap;
    if (!ours && memcmp(g, truth, sizeof r->snap)) {
        s_diag.ovr[owner_of(mat)]++;
        capture_from(key, truth, step);
        o = rec_get(key + 1u);
        if (!o)
            return 0;
        o->veh = 1;
        capture_from(key + 1u, g, step);
        if (!blend_now(o, step, m))
            return 0;
    } else {
        capture_from(key, truth, step);
        if (mat == BO3_VA(0x0073A1D0u, 0x0073A4E0u, 0x0073A580u) + 0x10u)
            s_dbg_car_why = why_not(r, g, step);
        if (!at_take(r, ours ? r->cur : g) || !blend_now(r, step, m))
            return 0;
    }
    if (r->put_gen == s_put_gen) {
        k = r->put;
    } else {
        if (s_put_n == REC_N)
            return 0;
        k = s_put_n++;
        r->put_gen = s_put_gen;
        r->put = k;
        s_put[k].va = key;
        memcpy(s_put[k].wrote1, m, sizeof m);
        memcpy(s_put[k].cur, truth, sizeof s_put[k].cur);
    }
    /* Put back while the matrix still holds a blend of ours: the steps'
     * snapshot, never a value the title wrote to draw (it may be made from
     * our blend). A value the title writes after our last blend stays. */
    memcpy(s_put[k].snap, truth, sizeof s_put[k].snap);
    memcpy(g, m, sizeof m);
    memcpy(s_put[k].wrote, m, sizeof m);
    return 1;
}

/* debug (CAMREL), at each swap: "V <swap> <step> <u> <active> <player 1's
 * car drawn n times> <its +0x18F8> <the camera point x y z>". */
static void dbg_swap(void)
{
    FILE *f = dbg_file();
    uint32_t cp = MEM32(BO3_VA(0x4D6200u, 0x4D6520u, 0x4D65C0u) + 0x68u);
    if (f && MEM32(GAME_STATE) == 5 && cp >= 0x10000u && cp < 0x08000000u)
    {
        uint32_t car = BO3_VA(0x0073A1D0u, 0x0073A4E0u, 0x0073A580u), o = MEM32(car + 0x1198u);
        int ok = o >= 0x10000u && o < 0x08000000u;
        fprintf(f, "V %u %u %.4f %d %d %u %.3f %.3f %.3f W %u %08x %.4f %.4f %.4f %.4f %u\n", s_swaps, MEM32(STEP_COUNT), s_lag / STEP_S, s_active,
                s_dbg_car_drawn, MEM8(car + 0x18F8u), MEMF(cp + 0x30u), MEMF(cp + 0x34u), MEMF(cp + 0x38u),
                MEM8(car + 0x19ABu), o, ok ? MEMF(o + 0x1598u) : 0.0f, ok ? MEMF(o + 0x10DCu) : 0.0f,
                MEMF(car + 0x1598u), MEMF(car + 0x10DCu), MEM8(car + 0x159Cu));
    }
    s_dbg_car_drawn = 0;
}

static FILE *dbg_file(void)
{
    static FILE *f;
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("BO3_HIREFRESH_CAMREL");
        on = e && *e && (f = fopen(e, "w")) != NULL;
        if (on)
            setvbuf(f, NULL, _IOFBF, 1 << 16);
    }
    return on ? f : NULL;
}

/* BO3_HIREFRESH_CAMREL=<file>: player 1's car against the camera the world
 * is drawn with, once a picture at the car's draw: "C <swap> <step> <u> <car
 * blended> <camera frame> <camera blended> <car in the camera's axes x y z>
 * <car's point on the screen x y, 640x480>". The car's trace alone cannot
 * see a camera that is not blended (BO-241). */
static void camrel_probe(uint32_t mat, uint32_t step)
{
    static FILE *f;
    static int on = -1;
    static uint32_t last_sw = ~0u;
    uint32_t v, cam, frame;
    const float *cm, *p, *vm;
    float d[3], c[3], a[3], w, h;
    rec_t *rc, *rf;
    int k;

    f = dbg_file();
    on = f != NULL;
    if (!on || mat != BO3_VA(0x0073A1D0u, 0x0073A4E0u, 0x0073A580u) + 0x10u || last_sw == s_swaps)
        return;
    v = MEM32(VIEW_PTR);
    if (v < 0x10000u || v >= 0x08000000u)
        return;
    cam = MEM32(v + VIEW_CAMERA);
    if (cam < 0x10000u || cam >= 0x08000000u)
        return;
    frame = MEM32(cam + 4u);
    if (frame < 0x10000u || frame >= 0x08000000u)
        return;
    last_sw = s_swaps;
    cm = ltm(frame);
    p = (const float *)(uintptr_t)XBOX_PTR(mat);
    vm = (const float *)(uintptr_t)XBOX_PTR(v);
    for (k = 0; k < 3; k++)
        d[k] = p[12 + k] - cm[12 + k];
    for (k = 0; k < 3; k++) {
        c[k] = dot3(d, cm + k * 4) / dot3(cm + k * 4, cm + k * 4);
        a[k] = p[12] * vm[k] + p[13] * vm[4 + k] + p[14] * vm[8 + k] + vm[12 + k];
    }
    w = (float)(int32_t)MEM32(v + 0x78u);
    h = (float)(int32_t)MEM32(v + 0x7Cu);
    rc = rec_find(mat - FRAME_LTM);
    rf = rec_find(frame);
    fprintf(f, "C %u %u %.4f %d %08x %d %.4f %.4f %.4f %.2f %.2f why %d shake %u ctake %u %u yaw %.3f %.3f\n", s_swaps + 1u, step, s_lag / STEP_S,
            rc && rc->put_gen == s_put_gen && !memcmp(p, s_put[rc->put].wrote, 64), frame,
            rf && rf->put_gen == s_put_gen && !memcmp(cm, s_put[rf->put].wrote, 64),
            c[0], c[1], c[2], a[2] > 0.1f ? a[0] / a[2] * w : -1.0f, a[2] > 0.1f ? a[1] / a[2] * h : -1.0f,
            s_dbg_car_why, MEM8(mat - 0x10u + 0x19ABu), rc ? rc->sc : 0, rc ? rc->sp : 0,
            atan2f(p[8], p[10]) * 57.29578f, atan2f(cm[8], cm[10]) * 57.29578f);
}

/* A side hit on player 1's car, for a diagnostic log (High refresh on or
 * off): an Update where the car's sideways speed changed by more than
 * HIT_LAT units a step in a step (from its pose at each Update, before any
 * step: the simulation's own). For the 2 s (120 steps) after it, one line:
 * the Updates by the steps they ran, the pictures, those where the car and
 * the view's camera were not both blended or both not, the takes found
 * elsewhere, the car's yaw rate (its sign changes, the peak), the car's
 * sideways place in the camera's view per picture (its range and the sign
 * changes of its move: a sway on the screen), and pad 1's steering stick
 * (sign changes, range): whether a sway is the simulation's, the picture's
 * or the player's own steering. */
#define HIT_LAT  0.3f
#define HIT_PAD  BO3_VA(0x004AE20Cu, 0x004AE52Cu, 0x004AE5CCu)      /* the pad manager's records of 0x20 (state 2: open); + 22 the stick's X */
static struct {
    uint32_t last_step, step0;
    int      have, win;
    float    x, z, vx, vz, yaw;
    unsigned upd[5], pics, mism, skips0;
    int      ysign, yflips, ssign, sflips, smin, smax, csign, cflips;
    float    ypk, lat, cmin, cmax, clast, dyaw;
    unsigned wob;
    int      chave;
} s_hit;

static void hit_update(void)
{
    const float *c = (const float *)(uintptr_t)XBOX_PTR(BO3_VA(0x0073A1D0u, 0x0073A4E0u, 0x0073A580u) + 0x10u);
    {
        /* BO3_HIREFRESH_WOBBLE=<s> (test aid): at that race time (car +
         * 0x10DC, the car's clock, a step's time a step) the title's
         * post-slam body wobble is started as a slam would (car + 0x1598,
         * the slam's time, set to the clock; sub_0018DA00). */
        static float at = -2.0f;
        if (at == -2.0f) {
            const char *e = getenv("BO3_HIREFRESH_WOBBLE");
            at = e && *e ? (float)atof(e) : -1.0f;
        }
        if (at > 0.0f && MEM32(GAME_STATE) == 5 && MEMF(BO3_VA(0x0073A1D0u, 0x0073A4E0u, 0x0073A580u) + 0x10DCu) >= at) {
            MEMF(BO3_VA(0x0073A1D0u, 0x0073A4E0u, 0x0073A580u) + 0x1598u) = MEMF(BO3_VA(0x0073A1D0u, 0x0073A4E0u, 0x0073A580u) + 0x10DCu);
            hr_log("[HIREFRESH] wobble: started at car time %.3f\n", MEMF(BO3_VA(0x0073A1D0u, 0x0073A4E0u, 0x0073A580u) + 0x10DCu));
            at = -1.0f;
        }
    }
    uint32_t step = MEM32(STEP_COUNT), d;
    float vx, vz, yaw, n, lat, yr;

    if (MEM32(GAME_STATE) != 5) {
        s_hit.have = s_hit.win = 0;
        return;
    }
    d = step - s_hit.last_step;
    if (s_hit.win)
        s_hit.upd[d > 4 ? 4 : d]++;
    if (!d)
        return;
    yaw = atan2f(c[8], c[10]);
    if (s_hit.have && d <= 4) {
        vx = (c[12] - s_hit.x) / (float)d;
        vz = (c[14] - s_hit.z) / (float)d;
        n = sqrtf(c[8] * c[8] + c[10] * c[10]);
        lat = n > 1e-6f ? ((vx - s_hit.vx) * c[10] - (vz - s_hit.vz) * c[8]) / n : 0.0f;
        yr = yaw - s_hit.yaw;
        if (yr > 3.14159265f) yr -= 6.2831853f;
        if (yr < -3.14159265f) yr += 6.2831853f;
        yr /= (float)d;
        if (!s_hit.win && fabsf(lat) > HIT_LAT && s_hit.vx * s_hit.vx + s_hit.vz * s_hit.vz > 0.01f) {
            memset(s_hit.upd, 0, sizeof s_hit.upd);
            s_hit.win = 1;
            s_hit.step0 = step;
            s_hit.lat = lat;
            s_hit.pics = s_hit.mism = 0;
            s_hit.skips0 = s_diag.skips;
            s_hit.ysign = s_hit.yflips = s_hit.ssign = s_hit.sflips = s_hit.csign = s_hit.cflips = 0;
            s_hit.smin = 32767; s_hit.smax = -32768;
            s_hit.ypk = 0.0f; s_hit.chave = 0;
            s_hit.cmin = 1e9f; s_hit.cmax = -1e9f;
            s_hit.dyaw = 0.0f; s_hit.wob = 0;
        }
        if (s_hit.win) {
            int sg = yr > 0.002f ? 1 : yr < -0.002f ? -1 : 0;
            int st, ss;
            uint32_t k;
            for (k = 0; k < 3 && MEM32(HIT_PAD + k * 0x20u) != 2; k++)
                ;
            st = (int16_t)MEM16(HIT_PAD + k * 0x20u + 22u);
            ss = st > 8000 ? 1 : st < -8000 ? -1 : 0;
            if (sg && s_hit.ysign && sg != s_hit.ysign) s_hit.yflips++;
            if (sg) s_hit.ysign = sg;
            if (fabsf(yr) > s_hit.ypk) s_hit.ypk = fabsf(yr);
            if (ss && s_hit.ssign && ss != s_hit.ssign) s_hit.sflips++;
            if (ss) s_hit.ssign = ss;
            if (st < s_hit.smin) s_hit.smin = st;
            if (st > s_hit.smax) s_hit.smax = st;
            if (step - s_hit.step0 >= 120u) {
                hr_log("[HIREFRESH] side hit at step %u (sideways %+.2f a step, High refresh %s): 2 s after,"
                                " Updates by steps 0/1/2/3/4+ %u/%u/%u/%u/%u; pictures %u, car and camera blended unlike %u,"
                                " takes elsewhere %u; car yaw rate sign changes %d, peak %.0f deg/s; car across the view"
                                " %.2f..%.2f, its move's sign changes %d; stick X %d..%d, sign changes %d;"
                                " pictures with the body's rock (+0x19AB) %u, drawn yaw off the step's by up to %.1f deg\n",
                        s_hit.step0, s_hit.lat, s_active ? "on" : "off",
                        s_hit.upd[0], s_hit.upd[1], s_hit.upd[2], s_hit.upd[3], s_hit.upd[4],
                        s_hit.pics, s_hit.mism, s_diag.skips - s_hit.skips0, s_hit.yflips,
                        s_hit.ypk * 60.0f * 57.29578f, s_hit.chave ? s_hit.cmin : 0.0f,
                        s_hit.chave ? s_hit.cmax : 0.0f, s_hit.cflips, s_hit.smin, s_hit.smax, s_hit.sflips,
                        s_hit.wob, s_hit.dyaw);
                s_hit.win = 0;
            }
        }
        s_hit.vx = vx;
        s_hit.vz = vz;
    } else {
        s_hit.vx = s_hit.vz = 0.0f;
    }
    s_hit.x = c[12];
    s_hit.z = c[14];
    s_hit.yaw = yaw;
    s_hit.have = 1;
    s_hit.last_step = step;
}

/* At player 1's draw, in a hit's window: the car across the camera's view. */
static void hit_picture(uint32_t mat)
{
    static uint32_t last_sw = ~0u;
    uint32_t cam, frame;
    const float *cm, *p;
    float d[3], x;
    rec_t *rc, *rf;
    int cb, fb, k;

    if (!s_hit.win || mat != BO3_VA(0x0073A1D0u, 0x0073A4E0u, 0x0073A580u) + 0x10u || last_sw == s_swaps)
        return;
    last_sw = s_swaps;
    cam = MEM32(MEM32(VIEW_PTR) + VIEW_CAMERA);
    if (MEM32(VIEW_PTR) < 0x10000u || cam < 0x10000u || cam >= 0x08000000u
            || (frame = MEM32(cam + 4u)) < 0x10000u || frame >= 0x08000000u)
        return;
    cm = ltm(frame);
    p = (const float *)(uintptr_t)XBOX_PTR(mat);
    for (k = 0; k < 3; k++)
        d[k] = p[12 + k] - cm[12 + k];
    x = dot3(d, cm) / (dot3(cm, cm) > 1e-9f ? dot3(cm, cm) : 1.0f);
    rc = rec_find(mat - FRAME_LTM);
    rf = rec_find(frame);
    cb = rc && rc->put_gen == s_put_gen && !memcmp(p, s_put[rc->put].wrote, 64);
    fb = rf && rf->put_gen == s_put_gen && !memcmp(cm, s_put[rf->put].wrote, 64);
    s_hit.pics++;
    if (cb != fb && s_lag < STEP_S * 0.999)
        s_hit.mism++;
    if (MEM8(mat - 0x10u + 0x19ABu))
        s_hit.wob++;
    {
        float dy = (atan2f(p[8], p[10]) - s_hit.yaw) * 57.29578f;
        while (dy > 180.0f) dy -= 360.0f;
        while (dy < -180.0f) dy += 360.0f;
        if (fabsf(dy) > s_hit.dyaw)
            s_hit.dyaw = fabsf(dy);
    }
    if (s_hit.chave) {
        float mv = x - s_hit.clast;
        int sg = mv > 0.005f ? 1 : mv < -0.005f ? -1 : 0;
        if (sg && s_hit.csign && sg != s_hit.csign) s_hit.cflips++;
        if (sg) s_hit.csign = sg;
    }
    if (x < s_hit.cmin) s_hit.cmin = x;
    if (x > s_hit.cmax) s_hit.cmax = x;
    s_hit.clast = x;
    s_hit.chave = 1;
}

void bo3_draw_vehicle(bo3_hirefresh_fn title_fn)
{
    uint32_t mat = MEM32(g_esp + 8), step;

    if (!s_active || !mat) {
        if (mat && MEM32(GAME_STATE) == 5)
            camrel_probe(mat, MEM32(STEP_COUNT));
        title_fn();
        return;
    }
    step = MEM32(STEP_COUNT);
    step_changed(step);
    {
        /* BO3_HIREFRESH_OVR=1|2 (test aid): every vehicle but player 1's is
         * drawn from its matrix moved 0.5 units up, as a title's draw-time
         * override would; 1 puts the matrix back after the draw (as
         * CB3RaceCar::Draw does), 2 leaves the moved one. */
        static int ovr = -1;
        float *g = (float *)(uintptr_t)XBOX_PTR(mat), keep[16];
        if (ovr < 0) {
            const char *e = getenv("BO3_HIREFRESH_OVR");
            ovr = e && *e ? atoi(e) : 0;
        }
        if (ovr && mat != BO3_VA(0x0073A1D0u, 0x0073A4E0u, 0x0073A580u) + 0x10u) {
            memcpy(keep, g, sizeof keep);
            g[13] += 0.5f;
            blend_matrix(mat, step);
            camrel_probe(mat, step);
            title_fn();
            if (ovr == 1)
                memcpy(g, keep, sizeof keep);
            return;
        }
    }
    blend_matrix(mat, step);
    trace_car(mat, (const float *)(uintptr_t)XBOX_PTR(mat));
    camrel_probe(mat, step);
    hit_picture(mat);
    if (mat == BO3_VA(0x0073A1D0u, 0x0073A4E0u, 0x0073A580u) + 0x10u)
        s_dbg_car_drawn++;
    title_fn();
}

/* The racers, every one, at the picture's frame sync (BO-241). The rival
 * marker (sub_0018EA50, after the stage's render) gives its look, arrow or
 * position number, by distance rank from the view's camera, not by car: the
 * slots are the ranks, sorted again each picture, at each racer's matrix
 * (the racer table 0x64B38C, + 0x204: the car's matrix, car + 0x10). A car
 * was blended only when drawn (bo3_draw_vehicle): one behind the camera, not
 * drawn, stayed at its step while a rival in view at about the same
 * distance was a blend up to a step behind, so the two swapped ranks from
 * picture to picture and the rival's marker flickered arrow / number
 * (Original mode with High refresh). Now each racer's matrix is blended
 * here, before anything of the picture is drawn, at the one picture time;
 * the draw then finds the blend in place. A car drawn from its own copy
 * (+ 0x19AB set: sub_0018DE00 draws the matrix at + 0x1930 and puts its
 * saved one back after, 0x18DEF2) is left to the draw as before.
 * BO3_HIREFRESH_RACERS=draw: blended at the draw only, as before (the A/B
 * arm). */

static int racers_at_draw(void)
{
    static int v = -1;
    if (v < 0) {
        const char *e = getenv("BO3_HIREFRESH_RACERS");
        v = e && !strcmp(e, "draw");
        if (v)
            fprintf(stderr, "[HIREFRESH] racers blended at their draw only"
                            " (BO3_HIREFRESH_RACERS=draw)\n");
    }
    return v;
}

/* The point the marker ranks from: the view's game camera (view + 0x68, the
 * object sub_001C8A60 builds the camera frame from), its + 0x30, which the
 * steps set. Taken once a step and blended to the picture's time like the
 * racers, so the distances it sorts by are of one time; put back with the
 * blends (restore_blends). */
static struct {
    uint32_t va;                   /* the camera object + 0x30, 0 none */
    uint32_t sp, sc;               /* the step counts of prev and cur */
    int      put;                  /* the blend is in place */
    float    prev[3], cur[3], wrote[3];
} s_cpt;

static void campt_restore(void)
{
    if (s_cpt.put) {
        float *q = (float *)(uintptr_t)XBOX_PTR(s_cpt.va);
        if (!memcmp(q, s_cpt.wrote, sizeof s_cpt.wrote))
            memcpy(q, s_cpt.cur, sizeof s_cpt.cur);
        s_cpt.put = 0;
    }
}

static void campt_blend(uint32_t step)
{
    uint32_t v = MEM32(VIEW_PTR), cp, va;
    float *q, d[3];
    double t, u;
    int k;

    if (v < 0x10000u || v >= 0x08000000u)
        return;
    cp = MEM32(v + 0x68u);
    if (cp < 0x10000u || cp >= 0x08000000u)
        return;
    va = cp + 0x30u;
    q = (float *)(uintptr_t)XBOX_PTR(va);
    if (va != s_cpt.va) {
        memset(&s_cpt, 0, sizeof s_cpt);
        s_cpt.va = va;
    }
    if (s_cpt.put && !memcmp(q, s_cpt.wrote, sizeof s_cpt.wrote))
        return;                    /* blended in this picture already */
    s_cpt.put = 0;
    if (s_cpt.sc != step) {        /* the first take after the steps: the truth */
        if (s_cpt.sc) {
            memcpy(s_cpt.prev, s_cpt.cur, sizeof s_cpt.prev);
            s_cpt.sp = s_cpt.sc;
        } else {
            memcpy(s_cpt.prev, q, sizeof s_cpt.prev);
            s_cpt.sp = step;
        }
        memcpy(s_cpt.cur, q, sizeof s_cpt.cur);
        s_cpt.sc = step;
    } else if (memcmp(q, s_cpt.cur, sizeof s_cpt.cur)) {
        return;                    /* moved by something else within the step */
    }
    if (s_cpt.sp >= s_cpt.sc || s_cpt.sc - s_cpt.sp > 4)
        return;
    t = (double)step - 1.0 + s_lag / STEP_S;
    u = (t - (double)s_cpt.sp) / (double)(s_cpt.sc - s_cpt.sp);
    if (u >= 1.0)
        return;
    if (u < 0.0)
        u = 0.0;
    for (k = 0; k < 3; k++)
        d[k] = s_cpt.cur[k] - s_cpt.prev[k];
    if (dot3(d, d) > SNAP_DIST * SNAP_DIST * (float)((s_cpt.sc - s_cpt.sp) * (s_cpt.sc - s_cpt.sp)))
        return;                    /* a cut */
    for (k = 0; k < 3; k++)
        q[k] = s_cpt.prev[k] + d[k] * (float)u;
    memcpy(s_cpt.wrote, q, sizeof s_cpt.wrote);
    s_cpt.put = 1;
}

static void blend_racers(uint32_t step)
{
    uint32_t n = MEM32(RACERS_N), i;

    if (racers_at_draw())
        return;
    if (n > 8)
        n = 8;
    for (i = 0; i < n; i++) {
        uint32_t obj = MEM32(RACERS + i * 0x30u), mp;
        if (obj < 0x10000u || obj >= 0x08000000u)
            continue;
        mp = MEM32(obj + 0x204u);
        /* The car's matrix at car + 0x10 (CB3RaceCar: the player's cars in
         * the array at 0x73A1D0, the rivals' elsewhere). */
        if (mp < 0x10010u || mp >= 0x08000000u || MEM8(mp - 0x10u + 0x19ABu))
            continue;
        blend_matrix(mp, step);
    }
    campt_blend(step);
}

/* BO3_HIREFRESH_MARKS=<file>: at each swap (High refresh on or off), each
 * racer's point (the rival marker's, sub_0018EA50: the racer's matrix +
 * 0x30) projected with the view matrix as the marker used it, and with the
 * view the world was drawn with (the camera frame's LTM in place, its view
 * window from the true pair): "M <swap> <step> <u> <racer> <is car k> <car
 * blended> <depth> <marker x y> <world x y> <world point>", 640x480 view
 * units; "R <swap> <racer> <point>" for every racer; "S ..." the marker
 * object's slots. */
static void marks_probe(void)
{
    static FILE *f;
    static int on = -1;
    uint32_t v = BO3_VA(0x4D6200u, 0x4D6520u, 0x4D65C0u), cam, frame, n, i;
    float vm[16], vold[16], mt[16], bl[16], bi[16], p[16], vr[16];
    float w, h;
    rec_t *r, *rv;

    if (on < 0) {
        const char *e = getenv("BO3_HIREFRESH_MARKS");
        on = e && *e && (f = fopen(e, "w")) != NULL;
        if (on)
            setvbuf(f, NULL, _IOFBF, 1 << 16);
    }
    if (!on)
        return;
    cam = MEM32(v + VIEW_CAMERA);
    if (cam < 0x10000u || cam >= 0x08000000u)
        return;
    frame = MEM32(cam + 4u);
    if (frame < 0x10000u || frame >= 0x08000000u)
        return;
    memcpy(vm, (void *)(uintptr_t)XBOX_PTR(v), sizeof vm);
    rv = rec_find(v - FRAME_LTM);
    if (rv && rv->put_gen == s_put_gen && !memcmp(vm, s_put[rv->put].wrote, sizeof vm))
        memcpy(vold, s_put[rv->put].cur, sizeof vold);
    else
        memcpy(vold, vm, sizeof vold);
    r = rec_find(frame);
    mat_affine(ltm(frame), bl);
    if (r && r->put_gen == s_put_gen && !memcmp(ltm(frame), s_put[r->put].wrote, sizeof r->cur))
        mat_affine(s_put[r->put].cur, mt);
    else
        memcpy(mt, bl, sizeof mt);
    if (!mat_affine_inv(bl, bi))
        return;
    mat_mul(mt, vold, p);
    mat_mul(bi, p, vr);
    w = (float)(int32_t)MEM32(v + 0x78u);
    h = (float)(int32_t)MEM32(v + 0x7Cu);
    fprintf(f, "P %u %u %.4f %.5f %.5f %.5f %.5f %d\n", s_swaps, MEM32(STEP_COUNT), s_lag / STEP_S,
            p[0], p[5], p[10], p[14], r && r->put_gen == s_put_gen);
    {
        /* The marker object (0x735550: the race world 0x60EA00 + 0x126B50,
         * sub_0018EA50): the looks of its slots (the i-th nearest racer from
         * the view's camera: 1 the position, 0 the arrow), its 30-step
         * counter, its mode. */
        uint32_t mo = BO3_VA(0x735550u, 0x735860u, 0x735900u), cp = MEM32(v + 0x68u);
        if (cp >= 0x10000u && cp < 0x08000000u)
            fprintf(f, "S %u %u %u %u %u %u %u %u counter %u mode %u cam %.3f %.3f %.3f\n", s_swaps,
                    MEM32(mo), MEM32(mo + 4u), MEM32(mo + 8u), MEM32(mo + 12u), MEM32(mo + 16u),
                    MEM32(mo + 20u), MEM32(mo + 0x1Cu), MEM32(mo + 0x18u), MEM32(mo + 0x20u),
                    MEMF(cp + 0x30u), MEMF(cp + 0x34u), MEMF(cp + 0x38u));
    }
    n = MEM32(BO3_VA(0x73A19Cu, 0x73A4ACu, 0x73A54Cu));
    if (n > 8)
        n = 8;
    for (i = 0; i < n; i++) {
        uint32_t obj = MEM32(BO3_VA(0x64B38Cu, 0x64B69Cu, 0x64B73Cu) + i * 0x30u), mp, k;
        int kc = -1, blended;
        const float *q;
        float a[3], c[3];
        int j;
        rec_t *rc;

        if (obj < 0x10000u || obj >= 0x08000000u)
            continue;
        mp = MEM32(obj + 0x204u);
        if (mp < 0x10000u || mp >= 0x08000000u)
            continue;
        for (k = 0; k < 8; k++)
            if (mp == BO3_VA(0x73A1D0u, 0x73A4E0u, 0x73A580u) + k * 0x27E0u + 0x10u)
                kc = (int)k;
        rc = rec_find(mp - FRAME_LTM);
        blended = rc && rc->put_gen == s_put_gen
                  && !memcmp((void *)(uintptr_t)XBOX_PTR(mp), s_put[rc->put].wrote, 64);
        q = (const float *)(uintptr_t)XBOX_PTR(mp + 0x30u);
        for (j = 0; j < 3; j++) {
            a[j] = q[0] * vm[j] + q[1] * vm[4 + j] + q[2] * vm[8 + j] + vm[12 + j];
            c[j] = q[0] * vr[j] + q[1] * vr[4 + j] + q[2] * vr[8 + j] + vr[12 + j];
        }
        fprintf(f, "R %u %u %.3f %.3f %.3f\n", s_swaps, i, q[0], q[1], q[2]);
        if (a[2] < 0.5f || c[2] < 0.5f)
            continue;
        fprintf(f, "M %u %u %.4f %u %d %d %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f\n", s_swaps,
                MEM32(STEP_COUNT), s_lag / STEP_S, i, kc, blended, a[2], a[0] / a[2] * w,
                a[1] / a[2] * h, c[0] / c[2] * w, c[1] / c[2] * h, q[0], q[1], q[2]);
    }
}

/* ── 3. The HUD and the players' cameras ──────────────────── */

/* sub_00167940 (cdecl in esi: the player's record, 0x4CFB20 + i * 0xC50;
 * one float, the frame's time; ret 4): each player's cameras, once a frame
 * from sub_000170B0 after the steps, with the frame's steps times one
 * step's time. The cameras in force (+ 0xC20, a bit each) are updated
 * through their vtable (+ 0x10) with that time, but cameras 0 and 14 with
 * one step's time, MEMF(0x4AE1FC), whatever the steps: they assume a frame
 * is a step. With pictures between steps they ran once a picture, 1.5 to
 * 2.4 times as often as the steps: the chase camera swung three or four
 * times after a side hit where the console swings once, and the look-back
 * view hid the player's car only in the pictures with a step (BO-241). So
 * the cameras run once a step, as shipped: not at all in a picture with no
 * step (their state is a step's; the picture between is the blend), and in
 * a picture after n steps n times with one step's time each. High refresh
 * off: as shipped. */
void bo3_player_frame(bo3_hirefresh_fn title_fn)
{
    uint32_t n = s_upd_steps, ret, i, esi0, edi0, ebx0;
    float dt;

    if (!s_active || !s_in_update || n == 1) {
        title_fn();
        return;
    }
    if (n == 0) {
        g_esp += 8;                    /* its ret 4: nothing run */
        return;
    }
    ret = MEM32(g_esp);
    dt = MEMF(g_esp + 4) / (float)n;
    esi0 = g_esi;
    edi0 = g_edi;
    ebx0 = g_ebx;
    for (i = 0; i < n; i++) {
        if (i) {
            g_esp -= 8;
            MEM32(g_esp) = ret;
            g_esi = esi0;
            g_edi = edi0;
            g_ebx = ebx0;
        }
        MEMF(g_esp + 4) = dt;
        title_fn();
    }
}


/* The car's reflection cube, sub_0003FA20 (stdcall, ret 12: the cube's
 * object 0x4D6170, a point, the player), called once a picture by the
 * stage render sub_001AE340 (0x1AE397, return 0x1AE39C) before the main
 * view, the point the player's at stage + 0x12B810 + player x 0x27E0. While
 * either word at 0x73A190 / 0x73A194 is set it draws six axis-aligned
 * faces around the point (a view each: Clear, the world, the sky bits, the
 * sun sprite) and puts the main target back; with both 0 (the title's "no
 * reflections") it only puts the target back, sub_0034CBF0(obj + 0x890,
 * *(obj + 0x868)), and returns. Nothing reads the faces on the CPU, and the
 * backends copy a face into the cube only when it was drawn again (its
 * gen), so faces not drawn keep the last ones. The faces are ~45 % of a
 * race picture's draws.
 *
 * The point is a step's value: the steps write it and nothing here blends
 * it (the blends are on RenderWare frames and the vehicles' matrices). It
 * did not move in any picture with no step (the "from another point"
 * counter, 0 in every BO-282.1 run), so such a picture drew the faces from
 * the same point again, as the title does at 60. With High refresh in
 * force they are drawn once a step: a picture with no step right after a
 * picture that drew or kept them at this step takes the title's own
 * no-reflection path (the words are not touched: they are read in ~70
 * places). Kept faces counted "from another point" if the point moved
 * after all (it should stay 0). Any other picture, High refresh off, at 60,
 * split screen: as shipped. BO3_HIREFRESH_CUBE=picture: drawn every
 * picture (the build before, for an A/B). */
#define CUBE_ON_A    BO3_VA(0x0073A190u, 0x0073A4A0u, 0x0073A540u)     /* the title's gate: either word set */
#define CUBE_ON_B    BO3_VA(0x0073A194u, 0x0073A4A4u, 0x0073A544u)
#define CUBE_RET     BO3_VA(0x001AE39Cu, 0x001AE69Cu, 0x001AE73Cu)   /* the stage render's call */
extern void BO3_SUB(sub_0034CBF0, sub_0034CEF0, sub_0034CF90)(void);
static struct {
    uint32_t upd;                  /* s_updates of the last call drawn or kept, 0 none */
    uint32_t step;                 /* the step count the faces were drawn at */
    uint32_t obj, player;
    uint32_t pt[4];                /* the point they were drawn from */
} s_cube;

static int cube_per_picture(void)
{
    static int v = -1;
    if (v < 0) {
        const char *e = getenv("BO3_HIREFRESH_CUBE");
        v = e && !strcmp(e, "picture");
        if (v)
            fprintf(stderr, "[HIREFRESH] reflection cube drawn every picture (BO3_HIREFRESH_CUBE=picture)\n");
    }
    return v;
}

void bo3_reflection_cube(bo3_hirefresh_fn title_fn)
{
    uint32_t obj = MEM32(g_esp + 4u), pt = MEM32(g_esp + 8u), player = MEM32(g_esp + 12u), frame;
    uint32_t step = MEM32(STEP_COUNT);
    int on = (MEM32(CUBE_ON_A) | MEM32(CUBE_ON_B)) != 0;

    if (!s_active || !s_in_update || MEM32(g_esp) != CUBE_RET || !on
            || pt < 0x10000u || pt >= 0x08000000u || cube_per_picture()) {
        s_cube.upd = 0;
        title_fn();
        return;
    }
    if (s_upd_steps != 0 || s_cube.upd == 0 || s_cube.upd + 1u != s_updates
            || s_cube.step != step || s_cube.obj != obj || s_cube.player != player) {
        s_cube.upd = s_updates;
        s_cube.step = step;
        s_cube.obj = obj;
        s_cube.player = player;
        memcpy(s_cube.pt, (const void *)(uintptr_t)XBOX_PTR(pt), sizeof s_cube.pt);
        s_diag.cube_drawn++;
        title_fn();
        return;
    }
    /* Kept: the faces of this step's picture before. */
    s_cube.upd = s_updates;
    s_diag.cube_skipped++;
    if (memcmp(s_cube.pt, (const void *)(uintptr_t)XBOX_PTR(pt), 12))
        s_diag.cube_moved++;
    /* The title's gate path (0x3FA20-0x3FA56), instruction for
     * instruction: its frame, the three saves, the call with its two
     * arguments, the restores and ret 12. */
    g_esp -= 4u;
    MEM32(g_esp) = g_ebp;              /* push ebp */
    frame = g_esp;                     /* mov ebp, esp */
    g_ebp = frame;
    g_seh_ebp = frame;
    g_esp = (g_esp & 0xFFFFFFF0u) - 0x64u;
    g_esp -= 4u; MEM32(g_esp) = g_ebx;
    g_esp -= 4u; MEM32(g_esp) = g_esi;
    g_esp -= 4u; MEM32(g_esp) = g_edi;
    g_eax = obj;                       /* mov eax, [ebp + 8] */
    g_ecx = MEM32(obj + 0x868u);
    g_esp -= 4u; MEM32(g_esp) = g_ecx;
    g_eax = obj + 0x890u;
    g_esp -= 4u; MEM32(g_esp) = g_eax;
    g_ebp = frame;
    g_seh_ebp = frame;
    g_esp -= 4u; MEM32(g_esp) = BO3_VA(0x0003FA4Eu, 0x0003F99Eu, 0x0003FC5Eu);
    BO3_SUB(sub_0034CBF0, sub_0034CEF0, sub_0034CF90)();                    /* ret 8 */
    g_edi = MEM32(g_esp); g_esp += 4u;
    g_esi = MEM32(g_esp); g_esp += 4u;
    g_ebx = MEM32(g_esp); g_esp += 4u;
    g_esp = frame + 4u;                /* mov esp, ebp; pop ebp (the title's is a local) */
    g_esp += 16u;                      /* ret 12 */
}

/* 3. The HUD's update runs once a step: as shipped (its call is only
 * counted for the BO-275 audit). */
void bo3_hud_update(bo3_hirefresh_fn title_fn)
{
    bo3_hraudit_hud(MEMF(g_esp + 4), MEMF(g_esp + 4));
    title_fn();
}

/* 4. The race audio manager, sub_0013EF40 (eax = 0x40E120; called by
 * sub_000170B0 once a frame after the steps while 0x40E124 is 0x17): its
 * one-shot holds and cooldowns count frames (sub_001401F0: + 0x14, + 0x48,
 * + 0x50, + 0x54, + 0x68..+ 0x6C), it ramps a volume by a constant a call
 * (+ 0x1C), counts the start fade (+ 0x3D3C), slews the room level
 * 0x3EC250 by 300 a call and steps the audio random generator 0x4A1BE0;
 * the engine, skid and impact sounds below it are updated the same way.
 * Once a step, as the cameras: not run in a picture with no step (its
 * plain ret emulated), n times after n steps. High refresh off: as
 * shipped. */
void bo3_race_audio(bo3_hirefresh_fn title_fn)
{
    uint32_t n = s_upd_steps, ret, i, eax0, esi0, edi0, ebx0, ebp0;

    bo3_hraudit_count(1, !s_active || !s_in_update ? 1u : n);
    if (!s_active || !s_in_update || n == 1) {
        title_fn();
        return;
    }
    if (n == 0) {
        g_esp += 4;                    /* its ret: nothing run */
        return;
    }
    ret = MEM32(g_esp);
    eax0 = g_eax;
    esi0 = g_esi;
    edi0 = g_edi;
    ebx0 = g_ebx;
    ebp0 = g_ebp;
    for (i = 0; i < n; i++) {
        if (i) {
            g_esp -= 4;
            MEM32(g_esp) = ret;
            g_esi = esi0;
            g_edi = edi0;
            g_ebx = ebx0;
            g_ebp = ebp0;
        }
        g_eax = eax0;
        title_fn();
    }
}

/* 4. The audio world's update, sub_001CA530 (eax = 0x40B310; four
 * arguments, the last the frame's time; ret 16), called by sub_000170B0
 * once a frame right after the race audio manager (0x17136; return
 * 0x1713B). It runs every sound source's update: the volume and pitch from
 * its bus, the bus fades (sub_001CDBC0, by the time given) and the
 * streamed sounds' states (+ 0x170, sub_001CB7E0). The streams hand over
 * to the race audio manager's stream player (sub_0014C880 ->
 * sub_00150E80, object 0x40F270) through those states: when a stream has
 * been stopped (6) the player picks the next file (the junction's
 * crash1.rws at an impact in Crash mode, the drone under Impact Time; the
 * playlist tracks\crash%d.rws otherwise) and clears its "started" byte
 * (+ 0x8DB), and it starts that file once the source is idle (0, 1, 4).
 * The source leaves 6 for 4 in this update. As shipped the player always
 * runs before this in the same frame, so it sees every 6; with the manager
 * once a step (bo3_race_audio) and this once a picture, a picture with no
 * step moved the source from 6 to 4 unseen, the player kept its "started"
 * byte and never started another stream: from the first crash on, no
 * Impact Time drone and no crash music until a restart (BO-297, GitHub #41).
 * So this runs with the manager: not at all in a picture with no step (its
 * ret 16 emulated), once with the frame's time after one or more steps, as
 * shipped. Its other work moves only by the time given, which is 0 in a
 * picture with no step. Other call sites, High refresh off: as shipped.
 * BO3_HRAUDIO_WORLD=picture: once a picture (the build before the fix, for
 * an A/B). */
static int audio_world_per_picture(void)
{
    static int v = -1;
    if (v < 0) {
        const char *e = getenv("BO3_HRAUDIO_WORLD");
        v = e && !strcmp(e, "picture");
        if (v)
            fprintf(stderr, "[HIREFRESH] audio world once a picture (BO3_HRAUDIO_WORLD=picture)\n");
    }
    return v;
}

void bo3_audio_world(bo3_hirefresh_fn title_fn)
{
    if (s_active && s_in_update && s_upd_steps == 0 && MEM32(g_esp) == BO3_VA(0x0001713Bu, 0x000171FBu, 0x000171FBu)
            && !audio_world_per_picture()) {
        g_esp += 20;                   /* its ret 16: nothing run */
        return;
    }
    title_fn();
}

/* 4. The effects manager's clock, 0x5A9A00 (+ 4 a duration, + 8 a mode:
 * 1 a flash up and down, 2 a fade out), advanced and drawn by sub_00188750
 * (eax = the manager) once a picture from the effects render
 * sub_0017F7E0: t += 0x4D53AC x one step's time, but during the render
 * 0x4D53AC holds the stepper's count for the next frame (at least 1), so
 * every picture gained a step. The race's end (sub_00026050: all
 * finished) starts a flash on it and waits for half of it before the
 * results, so the flash ran Hz/60 fast and the results came sooner. Here
 * the clock gains the steps this Update ran, the flash is drawn at the
 * picture's time (the clock lerped like the cars, a step behind at most)
 * and the mode is what the title's own rule (off once t reaches the
 * duration) gives at the steps' time: the step logic reads only that. */
#define FX_STEPS     BO3_VA(0x4D53ACu, 0x4D56CCu, 0x4D576Cu)
#define FX_STEP_DT   BO3_VA(0x4AE1FCu, 0x4AE51Cu, 0x4AE5BCu)
void bo3_fx_clock(bo3_hirefresh_fn title_fn)
{
    uint32_t m = g_eax, steps0, mode0;
    float t0, t1, dt;

    if (!s_active || !s_in_update || m < 0x10000u || m >= 0x04000000u) {
        title_fn();
        return;
    }
    steps0 = MEM32(FX_STEPS);
    mode0 = MEM32(m + 8u);
    t0 = MEMF(m);
    dt = MEMF(FX_STEP_DT);
    t1 = (float)s_upd_steps * dt + t0;          /* as the title adds it */
    {
        float td = t1 - (float)(1.0 - s_lag / STEP_S) * dt;
        MEMF(m) = td > 0.0f ? td : 0.0f;
    }
    MEM32(FX_STEPS) = 0;
    title_fn();
    MEM32(FX_STEPS) = steps0;
    MEMF(m) = t1;
    MEM32(m + 8u) = (mode0 == 1u || mode0 == 2u) && MEMF(m + 4u) <= t1 ? 0u : mode0;
}

/* ── 6. The sparks ────────────────────────────────────────────
 *
 * The sparks (the six particle systems of the effects manager 0x5A9A00,
 * + 0x39DD0, 0xF0 each; fxspark, descriptors 0x418EA0 + 0x7C each) are not
 * stepped particles drawn where they stand: sub_000367C0 draws each one as
 * a streak through the last pictures. A history of 18 entries a player
 * (0x5492B0 + player * 0x5A0, 0x50 each: the picture's world-to-clip matrix,
 * 0x4D6670, and its time, + 0x40) is pushed once a picture by sub_00038200
 * (from the effects render sub_0017F7E0, 0x17F9C9): entry 0 is the picture
 * itself; when it is 1/96 s (0x386EF8) past entry 1 the history moves down
 * one first, and in every picture, while 0x4D53BC or 0x4A4B90 is 0, the
 * older entries' times are put back by half the frame's time (0x4D53B0, the
 * frame's steps x 0x60EA1C), which draws the streak half as long again. The draw places each spark at each entry's
 * time (its start, velocity and gravity, evaluated anew) through that
 * entry's matrix, one quad from entry to entry, while the entry is younger
 * than the descriptor's life (+ 0x5C: 0.049 s, 0x3687D), and stops at the
 * first two entries with the same time (0x3688C, a paused race). The six
 * draws are at 0x17F9E1.
 *
 * The times are the race clock, 0x60EA20, which moves only at the steps. A
 * picture between two steps had the time of the one before: it was 1/60 s
 * past entry 1, so the history moved down, and the two newest entries had the
 * same time. Such a picture drew no spark at all, and the next one a single
 * quad of the history (at 120 Hz: none in every other picture, one quad in
 * the rest, where 60 Hz draws three, 0.05 s of the spark's path; the faster
 * the display, the fewer). The owner saw them get thinner with the rate
 * (BO-274).
 *
 * The history is the title's way of drawing at any frame rate, so it is
 * given the picture's time: the race clock at the picture's place between
 * its last two steps, and the frame's time from the last picture to this
 * one, for the push and the six draws, put back after each call. The
 * entries are then pictures, 1/96 s or more apart, and the streak is the
 * same 0.05 s of the spark's path as at 60 (more quads, finer). The sparks
 * are placed at the picture's time, as the cars are. High refresh off, or a
 * picture outside the active stage: as shipped. BO3_HIREFRESH_SPARKS=step
 * leaves them at the steps' clock (the A/B arm), =ab takes the two in turn,
 * 15 steps each; BO3_HIREFRESH_SPARKLOG=<file> writes a line a push for
 * player 1 (the history after it) and what the draws put in the push buffer. */
#define RACE_CLOCK   BO3_VA(0x60EA20u, 0x60ED30u, 0x60EDD0u)     /* the race clock, s; moves at the steps */
#define STEP_TIME    BO3_VA(0x60EA1Cu, 0x60ED2Cu, 0x60EDCCu)     /* a step's time on that clock */
#define FRAME_TIME   BO3_VA(0x4D53B0u, 0x4D56D0u, 0x4D5770u)     /* the frame's steps x STEP_TIME */
#define SPARK_HIST   BO3_VA(0x5492B0u, 0x5495D0u, 0x549670u)     /* the streaks' history, 0x5A0 a player */
#define SPARK_LIFE   BO3_VA(0x418EFCu, 0x41922Cu, 0x4192CCu)     /* the first spark descriptor's life (+ 0x5C) */

static struct {
    int      valid;
    uint32_t sc;                   /* the step the clock was read at */
    float    cur, prev;            /* the clock at it and at the step before */
    uint32_t pic_swap;             /* the picture the times below are for */
    float    now, ft;              /* this picture's clock and frame time */
    double   last_p;               /* the last picture's place, in steps */
    int      have_last;
} s_spark;

/* BO3_HIREFRESH_SPARKS=step: the steps' clock (as before the fix); =ab:
 * the steps' clock and the picture's in turn, 15 steps each (test). */
static int sparks_at_step(void)
{
    static int v = -1;
    if (v < 0) {
        const char *e = getenv("BO3_HIREFRESH_SPARKS");
        v = !e ? 0 : strcmp(e, "step") == 0 ? 1 : strcmp(e, "ab") == 0 ? 2 : 0;
    }
    if (v == 2)
        return (MEM32(STEP_COUNT) / 15u) % 2u == 0u;
    return v;
}

static FILE *spark_log(void)
{
    static FILE *f;
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("BO3_HIREFRESH_SPARKLOG");
        on = e && *e && (f = fopen(e, "w")) != NULL;
        if (on)
            setvbuf(f, NULL, _IOFBF, 1 << 16);
    }
    return on ? f : NULL;
}

/* The race clock read once a step: at the step and the one before. Several
 * steps in one Update: the one before is taken an even share back. */
static void spark_clock(uint32_t step)
{
    float v = MEMF(RACE_CLOCK);

    if (s_spark.valid && step == s_spark.sc) {
        if (v != s_spark.cur)      /* set without a step (a restart) */
            s_spark.cur = s_spark.prev = v;
        return;
    }
    if (s_spark.valid && step > s_spark.sc && step - s_spark.sc <= 4u
        && v >= s_spark.cur && v - s_spark.cur <= 0.1f * (float)(step - s_spark.sc))
        s_spark.prev = v - (v - s_spark.cur) / (float)(step - s_spark.sc);
    else
        s_spark.prev = v;
    s_spark.cur = v;
    s_spark.sc = step;
    s_spark.valid = 1;
}

/* 1 and the picture's clock and frame time in *now / *ft, while they are
 * the picture's; 0: as shipped. */
static int spark_times(float *now, float *ft)
{
    uint32_t step = MEM32(STEP_COUNT);
    double p;
    float u;

    if (!s_active || !s_in_update || sparks_at_step() || step != s_steps_end)
        return 0;
    spark_clock(step);
    if (!s_spark.have_last || s_spark.pic_swap != s_swaps) {
        u = (float)(s_lag / STEP_S);
        s_spark.now = s_spark.prev + (s_spark.cur - s_spark.prev) * u;
        p = (double)step - 1.0 + s_lag / STEP_S;
        {
            /* The clock's advance since the last picture; after a gap
             * (High refresh was off), the picture's own time. */
            double adv = s_spark.have_last && s_swaps - s_spark.pic_swap == 1u
                         ? p - s_spark.last_p : s_pic_dt / STEP_S;
            if (adv < 0.0) adv = 0.0;
            if (adv > 4.0) adv = 4.0;
            s_spark.ft = (float)(adv * MEMF(STEP_TIME));
        }
        s_spark.last_p = p;
        s_spark.have_last = 1;
        s_spark.pic_swap = s_swaps;
    }
    *now = s_spark.now;
    *ft = s_spark.ft;
    return 1;
}

static void spark_log_push(uint32_t player, int ours, float now, float ft)
{
    FILE *f = spark_log();
    uint32_t h = SPARK_HIST + player * 0x5A0u;
    float life = MEMF(SPARK_LIFE), t0 = MEMF(h + 0x40);
    int k, c = 0;

    if (!f || player != 0)
        return;
    for (k = 0; k < 17; k++) {
        float tk = MEMF(h + 0x40 + k * 0x50), tn = MEMF(h + 0x40 + (k + 1) * 0x50);
        if (life <= t0 - tk || tk == tn)
            break;
        c++;
    }
    fprintf(f, "S %u %u %u %d %.5f %.5f %.5f %.5f %d", s_swaps, MEM32(STEP_COUNT),
            s_upd_steps, ours, MEMF(RACE_CLOCK), now, ft, MEMF(STEP_TIME), c);
    for (k = 0; k <= c && k < 18; k++)
        fprintf(f, " %.1f", (t0 - MEMF(h + 0x40 + k * 0x50)) * 1000.0f);
    fputc('\n', f);
}

void bo3_spark_history(bo3_hirefresh_fn title_fn)
{
    uint32_t player = g_ecx;
    float now, ft, clock, frame;
    int ours = spark_times(&now, &ft);

    if (!ours) {
        title_fn();
        spark_log_push(player, 0, MEMF(RACE_CLOCK), MEMF(FRAME_TIME));
        return;
    }
    clock = MEMF(RACE_CLOCK);
    frame = MEMF(FRAME_TIME);
    MEMF(RACE_CLOCK) = now;
    MEMF(FRAME_TIME) = ft;
    title_fn();
    MEMF(RACE_CLOCK) = clock;
    MEMF(FRAME_TIME) = frame;
    spark_log_push(player, 1, now, ft);
}

/* With BO3_HIREFRESH_SPARKLOG: "P <swap> <system address> <sparks>" for a system
 * drawing any, the sparks of its live bursts (the system's four at + 0x40,
 * 0x14 each: + 4 the count, + 0xC the life, + 0x10 the start). */
static void spark_log_draw(uint32_t sys, float now)
{
    FILE *f = spark_log();
    unsigned i, n = 0;

    if (!f)
        return;
    for (i = 0; i < 4; i++) {
        uint32_t b = sys + 0x40u + i * 0x14u;
        if (now - MEMF(b + 0x10) < MEMF(b + 0xC))
            n += MEM32(b + 4);
    }
    if (n)
        fprintf(f, "P %u %08X %u\n", s_swaps, sys, n);
}

/* With BO3_HIREFRESH_SPARKLOG, "Q <swap> <bytes>": what the draw wrote
 * into D3D's push buffer (its put pointer, the word the pointer at 0x35FB48
 * points at), 160 bytes a spark quad plus the draw's own state. */
#define D3D_PUSH BO3_VA(0x35FB48u, 0x35FE48u, 0x35FEE8u)

void bo3_spark_draw(bo3_hirefresh_fn title_fn)
{
    float now, ft, clock;
    uint32_t put0 = spark_log() ? MEM32(MEM32(D3D_PUSH)) : 0;
    int ours = spark_times(&now, &ft);

    spark_log_draw(MEM32(g_esp + 4), ours ? now : MEMF(RACE_CLOCK));
    if (!ours) {
        title_fn();
    } else {
        clock = MEMF(RACE_CLOCK);
        MEMF(RACE_CLOCK) = now;
        title_fn();
        MEMF(RACE_CLOCK) = clock;
    }
    if (put0) {
        uint32_t put1 = MEM32(MEM32(D3D_PUSH));
        if (put1 >= put0 && put1 - put0 < (1u << 22))
            fprintf(spark_log(), "Q %u %u\n", s_swaps, put1 - put0);
    }
}
