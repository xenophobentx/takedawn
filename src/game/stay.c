/*
 * stay.c - "stay together" after an online race (stay.h).
 */
#include "stay.h"

#include <stdio.h>
#include <string.h>

/* ---- the pure decisions (tests/stay) ---- */

static int stay_off_chain(uint32_t flow)
{
    /* places the chain after a race never passes: the player left it (the
     * results' Quit, a failed sign-in back to the online page, the main menu)
     * or another game began */
    return flow == STAY_FLOW_ONLINE || flow == STAY_FLOW_MAIN || flow == STAY_FLOW_LOBBY ||
           flow == STAY_FLOW_QUICK || flow == STAY_FLOW_CREATE;
}

int bo3_stay_step(bo3_stay *s, const bo3_stay_in *in)
{
    if (s->state == STAY_ARMED) {
        if (in->now - s->armed_at > STAY_MAX_MS) {
            s->state = STAY_IDLE;
            return STAY_EV_TIMEOUT;
        }
        if (stay_off_chain(in->flow)) {
            s->state = STAY_IDLE;
            return STAY_EV_CANCEL;
        }
        return STAY_EV_NONE;
    }
    if (in->flow == STAY_FLOW_LOBBY && in->record) {
        int ev = s->state == STAY_LOBBY ? STAY_EV_NONE : STAY_EV_LOBBY;
        if (s->state != STAY_LOBBY)
            s->started = 0;
        s->state = STAY_LOBBY;
        s->role = in->host ? STAY_ROLE_HOST : STAY_ROLE_GUEST;
        if (in->sysflags & STAY_SYS_STARTED)
            s->started = 1;
        if (in->name && in->name[0]) {
            snprintf(s->name, sizeof s->name, "%s", in->name);
            s->usable = !in->password;
        } else {
            s->name[0] = 0;
            s->usable = 0;
        }
        return ev;
    }
    if (s->state == STAY_LOBBY && in->flow != STAY_FLOW_LOBBY) {
        /* the lobby's start case goes to the loading screen once the game is
         * under way; anything else is a player leaving the lobby */
        if (in->prev == STAY_FLOW_LOBBY && in->flow == STAY_FLOW_LOAD && s->started && s->usable &&
            s->role != STAY_ROLE_NONE) {
            s->state = STAY_ARMED;
            s->armed_at = in->now;
            return STAY_EV_ARMED;
        }
        s->state = STAY_IDLE;
        return STAY_EV_LEFT;
    }
    return STAY_EV_NONE;
}

int bo3_stay_room(const bo3_stay *s, uint32_t event, int title_path)
{
    /* event 3 builds the room page, or shows the points first; title_path is
     * what the points state's end does otherwise than come back here */
    if (s->state != STAY_ARMED || event != 3u || title_path)
        return STAY_ROOM_TITLE;
    return s->role == STAY_ROLE_HOST ? STAY_ROOM_HOST : s->role == STAY_ROLE_GUEST ? STAY_ROOM_GUEST : STAY_ROOM_TITLE;
}

int bo3_stay_wait_on(int on, const bo3_stay *s, uint32_t flow, unsigned lobby_ms)
{
    /* on from the results (nothing there reads the strings), off where the
     * chain ends, well or not */
    if (flow == STAY_FLOW_LOBBY)
        return on && lobby_ms < STAY_WAIT_LOBBY_MS;
    if (flow == STAY_FLOW_ONLINE || flow == STAY_FLOW_MAIN)
        return 0;
    if (s->state == STAY_ARMED && flow == STAY_FLOW_RESULTS)
        return 1;
    return on;
}

int bo3_stay_wait_keep(int on, int ev, int room_act, uint32_t room_event)
{
    if (ev == STAY_EV_TIMEOUT || ev == STAY_EV_CANCEL)
        return 0;
    if (room_act == STAY_ROOM_TITLE && room_event == 3u)
        return 0;
    return on;
}

int bo3_stay_results_quit(const bo3_stay *s, uint32_t flow, uint32_t phase, uint32_t code)
{
    return s->state == STAY_ARMED && flow == STAY_FLOW_RESULTS && phase == 3u && code == STAY_RESULTS_QUIT;
}

int bo3_stay_results_release(int host_left, uint32_t flow, int have_mode, uint8_t go)
{
    return host_left && flow == STAY_FLOW_RESULTS && have_mode && !go;
}

int bo3_stay_connect_stuck(int wait_on, uint32_t flow, uint32_t phase, int on_connect_page, unsigned long long ms)
{
    return wait_on && flow == STAY_FLOW_SIGNIN && phase == 5u && on_connect_page && ms >= STAY_CONNECT_MS;
}

int bo3_stay_save_done(int wait_on, uint32_t flow, uint32_t phase, uint32_t page_state)
{
    return wait_on && flow == STAY_FLOW_AUTOSAVE && phase == 5u && page_state == STAY_SAVE_DONE;
}

