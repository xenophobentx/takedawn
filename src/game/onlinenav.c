/*
 * onlinenav.c - the player's online navigator (BO-352, step 2): Quick Play,
 * Create game and Join with code, driven through the title's own menus.
 *
 * The debug bot onlinebot.c already walks these menus by the screen they are
 * on, but it pushes its presses through the script-pad queue, which is right
 * for a bot and wrong for a player: the player's own presses would mix in.
 * This navigator writes its press straight into the menu player's XINPUT_STATE
 * at the title's pad rewrite point (ctl_pad, controls.c chains it in) and, on
 * the same frame, clears every other button on that pad except B, so the
 * player's own presses are suppressed and B still cancels. When B is pressed,
 * or the flow cannot go on (an unknown screen, the same step three times),
 * it stops and hands the pad back: the player is on whichever title screen it
 * reached, with full control.
 *
 * The screen read and the step rules follow onlinebot.c (the same 600 ms
 * settle, no press into a page change, one press until the screen answers).
 * The pure decisions are at the top so tests/onlinenav can run them without
 * the title's memory.
 */
#include "onlinenav.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ---- the pure decisions (tests/onlinenav) ---- */

int bo3_onlinenav_cancel_latch(int *latched, int pressed)
{
    if (!*latched) return 0;
    if (!pressed) { *latched = 0; return 0; }
    return 1;
}

/* "quick"|"create"|"join" (optionally "join=XXXXX" / "code=XXXXX"). */
int bo3_onlinenav_parse_intent(const char *v, int *intent, char *code, size_t n)
{
    const char *eq;
    if (intent)
        *intent = 0;
    if (code && n)
        code[0] = 0;
    if (!v)
        return 0;
    while (*v == ' ' || *v == ',')
        v++;
    eq = strchr(v, '=');
    if (!strncmp(v, "quick", 5) && (!v[5] || v[5] == ' ' || v[5] == ',' || v[5] == '=')) {
        if (intent)
            *intent = NAV_QUICK;
        return 1;
    }
    if (!strncmp(v, "create", 6) && (!v[6] || v[6] == ' ' || v[6] == ',' || v[6] == '=')) {
        if (intent)
            *intent = NAV_CREATE;
        return 1;
    }
    if (!strncmp(v, "join", 4) && (!v[4] || v[4] == ' ' || v[4] == ',' || v[4] == '=')) {
        if (intent)
            *intent = NAV_JOIN;
        if (eq && code && n) {
            size_t i, k = 0;
            for (i = 0; eq[i + 1] && k + 1 < n; i++)
                if (eq[i + 1] != ' ' && eq[i + 1] != '\n' && eq[i + 1] != '\r')
                    code[k++] = (char)(eq[i + 1] >= 'a' && eq[i + 1] <= 'z'
                                      ? eq[i + 1] - 'a' + 'A' : eq[i + 1]);
            code[k] = 0;
        } else if (code && n)
            code[0] = 0;
        return 1;
    }
    return 0;
}

/* A Quick Play game starts by itself: at once when the lobby is full (6/6
 * ready), else 30 s after the second player is ready. The host is the lobby
 * record's first player; its own ready value counts, and the guests' 1 or 3
 * (a car chosen) count too. */
