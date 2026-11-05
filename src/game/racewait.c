/*
 * racewait.c - waiting for a race under way (racewait.h). Pure: no title
 * memory, no toolkit; quickplay.c is the glue.
 */
#include "racewait.h"

#include <stdio.h>
#include <string.h>

#include "quickplay.h"
#include "statustext.h"

int bo3_racewait_should_wait(const rw_in *in, int join_with_code)
{
    (void)join_with_code;   /* the server filters by the code: the same rule */
    if (in->lv_racing <= 0 || in->lv_race_id <= 0)
        return 0;
    if (in->lv_race_max > 0 && in->lv_race_players >= in->lv_race_max)
        return 0;           /* no room after the racers (the server leaves these out too) */
    return 1;
}

static void rw_name(char *dst, const char *src)
{
    size_t i = 0;
    if (src)
        for (; i + 1 < 32 && src[i]; i++)
            dst[i] = src[i];
    dst[i] = 0;
}

void bo3_racewait_begin(rw_state *s, const rw_in *in)
{
    memset(s, 0, sizeof *s);
    s->state = RW_WAIT;
    s->race_id = in->lv_race_id;
    s->since_ms = in->now;
    s->deadline_ms = in->now + RW_MAX_MS;
    s->next_poll_ms = in->now;          /* the first poll at once: the server ranks waiters by arrival */
    s->holding = 1;                     /* the empty answer that named the race is in */
    s->last_seq = in->lv_seq;
    s->players = in->lv_race_players;
    s->secs_at = s->secs = in->lv_race_secs > 0 ? in->lv_race_secs : 0;
    s->answer_ms = in->now;
    s->wait_state = RW_WS_RACING;
    rw_name(s->name, in->lv_race_name);
}

static int rw_end(rw_state *s, int why, int act)
{
    s->state = RW_LEAVING;
    s->why = why;
    s->holding = 1;
    return act;
}

/* an answer to our poll: its wait keys, when it carried its own */
static void rw_take(rw_state *s, const rw_in *in)
{
    if (in->lv_seq == s->last_seq) {
        s->stale++;
        return;
    }
    s->last_seq = in->lv_seq;
    s->stale = 0;
    s->wait_state = in->lv_wait_state;
    if (in->lv_players > 0)
        s->players = in->lv_players;
    s->queue = in->lv_queue > 0 ? in->lv_queue : 0;
    s->secs_at = s->secs = in->lv_secs > 0 ? in->lv_secs : s->secs;
    s->answer_ms = in->now;
    if (in->lv_name && in->lv_name[0])
        rw_name(s->name, in->lv_name);
}

/* the title's own join is running: it picked a record (7), moves into the
 * room (4), or its gjoi waits (5) */
static int rw_joining(const rw_in *in)
{
    return in->count > 0 || in->sub == QP_SUB_PICKED || in->sub == 4 ||
           (in->sub == QP_SUB_JOINING && !in->join_failed);
}

