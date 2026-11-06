/*
 * quickplay.c - QUICK PLAY through the title's own screens (quickplay.h).
 */
#include "quickplay.h"
#include "racewait.h"

#include <stdio.h>
#include <string.h>

/* ---- the pure decisions (tests/quickplay) ---- */

int bo3_quickplay_decide(const qp_in *in)
{
    if (!in->armed)
        return QP_NONE;
    if (in->creating) {
        /* B: leave at the one point with nothing in flight, the room list in
         * and the room not asked for yet (the room's own answer is turned into
         * Back by bo3_quickplay_recode) */
        if (in->cancel && in->on_search && in->active && in->sub == QP_SUB_ROOMS && in->rooms_done)
            return QP_CANCEL;
        /* the room list never answered: the page goes back to the title's own
         * NO GAMES FOUND, which has its Back */
        if (in->on_search && in->now - in->create_at > QP_ROOM_WAIT_MS &&
            (in->sub == QP_SUB_ROOMS || in->sub == QP_SUB_ROOM))
            return QP_GIVEUP;
        return QP_NONE;
    }
    if (!in->on_search || !in->active)
        return QP_NONE;
    /* BO-365: the race wait has the page (qp_wait_tick, racewait.c) */
    if (in->waiting)
        return QP_NONE;
    /* the game picked refused the join (BO-364): like nothing found */
    if (in->sub == QP_SUB_JOINING && in->join_failed) {
        if (in->cancel)
            return QP_CANCEL;
        if (in->join)
            return QP_REFUSED;
        return in->refusals + 1 >= QP_REFUSE_MAX ? QP_CREATE : QP_RESEARCH;   /* this refusal is one more */
    }
    /* the search is over and kept nothing: NO GAMES FOUND is about to be
     * drawn (state 0 with the results ready) or is up (state 2); with B
     * pressed in the meantime the title's Back exit is taken instead of the
     * create branch */
    if (in->count == 0 && ((in->sub == QP_SUB_WAIT && in->ready) || in->sub == QP_SUB_NONE))
        return in->cancel ? QP_CANCEL : in->racing ? QP_WAIT : in->join ? QP_NOTFOUND : QP_CREATE;
    return QP_NONE;
}

int bo3_quickplay_join_result(int refused, int removed)
{
    return removed ? QP_JOIN_REMOVED : refused ? QP_JOIN_REFUSED : QP_JOIN_MISSED;
}

int bo3_quickplay_room_ask(unsigned long long *asked_ms, unsigned long long now, int done, unsigned count)
{
    unsigned long long since;
    if (*asked_ms) {
        if (done && count)
            return 0;               /* answered: the room is taken */
        since = now - *asked_ms;
        if (done ? since < QP_ROOM_RETRY_MS : since < QP_ROOM_ANSWER_MS)
            return 0;               /* waiting for the answer */
    }
    *asked_ms = now ? now : 1;
    return 1;
}

/* the game made again waited for the room: go on only if it is still being made */
int bo3_quickplay_stay_release(int creating, int in_create_state)
{
    return creating && in_create_state;
}

int bo3_quickplay_recode(int cancel, int creating, unsigned phase, unsigned code)
{
    return cancel && creating && (phase == 3u || phase == 4u) && code == QP_EXIT_CREATE;
}

int bo3_quickplay_countdown(qp_cd *c, const qp_cd_in *in, int *seconds)
{
    unsigned long long el;
    if (seconds)
        *seconds = 0;
    if (!in->host_game || in->players < 2 || !in->idle) {
        c->start_ms = 0;            /* below two players, or not in the plain lobby: it stops */
        return QP_CD_NONE;
    }
    if (!c->start_ms)
        c->start_ms = in->now ? in->now : 1;
    el = in->now > c->start_ms ? in->now - c->start_ms : 0;
    if (el < QP_COUNTDOWN_MS) {
        if (seconds)
            *seconds = (int)((QP_COUNTDOWN_MS - el + 999) / 1000);
        return QP_CD_TICK;
    }
    /* time is up: start when every guest has a car, else wait for them (the
     * line shows nothing then; the title's own text is back) */
    if (!in->guests_ready || in->now < c->hold_until)
        return QP_CD_NONE;
    c->hold_until = in->now + QP_START_COOLDOWN_MS;
    return QP_CD_START;
}

int bo3_quickplay_target_decide(const qp_target_in *in)
{
    if (!in->active)
        return QP_T_NONE;
    if (in->holding) {
        if (in->cancel)
            return QP_T_CANCEL;
        if (in->now >= in->deadline)
            return QP_T_TIMEOUT;
        return in->now >= in->retry_at ? QP_T_RESEARCH : QP_T_NONE;
    }
    /* NO GAMES FOUND is never meant to be up on this path: leave */
    if (in->sub == QP_SUB_NONE)
        return QP_T_TIMEOUT;
    /* the host's game refused the join (BO-364: it had started, or kicked us):
     * waited out like an answer without it */
    if (in->sub == QP_SUB_JOINING && in->join_failed) {
        if (in->cancel)
            return QP_T_CANCEL;
        return in->now >= in->deadline ? QP_T_TIMEOUT : QP_T_HOLD;
    }
    /* an answer without the game: wait, search again */
    if (in->sub == QP_SUB_WAIT && in->ready && in->count == 0) {
        if (in->cancel)
            return QP_T_CANCEL;
        return in->now >= in->deadline ? QP_T_TIMEOUT : QP_T_HOLD;
    }
    return QP_T_NONE;       /* searching, or the title's own join runs */
}

int bo3_quickplay_start_reads_record(int host, unsigned lobby_state, uint32_t sysflags)
{
    return host && lobby_state == 0x11u && (sysflags & QP_SYS_LOCK) && !(sysflags & QP_SYS_STARTED);
}

int bo3_quickplay_name_match(const char *rec, const char *want)
{
    return rec && want && want[0] && !strncmp(rec, want, 31);
}

void bo3_quickplay_stay_request(uint32_t *blk, const uint32_t *saved, uint8_t language)
{
    uint32_t k;
    for (k = 2; k < QP_STAY_REQ_WORDS; k++)     /* +8 limit .. +0x28 */
        blk[k] = saved[k];
    blk[6] = bo3_quickplay_flags18(blk[6], language);
}

void bo3_quickplay_copy_str(uint8_t *dst, size_t n, const uint8_t *src)
{
    size_t i = 0;
    while (i < n && src[i]) {
        dst[i] = src[i];
        i++;
    }
    while (i < n)
        dst[i++] = 0;
}

int bo3_quickplay_mode(int mode, int limit)
{
    if (limit > 6)
        mode = 4;
    if (limit != 2 && (mode == 2 || mode == 3))
        mode = 0;
    return mode;
}

uint32_t bo3_quickplay_flags18(uint32_t w, uint8_t language)
{
    return (w & 0x00FFFFFFu) | ((uint32_t)language << 24);
}

uint32_t bo3_quickplay_exit_ctx(uint32_t code, uint32_t manager_ctx)
{
    return code >= 3u ? 0u : manager_ctx;
}

/* ---- BROWSE GAMES (BO-372), the pure part ---- */

int bo3_quickplay_browse_pick(int state)
{
    return state == QP_BR_RACING ? QP_PICK_WAIT : state == QP_BR_FULL ? QP_PICK_FULL : QP_PICK_JOIN;
}

void bo3_quickplay_browse_label(char *out, size_t n, const char *name, int state, int secs)
{
    char suf[32];
    size_t room, k, sl;
    if (!out || !n)
        return;
    if (secs < 0)
        secs = 0;
    if (secs > 99 * 60 + 59)
        secs = 99 * 60 + 59;
    suf[0] = 0;
    if (n > QP_BR_NAME)
        n = QP_BR_NAME;
    if (state == QP_BR_RACING)
        snprintf(suf, sizeof suf, " - RACING %d:%02d", secs / 60, secs % 60);
    else if (state == QP_BR_FULL)
        snprintf(suf, sizeof suf, " - FULL");
    else if (state == QP_BR_WAITING) {
        snprintf(suf, sizeof suf, " - RACING %d:%02d - WAITING", secs / 60, secs % 60);
        if (strlen(suf) + (name ? strlen(name) : 0) > n - 1)    /* a long name: the short form */
            snprintf(suf, sizeof suf, " - WAITING %d:%02d", secs / 60, secs % 60);
    } else if (state == QP_BR_QUEUED)
        snprintf(suf, sizeof suf, " - WAITING");
    else if (state == QP_BR_JOINING)
        snprintf(suf, sizeof suf, " - JOINING");
    sl = strlen(suf);
    room = n - 1 > sl ? n - 1 - sl : 0;
    for (k = 0; name && name[k] && k < room; k++)
        out[k] = name[k];
    while (k > 0 && out[k - 1] == ' ' && sl)
        k--;                            /* a name cut at a blank: no double blank before the dash */
    snprintf(out + k, n - k, "%s", suf);
}

int bo3_quickplay_browse_wait_label(int rw, int ws, int queue)
{
    /* as the bottom line says it (bo3_racewait_line) */
    if (rw == RW_JOINING || ws == RW_WS_OPEN || ((ws == RW_WS_LOBBY || ws == RW_WS_FULL) && queue <= 0))
        return QP_BR_JOINING;
    if (ws == RW_WS_LOBBY || ws == RW_WS_FULL)
        return QP_BR_QUEUED;
    return QP_BR_WAITING;
}

uint32_t bo3_quickplay_browse_key(int state, int pos)
{
    uint32_t rank = state == QP_BR_RACING ? 1u : state == QP_BR_FULL ? 2u : 0u;
    return rank * 0x100u + (uint32_t)(pos & 0xFF);
}

#ifndef QUICKPLAY_PURE_ONLY

/* ---- the glue ---- */

#include <time.h>

#include "platform/recomp_patch.h"
#include "recomp_types.h"
#include "region.h"
#include "vote.h"
#include "stay.h"
#include "xnet_hle.h"
#include "online.h"
#include "racewait.h"
#include "onlinedesc.h"
#include "languages.h"

extern RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi, g_ebp, g_seh_ebp;
extern ptrdiff_t g_xbox_mem_offset;
extern void BO3_SUB(sub_001BC090, sub_001BC3B0, sub_001BC450)(void);    /* the room list request (thiscall, 2 stack args) */
extern void BO3_SUB(sub_0009AC40, sub_0009AD90, sub_0009AF40)(void);    /* the title's pick of a track key (edx:eax) */
extern void BO3_SUB(sub_000146E0, sub_000147A0, sub_000147A0)(void);    /* the flow's transition (flow, hash low, hash high; edi context) */
extern void BO3_SUB(sub_001BC150, sub_001BC470, sub_001BC510)(void);    /* the room's move (thiscall, the room's id) */
extern void BO3_SUB(sub_00017810, sub_00017930, sub_00017920)(void);    /* the title's "B pressed on any pad" (al, edx = the pads) */
extern void BO3_SUB(sub_00075DB0, sub_00075FB0, sub_00076020)(void);    /* the title's message box */
extern void BO3_SUB(sub_001BB1E0, sub_001BB500, sub_001BB5A0)(void);    /* the session's game search, gsea (thiscall, 5 stack args) */
extern void BO3_SUB(sub_00216460, sub_00216770, sub_00216810)(void);    /* the lobby connection's status block (cdecl: conn, tag) */