int bo3_stay_key(const uint8_t *body, int n, const char *key, char *out, int cap)
{
    int kl = (int)strlen(key), i, o;
    for (i = 0; i + kl + 1 <= n; i++) {
        if ((i && body[i - 1] != '\n' && body[i - 1] != 0) || memcmp(body + i, key, (size_t)kl) || body[i + kl] != '=')
            continue;
        for (o = 0, i += kl + 1; i < n && body[i] != '\n' && body[i] && o + 1 < cap; i++)
            out[o++] = (char)body[i];
        if (cap > 0)
            out[o] = 0;
        return 1;
    }
    if (cap > 0)
        out[0] = 0;
    return 0;
}

#ifndef STAY_PURE_ONLY

/* ---- the glue ---- */

#include <stdlib.h>
#include <time.h>

#include "languages.h"
#include "platform/recomp_patch.h"
#include "quickplay.h"
#include "racewait.h"
#include "recomp_types.h"
#include "region.h"
#include "statustext.h"
#include "xnet_hle.h"

extern RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi, g_ebp, g_seh_ebp;
extern ptrdiff_t g_xbox_mem_offset;
extern const char *xbox_SettingsText(const char *key, int *locked);
extern void BO3_SUB(sub_000146E0, sub_000147A0, sub_000147A0)(void);    /* the flow's transition (machine, hash lo, hi; edi context) */
extern void BO3_SUB(sub_0001BE60, sub_0001BF80, sub_0001BF70)(void);    /* the profile's derived values (eax = the profile) */
extern void BO3_SUB(sub_001D2879, sub_001D2B99, sub_001D2C39)(void);    /* the title's allocator */

#define ST_FLOW         BO3_VA(0x00567170u, 0x00567490u, 0x00567530u)
#define ST_SESSION      BO3_VA(0x0057BCFCu, 0x0057C00Cu, 0x0057C0ACu)
#define ST_STAGE        BO3_VA(0x004D5370u, 0x004D5690u, 0x004D5730u)  /* [it] + 0x1B8 = the race's mode object */
#define ST_GAMERTAG     BO3_VA(0x0057C084u, 0x0057C394u, 0x0057C434u)
#define ST_POINTS       BO3_VA(0x00463D4Bu, 0x0046406Bu, 0x0046410Bu)  /* the room shows the points first */
#define ST_LOBBY_REQ    BO3_VA(0x0055DE80u, 0x0055E1A0u, 0x0055E240u)  /* the lobby's options: the game's request */
#define ST_STRTAB       BO3_VA(0x004D532Cu, 0x004D564Cu, 0x004D56ECu)  /* [[it] + 0xC] = the string pointers */
#define ST_PROFILE_PTR  BO3_VA(0x0044CFBCu, 0x0044D2DCu, 0x0044D37Cu)
#define ST_PROFILE      BO3_VA(0x0044CFC8u, 0x0044D2E8u, 0x0044D388u)
#define ST_PROFILE_SHOWN BO3_VA(0x0044D4B8u, 0x0044D7D8u, 0x0044D878u) /* what the points pages counted up from */
#define ST_AFTER_POINTS BO3_VA(0x004A1D44u, 0x004A2064u, 0x004A2104u)  /* the points' end goes elsewhere (0x64A95) */
#define ST_NEXT_RACE    BO3_VA(0x004D426Bu, 0x004D458Bu, 0x004D462Bu)  /* the points' end starts a race (0x64ACE) */
#define ST_SAVE_PAGE    BO3_VA(0x0055C504u, 0x0055C824u, 0x0055C8C4u)
#define ST_MGR_PHASE    BO3_VA(0x00557A64u, 0x00557D84u, 0x00557E24u)
#define ST_MGR_CODE     (ST_MGR_PHASE + 4u)
#define ST_MGR_CTX      (ST_MGR_PHASE + 8u)
#define ST_MGR_PAGE     (ST_MGR_PHASE + 0xCu)
#define ST_CONNECT_PAGE BO3_VA(0x0055FBD0u, 0x0055FEF0u, 0x0055FF90u)
#define ST_EXIT_NO_SERVER 0x15u         /* its session failed (0x96761): the sign-in state's box (0x6E680) */
#define ST_MGR_FADE     BO3_VA(0x00557A5Cu, 0x00557D7Cu, 0x00557E1Cu)
#define ST_MGR_FLAG     BO3_VA(0x0055927Fu, 0x0055959Fu, 0x0055963Fu)
#define ST_FADE_SRC     BO3_VA(0x00557838u, 0x00557B58u, 0x00557BF8u)
#define ST_EXIT_CTX     BO3_VA(0x004AED44u, 0x004AF064u, 0x004AF104u)
#define ST_CAR_OUT      BO3_VA(0x00559650u, 0x00559970u, 0x00559A10u)  /* the car page's choice (lo, hi) */
#define ST_COLOUR_OUT   BO3_VA(0x00559670u, 0x00559990u, 0x00559A30u)
#define ST_CAR          BO3_VA(0x00463B70u, 0x00463E90u, 0x00463F30u)  /* the same, committed (0x6DA30) */
#define ST_COLOUR       BO3_VA(0x00463BB0u, 0x00463ED0u, 0x00463F70u)
#define ST_LOBBY_STEP   (ST_FLOW + STAY_FLOW_LOBBY + 0x18u)            /* 1 once the car is chosen */
#define ST_LINE_TEXT     BO3_VA(0x00550C38u, 0x00550F58u, 0x00550FF8u)  /* the line along the bottom's text widget */
#define ST_HEADING      BO3_VA(0x005509C8u, 0x00550CE8u, 0x00550D88u)  /* the small heading's widget */
#define ST_HASH_HI      0x94414018u
#define ST_HASH_ONLINE  0xF8512871u     /* +0x5B0, the online page */
#define ST_HASH_CREATE  0xF84B7CB7u     /* +0x5D0, the room's CREATE GAME (0x6EFB8) */
#define ST_QUICK_LO     0x116D104Fu     /* +0x808, the online page's Quick Match (sub_0006E950 case 0) */
#define ST_QUICK_HI     0x9441401Bu
#define ST_JOIN_WAIT_MS 20000ull        /* a guest looks for the host's game this long */

