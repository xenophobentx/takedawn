/* QUICK PLAY's pure decisions (quickplay.c, Takedawn Online step 3). */
#include <stdio.h>
#include <string.h>

#include "quickplay.h"
#include "racewait.h"

static int s_fail;

#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); s_fail++; } } while (0)

static qp_in base(void)
{
    qp_in q;
    memset(&q, 0, sizeof q);
    q.armed = 1;
    q.on_search = 1;
    q.active = 1;
    q.sub = QP_SUB_WAIT;
    q.now = 100000;
    return q;
}

static void idle_cases(void)
{
    qp_in q = base();
    /* searching: nothing yet */
    CHECK(bo3_quickplay_decide(&q) == QP_NONE);
    /* results in, one kept: the title's own callback joins, nothing to do */
    q.ready = 1; q.count = 1;
    CHECK(bo3_quickplay_decide(&q) == QP_NONE);
    q.sub = QP_SUB_LIST;
    CHECK(bo3_quickplay_decide(&q) == QP_NONE);
    /* not armed (BROWSE GAMES, the player's own Quick Match): never */
    q = base(); q.armed = 0; q.ready = 1; q.count = 0;
    CHECK(bo3_quickplay_decide(&q) == QP_NONE);
    /* another page, or the page fading */
    q = base(); q.ready = 1; q.on_search = 0;
    CHECK(bo3_quickplay_decide(&q) == QP_NONE);
    q = base(); q.ready = 1; q.active = 0;
    CHECK(bo3_quickplay_decide(&q) == QP_NONE);
}

static void create_cases(void)
{
    qp_in q = base();
    /* the search came back with nothing: state 0 with the results ready */
    q.ready = 1; q.count = 0;
    CHECK(bo3_quickplay_decide(&q) == QP_CREATE);
    /* NO GAMES FOUND already up */
    q = base(); q.sub = QP_SUB_NONE;
    CHECK(bo3_quickplay_decide(&q) == QP_CREATE);
    /* results not ready yet: wait */
    q = base(); q.ready = 0; q.count = 0;
    CHECK(bo3_quickplay_decide(&q) == QP_NONE);
    /* started: it does not start again, and waits for the room */
    q = base(); q.creating = 1; q.sub = QP_SUB_ROOMS; q.create_at = 100000 - 1000;
    CHECK(bo3_quickplay_decide(&q) == QP_NONE);
    /* ... but not for ever */
    q.create_at = 100000 - QP_ROOM_WAIT_MS - 1;
    CHECK(bo3_quickplay_decide(&q) == QP_GIVEUP);
    q.sub = QP_SUB_ROOM;
    CHECK(bo3_quickplay_decide(&q) == QP_GIVEUP);
    /* the page has gone on to the create state (the page is not the search page) */
    q.on_search = 0;
    CHECK(bo3_quickplay_decide(&q) == QP_NONE);
}

/* JOIN WITH CODE: the same search, but nothing found is "no game with that
 * code", never the create branch */
static void join_cases(void)
{
    qp_in q = base();
    q.join = 1;
    CHECK(bo3_quickplay_decide(&q) == QP_NONE);           /* searching */
    q.ready = 1; q.count = 1;
    CHECK(bo3_quickplay_decide(&q) == QP_NONE);           /* found: the title joins */
    q = base(); q.join = 1; q.ready = 1; q.count = 0;
    CHECK(bo3_quickplay_decide(&q) == QP_NOTFOUND);
    q = base(); q.join = 1; q.sub = QP_SUB_NONE;
    CHECK(bo3_quickplay_decide(&q) == QP_NOTFOUND);
    /* B while searching: held until the search is in, then Back */
    q = base(); q.join = 1; q.cancel = 1;
    CHECK(bo3_quickplay_decide(&q) == QP_NONE);
    q.ready = 1; q.count = 0;
    CHECK(bo3_quickplay_decide(&q) == QP_CANCEL);
}

/* BO-364: the game picked refuses the join (it had just started, or its host
 * kicked us): never the title's error box and the one game's list for ever */
