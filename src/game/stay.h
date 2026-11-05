/*
 * stay.h - "stay together" after an online race (Takedawn Online, step 4).
 *
 * The title ends an online game at the race start: the lobby state's start
 * case (sub_0006DAC0 at 0x6DB12) disconnects from the lobby server
 * (sub_00216190, "disc"), and the race runs on the peer links alone. After
 * the race it signs in again (state +0x630: CONNECTING, SIGNING IN, UPDATE
 * RANKINGS, the room), shows the points (+0x260) and lands on the room page
 * (+0x928). The game is gone on the server; the console version ended there.
 *
 * Here the players of a game that raced go back to one game lobby, and all
 * the player sees between the results and the lobby is one wait message. The
 * title's message pages on the way (autosave, connecting, signing in, the
 * rankings) do real work in their ticks, so they run; while they run, the
 * strings they read show that one message and their small heading is empty.
 * The autosave leaves by its own Continue exit when the save is done. The
 * lobby select page is not built (it only picks the room; a guest's Quick
 * Match enters one itself, and the host's create state enters it with the
 * Quick Match page's steps before gcre: the title takes a gcre answer only
 * from inside a room). When the room state is
 * entered (its event 3), the points pages are passed over (they only show what
 * the race already stored; the title's closing step runs), and the host does
 * the room's own CREATE GAME transition (into the create state +0x5D0, whose
 * name and mode pages QUICK PLAY's wrappers fill with the last game's name and
 * settings), every guest the title's Quick Match (+0x808, Quick Match context
 * 1) with its search limited to the host's game, searching again until it is
 * there. The car page is not built: the car and colour of the race go through
 * the car state's own exit (step 0), and the lobby comes up.
 *
 * The decisions are pure so tests/stay can run them without the title.
 */
#ifndef BO3_STAY_H
#define BO3_STAY_H

#include <stdint.h>

/* flow states (offsets in the flow machine, see FLOW-RE.md) */
#define STAY_FLOW_LOBBY     0x590u
#define STAY_FLOW_ONLINE    0x5B0u
#define STAY_FLOW_MAIN      0x0C0u
#define STAY_FLOW_ROOM      0x928u
#define STAY_FLOW_QUICK     0x808u
#define STAY_FLOW_CREATE    0x5D0u
#define STAY_FLOW_LOAD      0x018u      /* the loading screen between the lobby and the race */
#define STAY_FLOW_RESULTS   0x178u
#define STAY_FLOW_AUTOSAVE  0x4D8u
#define STAY_FLOW_SIGNIN    0x630u

/* the game record's SYSFLAGS bit the server sets with gsta: under way */
#define STAY_SYS_STARTED    0x80000u

/* the longest a race and its results may take before the next lobby is
 * given up (a stuck chain must not arm a later, unrelated room) */
#define STAY_MAX_MS         (30ull * 60ull * 1000ull)

enum { STAY_IDLE, STAY_LOBBY, STAY_ARMED };
enum { STAY_ROLE_NONE, STAY_ROLE_HOST, STAY_ROLE_GUEST };

typedef struct {
    int state;                  /* STAY_IDLE / STAY_LOBBY (in a game's lobby) / STAY_ARMED (raced) */
    int role;
    int started;                /* the record said "under way" while in the lobby */
    int usable;                 /* the game can be made again (no password, a name) */
    unsigned long long armed_at;
    char name[32];              /* the game's name (record + 4): the host makes it again, the guests look for it */
} bo3_stay;

typedef struct {
    uint32_t flow, prev;        /* flow state now and at the last flip */
    int record;                 /* a game record is held ([session + 0x38]) */
    uint32_t sysflags;          /* its SYSFLAGS */
    int host;                   /* its HOST is this copy */
    int password;               /* the game has a password */
    const char *name;           /* its NAME */
    unsigned long long now;
} bo3_stay_in;

/* what changed on this flip, for the log */
enum { STAY_EV_NONE, STAY_EV_LOBBY, STAY_EV_ARMED, STAY_EV_LEFT, STAY_EV_CANCEL, STAY_EV_TIMEOUT };

/* every flip: follows the player from a game's lobby into the race and on */
int bo3_stay_step(bo3_stay *s, const bo3_stay_in *in);

/* the room state's handler is called with this event; what to do instead of
 * the room page */
enum { STAY_ROOM_TITLE, STAY_ROOM_HOST, STAY_ROOM_GUEST };
int bo3_stay_room(const bo3_stay *s, uint32_t event, int title_path);

/* the one wait message from the results to the lobby: is it on at this flow?
 * lobby_ms: how long this copy has been in the lobby (the title's short
 * "updating" box comes up there) */
