/*
 * racewait.h - QUICK PLAY / JOIN WITH CODE waiting for a race under way and
 * joining its game when it opens again (BO-365, .handoff/racewait-2026-11-03).
 *
 * A game whose race runs is not on the lobby server's list (its players left
 * the lobby at the start). The server keeps a record of it and says so in an
 * empty gsea answer (TKRACING, TKRACEID ...). Instead of making a game of its
 * own, QUICK PLAY (or JOIN WITH CODE, when the code's game is racing) waits on
 * the Quick Match page: it searches again every RW_POLL_MS with TKWAITFOR=<the
 * race's IDENT> (online.c's lobby tag), the server answers with the state of
 * that race (TKWAITSTATE ...) and, once the race's host has made the game
 * again and there is a seat for us, lists that game first: the title's own
 * Quick Match then joins it as it joins any game it found. No pad press: the
 * page is held and searched again with quickplay.c's ops.
 *
 * This file is the pure part (tests/racewait). quickplay.c fills rw_in from
 * the page and the toolkit's lobby view and does what bo3_racewait_step says.
 */
#ifndef BO3_RACEWAIT_H
#define BO3_RACEWAIT_H

#include <stddef.h>

/* the machine */
enum { RW_OFF, RW_WAIT, RW_JOINING, RW_LEAVING };
/* how a wait ended */
enum { RW_WHY_NONE, RW_WHY_GONE, RW_WHY_FULL, RW_WHY_TIMEOUT, RW_WHY_CANCEL, RW_WHY_SERVER, RW_WHY_JOINED,
       RW_WHY_MISSED };     /* the server listed the game ("open") but the title's own filter kept nothing */
/* what the page does on this tick:
 *   POLL     search again (qp_research; the lobby tag adds TKWAITFOR)
 *   HOLD     nothing in flight or the title's join runs: keep the page as it is
 *            (an answer that kept nothing gets its results flag cleared, so
 *            NO GAMES FOUND is never built)
 *   REFUSED  the join failed: the page byte to QP_SUB_IDLE (no error box), the
 *            next poll comes after RW_POLL_MS
 *   FALLBACK the wait is over without a game (s->why): QUICK PLAY makes its own
 *            game, JOIN WITH CODE goes back to the code entry
 *   CANCEL   B: the page's Back exit
 *   JOINED   only from bo3_racewait_finish */
enum { RW_ACT_NONE, RW_ACT_POLL, RW_ACT_HOLD, RW_ACT_JOINED, RW_ACT_FALLBACK, RW_ACT_CANCEL, RW_ACT_REFUSED,
       RW_ACT_CREATE };     /* the end message was up RW_LEAVE_MS: QUICK PLAY makes its own game now */
/* the server's TKWAITSTATE, as the toolkit maps it (xnet_hle.h XNET_WAIT_*) */
enum { RW_WS_NONE, RW_WS_RACING, RW_WS_LOBBY, RW_WS_OPEN, RW_WS_FULL, RW_WS_GONE };

#define RW_POLL_MS      3000ull
#define RW_LEAVE_MS     3000ull     /* QUICK PLAY: the "ended" / "full" line stays this long before its own create */
#define RW_MAX_MS       480000ull   /* 8 min: longer than any race + lobby; the server says gone earlier in every known case */
#define RW_JOIN_FAILS   6           /* six "full"/"gone" joins while waiting: give up (why FULL) */
#define RW_ANSWER_MS    12000ull    /* a poll with no answer this long (4 polls' time): the server is gone */
#define RW_STALE_MAX    3           /* answers without TKWAITSTATE in a row: the server no longer knows the race */
#define RW_OPEN_MISSES  5           /* "open" answers in a row the title's per-record filter (ping bands, own
                                     * game, full) kept nothing of: it will not take that game (why MISSED) */

typedef struct rw_state {
    int state, why;
    int race_id;                /* TKRACEID we wait for (TKWAITFOR on every poll) */
    int join_fails;             /* joins that failed while waiting */
    int holding;                /* the last poll's answer came: nothing in flight */
    int stale;                  /* answers in a row without a TKWAITSTATE of their own */
    int open_misses;            /* "open" answers in a row that kept nothing */
    unsigned long long leave_ms;    /* RW_LEAVING with a message held: when QUICK PLAY may create (0: not holding) */
    unsigned long long since_ms, next_poll_ms, deadline_ms, poll_ms, answer_ms;
    unsigned last_seq;          /* wait_seq of the last answer taken */
    unsigned last_answers;      /* gsea answers seen when the last poll went out */
    unsigned polls;
    int players, secs, queue, wait_state;   /* for the line (secs counts on between answers) */
    int secs_at;                /* the server's seconds at answer_ms */
    char name[32];              /* TKRACENAME, then TKWAITNAME (the game made again) */
} rw_state;

