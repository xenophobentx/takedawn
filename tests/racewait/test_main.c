/* Waiting for a race under way (racewait.c, BO-365). */
#include <stdio.h>
#include <string.h>

#include "languages.h"
#include "quickplay.h"
#include "racewait.h"

static int s_fail;

#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); s_fail++; } } while (0)

/* the empty answer that names a race: TKRACING=1 TKRACEID=7, 2 of 6 players, 40 s in */
static rw_in named(void)
{
    rw_in r;
    memset(&r, 0, sizeof r);
    r.active = 1;
    r.sub = QP_SUB_WAIT;
    r.ready = 1;
    r.lv_racing = 1;
    r.lv_race_id = 7;
    r.lv_race_players = 2;
    r.lv_race_max = 6;
    r.lv_race_secs = 40;
    r.lv_race_name = "HostA";
    r.now = 100000;
    return r;
}

/* the page right after a poll went out: searching */
static rw_in searching(unsigned long long now, unsigned seq)
{
    rw_in r = named();
    r.ready = 0;
    r.lv_seq = seq;
    r.now = now;
    return r;
}

/* a poll's answer that kept nothing, with the wait keys */
static rw_in answer(unsigned long long now, unsigned seq, int ws, int players, int secs, int queue)
{
    rw_in r = named();
    r.lv_seq = seq;
    r.lv_wait_state = ws;
    r.lv_players = players;
    r.lv_secs = secs;
    r.lv_queue = queue;
    r.now = now;
    return r;
}

static void should_wait_cases(void)
{
    rw_in r = named();
    CHECK(bo3_racewait_should_wait(&r, 0));
    CHECK(bo3_racewait_should_wait(&r, 1));         /* JOIN WITH CODE: the code's own race */
    r.lv_racing = 0;
    CHECK(!bo3_racewait_should_wait(&r, 0));        /* nothing racing (or an old server) */
    r = named(); r.lv_race_id = 0;
    CHECK(!bo3_racewait_should_wait(&r, 0));
    r = named(); r.lv_race_players = 6;
    CHECK(!bo3_racewait_should_wait(&r, 0));        /* no room after the racers */
    r.lv_race_max = 0;
    CHECK(bo3_racewait_should_wait(&r, 0));         /* no max said: the server's filter stands */
}

/* the first poll at once, then one every RW_POLL_MS, HOLD in between */
static void cadence_cases(void)
{
    rw_state s;
    rw_in r = named();
    unsigned long long t = r.now;
    bo3_racewait_begin(&s, &r);
    CHECK(s.state == RW_WAIT && s.race_id == 7 && s.players == 2 && s.secs == 40 && !strcmp(s.name, "HostA"));
    CHECK(s.wait_state == RW_WS_RACING);
    CHECK(bo3_racewait_step(&s, &r) == RW_ACT_POLL);
    CHECK(s.polls == 1);
    /* in flight */
    r = searching(t + 500, 0);
    CHECK(bo3_racewait_step(&s, &r) == RW_ACT_HOLD);
    /* the answer: racing, 2 players, 41 s, first in the queue */
    r = answer(t + 800, 1, RW_WS_RACING, 2, 41, 0);
    CHECK(bo3_racewait_step(&s, &r) == RW_ACT_HOLD);
    CHECK(s.holding && s.secs == 41 && s.last_seq == 1);
    /* the page's results flag was cleared by the glue: holding until the next poll */
    r = searching(t + 2000, 1);
    CHECK(bo3_racewait_step(&s, &r) == RW_ACT_HOLD);
    /* the seconds count on between answers */
    CHECK(s.secs == 42);
    r = searching(t + RW_POLL_MS - 1, 1);
    CHECK(bo3_racewait_step(&s, &r) == RW_ACT_HOLD);
    r = searching(t + RW_POLL_MS, 1);
    CHECK(bo3_racewait_step(&s, &r) == RW_ACT_POLL);
    CHECK(s.polls == 2);
    /* the page fading: nothing */
    r = searching(t + RW_POLL_MS + 10, 1); r.active = 0;
    CHECK(bo3_racewait_step(&s, &r) == RW_ACT_NONE);
}

