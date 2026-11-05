/* linkstate test: see CMakeLists.txt. */
#include <stdio.h>
#include <string.h>

#include "linkstate.h"

static int fails, checks;

#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

#define P1 0x01A00000u
#define P2 0x01A01000u
#define ID1 0x0A4052E2u
#define ID2 0x0A4052DEu

/* the first look logs the state it finds, from "nothing seen" */
static void test_first(void)
{
    bo3_linkstate e;
    uint32_t from = 7;
    memset(&e, 0, sizeof e);
    CHECK(bo3_linkstate_step(&e, P1, ID1, 0, 1000, &from) == BO3_LS_CHANGED);
    CHECK(from == BO3_LS_NONE);
    CHECK(e.appeared == 1000 && e.since == 1000 && e.lines == 1);
    CHECK(bo3_linkstate_step(&e, P1, ID1, 0, 1016, &from) == BO3_LS_SAME);
    CHECK(e.since == 1000);
}

/* the title's own way up: 0, 1, 2, 3, one line each, in order */
static void test_way_up(void)
{
    bo3_linkstate e;
    uint32_t from = 0, t = 5000, st;
    memset(&e, 0, sizeof e);
    bo3_linkstate_step(&e, P1, ID1, 0, t, &from);
    for (st = 1; st <= 3; st++) {
        t += 16;
        CHECK(bo3_linkstate_step(&e, P1, ID1, st, t, &from) == BO3_LS_CHANGED);
        CHECK(from == st - 1);
        CHECK(e.since == t && e.appeared == 5000);
        CHECK(bo3_linkstate_step(&e, P1, ID1, st, t + 16, &from) == BO3_LS_SAME);
        t += 16;
    }
    CHECK(e.lines == 4);
}

/* a skipped step (0 to 2 between two looks) is one line, 0 -> 2 */
static void test_skip(void)
{
    bo3_linkstate e;
    uint32_t from = 0;
    memset(&e, 0, sizeof e);
    bo3_linkstate_step(&e, P1, ID1, 0, 0, &from);
    CHECK(bo3_linkstate_step(&e, P1, ID1, 2, 16, &from) == BO3_LS_CHANGED && from == 0);
}

/* a new peer object or a new id in the entry starts it over; an empty entry
 * clears it, and the same object coming back is a new appearance */
static void test_new_peer(void)
{
    bo3_linkstate e;
    uint32_t from = 0;
    memset(&e, 0, sizeof e);
    bo3_linkstate_step(&e, P1, ID1, 0, 100, &from);
    bo3_linkstate_step(&e, P1, ID1, 3, 200, &from);
    CHECK(bo3_linkstate_step(&e, P2, ID2, 3, 300, &from) == BO3_LS_CHANGED);
    CHECK(from == BO3_LS_NONE && e.appeared == 300 && e.lines == 1 && e.peer == P2);
    CHECK(bo3_linkstate_step(&e, P2, ID1, 3, 400, &from) == BO3_LS_CHANGED);
    CHECK(from == BO3_LS_NONE && e.appeared == 400 && e.id == ID1);
    CHECK(bo3_linkstate_step(&e, 0, 0, 0, 500, &from) == BO3_LS_SAME);
    CHECK(e.peer == 0);
    CHECK(bo3_linkstate_step(&e, P2, ID1, 3, 600, &from) == BO3_LS_CHANGED);
    CHECK(from == BO3_LS_NONE && e.appeared == 600 && e.lines == 1);
}

/* a link that keeps changing logs BO3_LS_LINES lines, then stays quiet but
 * is still followed */
static void test_line_cap(void)
{
    bo3_linkstate e;
    uint32_t from = 0, i, logged = 0, quiet = 0;
    memset(&e, 0, sizeof e);
    for (i = 0; i < 100; i++) {
        int r = bo3_linkstate_step(&e, P1, ID1, i & 1, i * 16, &from);
        if (r == BO3_LS_CHANGED)
            logged++;
        else if (r == BO3_LS_QUIET)
            quiet++;
    }
    CHECK(logged == BO3_LS_LINES);
    CHECK(quiet == 100 - BO3_LS_LINES);
    CHECK(e.lines == BO3_LS_LINES);
    CHECK(e.state == 1 && e.since == 99 * 16);
    /* the next game's object gets its lines back */
    CHECK(bo3_linkstate_step(&e, P2, ID2, 0, 5000, &from) == BO3_LS_CHANGED);
}

/* int039 game 83, the host's entry for Mac2: up to 2, then nothing. The
 * lines end at "1 -> 2" and the entry knows since when it is 2. */
static void test_game83(void)
{
    bo3_linkstate e;
    uint32_t from = 0, t, n = 0;
    memset(&e, 0, sizeof e);
    if (bo3_linkstate_step(&e, P1, ID1, 0, 0, &from) == BO3_LS_CHANGED) n++;
    if (bo3_linkstate_step(&e, P1, ID1, 1, 33, &from) == BO3_LS_CHANGED) n++;
    if (bo3_linkstate_step(&e, P1, ID1, 2, 50, &from) == BO3_LS_CHANGED) n++;
    CHECK(from == 1);
    for (t = 66; t < 20000; t += 16)
        CHECK(bo3_linkstate_step(&e, P1, ID1, 2, t, &from) == BO3_LS_SAME);
    CHECK(n == 3 && e.state == 2 && e.since == 50);
}

