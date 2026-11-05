/* linkresend test: see CMakeLists.txt. */
#include <stdio.h>
#include <string.h>

#include "linkresend.h"

static int fails, checks;

#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

#define FRAME 16u

/* every half second, from the first lobby frame on */
static void test_cadence(void)
{
    bo3_linkresend r;
    uint64_t t;
    int n = 0;
    memset(&r, 0, sizeof r);
    CHECK(bo3_linkresend_due(&r, 0) == 1);
    for (t = 1; t < BO3_LR_MS; t++)
        CHECK(bo3_linkresend_due(&r, t) == 0);
    CHECK(bo3_linkresend_due(&r, BO3_LR_MS) == 1);
    CHECK(bo3_linkresend_due(&r, BO3_LR_MS + 1) == 0);
    /* 10 s of 16 ms frames: 20 more, never two within 500 ms */
    memset(&r, 0, sizeof r);
    {
        uint64_t last = 0;
        int first = 1;
        for (t = 100000; t < 110000; t += FRAME)
            if (bo3_linkresend_due(&r, t)) {
                if (!first)
                    CHECK(t - last >= BO3_LR_MS && t - last < BO3_LR_MS + FRAME);
                first = 0;
                last = t;
                n++;
            }
    }
    CHECK(n == 20);
    /* a clock that went back starts over at once */
    memset(&r, 0, sizeof r);
    CHECK(bo3_linkresend_due(&r, 50000) == 1);
    CHECK(bo3_linkresend_due(&r, 1000) == 1);
    CHECK(bo3_linkresend_due(&r, 1200) == 0);
    /* a long pause (no lobby screen): due at once on return */
    CHECK(bo3_linkresend_due(&r, 900000) == 1);
}

/* only a peer whose link is up and whose slot is free */
static void test_peer_filter(void)
{
    uint32_t link;
    for (link = 0; link < 8; link++)
        CHECK(bo3_linkresend_peer(link, 0) == (link == 3));
    CHECK(bo3_linkresend_peer(3, 1) == 0);
    CHECK(bo3_linkresend_peer(3, 3) == 0);
    CHECK(bo3_linkresend_peer(3, 2) == 1);   /* bit 1 alone is not "busy" */
    CHECK(BO3_LR_STALE != 0xFFFFFFFFu);
}

/* A model of the title's sender (0x102610) toward one peer, the network, and
 * the peer's receiver (0xFE2C0): the table is 8 ids and 8 link states. */
typedef struct {
    uint32_t cur_id[8], cur_st[8];     /* our table now (built every frame) */
    uint32_t last_id[8], last_st[8];   /* +0x540/+0x560: what we last queued */
    uint32_t link;                     /* +0x4F0 */
    uint8_t slot;                      /* +0xC80 */
    uint32_t rx_id[8], rx_st[8];       /* the peer's +0x580/+0x5A0 for us */
    int sent, lost_first;              /* datagrams sent; how many to lose first */
} model;

static void model_init(model *m, int lost_first)
{
    int i;
    memset(m, 0, sizeof *m);
    for (i = 0; i < 8; i++) {
        m->cur_id[i] = m->last_id[i] = m->rx_id[i] = 0xFFFFFFFFu;
        m->cur_st[i] = m->last_st[i] = m->rx_st[i] = 5;
    }
    m->lost_first = lost_first;
}

/* 0x102610, then the slot goes out in the same frame */
static void model_frame(model *m)
{
    int i, same = 1;
    for (i = 0; i < 8; i++)
        if (m->cur_id[i] != m->last_id[i] || m->cur_st[i] != m->last_st[i])
            same = 0;
    if (same || (m->slot & 1) || m->link != 3)
        return;
    m->slot |= 1;
    memcpy(m->last_id, m->cur_id, sizeof m->cur_id);
    memcpy(m->last_st, m->cur_st, sizeof m->cur_st);
    /* the send: one datagram, no answer */
    m->slot &= (uint8_t)~1u;
    m->sent++;
    if (m->lost_first > 0) {
        m->lost_first--;
        return;
    }
    memcpy(m->rx_id, m->cur_id, sizeof m->cur_id);
    memcpy(m->rx_st, m->cur_st, sizeof m->cur_st);
}