#define ST_LOG(...) do { fprintf(stderr, "[STAY] " __VA_ARGS__); fputc('\n', stderr); fflush(stderr); } while (0)

static bo3_stay s_stay;
static uint32_t s_prev_flow;
static uint32_t s_req[QP_STAY_REQ_WORDS];   /* the host's lobby request, kept while in the lobby */
static int s_req_ok;
static int s_qp_game;                       /* the game was one QUICK PLAY made (its start countdown) */
static unsigned long long s_log_until;      /* flow changes are logged until then */
static uint32_t s_car[3];                   /* the car (lo, hi) and colour the lobby committed */
static int s_car_ok, s_car_skip;
static int s_to_online;                     /* the title's box said the game is gone: the room leads to the online page */
static int s_wait;                          /* the one wait message is up */
static unsigned long long s_lobby_at;       /* the lobby came up (0: not in it) */
static unsigned long long s_signin_at;      /* the sign-in state began (0: not in it) */
static uint32_t s_wait_block;               /* its text, in the title's heap */
static int s_ident;                         /* the game's IDENT in its lobby (the race a guest asks about) */
/* what the lobby server said (bo3_stay_lobby_scan, on the title's lobby reads) */
static int s_coming;                        /* TKCOMING of our game's member record */
static int s_wait_host, s_wait_why;         /* TKWAITHOST, TKWAITWHY of the last gsea answer */
static volatile int s_quit_note;            /* quit on the results: the next sign-in says so (the lobby send's thread takes it) */
static char s_host_name[32];                /* the host of the game we are in (its lobby record's HOST) */
static int s_host_left;                     /* that host said it quit on its results (bo3_stay_peer_left) */
static int s_left_logged;                   /* the results' box was let go for it */
static unsigned long long s_left_again_at;  /* our own "left" word goes once more then (0: not due) */

static int st_ok_va(uint32_t va) { return va >= 0x10000u && va < 0x04000000u; }
static void st_pools(void);

static unsigned long long st_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000ull + (unsigned long long)ts.tv_nsec / 1000000ull;
}

/* a NUL-terminated title string of at most n - 1 bytes */
static void st_str(char *out, size_t n, uint32_t va)
{
    size_t i;
    for (i = 0; i + 1 < n && st_ok_va(va + (uint32_t)i) && MEM8(va + (uint32_t)i); i++)
        out[i] = (char)MEM8(va + (uint32_t)i);
    out[i] = 0;
}

/* ---- the one wait message ----
 *
 * While it is on, the string slots the pages on the way read hold the message.
 * An online page reads one slot for two things, its message box and the line
 * along the bottom: bo3_stay_line_first gives the line a space, and
 * bo3_stay_heading the small heading at the top right. */
static const uint16_t k_wait_text[] = {
    2239, 2242, 415,    /* AUTOSAVING, AUTOSAVE SUCCESSFUL */
    193, 2241,          /* LOADING */
    102, 104, 105,      /* CONNECTING, ACCESSING NETWORK */
    162, 163,           /* SIGNING IN */
    1243, 1244,         /* UPDATE RANKINGS */
    111,                /* RETRIEVING LIST OF GAMES */
    112, 114,           /* CREATING GAME, JOINING GAME */
    926,                /* UPDATING PLAYER OPTIONS (the lobby's first moment) */
};
#define ST_WAIT_CHARS 48u
#define ST_N(a) (sizeof(a) / sizeof((a)[0]))
static uint32_t s_saved_text[ST_N(k_wait_text)], s_slots;

static uint32_t st_slots(void)
{
    uint32_t t = MEM32(ST_STRTAB);
    return st_ok_va(t & 0x03FFFFFFu) ? MEM32(t + 0xCu) : 0u;
}

