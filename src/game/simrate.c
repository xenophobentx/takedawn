/*
 * simrate.c - graphics.native_rate (the Display page's "Native frame rate",
 * read at start): the simulation itself at the display's refresh rate, a
 * step a frame, instead of the title's 60 steps a second with High refresh's
 * pictures blended between them.
 *
 * The title keeps its step as data: sub_00015A60 sets MEMF(0x49C120) (the
 * step in seconds) and MEM32(0x49C11C) (in whole ms) and hands the stepper
 * (timer 0x4D5338) its period in ms through sub_001B5880, which
 * sub_00016E00 calls again at every scene change. The game's timers
 * (sub_001B5A80) copy 0x49C120 into + 0x1C and turn steps into seconds with
 * it. So with the setting on (a split-screen race keeps the shipped step,
 * and so does an online session: see shipped_step), the period is scaled to
 * 1 / N s (N the display's rate), the step globals say 1 / N, and every
 * timer made so far that holds the old step gets the new one (only a timer
 * whose + 0x1C is exactly the old step, so memory reused for something else
 * is left alone). High refresh
 * (hirefresh.c) then runs the frames with STEP_S = 1 / N: a step a picture
 * when the machine keeps up, and its blend at most one short step long.
 *
 * Code that counts calls instead of reading the step was made for the
 * title's own rate, so it keeps it:
 * - frozen states (the pause menu, the results pages, the signature
 *   takedown's photos: stepper mode 2 or held) run at the shipped steps with
 *   the shipped step's time (their menus count steps, their transitions run
 *   by time);
 * - the race audio manager and the HUD's update (hirefresh.c) keep the
 *   shipped calls a second;
 * - the chase camera eases by factors to the power of a call count, and
 *   camera 0/14 and camera 13 move by a constant a call: scaled below.
 * The front end and the loads run at the display's rate too (hirefresh.c:
 * the front end unblended, a step a picture, for the showroom's cars).
 *
 * game.loading_min (bo3_load_hold): the loading screen held up a least
 * time, the load itself untouched.
 *
 * For a run without a display, RECOMP_FLIP_PACING=host:<Hz> gives the rate.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "recomp_types.h"
#include "platform/recomp_env.h"
#include "platform/host_time.h"
#ifdef XBOXRECOMP_HAVE_OVERLAY
#include "xbox_settings.h"
#endif
#include "region.h"
#include "simrate.h"
#include "xnet_hle.h"                  /* xnet_hle_session_up */

#define STEP_S_G   BO3_VA(0x49C120u, 0x49C440u, 0x49C4E0u)   /* the step, s (float) */
#define STEP_MS_G  BO3_VA(0x49C11Cu, 0x49C43Cu, 0x49C4DCu)   /* the step, whole ms */
#define TIMER      BO3_VA(0x4D5338u, 0x4D5658u, 0x4D56F8u)   /* the stepper: mode + 0, period (ticks) + 0x20, held + 0x34 */
#define PLAYERS    BO3_VA(0x73A1C0u, 0x73A4D0u, 0x73A570u)   /* players at once */
#define GAME_STATE BO3_VA(0x4D53B4u, 0x4D56D4u, 0x4D5774u)   /* the game object's state, 5 running */
#define STAGE      BO3_VA(0x4D5370u, 0x4D5690u, 0x4D5730u)   /* the stage; its mode object at + 0x1B8, 0 in the front end */
#define CHASE_K    BO3_VA(0x3F720Cu, 0x3F753Cu, 0x3F75DCu)   /* the chase camera's 60: n = round(K t + 0.5) */
#define CAM0_TURN  BO3_VA(0x3B1698u, 0x3B19B8u, 0x3B1A58u)   /* camera 0/14's turn a call, 3 degrees */
#define CAM13_EASE BO3_VA(0x3A69BCu, 0x3A6CDCu, 0x3A6D7Cu)   /* camera 13's easing a call, 0.05 */
#define TIMERS_MAX 4096

extern double xbox_DisplayRate(void), xbox_DisplayModeRate(void);

