/* "stay together" after an online race: the pure decisions (stay.c). */
#include <stdio.h>
#include <string.h>

#include "stay.h"

static int s_fail;

#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); s_fail++; } } while (0)

static bo3_stay_in lobby(int host, uint32_t sys, unsigned long long now)
{
    bo3_stay_in in;
    memset(&in, 0, sizeof in);
    in.flow = STAY_FLOW_LOBBY;
    in.prev = STAY_FLOW_LOBBY;
    in.record = 1;
    in.sysflags = sys;
    in.host = host;
    in.name = "HostS";
    in.now = now;
    return in;
}

static bo3_stay_in at(uint32_t prev, uint32_t flow, unsigned long long now)
{
    bo3_stay_in in;
    memset(&in, 0, sizeof in);
    in.prev = prev;
    in.flow = flow;
    in.now = now;
    return in;
}

/* the lobby, the start, the race: armed, with the role and the name */
static void race_cases(void)
{
    bo3_stay s;
    bo3_stay_in in;
    memset(&s, 0, sizeof s);
    in = lobby(1, 0x40140, 1000);
    CHECK(bo3_stay_step(&s, &in) == STAY_EV_LOBBY);
    CHECK(s.state == STAY_LOBBY && s.role == STAY_ROLE_HOST && !strcmp(s.name, "HostS") && s.usable);
    CHECK(bo3_stay_step(&s, &in) == STAY_EV_NONE);
    in = lobby(1, 0xC0140, 2000);           /* gsta: under way */
    CHECK(bo3_stay_step(&s, &in) == STAY_EV_NONE && s.started);
    in = at(STAY_FLOW_LOBBY, STAY_FLOW_LOAD, 2100);
    CHECK(bo3_stay_step(&s, &in) == STAY_EV_ARMED && s.state == STAY_ARMED && s.armed_at == 2100);
    /* the race, the results, the sign-in, the points: still armed */
    {
        static const uint32_t chain[] = { 0x30, 0x18, 0x1C8, 0x30, 0x178, 0x18, 0x4D8, 0x630, 0x260, 0x928 };
        unsigned k;
        for (k = 0; k < sizeof chain / sizeof chain[0]; k++) {
            in = at(k ? chain[k - 1] : STAY_FLOW_LOAD, chain[k], 3000 + k);
            CHECK(bo3_stay_step(&s, &in) == STAY_EV_NONE && s.state == STAY_ARMED);
        }
    }
    /* the room's event 3 after the points: the host makes the game again */
    CHECK(bo3_stay_room(&s, 3, 1) == STAY_ROOM_TITLE);     /* points first */
    CHECK(bo3_stay_room(&s, 5, 0) == STAY_ROOM_TITLE);     /* a page exit */
    CHECK(bo3_stay_room(&s, 3, 0) == STAY_ROOM_HOST);

    /* a guest */
    memset(&s, 0, sizeof s);
    in = lobby(0, 0xC0140, 1000);
    bo3_stay_step(&s, &in);
    in = at(STAY_FLOW_LOBBY, STAY_FLOW_LOAD, 1100);
    CHECK(bo3_stay_step(&s, &in) == STAY_EV_ARMED);
    CHECK(bo3_stay_room(&s, 3, 0) == STAY_ROOM_GUEST);
}

