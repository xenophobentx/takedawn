/*
 * onlinebot.c - RECOMP_DEBUG=onlinebot=host|guest: an online test navigator
 * that plays the title's Xbox Live menus by the screen it is on (BO-134).
 * A test driver for online soaks; debug only, off unless set, not in the
 * overlay. With the switch unset the only cost is one test a flip.
 *
 * Why: soak scripts that sent pad presses with sleeps landed on the wrong
 * screens (an A that toggled a guest's car, a B that opened "leave the
 * game?", the title's idle sign-out nobody saw). This reads the screen from
 * the title's own state every flip and presses only what that screen needs,
 * once it is stable.
 *
 * What a screen is (NTSC-U addresses; tools/online/bot/README.md has the table):
 *
 *   flow      the front end's state machine 0x567170 (modsmenu.c): +4 the
 *             current state, a member of the machine at a fixed offset; its
 *             +0x18 a step some states keep. The offset names the state.
 *   page      the front end's current page object [0x557A70] (each page is a
 *             static object; every page change stores it after
 *             sub_00063670). Several pages share a state: sign-in, update
 *             rankings and lobby select all run in state +0x630.
 *   cursor    the focused list's line, [0x550A98] (the list widget 0x5509F4
 *             + 0xA4; every list page moves it). The mode page's GAME MODE
 *             value is [0x564FE4]: the title's mode code (0 Single Race,
 *             1 Road Rage, 2 Team Crush, 3 Double Impact, 4 Party Crash; the
 *             third PARAMS value of gcre).
 *   popup     the message box widget 0x550CB0: open while [+0x110] holds a
 *             pointer; [+0x184] its kind (2 a notice with one button, 7 a
 *             YES / NO question), [+0x138] its cursor (0 YES, 1 NO).
 *   lobby     the game lobby screen object (online.c): its state byte +0x13AC
 *             (0x01 plain, 0x03 a player's menu, 0x09 "leave the game?",
 *             0x0A leaving, 0x0B starting, 0x0C-0x0E its own steps, 0x11 the
 *             start lock), +0x13A4 every peer linked, +0x13A5 alive.
 *   lobby view  what the copy sent to and got from the lobby server
 *             (xnet_hle_lobby_view): searches and their counts, the game's
 *             players and their ready values.
 *   race      the current module is a race module (race_module.h).
 *
 * onlinescreens.c's shared table maps (flow, page) to a screen for all three
 * discs (BO-352); ob_act() says what each one gets.
 * Nothing is pressed until the screen's key (screen, cursor, popup, lobby
 * state) has held for OB_SETTLE_MS; then one press, and the next only after
 * the screen answered or the press's own wait ran out. The same step on an
 * unchanged screen three times, an unknown screen for OB_UNKNOWN_MS or a wait
 * past its limit is "stuck": "[ONLINEBOT] stuck: screen=... step=...", a frame
 * (RECOMP_FB_DUMP_AT=@file), then one B to back out (every dialog B can open
 * is answered here); after OB_MAX_RECOVER recoveries without progress it
 * stops. Screens before the main menu (films, title, profile) are left to the
 * scene script that starts the copy.
 *
 * Settings (environment):
 *   RECOMP_DEBUG=onlinebot=host|guest|probe   (probe: screens and frames only)
 *   TAKEDAWN_ONLINEBOT_MODES     host: modes in turn, codes or names (race,
 *                                roadrage, partycrash), default "0"
 *   TAKEDAWN_ONLINEBOT_GUESTS    host: guests to wait for (default 1)
 *   TAKEDAWN_ONLINEBOT_JOINWAIT  host: seconds to wait for them before starting
 *                                with those there (at least one; default 300)
 *   TAKEDAWN_ONLINEBOT_GAMES     games to play, then stay put (0: no end)
 *   TAKEDAWN_ONLINEBOT_HOST      guest: play only in this host's game (Quick
 *                                Match finds a game; another host's is left)
 *   TAKEDAWN_ONLINEBOT_PLAN      the TAKEDAWN ONLINE line for each game, in
 *                                turn: quick, create, code, browse, wait (default
 *                                host: create, guest: code). A search that
 *                                found nothing goes on to the next one. wait:
 *                                the QUICK PLAY line, pressed only once a race
 *                                is under way (BO-365); then the Quick Match
 *                                page waits for it and joins the game made
 *                                again. A code whose game is racing waits too.
 *   TAKEDAWN_ONLINEBOT_CODE      the five-letter code: CREATE GAME makes this
 *                                one, JOIN WITH CODE types it (default TKDW3).
 *                                Valid codes only: five letters of the entry's
 *                                alphabet (codeentry.h: no I, O, 0, 1); a bad
 *                                one is refused with a log line and the default
 *                                stays. One code per pair or lobby, never shared.
 *   TAKEDAWN_ONLINEBOT_WAITFOR   plan wait: seconds to wait on the online page for a
 *                                race under way (the room's TKRACING) before
 *                                QUICK PLAY as it is (default 600)
 *   TAKEDAWN_ONLINEBOT_START_DELAY  seconds after the bot started in which it does
 *                                nothing on the main menu and the online page (a
 *                                third copy that must arrive mid-race)
 *   TAKEDAWN_ONLINEBOT_CANCEL_AFTER  seconds into a wait for a race (still
 *                                racing) after which the bot presses B once
 *   TAKEDAWN_ONLINEBOT_QUIT_RESULTS  1: the bot quits on the race's results
 *                                (B, YES) and stays out: the host that does not
 *                                make the game again (BO-365 run C; not yet
 *                                proven on a real copy)
 *   TAKEDAWN_ONLINEBOT_RESULTS_HOLD  seconds the bot stays on the race's
 *                                results before Continue To Lobby (a slow
 *                                player; stay together's wait, BO-365 item 3),
 *                                or before QUIT_RESULTS's Quit
 *   TAKEDAWN_ONLINEBOT_RESULTS_CANCEL  1: CANCEL on the results' "WAITING
 *                                FOR OTHER PLAYERS" box (the player goes on alone)
 *   TAKEDAWN_ONLINEBOT_POPUP_HOLD  seconds a message box after a race (the
 *                                autosave's question, the sign-in's) stays
 *                                up before the bot answers it
 *   TAKEDAWN_ONLINEBOT_VOTE      none: no vote in the track vote, its clock
 *                                runs out (default: a random row)
 *   TAKEDAWN_ONLINEBOT_PICKS     plan browse (BO-372): the games to pick on the
 *                                BROWSE GAMES list, in turn, NAME:STATE with
 *                                STATE open, racing, full or any: the bot waits
 *                                on the list (it refreshes) until NAME is listed
 *                                in that state, moves the cursor there and
 *                                presses A (a full one: the list stays and the
 *                                next pick is taken), separated by , or |;
 *                                "back" presses B on the list. Up
 *                                to TAKEDAWN_ONLINEBOT_PICK_WAIT s (600) a pick.
 *   TAKEDAWN_ONLINEBOT_PAGE_HOLD  s the bot leaves the online page as it is
 *                                each time it comes back to it (tests: the
 *                                page's message read in a frame; default 0)
 *
 * Who starts a game: the host of a CREATE GAME game presses Start at the start
 * gate. A game QUICK PLAY made starts by itself: the track vote, or the 30 s
 * countdown where a mode has no vote (quickplay.c), so its host never presses.
 * Who hosts is read in the lobby: QUICK PLAY makes a game when it finds none.
 * Copies leaving one race search together, so QUICK PLAY waits a random time
 * first, and a host alone in such a game leaves after 45 to 90 s and searches
 * again.
 *
 * The log: "screen A -> B", "press X on S: why", "game N ...: started,
 * result, done", "race N end: ..., stayed|left", "stuck: ...", "stopped", and for a wait for a race (BO-365)
 * "waiting for game N: racing|lobby|open|full|gone (P players, S s)" on every
 * change and "wait ended: joined|gone|full|timeout|cancelled|other (S s)".
 * A soak's pass/fail reads one "race N end" per race per copy (a stay keeps the
 * copy in its lobby, so "game N done" comes only when it leaves) and no "stuck".
 * TAKEDAWN_ONLINEBOT_RACES=N: done after N races in all; _LEAVE_AFTER=K: leave
 * a lobby after K races in it.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#endif

#include "onlinebot.h"
#include "onlinescreens.h"
#include "codeentry.h"
#ifndef ONLINEBOT_TABLE_ONLY
#include "platform/recomp_env.h"
#include "recomp_types.h"
#include "xnet_hle.h"
#include "usb/usb_gamepad.h"
#include "race_module.h"
#include "online.h"
#include "quickplay.h"
#include "racewait.h"
#include "vote.h"
#include "xboxlive.h"
#endif

#define OB_FLOW        0x00567170u   /* the front end's state machine */
#define OB_PAGE        0x00557A70u   /* the current page */
#define OB_CURSOR      0x00550A98u   /* the focused list's line */
#define OB_MODE_VALUE  0x00564FE4u   /* the mode page's GAME MODE value */
#define OB_CREATE_REQ  0x0055DF90u   /* the create request (quickplay.c QP_BLOCK) */
#define OB_MBOX        0x00550CB0u   /* the message box widget */
#define OB_MBOX_OPEN   (OB_MBOX + 0x110u)
#define OB_MBOX_CUR    (OB_MBOX + 0x138u)
#define OB_MBOX_KIND   (OB_MBOX + 0x184u)
#define OB_PEERS       0x00572988u   /* the title's peer table */
#define OB_NPEERS      0x005729A8u
#define OB_CAR1        0x0073A1D0u   /* player 1's car (autopilot.c) */
#define OB_RACERS      0x0073A19Cu