static void refused_cases(void)
{
    qp_in q = base();
    /* joining, no answer yet: the title's state 5 waits */
    q.sub = QP_SUB_JOINING; q.ready = 1; q.count = 1;
    CHECK(bo3_quickplay_decide(&q) == QP_NONE);
    /* QUICK PLAY: search again; at the QP_REFUSE_MAX-th refusal a game of its own */
    q.join_failed = 1;
    CHECK(bo3_quickplay_decide(&q) == QP_RESEARCH);
    q.refusals = QP_REFUSE_MAX - 2;
    CHECK(bo3_quickplay_decide(&q) == QP_RESEARCH);
    q.refusals = QP_REFUSE_MAX - 1;                 /* the third refusal: a game of its own */
    CHECK(bo3_quickplay_decide(&q) == QP_CREATE);
    /* JOIN WITH CODE: back to the code entry, whatever the count */
    q.join = 1;
    CHECK(bo3_quickplay_decide(&q) == QP_REFUSED);
    q.refusals = 0;
    CHECK(bo3_quickplay_decide(&q) == QP_REFUSED);
    /* B pressed meanwhile: out */
    q.cancel = 1;
    CHECK(bo3_quickplay_decide(&q) == QP_CANCEL);
    q.join = 0;
    CHECK(bo3_quickplay_decide(&q) == QP_CANCEL);
    /* a failed session in any other state is the title's (the search's own error) */
    q = base(); q.join_failed = 1; q.sub = QP_SUB_LIST; q.count = 1;
    CHECK(bo3_quickplay_decide(&q) == QP_NONE);
    /* the page fading, or the create branch: nothing */
    q = base(); q.sub = QP_SUB_JOINING; q.join_failed = 1; q.active = 0;
    CHECK(bo3_quickplay_decide(&q) == QP_NONE);
    q.active = 1; q.creating = 1;
    CHECK(bo3_quickplay_decide(&q) == QP_NONE);
    /* what JOIN WITH CODE comes back with */
    CHECK(bo3_quickplay_join_result(0, 0) == QP_JOIN_MISSED);
    CHECK(bo3_quickplay_join_result(1, 0) == QP_JOIN_REFUSED);
    CHECK(bo3_quickplay_join_result(0, 1) == QP_JOIN_REMOVED);
    CHECK(bo3_quickplay_join_result(1, 1) == QP_JOIN_REMOVED);
}

/* BO-365: nothing open but a race under way with room: wait instead of
 * creating (QUICK PLAY) or going back (JOIN WITH CODE); while the wait runs
 * the page is racewait.c's */
static void racewait_cases(void)
{
    qp_in q = base();
    q.ready = 1; q.count = 0; q.racing = 1;
    CHECK(bo3_quickplay_decide(&q) == QP_WAIT);
    q.join = 1;
    CHECK(bo3_quickplay_decide(&q) == QP_WAIT);
    q = base(); q.sub = QP_SUB_NONE; q.racing = 1;
    CHECK(bo3_quickplay_decide(&q) == QP_WAIT);
    /* B first */
    q = base(); q.ready = 1; q.racing = 1; q.cancel = 1;
    CHECK(bo3_quickplay_decide(&q) == QP_CANCEL);
    /* an open game is always taken: the title joins it */
    q = base(); q.ready = 1; q.count = 1; q.racing = 1;
    CHECK(bo3_quickplay_decide(&q) == QP_NONE);
    /* still searching */
    q = base(); q.racing = 1;
    CHECK(bo3_quickplay_decide(&q) == QP_NONE);
    /* nothing racing: as before */
    q = base(); q.ready = 1;
    CHECK(bo3_quickplay_decide(&q) == QP_CREATE);
    q.join = 1;
    CHECK(bo3_quickplay_decide(&q) == QP_NOTFOUND);
    /* the wait runs: the old branches are off, even a refused join */
    q = base(); q.ready = 1; q.waiting = 1;
    CHECK(bo3_quickplay_decide(&q) == QP_NONE);
    q = base(); q.sub = QP_SUB_JOINING; q.join_failed = 1; q.waiting = 1;
    CHECK(bo3_quickplay_decide(&q) == QP_NONE);
    /* a refused join with nothing racing keeps BO-364's path */
    q = base(); q.sub = QP_SUB_JOINING; q.join_failed = 1; q.racing = 1;
    CHECK(bo3_quickplay_decide(&q) == QP_RESEARCH);
}

