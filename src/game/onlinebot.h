/*
 * onlinebot.h - RECOMP_DEBUG=onlinebot=host|guest: the online test navigator
 * (BO-134, onlinebot.c). Off unless set.
 */
#ifndef BO3_ONLINEBOT_H
#define BO3_ONLINEBOT_H

#include <stddef.h>
#include <stdint.h>

/* the screens the navigator knows */
enum {
    OB_SC_UNKNOWN, OB_SC_LOADING, OB_SC_BOOT, OB_SC_MAIN, OB_SC_GAMERTAG, OB_SC_SIGNIN, OB_SC_LIVE,
    OB_SC_LOBBYSEL, OB_SC_RANKINGS, OB_SC_ROOM, OB_SC_CREATE, OB_SC_MODE, OB_SC_CREATING, OB_SC_QUICK,
    OB_SC_CAR, OB_SC_LOBBY, OB_SC_RACE, OB_SC_RACE_END, OB_SC_RESULTS, OB_SC_AUTOSAVE, OB_SC_POINTS,
    OB_SC_OPTIONS,
    OB_SC_N
};

/* at start: reads the switch (nothing more when it is off) */
void bo3_onlinebot_start(void);
/* every flip the title emits (main.c's uw_flip) */
void bo3_onlinebot_flip(uint32_t flip);

/* the screen of a front-end state (offset in the machine 0x567170) and page
 * is bo3_online_screen_for (onlinescreens.h, BO-352: shared, all three discs) */
const char *bo3_onlinebot_screen_name(int sc);
/* "0,roadrage,pc" -> codes; the count */
int bo3_onlinebot_parse_modes(const char *v, int *out, int max);

/* The plan: the line of the TAKEDAWN ONLINE page a copy takes for a game, in
 * the page's own order (XL_QUICK .. XL_BROWSE, xboxlive.h). */
enum { OB_PLAN_QUICK, OB_PLAN_CREATE, OB_PLAN_CODE, OB_PLAN_BROWSE, OB_PLAN_WAIT, OB_PLAN_N };
const char *bo3_onlinebot_plan_name(int plan);
/* the online page's line a plan presses: wait is QUICK PLAY (line 0), after
 * the bot has seen a race under way (BO-365) */
int bo3_onlinebot_plan_line(int plan);
/* "quick,create,code,browse" -> plans; the count */
int bo3_onlinebot_parse_plans(const char *v, int *out, int max);

/* the code entry's next press for the code `want`, from what is typed, the
 * list's row and the grid's column (codeentry.h) */
enum { OB_CE_DELETE, OB_CE_UP, OB_CE_DOWN, OB_CE_LEFT, OB_CE_RIGHT, OB_CE_TYPE, OB_CE_JOIN };
int bo3_onlinebot_entry_step(const char *want, const char *typed, int row, int col);
/* the code CREATE GAME takes from the switch, or NULL for its random one */
const char *bo3_onlinebot_code(void);

/* Waiting for a race (BO-365, plan "wait" or a code whose game is racing).
 * The states are the toolkit's XNET_WAIT_* values (xnet_hle.h): 0 none,
 * 1 racing, 2 lobby, 3 open, 4 full, 5 gone. */
enum { OB_WAIT_JOINED, OB_WAIT_GONE, OB_WAIT_FULL, OB_WAIT_TIMEOUT, OB_WAIT_CANCELLED, OB_WAIT_OTHER };
#define OB_WAIT_MAX_MS     480000ull    /* the client's RW_MAX_MS */
#define OB_WAIT_STUCK_MS   (OB_WAIT_MAX_MS + 60000ull)   /* Quick Match "stuck" while a wait runs */
#define OB_STUCK_QUICK_MS  60000ull     /* ... and when none does */
/* how a wait that has left the Quick Match page ended: the last state the
 * server gave, how long it ran, the screen it went to, whether the bot
 * pressed B */
int bo3_onlinebot_wait_result(int last_state, unsigned long long waited_ms, int next_sc, int cancelled);
const char *bo3_onlinebot_wait_result_name(int result);
/* 1 once next_sc is a screen the wait's end can be told from (loading and
 * unknown screens are on the way) */
int bo3_onlinebot_wait_settled(int next_sc);
int bo3_onlinebot_races_done(int max_races, int race_n);
int bo3_onlinebot_step_fresh(int from_sc, int to_sc);
/* the state's word for the log: racing, lobby, open, full, gone */
const char *bo3_onlinebot_wait_state_name(int state);
/* how long Quick Match may search before the bot calls it stuck */
unsigned long long bo3_onlinebot_quick_limit_ms(int waiting);

/* the host's start gate: the lobby screen's state, the title's peer table,
 * the game's players and their ready values as the lobby's record says */
typedef struct {
    uint8_t lstate, linked, alive;
    uint32_t np;               /* players in the title's peer table */
    uint32_t links[8];         /* their link states; 0xFF: none (this copy) */
    int players;               /* in the lobby record */
    char name[8][16];
    int ready[8];
    const char *me;            /* this copy's gamertag */
    int want;                  /* guests to wait for */
} ob_gate_in;
/* 1: Start now; why says what holds it (or what is there) */
int bo3_onlinebot_gate(const ob_gate_in *g, char *why, size_t n);

#endif