/* state 2 for 1.5 s, or 4 at once, goes back to 0; 0, 1 and 3 never do */
static void test_retry_when(void)
{
    bo3_linkstate e;
    uint32_t from = 0, st;
    memset(&e, 0, sizeof e);
    bo3_linkstate_step(&e, P1, ID1, 2, 10000, &from);
    CHECK(bo3_linkstate_retry(&e, 10000 + BO3_LS_STUCK_MS - 1) == 0);
    CHECK(e.state == 2 && e.retries == 0);
    CHECK(bo3_linkstate_retry(&e, 10000 + BO3_LS_STUCK_MS) == 1);
    CHECK(e.state == 0 && e.retries == 1 && e.since == 10000 + BO3_LS_STUCK_MS);
    /* the title's 0 is not a change for the log, its next step is */
    CHECK(bo3_linkstate_step(&e, P1, ID1, 0, 11600, &from) == BO3_LS_SAME);
    CHECK(bo3_linkstate_step(&e, P1, ID1, 1, 11616, &from) == BO3_LS_CHANGED && from == 0);
    memset(&e, 0, sizeof e);
    bo3_linkstate_step(&e, P1, ID1, 4, 500, &from);
    CHECK(bo3_linkstate_retry(&e, 500) == 1 && e.state == 0);
    for (st = 0; st <= 3; st += (st == 1 ? 2 : 1)) {
        memset(&e, 0, sizeof e);
        bo3_linkstate_step(&e, P1, ID1, st, 0, &from);
        CHECK(bo3_linkstate_retry(&e, 600000) == 0);
        CHECK(e.state == st && e.retries == 0);
    }
    /* an empty entry never */
    memset(&e, 0, sizeof e);
    CHECK(bo3_linkstate_retry(&e, 600000) == 0);
}

/* at most 3 tries per peer per game; the next game's object gets its own */
static void test_retry_cap(void)
{
    bo3_linkstate e;
    uint32_t from = 0, t = 0, n = 0, i;
    memset(&e, 0, sizeof e);
    bo3_linkstate_step(&e, P1, ID1, 4, t, &from);
    for (i = 0; i < 1000; i++) {
        t += 16;
        bo3_linkstate_step(&e, P1, ID1, 4, t, &from);   /* the title gives up again at once */
        if (bo3_linkstate_retry(&e, t))
            n++;
    }
    CHECK(n == BO3_LS_RETRIES && e.retries == BO3_LS_RETRIES);
    CHECK(bo3_linkstate_step(&e, P2, ID2, 4, t + 16, &from) == BO3_LS_CHANGED);
    CHECK(e.retries == 0 && bo3_linkstate_retry(&e, t + 16) == 1);
    /* an empty table between games clears the entry, so the same object
     * coming back in the next game has its tries again */
    bo3_linkstate_step(&e, 0, 0, 0, t + 32, &from);
    bo3_linkstate_step(&e, P2, ID2, 4, t + 48, &from);
    CHECK(e.retries == 0);
}

/* a clock that went back counts from now, it does not fire at once */
static void test_retry_clock_back(void)
{
    bo3_linkstate e;
    uint32_t from = 0;
    memset(&e, 0, sizeof e);
    bo3_linkstate_step(&e, P1, ID1, 2, 50000, &from);
    CHECK(bo3_linkstate_retry(&e, 1000) == 0);
    CHECK(e.since == 1000);
    CHECK(bo3_linkstate_retry(&e, 1000 + BO3_LS_STUCK_MS - 1) == 0);
    CHECK(bo3_linkstate_retry(&e, 1000 + BO3_LS_STUCK_MS) == 1);
}

/* A model of one side of the title's handshake (0xFD830, 0xFD7D0), one
 * frame a call: 0 -> 1 -> 2 sends our link message; the peer's answer comes
 * 3 frames later and takes 2 to 3; lose_first drops the first answer. The
 * 0xFDA12 timer is left out: it only moves 2 to 4, which retries the same.
 * Returns the frame the link came up in, or -1 after 20 s. */
static int model(int lose_first, int with_retry, uint32_t *retries)
{
    bo3_linkstate e;
    uint32_t link = 0, from = 0, t, sent_at = 0, sends = 0;
    int answer_pending = 0;
    memset(&e, 0, sizeof e);
    for (t = 0; t < 20000; t += 16) {
        if (link == 0)
            link = 1;
        else if (link == 1) {
            link = 2;
            sent_at = t;
            sends++;
            answer_pending = !(lose_first && sends == 1);
        } else if (link == 2 && answer_pending && t >= sent_at + 48) {
            link = 3;
            answer_pending = 0;
        }
        bo3_linkstate_step(&e, P1, ID1, link, t, &from);
        if (with_retry && bo3_linkstate_retry(&e, t)) {
            link = 0;
            answer_pending = 0;
        }
        if (link == 3) {
            *retries = e.retries;
            return (int)(t / 16);
        }
    }
    *retries = e.retries;
    return -1;
}

static void test_model(void)
{
    uint32_t r = 9;
    int f;
    /* nothing lost: up in a few frames, no retry */
    f = model(0, 1, &r);
    CHECK(f >= 0 && f < 10 && r == 0);
    /* int039 game 83: the one answer lost, no retry: never up */
    CHECK(model(1, 0, &r) == -1);
    /* with the retry: up after one try, about 1.6 s in */
    f = model(1, 1, &r);
    CHECK(f >= 0 && r == 1);
    CHECK((uint32_t)f * 16 >= BO3_LS_STUCK_MS && (uint32_t)f * 16 < BO3_LS_STUCK_MS + 200);
}

int main(void)
{
    test_first();
    test_way_up();
    test_skip();
    test_new_peer();
    test_line_cap();
    test_game83();
    test_retry_when();
    test_retry_cap();
    test_retry_clock_back();
    test_model();
    printf("linkstate: %d/%d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
