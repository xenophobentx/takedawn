/*
 * online.c - Burnout 3's XNet and XOnline on the toolkit's host network layer
 * (BO-134).
 *
 * Burnout 3 links the XDK's XNet and XOnline statically. Every public API is
 * an 11-byte stub in the XNET (0x3391E0..) or XONLINE (0x31AA80..) section
 * that loads the library object and jumps into the library; the title's own
 * code (DirtySock's NetConn and LobbyApi, the BuddyApi, system link) calls
 * only those. recomp_manual.c wraps each stub with bo3_online_stub(): with
 * TAKEDAWN_ONLINE=1 the call is answered here, from the toolkit's xnet_hle
 * (host sockets, a cable, an address, an XNADDR) and a stand-in XOnline that
 * reports one signed-in user and points every service at the lobby server
 * (TAKEDAWN_LOBBY). Otherwise the title's library runs as shipped (no cable,
 * BO-164).
 *
 * Names come from the arguments each stub pops, the WSA errors its body
 * sets, and how NetConn uses the results (BO-134 notes). NTSC-U addresses.
 *
 * The XOnline stand-in signs in without a server: the Kerberos sign-in to
 * Xbox Live cannot be done, and the title only needs the outcome (a user
 * with hr S_OK and a service address). The Aries lobby that the title talks
 * to over that address is a separate program (tools/lobby).
 */
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#endif

#include "online.h"
#include "react.h"                 /* the quick reactions (BO-352) */
#include "quickplay.h"             /* the host's start countdown */
#include "vote.h"                  /* the track vote */
#include "stay.h"                  /* stay together: the server's keys on the lobby stream */
#include "../../tools/lobby/aries_server.h"
#include "recomp_types.h"
#include "xnet_hle.h"
#include "xnet_lobby.h"
#include "region.h"
#include "race_module.h"
#include "startwatch.h"
#include "peertask.h"
#include "linkresend.h"
#include "peerqos.h"
#include "linkstate.h"
#include "unreach.h"
#include "lobbystatus.h"
#include "xboxlive.h"               /* CREATE GAME: a public game with a code */
#include "languages.h"

extern void BO3_SUB(sub_001D1953, sub_001D1C73, sub_001D1D13)(void);   /* the title's SetLastError (stdcall, 1 arg) */
extern void BO3_SUB(sub_001D2879, sub_001D2B99, sub_001D2C39)(void);   /* the title's heap: (size, tag), stdcall */
extern void BO3_SUB(sub_001D2919, sub_001D2C39, sub_001D2CD9)(void);   /* its free: (block, tag), stdcall (XMemFree) */
extern void BO3_SUB(sub_001BAF70, sub_001BB290, sub_001BB330)(void);   /* the session's "leave the game" (thiscall, no args) */

#define ARG(n)    MEM32(g_esp + 4u + 4u * (n))
#define HP(va)    ((void *)XBOX_PTR(va))
#define HEAP_TAG  0x54454E58u     /* "XNET" */
/* BO-352: the game lobby's top message line ("Welcome to game NAME" when the
 * lobby opens). LINE_BUF is its UTF-16 buffer, one of the front end's static
 * text slots; LINE_DESC is the slot's text pointer in the line's draw entry.
 * The line is drawn from the buffer every frame (a write shows on the next
 * frame), and the next slot starts 0x80 bytes on, so 63 units and the null. */
#define LINE_BUF   BO3_VA(0x00558436u, 0x00558756u, 0x005587F6u)
#define LINE_DESC  BO3_VA(0x00554878u, 0x00554B98u, 0x00554C38u)
#define LINE_UNITS 64u

#define S_OK      0u
#define E_FAIL    0x80004005u
#define XONLINE_S_LOGON_CONNECTION_ESTABLISHED 0x001510F0u   /* region: not an address (an HRESULT) */

#define LOGON_TASK  0xF1000001u
#define USER_SIZE   0x70u         /* XONLINE_USER as the title strides it */

static void ret(uint32_t eax, uint32_t argbytes)
{
    g_eax = eax;
    g_esp += 4u + argbytes;
}

static void set_wsa_error(int e)
{
    uint32_t a = g_eax, c = g_ecx, d = g_edx, sp = g_esp;
    PUSH32(g_esp, (uint32_t)e);
    PUSH32(g_esp, 0);
    BO3_SUB(sub_001D1953, sub_001D1C73, sub_001D1D13)();
    g_eax = a; g_ecx = c; g_edx = d; g_esp = sp;
}

static uint32_t title_alloc(uint32_t size)
{
    uint32_t a = g_eax, c = g_ecx, d = g_edx, sp = g_esp, p;
    PUSH32(g_esp, HEAP_TAG);
    PUSH32(g_esp, size);
    PUSH32(g_esp, 0);
    BO3_SUB(sub_001D2879, sub_001D2B99, sub_001D2C39)();
    p = g_eax;
    g_eax = a; g_ecx = c; g_edx = d; g_esp = sp;
    if (p)
        memset(HP(p), 0, size);
    return p;
}

/* A block from title_alloc back to the title's heap, with the same tag: the
 * free tests the tag's top bit as the alloc does (0x1D2919: heap or physical) */
static void title_free(uint32_t p)
{
    uint32_t a = g_eax, c = g_ecx, d = g_edx, sp = g_esp;
    if (!p)
        return;
    PUSH32(g_esp, HEAP_TAG);
    PUSH32(g_esp, p);
    PUSH32(g_esp, 0);
    BO3_SUB(sub_001D2919, sub_001D2C39, sub_001D2CD9)();
    g_eax = a; g_ecx = c; g_edx = d; g_esp = sp;
}

/* A Winsock result: SOCKET_ERROR / INVALID_SOCKET also set the error. */
static void wsa_ret(uint32_t v, int werr, uint32_t argbytes)
{
    if (werr)
        set_wsa_error(werr);
    ret(v, argbytes);
}

static void fill_user(uint32_t va, uint32_t index)
{
    uint8_t *u = (uint8_t *)HP(va);
    uint32_t ip = xnet_hle_local_ip();
    const char *tag = xnet_hle_gamertag();
    memset(u, 0, USER_SIZE);
    /* xuid: 16 bits from the name, this copy's port offset (little-endian),
     * its address. A peer reaches it by the last two (xnet_route.h). */
    {
        uint32_t h = 2166136261u, i;
        uint16_t off = xnet_hle_port_offset();
        for (i = 0; tag[i]; i++)
            h = (h ^ (uint8_t)tag[i]) * 16777619u;
        u[0] = (uint8_t)h;
        u[1] = (uint8_t)(h >> 8);
        u[2] = (uint8_t)off;
        u[3] = (uint8_t)(off >> 8);
        memcpy(u + 4, &ip, 4);
        u[7] = (uint8_t)(u[7] ^ index);
    }
    strncpy((char *)u + 0x0C, tag, 15);
    memcpy(u + 0x24, "xbox.com", 8);          /* domain */
    memcpy(u + 0x38, "PASSPORT.NET", 12);     /* realm */
}

static uint32_t s_logon_users;   /* title heap: 4 users */

/* Peer lookups (0x31AB16): a player's XNADDR, session id and key by XUID,
 * as a task whose result lands in the caller's block (0x214910 reads the
 * XNADDR at +0x18, the XNKID at +0x3C, the key at +0x44 once
 * XOnlineTaskContinue says 0x001500F0). Our XUIDs carry the player's
 * address in bytes 4..7 (fill_user), so the lookup needs no server. */
static bo3_peertasks s_peer;   /* peertask.h (BO-344: one handle per lookup, no shared slot) */

/* the result into the title's block: the XNADDR at +0x18, the XNKID at
 * +0x3C and the key at +0x44 (one per pair is enough: from the address) */
static void peer_result(uint32_t out, const uint8_t xuid[8])
{
    uint32_t ina;
    if (!out)
        return;
    memcpy(&ina, xuid + 4, 4);
    xnet_hle_make_xnaddr(HP(out + 0x18), ina);
    memset(HP(out + 0x3C), 0, 8 + 16);
    memcpy(HP(out + 0x3C), &ina, 4);
    memcpy(HP(out + 0x44), &ina, 4);
}

/* Startups not yet cleaned up (XOnlineStartup, XNetStartup, WSAStartup).
 * The last cleanup closes every socket still open, as Winsock does at its
 * last WSACleanup; the stubs used to close nothing, so a title that leaves
 * sockets open for its cleanup to close filled the 64-slot table. Without
 * the toolkit's xnet_hle_close_all (before the batch B toolkit) the count is
 * kept and nothing is closed, as before. */
static int s_net_refs;

static void net_startup(const char *what)
{
    s_net_refs++;
    xnet_hle_log("%s (%d open)", what, s_net_refs);
}

static void net_cleanup(const char *what)
{
    if (s_net_refs <= 0) {
        xnet_hle_log("%s with no startup open", what);
        return;
    }
    if (--s_net_refs)
        return;
#ifdef XNET_HLE_HAS_CLOSE_ALL
    {
        int n = xnet_hle_close_all();
        xnet_hle_log("%s: the last cleanup, %d socket(s) closed, %d open", what, n, xnet_hle_socket_count());
    }
#else
    xnet_hle_log("%s: the last cleanup (this toolkit closes nothing)", what);
#endif
}

