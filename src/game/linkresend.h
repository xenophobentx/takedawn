/*
 * linkresend.h - send each peer our link report again every half second
 * while the online lobby screen runs, apart from the title's memory so a
 * unit test can run it (tests/linkresend, BO-344).
 *
 * The title sends a peer its link report (channel 9, 80 bytes: the id and
 * link state of every player in our table) from 0x102610, which 0xA05B0
 * calls for every peer in every lobby frame. It sends only when the table
 * differs from the one it last queued for that peer (peer +0x540 ids, +0x560
 * states), only when the peer's slot is free (+0xC80 bit 0) and our link to
 * that peer is up (+0x4F0 == 3), and it copies the table to +0x540/+0x560
 * the moment it queues it. The title itself never repeats it. It goes out as
 * a reliable message (slot +0xC82 = 1): 0x1B6E10 copies it into the session's
 * one resend queue (session = [peer + 0xC], 32 entries of 0x14 bytes at
 * session + 0xA0: +0 the peer's id, +0xC the datagram, +0x10 done), which
 * sends it again every 10 ticks until the peer acknowledges it (0x1B84E0).
 * When all 32 entries wait for an acknowledgement, 0x1B80C0 drops every new
 * reliable message, to every peer, without a word.
 *
 * So a player that has been in the game a while sends a newcomer exactly one
 * report, in the frame its link to the newcomer comes up. If that one
 * datagram is lost, the newcomer never has the report, its own linked flag
 * (0xA05B0, 0x102730: every peer's report must list us as up) stays 0, and
 * it cannot leave "the game is about to launch" (0x9F545) when the host
 * starts (int036 soak: games 29, 30, 31, the host's report always missing).
 *
 * online.c marks the last-queued table of every peer whose link is up and
 * whose slot is free as stale, before 0xA05B0 runs, every BO3_LR_MS. The
 * title then sends the same report again that frame, through its own code.
 * A report carries the whole table, so a repeat is harmless: the receiver
 * just copies it again.
 *
 * BO-379: a repeat is only made while no earlier report to that peer still
 * waits for its acknowledgement in the resend queue. Before that, a peer that
 * still hears us but whose answers stopped reaching us (night056 F2: a Mac
 * that lost its LAN route mid-lobby and fell back to the router's public
 * address) gained two never-acknowledged entries a second while its link
 * stayed up, filled the queue in 15 s, and from then on the title's reliable
 * messages to everyone (the race start, the results) were lost: two players
 * sat at the start line on WAITING FOR OTHER PLAYERS for good. The title's
 * own resend of the waiting report still runs, so nothing is lost. A peer
 * that left the game is no longer in the table, so it gets no report at all.
 */
#ifndef BO3_LINKRESEND_H
#define BO3_LINKRESEND_H

#include <stdint.h>

#define BO3_LR_MS 500u                 /* how often every report goes out again */
#define BO3_LR_STALE 0xFFFFFFFEu       /* never a player's id; -1 is an empty entry */

typedef struct {
    uint64_t next;                     /* when the reports are due again (0 = now) */
} bo3_linkresend;

/* One lobby frame at now (a monotonic ms clock): 1 when the reports are due
 * again, then not for BO3_LR_MS. A clock that went back starts over. */
static inline int bo3_linkresend_due(bo3_linkresend *r, uint64_t now)
{
    if (r->next && now < r->next && r->next - now <= BO3_LR_MS)
        return 0;
    r->next = now + BO3_LR_MS;
    return 1;
}

/* Whether to mark a peer's last-queued report stale: only a peer whose link
 * is up (+0x4F0 == 3) and whose slot is free (+0xC80 bit 0 clear). A busy
 * slot sends the newest table anyway once it is free (0x102610 leaves the
 * last-queued copy alone while it waits). */
static inline int bo3_linkresend_peer(uint32_t link, uint8_t slot)
{
    return link == 3 && !(slot & 1);
}

/* The title's resend queue (0x1B80C0): its 32 entries, as online.c reads them */
#define BO3_LR_QUEUE 32
#define BO3_LR_CHANNEL 9               /* the link report */

typedef struct {
    uint32_t dest;                     /* +0: the peer's id */
    uint8_t chan;                      /* the datagram's byte 3: its channel */
    uint8_t done;                      /* +0x10: acknowledged (or never used) */
} bo3_lr_entry;

/* How many entries still wait for dest's acknowledgement on channel chan */
static inline int bo3_linkresend_waiting(const bo3_lr_entry *q, int n, uint32_t dest, uint8_t chan)
{
    int i, w = 0;
    for (i = 0; i < n; i++)
        if (!q[i].done && q[i].dest == dest && q[i].chan == chan)
            w++;
    return w;
}

/* How many entries still wait for a peer that is not in ids[0..nids): a
 * player who left the game. Nothing acknowledges those; the title frees
 * them only when the next game sets the session up again. */
static inline int bo3_linkresend_gone(const bo3_lr_entry *q, int n, const uint32_t *ids, int nids)
{
    int i, k, w = 0;
    for (i = 0; i < n; i++) {
        if (q[i].done)
            continue;
        for (k = 0; k < nids && ids[k] != q[i].dest; k++)
            ;
        w += k == nids;
    }
    return w;
}

#endif