static void st_wait_set(int on)
{
    uint32_t k;
    if (on == s_wait)
        return;
    if (on) {
        const unsigned char *text = (const unsigned char *)bo3_statustext(
            bo3_language_index(xbox_SettingsText("game.language", NULL)), BO3_TXT_RETURNING);
        uint32_t n = 0;
        if (!s_wait_block) {
            uint32_t a = g_eax, c = g_ecx, d = g_edx, sp = g_esp;
            PUSH32(g_esp, 0x54415953u);
            PUSH32(g_esp, (ST_WAIT_CHARS + 2u) * 2u);
            PUSH32(g_esp, 0u);
            BO3_SUB(sub_001D2879, sub_001D2B99, sub_001D2C39)();
            s_wait_block = g_eax;
            g_eax = a; g_ecx = c; g_edx = d; g_esp = sp;
        }
        s_slots = st_slots();
        if (!s_wait_block || !s_slots)
            return;
        for (k = 0; n + 1u < ST_WAIT_CHARS && text[k]; n++) {   /* UTF-8, up to three bytes */
            uint32_t ch = text[k++];
            if (ch >= 0xE0u && text[k] && text[k + 1])
                ch = (ch & 0x0Fu) << 12 | (uint32_t)(text[k] & 0x3Fu) << 6 | (text[k + 1] & 0x3Fu), k += 2;
            else if (ch >= 0xC0u && text[k])
                ch = (ch & 0x1Fu) << 6 | (text[k++] & 0x3Fu);
            MEM16(s_wait_block + n * 2u) = (uint16_t)ch;
        }
        MEM16(s_wait_block + n * 2u) = 0;
        MEM16(s_wait_block + ST_WAIT_CHARS * 2u) = 0x20u;   /* the blank: a title text needs a character */
        MEM16(s_wait_block + ST_WAIT_CHARS * 2u + 2u) = 0;
        for (k = 0; k < ST_N(k_wait_text); k++) {
            s_saved_text[k] = MEM32(s_slots + k_wait_text[k] * 4u);
            MEM32(s_slots + k_wait_text[k] * 4u) = s_wait_block;
        }
    } else {
        for (k = 0; k < ST_N(k_wait_text); k++)
            MEM32(s_slots + k_wait_text[k] * 4u) = s_saved_text[k];
    }
    s_wait = on;
    ST_LOG("the wait message is %s", on ? "up" : "down");
}

void bo3_stay_wait_done(void)
{
    if (g_xbox_mem_offset)
        st_wait_set(0);
    s_car_skip = 0;
    s_stay.state = STAY_IDLE;
}

/* One read of the lobby stream. The title reads a packet's 12-byte header
 * and its body separately, so the keys are looked for in whatever came: a
 * waiter's gsea answer has TKWAITSTATE (and TKWAITHOST / TKWAITWHY with it),
 * the member record of a game made again after a race has TKCOMING. */
void bo3_stay_lobby_scan(const uint8_t *buf, int n)
{
    char v[16];
    if (bo3_stay_key(buf, n, "TKWAITSTATE", v, (int)sizeof v)) {
        s_wait_host = bo3_stay_key(buf, n, "TKWAITHOST", v, (int)sizeof v) && v[0] == '1';
        bo3_stay_key(buf, n, "TKWAITWHY", v, (int)sizeof v);
        s_wait_why = bo3_racewait_stay_why(v);
    }
    if (bo3_stay_key(buf, n, "TKCOMING", v, (int)sizeof v)) {
        int c = atoi(v);
        c = c > 0 && c < 8 ? c : 0;
        if (c != s_coming)
            ST_LOG("players of the race still on the way: %d", c);
        s_coming = c;
    }
}

void bo3_stay_wait_keys(int *host_back, int *why)
{
    if (host_back)
        *host_back = s_wait_host;
    if (why)
        *why = s_wait_why;
}

int bo3_stay_coming(void)
{
    return s_coming;
}

int bo3_stay_quit_take(void)
{
    if (!s_quit_note)
        return 0;
    s_quit_note = 0;
    return 1;
}

void bo3_stay_peer_left(const char *name, int why)
{
    /* only our host's word counts: the game is over without it. A guest that
     * quit is not waited for by the server; the title's box still waits for
     * it, as it does for a player still racing (that is not cut short) */
    if (s_stay.state != STAY_ARMED || s_stay.role != STAY_ROLE_GUEST || !name || !name[0] ||
        strcmp(name, s_host_name))
        return;
    if (!s_host_left)
        ST_LOG("'%s' (the host) quit on its results (its word, %d)", name, why);
    s_host_left = 1;
}

void bo3_stay_wait_end(void)
{
    if (g_xbox_mem_offset)
        st_wait_set(0);
    s_to_online = 1;
}

/* The autosave page's Continue (sub_0006FDA0 at 0x70075, the save done, state
 * 8): code 2, the context, the fade clock, phase 3. */