#define QP_SEARCH_PAGE   BO3_VA(0x005628E0u, 0x00562C00u, 0x00562CA0u)
#define QP_FLOW          BO3_VA(0x00567170u, 0x00567490u, 0x00567530u)
#define QP_STATE_PTR     (QP_FLOW + 4u)
#define QP_EVENT_W2      (QP_FLOW + 8u)         /* the second word the manager passes in events */
#define QP_MGR_PHASE     BO3_VA(0x00557A64u, 0x00557D84u, 0x00557E24u)
#define QP_MGR_CODE      (QP_MGR_PHASE + 4u)
#define QP_MGR_CTX       (QP_MGR_PHASE + 8u)
#define QP_SESSION       BO3_VA(0x0057BCFCu, 0x0057C00Cu, 0x0057C0ACu)
#define QP_ROOMS_CB      BO3_VA(0x000A9A50u, 0x000A9DA0u, 0x000A9E40u)
#define QP_SEARCH_CB     BO3_VA(0x000A99F0u, 0x000A9D40u, 0x000A9DE0u)
#define QP_NAME_PAGE     BO3_VA(0x0055DF8Cu, 0x0055E2ACu, 0x0055E34Cu)
#define QP_BLOCK         (QP_NAME_PAGE + 4u)    /* the create request the name page overlaps */
#define QP_GAMERTAG      BO3_VA(0x0057C084u, 0x0057C394u, 0x0057C434u)
#define QP_LANGUAGE      BO3_VA(0x0045B9BCu, 0x0045BCDCu, 0x0045BD7Cu)
#define QP_MGR_FLAG      BO3_VA(0x0055927Fu, 0x0055959Fu, 0x0055963Fu)
#define QP_MGR_FADE      BO3_VA(0x00557A5Cu, 0x00557D7Cu, 0x00557E1Cu)
#define QP_FADE_SRC      BO3_VA(0x00557838u, 0x00557B58u, 0x00557BF8u)
#define QP_PADS          BO3_VA(0x004AE20Cu, 0x004AE52Cu, 0x004AE5CCu)
#define QP_ROOM_ID       BO3_VA(0x0056781Cu, 0x00567B3Cu, 0x00567BDCu)  /* the room the title is in */
#define QP_HASH_CREATE   0xF84B7CB7u            /* the create state: the room's CREATE GAME, the Quick Match exit 0x2D */
#define QP_HASH_HI       0x94414018u
#define QP_BOX_PAGE      BO3_VA(0x0055A29Cu, 0x0055A5BCu, 0x0055A65Cu)
#define QP_STR_ERROR      0x81u                 /* the box's heading, as in the lobby */
#define QP_STR_NO_SESSION 99u                   /* "This game session is no longer available" */
#define QP_FLOW_QUICK    0x808u
#define QP_FLOW_CREATE   0x5D0u
#define QP_FLOW_ONLINE   0x5B0u
#define QP_MAX_MS        150000ull

#define QP_LOG(...) do { fprintf(stderr, "[QUICKPLAY] " __VA_ARGS__); fputc('\n', stderr); fflush(stderr); } while (0)
#define RW_LOG(...) do { fprintf(stderr, "[RACEWAIT] " __VA_ARGS__); fputc('\n', stderr); fflush(stderr); } while (0)

static int s_armed, s_creating, s_cancel, s_host_game, s_cd_seconds, s_join, s_join_ret;
static int s_direct;                    /* CREATE GAME is in the create state, from the online page */
static unsigned long long s_direct_at;
static int s_room_step;                 /* the room is being entered behind the mode page */
static unsigned long long s_room_asked; /* the room list was asked for (bo3_quickplay_room_ask) */
static int s_stay_hold;                 /* the game made again waits for the room: its request is ready */
static unsigned long long s_room_until;
/* step 4 (stay.c): a game made again after a race, or looked for */
static int s_stay, s_stay_qp, s_holding, s_box_pending;
static char s_stay_name[32], s_target[32];
static uint32_t s_stay_req[QP_STAY_REQ_WORDS];
static unsigned long long s_deadline, s_retry_at;
static unsigned s_searches, s_seen;
static int s_refusals;                  /* joins refused since QUICK PLAY began (BO-364) */
static qp_cd s_cd;
static rw_state s_wait;                 /* BO-365: waiting for a race under way (racewait.h) */
static rw_stay s_sw;                    /* stay together: the guest's wait for its host's game (racewait.h) */
static int s_stay_ident;                /* the raced game's IDENT the stay-join asks about (TKWAITFOR) */
static int s_wait_ws = -1;              /* the TKWAITSTATE last logged */
static int s_wait_line;                 /* the Quick Match page's line is ours */
static int s_stay_hb;                   /* the stay wait's TKWAITHOST last logged */
/* the race the lobby tag asks for (TKWAITFOR): the tag runs on the lobby
 * send's thread, so it reads this copy, set after every change of s_wait */
static volatile int s_wait_tag;
static void qp_wait_pub(void)
{
    s_wait_tag = s_wait.state == RW_WAIT || s_wait.state == RW_JOINING ? s_wait.race_id
               : s_target[0] && !s_sw.end ? s_stay_ident : 0;
}
static unsigned long long s_armed_at, s_create_at, s_trail_until;
static uint32_t s_last_flow;
/* BO-372: BROWSE GAMES (quickplay.h) */
static int s_browse;                    /* the page is BROWSE GAMES' list */
static volatile int s_br_tag;           /* its searches are browse searches (the lobby tag's copy) */
static int s_br_head;                   /* the page's title is ours until the page is up */
static uint32_t s_br_head_old, s_br_head_str;
static int s_br_act, s_br_act_row;      /* a pick the pad saw: QP_PICK_WAIT / QP_PICK_FULL, its row */
static char s_br_msg[96];               /* the bottom line's message ... */
static unsigned long long s_br_msg_until;   /* ... until then */
static unsigned long long s_br_refresh_at;
static int s_br_keep;                   /* the IDENT the cursor stays on across a search, 0 none */
static int s_br_n, s_br_ident[QP_BR_ROWS], s_br_state[QP_BR_ROWS];
static char s_br_name[QP_BR_ROWS][QP_BR_NAME];
static int s_br_secs[QP_BR_ROWS];       /* a racing row's race time at s_br_since (this copy's clock) */
static unsigned long long s_br_since[QP_BR_ROWS];
static char s_br_shown[QP_BR_ROWS][QP_BR_NAME];     /* the label last written into the row's text */
static int s_br_wait_row = -1;          /* the line of the game a race wait runs for */
static unsigned s_br_records;           /* records the callback saw in this search (logged) */
static struct { int ident, secs; unsigned long long at; } s_br_rec[16];     /* this search's race times */
static void qp_br_pub(void) { s_br_tag = s_browse && s_wait.state == RW_OFF; }
static void qp_br_head(int ours);
static void qp_br_say(const char *t);
static void qp_br_relist(uint32_t page);

static unsigned long long qp_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000ull + (unsigned long long)ts.tv_nsec / 1000000ull;
}

static int qp_ok_va(uint32_t va) { return va >= 0x10000u && va < 0x04000000u; }

int bo3_quickplay_armed(void) { return s_armed; }
int bo3_quickplay_host_game(void) { return s_host_game; }

void bo3_quickplay_begin_join(void)
{
    bo3_quickplay_begin();
    s_join = 1;
    QP_LOG("JOIN WITH CODE: the title's Quick Match runs with the code");
}

int bo3_quickplay_create_direct(void) { return s_direct; }
int bo3_quickplay_room_pending(void) { return s_room_step != CR_ROOM_NONE; }

int bo3_quickplay_join_return(void)
{
    int r = s_join_ret;
    s_join_ret = QP_JOIN_NONE;
    return r;
}

void bo3_quickplay_begin(void)
{
    s_join = 0;
    s_join_ret = QP_JOIN_NONE;
    s_refusals = 0;
    (void)bo3_online_removed_take();    /* an older answer's word is not this search's */
    s_armed = 1;
    s_creating = 0;
    s_cancel = 0;
    s_host_game = 0;
    s_cd_seconds = 0;
    memset(&s_cd, 0, sizeof s_cd);
    s_stay = 0;
    s_target[0] = 0;
    s_holding = 0;
    memset(&s_wait, 0, sizeof s_wait);
    qp_wait_pub();
    s_browse = 0;
    qp_br_pub();
    s_armed_at = qp_ms();
    s_trail_until = 0;
    QP_LOG("QUICK PLAY: the title's Quick Match runs");
}

static void qp_wait_quiet(int on)
{
#ifdef XNET_HLE_HAS_RACEWAIT
    xnet_hle_set_join_quiet(on);
#else
    (void)on;
#endif
}

static void qp_end(const char *why)
{
    if (!s_armed && !s_creating)
        return;
    QP_LOG("done: %s", why);
    if (s_wait.state != RW_OFF) {
        int was = s_wait.state;
        int fin = bo3_racewait_finish(&s_wait, strstr(why, "went on") != NULL);
        qp_wait_pub();
        if (fin == RW_ACT_JOINED) {
            /* the game joined: the race's successor, or another one that
             * opened meanwhile (its IDENT from our member record) */
            xnet_hle_lobby_view lv;
            int joined = xnet_hle_lobby_view_get(&lv) && lv.game > 0 ? lv.game : 0;
            if (joined)
                RW_LOG("wait ended: joined (game %d, waited for game %d, %llu s)", joined, s_wait.race_id,
                       (qp_ms() - s_wait.since_ms) / 1000ull);
            else
                RW_LOG("wait ended: joined (waited for game %d, %llu s)", s_wait.race_id,
                       (qp_ms() - s_wait.since_ms) / 1000ull);
        }
        else if (was != RW_LEAVING)
            RW_LOG("wait ended: %s", why);
        qp_wait_quiet(0);
    }
    if (strstr(why, "creating page")) {
        /* a game QUICK PLAY made: its host's countdown applies; a game made
         * again after a race keeps what the first one was */
        s_host_game = s_stay ? s_stay_qp : 1;
        memset(&s_cd, 0, sizeof s_cd);
    }
    s_armed = 0;
    s_creating = 0;
    s_cancel = 0;
    s_stay = 0;
    s_target[0] = 0;
    s_holding = 0;
    s_join = 0;
    s_stay_ident = 0;
    qp_wait_pub();
    if (s_browse) {
        s_browse = 0;
        qp_br_head(0);
        s_br_msg_until = 0;
    }
    qp_br_pub();
    s_trail_until = qp_ms() + 60000ull;     /* the flow is still logged for a minute */
}

/* a guest function with its stack arguments already pushed (argbytes of
 * them: its ret pops them): a return address for its ret, the registers
 * around it as they were (xboxlive.c rebuild()). *lo and *hi take eax and edx
 * of the call. */
static void qp_call(void (*fn)(void), uint32_t argbytes, uint32_t *lo, uint32_t *hi)
{
    uint32_t a = g_eax, c = g_ecx, d = g_edx, sp = g_esp, b = g_ebx, si = g_esi, di = g_edi, bp = g_ebp, seh = g_seh_ebp;
    PUSH32(g_esp, 0);
    fn();
    if (lo) *lo = g_eax;
    if (hi) *hi = g_edx;
    g_eax = a; g_ecx = c; g_edx = d; g_esp = sp + argbytes; g_ebx = b; g_esi = si; g_edi = di; g_ebp = bp; g_seh_ebp = seh;
}

/* what the page manager does when a page exits (sub_000636D0 at 0x6393E): the
 * current state's handler, vtable word 0, called with the event 5, the
 * manager's second word, the exit code and the context. */
