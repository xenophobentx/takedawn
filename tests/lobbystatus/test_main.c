/* lobbystatus test: see CMakeLists.txt (BO-352 steps 5a, 5c and 9). */
#include <stdio.h>
#include <string.h>

#include "lobbystatus.h"
#include "statustext.h"
#include "languages.h"

static int fails, checks;

#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static void base(bo3_status_in *in)
{
    memset(in, 0, sizeof *in);
    in->port = 31000;
}

int main(void)
{
    char m[160];
    bo3_status_in in;

    /* the plain wait: players and those still picking a car */
    base(&in);
    in.host = 1;
    in.players = 3;
    in.ready[0] = 3; in.ready[1] = 0; in.ready[2] = 0;
    bo3_status_line(&in, m, sizeof m);
    CHECK(strcmp(m, "WAITING FOR PLAYERS 3/6 - 2 PICKING A CAR") == 0);

    /* nobody picking: the host is told to start */
    base(&in);
    in.host = 1;
    in.players = 2;
    in.ready[0] = 3; in.ready[1] = 3;
    bo3_status_line(&in, m, sizeof m);
    CHECK(strcmp(m, "HOST: PRESS START WHEN READY") == 0);

    /* a guest waits on the host, not on itself */
    base(&in);
    in.host = 0;
    in.players = 2;
    in.ready[0] = 3; in.ready[1] = 3;
    bo3_status_line(&in, m, sizeof m);
    CHECK(strcmp(m, "WAITING FOR THE HOST") == 0);

    /* the countdown wins over everything */
    base(&in);
    in.host = 1;
    in.players = 6;
    in.ready[0] = 3; in.ready[1] = 3; in.ready[2] = 3; in.ready[3] = 3; in.ready[4] = 3; in.ready[5] = 3;
    in.countdown = 30;
    bo3_status_line(&in, m, sizeof m);
    CHECK(strcmp(m, "STARTING IN 30 S") == 0);

    /* state 0x0B: the race is launching */
    base(&in);
    in.state = 0x0B;
    in.players = 6;
    bo3_status_line(&in, m, sizeof m);
    CHECK(strcmp(m, "STARTING...") == 0);

    /* a peer that will not link, named, with the copy's own port */
    base(&in);
    in.unreach = "MAC1";
    bo3_status_line(&in, m, sizeof m);
    CHECK(strcmp(m, "CAN'T REACH MAC1 - OPEN UDP PORT 31000") == 0);

    /* a private game shows its code */
    base(&in);
    in.host = 1;
    in.players = 1;
    in.ready[0] = 3;
    in.code = "K7MXP";
    bo3_status_line(&in, m, sizeof m);
    CHECK(strcmp(m, "CODE K7MXP - HOST: PRESS START WHEN READY") == 0);
    /* the code stays in front of every status: waiting, the countdown */
    in.ready[0] = 0;
    bo3_status_line(&in, m, sizeof m);
    CHECK(strcmp(m, "CODE K7MXP - WAITING FOR PLAYERS 1/6 - 1 PICKING A CAR") == 0);
    in.countdown = 12;
    bo3_status_line(&in, m, sizeof m);
    CHECK(strcmp(m, "CODE K7MXP - STARTING IN 12 S") == 0);
    /* the longest: a peer that will not link, with a 15-letter name */
    in.countdown = 0;
    in.unreach = "ABCDEFGHIJKLMNO";
    bo3_status_line(&in, m, sizeof m);
    CHECK(strcmp(m, "CODE K7MXP - CAN'T REACH ABCDEFGHIJKLMNO - OPEN UDP PORT 31000") == 0 && strlen(m) <= 63);

    /* the kick reasons (step 5c) */
    bo3_status_kick_line(BO3_STATUS_KICK_NOLINK, 0, 31000, m, sizeof m);
    CHECK(strcmp(m, "REMOVED: COULD NOT CONNECT TO THE HOST") == 0);
    bo3_status_kick_line(BO3_STATUS_KICK_HOST, 0, 31000, m, sizeof m);
    CHECK(strcmp(m, "REMOVED: THE HOST REMOVED YOU") == 0);
    bo3_status_kick_line(BO3_STATUS_KICK_CLOSED, 0, 31000, m, sizeof m);
    CHECK(strcmp(m, "THE GAME ENDED OR THE HOST REMOVED YOU") == 0);
    bo3_status_kick_line(BO3_STATUS_KICK_NONE, 0, 31000, m, sizeof m);
    CHECK(m[0] == 0);

    /* the title's kick box with the reason: what happened, then what to check */
    {
        char b[BO3_KICK_BOX_MAX];
        int p;
        bo3_status_kick_box(BO3_STATUS_KICK_NOLINK, 0, BO3_PLAT_MAC, 31000, 36000, b, sizeof b);
        CHECK(strcmp(b, "Your connection to the host did not come up, so the game removed you. Allow Takedawn under "
                        "Local Network in System Settings, Privacy and Security. If this keeps happening, open UDP "
                        "ports 31000 and 36000 on your router (the host and the players who join), or make sure the "
                        "game can reach the server on UDP 21811 (the relay).") == 0);
        bo3_status_kick_box_peer(BO3_STATUS_KICK_NOLINK, 0, BO3_PLAT_OTHER, 1, 2, "Zed", b, sizeof b);
        CHECK(strncmp(b, "Your connection to Zed did not come up", 38) == 0);
        bo3_status_kick_box(BO3_STATUS_KICK_NOLINK, 0, BO3_PLAT_WINDOWS, 31000, 36000, b, sizeof b);
        CHECK(strstr(b, "Set this network to Private") && strstr(b, "31000 and 36000"));
        bo3_status_kick_box(BO3_STATUS_KICK_NOLINK, 0, BO3_PLAT_LINUX, 31000, 36000, b, sizeof b);
        CHECK(strstr(b, "Check the firewall") && strstr(b, "31000 and 36000"));
        /* no settings hint where there is none to give (Android): the ports only */
        bo3_status_kick_box(BO3_STATUS_KICK_NOLINK, 0, BO3_PLAT_OTHER, 1000, 6000, b, sizeof b);
        CHECK(strcmp(b, "Your connection to the host did not come up, so the game removed you. "
                        "If this keeps happening, open UDP ports 1000 and 6000 on your router (the host and the players who join), "
                        "or make sure the game can reach the server on UDP 21811 (the relay).") == 0);
        bo3_status_kick_box(BO3_STATUS_KICK_HOST, 0, BO3_PLAT_MAC, 31000, 36000, b, sizeof b);
        CHECK(strcmp(b, "The host removed you from the game.") == 0);
        /* the game ended: the title's own text stays */
        bo3_status_kick_box(BO3_STATUS_KICK_CLOSED, 0, BO3_PLAT_MAC, 31000, 36000, b, sizeof b);
        CHECK(b[0] == 0);
        /* every one fits, in the title's font (printable ASCII, no markup) */
        for (p = BO3_PLAT_OTHER; p <= BO3_PLAT_LINUX; p++) {
            size_t i;
            bo3_status_kick_box(BO3_STATUS_KICK_NOLINK, 0, p, 65535, 65535, b, sizeof b);
            CHECK(strlen(b) + 1 < sizeof b);
            for (i = 0; b[i]; i++)
                CHECK(b[i] >= 0x20 && b[i] < 0x7F && !strchr("{}^~<>&|", b[i]));
        }
        /* a small buffer is cut, never overrun */
        bo3_status_kick_box(BO3_STATUS_KICK_NOLINK, 0, BO3_PLAT_MAC, 31000, 36000, b, 20);
        CHECK(strlen(b) == 19);
    }

    /* the kick box text in the string table: out once the box has been up
     * and closed; with no box in BO3_KICK_BOX_MS, out and on the overlay */
    {
        bo3_kick_box k;
        memset(&k, 0, sizeof k);
        CHECK(bo3_kick_box_frame(&k, 0, 0, 0) == 0);
        /* in at 1000; the box opens a frame later and stays up */
        CHECK(bo3_kick_box_frame(&k, BO3_STATUS_KICK_NOLINK, 1000, 0) == 0);
        CHECK(bo3_kick_box_frame(&k, BO3_STATUS_KICK_NOLINK, 1016, 1) == 0);
        CHECK(bo3_kick_box_frame(&k, BO3_STATUS_KICK_NOLINK, 1000 + BO3_KICK_BOX_MS + 5000, 1) == 0);
        /* the player pressed OK: out */
        CHECK(bo3_kick_box_frame(&k, BO3_STATUS_KICK_NOLINK, 30000, 0) == 1 && k.why == 0);
        CHECK(bo3_kick_box_frame(&k, 0, 30016, 0) == 0);
        /* no box came: the overlay after BO3_KICK_BOX_MS */
        CHECK(bo3_kick_box_frame(&k, BO3_STATUS_KICK_HOST, 40000, 0) == 0);
        CHECK(bo3_kick_box_frame(&k, BO3_STATUS_KICK_HOST, 40000 + BO3_KICK_BOX_MS - 1, 0) == 0);
        CHECK(bo3_kick_box_frame(&k, BO3_STATUS_KICK_HOST, 40000 + BO3_KICK_BOX_MS, 0) == 2 && k.why == 0);
        /* a box left up for good: the text goes out anyway, the box keeps what it built */
        CHECK(bo3_kick_box_frame(&k, BO3_STATUS_KICK_NOLINK, 100000, 1) == 0);
        CHECK(bo3_kick_box_frame(&k, BO3_STATUS_KICK_NOLINK, 100000 + BO3_KICK_BOX_MAX_MS, 1) == 1);
    }

    /* the host's kick reason (step 5c): a packet is read from its bytes */
    {
        const char *names[8] = { "Host", "Mac1", "Mac2", 0 };
        int links[8] = { 3, 1, 3, 0 };
        unsigned char pkt[64];
        /* a gset KICK of a peer whose link is below 3: the automatic kick */
        memcpy(pkt, "gset", 4);
        memcpy(pkt + 12, "NAME=Host\nKICK=Mac1\n", 20);
        CHECK(bo3_status_kick_reason(pkt, 32, names, links, 3) == BO3_STATUS_KICK_NOLINK);
        /* a linked peer: the host's choice */
        memcpy(pkt + 12, "NAME=Host\nKICK=Mac2\n", 20);
        CHECK(bo3_status_kick_reason(pkt, 32, names, links, 3) == BO3_STATUS_KICK_HOST);
        /* a name no longer in the game: a host's choice, not "nolink" */
        memcpy(pkt + 12, "NAME=Host\nKICK=Gone\n", 20);
        CHECK(bo3_status_kick_reason(pkt, 32, names, links, 3) == BO3_STATUS_KICK_HOST);
        /* not a gset, or no KICK: nothing */
        memcpy(pkt, "glea", 4);
        CHECK(bo3_status_kick_reason(pkt, 32, names, links, 3) == BO3_STATUS_KICK_NONE);
        memcpy(pkt, "gset", 4);
        memcpy(pkt + 12, "NAME=Host\nSYSFLAGS=1\n", 20);
        CHECK(bo3_status_kick_reason(pkt, 32, names, links, 3) == BO3_STATUS_KICK_NONE);
    }

    /* stay together: players of the race still on the way come before the
     * cars, after a start countdown; the code stays in front */
    base(&in);
    in.host = 1;
    in.players = 2;
    in.ready[0] = 3; in.ready[1] = 3;
    in.coming = 1;
    bo3_status_line(&in, m, sizeof m);
    CHECK(strcmp(m, "WAITING FOR 1 MORE FROM THE RACE") == 0);
    in.code = "K7MXP";
    bo3_status_line(&in, m, sizeof m);
    CHECK(strcmp(m, "CODE K7MXP - WAITING FOR 1 MORE FROM THE RACE") == 0);
    in.coming = 0;
    bo3_status_line(&in, m, sizeof m);
    CHECK(strcmp(m, "CODE K7MXP - HOST: PRESS START WHEN READY") == 0);

    /* the language table (step 9): every id has an English text */
    {
        int i, all = 1, widest = 0;
        for (i = 0; i < BO3_TXT_N; i++)
            all &= bo3_statustext(BO3_LANG_ENGLISH, i)[0] != 0;
        CHECK(all);
        /* a language with no table of its own falls back to English */
        CHECK(strcmp(bo3_statustext(BO3_LANG_SPANISH, BO3_TXT_REACT_GG), "GG") == 0);
        /* an id the program does not know is empty */
        CHECK(bo3_statustext(BO3_LANG_ENGLISH, BO3_TXT_N)[0] == 0 && bo3_statustext(-1, 0)[0] != 0);
        /* no text wider than a line: 64 chars with the widest usual values */
        for (i = 0; i < BO3_TXT_N; i++) {
            char f[96];
            const char *t = bo3_statustext(BO3_LANG_ENGLISH, i);
            int w;
            switch (i) {
            case BO3_TXT_STARTING_IN: w = snprintf(f, sizeof f, t, 30); break;
            case BO3_TXT_CANT_REACH: w = snprintf(f, sizeof f, t, "MAC1", 31000); break;
            case BO3_TXT_CODE: w = snprintf(f, sizeof f, t, "K7MXP"); break;
            case BO3_TXT_WAIT_PICKING: w = snprintf(f, sizeof f, t, 6, 6, 5); break;
            case BO3_TXT_WAIT: w = snprintf(f, sizeof f, t, 6, 6); break;
            case BO3_TXT_REMOVED_NOLINK: w = snprintf(f, sizeof f, t, 31000); break;
            case BO3_TXT_RACEWAIT: w = snprintf(f, sizeof f, t, 8, 20, 59); break;
            case BO3_TXT_RACEWAIT_QUEUE: w = snprintf(f, sizeof f, t, 7); break;
            case BO3_TXT_STAY_WAIT: w = snprintf(f, sizeof f, t, 99, 59); break;
            case BO3_TXT_WAIT_RACERS: w = snprintf(f, sizeof f, t, 7); break;
            case BO3_TXT_RACEWAIT_CODE_GONE:
            case BO3_TXT_RACEWAIT_CODE_FULL: w = snprintf(f, sizeof f, t, "K7MXP"); break;
            case BO3_TXT_BOX_NOLINK: case BO3_TXT_BOX_HOST: case BO3_TXT_BOX_HINT_MAC:
            case BO3_TXT_BOX_HINT_WIN: case BO3_TXT_BOX_HINT_LINUX: case BO3_TXT_BOX_HINT_PORTS:
                w = 0; break;       /* the kick box's: the box wraps them (checked above) */
            default: w = snprintf(f, sizeof f, "%s", t); break;
            }
            if (w > widest)
                widest = w;
        }
        CHECK(widest <= 64);
    }

    /* the pending kick message (step 5c follow-up): a lobby line, else overlay */
    {
        bo3_kick_msg k;
        memset(&k, 0, sizeof k);
        /* no pending message: nothing */
        CHECK(bo3_kick_msg_frame(&k, 0, 1) == 0);
        /* a kick waits for a lobby with the line; before it, nothing */
        k.why = BO3_STATUS_KICK_HOST;
        k.set_ms = 1000;
        CHECK(bo3_kick_msg_frame(&k, 2000, 0) == 0);
        /* such a lobby shows it for 8 s */
        CHECK(bo3_kick_msg_frame(&k, 3000, 1) == 1);
        CHECK(bo3_kick_msg_frame(&k, 5000, 1) == 1);
        CHECK(bo3_kick_msg_frame(&k, 10999, 1) == 1);
        /* 8 s done: nothing more */
        CHECK(bo3_kick_msg_frame(&k, 11000, 1) == 0 && k.why == 0);
        /* no lobby with the line for BO3_KICK_OVERLAY_MS: the overlay shows
         * once, then nothing */
        k.why = BO3_STATUS_KICK_NOLINK;
        k.set_ms = 1000;
        CHECK(bo3_kick_msg_frame(&k, 1000 + BO3_KICK_OVERLAY_MS - 1, 0) == 0);
        CHECK(bo3_kick_msg_frame(&k, 1000 + BO3_KICK_OVERLAY_MS, 0) == 2 && k.why == 0);
        CHECK(bo3_kick_msg_frame(&k, 1000 + BO3_KICK_OVERLAY_MS + 1000, 0) == 0);
    }

    /* the lobby's top line: the title's own text is kept up a while, our line
     * is written only when it changes, reactions and kick reasons at once */
    {
        static const uint16_t w1[] = { 'W', 'e', 0 }, w2[] = { 'W', 'e', 0 }, w3[] = { 'W', 0 }, e[] = { 0 };
        CHECK(bo3_u16eq(w1, w2) && !bo3_u16eq(w1, w3) && !bo3_u16eq(w3, w1) && bo3_u16eq(e, e) && !bo3_u16eq(e, w3));
        /* the title's welcome, unseen: note it, write nothing */
        CHECK(bo3_line_action(1, 0, 0, -1) == BO3_LINE_TITLE);
        CHECK(bo3_line_action(1, 0, 1, -1) == BO3_LINE_TITLE);
        /* seen: the status line waits BO3_LINE_TITLE_MS */
        CHECK(bo3_line_action(0, 0, 0, 0) == BO3_LINE_SKIP);
        CHECK(bo3_line_action(0, 0, 0, BO3_LINE_TITLE_MS - 1) == BO3_LINE_SKIP);
        CHECK(bo3_line_action(0, 0, 0, BO3_LINE_TITLE_MS) == BO3_LINE_WRITE);
        /* a reaction or a kick reason does not wait */
        CHECK(bo3_line_action(0, 0, 1, 0) == BO3_LINE_WRITE);
        /* never a title text: write; already there: nothing */
        CHECK(bo3_line_action(0, 0, 0, -1) == BO3_LINE_WRITE);
        CHECK(bo3_line_action(0, 1, 0, -1) == BO3_LINE_SKIP);
        CHECK(bo3_line_action(1, 1, 1, -1) == BO3_LINE_SKIP);
    }

    /* the title's own pair rule (bo3_status_kick_reason2) */
    {
        static const uint8_t k1[] = "gset\0\0\0\0\0\0\0\0KICK=G1\n", k2[] = "gset\0\0\0\0\0\0\0\0KICK=G2\n",
                             kx[] = "gset\0\0\0\0\0\0\0\0KICK=Nobody\n";
        bo3_link_view v;
        /* slot 0 the local host (id 10), G1 id 11, G2 id 12 */
        memset(&v, 0, sizeof v);
        v.np = 3; v.local_id = 10;
        v.name[0] = "Host"; v.name[1] = "G1"; v.name[2] = "G2";
        v.id[0] = 10; v.id[1] = 11; v.id[2] = 12;
        v.own_link[0] = v.own_link[1] = v.own_link[2] = 3;
        /* all up: a host's choice; an unknown name too */
        CHECK(bo3_status_kick_reason2(k2, sizeof k2 - 1, &v) == BO3_STATUS_KICK_HOST);
        CHECK(bo3_status_kick_reason2(kx, sizeof kx - 1, &v) == BO3_STATUS_KICK_HOST);
        CHECK(bo3_status_kick_reason2((const uint8_t *)"auth", 4, &v) == BO3_STATUS_KICK_NONE);
        /* 3 is not a failure, 0 to 2 and 5 are not either (the title kicks on 4) */
        v.own_link[2] = 2;
        CHECK(bo3_status_kick_reason2(k2, sizeof k2 - 1, &v) == BO3_STATUS_KICK_HOST);
        /* our own link to G2 failed */
        v.own_link[2] = 4;
        CHECK(bo3_status_kick_reason2(k2, sizeof k2 - 1, &v) == BO3_STATUS_KICK_NOLINK);
        CHECK(bo3_status_kick_reason2(k1, sizeof k1 - 1, &v) == BO3_STATUS_KICK_HOST);
        v.own_link[2] = 3;
        /* G1 reports G2 failed: kicking G2 is nolink, and so is kicking G1 */
        v.rep_id[1][0] = 12; v.rep_state[1][0] = 4;
        CHECK(bo3_status_kick_reason2(k2, sizeof k2 - 1, &v) == BO3_STATUS_KICK_NOLINK);
        CHECK(bo3_status_kick_reason2(k1, sizeof k1 - 1, &v) == BO3_STATUS_KICK_NOLINK);
        /* G2 reports G1 failed: kicking G1 is nolink */
        memset(v.rep_id, 0, sizeof v.rep_id); memset(v.rep_state, 0, sizeof v.rep_state);
        v.rep_id[2][3] = 11; v.rep_state[2][3] = 4;
        CHECK(bo3_status_kick_reason2(k1, sizeof k1 - 1, &v) == BO3_STATUS_KICK_NOLINK);
        /* a 4 for someone else's id is no reason; a report of 3 neither */
        memset(v.rep_id, 0, sizeof v.rep_id); memset(v.rep_state, 0, sizeof v.rep_state);
        v.rep_id[1][0] = 99; v.rep_state[1][0] = 4;
        v.rep_id[1][1] = 12; v.rep_state[1][1] = 3;
        CHECK(bo3_status_kick_reason2(k2, sizeof k2 - 1, &v) == BO3_STATUS_KICK_HOST);
        /* G1 reports the local host (id 10) failed: kick G1 is nolink */
        v.rep_id[1][2] = 10; v.rep_state[1][2] = 4;
        CHECK(bo3_status_kick_reason2(k1, sizeof k1 - 1, &v) == BO3_STATUS_KICK_NOLINK);
        CHECK(bo3_status_kick_reason2(k2, sizeof k2 - 1, &v) == BO3_STATUS_KICK_HOST);
    }

    printf("lobbystatus: %d/%d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