#define OB_SETTLE_MS     600u        /* a screen holds this long before a press */
#define OB_UNKNOWN_MS    20000u
#define OB_KEEPALIVE_MS  40000u      /* the title signs idle players out */
#define OB_MAX_RECOVER   5
#define OB_QUICK_DELAY_MS 25000u     /* QUICK PLAY: up to this before the search */
#define OB_ALONE_MIN_MS   45000u     /* a QUICK PLAY host alone: this, up to twice this */

static const char *const k_sc_name[OB_SC_N] = {
    "unknown", "loading", "boot", "main-menu", "gamertag", "signing-in", "xbox-live", "lobby-select",
    "update-rankings", "room", "create-game", "mode", "creating", "quick-match", "car", "game-lobby", "race",
    "race-end", "results", "autosave", "points"
};

/* (flow state offset, page) -> screen lives in onlinescreens.c (BO-352). */

const char *bo3_onlinebot_screen_name(int sc)
{
    return sc >= 0 && sc < OB_SC_N ? k_sc_name[sc] : "?";
}

int bo3_onlinebot_parse_modes(const char *v, int *out, int max)
{
    int n = 0;
    while (v && *v && n < max) {
        while (*v == ',' || *v == ' ' || *v == '/')
            v++;
        if (!*v)
            break;
        if (*v >= '0' && *v <= '9')
            out[n++] = atoi(v);
        else if (!strncmp(v, "race", 4) || !strncmp(v, "single", 6))
            out[n++] = 0;
        else if (!strncmp(v, "roadrage", 8) || !strncmp(v, "rr", 2))
            out[n++] = 1;
        else if (!strncmp(v, "partycrash", 10) || !strncmp(v, "pc", 2))
            out[n++] = 4;
        while (*v && *v != ',' && *v != '/' && *v != ' ')
            v++;
    }
    return n;
}

static const char *const k_plan_name[OB_PLAN_N] = { "quick", "create", "code", "browse", "wait" };

int bo3_onlinebot_plan_line(int plan)
{
    /* the page's own order (xboxlive.h XL_QUICK .. XL_BROWSE) */
    static const int line[OB_PLAN_N] = { 0, 1, 2, 3, 0 };
    return plan >= 0 && plan < OB_PLAN_N ? line[plan] : 0;
}

const char *bo3_onlinebot_wait_state_name(int state)
{
    static const char *const k[] = { "none", "racing", "lobby", "open", "full", "gone" };
    return state >= 0 && state <= 5 ? k[state] : "?";
}

int bo3_onlinebot_wait_settled(int next_sc)
{
    return next_sc != OB_SC_UNKNOWN && next_sc != OB_SC_LOADING && next_sc != OB_SC_BOOT && next_sc != OB_SC_SIGNIN &&
           next_sc != OB_SC_QUICK;
}

int bo3_onlinebot_wait_result(int last_state, unsigned long long waited_ms, int next_sc, int cancelled)
{
    if (cancelled)
        return OB_WAIT_CANCELLED;
    if (next_sc == OB_SC_CAR || next_sc == OB_SC_LOBBY)
        return OB_WAIT_JOINED;
    if (last_state == 5)
        return OB_WAIT_GONE;
    if (last_state == 4)
        return OB_WAIT_FULL;
    if (waited_ms >= OB_WAIT_MAX_MS)
        return OB_WAIT_TIMEOUT;
    return OB_WAIT_OTHER;
}

const char *bo3_onlinebot_wait_result_name(int result)
{
    static const char *const k[] = { "joined", "gone", "full", "timeout", "cancelled", "other" };
    return result >= 0 && result < OB_WAIT_OTHER + 1 ? k[result] : "?";
}

unsigned long long bo3_onlinebot_quick_limit_ms(int waiting)
{
    return waiting ? OB_WAIT_STUCK_MS : OB_STUCK_QUICK_MS;
}

/* TAKEDAWN_ONLINEBOT_RACES=N: N races ended, however the last one ended
 * (back in the lobby, or out of the game) */
int bo3_onlinebot_races_done(int max_races, int race_n)
{
    return max_races > 0 && race_n >= max_races;
}

/* a step's attempts start afresh when a race ends: the same step on the same
 * screen a race later (Continue To Lobby on the next race's results) is a new
 * visit, not a fourth try at the last one */
int bo3_onlinebot_step_fresh(int from_sc, int to_sc)
{
    return from_sc == OB_SC_RACE && to_sc != OB_SC_RACE;
}

const char *bo3_onlinebot_plan_name(int plan)
{
    return plan >= 0 && plan < OB_PLAN_N ? k_plan_name[plan] : "?";
}

int bo3_onlinebot_parse_plans(const char *v, int *out, int max)
{
    int n = 0;
    while (v && *v && n < max) {
        int p;
        size_t len;
        while (*v == ',' || *v == ' ' || *v == '/')
            v++;
        len = strcspn(v, ",/ ");
        for (p = 0; p < OB_PLAN_N && len; p++)
            if (strlen(k_plan_name[p]) == len && !strncmp(v, k_plan_name[p], len))
                out[n++] = p;
        v += len;
    }
    return n;
}

int bo3_onlinebot_entry_step(const char *want, const char *typed, int row, int col)
{
    size_t n = strlen(typed), w = strlen(want);
    const char *c;
    int i, d;
    if (n > w || strncmp(want, typed, n))
        return OB_CE_DELETE;
    if (n == w)
        return row == CE_ROW_CODE ? OB_CE_JOIN : OB_CE_UP;
    c = strchr(bo3_ce_alphabet, want[n]);
    if (!c)
        return OB_CE_DELETE;
    i = (int)(c - bo3_ce_alphabet);
    if (row != CE_ROW_GRID + i / CE_COLS)
        return row < CE_ROW_GRID + i / CE_COLS ? OB_CE_DOWN : OB_CE_UP;
    d = (i % CE_COLS - col + CE_COLS) % CE_COLS;
    if (!d)
        return OB_CE_TYPE;
    return d <= CE_COLS / 2 ? OB_CE_RIGHT : OB_CE_LEFT;
}

/* What the title's Start checks (0xA1560: online.c, the [ONLINE] line): the
 * lobby screen in its plain state 0x01, every peer linked (+0x13A4), the
 * screen alive (+0x13A5), each peer's link at 3; and a car for every guest:
 * the third USERPARAMS value 1 or 3 (both let Start through; 0, no car yet,
 * holds alive at 0). want: guests to wait for. */
int bo3_onlinebot_gate(const ob_gate_in *g, char *why, size_t n)
{
    int i, guests = 0, cars = 0;
    uint32_t k;
    if (g->lstate != 0x01) { snprintf(why, n, "lobby state 0x%02X", g->lstate); return 0; }
    for (i = 0; i < g->players && i < 8; i++) {
        if (g->me && !strcmp(g->name[i], g->me))
            continue;
        guests++;
        if (g->ready[i] == 1 || g->ready[i] == 3)
            cars++;
    }
    if (guests < g->want) { snprintf(why, n, "%d of %d guests", guests, g->want); return 0; }
    if (cars < guests) { snprintf(why, n, "%d of %d guests have a car", cars, guests); return 0; }
    if (g->np < (uint32_t)guests + 1) { snprintf(why, n, "%u in the title's table, %d guests", g->np, guests); return 0; }
    for (k = 0; k < g->np && k < 8; k++)
        if (g->links[k] != 0xFF && g->links[k] != 3) {
            snprintf(why, n, "peer %u link %u", k, g->links[k]);
            return 0;
        }
    if (!g->linked) { snprintf(why, n, "not every peer linked"); return 0; }
    if (!g->alive) { snprintf(why, n, "the screen is not alive"); return 0; }
    snprintf(why, n, "%d guests with cars, every link up", guests);
    return 1;
}

#ifndef ONLINEBOT_TABLE_ONLY

/* what one flip shows */
typedef struct {
    uint32_t flow, step, page, cursor;
    int sc;
    int popup;                  /* the message box is open */
    int busy;                   /* the front end is changing pages ([0x550DA8] bit 16) */
    uint32_t pkind, pcur;
    int lobby;                  /* the lobby screen runs */
    uint8_t lstate, linked, alive;
    int race;
    uint32_t np;                /* the title's peer table */
    uint32_t links[8];          /* each peer's link state; 0xFF: none (ourselves) */
    xnet_hle_lobby_view lv;
} ob_view;

static int s_role;              /* 0 off, 1 host, 2 guest, 3 probe */
static uint32_t s_flip;
static const char *s_dumpat, *s_dumppfx;
static int s_modes[16], s_nmodes = 1, s_guests = 1, s_games;
static unsigned s_joinwait_ms = 300000;
static char s_host[16];
static int s_plans[16], s_nplans = 1, s_plan_i;
static char s_code[CE_LEN + 1] = "TKDW3";
static int s_vote_none;
static uint32_t s_rng;
static unsigned s_waitfor_s = 600, s_start_delay_s, s_cancel_after_s;
/* BO-372: plan browse's picks, in turn: "NAME:state" (open, racing, full or
 * any), the game's name as the list shows it (TAKEDAWN_ONLINEBOT_PICKS) */
#define OB_PICKS 8
static char s_picks[OB_PICKS][40];
static int s_npicks, s_pick_i;
static unsigned long long s_pick_since, s_br_line_at;
static unsigned s_pick_wait_s = 600;
static unsigned s_page_hold_s;
static unsigned long long s_page_seen;  /* the online page's last tick (PAGE_HOLD) */
static char s_br_seen[400];
static int s_quit_results;
static unsigned s_results_hold_s, s_popup_hold_s;
static int s_results_held, s_popup_held, s_results_cancel, s_results_cancelled;
static unsigned long long s_start_ms;

