/* The online navigator's table, mode list and start gate (onlinebot.c). */
#include <stdio.h>
#include <string.h>

#include "onlinebot.h"
#include "codeentry.h"
#include "onlinescreens.h"

static int s_fail;

#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); s_fail++; } } while (0)

static void screens(void)
{
    /* states named by their page; one state, several pages */
    CHECK(bo3_online_screen_for(0x0C0, 0x00559390u) == OB_SC_MAIN);
    CHECK(bo3_online_screen_for(0x630, 0x0055FBDCu) == OB_SC_GAMERTAG);
    CHECK(bo3_online_screen_for(0x630, 0x00561100u) == OB_SC_LOBBYSEL);
    CHECK(bo3_online_screen_for(0x630, 0x00565060u) == OB_SC_RANKINGS);
    CHECK(bo3_online_screen_for(0x5B0, 0x00561100u) == OB_SC_LOBBYSEL);
    CHECK(bo3_online_screen_for(0x5B0, 0x0055DF50u) == OB_SC_LIVE);
    CHECK(bo3_online_screen_for(0x5D0, 0x00564900u) == OB_SC_MODE);
    CHECK(bo3_online_screen_for(0x590, 0x00559648u) == OB_SC_CAR);
    CHECK(bo3_online_screen_for(0x590, 0x0055CBA0u) == OB_SC_LOBBY);
    CHECK(bo3_online_screen_for(0x178, 0x00559FA8u) == OB_SC_RESULTS);
    /* any page of a waiting state */
    CHECK(bo3_online_screen_for(0x018, 0x12345678u) == OB_SC_LOADING);
    CHECK(bo3_online_screen_for(0x260, 0x0055CB70u) == OB_SC_POINTS);
    /* a page the table does not know in a state it knows by page: unknown */
    CHECK(bo3_online_screen_for(0x590, 0x00561100u) == OB_SC_UNKNOWN);
    CHECK(bo3_online_screen_for(0x999, 0) == OB_SC_UNKNOWN);
    CHECK(!strcmp(bo3_onlinebot_screen_name(OB_SC_LOBBY), "game-lobby"));
    CHECK(!strcmp(bo3_onlinebot_screen_name(OB_SC_N), "?"));
}

/* Every screen with a page has three distinct addresses (one per disc), and
 * the same (flow, page) names the same screen on each disc (BO-352 step 1). */
static void regions(void)
{
    int i, r;
    for (i = 0; i < bo3_online_screens_n; i++) {
        const bo3_online_screen *s = &bo3_online_screens[i];
        if (!s->ntsc) {                     /* "any page of that state" */
            CHECK(s->pal == 0 && s->ensv == 0);
            continue;
        }
        CHECK(s->pal != 0 && s->ensv != 0);
        CHECK(s->ntsc != s->pal && s->ntsc != s->ensv && s->pal != s->ensv);
        /* each disc's page resolves to the same screen */
        for (r = 0; r < 3; r++) {
            uint32_t p = bo3_online_screen_page_for(s, r);
            CHECK(bo3_online_screen_for_region(s->flow, p, r) == s->sc);
        }
    }
    /* spot checks on the PAL and PAL-ENSV twins of the main menu and lobby */
    CHECK(bo3_online_screen_for_region(0x0C0, 0x005596B0u, 1) == OB_SC_MAIN);
    CHECK(bo3_online_screen_for_region(0x0C0, 0x00559750u, 2) == OB_SC_MAIN);
    CHECK(bo3_online_screen_for_region(0x590, 0x0055CEC0u, 1) == OB_SC_LOBBY);
    CHECK(bo3_online_screen_for_region(0x590, 0x0055CF60u, 2) == OB_SC_LOBBY);
}

static void modes(void)
{
    int m[16], n;
    n = bo3_onlinebot_parse_modes("0,roadrage, partycrash/1", m, 16);
    CHECK(n == 4 && m[0] == 0 && m[1] == 1 && m[2] == 4 && m[3] == 1);
    n = bo3_onlinebot_parse_modes("race rr pc", m, 16);
    CHECK(n == 3 && m[0] == 0 && m[1] == 1 && m[2] == 4);
    CHECK(bo3_onlinebot_parse_modes("", m, 16) == 0);
    CHECK(bo3_onlinebot_parse_modes(NULL, m, 16) == 0);
    CHECK(bo3_onlinebot_parse_modes("0,0,0", m, 2) == 2);
}

static ob_gate_in base(void)
{
    ob_gate_in g;
    memset(&g, 0, sizeof g);
    g.lstate = 0x01;
    g.linked = 1;
    g.alive = 1;
    g.np = 3;
    g.links[0] = 0xFF;          /* ourselves */
    g.links[1] = 3;
    g.links[2] = 3;
    g.players = 3;
    strcpy(g.name[0], "Host");
    strcpy(g.name[1], "G1");
    strcpy(g.name[2], "G2");
    g.ready[0] = 1;
    g.ready[1] = 1;
    g.ready[2] = 3;
    g.me = "Host";
    g.want = 2;
    return g;
}