static int s_on;                    /* graphics.native_rate */
static int s_hz = 60;               /* the rate, chosen once */
static int s_short;                 /* the short step is in force */
static float s_title_step;          /* the title's own: 1/60 (1/50 on a PAL 50 Hz path) */
static uint32_t s_title_ms;
static uint32_t s_timers[TIMERS_MAX];
static int s_ntimers;
static uint32_t s_ticks_short, s_ticks_title;   /* the stepper's period, short and as shipped */
static uint32_t s_ticks_asked;                  /* the period the title asked for last */
static int s_was_frozen;                        /* the last Update's freeze, for the short step */

/* Timers the title sets up inline, not through sub_001B5A80: the game
 * object's (game + 0x7040, sub_00015F10; its step at 0x4AE1FC is what the
 * HUD and the time played gain a step) and the one sub_001C0520 sets up. */
static const uint32_t k_inline_timers[] = { BO3_VA(0x4AE1E0u, 0x4AE500u, 0x4AE5A0u), BO3_VA(0x49FA98u, 0x49FDB8u, 0x49FE58u) };

/* After the settings are read (main.c): the setting, and the three it needs
 * on, as values (the file keeps its own): High refresh's frames, the crash
 * cameras at the full rate (their half rate runs on the vblank) and the race
 * clock (with crash cameras at the full rate the stepper would otherwise
 * stay at a step a frame, and the race slow down whenever High refresh
 * stood aside for a moment). They are put back before every Update too
 * (bo3_sim_before_update): the settings are read into the environment again
 * once the defaults are in (xbox_SettingsFinish), and a row turned off in
 * the overlay sets its value. */
#ifdef XBOXRECOMP_HAVE_OVERLAY
#define NEEDED(env, row) { env, row }
#else
#define NEEDED(env, row) { env }
#endif
static const struct {
    recomp_env_id id;
#ifdef XBOXRECOMP_HAVE_OVERLAY
    xs_id row;
#endif
} k_needed[] = {
    NEEDED(RENV_GFX_HIGH_REFRESH, XS_GRAPHICS_HIGH_REFRESH),
    NEEDED(RENV_GFX_CRASH_60FPS, XS_GRAPHICS_CRASH_60FPS),
    NEEDED(RENV_GAME_RACE_CLOCK, XS_GAME_RACE_CLOCK),
};

static void needed_on(void)
{
    unsigned i;

    for (i = 0; i < sizeof k_needed / sizeof k_needed[0]; i++) {
        const char *v = recomp_env(k_needed[i].id);
        if (!v || strcmp(v, "1"))
            recomp_env_set(k_needed[i].id, "1");
    }
}

void bo3_sim_settings(void)
{
    const char *v = recomp_env(RENV_GFX_NATIVE_RATE);
    unsigned i;

    if (!v || !*v || !strcmp(v, "0"))
        return;
    s_on = 1;
    /* In the process environment too, as a player's RECOMP_* variable
     * would be: the settings are read into the values again once the
     * defaults are in (xbox_SettingsFinish), and an environment variable
     * wins there, from the first frame on. */
    for (i = 0; i < sizeof k_needed / sizeof k_needed[0]; i++) {
#if defined(_WIN32)
        _putenv_s(recomp_env_name(k_needed[i].id), "1");
#else
        setenv(recomp_env_name(k_needed[i].id), "1", 1);
#endif
#ifdef XBOXRECOMP_HAVE_OVERLAY
        xbox_SettingLock(k_needed[i].row, "Native frame rate");  /* their rows: held */
#endif
    }
    needed_on();
    fprintf(stderr, "[SIMRATE] native frame rate: the simulation at the display's rate (High refresh, "
            "60 fps crash cameras and the race clock on)\n");
}

int bo3_sim_short(void)
{
    return s_short;
}

/* The rate: the display mode's stated one (the measured one wanders while
 * the game starts: 111 Hz once on a 170 Hz display, and this is chosen
 * once), the measured one when that is unknown; 60 when not faster. */