static int xonline(uint32_t va)
{
    uint32_t i;
    switch (va) {
    case BO3_VA(0x0031AF83u, 0x0031B283u, 0x0031B323u):            /* XOnlineStartup(params) */
        net_startup("XOnlineStartup");
        ret(S_OK, 4);
        return 1;
    case BO3_VA(0x0031B080u, 0x0031B380u, 0x0031B420u):            /* XOnlineCleanup() */
        net_cleanup("XOnlineCleanup");
        ret(S_OK, 0);
        return 1;
    case BO3_VA(0x0031AA96u, 0x0031AD96u, 0x0031AE36u): {          /* XOnlineGetUsers(pUsers, pcUsers) */
        uint32_t users = ARG(0), pc = ARG(1);
        if (users)
            fill_user(users, 0);
        if (pc)
            MEM32(pc) = 1;
        xnet_hle_log("XOnlineGetUsers -> 1 user '%s'", xnet_hle_gamertag());
        ret(S_OK, 8);
        return 1;
    }
    case BO3_VA(0x0031AAA1u, 0x0031ADA1u, 0x0031AE41u): {          /* XOnlineLogon(pUsers, pServiceIDs, cServices, hEvent, phTask) */
        uint32_t svc = ARG(1), n = ARG(2), ph = ARG(4);
        xnet_hle_log("XOnlineLogon: %u service(s), first 0x%08X", n, (n && svc) ? MEM32(svc) : 0);
        if (!s_logon_users)
            s_logon_users = title_alloc(4 * USER_SIZE);
        if (s_logon_users)
            for (i = 0; i < 4; i++) {
                if (i == 0)
                    fill_user(s_logon_users, 0);
                else
                    memset(HP(s_logon_users + i * USER_SIZE), 0, USER_SIZE);
            }
        if (ph)
            MEM32(ph) = LOGON_TASK;
        ret(S_OK, 20);
        return 1;
    }
    case BO3_VA(0x0031AB16u, 0x0031AE16u, 0x0031AEB6u): {          /* (0, XUID, 0, phTask, pResult): find a player */
        uint32_t ph = ARG(5), out = ARG(6), h, ina;
        uint8_t x[8];
        memcpy(x, HP(g_esp + 8), 8);   /* the XUID, 8 bytes by value */
        memcpy(&ina, x + 4, 4);
        if (ina)
            xnet_hle_peer_offset(ina, (uint16_t)(x[2] | x[3] << 8));
        h = bo3_peertask_open(&s_peer, x, out);
        peer_result(out, x);           /* now: a Continue finds it done whatever came between */
        if (ph)
            MEM32(ph) = h;
        xnet_hle_log("peer lookup %u: xuid %02X%02X%02X%02X-%u.%u.%u.%u (port offset %u)", h & 0x00FFFFFFu, x[3],
                     x[2], x[1], x[0], x[4], x[5], x[6], x[7], (unsigned)(x[2] | x[3] << 8));
        ret(S_OK, 28);
        return 1;
    }
    case BO3_VA(0x0031AA80u, 0x0031AD80u, 0x0031AE20u): {          /* XOnlineTaskContinue(hTask) */
        uint32_t h = ARG(0);
        static int said;
        if (bo3_peertask_is(h)) {
            const bo3_peertask *t = bo3_peertask_find(&s_peer, h);
            if (t)
                peer_result(t->out, t->xuid);   /* again, in case the title cleared its block */
            ret(0x001500F0u, 4);   /* done (region: not an address, an HRESULT); a task too old to
                                    * be kept was filled at its lookup */
            return 1;
        }
        if (h == LOGON_TASK) {
            if (!said++)
                xnet_hle_log("XOnlineTaskContinue(logon) -> connection established");
            ret(XONLINE_S_LOGON_CONNECTION_ESTABLISHED, 4);
        } else {
            xnet_hle_log("XOnlineTaskContinue(0x%08X): unknown task -> S_OK", h);
            ret(S_OK, 4);
        }
        return 1;
    }
    case BO3_VA(0x0031AA8Bu, 0x0031AD8Bu, 0x0031AE2Bu):            /* XOnlineTaskClose(hTask) */
        bo3_peertask_close(&s_peer, ARG(0));
        xnet_hle_log("XOnlineTaskClose(0x%08X)", ARG(0));
        ret(S_OK, 4);
        return 1;
    case BO3_VA(0x0031AAB0u, 0x0031ADB0u, 0x0031AE50u):            /* XOnlineLogonTaskGetResults(hTask) */
        ret(S_OK, 4);
        return 1;
    case BO3_VA(0x0031AABBu, 0x0031ADBBu, 0x0031AE5Bu):            /* XOnlineGetLogonUsers() */
        ret(s_logon_users, 0);
        return 1;
    case BO3_VA(0x0031AAC6u, 0x0031ADC6u, 0x0031AE66u): {          /* XOnlineGetServiceInfo(dwServiceID, pInfo) */
        uint32_t id = ARG(0), p = ARG(1);
        uint16_t port = xnet_hle_lobby_port();
        if (p) {
            MEM32(p) = id;
            MEM32(p + 4) = xnet_hle_lobby_ip();
            MEM32(p + 8) = port;   /* WORD port, WORD reserved */
        }
        xnet_hle_log("XOnlineGetServiceInfo(0x%08X) -> lobby, port %u", id, (unsigned)port);
        ret(S_OK, 8);
        return 1;
    }
    case BO3_VA(0x0031ABF3u, 0x0031AEF3u, 0x0031AF93u):            /* (pInvite): an accepted game invite? (0x210D80) */
        ret(1, 4);              /* S_FALSE: none */
        return 1;
    case BO3_VA(0x0031AB0Bu, 0x0031AE0Bu, 0x0031AEABu): {          /* (pxna, pxnkid, pxnkey): a session key for this console */
        uint32_t xna = ARG(0), kid = ARG(1), key = ARG(2);
        if (xna) xnet_hle_title_xnaddr(HP(xna));
        if (kid) xnet_hle_random(HP(kid), 8);
        if (key) xnet_hle_random(HP(key), 16);
        xnet_hle_log("XOnline create session key");
        ret(S_OK, 12);
        return 1;
    }
    }
    return 0;
}

/* XOnline stubs not answered above (presence, friends, notifications): the
 * call is logged and fails, so the title's error path shows what it needed.
 * The library itself cannot run: it was never started. */
static const struct { uint32_t va, argbytes; } k_xonline_other[] = {
    { BO3_VA(0x0031AAD1u, 0x0031ADD1u, 0x0031AE71u), 4 }, { BO3_VA(0x0031AADCu, 0x0031ADDCu, 0x0031AE7Cu), 4 }, { BO3_VA(0x0031AAE7u, 0x0031ADE7u, 0x0031AE87u), 24 },
    { BO3_VA(0x0031AB43u, 0x0031AE43u, 0x0031AEE3u), 8 }, { BO3_VA(0x0031AB4Eu, 0x0031AE4Eu, 0x0031AEEEu), 12 }, { BO3_VA(0x0031AB59u, 0x0031AE59u, 0x0031AEF9u), 4 }, { BO3_VA(0x0031AB64u, 0x0031AE64u, 0x0031AF04u), 12 },
    { BO3_VA(0x0031AB6Fu, 0x0031AE6Fu, 0x0031AF0Fu), 16 }, { BO3_VA(0x0031AB7Au, 0x0031AE7Au, 0x0031AF1Au), 8 }, { BO3_VA(0x0031AB85u, 0x0031AE85u, 0x0031AF25u), 16 }, { BO3_VA(0x0031AB90u, 0x0031AE90u, 0x0031AF30u), 20 },
    { BO3_VA(0x0031ABB1u, 0x0031AEB1u, 0x0031AF51u), 20 }, { BO3_VA(0x0031ABD2u, 0x0031AED2u, 0x0031AF72u), 12 }, { BO3_VA(0x0031ABDDu, 0x0031AEDDu, 0x0031AF7Du), 12 }, { BO3_VA(0x0031ABE8u, 0x0031AEE8u, 0x0031AF88u), 8 },
    { BO3_VA(0x0031ABFEu, 0x0031AEFEu, 0x0031AF9Eu), 24 }, { BO3_VA(0x0031AC0Du, 0x0031AF0Du, 0x0031AFADu), 16 },
};

/* the XNQOS blocks out with the title (peerqos.h); a stub can run on more
 * than one guest thread (bo3_online_flip), so the table has a lock */
static bo3_qos_keep s_qos_keep;
#ifdef _WIN32
static volatile LONG s_qos_lock;
#define QOS_LOCK()   while (InterlockedExchange(&s_qos_lock, 1)) Sleep(0)
#define QOS_UNLOCK() InterlockedExchange(&s_qos_lock, 0)
#else
static int s_qos_lock;
#define QOS_LOCK()   while (__atomic_exchange_n(&s_qos_lock, 1, __ATOMIC_ACQUIRE)) {}
#define QOS_UNLOCK() __atomic_store_n(&s_qos_lock, 0, __ATOMIC_RELEASE)
#endif

/* apxna: the peers' XNADDR pointers, the first cxna of n entries (0: none,
 * a service lookup). Each XNADDR starts with the title address the online
 * layer knows the peer by (XNetXnAddrToInAddr above). */
static uint32_t qos_block(uint32_t n, uint32_t apxna, uint32_t cxna)
{
    /* XNQOS: cxnqos, cxnqosPending, then XNQOSINFO[n] of 0x18 bytes:
     * bFlags, bReserved, cProbesXmit, cProbesRecv, cbData, pbData,
     * wRttMin, wRttMed, dwUpBitsPerSec, dwDnBitsPerSec. */
    uint32_t p = title_alloc(8 + 0x18 * (n ? n : 1)), i;
    if (!p)
        return 0;
    MEM32(p) = n;
    for (i = 0; i < n; i++) {
        uint32_t q = p + 8 + i * 0x18;
        unsigned rmin = 0, rmed = 0;
        int sent = 0, lost = 0, have = 0;
        bo3_qos_values v;
        if (apxna && i < cxna) {
            uint32_t x = MEM32(apxna + i * 4);
            if (x)
                have = xnet_hle_peer_rtt(MEM32(x), &rmin, &rmed, &sent, &lost);
        }
        v = bo3_qos_fill(have, rmin, rmed, sent, lost);
        MEM8(q) = 0x0B;               /* COMPLETE | CONTACTED | DATA_RECEIVED */
        MEM8(q + 2) = v.xmit;
        MEM8(q + 3) = v.recv;
        *(uint16_t *)HP(q + 4) = 4;
        *(uint16_t *)HP(q + 0xC) = v.rtt_min;
        *(uint16_t *)HP(q + 0xE) = v.rtt_med;
        MEM32(q + 0x10) = 1000000;
        MEM32(q + 0x14) = 1000000;
    }
    {
        static int told;
        int kept;
        QOS_LOCK();
        kept = bo3_qos_keep_add(&s_qos_keep, p);
        QOS_UNLOCK();
        if (!kept && !told) {
            told = 1;
            xnet_hle_log("XNetQos: %d blocks out and none released: this one is never freed", BO3_QOS_KEEP);
        }
    }
    return p;
}

/* XNetQosLookup / XNetQosServiceLookup: the block into *pp, the result */
static uint32_t qos_lookup(uint32_t pp, uint32_t n, uint32_t apxna, uint32_t cxna)
{
    uint32_t p = qos_block(n, apxna, cxna);
    if (pp)
        MEM32(pp) = p;
    if (!p)
        xnet_hle_log("XNetQos: the title's heap has no block for %u: WSAENOBUFS", n);
    return bo3_qos_lookup_ret(p);
}

/* CREATE GAME's gcre goes out with TKPUBLIC=1 (a public game with a code).
 * BO-352 step 5c: a host's "gset KICK" goes out tagged with its reason. out
 * gets the packet with TKWHY=nolink/host added (xl_add_line); the new length,
 * or 0 when this is not a host's kick (send it as it is). The online layer
 * calls it from its lobby send (xnet_hle_set_lobby_tag) and tells the title
 * its own length back (BO-351). */
