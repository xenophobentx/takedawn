/*
 * react.h - the quick reactions (BO-352, step 7): five fixed phrases a player
 * sends on the d-pad (LT + d-pad in the game lobby, the d-pad alone on the
 * results screen) or the keys 1..5, carried on the toolkit's TKRX side
 * channel (xnet_hle.h, xnet_route.h) and shown at the sender's name.
 *
 * The phrases and the d-pad mapping are pure here (tests/react), so they can
 * be checked on every host; react.c reads the pad at ctl_pad, sends the
 * packet and shows what arrives. English first (decision 6); step 9 localises
 * the phrases through the language table.
 *
 * The reaction ids (0..4) are the wire format (xnet_route.h XR_REACT_N):
 *   0 LET'S GO!, 1 GG, 2 SORRY!, 3 ONE MORE?, 4 NICE TAKEDOWN!
 * The d-pad has four directions, so the lobby and the results screen map them
 * differently (decision 5):
 *   lobby:   up LET'S GO!, right GG, left SORRY!, down ONE MORE?
 *   results: up GG, right NICE TAKEDOWN!, left SORRY!, down ONE MORE?
 * The keys 1..5 are ids 0..4 in order.
 */
#ifndef BO3_REACT_H
#define BO3_REACT_H

#include <string.h>

#include "onlinebot.h"   /* OB_SC_LOBBY, OB_SC_RESULTS */
#include "xnet_hle.h"    /* XR_REACT_N, the TKRX channel */

/* id 0..4 -> its English phrase, "" past the table */
static inline const char *bo3_react_phrase(int id)
{
    static const char *const p[XR_REACT_N] = {
        "LET'S GO!", "GG", "SORRY!", "ONE MORE?", "NICE TAKEDOWN!"
    };
    return id >= 0 && id < XR_REACT_N ? p[id] : "";
}

/* the d-pad direction (0 up, 1 right, 2 left, 3 down) to a reaction id on
 * the lobby or the results screen, or -1 (not a direction, or not one of the
 * two screens) */
static inline int bo3_react_dpad_id(int screen, int dir)
{
    static const int8_t lobby[4]   = { 0, 1, 2, 3 };   /* up, right, left, down */
    static const int8_t results[4] = { 1, 4, 2, 3 };
    if (dir < 0 || dir > 3)
        return -1;
    if (screen == OB_SC_LOBBY)
        return lobby[dir];
    if (screen == OB_SC_RESULTS)
        return results[dir];
    return -1;
}

/* reactions take input on the game lobby and the results screen */
static inline int bo3_react_active_screen(int screen)
{
    return screen == OB_SC_LOBBY || screen == OB_SC_RESULTS;
}

/* online.reactions ("on"/""/"1" -> 1, "off"/"0" -> 0; unset -> on) */
static inline int bo3_react_setting_on(const char *v)
{
    return !v || !*v || !strcmp(v, "on") || !strcmp(v, "1");
}

/* a-z -> A-Z, drop anything the title's upper-case Latin font has not (in
 * place), so a reaction text is upper-case ASCII for the ticker */
static inline void bo3_react_upper_ascii(char *s)
{
    char *o = s;
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c >= 'a' && c <= 'z')
            c = (unsigned char)(c - ('a' - 'A'));
        else if (c < 0x20 || c >= 0x7F)
            continue;
        *o++ = (char)c;
    }
    *o = 0;
}

/* at start (main.c): reads the setting */
void bo3_react_start(void);
/* controls.c's ctl_pad on the menu pad: takes a reaction press (LT + d-pad in
 * the lobby, the d-pad alone on results, keys 1..5), sends it, and clears the
 * press from *g so the title never sees it. Returns 1 when it took a press. */
int  bo3_react_pad(void *g);
/* online.c's lobby_watch, on the title's thread each online frame: sends the
 * due second copy and drains a received reaction into the display */
void bo3_react_tick(void);

/* The newest received reaction as "NAME: PHRASE" for 3 s (the ticker's
 * fallback): copies it (upper-case ASCII) into out and returns 1 while one
 * is active, else 0. Lane B's lobby_line (online.c) shows it in the lobby's top line. */
int  bo3_react_ticker_text(char *out, int n);

/* The bubble (online.reactions_bubble, off until scene-tested): one lobby
 * row's name pointer swapped for our UTF-16 text, then put back. Pure
 * bookkeeping (tests/react); react.c drives it each lobby frame. */
typedef struct {
    int row;            /* the row being swapped, -1 none */
    uint32_t orig;      /* the title's pointer to put back */
    uint64_t until;     /* ms clock; 0 = none */
} bo3_react_bubble;

/* start (or replace) a bubble for `row`, remembering `orig` to restore */
static inline void bo3_react_bubble_begin(bo3_react_bubble *b, int row, uint32_t orig, uint64_t until)
{
    b->row = row;
    b->orig = orig;
    b->until = until;
}

/* what this frame does: SWAP (keep our pointer in *row), RESTORE (put *orig
 * back in *row; the bubble ends), or NONE */
enum { BO3_REACT_BUBBLE_SWAP = 1, BO3_REACT_BUBBLE_RESTORE = 2, BO3_REACT_BUBBLE_NONE = 0 };
static inline int bo3_react_bubble_step(bo3_react_bubble *b, uint64_t now, int *row, uint32_t *orig)
{
    if (b->row < 0 || !b->until)
        return BO3_REACT_BUBBLE_NONE;
    if (row) *row = b->row;
    if (orig) *orig = b->orig;
    if (now < b->until)
        return BO3_REACT_BUBBLE_SWAP;
    b->row = -1;
    b->until = 0;
    return BO3_REACT_BUBBLE_RESTORE;
}

/* online.c's lobby screen hook (0xA05B0): resolves a pending reaction into a
 * bubble and re-applies the swap/restore each lobby frame */
void bo3_react_lobby_frame(uint32_t scr);

#endif /* BO3_REACT_H */