/* the run */
static int s_stopped, s_done;
static int s_game;              /* games begun; the current one's number */
static int s_game_mode = -1;    /* the mode the current game was made for */
static int s_made_mode = -1;    /* the mode page's choice for the game being made */
static int s_in_game;           /* from its lobby until the room again */
static int s_raced, s_result_said, s_played;
static int s_race_n, s_races_in_game;  /* races ended in all, and in this lobby */
static int s_max_races, s_leave_after; /* 0: no limit */
static int s_res_place = -1;
static unsigned s_res_racers;
static float s_res_time;
static unsigned long long s_car_at;     /* the car page's A of this visit */

/* the screen and its timing */
static unsigned long long s_now;
static ob_view s_v;
static char s_key[96];
static unsigned long long s_key_since, s_sc_since;
static int s_last_sc = -1;
static unsigned long long s_next_ms;          /* no press before this */
static unsigned long long s_last_press_ms;
static char s_step[48], s_step_key[96];
static int s_attempts;
static unsigned long long s_attempt_wait;
static uint32_t s_attempt_flip, s_key_flip;   /* the same waits in the title's frames */
static unsigned long long s_busy_since;
static int s_commit_sc = -1;                  /* an A, B or Start on this screen: wait for it to go */
static int s_recover;
static unsigned long long s_retry_at;         /* guest: Quick Match again after this */
static unsigned s_gsea_seen;
static int s_join_tried;                      /* JOIN WITH CODE's join pressed; the entry back: a miss */
static int s_vote_row = -1;
static unsigned s_vote_round;
static int s_quick_waited;                    /* QUICK PLAY's own delay is over */
static unsigned s_alone_ms;                   /* how long a QUICK PLAY host waits alone */
static unsigned long long s_gate_since, s_lobby_since, s_start_at, s_wait_said;
/* waiting for a race under way (BO-365) */
static int s_wait_on;                         /* the Quick Match page is waiting for a race */
static unsigned long long s_wait_since;
static int s_wait_state, s_wait_id, s_wait_cancelled, s_wait_cancel_done;
static unsigned s_wait_seq;
static unsigned long long s_race_gate_since, s_race_gate_said;   /* plan wait: the online page's wait for a race */
static int s_quit_pressed;
static int s_race_gate;                       /* TAKEDAWN_ONLINEBOT_RACE_GATE: plan code waits for a race too */
static int s_starts, s_leaving, s_points;
static unsigned long long s_dead_since;       /* not alive with every guest's car chosen, since */

static unsigned long long ob_ms(void)
{
#ifdef _WIN32
    return (unsigned long long)GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000ull + (unsigned long long)ts.tv_nsec / 1000000ull;
#endif
}

static int ok_va(uint32_t va) { return va >= 0x10000u && va < 0x04000000u; }

#define OB_LOG(...) do { fprintf(stderr, "[ONLINEBOT] " __VA_ARGS__); fputc('\n', stderr); fflush(stderr); } while (0)

static void ob_read(ob_view *v)
{
    uint32_t cur = MEM32(OB_FLOW + 4), k, mod;
    memset(v, 0, sizeof *v);
    v->flow = ok_va(cur) ? cur - OB_FLOW : 0;
    v->step = ok_va(cur) ? MEM32(cur + 0x18) : 0;
    v->page = MEM32(OB_PAGE);
    v->cursor = MEM32(OB_CURSOR);
    v->popup = MEM32(OB_MBOX_OPEN) != 0;
    v->busy = (MEM32(OB_MBOX + 0xF8u) & 0x10000u) != 0;
    v->pkind = MEM32(OB_MBOX_KIND);
    v->pcur = MEM32(OB_MBOX_CUR);
    v->lobby = bo3_online_lobby_now(&v->lstate, &v->linked, &v->alive);
    mod = MEM32(BO3_MODULE_CUR);
    v->race = ok_va(mod) && bo3_race_module_vt(MEM32(mod));
    v->np = MEM32(OB_NPEERS);
    for (k = 0; k < 8; k++) {
        uint32_t P = k < v->np ? MEM32(OB_PEERS + 4 * k) : 0;
        v->links[k] = ok_va(P) ? MEM32(P + 0x4F0) : 0xFF;
    }
    xnet_hle_lobby_view_get(&v->lv);
    v->sc = bo3_online_screen_for(v->flow, v->page);
    /* the lobby page shows a moment before its screen object runs */
    if (v->sc == OB_SC_LOBBY && !v->lobby)
        v->sc = OB_SC_LOADING;
    if (v->race && v->sc != OB_SC_RACE_END && v->sc != OB_SC_RESULTS)
        v->sc = OB_SC_RACE;
}

/* ---- frames and presses ---- */

/* TAKEDAWN_ONLINEBOT_WAIT_FRAMES=<flips> (tests, BO-365): a frame every that
 * many flips on the Quick Match page and for 45 s after it (car select, the
 * lobby, or what came instead), plus one at each screen change in that time:
 * the frames a proof of the wait path looks at. No log line per frame. */
static unsigned s_wf_period;
static unsigned long long s_wf_until;
static uint32_t s_wf_next;
static void ob_wait_frames(int changed)
{
    FILE *f;
    if (!s_wf_period || !s_dumpat)
        return;
    /* the screens after a race too: results to the lobby, both sides of
     * stay together */
    if (s_v.sc == OB_SC_QUICK || s_v.sc == OB_SC_RACE_END || s_v.sc == OB_SC_RESULTS || s_v.sc == OB_SC_AUTOSAVE ||
        s_v.sc == OB_SC_POINTS || (s_in_game && (s_v.sc == OB_SC_SIGNIN || s_v.sc == OB_SC_RANKINGS)))
        s_wf_until = s_now + 45000ull;
    if (s_now >= s_wf_until || (!changed && s_flip < s_wf_next))
        return;
    s_wf_next = s_flip + s_wf_period;
    f = fopen(s_dumpat, "wb");
    if (!f)
        return;
    fprintf(f, "%u\n", s_flip + 5u);
    fclose(f);
}

static void ob_frame(const char *why)
{
    FILE *f;
    uint32_t at = s_flip + 10;
    if (!s_dumpat) {
        OB_LOG("frame %s: none (RECOMP_FB_DUMP_AT=@file is not set)", why);
        return;
    }
    f = fopen(s_dumpat, "wb");
    if (!f)
        return;
    fprintf(f, "%u-%u\n", at, at);
    fclose(f);
    OB_LOG("frame %s: %sflip_%05u.bmp", why, s_dumppfx ? s_dumppfx : "", at);
}

/* hold: the press's hold in ms; wait: how long the screen may take to answer
 * before the same step counts again */
static void ob_press(const char *btn, unsigned hold, unsigned wait, const char *why)
{
    char line[48];
    snprintf(line, sizeof line, "%s:%u", btn, hold);
    if (!usb_gamepad_live_push(line)) {
        OB_LOG("press %s: the pad queue is full", line);
        s_next_ms = s_now + 500;
        return;
    }
    OB_LOG("press %s on %s: %s", btn, bo3_onlinebot_screen_name(s_v.sc), why);
    s_last_press_ms = s_now;
    s_next_ms = s_now + hold + 350u;
    s_attempt_wait = s_now + hold + wait;
    /* menus move a step a frame: on a slow copy a wait is frames as much as
     * milliseconds (60 a second) */
    s_attempt_flip = s_flip + (hold + wait) * 60u / 1000u;
    /* A, B and Start leave the screen or answer it: nothing more on this
     * screen until it goes or the wait is over (its cursor and popup may
     * move while it goes, and an A on the next screen is another press) */
    s_commit_sc = (!strcmp(btn, "a") || !strcmp(btn, "b") || !strcmp(btn, "start")) ? s_v.sc : -1;
}

static void ob_stop(const char *why)
{
    if (s_stopped)
        return;
    s_stopped = 1;
    OB_LOG("stopped: %s", why);
    ob_frame("stopped");
}

static void ob_screen_line(char *out, size_t n)
{
    snprintf(out, n, "%s flow=+0x%X step=%u page=%08X cursor=%u popup=%d kind=%u lobby=%s%02X linked=%u alive=%u",
             bo3_onlinebot_screen_name(s_v.sc), s_v.flow, s_v.step, s_v.page, s_v.cursor, s_v.popup,
             s_v.popup ? s_v.pkind : 0u, s_v.lobby ? "" : "-", s_v.lstate, s_v.linked, s_v.alive);
}

/* No progress: the line, a frame, one B (every dialog it can open is
 * answered by the steps), and after OB_MAX_RECOVER of those, stop. */
static void ob_stuck(const char *why)
{
    char sl[200];
    ob_screen_line(sl, sizeof sl);
    OB_LOG("stuck: screen=%s step=%s (%s) game=%d", sl, s_step[0] ? s_step : "-", why, s_game);
    ob_frame("stuck");
    s_attempts = 0;
    s_step[0] = 0;
    s_step_key[0] = 0;
    if (++s_recover > OB_MAX_RECOVER) {
        ob_stop("no progress after several recoveries");
        return;
    }
    if (s_v.sc == OB_SC_RACE || s_v.sc == OB_SC_LOADING || s_v.sc == OB_SC_BOOT) {
        s_next_ms = s_now + 5000;
        return;
    }
    /* the event's end: B is Quit there, A goes on */
    if (s_v.sc == OB_SC_RESULTS || s_v.sc == OB_SC_POINTS || s_v.sc == OB_SC_RACE_END) {
        ob_press("a", 300, 4000, "on (recovery)");
        return;
    }
    ob_press("b", 300, 4000, "back out (recovery)");
}