static int tag_kick(const uint8_t *pkt, int n, uint8_t *out, int cap)
{
    xnet_hle_lobby_view lv;
    bo3_link_view v;
    int np, k, e, why;
    /* CREATE GAME's gcre: a public game with its code (xboxlive.h,
     * bo3_xboxlive_gcre_public); the toolkit adds the TKCODE line after this */
    if (n >= 12 && !memcmp(pkt, "gcre", 4))
        return bo3_xboxlive_gcre_public_now() ? xl_add_line(pkt, n, "TKPUBLIC=1", out, cap) : 0;
    /* BO-365: a search while QUICK PLAY waits for a race asks for that race
     * (TKWAITFOR, quickplay.c / racewait.h) */
    if (n >= 12 && !memcmp(pkt, "gsea", 4)) {
        char line[32];
        int id = bo3_quickplay_wait_ident();
        /* BO-372: BROWSE GAMES asks for every game, with its state */
        if (id <= 0 && bo3_quickplay_browse_tag())
            return xl_add_line(pkt, n, "TKBROWSE=1", out, cap);
        if (id <= 0)
            return 0;
        snprintf(line, sizeof line, "TKWAITFOR=%d", id);
        return xl_add_line(pkt, n, line, out, cap);
    }
    /* stay together: the sign-in after quitting on the results says so, and
     * the others stop waiting for this player (stay.c) */
    if (n >= 12 && !memcmp(pkt, "auth", 4))
        return bo3_stay_quit_take() ? xl_add_line(pkt, n, "TKSTAYQUIT=1", out, cap) : 0;
    if (n < 12 || memcmp(pkt, "gset", 4) || !xnet_hle_lobby_view_get(&lv))
        return 0;
    np = lv.np > 0 && lv.np <= 8 ? lv.np : 0;
    memset(&v, 0, sizeof v);
    v.np = np;
    v.local_id = MEM32(BO3_VA(0x005729F8u, 0x00572D08u, 0x00572DA8u));
    for (k = 0; k < np; k++) {
        uint32_t P = k < (int)MEM32(BO3_VA(0x005729A8u, 0x00572CB8u, 0x00572D58u))
                         ? MEM32(BO3_VA(0x00572988u, 0x00572C98u, 0x00572D38u) + 4u * (uint32_t)k) : 0;
        v.name[k] = lv.name[k][0] ? lv.name[k] : "";
        v.own_link[k] = 3;
        v.id[k] = v.local_id;                       /* a null slot is the local copy itself */
        if (P >= 0x10000u && P < 0x04000000u - 0xD00u) {
            v.own_link[k] = (int)MEM32(P + 0x4F0);
            v.id[k] = MEM32(P + 8);
            for (e = 0; e < 8; e++) {               /* what k reported about its links (table C) */
                v.rep_id[k][e] = MEM32(P + 0x580u + 4u * (uint32_t)e);
                v.rep_state[k][e] = (int)MEM32(P + 0x5A0u + 4u * (uint32_t)e);
            }
        }
    }
    why = bo3_status_kick_reason2(pkt, n, &v);
    if (why == BO3_STATUS_KICK_NONE)
        return 0;
    return xl_add_line(pkt, n, why == BO3_STATUS_KICK_NOLINK ? "TKWHY=nolink" : "TKWHY=host", out, cap);
}

/* BO-352: the player's language, as a languages.h index; English when unset
 * or unknown. */
static unsigned long long mono_ms(void);

static int status_lang(void)
{
    extern const char *xbox_SettingsText(const char *key, int *locked);
    int lang = bo3_language_index(xbox_SettingsText("game.language", NULL));
    return lang >= 0 ? lang : BO3_LANG_ENGLISH;
}

/* BO-352 step 5c: the empty game record the server sends a kicked player
 * carries TKWHY=<reason> (step 5b). Keep it as a pending message: it is shown
 * in the top line of the next game lobby (kick_tick, status_tick), or on the
 * overlay if none shows up in time. */
static bo3_kick_msg s_kick;         /* main thread only (kick_tick) */
static char s_kick_line[160];       /* the kick line the lobby shows now, "" none */
/* the reason a lobby receive saw, handed to the main thread: the receive runs
 * on whichever guest thread calls it (see bo3_online_flip) */
#ifdef _WIN32
static volatile LONG s_kick_in;
#define KICK_IN_OFFER(v) InterlockedCompareExchange(&s_kick_in, (LONG)(v), 0)
#define KICK_IN_TAKE()   ((int)InterlockedExchange(&s_kick_in, 0))
#else
static int s_kick_in;
#define KICK_IN_OFFER(v) do { int z_ = 0; __atomic_compare_exchange_n(&s_kick_in, &z_, (v), 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE); } while (0)
#define KICK_IN_TAKE()   __atomic_exchange_n(&s_kick_in, 0, __ATOMIC_ACQ_REL)
#endif

/* The kick box (lobbystatus.h bo3_status_kick_box). A kicked player gets
 * the title's own full-screen box "You have been kicked out of the room."
 * (string 0x44F, the lobby's event handler 0x6D537), built a frame after the
 * empty game record that carries TKWHY; the lobby screen still runs under
 * it, so a line in the lobby's top line stays hidden. So the reason goes
 * into the box itself: the record's receive (kick_scan) points the string
 * table's slot at our text before the title reads the record, and kick_tick
 * puts the title's text back once the box has been up and closed. The texts
 * are made on the main thread while a lobby runs (kick_box_prepare); the
 * receive only swaps a pointer. */
#define STR_TABLE    BO3_VA(0x004D532Cu, 0x004D564Cu, 0x004D56ECu)   /* the loaded string table */
#define STR_KICKED   0x44Fu         /* "You have been kicked out of the room.": the same id on all three discs */
#define MBOX_OPEN    (BO3_VA(0x00550CB0u, 0x00550FD0u, 0x00551070u) + 0x110u)   /* the message box widget is up */
#if defined(__APPLE__)
#define KICK_PLAT BO3_PLAT_MAC
#elif defined(_WIN32)
#define KICK_PLAT BO3_PLAT_WINDOWS
#elif defined(__ANDROID__)
#define KICK_PLAT BO3_PLAT_OTHER
#else
#define KICK_PLAT BO3_PLAT_LINUX
#endif
#ifdef _WIN32
#define KB_LOAD(p)        ((int)InterlockedCompareExchange((volatile LONG *)(p), 0, 0))
#define KB_STORE(p, v)    InterlockedExchange((volatile LONG *)(p), (LONG)(v))
#define KB_CAS(p, o, v)   (InterlockedCompareExchange((volatile LONG *)(p), (LONG)(v), (LONG)(o)) == (LONG)(o))
#else
#define KB_LOAD(p)        __atomic_load_n((p), __ATOMIC_ACQUIRE)
#define KB_STORE(p, v)    __atomic_store_n((p), (v), __ATOMIC_RELEASE)
#define KB_CAS(p, o, v)   __extension__({ int o_ = (o); __atomic_compare_exchange_n((p), &o_, (v), 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE); })
#endif
static int s_kb_text[BO3_STATUS_KICK_HOST + 1];     /* each reason's UTF-16 text (a guest address), 0 = not made */
static int s_kb_in;                 /* the reason whose text is in the table; -1 while the receive puts it in */
static uint32_t s_kb_slot, s_kb_old;    /* the slot and the title's text it held (set before s_kb_in) */
static bo3_kick_box s_kb;           /* main thread only (kick_tick) */

static void kick_box_text(int why, char *out, size_t n)
{
    unsigned off = xnet_hle_port_offset();
    bo3_status_kick_box(why, status_lang(), KICK_PLAT, 1000u + off, 6000u + off, out, n);
}

/* main thread, a lobby frame: the two texts in the title's heap, once */
static void kick_box_prepare(void)
{
    int why;
    for (why = BO3_STATUS_KICK_NOLINK; why <= BO3_STATUS_KICK_HOST; why++) {
        char t[BO3_KICK_BOX_MAX];
        uint32_t p, k;
        if (KB_LOAD(&s_kb_text[why]))
            continue;
        kick_box_text(why, t, sizeof t);
        if (!t[0] || !(p = title_alloc((uint32_t)(strlen(t) + 1) * 2u)))
            continue;
        for (k = 0; t[k]; k++)
            MEM16(p + 2u * k) = (uint16_t)(uint8_t)t[k];
        MEM16(p + 2u * k) = 0;
        KB_STORE(&s_kb_text[why], (int)p);
    }
}

/* the receive's thread: our text into the kick string's slot; 1 when it is in */
static int kick_box_swap(int why)
{
    uint32_t txt = (uint32_t)KB_LOAD(&s_kb_text[why]), t, slots, slot;
    if (!txt || !KB_CAS(&s_kb_in, 0, -1))
        return 0;
    t = MEM32(STR_TABLE);
    /* the language file is in the title's heap, the 64 MiB RAM alias (onlinebrand.c) */
    slots = t >= 0x80010000u && t < 0x84000000u && MEM32(t + 8u) > STR_KICKED ? MEM32(t + 12u) : 0;
    if (slots < 0x80010000u || slots >= 0x84000000u) {
        KB_STORE(&s_kb_in, 0);
        return 0;
    }
    slot = slots + 4u * STR_KICKED;
    s_kb_slot = slot;
    s_kb_old = MEM32(slot);
    MEM32(slot) = txt;
    KB_STORE(&s_kb_in, why);
    return 1;
}

/* main thread: the title's text back (only if the slot still holds ours) */
static void kick_box_restore(void)
{
    int why = KB_LOAD(&s_kb_in);
    if (why <= 0)
        return;
    if (MEM32(s_kb_slot) == (uint32_t)KB_LOAD(&s_kb_text[why]))
        MEM32(s_kb_slot) = s_kb_old;
    KB_STORE(&s_kb_in, 0);
}

static void kick_scan(const uint8_t *buf, int n)
{
    int i, why = 0;
    for (i = 0; i + 6 < n; i++) {
        if (memcmp(buf + i, "TKWHY=", 6))
            continue;
        if (i + 12 <= n && !memcmp(buf + i + 6, "nolink", 6))
            why = BO3_STATUS_KICK_NOLINK;
        else if (i + 10 <= n && !memcmp(buf + i + 6, "host", 4))
            why = BO3_STATUS_KICK_HOST;
        else if (i + 12 <= n && !memcmp(buf + i + 6, "closed", 6))
            why = BO3_STATUS_KICK_CLOSED;
        break;
    }
    /* nolink or host: the title's own box says it; closed (the game ended)
     * keeps the pending line (kick_tick) */
    if (why && !((why == BO3_STATUS_KICK_NOLINK || why == BO3_STATUS_KICK_HOST) && kick_box_swap(why)))
        KICK_IN_OFFER(why);
}

/* BO-364: TKKICKED=1, the lobby's word that the code's game (or the one
 * joined) kicked this player, for JOIN WITH CODE's text (quickplay.c) */
#ifdef _WIN32
static volatile LONG s_removed_in;
int bo3_online_removed_take(void) { return (int)InterlockedExchange(&s_removed_in, 0); }
static void removed_set(void) { InterlockedExchange(&s_removed_in, 1); }
#else
static int s_removed_in;
int bo3_online_removed_take(void) { return __atomic_exchange_n(&s_removed_in, 0, __ATOMIC_ACQ_REL); }
static void removed_set(void) { __atomic_store_n(&s_removed_in, 1, __ATOMIC_RELEASE); }
#endif

static void removed_scan(const uint8_t *buf, int n)
{
    int i;
    for (i = 0; i + 10 <= n; i++)
        if (!memcmp(buf + i, "TKKICKED=1", 10)) {
            removed_set();
            return;
        }
}