#define STAY_WAIT_LOBBY_MS  2000u
int bo3_stay_wait_on(int on, const bo3_stay *s, uint32_t flow, unsigned lobby_ms);
/* the wait message after this flip's step event (bo3_stay_step) or the room
 * state's call (room_act from bo3_stay_room for room_event; -1 when the room
 * state was not called): off when the chain ended without the lobby (the
 * time ran out, the player went elsewhere) and when the title builds its
 * room page or its points pages itself; otherwise as it was */
int bo3_stay_wait_keep(int on, int ev, int room_act, uint32_t room_event);

/* the results' last page (0x559FA8) leaves with this code on the Quit question's
 * Yes (traced; 0x41 is its Continue): the title goes on the same way, to the room */
#define STAY_RESULTS_QUIT   0x42u
int bo3_stay_results_quit(const bo3_stay *s, uint32_t flow, uint32_t phase, uint32_t code);

/* The results page online (sub_0008A730, page +0x24 its step): Continue To
 * Lobby puts up the box WAITING FOR OTHER PLAYERS (step 4, sub_0008B180; its
 * CANCEL is back to step 0). The box, and the steps after Continue (2, 3),
 * end with the page's Continue exit (sub_0008AFF0, code 0x41) once the race's
 * mode object ([[0x4D5370] + 0x1B8]) has its "go on" byte at +0x5D: its
 * message handler (vtable +0xC8, sub_0002B0D0, flag 2) sets it, with +0x44 at
 * -1, when the host's word comes over the game link. A host that quit on its
 * results never sends it (its guests sat in the box ~2 minutes). When the host
 * says it quit (the TKRX "left" word), the byte is set here as that handler
 * does: the box goes at once; a guest still reading its results goes on at
 * its own Continue. Nothing changes without the word. */
#define STAY_MODE_GO 0x5Du
int bo3_stay_results_release(int host_left, uint32_t flow, int have_mode, uint8_t go);

/* the sign-in state's connecting page has not got through in this long */
#define STAY_CONNECT_MS     25000ull
int bo3_stay_connect_stuck(int wait_on, uint32_t flow, uint32_t phase, int on_connect_page, unsigned long long ms);

/* the autosave page's state word when the save has worked (code 0x35 at
 * sub_00070400; a second later the page offers its Continue) */
#define STAY_SAVE_DONE      7u
/* the autosave page has saved: leave it by its own Continue exit */
int bo3_stay_save_done(int wait_on, uint32_t flow, uint32_t phase, uint32_t page_state);

/* the room state's handler (recomp_manual*.c wrapper of sub_0006EEE0) */
void bo3_stay_room_state(void (*title_fn)(void));
/* every flip (from the online navigator's flip hook) */
void bo3_stay_flip(void);
/* the car page's builder, sub_0006DE50 (recomp_manual*.c wrapper) */
void bo3_stay_car_page(void (*title_fn)(void));
/* the small heading's setter, sub_000C9240 (recomp_manual*.c wrapper) */
void bo3_stay_heading(void (*title_fn)(void));
/* a page's change of the line along the bottom, sub_000CAD00 (recomp_manual*.c wrapper) */
void bo3_stay_line_text(void (*title_fn)(void));
/* the first text of the line along the bottom, sub_000CA090 (recomp_manual*.c wrapper) */
void bo3_stay_line_first(void (*title_fn)(void));
/* the lobby select page's builder, sub_000A4900 (recomp_manual*.c wrapper) */
void bo3_stay_lobby_select(void (*title_fn)(void));
/* a player of the race said it quit on its results (the TKRX "left" word,
 * react.c): when it is our host, our results box ends at once */
void bo3_stay_peer_left(const char *name, int why);
/* the wait message ends here (the guest's timeout shows the title's box) */
void bo3_stay_wait_end(void);
/* the guest's wait ended with our own line (the host left, the game is
 * full): the wait message goes, no box, the Quick Match page's Back leads to
 * the online page */
void bo3_stay_wait_done(void);

/* ---- what the lobby server says about the race (item 3 of the 0.3.0
 * review): keys the toolkit's view does not keep, read from the raw lobby
 * stream (online.c) ---- */
/* every lobby read (a header or a body): TKWAITHOST / TKWAITWHY of a waiter's gsea
 * answer, TKCOMING of a member record */
void bo3_stay_lobby_scan(const uint8_t *buf, int n);
/* the last gsea answer's TKWAITHOST (the host signed in again) and TKWAITWHY
 * (racewait.h RW_SWHY_*) */
void bo3_stay_wait_keys(int *host_back, int *why);
/* the players of the race this game was made again after who are still on
 * their way back (TKCOMING): the game lobby waits for them */
int  bo3_stay_coming(void);
/* the player quit on the results: the sign-in after it says so (TKSTAYQUIT on
 * auth, online.c's lobby tag), once */
int  bo3_stay_quit_take(void);
/* the pure parts of the scan (tests/stay): the value of KEY= in the n bytes
 * of a lobby packet body, into out (at most cap - 1); 1 when found */
int  bo3_stay_key(const uint8_t *body, int n, const char *key, char *out, int cap);

#endif
