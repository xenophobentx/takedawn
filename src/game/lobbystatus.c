/*
 * lobbystatus.c - the game lobby's status line and kick reason (lobbystatus.h,
 * BO-352 steps 5a and 5c). The texts come from the per-language table
 * (statustext.c, step 9); every line is upper case and printable ASCII, the
 * title font's alphabet.
 */
#include "lobbystatus.h"

#include <stdio.h>
#include <string.h>

#include "statustext.h"

/* How many players have no car yet (ready value 0): they are "picking a car". */
static int picking(const bo3_status_in *in)
{
    int i, n = 0;
    for (i = 0; i < in->players && i < 8; i++)
        if (in->ready[i] == 0)
            n++;
    return n;
}

/* Every guest has a car (ready 1 or 3): the host may start. */
static int all_cars(const bo3_status_in *in)
{
    int i;
    for (i = 0; i < in->players && i < 8; i++)
        if (in->ready[i] == 0)
            return 0;
    return in->players > 0;
}

/* the status itself, without the code */
static void status_text(const bo3_status_in *in, char *out, size_t n)
{
    /* most urgent first */
    if (in->countdown > 0) {
        snprintf(out, n, bo3_statustext(in->lang, BO3_TXT_STARTING_IN), in->countdown);
        return;
    }
    if (in->state == 0x0B) {
        snprintf(out, n, "%s", bo3_statustext(in->lang, BO3_TXT_STARTING));
        return;
    }
    if (in->voting) {
        snprintf(out, n, "%s", bo3_statustext(in->lang, BO3_TXT_VOTING));
        return;
    }
    if (in->unreach && in->unreach[0]) {
        snprintf(out, n, bo3_statustext(in->lang, BO3_TXT_CANT_REACH), in->unreach, in->port);
        return;
    }
    /* stay together: the game waits for the slower players of the race */
    if (in->coming > 0) {
        snprintf(out, n, bo3_statustext(in->lang, BO3_TXT_WAIT_RACERS), in->coming);
        return;
    }
    if (all_cars(in)) {
        snprintf(out, n, "%s", bo3_statustext(in->lang, in->host ? BO3_TXT_HOST_START : BO3_TXT_WAIT_HOST));
        return;
    }
    {
        int p = picking(in);
        if (p > 0)
            snprintf(out, n, bo3_statustext(in->lang, BO3_TXT_WAIT_PICKING), in->players, BO3_LOBBY_MAX, p);
        else
            snprintf(out, n, bo3_statustext(in->lang, BO3_TXT_WAIT), in->players, BO3_LOBBY_MAX);
    }
}

void bo3_status_line(const bo3_status_in *in, char *out, size_t n)
{
    char st[128];
    if (!in || !out || !n)
        return;
    out[0] = 0;
    st[0] = 0;
    status_text(in, st, sizeof st);
    /* a coded game (CREATE GAME, JOIN WITH CODE) keeps its code in front of
     * whatever the status says, so the host can read it out at any time */
    if (in->code && in->code[0]) {
        size_t o = (size_t)snprintf(out, n, bo3_statustext(in->lang, BO3_TXT_CODE), in->code);
        if (st[0] && o + 3 < n)
            snprintf(out + o, n - o, " - %s", st);
        return;
    }
    snprintf(out, n, "%s", st);
}

void bo3_status_kick_line(int why, int lang, unsigned port, char *out, size_t n)
{
    int id = 0;
    (void)port;                     /* no text names it any more (BO-364: a closed port may be either side's) */
    if (!out || !n)
        return;
    out[0] = 0;
    switch (why) {
    case BO3_STATUS_KICK_NOLINK:
        id = BO3_TXT_REMOVED_NOLINK;
        break;
    case BO3_STATUS_KICK_HOST:
        id = BO3_TXT_REMOVED_HOST;
        break;
    case BO3_STATUS_KICK_CLOSED:
        id = BO3_TXT_GAME_ENDED;
        break;
    default:
        return;
    }
    if (id)
        snprintf(out, n, "%s", bo3_statustext(lang, id));
}

