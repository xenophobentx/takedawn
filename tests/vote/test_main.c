/* The track vote's rules: vote.c without the game. */
#include <stdio.h>
#include <string.h>

#include "vote.h"

static int s_fail;

#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); s_fail++; } } while (0)

static const uint8_t VALID[] = { 1, 4, 7, 10, 13, 20, 22 };
#define NV ((int)sizeof VALID)

static int in_valid(uint8_t t)
{
    int i;
    for (i = 0; i < NV; i++)
        if (VALID[i] == t)
            return 1;
    return 0;
}

static void start(vote_host *h, uint32_t *rng, int guests)
{
    uint32_t ids[] = { 11, 12, 13, 14, 15, 16, 17 };
    CHECK(bo3_vote_host_begin(h, 5, VALID, NV, 1000, rng));
    bo3_vote_host_players(h, ids, guests);
}

static void test_rows(void)
{
    uint32_t rng = 7;
    int i, n;
    for (n = 0; n < 500; n++) {
        vote_host h;
        CHECK(bo3_vote_host_begin(&h, 1, VALID, NV, 0, &rng));
        CHECK(h.rows == VOTE_ROWS);
        for (i = 0; i < VOTE_TRACKS; i++)
            CHECK(in_valid(h.track[i]));
        CHECK(h.track[0] != h.track[1] && h.track[0] != h.track[2] && h.track[1] != h.track[2]);
    }
    {
        /* few unlocked: fewer rows, RANDOM still last */
        vote_host h;
        uint8_t two[] = { 3, 9 };
        CHECK(bo3_vote_host_begin(&h, 1, two, 2, 0, &rng));
        CHECK(h.rows == 3 && h.track[2] == VOTE_NO_TRACK);
        CHECK(!bo3_vote_host_begin(&h, 1, two, 0, 0, &rng));
    }
    {
        /* every valid track comes up in time */
        int seen[32];
        memset(seen, 0, sizeof seen);
        for (n = 0; n < 500; n++) {
            vote_host h;
            bo3_vote_host_begin(&h, 1, VALID, NV, 0, &rng);
            for (i = 0; i < VOTE_TRACKS; i++)
                seen[h.track[i]] = 1;
        }
        for (i = 0; i < NV; i++)
            CHECK(seen[VALID[i]]);
    }
}

static void test_tally_change(void)
{
    vote_host h;
    uint32_t rng = 9;
    start(&h, &rng, 2);
    CHECK(h.np == 3);
    CHECK(bo3_vote_host_choice(&h, 0, 5, 1, 1));
    CHECK(bo3_vote_host_choice(&h, 11, 5, 1, 1));
    CHECK(h.tally[1] == 2);
    /* a vote can change */
    CHECK(bo3_vote_host_choice(&h, 11, 5, 2, 2));
    CHECK(h.tally[1] == 1 && h.tally[2] == 1);
    /* an old packet coming late changes nothing */
    CHECK(!bo3_vote_host_choice(&h, 11, 5, 1, 1));
    CHECK(h.tally[2] == 1);
    /* the same again: harmless */
    bo3_vote_host_choice(&h, 11, 5, 2, 2);
    CHECK(h.tally[2] == 1 && h.tally[1] == 1);
    /* the number wraps */
    CHECK(bo3_vote_host_choice(&h, 12, 5, 0, 255));
    CHECK(bo3_vote_host_choice(&h, 12, 5, 3, 0));
    CHECK(h.tally[3] == 1 && h.tally[0] == 0);
    /* not ours */
    CHECK(!bo3_vote_host_choice(&h, 99, 5, 1, 1));
    CHECK(!bo3_vote_host_choice(&h, 11, 4, 1, 9));      /* another round */
    CHECK(!bo3_vote_host_choice(&h, 11, 5, 4, 9));      /* no such row */
    CHECK(!bo3_vote_host_choice(&h, 11, 5, -1, 9));
}

static void test_clock(void)
{
    vote_host h;
    uint32_t rng = 11;
    start(&h, &rng, 1);
    CHECK(bo3_vote_host_secs(&h, 1000) == 15);
    CHECK(bo3_vote_host_secs(&h, 1001) == 15);
    CHECK(bo3_vote_host_secs(&h, 2000) == 14);
    CHECK(bo3_vote_host_secs(&h, 15999) == 1);
    CHECK(bo3_vote_host_tick(&h, 15999, VALID, NV, &rng) == VH_NONE);
    bo3_vote_host_choice(&h, 11, 5, 2, 1);
    CHECK(bo3_vote_host_tick(&h, 16000, VALID, NV, &rng) == VH_WON);
    CHECK(h.phase == VP_WIN && h.win_row == 2 && h.win_track == h.track[2]);
    /* once */
    CHECK(bo3_vote_host_tick(&h, 16001, VALID, NV, &rng) == VH_NONE);
    /* a vote after the end does not count */
    CHECK(!bo3_vote_host_choice(&h, 0, 5, 1, 1));
    /* everybody voted: at once */
    start(&h, &rng, 2);
    bo3_vote_host_choice(&h, 0, 5, 0, 1);
    bo3_vote_host_choice(&h, 11, 5, 0, 1);
    CHECK(bo3_vote_host_tick(&h, 2000, VALID, NV, &rng) == VH_NONE);
    bo3_vote_host_choice(&h, 12, 5, 1, 1);
    CHECK(bo3_vote_host_tick(&h, 2000, VALID, NV, &rng) == VH_WON);
    CHECK(h.win_row == 0);
}

