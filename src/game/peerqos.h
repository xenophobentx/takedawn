/*
 * peerqos.h - what the title's XNetQos block says about a peer (BO-357).
 *
 * The online layer measures the round trip to each peer (xnet_route.h, the
 * TKPG ping). The title reads wRttMed only (0x226740: 0 becomes 1, 0xFFFF
 * becomes "no answer"), through the lobby library's ping manager. Before the
 * first answer, or from an older copy that cannot answer, the block keeps the
 * fixed values it always had: a player is never shown as unreachable for
 * lack of a measurement. The lobby's bars do not come from here: they read the
 * title's own ping (peer + 0x5D8).
 */
#ifndef BO3_PEERQOS_H
#define BO3_PEERQOS_H

#define BO3_QOS_RTT_MIN_DEFAULT 20u
#define BO3_QOS_RTT_MED_DEFAULT 30u
#define BO3_QOS_PROBES_DEFAULT  4u

typedef struct {
    unsigned short rtt_min, rtt_med;
    unsigned char xmit, recv;
} bo3_qos_values;

static inline unsigned short bo3_qos_ms(unsigned ms)
{
    if (ms < 1u)
        return 1u;
    return ms > 0xFFFEu ? 0xFFFEu : (unsigned short)ms;
}

/* have: the layer has a measurement (min_ms, med_ms, of sent pings lost) */
static inline bo3_qos_values bo3_qos_fill(int have, unsigned min_ms, unsigned med_ms, int sent, int lost)
{
    bo3_qos_values v;
    if (!have) {
        v.rtt_min = BO3_QOS_RTT_MIN_DEFAULT;
        v.rtt_med = BO3_QOS_RTT_MED_DEFAULT;
        v.xmit = v.recv = BO3_QOS_PROBES_DEFAULT;
        return v;
    }
    if (sent < 1)
        sent = 1;
    if (sent > 255)
        sent = 255;
    if (lost < 0)
        lost = 0;
    if (lost > sent)
        lost = sent;
    v.rtt_min = bo3_qos_ms(min_ms);
    v.rtt_med = bo3_qos_ms(med_ms);
    v.xmit = (unsigned char)sent;
    v.recv = (unsigned char)(sent - lost);
    return v;
}

/* The XNQOS blocks handed to the title (XNetQosLookup, XNetQosServiceLookup)
 * come from its heap; XNetQosRelease gives one back. Only blocks we handed
 * out and have not taken back are freed, so a stray or repeated release frees
 * nothing. A full table keeps the block untracked (it is never freed). */
#define BO3_QOS_KEEP 64
typedef struct {
    unsigned blk[BO3_QOS_KEEP];
} bo3_qos_keep;

/* 1: kept; 0: table full or no block */
static inline int bo3_qos_keep_add(bo3_qos_keep *k, unsigned p)
{
    int i;
    if (!p)
        return 0;
    for (i = 0; i < BO3_QOS_KEEP; i++)
        if (!k->blk[i]) {
            k->blk[i] = p;
            return 1;
        }
    return 0;
}

/* 1: p was ours and is no longer kept: free it now */
static inline int bo3_qos_keep_take(bo3_qos_keep *k, unsigned p)
{
    int i;
    if (!p)
        return 0;
    for (i = 0; i < BO3_QOS_KEEP; i++)
        if (k->blk[i] == p) {
            k->blk[i] = 0;
            return 1;
        }
    return 0;
}

/* A lookup's result: 0 with a block; WSAENOBUFS (the XNet error for "out of
 * memory") and a NULL block when the heap had none. Never 0 with NULL: the
 * title reads the block on success. */
#define BO3_QOS_ENOBUFS 10055u
static inline unsigned bo3_qos_lookup_ret(unsigned blk)
{
    return blk ? 0u : BO3_QOS_ENOBUFS;
}

#endif