static void st_page_exit(uint32_t code, uint32_t ctx)
{
    MEM32(ST_MGR_CODE) = code;
    MEM8(ST_MGR_FLAG) = 0;
    MEM32(ST_MGR_FADE) = MEM32(ST_FADE_SRC);
    MEM32(ST_MGR_CTX) = ctx;
    MEM32(ST_MGR_PHASE) = 3u;
}

static void st_save_exit(void)
{
    st_page_exit(2u, (uint32_t)(int32_t)(int8_t)MEM8(ST_EXIT_CTX));
    ST_LOG("the save is written: the autosave page's Continue");
}

/* TAKEDAWN_TRACK_TRACE=1 (tests, BO-384): a line whenever the race's track
 * (0x463BD8/DC), the lobby's request track (0x55DE80 +0xC/+0x20) or the game
 * record's PARAMS change, with the flow state and the lobby screen's state */
#define ST_RACE_TRACK   BO3_VA(0x00463BD8u, 0x00463EF8u, 0x00463F98u)
#define ST_LOBBY_STATE  BO3_VA(0x0055DF4Cu, 0x0055E26Cu, 0x0055E30Cu)  /* the lobby screen 0x55CBA0 + 0x13AC */
static void st_track_trace(uint32_t cur, uint32_t rec)
{
    static int on = -1;
    static uint32_t last[4];
    static char last_p[64];
    uint32_t now[4];
    char p[64];
    if (on < 0)
        on = getenv("TAKEDAWN_TRACK_TRACE") && atoi(getenv("TAKEDAWN_TRACK_TRACE")) > 0;
    if (!on)
        return;
    now[0] = MEM32(ST_RACE_TRACK);
    now[1] = MEM32(ST_RACE_TRACK + 4u);
    now[2] = MEM32(ST_LOBBY_REQ + 0xCu);
    now[3] = MEM32(ST_LOBBY_REQ + 0x20u);
    p[0] = 0;
    if (st_ok_va(rec))
        st_str(p, sizeof p, rec + 0x8Cu);
    if (memcmp(now, last, sizeof now) || strcmp(p, last_p)) {
        ST_LOG("track trace: race %08X:%08X request %08X:%08X record '%s' (flow %X, lobby state %02X)", now[0], now[1],
               now[2], now[3], p, cur, MEM8(ST_LOBBY_STATE));
        memcpy(last, now, sizeof now);
        snprintf(last_p, sizeof last_p, "%s", p);
    }
}

