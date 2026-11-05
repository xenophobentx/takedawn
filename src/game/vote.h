/*
 * vote.h - the track vote of a QUICK PLAY game's lobby (BO-356).
 *
 * Once at least two players are in a game QUICK PLAY made and every guest has
 * a car, every player gets the title's question box with up to three tracks
 * and RANDOM. The d-pad moves, A votes, and a vote can change until the end.
 * After 15 s, or once everyone has voted, the winner shows on every copy, the
 * host writes it into the lobby's request block and sets the flag the title's
 * options page sets, so the track goes out through the title's own game update
 * and the race starts through the host's start code.
 *
 * The host owns the vote: it sends its state to each player several times a
 * second, each packet echoing that player's own choice, and a guest resends
 * its choice until the echo comes back. So a lost packet costs nothing, and a
 * guest that joins during the vote simply starts getting the state.
 *
 * The rules are pure here (tests/vote). VOTE_PURE_ONLY leaves out the glue.
 */
#ifndef BO3_VOTE_H
#define BO3_VOTE_H

#include <stddef.h>
#include <stdint.h>

#define VOTE_MS         15000ull    /* how long a vote lasts */
#define VOTE_WIN_MS     3000ull     /* how long the winner shows */
#define VOTE_APPLY_MS   1800ull     /* into the winner: the host applies the track */
#define VOTE_SEND_MS    250ull      /* the host's state goes out this often */
#define VOTE_RESEND_MS  150ull      /* a guest repeats its choice until it is echoed */
#define VOTE_LOST_MS    3000ull     /* no state for this long: the host has gone */
#define VOTE_TRACKS     3           /* tracks on offer, RANDOM is one row more */
#define VOTE_ROWS       (VOTE_TRACKS + 1)
#define VOTE_PLAYERS    8
#define VOTE_DATA       24          /* the side channel's bytes (XR_VOTE_DATA) */
#define VOTE_NO_TRACK   0xFFu

enum { VP_NONE = 0, VP_VOTE = 1, VP_WIN = 2, VP_OVER = 3 };  /* the vote's phase; OVER: called off */
enum { VH_NONE = 0, VH_WON = 1, VH_OFF = 2 };   /* what a host tick did */
enum { VK_STATE = 1, VK_CHOICE = 2 };           /* the packet kinds */

/* a small generator of our own: the title's random state is never touched */
uint32_t bo3_vote_rand(uint32_t *state);

/* what the host keeps. Player 0 is the host itself (id 0), the others are the
 * game's members by title address. */
typedef struct {
    uint32_t id;
    int choice;             /* row, -1 no vote */
    uint8_t seq;            /* the last choice number taken */
    uint8_t seen;           /* a choice came in, so seq means something */
} vote_player;

typedef struct {
    int phase;
    uint8_t round;
    int rows;               /* tracks on offer + RANDOM */
    uint8_t track[VOTE_TRACKS];
    unsigned long long end_ms, won_ms;
    vote_player p[VOTE_PLAYERS];
    int np;
    int tally[VOTE_ROWS];
    int win_row;
    uint8_t win_track;
} vote_host;

/* start a round: rows from the valid track list (at most VOTE_TRACKS, all
 * different, random), everyone unvoted. 0 when there is no valid track. */
int  bo3_vote_host_begin(vote_host *h, uint8_t round, const uint8_t *valid, int nvalid,
                         unsigned long long now, uint32_t *rng);
/* the players now: ids of the game's members but ourselves. One gone takes
 * its vote with it; a new one starts unvoted. */
void bo3_vote_host_players(vote_host *h, const uint32_t *ids, int n);
/* a choice (id 0 is the host's own). Refused: another round, not a row, an
 * old number, not a player, the vote is over. 1 when it counted. */
int  bo3_vote_host_choice(vote_host *h, uint32_t id, uint8_t round, int row, uint8_t seq);
/* the tally of the votes as they stand */
void bo3_vote_host_tally(vote_host *h);
/* the winner row: the most votes; a tie, or no votes at all, is a random pick
 * among the tied rows */