static void cancel_cases(void)
{
    qp_in q = base();
    /* B during the search: held; the search kept nothing: back, not create */
    q.cancel = 1; q.ready = 1; q.count = 0;
    CHECK(bo3_quickplay_decide(&q) == QP_CANCEL);
    /* B during the search that found a game: the title's join goes on */
    q.count = 1;
    CHECK(bo3_quickplay_decide(&q) == QP_NONE);
    /* B in the create branch: back once the room list is in and nothing is in flight */
    q = base(); q.creating = 1; q.cancel = 1; q.sub = QP_SUB_ROOMS; q.create_at = q.now;
    CHECK(bo3_quickplay_decide(&q) == QP_NONE);
    q.rooms_done = 1;
    CHECK(bo3_quickplay_decide(&q) == QP_CANCEL);
    /* not while the room is being asked for */
    q.sub = QP_SUB_ROOM;
    CHECK(bo3_quickplay_decide(&q) == QP_NONE);
    /* the room's answer, an exit into the create state, becomes Back after B */
    CHECK(bo3_quickplay_recode(1, 1, 3, QP_EXIT_CREATE));
    CHECK(bo3_quickplay_recode(1, 1, 4, QP_EXIT_CREATE));
    CHECK(!bo3_quickplay_recode(0, 1, 3, QP_EXIT_CREATE));       /* no B */
    CHECK(!bo3_quickplay_recode(1, 0, 3, QP_EXIT_CREATE));       /* not creating */
    CHECK(!bo3_quickplay_recode(1, 1, 5, QP_EXIT_CREATE));       /* page still active */
    CHECK(!bo3_quickplay_recode(1, 1, 3, QP_EXIT_BACK));         /* already Back */
    CHECK(!bo3_quickplay_recode(1, 1, 3, 1u));                   /* a join's success exit */

    CHECK(bo3_quickplay_stay_release(1, 1));                     /* the room answered: gcre */
    CHECK(!bo3_quickplay_stay_release(0, 1));                    /* no longer making it */
    CHECK(!bo3_quickplay_stay_release(1, 0));                    /* the flow went elsewhere */
}

static void page_values(void)
{
    uint8_t dst[8], src[8];

    /* the title's name copy: strncpy, zero padded */
    memset(dst, 0xAA, sizeof dst);
    memcpy(src, "Guest\0zz", 8);
    bo3_quickplay_copy_str(dst, 8, src);
    CHECK(!memcmp(dst, "Guest\0\0\0", 8));
    memset(dst, 0xAA, sizeof dst);
    memcpy(src, "ABCDEFGH", 8);
    bo3_quickplay_copy_str(dst, 4, src);                 /* no NUL within n: none added */
    CHECK(!memcmp(dst, "ABCD", 4) && dst[4] == 0xAA);

    /* the mode page: 0 (Single Race) stays; a big limit forces 4; a limit other than 2 has no 2 or 3 */
    CHECK(bo3_quickplay_mode(0, 6) == 0);
    CHECK(bo3_quickplay_mode(0, 8) == 4);
    CHECK(bo3_quickplay_mode(2, 6) == 0);
    CHECK(bo3_quickplay_mode(3, 6) == 0);
    CHECK(bo3_quickplay_mode(2, 2) == 2);
    CHECK(bo3_quickplay_mode(1, 6) == 1);

    /* block + 0x18 after the visible flow's Continue: 0x00010000 with the language 1 in the top byte */
    CHECK(bo3_quickplay_flags18(0x00010000u, 1) == 0x01010000u);
    CHECK(bo3_quickplay_flags18(0x7F010000u, 2) == 0x02010000u);

    /* the page manager's context: the manager's word below code 3, 0 from it on */
    CHECK(bo3_quickplay_exit_ctx(1, 2) == 2);
    CHECK(bo3_quickplay_exit_ctx(2, 2) == 2);
    CHECK(bo3_quickplay_exit_ctx(3, 2) == 0);
    CHECK(bo3_quickplay_exit_ctx(0x2D, 2) == 0);
}

