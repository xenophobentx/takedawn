/*
 * onlineinfo.h - the online info line (BO-352, step 8): how many players are
 * online and how many games are open, from the lobby server's @dir reply, and
 * the line the main menu and the Xbox Live page show ("14 ONLINE - 3 OPEN
 * GAMES - SERVER 38 MS").
 *
 * The server adds TKONLINE and TKWAITING to its @dir reply (lane B/C, step
 * 5b); until it does, the line is just "ONLINE". A small probe (the game's
 * side) sends @dir every 30 s while a menu that shows the line is up, and
 * measures the ping as the round trip. The parser and the text are pure so
 * tests/onlineinfo can run them; the probe itself is scene-tested.
 */
#ifndef BO3_ONLINEINFO_H
#define BO3_ONLINEINFO_H

#include <stddef.h>

/* an integer field of a lobby reply body ("KEY=VALUE" lines); 1 when found */
int bo3_onlineinfo_field_int(const char *body, const char *key, int *out);

/* TKONLINE and TKWAITING of an @dir reply. Returns a mask (1 = TKONLINE,
 * 2 = TKWAITING); *online / *waiting are written only when present. */
int bo3_onlineinfo_parse_status(const char *body, int *online, int *waiting);

/* The line shown. online/waiting < 0 mean "the server did not say": the line
 * is then "ONLINE". ping_ms < 0 means "not measured". */
void bo3_onlineinfo_text(char *out, size_t n, int online, int waiting, int ping_ms);

/* Takedawn Online step 5: the counts as the menus say them. mode: 1 the
 * internet lobby, 2 the local network (no counts: "LOCAL NETWORK"); have: the
 * server said its counts (online, waiting). "" when there is nothing to say
 * (no server answered, or an old one). Singular for 1. */
void bo3_onlineinfo_counts(char *out, size_t n, int mode, int have, int online, int waiting);
/* BO-365: the counts with the races a stranger could wait for appended
 * (" - 1 RACING"; nothing for racing <= 0, which an old server says) */
void bo3_onlineinfo_counts_racing(char *out, size_t n, int mode, int have, int online, int waiting, int racing);
/* the BROWSE GAMES hint: with no open game but racing ones it says a racing
 * game can be picked and waited for (BO-372), else hint */
const char *bo3_onlineinfo_browse_hint(const char *hint, int have, int waiting, int racing);
/* the main menu's line for TAKEDAWN ONLINE: "RACE ONLINE", then the counts */
void bo3_onlineinfo_menu_line(char *out, size_t n, const char *counts);
/* a line of the online page: the counts, then the line's own hint */
void bo3_onlineinfo_page_line(char *out, size_t n, const char *counts, const char *hint);

/* Send @dir to the lobby and read the counts and the ping. Returns the same
 * mask as parse_status (0: could not reach the server); *ping_ms is the round
 * trip, -1 when the request failed. Blocks up to 2 s. */
int bo3_onlineinfo_probe(int *online, int *waiting, int *ping_ms);

/* The counts the menus show, kept fresh (onlineinfo.c's glue): want says a
 * menu that shows them is up (called every flip); while it is, a thread asks
 * the lobby (bo3_onlineinfo_probe) at once and then every 30 s. The newest of
 * that and the counts the lobby pushes to the signed-in title
 * (xnet_hle_lobby_status) is returned: 1 and *online / *waiting, or 0 when no
 * server has said anything. */
void bo3_onlineinfo_want(int want);
/* a page came up: it asks now, not at the next 30 s (signing in changed the
 * count; what the last probe said from before that is old) */
void bo3_onlineinfo_kick(void);
int  bo3_onlineinfo_latest(int *online, int *waiting);

#endif
