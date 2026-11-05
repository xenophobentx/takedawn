/* SELECT GAMERTAG as the title builds it (rows CREATE NEW ACCOUNT, MANAGE
 * ACCOUNTS, then the gamertags), and what nodash.c makes of it. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "nodash.h"

static int s_fail;

#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); s_fail++; } } while (0)

static uint8_t page[0x400];

static uint16_t r16(uint32_t o) { return (uint16_t)(page[o] | page[o + 1] << 8); }
static uint32_t r32(uint32_t o) { return (uint32_t)page[o] | (uint32_t)page[o + 1] << 8 | (uint32_t)page[o + 2] << 16 | (uint32_t)page[o + 3] << 24; }
static void w16(uint32_t o, uint16_t v) { page[o] = (uint8_t)v; page[o + 1] = (uint8_t)(v >> 8); }
static void w32(uint32_t o, uint32_t v) { int i; for (i = 0; i < 4; i++) page[o + i] = (uint8_t)(v >> (8 * i)); }

/* the title's list (sub_000B3340 / sub_000B34F0): n gamertags, the cursor on the first */
static void title_list(uint32_t n)
{
    uint32_t i;
    memset(page, 0xCC, sizeof page);
    w32(NODASH_USERS, n);
    if (n) {
        w16(NODASH_CURSOR, 2);
        w16(NODASH_COUNT, (uint16_t)(n + 2));
        for (i = 0; i < n; i++) {
            w32(NODASH_ITEMS + 16 + i * 8, 0x1000u + i);   /* the gamertag's text */
            page[NODASH_ITEMS + 16 + i * 8 + 4] = 0;
        }
    } else {
        w16(NODASH_CURSOR, 0);
        w16(NODASH_COUNT, 1);
    }
    w32(NODASH_ITEMS + 0, 0xAAAAu);   /* CREATE NEW ACCOUNT */
    page[NODASH_ITEMS + 4] = 0;
    w32(NODASH_ITEMS + 8, 0xBBBBu);   /* MANAGE ACCOUNTS */
    page[NODASH_ITEMS + 12] = 0;
}

/* the title's A (0xB36DC ..): row 0 / 1 the dashboard, else gamertag cursor-2 */
static int title_a(int *gamertag)
{
    uint16_t cur;
    bo3_nodash_pick(page);
    cur = r16(NODASH_CURSOR);
    int dash = cur == 0 || cur == 1;
    *gamertag = dash ? -1 : (int)cur - 2;
    return dash;
}

/* the list built again (the title puts its cursor on the first gamertag) */
static void rebuild(uint32_t n)
{
    title_list(n);
    bo3_nodash_list(page);
}

static void one_gamertag(void)
{
    int g;
    title_list(1);
    CHECK(!bo3_nodash_hidden(page));
    CHECK(bo3_nodash_list(page) == 1);
    CHECK(bo3_nodash_hidden(page));
    CHECK(r16(NODASH_COUNT) == 1 && r16(NODASH_CURSOR) == 0);
    CHECK(r32(NODASH_ITEMS) == 0x1000u);               /* row 0 is the gamertag */
    CHECK(r32(NODASH_ITEMS + 8) == 0 && r32(NODASH_ITEMS + 12) == 0);
    CHECK(bo3_nodash_list(page) == 0);                 /* not twice */
    CHECK(r32(NODASH_ITEMS) == 0x1000u);
    /* up: the title's own `cursor != 0` keeps it on row 0 */
    CHECK(!title_a(&g) && g == 0);
    /* the page leaves with the title's cursor: sub_0006E1B0 reads it, 0 would be
     * the account row (EA's CREATE NEW ACCOUNT), a gamertag row CONNECTING */
    CHECK(r16(NODASH_CURSOR) == 2);
    CHECK(!bo3_nodash_hidden(page) && bo3_nodash_pick(page) == 0);   /* no second move */
}

static void three_gamertags(void)
{
    int g;
    title_list(3);
    CHECK(bo3_nodash_list(page) == 1);
    CHECK(r16(NODASH_COUNT) == 3 && r16(NODASH_CURSOR) == 0);
    CHECK(r32(NODASH_ITEMS + 0) == 0x1000u && r32(NODASH_ITEMS + 8) == 0x1001u && r32(NODASH_ITEMS + 16) == 0x1002u);
    w16(NODASH_CURSOR, 2);                             /* down, down (the title stops at count-1) */
    CHECK(!title_a(&g) && g == 2);
    CHECK(r16(NODASH_CURSOR) == 4);
    rebuild(3);
    CHECK(!title_a(&g) && g == 0);
    rebuild(3);
    w16(NODASH_CURSOR, 1);
    CHECK(!title_a(&g) && g == 1);
}

static void cursor_kept(void)
{
    title_list(2);
    w16(NODASH_CURSOR, 3);                             /* on the second gamertag when rebuilt */
    CHECK(bo3_nodash_list(page) == 1);
    CHECK(r16(NODASH_CURSOR) == 1);
    title_list(2);
    w16(NODASH_CURSOR, 0);                             /* on CREATE NEW ACCOUNT: the first gamertag */
    CHECK(bo3_nodash_list(page) == 1);
    CHECK(r16(NODASH_CURSOR) == 0);
}

static void not_the_title_list(void)
{
    int g;
    title_list(0);                                     /* CREATE NEW ACCOUNT alone: left as it is */
    CHECK(bo3_nodash_list(page) == 0);
    CHECK(!bo3_nodash_hidden(page) && bo3_nodash_pick(page) == 0);
    title_list(1);                                     /* the title's list, A unchanged */
    w16(NODASH_CURSOR, 2);
    CHECK(!title_a(&g) && g == 0);
    w16(NODASH_CURSOR, 1);
    CHECK(title_a(&g));
    title_list(1);
    w32(NODASH_USERS, 0x7FFFFFFFu);                    /* garbage count */
    CHECK(bo3_nodash_list(page) == 0);
}

static void answers(void)
{
    CHECK(bo3_nodash_answer(NODASH_ANSWER_YES_NTSC, NODASH_ANSWER_YES_NTSC) == 5);
    CHECK(bo3_nodash_answer(5, NODASH_ANSWER_YES_NTSC) == 5);
    CHECK(bo3_nodash_answer(0, NODASH_ANSWER_YES_NTSC) == 0);
    CHECK(bo3_nodash_answer(NODASH_ANSWER_YES_PAL, NODASH_ANSWER_YES_PAL) == 6);
    CHECK(bo3_nodash_answer(4, NODASH_ANSWER_YES_PAL) == 4);
    CHECK(bo3_nodash_mu_answer(0, 4) == 1);
    CHECK(bo3_nodash_mu_answer(0, 3) == 0);            /* step 3's YES leads to step 4, kept */
    CHECK(bo3_nodash_mu_answer(1, 4) == 1);
}

int main(void)
{
    one_gamertag();
    three_gamertags();
    cursor_kept();
    not_the_title_list();
    answers();
    if (s_fail) {
        printf("%d failed\n", s_fail);
        return 1;
    }
    printf("bo3_nodash: all passed\n");
    return 0;
}
