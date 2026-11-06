/*
 * quickplay.h - QUICK PLAY on the TAKEDAWN ONLINE page, through the title's
 * own screens (Takedawn Online, step 3).
 *
 * The title's Quick Match (the Xbox Live page's case 0, state +0x808, page
 * 0x5628E0) already joins by itself: its per-record callback (sub_000A9A90)
 * filters the +gam records and calls the select routine (sub_000AA2F0) on the
 * first it accepts, and the page goes on to the join, the car select and the
 * lobby with no input. So QUICK PLAY needs no pad press for that path. What
 * it adds is the other end: when the search comes back with no game the page
 * would show NO GAMES FOUND and wait for A (Create Game); here the page's own
 * A branch (the room list request, sub_001BC090, then the room, then exit
 * code 0x2D into the create state) runs without the screen, and the create
 * state's name and mode pages, which only collect values, are skipped: the
 * values the title would have put there are written by the wrappers in
 * recomp_manual*.c, and the creating page (sub_00098E80, gcre) is the title's.
 *
 * The decisions are pure so tests/quickplay can run them without the title.
 */
#ifndef BO3_QUICKPLAY_H
#define BO3_QUICKPLAY_H

#include <stddef.h>
#include <stdint.h>

/* the Quick Match page's own state byte (page + 0x1942) */
enum {
    QP_SUB_WAIT = 0,        /* searching, or the results are being processed */
    QP_SUB_LIST = 1,        /* the result list is shown */
    QP_SUB_NONE = 2,        /* NO GAMES FOUND */
    QP_SUB_ROOMS = 8,       /* the room list was asked for (the A branch of NO GAMES) */
    QP_SUB_ROOM = 6,        /* a room was chosen, waiting for the session */
    QP_SUB_PICKED = 7,      /* a game was picked (sub_000AA2F0): the room is moved into (state 4) */
    QP_SUB_JOINING = 5,     /* its gjoi (sub_001BAD70) waits for the session (0xAAA85): a failure
                             * there builds the error box (state 3) and then the list (state 1),
                             * whose one game refuses every A */
    QP_SUB_IDLE = 0xFF      /* ours: past the page's switch (only sub_000AA4D0 reads the byte), so the
                             * page does nothing while it leaves or waits to search again */
};

/* what the tick does: nothing, start the create branch, give the create
 * branch up (the room list never came), or leave the page by the title's Back
 * exit (the player's B, held until it is safe: nothing in flight) */
enum { QP_NONE, QP_CREATE, QP_GIVEUP, QP_CANCEL, QP_NOTFOUND, QP_RESEARCH, QP_REFUSED,
       QP_WAIT };       /* BO-365: nothing open, but a race under way has room: wait for it (racewait.h) */

/* the exit code of the title's Back from the Quick Match page, and of the
 * page's exit into the create state */
#define QP_EXIT_BACK    0x18u
#define QP_EXIT_CREATE  0x2Du

typedef struct {
    int armed;              /* QUICK PLAY was chosen and has not finished */
    int creating;           /* the create branch was started */
    int on_search;          /* the page being ticked is the Quick Match page */
    int active;             /* its page manager phase is "active" (5) */
    int sub;                /* page + 0x1942 */
    int ready;              /* page + 0x1940: the results were processed */
    int count;              /* page + 0x1920: the results kept */
    int rooms_done;         /* page + 0x1941: the room list came in */
    int cancel;             /* the player pressed B while QUICK PLAY ran */
    int join;               /* JOIN WITH CODE: the search is for one code; nothing found is
                             * "no open game with that code", never a game of our own */
    int join_failed;        /* state 5 and the session says the join failed: the lobby refused
                             * it (the race had just started, or the host kicked us) */
    int refusals;           /* joins refused since QUICK PLAY began */
    int racing;             /* BO-365: the empty answer named a race with room (bo3_racewait_should_wait) */
    int waiting;            /* the race wait runs: the page is racewait.c's (qp_wait_tick) */
    unsigned long long now, create_at;
} qp_in;