static void countdown(void)
{
    qp_cd c;
    qp_cd_in in;
    int secs;
    memset(&c, 0, sizeof c);
    memset(&in, 0, sizeof in);
    in.host_game = 1; in.idle = 1; in.guests_ready = 1; in.players = 1; in.now = 1000;
    /* alone: nothing runs */
    CHECK(bo3_quickplay_countdown(&c, &in, &secs) == QP_CD_NONE && secs == 0 && !c.start_ms);
    /* the second player: 30 s */
    in.players = 2;
    CHECK(bo3_quickplay_countdown(&c, &in, &secs) == QP_CD_TICK && secs == 30);
    in.now = 1000 + 5000;
    CHECK(bo3_quickplay_countdown(&c, &in, &secs) == QP_CD_TICK && secs == 25);
    in.now = 1000 + 29001;
    CHECK(bo3_quickplay_countdown(&c, &in, &secs) == QP_CD_TICK && secs == 1);
    /* a third player does not restart it */
    in.players = 3;
    in.now = 1000 + 29500;
    CHECK(bo3_quickplay_countdown(&c, &in, &secs) == QP_CD_TICK && secs == 1);
    /* time is up and every guest has a car: start, once */
    in.now = 1000 + 30000;
    CHECK(bo3_quickplay_countdown(&c, &in, &secs) == QP_CD_START && secs == 0);
    in.now = 1000 + 30100;
    CHECK(bo3_quickplay_countdown(&c, &in, &secs) == QP_CD_NONE);          /* cooling down */
    in.now = 1000 + 30000 + QP_START_COOLDOWN_MS;
    CHECK(bo3_quickplay_countdown(&c, &in, &secs) == QP_CD_START);         /* the start did not take: again */
    /* a guest still on car select holds the start, the line is empty */
    memset(&c, 0, sizeof c);
    in.players = 2; in.guests_ready = 0; in.now = 50000;
    CHECK(bo3_quickplay_countdown(&c, &in, &secs) == QP_CD_TICK && secs == 30);
    in.now = 50000 + 31000;
    CHECK(bo3_quickplay_countdown(&c, &in, &secs) == QP_CD_NONE && secs == 0);
    in.guests_ready = 1;
    CHECK(bo3_quickplay_countdown(&c, &in, &secs) == QP_CD_START);
    /* below two players it stops and starts over at 30 */
    memset(&c, 0, sizeof c);
    in.now = 100000; in.players = 2;
    CHECK(bo3_quickplay_countdown(&c, &in, &secs) == QP_CD_TICK && secs == 30);
    in.now = 110000; in.players = 1;
    CHECK(bo3_quickplay_countdown(&c, &in, &secs) == QP_CD_NONE && !c.start_ms);
    in.now = 112000; in.players = 2;
    CHECK(bo3_quickplay_countdown(&c, &in, &secs) == QP_CD_TICK && secs == 30);
    /* only a QUICK PLAY game, only in the plain lobby (a START press, a leave, a race: state not 1) */
    in.host_game = 0;
    CHECK(bo3_quickplay_countdown(&c, &in, &secs) == QP_CD_NONE && !c.start_ms);
    in.host_game = 1; in.idle = 0;
    CHECK(bo3_quickplay_countdown(&c, &in, &secs) == QP_CD_NONE && !c.start_ms);
}

/* step 4: a guest's Quick Match for its host's game after a race */
static qp_target_in tbase(void)
{
    qp_target_in t;
    memset(&t, 0, sizeof t);
    t.active = 1;
    t.sub = QP_SUB_WAIT;
    t.now = 10000;
    t.deadline = 100000;
    return t;
}