/* the peer has our report with every player's link up */
static int model_has(const model *m, int players)
{
    int i;
    for (i = 0; i < players; i++)
        if (m->rx_id[i] != m->cur_id[i] || m->rx_st[i] != 3)
            return 0;
    return 1;
}

/* the int036 case: the host has 5 guests up; a sixth player appears, its
 * link comes up 50 ms later, the one report is lost. Without the repeat the
 * newcomer never has it (stuck in 0xB); with it, by the next half second. */
static uint64_t run(int lost_first, int with_resend, uint64_t until)
{
    model m;
    bo3_linkresend r;
    uint64_t t, got = 0;
    int i;
    model_init(&m, lost_first);
    memset(&r, 0, sizeof r);
    for (i = 0; i < 6; i++) {
        m.cur_id[i] = 0xB0u + (uint32_t)i;
        m.cur_st[i] = 3;
    }
    m.cur_st[5] = 1;                    /* the newcomer, not linked yet */
    for (t = 10000; t < until; t += FRAME) {
        if (t >= 10050) {
            m.link = 3;
            m.cur_st[5] = 3;
        }
        if (with_resend && bo3_linkresend_due(&r, t) && bo3_linkresend_peer(m.link, m.slot))
            m.last_id[0] = BO3_LR_STALE;
        model_frame(&m);
        if (!got && model_has(&m, 6))
            got = t;
    }
    return got;
}

static void test_one_report_lost(void)
{
    uint64_t got;
    /* the title alone: one lost datagram and never again */
    CHECK(run(1, 0, 60000) == 0);
    CHECK(run(0, 0, 60000) != 0);
    /* with the repeat: within half a second (and a frame) of the loss */
    got = run(1, 1, 60000);
    CHECK(got != 0 && got - 10050 <= BO3_LR_MS + FRAME);
    /* five lost in a row: still there in under 3 s, well before a 4 s start */
    got = run(5, 1, 60000);
    CHECK(got != 0 && got - 10050 <= 5 * BO3_LR_MS + FRAME);
    /* nothing lost: the same frame as before (the title's own send) */
    CHECK(run(0, 1, 60000) == run(0, 0, 60000));
}

/* the repeat is one datagram per peer per half second, never more; and a
 * change to the table still goes out at once, as in the title */
static void test_traffic_and_changes(void)
{
    model m;
    bo3_linkresend r;
    uint64_t t;
    int i, before;
    model_init(&m, 0);
    memset(&r, 0, sizeof r);
    for (i = 0; i < 6; i++) {
        m.cur_id[i] = 0xB0u + (uint32_t)i;
        m.cur_st[i] = 3;
    }
    m.link = 3;
    for (t = 0; t < 10000; t += FRAME) {
        if (bo3_linkresend_due(&r, t) && bo3_linkresend_peer(m.link, m.slot))
            m.last_id[0] = BO3_LR_STALE;
        model_frame(&m);
    }
    CHECK(m.sent == 20);
    /* a link goes down: the new table goes out in that frame, not later */
    before = m.sent;
    m.cur_st[2] = 2;
    model_frame(&m);
    CHECK(m.sent == before + 1 && m.rx_st[2] == 2);
    /* a link that is down: nothing to that peer, however long */
    model_init(&m, 0);
    memset(&r, 0, sizeof r);
    m.cur_id[0] = 0xB0u;
    m.cur_st[0] = 3;
    m.link = 2;
    for (t = 0; t < 10000; t += FRAME) {
        if (bo3_linkresend_due(&r, t) && bo3_linkresend_peer(m.link, m.slot))
            m.last_id[0] = BO3_LR_STALE;
        model_frame(&m);
    }
    CHECK(m.sent == 0);
}