/* BO-364. A refused join (state 5) is taken like a search that found nothing,
 * before the title's error box (sub_000A98E0, state 3, then the list of the one
 * game that refused, A by A, for ever): QUICK PLAY searches again (QP_RESEARCH;
 * the lobby no longer lists a game that kicked us or one under way) and at the
 * QP_REFUSE_MAX-th refusal makes its own game (QP_CREATE); JOIN WITH CODE goes
 * back to the code entry (QP_REFUSED); B pressed meanwhile leaves (QP_CANCEL). */
#define QP_REFUSE_MAX 3

#define QP_ROOM_WAIT_MS 25000ull    /* the create branch waits this long for the room */

/* what to do on this tick of the Quick Match page */
int bo3_quickplay_decide(const qp_in *in);
/* the page has started to exit with the code to the create state and the
 * player has pressed B: the exit becomes the Back exit */
int bo3_quickplay_stay_release(int creating, int in_create_state);
int bo3_quickplay_recode(int cancel, int creating, unsigned phase, unsigned code);

/* the Continue of the name page, for what the player did not type: the name
 * the title puts there is the gamertag (strncpy semantics, like the title's own
 * copy); returns the number of bytes written (n) */
void bo3_quickplay_copy_str(uint8_t *dst, size_t n, const uint8_t *src);
/* the mode page's Continue, for the mode it shows: the title's list steps
 * (a limit above 6 forces mode 4; a limit other than 2 allows neither 2 nor 3) */
int bo3_quickplay_mode(int mode, int limit);
/* block + 0x18 with the language byte the Continue stores in its top byte */
uint32_t bo3_quickplay_flags18(uint32_t w, uint8_t language);
/* the event the page manager hands the state when a page exits: 5, the
 * state's second word, the exit code, and the context (0 from code 3 on) */
uint32_t bo3_quickplay_exit_ctx(uint32_t code, uint32_t manager_ctx);

/* ---- the host's start countdown (games created by QUICK PLAY only) ----
 *
 * With two players or more in the lobby a 30 s countdown runs, shown in the
 * lobby's top line; at 0, once every guest has a car (the title's own START
 * asks only for the host, a linked and alive lobby and a peer; a guest still
 * on car select must not be started), the host's own START code runs. The
 * host can press START earlier. Below two players the countdown stops. */
#define QP_COUNTDOWN_MS 30000ull
#define QP_START_COOLDOWN_MS 3000ull

typedef struct {
    unsigned long long start_ms;    /* when the countdown began; 0 = not running */
    unsigned long long hold_until;  /* no new start before this (a start just asked for) */
} qp_cd;
typedef struct {
    int host_game;          /* this copy hosts a game QUICK PLAY created */
    int players;            /* in the lobby record */
    int idle;               /* the lobby screen's state is 1 (plain) */
    int guests_ready;       /* every guest has a car (ready 1 or 3) */
    unsigned long long now;
} qp_cd_in;
enum { QP_CD_NONE, QP_CD_TICK, QP_CD_START };
/* advances the countdown; *seconds is the number to show (0: show nothing) */
int bo3_quickplay_countdown(qp_cd *c, const qp_cd_in *in, int *seconds);
/* the lobby screen's frame (online.c, before the title's): runs the countdown
 * and the start. The seconds for the top line (0 = none). */
void bo3_quickplay_lobby_frame(uint32_t screen);
int bo3_quickplay_countdown_seconds(void);

/* ---- after a raced game: back to one lobby (stay.c, Takedawn Online step 4) ----
 *
 * The host makes the game again through the create state, its name and mode
 * pages filled from the last lobby's request (words +0 .. +0x28 of 0x55DE80,
 * the block the lobby's options page edits). A guest runs the Quick Match with
 * its search limited to that game's name (the per-record callback,
 * sub_000A9A90, wrapped); while the game is not there yet it searches again
 * every QP_RETRY_MS, as the page's own setup does (sub_000A9590 at 0xA980A),
 * until the wait is over: then the page's Back exit, to the online page. */