int bo3_onlinenav_quick_start(const nav_quick_in *in, char *why, size_t n, int *seconds)
{
    int i, guests_ready = 0, ready_total, max = in->maxsize > 0 ? in->maxsize : 6;
    unsigned long long elapsed, left;
    if (seconds)
        *seconds = 0;
    for (i = 1; i < in->np && i < 8; i++)
        if (in->ready[i] == 1 || in->ready[i] == 3)
            guests_ready++;
    ready_total = guests_ready + (in->np > 0 && (in->ready[0] == 1 || in->ready[0] == 3) ? 1 : 0);
    if (ready_total >= max) {
        if (why)
            snprintf(why, n, "%d/%d ready", ready_total, max);
        return NAV_QS_START;
    }
    if (guests_ready < 1) {
        if (why)
            snprintf(why, n, "%d player%s ready, waiting for the second", ready_total, ready_total == 1 ? "" : "s");
        return NAV_QS_WAIT;
    }
    /* the second player is ready: a 30 s countdown (the caller sets
     * second_ready_at when it first sees this) */
    if (!in->second_ready_at) {
        if (seconds)
            *seconds = 30;
        if (why)
            snprintf(why, n, "second player ready, the 30 s countdown begins");
        return NAV_QS_COUNTDOWN;
    }
    elapsed = in->now > in->second_ready_at ? in->now - in->second_ready_at : 0;
    if (elapsed >= 30000ull) {
        if (why)
            snprintf(why, n, "30 s since the second player was ready");
        return NAV_QS_START;
    }
    left = 30000ull - elapsed;
    if (seconds)
        *seconds = (int)((left + 999) / 1000);
    if (why)
        snprintf(why, n, "starting in %d s", seconds ? *seconds : 1);
    return NAV_QS_COUNTDOWN;
}

void bo3_onlinenav_countdown_text(char *out, size_t n, int seconds)
{
    snprintf(out, n, "STARTING IN %d S", seconds > 0 ? seconds : 1);
}

const char bo3_onlinenav_code_chars[NAV_CODE_CHARS + 1] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

int bo3_onlinenav_code_norm(char c)
{
    int i;
    if (c >= 'a' && c <= 'z')
        c = (char)(c - 'a' + 'A');
    for (i = 0; i < NAV_CODE_CHARS; i++)
        if (bo3_onlinenav_code_chars[i] == c)
            return i;
    return -1;
}

void bo3_onlinenav_code_step(char *code, int pos, int dir)
{
    int i = bo3_onlinenav_code_norm(code[pos]);   /* -1: blank, before 'A' */
    i += dir;
    if (i < 0)
        i = NAV_CODE_CHARS - 1;                   /* wrap to '9' */
    if (i >= NAV_CODE_CHARS)
        i = 0;                                    /* wrap to 'A' */
    code[pos] = bo3_onlinenav_code_chars[i];
}

int bo3_onlinenav_code_complete(const char *code)
{
    int i;
    for (i = 0; i < 5; i++)
        if (bo3_onlinenav_code_norm(code[i]) < 0)
            return 0;
    return 1;
}

#ifndef ONLINENAV_PURE_ONLY

/* ---- the glue ---- */

#include "platform/recomp_env.h"
#include "recomp_types.h"
#include "xnet_hle.h"
#include "online.h"
#include "race_module.h"
#include "onlinescreens.h"
#include "onlinebot.h"
#include "quickplay.h"
#include "stay.h"
#include "region.h"
#include "xinput_xbox.h"        /* XBOX_GAMEPAD, the button flags */

/* the title's front-end state, as onlinebot.c reads it (BO-352: BO3_VA, all discs) */
#define NAV_FLOW       BO3_VA(0x00567170u, 0x00567490u, 0x00567530u)
#define NAV_PAGE       BO3_VA(0x00557A70u, 0x00557D90u, 0x00557E30u)
#define NAV_CURSOR     BO3_VA(0x00550A98u, 0x00550DB8u, 0x00550E58u)
#define NAV_MODE_VALUE BO3_VA(0x00564FE4u, 0x00565304u, 0x005653A4u)
#define NAV_MBOX       BO3_VA(0x00550CB0u, 0x00550FD0u, 0x00551070u)
#define NAV_MBOX_OPEN  (NAV_MBOX + 0x110u)
#define NAV_MBOX_CUR   (NAV_MBOX + 0x138u)
#define NAV_MBOX_KIND  (NAV_MBOX + 0x184u)
#define NAV_PEERS      BO3_VA(0x00572988u, 0x00572C98u, 0x00572D38u)
#define NAV_NPEERS     BO3_VA(0x005729A8u, 0x00572CB8u, 0x00572D58u)

