/*
 * peertask.h - the peer lookup tasks of the online layer (online.c, the
 * title's 0x31AB16 "find a player by XUID"), apart from the title's memory so
 * a unit test can run them (tests/peertask, BO-344).
 *
 * The title opens one task per lookup and reads the result (XNADDR, session
 * id and key) from its own block once XOnlineTaskContinue says done. After a
 * lost peer link it looks the peer up again about 5 times a second and never
 * closes those tasks. The old table had 32 slots handed out by index: full,
 * every lookup took slot 0, lookups at the same time overwrote each other's
 * XUID and block, one Close freed slot 0 for all of them, and the others'
 * Continue found no task ("unknown task -> S_OK", nothing filled in). The
 * copy then got wrong or no peer addresses for the rest of its run (int035
 * long run: Mac1 and Mac2 from game 20, the host from game 25).
 *
 * Now each lookup gets its own handle (a running count, never reused while
 * a lookup that recent can still be open) and online.c fills the block at the
 * lookup itself, so a Continue has nothing left to lose: a task too old to be
 * in the ring is done all the same. Close frees only its own task.
 */
#ifndef BO3_PEERTASK_H
#define BO3_PEERTASK_H

#include <stdint.h>
#include <string.h>

#define BO3_PEERTASK_BASE 0xF2000000u
#define BO3_PEERTASK_RING 64        /* the newest lookups kept for Continue */

typedef struct {
    uint32_t handle;                /* 0: free */
    uint32_t out;                   /* the title's result block */
    uint8_t xuid[8];
} bo3_peertask;

typedef struct {
    bo3_peertask t[BO3_PEERTASK_RING];
    uint32_t seq;                   /* lookups so far */
} bo3_peertasks;

/* a handle of ours: 0xF2nnnnnn */
static inline int bo3_peertask_is(uint32_t h)
{
    return (h & 0xFF000000u) == BO3_PEERTASK_BASE;
}

/* A new lookup: its handle. The ring slot it takes held the lookup
 * BO3_PEERTASK_RING before it, closed or not. */
static inline uint32_t bo3_peertask_open(bo3_peertasks *p, const uint8_t xuid[8], uint32_t out)
{
    bo3_peertask *t;
    uint32_t n;
    p->seq = (p->seq + 1) & 0x00FFFFFFu;
    if (!p->seq)
        p->seq = 1;                 /* never handle 0xF2000000: 0 is "free" below */
    n = p->seq;
    t = &p->t[n % BO3_PEERTASK_RING];
    t->handle = BO3_PEERTASK_BASE | n;
    t->out = out;
    memcpy(t->xuid, xuid, 8);
    return t->handle;
}

/* The lookup behind a handle, or NULL when it is closed or too old (its
 * block was filled at the lookup: done all the same). */
static inline const bo3_peertask *bo3_peertask_find(const bo3_peertasks *p, uint32_t h)
{
    const bo3_peertask *t;
    if (!bo3_peertask_is(h) || !(h & 0x00FFFFFFu))
        return NULL;
    t = &p->t[(h & 0x00FFFFFFu) % BO3_PEERTASK_RING];
    return t->handle == h ? t : NULL;
}

static inline void bo3_peertask_close(bo3_peertasks *p, uint32_t h)
{
    bo3_peertask *t;
    if (!bo3_peertask_is(h) || !(h & 0x00FFFFFFu))
        return;
    t = &p->t[(h & 0x00FFFFFFu) % BO3_PEERTASK_RING];
    if (t->handle == h)
        t->handle = 0;
}

#endif