static void target_cases(void)
{
    qp_target_in t = tbase();
    /* searching: nothing */
    CHECK(bo3_quickplay_target_decide(&t) == QP_T_NONE);
    /* the answer has the game: the title's callback joined it (state 7 ...) */
    t.ready = 1; t.count = 1; t.sub = 7;
    CHECK(bo3_quickplay_target_decide(&t) == QP_T_NONE);
    /* an answer without it: hold, the page keeps searching */
    t = tbase(); t.ready = 1; t.count = 0;
    CHECK(bo3_quickplay_target_decide(&t) == QP_T_HOLD);
    /* holding: wait for the retry time, then search again */
    t = tbase(); t.holding = 1; t.retry_at = 12000;
    CHECK(bo3_quickplay_target_decide(&t) == QP_T_NONE);
    t.now = 12000;
    CHECK(bo3_quickplay_target_decide(&t) == QP_T_RESEARCH);
    /* B: at once while holding, at the answer while searching */
    t = tbase(); t.holding = 1; t.retry_at = 12000; t.cancel = 1;
    CHECK(bo3_quickplay_target_decide(&t) == QP_T_CANCEL);
    t = tbase(); t.cancel = 1;
    CHECK(bo3_quickplay_target_decide(&t) == QP_T_NONE);
    t.ready = 1;
    CHECK(bo3_quickplay_target_decide(&t) == QP_T_CANCEL);
    /* the wait is over: leave, holding or at an answer */
    t = tbase(); t.holding = 1; t.retry_at = 200000; t.now = 100000;
    CHECK(bo3_quickplay_target_decide(&t) == QP_T_TIMEOUT);
    t = tbase(); t.ready = 1; t.now = 100000;
    CHECK(bo3_quickplay_target_decide(&t) == QP_T_TIMEOUT);
    /* NO GAMES FOUND must not stay up on this path */
    t = tbase(); t.sub = QP_SUB_NONE;
    CHECK(bo3_quickplay_target_decide(&t) == QP_T_TIMEOUT);
    /* BO-364: the host's game refused the join: waited out like an answer without it */
    t = tbase(); t.sub = QP_SUB_JOINING; t.join_failed = 1;
    CHECK(bo3_quickplay_target_decide(&t) == QP_T_HOLD);
    t.cancel = 1;
    CHECK(bo3_quickplay_target_decide(&t) == QP_T_CANCEL);
    t = tbase(); t.sub = QP_SUB_JOINING; t.join_failed = 1; t.now = 100000;
    CHECK(bo3_quickplay_target_decide(&t) == QP_T_TIMEOUT);
    t = tbase(); t.sub = QP_SUB_JOINING;                    /* still joining: the title's */
    CHECK(bo3_quickplay_target_decide(&t) == QP_T_NONE);
    /* the page fading: nothing, whatever else holds */
    t = tbase(); t.active = 0; t.holding = 1; t.now = 999999; t.cancel = 1;
    CHECK(bo3_quickplay_target_decide(&t) == QP_T_NONE);

    CHECK(bo3_quickplay_name_match("HostS", "HostS"));
    CHECK(!bo3_quickplay_name_match("HostA", "HostS"));
    CHECK(!bo3_quickplay_name_match("HostS2", "HostS"));
    CHECK(!bo3_quickplay_name_match("", ""));
    CHECK(!bo3_quickplay_name_match(NULL, "HostS"));
}

static void stay_request(void)
{
    /* the lobby's request as the trace read it (step 4 notes): name, password,
     * limit 6, track key, mode 0, flags, laps 3 */
    const uint32_t saved[QP_STAY_REQ_WORDS] = { 0x01099EC4u, 0x0057C064u, 6u, 0x9F445000u, 0u, 2u, 0x01010024u,
                                                0x05000000u, 0xC4BD587Du, 7u, 8u };
    uint32_t blk[QP_STAY_REQ_WORDS] = { 0x0055E038u, 0x0055E058u, 6u, 0u, 0u, 0u, 0x00010000u, 0x03000000u, 0u, 0u, 0u };
    bo3_quickplay_stay_request(blk, saved, 2);
    CHECK(blk[0] == 0x0055E038u && blk[1] == 0x0055E058u);     /* the name page's own buffers */
    CHECK(blk[2] == 6u && blk[3] == 0x9F445000u && blk[5] == 2u && blk[7] == 0x05000000u && blk[8] == 0xC4BD587Du);
    CHECK(blk[9] == 7u && blk[10] == 8u);
    CHECK(blk[6] == 0x02010024u);                               /* this copy's language */
}