static void test_ties_and_none(void)
{
    uint32_t rng = 13;
    int seen[VOTE_ROWS], i, n;
    memset(seen, 0, sizeof seen);
    for (n = 0; n < 400; n++) {
        vote_host h;
        start(&h, &rng, 1);
        bo3_vote_host_choice(&h, 0, 5, 0, 1);
        bo3_vote_host_choice(&h, 11, 5, 2, 1);
        CHECK(bo3_vote_host_tick(&h, 2000, VALID, NV, &rng) == VH_WON);
        CHECK(h.win_row == 0 || h.win_row == 2);
        seen[h.win_row]++;
    }
    CHECK(seen[0] > 100 && seen[2] > 100);
    /* no votes: any row, a real track in the end */
    memset(seen, 0, sizeof seen);
    for (n = 0; n < 800; n++) {
        vote_host h;
        start(&h, &rng, 2);
        CHECK(bo3_vote_host_tick(&h, 16000, VALID, NV, &rng) == VH_WON);
        seen[h.win_row]++;
        CHECK(in_valid(h.win_track));
    }
    for (i = 0; i < VOTE_ROWS; i++)
        CHECK(seen[i] > 100);
    /* RANDOM won: a valid track, not always the same */
    {
        int t[32], k = 0;
        memset(t, 0, sizeof t);
        for (n = 0; n < 200; n++) {
            vote_host h;
            start(&h, &rng, 1);
            bo3_vote_host_choice(&h, 0, 5, 3, 1);
            bo3_vote_host_choice(&h, 11, 5, 3, 1);
            CHECK(bo3_vote_host_tick(&h, 2000, VALID, NV, &rng) == VH_WON);
            CHECK(h.win_row == 3 && in_valid(h.win_track));
            t[h.win_track] = 1;
        }
        for (i = 0; i < 32; i++)
            k += t[i];
        CHECK(k == NV);
    }
}

static void test_players(void)
{
    vote_host h;
    uint32_t rng = 17, two[] = { 12 }, none[] = { 0 }, back[] = { 12, 11, 30 };
    start(&h, &rng, 2);
    bo3_vote_host_choice(&h, 0, 5, 1, 1);
    bo3_vote_host_choice(&h, 11, 5, 2, 1);
    bo3_vote_host_choice(&h, 12, 5, 2, 7);
    CHECK(h.tally[2] == 2);
    /* one leaves: the vote goes on without his vote */
    bo3_vote_host_players(&h, two, 1);
    CHECK(h.np == 2 && h.tally[2] == 1 && h.tally[1] == 1);
    CHECK(h.p[1].id == 12 && h.p[1].choice == 2 && h.p[1].seq == 7);
    /* a guest back or new: unvoted; the one who stayed keeps his */
    bo3_vote_host_players(&h, back, 3);
    CHECK(h.np == 4 && h.p[1].choice == 2 && h.p[2].choice == -1 && h.p[3].choice == -1);
    /* a late joiner blocks "everybody voted" until he has, and can vote */
    CHECK(bo3_vote_host_tick(&h, 3000, VALID, NV, &rng) == VH_NONE);
    CHECK(bo3_vote_host_choice(&h, 30, 5, 2, 1) && bo3_vote_host_choice(&h, 11, 5, 2, 1));
    CHECK(bo3_vote_host_tick(&h, 3000, VALID, NV, &rng) == VH_WON);
    CHECK(h.win_row == 2 && h.tally[2] == 3);
    /* all guests gone: off */
    start(&h, &rng, 2);
    bo3_vote_host_players(&h, none, 0);
    CHECK(bo3_vote_host_tick(&h, 2000, VALID, NV, &rng) == VH_OFF);
    CHECK(h.phase == VP_OVER);
    /* more members than players: no overrun */
    {
        uint32_t many[20];
        int i;
        for (i = 0; i < 20; i++)
            many[i] = 100u + (uint32_t)i;
        start(&h, &rng, 0);
        bo3_vote_host_players(&h, many, 20);
        CHECK(h.np == VOTE_PLAYERS);
    }
}