static void gate(void)
{
    char why[96];
    ob_gate_in g = base();
    CHECK(bo3_onlinebot_gate(&g, why, sizeof why));
    /* 1 and 3 both let Start through; 0 (no car) does not */
    g = base(); g.ready[1] = 3; CHECK(bo3_onlinebot_gate(&g, why, sizeof why));
    g = base(); g.ready[2] = 0; CHECK(!bo3_onlinebot_gate(&g, why, sizeof why) && strstr(why, "car"));
    /* the host's own value does not count */
    g = base(); g.ready[0] = 0; CHECK(bo3_onlinebot_gate(&g, why, sizeof why));
    /* fewer guests than wanted, then wanting fewer */
    g = base(); g.players = 2; g.np = 2; CHECK(!bo3_onlinebot_gate(&g, why, sizeof why) && strstr(why, "1 of 2"));
    g.want = 1; CHECK(bo3_onlinebot_gate(&g, why, sizeof why));
    /* a link not up, the screen's flags, a dialog */
    g = base(); g.links[2] = 2; CHECK(!bo3_onlinebot_gate(&g, why, sizeof why) && strstr(why, "link 2"));
    g = base(); g.linked = 0; CHECK(!bo3_onlinebot_gate(&g, why, sizeof why));
    g = base(); g.alive = 0; CHECK(!bo3_onlinebot_gate(&g, why, sizeof why) && strstr(why, "alive"));
    g = base(); g.lstate = 0x09; CHECK(!bo3_onlinebot_gate(&g, why, sizeof why) && strstr(why, "0x09"));
    /* a guest in the record the title has not taken in yet */
    g = base(); g.np = 2; CHECK(!bo3_onlinebot_gate(&g, why, sizeof why) && strstr(why, "title's table"));
}

static void plans(void)
{
    int p[8], n;
    n = bo3_onlinebot_parse_plans("quick,create, code/browse", p, 8);
    CHECK(n == 4 && p[0] == OB_PLAN_QUICK && p[1] == OB_PLAN_CREATE && p[2] == OB_PLAN_CODE && p[3] == OB_PLAN_BROWSE);
    CHECK(bo3_onlinebot_parse_plans("nonsense,quick", p, 8) == 1 && p[0] == OB_PLAN_QUICK);
    CHECK(bo3_onlinebot_parse_plans("", p, 8) == 0);
    CHECK(bo3_onlinebot_parse_plans(NULL, p, 8) == 0);
    CHECK(bo3_onlinebot_parse_plans("code,code,code", p, 2) == 2);
    CHECK(!strcmp(bo3_onlinebot_plan_name(OB_PLAN_BROWSE), "browse"));
    CHECK(!strcmp(bo3_onlinebot_plan_name(OB_PLAN_N), "?"));
    /* BO-365: wait is its own plan on the QUICK PLAY line */
    CHECK(bo3_onlinebot_parse_plans("wait,quick", p, 8) == 2 && p[0] == OB_PLAN_WAIT && p[1] == OB_PLAN_QUICK);
    CHECK(!strcmp(bo3_onlinebot_plan_name(OB_PLAN_WAIT), "wait"));
    CHECK(bo3_onlinebot_plan_line(OB_PLAN_WAIT) == bo3_onlinebot_plan_line(OB_PLAN_QUICK) &&
          bo3_onlinebot_plan_line(OB_PLAN_QUICK) == 0 && bo3_onlinebot_plan_line(OB_PLAN_CREATE) == 1 &&
          bo3_onlinebot_plan_line(OB_PLAN_CODE) == 2 && bo3_onlinebot_plan_line(OB_PLAN_BROWSE) == 3);
}