#define NAV_SETTLE_MS     600u        /* a screen holds this long before a press */
#define NAV_UNKNOWN_MS    20000u

/* the press the pad hook writes */
#define NAV_BTN_NONE  0
#define NAV_BTN_A     'a'
#define NAV_BTN_B     'b'
#define NAV_BTN_UP    'u'
#define NAV_BTN_DOWN  'd'
#define NAV_BTN_LEFT  'l'
#define NAV_BTN_RIGHT 'r'
#define NAV_BTN_START 's'

#define NAV_LOG(...) do { fprintf(stderr, "[ONLINENAV] " __VA_ARGS__); fputc('\n', stderr); fflush(stderr); } while (0)

static int s_intent;                 /* NAV_QUICK / NAV_CREATE / NAV_JOIN */
static char s_code[6];
static int s_running, s_stopped;
static int s_cancel_latched;
static int s_hosting;                /* Quick Play found no game: host one */
static unsigned long long s_now;
static unsigned long long s_next_ms;      /* no press before this */
static char s_step[48], s_key[96];
static unsigned long long s_key_since;
static int s_attempts;
static unsigned long long s_attempt_wait;
static int s_commit_sc;                   /* an A, B or Start on this screen */
static unsigned long long s_sc_since;
static int s_last_sc = -1;
static unsigned long long s_busy_since;
static unsigned long long s_host_jitter_at;   /* Quick Play hosting: wait a little, then one more gsea */
static unsigned long long s_second_ready_at;  /* the countdown's start */
static unsigned long long s_start_said;

/* the pad rewrite the navigator wants this moment: the button and how long */
static int s_press_btn = NAV_BTN_NONE;
static unsigned long long s_press_until;

static unsigned long long nav_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000ull + (unsigned long long)ts.tv_nsec / 1000000ull;
}

static int ok_va(uint32_t va) { return va >= 0x10000u && va < 0x04000000u; }

/* what one flip shows (onlinebot.c's ob_view) */
typedef struct {
    uint32_t flow, step, page, cursor;
    int sc, popup, busy;
    uint32_t pkind, pcur;
    int lobby;
    uint8_t lstate, linked, alive;
    int race;
    uint32_t np, links[8];
    xnet_hle_lobby_view lv;
} nav_view;

static void nav_read(nav_view *v)
{
    uint32_t cur = MEM32(NAV_FLOW + 4), mod, k;
    memset(v, 0, sizeof *v);
    v->flow = ok_va(cur) ? cur - NAV_FLOW : 0;
    v->step = ok_va(cur) ? MEM32(cur + 0x18) : 0;
    v->page = MEM32(NAV_PAGE);
    v->cursor = MEM32(NAV_CURSOR);
    v->popup = MEM32(NAV_MBOX_OPEN) != 0;
    v->busy = (MEM32(NAV_MBOX + 0xF8u) & 0x10000u) != 0;
    v->pkind = MEM32(NAV_MBOX_KIND);
    v->pcur = MEM32(NAV_MBOX_CUR);
    v->lobby = bo3_online_lobby_now(&v->lstate, &v->linked, &v->alive);
    mod = MEM32(BO3_MODULE_CUR);
    v->race = ok_va(mod) && bo3_race_module_vt(MEM32(mod));
    v->np = MEM32(NAV_NPEERS);
    for (k = 0; k < 8; k++) {
        uint32_t P = k < v->np ? MEM32(NAV_PEERS + 4 * k) : 0;
        v->links[k] = ok_va(P) ? MEM32(P + 0x4F0) : 0xFF;
    }
    xnet_hle_lobby_view_get(&v->lv);
    v->sc = bo3_online_screen_for(v->flow, v->page);
    if (v->sc == OB_SC_LOBBY && !v->lobby)
        v->sc = OB_SC_LOADING;
    if (v->race && v->sc != OB_SC_RACE_END && v->sc != OB_SC_RESULTS)
        v->sc = OB_SC_RACE;
}