/* leaving the lobby is not a race; nothing is armed */
static void leave_cases(void)
{
    bo3_stay s;
    bo3_stay_in in;
    memset(&s, 0, sizeof s);
    in = lobby(0, 0x40140, 1000);
    bo3_stay_step(&s, &in);
    /* B, Yes: to the online page */
    in = at(STAY_FLOW_LOBBY, STAY_FLOW_ONLINE, 1100);
    CHECK(bo3_stay_step(&s, &in) == STAY_EV_LEFT && s.state == STAY_IDLE);
    CHECK(bo3_stay_room(&s, 3, 0) == STAY_ROOM_TITLE);
    /* a kick to the room */
    in = lobby(0, 0x40140, 1200);
    bo3_stay_step(&s, &in);
    in = at(STAY_FLOW_LOBBY, STAY_FLOW_ROOM, 1300);
    CHECK(bo3_stay_step(&s, &in) == STAY_EV_LEFT);
    CHECK(bo3_stay_room(&s, 3, 0) == STAY_ROOM_TITLE);
    /* the loading screen without a start (never seen under way): not armed */
    in = lobby(0, 0x40140, 1400);
    bo3_stay_step(&s, &in);
    in = at(STAY_FLOW_LOBBY, STAY_FLOW_LOAD, 1500);
    CHECK(bo3_stay_step(&s, &in) == STAY_EV_LEFT && s.state == STAY_IDLE);
    /* a game with a password is not made again */
    in = lobby(1, 0xC0140, 1600);
    in.password = 1;
    bo3_stay_step(&s, &in);
    CHECK(!s.usable);
    in = at(STAY_FLOW_LOBBY, STAY_FLOW_LOAD, 1700);
    CHECK(bo3_stay_step(&s, &in) == STAY_EV_LEFT && s.state == STAY_IDLE);
    /* a record with no name */
    in = lobby(1, 0xC0140, 1800);
    in.name = "";
    bo3_stay_step(&s, &in);
    in = at(STAY_FLOW_LOBBY, STAY_FLOW_LOAD, 1900);
    CHECK(bo3_stay_step(&s, &in) == STAY_EV_LEFT);
    /* "started" is per game: a new lobby starts clean */
    in = lobby(1, 0xC0140, 2000);
    bo3_stay_step(&s, &in);
    in = at(STAY_FLOW_LOBBY, STAY_FLOW_ONLINE, 2100);
    bo3_stay_step(&s, &in);
    in = lobby(1, 0x40140, 2200);
    bo3_stay_step(&s, &in);
    CHECK(!s.started);
}

/* after the race the player goes elsewhere: given up */
static void cancel_cases(void)
{
    static const uint32_t away[] = { STAY_FLOW_ONLINE, STAY_FLOW_MAIN, STAY_FLOW_LOBBY, STAY_FLOW_QUICK, STAY_FLOW_CREATE };
    unsigned k;
    for (k = 0; k < sizeof away / sizeof away[0]; k++) {
        bo3_stay s;
        bo3_stay_in in;
        memset(&s, 0, sizeof s);
        in = lobby(0, 0xC0140, 1000);
        bo3_stay_step(&s, &in);
        in = at(STAY_FLOW_LOBBY, STAY_FLOW_LOAD, 1100);
        bo3_stay_step(&s, &in);
        in = at(0x178, away[k], 5000);
        CHECK(bo3_stay_step(&s, &in) == STAY_EV_CANCEL && s.state == STAY_IDLE);
        CHECK(bo3_stay_room(&s, 3, 0) == STAY_ROOM_TITLE);
    }
    /* too long */
    {
        bo3_stay s;
        bo3_stay_in in;
        memset(&s, 0, sizeof s);
        in = lobby(0, 0xC0140, 1000);
        bo3_stay_step(&s, &in);
        in = at(STAY_FLOW_LOBBY, STAY_FLOW_LOAD, 1100);
        bo3_stay_step(&s, &in);
        in = at(0x30, 0x30, 1100 + STAY_MAX_MS);
        CHECK(bo3_stay_step(&s, &in) == STAY_EV_NONE);
        in = at(0x30, 0x30, 1101 + STAY_MAX_MS);
        CHECK(bo3_stay_step(&s, &in) == STAY_EV_TIMEOUT && s.state == STAY_IDLE);
    }
}