/* how a wait for a race ended (BO-365) */
static void waits(void)
{
    CHECK(bo3_onlinebot_wait_result(3, 90000, OB_SC_CAR, 0) == OB_WAIT_JOINED);
    CHECK(bo3_onlinebot_wait_result(2, 90000, OB_SC_LOBBY, 0) == OB_WAIT_JOINED);
    CHECK(bo3_onlinebot_wait_result(5, 80000, OB_SC_CREATING, 0) == OB_WAIT_GONE);
    CHECK(bo3_onlinebot_wait_result(4, 80000, OB_SC_LIVE, 0) == OB_WAIT_FULL);
    CHECK(bo3_onlinebot_wait_result(1, OB_WAIT_MAX_MS, OB_SC_LIVE, 0) == OB_WAIT_TIMEOUT);
    CHECK(bo3_onlinebot_wait_result(1, 30000, OB_SC_LIVE, 1) == OB_WAIT_CANCELLED);
    CHECK(bo3_onlinebot_wait_result(1, 30000, OB_SC_LIVE, 0) == OB_WAIT_OTHER);
    CHECK(!strcmp(bo3_onlinebot_wait_result_name(OB_WAIT_JOINED), "joined") &&
          !strcmp(bo3_onlinebot_wait_result_name(OB_WAIT_CANCELLED), "cancelled") &&
          !strcmp(bo3_onlinebot_wait_result_name(OB_WAIT_OTHER), "other") &&
          !strcmp(bo3_onlinebot_wait_result_name(99), "?"));
    CHECK(!bo3_onlinebot_wait_settled(OB_SC_LOADING) && !bo3_onlinebot_wait_settled(OB_SC_UNKNOWN) &&
          !bo3_onlinebot_wait_settled(OB_SC_QUICK) && bo3_onlinebot_wait_settled(OB_SC_CAR) &&
          bo3_onlinebot_wait_settled(OB_SC_LIVE));
    CHECK(!strcmp(bo3_onlinebot_wait_state_name(1), "racing") && !strcmp(bo3_onlinebot_wait_state_name(5), "gone") &&
          !strcmp(bo3_onlinebot_wait_state_name(0), "none") && !strcmp(bo3_onlinebot_wait_state_name(9), "?"));
    /* the Quick Match "stuck" limit: a minute, or the wait's cap and a minute */
    CHECK(bo3_onlinebot_quick_limit_ms(0) == 60000ull && bo3_onlinebot_quick_limit_ms(1) == 540000ull);
}

/* the steps the navigator takes, run on the entry's own model */
static void entry(void)
{
    static const char *const codes[] = { "TKDW3", "AAAAA", "99999", "HA9B2" };
    size_t c;
    bo3_ce e;
    int row, steps;
    /* a wrong letter goes first */
    CHECK(bo3_onlinebot_entry_step("TKDW3", "TX", 1, 0) == OB_CE_DELETE);
    CHECK(bo3_onlinebot_entry_step("TKDW3", "TKDW3X", 0, 0) == OB_CE_DELETE);
    /* the shorter way round the columns */
    CHECK(bo3_onlinebot_entry_step("H", "", 1, 0) == OB_CE_LEFT);
    CHECK(bo3_onlinebot_entry_step("B", "", 1, 0) == OB_CE_RIGHT);
    for (c = 0; c < sizeof codes / sizeof codes[0]; c++) {
        bo3_ce_reset(&e, NULL);
        row = CE_ROW_GRID;
        for (steps = 0; steps < 100; steps++) {
            int a = bo3_onlinebot_entry_step(codes[c], e.code, row, e.col);
            if (a == OB_CE_JOIN)
                break;
            switch (a) {
            case OB_CE_UP: row--; break;
            case OB_CE_DOWN: row++; break;
            case OB_CE_LEFT: bo3_ce_press(&e, CE_BTN_LEFT, row); break;
            case OB_CE_RIGHT: bo3_ce_press(&e, CE_BTN_RIGHT, row); break;
            case OB_CE_TYPE:
                CHECK(bo3_ce_press(&e, CE_BTN_A, row) == CE_ACT_TYPE);
                if (bo3_ce_complete(&e))
                    row = CE_ROW_CODE;      /* as xboxlive.c moves it */
                break;
            default: CHECK(0); break;
            }
        }
        CHECK(steps < 100 && !strcmp(e.code, codes[c]));
    }
}

/* soak052 (BO-365): RACES counts a race that ended out of the game, and a
 * step's attempts start over after a race (the fourth race's results were
 * "stuck" before any press) */
static void races_and_steps(void)
{
    CHECK(!bo3_onlinebot_races_done(0, 5));
    CHECK(!bo3_onlinebot_races_done(2, 1));
    CHECK(bo3_onlinebot_races_done(1, 1));
    CHECK(bo3_onlinebot_races_done(1, 2));
    CHECK(bo3_onlinebot_step_fresh(OB_SC_RACE, OB_SC_RACE_END));
    CHECK(bo3_onlinebot_step_fresh(OB_SC_RACE, OB_SC_RESULTS));
    CHECK(bo3_onlinebot_step_fresh(OB_SC_RACE, OB_SC_LOADING));
    CHECK(!bo3_onlinebot_step_fresh(OB_SC_RACE_END, OB_SC_RESULTS));
    CHECK(!bo3_onlinebot_step_fresh(OB_SC_RESULTS, OB_SC_RACE));
    CHECK(!bo3_onlinebot_step_fresh(OB_SC_LOBBY, OB_SC_LIVE));
}

int main(void)
{
    screens();
    regions();
    modes();
    gate();
    plans();
    waits();
    races_and_steps();
    entry();
    if (s_fail) {
        printf("%d failed\n", s_fail);
        return 1;
    }
    printf("onlinebot: all passed\n");
    return 0;
}
