/* peerqos test: see CMakeLists.txt. */
#include <stdio.h>

#include "peerqos.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

int main(void)
{
    bo3_qos_values v;
    /* no measurement: the old fixed answer */
    v = bo3_qos_fill(0, 500, 500, 9, 9);
    CHECK(v.rtt_min == 20 && v.rtt_med == 30 && v.xmit == 4 && v.recv == 4);
    /* measured */
    v = bo3_qos_fill(1, 140, 152, 16, 2);
    CHECK(v.rtt_min == 140 && v.rtt_med == 152 && v.xmit == 16 && v.recv == 14);
    /* the title's special values never come out: 0 and 0xFFFF */
    v = bo3_qos_fill(1, 0, 0, 1, 0);
    CHECK(v.rtt_min == 1 && v.rtt_med == 1);
    v = bo3_qos_fill(1, 70000, 70000, 1, 0);
    CHECK(v.rtt_min == 0xFFFE && v.rtt_med == 0xFFFE);
    /* odd counts stay in range */
    v = bo3_qos_fill(1, 10, 10, 0, 0);
    CHECK(v.xmit == 1 && v.recv == 1);
    v = bo3_qos_fill(1, 10, 10, 5, 9);
    CHECK(v.xmit == 5 && v.recv == 0);
    /* the blocks: kept, released once, a stray or second release frees nothing */
    {
        static bo3_qos_keep k;
        int i;
        CHECK(!bo3_qos_keep_add(&k, 0));
        CHECK(bo3_qos_keep_add(&k, 0x1000));
        CHECK(bo3_qos_keep_add(&k, 0x2000));
        CHECK(!bo3_qos_keep_take(&k, 0x3000));
        CHECK(!bo3_qos_keep_take(&k, 0));
        CHECK(bo3_qos_keep_take(&k, 0x1000));
        CHECK(!bo3_qos_keep_take(&k, 0x1000));
        CHECK(bo3_qos_keep_take(&k, 0x2000));
        /* a lookup-and-release cycle many times over stays inside the table */
        for (i = 0; i < 10000; i++) {
            CHECK(bo3_qos_keep_add(&k, 0x5000u + 0x40u * (unsigned)(i % 3)));
            CHECK(bo3_qos_keep_take(&k, 0x5000u + 0x40u * (unsigned)(i % 3)));
        }
        for (i = 0; i < BO3_QOS_KEEP; i++)
            CHECK(bo3_qos_keep_add(&k, 0x10000u + 0x40u * (unsigned)i));
        CHECK(!bo3_qos_keep_add(&k, 0x90000));      /* full */
        CHECK(bo3_qos_keep_take(&k, 0x10000));
        CHECK(bo3_qos_keep_add(&k, 0x90000));       /* a free slot again */
        /* no block: an error, never success */
        CHECK(bo3_qos_lookup_ret(0) == 10055u);
        CHECK(bo3_qos_lookup_ret(0x1000) == 0);
    }
    printf("peerqos: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