/* the guest's copy against the host's, through a line that may lose packets */
static void test_client_host(void)
{
    vote_host h;
    vote_client c, late;
    uint32_t rng = 19;
    uint8_t d[VOTE_DATA];
    int lost, i, sends = 0;
    uint8_t round, seq;
    int row;
    unsigned long long t;

    start(&h, &rng, 2);
    bo3_vote_client_reset(&c);
    bo3_vote_host_pack(&h, 11, 1000, d);
    CHECK(bo3_vote_client_state(&c, 77, d, 1000));
    CHECK(c.phase == VP_VOTE && c.round == 5 && c.secs == 15 && c.rows == 4 && c.host == 77);
    for (i = 0; i < VOTE_TRACKS; i++)
        CHECK(c.track[i] == h.track[i]);
    CHECK(!bo3_vote_client_state(&c, 77, d, 1100));     /* the same again: nothing to redraw */

    /* a vote, the first ten copies lost: it keeps going until the echo */
    bo3_vote_client_choose(&c, 2);
    for (t = 1200; t < 1200 + 150 * 10; t += 16) {
        if (bo3_vote_client_tick(&c, t, &lost))
            sends++;
        c.state_ms = t;                                 /* the state still comes */
    }
    CHECK(sends >= 9 && sends <= 11);
    CHECK(!c.acked);
    CHECK(bo3_vote_client_tick(&c, t + 200, &lost) == 1);
    bo3_vote_client_pack(&c, d);
    CHECK(bo3_vote_choice_read(d, &round, &row, &seq));
    CHECK(bo3_vote_host_choice(&h, 11, round, row, seq));
    CHECK(h.tally[2] == 1);
    bo3_vote_host_pack(&h, 11, 2000, d);
    bo3_vote_client_state(&c, 77, d, t + 300);
    CHECK(c.acked && c.tally[2] == 1);
    CHECK(!bo3_vote_client_tick(&c, t + 600, &lost));   /* echoed: quiet */

    /* changed: an echo of the old vote does not settle the new one */
    bo3_vote_client_choose(&c, 1);
    CHECK(!c.acked);
    bo3_vote_client_state(&c, 77, d, t + 700);
    CHECK(!c.acked);
    /* the guest's own packet out of order: the older never overrides */
    {
        uint8_t oldseq = (uint8_t)(c.seq - 1u);
        CHECK(bo3_vote_host_choice(&h, 11, 5, 1, c.seq));
        CHECK(!bo3_vote_host_choice(&h, 11, 5, 2, oldseq));
        CHECK(h.p[1].choice == 1);
    }

    /* a late joiner: the next state shows the vote as it stands */
    bo3_vote_host_players(&h, (uint32_t[]){ 11, 12, 13 }, 3);
    bo3_vote_host_pack(&h, 13, 9000, d);
    bo3_vote_client_reset(&late);
    CHECK(bo3_vote_client_state(&late, 77, d, 9000));
    CHECK(late.phase == VP_VOTE && late.secs == 7 && late.tally[1] == 1 && late.choice == -1);

    /* the winner reaches every copy */
    h.phase = VP_WIN;
    h.win_row = 1;
    h.win_track = h.track[1];
    bo3_vote_host_pack(&h, 11, 9500, d);
    CHECK(bo3_vote_client_state(&c, 77, d, 9500));
    CHECK(c.phase == VP_WIN && c.win_row == 1 && c.win_track == h.track[1]);
    CHECK(!bo3_vote_client_tick(&c, 9600, &lost) && !lost);     /* no sending after the end */
    bo3_vote_client_choose(&c, 2);                              /* no choosing either */
    CHECK(c.choice == 1);

    /* the next round starts clean, and its numbers stay ahead of the last's */
    {
        uint8_t was = c.seq;
        bo3_vote_host_begin(&h, 6, VALID, NV, 20000, &rng);
        bo3_vote_host_pack(&h, 11, 20000, d);
        CHECK(bo3_vote_client_state(&c, 77, d, 20000));
        CHECK(c.round == 6 && c.choice == -1 && c.phase == VP_VOTE && c.seq == was);
    }

    /* the host is gone: no state for 3 s */
    CHECK(!bo3_vote_client_tick(&c, 22900, &lost) && !lost);
    bo3_vote_client_tick(&c, 23001, &lost);
    CHECK(lost);
    /* nothing running: nothing lost */
    bo3_vote_client_reset(&c);
    bo3_vote_client_tick(&c, 99999, &lost);
    CHECK(!lost);

    /* junk */
    memset(d, 0, sizeof d);
    CHECK(!bo3_vote_client_state(&c, 1, d, 1));
    CHECK(c.phase == VP_NONE);
    d[0] = VK_STATE; d[2] = 9; d[7] = 4;
    CHECK(!bo3_vote_client_state(&c, 1, d, 1));
    d[2] = VP_VOTE; d[7] = 9;
    CHECK(!bo3_vote_client_state(&c, 1, d, 1));
    d[0] = VK_STATE;
    CHECK(!bo3_vote_choice_read(d, &round, &row, &seq));
}