/* hold the press for `hold` ms, then wait `wait` for the screen to answer */
static void nav_press(int btn, unsigned hold, unsigned wait, const char *why)
{
    s_press_btn = btn;
    s_press_until = s_now + hold;
    NAV_LOG("press %c on %s: %s", btn, bo3_onlinebot_screen_name(s_last_sc >= 0 ? s_last_sc : OB_SC_UNKNOWN), why);
    s_next_ms = s_now + hold + 350u;
    s_attempt_wait = s_now + hold + wait;
    s_commit_sc = (btn == NAV_BTN_A || btn == NAV_BTN_START) ? s_last_sc : -1;
}

static void nav_stop(const char *why)
{
    if (s_stopped)
        return;
    s_stopped = 1;
    s_press_btn = NAV_BTN_NONE;
    NAV_LOG("stopped: %s", why);
    xnet_hle_message("QUICK PLAY STOPPED - CONTINUE BY HAND");
}

static int nav_step(const char *step, const char *key)
{
    if (strcmp(step, s_step) || strcmp(key, s_key)) {
        snprintf(s_step, sizeof s_step, "%s", step);
        snprintf(s_key, sizeof s_key, "%s", key);
        s_attempts = 0;
    }
    if (s_attempts && s_now < s_attempt_wait)
        return 0;
    if (++s_attempts > 3) {
        nav_stop("the same step three times without the screen changing");
        return 0;
    }
    return 1;
}

/* the focused list's cursor to line `want`; 1 when it is there */
static int nav_cursor_to(uint32_t want, const char *what, nav_view *v)
{
    if (v->cursor == want)
        return 1;
    if (v->cursor == 0xFFFFFFFFu)
        return 0;
    if (nav_step("cursor", what))
        nav_press(v->cursor < want ? NAV_BTN_DOWN : NAV_BTN_UP, 150, 1500, what);
    return 0;
}

/* the Quick Play host's own start, from the view (decision 3) */
static void nav_host_start(nav_view *v)
{
    nav_quick_in q;
    char why[96];
    int seconds = 0, r;
    memset(&q, 0, sizeof q);
    q.np = v->lv.np;
    q.me = xnet_hle_gamertag();
    q.maxsize = 6;
    for (int i = 0; i < 8 && i < v->lv.np; i++)
        q.ready[i] = v->lv.ready[i];
    q.now = s_now;
    q.second_ready_at = s_second_ready_at;
    r = bo3_onlinenav_quick_start(&q, why, sizeof why, &seconds);
    if (r == NAV_QS_START) {
        if (nav_step("quick start", why))
            nav_press(NAV_BTN_START, 400, 8000, "start the Quick Play game");
        return;
    }
    if (r == NAV_QS_COUNTDOWN) {
        if (!s_second_ready_at)
            s_second_ready_at = s_now;
        if (s_now - s_start_said > 1000) {
            char t[40];
            s_start_said = s_now;
            bo3_onlinenav_countdown_text(t, sizeof t, seconds);
            NAV_LOG("%s (%s)", t, why);
        }
        return;
    }
    /* WAIT: the host is alone, waiting for the second player */
    s_second_ready_at = 0;
}