int bo3_quickplay_exit_event(uint32_t code)
{
    uint32_t st = MEM32(QP_STATE_PTR), vt, va, sp = g_esp, c = g_ecx, a = g_eax, d = g_edx;
    uint32_t b = g_ebx, si = g_esi, di = g_edi, bp = g_ebp, seh = g_seh_ebp;
    recomp_func_t fn;

    if (!qp_ok_va(st) || !qp_ok_va(vt = MEM32(st)) || !qp_ok_va(va = MEM32(vt)))
        return 0;
    fn = recomp_lookup_manual(va);
    if (!fn)
        fn = recomp_lookup(va);
    if (!fn)
        return 0;
    PUSH32(g_esp, bo3_quickplay_exit_ctx(code, MEM32(QP_MGR_CTX)));
    PUSH32(g_esp, code);
    PUSH32(g_esp, MEM32(QP_EVENT_W2));
    PUSH32(g_esp, 5u);
    PUSH32(g_esp, 0);               /* the return address its ret takes */
    g_ecx = st;
    fn();
    g_eax = a; g_ecx = c; g_edx = d; g_esp = sp; g_ebx = b; g_esi = si; g_edi = di; g_ebp = bp; g_seh_ebp = seh;
    return 1;
}

/* The session's status (the method the Quick Match page asks every tick in
 * its waiting states): 0 going on, 1 failed, 2 done. */
static int qp_session_status(void)
{
    uint32_t vt = MEM32(QP_SESSION), va, r = 0, c = g_ecx;
    recomp_func_t fn;
    if (!qp_ok_va(vt) || !qp_ok_va(va = MEM32(vt + 0xCu)))
        return 1;
    fn = recomp_lookup_manual(va);
    if (!fn)
        fn = recomp_lookup(va);
    if (!fn)
        return 1;
    g_ecx = QP_SESSION;
    qp_call(fn, 0u, &r, NULL);
    g_ecx = c;
    return (int)r;
}

/* The title makes a game only from inside a room: a gcre answered outside one
 * is never taken, and the creating page falls to DISCONNECTED. The room's
 * steps, as the Quick Match page's states 8 and 6 take them (0xAA881, 0xAA8BE,
 * 0xAAB5A), with no page of their own: the mode page is up meanwhile. The room
 * list's callback and fields stay that page's: it is not running. */
static void cr_room_tick(void)
{
    uint32_t page = QP_SEARCH_PAGE, c = g_ecx;
    if (s_room_step == CR_ROOM_LIST) {
        uint32_t id;
        if (bo3_quickplay_room_ask(&s_room_asked, qp_ms(), MEM8(page + 0x1941u), MEM32(page + 0x18C8u))) {
            QP_LOG("the room list is asked for");
            MEM8(page + 0x1941u) = 0;
            MEM32(page + 0x18C8u) = 0;
            g_ecx = QP_SESSION;
            PUSH32(g_esp, page);
            PUSH32(g_esp, QP_ROOMS_CB);
            qp_call(BO3_SUB(sub_001BC090, sub_001BC3B0, sub_001BC450), 8u, NULL, NULL);
        }
        if (MEM8(page + 0x1941u) && MEM32(page + 0x18C8u)) {
            id = MEM32(page + 0x18CCu);
            PUSH32(g_esp, id);
            qp_call(BO3_SUB(sub_001BC150, sub_001BC470, sub_001BC510), 4u, NULL, NULL);
            MEM32(QP_ROOM_ID) = id;
            s_room_step = CR_ROOM_WAIT;
            QP_LOG("the room %u is being entered", id);
        }
        g_ecx = c;
    } else if (s_room_step == CR_ROOM_WAIT) {
        int r = qp_session_status();
        if (r) {
            s_room_step = CR_ROOM_NONE;
            QP_LOG("%s", r == 2 ? "in the room" : "the room did not take us");
        }
    }
    if (s_room_step != CR_ROOM_NONE && qp_ms() > s_room_until) {
        s_room_step = CR_ROOM_NONE;
        QP_LOG("the room did not answer");
    }
}

/* The select of the online page: the room first (cr_room_tick goes on from
 * the flip), and the transition the room's CREATE GAME makes (sub_000146E0
 * with edi 0). Its enter event builds the name page, which
 * bo3_quickplay_name_page skips, and the state's step machine builds the
 * title's mode page. */
void bo3_quickplay_create_game(void)
{
    uint32_t di = g_edi;
    s_armed = s_creating = s_cancel = s_join = s_host_game = 0;
    s_join_ret = QP_JOIN_NONE;
    s_direct = 1;
    s_direct_at = qp_ms();
    s_trail_until = s_direct_at + 60000ull;
    QP_LOG("CREATE GAME: the title's create state");
    s_room_step = CR_ROOM_LIST;
    s_room_asked = 0;
    s_room_until = s_direct_at + QP_ROOM_WAIT_MS;
    cr_room_tick();
    g_edi = 0;
    PUSH32(g_esp, QP_HASH_HI);
    PUSH32(g_esp, QP_HASH_CREATE);
    PUSH32(g_esp, QP_FLOW);
    qp_call(BO3_SUB(sub_000146E0, sub_000147A0, sub_000147A0), 12u, NULL, NULL);
    g_edi = di;
}

/* The create state's Back case, after it read its page step: see the header */
static void cr_step(void)
{
    uint32_t st = bo3_quickplay_create_step(s_direct, g_eax);
    if (st != g_eax) {
        QP_LOG("B on the mode page: no name page behind it");
        g_eax = st;
    }
}

/* the title's own question "is B pressed" (the one the Quick Match page asks
 * in its list and NO GAMES states), which reads the pads and changes nothing */
static int qp_b_pressed(void)
{
    uint32_t r = 0;
    uint32_t a = g_eax, d = g_edx;
    g_eax = 0;
    g_edx = QP_PADS;
    qp_call(BO3_SUB(sub_00017810, sub_00017930, sub_00017920), 0u, &r, NULL);
    g_eax = a; g_edx = d;
    return (r & 0xFFu) != 0;
}

/* The page's exit, as its own branches write it (sub_000AA4D0 at 0xAAB95): the
 * code, the fade clock from the title's constant, phase 3. The page manager
 * fades and hands the state the event 5 with this code. */
static void qp_exit_back(void)
{
    MEM32(QP_MGR_CODE) = QP_EXIT_BACK;
    MEM8(QP_MGR_FLAG) = 0;
    MEM32(QP_MGR_FADE) = MEM32(QP_FADE_SRC);
    MEM32(QP_MGR_PHASE) = 3u;
    QP_LOG("B: back to the online page");
}

/* The A branch of NO GAMES FOUND (sub_000AA4D0 at 0xAA881), without the
 * screen: ask for the room list with the page's callback, state 8. The title
 * goes on from there by itself: state 8 picks the first room, state 6 waits
 * for the session, the page exits with code 0x2D into the create state. */
static void qp_start_create(uint32_t page)
{
    s_creating = 1;
    s_create_at = qp_ms();
    QP_LOG("no game found: the title's create branch (room list, room, create state)");
    MEM8(page + 0x1941u) = 0;
    MEM32(page + 0x18C8u) = 0;
    {
        uint32_t c = g_ecx;
        g_ecx = QP_SESSION;
        PUSH32(g_esp, page);
        PUSH32(g_esp, QP_ROOMS_CB);
        qp_call(BO3_SUB(sub_001BC090, sub_001BC3B0, sub_001BC450), 8u, NULL, NULL);
        g_ecx = c;
    }
    MEM8(page + 0x1942u) = QP_SUB_ROOMS;
}

/* the room list never came: back to the title's own NO GAMES FOUND */
static void qp_give_up(uint32_t page)
{
    QP_LOG("the room list did not answer: the title's NO GAMES FOUND screen");
    MEM8(page + 0x1942u) = QP_SUB_WAIT;     /* state 0 with the results ready builds it */
    MEM8(page + 0x1940u) = 1;
    qp_end("gave up");
}

/* The search the page's setup starts (sub_000A9590 at 0xA980A .. 0xA98BE),
 * again on the same page: its state, count and nine result rows as the setup
 * leaves them, the session's search (sub_001BB1E0, the page's own callback
 * 0xA99F0), this copy's ping for the page's filter. The page's text and
 * widgets are the setup's and stay as they are. */
static void qp_research(uint32_t page)
{
    uint32_t k, r, conn, info = 0;
    MEM8(page + 0x1942u) = QP_SUB_WAIT;
    MEM16(page + 0x330u) = 0;
    MEM16(page + 0x332u) = 1;
    MEM32(page + 0x1920u) = 0;
    for (k = 0; k < 9u; k++) {
        r = page + 0x60u + k * 0x50u;
        MEM8(r) = 0;                    /* strcpy(row, "") */
        MEM32(r + 0x30u) = 0xFFFFFFFFu;
        MEM32(r + 0x20u) = 0; MEM32(r + 0x24u) = 0; MEM32(r + 0x28u) = 0; MEM32(r + 0x2Cu) = 0;
        MEM32(r + 0x3Cu) = 0; MEM32(r + 0x40u) = 0;
        MEM32(r + 0x34u) = 0x7FFFFFFFu;
        MEM32(r + 0x44u) = 0;
        MEM32(r + 0x48u) = 0xFFFFFFFFu;
    }
    {
        uint32_t c = g_ecx;
        g_ecx = QP_SESSION;
        PUSH32(g_esp, page);
        PUSH32(g_esp, QP_SEARCH_CB);
        PUSH32(g_esp, 0u);
        PUSH32(g_esp, 0xFFFFFFFFu);
        PUSH32(g_esp, 0x1F4u);
        qp_call(BO3_SUB(sub_001BB1E0, sub_001BB500, sub_001BB5A0), 0x14u, NULL, NULL);
        g_ecx = c;
    }
    conn = MEM32(QP_SESSION + 4u);
    if (qp_ok_va(conn)) {
        PUSH32(g_esp, 0x73656C66u);     /* 'flse' */
        PUSH32(g_esp, conn);
        qp_call(BO3_SUB(sub_00216460, sub_00216770, sub_00216810), 8u, &info, NULL);
        if (qp_ok_va(info))
            MEM32(page + 0x192Cu) = MEM32(info + 0x24u);
    }
    MEM32(page + 0x1938u) = 0;
    MEM8(page + 0x1940u) = 0;
    MEM32(page + 0x1930u) = 0;
    MEM32(page + 0x1934u) = 0xFFFFFFFFu;
    s_searches++;
}

/* The title's message box (sub_00075DB0: eax the kind, ebx the string, the
 * box's page and its heading's string on the stack), as the lobby shows the same text
 * when a game goes away (0x6D55E). Its Continue leads to the room state, and
 * stay.c goes on from there to the online page. */
static void qp_message_back(void)
{
    uint32_t a = g_eax, b = g_ebx;
    PUSH32(g_esp, QP_STR_ERROR);
    PUSH32(g_esp, QP_BOX_PAGE);
    g_eax = 2u;
    g_ebx = QP_STR_NO_SESSION;
    qp_call(BO3_SUB(sub_00075DB0, sub_00075FB0, sub_00076020), 8u, NULL, NULL);
    g_eax = a; g_ebx = b;
}

/* stay together: an answer that kept nothing, read for what the server
 * says about the race (racewait.h rw_stay); 1 when the wait is over */