typedef struct {
    int sub, ready, count, active;          /* the Quick Match page, as qp_in */
    int cancel, join_failed;                /* B held; state 5 failed */
    int lv_wait_state, lv_players, lv_secs, lv_queue; unsigned lv_seq; const char *lv_name;
    int lv_racing, lv_race_id, lv_race_players, lv_race_max, lv_race_secs; const char *lv_race_name;
    unsigned lv_answers;                    /* gsea answers the toolkit has seen (got[XNET_LV_GSEA]) */
    unsigned long long now;
} rw_in;

/* a QUICK PLAY / JOIN WITH CODE search that found nothing: wait for the race
 * the answer named instead of creating / going back? 1 when the server named
 * one (TKRACING > 0, TKRACEID) with room for one more. join_with_code: the
 * server only names the code's own race to a search with the code, so the
 * rule is the same. */
int  bo3_racewait_should_wait(const rw_in *in, int join_with_code);
/* RW_WAIT for lv_race_id; the first poll is due at once */
void bo3_racewait_begin(rw_state *s, const rw_in *in);
/* one tick of the page while state != RW_OFF; returns RW_ACT_* */
int  bo3_racewait_step(rw_state *s, const rw_in *in);
/* QUICK PLAY after RW_ACT_FALLBACK: hold the page RW_LEAVE_MS when the wait's end has a line
 * (gone, timeout, full); the step then answers HOLD, CANCEL (B) or CREATE. 0: nothing to read, create now. */
int  bo3_racewait_hold(rw_state *s, unsigned long long now);
/* the flow left the Quick Match page (car select: joined; the online page)
 * or QUICK PLAY ended some other way: RW_OFF. Returns RW_ACT_JOINED when the
 * title's join was running, else RW_ACT_NONE. */
int  bo3_racewait_finish(rw_state *s, int went_on);
/* the line's text (statustext ids) for lang; "" when nothing to show */
void bo3_racewait_line(const rw_state *s, int lang, char *out, size_t n);
/* names for the log */
const char *bo3_racewait_why_name(int why);
const char *bo3_racewait_ws_name(int ws);

/* ---- stay together: a guest back first waits for its host's game ----
 *
 * After a race a guest's Quick Match looks only for its host's game made
 * again (quickplay.c, the stay-join). Its searches carry TKWAITFOR=<the
 * race's IDENT> as a race wait's do, so the server answers how that race
 * stands: racing (the host is still racing, on its results or at the
 * autosave's question: it has not signed in again), with TKWAITHOST=1 once
 * it has and is making the game, open (the game is there: listed first, the
 * title joins it), gone (TKWAITWHY: the host quit or left, did not come back,
 * the race's time ran out). The guest waits as long as the server says
 * racing / lobby / open; only gone (or full) ends it, with a line that says
 * why, and back to the online page, no box of the title's. An old server
 * says nothing: the old ST_JOIN_WAIT_MS deadline and the title's box. */
enum { RW_SW_KEEP, RW_SW_OLD, RW_SW_GONE, RW_SW_FULL, RW_SW_MISSED };
/* TKWAITWHY, as the client keeps it */
enum { RW_SWHY_NONE, RW_SWHY_QUIT, RW_SWHY_LEFT, RW_SWHY_ABSENT, RW_SWHY_TIME, RW_SWHY_FULL };
#define RW_STAY_MAX_MS  780000ull   /* 13 min: past the server's own hold of a race (720 s from its start) */
#define RW_STAY_LEAVE_MS 4000ull    /* the end line stays this long before the online page */

typedef struct rw_stay {
    int on;                     /* an answer said how the race stands (a server that knows races) */
    int ws, host_back, why;     /* the last answer's TKWAITSTATE, TKWAITHOST, TKWAITWHY */
    int end;                    /* RW_SW_GONE / _FULL / _MISSED once over, else 0 */
    int open_misses;            /* "open" answers in a row the title's filter kept nothing of */
    unsigned last_seq;          /* wait_seq of the last answer taken */
    unsigned long long since_ms;    /* the guest began waiting */
    unsigned long long leave_ms;    /* the end line is up until then (0: not ending) */
} rw_stay;

void bo3_racewait_stay_begin(rw_stay *s, unsigned seq, unsigned long long now);
/* a search's answer that kept nothing: seq is the toolkit's wait_seq (it
 * moves when an answer carried TKWAITSTATE), ws/host_back/why that answer's
 * keys. RW_SW_KEEP: wait on; RW_SW_OLD: no word from the server (yet) about
 * the race, the old deadline decides; GONE / FULL / MISSED: over (s->end) */
int  bo3_racewait_stay_answer(rw_stay *s, unsigned seq, int ws, int host_back, int why, unsigned long long now);
/* the deadline of the guest's search: the old one until the server spoke */
unsigned long long bo3_racewait_stay_deadline(const rw_stay *s, unsigned long long old_deadline);
/* the Quick Match page's line, "" for the stay's own RETURNING TO THE LOBBY... */
void bo3_racewait_stay_line(const rw_stay *s, int lang, unsigned long long now, char *out, size_t n);
/* TKWAITWHY's text to RW_SWHY_* */
int  bo3_racewait_stay_why(const char *v);

#endif