int bo3_racewait_step(rw_state *s, const rw_in *in)
{
    unsigned long long now = in->now;
    if (s->state == RW_LEAVING && s->leave_ms) {
        if (!in->active)
            return RW_ACT_NONE;
        if (in->cancel) {
            s->leave_ms = 0;
            s->why = RW_WHY_CANCEL;
            return RW_ACT_CANCEL;
        }
        if (now >= s->leave_ms) {
            s->leave_ms = 0;
            return RW_ACT_CREATE;
        }
        return RW_ACT_HOLD;
    }
    if (s->state != RW_WAIT && s->state != RW_JOINING)
        return RW_ACT_NONE;
    if (s->answer_ms && now >= s->answer_ms)
        s->secs = s->secs_at + (int)((now - s->answer_ms) / 1000ull);
    if (!in->active)
        return RW_ACT_NONE;             /* the page fades: nothing is touched */

    if (s->state == RW_JOINING) {
        if (in->sub == QP_SUB_JOINING && in->join_failed) {
            /* "full" (the racers' seats are kept) or "gone" (it started
             * again): not the end, the server tells us what comes next */
            s->join_fails++;
            if (in->cancel)
                return rw_end(s, RW_WHY_CANCEL, RW_ACT_CANCEL);
            if (s->join_fails >= RW_JOIN_FAILS)
                return rw_end(s, RW_WHY_FULL, RW_ACT_FALLBACK);
            s->state = RW_WAIT;
            s->holding = 1;
            s->next_poll_ms = now + RW_POLL_MS;
            return RW_ACT_REFUSED;
        }
        if (rw_joining(in))
            return RW_ACT_HOLD;
        /* the page is back to an answer without the game: wait again */
        s->state = RW_WAIT;
        s->holding = 0;
    }

    if (!s->holding) {
        if (rw_joining(in)) {
            rw_take(s, in);
            s->state = RW_JOINING;
            return RW_ACT_HOLD;
        }
        if ((in->sub == QP_SUB_WAIT && in->ready) || in->sub == QP_SUB_NONE) {
            /* the poll's answer kept nothing */
            rw_take(s, in);
            s->holding = 1;
            if (in->cancel)
                return rw_end(s, RW_WHY_CANCEL, RW_ACT_CANCEL);
            if (s->wait_state == RW_WS_GONE)
                return rw_end(s, RW_WHY_GONE, RW_ACT_FALLBACK);
            if (s->wait_state == RW_WS_FULL)
                return rw_end(s, RW_WHY_FULL, RW_ACT_FALLBACK);
            if (s->stale >= RW_STALE_MAX)
                return rw_end(s, RW_WHY_SERVER, RW_ACT_FALLBACK);
            s->open_misses = s->wait_state == RW_WS_OPEN ? s->open_misses + 1 : 0;
            if (s->open_misses >= RW_OPEN_MISSES)
                return rw_end(s, RW_WHY_MISSED, RW_ACT_FALLBACK);
            if (now >= s->deadline_ms)
                return rw_end(s, RW_WHY_TIMEOUT, RW_ACT_FALLBACK);
            return RW_ACT_HOLD;
        }
        /* the search is in flight */
        if (now - s->poll_ms >= RW_ANSWER_MS)
            return rw_end(s, RW_WHY_SERVER, RW_ACT_FALLBACK);
        return RW_ACT_HOLD;
    }

    /* holding: nothing in flight */
    if (in->cancel)
        return rw_end(s, RW_WHY_CANCEL, RW_ACT_CANCEL);
    if (now >= s->deadline_ms)
        return rw_end(s, RW_WHY_TIMEOUT, RW_ACT_FALLBACK);
    if (now >= s->next_poll_ms) {
        s->holding = 0;
        s->poll_ms = now;
        s->next_poll_ms = now + RW_POLL_MS;
        s->polls++;
        return RW_ACT_POLL;
    }
    return RW_ACT_HOLD;
}

int bo3_racewait_hold(rw_state *s, unsigned long long now)
{
    if (s->state != RW_LEAVING ||
        (s->why != RW_WHY_GONE && s->why != RW_WHY_TIMEOUT && s->why != RW_WHY_FULL))
        return 0;
    s->leave_ms = now + RW_LEAVE_MS;
    return 1;
}

int bo3_racewait_finish(rw_state *s, int went_on)
{
    int joined = s->state == RW_JOINING && went_on;
    if (s->state == RW_OFF)
        return RW_ACT_NONE;
    if (joined)
        s->why = RW_WHY_JOINED;
    s->state = RW_OFF;
    return joined ? RW_ACT_JOINED : RW_ACT_NONE;
}

void bo3_racewait_line(const rw_state *s, int lang, char *out, size_t n)
{
    if (!out || !n)
        return;
    out[0] = 0;
    if (s->state == RW_WAIT || s->state == RW_JOINING) {
        int secs = s->secs > 0 ? s->secs : 0;
        if (s->state == RW_JOINING || s->wait_state == RW_WS_OPEN ||
            ((s->wait_state == RW_WS_LOBBY || s->wait_state == RW_WS_FULL) && s->queue <= 0))
            snprintf(out, n, "%s", bo3_statustext(lang, BO3_TXT_RACEWAIT_LOBBY));
        else if (s->wait_state == RW_WS_LOBBY || s->wait_state == RW_WS_FULL)
            snprintf(out, n, bo3_statustext(lang, BO3_TXT_RACEWAIT_QUEUE), s->queue);
        else
            snprintf(out, n, bo3_statustext(lang, BO3_TXT_RACEWAIT), s->players, secs / 60, secs % 60);
    } else if (s->state == RW_LEAVING) {
        if (s->why == RW_WHY_GONE || s->why == RW_WHY_TIMEOUT)
            snprintf(out, n, "%s", bo3_statustext(lang, BO3_TXT_RACEWAIT_GONE));
        else if (s->why == RW_WHY_FULL)
            snprintf(out, n, "%s", bo3_statustext(lang, BO3_TXT_RACEWAIT_FULL));
    }
}