/* the race's host made the game again: lobby (seats kept), then open: the
 * title takes the first record and joins; the flow going on is "joined" */
static void join_cases(void)
{
    rw_state s;
    rw_in r = named();
    unsigned long long t = r.now;
    bo3_racewait_begin(&s, &r);
    CHECK(bo3_racewait_step(&s, &r) == RW_ACT_POLL);
    r = answer(t + 500, 1, RW_WS_LOBBY, 2, 200, 1);
    r.lv_name = "HostA's game";
    CHECK(bo3_racewait_step(&s, &r) == RW_ACT_HOLD);
    CHECK(s.wait_state == RW_WS_LOBBY && s.queue == 1 && !strcmp(s.name, "HostA's game"));
    r = searching(t + 3500, 1);
    CHECK(bo3_racewait_step(&s, &r) == RW_ACT_POLL);
    /* open: the answer has the game; the title picks it (7), moves (4), joins (5) */
    r = answer(t + 3800, 2, RW_WS_OPEN, 2, 203, 0);
    r.count = 1;
    CHECK(bo3_racewait_step(&s, &r) == RW_ACT_HOLD);
    CHECK(s.state == RW_JOINING && s.wait_state == RW_WS_OPEN);
    r.sub = QP_SUB_PICKED;
    CHECK(bo3_racewait_step(&s, &r) == RW_ACT_HOLD);
    r.sub = 4;
    CHECK(bo3_racewait_step(&s, &r) == RW_ACT_HOLD);
    r.sub = QP_SUB_JOINING;
    CHECK(bo3_racewait_step(&s, &r) == RW_ACT_HOLD);
    /* B while the title's join runs: held */
    r.cancel = 1;
    CHECK(bo3_racewait_step(&s, &r) == RW_ACT_HOLD && s.state == RW_JOINING);
    CHECK(bo3_racewait_finish(&s, 1) == RW_ACT_JOINED);
    CHECK(s.state == RW_OFF && s.why == RW_WHY_JOINED);
    CHECK(bo3_racewait_finish(&s, 1) == RW_ACT_NONE);
    /* a wait that ends while not joining is not "joined" */
    r = named();
    bo3_racewait_begin(&s, &r);
    CHECK(bo3_racewait_finish(&s, 1) == RW_ACT_NONE && s.state == RW_OFF);
}

/* a refused join (the racers' seats, or it started again) keeps waiting,
 * up to RW_JOIN_FAILS */
static void refused_cases(void)
{
    rw_state s;
    rw_in r = named();
    unsigned long long t = r.now;
    int k, act = 0;
    bo3_racewait_begin(&s, &r);
    for (k = 0; k < RW_JOIN_FAILS; k++) {
        CHECK(bo3_racewait_step(&s, &r) == RW_ACT_POLL);
        r = answer(t + 100, (unsigned)k + 1, RW_WS_OPEN, 3, 100, 0);
        r.count = 1;
        r.sub = QP_SUB_JOINING;
        CHECK(bo3_racewait_step(&s, &r) == RW_ACT_HOLD && s.state == RW_JOINING);
        r.join_failed = 1;
        act = bo3_racewait_step(&s, &r);
        if (k + 1 < RW_JOIN_FAILS) {
            CHECK(act == RW_ACT_REFUSED && s.state == RW_WAIT && s.join_fails == k + 1);
            /* the page byte is IDLE now: no poll before RW_POLL_MS */
            r = searching(t + 200, (unsigned)k + 1);
            r.sub = QP_SUB_IDLE;
            CHECK(bo3_racewait_step(&s, &r) == RW_ACT_HOLD);
            t += RW_POLL_MS;
            r = searching(t + 100, (unsigned)k + 1);
            r.sub = QP_SUB_IDLE;
        }
    }
    CHECK(act == RW_ACT_FALLBACK && s.state == RW_LEAVING && s.why == RW_WHY_FULL);
    CHECK(bo3_racewait_step(&s, &r) == RW_ACT_NONE);
    /* B pressed while the join was running: the failure is the safe point */
    r = named();
    t = r.now;
    bo3_racewait_begin(&s, &r);
    CHECK(bo3_racewait_step(&s, &r) == RW_ACT_POLL);
    r = answer(t + 100, 1, RW_WS_OPEN, 3, 100, 0);
    r.count = 1; r.sub = QP_SUB_JOINING; r.cancel = 1;
    CHECK(bo3_racewait_step(&s, &r) == RW_ACT_HOLD);
    r.join_failed = 1;
    CHECK(bo3_racewait_step(&s, &r) == RW_ACT_CANCEL && s.why == RW_WHY_CANCEL);
}