/* BO-352: the pending kick message, each XNet call: a game lobby with the
 * top line shows it for 8 s (status_tick writes it); after a few seconds with
 * no such lobby the overlay shows it. The pages a kicked player lands on (the
 * Xbox Live menu, the lobby list) draw no line of ours; the bottom ticker
 * widget (0x5509C8) is not drawn there either. */
static void kick_tick(void)
{
    int r, kb = KB_LOAD(&s_kb_in), was = s_kb.why;
    int in = KICK_IN_TAKE();
    r = bo3_kick_box_frame(&s_kb, kb > 0 ? kb : 0, mono_ms(), g_xbox_mem_offset && MEM32(MBOX_OPEN) != 0);
    if (kb > 0 && !was && s_kb.why)
        xnet_hle_note("kick box: the title's kick box says why (%s)", kb == BO3_STATUS_KICK_NOLINK ? "nolink" : "host");
    if (r == 1) {
        kick_box_restore();
        xnet_hle_note("kick box: closed, the title's text is back");
    } else if (r == 2) {
        char m[BO3_KICK_BOX_MAX];
        kick_box_restore();
        kick_box_text(kb, m, sizeof m);
        xnet_hle_note("kick box: no box came up; the overlay says it");
        if (m[0])
            xnet_hle_message(m);
    }
    if (in && !s_kick.why) {
        s_kick.why = in;
        s_kick.set_ms = mono_ms();
        s_kick.tick_ms = 0;
    }
    s_kick_line[0] = 0;
    if (!s_kick.why)
        return;
    r = bo3_kick_msg_frame(&s_kick, mono_ms(), bo3_online_lobby_now(NULL, NULL, NULL) && bo3_online_line_shown());
    if (r == 1)
        bo3_status_kick_line(s_kick.why, status_lang(), 1000u + xnet_hle_port_offset(), s_kick_line, sizeof s_kick_line);
    else if (r == 2) {
        char m[160];
        bo3_status_kick_line(s_kick.why, status_lang(), 1000u + xnet_hle_port_offset(), m, sizeof m);
        if (m[0])
            xnet_hle_message(m);
    }
}

static int xnet(uint32_t va)
{
    int e = 0;
    switch (va) {
    case BO3_VA(0x00339722u, 0x00339A22u, 0x00339AC2u):            /* XNetStartup(params) */
        net_startup("XNetStartup");
        ret(0, 4);
        return 1;
    case BO3_VA(0x00339739u, 0x00339A39u, 0x00339AD9u):            /* WSAStartup(version, pWSAData) */
        net_startup("WSAStartup");
        if (ARG(1)) {
            *(uint16_t *)HP(ARG(1)) = 0x0202;
            *(uint16_t *)HP(ARG(1) + 2) = 0x0202;
        }
        ret(0, 8);
        return 1;
    case BO3_VA(0x00339827u, 0x00339B27u, 0x00339BC7u):            /* WSACleanup / XNetCleanup */
        net_cleanup("WSACleanup/XNetCleanup");
        ret(0, 0);
        return 1;
    case BO3_VA(0x0033932Eu, 0x0033962Eu, 0x003396CEu):            /* XNetGetTitleXnAddr(pxna) */
        ret(xnet_hle_title_xnaddr(ARG(0) ? HP(ARG(0)) : NULL), 4);
        return 1;
    case BO3_VA(0x003392A9u, 0x003395A9u, 0x00339649u):            /* XNetRegisterKey(pxnkid, pxnkey) */
        ret(0, 8);
        return 1;
    case BO3_VA(0x003392B4u, 0x003395B4u, 0x00339654u):            /* XNetUnregisterKey(pxnkid) */
        ret(0, 4);
        return 1;
    case BO3_VA(0x003392BFu, 0x003395BFu, 0x0033965Fu):            /* XNetXnAddrToInAddr(pxna, pxnkid, pina) */
        if (ARG(2)) MEM32(ARG(2)) = ARG(0) ? MEM32(ARG(0)) : 0;
        ret(0, 12);
        return 1;
    case BO3_VA(0x003392D6u, 0x003395D6u, 0x00339676u):            /* XNetInAddrToXnAddr(ina, pxna, pxnkid) */
        if (ARG(1)) xnet_hle_make_xnaddr(HP(ARG(1)), ARG(0));
        if (ARG(2)) memset(HP(ARG(2)), 0, 8);
        ret(0, 12);
        return 1;
    case BO3_VA(0x003392EDu, 0x003395EDu, 0x0033968Du):            /* XNetUnregisterInAddr(ina) */
        ret(0, 4);
        return 1;
    case BO3_VA(0x003392F8u, 0x003395F8u, 0x00339698u):            /* XNetQosListen(pxnkid, pb, cb, bitsPerSec, flags) */
        ret(0, 20);
        return 1;
    case BO3_VA(0x00339307u, 0x00339607u, 0x003396A7u): {          /* XNetQosLookup(... 12 args ..., ppxnqos) */
        uint32_t n = ARG(0) + ARG(4), pp = ARG(11);
        xnet_hle_log("XNetQosLookup: %u peer(s), %u server(s)", ARG(0), ARG(4));
        ret(qos_lookup(pp, n, ARG(1), ARG(0)), 48);
        return 1;
    }
    case BO3_VA(0x00339511u, 0x00339811u, 0x003398B1u): {          /* XNetQosServiceLookup(flags, hEvent, ppxnqos) */
        ret(qos_lookup(ARG(2), 1, 0, 0), 12);
        return 1;
    }
    case BO3_VA(0x00339323u, 0x00339623u, 0x003396C3u): {          /* XNetQosRelease(pxnqos) */
        int ours;
        QOS_LOCK();
        ours = bo3_qos_keep_take(&s_qos_keep, ARG(0));
        QOS_UNLOCK();
        if (ours)
            title_free(ARG(0));
        ret(0, 4);
        return 1;
    }
    case BO3_VA(0x003394FBu, 0x003397FBu, 0x0033989Bu):            /* XNetConnect(ina) */
        xnet_hle_log("XNetConnect(%u.%u.%u.%u)", ARG(0) & 0xFF, (ARG(0) >> 8) & 0xFF,
                     (ARG(0) >> 16) & 0xFF, ARG(0) >> 24);
        ret(0, 4);
        return 1;
    case BO3_VA(0x00339506u, 0x00339806u, 0x003398A6u):            /* XNetGetConnectStatus(ina) */
        ret(2, 4);              /* XNET_CONNECT_STATUS_CONNECTED */
        return 1;

    /* Winsock */
    case BO3_VA(0x00339339u, 0x00339639u, 0x003396D9u): {
        uint32_t s = xnet_hle_socket((int)ARG(0), (int)ARG(1), (int)ARG(2), &e);
        wsa_ret(s, e, 12);
        return 1;
    }
    case BO3_VA(0x00339344u, 0x00339644u, 0x003396E4u): wsa_ret((uint32_t)xnet_hle_closesocket(ARG(0), &e), e, 4); return 1;
    case BO3_VA(0x0033934Fu, 0x0033964Fu, 0x003396EFu): wsa_ret((uint32_t)xnet_hle_shutdown(ARG(0), (int)ARG(1), &e), e, 8); return 1;
    case BO3_VA(0x0033935Au, 0x0033965Au, 0x003396FAu):
        wsa_ret((uint32_t)xnet_hle_ioctlsocket(ARG(0), ARG(1),
                ARG(2) ? (uint32_t *)HP(ARG(2)) : NULL, &e), e, 12);
        return 1;
    case BO3_VA(0x00339365u, 0x00339665u, 0x00339705u):
        wsa_ret((uint32_t)xnet_hle_setsockopt(ARG(0), (int)ARG(1), (int)ARG(2),
                ARG(3) ? HP(ARG(3)) : NULL, (int)ARG(4), &e), e, 20);
        return 1;
    case BO3_VA(0x00339374u, 0x00339674u, 0x00339714u):
        wsa_ret((uint32_t)xnet_hle_getsockname(ARG(0), ARG(1) ? HP(ARG(1)) : NULL,
                ARG(2) ? (int32_t *)HP(ARG(2)) : NULL, &e), e, 12);
        return 1;
    case BO3_VA(0x0033937Fu, 0x0033967Fu, 0x0033971Fu):
        wsa_ret((uint32_t)xnet_hle_getpeername(ARG(0), ARG(1) ? HP(ARG(1)) : NULL,
                ARG(2) ? (int32_t *)HP(ARG(2)) : NULL, &e), e, 12);
        return 1;
    case BO3_VA(0x0033938Au, 0x0033968Au, 0x0033972Au):
        wsa_ret((uint32_t)xnet_hle_bind(ARG(0), ARG(1) ? HP(ARG(1)) : NULL, (int)ARG(2), &e), e, 12);
        return 1;
    case BO3_VA(0x00339395u, 0x00339695u, 0x00339735u):
        wsa_ret((uint32_t)xnet_hle_connect(ARG(0), ARG(1) ? HP(ARG(1)) : NULL, (int)ARG(2), &e), e, 12);
        return 1;
    case BO3_VA(0x003393A0u, 0x003396A0u, 0x00339740u):
        wsa_ret(xnet_hle_accept(ARG(0), ARG(1) ? HP(ARG(1)) : NULL,
                ARG(2) ? (int32_t *)HP(ARG(2)) : NULL, &e), e, 12);
        return 1;
    case BO3_VA(0x003393ABu, 0x003396ABu, 0x0033974Bu):            /* select(nfds, rd, wr, ex, timeout) */
        wsa_ret((uint32_t)xnet_hle_select(ARG(1) ? HP(ARG(1)) : NULL, ARG(2) ? HP(ARG(2)) : NULL,
                ARG(3) ? HP(ARG(3)) : NULL, ARG(4) ? (const int32_t *)HP(ARG(4)) : NULL, &e), e, 20);
        return 1;
    case BO3_VA(0x003393D4u, 0x003396D4u, 0x00339774u): {
        int r = (int)xnet_hle_recv(ARG(0), HP(ARG(1)), (int)ARG(2), (int)ARG(3), &e);
        if (r > 0) {
            kick_scan((const uint8_t *)HP(ARG(1)), r);
            removed_scan((const uint8_t *)HP(ARG(1)), r);
            bo3_stay_lobby_scan((const uint8_t *)HP(ARG(1)), r);    /* stay together's keys */
        }
        wsa_ret((uint32_t)r, e, 16);
        return 1;
    }
    case BO3_VA(0x003393EEu, 0x003396EEu, 0x0033978Eu):
        wsa_ret((uint32_t)xnet_hle_recvfrom(ARG(0), HP(ARG(1)), (int)ARG(2), (int)ARG(3),
                ARG(4) ? HP(ARG(4)) : NULL, ARG(5) ? (int32_t *)HP(ARG(5)) : NULL, &e), e, 24);
        return 1;
    case BO3_VA(0x003393FDu, 0x003396FDu, 0x0033979Du):
        /* the kick's reason is added inside (tag_kick, bo3_online_init). It
         * used to be added here, and the tagged length came back to the
         * title: its lobby send queue ran past its end and guest-main spun
         * in 0x00221320 for good (BO-351, the int044 Host1 freeze). */
        wsa_ret((uint32_t)xnet_hle_send(ARG(0), HP(ARG(1)), (int)ARG(2), (int)ARG(3), &e), e, 16);
        return 1;
    case BO3_VA(0x00339417u, 0x00339717u, 0x003397B7u):
        wsa_ret((uint32_t)xnet_hle_sendto(ARG(0), HP(ARG(1)), (int)ARG(2), (int)ARG(3),
                ARG(4) ? HP(ARG(4)) : NULL, (int)ARG(5), &e), e, 24);
        return 1;
    case BO3_VA(0x003393DFu, 0x003396DFu, 0x0033977Fu):            /* WSARecv(s, bufs, n, pRecvd, pFlags, pOv, cb) */
    case BO3_VA(0x00339408u, 0x00339708u, 0x003397A8u): {          /* WSASend(s, bufs, n, pSent, flags, pOv, cb) */
        uint32_t bufs = ARG(1), r;
        if (ARG(5) || !bufs) {
            xnet_hle_log("%s with OVERLAPPED: not handled", va == BO3_VA(0x003393DFu, 0x003396DFu, 0x0033977Fu) ? "WSARecv" : "WSASend");
            wsa_ret(0xFFFFFFFFu, 10045, 28);
            return 1;
        }
        if (va == BO3_VA(0x003393DFu, 0x003396DFu, 0x0033977Fu))
            r = (uint32_t)xnet_hle_recv(ARG(0), HP(MEM32(bufs + 4)), (int)MEM32(bufs), 0, &e);
        else
            r = (uint32_t)xnet_hle_send(ARG(0), HP(MEM32(bufs + 4)), (int)MEM32(bufs), 0, &e);
        if (!e && ARG(3)) MEM32(ARG(3)) = r;
        wsa_ret(e ? 0xFFFFFFFFu : 0, e, 28);
        return 1;
    }
    case BO3_VA(0x003393BAu, 0x003396BAu, 0x0033975Au):            /* WSAGetOverlappedResult */
    case BO3_VA(0x003393C9u, 0x003396C9u, 0x00339769u):            /* WSACancelOverlappedIO */
    case BO3_VA(0x00339426u, 0x00339726u, 0x003397C6u): {          /* WSASendTo */
        uint32_t n = va == BO3_VA(0x003393BAu, 0x003396BAu, 0x0033975Au) ? 20 : va == BO3_VA(0x003393C9u, 0x003396C9u, 0x00339769u) ? 4 : 36;
        xnet_hle_log("stub 0x%08X (overlapped I/O): not handled", va);
        wsa_ret(va == BO3_VA(0x003393BAu, 0x003396BAu, 0x0033975Au) ? 0 : 0xFFFFFFFFu, 10045, n);
        return 1;
    }
    }
    return 0;
}