static void wait_cases(void)
{
    bo3_stay s;
    memset(&s, 0, sizeof s);
    /* not armed: never on, by itself */
    CHECK(bo3_stay_wait_on(0, &s, STAY_FLOW_RESULTS, 0) == 0);
    s.state = STAY_ARMED;
    CHECK(bo3_stay_wait_on(0, &s, 0x30, 0) == 0);              /* the race */
    CHECK(bo3_stay_wait_on(0, &s, STAY_FLOW_RESULTS, 0) == 1); /* the results */
    CHECK(bo3_stay_wait_on(1, &s, STAY_FLOW_LOAD, 0) == 1);
    CHECK(bo3_stay_wait_on(1, &s, STAY_FLOW_AUTOSAVE, 0) == 1);
    CHECK(bo3_stay_wait_on(1, &s, 0x630, 0) == 1);
    /* the room wrapper has let go of the arming, the search goes on */
    s.state = STAY_IDLE;
    CHECK(bo3_stay_wait_on(1, &s, STAY_FLOW_QUICK, 0) == 1);
    CHECK(bo3_stay_wait_on(1, &s, STAY_FLOW_CREATE, 0) == 1);
    /* where it ends */
    CHECK(bo3_stay_wait_on(1, &s, STAY_FLOW_LOBBY, 0) == 1);                /* the title's short box */
    CHECK(bo3_stay_wait_on(1, &s, STAY_FLOW_LOBBY, STAY_WAIT_LOBBY_MS) == 0);
    CHECK(bo3_stay_wait_on(0, &s, STAY_FLOW_LOBBY, 0) == 0);
    CHECK(bo3_stay_wait_on(1, &s, STAY_FLOW_ONLINE, 0) == 0);
    CHECK(bo3_stay_wait_on(1, &s, STAY_FLOW_MAIN, 0) == 0);
    s.state = STAY_ARMED;
    CHECK(bo3_stay_wait_on(1, &s, STAY_FLOW_ONLINE, 0) == 0);

    CHECK(bo3_stay_connect_stuck(1, STAY_FLOW_SIGNIN, 5, 1, STAY_CONNECT_MS) == 1);
    CHECK(bo3_stay_connect_stuck(1, STAY_FLOW_SIGNIN, 5, 1, STAY_CONNECT_MS - 1) == 0);
    CHECK(bo3_stay_connect_stuck(0, STAY_FLOW_SIGNIN, 5, 1, STAY_CONNECT_MS) == 0);     /* the title's own sign-in */
    CHECK(bo3_stay_connect_stuck(1, STAY_FLOW_SIGNIN, 3, 1, STAY_CONNECT_MS) == 0);     /* already leaving */
    CHECK(bo3_stay_connect_stuck(1, STAY_FLOW_SIGNIN, 5, 0, STAY_CONNECT_MS) == 0);     /* the rankings page */
    CHECK(bo3_stay_connect_stuck(1, STAY_FLOW_QUICK, 5, 1, STAY_CONNECT_MS) == 0);

    s.state = STAY_ARMED;
    CHECK(bo3_stay_results_quit(&s, STAY_FLOW_RESULTS, 3, STAY_RESULTS_QUIT) == 1);
    CHECK(bo3_stay_results_quit(&s, STAY_FLOW_RESULTS, 3, 0x41) == 0);      /* its Continue */
    CHECK(bo3_stay_results_quit(&s, STAY_FLOW_RESULTS, 5, STAY_RESULTS_QUIT) == 0);
    CHECK(bo3_stay_results_quit(&s, STAY_FLOW_AUTOSAVE, 3, STAY_RESULTS_QUIT) == 0);
    s.state = STAY_IDLE;
    CHECK(bo3_stay_results_quit(&s, STAY_FLOW_RESULTS, 3, STAY_RESULTS_QUIT) == 0);

    /* the host quit on its results: the mode's "go on" byte is set, once */
    CHECK(bo3_stay_results_release(1, STAY_FLOW_RESULTS, 1, 0) == 1);
    CHECK(bo3_stay_results_release(1, STAY_FLOW_RESULTS, 1, 1) == 0);         /* already set */
    CHECK(bo3_stay_results_release(0, STAY_FLOW_RESULTS, 1, 0) == 0);         /* no word */
    CHECK(bo3_stay_results_release(1, STAY_FLOW_RESULTS, 0, 0) == 0);         /* no mode object */
    CHECK(bo3_stay_results_release(1, STAY_FLOW_AUTOSAVE, 1, 0) == 0);

    CHECK(bo3_stay_save_done(1, STAY_FLOW_AUTOSAVE, 5, STAY_SAVE_DONE) == 1);
    CHECK(bo3_stay_save_done(0, STAY_FLOW_AUTOSAVE, 5, STAY_SAVE_DONE) == 0);   /* a save outside the chain */
    CHECK(bo3_stay_save_done(1, STAY_FLOW_AUTOSAVE, 3, STAY_SAVE_DONE) == 0);   /* already fading */
    CHECK(bo3_stay_save_done(1, STAY_FLOW_AUTOSAVE, 5, 2) == 0);                /* still saving */
    CHECK(bo3_stay_save_done(1, STAY_FLOW_AUTOSAVE, 5, 3) == 0);                /* the retry question stays */
    CHECK(bo3_stay_save_done(1, 0x630, 5, STAY_SAVE_DONE) == 0);
}

