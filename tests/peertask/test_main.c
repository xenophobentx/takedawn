/* peertask test: see CMakeLists.txt. */
#include <stdio.h>
#include <string.h>

#include "peertask.h"

static int fails, checks;

#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

/* our XUIDs: the port offset in bytes 2..3, the player's address in 4..7 */
static void xuid_of(uint8_t x[8], int player)
{
    memset(x, 0, 8);
    x[2] = (uint8_t)(player * 100);
    x[4] = 10; x[5] = 64; x[6] = 82; x[7] = (uint8_t)(213 + player);
}

/* What online.c's XOnlineTaskContinue answers for a handle: 1 done with this
 * lookup's own result (xuid, block), 2 done (filled at its lookup), 0 not a
 * peer task (the old "unknown task -> S_OK", nothing filled in). */
static int cont(const bo3_peertasks *p, uint32_t h, const uint8_t *want_xuid, uint32_t want_out)
{
    const bo3_peertask *t;
    if (!bo3_peertask_is(h))
        return 0;
    t = bo3_peertask_find(p, h);
    if (!t)
        return 2;
    return memcmp(t->xuid, want_xuid, 8) == 0 && t->out == want_out ? 1 : -1;
}

/* int035 long run, Mac1 game 17: a lost link, then a lookup of that peer
 * about 5 times a second, never closed. Then a new game: the host and four
 * guests looked up at the same time; each must get its own result. */
static void test_storm_then_concurrent(void)
{
    static bo3_peertasks p;
    uint8_t x[8], xs[6][8];
    uint32_t h[6], storm_first = 0, storm_last = 0;
    int i, k;
    memset(&p, 0, sizeof p);
    xuid_of(x, 3);
    for (i = 0; i < 200; i++) {           /* far past the old 32 slots, no Close */
        uint32_t s = bo3_peertask_open(&p, x, 0x00100000u + 0x100u * (uint32_t)i);
        if (!i)
            storm_first = s;
        storm_last = s;
        CHECK(bo3_peertask_is(s));
    }
    CHECK(storm_first != storm_last);
    /* six lookups at once (all opened before any Continue) */
    for (k = 0; k < 6; k++) {
        xuid_of(xs[k], k);
        h[k] = bo3_peertask_open(&p, xs[k], 0x00200000u + 0x100u * (uint32_t)k);
    }
    for (k = 0; k < 6; k++) {
        int j;
        for (j = 0; j < k; j++)
            CHECK(h[j] != h[k]);           /* each its own handle */
    }
    /* each Continue: its own peer, its own block, never "unknown" */
    for (k = 0; k < 6; k++)
        CHECK(cont(&p, h[k], xs[k], 0x00200000u + 0x100u * (uint32_t)k) == 1);
    /* one Close frees only its own task */
    bo3_peertask_close(&p, h[2]);
    CHECK(cont(&p, h[2], xs[2], 0x00200200u) == 2);
    for (k = 0; k < 6; k++)
        if (k != 2)
            CHECK(cont(&p, h[k], xs[k], 0x00200000u + 0x100u * (uint32_t)k) == 1);
    /* a storm task too old for the ring: still done (its block was filled
     * at the lookup), never "unknown" */
    CHECK(cont(&p, storm_first, x, 0x00100000u) == 2);
    /* a Close of a task long gone does not touch the newer one in its slot */
    bo3_peertask_close(&p, storm_first);
    for (k = 0; k < 6; k++)
        if (k != 2)
            CHECK(bo3_peertask_find(&p, h[k]) != NULL);
    /* the logon task and anything else are not ours */
    CHECK(!bo3_peertask_is(0xF1000001u) && !bo3_peertask_is(0));
}

/* the running count wraps after 16M lookups without handing out 0xF2000000
 * (online.c's old first handle; 0 marks a free slot) */
static void test_wrap(void)
{
    static bo3_peertasks p;
    uint8_t x[8];
    uint32_t h;
    memset(&p, 0, sizeof p);
    xuid_of(x, 1);
    p.seq = 0x00FFFFFEu;
    h = bo3_peertask_open(&p, x, 1);
    CHECK(h == 0xF2FFFFFFu && bo3_peertask_find(&p, h));
    h = bo3_peertask_open(&p, x, 2);
    CHECK(h == 0xF2000001u && bo3_peertask_find(&p, h) && bo3_peertask_find(&p, h)->out == 2);
    CHECK(!bo3_peertask_find(&p, 0xF2000000u));
}

int main(void)
{
    test_storm_then_concurrent();
    test_wrap();
    printf("%d checks, %d failed\n", checks, fails);
    return fails != 0;
}