static int qp_stay_answer(const qp_in *q)
{
    xnet_hle_lobby_view lv;
    int r, ws = 0, host_back = 0, why = 0;
    unsigned seq = s_sw.last_seq;
    memset(&lv, 0, sizeof lv);
#ifdef XNET_HLE_HAS_RACEWAIT
    if (xnet_hle_lobby_view_get(&lv)) {
        seq = lv.wait_seq;
        ws = lv.wait_state;
    }
#endif
    bo3_stay_wait_keys(&host_back, &why);
    r = bo3_racewait_stay_answer(&s_sw, seq, ws, host_back, why, q->now);
    if (s_sw.on && (ws != s_wait_ws || host_back != s_stay_hb)) {
        RW_LOG("stay together: game %d: %s%s (waited %llu s)", s_stay_ident, bo3_racewait_ws_name(ws),
               host_back ? ", its host is back" : "", (q->now - s_sw.since_ms) / 1000ull);
        s_wait_ws = ws;
        s_stay_hb = host_back;
    }
    s_deadline = bo3_racewait_stay_deadline(&s_sw, s_deadline);
    if (r == RW_SW_KEEP || r == RW_SW_OLD)
        return 0;
    RW_LOG("stay together: the wait for '%s' is over: %s", s_target,
           r == RW_SW_FULL ? "the game is full" : r == RW_SW_MISSED ? "the title's filter keeps the game out"
           : s_sw.why == RW_SWHY_QUIT ? "its host quit" : s_sw.why == RW_SWHY_LEFT ? "its host left"
           : s_sw.why == RW_SWHY_ABSENT ? "its host did not come back" : s_sw.why == RW_SWHY_TIME ? "the race's time ran out"
           : "the server no longer knows the race");
    s_sw.leave_ms = q->now + RW_STAY_LEAVE_MS;
    qp_wait_pub();
    return 1;
}

/* the guest's Quick Match after a race: only the host's game, searched for
 * again until it is there */
static void qp_target_tick(uint32_t page, const qp_in *q)
{
    qp_target_in t;
    int act;
    /* the end line is up: then the online page (B goes at once) */
    if (s_sw.leave_ms) {
        if (q->sub == QP_SUB_WAIT && q->ready)
            MEM8(page + 0x1940u) = 0;
        if (q->active && (q->cancel || q->now >= s_sw.leave_ms)) {
            s_sw.leave_ms = 0;
            s_holding = 1;
            s_deadline = s_retry_at = ~0ull;
            bo3_stay_wait_done();
            qp_exit_back();
        }
        return;
    }
    memset(&t, 0, sizeof t);
    t.active = q->active;
    t.sub = q->sub;
    t.ready = q->ready;
    t.count = q->count;
    t.holding = s_holding;
    t.cancel = q->cancel;
    t.join_failed = q->join_failed;
    t.now = q->now;
    t.deadline = s_deadline;
    t.retry_at = s_retry_at;
    act = bo3_quickplay_target_decide(&t);
    /* the last refusal's search has answered: that game is over for us */
    if (s_refusals >= QP_REFUSE_MAX && q->sub == QP_SUB_WAIT && q->ready && act != QP_T_CANCEL)
        act = QP_T_TIMEOUT;
    if (act == QP_T_HOLD && q->sub == QP_SUB_JOINING) {
        /* A refused join leaves the session in its failed state. A page
         * left idle on it is taken for a lost connection a frame later: the
         * title's "cannot connect to Takedawn Online" page (flow 0x948,
         * soak055 S-b2: the host left as we joined). So the search runs
         * again at once, as QUICK PLAY's own refusal does (BO-364); after
         * QP_REFUSE_MAX refusals its answer ends the wait with the title's
         * "This game session is no longer available". */
        s_refusals++;
        s_holding = 0;
        QP_LOG("'%s' refused the join (%d of %d): searching again%s", s_target, s_refusals, QP_REFUSE_MAX,
               s_refusals >= QP_REFUSE_MAX ? ", then the title's message" : "");
        qp_research(page);
    } else if (act == QP_T_HOLD) {
        MEM8(page + 0x1940u) = 0;       /* the page keeps searching, no NO GAMES FOUND */
        s_holding = 1;
        s_retry_at = q->now + QP_RETRY_MS;
        if (s_stay_ident > 0 && qp_stay_answer(q))
            return;
        if (s_searches < 4 || !(s_searches % 10))
            QP_LOG("'%s' is not open yet (search %u, %u record(s) seen): again in %llu ms", s_target, s_searches, s_seen,
                   QP_RETRY_MS);
    } else if (act == QP_T_RESEARCH) {
        s_holding = 0;
        qp_research(page);
    } else if (act == QP_T_CANCEL || act == QP_T_TIMEOUT) {
        if (!s_holding && q->sub == QP_SUB_WAIT)
            MEM8(page + 0x1940u) = 0;
        if (q->sub == QP_SUB_JOINING)
            MEM8(page + 0x1942u) = QP_SUB_IDLE;
        s_holding = 1;                  /* nothing more on this page */
        s_deadline = ~0ull;
        s_retry_at = ~0ull;
        if (act == QP_T_TIMEOUT) {
            QP_LOG("'%s' did not open again: the title's message, then the online page", s_target);
            bo3_stay_wait_end();
            s_box_pending = 1;          /* at the flip: this page's tick is still running */
        } else {
            qp_exit_back();
        }
    }
}

/* ---- BO-365: waiting for a race under way (racewait.h) ---- */

/* the page and the lobby view (the last gsea answer's TKRACE* / TKWAIT* keys) */
static void qp_rw_in(rw_in *r, const qp_in *q, xnet_hle_lobby_view *lv)
{
    memset(r, 0, sizeof *r);
    r->sub = q->sub;
    r->ready = q->ready;
    r->count = q->count;
    r->active = q->active;
    r->cancel = q->cancel;
    r->join_failed = q->join_failed;
    r->now = q->now;
    memset(lv, 0, sizeof *lv);
    if (!xnet_hle_lobby_view_get(lv))
        return;
#ifdef XNET_HLE_HAS_RACEWAIT
    /* XNET_WAIT_* and RW_WS_* are the same list */
    r->lv_wait_state = lv->wait_state;
    r->lv_players = lv->wait_players;
    r->lv_secs = lv->wait_secs;
    r->lv_queue = lv->wait_queue;
    r->lv_seq = lv->wait_seq;
    r->lv_name = lv->wait_name;
    r->lv_racing = lv->racing;
    r->lv_race_id = lv->race_id;
    r->lv_race_players = lv->race_players;
    r->lv_race_max = lv->race_max;
    r->lv_race_secs = lv->race_secs;
    r->lv_race_name = lv->race_name;
#endif
}

int bo3_quickplay_wait_ident(void)
{
    return s_wait_tag;
}

int bo3_quickplay_wait_view(struct rw_state *out)
{
    if (out)
        *out = s_wait;
    return s_wait.state;
}

static void qp_wait_begin(uint32_t page, const qp_in *q, const rw_in *r)
{
    (void)q;
    bo3_racewait_begin(&s_wait, r);
    qp_wait_pub();
    s_wait_ws = -1;
    qp_wait_quiet(1);
    if (MEM8(page + 0x1942u) == QP_SUB_WAIT)
        MEM8(page + 0x1940u) = 0;       /* no NO GAMES FOUND for the empty answer */
    RW_LOG("no open game%s: waiting for game %d '%s' (race under way, %d of %d players, %d s in)",
           s_join ? " with the code" : "", s_wait.race_id, s_wait.name, r->lv_race_players, r->lv_race_max,
           r->lv_race_secs);
}

/* the Quick Match page while the wait runs: racewait.c decides, the ops are
 * the stay-join's (qp_research, the results flag, QP_SUB_IDLE, the Back exit,
 * the create branch) */
static void qp_wait_tick(uint32_t page, const qp_in *q)
{
    xnet_hle_lobby_view lv;
    rw_in r;
    int act, st0 = s_wait.state;
    qp_rw_in(&r, q, &lv);
    act = bo3_racewait_step(&s_wait, &r);
    qp_wait_pub();
    if (s_wait.wait_state != s_wait_ws || s_wait.state != st0) {
        RW_LOG("game %d: %s%s (%d players, %d s, queue %d%s%s) poll %u", s_wait.race_id,
               bo3_racewait_ws_name(s_wait.wait_state), s_wait.state == RW_JOINING ? ", the title joins" : "",
               s_wait.players, s_wait.secs, s_wait.queue, s_wait.name[0] ? ", " : "", s_wait.name, s_wait.polls);
        s_wait_ws = s_wait.wait_state;
    }
    switch (act) {
    case RW_ACT_POLL:
        qp_research(page);              /* the lobby tag adds TKWAITFOR (online.c) */
        break;
    case RW_ACT_HOLD:
        if (q->sub == QP_SUB_WAIT && q->ready && q->count == 0)
            MEM8(page + 0x1940u) = 0;   /* the answer kept nothing: no NO GAMES FOUND */
        break;
    case RW_ACT_REFUSED:
        MEM8(page + 0x1942u) = QP_SUB_IDLE;     /* no error box: the next poll in RW_POLL_MS */
        RW_LOG("game %d refused the join (%d of %d): waiting on", s_wait.race_id, s_wait.join_fails, RW_JOIN_FAILS);
        break;
    case RW_ACT_CANCEL:
        if (q->sub == QP_SUB_WAIT)
            MEM8(page + 0x1940u) = 0;
        if (q->sub == QP_SUB_JOINING)
            MEM8(page + 0x1942u) = QP_SUB_IDLE;
        if (s_join)
            s_join_ret = QP_JOIN_CANCELLED;
        qp_wait_quiet(0);
        s_armed_at = q->now;            /* the Back exit's own QP_MAX_MS, not the wait's minutes ("too long") */
        RW_LOG("wait ended: cancel (B)");
        if (s_browse) {                 /* BO-372: B in a wait goes back to the list */
            qp_br_relist(page);
            break;
        }
        qp_exit_back();
        break;
    case RW_ACT_FALLBACK:
        qp_wait_quiet(0);
        RW_LOG("wait ended: %s (game %d, %llu s)", bo3_racewait_why_name(s_wait.why), s_wait.race_id,
               (q->now - s_wait.since_ms) / 1000ull);
        if (q->sub == QP_SUB_JOINING)
            MEM8(page + 0x1942u) = QP_SUB_IDLE;
        if (s_browse) {
            /* BO-372: the end line is read, then the list again (no game of our own) */
            s_armed_at = q->now;
            if (q->sub == QP_SUB_WAIT)
                MEM8(page + 0x1940u) = 0;
            if (!bo3_racewait_hold(&s_wait, q->now)) {
                qp_br_say(s_wait.why == RW_WHY_MISSED ? "THAT GAME IS NO LONGER OPEN" : "THE SERVER DID NOT ANSWER");
                qp_br_relist(page);
            }
            break;
        }
        if (s_join) {
            /* back to the code entry, which says why (xboxlive.c) */
            if (q->sub == QP_SUB_WAIT)
                MEM8(page + 0x1940u) = 0;
            s_join_ret = s_wait.why == RW_WHY_FULL ? QP_JOIN_RACE_FULL
                       : s_wait.why == RW_WHY_SERVER || s_wait.why == RW_WHY_MISSED ? QP_JOIN_MISSED
                       : QP_JOIN_RACE_GONE;
            s_armed_at = q->now;        /* as the create branch below: no "too long" on the way out */
            qp_exit_back();
        } else {
            /* QUICK PLAY makes its own game, as with nothing under way; the
             * "ended" / "full" line is read first (RW_LEAVE_MS, B still goes back) */
            s_armed_at = q->now;        /* the create branch gets its own QP_MAX_MS */
            if (bo3_racewait_hold(&s_wait, q->now)) {
                if (q->sub == QP_SUB_WAIT)
                    MEM8(page + 0x1940u) = 0;
                RW_LOG("the end line is up %llu ms before QUICK PLAY makes its own game", RW_LEAVE_MS);
            } else
                qp_start_create(page);
        }
        break;
    case RW_ACT_CREATE:
        s_armed_at = q->now;
        if (s_browse) {
            qp_br_relist(page);
            break;
        }
        qp_start_create(page);
        break;
    default:
        break;
    }
}