#define QP_STAY_REQ_WORDS 11u
#define QP_RETRY_MS 2000ull

enum { QP_T_NONE, QP_T_HOLD, QP_T_RESEARCH, QP_T_CANCEL, QP_T_TIMEOUT };
typedef struct {
    int active;             /* the page manager's phase is "active" */
    int sub;                /* page + 0x1942 */
    int ready;              /* page + 0x1940 */
    int count;              /* page + 0x1920 */
    int holding;            /* an empty answer came: waiting to search again */
    int cancel;             /* the player pressed B */
    int join_failed;        /* state 5 and the join failed (BO-364): waited out like an empty answer */
    unsigned long long now, deadline, retry_at;
} qp_target_in;
int bo3_quickplay_target_decide(const qp_target_in *in);
/* BO-384 (GitHub #122): the host's start reads the game record. A guest's race
 * takes its mode, track (the direction is part of the track), laps and the
 * rest from the game record when the record says "under way" (sub_000A3AA0 at
 * 0xA165C). The host goes from its start lock (state 0x11) to 0x0B and gsta at
 * 0x9F4EB without that read, on the race values its lobby set last, and the
 * lobby's Options return sets them from the edited request and then from the
 * record before the update has gone out (0x6DD70 -> 0x9EA20 -> 0xA3AA0): the
 * host raced the old settings and the guests the new ones. So on the frame the
 * title sends the host's gsta (state 0x11, the record's lock bit 12 back, not
 * yet under way) the host runs the same read: before the title's lobby frame,
 * or, when the lock's answer came inside that frame, right after it (the state
 * went 0x11 -> 0x0B; the race loads frames later, after 0x0B's wait for the
 * peers; the next frame the lobby connection, and with it the record, may be
 * gone). 1: read now. */
#define QP_SYS_LOCK     0x00001000u
#define QP_SYS_STARTED  0x00080000u
int bo3_quickplay_start_reads_record(int host, unsigned lobby_state, uint32_t sysflags);
/* the lobby frame's hooks (online.c, every game): before the title's frame
 * (bo3_quickplay_lobby_frame calls it) and after it, with the state before */
void bo3_quickplay_host_start_sync(uint32_t scr);
void bo3_quickplay_host_start_after(uint32_t scr, unsigned state_before);

/* a +gam record's NAME against the game looked for (exact, at most 31 bytes) */
int bo3_quickplay_name_match(const char *rec, const char *want);
/* the request words the stay create writes: +8 .. +0x28 from the saved lobby
 * request, the language byte (+0x18 top) as the mode page stores it */
void bo3_quickplay_stay_request(uint32_t *blk, const uint32_t *saved, uint8_t language);

void bo3_quickplay_stay_create(const char *name, const uint32_t *req, int qp_game);
/* ident: the raced game's IDENT (its searches carry TKWAITFOR: the server
 * says how the race stands, racewait.h rw_stay; 0 = only the wait_ms deadline) */
void bo3_quickplay_stay_join(const char *name, int ident, unsigned long long wait_ms);
int bo3_quickplay_host_game(void);
/* the page manager's exit event to the current state (the code a page's exit leaves) */
int bo3_quickplay_exit_event(uint32_t code);

/* recomp_manual*.c: the Quick Match page's per-record callback */
void bo3_quickplay_record(void (*title_fn)(void));

/* QUICK PLAY was chosen on the online page (xboxlive.c) */
void bo3_quickplay_begin(void);
/* JOIN WITH CODE's JOIN (xboxlive.c): the same Quick Match with the code set
 * (the lobby lists only that code's game); finding none leaves by the page's
 * Back exit instead of creating a game */
