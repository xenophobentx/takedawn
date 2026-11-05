/* The online info line's parser and text (onlineinfo.c, BO-352 step 8). */
#include <stdio.h>
#include <string.h>

#include "onlineinfo.h"

static int s_fail;

#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); s_fail++; } } while (0)

static void parse(void)
{
    int on = -1, wa = -1, v = 0;

    CHECK(bo3_onlineinfo_parse_status("ADDR=1.2.3.4\nPORT=21810\nSESS=1\nTKONLINE=14\nTKWAITING=3\n",
                                      &on, &wa) == 3 && on == 14 && wa == 3);
    /* an old server: no counts */
    on = wa = -1;
    CHECK(bo3_onlineinfo_parse_status("ADDR=1.2.3.4\nPORT=21810\n", &on, &wa) == 0 && on == -1 && wa == -1);
    /* only one present */
    on = wa = -1;
    CHECK(bo3_onlineinfo_parse_status("TKONLINE=5\n", &on, &wa) == 1 && on == 5 && wa == -1);
    /* the field helper */
    CHECK(bo3_onlineinfo_field_int("TKWAITING=7\n", "TKWAITING", &v) && v == 7);
    CHECK(!bo3_onlineinfo_field_int("TKONLINE=14\n", "TKWAITING", &v));
    CHECK(!bo3_onlineinfo_field_int("TKONLINE14\n", "TKONLINE", &v));
}

static void menus(void)
{
    char c[80], t[120];
    /* the internet lobby with its counts */
    bo3_onlineinfo_counts(c, sizeof c, 1, 1, 12, 2);
    CHECK(!strcmp(c, "12 PLAYERS ONLINE - 2 GAMES OPEN"));
    bo3_onlineinfo_counts(c, sizeof c, 1, 1, 1, 1);
    CHECK(!strcmp(c, "1 PLAYER ONLINE - 1 GAME OPEN"));
    bo3_onlineinfo_counts(c, sizeof c, 1, 1, 3, -1);
    CHECK(!strcmp(c, "3 PLAYERS ONLINE - 0 GAMES OPEN"));
    /* BO-365: the races a stranger could wait for */
    bo3_onlineinfo_counts_racing(c, sizeof c, 1, 1, 5, 0, 1);
    CHECK(!strcmp(c, "5 PLAYERS ONLINE - 0 GAMES OPEN - 1 RACING"));
    bo3_onlineinfo_counts_racing(c, sizeof c, 1, 1, 5, 1, 0);
    CHECK(!strcmp(c, "5 PLAYERS ONLINE - 1 GAME OPEN"));
    bo3_onlineinfo_counts_racing(c, sizeof c, 1, 1, 5, 1, -1);          /* an old server */
    CHECK(!strcmp(c, "5 PLAYERS ONLINE - 1 GAME OPEN"));
    bo3_onlineinfo_counts_racing(c, sizeof c, 2, 1, 5, 1, 3);           /* Local network: no counts */
    CHECK(!strcmp(c, "LOCAL NETWORK"));
    CHECK(!strcmp(bo3_onlineinfo_browse_hint("EVERY GAME: OPEN, RACING, FULL", 1, 0, 2), "RACING NOW: PICK ONE TO JOIN AFTER ITS RACE"));
    CHECK(!strcmp(bo3_onlineinfo_browse_hint("EVERY GAME: OPEN, RACING, FULL", 1, 1, 2), "EVERY GAME: OPEN, RACING, FULL"));
    CHECK(!strcmp(bo3_onlineinfo_browse_hint("EVERY GAME: OPEN, RACING, FULL", 0, 0, 2), "EVERY GAME: OPEN, RACING, FULL"));
    bo3_onlineinfo_counts_racing(c, sizeof c, 1, 1, 12, 0, 1);
    bo3_onlineinfo_page_line(t, sizeof t, c, bo3_onlineinfo_browse_hint("EVERY GAME: OPEN, RACING, FULL", 1, 0, 1));
    CHECK(strlen(t) < 96);                                              /* the line's buffer (onlinedesc.c OD_UNITS) */
    bo3_onlineinfo_counts(c, sizeof c, 1, 1, 3, -1);
    bo3_onlineinfo_menu_line(t, sizeof t, c);
    CHECK(!strcmp(t, "RACE ONLINE - 3 PLAYERS ONLINE - 0 GAMES OPEN"));
    /* no server answered (or an old one): no counts, just RACE ONLINE */
    bo3_onlineinfo_counts(c, sizeof c, 1, 0, 12, 2);
    CHECK(c[0] == 0);
    bo3_onlineinfo_menu_line(t, sizeof t, c);
    CHECK(!strcmp(t, "RACE ONLINE"));
    /* the local network: never counts */
    bo3_onlineinfo_counts(c, sizeof c, 2, 1, 12, 2);
    CHECK(!strcmp(c, "LOCAL NETWORK"));
    bo3_onlineinfo_menu_line(t, sizeof t, c);
    CHECK(!strcmp(t, "RACE ONLINE - LOCAL NETWORK"));
    /* online off: nothing */
    bo3_onlineinfo_counts(c, sizeof c, 0, 1, 12, 2);
    CHECK(c[0] == 0);
    /* the online page: the counts, then the line's hint; the hint alone */
    bo3_onlineinfo_page_line(t, sizeof t, "12 PLAYERS ONLINE - 2 GAMES OPEN", "ONE PRESS AND YOU RACE");
    CHECK(!strcmp(t, "12 PLAYERS ONLINE - 2 GAMES OPEN - ONE PRESS AND YOU RACE"));
    bo3_onlineinfo_page_line(t, sizeof t, "", "ONE PRESS AND YOU RACE");
    CHECK(!strcmp(t, "ONE PRESS AND YOU RACE"));
}

static void text(void)
{
    char t[80];

    bo3_onlineinfo_text(t, sizeof t, 14, 3, 38);
    CHECK(!strcmp(t, "14 ONLINE - 3 OPEN GAMES - SERVER 38 MS"));
    bo3_onlineinfo_text(t, sizeof t, 14, 3, -1);
    CHECK(!strcmp(t, "14 ONLINE - 3 OPEN GAMES"));
    /* an old server, or no probe yet: no numbers */
    bo3_onlineinfo_text(t, sizeof t, -1, -1, -1);
    CHECK(!strcmp(t, "ONLINE"));
}

int main(void)
{
    parse();
    text();
    menus();
    if (s_fail) {
        printf("%d failed\n", s_fail);
        return 1;
    }
    printf("onlineinfo: all passed\n");
    return 0;
}
