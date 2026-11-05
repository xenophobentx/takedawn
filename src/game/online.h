/*
 * online.h - Burnout 3's XNet and XOnline entry points on the toolkit's host
 * network layer (xnet_hle.h). Off unless TAKEDAWN_ONLINE=1; see online.c.
 */
#ifndef BO3_ONLINE_H
#define BO3_ONLINE_H

#include <stdint.h>

typedef void (*bo3_online_fn)(void);

/* At start: in LAN mode (online.mode = lan), finds a lobby on the local
 * network or runs one in this copy, and points the online layer at it. */
void bo3_online_init(void);

/* One of the title's XNet/XOnline API stubs, by its address: the generated
 * body runs unless the online layer is on. */
void bo3_online_stub(uint32_t va, bo3_online_fn title_fn);
/* the per-frame watch (links, kick line, reactions): the title's flip on the
 * guest main thread calls it, never a stub */
void bo3_online_flip(void);

/* 0xA05B0, the online lobby screen's readiness check: notes the screen
 * object for bo3_online_fd660. */
void bo3_online_lobby_screen(bo3_online_fn title_fn);

/* The online navigator (onlinebot.c): the game lobby screen's state byte
 * (+0x13AC) and its flags +0x13A4 (every peer linked) and +0x13A5 (alive),
 * while the screen runs; 0 when it does not. */
int bo3_online_lobby_now(uint8_t *state, uint8_t *linked, uint8_t *alive);
/* the lobby screen was in its "leaving" state (0x0A) in the last seconds */
int bo3_online_lobby_leaving(void);

/* The game lobby's top message line is up: its draw entry points at the
 * buffer our status line, reactions and kick reasons go to (online.c,
 * lobby_line). */
int bo3_online_line_shown(void);

/* BO-364: the lobby said a search or join came back empty because the host
 * had kicked this player (TKKICKED=1 in a gsea or gjoi answer) since the last
 * call; 1 once, then 0 */
int bo3_online_removed_take(void);

/* 0xFD660 "the lobby may change the game" (see online.c) */
void bo3_online_fd660(bo3_online_fn title_fn);

#endif