static void nav_act(nav_view *v)
{
    /* a message box first, answered by its kind */
    if (v->popup && !(v->sc == OB_SC_LOBBY && v->lstate == 0x09)) {
        if (v->pkind == 7) {                    /* YES / NO: NO (stay) */
            uint32_t want = 1u;
            if (v->pcur != want) {
                if (nav_step("popup cursor", "no"))
                    nav_press(v->pcur > want ? NAV_BTN_UP : NAV_BTN_DOWN, 150, 1500, "to NO");
                return;
            }
            if (nav_step("popup answer", "no"))
                nav_press(NAV_BTN_A, 300, 4000, "NO");
            return;
        }
        if (nav_step("popup ok", "ok"))
            nav_press(NAV_BTN_A, 300, 4000, "OK");
        return;
    }

    /* never press on a lobby in a state the player's B or a menu may answer */
    if (v->sc == OB_SC_LOBBY && (v->lstate == 0x03 || v->lstate == 0x09 || v->lstate == 0x0B))
        return;

    switch (v->sc) {
    case OB_SC_LOADING:
    case OB_SC_SIGNIN:
    case OB_SC_CREATING:
    case OB_SC_RANKINGS:
    case OB_SC_AUTOSAVE:
    case OB_SC_RACE_END:
    case OB_SC_BOOT:
    case OB_SC_RACE:
        return;                                 /* waits, left to the title */
    case OB_SC_GAMERTAG:
        if (nav_step("gamertag", "sign in"))
            nav_press(NAV_BTN_A, 300, 6000, "sign in");
        return;
    case OB_SC_MAIN:
        if (s_intent == NAV_QUICK) {
            if (nav_cursor_to(3, "XBOX LIVE", v) && nav_step("main: xbox live", "go"))
                nav_press(NAV_BTN_A, 300, 5000, "XBOX LIVE");
        }
        return;
    case OB_SC_LIVE:
        if (s_intent == NAV_CREATE || s_hosting) {
            if (nav_cursor_to(2, "LOBBY SELECT", v) && nav_step("live: lobby select", "go"))
                nav_press(NAV_BTN_A, 300, 5000, "LOBBY SELECT");
        } else {                                /* QUICK or JOIN */
            if (nav_cursor_to(0, "QUICK MATCH", v) && nav_step("live: quick match", "go"))
                nav_press(NAV_BTN_A, 300, 6000, "QUICK MATCH");
        }
        return;
    case OB_SC_LOBBYSEL:
        if (s_intent == NAV_QUICK || s_intent == NAV_JOIN) {
            /* after a race (state +0x630) A goes on; a guest that found no
             * game backs out to Quick Match instead */
            if (v->flow == 0x630) {
                if (nav_step("lobby select: room", "go"))
                    nav_press(NAV_BTN_A, 300, 6000, "into the room");
            } else if (nav_step("lobby select: back", "back")) {
                nav_press(NAV_BTN_A, 300, 4000, "into the room (hosting)");
            }
        } else {
            if (nav_step("lobby select: room", "go"))
                nav_press(NAV_BTN_A, 300, 6000, "into the room");
        }
        return;
    case OB_SC_ROOM:
        if (s_intent == NAV_QUICK || s_intent == NAV_JOIN) {
            if (nav_cursor_to(0, "CREATE GAME", v) && nav_step("room: create game", "go"))
                nav_press(NAV_BTN_A, 300, 5000, "CREATE GAME");
        } else {
            if (nav_cursor_to(0, "CREATE GAME", v) && nav_step("room: create game", "go"))
                nav_press(NAV_BTN_A, 300, 5000, "CREATE GAME");
        }
        return;
    case OB_SC_CREATE:
        if (nav_step("create: continue", "go"))
            nav_press(NAV_BTN_A, 300, 5000, "Continue (name, limit as they are)");
        return;
    case OB_SC_MODE:
        if (s_intent == NAV_CREATE) {
            /* CREATE GAME stops here: the player picks mode, track and laps */
            nav_stop("Create game: on the mode page, the player picks mode, track and laps");
        } else {
            /* Quick Play hosting: take the defaults (Single Race, first track) */
            if (nav_step("mode: continue", "go"))
                nav_press(NAV_BTN_A, 300, 8000, "Continue: create the game");
        }
        return;
    case OB_SC_QUICK:
        /* Quick Match is searching. A "no games" answer means the host path
         * (decision 8): nobody is online, so host a game and wait. */
        if (v->lv.search_count == 0 && v->lv.got[XNET_LV_GSEA] > 0 && s_intent == NAV_QUICK) {
            if (!s_host_jitter_at)
                s_host_jitter_at = s_now + (unsigned)(rand() % 3000);
            if (s_now >= s_host_jitter_at && nav_step("quick: host", "go")) {
                s_hosting = 1;
                NAV_LOG("Quick Match found no game: backing out to host one");
                nav_press(NAV_BTN_B, 300, 6000, "no games found: back, then LOBBY SELECT");
            }
        }
        /* JOIN with a code that finds nothing: the player cancels with B */
        return;
    case OB_SC_CAR:
        if (nav_step("car: select", "go"))
            nav_press(NAV_BTN_A, 400, 10000, "this car");
        return;
    case OB_SC_LOBBY:
        if (s_intent == NAV_CREATE)
            return;                             /* the player runs their own game */
        nav_host_start(v);
        return;
    case OB_SC_RESULTS:
    case OB_SC_POINTS:
        if (nav_step("results: continue", "go"))
            nav_press(NAV_BTN_A, 300, 6000, "Continue");
        return;
    default:
        if (s_now - s_key_since > NAV_UNKNOWN_MS) {
            nav_stop("unknown screen");
            s_key_since = s_now;
        }
        return;
    }
}