/* snprintf onto out at *o, never past cap (BO-337 M7): snprintf returns the
 * length it wanted, so a cut line would otherwise push *o past the end */
static void append(char *out, size_t cap, int *o, const char *fmt, ...)
{
    va_list ap;
    int r;
    if (*o < 0 || (size_t)*o >= cap)
        return;
    va_start(ap, fmt);
    r = vsnprintf(out + *o, cap - (size_t)*o, fmt, ap);
    va_end(ap);
    if (r < 0)
        return;
    *o = (size_t)(*o + r) >= cap ? (int)cap - 1 : *o + r;
}

static unsigned long long mono_ms(void);

/* BO-345: every table entry's link state (linkstate.h) */
static bo3_linkstate s_linkstate[8];

/* BO-352: which peers link_watch has flagged "not linked after 20 s", so the
 * lobby's status line can name the one that will not link. Mirrored by
 * link_watch: set when it tells, cleared when the peer links (or a new peer
 * appears in its slot). */
static uint8_t s_unlinked[8];

/* An always-on [NET] line for every change of a peer's link state, so a
 * link that never comes up shows which step it stopped at (int039 game 83). */
static void link_state_log(uint32_t k, uint32_t P, unsigned long long now)
{
    bo3_linkstate *e = &s_linkstate[k];
    uint32_t from = BO3_LS_NONE, st = P ? MEM32(P + 0x4F0) : 0;
    char f[12];
    if (bo3_linkstate_step(e, P, P ? MEM32(P + 8) : 0, st, now, &from) != BO3_LS_CHANGED)
        return;
    if (from == BO3_LS_NONE)
        snprintf(f, sizeof f, "-");
    else
        snprintf(f, sizeof f, "%u", from);
    xnet_hle_note("peer %u link state %s -> %u, %llu ms after it appeared%s", k, f, st, now - e->appeared,
                  e->lines >= BO3_LS_LINES ? " (no more state lines for this peer this game)" : "");
}

/* A player in our game whose peer link is not up (peer state [peer + 0x4F0]
 * below 3, the link-up 0xFD7D0) for 20 s in a row: the host's UDP port is
 * most likely closed on its router. Told once until the link is up again. */
static void link_watch(void)
{
    static uint32_t seen_peer[8], told[8];
    static unsigned long long since[8], appeared[8];
    static uint8_t linked_told[8], lost_told[8];   /* BO-340: the always-on log lines, once per peer */
    /* a monotonic clock: a clock change neither raises nor hides it (L7) */
    unsigned long long now = mono_ms();
    uint32_t np = MEM32(BO3_VA(0x005729A8u, 0x00572CB8u, 0x00572D58u)), k;
    for (k = 0; k < 8; k++) {
        uint32_t P = k < np ? MEM32(BO3_VA(0x00572988u, 0x00572C98u, 0x00572D38u) + 4 * k) : 0;
        link_state_log(k, P, now);
        if (P != seen_peer[k]) {
            seen_peer[k] = P;
            since[k] = now;
            appeared[k] = now;
            told[k] = 0;
            linked_told[k] = lost_told[k] = 0;
            s_unlinked[k] = 0;
        }
        if (P && MEM32(P + 0x4F0) == 3) {
            since[k] = now;         /* linked: the 20 s count from the last time it was */
            told[k] = 0;
            s_unlinked[k] = 0;
            if (!linked_told[k]) {
                linked_told[k] = 1;
                xnet_hle_note("peer %u linked, %llu ms after it appeared", k, now - appeared[k]);
            }
            continue;
        }
        if (P && linked_told[k] && !lost_told[k]) {
            lost_told[k] = 1;
            xnet_hle_note("peer %u: the link was lost", k);
        }
        if (!P || told[k] || now - since[k] < 20000)
            continue;
        told[k] = 1;
        s_unlinked[k] = 1;
        xnet_hle_note("peer %u not linked after 20 s", k);
        {
            char m[256];
            bo3_unreach_text(m, sizeof m, xnet_hle_on() == 2, xnet_hle_port_offset());
            xnet_hle_message(m);
        }
    }
}

/* TAKEDAWN_NET_TRACE=1: every 2 s in a race, each racing car's own position
 * (the car objects [0x73A1A8 + 4i], [0x73A19C] of them, translation at
 * +0x40) with a monotonic millisecond clock, for comparing two copies (the
 * online soak's desync check, tools/online/drawn.py). */