static void fallback_cases(void)
{
    rw_state s;
    rw_in r = named();
    unsigned long long t = r.now;
    /* gone */
    bo3_racewait_begin(&s, &r);
    CHECK(bo3_racewait_step(&s, &r) == RW_ACT_POLL);
    r = answer(t + 300, 1, RW_WS_GONE, 0, 0, 0);
    CHECK(bo3_racewait_step(&s, &r) == RW_ACT_FALLBACK && s.why == RW_WHY_GONE && s.state == RW_LEAVING);
    CHECK(bo3_racewait_step(&s, &r) == RW_ACT_NONE);
    /* QUICK PLAY holds the end line RW_LEAVE_MS, then creates */
    CHECK(bo3_racewait_hold(&s, r.now) == 1 && s.leave_ms == r.now + RW_LEAVE_MS);
    r.now += RW_LEAVE_MS - 1;
    CHECK(bo3_racewait_step(&s, &r) == RW_ACT_HOLD && s.state == RW_LEAVING);
    r.now += 1;
    CHECK(bo3_racewait_step(&s, &r) == RW_ACT_CREATE && s.leave_ms == 0);
    CHECK(bo3_racewait_step(&s, &r) == RW_ACT_NONE);
    /* B during the hold goes back, no create */
    r = named();
    bo3_racewait_begin(&s, &r);
    CHECK(bo3_racewait_step(&s, &r) == RW_ACT_POLL);
    r = answer(t + 300, 1, RW_WS_GONE, 0, 0, 0);
    CHECK(bo3_racewait_step(&s, &r) == RW_ACT_FALLBACK && bo3_racewait_hold(&s, r.now));
    r.now += 1000; r.cancel = 1;
    CHECK(bo3_racewait_step(&s, &r) == RW_ACT_CANCEL && s.why == RW_WHY_CANCEL);
    CHECK(bo3_racewait_step(&s, &r) == RW_ACT_NONE);
    /* a fade (page inactive) touches nothing; causes without a line create at once */
    r = named();
    bo3_racewait_begin(&s, &r);
    CHECK(bo3_racewait_step(&s, &r) == RW_ACT_POLL);
    r = answer(t + 300, 1, RW_WS_GONE, 0, 0, 0);
    CHECK(bo3_racewait_step(&s, &r) == RW_ACT_FALLBACK && bo3_racewait_hold(&s, r.now));
    r.active = 0; r.now += 5000;
    CHECK(bo3_racewait_step(&s, &r) == RW_ACT_NONE && s.leave_ms != 0);
    s.why = RW_WHY_SERVER; s.leave_ms = 0;
    CHECK(bo3_racewait_hold(&s, r.now) == 0);
    /* full */
    r = named();
    bo3_racewait_begin(&s, &r);
    CHECK(bo3_racewait_step(&s, &r) == RW_ACT_POLL);
    r = answer(t + 300, 1, RW_WS_FULL, 6, 300, 2);
    CHECK(bo3_racewait_step(&s, &r) == RW_ACT_FALLBACK && s.why == RW_WHY_FULL);
    /* timeout: only at a safe point, at the deadline */
    r = named();
    bo3_racewait_begin(&s, &r);
    CHECK(bo3_racewait_step(&s, &r) == RW_ACT_POLL);
    r = answer(t + 300, 1, RW_WS_RACING, 2, 60, 0);
    CHECK(bo3_racewait_step(&s, &r) == RW_ACT_HOLD);
    r = searching(t + RW_MAX_MS, 1);
    CHECK(bo3_racewait_step(&s, &r) == RW_ACT_FALLBACK && s.why == RW_WHY_TIMEOUT);
    /* ... and at an answer past it */
    r = named();
    bo3_racewait_begin(&s, &r);
    CHECK(bo3_racewait_step(&s, &r) == RW_ACT_POLL);
    r = answer(t + RW_MAX_MS + 1, 1, RW_WS_RACING, 2, 600, 0);
    CHECK(bo3_racewait_step(&s, &r) == RW_ACT_FALLBACK && s.why == RW_WHY_TIMEOUT);
    /* open, but the title's own filter keeps the game out: RW_OPEN_MISSES answers */
    {
        int k, act = 0;
        r = named();
        bo3_racewait_begin(&s, &r);
        for (k = 0; k < RW_OPEN_MISSES; k++) {
            r = searching(t + (unsigned long long)k * RW_POLL_MS, (unsigned)k);
            CHECK(bo3_racewait_step(&s, &r) == RW_ACT_POLL);
            /* one "lobby" answer in between starts the count again */
            r = answer(t + (unsigned long long)k * RW_POLL_MS + 100, (unsigned)k + 1, k == 1 ? RW_WS_LOBBY : RW_WS_OPEN, 1, 70, 0);
            act = bo3_racewait_step(&s, &r);
            CHECK(act == RW_ACT_HOLD);
        }
        CHECK(s.open_misses == RW_OPEN_MISSES - 2);
        r = searching(t + (unsigned long long)k * RW_POLL_MS, (unsigned)k);
        CHECK(bo3_racewait_step(&s, &r) == RW_ACT_POLL);
        r = answer(t + (unsigned long long)k * RW_POLL_MS + 100, (unsigned)k + 1, RW_WS_OPEN, 1, 70, 0);
        CHECK(bo3_racewait_step(&s, &r) == RW_ACT_HOLD);
        r = searching(t + (unsigned long long)(k + 1) * RW_POLL_MS, (unsigned)k + 1);
        CHECK(bo3_racewait_step(&s, &r) == RW_ACT_POLL);
        r = answer(t + (unsigned long long)(k + 1) * RW_POLL_MS + 100, (unsigned)k + 2, RW_WS_OPEN, 1, 70, 0);
        act = bo3_racewait_step(&s, &r);
        CHECK(act == RW_ACT_FALLBACK && s.why == RW_WHY_MISSED);
        {
            char t[96];
            bo3_racewait_line(&s, BO3_LANG_ENGLISH, t, sizeof t);
            CHECK(t[0] == 0);       /* no claim about the game: QUICK PLAY just makes its own */
        }
    }
    /* server: a poll never answered */
    r = named();
    bo3_racewait_begin(&s, &r);
    CHECK(bo3_racewait_step(&s, &r) == RW_ACT_POLL);
    r = searching(t + RW_ANSWER_MS - 1, 0);
    CHECK(bo3_racewait_step(&s, &r) == RW_ACT_HOLD);
    r = searching(t + RW_ANSWER_MS, 0);
    CHECK(bo3_racewait_step(&s, &r) == RW_ACT_FALLBACK && s.why == RW_WHY_SERVER);
    /* server: answers without wait keys of their own (an old server after a restart) */
    {
        int k, act = 0;
        r = named();
        bo3_racewait_begin(&s, &r);
        for (k = 0; k < RW_STALE_MAX; k++) {
            r = searching(t + (unsigned long long)k * RW_POLL_MS, 0);
            CHECK(bo3_racewait_step(&s, &r) == RW_ACT_POLL);
            r = answer(t + (unsigned long long)k * RW_POLL_MS + 100, 0, RW_WS_NONE, 0, 0, 0);
            act = bo3_racewait_step(&s, &r);
            CHECK(k + 1 < RW_STALE_MAX ? act == RW_ACT_HOLD : act == RW_ACT_FALLBACK);
        }
        CHECK(s.why == RW_WHY_SERVER);
    }
}

