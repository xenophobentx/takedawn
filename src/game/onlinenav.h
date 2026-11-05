/*
 * onlinenav.h - the player's online navigator (BO-352, step 2): Quick Play,
 * Create game and Join with code, driven through the title's own menus.
 *
 * Unlike onlinebot.c (a debug bot that pushes its presses through the script
 * pad queue), this navigator runs for a real player: it writes its press into
 * the menu player's XINPUT_STATE at the title's pad rewrite point (ctl_pad,
 * controls.c) and clears every other button on that pad except B, so the
 * player's own presses do not mix in and B still cancels. When B is pressed,
 * or the flow cannot go on, it stops and hands the pad back.
 *
 * The pure decisions (which intent a switch names, when a Quick Play game
 * starts by itself, what each screen gets) are split out so the unit test can
 * run them without the title's memory; onlinenav.c holds them and the glue
 * (the screen read, the pad write, the per-flip tick).
 */
#ifndef BO3_ONLINENAV_H
#define BO3_ONLINENAV_H

#include <stddef.h>
#include <stdint.h>

struct XBOX_GAMEPAD;            /* xinput_xbox.h; forward-declared so the
                                   pure decisions stay free of the toolkit */

/* the intents the navigator can start with */
enum {
    NAV_QUICK = 1,          /* Quick Play, stepped through the menus by pad presses: only the debug
                               switch TAKEDAWN_ONLINENAV=quick starts it now; the QUICK PLAY line runs
                               the title's own Quick Match (quickplay.c, no presses) */
    NAV_CREATE,             /* Create game: drive to the mode page and stop */
    NAV_JOIN                /* Join with code: set the code, then Quick Play */
};

/* start the navigator with an intent (the menu entries in step 3 call this;
 * the debug switch bo3_onlinenav_start parses one too). code: JOIN's code,
 * or NULL. Returns 0 when it could not start (already running, or offline). */
int bo3_onlinenav_begin(int intent, const char *code);
int bo3_onlinenav_active(void);
/* Keep a cancel press consumed through release when Quick Play is still on
 * its starting page. Return whether this pad sample must be suppressed. */
int bo3_onlinenav_cancel_latch(int *latched, int pressed);

/* at start: reads RECOMP_DEBUG=onlinenav=<intent> (nothing more when off) */
void bo3_onlinenav_start(void);
/* every flip the title emits (main.c's uw_flip) */
void bo3_onlinenav_flip(uint32_t flip);
/* the pad rewrite point (controls.c chains this in at ctl_pad): writes the
 * navigator's press, clears every other button except B */
void bo3_onlinenav_pad(uint32_t port, struct XBOX_GAMEPAD *gamepad);

/* "quick"|"create"|"join" (optionally "join=XXXXX" / "code=XXXXX"): the
 * intent and, for JOIN, the five-letter code. 0 when none matched. */
int bo3_onlinenav_parse_intent(const char *v, int *intent, char *code, size_t n);

/* ---- the five-letter game code (step 4) ---------------------------------
 *
 * The picker page is five lines, one per character; left and right step the
 * character, up and down move between lines. The alphabet is A-Z then 0-9.
 * The pure model is here so tests/onlinenav can run it; the picker page
 * itself (a second mode on the Xbox Live page object) needs step 3's lines. */

/* the alphabet's size (A-Z, 0-9) */
#define NAV_CODE_CHARS 36
/* the alphabet, NAV_CODE_CHARS long, NUL-terminated */
extern const char bo3_onlinenav_code_chars[NAV_CODE_CHARS + 1];
/* a character's place in the alphabet (-1 when not in it; lower case folds) */
int bo3_onlinenav_code_norm(char c);
/* step code[pos] by dir (+1 right/next, -1 left/prev), wrapping; a blank
 * starts at 'A' */
void bo3_onlinenav_code_step(char *code, int pos, int dir);
/* 1 when all five characters are in the alphabet */
int bo3_onlinenav_code_complete(const char *code);

/* ---- Quick Play's own start countdown (decision 3) ---------------------
 *
 * A Quick Play game starts by itself: at once when the lobby is full (6/6
 * ready), else 30 s after the second player is ready. The decision is pure
 * so the unit test can run it. */
enum { NAV_QS_WAIT, NAV_QS_COUNTDOWN, NAV_QS_START };

typedef struct {
    int np;                 /* players in the lobby's record */
    int ready[8];           /* their ready values (1 and 3 = a car chosen) */
    const char *me;         /* this copy's gamertag (the host) */
    int maxsize;            /* 6 */
    unsigned long long now;             /* monotonic ms */
    unsigned long long second_ready_at; /* ms the second player became ready (0: none) */
} nav_quick_in;

/* NAV_QS_WAIT (still short of two players), NAV_QS_COUNTDOWN (*seconds set
 * to the seconds left in the 30 s) or NAV_QS_START (press Start now). */
int bo3_onlinenav_quick_start(const nav_quick_in *in, char *why, size_t n, int *seconds);

/* the "STARTING IN N S" line the status builder shows (lane B, step 5a); the
 * receiver shows it from the countdown over the side channel. */
void bo3_onlinenav_countdown_text(char *out, size_t n, int seconds);

#endif
