/*
 * aries_server.h - Takedawn's lobby server: a stand-in for EA's Aries lobby
 * as Burnout 3's DirtySock speaks it (BO-134).
 *
 * MIT. Written from what the game sends and what its code reads back (the
 * notebook is tools/lobby/aries_logger.py, which learnt it), not from any
 * other server's code. The game reaches it only over TCP.
 *
 * One file of plain C (POSIX sockets, or Winsock), so it is both the
 * standalone program (main.c: tools/lobby/takedawn-lobby) and the lobby a
 * copy of the game runs for its local network (online.mode = lan).
 */
#ifndef ARIES_SERVER_H
#define ARIES_SERVER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct aries_config {
    uint16_t port;          /* TCP, the game's own: 21810 */
    const char *bind_ip;    /* NULL: every address */
    const char *public_ip;  /* the address clients reach this server at, for
                             * redirects and for players seen on loopback;
                             * NULL: found from the routing table */
    uint16_t beacon_port;   /* UDP LAN beacon (0: none) */
    uint16_t relay_port;    /* UDP relay between a game's members (BO-381; 0: off) */
    void (*log)(const char *line);  /* NULL: stderr */
    int verbose;            /* log every packet */
    int max_sessions;       /* connected players at once (0: 256) */
    int max_games;          /* games at once (0: 128) */
    int max_players;        /* players per game, 2..8 (0: 8; the game asks for 6) */
    int stats_secs;         /* log a "stats:" line this often (0: never) */
    const char *stats_dir;  /* usage totals (counts only) go here: daily.jsonl,
                             * current.json; NULL: nothing is written */
    const char *public_dir; /* the public page's folder: stats.json (every 20 s) and
                             * index.html; NULL: <stats_dir>/public when page_html is
                             * set, else none. Aggregates and open games only. */
    const char *page_html;  /* the text of index.html (main.c embeds one); NULL: none */
    const char *geoip_file; /* BO-367: DB-IP Lite country CSV (IPv4 lines used); sign-ins are
                             * counted per country, no address kept. NULL/missing: "??" */
    const char *admin_token_file; /* BO-367: one line, the secret folder name of the owner's
                             * page under public_dir; NULL/unusable: no owner's page */
    const char *admin_html; /* the owner's page (main.c embeds one) */
    int max_per_ip;         /* sessions from one address (loopback excepted; 0: 64, <0: any) */
    int idle_secs;          /* drop a session that sent nothing this long (0: 90, <0: never) */
    int auth_secs;          /* drop a session that has not signed in by then (0: 25, <0: never) */
    int crowd_test;         /* test lobbies only (BO-366): sessions that say TKCROWD=1 in addr
                             * are load-tool players; their games and the others' are
                             * invisible to each other. 0 (the default): TKCROWD is ignored */
} aries_config;

/* Runs the server until aries_stop(); returns nonzero if it cannot listen. */
int  aries_run(const aries_config *cfg);
void aries_stop(void);

/* A thread running aries_run (the game's LAN mode); 0 on success. */
int  aries_start_thread(const aries_config *cfg);

/* LAN discovery: listens up to wait_ms for a lobby's beacon on the port;
 * fills ip (a.b.c.d) and returns 1 when one answered. */
int  aries_find_lan(uint16_t beacon_port, unsigned wait_ms, char *ip, int n);

#ifdef __cplusplus
}
#endif
#endif