/* B: only with nothing in flight */
static void cancel_cases(void)
{
    rw_state s;
    rw_in r = named();
    unsigned long long t = r.now;
    bo3_racewait_begin(&s, &r);
    r.cancel = 1;
    CHECK(bo3_racewait_step(&s, &r) == RW_ACT_CANCEL && s.why == RW_WHY_CANCEL);
    /* a poll in flight: held until its answer */
    r = named();
    bo3_racewait_begin(&s, &r);
    CHECK(bo3_racewait_step(&s, &r) == RW_ACT_POLL);
    r = searching(t + 100, 0); r.cancel = 1;
    CHECK(bo3_racewait_step(&s, &r) == RW_ACT_HOLD && s.state == RW_WAIT);
    r = answer(t + 200, 1, RW_WS_RACING, 2, 50, 0); r.cancel = 1;
    CHECK(bo3_racewait_step(&s, &r) == RW_ACT_CANCEL);
    /* the answer with the game: the title joins, B waits */
    r = named();
    bo3_racewait_begin(&s, &r);
    CHECK(bo3_racewait_step(&s, &r) == RW_ACT_POLL);
    r = answer(t + 200, 1, RW_WS_OPEN, 2, 50, 0); r.count = 1; r.cancel = 1;
    CHECK(bo3_racewait_step(&s, &r) == RW_ACT_HOLD && s.state == RW_JOINING);
    /* off: nothing */
    memset(&s, 0, sizeof s);
    CHECK(bo3_racewait_step(&s, &r) == RW_ACT_NONE);
}