static void wait_keep_cases(void)
{
    /* the chain ended without the lobby: off */
    CHECK(bo3_stay_wait_keep(1, STAY_EV_TIMEOUT, -1, 0u) == 0);
    CHECK(bo3_stay_wait_keep(1, STAY_EV_CANCEL, -1, 0u) == 0);
    /* the title's own room page or points pages (event 3 on its path): off */
    CHECK(bo3_stay_wait_keep(1, STAY_EV_NONE, STAY_ROOM_TITLE, 3u) == 0);
    /* our way back to the lobby, other events, nothing new: as it was */
    CHECK(bo3_stay_wait_keep(1, STAY_EV_NONE, STAY_ROOM_HOST, 3u) == 1);
    CHECK(bo3_stay_wait_keep(1, STAY_EV_NONE, STAY_ROOM_GUEST, 3u) == 1);
    CHECK(bo3_stay_wait_keep(1, STAY_EV_NONE, STAY_ROOM_TITLE, 1u) == 1);
    CHECK(bo3_stay_wait_keep(1, STAY_EV_NONE, -1, 0u) == 1);
    CHECK(bo3_stay_wait_keep(1, STAY_EV_ARMED, -1, 0u) == 1);
    CHECK(bo3_stay_wait_keep(0, STAY_EV_NONE, STAY_ROOM_HOST, 3u) == 0);
    /* armed, the points pages first: the room state says TITLE for event 3 */
    {
        bo3_stay st;
        memset(&st, 0, sizeof st);
        st.state = STAY_ARMED;
        st.role = STAY_ROLE_GUEST;
        CHECK(bo3_stay_wait_keep(1, STAY_EV_NONE, bo3_stay_room(&st, 3u, 1), 3u) == 0);
        CHECK(bo3_stay_wait_keep(1, STAY_EV_NONE, bo3_stay_room(&st, 3u, 0), 3u) == 1);
    }
}

/* the server's keys on the lobby stream (bo3_stay_key) */
static void key_cases(void)
{
    static const uint8_t body[] = "COUNT=0\nTKWAITSTATE=gone\nTKWAITWHY=quit\nXTKCOMING=9\nTKCOMING=2\n";
    char v[16];
    int n = (int)sizeof body - 1;
    CHECK(bo3_stay_key(body, n, "TKWAITWHY", v, sizeof v) && !strcmp(v, "quit"));
    CHECK(bo3_stay_key(body, n, "COUNT", v, sizeof v) && !strcmp(v, "0"));
    CHECK(bo3_stay_key(body, n, "TKCOMING", v, sizeof v) && !strcmp(v, "2"));     /* the whole key only */
    CHECK(!bo3_stay_key(body, n, "TKWAITHOST", v, sizeof v) && v[0] == 0);
    CHECK(!bo3_stay_key(body, n, "TKWAIT", v, sizeof v));                         /* not a prefix */
    CHECK(bo3_stay_key(body, 30, "TKWAITWHY", v, sizeof v) == 0);                 /* past n: not read */
    CHECK(bo3_stay_key(body, n, "TKWAITSTATE", v, 3) && !strcmp(v, "go"));        /* cut to the room */
}

int main(void)
{
    key_cases();
    race_cases();
    leave_cases();
    cancel_cases();
    wait_cases();
    wait_keep_cases();
    if (s_fail) {
        printf("%d check(s) failed\n", s_fail);
        return 1;
    }
    printf("all stay checks passed\n");
    return 0;
}