void bo3_quickplay_begin_join(void);
/* CREATE GAME: straight from the online page into the title's create state
 * (the transition the room's CREATE GAME makes). The name page is skipped as
 * for QUICK PLAY; the mode page is the title's, the player's real choice. */
void bo3_quickplay_create_game(void);
/* that create state is running (its Back goes to the online page) */
int  bo3_quickplay_create_direct(void);
/* the room is still being entered behind the mode page: its Continue waits
 * (a game is made only from inside a room) */
int  bo3_quickplay_room_pending(void);
enum { CR_ROOM_NONE, CR_ROOM_LIST, CR_ROOM_WAIT };
/* The room list for CREATE GAME, asked once per entry (*asked_ms 0: not yet)
 * and then waited for: 1 when the request has to go out now. The answer
 * (done: the callback ran; count: rooms in it) is read after every call, so a
 * request answered inside the call is taken at once. The first call always
 * asks (the fields may hold an older search's answer). Asked again only when
 * an answer came back with no room (after QP_ROOM_RETRY_MS) or none came at
 * all (after QP_ROOM_ANSWER_MS); never every flip. */
#define QP_ROOM_RETRY_MS  1000ull
#define QP_ROOM_ANSWER_MS 5000ull
int bo3_quickplay_room_ask(unsigned long long *asked_ms, unsigned long long now, int done, unsigned count);
/* the create state's page step for its Back case: with no name page behind it,
 * the mode page's step (1) leaves as the first page's (0) does */
static inline uint32_t bo3_quickplay_create_step(int direct, uint32_t step)
{
    return direct && step == 1u ? 0u : step;
}
/* how the last join came back to the online page, once: 0 it did not (or it
 * joined), QP_JOIN_CANCELLED (B while searching), QP_JOIN_MISSED (no open
 * game with the code), QP_JOIN_REFUSED (the game was found but refused the
 * join: it had just started), QP_JOIN_REMOVED (its host had kicked us: the
 * lobby's TKKICKED, BO-364) */
enum { QP_JOIN_NONE, QP_JOIN_CANCELLED, QP_JOIN_MISSED, QP_JOIN_REFUSED, QP_JOIN_REMOVED,
       QP_JOIN_RACE_GONE,   /* BO-365: the code's game was racing; nobody made it again */
       QP_JOIN_RACE_FULL,   /* ... it was made again, with no seat for us */
       QP_JOIN_BROWSE_EMPTY,    /* BO-372: BROWSE GAMES found no game at all */
       QP_JOIN_BROWSE_BACK };   /* ... B on its list: the cursor back on BROWSE GAMES */
/* what JOIN WITH CODE comes back with: missed (nothing found) or refused, and
 * whether the lobby said the host had removed us */
int bo3_quickplay_join_result(int refused, int removed);
int bo3_quickplay_join_return(void);
int bo3_quickplay_armed(void);
/* BO-365: the race this copy waits for (its IDENT, TKWAITFOR on every gsea:
 * online.c's lobby tag); 0 when it waits for none */
int bo3_quickplay_wait_ident(void);
/* the wait as it stands, for the soak bot and the line; returns its state
 * (RW_OFF when none). rw_state is racewait.h's. */
struct rw_state;
int bo3_quickplay_wait_view(struct rw_state *out);
/* this copy hosts a game QUICK PLAY made: it starts by itself */
int bo3_quickplay_host_game(void);