static int display_rate(void)
{
    const char *e = recomp_env(RENV_FLIP_PACING);
    double hz = e && !strncmp(e, "host:", 5) ? atof(e + 5) : xbox_DisplayModeRate();

    if (hz < 61.0)
        hz = xbox_DisplayRate();
    return hz >= 61.0 && hz <= 1000.0 ? (int)(hz + 0.5) : 60;
}

double bo3_sim_title_step_s(void)
{
    return s_title_step > 0.0f ? s_title_step : 1.0 / 60.0;
}

/* The race standing still: the pause menu (the stepper held) or the pause
 * and results pages (stepper mode 2). */
static int frozen(void)
{
    return MEM32(TIMER) == 2 || MEM8(TIMER + 0x34);
}

/* The step the frames run now: hirefresh.c's STEP_S. */
double bo3_sim_step_s(void)
{
    return s_short && !frozen() ? 1.0 / s_hz : bo3_sim_title_step_s();
}

/* The short step in whole ms, rounded (the title truncates its own 16.67
 * to 16: 4%; 1000/170 truncated would be 15% short). */
static uint32_t short_ms(void)
{
    return (uint32_t)(1000.0 / s_hz + 0.5);
}

/* The step globals and every timer holding `from` get `to`. */
static void retime(float from, float to, uint32_t to_ms)
{
    int i;

    if (MEMF(STEP_S_G) == from) {
        MEMF(STEP_S_G) = to;
        MEM32(STEP_MS_G) = to_ms;
    }
    for (i = 0; i < s_ntimers; i++)
        if (MEMF(s_timers[i] + 0x1C) == from)
            MEMF(s_timers[i] + 0x1C) = to;
}

static void regime(int on);

/* The shipped step is kept in a split-screen race and while an online
 * session is up (xnet_hle_session_up: signed in to the online menus, a
 * game's lobby, its race, its results): the title's netcode assumes the
 * shipped step on every peer, so online play runs as shipped until
 * native-rate peers have been tried against each other. Its row is locked
 * meanwhile (the setting's XS_OFF_ONLINE flag). Once the session is over
 * the short step is back at the next scene change (the next loading
 * screen). */
static int shipped_step(void)
{
    /* Split screen: its race (the running state) at the shipped step, its
     * load (the player count is 2 from the load's first frame) at the short
     * one, so the load flips at the display's rate as a one-player one does
     * (hirefresh.c bo3_update); the switch comes with the running state. */
    if (MEM32(PLAYERS) >= 2 && MEM32(GAME_STATE) == 5)
        return 1;
#ifdef XNET_HLE_HAS_SESSION_UP
    if (xnet_hle_session_up())
        return 1;
#endif
    return 0;
}

/* Before each Update (the recomp_manual*.c wraps): the stepper's period
 * follows, and a freeze gets the shipped step's time with the shipped
 * steps. */
void bo3_sim_before_update(void)
{
    int fz;

    if (s_on)
        needed_on();
    if (!s_short || !s_ticks_short)
        return;
    if (shipped_step()) {
        /* a split-screen race begun, or an online session: the shipped
         * step, and the period the title asked for */
        regime(0);
        MEM32(TIMER + 0x20) = s_ticks_asked;
        MEM32(TIMER + 0x24) = 0;
        return;
    }
    fz = frozen();
    MEM32(TIMER + 0x20) = fz ? s_ticks_title : s_ticks_short;
    MEM32(TIMER + 0x24) = 0;
    if (fz != s_was_frozen) {
        float shortstep = 1.0f / (float)s_hz;
        if (fz)
            retime(shortstep, s_title_step, s_title_ms);
        else
            retime(s_title_step, shortstep, short_ms());
        s_was_frozen = fz;
    }
}