static void room_ask(void)
{
    unsigned long long asked = 0, t;
    int n = 0;
    /* an older search's answer left in the fields: still asked */
    CHECK(bo3_quickplay_room_ask(&asked, 500, 1, 2) == 1);
    asked = 0;
    /* the first tick asks; a sync answer is then taken in the same tick */
    CHECK(bo3_quickplay_room_ask(&asked, 1000, 0, 0) == 1);
    CHECK(bo3_quickplay_room_ask(&asked, 1000, 1, 3) == 0);
    /* no answer yet: one request, then waiting flip after flip (16 ms) */
    asked = 0;
    for (t = 2000; t < 2000 + QP_ROOM_ANSWER_MS; t += 16)
        n += bo3_quickplay_room_ask(&asked, t, 0, 0);
    CHECK(n == 1);
    /* still none after the answer time: asked once more */
    CHECK(bo3_quickplay_room_ask(&asked, 2000 + QP_ROOM_ANSWER_MS, 0, 0) == 1);
    /* an answer with no room: again only after the retry time */
    asked = 0;
    n = 0;
    CHECK(bo3_quickplay_room_ask(&asked, 5000, 0, 0) == 1);
    for (t = 5016; t < 5000 + QP_ROOM_RETRY_MS; t += 16)
        n += bo3_quickplay_room_ask(&asked, t, 1, 0);
    CHECK(n == 0);
    CHECK(bo3_quickplay_room_ask(&asked, 5000 + QP_ROOM_RETRY_MS, 1, 0) == 1);
    /* the whole create wait at one flip a frame: a handful of requests, not
     * one per flip */
    asked = 0;
    n = 0;
    for (t = 1; t < QP_ROOM_WAIT_MS; t += 16)
        n += bo3_quickplay_room_ask(&asked, t, 0, 0);
    CHECK(n <= (int)(QP_ROOM_WAIT_MS / QP_ROOM_ANSWER_MS) + 1);
    /* answered: never asked again */
    CHECK(bo3_quickplay_room_ask(&asked, 999999, 1, 1) == 0);
}

/* BO-372: BROWSE GAMES' rows */
static void browse_cases(void)
{
    char t[64];
    CHECK(bo3_quickplay_browse_pick(QP_BR_OPEN) == QP_PICK_JOIN);
    CHECK(bo3_quickplay_browse_pick(QP_BR_NONE) == QP_PICK_JOIN);       /* an old server: the title's join */
    CHECK(bo3_quickplay_browse_pick(QP_BR_RACING) == QP_PICK_WAIT);
    CHECK(bo3_quickplay_browse_pick(QP_BR_FULL) == QP_PICK_FULL);
    bo3_quickplay_browse_label(t, sizeof t, "Racer1234", QP_BR_OPEN, 0);
    CHECK(!strcmp(t, "Racer1234"));
    bo3_quickplay_browse_label(t, sizeof t, "Racer1234", QP_BR_RACING, 80);
    CHECK(!strcmp(t, "Racer1234 - RACING 1:20"));
    bo3_quickplay_browse_label(t, sizeof t, "Racer1234", QP_BR_FULL, 0);
    CHECK(!strcmp(t, "Racer1234 - FULL"));
    bo3_quickplay_browse_label(t, sizeof t, "Racer1234", QP_BR_RACING, -5);
    CHECK(!strcmp(t, "Racer1234 - RACING 0:00"));
    bo3_quickplay_browse_label(t, sizeof t, "Racer1234", QP_BR_RACING, 200 * 60);
    CHECK(!strcmp(t, "Racer1234 - RACING 99:59"));
    /* a long name is cut, the state never; the row holds 31 characters */
    bo3_quickplay_browse_label(t, sizeof t, "ABCDEFGHIJKLMNOPQRSTUVWXYZABCDE", QP_BR_RACING, 61);
    CHECK(strlen(t) == QP_BR_NAME - 1 && !strcmp(t + strlen(t) - 14, " - RACING 1:01"));
    bo3_quickplay_browse_label(t, sizeof t, "ABCDEFGHIJKLMNOPQRSTUVW Z", QP_BR_FULL, 0);
    CHECK(!strcmp(t, "ABCDEFGHIJKLMNOPQRSTUVW - FULL"));                /* cut at a blank: one blank */
    bo3_quickplay_browse_label(t, sizeof t, "ABCDEFGHIJKLMNOPQRSTUVWXYZABCDEF", QP_BR_OPEN, 0);
    CHECK(strlen(t) == QP_BR_NAME - 1);
    bo3_quickplay_browse_label(t, 8, "Racer1234", QP_BR_OPEN, 0);
    CHECK(!strcmp(t, "Racer12"));
    /* the row this copy waits for (BO-372): it says so, its clock counts on */
    bo3_quickplay_browse_label(t, sizeof t, "RaceH", QP_BR_WAITING, 287);
    CHECK(!strcmp(t, "RaceH - RACING 4:47 - WAITING"));
    bo3_quickplay_browse_label(t, sizeof t, "Racer1234", QP_BR_WAITING, 61);
    CHECK(!strcmp(t, "Racer1234 - WAITING 1:01"));                      /* too long for the full form */
    bo3_quickplay_browse_label(t, sizeof t, "ABCDEFGHIJKLMNOPQRSTUVWXYZ", QP_BR_WAITING, 61);
    CHECK(strlen(t) == QP_BR_NAME - 1 && !strcmp(t + strlen(t) - 15, " - WAITING 1:01"));
    bo3_quickplay_browse_label(t, sizeof t, "RaceH", QP_BR_QUEUED, 0);
    CHECK(!strcmp(t, "RaceH - WAITING"));
    bo3_quickplay_browse_label(t, sizeof t, "RaceH", QP_BR_JOINING, 0);
    CHECK(!strcmp(t, "RaceH - JOINING"));
    CHECK(bo3_quickplay_browse_wait_label(RW_WAIT, RW_WS_RACING, 0) == QP_BR_WAITING);
    CHECK(bo3_quickplay_browse_wait_label(RW_WAIT, RW_WS_NONE, 0) == QP_BR_WAITING);
    CHECK(bo3_quickplay_browse_wait_label(RW_WAIT, RW_WS_LOBBY, 2) == QP_BR_QUEUED);
    CHECK(bo3_quickplay_browse_wait_label(RW_WAIT, RW_WS_LOBBY, 0) == QP_BR_JOINING);
    CHECK(bo3_quickplay_browse_wait_label(RW_WAIT, RW_WS_OPEN, 3) == QP_BR_JOINING);
    CHECK(bo3_quickplay_browse_wait_label(RW_JOINING, RW_WS_RACING, 0) == QP_BR_JOINING);
    /* open before racing before full; the server's order inside one state */
    CHECK(bo3_quickplay_browse_key(QP_BR_OPEN, 8) < bo3_quickplay_browse_key(QP_BR_RACING, 0));
    CHECK(bo3_quickplay_browse_key(QP_BR_RACING, 8) < bo3_quickplay_browse_key(QP_BR_FULL, 0));
    CHECK(bo3_quickplay_browse_key(QP_BR_OPEN, 1) < bo3_quickplay_browse_key(QP_BR_OPEN, 2));
    CHECK(bo3_quickplay_browse_key(QP_BR_NONE, 3) == bo3_quickplay_browse_key(QP_BR_OPEN, 3));
}