/* One step on this screen. The same step on the same screen again counts as
 * an attempt, once the last press's wait is over; a fourth is stuck. */
static int ob_step(const char *step)
{
    if (strcmp(step, s_step) || strcmp(s_key, s_step_key)) {
        snprintf(s_step, sizeof s_step, "%s", step);
        snprintf(s_step_key, sizeof s_step_key, "%s", s_key);
        s_attempts = 0;
    }
    if (s_attempts && (s_now < s_attempt_wait || (int32_t)(s_flip - s_attempt_flip) < 0))
        return 0;
    if (++s_attempts > 3) {
        ob_stuck("the same step three times without the screen changing");
        return 0;
    }
    return 1;
}

static void ob_progress(void)
{
    s_recover = 0;
}

/* ---- the steps ---- */

/* the focused list's cursor to line `want`; 1 when it is there */
static int ob_cursor_to(uint32_t want, const char *what)
{
    char st[48];
    if (s_v.cursor == want)
        return 1;
    if (s_v.cursor == 0xFFFFFFFFu)
        return 0;                       /* no list has the focus yet (the page comes in) */
    snprintf(st, sizeof st, "cursor to %s", what);
    if (ob_step(st))
        ob_press(s_v.cursor < want ? "down" : "up", 150, 1500, st);
    return 0;
}

static int mode_of_game(int n)
{
    return s_modes[(n > 0 ? n - 1 : 0) % s_nmodes];
}

static const char *mode_name(int m)
{
    switch (m) {
    case 0: return "Single Race";
    case 1: return "Road Rage";
    case 2: return "Team Crush";
    case 3: return "Double Impact";
    case 4: return "Party Crash";
    }
    return "?";
}

static unsigned ob_rand(unsigned n)
{
    s_rng = s_rng * 1103515245u + 12345u;
    return (s_rng >> 16) % n;
}

static int ob_plan(void)
{
    return s_plans[(unsigned)s_plan_i % (unsigned)s_nplans];
}

/* every guest in the lobby's record has a car (ready value 1 or 3) */
static int ob_all_cars(void)
{
    int i;
    for (i = 0; i < s_v.lv.np && i < 8; i++)
        if (strcmp(s_v.lv.name[i], xnet_hle_gamertag()) && s_v.lv.ready[i] != 1 && s_v.lv.ready[i] != 3)
            return 0;
    return 1;
}

/* the host's Start, from the view */
static int ob_gate(int want, char *why, size_t n)
{
    ob_gate_in g;
    int i;
    memset(&g, 0, sizeof g);
    g.lstate = s_v.lstate;
    g.linked = s_v.linked;
    g.alive = s_v.alive;
    g.np = s_v.np;
    memcpy(g.links, s_v.links, sizeof g.links);
    g.players = s_v.lv.np;
    for (i = 0; i < 8; i++) {
        memcpy(g.name[i], s_v.lv.name[i], sizeof g.name[i]);
        g.ready[i] = s_v.lv.ready[i];
    }
    g.me = xnet_hle_gamertag();
    g.want = want;
    return bo3_onlinebot_gate(&g, why, n);
}

static void ob_game_started(void)
{
    if (s_raced || !s_in_game)
        return;
    s_raced = 1;
    OB_LOG("game %d started: %s, %d players in the lobby record, %u in the title's table", s_game,
           s_game_mode >= 0 ? mode_name(s_game_mode) : "(the host's mode)", s_v.lv.np, s_v.np);
    ob_progress();
}

static void ob_game_result(void)
{
    float t;
    if (s_result_said || !s_raced)
        return;
    s_result_said = 1;
    memcpy(&t, (const void *)XBOX_PTR(OB_CAR1 + 0x10DCu), 4);
    s_res_place = (int)(int16_t)MEM16(OB_CAR1 + 0x10D0u);
    s_res_racers = MEM32(OB_RACERS);
    s_res_time = t;
    OB_LOG("game %d result: %s place %d/%u time %.3f s", s_game,
           s_game_mode >= 0 ? mode_name(s_game_mode) : "(the host's mode)", s_res_place, s_res_racers, t);
}

/* One race over, the copy back in a lobby (stayed) or out of the game (left).
 * The soak counts these lines: "race N end" */
static void ob_race_end(const char *how)
{
    if (!s_raced)
        return;
    ob_game_result();
    s_race_n++;
    s_races_in_game++;
    OB_LOG("race %d end: game %d race %d, %s, place %d/%u, %u players, time %.3f s, %s", s_race_n, s_game,
           s_races_in_game, s_game_mode >= 0 ? mode_name(s_game_mode) : "(the host's mode)", s_res_place, s_res_racers,
           s_res_racers, s_res_time, how);
    s_raced = 0;
    s_result_said = 0;
    s_played = 0;
    /* the next race starts the lobby's timers and Start count afresh */
    s_lobby_since = s_now;
    s_starts = 0;
    s_start_at = 0;
    s_gate_since = 0;
    s_dead_since = 0;
}

static void ob_game_done(const char *where)
{
    if (!s_in_game)
        return;
    s_in_game = 0;
    ob_race_end("left");
    if (s_races_in_game) {
        OB_LOG("game %d done: back at %s", s_game, where);
        ob_progress();
        /* the race ended out of the game (the host gone, the stay-join
         * missed): RACES counts it as well, or a wait plan starts again */
        if (bo3_onlinebot_races_done(s_max_races, s_race_n) && !s_done) {
            s_done = 1;
            OB_LOG("all %d races played: staying here", s_max_races);
        } else if (s_games && s_game >= s_games && !s_done) {
            s_done = 1;
            OB_LOG("all %d games played: staying here", s_games);
        }
    } else {
        OB_LOG("game %d left before its race (at %s)", s_game, where);
    }
    s_raced = 0;
    s_races_in_game = 0;
    s_plan_i++;
}

/* this copy made the game in its lobby (the lobby record's first player) */
static int ob_owner(void)
{
    return s_v.lv.np > 0 && !strcmp(s_v.lv.name[0], xnet_hle_gamertag());
}

static void ob_lobby_enter(void)
{
    if (s_in_game)
        return;
    s_in_game = 1;
    s_raced = 0;
    s_races_in_game = 0;
    s_result_said = 0;
    s_played = 0;
    s_starts = 0;
    s_start_at = 0;
    s_leaving = 0;
    s_dead_since = 0;
    s_join_tried = 0;
    s_vote_row = -1;
    s_alone_ms = OB_ALONE_MIN_MS + ob_rand(OB_ALONE_MIN_MS);
    s_game++;
    s_game_mode = s_made_mode;
    s_made_mode = -1;
    OB_LOG("game %d: in the lobby of %s's game (%d players), plan %s", s_game, s_v.lv.np ? s_v.lv.name[0] : "?",
           s_v.lv.np, bo3_onlinebot_plan_name(ob_plan()));
    ob_progress();
}

/* The track vote: the box is the vote's, not a question to answer. One random
 * row per round, the cursor moved there and A, as a player votes; or nothing,
 * and the clock runs out. The winner's box is left alone. */
static void ob_vote(void)
{
    int rows, cursor, mine;
    unsigned round;
    char st[48];
    if (!bo3_vote_bot_view(&rows, &cursor, &mine, &round) || s_vote_none || mine >= 0 || rows < 1)
        return;
    if (s_vote_row < 0 || s_vote_round != round) {
        s_vote_row = (int)ob_rand((unsigned)rows);
        s_vote_round = round;
        OB_LOG("game %d: the vote, round %u: row %d of %d", s_game, round, s_vote_row, rows);
    }
    if (cursor != s_vote_row) {
        snprintf(st, sizeof st, "vote: row %d to %d", cursor, s_vote_row);
        if (ob_step(st))
            ob_press(cursor < s_vote_row ? "down" : "up", 150, 1000, st);
        return;
    }
    if (ob_step("vote: this row"))
        ob_press("a", 200, 1000, "vote");
}

/* back in a game lobby (its vote, or its own screen) with a race behind: count it and decide to
 * stay, to be done, or to leave after this lobby's K races */
static void ob_after_race(void)
{
    if (!s_in_game || !s_raced)
        return;
    ob_race_end("stayed");
    if (bo3_onlinebot_races_done(s_max_races, s_race_n) && !s_done) {
        s_done = 1;
        OB_LOG("all %d races played: staying here", s_max_races);
    } else if (s_leave_after && s_races_in_game >= s_leave_after && !s_done) {
        s_leaving = 1;
        OB_LOG("game %d: %d races played here: leaving the lobby", s_game, s_races_in_game);
    }
    ob_progress();
}