void bo3_status_kick_box(int why, int lang, int plat, unsigned port1, unsigned port2, char *out, size_t n)
{
    size_t o;
    int hint;
    if (!out || !n)
        return;
    out[0] = 0;
    if (why == BO3_STATUS_KICK_HOST) {
        snprintf(out, n, "%s", bo3_statustext(lang, BO3_TXT_BOX_HOST));
        return;
    }
    if (why != BO3_STATUS_KICK_NOLINK)
        return;
    /* either side may be the one that blocks: this computer's own setting
     * first, then the host's ports (online.md, Players behind routers) */
    hint = plat == BO3_PLAT_MAC ? BO3_TXT_BOX_HINT_MAC : plat == BO3_PLAT_WINDOWS ? BO3_TXT_BOX_HINT_WIN
         : plat == BO3_PLAT_LINUX ? BO3_TXT_BOX_HINT_LINUX : -1;
    o = (size_t)snprintf(out, n, "%s", bo3_statustext(lang, BO3_TXT_BOX_NOLINK));
    if (hint >= 0 && o + 1 < n)
        o += (size_t)snprintf(out + o, n - o, " %s", bo3_statustext(lang, hint));
    if (o + 1 < n) {
        out[o++] = ' ';
        snprintf(out + o, n - o, bo3_statustext(lang, BO3_TXT_BOX_HINT_PORTS), port1, port2);
    }
}

int bo3_kick_box_frame(bo3_kick_box *b, int why, unsigned long long now, int box_open)
{
    if (!b)
        return 0;
    if (!why) {
        b->why = 0;
        return 0;
    }
    if (b->why != why) {                    /* it just went in */
        b->why = why;
        b->since = now;
        b->seen = 0;
    }
    if (box_open) {
        b->seen = 1;
        if (now - b->since < BO3_KICK_BOX_MAX_MS)
            return 0;
    } else if (!b->seen && now - b->since < BO3_KICK_BOX_MS)
        return 0;
    b->why = 0;
    return b->seen ? 1 : 2;
}

/* "KICK=<name>" at the start of a line of the body (KEY=VALUE lines): the
 * name into out (n bytes). 1 when found and non-empty. */
static int kick_name(const char *body, int n, char *out, int cap)
{
    const char *p = body, *end = body + n;
    out[0] = 0;
    while (p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        const char *le = nl ? nl : end;
        if (le - p >= 5 && !memcmp(p, "KICK=", 5)) {
            int i = 0;
            p += 5;
            while (p < le && *p && *p != '\r' && i + 1 < cap)
                out[i++] = *p++;
            out[i] = 0;
            return i > 0;
        }
        p = nl ? nl + 1 : end;
    }
    return 0;
}

int bo3_status_kick_reason(const uint8_t *pkt, int n, const char *const names[8],
                           const int links[8], int np)
{
    char name[32];
    int k;
    if (!pkt || n < 12 || memcmp(pkt, "gset", 4))
        return BO3_STATUS_KICK_NONE;
    if (!kick_name((const char *)pkt + 12, n - 12, name, sizeof name))
        return BO3_STATUS_KICK_NONE;
    for (k = 0; k < np && k < 8; k++)
        if (names[k] && !strcmp(names[k], name))
            return links[k] != 3 ? BO3_STATUS_KICK_NOLINK : BO3_STATUS_KICK_HOST;
    return BO3_STATUS_KICK_HOST;   /* not in the game any more: a host's choice */
}

int bo3_kick_msg_frame(bo3_kick_msg *k, unsigned long long now, int on_ticker_page)
{
    if (!k || !k->why)
        return 0;
    if (k->tick_ms) {                       /* the ticker is already showing it */
        if (now - k->tick_ms >= BO3_KICK_TICKER_MS) {
            k->why = 0;
            k->tick_ms = 0;
            return 0;
        }
        return 1;
    }
    if (on_ticker_page) {                   /* the next ticker page: show it there */
        k->tick_ms = now;
        return 1;
    }
    if (now - k->set_ms >= BO3_KICK_OVERLAY_MS) {   /* no ticker page: the overlay */
        k->why = 0;
        return 2;
    }
    return 0;
}

int bo3_line_action(int title_text, int same, int urgent, long long since_title_ms)
{
    if (title_text && !same)
        return BO3_LINE_TITLE;      /* the title wrote its own text: keep it a while */
    if (same)
        return BO3_LINE_SKIP;       /* it shows this line already */
    if (!urgent && since_title_ms >= 0 && since_title_ms < BO3_LINE_TITLE_MS)
        return BO3_LINE_SKIP;       /* the title's text is still up */
    return BO3_LINE_WRITE;
}

int bo3_u16eq(const uint16_t *a, const uint16_t *b)
{
    for (; *a == *b; a++, b++)
        if (!*a)
            return 1;
    return 0;
}
