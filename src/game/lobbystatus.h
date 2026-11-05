/*
 * lobbystatus.h - the game lobby's own status line (BO-352, step 5a): the
 * short text the bottom ticker shows while the lobby is up, and the reason a
 * kicked player is told (step 5c). A pure function over a gathered view, so
 * a unit test can check every line without the title running.
 *
 * The lines are short on purpose: the title's own lobby text sits in a small
 * buffer, and the status goes in front of it (see lobbystatus.c).
 */
#ifndef BO3_LOBBYSTATUS_H
#define BO3_LOBBYSTATUS_H

#include <stddef.h>
#include <stdint.h>

/* the game's players (the title asks the server for 6) */
#define BO3_LOBBY_MAX 6

/* The lobby as the caller sees it. players and ready come from the lobby's
 * game record (xnet_hle_lobby_view); state from the screen object (+0x13AC);
 * countdown is only our own Quick Play countdown (a host sends it over the
 * side channel); code is this game's code ("" or NULL = public). */
typedef struct {
    int host;            /* this copy hosts the game (players[0] is us) */
    int players;         /* players in the lobby record */
    int ready[8];        /* each one's third USERPARAMS value: 0 no car, 1 car, 3 ready */
    int state;           /* the lobby screen's state byte (0x0B = about to start) */
    int countdown;       /* seconds left, our own countdown; 0 = none */
    int voting;          /* a track vote runs in this lobby (vote.c) */
    const char *code;    /* game code ("" or NULL = public) */
    const char *unreach; /* a peer's name whose link will not come up; NULL = none */
    int coming;          /* players of the last race still on their way back (stay together) */
    unsigned port;       /* this copy's race port (1000 + the port offset) */
    int kick_why;        /* why we were kicked (BO3_STATUS_KICK_*), 0 = not kicked */
    int lang;            /* the player's language (a languages.h index; 0 = English) */
} bo3_status_in;

/* why a player was kicked (the server's TKWHY, step 5b/5c) */
enum { BO3_STATUS_KICK_NONE = 0, BO3_STATUS_KICK_NOLINK, BO3_STATUS_KICK_HOST, BO3_STATUS_KICK_CLOSED };

/* The status line for the lobby ticker, "" when there is nothing to say. */
void bo3_status_line(const bo3_status_in *in, char *out, size_t n);

/* The reason a kicked player is told, "" when there is none. lang: the
 * player's language (a languages.h index; 0 = English). */
void bo3_status_kick_line(int why, int lang, unsigned port, char *out, size_t n);

/* The reason for a host's "gset KICK" (BO3_STATUS_KICK_NOLINK or _HOST),
 * decided from the whole request packet (12-byte header + body, n bytes) and
 * the game's players: names[k] and links[k] (each peer's link state, 3 = up)
 * in the lobby-record order. BO3_STATUS_KICK_NONE when the packet is not such
 * a kick. A kick of a peer whose link is below 3 is the title's own automatic
 * "cannot reach" kick; anything else is a host's choice. */
int bo3_status_kick_reason(const uint8_t *pkt, int n, const char *const names[8],
                           const int links[8], int np);

/* A kick reason that waits for the next game lobby with the top message line
 * (step 5c follow-up). why is set when the player is kicked; the message is
 * shown in that line once such a lobby is up, for BO3_KICK_TICKER_MS, or on
 * the overlay after BO3_KICK_OVERLAY_MS with no such lobby (the usual case:
 * a kicked player lands on the Xbox Live menu). */
#define BO3_KICK_TICKER_MS 8000u
#define BO3_KICK_OVERLAY_MS 3000u
typedef struct {
    int why;                    /* BO3_STATUS_KICK_*, 0 = none */
    unsigned long long set_ms;  /* when it was set */
    unsigned long long tick_ms; /* when the ticker started showing it; 0 = not yet */
} bo3_kick_msg;

/* What to do with a pending kick message this frame: 1 show the kick line in
 * the lobby's top line, 2 show it on the overlay (once), 0 nothing.
 * on_ticker_page: a game lobby with the top line is up now. */
int bo3_kick_msg_frame(bo3_kick_msg *k, unsigned long long now, int on_ticker_page);

/* The title's own kick box (the "You have been kicked out of the room." of
 * the lobby, string 0x44F) with the reason instead, for a kick the server
 * tagged TKWHY=nolink or host: what happened and what to check. plat: the
 * BO3_PLAT_* this copy runs on (the hint names its own settings); ports: the
 * host's race port and its second one (1000 and 6000 plus the offset). ""
 * for any other reason (closed keeps the title's own text). */
enum { BO3_PLAT_OTHER = 0, BO3_PLAT_MAC, BO3_PLAT_WINDOWS, BO3_PLAT_LINUX };
#define BO3_KICK_BOX_MAX 240    /* the longest box text, with the null */
void bo3_status_kick_box(int why, int lang, int plat, unsigned port1, unsigned port2, char *out, size_t n);

/* The kick box's text in the title's string table: put in when the kick
 * arrives (the box is built from the table a frame later), taken out again
 * once the box has been up and closed. When no box opens in
 * BO3_KICK_BOX_MS, the text goes out and the overlay says it instead. */
#define BO3_KICK_BOX_MS 4000u
#define BO3_KICK_BOX_MAX_MS 120000u    /* a box left up this long: the text goes out anyway */
typedef struct {
    int why;                    /* the reason whose text is in, 0 = none */
    unsigned long long since;   /* when it went in */
    int seen;                   /* the box has been up since */
} bo3_kick_box;
/* Each frame with the text in (why = the reason in the table, 0 none):
 * 0 keep it, 1 take it out (the box was shown), 2 take it out and say it on
 * the overlay (no box came). */
int bo3_kick_box_frame(bo3_kick_box *b, int why, unsigned long long now, int box_open);

/* What lobby_line (online.c) does with a line for the lobby's top message
 * line, which it writes straight into the line's buffer. title_text: the
 * buffer holds text we did not write and have not seen (the title's own, such
 * as "Welcome to game NAME"); same: it holds the wanted line already; urgent:
 * a reaction or a kick reason; since_title_ms: ms since the title's text was
 * last seen, -1 for never. The title's text is kept up BO3_LINE_TITLE_MS
 * unless the line is urgent. */
#define BO3_LINE_TITLE_MS 2000
#define BO3_LINE_SKIP  0
#define BO3_LINE_WRITE 1
#define BO3_LINE_TITLE 2        /* note the title's text, write nothing */
int bo3_line_action(int title_text, int same, int urgent, long long since_title_ms);

/* two null-terminated UTF-16 strings are equal */
int bo3_u16eq(const uint16_t *a, const uint16_t *b);

#ifdef BO3_HAVE_REACT
/* Lane C (step 7): the reaction to show in the lobby's top line this frame. 1 and
 * out (n bytes) filled when there is one; 0 when not. The build that has the
 * reactions (BO3_HAVE_REACT) provides it; status_tick prefers it. */
int bo3_react_ticker_text(char *out, int n);
#endif

#endif
