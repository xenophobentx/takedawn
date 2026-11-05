/*
 * linkstate.h - follow each peer's link state, log every change, and give a
 * link that is stuck in the lobby another try. Apart from the title's
 * memory so a unit test can run it (tests/linkstate, BO-345).
 *
 * The title's link to a peer is the state at peer +0x4F0, run by 0xFD830
 * every frame, one peer at a time:
 *   0  look the peer's address up (0x1BD970) and start a probe (0x214840): 1
 *   1  the probe is done (0x214910): 2, and queue our 16-byte link message
 *      on channel 6 (slot +0x5C0, registered at 0x1008A9); a failed probe: 4
 *   2  the peer's answer (0xFD7D0, only from state 2): 3, up (+0x4B0 = 1)
 *   2  still 2 a while after the last probe finished (0xFDA12): 4
 *   4  given up: nothing in 0xFD830 takes it back to 0
 * The game's QoS stub answers at once, so 0 to 2 takes a frame or two. Then
 * the title gives the link exactly one handshake. In the int039 soak (game
 * 83) the host's links to two players that joined within a second of the
 * game's creation never got past it, and the host kicked them after 20 s.
 *
 * bo3_linkstate_step() follows one table entry and says when its state
 * changed, for an always-on [NET] line: at most BO3_LS_LINES per peer per
 * game, so a link that keeps changing cannot flood the log.
 *
 * bo3_linkstate_retry() says when to put a stuck link back to 0, so 0xFD830
 * runs its own lookup, probe and link message again: state 2 for
 * BO3_LS_STUCK_MS, or state 4, at most BO3_LS_RETRIES times per peer per
 * game. online.c asks it only while the lobby screen runs (0xA05B0), never
 * in a race, and logs every retry. A repeated link message is harmless to
 * the other side: 0xFD7D0 acts only on a link in state 2.
 */
#ifndef BO3_LINKSTATE_H
#define BO3_LINKSTATE_H

#include <stdint.h>

#define BO3_LS_STUCK_MS 1500u          /* state 2 this long: try again */
#define BO3_LS_RETRIES 3u              /* tries per peer per game */
#define BO3_LS_LINES 16u               /* state lines per peer per game */
#define BO3_LS_NONE 0xFFFFFFFFu        /* no state seen yet */

typedef struct {
    uint32_t peer;                     /* the peer object, 0 = empty entry */
    uint32_t id;                       /* its player id (+8) */
    uint32_t state;                    /* the last state seen, BO3_LS_NONE at first */
    uint64_t appeared;                 /* when this peer object showed up */
    uint64_t since;                    /* when it got its current state */
    uint32_t lines;                    /* state lines logged this game */
    uint32_t retries;                  /* retries given this game */
} bo3_linkstate;

/* What one look at an entry found. */
enum { BO3_LS_SAME = 0, BO3_LS_CHANGED = 1, BO3_LS_QUIET = 2 };

/* One look at table entry e, holding peer object P (0 = empty: the entry is
 * cleared) with player id and link state, at now (a monotonic ms clock). A
 * new object or id starts the entry over. BO3_LS_CHANGED: log *from ->
 * state (*from is BO3_LS_NONE for the first state seen); when e->lines has
 * just reached BO3_LS_LINES that was the last line for this peer this game.
 * BO3_LS_QUIET: it changed, but the lines are used up. */
static inline int bo3_linkstate_step(bo3_linkstate *e, uint32_t P, uint32_t id, uint32_t state,
                                     uint64_t now, uint32_t *from)
{
    if (!P) {
        e->peer = 0;
        return BO3_LS_SAME;
    }
    if (P != e->peer || id != e->id) {
        e->peer = P;
        e->id = id;
        e->state = BO3_LS_NONE;
        e->appeared = now;
        e->since = now;
        e->lines = 0;
        e->retries = 0;
    }
    if (state == e->state)
        return BO3_LS_SAME;
    *from = e->state;
    e->state = state;
    e->since = now;
    if (e->lines >= BO3_LS_LINES)
        return BO3_LS_QUIET;
    e->lines++;
    return BO3_LS_CHANGED;
}

/* Whether to put this entry's link back to 0 now, in a lobby frame: state 2
 * for BO3_LS_STUCK_MS or state 4, and tries left. Counts the try and notes
 * state 0 as seen, so the next step logs only what the title does next. A
 * clock that went back counts from now. */
static inline int bo3_linkstate_retry(bo3_linkstate *e, uint64_t now)
{
    if (!e->peer || e->retries >= BO3_LS_RETRIES)
        return 0;
    if (now < e->since)
        e->since = now;
    if (e->state == 4 || (e->state == 2 && now - e->since >= BO3_LS_STUCK_MS)) {
        e->retries++;
        e->state = 0;
        e->since = now;
        return 1;
    }
    return 0;
}

#endif