/* every flip: the Quick Match page's bottom line while the wait has
 * something to say (onlinedesc.c swaps the widget's text array) */
static void qp_wait_line(uint32_t cur)
{
    extern const char *xbox_SettingsText(const char *key, int *locked);
    char t[96];
    int lang;
    t[0] = 0;
    if (cur == QP_FLOW_QUICK && s_browse && s_wait.state == RW_OFF) {
        /* BO-372: the list's line: a message for a while, else what A does */
        if (qp_ms() < s_br_msg_until)
            snprintf(t, sizeof t, "%s", s_br_msg);
        else if (s_br_n > 0)
            snprintf(t, sizeof t, "%s", "PICK A GAME - A RACING ONE IS JOINED AFTER ITS RACE");
    } else if (cur == QP_FLOW_QUICK && s_wait.state != RW_OFF) {
        lang = bo3_language_index(xbox_SettingsText("game.language", NULL));
        bo3_racewait_line(&s_wait, lang >= 0 ? lang : BO3_LANG_ENGLISH, t, sizeof t);
    } else if (cur == QP_FLOW_QUICK && s_armed && s_target[0] && s_stay_ident > 0) {
        lang = bo3_language_index(xbox_SettingsText("game.language", NULL));
        bo3_racewait_stay_line(&s_sw, lang >= 0 ? lang : BO3_LANG_ENGLISH, qp_ms(), t, sizeof t);
    }
    if (t[0]) {
        static char said[96];
        if (s_browse && strcmp(said, t)) {
            snprintf(said, sizeof said, "%s", t);
            QP_LOG("the bottom line: \"%s\"", t);
        }
        bo3_onlinedesc_wait_line(t);
        s_wait_line = 1;
    } else if (s_wait_line) {
        bo3_onlinedesc_wait_line(NULL);
        s_wait_line = 0;
    }
}

/* ---- BO-372: BROWSE GAMES (quickplay.h) ---- */

#define QP_STR_TABLE  BO3_VA(0x004D532Cu, 0x004D564Cu, 0x004D56ECu)   /* the loaded string table */
/* its slot the page's title is (sub_000A9590, context 1): an offset in the
 * table, one more slot on both PAL discs (not a guest address, so no BO3_VA) */
#if defined(BO3_REGION_PAL) && BO3_REGION_PAL
#define QP_HEAD_SLOT  0x311Cu
#else
#define QP_HEAD_SLOT  0x3118u
#endif
#define QP_ROW(p, i)  ((p) + 0x60u + (uint32_t)(i) * 0x50u)   /* the page's result rows */
#define QP_ROW_IDENT  0x30u
#define QP_ROW_KEY    0x34u                     /* the rows' first sort key (sub_000AA1F0) */
#define QP_ROW_COUNT  0x3Cu
#define QP_ROW_MAX    0x40u
#define QP_CURSOR     0x330u                    /* the list's cursor (16 bits), its count after it */
#define QP_CTX        0x1928u                   /* 1 Quick Match (joins the first), 0 the list */
#define QP_CRIT       0x1924u                   /* the list mode's criteria block */
extern void BO3_SUB(sub_001D2879, sub_001D2B99, sub_001D2C39)(void);    /* the title's heap: (size, tag), stdcall */

static const char k_br_head[] = "BROWSE GAMES";

/* the page's title: the string table's slot names our text while the page is
 * built (bo3_quickplay_begin_browse), the title's again once it is up */
static void qp_br_head(int ours)
{
    uint32_t t = MEM32(QP_STR_TABLE), slots;
    /* the language file is in the title's heap, the 64 MiB RAM alias (onlinebrand.c) */
    if (t < 0x80010000u || t >= 0x84000000u)
        return;
    slots = MEM32(t + 0xCu);
    if (slots < 0x80010000u || slots >= 0x84000000u)
        return;
    if (ours) {
        if (!s_br_head_str) {
            uint32_t a = g_eax, c = g_ecx, d = g_edx, sp = g_esp, k;
            PUSH32(g_esp, 0x5441424Bu);     /* "TABK" */
            PUSH32(g_esp, (uint32_t)sizeof k_br_head * 2u);
            PUSH32(g_esp, 0);
            BO3_SUB(sub_001D2879, sub_001D2B99, sub_001D2C39)();
            s_br_head_str = g_eax;
            g_eax = a; g_ecx = c; g_edx = d; g_esp = sp;
            if (!s_br_head_str)
                return;
            for (k = 0; k < sizeof k_br_head; k++)
                MEM16(s_br_head_str + 2u * k) = (uint16_t)(uint8_t)k_br_head[k];
        }
        if (!s_br_head) {
            s_br_head_old = MEM32(slots + QP_HEAD_SLOT);
            MEM32(slots + QP_HEAD_SLOT) = s_br_head_str;
            s_br_head = 1;
        }
    } else if (s_br_head) {
        if (MEM32(slots + QP_HEAD_SLOT) == s_br_head_str)
            MEM32(slots + QP_HEAD_SLOT) = s_br_head_old;
        s_br_head = 0;
    }
}

void bo3_quickplay_begin_browse(void)
{
    bo3_quickplay_begin();
    s_browse = 1;
    qp_br_pub();
    s_br_act = 0;
    s_br_msg[0] = 0;
    s_br_msg_until = 0;
    s_br_keep = 0;
    s_br_n = 0;
    s_br_records = 0;
    s_br_wait_row = -1;
    s_br_refresh_at = 0;
    qp_br_head(1);
    QP_LOG("BROWSE GAMES: the title's Quick Match page as a list of every game");
}

int bo3_quickplay_browse_tag(void) { return s_br_tag; }

static void qp_br_say(const char *t)
{
    snprintf(s_br_msg, sizeof s_br_msg, "%s", t);
    s_br_msg_until = qp_ms() + QP_BR_MSG_MS;
    QP_LOG("BROWSE GAMES: \"%s\"", t);
}

/* the list mode: context 0, criteria that keep any game */
static void qp_br_list_mode(uint32_t page)
{
    uint32_t crit = MEM32(page + QP_CRIT);
    MEM32(page + QP_CTX) = 0;
    if (qp_ok_va(crit)) {
        MEM32(crit) = 0;                /* any ranking */
        MEM16(crit + 4u) = 0;           /* any ping band */
        MEM16(crit + 6u) = 5;           /* any mode */
    }
}

/* a row's label without its state ("NAME - RACING 1:20" -> "NAME") */
static void qp_br_strip(char *nm)
{
    size_t k;
    for (k = strlen(nm); k > 3; k--)
        if (!strncmp(nm + k - 3, " - ", 3) && (!strncmp(nm + k, "RACING", 6) || !strcmp(nm + k, "FULL"))) {
            nm[k - 3] = 0;
            return;
        }
}

/* the callback's record in browse mode (bo3_quickplay_record): a full game is
 * kept (the title drops a record whose count is its size), its row then says
 * the game's own numbers; every row gets its label and its place */
static void qp_br_record(void (*title_fn)(void))
{
    uint32_t sp = g_esp, page = MEM32(sp + 0x3Cu), src = MEM32(sp + 4u), cnt = MEM32(sp + 0x18u), mx = MEM32(sp + 0x1Cu);
    int ident = (int)MEM32(sp + 8u), st, secs = 0, i, k;
    unsigned long long at = 0;
    char nm[QP_BR_NAME], lab[QP_BR_NAME];
    if (page != QP_SEARCH_PAGE) {
        title_fn();
        return;
    }
    qp_br_list_mode(page);
#ifdef XNET_HLE_HAS_BROWSE
    st = xnet_hle_browse_state(ident, &secs, &at);
#else
    st = QP_BR_NONE;
#endif
    if (st == QP_BR_NONE)
        st = cnt >= mx ? QP_BR_FULL : QP_BR_OPEN;
    for (i = 0; i + 1 < (int)sizeof nm && qp_ok_va(src + (uint32_t)i) && MEM8(src + (uint32_t)i); i++)
        nm[i] = (char)MEM8(src + (uint32_t)i);
    nm[i] = 0;
    if (s_br_records++ < 12)
        QP_LOG("browse record '%s' game %d: state %d, %u of %u, P %08X %08X %08X %08X %08X, flags %08X", nm, ident, st,
               cnt, mx, MEM32(sp + 0xCu), MEM32(sp + 0x10u), MEM32(sp + 0x14u), MEM32(sp + 0x24u), MEM32(sp + 0x28u),
               MEM32(sp + 0x38u));
    if (cnt >= mx && mx < 64u)
        MEM32(sp + 0x1Cu) = cnt + 1u;   /* the title keeps it; its row is put right below */
    title_fn();
    MEM32(sp + 0x1Cu) = mx;
    (void)at;                           /* the toolkit's clock: the answer came just now, TKSECS is the time */
    if (s_br_records - 1u < 16u) {
        s_br_rec[s_br_records - 1u].ident = ident;
        s_br_rec[s_br_records - 1u].secs = secs;
        s_br_rec[s_br_records - 1u].at = qp_ms();
    }
    for (k = 0; k < QP_BR_ROWS; k++) {
        uint32_t r = QP_ROW(page, k);
        if ((int)MEM32(r + QP_ROW_IDENT) != ident)
            continue;
        MEM32(r + QP_ROW_MAX) = mx;
        if (s_br_records <= 12)
            QP_LOG("browse row %d: key %08X %08X %08X, +20 %08X %08X %08X %08X, +44 %08X %08X %08X", k,
                   MEM32(r + 0x34u), MEM32(r + 0x38u), MEM32(r + 0x3Cu), MEM32(r + 0x20u), MEM32(r + 0x24u),
                   MEM32(r + 0x28u), MEM32(r + 0x2Cu), MEM32(r + 0x44u), MEM32(r + 0x48u), MEM32(r + 0x4Cu));
        MEM32(r + QP_ROW_KEY) = bo3_quickplay_browse_key(st, (int)s_br_records);
        bo3_quickplay_browse_label(lab, sizeof lab, nm, st, secs);
        for (i = 0; i < (int)QP_BR_NAME; i++)
            MEM8(r + (uint32_t)i) = (uint8_t)(i < (int)sizeof lab ? lab[i] : 0);
        MEM8(r + QP_BR_NAME - 1u) = 0;
        break;
    }
}

/* the rows in the page's order (sub_000AA1F0: the key, then fullest, then
 * the smallest size), before the page sorts and shows them: the cursor goes
 * to the game it was on (s_br_keep) */
static int qp_br_cmp_rows(uint32_t a, uint32_t b)
{
    int ka = (int)MEM32(a + QP_ROW_KEY), kb = (int)MEM32(b + QP_ROW_KEY);
    if (ka != kb) return ka < kb ? -1 : 1;
    if ((int)MEM32(a + QP_ROW_COUNT) != (int)MEM32(b + QP_ROW_COUNT))
        return (int)MEM32(a + QP_ROW_COUNT) > (int)MEM32(b + QP_ROW_COUNT) ? -1 : 1;
    if ((int)MEM32(a + QP_ROW_MAX) != (int)MEM32(b + QP_ROW_MAX))
        return (int)MEM32(a + QP_ROW_MAX) < (int)MEM32(b + QP_ROW_MAX) ? -1 : 1;
    return 0;
}