int bo3_onlinenav_active(void) { return s_running && !s_stopped; }

int bo3_onlinenav_begin(int intent, const char *code)
{
    /* one run at a time; a run that stopped (the player's B, an unknown
     * screen, ...) leaves s_running set for the flip and pad hooks' sake, so
     * it must not block the next CREATE GAME / JOIN WITH CODE / QUICK PLAY */
    if (s_running && !s_stopped)
        return 0;
    if (!xnet_hle_on())
        return 0;
    s_intent = intent;
    s_code[0] = 0;
    if (code && *code)
        snprintf(s_code, sizeof s_code, "%s", code);
    s_running = 1;
    s_stopped = 0;
    s_hosting = 0;
    s_next_ms = 0;
    s_step[0] = s_key[0] = 0;
    s_attempts = 0;
    s_commit_sc = -1;
    s_last_sc = -1;
    s_busy_since = 0;
    s_host_jitter_at = 0;
    s_second_ready_at = 0;
    s_start_said = 0;
    s_press_btn = NAV_BTN_NONE;
    s_press_until = 0;
    if (s_intent == NAV_JOIN && s_code[0])
        xnet_hle_set_code(s_code);
    NAV_LOG("on: %s%s%s", s_intent == NAV_QUICK ? "quick play" : s_intent == NAV_CREATE ? "create game" : "join",
            s_code[0] ? " code " : "", s_code);
    return 1;
}

void bo3_onlinenav_flip(uint32_t flip)
{
    nav_view v;
    char key[96];
    bo3_quickplay_flip(flip);           /* nothing unless QUICK PLAY armed it */
    bo3_stay_flip();                    /* after a raced online game: back to its lobby */
    if (!s_running || !g_xbox_mem_offset)
        return;
    s_now = nav_ms();
    nav_read(&v);
    snprintf(key, sizeof key, "%d.%X.%u.%08X.%u.%d.%u.%u.%d.%02X.%d", v.sc, v.flow, v.step, v.page, v.cursor,
             v.popup, v.popup ? v.pkind : 0u, v.popup ? v.pcur : 0u, v.lobby, v.lstate, v.busy);
    if (strcmp(key, s_key)) {
        snprintf(s_key, sizeof s_key, "%s", key);
        s_key_since = s_now;
    }
    if (v.sc != s_last_sc) {
        if (s_last_sc >= 0)
            NAV_LOG("screen %s -> %s", bo3_onlinebot_screen_name(s_last_sc), bo3_onlinebot_screen_name(v.sc));
        s_last_sc = v.sc;
        s_sc_since = s_now;
    }
    if (s_stopped)
        return;
    if (s_now < s_next_ms || s_now - s_key_since < NAV_SETTLE_MS)
        return;
    if (v.busy && !v.popup) {
        if (!s_busy_since)
            s_busy_since = s_now;
        if (s_now - s_busy_since < 20000)
            return;
    } else {
        s_busy_since = 0;
    }
    if (s_commit_sc >= 0) {
        if (v.sc == s_commit_sc && s_now < s_attempt_wait)
            return;
        s_commit_sc = -1;
    }
    nav_act(&v);
}