static void line_cases(void)
{
    rw_state s;
    rw_in r = named();
    char t[96];
    int ws;
    bo3_racewait_begin(&s, &r);
    s.secs = 83;
    bo3_racewait_line(&s, BO3_LANG_ENGLISH, t, sizeof t);
    CHECK(!strcmp(t, "RACE IN PROGRESS - 2 PLAYERS - 1:23 - WAITING"));
    s.wait_state = RW_WS_LOBBY; s.queue = 2;
    bo3_racewait_line(&s, BO3_LANG_ENGLISH, t, sizeof t);
    CHECK(!strcmp(t, "THE RACE IS OVER - 2 AHEAD OF YOU"));
    s.queue = 0;
    bo3_racewait_line(&s, BO3_LANG_ENGLISH, t, sizeof t);
    CHECK(!strcmp(t, "THE RACE IS OVER - JOINING THE GAME..."));
    s.wait_state = RW_WS_OPEN;
    bo3_racewait_line(&s, BO3_LANG_ENGLISH, t, sizeof t);
    CHECK(!strcmp(t, "THE RACE IS OVER - JOINING THE GAME..."));
    s.wait_state = RW_WS_RACING; s.state = RW_JOINING;
    bo3_racewait_line(&s, BO3_LANG_ENGLISH, t, sizeof t);
    CHECK(!strcmp(t, "THE RACE IS OVER - JOINING THE GAME..."));
    s.state = RW_LEAVING; s.why = RW_WHY_GONE;
    bo3_racewait_line(&s, BO3_LANG_ENGLISH, t, sizeof t);
    CHECK(!strcmp(t, "THE RACE ENDED AND NOBODY MADE THE GAME AGAIN"));
    s.why = RW_WHY_TIMEOUT;
    bo3_racewait_line(&s, BO3_LANG_ENGLISH, t, sizeof t);
    CHECK(!strcmp(t, "THE RACE ENDED AND NOBODY MADE THE GAME AGAIN"));
    s.why = RW_WHY_FULL;
    bo3_racewait_line(&s, BO3_LANG_ENGLISH, t, sizeof t);
    CHECK(!strcmp(t, "THE GAME IS FULL AFTER THE RACE"));
    s.why = RW_WHY_CANCEL;
    bo3_racewait_line(&s, BO3_LANG_ENGLISH, t, sizeof t);
    CHECK(t[0] == 0);
    s.why = RW_WHY_SERVER;
    bo3_racewait_line(&s, BO3_LANG_ENGLISH, t, sizeof t);
    CHECK(t[0] == 0);
    s.state = RW_OFF; s.why = RW_WHY_GONE;
    bo3_racewait_line(&s, BO3_LANG_ENGLISH, t, sizeof t);
    CHECK(t[0] == 0);
    /* the widest: 8 players, 20 minutes, 7 ahead; every line under 64 */
    for (ws = RW_WS_NONE; ws <= RW_WS_GONE; ws++) {
        bo3_racewait_begin(&s, &r);
        s.players = 8; s.secs = 20 * 60 + 59; s.queue = 7; s.wait_state = ws;
        bo3_racewait_line(&s, BO3_LANG_ENGLISH, t, sizeof t);
        CHECK(strlen(t) > 0 && strlen(t) < 64);
    }
    /* another language falls back to English */
    bo3_racewait_begin(&s, &r);
    s.secs = 5;
    bo3_racewait_line(&s, BO3_LANG_SPANISH, t, sizeof t);
    CHECK(!strcmp(t, "RACE IN PROGRESS - 2 PLAYERS - 0:05 - WAITING"));
    CHECK(!strcmp(bo3_racewait_why_name(RW_WHY_JOINED), "joined") && !strcmp(bo3_racewait_ws_name(RW_WS_LOBBY), "lobby"));
}