const char *bo3_racewait_why_name(int why)
{
    switch (why) {
    case RW_WHY_GONE: return "gone";
    case RW_WHY_FULL: return "full";
    case RW_WHY_TIMEOUT: return "timeout";
    case RW_WHY_CANCEL: return "cancel";
    case RW_WHY_SERVER: return "server";
    case RW_WHY_JOINED: return "joined";
    case RW_WHY_MISSED: return "the title's filter keeps the game out";
    default: return "none";
    }
}

const char *bo3_racewait_ws_name(int ws)
{
    switch (ws) {
    case RW_WS_RACING: return "racing";
    case RW_WS_LOBBY: return "lobby";
    case RW_WS_OPEN: return "open";
    case RW_WS_FULL: return "full";
    case RW_WS_GONE: return "gone";
    default: return "none";
    }
}

/* ---- stay together (racewait.h) ---- */

void bo3_racewait_stay_begin(rw_stay *s, unsigned seq, unsigned long long now)
{
    memset(s, 0, sizeof *s);
    s->last_seq = seq;
    s->since_ms = now;
}

int bo3_racewait_stay_answer(rw_stay *s, unsigned seq, int ws, int host_back, int why, unsigned long long now)
{
    (void)now;
    if (s->end)
        return s->end;
    if (seq == s->last_seq)
        return s->on ? RW_SW_KEEP : RW_SW_OLD;  /* no wait keys in this one (an old server, or none yet) */
    s->last_seq = seq;
    s->on = 1;
    s->ws = ws;
    s->host_back = host_back;
    if (ws == RW_WS_GONE) {
        s->why = why;
        return s->end = RW_SW_GONE;
    }
    if (ws == RW_WS_FULL) {
        s->why = RW_SWHY_FULL;
        return s->end = RW_SW_FULL;
    }
    /* the server lists the game ("open") but the title's own filter keeps
     * nothing of it: it will not take that game */
    s->open_misses = ws == RW_WS_OPEN ? s->open_misses + 1 : 0;
    if (s->open_misses >= RW_OPEN_MISSES)
        return s->end = RW_SW_MISSED;
    return RW_SW_KEEP;
}

unsigned long long bo3_racewait_stay_deadline(const rw_stay *s, unsigned long long old_deadline)
{
    return s->on ? s->since_ms + RW_STAY_MAX_MS : old_deadline;
}

void bo3_racewait_stay_line(const rw_stay *s, int lang, unsigned long long now, char *out, size_t n)
{
    unsigned long long el;
    if (!out || !n)
        return;
    out[0] = 0;
    if (s->end) {
        int id = s->end == RW_SW_FULL ? BO3_TXT_STAY_FULL
               : s->why == RW_SWHY_QUIT || s->why == RW_SWHY_LEFT ? BO3_TXT_STAY_HOST_LEFT
               : s->why == RW_SWHY_ABSENT || s->why == RW_SWHY_TIME ? BO3_TXT_STAY_HOST_GONE
               : BO3_TXT_STAY_OVER;
        snprintf(out, n, "%s", bo3_statustext(lang, id));
        return;
    }
    if (!s->on)
        return;
    if (s->ws == RW_WS_OPEN || s->ws == RW_WS_LOBBY) {
        snprintf(out, n, "%s", bo3_statustext(lang, BO3_TXT_STAY_JOINING));
        return;
    }
    if (s->host_back) {
        snprintf(out, n, "%s", bo3_statustext(lang, BO3_TXT_STAY_HOST_BACK));
        return;
    }
    el = now > s->since_ms ? (now - s->since_ms) / 1000ull : 0;
    if (el > 5999)
        el = 5999;
    snprintf(out, n, bo3_statustext(lang, BO3_TXT_STAY_WAIT), (int)(el / 60), (int)(el % 60));
}

int bo3_racewait_stay_why(const char *v)
{
    if (!v || !v[0])
        return RW_SWHY_NONE;
    if (!strcmp(v, "quit"))
        return RW_SWHY_QUIT;
    if (!strcmp(v, "left"))
        return RW_SWHY_LEFT;
    if (!strcmp(v, "absent"))
        return RW_SWHY_ABSENT;
    if (!strcmp(v, "time"))
        return RW_SWHY_TIME;
    if (!strcmp(v, "full"))
        return RW_SWHY_FULL;
    return RW_SWHY_NONE;
}