void bo3_stay_flip(void)
{
    bo3_stay_in in;
    char name[32], host[32], me[32], pass[20];
    uint32_t cur, rec;
    int ev;
    if (!g_xbox_mem_offset || !xnet_hle_started())
        return;
    cur = MEM32(ST_FLOW + 4u);
    cur = st_ok_va(cur) ? cur - ST_FLOW : 0;
    rec = MEM32(ST_SESSION + 0x38u);
    st_track_trace(cur, rec);
    memset(&in, 0, sizeof in);
    in.flow = cur;
    in.prev = s_prev_flow;
    in.now = st_ms();
    name[0] = 0;
    if (cur == STAY_FLOW_LOBBY && st_ok_va(rec)) {
        /* the game record (FLOW-RE / step-4 notes): +4 NAME, +0x38 HOST,
         * +0x114 SYSFLAGS */
        in.record = 1;
        in.sysflags = MEM32(rec + 0x114u);
        st_str(name, sizeof name, rec + 4u);
        st_str(host, sizeof host, rec + 0x38u);
        st_str(me, sizeof me, ST_GAMERTAG);
        snprintf(s_host_name, sizeof s_host_name, "%s", host);
        in.host = me[0] && !strcmp(host, me);
        in.name = name;
        if (in.host) {
            /* the lobby's request: what the host's create pages stored, and
             * what its options page changes (+4 the password) */
            uint32_t k, req = ST_LOBBY_REQ;
            if (MEM32(req + 8u)) {
                for (k = 0; k < QP_STAY_REQ_WORDS; k++)
                    s_req[k] = MEM32(req + 4u * k);
                s_req_ok = 1;
                pass[0] = 0;
                if (st_ok_va(MEM32(req + 4u)))
                    st_str(pass, sizeof pass, MEM32(req + 4u));
                in.password = pass[0] != 0;
            } else {
                in.password = 1;        /* not filled in: the game is not made again */
            }
            s_qp_game = bo3_quickplay_host_game();
        }
        /* the car: the race start copies every player's car over it (0x6DB12),
         * so it is read before the game is under way */
        if (MEM8(ST_LOBBY_STEP) == 1u && !(in.sysflags & STAY_SYS_STARTED) &&
            (MEM32(ST_CAR) | MEM32(ST_CAR + 4u))) {
            s_car[0] = MEM32(ST_CAR);
            s_car[1] = MEM32(ST_CAR + 4u);
            s_car[2] = MEM32(ST_COLOUR);
            s_car_ok = 1;
        }
    }
    if (cur != s_prev_flow && (s_stay.state == STAY_ARMED || in.now < s_log_until))
        ST_LOG("flow %X -> %X", s_prev_flow, cur);
    ev = bo3_stay_step(&s_stay, &in);
    if (ev == STAY_EV_LOBBY) {
        ST_LOG("in the lobby of '%s' as the %s", s_stay.name, s_stay.role == STAY_ROLE_HOST ? "host" : "guest");
        st_pools();
        s_quit_note = 0;
    }
    if (cur == STAY_FLOW_LOBBY && in.record) {
        xnet_hle_lobby_view lv;
        if (xnet_hle_lobby_view_get(&lv) && lv.game > 0)
            s_ident = lv.game;
    } else if (cur == STAY_FLOW_ONLINE || cur == STAY_FLOW_MAIN || cur == STAY_FLOW_ROOM)
        s_coming = 0;               /* a word about a game we are no longer in */
    else if (ev == STAY_EV_ARMED)
        ST_LOG("the race of '%s' started: after it the %s goes back to its lobby", s_stay.name,
               s_stay.role == STAY_ROLE_HOST ? "host makes the game again and" : "guest joins it again,");
    else if (ev == STAY_EV_LEFT)
        ST_LOG("the lobby was left (flow %X -> %X)%s", s_prev_flow, cur,
               s_stay.started && !s_stay.usable ? ": a game with a password is not made again" : "");
    else if (ev == STAY_EV_CANCEL)
        ST_LOG("not going back to the lobby: the player went elsewhere (flow %X)", cur);
    else if (ev == STAY_EV_TIMEOUT)
        ST_LOG("not going back to the lobby: the race and its results took too long");
    if (!bo3_stay_wait_keep(s_wait, ev, -1, 0u))
        st_wait_set(0);
    if (cur == STAY_FLOW_ONLINE || cur == STAY_FLOW_MAIN)
        s_car_skip = s_to_online = 0;
    if (ev == STAY_EV_ARMED || cur == STAY_FLOW_ONLINE || cur == STAY_FLOW_MAIN || cur == STAY_FLOW_LOBBY)
        s_host_left = s_left_logged = 0;
    /* the results' Quit, Yes: this player leaves, the title's own way */
    if (bo3_stay_results_quit(&s_stay, cur, MEM32(ST_MGR_PHASE), MEM32(ST_MGR_CODE))) {
        s_stay.state = STAY_IDLE;
        s_car_skip = 0;
        st_wait_set(0);
        s_quit_note = 1;            /* the group does not wait for us (TKSTAYQUIT on the sign-in) */
        /* and the others hear it now over the game's links (TKRX "left"): a
         * guest's results box need not wait for us; once more in case one
         * datagram is lost (the links stay until our next sign-in) */
        xnet_hle_react_send(XR_REACT_LEFT, XR_LEFT_RESULTS);
        s_left_again_at = in.now + 1600u;
        ST_LOG("the player quit on the results: not going back to the lobby");
    }
    if (s_left_again_at && in.now >= s_left_again_at) {
        s_left_again_at = 0;
        xnet_hle_react_send(XR_REACT_LEFT, XR_LEFT_RESULTS);
    }
    /* our host quit on its results: the box WAITING FOR OTHER PLAYERS is let
     * go now, by the race mode's own "go on" flag (stay.h) */
    if (s_host_left && cur == STAY_FLOW_RESULTS) {
        uint32_t stage = MEM32(ST_STAGE), mode = st_ok_va(stage) ? MEM32(stage + 0x1B8u) : 0;
        int have = st_ok_va(mode);
        if (bo3_stay_results_release(s_host_left, cur, have, have ? MEM8(mode + STAY_MODE_GO) : 0u)) {
            if (!s_left_logged)
                ST_LOG("the host quit on its results: the results' wait for the others ends now");
            s_left_logged = 1;
            MEM32(mode + 0x44u) = 0xFFFFFFFFu;
            MEM8(mode + STAY_MODE_GO) = 1u;
        }
    }
    /* the lobby server is gone (on a local network the host's copy runs it):
     * the connecting page tries for ever, the player must not wait for ever */
    if (cur != STAY_FLOW_SIGNIN)
        s_signin_at = 0;
    else if (!s_signin_at)
        s_signin_at = in.now;
    if (bo3_stay_connect_stuck(s_wait, cur, MEM32(ST_MGR_PHASE), MEM32(ST_MGR_PAGE) == ST_CONNECT_PAGE,
                               s_signin_at ? in.now - s_signin_at : 0ull)) {
        s_stay.state = STAY_IDLE;
        s_car_skip = 0;
        s_to_online = 1;
        st_wait_set(0);
        st_page_exit(ST_EXIT_NO_SERVER, 0u);
        ST_LOG("the lobby server does not answer: the connecting page's failure exit");
    }
    if (cur != STAY_FLOW_LOBBY)
        s_lobby_at = 0;
    else if (!s_lobby_at)
        s_lobby_at = in.now;
    st_wait_set(bo3_stay_wait_on(s_wait, &s_stay, cur, s_lobby_at ? (unsigned)(in.now - s_lobby_at) : 0u));
    if (bo3_stay_save_done(s_wait, cur, MEM32(ST_MGR_PHASE), MEM32(ST_SAVE_PAGE + 4u)))
        st_save_exit();
    s_prev_flow = cur;
}