/* stay together: the guest's wait for its host's game */
static void stay_cases(void)
{
    rw_stay s;
    char t[96];
    unsigned long long t0 = 50000;
    bo3_racewait_stay_begin(&s, 4, t0);
    /* no answer with the wait keys yet (or an old server): the old deadline */
    CHECK(bo3_racewait_stay_answer(&s, 4, 0, 0, 0, t0 + 2000) == RW_SW_OLD);
    CHECK(bo3_racewait_stay_deadline(&s, t0 + 20000) == t0 + 20000);
    bo3_racewait_stay_line(&s, BO3_LANG_ENGLISH, t0 + 2000, t, sizeof t);
    CHECK(t[0] == 0);                                   /* RETURNING TO THE LOBBY... stays */
    /* racing: the host has not signed in again (its save, its question, its sign-in) */
    CHECK(bo3_racewait_stay_answer(&s, 5, RW_WS_RACING, 0, 0, t0 + 4000) == RW_SW_KEEP);
    CHECK(bo3_racewait_stay_deadline(&s, t0 + 20000) == t0 + RW_STAY_MAX_MS);
    bo3_racewait_stay_line(&s, BO3_LANG_ENGLISH, t0 + 65000, t, sizeof t);
    CHECK(!strcmp(t, "WAITING FOR THE HOST TO COME BACK 1:05 (B: LEAVE)"));
    /* an answer without its own keys changes nothing */
    CHECK(bo3_racewait_stay_answer(&s, 5, 0, 0, 0, t0 + 66000) == RW_SW_KEEP);
    /* the host signed in again */
    CHECK(bo3_racewait_stay_answer(&s, 6, RW_WS_RACING, 1, 0, t0 + 70000) == RW_SW_KEEP);
    bo3_racewait_stay_line(&s, BO3_LANG_ENGLISH, t0 + 70000, t, sizeof t);
    CHECK(!strcmp(t, "THE HOST IS BACK - MAKING THE GAME AGAIN"));
    /* open: the title joins */
    CHECK(bo3_racewait_stay_answer(&s, 7, RW_WS_OPEN, 0, 0, t0 + 72000) == RW_SW_KEEP);
    bo3_racewait_stay_line(&s, BO3_LANG_ENGLISH, t0 + 72000, t, sizeof t);
    CHECK(!strcmp(t, "THE HOST IS BACK - JOINING THE GAME..."));
    /* five open answers that kept nothing: the title will not take it */
    CHECK(bo3_racewait_stay_answer(&s, 8, RW_WS_OPEN, 0, 0, t0 + 75000) == RW_SW_KEEP);
    CHECK(bo3_racewait_stay_answer(&s, 9, RW_WS_OPEN, 0, 0, t0 + 78000) == RW_SW_KEEP);
    CHECK(bo3_racewait_stay_answer(&s, 10, RW_WS_OPEN, 0, 0, t0 + 81000) == RW_SW_KEEP);
    CHECK(bo3_racewait_stay_answer(&s, 11, RW_WS_OPEN, 0, 0, t0 + 84000) == RW_SW_MISSED);
    CHECK(bo3_racewait_stay_answer(&s, 12, RW_WS_RACING, 0, 0, t0 + 87000) == RW_SW_MISSED);   /* over is over */
    /* gone: why decides the line */
    bo3_racewait_stay_begin(&s, 0, t0);
    CHECK(bo3_racewait_stay_answer(&s, 1, RW_WS_GONE, 0, bo3_racewait_stay_why("quit"), t0 + 3000) == RW_SW_GONE);
    bo3_racewait_stay_line(&s, BO3_LANG_ENGLISH, t0 + 3000, t, sizeof t);
    CHECK(!strcmp(t, "THE HOST LEFT - THE GAME IS OVER"));
    bo3_racewait_stay_begin(&s, 0, t0);
    bo3_racewait_stay_answer(&s, 1, RW_WS_GONE, 0, bo3_racewait_stay_why("absent"), t0 + 3000);
    bo3_racewait_stay_line(&s, BO3_LANG_ENGLISH, t0 + 3000, t, sizeof t);
    CHECK(!strcmp(t, "THE HOST DID NOT COME BACK - THE GAME IS OVER"));
    bo3_racewait_stay_begin(&s, 0, t0);
    bo3_racewait_stay_answer(&s, 1, RW_WS_GONE, 0, bo3_racewait_stay_why(""), t0 + 3000);
    bo3_racewait_stay_line(&s, BO3_LANG_ENGLISH, t0 + 3000, t, sizeof t);
    CHECK(!strcmp(t, "THE GAME IS OVER"));
    bo3_racewait_stay_begin(&s, 0, t0);
    CHECK(bo3_racewait_stay_answer(&s, 1, RW_WS_FULL, 0, 0, t0 + 3000) == RW_SW_FULL);
    bo3_racewait_stay_line(&s, BO3_LANG_ENGLISH, t0 + 3000, t, sizeof t);
    CHECK(!strcmp(t, "THE GAME IS FULL"));
    CHECK(bo3_racewait_stay_why("left") == RW_SWHY_LEFT && bo3_racewait_stay_why("time") == RW_SWHY_TIME &&
          bo3_racewait_stay_why("x") == RW_SWHY_NONE && bo3_racewait_stay_why(NULL) == RW_SWHY_NONE);
}

int main(void)
{
    should_wait_cases();
    stay_cases();
    cadence_cases();
    join_cases();
    refused_cases();
    fallback_cases();
    cancel_cases();
    line_cases();
    if (s_fail) {
        printf("racewait: %d failure(s)\n", s_fail);
        return 1;
    }
    printf("racewait: all passed\n");
    return 0;
}
