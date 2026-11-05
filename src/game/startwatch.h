/*
 * startwatch.h - the online lobby screen stuck in "the game is about to
 * launch" (state 0xB), apart from the title's memory so a unit test can run
 * it (tests/startwatch, BO-344). online.c feeds it the lobby screen object and
 * its state byte every lobby frame (0xA05B0) and does what it says.
 *
 * State 0xB (0x9F545) moves to the race only when every peer is linked and
 * has reported its links (+0x13A4, 0xA05B0 and 0x102730), the peer table has
 * been rebuilt from the game record (0x572985, 0xFD6B0 -> 0x1B86A0) and no
 * lobby request is open (0x57BCFC vt[3] == 2). It has no timer: a guest that
 * misses one of them stays there for good, and its peer links stay up, so
 * the others wait for it at their results with no end (soak034 game 6).
 * Every copy that starts normally leaves 0xB in the frame it enters it.
 */
#ifndef BO3_STARTWATCH_H
#define BO3_STARTWATCH_H

#include <stdint.h>

#define BO3_SW_REPORT_MS 2000    /* say what 0xB still waits for */
#define BO3_SW_LEAVE_MS 10000    /* then leave the game the title's own way */

enum { BO3_SW_NONE = 0, BO3_SW_REPORT, BO3_SW_LEAVE };

typedef struct {
    uint32_t scr;          /* the lobby screen object being watched */
    uint64_t since;        /* when it entered 0xB (ms + 1; 0 = not in 0xB) */
    int said, leave;       /* the report given, the leave asked for */
} bo3_startwatch;

/* One lobby frame: scr the screen object, state its state byte (+0x13AC),
 * now a monotonic ms clock. BO3_SW_REPORT once after 2 s in 0xB,
 * BO3_SW_LEAVE once after 10 s, else BO3_SW_NONE. Another screen object, or
 * any other state, starts the count over. */
static inline int bo3_startwatch_step(bo3_startwatch *w, uint32_t scr, int state, uint64_t now)
{
    if (scr != w->scr || state != 0x0B) {
        w->scr = scr;
        w->since = 0;
        w->said = w->leave = 0;
        if (state != 0x0B)
            return BO3_SW_NONE;
    }
    if (!w->since) {
        w->since = now + 1;
        return BO3_SW_NONE;
    }
    if (!w->leave && now + 1 - w->since >= BO3_SW_LEAVE_MS) {
        w->leave = w->said = 1;
        return BO3_SW_LEAVE;
    }
    if (!w->said && now + 1 - w->since >= BO3_SW_REPORT_MS) {
        w->said = 1;
        return BO3_SW_REPORT;
    }
    return BO3_SW_NONE;
}

#endif