/* the game lobby: host and guest */
static void ob_lobby(void)
{
    char why[96];
    if (!s_lobby_since)
        s_lobby_since = s_now;
    ob_after_race();
    switch (s_v.lstate) {
    case 0x03:                          /* a player's menu (VIEW PROFILE): never wanted */
        if (ob_step("close the player menu"))
            ob_press("b", 300, 3000, "close the player menu");
        return;
    case 0x09: {                        /* "leave the game?" */
        uint32_t want = s_leaving ? 0u : 1u;
        if (!s_v.popup)
            return;
        if (s_v.pcur != want) {
            if (ob_step(s_leaving ? "leave: to yes" : "stay: to no"))
                ob_press(s_v.pcur > want ? "up" : "down", 150, 1500, s_leaving ? "to YES" : "to NO");
            return;
        }
        if (ob_step(s_leaving ? "leave: yes" : "stay: no"))
            ob_press("a", 300, 5000, s_leaving ? "YES, leave the game" : "NO, stay in the game");
        return;
    }
    case 0x01:
        break;
    default:                            /* the screen's own steps, starting, the lock */
        return;
    }
    ob_lobby_enter();
    if (s_done)
        return;
    if (s_leaving) {
        if (ob_step("leave the game"))
            ob_press("b", 300, 3000, "leave the game");
        return;
    }
    if (!ob_owner()) {
        /* a guest in another host's game leaves it */
        if (s_host[0] && s_v.lv.np && strcmp(s_v.lv.name[0], s_host)) {
            if (!s_leaving)
                OB_LOG("the game of %s, not %s: leaving it", s_v.lv.name[0], s_host);
            s_leaving = 1;
            if (ob_step("leave the game"))
                ob_press("b", 300, 3000, "leave the game");
            return;
        }
    } else {
        /* a game QUICK PLAY made starts by itself: its vote or countdown,
         * never this copy's Start */
        int own_start = !bo3_quickplay_host_game();
        int want = s_guests;
        if (s_now - s_lobby_since > s_joinwait_ms)
            want = 1;
        if (s_start_at && s_now - s_start_at < 8000)
            return;                     /* the Start may still be on its way */
        if (!own_start) {
            snprintf(why, sizeof why, "the vote or the countdown starts this game (%d players)", s_v.lv.np);
            /* Copies that came off the same race search in the same second, find
             * nothing and each make a game: alone for a while, this copy leaves
             * and searches again */
            if (s_v.lv.np < 2 && s_now - s_lobby_since > s_alone_ms) {
                if (!s_leaving)
                    OB_LOG("game %d: alone in a QUICK PLAY game for %u s: searching again", s_game, s_alone_ms / 1000u);
                s_leaving = 1;
                if (ob_step("leave the game"))
                    ob_press("b", 300, 3000, "leave the game");
                return;
            }
        }
        if (own_start && ob_gate(want, why, sizeof why)) {
            if (!s_gate_since)
                s_gate_since = s_now;
            if (s_now - s_gate_since >= 2000) {
                if (++s_starts > 3) {
                    ob_stuck("Start pressed three times on an open gate, the game did not start");
                    return;
                }
                OB_LOG("game %d: start gate open (%s)", s_game, why);
                ob_press("start", 400, 8000, "start the game");
                s_start_at = s_now;
                s_gate_since = 0;
                return;
            }
        } else {
            s_gate_since = 0;
            if (s_now - s_wait_said > 30000) {
                s_wait_said = s_now;
                OB_LOG("game %d: waiting to start: %s", s_game, why);
            }
            /* not alive (0x13A5) for 90 s with every guest's car chosen: that game's
             * screen is dead. A guest without a car (still on its car page, a
             * late joiner) holds alive at 0 too, and that passes. */
            if (!s_v.alive && ob_all_cars()) {
                if (!s_dead_since)
                    s_dead_since = s_now;
            } else {
                s_dead_since = 0;
            }
            if ((s_dead_since && s_now - s_dead_since > 90000) || s_now - s_lobby_since > 2 * s_joinwait_ms + 60000) {
                if (!s_leaving)
                    OB_LOG("game %d: %s: leaving the game to make a new one", s_game,
                           !s_v.alive ? "the lobby screen is not alive" : "no guest came");
                s_leaving = 1;
                if (ob_step("leave the game"))
                    ob_press("b", 300, 3000, "leave the game");
                return;
            }
        }
    }
    /* the player stays active: down then up on the player list */
    if (s_now - s_last_press_ms > OB_KEEPALIVE_MS) {
        OB_LOG("keep-alive in the game lobby");
        usb_gamepad_live_push("down:100");
        usb_gamepad_live_push("up:100");
        s_last_press_ms = s_now;
        s_next_ms = s_now + 1500;
    }
}

/* JOIN WITH CODE's entry, up on the online page */
static void ob_entry(const char *typed, int col)
{
    char st[48];
    if (s_join_tried) {
        s_join_tried = 0;
        s_retry_at = s_now + 10000;
        s_plan_i++;
        OB_LOG("JOIN WITH CODE: no open game with %s; next %s in 10 s", s_code,
               bo3_onlinebot_plan_name(ob_plan()));
        ob_progress();
        /* a recorded miss is an answer, not a stuck step: the next join starts its count again */
        s_attempts = 0;
        s_step[0] = 0;
        s_step_key[0] = 0;
    }
    if (ob_plan() != OB_PLAN_CODE) {
        /* B takes a letter back, and with none left it leaves the entry */
        snprintf(st, sizeof st, "entry: leave, %zu typed", strlen(typed));
        if (ob_step(st))
            ob_press("b", 150, 700, st);
        return;
    }
    snprintf(st, sizeof st, "entry %zu: line %u col %d", strlen(typed), s_v.cursor, col);
    switch (bo3_onlinebot_entry_step(s_code, typed, (int)s_v.cursor, col)) {
    case OB_CE_DELETE:
        if (ob_step(st))
            ob_press("x", 150, 700, "take a letter back");
        break;
    case OB_CE_UP:
        if (ob_step(st))
            ob_press("up", 150, 700, st);
        break;
    case OB_CE_DOWN:
        if (ob_step(st))
            ob_press("down", 150, 700, st);
        break;
    case OB_CE_LEFT:
        if (ob_step(st))
            ob_press("left", 150, 700, st);
        break;
    case OB_CE_RIGHT:
        if (ob_step(st))
            ob_press("right", 150, 700, st);
        break;
    case OB_CE_TYPE:
        if (ob_step(st))
            ob_press("a", 150, 700, "type a letter");
        break;
    case OB_CE_JOIN:
        if (s_now >= s_retry_at && ob_step("entry: join")) {
            s_gsea_seen = s_v.lv.got[XNET_LV_GSEA];
            s_join_tried = 1;
            OB_LOG("JOIN WITH CODE: %s", s_code);
            ob_press("a", 300, 6000, "JOIN");
        }
        break;
    }
}

/* ---- waiting for a race under way (BO-365) ---- */

/* races a stranger could wait for, as the lobby last said (room record or a
 * search's answer); 0 with a toolkit that has no race keys */
static int ob_racing(void)
{
#ifdef XNET_HLE_HAS_RACEWAIT
    int r = xnet_hle_lobby_racing();
    return r > s_v.lv.racing ? r : s_v.lv.racing;
#else
    return 0;
#endif
}

/* the Quick Match page, every settled flip: follow the server's TKWAITSTATE answers */
static void ob_wait_track(void)
{
#ifdef XNET_HLE_HAS_RACEWAIT
    if (!s_wait_on) {
        /* the race the client waits for (a poll's answer carries no TKRACEID) */
        int id = bo3_quickplay_wait_ident();
        if (id > 0)
            s_wait_id = id;
        else if (s_v.lv.race_id > 0)
            s_wait_id = s_v.lv.race_id;
    }
    if (s_v.lv.wait_seq == s_wait_seq || s_v.lv.wait_state == XNET_WAIT_NONE)
        return;
    s_wait_seq = s_v.lv.wait_seq;
    if (!s_wait_on) {
        s_wait_on = 1;
        s_wait_since = s_now;
        s_wait_state = -1;
        s_wait_cancelled = 0;
    }
    if (s_v.lv.wait_state != s_wait_state) {
        s_wait_state = s_v.lv.wait_state;
        OB_LOG("waiting for game %d: %s (%d players, %d s)", s_wait_id, bo3_onlinebot_wait_state_name(s_wait_state),
               s_v.lv.wait_players, s_v.lv.wait_secs);
    }
#endif
}

/* a wait runs on the Quick Match page: the lobby has answered a wait poll, or
 * the client's own machine waits (its first poll may not be answered yet) */
static int ob_waiting(void)
{
    int st = bo3_quickplay_wait_view(NULL);
    return s_wait_on || st == RW_WAIT || st == RW_JOINING;
}

/* the screen changed away from Quick Match while a wait ran: how it ended */
static void ob_wait_leave(int next_sc)
{
    int r;
    unsigned long long waited;
    if (!s_wait_on || !bo3_onlinebot_wait_settled(next_sc))
        return;
    waited = s_now - s_wait_since;
    r = bo3_onlinebot_wait_result(s_wait_state, waited, next_sc, s_wait_cancelled);
    /* joined: the game this copy is in now (the race's successor, or another
     * game that opened meanwhile), not the race it waited for */
    if (r == OB_WAIT_JOINED && s_v.lv.game > 0)
        OB_LOG("wait ended: %s (game %d, waited for game %d, %llu s, last state %s, screen %s)",
               bo3_onlinebot_wait_result_name(r), s_v.lv.game, s_wait_id, waited / 1000ull,
               bo3_onlinebot_wait_state_name(s_wait_state), bo3_onlinebot_screen_name(next_sc));
    else
        OB_LOG("wait ended: %s (waited for game %d, %llu s, last state %s, screen %s)", bo3_onlinebot_wait_result_name(r),
               s_wait_id, waited / 1000ull, bo3_onlinebot_wait_state_name(s_wait_state),
               bo3_onlinebot_screen_name(next_sc));
    /* back on the online page without a game (the wait plan; the code entry
     * counts its own miss): the plan goes on, but not after our own B */
    if (r != OB_WAIT_JOINED && r != OB_WAIT_CANCELLED && ob_plan() == OB_PLAN_WAIT &&
        (next_sc == OB_SC_LIVE || next_sc == OB_SC_ROOM || next_sc == OB_SC_LOBBYSEL || next_sc == OB_SC_MAIN)) {
        s_plan_i++;
        s_retry_at = s_now + 10000;
        ob_progress();
    }
    if (r == OB_WAIT_CANCELLED)
        s_retry_at = s_now + 5000;
    s_wait_on = 0;
    s_wait_state = 0;
    s_wait_id = 0;
    s_wait_cancelled = 0;
    s_wait_seq = s_v.lv.wait_seq;
}