/* The pad rewrite point (controls.c chains this in at ctl_pad): while the
 * navigator runs, write its press into the menu player's pad (the title's
 * port 2, the first host pad) and clear every other button, so the player's
 * own presses do not mix in. B is special: when the navigator itself presses
 * B (a normal step, like backing out of Quick Match to host), that press
 * wins; otherwise the player's B cancels the navigator. On the starting
 * online page it is swallowed through release so the title stays there;
 * elsewhere the title sees B and uses its own back path. */
void bo3_onlinenav_pad(uint32_t port, struct XBOX_GAMEPAD *g)
{
    int btn;
    if (port != 2u || !g) return;
    if (bo3_onlinenav_cancel_latch(&s_cancel_latched,
            g->bAnalogButtons[XBOX_BUTTON_B] >= XBOX_ANALOG_BUTTON_THRESHOLD)) {
        g->bAnalogButtons[XBOX_BUTTON_B] = 0;
        return;
    }
    if (!s_running || s_stopped) return;
    btn = (s_now < s_press_until) ? s_press_btn : NAV_BTN_NONE;
    if (btn != NAV_BTN_B && g->bAnalogButtons[XBOX_BUTTON_B] >= XBOX_ANALOG_BUTTON_THRESHOLD) {
        if (s_intent == NAV_QUICK && MEM32(NAV_PAGE) ==
            BO3_VA(0x0055DF50u, 0x0055E270u, 0x0055E310u)) {
            s_cancel_latched = 1;
            g->bAnalogButtons[XBOX_BUTTON_B] = 0;
        }
        nav_stop("B: the player cancelled");
        return;                             /* B is passed through only off the starting page */
    }
    memset(g, 0, sizeof *g);
    switch (btn) {
    case NAV_BTN_A:     g->bAnalogButtons[XBOX_BUTTON_A] = 255; break;
    case NAV_BTN_B:     g->bAnalogButtons[XBOX_BUTTON_B] = 255; break;
    case NAV_BTN_UP:    g->wButtons |= XBOX_GAMEPAD_DPAD_UP; break;
    case NAV_BTN_DOWN:  g->wButtons |= XBOX_GAMEPAD_DPAD_DOWN; break;
    case NAV_BTN_LEFT:  g->wButtons |= XBOX_GAMEPAD_DPAD_LEFT; break;
    case NAV_BTN_RIGHT: g->wButtons |= XBOX_GAMEPAD_DPAD_RIGHT; break;
    case NAV_BTN_START: g->wButtons |= XBOX_GAMEPAD_START; break;
    }
}

void bo3_onlinenav_start(void)
{
    const char *v = getenv("TAKEDAWN_ONLINENAV");
    int intent = 0;
    char code[6] = "";
    if (!v || !*v || !strcmp(v, "0"))
        return;
    bo3_onlinenav_parse_intent(v, &intent, code, sizeof code);
    if (!intent) {
        NAV_LOG("TAKEDAWN_ONLINENAV=%s: no intent", v);
        return;
    }
    bo3_onlinenav_begin(intent, code[0] ? code : NULL);
}

#endif /* ONLINENAV_PURE_ONLY */