static void race_log(void)
{
    static int on = -1;
    static unsigned long long last;
    unsigned long long ms;
    char line[400];
    uint32_t nr, i;
    int o = 0;
    line[0] = 0;
    if (on < 0) {
        const char *e = getenv("TAKEDAWN_NET_TRACE");
        on = e && *e && *e != '0';
    }
    if (!on || MEM32(BO3_VA(0x0073A1D0u, 0x0073A4E0u, 0x0073A580u) + 0x134Cu) != 2)     /* player 1 racing */
        return;
    /* wall-clock ms: copies on two machines compare (tools/online/drawn.py
     * --fit takes out what the clocks differ by) */
#ifdef _WIN32
    {
        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);
        ms = ((((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime) - 116444736000000000ull) / 10000ull;
    }
#else
    {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ms = (unsigned long long)ts.tv_sec * 1000ull + (unsigned long long)ts.tv_nsec / 1000000ull;
    }
#endif
    if (ms - last < 2000)
        return;
    last = ms;
    nr = MEM32(BO3_VA(0x0073A19Cu, 0x0073A4ACu, 0x0073A54Cu));
    for (i = 0; i < nr && i < 8; i++) {
        uint32_t car = MEM32(BO3_VA(0x0073A1A8u, 0x0073A4B8u, 0x0073A558u) + 4u * i);
        float f[3];
        if (car < 0x10000u || car >= 0x04000000u)
            continue;
        memcpy(f, HP(car + 0x40u), 12);
        append(line, sizeof line, &o, " | %08x %u %.1f %.1f %.1f", car, MEM32(car + 0x27D0u), f[0], f[1], f[2]);
    }
    xnet_hle_log("cars step %u ms %llu:%s", MEM32(BO3_VA(0x004A1EB4u, 0x004A21D4u, 0x004A2274u)), ms, line);
}

static uint32_t lobby_screen_now(void);

/* [ONLINE] (TAKEDAWN_NET_TRACE): the state the host's Start depends on, once a
 * second when it changes and every 10 s anyway. The lobby screen's state byte
 * (+0x13AC) and its two flags (+0x13A4 every peer linked, +0x13A5 cleared for
 * good by a failed peer step), the players in the title's peer table
 * (0x5729A8) and each one's link state ([0x572988 + 4k] + 0x4F0, 3 = up), the
 * game record's SYSFLAGS ([0x57BD34] + 0x114: bit 12 lock, bit 19 started),
 * the module (race or menus), player 1's car phase, and the client's view of
 * the game: its players' ready values (3 = ready) and the secret. */
static void state_log(void)
{
    static int on = -1;
    static unsigned long long last_ms, last_full;
    static char last[512];
    char line[512], links[64], game[300];
    unsigned long long now;
    uint32_t scr, np, k, rec, mod;
    int o = 0;
    if (on < 0) {
        const char *e = getenv("TAKEDAWN_NET_TRACE");
        on = e && *e && *e != '0';
    }
    if (!on)
        return;
#ifdef _WIN32
    now = (unsigned long long)GetTickCount64();
#else
    {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        now = (unsigned long long)ts.tv_sec * 1000ull + (unsigned long long)ts.tv_nsec / 1000000ull;
    }
#endif
    if (now - last_ms < 1000)
        return;
    last_ms = now;
    np = MEM32(BO3_VA(0x005729A8u, 0x00572CB8u, 0x00572D58u));
    links[0] = 0;
    for (k = 0; k < np && k < 8; k++) {
        uint32_t P = MEM32(BO3_VA(0x00572988u, 0x00572C98u, 0x00572D38u) + 4 * k);
        append(links, sizeof links, &o, "%s", k ? "," : "");
        if (P && P < 0x04000000u)
            append(links, sizeof links, &o, "%u", MEM32(P + 0x4F0));
        else
            append(links, sizeof links, &o, "-");
    }
    scr = lobby_screen_now();
    rec = MEM32(BO3_VA(0x0057BD34u, 0x0057C044u, 0x0057C0E4u));
    mod = MEM32(BO3_VA(0x004D5370u, 0x004D5690u, 0x004D5730u));
    xnet_hle_game_state(game, sizeof game);
    if (scr)
        snprintf(line, sizeof line, "screen=lobby state=0x%02X linked=%u alive=%u",
                 MEM8(scr + 0x13ACu), MEM8(scr + 0x13A4u), MEM8(scr + 0x13A5u));
    else
        snprintf(line, sizeof line, "screen=other");
    o = (int)strlen(line);
    snprintf(line + o, sizeof line - (size_t)o, " np=%u links=%s sysflags=0x%X mod=%s phase=%u %s", np,
             links[0] ? links : "-", (rec && rec < 0x04000000u) ? MEM32(rec + 0x114u) : 0u,
             (mod && mod < 0x04000000u) ? (bo3_race_module_vt(MEM32(mod)) ? "race" : "menus") : "-",
             MEM32(BO3_VA(0x0073A1D0u, 0x0073A4E0u, 0x0073A580u) + 0x134Cu), game);
    if (strcmp(line, last) || now - last_full >= 10000) {
        snprintf(last, sizeof last, "%s", line);
        last_full = now;
        xnet_hle_log("[ONLINE] %s", line);
    }
}

/* BO-379: an always-on line when the title's reliable-message queue
 * (linkresend.h) fills: at 16, 24 and 32 waiting entries of 32 (per peer id,
 * and how many wait for players no longer in the game), and when it empties
 * again. With 32 waiting, every new reliable message is dropped. */
static int read_resend_queue(uint32_t P, bo3_lr_entry *q);

static void queue_watch(void)
{
    static unsigned long long last;
    static int level;
    unsigned long long now = mono_ms();
    uint32_t np = MEM32(BO3_VA(0x005729A8u, 0x00572CB8u, 0x00572D58u)), k, ids[8];
    bo3_lr_entry q[BO3_LR_QUEUE];
    char per[200];
    int o = 0, w = 0, nid = 0, have = 0, lv, i;
    if (now - last < 1000)
        return;
    last = now;
    per[0] = 0;
    for (k = 0; k < np && k < 8; k++) {
        uint32_t P = MEM32(BO3_VA(0x00572988u, 0x00572C98u, 0x00572D38u) + 4 * k), n = 0;
        if (P < 0x10000u || P >= 0x04000000u - 0xD00u)
            continue;
        ids[nid++] = MEM32(P + 8);
        if (!have) {
            if (!read_resend_queue(P, q))
                continue;
            have = 1;
            for (i = 0; i < BO3_LR_QUEUE; i++)
                w += !q[i].done;
        }
        for (i = 0; i < BO3_LR_QUEUE; i++)
            n += !q[i].done && q[i].dest == MEM32(P + 8);
        if (n)
            append(per, sizeof per, &o, " peer %X: %u", MEM32(P + 8), n);
    }
    if (have && w) {
        int g = bo3_linkresend_gone(q, BO3_LR_QUEUE, ids, nid);
        if (g)
            append(per, sizeof per, &o, " gone players: %d", g);
    }
    lv = w >= 32 ? 3 : w >= 24 ? 2 : w >= 16 ? 1 : (w < 8 ? 0 : level);
    if (lv == level)
        return;
    if (lv > level)
        xnet_hle_note("reliable messages: %d of 32 wait for an answer%s%s", w, per,
                      w >= 32 ? " (full: new ones are lost)" : "");
    else if (!lv)
        xnet_hle_note("reliable messages: the queue has room again (%d waiting)", w);
    level = lv;
}

static void lobby_watch(void)
{
    link_watch();
    queue_watch();
    race_log();
    state_log();
    kick_tick();
    bo3_react_tick();               /* BO-352: the resend and received reactions */
}

/* The watch code, once per title flip on the guest main thread (uw_flip,
 * main.c). It used to run inside every XNet/Winsock stub, which is not only
 * the main thread: the title's worker 0x242340 (started by sub_00242400
 * through CreateThread 0x1D18B0) calls recvfrom 0x3393EE, sendto 0x339417
 * and closesocket 0x339344 itself (0x242340 -> 0x243060 -> 0x242F90 ->
 * 0x2431B0 / 0x243170, 0x242ED0 -> 0x242B00 -> 0x243150), so link_watch,
 * kick_tick and the reactions ran there too, against s_linkstate (also
 * stepped by retry_stuck_links on the main thread), the kick line and the
 * react ticker the lobby frame reads, unlocked. */
static void net_watch(void);

void bo3_online_flip(void)
{
    if (!xnet_hle_started())
        return;
    lobby_watch();
    net_watch();
}

/* TAKEDAWN_NET_WATCH=1 (tests, BO-357): once a second one line per peer, read
 * only: the title's own ping (+0x5D8) and bars' source, its offset to the peer
 * (+0x5E0, frames), our measured round trip and loss, the title's silent flag
 * (+0x4D8C), and how the other car moved: the largest step per sim frame in
 * units, and how many frames changed the car's step by over 0.5 and over 2
 * units against the frame before (kinks: a car in a line changes it by far
 * less), all of it and with the car near. */
static void net_watch(void)
{
    static int on = -1;
    static uint32_t last_frame;
    static struct { uint32_t P; float x, y, z, vx, vy, vz, maxstep, nearstep; uint32_t w1, w5, n, nw1, nw5, nn; } c[8];
    static uint64_t next;
    uint32_t frame, np, k;
    uint64_t now;
#if defined(BO3_RELEASE_BUILD)
    on = 0;                 /* a test switch: not in a package */
#endif
    if (on < 0) {
        const char *e = getenv("TAKEDAWN_NET_WATCH");
        on = e && *e && *e != '0';
    }
    if (!on)
        return;
    frame = MEM32(BO3_VA(0x004A1D84u, 0x004A20A4u, 0x004A2144u));
    np = MEM32(BO3_VA(0x005729A8u, 0x00572CB8u, 0x00572D58u));
    if (np > 8)
        np = 8;
    if (frame != last_frame) {
        uint32_t steps = frame - last_frame;
        last_frame = frame;
        for (k = 0; k < np && steps < 8; k++) {
            uint32_t P = MEM32(BO3_VA(0x00572988u, 0x00572C98u, 0x00572D38u) + 4 * k), car, m, lnk;
            int nearcar;
            float x, y, z, dx, dy, dz, d, e;
            if (P < 0x10000u || P >= 0x04000000u - 0x5000u)
                continue;
            car = MEM32(P + 0x6F4);
            if (car < 0x10000u || car >= 0x04000000u - 0x2000u || !MEM8(P + 0x5D4))
                continue;
            m = MEM32(car + 0x204);
            if (m < 0x10000u || m >= 0x04000000u - 0x40u)
                continue;
            /* the title's own near flag (sub_0010EFB0): in range and on screen,
             * the only case where it runs the car's physics; a far car is just
             * placed at each packet */
            lnk = MEM32(car + 0x1550);
            nearcar = lnk >= 0x10000u && lnk < 0x04000000u - 0x2000u && MEM8(lnk + 0x1AA0);
            x = MEMF(m + 0x30); y = MEMF(m + 0x34); z = MEMF(m + 0x38);
            if (c[k].P == P) {
                dx = (x - c[k].x) / (float)steps; dy = (y - c[k].y) / (float)steps; dz = (z - c[k].z) / (float)steps;
                d = sqrtf(dx * dx + dy * dy + dz * dz);
                e = sqrtf((dx - c[k].vx) * (dx - c[k].vx) + (dy - c[k].vy) * (dy - c[k].vy) +
                          (dz - c[k].vz) * (dz - c[k].vz));
                c[k].vx = dx; c[k].vy = dy; c[k].vz = dz;
                if (d > c[k].maxstep)
                    c[k].maxstep = d;
                c[k].w1 += e > 0.5f;
                c[k].w5 += e > 2.0f;
                c[k].n += steps;
                if (nearcar) {
                    if (d > c[k].nearstep)
                        c[k].nearstep = d;
                    c[k].nw1 += e > 0.5f;
                    c[k].nw5 += e > 2.0f;
                    c[k].nn += steps;
                }
            }
            c[k].P = P;
            c[k].x = x; c[k].y = y; c[k].z = z;
        }
    }
    now = mono_ms();
    if (now < next)
        return;
    next = now + 1000;
    for (k = 0; k < np; k++) {
        uint32_t P = MEM32(BO3_VA(0x00572988u, 0x00572C98u, 0x00572D38u) + 4 * k);
        unsigned rmin = 0, rmed = 0;
        int sent = 0, lost = 0, have;
        uint32_t id;
        if (P < 0x10000u || P >= 0x04000000u - 0x5000u)
            continue;
        /* +0x24 holds the address the other way round from the XNADDR's */
        id = MEM32(P + 0x24);
        id = id >> 24 | (id >> 8 & 0xFF00u) | (id << 8 & 0xFF0000u) | id << 24;
        have = xnet_hle_peer_rtt(id, &rmin, &rmed, &sent, &lost);
        xnet_hle_note("watch peer %u: title ping %.0f ms, offset %d frames, link %u, silent %u; "
                      "ours %s %u/%u ms, lost %d of %d; car max step %.2f, kinks %u over 0.5, %u over 2, in %u frames; "
                      "near max step %.2f, kinks %u over 0.5, %u over 2, in %u frames",
                      k, (double)MEMF(P + 0x5D8), (int)MEM32(P + 0x5E0), MEM32(P + 0x4F0), MEM8(P + 0x4D8C),
                      have ? "rtt" : "none", rmin, rmed, lost, sent, (double)c[k].maxstep, c[k].w1, c[k].w5, c[k].n,
                      (double)c[k].nearstep, c[k].nw1, c[k].nw5, c[k].nn);
        c[k].maxstep = c[k].nearstep = 0;
        c[k].w1 = c[k].w5 = c[k].n = c[k].nw1 = c[k].nw5 = c[k].nn = 0;
    }
}

void bo3_online_stub(uint32_t va, bo3_online_fn title_fn)
{
    uint32_t i;
    if (!xnet_hle_on()) {
        title_fn();
        return;
    }
    {
        static int trace = -1;
        if (trace < 0) {
            const char *e = getenv("TAKEDAWN_NET_TRACE");
            trace = e && *e && *e != '0';
        }
        /* every call but the per-frame polls */
        if (trace && va != BO3_VA(0x003393ABu, 0x003396ABu, 0x0033974Bu) && va != BO3_VA(0x003393D4u, 0x003396D4u, 0x00339774u) && va != BO3_VA(0x003393EEu, 0x003396EEu, 0x0033978Eu) &&
            va != BO3_VA(0x0033932Eu, 0x0033962Eu, 0x003396CEu) && va != BO3_VA(0x0020F9AEu, 0x0020FCBEu, 0x0020FD5Eu) && va != BO3_VA(0x00339506u, 0x00339806u, 0x003398A6u) && va != BO3_VA(0x0031AA80u, 0x0031AD80u, 0x0031AE20u))
            xnet_hle_log("stub %08X from %08X args %08X %08X %08X %08X %08X", va, MEM32(g_esp),
                         ARG(0), ARG(1), ARG(2), ARG(3), ARG(4));
    }
    if (va == BO3_VA(0x0020F9AEu, 0x0020FCBEu, 0x0020FD5Eu)) {     /* XNetGetEthernetLinkStatus() */
        static int said;
        if (!said++)
            xnet_hle_log("XNetGetEthernetLinkStatus -> cable in");
        ret(xnet_hle_link_status(), 0);
        return;
    }
    if (xnet(va) || xonline(va))
        return;
    for (i = 0; i < sizeof k_xonline_other / sizeof k_xonline_other[0]; i++)
        if (k_xonline_other[i].va == va) {
            xnet_hle_log("XOnline stub 0x%08X (args %08X %08X %08X %08X): not implemented -> E_FAIL",
                         va, ARG(0), ARG(1), ARG(2), ARG(3));
            ret(E_FAIL, k_xonline_other[i].argbytes);
            return;
        }
    xnet_hle_log("stub 0x%08X: no handler, the title's own runs", va);
    title_fn();
}

/* The online lobby screen object: the argument of 0xA05B0 (its readiness
 * check, every lobby frame), kept for bo3_online_fd660. */
static uint32_t s_lobby_screen;
static unsigned long long s_lobby_screen_ms;   /* when it was last seen */
static unsigned long long s_lobby_leave_ms;    /* when it was last seen in its "leaving" state 0x0A */

static unsigned long long mono_ms(void)
{
#ifdef _WIN32
    return (unsigned long long)GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000ull + (unsigned long long)ts.tv_nsec / 1000000ull;
#endif
}

/* BO-344: a lobby screen stuck in 0xB (startwatch.h). What it waits for, in
 * the [NET] log: its linked flag (+0x13A4: every peer's link up and every
 * peer's own link report, 0xA05B0/0x102730), the peer table rebuilt from the
 * record (0x572985, 0xFD6B0), the session's open request (0x57BCFC: +0x10
 * error, +0x14 busy, +0x15 answered), and each peer: its id (+8), link (+0x4F0,
 * 3 = up) and its last link report to us (+0x580 ids, +0x5A0 states): how
 * many links it says are up and whether ours ([+0xC] + 0x78) is one. */
static void start_report(uint32_t scr, unsigned long long ms)
{
    const uint32_t ses = BO3_VA(0x0057BCFCu, 0x0057C00Cu, 0x0057C0ACu);
    uint32_t np = MEM32(BO3_VA(0x005729A8u, 0x00572CB8u, 0x00572D58u)), rec, k, j;
    char peers[400];
    int o = 0;
    peers[0] = 0;
    for (k = 0; k < np && k < 8; k++) {
        uint32_t P = MEM32(BO3_VA(0x00572988u, 0x00572C98u, 0x00572D38u) + 4 * k), up = 0, me = 0, mgr, self = 0;
        if (P < 0x10000u || P >= 0x04000000u - 0xD00u) {
            append(peers, sizeof peers, &o, " %u:-", k);
            continue;
        }
        mgr = MEM32(P + 0xC);
        if (mgr >= 0x10000u && mgr < 0x04000000u - 0x100u)
            self = MEM32(mgr + 0x78);
        for (j = 0; j < 8; j++)
            if (MEM32(P + 0x5A0u + 4 * j) == 3) {
                up++;
                me |= MEM32(P + 0x580u + 4 * j) == self;
            }
        append(peers, sizeof peers, &o, " %u:id=%08X link=%u report=%u up%s", k, MEM32(P + 8), MEM32(P + 0x4F0),
               up, me ? " incl. us" : ", not us");
    }
    rec = MEM32(BO3_VA(0x0057BD34u, 0x0057C044u, 0x0057C0E4u));
    xnet_hle_note("start: still waiting in 'the game is about to launch' after %llu ms: linked=%u alive=%u "
                  "table rebuilt=%u, %u peer(s); session error=%u busy=%u answered=%u; sysflags=0x%X;%s",
                  ms, MEM8(scr + 0x13A4u), MEM8(scr + 0x13A5u), MEM8(BO3_VA(0x00572985u, 0x00572C95u, 0x00572D35u)),
                  np, MEM32(ses + 0x10), MEM8(ses + 0x14), MEM8(ses + 0x15),
                  (rec >= 0x10000u && rec < 0x04000000u) ? MEM32(rec + 0x114) : 0u, peers[0] ? peers : " no peers");
}

static bo3_startwatch s_startwatch;
static uint32_t s_leave_scr;   /* the screen to take out of its game (bo3_online_fd660) */
static bo3_linkresend s_linkresend;

/* BO-379: the title's reliable-message queue of peer P's session
 * ([P + 0xC], linkresend.h) into q; 0 when P has no readable session */
static int read_resend_queue(uint32_t P, bo3_lr_entry *q)
{
    uint32_t S = MEM32(P + 0xC), i;
    if (S < 0x10000u || S >= 0x04000000u - 0x340u)
        return 0;
    for (i = 0; i < BO3_LR_QUEUE; i++) {
        uint32_t e = S + 0xA0u + 0x14u * i, d = MEM32(e + 0xC);
        q[i].dest = MEM32(e);
        q[i].done = MEM8(e + 0x10);
        q[i].chan = (d >= 0x10000u && d < 0x04000000u - 4u) ? MEM8(d + 3) : 0xFF;
    }
    return 1;
}

/* BO-344: our link report to every peer goes out again (linkresend.h). Run
 * just before 0xA05B0, so its 0x102610 sends them in this frame. Not while
 * an earlier report to that peer waits for its acknowledgement (BO-379). */
static void resend_link_reports(void)
{
    uint32_t np = MEM32(BO3_VA(0x005729A8u, 0x00572CB8u, 0x00572D58u)), k;
    bo3_lr_entry q[BO3_LR_QUEUE];
    for (k = 0; k < np && k < 8; k++) {
        uint32_t P = MEM32(BO3_VA(0x00572988u, 0x00572C98u, 0x00572D38u) + 4 * k);
        if (P < 0x10000u || P >= 0x04000000u - 0xD00u)
            continue;
        if (!bo3_linkresend_peer(MEM32(P + 0x4F0), MEM8(P + 0xC80)))
            continue;
        if (!read_resend_queue(P, q) ||
            bo3_linkresend_waiting(q, BO3_LR_QUEUE, MEM32(P + 8), BO3_LR_CHANNEL))
            continue;
        MEM32(P + 0x540) = BO3_LR_STALE;
    }
}

/* BO-345: a link stuck at 2 (our link message sent, no answer) for 1.5 s, or
 * given up (4), goes back to 0 so the title's 0xFD830 looks the peer up,
 * probes and sends its link message again (linkstate.h). Lobby frames only,
 * at most 3 times per peer per game, every one logged. */
static void retry_stuck_links(unsigned long long now)
{
    uint32_t np = MEM32(BO3_VA(0x005729A8u, 0x00572CB8u, 0x00572D58u)), k;
    for (k = 0; k < np && k < 8; k++) {
        uint32_t P = MEM32(BO3_VA(0x00572988u, 0x00572C98u, 0x00572D38u) + 4 * k), was;
        bo3_linkstate *e = &s_linkstate[k];
        if (P < 0x10000u || P >= 0x04000000u - 0xD00u)
            continue;
        link_state_log(k, P, now);
        was = e->state;
        if (e->peer != P || !bo3_linkstate_retry(e, now))
            continue;
        MEM32(P + 0x4F0) = 0;
        xnet_hle_note("peer %u: link retry %u of %u (it was in state %u), %llu ms after it appeared", k,
                      e->retries, BO3_LS_RETRIES, was, now - e->appeared);
    }
}

/* BO-352: the lobby's status line, reactions and kick reasons go in the
 * game lobby's top message line, written straight into its UTF-16 buffer
 * (LINE_BUF). Nothing is allocated or set up: the title draws the buffer each
 * frame, so there is nothing to release and nothing to leak.
 *
 * Until int044 these went to the bottom ticker widget (0x5509C8) through its
 * setter sub_000C9240, and none of them ever showed: the lobby's bottom line
 * kept the title's "Single Race - <track> - 3 Laps - ..." (drawn from the
 * static text slot 0x557E5E, which no set of ours changed; the block we
 * handed the setter was referenced by no draw object afterwards), and the
 * Xbox Live menu, where a kicked player lands, has no bottom line at all.
 * Every set also took three shared-pool objects and one front-end root node
 * that only the widget's release gives back; without the release that was
 * crash0211 (int043: the root pool ran dry, the results page got node 0 + 8,
 * and the tree walk sub_001C1740 recursed without end). This writer does not
 * touch that widget or either pool at all.
 *
 * The title writes the line too (its "Welcome to game NAME" when the lobby
 * opens). Text in the buffer that we did not write is the title's: it stays
 * up for BO3_LINE_TITLE_MS before the status line comes back; a reaction or
 * a kick reason (urgent) does not wait. */
int bo3_online_line_shown(void)
{
    return g_xbox_mem_offset && MEM32(LINE_DESC) == LINE_BUF;
}

static void lobby_line(const char *text, int urgent)
{
    static uint16_t s_known[LINE_UNITS];    /* what the buffer held after our last look */
    static long long s_title_ms = -1;       /* when the title's own text was seen */
    uint16_t want[LINE_UNITS], have[LINE_UNITS];
    unsigned long long now = mono_ms();
    int i, n, act;
    if (!bo3_online_line_shown())
        return;
    for (n = 0; text[n] && n < (int)LINE_UNITS - 1; n++) {
        unsigned char c = (unsigned char)text[n];
        want[n] = (uint16_t)(c >= 0x20 && c < 0x7F ? c : '?');
    }
    want[n] = 0;
    for (i = 0; i < (int)LINE_UNITS - 1; i++)
        if (!(have[i] = MEM16(LINE_BUF + 2u * (uint32_t)i)))
            break;
    have[i] = 0;
    act = bo3_line_action(!bo3_u16eq(have, s_known), bo3_u16eq(have, want), urgent,
                          s_title_ms >= 0 ? (long long)now - s_title_ms : -1);
    if (act == BO3_LINE_TITLE) {
        char t[LINE_UNITS];
        for (i = 0; have[i]; i++)
            t[i] = have[i] >= 0x20 && have[i] < 0x7F ? (char)have[i] : '?';
        t[i] = 0;
        memcpy(s_known, have, sizeof have);
        s_title_ms = (long long)now;
        xnet_hle_note("lobby line: the title's own text '%s'", t);
    } else if (act == BO3_LINE_WRITE) {
        for (i = 0; i <= n; i++)
            MEM16(LINE_BUF + 2u * (uint32_t)i) = want[i];
        memcpy(s_known, want, sizeof want);
    }
}

/* BO-352: the lobby's own status line (lobbystatus.h). Gathered each lobby
 * frame and written to the top line when it changes (lobby_line); a kick
 * reason, then a reaction, win over it. */
static void status_tick(uint32_t scr)
{
    xnet_hle_lobby_view lv;
    bo3_status_in in;
    char text[160];
    uint32_t np, P;
    int k;
    if (!xnet_hle_lobby_view_get(&lv))
        return;
    memset(&in, 0, sizeof in);
    in.players = lv.np > 0 && lv.np <= 8 ? lv.np : 0;
    for (k = 0; k < in.players; k++)
        in.ready[k] = lv.ready[k];
    in.host = in.players > 0 && lv.name[0][0] && !strcmp(lv.name[0], xnet_hle_gamertag());
    in.state = (int)MEM8(scr + 0x13ACu);
    in.countdown = bo3_quickplay_countdown_seconds();   /* the host's start countdown of a QUICK PLAY game */
    in.voting = bo3_vote_busy();
    in.code = xnet_hle_code();
    in.port = 1000u + xnet_hle_port_offset();
    in.lang = status_lang();
    in.coming = bo3_stay_coming();
    np = MEM32(BO3_VA(0x005729A8u, 0x00572CB8u, 0x00572D58u));
    for (k = 1; k < in.players && k < 8; k++) {
        P = k < (int)np ? MEM32(BO3_VA(0x00572988u, 0x00572C98u, 0x00572D38u) + 4u * (uint32_t)k) : 0;
        if (s_unlinked[k] && P && MEM32(P + 0x4F0) != 3) {
            in.unreach = lv.name[k][0] ? lv.name[k] : NULL;
            break;
        }
    }
    if (s_kick_line[0]) {
        lobby_line(s_kick_line, 1);
        return;
    }
#ifdef BO3_HAVE_REACT
    {
        /* a reaction to show wins over the status line */
        char rt[160];
        if (bo3_react_ticker_text(rt, sizeof rt) && rt[0]) {
            lobby_line(rt, 1);
            return;
        }
    }
#endif
    bo3_status_line(&in, text, sizeof text);
    if (text[0])
        lobby_line(text, 0);
}

void bo3_online_lobby_screen(bo3_online_fn title_fn)
{
    uint32_t scr = 0;
    unsigned st0 = 0;
    if (xnet_hle_on()) {
        scr = MEM32(g_esp + 4);
        s_lobby_screen = scr;
        s_lobby_screen_ms = mono_ms();
        if (scr >= 0x10000u && scr < 0x04000000u - 0x2000u && bo3_linkresend_due(&s_linkresend, s_lobby_screen_ms))
            resend_link_reports();
        if (scr >= 0x10000u && scr < 0x04000000u - 0x2000u) {
            kick_box_prepare();
            retry_stuck_links(s_lobby_screen_ms);
            bo3_quickplay_lobby_frame(scr);
            bo3_vote_lobby_frame(scr);
            status_tick(scr);
            if (MEM8(scr + 0x13ACu) == 0x0Au)
                s_lobby_leave_ms = s_lobby_screen_ms;
            st0 = MEM8(scr + 0x13ACu);
        }
    }
    title_fn();
    if (scr && scr >= 0x10000u && scr < 0x04000000u - 0x2000u) {
        unsigned long long now = mono_ms();
        bo3_quickplay_host_start_after(scr, st0);   /* BO-384: the host's race takes the game record */
        bo3_react_lobby_frame(scr);     /* BO-352: the reaction bubble */
        int r = bo3_startwatch_step(&s_startwatch, scr, MEM8(scr + 0x13ACu), now);
        if (r == BO3_SW_REPORT)
            start_report(scr, now + 1 - s_startwatch.since);
        else if (r == BO3_SW_LEAVE) {
            start_report(scr, now + 1 - s_startwatch.since);
            s_leave_scr = scr;
        }
        if (MEM8(scr + 0x13ACu) != 0x0Bu && s_leave_scr == scr)
            s_leave_scr = 0;
    }
}

/* The screen object, only while the screen runs (0xA05B0 every lobby frame):
 * a pointer from a screen that has gone may be freed memory. */
static uint32_t lobby_screen_now(void)
{
    if (!s_lobby_screen || s_lobby_screen < 0x10000u || s_lobby_screen >= 0x04000000u - 0x2000u ||
        mono_ms() - s_lobby_screen_ms > 1000) {
        s_lobby_screen = 0;
        return 0;
    }
    return s_lobby_screen;
}

/* The online navigator (onlinebot.c): the game lobby screen's state byte
 * and its two flags, while the screen runs. 0 when it does not. */
int bo3_online_lobby_now(uint8_t *state, uint8_t *linked, uint8_t *alive)
{
    uint32_t scr = lobby_screen_now();
    if (!scr)
        return 0;
    if (state) *state = MEM8(scr + 0x13ACu);
    if (linked) *linked = MEM8(scr + 0x13A4u);
    if (alive) *alive = MEM8(scr + 0x13A5u);
    return 1;
}

/* The screen's leaving state, seen in the last seconds: the lobby state's
 * transition back runs after the page's fade and the lobby's answer, by then
 * the screen itself may have stopped running. */
int bo3_online_lobby_leaving(void)
{
    unsigned long long now = mono_ms();
    return s_lobby_leave_ms && now - s_lobby_leave_ms < 10000ull;
}

/* 0xFD660 answers "may this machine change the game" (the host, lobby idle).
 * The lobby screen (0x9EE60) asks it at 0x9F121 to undo a start lock (SYSFLAGS
 * bit 12) left in the game record: if the record still has the lock and the
 * screen is not in its "locked, waiting" state 0x11, it unlocks the game
 * (0x1BD870) and goes back to the options. But the host enters state 0xB
 * (start the race, 0x9F545) by sending gsta in the frame it sees the lock,
 * and the record cannot lose the lock before the next frame: any reply needs
 * a round trip, and a reply that early would also break the peer messages
 * (see the TCP hold in the toolkit's xnet_hle.c). On Xbox Live the lock's
 * echo came back late enough that the host was already past this check; on
 * a lobby a few milliseconds away it is not. So in state 0xB, and only for
 * this caller and only online, the leftover-lock check is answered "no", and
 * the host goes on to the race (BO-134). */
void bo3_online_fd660(bo3_online_fn title_fn)
{
    uint32_t ret = MEM32(g_esp);
    uint32_t scr = ret == BO3_VA(0x0009F126u, 0x0009F326u, 0x0009F446u) ? lobby_screen_now() : 0;
    if (xnet_hle_on() && scr && MEM8(scr + 0x13ACu) == 0x0Bu) {
        static int n;
        if (n++ < 20)
            xnet_hle_log("start: leftover-lock check skipped in state 0xB");
        if (s_leave_scr == scr) {
            /* BO-344: 10 s in 0xB (startwatch.h). Leave the game the way the
             * title's own "leave the game?" YES does (0xA1932): state 0xA and
             * the session's leave (0x1BAF70); state 0xA (0x9F5FB) then closes
             * the peer links (0x1B7CC0) and goes back to the menus once the
             * lobby has answered. Done here, early in the screen's frame:
             * from 0xA05B0 the screen's next step (0xA1560) would put it
             * back in 0xB, as the game record still says "started". */
            uint32_t a = g_eax, b = g_ebx, c = g_ecx, d = g_edx, si = g_esi, di = g_edi, sp = g_esp;
            s_leave_scr = 0;
            MEM8(scr + 0x13ACu) = 0x0Au;
            PUSH32(g_esp, 0);
            g_ecx = BO3_VA(0x0057BCFCu, 0x0057C00Cu, 0x0057C0ACu);
            BO3_SUB(sub_001BAF70, sub_001BB290, sub_001BB330)();
            g_eax = a; g_ebx = b; g_ecx = c; g_edx = d; g_esi = si; g_edi = di; g_esp = sp;
            xnet_hle_note("start: this copy never got into the race; it leaves the game (BO-344)");
            xnet_hle_message("The race started before this copy could reach every player in time, "
                             "so it has left that game. Join another one.");
        }
        g_eax &= ~0xFFu;
        g_esp += 4;
        return;
    }
    title_fn();
}

/* ── LAN mode ──────────────────────────────────────────────────────── */

#define LAN_BEACON 21809

static void set_env(const char *n, const char *v)
{
#ifdef _WIN32
    _putenv_s(n, v);
#else
    setenv(n, v, 1);
#endif
}

static void lobby_log(const char *line)
{
    xnet_hle_log("lobby: %s", line);
}

/* The name other players see: a neutral RacerNNNN, chosen once and kept in
 * settings.toml (online.name), never the computer's account name. */
static void ensure_name(void)
{
    extern const char *xbox_SettingsText(const char *key, int *locked);
    extern void xbox_SettingsSetText(const char *key, const char *value);
    extern void xbox_SettingsFlush(void);
    const char *nm = getenv("TAKEDAWN_GAMERTAG");
    char v[16];
    if (nm && *nm)
        return;
    nm = xbox_SettingsText("online.name", NULL);
    if (nm && *nm)
        return;
    srand((unsigned)time(NULL) ^ (unsigned)(uintptr_t)&v);
    snprintf(v, sizeof v, "Racer%04u", (unsigned)(rand() % 10000));
    xbox_SettingsSetText("online.name", v);
    xbox_SettingsFlush();
    set_env("RECOMP_ONLINE_NAME", v);
}

void bo3_online_init(void)
{
    const char *m = getenv("RECOMP_ONLINE");
    char ip[32];
    aries_config c;
    xnet_hle_set_lobby_tag(tag_kick);   /* BO-351 */
    if (!m || !*m)
        m = getenv("TAKEDAWN_ONLINE");
    if (!m || !*m || !strcmp(m, "off") || !strcmp(m, "0"))
        return;
    ensure_name();
    if (strcmp(m, "lan"))
        return;
    /* Another copy on the network already runs one: use it. */
    if (aries_find_lan(LAN_BEACON, 2500, ip, sizeof ip)) {
        xnet_hle_log("LAN lobby found at %s", ip);
        set_env("RECOMP_ONLINE_SERVER", ip);
        return;
    }
    memset(&c, 0, sizeof c);
    c.port = 21810;
    c.beacon_port = LAN_BEACON;
    c.log = lobby_log;
#ifdef XNET_HLE_HAS_LAN_ADDR
    {
        /* BO-349: the beacon names this machine's LAN address, found the way
         * the online layer finds its own. The lobby's own probe (a route
         * out) gives 127.0.0.1 on a LAN with no gateway, and the others
         * would look for the lobby there. */
        static char me[16];
        uint32_t a = xnet_hle_lan_addr();
        if (a) {
            const uint8_t *b = (const uint8_t *)&a;
            snprintf(me, sizeof me, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
            c.public_ip = me;
        }
    }
#endif
    if (aries_start_thread(&c) == 0) {
        xnet_hle_log("no LAN lobby found: this copy runs one");
        set_env("RECOMP_ONLINE_SERVER", "127.0.0.1");
    }
}