static void qp_br_order(uint32_t page)
{
    int n = (int)MEM32(page + 0x1920u), i, j, at = 0;
    uint8_t t[0x50];
    if (n > QP_BR_ROWS)
        n = QP_BR_ROWS;
    for (i = 1; i < n; i++)             /* insertion sort: nine rows */
        for (j = i; j > 0 && qp_br_cmp_rows(QP_ROW(page, j - 1), QP_ROW(page, j)) > 0; j--) {
            memcpy(t, (void *)((uintptr_t)g_xbox_mem_offset + QP_ROW(page, j)), sizeof t);
            memcpy((void *)((uintptr_t)g_xbox_mem_offset + QP_ROW(page, j)),
                   (void *)((uintptr_t)g_xbox_mem_offset + QP_ROW(page, j - 1)), sizeof t);
            memcpy((void *)((uintptr_t)g_xbox_mem_offset + QP_ROW(page, j - 1)), t, sizeof t);
        }
    s_br_n = n;
    for (i = 0; i < n; i++) {
        uint32_t r = QP_ROW(page, i), key = MEM32(r + QP_ROW_KEY);
        s_br_ident[i] = (int)MEM32(r + QP_ROW_IDENT);
        s_br_state[i] = key >= 0x200u ? QP_BR_FULL : key >= 0x100u ? QP_BR_RACING : QP_BR_OPEN;
        for (j = 0; j + 1 < (int)QP_BR_NAME && MEM8(r + (uint32_t)j); j++)
            s_br_name[i][j] = (char)MEM8(r + (uint32_t)j);
        s_br_name[i][j] = 0;
        qp_br_strip(s_br_name[i]);
        s_br_secs[i] = 0;
        s_br_since[i] = qp_ms();
        for (j = 0; j < 16 && j < (int)s_br_records; j++)
            if (s_br_rec[j].ident == s_br_ident[i]) {
                s_br_secs[i] = s_br_rec[j].secs;
                s_br_since[i] = s_br_rec[j].at;
            }
        s_br_shown[i][0] = 0;           /* the title writes the row's text from the record's label */
        if (s_br_keep && s_br_ident[i] == s_br_keep)
            at = i;
    }
    MEM16(page + QP_CURSOR) = (uint16_t)at;
    QP_LOG("BROWSE GAMES: %d game(s) listed, the cursor on line %d", n, at);
}

/* back to the list: a new browse search, the page in list mode */
static void qp_br_relist(uint32_t page)
{
    if (s_wait.state != RW_OFF) {
        bo3_racewait_finish(&s_wait, 0);
        qp_wait_pub();
        qp_wait_quiet(0);
    }
    qp_br_pub();
    s_br_wait_row = -1;
    s_cancel = 0;
    s_armed_at = qp_ms();
    qp_br_list_mode(page);
    s_br_records = 0;
    qp_research(page);
}

/* A on a racing game: the race wait for it, the page in its Quick Match
 * context so the title joins the game made again by itself */
static void qp_br_wait(uint32_t page, int row, const qp_in *q)
{
    xnet_hle_lobby_view lv;
    rw_in r;
    uint32_t rr = QP_ROW(page, row);
    int secs = s_br_secs[row] + (int)((q->now - s_br_since[row]) / 1000ull);
    qp_rw_in(&r, q, &lv);
    r.lv_racing = 1;
    r.lv_race_id = s_br_ident[row];
    r.lv_race_players = (int)MEM32(rr + QP_ROW_COUNT);
    r.lv_race_max = (int)MEM32(rr + QP_ROW_MAX);
    r.lv_race_secs = secs;
    r.lv_race_name = s_br_name[row];
    bo3_racewait_begin(&s_wait, &r);
    qp_wait_pub();
    qp_br_pub();
    s_wait_ws = -1;
    qp_wait_quiet(1);
    MEM32(page + QP_CTX) = 1;
    s_br_wait_row = row;
    RW_LOG("BROWSE GAMES: waiting for game %d '%s' (race under way, %d of %d players, %d s in)", s_wait.race_id,
           s_wait.name, r.lv_race_players, r.lv_race_max, secs);
}

/* The rows' text as it is drawn (the list's name widgets, page + 0x154C: the
 * UTF-16 text sub_000A9D10 writes from the rows with sub_001B2C60, 0x20
 * characters): a racing game's clock counts on between the searches, the
 * game a race wait runs for says so, with the wait's own clock (the bottom
 * line's). While the wait searches (the page in its state 0, the rows
 * cleared) the list stays drawn as it was; its cursor is held on that game. */
#define QP_ROW_TEXT(p, i)  MEM32((p) + 0x154Cu + 4u * (uint32_t)(i))   /* in the title's heap or below 64 MiB */
static void qp_br_paint(uint32_t page, unsigned long long now)
{
    static int said = -1;
    int i, k, ls, sub = (int)(uint8_t)MEM8(page + 0x1942u);
    int waiting = s_wait.state != RW_OFF && s_br_wait_row >= 0 && s_br_wait_row < s_br_n;
    char lab[QP_BR_NAME];
    if (waiting && (sub == QP_SUB_WAIT || sub == QP_SUB_IDLE)) {
        MEM16(page + QP_CURSOR) = (uint16_t)s_br_wait_row;
        MEM16(page + QP_CURSOR + 2u) = (uint16_t)s_br_n;
    }
    if (!waiting)
        said = -1;
    for (i = 0; i < s_br_n && i < QP_BR_ROWS; i++) {
        uint32_t w = QP_ROW_TEXT(page, i);
        if (waiting && i == s_br_wait_row) {
            ls = bo3_quickplay_browse_wait_label(s_wait.state, s_wait.wait_state, s_wait.queue);
            bo3_quickplay_browse_label(lab, sizeof lab, s_br_name[i], ls, s_wait.secs);
            if (ls != said)
                QP_LOG("BROWSE GAMES: line %d reads \"%s\"", i, lab);
            said = ls;
        } else if (s_br_state[i] == QP_BR_RACING)
            bo3_quickplay_browse_label(lab, sizeof lab, s_br_name[i], QP_BR_RACING,
                                       s_br_secs[i] + (int)((now - s_br_since[i]) / 1000ull));
        else
            continue;
        if (!strcmp(lab, s_br_shown[i]) || !(qp_ok_va(w) || (w >= 0x80010000u && w < 0x84000000u - 2u * QP_BR_NAME)))
            continue;
        for (k = 0; k < (int)QP_BR_NAME; k++)
            MEM16(w + 2u * (uint32_t)k) = (uint16_t)(uint8_t)(k < (int)sizeof lab ? lab[k] : 0);
        MEM16(w + 2u * (QP_BR_NAME - 1u)) = 0;
        snprintf(s_br_shown[i], sizeof s_br_shown[i], "%s", lab);
    }
}

/* the page's tick while BROWSE GAMES has it and no race wait runs */
static void qp_browse_tick(uint32_t page, const qp_in *q)
{
    if (s_br_head && q->active)
        qp_br_head(0);                  /* the page is built: the title's text back in its table */
    if (!q->active)
        return;
    if (MEM32(page + QP_CTX) != 0)
        qp_br_list_mode(page);
    /* the game picked refused the join (it filled or started meanwhile) */
    if (q->sub == QP_SUB_JOINING && q->join_failed) {
        MEM8(page + 0x1942u) = QP_SUB_IDLE;
        qp_br_say("THAT GAME IS NO LONGER OPEN");
        qp_br_relist(page);
        return;
    }
    /* B: back to the online page, once nothing is in flight */
    if (q->cancel && (q->sub == QP_SUB_LIST || q->sub == QP_SUB_NONE || q->sub == QP_SUB_IDLE ||
                      (q->sub == QP_SUB_WAIT && q->ready))) {
        if (q->sub == QP_SUB_WAIT)
            MEM8(page + 0x1940u) = 0;
        QP_LOG("BROWSE GAMES: B, back to the online page");
        s_join_ret = QP_JOIN_BROWSE_BACK;
        qp_exit_back();
        return;
    }
    /* a search's answer, before the page shows it */
    if ((q->sub == QP_SUB_WAIT && q->ready) || q->sub == QP_SUB_NONE) {
        if (q->count <= 0) {
            MEM8(page + 0x1940u) = 0;   /* no NO GAMES FOUND */
            s_join_ret = QP_JOIN_BROWSE_EMPTY;
            QP_LOG("BROWSE GAMES: no game at all: back to the online page");
            qp_exit_back();
            return;
        }
        if (q->sub == QP_SUB_WAIT) {
            qp_br_order(page);
            s_br_refresh_at = q->now + QP_BR_REFRESH_MS;
        }
        return;
    }
    if (q->sub != QP_SUB_LIST)
        return;
    if (s_br_act) {
        int row = s_br_act_row, act = s_br_act;
        s_br_act = 0;
        if (row < 0 || row >= s_br_n)
            return;
        if (act == QP_PICK_FULL) {
            char m[96];
            snprintf(m, sizeof m, "%s IS FULL - PICK ANOTHER GAME", s_br_name[row]);
            qp_br_say(m);
            return;
        }
        s_br_keep = s_br_ident[row];
        qp_br_wait(page, row, q);
        return;
    }
    /* the list again now and then: new games, the races' clocks */
    if (q->now >= s_br_refresh_at && q->now >= s_br_msg_until) {
        int c = (int)MEM16(page + QP_CURSOR);
        s_br_keep = c >= 0 && c < s_br_n ? s_br_ident[c] : 0;
        qp_br_relist(page);
    }
}

int bo3_quickplay_browse_pad(int a, int b)
{
    static int held_a, held_b, eat_a, eat_b;
    int fa = a && !held_a, fb = b && !held_b;
    uint32_t page = QP_SEARCH_PAGE;
    held_a = a;
    held_b = b;
    if (!a) eat_a = 0;
    if (!b) eat_b = 0;
    if (!s_browse || !g_xbox_mem_offset || s_wait.state != RW_OFF || MEM32(BO3_VA(0x00557A70u, 0x00557D90u, 0x00557E30u)) != page)
        return (eat_a ? 1 : 0) | (eat_b ? 2 : 0);
    if (MEM32(QP_MGR_PHASE) == 5u && MEM8(page + 0x1942u) == QP_SUB_LIST) {
        int c = (int)MEM16(page + QP_CURSOR);
        if (fb) {
            s_cancel = 1;               /* the page's Back exit (qp_browse_tick), not the list's own */
            eat_b = 1;
        }
        if (fa && c >= 0 && c < s_br_n && s_br_ident[c] == (int)MEM32(QP_ROW(page, c) + QP_ROW_IDENT)) {
            int pick = bo3_quickplay_browse_pick(s_br_state[c]);
            QP_LOG("BROWSE GAMES: A on line %d, '%s' (game %d): %s", c, s_br_name[c], s_br_ident[c],
                   pick == QP_PICK_JOIN ? "the title joins" : pick == QP_PICK_WAIT ? "wait for its race" : "full");
            if (pick != QP_PICK_JOIN) {
                s_br_act = pick;
                s_br_act_row = c;
                eat_a = 1;
            }
        }
    }
    return (eat_a ? 1 : 0) | (eat_b ? 2 : 0);
}

int bo3_quickplay_browse_view(qp_browse_view *v)
{
    uint32_t page = QP_SEARCH_PAGE;
    int k;
    memset(v, 0, sizeof *v);
    if (!s_browse || !g_xbox_mem_offset)
        return 0;
    v->up = s_wait.state == RW_OFF && MEM32(BO3_VA(0x00557A70u, 0x00557D90u, 0x00557E30u)) == page &&
            MEM32(QP_MGR_PHASE) == 5u && MEM8(page + 0x1942u) == QP_SUB_LIST;
    v->count = s_br_n;
    v->cursor = (int)MEM16(page + QP_CURSOR);
    for (k = 0; k < s_br_n && k < QP_BR_ROWS; k++) {
        v->ident[k] = s_br_ident[k];
        v->state[k] = s_br_state[k];
        snprintf(v->name[k], sizeof v->name[k], "%s", s_br_name[k]);
    }
    if (qp_ms() < s_br_msg_until)
        snprintf(v->line, sizeof v->line, "%s", s_br_msg);
    return 1;
}