int  bo3_vote_winner(const int *tally, int rows, uint32_t *rng);
/* the clock: one tick of the vote. VH_WON: time is up or everyone has voted,
 * the winner is set (RANDOM resolved to a track from the valid list). VH_OFF:
 * below two players, the vote is called off. */
int  bo3_vote_host_tick(vote_host *h, unsigned long long now, const uint8_t *valid, int nvalid, uint32_t *rng);
/* seconds left as shown, rounded up */
int  bo3_vote_host_secs(const vote_host *h, unsigned long long now);
/* the state packet for one player */
void bo3_vote_host_pack(const vote_host *h, uint32_t id, unsigned long long now, uint8_t *data);

/* what every copy shows */
typedef struct {
    int phase;
    uint8_t round;
    int secs, rows;
    uint8_t track[VOTE_TRACKS];
    int tally[VOTE_ROWS];
    int win_row;
    uint8_t win_track;
    int choice;             /* my row, -1 none */
    uint8_t seq;            /* my choice's number */
    int acked;              /* the host echoed it */
    unsigned long long state_ms, send_ms;
    uint32_t host;          /* where the state came from */
} vote_client;

void bo3_vote_client_reset(vote_client *c);
/* a state packet: 1 when it changed what is shown */
int  bo3_vote_client_state(vote_client *c, uint32_t from, const uint8_t *data, unsigned long long now);
/* the player picked a row */
void bo3_vote_client_choose(vote_client *c, int row);
/* 1 when the choice has to go out now; *lost set when the host went quiet */
int  bo3_vote_client_tick(vote_client *c, unsigned long long now, int *lost);
void bo3_vote_client_pack(const vote_client *c, uint8_t *data);
/* a choice packet: 1 and its fields */
int  bo3_vote_choice_read(const uint8_t *data, uint8_t *round, int *row, uint8_t *seq);

/* The winner notice: our own open of the title's box 0x550DA0 (sub_000CBE40).
 * The title's open takes nodes without giving the box's back, and its close
 * (sub_000CCC90) gives back what the box holds without looking at +0xA; the
 * title pairs them itself ("if [box+0xA] close; open", 0x9ED28). So each open
 * of ours is paired with exactly one close: ours, while the box is still ours
 * (open, the notice kind), or the title's own, which leaves +0xA clear.
 * *ours: the box holds our notice. Returns 1 when the caller has to close the
 * box now; the event clears *ours except VN_WE_OPEN (sets it) and VN_FRAME
 * (clears it only when the box was closed or is another kind). */
#define VOTE_NOTE_KIND 0            /* BX_NOTICE */
enum { VN_WE_OPEN, VN_FRAME, VN_TITLE_OPEN, VN_TIMEOUT, VN_LOST, VN_NEW_ROUND, VN_LEFT };
int  bo3_vote_note_step(int *ours, int ev, int box_open, int box_kind);

#ifndef VOTE_PURE_ONLY

/* The title's lobby state 1 input (sub_000A0BF0): while the box is up the vote
 * has the pad */
void bo3_vote_input(void (*title_fn)(void));
/* sub_000A1560: START does not run the race past a vote */
void bo3_vote_start_check(void (*title_fn)(void));
/* sub_000CCF00: a row's text of the box, any row */
void bo3_vote_row(void (*title_fn)(void));
/* sub_000CBE40: the title opens the box */
void bo3_vote_box_open(void (*title_fn)(void));
/* each lobby frame on every copy (online.c), before the title's */
void bo3_vote_lobby_frame(uint32_t scr);
/* each flip: a lobby that is gone takes the vote with it */
void bo3_vote_flip(void);
/* The host of a vote game, each lobby frame (quickplay.c): 1 when the vote is
 * over and the host's start may run, 0 while it runs, -1 when this game has no
 * vote (its countdown applies). */
int  bo3_vote_host_frame(uint32_t scr, int players, int idle, int guests_ready);
/* a vote is on or its winner is being applied: for the lobby's status line */
int  bo3_vote_busy(void);
/* the box as a player sees it, for the test navigator (onlinebot.c): 1 while
 * it takes a vote; its rows, its cursor, this copy's vote (-1 none), the round */
int  bo3_vote_bot_view(int *rows, int *cursor, int *mine, unsigned *round);

#endif
#endif