/* the TAKEDAWN ONLINE page: the plan's line (the plans are its lines, in order) */
static void ob_online_page(void)
{
    char typed[CE_LEN + 2];
    int col = 0, plan = ob_plan();
    if (bo3_xboxlive_entry_view(typed, sizeof typed, &col)) {
        ob_entry(typed, col);
        return;
    }
    if (s_now < s_start_ms + (unsigned long long)s_start_delay_s * 1000ull)
        return;                         /* TAKEDAWN_ONLINEBOT_START_DELAY */
    if (s_page_hold_s && s_now - s_page_seen > 1000) {    /* back on the page: TAKEDAWN_ONLINEBOT_PAGE_HOLD */
        s_page_seen = s_now;
        if (s_retry_at < s_now + (unsigned long long)s_page_hold_s * 1000ull)
            s_retry_at = s_now + (unsigned long long)s_page_hold_s * 1000ull;
        OB_LOG("online page: held %u s", s_page_hold_s);
    }
    s_page_seen = s_now;
    /* copies off the same race would all search in one second */
    if (plan == OB_PLAN_QUICK && !s_quick_waited) {
        s_quick_waited = 1;
        s_retry_at = s_now + ob_rand(OB_QUICK_DELAY_MS);
    }
    if (s_now < s_retry_at)
        return;
    /* plan wait: QUICK PLAY only once a race is under way (and JOIN WITH
     * CODE too with TAKEDAWN_ONLINEBOT_RACE_GATE=1: the code's game races) */
    if (plan == OB_PLAN_WAIT || (s_race_gate && plan == OB_PLAN_CODE)) {
        if (!s_race_gate_since) {
            s_race_gate_since = s_now;
            s_race_gate_said = s_now;
            OB_LOG("plan wait: waiting for a race under way (up to %u s)", s_waitfor_s);
        }
        if (ob_racing() <= 0) {
            if (s_now - s_race_gate_since < (unsigned long long)s_waitfor_s * 1000ull) {
                if (s_now - s_race_gate_said >= 30000) {
                    s_race_gate_said = s_now;
                    OB_LOG("plan wait: no race yet (%llu s)", (s_now - s_race_gate_since) / 1000ull);
                }
                return;
            }
            OB_LOG("plan wait: no race within %u s: QUICK PLAY as it is", s_waitfor_s);
        } else {
            OB_LOG("plan wait: %d race(s) under way: QUICK PLAY", ob_racing());
        }
    }
    if (ob_cursor_to((uint32_t)bo3_onlinebot_plan_line(plan), bo3_onlinebot_plan_name(plan)) &&
        ob_step("online: the plan's line")) {
        s_race_gate_since = 0;
        s_gsea_seen = s_v.lv.got[XNET_LV_GSEA];
        s_join_tried = 0;
        s_quick_waited = 0;
        OB_LOG("plan %s (game %d next)", bo3_onlinebot_plan_name(plan), s_game + 1);
        ob_press("a", 300, 6000, bo3_onlinebot_plan_name(plan));
    }
}

/* BO-372: plan browse on the BROWSE GAMES list; 1 when the list (or its
 * search) has the page and nothing else is to be done here */
static int ob_browse(void)
{
    qp_browse_view v;
    char want[40], seen[400], st[64], *colon;
    int i, row = -1, ws = -1, o = 0;
    if (!bo3_quickplay_browse_view(&v) || bo3_quickplay_wait_view(NULL) != RW_OFF)
        return 0;                       /* not browsing, or the race wait (as for QUICK PLAY) */
    s_sc_since = s_now;                 /* a list being read is not a stuck search */
    if (!v.up)
        return 1;
    for (i = 0; i < v.count && o < (int)sizeof seen - 48; i++)
        o += snprintf(seen + o, sizeof seen - (size_t)o, "%s%s(%s)", i ? ", " : "", v.name[i],
                      v.state[i] == QP_BR_RACING ? "racing" : v.state[i] == QP_BR_FULL ? "full" : "open");
    if (!o)
        seen[0] = 0;
    if (strcmp(seen, s_br_seen)) {
        snprintf(s_br_seen, sizeof s_br_seen, "%s", seen);
        OB_LOG("browse: the list: %s", seen);
        ob_frame("browse-list");
    }
    if (v.line[0] && (!s_br_line_at || s_now - s_br_line_at > 5000)) {
        s_br_line_at = s_now;
        OB_LOG("browse: the line says \"%s\"", v.line);
        ob_frame("browse-line");
    }
    if (s_pick_i >= s_npicks)
        return 1;                       /* nothing more to pick: the list stays */
    if (!s_pick_since)
        s_pick_since = s_now;
    snprintf(want, sizeof want, "%s", s_picks[s_pick_i]);
    if (!strcmp(want, "back")) {        /* B on the list: back to the online page */
        if (ob_step("browse: back")) {
            OB_LOG("browse: B on the list");
            ob_press("b", 300, 4000, "back to the online page");
            s_pick_i++;
            s_pick_since = 0;
        }
        return 1;
    }
    if ((colon = strchr(want, ':')) != NULL) {
        *colon++ = 0;
        ws = !strcmp(colon, "open") ? QP_BR_OPEN : !strcmp(colon, "racing") ? QP_BR_RACING
           : !strcmp(colon, "full") ? QP_BR_FULL : -1;
    }
    for (i = 0; i < v.count && row < 0; i++)
        if (!strcmp(v.name[i], want) && (ws < 0 || v.state[i] == ws))
            row = i;
    if (row < 0) {
        if (s_now - s_pick_since > (unsigned long long)s_pick_wait_s * 1000ull) {
            OB_LOG("browse: '%s' not listed%s%s within %u s: next pick", want, colon ? " as " : "", colon ? colon : "",
                   s_pick_wait_s);
            s_pick_i++;
            s_pick_since = 0;
        }
        return 1;
    }
    if (v.cursor != row) {
        snprintf(st, sizeof st, "browse: cursor %d to %d", v.cursor, row);
        if (ob_step(st))
            ob_press(v.cursor < row ? "down" : "up", 150, 1500, "to the game picked");
        return 1;
    }
    snprintf(st, sizeof st, "browse: pick %d", s_pick_i);
    if (ob_step(st)) {
        OB_LOG("browse: A on '%s' (%s, line %d)", want,
               v.state[row] == QP_BR_RACING ? "racing" : v.state[row] == QP_BR_FULL ? "full" : "open", row);
        ob_frame("browse-pick");
        ob_press("a", 300, 4000, "the game picked");
        s_pick_i++;
        s_pick_since = 0;
        s_br_line_at = 0;
        ob_progress();
    }
    return 1;
}