/* BO-379: the title's resend queue (0x1B80C0, 32 entries shared by
 * every peer) with one peer whose acknowledgements never reach us (it hears
 * us, we do not hear it) and one healthy peer that answers in 2 frames.
 * full = how often a new reliable message found all 32 entries waiting and
 * was dropped; healthy = reports that reached the healthy peer. */
typedef struct {
    bo3_lr_entry q[BO3_LR_QUEUE];
    uint64_t acked_at[BO3_LR_QUEUE];
    int full, healthy;
} queue_model;

static void q_put(queue_model *m, uint32_t dest, uint8_t chan, uint64_t ack_at)
{
    int i;
    for (i = 0; i < BO3_LR_QUEUE; i++)
        if (m->q[i].done) {
            m->q[i].dest = dest;
            m->q[i].chan = chan;
            m->q[i].done = 0;
            m->acked_at[i] = ack_at;
            if (dest == 0xB2u)
                m->healthy++;
            return;
        }
    m->full++;
}

static void q_tick(queue_model *m, uint64_t t)
{
    int i;
    for (i = 0; i < BO3_LR_QUEUE; i++)
        if (!m->q[i].done && m->acked_at[i] && t >= m->acked_at[i])
            m->q[i].done = 1;
}

static void one_way(int with_check, int *full, int *healthy, int *waiting)
{
    queue_model m;
    bo3_linkresend r;
    uint64_t t;
    int i;
    memset(&m, 0, sizeof m);
    memset(&r, 0, sizeof r);
    for (i = 0; i < BO3_LR_QUEUE; i++)
        m.q[i].done = 1;
    for (t = 0; t < 60000; t += FRAME) {
        q_tick(&m, t);
        if (bo3_linkresend_due(&r, t)) {
            /* 0xB1: one-way, never acknowledged; 0xB2: acknowledged 2 frames later */
            if (!with_check || !bo3_linkresend_waiting(m.q, BO3_LR_QUEUE, 0xB1u, BO3_LR_CHANNEL))
                q_put(&m, 0xB1u, BO3_LR_CHANNEL, 0);
            if (!with_check || !bo3_linkresend_waiting(m.q, BO3_LR_QUEUE, 0xB2u, BO3_LR_CHANNEL))
                q_put(&m, 0xB2u, BO3_LR_CHANNEL, t + 2 * FRAME);
        }
    }
    /* then the race's own reliable message (the start, a result) to the healthy peer */
    q_put(&m, 0xB2u, 0x0C, t + 2 * FRAME);
    *full = m.full;
    *healthy = m.healthy;
    *waiting = bo3_linkresend_waiting(m.q, BO3_LR_QUEUE, 0xB1u, BO3_LR_CHANNEL);
}

static void test_one_way_peer(void)
{
    bo3_lr_entry q[3] = { { 0xB1u, 9, 0 }, { 0xB1u, 9, 1 }, { 0xB1u, 0x0C, 0 } };
    int full, healthy, waiting;
    CHECK(bo3_linkresend_waiting(q, 3, 0xB1u, 9) == 1);
    CHECK(bo3_linkresend_waiting(q, 3, 0xB1u, 0x0C) == 1);
    CHECK(bo3_linkresend_waiting(q, 3, 0xB2u, 9) == 0);
    /* before: the one-way peer's reports fill the queue, then everything is dropped,
     * the race's own message to the healthy peer too */
    one_way(0, &full, &healthy, &waiting);
    CHECK(waiting == BO3_LR_QUEUE - 1 || waiting == BO3_LR_QUEUE);
    CHECK(full > 50);
    /* after: one entry waits for the one-way peer, the healthy peer still gets a report
     * every half second, and nothing is ever dropped */
    one_way(1, &full, &healthy, &waiting);
    CHECK(waiting == 1);
    CHECK(full == 0);
    CHECK(healthy > (int)(60000 / (BO3_LR_MS + FRAME)));   /* every due time (500 ms, a frame late at most) */
}