static void regime(int on)
{
    float from, to;
    int i, n = 0;

    if (on == s_short)
        return;
    if (!s_ntimers || s_timers[0] != k_inline_timers[0]) {
        /* first time: the inline ones head the list */
        for (i = s_ntimers - 1; i >= 0 && i + 2 < TIMERS_MAX; i--)
            s_timers[i + 2] = s_timers[i];
        s_timers[0] = k_inline_timers[0];
        s_timers[1] = k_inline_timers[1];
        s_ntimers = s_ntimers + 2 > TIMERS_MAX ? TIMERS_MAX : s_ntimers + 2;
    }
    from = MEMF(STEP_S_G);
    to = on ? 1.0f / (float)s_hz : s_title_step;
    MEMF(STEP_S_G) = to;
    MEM32(STEP_MS_G) = on ? short_ms() : s_title_ms;
    for (i = 0; i < s_ntimers; i++)
        if (MEMF(s_timers[i] + 0x1C) == from) {
            MEMF(s_timers[i] + 0x1C) = to;
            n++;
        }
    s_short = on;
    s_was_frozen = 0;                   /* the step globals and timers hold `to` now, frozen or not */
    fprintf(stderr, "[SIMRATE] %s: step %.6f -> %.6f s, %d of %d timers\n", on ? "on" : "off", from, to,
            n, s_ntimers);
}

/* sub_001B5880(float ms, int): the stepper's period; the caller's float at
 * esp + 4 on entry. Every scene change comes here: the short step is in
 * force unless shipped_step(). */
void bo3_sim_set_period(bo3_title_fn title_fn)
{
    float asked;
    int on;

    if (!s_on) {
        title_fn();
        return;
    }
    if (s_title_step == 0.0f) {
        s_title_step = MEMF(STEP_S_G);  /* sub_00015A60 set it just before */
        s_title_ms = MEM32(STEP_MS_G);
    }
    asked = MEMF(g_esp + 4);
    on = !shipped_step();
    if (on && !s_short)
        s_hz = display_rate();
    if (s_hz == 60)
        on = 0;                         /* not faster: as shipped */
    regime(on);
    if (on)
        MEMF(g_esp + 4) = 1000.0f / (float)s_hz;
    title_fn();
    if (on) {
        s_ticks_short = MEM32(TIMER + 0x20);
        s_ticks_title = (uint32_t)((double)s_ticks_short * s_hz * s_title_step + 0.5);
        s_ticks_asked = (uint32_t)((double)s_ticks_short * s_hz * asked / 1000.0 + 0.5);
    }
}

/* sub_001B5A80: a timer's set-up (this in ecx); it copies the step. */
void bo3_sim_timer_init(bo3_title_fn title_fn)
{
    uint32_t t = g_ecx;
    int i;

    title_fn();
    if (!s_on)
        return;
    for (i = 0; i < s_ntimers; i++)
        if (s_timers[i] == t)
            return;
    if (s_ntimers < TIMERS_MAX)
        s_timers[s_ntimers++] = t;
}

/* sub_0015E550: the chase camera's update. It eases by factors to the power
 * of n = round-half-even(K t + 0.5) (CHASE_K = 60, 0x3B1684 = 0.5): 2 at
 * the shipped 1/60 s, 120 easings a second; at 1/N s a step that is 1 a
 * step (stiffer) and 0 past 1/240 s. The count is kept at the shipped rate,
 * dithered: a fraction carried, and K set for the call so that the title's
 * own rounding gives the whole count due. The camera's time is its second
 * argument (esp + 8 on entry). */
void bo3_sim_camera(bo3_title_fn title_fn)
{
    static double carry, rate, k0;
    float t = MEMF(g_esp + 8);
    int n;

    if (!s_short || t <= 0.0f) {
        title_fn();
        return;
    }
    if (rate == 0.0) {
        double ts = bo3_sim_title_step_s();
        k0 = MEMF(CHASE_K);
        rate = rint(k0 * ts + 0.5) / ts;    /* easings a second as shipped */
    }
    carry += rate * t;
    n = (int)carry;
    carry -= n;
    MEMF(CHASE_K) = (float)(((double)n - 0.25) / t);  /* K t + 0.5 = n + 0.25 */
    title_fn();
    MEMF(CHASE_K) = (float)k0;
}