void bo3_quickplay_stay_create(const char *name, const uint32_t *req, int qp_game)
{
    s_armed = 1;
    s_creating = 1;
    s_cancel = 0;
    s_stay = 1;
    s_stay_qp = qp_game;
    s_target[0] = 0;
    snprintf(s_stay_name, sizeof s_stay_name, "%s", name);
    memcpy(s_stay_req, req, sizeof s_stay_req);
    s_armed_at = s_create_at = qp_ms();
    s_trail_until = 0;
    QP_LOG("after the race: the create state makes '%s' again", s_stay_name);
    /* the title takes a gcre answer only from inside a room, and the lobby
     * select page that would enter one is passed over */
    s_stay_hold = 0;
    s_room_step = CR_ROOM_LIST;
    s_room_asked = 0;
    s_room_until = s_armed_at + QP_ROOM_WAIT_MS;
    cr_room_tick();
}

void bo3_quickplay_stay_join(const char *name, int ident, unsigned long long wait_ms)
{
    s_armed = 1;
    s_creating = 0;
    s_cancel = 0;
    s_stay = 0;
    s_holding = 0;
    s_searches = 1;
    s_seen = 0;
    s_refusals = 0;
    snprintf(s_target, sizeof s_target, "%s", name);
    s_armed_at = qp_ms();
    s_deadline = s_armed_at + wait_ms;
    s_trail_until = 0;
    s_stay_ident = ident > 0 ? ident : 0;
    s_wait_ws = -1;
    s_stay_hb = 0;
    {
        xnet_hle_lobby_view lv;
        unsigned seq = 0;
#ifdef XNET_HLE_HAS_RACEWAIT
        if (xnet_hle_lobby_view_get(&lv))
            seq = lv.wait_seq;
#else
        (void)lv;
#endif
        bo3_racewait_stay_begin(&s_sw, seq, s_armed_at);
    }
    qp_wait_pub();
    QP_LOG("after the race: Quick Match for '%s' only (game %d's host makes it again)", s_target, s_stay_ident);
}

/* sub_000A9A90, the Quick Match page's per-record callback (cdecl; the
 * record's NAME is its first argument, the page its last; al = 1 goes on to
 * the next record). While a guest looks for its host's game, any other game
 * is passed over as the title passes over one its filters drop. */
void bo3_quickplay_record(void (*title_fn)(void))
{
    char nm[32];
    uint32_t src, i;
    if (s_browse && s_wait.state == RW_OFF && g_xbox_mem_offset) {
        qp_br_record(title_fn);         /* BO-372 */
        return;
    }
    if (s_browse && s_wait.state != RW_OFF && g_xbox_mem_offset) {
        /* BO-372: the player picked this race: only its game made again (the
         * same name) is joined, never another open game as QUICK PLAY would */
        src = MEM32(g_esp + 4u);
        for (i = 0; i + 1 < sizeof nm && qp_ok_va(src + i) && MEM8(src + i); i++)
            nm[i] = (char)MEM8(src + i);
        nm[i] = 0;
        if (bo3_quickplay_name_match(nm, s_wait.name)) {
            title_fn();
            return;
        }
        g_eax = (g_eax & ~0xFFu) | 1u;
        g_esp += 4u;                    /* ret */
        return;
    }
    if (!s_target[0] || !g_xbox_mem_offset) {
        title_fn();
        return;
    }
    src = MEM32(g_esp + 4u);
    for (i = 0; i + 1 < sizeof nm && qp_ok_va(src + i) && MEM8(src + i); i++)
        nm[i] = (char)MEM8(src + i);
    nm[i] = 0;
    s_seen++;
    if (bo3_quickplay_name_match(nm, s_target)) {
        QP_LOG("'%s' is open: the title's Quick Match takes it", nm);
        title_fn();
        return;
    }
    g_eax = (g_eax & ~0xFFu) | 1u;
    g_esp += 4u;                        /* ret */
}

/* The Quick Match page's tick, after sub_000AA4D0 has its page in ebx. */
static void qp_tick(void)
{
    qp_in in;
    uint32_t page = g_ebx;
    int act;

    if (!s_armed || !g_xbox_mem_offset || page != QP_SEARCH_PAGE)
        return;
    memset(&in, 0, sizeof in);
    in.armed = s_armed;
    in.creating = s_creating;
    in.on_search = 1;
    in.active = MEM32(QP_MGR_PHASE) == 5u;
    in.sub = (int)(int8_t)MEM8(page + 0x1942u);
    in.ready = MEM8(page + 0x1940u) != 0;
    in.count = (int)MEM32(page + 0x1920u);
    in.rooms_done = MEM8(page + 0x1941u) != 0;
    /* B is read on every tick of the page (it also fades in and out while the
     * search runs): a press is held until nothing is in flight */
    if (!s_cancel && qp_b_pressed()) {
        s_cancel = 1;
        QP_LOG("B pressed: QUICK PLAY will leave at the next safe point");
    }
    in.cancel = s_cancel;
    in.join = s_join;
    /* the join's answer, read as state 5 reads it (BO-364) */
    {
        static int last_sub = -2, last_st = -2;
        int st = in.active && in.sub == QP_SUB_JOINING ? qp_session_status() : -1;
        in.join_failed = st == 1;
        if (in.sub != last_sub || st != last_st) {
            QP_LOG("page state %d (results %d, kept %d, active %d, session %d)", in.sub, in.ready, in.count, in.active, st);
            last_sub = in.sub;
            last_st = st;
        }
    }
    in.refusals = s_refusals;
    in.now = qp_ms();
    in.create_at = s_create_at;
    if (s_target[0]) {
        qp_target_tick(page, &in);
        return;
    }
    if (s_wait.state == RW_WAIT || s_wait.state == RW_JOINING || (s_wait.state == RW_LEAVING && s_wait.leave_ms)) {
        qp_wait_tick(page, &in);
        if (s_browse && in.active)
            qp_br_paint(page, in.now);
        return;
    }
    if (s_browse) {
        if (s_wait.state != RW_OFF)     /* a wait's end without a hold (cancel, server): the list again */
            qp_br_relist(page);
        else
            qp_browse_tick(page, &in);
        if (s_browse && in.active)
            qp_br_paint(page, in.now);
        return;
    }
    {
        rw_in r;
        xnet_hle_lobby_view lv;
        qp_rw_in(&r, &in, &lv);
        in.racing = !s_creating && s_wait.state == RW_OFF && bo3_racewait_should_wait(&r, s_join);
        act = bo3_quickplay_decide(&in);
        if (act == QP_WAIT) {
            qp_wait_begin(page, &in, &r);
            qp_wait_tick(page, &in);
            return;
        }
    }
    if (act == QP_CANCEL) {
        /* the results flag is what sends state 0 on to NO GAMES FOUND, in this
         * very tick: no screen for a search the player cancelled */
        if (in.sub == QP_SUB_WAIT)
            MEM8(page + 0x1940u) = 0;
        if (in.sub == QP_SUB_JOINING)
            MEM8(page + 0x1942u) = QP_SUB_IDLE;     /* a refused join: no error box on the way out */
        if (s_join)
            s_join_ret = QP_JOIN_CANCELLED;
        qp_exit_back();
    }
    else if (act == QP_NOTFOUND) {
        /* no open game with the code: back to the code entry, which says so
         * (xboxlive.c); NO GAMES FOUND is never drawn */
        if (in.sub == QP_SUB_WAIT)
            MEM8(page + 0x1940u) = 0;
        s_join_ret = bo3_quickplay_join_result(0, bo3_online_removed_take());
        QP_LOG("JOIN WITH CODE: no open game with the code%s",
               s_join_ret == QP_JOIN_REMOVED ? " (its host removed us)" : "");
        qp_exit_back();
    }
    else if (act == QP_REFUSED) {
        /* the game with the code refused the join: back to the code entry,
         * which says why; the title's error box is never built */
        MEM8(page + 0x1942u) = QP_SUB_IDLE;
        s_join_ret = bo3_quickplay_join_result(1, bo3_online_removed_take());
        QP_LOG("JOIN WITH CODE: the game refused the join (%s): back to the code entry",
               s_join_ret == QP_JOIN_REMOVED ? "its host removed us" : "it had started or closed");
        qp_exit_back();
    }
    else if (act == QP_RESEARCH) {
        s_refusals++;
        QP_LOG("the game picked refused the join (%d of %d): searching again", s_refusals, QP_REFUSE_MAX);
        qp_research(page);
    }
    else if (act == QP_CREATE) {
        if (in.sub == QP_SUB_JOINING) {
            s_refusals++;
            QP_LOG("the game picked refused the join (%d of %d): QUICK PLAY makes its own", s_refusals, QP_REFUSE_MAX);
        }
        qp_start_create(page);
    }
    else if (act == QP_GIVEUP)
        qp_give_up(page);
}

void bo3_quickplay_flip(uint32_t flip)
{
    uint32_t cur;
    unsigned long long now;
    (void)flip;
    if (!g_xbox_mem_offset)
        return;
    if (s_box_pending) {
        s_box_pending = 0;
        qp_message_back();
    }
    now = qp_ms();
    if (s_room_step != CR_ROOM_NONE)
        cr_room_tick();
    if (s_stay_hold && s_room_step == CR_ROOM_NONE) {
        uint32_t st = MEM32(QP_STATE_PTR);
        s_stay_hold = 0;
        if (bo3_quickplay_stay_release(s_creating, (qp_ok_va(st) ? st - QP_FLOW : 0) == QP_FLOW_CREATE)) {
            bo3_quickplay_exit_event(1u);
            qp_end("the creating page is up");
        }
    }
    if (s_direct && now - s_direct_at > 1500ull) {
        uint32_t st = MEM32(QP_STATE_PTR);
        if ((qp_ok_va(st) ? st - QP_FLOW : 0) != QP_FLOW_CREATE)
            s_direct = 0;               /* car select, the lobby, or back on the online page */
    }
    if (s_host_game) {
        uint32_t st = MEM32(QP_STATE_PTR);
        st = qp_ok_va(st) ? st - QP_FLOW : 0;
        if (st == 0x928u || st == QP_FLOW_ONLINE) {     /* the room or the online page: that game is over */
            s_host_game = 0;
            s_cd_seconds = 0;
            QP_LOG("the QUICK PLAY game is left");
        }
    }
    bo3_vote_flip();
    if (s_wait_line || s_wait.state != RW_OFF || s_browse || (s_armed && s_target[0] && s_stay_ident > 0)) {
        cur = MEM32(QP_STATE_PTR);
        qp_wait_line(qp_ok_va(cur) ? cur - QP_FLOW : 0);
    }
    if (!s_armed && now >= s_trail_until)
        return;
    cur = MEM32(QP_STATE_PTR);
    cur = qp_ok_va(cur) ? cur - QP_FLOW : 0;
    if (cur != s_last_flow) {
        QP_LOG("flow state %X -> %X", s_last_flow, cur);
        s_last_flow = cur;
    }
    if (!s_armed)
        return;
    /* the page started its exit into the create state, after B: it goes back instead */
    if (bo3_quickplay_recode(s_cancel, s_creating, MEM32(QP_MGR_PHASE), MEM32(QP_MGR_CODE))) {
        MEM32(QP_MGR_CODE) = QP_EXIT_BACK;
        QP_LOG("B: the room answer is turned into Back");
    }
    /* the player left (B back to the online page) or the title went on (car
     * select, lobby): nothing is armed any more */
    if (now - s_armed_at > 1500ull && cur != QP_FLOW_QUICK && cur != QP_FLOW_CREATE)
        qp_end(cur == QP_FLOW_ONLINE ? "back on the online page" : "the title went on");
    else if (now - s_armed_at > ((s_wait.state == RW_WAIT || s_wait.state == RW_JOINING) ? RW_MAX_MS + 30000ull
                                 : s_target[0] && s_sw.on ? RW_STAY_MAX_MS + 30000ull : QP_MAX_MS) &&
             !(s_browse && s_wait.state == RW_OFF))     /* BO-372: a list the player reads has no limit */
        qp_end("too long");
}