/* TAKEDAWN_NET_TRACE: the front end's node pools at every lobby, to see that
 * game after game nothing is lost (the crash0211 pools: the front-end root's
 * free list [[0x557A20 + 0x14] + 0x40], and the shared pools of the ticker
 * widget's level, [[0x557A20 + 0x2C] + 16 L + 4]) */
static int st_pool_len(uint32_t pool)
{
    int n = 0;
    uint32_t q;
    if (!st_ok_va(pool))
        return -1;
    for (q = MEM32(pool + 0x10u); q && n < 100000; q = MEM32(q), n++)
        if (!st_ok_va(q))
            return -2 - n;
    return n;
}

static void st_pools(void)
{
    static int on = -1;
    uint32_t fe = BO3_VA(0x00557A20u, 0x00557D40u, 0x00557DE0u), la, root, L;
    if (on < 0) {
        const char *e = getenv("TAKEDAWN_NET_TRACE");
        on = e && *e && *e != '0';
    }
    if (!on)
        return;
    la = MEM32(fe + 0x2Cu);
    root = MEM32(fe + 0x14u);
    L = (uint32_t)(int32_t)(int16_t)MEM16(BO3_VA(0x005509C8u, 0x00550CE8u, 0x00550D88u) + 8u);
    if (!st_ok_va(la) || L > 200u)
        return;
    ST_LOG("pools: root %d free, shared %d / %d / %d free", st_ok_va(root) ? st_pool_len(MEM32(root + 0x40u)) : -9,
           st_pool_len(MEM32(la + L * 16u + 4u)), st_pool_len(MEM32(la + (L + 1u) * 16u + 4u)),
           st_pool_len(MEM32(la + (L + 2u) * 16u + 4u)));
}

/* sub_000146E0(machine, lo, hi) with edi the context: the transition the
 * title's own states make; stdcall, it pops its three arguments */
static void st_transition(uint32_t lo, uint32_t hi, uint32_t ctx)
{
    uint32_t a = g_eax, c = g_ecx, d = g_edx, sp = g_esp, b = g_ebx, si = g_esi, di = g_edi, bp = g_ebp, seh = g_seh_ebp;
    PUSH32(g_esp, hi);
    PUSH32(g_esp, lo);
    PUSH32(g_esp, ST_FLOW);
    PUSH32(g_esp, 0);               /* the return address its ret takes */
    g_edi = ctx;
    BO3_SUB(sub_000146E0, sub_000147A0, sub_000147A0)();
    g_eax = a; g_ecx = c; g_edx = d; g_esp = sp; g_ebx = b; g_esi = si; g_edi = di; g_ebp = bp; g_seh_ebp = seh;
}

/* the points state's end (sub_000649E0 at 0x64A6A): the pages only showed what
 * the race stored; this is what they leave behind them */
static void st_points_end(void)
{
    uint32_t a = g_eax, c = g_ecx, d = g_edx, sp = g_esp, b = g_ebx, si = g_esi, di = g_edi, bp = g_ebp, seh = g_seh_ebp;
    int k;
    for (k = 0; k < 2; k++) {       /* before the copy and after it, as 0x64A6F and 0x64AC2 */
        PUSH32(g_esp, 0);
        g_eax = MEM32(ST_PROFILE_PTR);
        BO3_SUB(sub_0001BE60, sub_0001BF80, sub_0001BF70)();
        g_esp = sp;
        if (!k) {
            memcpy((void *)((uintptr_t)g_xbox_mem_offset + ST_PROFILE_SHOWN),
                   (const void *)((uintptr_t)g_xbox_mem_offset + ST_PROFILE), 0x58u * 4u + 2u);
            MEM8(ST_POINTS) = 0;
        }
    }
    g_eax = a; g_ecx = c; g_edx = d; g_ebx = b; g_esi = si; g_edi = di; g_ebp = bp; g_seh_ebp = seh;
}

/* sub_0006DE50 (the state in the stack argument, ret 4), which builds the car
 * page. After a race the car does not change: the page's result goes where its
 * Continue leaves it, and the state's own step 0 (0x6DA28) commits it (0x463B70,
 * the game's USERPARAMS) and builds the lobby page. */
void bo3_stay_car_page(void (*title_fn)(void))
{
    if (!s_car_skip || !s_car_ok || !g_xbox_mem_offset) {
        title_fn();
        return;
    }
    s_car_skip = 0;
    MEM32(ST_CAR_OUT) = s_car[0];
    MEM32(ST_CAR_OUT + 4u) = s_car[1];
    MEM32(ST_COLOUR_OUT) = s_car[2];
    ST_LOG("the car of the race again: %08X:%08X, colour %u", s_car[1], s_car[0], s_car[2]);
    g_esp += 8u;                    /* ret 4 */
    bo3_quickplay_exit_event(0u);
}