static void ob_act(void)
{
    if (s_v.sc != OB_SC_LOBBY)
        s_lobby_since = 0;

    /* the track vote's box and the winner's: the vote's own */
    if (s_v.sc == OB_SC_LOBBY && bo3_vote_busy()) {
        if (!s_lobby_since)
            s_lobby_since = s_now;
        ob_lobby_enter();
        ob_after_race();
        if (s_done)
            return;
        if (s_leaving) {
            if (s_v.lstate == 0x09)
                ob_lobby();             /* "leave the game?" is the lobby's own: YES */
            else if (ob_step("leave the game"))
                ob_press("b", 300, 3000, "leave the game");
            return;
        }
        ob_vote();
        return;
    }
    /* the lobby's own steps carry notices (the track going out to the guests):
     * they are answered only if one is left standing */
    if (s_v.sc == OB_SC_LOBBY && s_v.popup && s_v.lstate != 0x01 && s_v.lstate != 0x09 &&
        s_now - s_key_since < 10000)
        return;

    /* a message box first, answered by its kind (the lobby's own "leave the
     * game?" is the lobby's) */
    if (s_v.popup && !(s_v.sc == OB_SC_LOBBY && s_v.lstate == 0x09)) {
        /* the results' "Leaving in 20 seconds / CANCEL" after Continue To
         * Lobby while others still race: its only button cancels the leaving */
        if (s_v.sc == OB_SC_RESULTS && !(s_quit_pressed && s_v.pkind == 7)) {
            /* TAKEDAWN_ONLINEBOT_RESULTS_CANCEL: its CANCEL, once a race: this
             * player goes on alone (stay together waits for the others) */
            if (s_results_cancel && !s_results_cancelled && s_now - s_key_since > 3000 && ob_step("results: cancel the wait")) {
                s_results_cancelled = 1;
                OB_LOG("game %d: CANCEL on the results' wait for the others", s_game);
                ob_frame("results wait");
                ob_press("a", 300, 4000, "CANCEL");
            }
            return;
        }
        /* TAKEDAWN_ONLINEBOT_POPUP_HOLD: a slow answer to a box after the race */
        if (s_popup_hold_s && s_in_game && s_now - s_key_since < s_popup_hold_s * 1000ull) {
            if (!s_popup_held) {
                s_popup_held = 1;
                OB_LOG("popup kind %u on %s: held %u s before the answer", s_v.pkind,
                       bo3_onlinebot_screen_name(s_v.sc), s_popup_hold_s);
                ob_frame("popup held");
            }
            return;
        }
        s_popup_held = 0;
        if (s_v.pkind == 7) {
            /* YES / NO: the one asked here unplanned is "sign out?" (B on
             * the Xbox Live menu): NO. Any other (the title's "sign back
             * in?" after an idle sign-out): YES. */
            uint32_t want = s_v.sc == OB_SC_LIVE ? 1u : 0u;
            if (s_v.pcur != want) {
                if (ob_step("popup: cursor"))
                    ob_press(s_v.pcur > want ? "up" : "down", 150, 1500, want ? "to NO" : "to YES");
                return;
            }
            if (ob_step("popup: answer")) {
                OB_LOG("popup on %s: %s", bo3_onlinebot_screen_name(s_v.sc), want ? "NO" : "YES");
                ob_frame("popup");
                ob_press("a", 300, 4000, want ? "NO" : "YES");
            }
            return;
        }
        if (ob_step("popup: OK")) {
            /* the main menu opens no box on the way online any more: the
             * title's Xbox Live notice is skipped (xboxlive.c xl_notice), so
             * one here is a regression a soak should show */
            if (s_v.sc == OB_SC_MAIN)
                OB_LOG("unexpected: a message box on the main menu (kind %u)", s_v.pkind);
            OB_LOG("popup kind %u on %s: A", s_v.pkind, bo3_onlinebot_screen_name(s_v.sc));
            if (s_v.pkind != 2)
                ob_frame("popup");
            ob_press("a", 300, 4000, "OK");
        }
        return;
    }

    /* a screen with steps of its own that will not go: stuck (the lobby,
     * the race, Quick Match and the waits have their own limits) */
    if (s_v.sc != OB_SC_LOBBY && s_v.sc != OB_SC_RACE && s_v.sc != OB_SC_BOOT && s_v.sc != OB_SC_QUICK &&
        s_v.sc != OB_SC_LIVE && s_v.sc != OB_SC_LOADING && s_v.sc != OB_SC_SIGNIN && s_v.sc != OB_SC_CREATING &&
        s_v.sc != OB_SC_RANKINGS && s_v.sc != OB_SC_AUTOSAVE && s_v.sc != OB_SC_RACE_END &&
        !(s_done && (s_v.sc == OB_SC_ROOM || s_v.sc == OB_SC_MAIN)) && s_now - s_sc_since > 120000) {
        ob_stuck("the same screen for 2 minutes");
        s_sc_since = s_now;
        return;
    }

    switch (s_v.sc) {
    case OB_SC_LOADING:
    case OB_SC_SIGNIN:
    case OB_SC_CREATING:
    case OB_SC_RANKINGS:
    case OB_SC_AUTOSAVE:
    case OB_SC_RACE_END:
        if (s_now - s_sc_since > 180000) {
            ob_stuck("a waiting screen for 3 minutes");
            s_sc_since = s_now;
        }
        return;
    case OB_SC_BOOT:
        return;                         /* the scene script's (films, title, profile) */
    case OB_SC_RACE:
        ob_game_started();
        /* the car's phase: 2 in play, 3 finished (the last race's 3 stays until this one's GO) */
        if (MEM32(OB_CAR1 + 0x134Cu) == 2)
            s_played = 1;
        else if (s_played && MEM32(OB_CAR1 + 0x134Cu) == 3)
            ob_game_result();
        if (s_now - s_sc_since > 30u * 60000u) {
            ob_stuck("a race of 30 minutes");
            s_sc_since = s_now;
        }
        return;
    case OB_SC_RESULTS:
        ob_game_started();
        ob_game_result();
        if (s_results_hold_s && s_now - s_sc_since < s_results_hold_s * 1000ull) {
            if (!s_results_held) {
                s_results_held = 1;
                OB_LOG("game %d: %u s on the results before %s", s_game, s_results_hold_s,
                       s_quit_results && !s_done ? "Quit" : "Continue");
            }
            return;
        }
        s_results_held = 0;
        if (s_quit_results && !s_done) {
            /* TAKEDAWN_ONLINEBOT_QUIT_RESULTS: Quit (B) and its YES (after RESULTS_HOLD,
             * when set); then stay out of the lobby, so the host does not make the game again */
            if (ob_step("results: quit")) {
                s_quit_pressed = 1;
                OB_LOG("game %d: quitting on the results", s_game);
                ob_press("b", 300, 6000, "Quit");
            }
            return;
        }
        if (ob_step("results: continue"))
            ob_press("a", 300, 6000, "Continue To Lobby");
        return;
    case OB_SC_POINTS: {
        /* several pages on one page object (BURNOUT POINTS, TAKEDOWN
         * BREAKDOWN ...): each A its own step, ten at most a visit */
        char st[32];
        if (s_points >= 10) {
            ob_stuck("ten A on the points screens");
            s_points = 0;
            return;
        }
        snprintf(st, sizeof st, "points: next %d", s_points + 1);
        if (ob_step(st)) {
            s_points++;
            ob_press("a", 300, 4000, "Continue");
        }
        return;
    }
    case OB_SC_MAIN:
        if (s_done || s_now < s_start_ms + (unsigned long long)s_start_delay_s * 1000ull)
            return;
        if (ob_cursor_to(3, "TAKEDAWN ONLINE") && ob_step("main: takedawn online"))
            ob_press("a", 300, 5000, "TAKEDAWN ONLINE");
        return;
    case OB_SC_GAMERTAG:
        /* the gamertag alone (nodash.c takes the title's CREATE NEW ACCOUNT
         * and MANAGE ACCOUNTS out): the list opens on it */
        if (ob_step("gamertag: select"))
            ob_press("a", 300, 6000, "sign in");
        return;
    case OB_SC_LIVE:
        ob_game_done("xbox-live");
        if (s_done)
            return;
        ob_online_page();
        return;
    case OB_SC_LOBBYSEL:
        ob_game_done("lobby-select");
        if (s_v.flow == 0x630) {
            /* after a race A is the only way on: the room */
            if (ob_step("lobby select: room"))
                ob_press("a", 300, 6000, "into the room");
        } else if (ob_step("lobby select: back")) {
            ob_press("b", 300, 4000, "to the online page");
        }
        return;
    case OB_SC_ROOM:
        ob_game_done("room");
        if (s_done)
            return;
        /* every plan starts on the online page */
        if (ob_step("room: leave"))
            ob_press("b", 300, 5000, "out of the room, to the online page");
        return;
    case OB_SC_CREATE:
        if (ob_step("create: continue"))
            ob_press("a", 300, 5000, "Continue (name, limit as they are)");
        return;
    case OB_SC_MODE: {
        uint32_t m = MEM32(OB_MODE_VALUE);
        int want = mode_of_game(s_game + 1);
        if (bo3_quickplay_room_pending())
            return;                         /* its Continue waits for the room */
        if (MEM16(s_v.page + 4)) {          /* the page's row: GAME MODE is 0 */
            if (ob_step("mode: to the GAME MODE row"))
                ob_press("up", 150, 1500, "to GAME MODE");
            return;
        }
        if (m != (uint32_t)want) {
            char st[48];
            snprintf(st, sizeof st, "mode %u to %d", m, want);
            if (ob_step(st))
                ob_press(m < (uint32_t)want ? "right" : "left", 150, 1500, mode_name(want));
            return;
        }
        if (ob_step("mode: continue")) {
            s_made_mode = want;
            s_in_game = 0;
            OB_LOG("game %d: creating a %s game", s_game + 1, mode_name(want));
            ob_press("a", 300, 8000, "Continue: create the game");
        }
        return;
    }
    case OB_SC_QUICK:
        /* QUICK PLAY and JOIN WITH CODE run this page by themselves
         * (quickplay.c). BROWSE GAMES is the title's: NO GAMES FOUND waits for
         * a press. */
        ob_wait_track();
        if (ob_plan() == OB_PLAN_BROWSE && ob_browse())
            return;                     /* BO-372: the list's picks */
        /* TAKEDAWN_ONLINEBOT_CANCEL_AFTER: B once, while the race still runs */
        if (s_wait_on && s_cancel_after_s && !s_wait_cancel_done && s_wait_state == 1 &&
            s_now - s_wait_since >= (unsigned long long)s_cancel_after_s * 1000ull) {
            if (ob_step("wait: cancel")) {
                s_wait_cancel_done = 1;
                s_wait_cancelled = 1;
                OB_LOG("waiting for game %d: B after %llu s", s_wait_id, (s_now - s_wait_since) / 1000ull);
                ob_frame("wait-cancel");
                ob_press("b", 300, 6000, "cancel the wait");
            }
            return;
        }
        if (!bo3_quickplay_armed() && s_v.lv.got[XNET_LV_GSEA] > s_gsea_seen && s_v.lv.search_count == 0) {
            if (ob_step("quick match: no games")) {
                s_plan_i++;
                OB_LOG("no games found; next %s in 10 s", bo3_onlinebot_plan_name(ob_plan()));
                s_retry_at = s_now + 10000;
                ob_press("b", 300, 4000, "no games found: back");
                ob_progress();
            }
            return;
        }
        if (s_now - s_sc_since > bo3_onlinebot_quick_limit_ms(ob_waiting())) {
            ob_stuck(ob_waiting() ? "Quick Match waiting for a race for 9 minutes" : "Quick Match searching for a minute");
            s_sc_since = s_now;
        }
        return;
    case OB_SC_CAR:
        s_join_tried = 0;
        /* one A a visit, and another only after 10 s on the same page:
         * an A that lands in the lobby opens a player's menu */
        if (s_car_at && s_now - s_car_at < 10000)
            return;
        if (ob_step("car: select")) {
            s_car_at = s_now;
            ob_press("a", 400, 10000, "this car");
        }
        return;
    case OB_SC_LOBBY:
        ob_lobby();
        return;
    default:
        if (s_now - s_key_since > OB_UNKNOWN_MS) {
            ob_stuck("unknown screen");
            s_key_since = s_now;
        }
        return;
    }
}