/* BO-384 (GitHub #122): when the host's start reads the game record, as every
 * guest does at its start */
static void start_record(void)
{
    const uint32_t open = 0x00040140u, locked = 0x00041140u, racing = 0x000C0140u;
    /* the start lock's answer is back (state 0x11, bit 12): read, then gsta */
    CHECK(bo3_quickplay_start_reads_record(1, 0x11u, locked));
    /* the lock still on its way: not yet (the record may not have the last update) */
    CHECK(!bo3_quickplay_start_reads_record(1, 0x11u, open));
    /* a record already under way: the title's own start ran (a guest's read) */
    CHECK(!bo3_quickplay_start_reads_record(1, 0x11u, racing));
    CHECK(!bo3_quickplay_start_reads_record(1, 0x11u, locked | racing));
    /* a guest reads at 0xA165C by itself; nothing in the plain lobby, the
     * update steps, the menu, the leave question or after the start */
    CHECK(!bo3_quickplay_start_reads_record(0, 0x11u, locked));
    CHECK(!bo3_quickplay_start_reads_record(1, 0x01u, locked));
    CHECK(!bo3_quickplay_start_reads_record(1, 0x0Bu, locked));
    CHECK(!bo3_quickplay_start_reads_record(1, 0x0Cu, locked));
    CHECK(!bo3_quickplay_start_reads_record(1, 0x0Du, locked));
    CHECK(!bo3_quickplay_start_reads_record(1, 0x02u, locked));
    CHECK(!bo3_quickplay_start_reads_record(1, 0x09u, locked));
}

int main(void)
{
    countdown();
    idle_cases();
    create_cases();
    join_cases();
    refused_cases();
    CHECK(bo3_quickplay_create_step(1, 1u) == 0u);      /* B on the mode page: out, not to a name page */
    CHECK(bo3_quickplay_create_step(1, 2u) == 2u);
    CHECK(bo3_quickplay_create_step(0, 1u) == 1u);      /* the room's own CREATE GAME keeps its name page */
    cancel_cases();
    racewait_cases();
    page_values();
    target_cases();
    stay_request();
    room_ask();
    browse_cases();
    start_record();
    if (s_fail)
        return 1;
    printf("quickplay: all checks passed\n");
    return 0;
}