/* Two cameras run once a step with one step's time but move by a constant
 * a call: camera 0/14 (sub_0015C130, the look-back view) turns + 0x58 by
 * 3 degrees a call in its mode 3, and camera 13 (sub_00159A80) eases + 0x38
 * to its target by 0.05 a call. While the short step is in force the
 * constant holds, for the call only, what makes N calls a second do what
 * the shipped calls did. Neither is read by anything they call. */
static void scaled_call(bo3_title_fn title_fn, uint32_t addr, float value)
{
    float keep = MEMF(addr);

    MEMF(addr) = value;
    title_fn();
    MEMF(addr) = keep;
}

static double calls_ratio(void)     /* the shipped calls a second over N */
{
    return 1.0 / bo3_sim_title_step_s() / s_hz;
}

void bo3_sim_camera_0(bo3_title_fn title_fn)
{
    if (s_short && !frozen() && MEMF(CAM0_TURN) == 3.0f)
        scaled_call(title_fn, CAM0_TURN, (float)(3.0 * calls_ratio()));
    else
        title_fn();
}

void bo3_sim_camera_13(bo3_title_fn title_fn)
{
    if (s_short && !frozen() && MEMF(CAM13_EASE) == 0.05f)
        scaled_call(title_fn, CAM13_EASE, (float)(1.0 - pow(0.95, calls_ratio())));
    else
        title_fn();
}

/* For a routine that counts calls rather than time (the race audio
 * manager: its holds, cooldowns, ramps and fades a call), the calls due for
 * `steps` steps so that it keeps the shipped calls a second. */
uint32_t bo3_sim_legacy_calls(uint32_t steps)
{
    static double carry;
    uint32_t k;

    if (!s_short || frozen())
        return steps;
    carry += steps * calls_ratio();
    k = (uint32_t)carry;
    carry -= k;
    return k;
}

/* The HUD's update (sub_000535E0, once a step): ~0 to run it as it comes,
 * else 0 or 1, at most one a step, the shipped calls a second. */
uint32_t bo3_sim_hud_calls(void)
{
    static double carry;

    if (!s_short || frozen())
        return ~0u;
    carry += calls_ratio();
    if (carry >= 1.0) {
        carry -= 1.0;
        return 1;
    }
    return 0;
}

static double host_now(void)
{
    return (double)xbox_HostNowNs() / 1e9;
}

/* sub_001AA100 (the stage loader, ret 4): the stage's set-up a piece a call
 * from Update's state 4 (0x16953), 1 in al once it is done; the frames in
 * between draw the loading screen. game.loading_min: a race's loading
 * screen stays at least that long from the set-up's first call; a "done" that
 * comes sooner is answered 0 and the loader is not called again (it has
 * nothing left to do) until the time is up, then 1. */
void bo3_load_hold(bo3_title_fn title_fn)
{
    static double min_s, t0, last;
    static int active, held;
    double now = host_now();

    /* The loader is called every frame of a set-up: a gap means that one
     * ended (or was left) without its "done", and this is a new one. */
    if (active && now - last > 0.5)
        active = held = 0;
    last = now;
    if (!active) {
        /* Only a race's set-up (its stage has a mode object when the set-up
         * starts) has a loading screen to look at: the front end's set-up,
         * after a race, shows none, and held it looked like a freeze. */
        const char *e = recomp_env(RENV_GAME_LOADING_MIN);
        uint32_t st = MEM32(STAGE);
        int race = st > 0x10000u && st < 0x08000000u && MEM32(st + 0x1B8u);
        min_s = e && strcmp(e, "off") && race ? atof(e) : 0.0;
        if (min_s <= 0.0) {
            title_fn();
            return;
        }
        active = 1;
        held = 0;
        t0 = host_now();
    }
    if (held) {
        int done = host_now() - t0 >= min_s;
        g_esp += 8;                     /* its ret 4 */
        g_eax = (g_eax & ~0xFFu) | (done ? 1u : 0u);
        if (done)
            active = held = 0;
        return;
    }
    title_fn();
    if (g_eax & 0xFFu) {
        if (host_now() - t0 < min_s) {
            held = 1;
            g_eax &= ~0xFFu;
        } else {
            active = 0;
        }
    }
}