/* sub_000C9240 (ecx the widget, the text on the stack): the heading of every
 * page. While the wait message is up the pages have none; the lobby at the end
 * keeps its own. */
void bo3_stay_heading(void (*title_fn)(void))
{
    if (s_wait && g_ecx == ST_HEADING && MEM32(ST_FLOW + 4u) != ST_FLOW + STAY_FLOW_LOBBY)
        MEM32(g_esp + 4u) = s_wait_block + ST_WAIT_CHARS * 2u;
    title_fn();
}

/* sub_000CAD00 (edi the widget, the text and two words on the stack, ret 0xC):
 * a page changes the line along the bottom. While the wait message is up the
 * line stays as it is. */
void bo3_stay_line_text(void (*title_fn)(void))
{
    if (s_wait && g_edi == ST_LINE_TEXT) {
        g_esp += 0x10u;             /* ret 0xC */
        return;
    }
    title_fn();
}

/* sub_000CA090 (eax the widget, ebx the list of its texts): a page's first text
 * for the line along the bottom. */
void bo3_stay_line_first(void (*title_fn)(void))
{
    if (s_wait && g_eax == ST_LINE_TEXT)
        MEM32(g_ebx) = s_wait_block + ST_WAIT_CHARS * 2u;
    title_fn();
}

/* sub_000A4900 (page, row, flag on the stack, ret 0xC), step 5 of the sign-in
 * state after a race. The page only offers the room list; with no page, the
 * exit its Continue gives: code 1, the sign-in state's end, to the room. */
void bo3_stay_lobby_select(void (*title_fn)(void))
{
    if (!s_wait || s_stay.state != STAY_ARMED || MEM32(ST_FLOW + 4u) != ST_FLOW + STAY_FLOW_SIGNIN) {
        title_fn();
        return;
    }
    ST_LOG("the lobby select page is passed over");
    g_eax = (g_eax & ~0xFFu) | 1u;
    g_esp += 0x10u;                 /* ret 0xC */
    bo3_quickplay_exit_event(1u);
}

/* sub_0006EEE0, the room state's handler (thiscall, event / word / code /
 * context on the stack, ret 0x10). Its event 3 builds the room page, or with
 * the points flag goes to the points state first (0x6EF3B, a transition in
 * the same place). After a raced game, instead of those: the points' end, then
 * the host's CREATE GAME (the room's own transition, 0x6EFB8, context 0), a
 * guest's Quick Match (the online page's, context 1: it joins by itself). */
void bo3_stay_room_state(void (*title_fn)(void))
{
    uint32_t ev;
    int act;
    if (!g_xbox_mem_offset) {
        title_fn();
        return;
    }
    ev = MEM32(g_esp + 4u);
    if (s_to_online && ev == 3u) {
        /* the box's Continue leads from Quick Match to the room; the room's own
         * way to the online page (0x6EFD7), before its page is built */
        s_to_online = 0;
        s_car_skip = 0;
        ST_LOG("the game is gone: on to the online page");
        st_transition(ST_HASH_ONLINE, ST_HASH_HI, 0u);
        g_esp += 0x14u;             /* ret 0x10 */
        return;
    }
    act = bo3_stay_room(&s_stay, ev, MEM8(ST_POINTS) && (MEM8(ST_AFTER_POINTS) || (int8_t)MEM8(ST_NEXT_RACE) > 0));
    if (act == STAY_ROOM_TITLE) {
        if (s_wait && !bo3_stay_wait_keep(s_wait, STAY_EV_NONE, act, ev)) {
            ST_LOG("the title builds its own room or points page: the wait message goes");
            st_wait_set(0);         /* before the page reads the text slots */
        }
        title_fn();
        return;
    }
    s_stay.state = STAY_IDLE;
    s_log_until = st_ms() + 120000ull;
    if (act == STAY_ROOM_HOST && !s_req_ok) {
        ST_LOG("the lobby's settings were never read: the room page as the title shows it");
        st_wait_set(0);
        title_fn();
        return;
    }
    if (MEM8(ST_POINTS)) {
        ST_LOG("the points pages are passed over");
        st_points_end();
    }
    s_car_skip = 1;
    if (act == STAY_ROOM_HOST) {
        ST_LOG("back to the lobby: the host makes '%s' again (CREATE GAME, the last game's settings)", s_stay.name);
        bo3_quickplay_stay_create(s_stay.name, s_req, s_qp_game);
        st_transition(ST_HASH_CREATE, ST_HASH_HI, 0u);
    } else {
        ST_LOG("back to the lobby: Quick Match for '%s' (the host makes it again; game %d)", s_stay.name, s_ident);
        bo3_quickplay_stay_join(s_stay.name, s_ident, ST_JOIN_WAIT_MS);
        st_transition(ST_QUICK_LO, ST_QUICK_HI, 1u);
    }
    g_esp += 0x14u;                 /* ret 0x10 */
}

#endif /* STAY_PURE_ONLY */
