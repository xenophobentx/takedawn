/* startwatch test: see CMakeLists.txt. */
#include <stdio.h>
#include <string.h>

#include "startwatch.h"

static int fails, checks;

#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

#define SCR 0x00700000u

/* every copy that starts normally: 0xB for one frame, then the race */
static void test_normal_start(void)
{
    bo3_startwatch w;
    uint64_t t;
    memset(&w, 0, sizeof w);
    for (t = 0; t < 5000; t += 16)
        CHECK(bo3_startwatch_step(&w, SCR, 0x01, t) == BO3_SW_NONE);
    CHECK(bo3_startwatch_step(&w, SCR, 0x0B, 5000) == BO3_SW_NONE);
    /* the screen goes (the race loads): nothing more, however long */
    for (t = 5016; t < 60000; t += 16)
        CHECK(bo3_startwatch_step(&w, 0, 0, t) == BO3_SW_NONE);
}

/* soak034 game 6: a guest stays in 0xB. One report after 2 s, one leave
 * after 10 s, nothing else */
static void test_stuck_guest(void)
{
    bo3_startwatch w;
    uint64_t t;
    int reports = 0, leaves = 0, first_report = -1, first_leave = -1;
    memset(&w, 0, sizeof w);
    /* from time 0, so the clock's zero is a real moment too */
    for (t = 0; t < 60000; t += 16) {
        int r = bo3_startwatch_step(&w, SCR, 0x0B, t);
        if (r == BO3_SW_REPORT && reports++ == 0)
            first_report = (int)t;
        if (r == BO3_SW_LEAVE && leaves++ == 0)
            first_leave = (int)t;
    }
    CHECK(reports == 1 && first_report >= 2000 && first_report < 2100);
    CHECK(leaves == 1 && first_leave >= 10000 && first_leave < 10100);
}

/* a flicker out of 0xB, or another screen object, starts over */
static void test_restart(void)
{
    bo3_startwatch w;
    uint64_t t;
    int leaves = 0;
    memset(&w, 0, sizeof w);
    for (t = 1000; t < 9000; t += 16)
        CHECK(bo3_startwatch_step(&w, SCR, 0x0B, t) != BO3_SW_LEAVE);
    CHECK(bo3_startwatch_step(&w, SCR, 0x0C, 9000) == BO3_SW_NONE);
    for (t = 9016; t < 18000; t += 16)
        CHECK(bo3_startwatch_step(&w, SCR, 0x0B, t) != BO3_SW_LEAVE);
    /* a new lobby screen (the next game) already in 0xB: its own 10 s */
    for (t = 18000; t < 27900; t += 16)
        CHECK(bo3_startwatch_step(&w, SCR + 0x2000u, 0x0B, t) != BO3_SW_LEAVE);
    for (t = 27900; t < 29000; t += 16)
        leaves += bo3_startwatch_step(&w, SCR + 0x2000u, 0x0B, t) == BO3_SW_LEAVE;
    CHECK(leaves == 1);
}

int main(void)
{
    test_normal_start();
    test_stuck_guest();
    test_restart();
    printf("%d checks, %d failed\n", checks, fails);
    return fails != 0;
}