/* ---- the create state's name and mode pages ----
 *
 * sub_0006D100 builds them one after the other and goes on when each one's
 * Continue exits. While the create branch above runs they are not built: the
 * wrappers write what the page's Continue would have stored in the request
 * (page 0x55DF8C overlays the request 4 bytes in) and give the state the same
 * exit event the page manager would, so the state's own step machine runs on:
 * name page, mode page, the creating page (the title's, gcre). */

/* sub_00098050 (page on the stack): the name page. Its builder copies the
 * name (the gamertag when the request holds none) and the password into the
 * page's two buffers, and its Continue stores their addresses in the request. */
void bo3_quickplay_name_page(void (*title_fn)(void))
{
    uint32_t page, src;
    if ((!s_creating && !s_direct) || !g_xbox_mem_offset) {
        title_fn();
        return;
    }
    page = MEM32(g_esp + 4u);
    if (page != QP_NAME_PAGE) {
        title_fn();
        return;
    }
    if (s_stay) {
        /* the game made again: the last game's name, no password */
        bo3_quickplay_copy_str((uint8_t *)((uintptr_t)g_xbox_mem_offset + page + 0xACu), 0x20, (const uint8_t *)s_stay_name);
        MEM8(page + 0xCCu) = 0;
        MEM32(page + 4u) = page + 0xACu;
        MEM32(page + 8u) = page + 0xCCu;
        QP_LOG("name page skipped: '%s' as before", s_stay_name);
        g_eax = (g_eax & ~0xFFu) | 1u;
        g_esp += 8u;
        bo3_quickplay_exit_event(1u);
        return;
    }
    src = MEM32(page + 4u);
    if (!src)
        src = QP_GAMERTAG;
    if (src != page + 0xACu) {
        uint8_t tmp[0x20];
        memcpy(tmp, (const void *)((uintptr_t)g_xbox_mem_offset + src), sizeof tmp);
        tmp[sizeof tmp - 1] = 0;
        bo3_quickplay_copy_str((uint8_t *)((uintptr_t)g_xbox_mem_offset + page + 0xACu), 0x20, tmp);
    }
    src = MEM32(page + 8u);
    if (src && src != page + 0xCCu) {
        uint8_t tmp[0x11];
        memcpy(tmp, (const void *)((uintptr_t)g_xbox_mem_offset + src), sizeof tmp);
        tmp[sizeof tmp - 1] = 0;
        bo3_quickplay_copy_str((uint8_t *)((uintptr_t)g_xbox_mem_offset + page + 0xCCu), 0x11, tmp);
    } else if (!src) {
        MEM8(page + 0xCCu) = 0;
    }
    MEM32(page + 4u) = page + 0xACu;
    MEM32(page + 8u) = page + 0xCCu;
    QP_LOG("name page skipped: the game is named as the title names it");
    g_eax = (g_eax & ~0xFFu) | 1u;          /* al = 1, as the builder returns */
    g_esp += 8u;                            /* ret 4 */
    bo3_quickplay_exit_event(1u);
}

/* sub_000995F0 (the request in eax, page and a flag on the stack): the mode
 * page. Its Continue stores the mode, the track key and the language. */
void bo3_quickplay_mode_page(void (*title_fn)(void))
{
    uint32_t blk = g_eax, lo = 0, hi = 0;
    if (!s_creating || !g_xbox_mem_offset || blk != QP_BLOCK) {
        title_fn();
        return;
    }
    if (s_stay) {
        /* the game made again: the last lobby's mode, track, laps, limit */
        uint32_t w[QP_STAY_REQ_WORDS], k;
        for (k = 0; k < QP_STAY_REQ_WORDS; k++)
            w[k] = MEM32(blk + 4u * k);
        bo3_quickplay_stay_request(w, s_stay_req, MEM8(QP_LANGUAGE));
        for (k = 2; k < QP_STAY_REQ_WORDS; k++)
            MEM32(blk + 4u * k) = w[k];
        QP_LOG("mode page skipped: the last game's settings (mode %u, track %08X:%08X)", w[5], w[3], w[8]);
        g_eax = (g_eax & ~0xFFu) | 1u;
        g_esp += 12u;
        if (s_room_step != CR_ROOM_NONE) {
            s_stay_hold = 1;            /* the flip goes on when the room has answered */
            return;
        }
        bo3_quickplay_exit_event(1u);
        qp_end("the creating page is up");
        return;
    }
    MEM32(blk + 0x14u) = (uint32_t)bo3_quickplay_mode((int)MEM32(blk + 0x14u), (int)MEM32(blk + 8u));
    /* the track key the page keeps in its list (edx:eax), and the request's two halves */
    qp_call(BO3_SUB(sub_0009AC40, sub_0009AD90, sub_0009AF40), 0u, &lo, &hi);
    MEM32(blk + 0x0Cu) = lo;
    MEM32(blk + 0x20u) = hi;
    MEM32(blk + 0x18u) = bo3_quickplay_flags18(MEM32(blk + 0x18u), MEM8(QP_LANGUAGE));
    QP_LOG("mode page skipped: Single Race, the title's pick of a track");
    g_eax = (g_eax & ~0xFFu) | 1u;
    g_esp += 12u;                           /* ret 8 */
    bo3_quickplay_exit_event(1u);                      /* the state builds the creating page, the title's */
    qp_end("the creating page is up");
}

extern void BO3_SUB(sub_001BD8F0, sub_001BDC10, sub_001BDCB0)(void);    /* the lobby's lock for the start (gset SYSFLAGS), session in esi */
#define QP_NPEERS   BO3_VA(0x005729A8u, 0x00572CB8u, 0x00572D58u)

int bo3_quickplay_countdown_seconds(void) { return s_cd_seconds; }

extern void BO3_SUB(sub_000A3AA0, sub_000A3DF0, sub_000A3E90)(void);    /* the game record into the race (mode, track, laps) */
#define QP_RACE_MODE    BO3_VA(0x00463B00u, 0x00463E20u, 0x00463EC0u)
#define QP_RACE_TRACK   BO3_VA(0x00463BD8u, 0x00463EF8u, 0x00463F98u)

static int s_start_read;        /* this lobby frame's host read was done before the title's frame */

static void qp_start_read(unsigned st, const char *when)
{
    uint32_t m0 = MEM32(QP_RACE_MODE), lo0 = MEM32(QP_RACE_TRACK), hi0 = MEM32(QP_RACE_TRACK + 4u);
    qp_call(BO3_SUB(sub_000A3AA0, sub_000A3DF0, sub_000A3E90), 0u, NULL, NULL);
    if (m0 != MEM32(QP_RACE_MODE) || lo0 != MEM32(QP_RACE_TRACK) || hi0 != MEM32(QP_RACE_TRACK + 4u))
        QP_LOG("start (lobby state %02X, %s): the host races the game's settings as its guests do: mode %u track "
               "%08X:%08X (it had mode %u track %08X:%08X)", st, when, MEM32(QP_RACE_MODE), MEM32(QP_RACE_TRACK),
               MEM32(QP_RACE_TRACK + 4u), m0, lo0, hi0);
    else
        QP_LOG("start (lobby state %02X, %s): the host's race settings are the game's (mode %u track %08X:%08X)", st,
               when, m0, lo0, hi0);
}

/* 1: this copy hosts the lobby's game and the record's start lock is back */
static int qp_start_due(void)
{
    xnet_hle_lobby_view lv;
    uint32_t rec = MEM32(QP_SESSION + 0x38u);
    int host;
    if (!qp_ok_va(rec))
        return 0;
    host = xnet_hle_lobby_view_get(&lv) && lv.np > 0 && !strcmp(lv.name[0], xnet_hle_gamertag());
    return bo3_quickplay_start_reads_record(host, 0x11u, MEM32(rec + 0x114u));
}

void bo3_quickplay_host_start_sync(uint32_t scr)
{
    s_start_read = 0;
    if (!g_xbox_mem_offset || !qp_ok_va(scr) || MEM8(scr + 0x13ACu) != 0x11u || !qp_start_due())
        return;
    qp_start_read(0x11u, "before the gsta");
    s_start_read = 1;
}

void bo3_quickplay_host_start_after(uint32_t scr, unsigned state_before)
{
    if (!g_xbox_mem_offset || !qp_ok_va(scr) || s_start_read || state_before != 0x11u || MEM8(scr + 0x13ACu) != 0x0Bu ||
        !qp_start_due())
        return;
    qp_start_read(0x0Bu, "the lock's answer came in the frame");
}

void bo3_quickplay_lobby_frame(uint32_t scr)
{
    xnet_hle_lobby_view lv;
    qp_cd_in in;
    int k, act, secs = 0;
    s_cd_seconds = 0;
    bo3_quickplay_host_start_sync(scr);    /* every game, BO-384 */
    if (!s_host_game || !g_xbox_mem_offset || !xnet_hle_lobby_view_get(&lv))
        return;
    memset(&in, 0, sizeof in);
    in.host_game = s_host_game && lv.np > 0 && !strcmp(lv.name[0], xnet_hle_gamertag());
    in.players = lv.np > 0 && lv.np <= 8 ? lv.np : 0;
    in.idle = MEM8(scr + 0x13ACu) == 0x01u;
    in.guests_ready = in.players > 1;
    for (k = 1; k < in.players; k++)
        if (lv.ready[k] != 1 && lv.ready[k] != 3)
            in.guests_ready = 0;
    in.now = qp_ms();
    /* stay together: no start while players of the race are on their way back */
    if (bo3_stay_coming() > 0) {
        in.idle = 0;
        in.guests_ready = 0;
    }
    /* the track vote replaces the countdown in these games */
    if (in.host_game) {
        int v = bo3_vote_host_frame(scr, in.players, in.idle, in.guests_ready);
        if (v >= 0) {
            act = v && in.idle && in.guests_ready && in.now >= s_cd.hold_until ? QP_CD_START : QP_CD_NONE;
            if (act == QP_CD_START)
                s_cd.hold_until = in.now + QP_START_COOLDOWN_MS;
            goto start;
        }
    }
    act = bo3_quickplay_countdown(&s_cd, &in, &secs);
    s_cd_seconds = secs;
start:
    if (act == QP_CD_START) {
        /* the host's START (sub_000A1560 at 0xA15ED): its conditions (the title's own host test,
         * linked, alive, a peer) are asked here too, then the lock and the lobby's state 0x11 */
        if (MEM8(scr + 0x13A5u) && MEM8(scr + 0x13A4u) && MEM32(QP_NPEERS) > 1u) {
            uint32_t si = g_esi;
            g_esi = QP_SESSION;
            qp_call(BO3_SUB(sub_001BD8F0, sub_001BDC10, sub_001BDCB0), 0u, NULL, NULL);
            g_esi = si;
            MEM8(scr + 0x13ACu) = 0x11u;
            QP_LOG("the countdown ran out: the host's start");
        }
    }
}

void bo3_quickplay_start(void)
{
    recomp_patch_on_mark("qp_tick", qp_tick);
    recomp_patch_on_mark("cr_step", cr_step);
}

#endif /* QUICKPLAY_PURE_ONLY */