/* ---- BROWSE GAMES (BO-372) ----
 *
 * BROWSE GAMES is the title's Quick Match page in its list mode: with the
 * page's context word (+0x1928) 0, the OptiMatch one, the per-record callback
 * keeps up to nine games and the page shows them as a list (state 1, A joins
 * the game under the cursor) instead of joining the first. The criteria that
 * mode reads (+0x1924, the state's block the page was given) are set to "any"
 * (ping band 0, mode 5, ranking 0). Its searches ask the lobby for every game
 * (TKBROWSE=1, online.c's lobby tag; tools/lobby lists racing and full games
 * too and says each one's state, xnet_hle_browse_state), open games first.
 * Each row's name says its state ("RACING 1:20", "FULL"). A on an open game
 * is the title's own join; on a racing one the race wait (racewait.h) runs
 * for it, with the page back in its Quick Match context so the title joins the
 * game made again by itself; on a full one the bottom line says so and the
 * list stays. B goes back to the online page (from a wait: back to the list).
 * The list is searched again every QP_BR_REFRESH_MS, the cursor kept on its
 * game. An empty list goes back to the online page, which says so
 * (QP_JOIN_BROWSE_EMPTY): no NO GAMES FOUND, no create flow of the title's. */
enum { QP_BR_NONE, QP_BR_OPEN, QP_BR_RACING, QP_BR_FULL,    /* XNET_GS_* */
       /* the labels of the racing game this copy waits for (only the row's name) */
       QP_BR_WAITING,       /* its race runs: "- RACING m:ss - WAITING" */
       QP_BR_QUEUED,        /* the race is over, others are ahead: "- WAITING" */
       QP_BR_JOINING };     /* the race is over, the title joins: "- JOINING" */
enum { QP_PICK_JOIN, QP_PICK_WAIT, QP_PICK_FULL };
#define QP_BR_ROWS        9         /* the page keeps nine */
#define QP_BR_NAME        32u       /* a row's name buffer (31 and its NUL) */
#define QP_BR_REFRESH_MS  10000ull
#define QP_BR_MSG_MS      4000ull   /* a message on the bottom line ("... IS FULL") */
/* what A on a row of this state does */
int  bo3_quickplay_browse_pick(int state);
/* the row's name as the list shows it: the game's name, then for a racing
 * game "- RACING m:ss" and for a full one "- FULL", for the one this copy
 * waits for "- RACING m:ss - WAITING" ("- WAITING m:ss" when the name is too
 * long for that), "- WAITING" or "- JOINING" (the name cut to fit
 * QP_BR_NAME - 1 characters, the state never) */
void bo3_quickplay_browse_label(char *out, size_t n, const char *name, int state, int secs);
/* the label of the row this copy waits for (QP_BR_WAITING, _QUEUED,
 * _JOINING) from the race wait's state, TKWAITSTATE and queue (racewait.h) */
int  bo3_quickplay_browse_wait_label(int rw, int ws, int queue);
/* the order of the list: open, racing, full (the page sorts by its rows' key
 * +0x34 first; pos keeps the server's order inside a state) */
uint32_t bo3_quickplay_browse_key(int state, int pos);

/* BROWSE GAMES was chosen on the online page (xboxlive.c) */
void bo3_quickplay_begin_browse(void);
/* the next gsea is a browse search (TKBROWSE=1): read by the lobby tag */
int  bo3_quickplay_browse_tag(void);
/* the pad (xboxlive.c's pad hook): A and B down now; the buttons the title
 * must not see (1 A, 2 B) */
int  bo3_quickplay_browse_pad(int a, int b);
/* the list as the player sees it, for the test navigator */
typedef struct {
    int up;                 /* the list is shown */
    int count, cursor;
    int ident[QP_BR_ROWS], state[QP_BR_ROWS];
    char name[QP_BR_ROWS][QP_BR_NAME];      /* the game's own name */
    char line[96];          /* the bottom line's message, "" none */
} qp_browse_view;
int  bo3_quickplay_browse_view(qp_browse_view *v);
/* registers the mark; every flip */
void bo3_quickplay_start(void);
void bo3_quickplay_flip(uint32_t flip);
/* recomp_manual*.c: the create state's name page and mode page builders */
void bo3_quickplay_name_page(void (*title_fn)(void));
void bo3_quickplay_mode_page(void (*title_fn)(void));

#endif