void bo3_onlinebot_flip(uint32_t flip)
{
    char key[96];
    if (!s_role || !g_xbox_mem_offset)
        return;
    s_flip = flip;
    s_now = ob_ms();
    ob_read(&s_v);
    snprintf(key, sizeof key, "%d.%X.%u.%08X.%u.%d.%u.%u.%d.%02X.%d", s_v.sc, s_v.flow, s_v.step, s_v.page,
             s_v.cursor, s_v.popup, s_v.popup ? s_v.pkind : 0u, s_v.popup ? s_v.pcur : 0u, s_v.lobby, s_v.lstate,
             s_v.busy);
    if (strcmp(key, s_key)) {
        snprintf(s_key, sizeof s_key, "%s", key);
        s_key_since = s_now;
        s_key_flip = flip;
    }
    if (s_v.sc != s_last_sc) {
        char sl[200];
        ob_screen_line(sl, sizeof sl);
        OB_LOG("screen %s -> %s (flip %u)", s_last_sc >= 0 ? bo3_onlinebot_screen_name(s_last_sc) : "-", sl, flip);
        if (s_v.sc == OB_SC_UNKNOWN || s_role == 3)
            ob_frame(bo3_onlinebot_screen_name(s_v.sc));
        if (s_wait_on && s_v.sc != OB_SC_QUICK)
            ob_wait_leave(s_v.sc);
        if (s_v.sc != OB_SC_CAR)
            s_car_at = 0;
        if (s_v.sc != OB_SC_POINTS)
            s_points = 0;
        if (s_v.sc != OB_SC_RESULTS)
            s_results_cancelled = 0;
        if (bo3_onlinebot_step_fresh(s_last_sc, s_v.sc)) {
            s_step[0] = 0;
            s_step_key[0] = 0;
            s_attempts = 0;
        }
        s_last_sc = s_v.sc;
        s_sc_since = s_now;
        ob_wait_frames(1);
    } else
        ob_wait_frames(0);
#if !defined(BO3_RELEASE_BUILD)
    /* TAKEDAWN_ONLINEBOT_TRACK=lo,hi (tests, BO-357): the two track values of
     * the create request (first and sixth of gcre's PARAMS), every flip on the
     * mode page: its Continue stores the page's pick, the request goes out
     * flips later. Runs on one track compare. */
    if (s_role == 1 && s_v.sc == OB_SC_MODE) {
        const char *e = getenv("TAKEDAWN_ONLINEBOT_TRACK");
        int lo, hi;
        if (e && sscanf(e, "%d,%d", &lo, &hi) == 2) {
            MEM32(OB_CREATE_REQ + 0x0Cu) = (uint32_t)lo;
            MEM32(OB_CREATE_REQ + 0x20u) = (uint32_t)hi;
        }
    }
#endif
    if (s_role == 3 || s_stopped)
        return;
    if (s_now < s_next_ms || s_now - s_key_since < OB_SETTLE_MS || flip - s_key_flip < 20)
        return;
    /* never into a page change (a press there lands on whichever page wins) */
    if (s_v.busy && !s_v.popup) {
        if (!s_busy_since)
            s_busy_since = s_now;
        if (s_now - s_busy_since < 20000)
            return;
    } else {
        s_busy_since = 0;
    }
    if (s_commit_sc >= 0) {
        if (s_v.sc == s_commit_sc && (s_now < s_attempt_wait || (int32_t)(flip - s_attempt_flip) < 0))
            return;
        s_commit_sc = -1;
    }
    ob_act();
}

void bo3_onlinebot_start(void)
{
    const char *v = recomp_env(RENV_ONLINEBOT), *e;
    int n;
    if (!v || !*v || !strcmp(v, "0"))
        return;
    s_role = !strcmp(v, "host") ? 1 : !strcmp(v, "guest") ? 2 : 3;
    s_dumpat = recomp_env(RENV_FB_DUMP_AT);
    if (s_dumpat && s_dumpat[0] == '@')
        s_dumpat++;
    else
        s_dumpat = NULL;
    s_dumppfx = recomp_env(RENV_FB_DUMP);
    n = bo3_onlinebot_parse_modes(getenv("TAKEDAWN_ONLINEBOT_MODES"), s_modes, 16);
    if (n)
        s_nmodes = n;
    if ((e = getenv("TAKEDAWN_ONLINEBOT_GUESTS")) && atoi(e) > 0)
        s_guests = atoi(e);
    if ((e = getenv("TAKEDAWN_ONLINEBOT_JOINWAIT")) && atoi(e) > 0)
        s_joinwait_ms = (unsigned)atoi(e) * 1000u;
    if ((e = getenv("TAKEDAWN_ONLINEBOT_GAMES")))
        s_games = atoi(e);
    if ((e = getenv("TAKEDAWN_ONLINEBOT_RACES")))
        s_max_races = atoi(e);
    if ((e = getenv("TAKEDAWN_ONLINEBOT_LEAVE_AFTER")))
        s_leave_after = atoi(e);
    if ((e = getenv("TAKEDAWN_ONLINEBOT_HOST")))
        snprintf(s_host, sizeof s_host, "%s", e);
    s_plans[0] = s_role == 2 ? OB_PLAN_CODE : OB_PLAN_CREATE;
    n = bo3_onlinebot_parse_plans(getenv("TAKEDAWN_ONLINEBOT_PLAN"), s_plans, 16);
    if (n)
        s_nplans = n;
    if ((e = getenv("TAKEDAWN_ONLINEBOT_CODE"))) {
        if (strlen(e) == CE_LEN && strspn(e, bo3_ce_alphabet) == CE_LEN)
            snprintf(s_code, sizeof s_code, "%s", e);
        else
            OB_LOG("TAKEDAWN_ONLINEBOT_CODE %s: not five letters of the entry's alphabet, %s stays", e, s_code);
    }
    if ((e = getenv("TAKEDAWN_ONLINEBOT_VOTE")) && !strcmp(e, "none"))
        s_vote_none = 1;
    if ((e = getenv("TAKEDAWN_ONLINEBOT_WAITFOR")) && atoi(e) > 0)
        s_waitfor_s = (unsigned)atoi(e);
    if ((e = getenv("TAKEDAWN_ONLINEBOT_START_DELAY")) && atoi(e) > 0)
        s_start_delay_s = (unsigned)atoi(e);
    if ((e = getenv("TAKEDAWN_ONLINEBOT_CANCEL_AFTER")) && atoi(e) > 0)
        s_cancel_after_s = (unsigned)atoi(e);
    if ((e = getenv("TAKEDAWN_ONLINEBOT_QUIT_RESULTS")) && atoi(e) > 0)
        s_quit_results = 1;
    if ((e = getenv("TAKEDAWN_ONLINEBOT_RESULTS_HOLD")) && atoi(e) > 0)
        s_results_hold_s = (unsigned)atoi(e);
    if ((e = getenv("TAKEDAWN_ONLINEBOT_RESULTS_CANCEL")) && atoi(e) > 0)
        s_results_cancel = 1;
    if ((e = getenv("TAKEDAWN_ONLINEBOT_POPUP_HOLD")) && atoi(e) > 0)
        s_popup_hold_s = (unsigned)atoi(e);
    if ((e = getenv("TAKEDAWN_ONLINEBOT_RACE_GATE")) && atoi(e) > 0)
        s_race_gate = 1;
    if ((e = getenv("TAKEDAWN_ONLINEBOT_WAIT_FRAMES")) && atoi(e) > 0)
        s_wf_period = (unsigned)atoi(e);
    if ((e = getenv("TAKEDAWN_ONLINEBOT_PICKS"))) {
        const char *p = e;
        while (*p && s_npicks < OB_PICKS) {
            size_t l = strcspn(p, ",|");
            if (l && l < sizeof s_picks[0])
                snprintf(s_picks[s_npicks++], sizeof s_picks[0], "%.*s", (int)l, p);
            p += l;
            if (*p == ',' || *p == '|')
                p++;
        }
    }
    if ((e = getenv("TAKEDAWN_ONLINEBOT_PAGE_HOLD")) && atoi(e) > 0)
        s_page_hold_s = (unsigned)atoi(e);
    if ((e = getenv("TAKEDAWN_ONLINEBOT_PICK_WAIT")) && atoi(e) > 0)
        s_pick_wait_s = (unsigned)atoi(e);
    s_start_ms = ob_ms();
    s_rng = (uint32_t)time(NULL) ^ ((uint32_t)ob_ms() << 8) ^ ((uint32_t)xnet_hle_port_offset() << 16);
    if (s_role != 3)
        usb_gamepad_live_enable();
    {
        char ms[64];
        int i, o = 0;
        ms[0] = 0;
        for (i = 0; i < s_nmodes && o < (int)sizeof ms - 4; i++)
            o += snprintf(ms + o, sizeof ms - (size_t)o, "%s%d", i ? "," : "", s_modes[i]);
        OB_LOG("on: %s; modes %s, guests %d (join wait %u s), games %d%s%s", v, ms, s_guests, s_joinwait_ms / 1000u,
               s_games, s_host[0] ? ", host " : "", s_host);
        ms[0] = 0;
        for (i = o = 0; i < s_nplans && o < (int)sizeof ms - 8; i++)
            o += snprintf(ms + o, sizeof ms - (size_t)o, "%s%s", i ? "," : "", bo3_onlinebot_plan_name(s_plans[i]));
        OB_LOG("plans %s, code %s, vote %s, races %d, leave after %d", ms, s_code, s_vote_none ? "none" : "random",
               s_max_races, s_leave_after);
        OB_LOG("race wait: wait for a race %u s, start delay %u s, cancel after %u s, quit on the results %d", s_waitfor_s,
               s_start_delay_s, s_cancel_after_s, s_quit_results);
    }
}

const char *bo3_onlinebot_code(void)
{
    return s_role == 1 || s_role == 2 ? s_code : NULL;
}

#endif /* ONLINEBOT_TABLE_ONLY */