/* BO-379, night056 F2 as it happened (u1's queue, read from memory): five
 * peers; 0x27B (a Mac) acknowledges for 16 s, then its answers stop reaching
 * us while its link stays up; 0x285 (a player that never fully linked) holds
 * four of the title's own channel 6 messages and then leaves the game
 * (kicked); 15 s later the race starts and the title sends one reliable
 * message to each of the four players still in the game.
 * Returns how many of those four were dropped; *mac = reports waiting for
 * 0x27B, *gone = entries waiting for players no longer in the table. */
static int f2_lobby(int with_check, int *mac, int *gone)
{
    static const uint32_t peers[5] = { 0x28Au, 0x283u, 0x27Bu, 0x281u, 0x285u };
    queue_model m;
    bo3_linkresend r;
    uint64_t t, mac_quiet = 16000, kick = 18000, start = 31000;
    int i, np = 5, dropped;
    memset(&m, 0, sizeof m);
    memset(&r, 0, sizeof r);
    for (i = 0; i < BO3_LR_QUEUE; i++)
        m.q[i].done = 1;
    for (i = 0; i < 4; i++)
        q_put(&m, 0x285u, 6, 0);
    for (t = 0; t < start; t += FRAME) {
        q_tick(&m, t);
        if (t >= kick)
            np = 4;                    /* 0x285 out of the table; its entries stay */
        if (bo3_linkresend_due(&r, t))
            for (i = 0; i < np; i++) {
                uint32_t d = peers[i];
                uint64_t ack = (d == 0x27Bu && t >= mac_quiet) ? 0 : t + 2 * FRAME;
                if (d == 0x285u)
                    continue;          /* link never up: no report (bo3_linkresend_peer) */
                if (with_check && bo3_linkresend_waiting(m.q, BO3_LR_QUEUE, d, BO3_LR_CHANNEL))
                    continue;
                q_put(&m, d, BO3_LR_CHANNEL, ack);
            }
    }
    *mac = bo3_linkresend_waiting(m.q, BO3_LR_QUEUE, 0x27Bu, BO3_LR_CHANNEL);
    *gone = bo3_linkresend_gone(m.q, BO3_LR_QUEUE, peers, 4);
    dropped = m.full;
    for (i = 0; i < 4; i++)
        q_put(&m, peers[i], 0x0B, peers[i] == 0x27Bu ? 0 : t + 2 * FRAME);
    return m.full - dropped;
}

static void test_f2_race_start(void)
{
    bo3_lr_entry q[4] = { { 0x10u, 9, 0 }, { 0x11u, 6, 0 }, { 0x12u, 6, 1 }, { 0x13u, 6, 0 } };
    uint32_t ids[2] = { 0x10u, 0x13u };
    int mac, gone;
    CHECK(bo3_linkresend_gone(q, 4, ids, 2) == 1);   /* 0x11 waits, 0x12 is done */
    CHECK(bo3_linkresend_gone(q, 4, ids, 0) == 3);
    /* before: 0x27B's reports fill what 0x285 left free; the race start is lost */
    CHECK(f2_lobby(0, &mac, &gone) > 0);
    CHECK(mac + gone >= BO3_LR_QUEUE - 3);       /* full but for the healthy ones in flight */
    CHECK(gone == 4);
    /* after: one report waits for 0x27B, the start reaches every player */
    CHECK(f2_lobby(1, &mac, &gone) == 0);
    CHECK(mac == 1);
    CHECK(gone == 4);
}

int main(void)
{
    test_cadence();
    test_peer_filter();
    test_one_report_lost();
    test_traffic_and_changes();
    test_one_way_peer();
    test_f2_race_start();
    printf("linkresend: %d of %d checks failed\n", fails, checks);
    return fails ? 1 : 0;
}
