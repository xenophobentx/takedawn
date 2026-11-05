/* The online navigator's pure decisions (onlinenav.c, BO-352 step 2): the
 * intent switch and the Quick Play auto-start countdown. */
#include <stdio.h>
#include <string.h>

#include "onlinenav.h"

static int s_fail;

#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); s_fail++; } } while (0)

static void intent(void)
{
    int it = 0;
    char code[6];

    CHECK(bo3_onlinenav_parse_intent("quick", &it, NULL, 0) && it == NAV_QUICK);
    CHECK(bo3_onlinenav_parse_intent("create", &it, NULL, 0) && it == NAV_CREATE);
    CHECK(bo3_onlinenav_parse_intent("join", &it, NULL, 0) && it == NAV_JOIN);

    CHECK(bo3_onlinenav_parse_intent("join=ABC12", &it, code, sizeof code) && it == NAV_JOIN
          && !strcmp(code, "ABC12"));
    /* lower case is upper-cased, spaces are dropped */
    CHECK(bo3_onlinenav_parse_intent("join= ab3 x", &it, code, sizeof code) && !strcmp(code, "AB3X"));
    /* create takes no code */
    CHECK(bo3_onlinenav_parse_intent("create=XYZ", &it, code, sizeof code) && it == NAV_CREATE
          && code[0] == 0);

    CHECK(!bo3_onlinenav_parse_intent("", &it, NULL, 0));
    CHECK(!bo3_onlinenav_parse_intent(NULL, &it, NULL, 0));
    CHECK(!bo3_onlinenav_parse_intent("bogus", &it, NULL, 0));
}

static nav_quick_in base(void)
{
    nav_quick_in q;
    memset(&q, 0, sizeof q);
    q.np = 1;
    q.ready[0] = 1;             /* the host, a car chosen */
    q.me = "Host";
    q.maxsize = 6;
    q.now = 1000000;
    q.second_ready_at = 0;
    return q;
}

static void quick(void)
{
    char why[96];
    int seconds = 0;
    nav_quick_in q;

    /* the host alone: wait for the second player */
    q = base(); CHECK(bo3_onlinenav_quick_start(&q, why, sizeof why, &seconds) == NAV_QS_WAIT);

    /* the second player just readied: the 30 s countdown begins (the caller
     * starts the timer by setting second_ready_at) */
    q = base(); q.np = 2; q.ready[1] = 1;
    CHECK(bo3_onlinenav_quick_start(&q, why, sizeof why, &seconds) == NAV_QS_COUNTDOWN && seconds == 30);

    /* the timer starts: 30 s */
    q = base(); q.np = 2; q.ready[1] = 1; q.second_ready_at = q.now;
    CHECK(bo3_onlinenav_quick_start(&q, why, sizeof why, &seconds) == NAV_QS_COUNTDOWN && seconds == 30);

    /* 15 s in */
    q = base(); q.np = 2; q.ready[1] = 3; q.second_ready_at = q.now - 15000;
    CHECK(bo3_onlinenav_quick_start(&q, why, sizeof why, &seconds) == NAV_QS_COUNTDOWN && seconds == 15);

    /* 30 s passed: start */
    q = base(); q.np = 2; q.ready[1] = 1; q.second_ready_at = q.now - 30000;
    CHECK(bo3_onlinenav_quick_start(&q, why, sizeof why, &seconds) == NAV_QS_START);

    /* full lobby (6/6): start at once, whatever the timer */
    q = base(); q.np = 6;
    q.ready[1] = 1; q.ready[2] = 1; q.ready[3] = 3; q.ready[4] = 1; q.ready[5] = 3;
    q.second_ready_at = 0;
    CHECK(bo3_onlinenav_quick_start(&q, why, sizeof why, &seconds) == NAV_QS_START);

    /* a guest without a car (0) does not count, so not full: the countdown */
    q = base(); q.np = 6; q.ready[1] = 1; q.ready[2] = 0; q.second_ready_at = q.now;
    CHECK(bo3_onlinenav_quick_start(&q, why, sizeof why, &seconds) == NAV_QS_COUNTDOWN && seconds == 30);

    /* a guest with a car (1 or 3) counts */
    q = base(); q.np = 2; q.ready[1] = 0;
    CHECK(bo3_onlinenav_quick_start(&q, why, sizeof why, &seconds) == NAV_QS_WAIT);
}

static void text(void)
{
    char t[40];
    bo3_onlinenav_countdown_text(t, sizeof t, 5);
    CHECK(!strcmp(t, "STARTING IN 5 S"));
    bo3_onlinenav_countdown_text(t, sizeof t, 0);
    CHECK(!strcmp(t, "STARTING IN 1 S"));
}

static void code(void)
{
    char c[6];

    /* the alphabet: A-Z then 0-9; lower case folds */
    CHECK(bo3_onlinenav_code_norm('A') == 0 && bo3_onlinenav_code_norm('Z') == 25);
    CHECK(bo3_onlinenav_code_norm('0') == 26 && bo3_onlinenav_code_norm('9') == 35);
    CHECK(bo3_onlinenav_code_norm('a') == 0 && bo3_onlinenav_code_norm('z') == 25);
    CHECK(bo3_onlinenav_code_norm('?') == -1 && bo3_onlinenav_code_norm(' ') == -1);

    /* stepping wraps at both ends and a blank starts at 'A' */
    strcpy(c, "AAAAA"); bo3_onlinenav_code_step(c, 0, 1);  CHECK(!strcmp(c, "BAAAA"));
    strcpy(c, "BAAAA"); bo3_onlinenav_code_step(c, 0, -1); CHECK(!strcmp(c, "AAAAA"));
    strcpy(c, "ZAAAA"); bo3_onlinenav_code_step(c, 0, 1);  CHECK(!strcmp(c, "0AAAA"));
    strcpy(c, "9AAAA"); bo3_onlinenav_code_step(c, 0, 1);  CHECK(!strcmp(c, "AAAAA"));
    strcpy(c, "AAAAA"); bo3_onlinenav_code_step(c, 0, -1); CHECK(!strcmp(c, "9AAAA"));
    strcpy(c, " AAAA"); bo3_onlinenav_code_step(c, 0, 1);  CHECK(!strcmp(c, "AAAAA"));

    /* complete only when every character is in the alphabet */
    CHECK(bo3_onlinenav_code_complete("AAAAA"));
    CHECK(bo3_onlinenav_code_complete("K7MXP"));
    CHECK(!bo3_onlinenav_code_complete("K7M P"));
    CHECK(!bo3_onlinenav_code_complete("A    "));
}

static void cancel_latch(void)
{
    int held = 1;
    CHECK(bo3_onlinenav_cancel_latch(&held, 1) && held);
    CHECK(bo3_onlinenav_cancel_latch(&held, 1) && held);
    CHECK(!bo3_onlinenav_cancel_latch(&held, 0) && !held);
    CHECK(!bo3_onlinenav_cancel_latch(&held, 1));
}

int main(void)
{
    intent();
    cancel_latch();
    quick();
    text();
    code();
    if (s_fail) {
        printf("%d failed\n", s_fail);
        return 1;
    }
    printf("onlinenav: all passed\n");
    return 0;
}