/* The winner notice on the title's box: a fake box with the title's own
 * rules (open takes two nodes and never gives the old ones back; close gives
 * back two without looking at +0xA; the title's guarded close "if open,
 * close"). Every path has to end with the pool where it started. */
static int fb_open, fb_kind, fb_free, fb_bad;

static void fb_title_open(int kind)
{
    fb_free -= 2;
    fb_open = 1;
    fb_kind = kind;
}

static void fb_close(void)
{
    if (!fb_open)
        fb_bad++;                   /* a double release */
    fb_free += 2;
    fb_open = 0;
}

static void fb_ev(int *ours, int ev)
{
    if (bo3_vote_note_step(ours, ev, fb_open, fb_kind))
        fb_close();
}

static void fb_we_open(int *ours)
{
    if (fb_open)
        return;                     /* the glue opens only on a free box */
    fb_title_open(VOTE_NOTE_KIND);
    bo3_vote_note_step(ours, VN_WE_OPEN, 1, VOTE_NOTE_KIND);
}

/* the title opens its own box: our hook runs first */
static void fb_title_opens(int *ours, int kind)
{
    fb_ev(ours, VN_TITLE_OPEN);
    fb_title_open(kind);
}

static void test_note_box(void)
{
    int ours, ev, k;
    const int ends[] = { VN_TIMEOUT, VN_LOST, VN_NEW_ROUND, VN_LEFT };
    /* ours up, then each way it ends, in the plain state and out of it */
    for (k = 0; k < 4; k++) {
        ours = 0;
        fb_open = 0;
        fb_free = 20;
        fb_bad = 0;
        fb_we_open(&ours);
        CHECK(ours && fb_free == 18);
        fb_ev(&ours, VN_FRAME);
        fb_ev(&ours, ends[k]);
        CHECK(!ours && !fb_open && fb_free == 20 && !fb_bad);
        fb_ev(&ours, VN_LEFT);      /* nothing twice */
        CHECK(fb_free == 20 && !fb_bad);
    }
    /* the title opens over ours (the update's notes, B's leave question, the
     * game gone): ours is closed first; the title's box stays the title's */
    for (ev = 0; ev < 3; ev++) {
        int kind = ev == 0 ? VOTE_NOTE_KIND : 7;
        ours = 0;
        fb_open = 0;
        fb_free = 20;
        fb_bad = 0;
        fb_we_open(&ours);
        fb_title_opens(&ours, kind);
        CHECK(!ours && fb_open && fb_free == 18);
        /* our time runs out while the title's box is up: not ours to close */
        fb_ev(&ours, VN_FRAME);
        fb_ev(&ours, VN_TIMEOUT);
        fb_ev(&ours, VN_LEFT);
        CHECK(fb_open && fb_free == 18);
        fb_close();                 /* the title closes its own */
        CHECK(fb_free == 20 && !fb_bad);
    }
    /* the title closes ours itself (its guarded close at a state change),
     * then its own unconditional close-and-open: no second close from us */
    ours = 0;
    fb_open = 0;
    fb_free = 20;
    fb_bad = 0;
    fb_we_open(&ours);
    if (fb_open)
        fb_close();
    fb_ev(&ours, VN_FRAME);
    CHECK(!ours);
    fb_title_opens(&ours, VOTE_NOTE_KIND);
    fb_ev(&ours, VN_TIMEOUT);
    fb_ev(&ours, VN_LOST);
    CHECK(fb_open && fb_free == 18 && !fb_bad);
    fb_close();
    CHECK(fb_free == 20 && !fb_bad);
    /* ten winners in a row, each one cut short a different way: flat */
    ours = 0;
    fb_open = 0;
    fb_free = 20;
    fb_bad = 0;
    for (k = 0; k < 10; k++) {
        fb_we_open(&ours);
        if (k % 3 == 0)
            fb_title_opens(&ours, 7), fb_close();
        else
            fb_ev(&ours, ends[k % 4]);
        fb_ev(&ours, VN_FRAME);
    }
    CHECK(!fb_open && fb_free == 20 && !fb_bad);
}

int main(void)
{
    test_rows();
    test_tally_change();
    test_clock();
    test_ties_and_none();
    test_players();
    test_client_host();
    test_note_box();
    if (s_fail)
        printf("%d failed\n", s_fail);
    else
        printf("ok\n");
    return s_fail != 0;
}
