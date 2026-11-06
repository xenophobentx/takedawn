/*
 * aries_server.c - Takedawn's lobby server (see aries_server.h). MIT.
 *
 * Wire format: a 12-byte header, then the body.
 *   cmd   4 ASCII bytes ("@dir", "auth", "+who" ...)
 *   code  4 bytes: 0 in a request; a reply's status (e.g. "new7")
 *   len   big-endian u32, the whole packet including the header
 *   body  KEY=VALUE lines, NUL-terminated
 *
 * What each answer has to hold was learnt run by run from Burnout 3 (the
 * addresses are the NTSC-U title's; BO-134 notes):
 *   news NAME=7   reply code "new7" with the server configuration (0x216120)
 *   sele          then "+rom" for each room; F is a letter bitset, 'C' (bit
 *                 3) marks rooms games can be created in (0x36FE70)
 *   auth          NAME, PERSONAS (0x21B2A0); any name logs in
 *   pers          PERS, LKEY, then "+who", the user's own record, which
 *                 LobbyApi waits for (state 1, 0x1B9C80)
 *   gcre/gjoi     the game record, then "+gam" (the room's list), "+agm"
 *                 (LobbyApi's own list: it gives up after 600 frames
 *                 without its game there, 0x1B9EE0), "+mgm" and "+who" with
 *                 G = the game (200 frames, 0x1B9AD0); a record needs HOST
 *                 (1000 frames, 0x1B9FE0)
 *   "~png"        the client drops a session quiet for a minute: the
 *                 server pings every 20 s
 * Anything else gets an empty success (same command, code 0), so a new
 * request does not stop the game; it is logged.
 */
#ifdef _WIN32
#  ifndef _WIN32_WINNT
#    define _WIN32_WINNT 0x0601     /* WSAPoll */
#  endif
#  define _CRT_RAND_S               /* rand_s: the daily salt */
#  include <stdlib.h>
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <windows.h>
#  include <direct.h>
#  include <io.h>
typedef SOCKET sock_t;
typedef WSAPOLLFD pfd_t;
#  define BAD_SOCK INVALID_SOCKET
#  define sock_close closesocket
#  define poll_(a, n, ms) WSAPoll((a), (ULONG)(n), (ms))
#  define sock_wouldblock() (WSAGetLastError() == WSAEWOULDBLOCK)
#  define sock_interrupted() 0
#  define sock_nofd() (WSAGetLastError() == WSAEMFILE)
#  define sock_reset() (WSAGetLastError() == WSAECONNRESET)
#  define SEND_FLAGS 0
#else
#  include <arpa/inet.h>
#  include <errno.h>
#  include <fcntl.h>
#  include <netdb.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <poll.h>
#  include <pthread.h>
#  include <sys/resource.h>
#  include <sys/socket.h>
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <unistd.h>
typedef int sock_t;
typedef struct pollfd pfd_t;
#  define BAD_SOCK (-1)
#  define sock_close close
#  define poll_(a, n, ms) poll((a), (nfds_t)(n), (ms))
#  define sock_wouldblock() (errno == EAGAIN || errno == EWOULDBLOCK)
#  define sock_interrupted() (errno == EINTR)
#  define sock_nofd() (errno == EMFILE || errno == ENFILE)
#  define sock_reset() (errno == ECONNRESET)
#  ifdef MSG_NOSIGNAL
#    define SEND_FLAGS MSG_NOSIGNAL
#  else
#    define SEND_FLAGS 0
#  endif
#endif

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "aries_server.h"

/* Limits: aries_config (flags and TAKEDAWN_LOBBY_* variables in main.c);
 * the tables are allocated at start for the configured sizes (about 0.4 KB
 * a session and 0.7 KB a game; main.c asks for 4096 and 2048). The loop is
 * poll(), so the limits are memory and the process's open-file limit. */
#define DEF_SESSIONS 256
#define DEF_GAMES    128
#define MAX_PLAYERS  8          /* hard cap per game; the game asks for 6 */
#define PING_SECS    20
#define BODY_MAX     8192
#define ACCEPT_BURST 128        /* connections taken per pass of the loop */
#define OUT_CAP      (1u << 20) /* queued replies per session; beyond it the player is dropped */
#define TOK_BURST    60.0       /* requests a session may send in a burst ... */
#define TOK_RATE     20.0       /* ... and per second after that */
#define NOOP_RATE     200.0     /* no-op repeats (mesg, a kick that changes nothing) answered per second ... */
#define NOOP_QUEUE    65536     /* no answers to no-op repeats while this much is queued unread for the session */
static long s_noop_queue = NOOP_QUEUE;   /* (tests lower it) */
#define NOOP_DROP     20000     /* ... beyond it they are ignored silently; this many over in 10 s ends the session */
#define THROTTLE_DROP 500       /* a session this many requests over the rate in one window is dropped */
#define THROTTLE_WINDOW 60      /* seconds: the count starts again (it was for the session's life) */
static int s_throttle_window = THROTTLE_WINDOW;   /* (tests make it short) */
#define DEF_AUTH_SECS 25        /* time to sign in */
#define DEF_IDLE_SECS 90        /* two missed pings */
#define DEF_PER_IP    64
static int s_partial_secs = 15;   /* an unfinished request may take this long (tests change it) */
#define NAME_MAX_CHARS 15       /* a persona (the toolkit caps its own at 15) */
#define GAME_LIST_REPLY 100     /* games in one search reply */
#define GAME_NAME_MAX  32       /* a game's name (not yet confirmed against the title's buffer) */
#define PASS_MAX       31
#define CODE_MIN       3         /* a game code: letters and digits, 3 to CODE_MAX (short ones are guessed too easily) */
#define CODE_MAX       15
#define CODE_MISSES    20        /* wrong game codes (no private game has it) a session may try a minute */
#define CODE_SEARCH_MS 1000      /* a session looks a code up (gsea, move with a code) at most this often */
static int s_code_search_ms = CODE_SEARCH_MS;   /* (tests change it) */
#define STATUS_SECS    30        /* TKONLINE / TKWAITING are pushed this often when they changed */
static int s_status_secs = STATUS_SECS;   /* (tests make it short) */
#define HOST_GONE_SECS 60
static int s_host_gone_secs = HOST_GONE_SECS;   /* a started game without its host (tests make it short) */
#define BIT_STARTED    0x80000ul

typedef struct {
    int used;
    sock_t fd;
    char peer[16];            /* as seen here (loopback: the public address) */
    char laddr[16];           /* the client's own idea ("addr" ADDR=) */
    unsigned uid;
    char name[32];
    int room, game;
    char userparams[128];
    char xuid[24];            /* "$" + 16 hex digits, from auth XUID= */
    unsigned char *in;
    size_t inlen, incap;
    unsigned char *out;       /* replies the socket did not take yet (drained on POLLOUT) */
    size_t outlen, outoff, outcap;
    time_t last_ping, last_rx, t_conn, t_partial;   /* t_partial: when the unfinished request in the buffer began */
    uint32_t peer_net;        /* the connection's address (network order) */
    double tok;               /* request token bucket */
    double ntok;              /* the bucket of no-op repeats (mesg, a kick of a player that is not a member) */
    long long ntok_ms;
    long noop_excess;         /* no-op repeats over their own rate in the current window */
    int noop_logged;          /* how many no-op repeats were written to the log (the first few, to see what the title sends) */
    time_t noop_t0;
    long long tok_ms;
    int throttled, nname;
    time_t throttle_t0;       /* the window of the throttled count (THROTTLE_WINDOW) */
    int wrong_n;              /* wrong game codes in the current minute */
    long long last_code_ms;   /* the last code lookup */
    time_t race_at;           /* its game started (gsta) with it in, for the online count (racer_add) */
    time_t kicked_until;      /* just kicked: a join retry storm is ignored, not a reason to drop it */
    time_t last_roompush;     /* the last time the room's games were pushed on entry */
    time_t wrong_t0;
    char sid[9];              /* TKSID: random per connection (CSPRNG), unique among live sessions; in log lines only */
    int tkid;                 /* announced TKID=1: gets a synthetic title address (BO-337 identity) */
    int synth_idx;            /* its slot in the synthetic address pool (10.64.0.0/16), 0: none */
    uint32_t synth;           /* the synthetic address (network order) */
    char code[CODE_MAX + 1];  /* the session's game code (TKCODE of its last request that had one) */
    const char *why;          /* how the session ended, for the "left (...)" log line; no address */
    int authed;               /* has signed in (counted in the statistics) */
    int probe;                /* sent TKPROBE=1 (the online info line): not logged as a session */
    int dead;                 /* a send failed or the queue is full: dropped at the end of the pass */
    int wait_for;             /* BO-365: the started game (race_t ident) its gsea TKWAITFOR waits for; 0 none */
    long long wait_since;     /* when it began waiting for it */
    unsigned long wait_seq;   /* the order the waiters began in (ranks them) */
    int wait_logged;          /* the race its "waits for" line was written for (once per race) */
    long long wait_poll_ms;   /* its last gsea with TKWAITFOR (a waiter that stopped asking has gone) */
    long long auth_ms;        /* its sign-in (on the way from the results to its game, stay together) */
    int wait_begun;           /* BO-367: the TKWAITFOR whose wait went into the statistics */
    int wait_counted;         /* BO-367: the TKWAITFOR whose outcome did */
    int crowd;                /* a load-tool player (TKCROWD=1 with --crowd-test, BO-366) */
} sess_t;

typedef struct {
    int used, ident, room;
    char name[64], pass[64], params[256];
    char sysflags[32], custflags[48], minsize[12], maxsize[12];   /* custflags: up to 4 numbers of 10 digits */
    char secret[33];            /* TKSECRET: per game, from the CSPRNG, for its players only */
    uint8_t key[16];            /* the secret's bytes: the relay's SipHash key (BO-381) */
    char gid[13];               /* TKGID: random per game (CSPRNG, 12 hex), member records and log lines only */
    unsigned kicked[16];        /* uids of players the host kicked: they cannot join this game again */
    int nkicked;
    int priv;                   /* made with TKCODE: pass holds the code; listed for that code only */
    char code[CODE_MAX + 1];    /* the game's code (TKCODE at gcre), "" none; a public one with TKPUBLIC=1 */
    long long t_ms;             /* created (now_ms): older games are offered first */
    int players[MAX_PLAYERS];   /* session indices; [0] is the host */
    int np;
    long long host_gone_ms;     /* a started game's host left (now_ms); 0: it has its host */
    int crowd;                  /* made by a load-tool player (--crowd-test, BO-366) */
} game_t;

/* One room: everyone who goes to LOBBY SELECT meets in it (the owner's call,
 * 2026-11-01; "Burnout", room 2, is gone). A "move" to any other id is a bad
 * request, and a game made with another ROOM goes into this one. */
static const struct { int id; const char *name; } k_rooms[] = {
    { 1, "Takedawn" },
};
#define N_ROOMS ((int)(sizeof k_rooms / sizeof k_rooms[0]))

static sess_t *s_sess;
static game_t *s_games;
static int MAX_SESS, MAX_GAMES, s_max_players;
static unsigned s_next_uid = 1;
static int s_next_game = 1;
static volatile int s_stop;
static aries_config s_cfg;
static char s_public[16] = "127.0.0.1";
static int s_nsess;                   /* sessions in use */
static int s_idle_secs, s_auth_secs, s_per_ip;   /* the effective limits (0: none) */
static int s_sndbuf;                  /* SO_SNDBUF for sessions (0: the system's; tests make it small) */
static int s_loop_exempt = 1;         /* 127.0.0.0/8 is not counted per address (tests turn it off) */
static long s_backoff_total;          /* connections refused without a word (an address at its cap) */
static time_t (*s_clock)(void);       /* statistics clock; tests replace it */

/* ── logging ───────────────────────────────────────────────────────── */

static void logf_(const char *fmt, ...)
{
    char line[1024];
    va_list ap;
    time_t t = time(NULL);
    struct tm tm;
    size_t o;
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    o = strftime(line, sizeof line, "%H:%M:%S ", &tm);
    va_start(ap, fmt);
    vsnprintf(line + o, sizeof line - o, fmt, ap);
    va_end(ap);
    for (; line[o]; o++)        /* nothing a player sent can move a terminal's cursor */
        if ((unsigned char)line[o] < 0x20 || (unsigned char)line[o] > 0x7E)
            line[o] = '?';
    if (s_cfg.log)
        s_cfg.log(line);
    else {
        fputs(line, stderr);
        fputc('\n', stderr);
    }
}

/* A log line about one session: it starts with its random session id, so a
 * player's bug report (which carries the id) finds every line. */
static void logs_(const sess_t *s, const char *fmt, ...)
{
    char msg[900];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    logf_("sid=%s %s", s->sid[0] ? s->sid : "-", msg);
}

/* Who a log line is about: the name; before sign-in only --verbose shows the address. */
static const char *who_log(const sess_t *s)
{
    return s->name[0] ? s->name : s_cfg.verbose ? s->peer : "(not signed in)";
}

/* A request or answer on one log line: control bytes become '?', and the
 * values of PASS and XUID are not logged. */
static const char *oneline(const char *body)
{
    static char out[512];
    size_t i = 0;
    const char *p = body;
    while (*p && i < sizeof out - 1) {
        const char *e = strchr(p, '\n');
        size_t n = e ? (size_t)(e - p) : strlen(p), k;
        const char *q = p;
        while (*q == ' ')
            q++;
        if (!strncmp(q, "PASS=", 5) || !strncmp(q, "XUID=", 5) || !strncmp(q, "TKSECRET=", 9) ||
            !strncmp(q, "TKCODE=", 7)) {
            n = (size_t)(q - p) + (q[0] != 'T' ? 5 : q[2] == 'S' ? 9 : 7);
            for (k = 0; k < n && i < sizeof out - 1; k++)
                out[i++] = p[k];
            if (i < sizeof out - 3) { out[i++] = '*'; out[i++] = '*'; out[i++] = '*'; }
        } else {
            for (k = 0; k < n && i < sizeof out - 1; k++) {
                unsigned char c = (unsigned char)p[k];
                out[i++] = c < 0x20 || c > 0x7E ? '?' : (char)c;
            }
        }
        if (!e)
            break;
        if (i < sizeof out - 1)
            out[i++] = ' ';
        p = e + 1;
    }
    out[i] = 0;
    return out;
}

static void addr_str(uint32_t net, char *out, size_t n)
{
    const unsigned char *a = (const unsigned char *)&net;
    snprintf(out, n, "%u.%u.%u.%u", a[0], a[1], a[2], a[3]);
}

/* Where an address is reachable from. */
enum { ADDR_PUBLIC, ADDR_PRIVATE, ADDR_LOOPBACK };

static int addr_class(uint32_t net)
{
    const unsigned char *a = (const unsigned char *)&net;
    if (a[0] == 127)
        return ADDR_LOOPBACK;
    if (a[0] == 10 || (a[0] == 172 && a[1] >= 16 && a[1] <= 31) || (a[0] == 192 && a[1] == 168) ||
        (a[0] == 100 && a[1] >= 64 && a[1] <= 127))
        return ADDR_PRIVATE;
    return ADDR_PUBLIC;
}

/* a.b.c.d, digits and dots only; the address in network order. */
static int parse_ip(const char *a, uint32_t *net)
{
    unsigned char o[4];
    int i;
    for (i = 0; i < 4; i++) {
        unsigned v = 0, d = 0;
        while (*a >= '0' && *a <= '9' && d < 4) { v = v * 10 + (unsigned)(*a - '0'); a++; d++; }
        if (!d || d > 3 || v > 255)
            return 0;
        o[i] = (unsigned char)v;
        if (i < 3) {
            if (*a != '.')
                return 0;
            a++;
        }
    }
    if (*a)
        return 0;
    if (net)
        memcpy(net, o, 4);
    return 1;
}

static int is_dotted(const char *a) { return parse_ip(a, NULL); }

/* A claimed address is believed when it is the connection's own, a private
 * or CGNAT one (a player behind a router), or loopback for a player on
 * loopback (test setups). Anything else would point other players' race
 * traffic at a third party. */
static int addr_believable(uint32_t peer_net, uint32_t claimed)
{
    int c = addr_class(claimed);
    return claimed == peer_net || c == ADDR_PRIVATE ||
           (c == ADDR_LOOPBACK && addr_class(peer_net) == ADDR_LOOPBACK);
}

/* n random bytes from the system's CSPRNG; 0 when there are none. */
static int rand_bytes(unsigned char *out, size_t n)
{
#ifdef _WIN32
    size_t i;
    for (i = 0; i < n; i += 4) {
        unsigned v = 0;
        size_t k;
        if (rand_s(&v) != 0)
            return 0;
        for (k = 0; k < 4 && i + k < n; k++)
            out[i + k] = (unsigned char)(v >> (8 * k));
    }
    return 1;
#else
    FILE *f = fopen("/dev/urandom", "rb");
    size_t got;
    if (!f)
        return 0;
    got = fread(out, 1, n, f);
    fclose(f);
    return got == n;
#endif
}

/* 32 lowercase hex digits and a NUL */
static int make_secret(char *out, uint8_t *key)
{
    unsigned char r[16];
    int i;
    if (!rand_bytes(r, sizeof r))
        return 0;
    memcpy(key, r, 16);
    for (i = 0; i < 16; i++)
        snprintf(out + 2 * i, 3, "%02x", r[i]);
    return 1;
}

/* 2*n lowercase hex digits and a NUL (n <= 8) */
static int make_hex(char *out, int n)
{
    unsigned char r[8];
    int i;
    if (n > 8 || !rand_bytes(r, (size_t)n))
        return 0;
    for (i = 0; i < n; i++)
        snprintf(out + 2 * i, 3, "%02x", r[i]);
    return 1;
}

/* A session id: 8 hex digits from the CSPRNG that no live session has. */
static int new_sid(char *out)
{
    int tries, i;
    for (tries = 0; tries < 32; tries++) {
        if (!make_hex(out, 4))
            return 0;
        for (i = 0; i < MAX_SESS; i++)
            if (s_sess[i].used && s_sess[i].sid != out && !strcmp(s_sess[i].sid, out))
                break;
        if (i == MAX_SESS)
            return 1;
    }
    return 0;
}

/* Monotonic milliseconds (the rate limit; not the statistics clock). */
static long long now_ms(void)
{
#ifdef _WIN32
    return (long long)GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#endif
}

/* Monotonic seconds: every session and game deadline (idle, unfinished
 * request, sign-in, ping, kick hold, penalties, windows). A wall-clock step
 * moves none of them; the statistics, leases and the racing count keep the
 * wall clock (they are dated). */
static time_t mono_s(void) { return (time_t)(now_ms() / 1000); }

/* ── usage statistics ──────────────────────────────────────────────── */
/* Counts only: no address and no name is kept. "Unique players" is a set
 * of 64-bit hashes of the lower-cased persona name under a random salt that
 * is made at the start of each UTC day and lives in memory only; the set is
 * never written, and both are dropped at the next midnight. With a stats
 * directory the day's totals go to daily.jsonl (one JSON line per day,
 * appended when the day ends) and current.json (today so far, rewritten
 * every minute). A restart on the same day carries on from current.json
 * (a player seen before and after the restart then counts twice). */

#define N_MODES 6               /* Single Race, Road Rage, Team Crush, Double Impact, Party Crash, other */
#define N_CC (26 * 26 + 1)      /* countries: AA..ZZ, and the last one "??" (not known) */
#define CC_UNKNOWN (N_CC - 1)
#define ST_LINE_MAX 32768       /* one day's line, every country included */
#define ST_SET_MAX ((size_t)1 << 20)
#define ST_WRITE_SECS 60
static const char *const k_mode[N_MODES] = {
    "single_race", "road_rage", "team_crush", "double_impact", "party_crash", "other"
};

typedef struct {
    long day;                   /* days since 1970-01-01, UTC */
    unsigned long connections, refused, signins, unique_base, games_started, results;
    unsigned long created[N_MODES], started[N_MODES], start_size[MAX_PLAYERS + 1];
    int peak_sessions, peak_players;
    unsigned long waits, waits_joined, waits_gone, waits_full;  /* BO-365 race waits */
    unsigned long relay_packets, relay_bytes, relay_dropped, relay_games;  /* BO-381 UDP relay: counts only */
    /* BO-367: per country (the sign-in's address looked up in the offline
     * database, never kept): new unique players and sign-ins */
    uint32_t cc_unique[N_CC], cc_signins[N_CC];
    uint64_t salt;
    uint64_t *set;
    size_t set_cap, set_n;
} stats_t;

static stats_t s_st;
static int s_players;           /* signed-in sessions now */
static int s_racers;            /* players a race took off the lobby (racer_t), still online */
/* what TKONLINE and the pages say: everyone signed in, and everyone racing */
static int online_now(void) { return s_players + s_racers; }
static char s_stats_dir[480];
static char s_public_dir[480];  /* the public page's folder (stats.json, index.html) */
static time_t s_pub_written, s_started;
#define HIST_DAYS 30
#define HIST_READ (HIST_DAYS * 24 * 1024L)
#define PUBLIC_SECS 20          /* stats.json is rewritten this often */
#define GAME_LIST_MAX 50
#define GAME_NAME_CUT 32
static stats_t s_hist[HIST_DAYS];   /* the last finished days (daily.jsonl), oldest first */
static int s_nhist;
static time_t s_st_written;
static int s_st_failed;

static time_t stats_now(void) { return s_clock ? s_clock() : time(NULL); }

static uint64_t st_rand64(void)
{
    uint64_t v = 0;
#ifdef _WIN32
    unsigned a = 0, b = 0;
    rand_s(&a); rand_s(&b);
    v = ((uint64_t)a << 32) | b;
#else
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) {
        if (fread(&v, sizeof v, 1, f) != 1)
            v = 0;
        fclose(f);
    }
#endif
    if (!v)
        v = (uint64_t)time(NULL) * 0x9E3779B97F4A7C15ull ^ (uint64_t)(uintptr_t)&v ^ (uint64_t)clock();
    return v;
}

static uint64_t st_hash(const char *name)
{
    uint64_t h = s_st.salt ^ 0xcbf29ce484222325ull;
    for (; *name; name++) {
        unsigned char c = (unsigned char)*name;
        if (c >= 'A' && c <= 'Z')
            c = (unsigned char)(c + 32);
        h = (h ^ c) * 0x100000001b3ull;
    }
    h ^= h >> 33; h *= 0xff51afd7ed558ccdull;
    h ^= h >> 33; h *= 0xc4ceb9fe1a85ec53ull;
    h ^= h >> 33;
    return h ? h : 1;
}

static int st_set_add(uint64_t h)
{
    size_t i;
    if (s_st.set_n >= ST_SET_MAX)
        return 0;
    if ((s_st.set_n + 1) * 2 > s_st.set_cap) {
        size_t nc = s_st.set_cap ? s_st.set_cap * 2 : 1024, k;
        uint64_t *t = (uint64_t *)calloc(nc, sizeof *t);
        if (!t)
            return 0;
        for (k = 0; k < s_st.set_cap; k++)
            if (s_st.set[k]) {
                size_t j = (size_t)s_st.set[k] & (nc - 1);
                while (t[j])
                    j = (j + 1) & (nc - 1);
                t[j] = s_st.set[k];
            }
        free(s_st.set);
        s_st.set = t;
        s_st.set_cap = nc;
    }
    for (i = (size_t)h & (s_st.set_cap - 1); s_st.set[i]; i = (i + 1) & (s_st.set_cap - 1))
        if (s_st.set[i] == h)
            return 0;
    s_st.set[i] = h;
    s_st.set_n++;
    return 1;
}

/* ── countries (BO-367) ────────────────────────────────────────────
 * An offline database (DB-IP Lite country, CSV: first,last,CC per line;
 * the IPv6 lines are skipped) maps a sign-in's IPv4 address to a country.
 * Only the day's counts per country are kept: the address and a player's
 * country are never stored or logged. No database: every country is "??". */
static uint32_t *s_geo_lo, *s_geo_hi;   /* host order, ascending, not overlapping */
static uint16_t *s_geo_cc;
static size_t s_geo_n;

static int cc_index(const char *c)
{
    if (c[0] < 'A' || c[0] > 'Z' || c[1] < 'A' || c[1] > 'Z' || (c[0] == 'Z' && c[1] == 'Z'))
        return CC_UNKNOWN;                  /* ZZ: DB-IP's "reserved" */
    return (c[0] - 'A') * 26 + (c[1] - 'A');
}

static void cc_code(int i, char out[3])
{
    if (i < 0 || i >= CC_UNKNOWN) { out[0] = out[1] = '?'; }
    else { out[0] = (char)('A' + i / 26); out[1] = (char)('A' + i % 26); }
    out[2] = 0;
}

static void geo_free(void)
{
    free(s_geo_lo); free(s_geo_hi); free(s_geo_cc);
    s_geo_lo = s_geo_hi = NULL;
    s_geo_cc = NULL;
    s_geo_n = 0;
}

/* Number of ranges read (0: none; the old table is dropped either way). */
static size_t geo_load(const char *path)
{
    char line[256];
    size_t cap = 0, bad = 0;
    FILE *f;
    geo_free();
    if (!path || !*path)
        return 0;
    f = fopen(path, "rb");
    if (!f) {
        logf_("countries: no database at %s (countries are not counted)", path);
        return 0;
    }
    while (fgets(line, sizeof line, f)) {
        unsigned a[8];
        char cc[3] = { 0 };
        uint32_t lo, hi;
        char *p = line;
        if (strchr(line, ':'))
            continue;                       /* IPv6 */
        while (*p == '"') p++;
        if (sscanf(p, "%u.%u.%u.%u%*[\",]%u.%u.%u.%u%*[\",]%2[A-Za-z]",
                   &a[0], &a[1], &a[2], &a[3], &a[4], &a[5], &a[6], &a[7], cc) != 9 ||
            (a[0] | a[1] | a[2] | a[3] | a[4] | a[5] | a[6] | a[7]) > 255) {
            bad++;
            continue;
        }
        lo = a[0] << 24 | a[1] << 16 | a[2] << 8 | a[3];
        hi = a[4] << 24 | a[5] << 16 | a[6] << 8 | a[7];
        if (hi < lo || (s_geo_n && lo <= s_geo_hi[s_geo_n - 1])) {
            bad++;                          /* out of order or overlapping: left out */
            continue;
        }
        if (s_geo_n == cap) {
            size_t nc = cap ? cap * 2 : 65536;
            uint32_t *l2 = (uint32_t *)realloc(s_geo_lo, nc * sizeof *l2);
            uint32_t *h2 = l2 ? (uint32_t *)realloc(s_geo_hi, nc * sizeof *h2) : NULL;
            uint16_t *c2 = h2 ? (uint16_t *)realloc(s_geo_cc, nc * sizeof *c2) : NULL;
            if (l2) s_geo_lo = l2;
            if (h2) s_geo_hi = h2;
            if (!c2) { fclose(f); geo_free(); logf_("countries: out of memory reading %s", path); return 0; }
            s_geo_cc = c2;
            cap = nc;
        }
        if (cc[0] >= 'a') cc[0] = (char)(cc[0] - 32);
        if (cc[1] >= 'a') cc[1] = (char)(cc[1] - 32);
        s_geo_lo[s_geo_n] = lo;
        s_geo_hi[s_geo_n] = hi;
        s_geo_cc[s_geo_n++] = (uint16_t)cc_index(cc);
    }
    fclose(f);
    logf_("countries: %lu IPv4 ranges from %s%s", (unsigned long)s_geo_n, path, bad ? " (some lines left out)" : "");
    return s_geo_n;
}

/* The country of an address (network order); CC_UNKNOWN when not known. */
static int geo_lookup(uint32_t net)
{
    uint32_t ip = ntohl(net);
    size_t lo = 0, hi = s_geo_n;
    while (lo < hi) {                       /* the last range starting at or before ip */
        size_t mid = lo + (hi - lo) / 2;
        if (s_geo_lo[mid] <= ip) lo = mid + 1; else hi = mid;
    }
    if (!lo || ip > s_geo_hi[lo - 1])
        return CC_UNKNOWN;
    return s_geo_cc[lo - 1];
}

/* Gregorian date <-> days since 1970-01-01 (no time-zone or libc help). */
static long days_from_civil(long y, unsigned m, unsigned d)
{
    long era, yoe, doy, doe;
    y -= m <= 2;
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = y - era * 400;
    doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + (long)d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static void civil_from_days(long z, int *y, int *m, int *d)
{
    long era, doe, yoe, doy, mp;
    z += 719468;
    era = (z >= 0 ? z : z - 146096) / 146097;
    doe = z - era * 146097;
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = (int)(yoe + era * 400 + (*m <= 2));
}

static void st_reset(long day)
{
    free(s_st.set);
    memset(&s_st, 0, sizeof s_st);
    s_st.day = day;
    s_st.salt = st_rand64();
    s_st.peak_sessions = s_nsess;
    s_st.peak_players = online_now();
}

static void st_path(char *out, size_t n, const char *file)
{
    snprintf(out, n, "%s/%s", s_stats_dir, file);
}

/* One JSON object on one line: the day's totals (now = 1: also the live
 * counts and the time of the write, for current.json). */
static int st_format(const stats_t *st, int now, char *out, size_t n)
{
    int y, m, d, o, i;
    civil_from_days(st->day, &y, &m, &d);
    o = snprintf(out, n, "{\"date\":\"%04d-%02d-%02d\",\"connections\":%lu,\"refused_full\":%lu,"
                 "\"signins\":%lu,\"unique_players\":%lu,\"peak_sessions\":%d,\"peak_players\":%d",
                 y, m, d, st->connections, st->refused, st->signins,
                 st->unique_base + (unsigned long)st->set_n, st->peak_sessions, st->peak_players);
    for (i = 0, m = 0; i < N_MODES; i++)
        m += (int)st->created[i];
    o += snprintf(out + o, n - (size_t)o, ",\"games_created\":%d", m);
    for (i = 0; i < N_MODES; i++)
        o += snprintf(out + o, n - (size_t)o, ",\"created_%s\":%lu", k_mode[i], st->created[i]);
    o += snprintf(out + o, n - (size_t)o, ",\"games_started\":%lu", st->games_started);
    for (i = 0; i < N_MODES; i++)
        o += snprintf(out + o, n - (size_t)o, ",\"started_%s\":%lu", k_mode[i], st->started[i]);
    o += snprintf(out + o, n - (size_t)o, ",\"race_results\":%lu", st->results);
    for (i = 1; i <= MAX_PLAYERS; i++)
        o += snprintf(out + o, n - (size_t)o, ",\"start_players_%d\":%lu", i, st->start_size[i]);
    o += snprintf(out + o, n - (size_t)o, ",\"waits\":%lu,\"waits_joined\":%lu,\"waits_gone\":%lu,\"waits_full\":%lu",
                  st->waits, st->waits_joined, st->waits_gone, st->waits_full);
    o += snprintf(out + o, n - (size_t)o, ",\"relay_packets\":%lu,\"relay_bytes\":%lu,\"relay_dropped\":%lu,\"relay_games\":%lu",
                  st->relay_packets, st->relay_bytes, st->relay_dropped, st->relay_games);
    o += snprintf(out + o, n - (size_t)o, ",\"countries\":{");
    for (i = 0, m = 0; i < N_CC && (size_t)o + 128 < n; i++)
        if (st->cc_unique[i] || st->cc_signins[i]) {
            char cc[3];
            cc_code(i, cc);
            o += snprintf(out + o, n - (size_t)o, "%s\"%s\":[%lu,%lu]", m++ ? "," : "", cc,
                          (unsigned long)st->cc_unique[i], (unsigned long)st->cc_signins[i]);
        }
    o += snprintf(out + o, n - (size_t)o, "}");
    if (now)
        o += snprintf(out + o, n - (size_t)o, ",\"sessions_now\":%d,\"players_now\":%d,\"updated\":%lld",
                      s_nsess, online_now(), (long long)stats_now());
    o += snprintf(out + o, n - (size_t)o, "}");
    return o;
}

static int json_ul(const char *b, const char *key, unsigned long *out)
{
    char pat[64];
    const char *p;
    snprintf(pat, sizeof pat, "\"%s\":", key);
    p = strstr(b, pat);
    if (!p)
        return 0;
    *out = strtoul(p + strlen(pat), NULL, 10);
    return 1;
}

static int st_parse(const char *b, stats_t *st)
{
    int y, m, d, i;
    unsigned long v;
    char key[40];
    const char *p = strstr(b, "\"date\":\"");
    memset(st, 0, sizeof *st);
    if (!p || sscanf(p + 8, "%d-%d-%d", &y, &m, &d) != 3 || m < 1 || m > 12 || d < 1 || d > 31)
        return 0;
    st->day = days_from_civil(y, (unsigned)m, (unsigned)d);
    json_ul(b, "connections", &st->connections);
    json_ul(b, "refused_full", &st->refused);
    json_ul(b, "signins", &st->signins);
    json_ul(b, "unique_players", &st->unique_base);
    json_ul(b, "games_started", &st->games_started);
    json_ul(b, "race_results", &st->results);
    if (json_ul(b, "peak_sessions", &v)) st->peak_sessions = (int)v;
    if (json_ul(b, "peak_players", &v)) st->peak_players = (int)v;
    for (i = 0; i < N_MODES; i++) {
        snprintf(key, sizeof key, "created_%s", k_mode[i]);
        json_ul(b, key, &st->created[i]);
        snprintf(key, sizeof key, "started_%s", k_mode[i]);
        json_ul(b, key, &st->started[i]);
    }
    for (i = 1; i <= MAX_PLAYERS; i++) {
        snprintf(key, sizeof key, "start_players_%d", i);
        json_ul(b, key, &st->start_size[i]);
    }
    json_ul(b, "waits", &st->waits);
    json_ul(b, "waits_joined", &st->waits_joined);
    json_ul(b, "waits_gone", &st->waits_gone);
    json_ul(b, "waits_full", &st->waits_full);
    json_ul(b, "relay_packets", &st->relay_packets);
    json_ul(b, "relay_bytes", &st->relay_bytes);
    json_ul(b, "relay_dropped", &st->relay_dropped);
    json_ul(b, "relay_games", &st->relay_games);
    if ((p = strstr(b, "\"countries\":{")) != NULL) {
        p += 13;
        while (*p == '"') {                 /* "HU":[unique,signins] */
            char cc[3];
            unsigned long u, sg;
            int k, used = 0;
            cc[0] = p[1]; cc[1] = p[1] ? p[2] : 0; cc[2] = 0;
            if (!cc[1] || p[3] != '"' || sscanf(p + 4, ":[%lu,%lu]%n", &u, &sg, &used) != 2 || !used)
                break;
            k = (cc[0] == '?' && cc[1] == '?') ? CC_UNKNOWN : cc_index(cc);
            st->cc_unique[k] += (uint32_t)u;
            st->cc_signins[k] += (uint32_t)sg;
            p += 4 + used;
            if (*p == ',') p++;
        }
    }
    return 1;
}

static void st_fail(const char *what, const char *path)
{
    if (!s_st_failed)
        logf_("statistics: cannot %s %s (no more messages about it)", what, path);
    s_st_failed = 1;
}

/* A file replaced in one step (a web server never sees half of it), readable by all. */
static void write_file(const char *path, const char *data, size_t n)
{
    char tmp[720];
    FILE *f;
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    f = fopen(tmp, "wb");
    if (!f) { st_fail("write", tmp); return; }
    if (fwrite(data, 1, n, f) != n) { fclose(f); st_fail("write", tmp); return; }
    if (fclose(f) != 0) { st_fail("write", tmp); return; }
#ifdef _WIN32
    remove(path);
#else
    chmod(tmp, 0644);
#endif
    if (rename(tmp, path) != 0)
        st_fail("rename to", path);
}

static char s_line[ST_LINE_MAX];   /* one day's line (the server runs on one thread) */

static void st_write_current(void)
{
    char *line = s_line, path[540];
    int n;
    if (!s_stats_dir[0])
        return;
    n = st_format(&s_st, 1, line, ST_LINE_MAX - 1);
    line[n++] = '\n';
    st_path(path, sizeof path, "current.json");
    write_file(path, line, (size_t)n);
    s_st_written = stats_now();
}

/* The day's line, unless daily.jsonl already ends with that date. */
static void st_append_daily(const stats_t *st)
{
    static char tail[ST_LINE_MAX];
    char *line = s_line, path[540], want[40];
    int y, m, d;
    FILE *f;
    size_t n;
    if (!s_stats_dir[0])
        return;
    civil_from_days(st->day, &y, &m, &d);
    snprintf(want, sizeof want, "\"date\":\"%04d-%02d-%02d\"", y, m, d);
    st_path(path, sizeof path, "daily.jsonl");
    f = fopen(path, "rb");
    if (f) {
        if (fseek(f, 0, SEEK_END) == 0) {
            long sz = ftell(f), from = sz > (long)sizeof tail - 1 ? sz - ((long)sizeof tail - 1) : 0;
            fseek(f, from, SEEK_SET);
            n = fread(tail, 1, sizeof tail - 1, f);
            tail[n] = 0;
            fclose(f);
            if (n) {
                char *last = tail + n;
                while (last > tail && (last[-1] == '\n' || last[-1] == '\r')) last--;
                *last = 0;
                while (last > tail && last[-1] != '\n') last--;
                if (strstr(last, want))
                    return;
            }
        } else
            fclose(f);
    }
    st_format(st, 0, line, ST_LINE_MAX);
    f = fopen(path, "ab");
    if (!f) { st_fail("append to", path); return; }
    fputs(line, f);
    fputc('\n', f);
    if (fclose(f) != 0)
        st_fail("append to", path);
}

static void st_write_public(void);
static int races_public_live(void);
static void st_hist_push(const stats_t *st);

/* ── the owner's page (BO-367) ─────────────────────────────────────────
 * With an admin token (a file holding one line of 20 to 64 of [A-Za-z0-9_-])
 * the server also writes <public dir>/<token>/: index.html (the page),
 * history.jsonl (a copy of daily.jsonl: every finished day) and now.json
 * (today so far, every country, and the live counts; every PUBLIC_SECS).
 * Counts only, as everywhere. The folder's name is the secret: the public
 * folder is then 0711 (no listing), the web server lists no folders, and
 * the token is never logged. */
static char s_admin_dir[720];

static int admin_token_read(const char *file, char *tok, size_t n)
{
    char line[160];
    size_t k;
    FILE *f;
    tok[0] = 0;
    if (!file || !*file || !(f = fopen(file, "rb")))
        return 0;
    if (!fgets(line, sizeof line, f))
        line[0] = 0;
    fclose(f);
    k = strcspn(line, "\r\n");
    line[k] = 0;
    if (k < 20 || k > 64 || k >= n)
        return 0;
    for (k = 0; line[k]; k++)
        if (!((line[k] >= 'a' && line[k] <= 'z') || (line[k] >= 'A' && line[k] <= 'Z') ||
              (line[k] >= '0' && line[k] <= '9') || line[k] == '_' || line[k] == '-'))
            return 0;
    memcpy(tok, line, k + 1);
    return 1;
}

/* history.jsonl: daily.jsonl as it is (at the start and after each midnight). */
static void st_admin_history(void)
{
    char path[760], *buf;
    FILE *f;
    long sz;
    size_t n = 0;
    if (!s_admin_dir[0] || !s_stats_dir[0])
        return;
    st_path(path, sizeof path, "daily.jsonl");
    f = fopen(path, "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz < 0 || sz > 64L * 1024 * 1024) sz = 0;
        buf = (char *)malloc((size_t)sz + 1);
        if (buf) {
            n = fread(buf, 1, (size_t)sz, f);
            snprintf(path, sizeof path, "%s/history.jsonl", s_admin_dir);
            write_file(path, buf, n);
            free(buf);
        }
        fclose(f);
    } else {
        snprintf(path, sizeof path, "%s/history.jsonl", s_admin_dir);
        write_file(path, "", 0);
    }
}

/* Day change (a line for the day that ended) and the periodic writes. */
static void st_roll(void)
{
    time_t now = stats_now();
    long day = (long)(now / 86400);
    if (day != s_st.day) {
        st_append_daily(&s_st);
        st_admin_history();
        st_hist_push(&s_st);
        st_reset(day);
        st_write_current();
        st_write_public();
    } else {
        if (s_stats_dir[0] && now - s_st_written >= ST_WRITE_SECS)
            st_write_current();
        if (s_public_dir[0] && now - s_pub_written >= PUBLIC_SECS)
            st_write_public();
    }
}

static void st_hist_push(const stats_t *st)
{
    if (s_nhist && s_hist[s_nhist - 1].day == st->day)
        s_nhist--;                          /* the same day again: replace it */
    if (s_nhist == HIST_DAYS) {
        memmove(s_hist, s_hist + 1, (HIST_DAYS - 1) * sizeof s_hist[0]);
        s_nhist--;
    }
    s_hist[s_nhist] = *st;
    s_hist[s_nhist].set = NULL;
    s_hist[s_nhist++].set_n = 0;
}

/* The last HIST_DAYS lines of daily.jsonl, for the public page. */
static void st_hist_load(void)
{
    char path[540], *buf, *p, *end, *nl;
    FILE *f;
    long sz, from;
    size_t n;
    s_nhist = 0;
    if (!s_stats_dir[0])
        return;
    st_path(path, sizeof path, "daily.jsonl");
    f = fopen(path, "rb");
    if (!f)
        return;
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    from = sz > HIST_READ ? sz - HIST_READ : 0;   /* enough for HIST_DAYS full lines */
    fseek(f, from, SEEK_SET);
    buf = (char *)malloc(HIST_READ + 1);
    if (!buf) { fclose(f); return; }
    n = fread(buf, 1, HIST_READ, f);
    buf[n] = 0;
    fclose(f);
    p = buf;
    if (from && (nl = strchr(p, '\n')))        /* the first line may be cut */
        p = nl + 1;
    for (end = buf + n; p < end; p = nl + 1) {
        stats_t d;
        nl = strchr(p, '\n');
        if (!nl)
            break;
        *nl = 0;
        if (st_parse(p, &d))
            st_hist_push(&d);
    }
    free(buf);
}

static void st_init(const char *dir, const char *pubdir, const char *page)
{
    static char buf[ST_LINE_MAX];
    static stats_t old;
    char path[540];
    FILE *f;
    size_t n;
    s_stats_dir[0] = s_public_dir[0] = s_admin_dir[0] = 0;
    s_st_failed = 0;
    geo_load(s_cfg.geoip_file);
    s_nhist = 0;
    s_started = stats_now();
    st_reset((long)(stats_now() / 86400));
    if (dir && *dir) {
        snprintf(s_stats_dir, sizeof s_stats_dir, "%s", dir);
#ifdef _WIN32
        _mkdir(s_stats_dir);
#else
        mkdir(s_stats_dir, 0755);
#endif
        st_path(path, sizeof path, "current.json");
        f = fopen(path, "rb");
        if (f) {
            n = fread(buf, 1, sizeof buf - 1, f);
            buf[n] = 0;
            fclose(f);
            if (st_parse(buf, &old)) {
                if (old.day < s_st.day)
                    st_append_daily(&old);      /* the server was down at midnight */
                else if (old.day == s_st.day) {
                    old.salt = s_st.salt;
                    if (old.peak_sessions < s_st.peak_sessions) old.peak_sessions = s_st.peak_sessions;
                    s_st = old;
                    logf_("statistics: carrying on today's totals from %s", path);
                }
            }
        }
        st_write_current();
        st_hist_load();
        if (!s_st_failed)
            logf_("statistics: writing to %s", s_stats_dir);
    }
    /* The public page: stats.json and index.html, in a folder a web server can read. */
    if (pubdir && *pubdir)
        snprintf(s_public_dir, sizeof s_public_dir, "%s", pubdir);
    else if (s_stats_dir[0] && page)
        snprintf(s_public_dir, sizeof s_public_dir, "%s/public", s_stats_dir);
    if (s_public_dir[0]) {
        char tok[72];
        int admin = s_cfg.admin_token_file && *s_cfg.admin_token_file;
        if (admin && !admin_token_read(s_cfg.admin_token_file, tok, sizeof tok)) {
            logf_("admin page: off (no usable token file)");
            admin = 0;
        }
        if (admin)
            snprintf(s_admin_dir, sizeof s_admin_dir, "%s/%s", s_public_dir, tok);
#ifdef _WIN32
        _mkdir(s_public_dir);
        if (admin) _mkdir(s_admin_dir);
#else
        /* with the owner's page in it the folder cannot be listed */
        mkdir(s_public_dir, 0755);
        chmod(s_public_dir, admin ? 0711 : 0755);
        if (admin) {
            mkdir(s_admin_dir, 0711);
            chmod(s_admin_dir, 0711);
        }
#endif
        if (page) {
            snprintf(path, sizeof path, "%s/index.html", s_public_dir);
            write_file(path, page, strlen(page));
        }
        if (admin) {
            char ap[740];
            if (s_cfg.admin_html) {
                snprintf(ap, sizeof ap, "%s/index.html", s_admin_dir);
                write_file(ap, s_cfg.admin_html, strlen(s_cfg.admin_html));
            }
            st_admin_history();
            logf_("admin page: on (a folder of the public one; its name is in the token file)");
        }
        st_write_public();
        if (!s_st_failed)
            logf_("public page: %s (stats.json every %d s)", s_public_dir, PUBLIC_SECS);
    }
}

static void st_free(void)
{
    geo_free();
    free(s_st.set);
    s_st.set = NULL;
    s_st.set_cap = s_st.set_n = 0;
}

static int mode_of(const char *params)
{
    const char *p = params;
    char *e;
    long v;
    int i;
    for (i = 0; i < 2; i++) {
        p = strchr(p, ',');
        if (!p)
            return N_MODES - 1;
        p++;
    }
    v = strtol(p, &e, 10);
    return e == p || v < 0 || v > 4 ? N_MODES - 1 : (int)v;
}

/* cc: the session's country (geo_lookup), counted with a new unique player */
static void st_player_cc(const char *name, int cc)
{
    if (name && *name && st_set_add(st_hash(name)))
        s_st.cc_unique[cc < 0 || cc >= N_CC ? CC_UNKNOWN : cc]++;
}

static void st_connection(void)
{
    st_roll();
    s_st.connections++;
    if (s_nsess > s_st.peak_sessions)
        s_st.peak_sessions = s_nsess;
}

static void st_refused(void) { st_roll(); s_st.refused++; }

static void st_signin_cc(const char *name, int cc)
{
    st_roll();
    if (cc < 0 || cc >= N_CC)
        cc = CC_UNKNOWN;
    s_st.signins++;
    s_st.cc_signins[cc]++;
    if (online_now() > s_st.peak_players)
        s_st.peak_players = online_now();
    st_player_cc(name, cc);
}

/* BO-365 race waits: one begun, and how each one ended (once per wait) */
static void st_wait(int what)
{
    st_roll();
    if (what == 0) s_st.waits++;
    else if (what == 1) s_st.waits_joined++;
    else if (what == 2) s_st.waits_gone++;
    else s_st.waits_full++;
}

static void st_game_created(const char *params) { st_roll(); s_st.created[mode_of(params)]++; }

static void st_game_started(const char *params, int players)
{
    st_roll();
    s_st.games_started++;
    s_st.started[mode_of(params)]++;
    if (players >= 1 && players <= MAX_PLAYERS)
        s_st.start_size[players]++;
}

static void st_result(void) { st_roll(); s_st.results++; }

/* BO-381 UDP relay: counts only (what was forwarded, what was dropped, games that used it) */
static void st_relay(int what, unsigned long bytes)
{
    st_roll();
    if (what == 0) { s_st.relay_packets++; s_st.relay_bytes += bytes; }
    else if (what == 1) s_st.relay_dropped++;
    else s_st.relay_games++;
}

/* ── the public page ───────────────────────────────────────────────── */
/* stats.json (every PUBLIC_SECS) and index.html in a folder a web server
 * reads. Aggregates and the list of open games only: a game's name is what
 * its host typed, its mode, how many players it has and whether it is under
 * way. No player name, no address. The name is cut and written as JSON
 * with every character that matters to HTML escaped (the page also shows
 * it as text, never as markup). */

static const char *const k_mode_label[N_MODES] = {
    "Single Race", "Road Rage", "Team Crush", "Double Impact", "Party Crash", "Other"
};

typedef struct { char *p; size_t n, cap; } jbuf_t;

static void ja(jbuf_t *j, const char *fmt, ...)
{
    va_list ap;
    int r;
    if (j->n + 1 >= j->cap)
        return;
    va_start(ap, fmt);
    r = vsnprintf(j->p + j->n, j->cap - j->n, fmt, ap);
    va_end(ap);
    if (r > 0)
        j->n = (size_t)r < j->cap - j->n ? j->n + (size_t)r : j->cap - 1;
}

/* A JSON string of at most `max` characters of s. Latin-1 above 0x7F (the
 * game's own text), control characters and < > & ' " \ become \u escapes. */
static void j_str(jbuf_t *j, const char *s, int max)
{
    int k = 0;
    ja(j, "\"");
    for (; *s && k < max; s++, k++) {
        unsigned char c = (unsigned char)*s;
        if (c < 0x20 || c >= 0x7F || c == '<' || c == '>' || c == '&' || c == '\'' || c == '"' || c == '\\')
            ja(j, "\\u%04x", c);
        else
            ja(j, "%c", c);
    }
    ja(j, "\"");
}

static void j_day(jbuf_t *j, const stats_t *st, int full)
{
    int y, m, d, i;
    unsigned long created = 0;
    civil_from_days(st->day, &y, &m, &d);
    for (i = 0; i < N_MODES; i++)
        created += st->created[i];
    ja(j, "{\"date\":\"%04d-%02d-%02d\",\"signins\":%lu,\"unique_players\":%lu,\"peak_players\":%d,"
          "\"games_created\":%lu,\"games_started\":%lu,\"race_results\":%lu",
       y, m, d, st->signins, st->unique_base + (unsigned long)st->set_n, st->peak_players, created,
       st->games_started, st->results);
    if (full) {
        ja(j, ",\"peak_sessions\":%d,\"by_mode\":[", st->peak_sessions);
        for (i = 0; i < N_MODES; i++)
            ja(j, "%s{\"mode\":\"%s\",\"created\":%lu,\"started\":%lu}", i ? "," : "",
               k_mode_label[i], st->created[i], st->started[i]);
        ja(j, "]");
    }
    ja(j, "}");
}

/* fullest first, then oldest (the order a search and the page list games in) */
static const game_t *s_cmp_games;
static int cmp_games(const void *a, const void *b)
{
    const game_t *x = &s_cmp_games[*(const int *)a], *y = &s_cmp_games[*(const int *)b];
    if (x->np != y->np)
        return y->np - x->np;                 /* fullest first */
    if (x->t_ms != y->t_ms)
        return x->t_ms < y->t_ms ? -1 : 1;    /* then oldest */
    return x->ident - y->ident;
}

static int game_racing(const game_t *g) { return (strtoul(g->sysflags, NULL, 10) & 0x80000ul) != 0; }

static void st_write_public(void)
{
    jbuf_t j;
    char path[720];
    int i, waiting = 0, racing = 0, priv = 0, listed = 0, pass, nidx = 0, *idx;
    if (!s_public_dir[0])
        return;
    idx = (int *)malloc((size_t)MAX_GAMES * sizeof *idx);
    if (!idx)
        return;
    j.cap = 96 * 1024;
    j.n = 0;
    j.p = (char *)malloc(j.cap);
    if (!j.p) {
        free(idx);
        return;
    }
    /* Private games (made with a game code) are only counted: no name, no
     * code, no player appears. */
    for (i = 0; s_games && i < MAX_GAMES; i++)
        if (s_games[i].used && s_games[i].np > 0) {
            if (s_games[i].priv) priv++;
            else {
                idx[nidx++] = i;
                if (game_racing(&s_games[i])) racing++; else waiting++;
            }
        }
    /* a started game is gone from s_games seconds after its start: the
     * races under way are the race records (BO-365) */
    racing = races_public_live();
    s_cmp_games = s_games;
    qsort(idx, (size_t)nidx, sizeof *idx, cmp_games);
    s_pub_written = stats_now();
    ja(&j, "{\"version\":1,\"updated\":%lld,\"started\":%lld,", (long long)s_pub_written, (long long)s_started);
    ja(&j, "\"now\":{\"sessions\":%d,\"players\":%d,\"games_waiting\":%d,\"games_racing\":%d,\"games_private\":%d},",
       s_nsess, online_now(), waiting, racing, priv);
    ja(&j, "\"today\":");
    j_day(&j, &s_st, 1);
    ja(&j, ",\"days\":[");
    for (i = 0; i < s_nhist; i++) {
        ja(&j, i ? "," : "");
        j_day(&j, &s_hist[i], 0);
    }
    ja(&j, "],\"games\":[");
    /* Waiting games first, then the ones under way; at most GAME_LIST_MAX. */
    for (pass = 0; pass < 2; pass++)
        for (i = 0; i < nidx && listed < GAME_LIST_MAX; i++) {
            const game_t *g = &s_games[idx[i]];
            int mx;
            if (game_racing(g) != pass)
                continue;
            mx = atoi(g->maxsize);
            if (mx < 1 || mx > s_max_players)
                mx = s_max_players;
            ja(&j, listed ? "," : "");
            ja(&j, "{\"name\":");
            j_str(&j, g->name, GAME_NAME_CUT);
            ja(&j, ",\"mode\":\"%s\",\"players\":%d,\"max\":%d,\"state\":\"%s\",\"locked\":%s}",
               k_mode_label[mode_of(g->params)], g->np, mx, pass ? "racing" : "waiting",
               g->pass[0] ? "true" : "false");
            listed++;
        }
    ja(&j, "]}\n");
    snprintf(path, sizeof path, "%s/stats.json", s_public_dir);
    write_file(path, j.p, j.n);
    if (s_admin_dir[0]) {               /* the owner's page: today with every country, and now */
        char ap[740];
        j.n = 0;
        ja(&j, "{\"version\":1,\"updated\":%lld,\"started\":%lld,\"geo_ranges\":%lu,",
           (long long)s_pub_written, (long long)s_started, (unsigned long)s_geo_n);
        ja(&j, "\"now\":{\"sessions\":%d,\"players\":%d,\"games_waiting\":%d,\"games_racing\":%d,\"games_private\":%d},",
           s_nsess, online_now(), waiting, racing, priv);
        st_format(&s_st, 1, s_line, ST_LINE_MAX);
        ja(&j, "\"today\":%s}\n", s_line);
        snprintf(ap, sizeof ap, "%s/now.json", s_admin_dir);
        write_file(ap, j.p, j.n);
    }
    free(j.p);
    free(idx);
}

/* ── bodies ────────────────────────────────────────────────────────── */

typedef struct { char b[BODY_MAX]; size_t n; } body_t;

static void kv_s(body_t *b, const char *k, const char *v)
{
    int r = snprintf(b->b + b->n, sizeof b->b - b->n, "%s=%s\n", k, v ? v : "");
    if (r > 0 && (size_t)r < sizeof b->b - b->n)
        b->n += (size_t)r;
}

static void kv_i(body_t *b, const char *k, long v)
{
    char t[24];
    snprintf(t, sizeof t, "%ld", v);
    kv_s(b, k, t);
}

/* A field of a request: KEY=VALUE separated by newlines or blanks. */
static int field(const char *body, const char *key, char *out, size_t n)
{
    size_t kl = strlen(key);
    const char *p = body;
    while (*p) {
        while (*p == '\n' || *p == ' ' || *p == '\r' || *p == '\t')
            p++;
        if (!strncmp(p, key, kl) && p[kl] == '=') {
            size_t i = 0;
            p += kl + 1;
            while (*p && *p != '\n' && *p != '\r' && i + 1 < n)
                out[i++] = *p++;
            out[i] = 0;
            return 1;
        }
        while (*p && *p != '\n' && *p != ' ')
            p++;
    }
    if (n)
        out[0] = 0;
    return 0;
}

/* ── what a client may send ────────────────────────────────────────── */
/* Everything that is stored and relayed to other players' copies of the
 * game is checked first against a strict form; a request that fails ends
 * the session. A name is printable ASCII (no '='), a parameter list is
 * comma-separated decimal integers, sizes are 2 to 8. */

static int v_name(const char *s, int maxlen)
{
    int n = 0;
    for (; *s; s++, n++)
        if ((unsigned char)*s < 0x20 || (unsigned char)*s > 0x7E || *s == '=')
            return 0;
    return n >= 1 && n <= maxlen;
}

/* A decimal integer in [lo, hi]. */
static int v_int(const char *s, long long lo, long long hi)
{
    long long v = 0;
    int neg = 0, d = 0;
    if (*s == '-') { neg = 1; s++; }
    for (; *s >= '0' && *s <= '9'; s++, d++) {
        if (d >= 10)
            return 0;
        v = v * 10 + (*s - '0');
    }
    if (*s || !d)
        return 0;
    if (neg)
        v = -v;
    return v >= lo && v <= hi;
}

/* "1,-2,3," : up to maxitems 32-bit decimals (signed or unsigned), a trailing
 * comma allowed, nothing else. */
static int v_list(const char *s, int maxitems)
{
    int n = 0;
    while (*s) {
        long long v = 0;
        int neg = 0, d = 0;
        if (*s == '-') { neg = 1; s++; }
        for (; *s >= '0' && *s <= '9'; s++, d++) {
            if (d >= 10)
                return 0;
            v = v * 10 + (*s - '0');
        }
        if (!d)
            return 0;
        if (neg)
            v = -v;
        if (v < -2147483648LL || v > 4294967295LL || ++n > maxitems)
            return 0;
        if (*s == ',')
            s++;
        else if (*s)
            return 0;
    }
    return 1;
}

/* a game code: CODE_MIN to CODE_MAX letters and digits */
static int v_code(const char *s)
{
    int n = 0;
    for (; *s; s++, n++)
        if (!((*s >= '0' && *s <= '9') || (*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z')))
            return 0;
    return n >= CODE_MIN && n <= CODE_MAX;
}

/* "$" and 16 hex digits */
static int v_xuid(const char *s)
{
    int i;
    if (*s++ != '$')
        return 0;
    for (i = 0; i < 16; i++, s++)
        if (!((*s >= '0' && *s <= '9') || (*s >= 'a' && *s <= 'f') || (*s >= 'A' && *s <= 'F')))
            return 0;
    return *s == 0;
}

/* NULL when the request is well formed, else the name of the bad field. */
static const char *validate(const char *cmd, const char *body)
{
    char v[256];
#define FLD(k) field(body, k, v, sizeof v)
#define OPT(k, test) if (FLD(k) && v[0] && !(test)) return k
    if (!memcmp(cmd, "addr", 4)) {
        OPT("ADDR", is_dotted(v));
        OPT("PORT", v_int(v, 0, 65535));
        OPT("TKID", v_int(v, 0, 9));
        OPT("TKNAME", v_name(v, NAME_MAX_CHARS));
        OPT("TKCROWD", v_int(v, 0, 1));
    } else if (!memcmp(cmd, "auth", 4)) {
        OPT("GTAG", v_name(v, NAME_MAX_CHARS));
        OPT("NAME", v_name(v, NAME_MAX_CHARS));
        OPT("XUID", v_xuid(v));
        OPT("TKID", v_int(v, 0, 9));
    } else if (!memcmp(cmd, "pers", 4)) {
        OPT("PERS", v_name(v, NAME_MAX_CHARS));
        OPT("GTAG", v_name(v, NAME_MAX_CHARS));
    } else if (!memcmp(cmd, "sele", 4)) {
        OPT("TKCODE", v_code(v));
    } else if (!memcmp(cmd, "move", 4)) {
        OPT("IDENT", v_int(v, -2147483648LL, 4294967295LL));
        OPT("TKCODE", v_code(v));
    } else if (!memcmp(cmd, "gsea", 4)) {
        OPT("START", v_int(v, -2147483648LL, 4294967295LL));
        OPT("COUNT", v_int(v, -2147483648LL, 4294967295LL));
        OPT("TKCODE", v_code(v));
        OPT("TKWAITFOR", v_int(v, 1, 2147483647LL));
        OPT("TKBROWSE", v_int(v, 0, 1));
    } else if (!memcmp(cmd, "gcre", 4)) {
        OPT("NAME", v_name(v, GAME_NAME_MAX));
        OPT("PASS", v_name(v, PASS_MAX));
        OPT("PARAMS", v_list(v, 16));
        OPT("SYSFLAGS", v_int(v, 0, 4294967295LL));
        OPT("CUSTFLAGS", v_list(v, 4));
        OPT("MINSIZE", v_int(v, 1, 8));
        OPT("MAXSIZE", v_int(v, 2, 8));
        OPT("ROOM", v_int(v, -2147483648LL, 4294967295LL));
        OPT("TKCODE", v_code(v));
        OPT("TKPUBLIC", v_int(v, 0, 1));
    } else if (!memcmp(cmd, "gjoi", 4)) {
        OPT("IDENT", v_int(v, -2147483648LL, 4294967295LL));
        OPT("NAME", v_name(v, GAME_NAME_MAX));
        OPT("PASS", v_name(v, PASS_MAX));
        OPT("USERPARAMS", v_list(v, 8));
        OPT("TKCODE", v_code(v));
    } else if (!memcmp(cmd, "gset", 4)) {
        OPT("USERPARAMS", v_list(v, 8));
        OPT("PARAMS", v_list(v, 16));
        OPT("SYSFLAGS", v_int(v, 0, 4294967295LL));
        OPT("CUSTFLAGS", v_list(v, 4));
        OPT("MINSIZE", v_int(v, 1, 8));
        OPT("MAXSIZE", v_int(v, 2, 8));
        OPT("KICK", v_name(v, NAME_MAX_CHARS));
    }
#undef OPT
#undef FLD
    return NULL;
}

/* ── sending ───────────────────────────────────────────────────────── */

static void out_append(sess_t *s, const unsigned char *p, size_t n)
{
    size_t pend = s->outlen - s->outoff;
    if (s->outoff && s->outoff == s->outlen)
        s->outoff = s->outlen = 0;
    if (pend + n > OUT_CAP) {         /* it does not read: dropped at the end of the pass */
        s->why = "output queue full";
        s->dead = 1;
        s->room = 0;
        return;
    }
    if (s->outoff && s->outlen + n > s->outcap) {
        memmove(s->out, s->out + s->outoff, pend);
        s->outoff = 0;
        s->outlen = pend;
    }
    if (s->outlen + n > s->outcap) {
        size_t cap = s->outcap ? s->outcap : 4096;
        unsigned char *q;
        while (cap < s->outlen + n)
            cap *= 2;
        q = (unsigned char *)realloc(s->out, cap);
        if (!q) {
            s->why = "out of memory";
            s->dead = 1;
            s->room = 0;
            return;
        }
        s->out = q;
        s->outcap = cap;
    }
    memcpy(s->out + s->outlen, p, n);
    s->outlen += n;
}

/* The socket can take more: send what is queued. */
static void out_flush(sess_t *s)
{
    while (s->outoff < s->outlen && !s->dead) {
        int r = (int)send(s->fd, (const char *)s->out + s->outoff, (int)(s->outlen - s->outoff), SEND_FLAGS);
        if (r < 0) {
            if (sock_wouldblock() || sock_interrupted())
                return;
            s->why = sock_reset() ? "connection reset" : "send failed";
            s->dead = 1;
            s->room = 0;
            return;
        }
        s->outoff += (size_t)r;
    }
    if (s->outoff >= s->outlen) {
        s->outoff = s->outlen = 0;
        if (s->outcap > 65536) {
            free(s->out);
            s->out = NULL;
            s->outcap = 0;
        }
    }
}

static void send_pkt(int si, const char *cmd, const char *code, const char *body)
{
    sess_t *s = &s_sess[si];
    unsigned char buf[12 + BODY_MAX + 1];
    size_t bl = body && *body ? strlen(body) + 1 : 0;
    uint32_t len;
    if (!s->used || s->dead)
        return;
    if (bl > BODY_MAX)
        bl = BODY_MAX;
    len = (uint32_t)(12 + bl);
    memcpy(buf, cmd, 4);
    if (code)
        memcpy(buf + 4, code, 4);
    else
        memset(buf + 4, 0, 4);
    buf[8] = (unsigned char)(len >> 24); buf[9] = (unsigned char)(len >> 16);
    buf[10] = (unsigned char)(len >> 8); buf[11] = (unsigned char)len;
    if (bl) {
        memcpy(buf + 12, body, bl);
        buf[12 + bl - 1] = 0;
    }
    /* The socket never blocks: what it does not take now is queued and sent
     * when it can (POLLOUT); a player whose queue passes OUT_CAP does not
     * read and is dropped at the end of the pass. */
    if (s->outlen > s->outoff) {
        out_append(s, buf, len);
    } else {
        int r = (int)send(s->fd, (const char *)buf, (int)len, SEND_FLAGS);
        if (r < 0) {
            if (sock_wouldblock() || sock_interrupted())
                r = 0;
            else {
                s->why = sock_reset() ? "connection reset" : "send failed";
                s->dead = 1;
                s->room = 0;
                return;
            }
        }
        if ((uint32_t)r < len)
            out_append(s, buf + r, len - (uint32_t)r);
    }
    if (s_cfg.verbose && strcmp(cmd, "~png"))
        logs_(s, "-> %s %.4s %s", s->name[0] ? s->name : s->peer, cmd, body ? oneline(body) : "");
}

static void reply(int si, const char *cmd, body_t *b)
{
    b->b[b->n] = 0;
    send_pkt(si, cmd, NULL, b->b);
}

/* ── records ───────────────────────────────────────────────────────── */

static const char *room_name(int id)
{
    int i;
    for (i = 0; i < N_ROOMS; i++)
        if (k_rooms[i].id == id)
            return k_rooms[i].name;
    return "";
}

static int in_room_count(int id)
{
    int i, n = 0;
    for (i = 0; i < MAX_SESS; i++)
        if (s_sess[i].used && s_sess[i].room == id)
            n++;
    return n;
}

/* ── identity: a synthetic title address per capable session ─────────── */
/* A session that sends TKID=1 gets an address from 10.64.0.0/16, unique among
 * the live sessions. Other capable players' titles see that address as its
 * address (in LADDRn, LA and the XUID's embedded address); the real local
 * address goes in TKRADDRn / TKRLA. Two players behind the same 192.168.1.100
 * are two players. A player's own entry is left as it sent it. See
 * .handoff/online-identity-spec.md. */

#define SYNTH_N 65535
static unsigned char s_synth_used[SYNTH_N];
static int s_synth_next = 1;

static uint32_t ip_of(const char *a) { uint32_t n = 0; parse_ip(a, &n); return n; }

static uint32_t synth_net(int idx)
{
    unsigned char a[4];
    uint32_t n;
    a[0] = 10; a[1] = 64; a[2] = (unsigned char)(idx >> 8); a[3] = (unsigned char)idx;
    memcpy(&n, a, 4);
    return n;
}

/* A free address that no live legacy session claims as its own; 0 when none. */
static int synth_alloc(void)
{
    int tries, i;
    for (tries = 0; tries < SYNTH_N - 1; tries++) {
        int idx = s_synth_next++;
        uint32_t n;
        int clash = 0;
        if (s_synth_next >= SYNTH_N)
            s_synth_next = 1;
        if (s_synth_used[idx])
            continue;
        n = synth_net(idx);
        for (i = 0; i < MAX_SESS && !clash; i++)
            if (s_sess[i].used && !s_sess[i].tkid && ip_of(s_sess[i].laddr) == n)
                clash = 1;
        if (clash)
            continue;
        s_synth_used[idx] = 1;
        return idx;
    }
    return 0;
}

/* A sticky lease: when a capable session ends, its synthetic address is kept
 * for LEASE_SECS for the same player coming back from the same public address,
 * so the other players' routes to it stay right across a re-sign-in. Keyed by
 * the public address and the gamertag (TKNAME in addr, or the session's name
 * at auth); a client that does not send TKNAME is matched by the local address
 * it claims instead. A leased address stays marked used in the pool, so nobody
 * else gets it; if the pool is full the oldest leases go first. At most
 * MAX_SESS leases. */
#define LEASE_SECS 1800
typedef struct {
    int used, idx;
    unsigned long seq;
    time_t until;
    uint32_t peer_net;
    char name[32], laddr[16];
} lease_t;
static lease_t *s_lease;
static unsigned long s_lease_seq;
static int s_lease_secs = LEASE_SECS;     /* (tests change it) */

/* Players in a race. Every player closes the lobby connection at the race
 * start, so a racer is no session, and TKONLINE would say 0 with everybody on
 * the road. A session that ends within RACE_HOLD_SECS of its game's gsta leaves
 * a record here, which counts as one player until that time is over (a crash or
 * a quit in the race fades out by itself) or the same name signs in again,
 * whatever the address (a phone changes network in a race): nobody counts twice.
 *
 * One table as big as the sessions'; one address keeps at most --max-per-ip
 * records, and a start with one player keeps none, so
 * nobody raises the number by starting games alone. */
/* Long enough for the longest race, from the start to signing in again. The
 * title's mode page goes up to 5 laps. Measured, two copies on autopilot: a lap
 * of 102 to 120 s (3 laps in 306 to 361 s), and 36 to 51 s more from the finish
 * through the results to the sign-in: 5 * 120 + 51 = 651 s, and 10% on top. A
 * Crash event is a minute or so and ends sooner: its players sign in again and
 * drop out of the table then, so the time only ever matters for a crash or a
 * quit in a race, which counts for at most this long. A race that runs longer
 * is one player too few until the sign-in, never one too many. */
#define RACE_HOLD_SECS 720
typedef struct {
    int used;
    unsigned long seq;
    time_t until;
    uint32_t peer_net;
    char name[32];
} racer_t;
static racer_t *s_racer;
static unsigned long s_racer_seq;
static int s_race_hold_secs = RACE_HOLD_SECS;     /* (tests change it) */

/* BO-365: a game whose race is under way. Every player closes its lobby
 * connection at the race start, so the started game_t is gone seconds after
 * gsta; this record is what is left of it: who hosted it (name and public
 * address), who raced, its code. After the race the host makes the game
 * again (stay together: a new game with the same name and code); its gcre is
 * linked here as the race's successor, and players who asked to wait for the
 * race (gsea TKWAITFOR) are offered it, after the racers' own seats. */
#define REHOST_GRACE_MS 60000     /* host back (or all the others back), no game made again: over */
#define RESERVE_MS      25000     /* the racers' seats in the successor are kept this long after it is made */
#define RACE_TAIL_MS    60000     /* a race that is over stays this long for its waiters' next poll */
/* Stay together (item 3 of the 0.3.0 review): a player of the race is slower
 * than the others (still racing, on the results, at the autosave's question),
 * so the others wait for it. The host not back while every other player is:
 * over only after HOST_SLOW_MS. A player of the race not back yet in the game
 * made again keeps its seat, and the host's lobby waits for it (TKCOMING),
 * for COMING_MS after that game was made. */
#define HOST_SLOW_MS    180000
#define COMING_MS       180000
#define COMING_SIGNIN_MS 20000    /* signed in this recently: on its way from the sign-in to the game */
#define COMING_POLL_MS  10000     /* a waiter's last poll this recently: still waiting */
static long long s_rehost_grace_ms = REHOST_GRACE_MS, s_reserve_ms = RESERVE_MS, s_race_tail_ms = RACE_TAIL_MS;   /* (tests change them) */
static long long s_host_slow_ms = HOST_SLOW_MS, s_coming_ms = COMING_MS;
typedef struct {
    int used;
    int ident;                  /* the started game's IDENT: what a waiter asks for (TKWAITFOR) */
    char gid[13];
    char name[64], code[CODE_MAX + 1];
    int priv;                   /* a private game: visible to its code only */
    int crowd;                  /* made by a load-tool player (--crowd-test): only they see it (BO-366) */
    char host[32];
    char params[256], custflags[48];    /* the game's: a browse search lists the race as a game (BO-372) */
    int room;
    uint32_t host_net;          /* the host's public address at gsta: the successor must come from both */
    char member[MAX_PLAYERS][32];
    int nmember;                /* players at the start (2 or more, else no record) */
    int maxsize;
    unsigned kicked[16];        /* inherited by the successor (BO-364) */
    int nkicked;
    long long started_ms;
    long long host_back_ms;     /* the host's name and address signed in again (0: not yet) */
    long long all_back_ms;      /* every other member signed in again, the host not (0: not yet) */
    int successor;              /* IDENT of the host's new game (0: none now) */
    long long successor_ms;     /* when the last one was linked (the reservation runs from here) */
    int fwd;                    /* the successor raced: the ident of that race's record */
    int gone;                   /* over: nobody to wait for */
    long long gone_ms;
    char why[8];                /* how it ended, for its waiters (TKWAITWHY): quit, left, absent, time, full */
    unsigned heard;             /* the members (bit k) told it is gone (TKWAITWHY) */
    unsigned left;              /* the members (bit k: member[k]) that left the group: quit on the results, another game */
    int coming_sent;            /* the TKCOMING its game made again last went out with (-1: none yet) */
} race_t;
static race_t *s_races;
static unsigned long s_wait_seq;

static void racer_clear(racer_t *r)
{
    memset(r, 0, sizeof *r);
    s_racers--;
}

/* name signed in: it is a session again */
static void racer_take(const char *name)
{
    int i;
    for (i = 0; s_racer && i < MAX_SESS; i++)
        if (s_racer[i].used && !strcmp(s_racer[i].name, name))
            racer_clear(&s_racer[i]);
}

static void racer_expire(time_t now)
{
    int i;
    for (i = 0; s_racer && i < MAX_SESS; i++)
        if (s_racer[i].used && s_racer[i].until <= now)
            racer_clear(&s_racer[i]);
}

/* the session ends: a player of a game started within the hold is racing */
static void racer_add(const sess_t *s)
{
    int i, slot = -1, oldest = -1, same_addr = 0;
    time_t until = s->race_at + s_race_hold_secs;
    if (!s_racer || !s->authed || !s->race_at || until <= time(NULL))
        return;
    racer_take(s->name);
    for (i = 0; i < MAX_SESS; i++) {
        if (!s_racer[i].used) {
            if (slot < 0)
                slot = i;
            continue;
        }
        if (s_racer[i].peer_net == s->peer_net)
            same_addr++;
        if (oldest < 0 || s_racer[i].seq < s_racer[oldest].seq)
            oldest = i;
    }
    if (s_per_ip > 0 && same_addr >= s_per_ip)
        return;
    if (slot < 0) {                         /* full: the oldest goes (one too few, never one too many) */
        racer_clear(&s_racer[oldest]);
        slot = oldest;
    }
    s_racer[slot].used = 1;
    s_racer[slot].seq = ++s_racer_seq;
    s_racer[slot].until = until;
    s_racer[slot].peer_net = s->peer_net;
    snprintf(s_racer[slot].name, sizeof s_racer[slot].name, "%s", s->name);
    s_racers++;
}

static void lease_drop(lease_t *l)
{
    if (l->used && l->idx > 0 && l->idx < SYNTH_N)
        s_synth_used[l->idx] = 0;
    memset(l, 0, sizeof *l);
}

static void lease_expire(time_t now)
{
    int i;
    for (i = 0; s_lease && i < MAX_SESS; i++)
        if (s_lease[i].used && s_lease[i].until <= now)
            lease_drop(&s_lease[i]);
}

/* The oldest lease goes (the pool is full, or the table); 0 when there is none. */
static int lease_evict_oldest(void)
{
    int i, best = -1;
    for (i = 0; s_lease && i < MAX_SESS; i++)
        if (s_lease[i].used && (best < 0 || s_lease[i].seq < s_lease[best].seq))
            best = i;
    if (best < 0)
        return 0;
    lease_drop(&s_lease[best]);
    return 1;
}

/* 1 when the address is now leased (its pool bit stays set), 0 when it is freed by the caller */
static int lease_add(const sess_t *s)
{
    int i, slot = -1;
    if (!s_lease || !s->authed || s->synth_idx <= 0 || s_lease_secs <= 0)
        return 0;
    for (i = 0; i < MAX_SESS; i++)          /* the same player again: one lease */
        if (s_lease[i].used && s_lease[i].peer_net == s->peer_net && !strcmp(s_lease[i].name, s->name)) {
            lease_drop(&s_lease[i]);
            slot = i;
            break;
        }
    for (i = 0; slot < 0 && i < MAX_SESS; i++)
        if (!s_lease[i].used)
            slot = i;
    if (slot < 0) {                         /* the table is full: the oldest lease goes */
        lease_evict_oldest();
        for (i = 0; slot < 0 && i < MAX_SESS; i++)
            if (!s_lease[i].used)
                slot = i;
    }
    if (slot < 0)
        return 0;
    s_lease[slot].used = 1;
    s_lease[slot].idx = s->synth_idx;       /* the pool bit stays set */
    s_lease[slot].seq = ++s_lease_seq;
    s_lease[slot].until = time(NULL) + s_lease_secs;
    s_lease[slot].peer_net = s->peer_net;
    snprintf(s_lease[slot].name, sizeof s_lease[slot].name, "%s", s->name);
    snprintf(s_lease[slot].laddr, sizeof s_lease[slot].laddr, "%s", s->laddr);
    return 1;
}

/* The address leased to this player, taken out of the table (0: none). The
 * name is the gamertag the client gave; without one the claimed local address matches. */
static int lease_take(const sess_t *s, const char *name, char *leased_name, size_t n)
{
    int i, best = -1;
    time_t now = time(NULL);
    for (i = 0; s_lease && i < MAX_SESS; i++) {
        const lease_t *l = &s_lease[i];
        int k, held = 0;
        if (!l->used || l->until <= now || l->peer_net != s->peer_net)
            continue;
        if (name[0] ? strcmp(l->name, name) != 0 : strcmp(l->laddr, s->laddr) != 0)
            continue;
        for (k = 0; k < MAX_SESS; k++)
            if (s_sess[k].used && s_sess[k].synth_idx == l->idx)
                held = 1;
        if (held)
            continue;
        if (best < 0 || l->seq > s_lease[best].seq)
            best = i;
    }
    if (best < 0)
        return 0;
    {
        int idx = s_lease[best].idx;
        snprintf(leased_name, n, "%s", s_lease[best].name);
        memset(&s_lease[best], 0, sizeof s_lease[best]);   /* the pool bit stays set: the session holds it now */
        return idx;
    }
}

static void synth_free(sess_t *s)
{
    if (s->synth_idx > 0 && s->synth_idx < SYNTH_N) {
        if (!lease_add(s))
            s_synth_used[s->synth_idx] = 0;
    }
    s->synth_idx = 0;
    s->synth = 0;
}

static game_t *game_by_ident(int ident);

/* A game id: 12 hex digits from the CSPRNG, not that of a live game; after a
 * restart it is a new one (it is never counted up like IDENT). */
static int new_gid(char *out)
{
    int tries, i;
    for (tries = 0; tries < 32; tries++) {
        if (!make_hex(out, 6))
            return 0;
        for (i = 0; i < MAX_GAMES; i++)
            if (s_games[i].used && s_games[i].gid != out && !strcmp(s_games[i].gid, out))
                break;
        if (i == MAX_GAMES)
            return 1;
    }
    return 0;
}

/* The address viewer's title uses for player m: its own entry as it sent it;
 * a capable player's synthetic address for a capable viewer; for a legacy
 * viewer the real one, unless another member has the same real address. */
static uint32_t shown_addr(int m, int viewer)
{
    const sess_t *M = &s_sess[m];
    uint32_t real = ip_of(M->laddr);
    const game_t *g;
    int i;
    if (m == viewer || !M->tkid || !M->synth)
        return real;
    if (viewer >= 0 && s_sess[viewer].tkid)
        return M->synth;
    g = game_by_ident(M->game);
    for (i = 0; g && i < g->np; i++)
        if (g->players[i] != m && ip_of(s_sess[g->players[i]].laddr) == real)
            return M->synth;
    return real;
}

/* The XUID of m with the address it shows (hash, port offset and user index kept). */
static void xuid_shown(const sess_t *m, uint32_t shown, char *out, size_t n)
{
    unsigned char u[8], real[4], sh[4];
    uint32_t r = ip_of(m->laddr);
    int k, upper = 0, idx;
    if (!m->xuid[0]) {
        out[0] = 0;
        return;
    }
    for (k = 0; k < 8; k++) {
        char t[3];
        t[0] = m->xuid[1 + 2 * k]; t[1] = m->xuid[2 + 2 * k]; t[2] = 0;
        u[7 - k] = (unsigned char)strtoul(t, NULL, 16);
    }
    for (k = 1; k < 17; k++)
        if (m->xuid[k] >= 'A' && m->xuid[k] <= 'F')
            upper = 1;
    memcpy(real, &r, 4);
    memcpy(sh, &shown, 4);
    idx = (u[7] ^ real[3]) < 4 ? (u[7] ^ real[3]) : 0;
    u[4] = sh[0]; u[5] = sh[1]; u[6] = sh[2]; u[7] = (unsigned char)(sh[3] ^ idx);
    snprintf(out, n, upper ? "$%02X%02X%02X%02X%02X%02X%02X%02X" : "$%02x%02x%02x%02x%02x%02x%02x%02x",
             u[7], u[6], u[5], u[4], u[3], u[2], u[1], u[0]);
}

static void who(body_t *b, int si, int viewer)
{
    sess_t *s = &s_sess[si];
    uint32_t shown = shown_addr(si, viewer);
    char sh[16], ma[24];
    int real_too = viewer >= 0 && viewer != si && s_sess[viewer].tkid && s->tkid && s->synth;
    addr_str(shown, sh, sizeof sh);
    if (shown == ip_of(s->laddr))
        snprintf(ma, sizeof ma, "%s", s->xuid);
    else
        xuid_shown(s, shown, ma, sizeof ma);
    b->n = 0;
    kv_i(b, "I", s->uid); kv_s(b, "N", s->name); kv_s(b, "M", s->name); kv_s(b, "F", "");
    kv_s(b, "A", s->peer); kv_s(b, "LA", sh); kv_i(b, "P", 0); kv_s(b, "S", "");
    kv_s(b, "X", ""); kv_i(b, "G", s->game); kv_s(b, "AT", ""); kv_i(b, "CL", 511);
    kv_i(b, "LV", 1049601); kv_i(b, "MD", 0); kv_s(b, "R", room_name(s->room));
    kv_i(b, "RI", s->room); kv_i(b, "RT", 0); kv_i(b, "RM", 0); kv_i(b, "RF", 0);
    /* MA is the player's XUID ($ + hex): a peer looks a player up by it
     * (record +0x198, 0x1BD970) to reach it peer to peer. */
    kv_i(b, "US", 0); kv_i(b, "HW", 0); kv_i(b, "RP", 0); kv_s(b, "MA", ma);
    kv_i(b, "WI", 0); kv_i(b, "WT", 0);
    if (real_too)                  /* a capable viewer also gets the real local address */
        kv_s(b, "TKRLA", s->laddr);
}

static int game_open(const game_t *g);

/* ── races under way (BO-365) ──────────────────────────────────────── */

/* The record of race ident, following a race that raced again (fwd) */
static race_t *race_by_ident(int ident)
{
    int i, hop;
    for (hop = 0; hop < 4 && ident > 0 && s_races; hop++) {
        race_t *r = NULL;
        for (i = 0; i < MAX_GAMES && !r; i++)
            if (s_races[i].used && s_races[i].ident == ident)
                r = &s_races[i];
        if (!r || !r->fwd)
            return r;
        ident = r->fwd;
    }
    return NULL;
}

/* the host's game made again, while it is there */
static game_t *race_succ(const race_t *r)
{
    game_t *g = r->successor ? game_by_ident(r->successor) : NULL;
    return g && g->np ? g : NULL;
}

static int race_live(const race_t *r) { return r->used && !r->gone && !r->fwd; }

static int race_cap(const race_t *r)
{
    int m = r->maxsize;
    return m < 2 || m > s_max_players ? s_max_players : m;
}

static int race_kicked(const race_t *r, int si)
{
    int k;
    for (k = 0; si >= 0 && k < 16 && k < r->nkicked; k++)
        if (r->kicked[k] == s_sess[si].uid)
            return 1;
    return 0;
}

static int race_member(const race_t *r, const char *name)
{
    int k;
    for (k = 0; k < r->nmember; k++)
        if (!strcmp(r->member[k], name))
            return 1;
    return 0;
}

/* member[k] is back in g, or in another game: not coming any more */
static int race_member_in(const race_t *r, const game_t *g, int k, int *elsewhere)
{
    int i, j;
    *elsewhere = 0;
    for (j = 0; g && j < g->np; j++)
        if (!strcmp(s_sess[g->players[j]].name, r->member[k]))
            return 1;
    for (i = 0; i < MAX_SESS; i++)
        if (s_sess[i].used && s_sess[i].game && (!g || s_sess[i].game != g->ident) && !strcmp(s_sess[i].name, r->member[k])) {
            *elsewhere = 1;
            return 0;
        }
    return 0;
}

/* Stay together: member[k] of r is on its way back to the game made again
 * (g): not in it, not in another game, not gone from the group (quit on the
 * results), and either not signed in again yet (still racing, on the
 * results, at the autosave's question), just signed in, or polling for this
 * race on its Quick Match page. Only for COMING_MS after g was made. */
static int race_member_coming(const race_t *r, const game_t *g, int k, long long now)
{
    int i, elsewhere, signed_in = 0;
    if (!strcmp(r->member[k], r->host) || (r->left >> k & 1u) || race_member_in(r, g, k, &elsewhere) || elsewhere)
        return 0;
    if (!r->successor_ms || now - r->successor_ms >= s_coming_ms)
        return 0;
    for (i = 0; i < MAX_SESS; i++) {
        const sess_t *o = &s_sess[i];
        if (!o->used || !o->authed || strcmp(o->name, r->member[k]))
            continue;
        signed_in = 1;
        if (now - o->auth_ms < COMING_SIGNIN_MS ||
            (o->wait_for && race_by_ident(o->wait_for) == r && now - o->wait_poll_ms < COMING_POLL_MS))
            return 1;
    }
    return !signed_in;
}

/* the players of r on their way back to g (TKCOMING in g's member record) */
static int race_coming(const race_t *r, const game_t *g)
{
    int k, n = 0;
    long long now = now_ms();
    for (k = 0; k < r->nmember; k++)
        n += race_member_coming(r, g, k, now);
    return n;
}

/* --crowd-test (BO-366): a race is seen only by players of the kind that
 * made it (a load-tool race is never offered to a real copy, nor the other
 * way round); si -1 is a real player. Off: everyone sees every race. */
static int race_kind(const race_t *r, int si)
{
    return !s_cfg.crowd_test || r->crowd == (si >= 0 ? s_sess[si].crowd : 0);
}

/* this search may know of race r: a private one only with its code */
static int race_visible(const race_t *r, const char *code)
{
    return !r->priv || (code[0] && !strcmp(r->code, code));
}

/* a race this search could wait for: room for one more, the code's rules
 * (with a code only the races with that code, without one only the public
 * ones), not kicked from it */
static int race_candidate(const race_t *r, const char *code, int si)
{
    if (!race_live(r) || r->nmember >= race_cap(r) || race_kicked(r, si) || !race_kind(r, si))
        return 0;
    return code[0] ? r->code[0] && !strcmp(r->code, code) : !r->priv;
}

static int races_public_live(void)
{
    int i, n = 0;
    for (i = 0; s_races && i < MAX_GAMES; i++)
        n += race_live(&s_races[i]) && !s_races[i].priv;
    return n;
}

static int races_for_strangers(void)
{
    int i, n = 0;
    for (i = 0; s_races && i < MAX_GAMES; i++)
        n += race_candidate(&s_races[i], "", -1);
    return n;
}

/* code: what its waiters are told (TKWAITWHY) */
static void race_over_why(race_t *r, const char *code, const char *why)
{
    if (r->gone)
        return;
    r->gone = 1;
    r->gone_ms = now_ms() ? now_ms() : 1;
    snprintf(r->why, sizeof r->why, "%s", code);
    logf_("race of game %d gid=%s over: nobody made it again (%s)", r->ident, r->gid, why);
}

static void race_over(race_t *r, const char *why)
{
    race_over_why(r, "left", why);
}

/* gsta: the started game becomes a race record (2 players or more, no
 * password but a code: a game with a password is never made again). A race
 * that game was the successor of is followed to the new one. */
static void race_started(const game_t *g)
{
    int i, slot = -1, old = -1, made = 0;
    race_t *r;
    if (!s_races)
        return;
    for (i = 0; i < MAX_GAMES; i++) {
        if (s_races[i].used && s_races[i].ident == g->ident)
            return;                         /* one record per game */
        if (!s_races[i].used) {
            if (slot < 0)
                slot = i;
        } else if (old < 0 || s_races[i].started_ms < s_races[old].started_ms)
            old = i;
    }
    if (g->np >= 2 && (!g->pass[0] || g->priv)) {
        if (slot < 0)
            slot = old;                     /* full: the oldest goes */
        r = &s_races[slot];
        memset(r, 0, sizeof *r);
        r->used = 1;
        r->ident = g->ident;
        snprintf(r->gid, sizeof r->gid, "%s", g->gid);
        snprintf(r->name, sizeof r->name, "%s", g->name);
        snprintf(r->code, sizeof r->code, "%s", g->code);
        r->priv = g->priv;
        r->crowd = g->crowd;
        snprintf(r->host, sizeof r->host, "%s", s_sess[g->players[0]].name);
        snprintf(r->params, sizeof r->params, "%s", g->params);
        snprintf(r->custflags, sizeof r->custflags, "%s", g->custflags);
        r->room = g->room;
        r->host_net = s_sess[g->players[0]].peer_net;
        for (i = 0; i < g->np && i < MAX_PLAYERS; i++)
            snprintf(r->member[i], sizeof r->member[i], "%s", s_sess[g->players[i]].name);
        r->nmember = i;
        r->maxsize = atoi(g->maxsize);
        memcpy(r->kicked, g->kicked, sizeof r->kicked);
        r->nkicked = g->nkicked;
        r->started_ms = now_ms() ? now_ms() : 1;
        r->coming_sent = -1;
        made = 1;
    }
    for (i = 0; i < MAX_GAMES; i++) {
        race_t *e = &s_races[i];
        if (!race_live(e) || e->successor != g->ident || e->ident == g->ident)
            continue;
        if (made) {                         /* its waiters wait for the new race */
            e->fwd = g->ident;
            e->gone = 1;
            e->gone_ms = now_ms() ? now_ms() : 1;
            logf_("race of game %d gid=%s: game %d raced again", e->ident, e->gid, g->ident);
        } else
            race_over_why(e, "full", "its game started again without room");
    }
}

/* gcre: the host of a race under way (its name and address) made a game: the
 * race's successor. Its kick list comes along. A game with a password is not
 * the same game. */
static void race_link(int si, game_t *g)
{
    const sess_t *s = &s_sess[si];
    race_t *best = NULL;
    int i, k, j;
    if (!s_races || (g->pass[0] && !g->priv))
        return;
    for (i = 0; i < MAX_GAMES; i++) {
        race_t *r = &s_races[i];
        if (race_live(r) && r->crowd == g->crowd && !strcmp(r->host, s->name) && r->host_net == s->peer_net && !race_succ(r) &&
            (!best || r->started_ms > best->started_ms))
            best = r;
    }
    if (!best)
        return;
    best->successor = g->ident;
    best->successor_ms = now_ms() ? now_ms() : 1;
    best->coming_sent = race_coming(best, g);      /* the gcre answer's record says it */
    if (!best->host_back_ms)
        best->host_back_ms = best->successor_ms;
    for (k = 0; k < best->nkicked && k < 16; k++) {
        for (j = 0; j < g->nkicked && j < 16 && g->kicked[j] != best->kicked[k]; j++)
            ;
        if (j >= g->nkicked || j >= 16)
            g->kicked[g->nkicked++ % 16] = best->kicked[k];
    }
    logf_("game %d gid=%s is game %d again (its host is back after the race)", g->ident, g->gid, best->ident);
}

/* auth: the host of a race signed in again */
static void race_host_back(int si)
{
    const sess_t *s = &s_sess[si];
    int i;
    for (i = 0; s_races && i < MAX_GAMES; i++) {
        race_t *r = &s_races[i];
        if (race_live(r) && !r->host_back_ms && !strcmp(r->host, s->name) && r->host_net == s->peer_net)
            r->host_back_ms = now_ms() ? now_ms() : 1;
    }
}

/* auth TKSTAYQUIT=1: the player quit on the results of its race. Its host:
 * the race is over (its waiters are told at once); another player: its seat
 * is not kept and the host's lobby does not wait for it. */
static void race_quit(int si)
{
    const sess_t *s = &s_sess[si];
    int i, k;
    for (i = 0; s_races && i < MAX_GAMES; i++) {
        race_t *r = &s_races[i];
        if (!race_live(r))
            continue;
        if (!strcmp(r->host, s->name) && r->host_net == s->peer_net) {
            race_over_why(r, "quit", "its host quit on the results");
            continue;
        }
        for (k = 0; k < r->nmember; k++)
            if (!strcmp(r->member[k], s->name) && !(r->left >> k & 1u)) {
                r->left |= 1u << k;
                logs_(s, "%s quit on the results of game %d gid=%s", s->name, r->ident, r->gid);
            }
    }
}

/* a game closed: a race it was the successor of has none now */
static void race_game_closed(int ident)
{
    int i;
    for (i = 0; s_races && i < MAX_GAMES; i++)
        if (race_live(&s_races[i]) && s_races[i].successor == ident)
            s_races[i].successor = 0;
}

/* the race g is the successor of, still waited for */
static race_t *race_of_successor(int ident)
{
    int i;
    for (i = 0; s_races && i < MAX_GAMES; i++)
        if (race_live(&s_races[i]) && s_races[i].successor == ident)
            return &s_races[i];
    return NULL;
}

/* the waiters of r before si (arrival order) */
static int race_rank(const race_t *r, int si)
{
    const sess_t *me = &s_sess[si];
    int i, n = 0;
    if (race_member(r, me->name))
        return 0;                       /* a racer has its own seat */
    for (i = 0; i < MAX_SESS; i++) {
        const sess_t *o = &s_sess[i];
        if (i == si || !o->used || !o->wait_for || race_by_ident(o->wait_for) != r || race_member(r, o->name))
            continue;
        if (o->wait_seq < me->wait_seq)
            n++;
    }
    return n;
}

static int race_waiters(const race_t *r)
{
    int i, n = 0;
    for (i = 0; i < MAX_SESS; i++)
        if (s_sess[i].used && s_sess[i].wait_for && (r->fwd ? s_sess[i].wait_for == r->ident : race_by_ident(s_sess[i].wait_for) == r))
            n++;
    return n;
}

/* Seats of the successor g that are free for players who did not race in r:
 * within RESERVE_MS of its making, the racers not back in it (and not in
 * another game) keep theirs. */
static int race_free_seats(const race_t *r, const game_t *g)
{
    int cap = atoi(g->maxsize), k, reserved = 0, window;
    long long now = now_ms();
    if (cap < 2 || cap > s_max_players)
        cap = s_max_players;
    window = now - r->successor_ms < s_reserve_ms;
    for (k = 0; k < r->nmember; k++) {
        int elsewhere;
        if (race_member_in(r, g, k, &elsewhere) || elsewhere)
            continue;
        /* a slow racer (stay together) keeps its seat past the window */
        reserved += (window && !(r->left >> k & 1u)) || race_member_coming(r, g, k, now);
    }
    return cap - g->np - reserved;
}

/* a search may list g to si: a game made again after a race whose racers'
 * seats fill it is left out for players who did not race in it (their join
 * would only be answered "full"). A waiter for that race is ranked by the
 * gsea's own TKWAITFOR answer instead. */
static int race_seat_for(const game_t *g, int si)
{
    const sess_t *s = si >= 0 ? &s_sess[si] : NULL;     /* -1: anyone (a stranger) */
    race_t *rr = race_of_successor(g->ident);
    if (!rr || (s && (race_member(rr, s->name) || (s->wait_for && race_by_ident(s->wait_for) == rr))))
        return 1;
    return race_free_seats(rr, g) > 0;
}

/* a gsea's TKWAITFOR (0: none): the session waits for that race, or not any more */
static void race_wait_note(int si, int ident)
{
    sess_t *s = &s_sess[si];
    race_t *r;
    if (ident != s->wait_for) {
        s->wait_for = ident;
        s->wait_since = now_ms();
        s->wait_seq = ++s_wait_seq;
    }
    s->wait_poll_ms = ident ? now_ms() : 0;
    if (ident && (r = race_by_ident(ident)) != NULL && race_live(r) && s->wait_logged != r->ident) {
        s->wait_logged = r->ident;
        if (s->wait_begun != ident) {
            s->wait_begun = ident;
            st_wait(0);
        }
        logs_(s, "%s waits for game %d gid=%s (race under way, %d player(s))", s->name, r->ident, r->gid, r->nmember);
    }
}

static void race_push_coming(const race_t *r, const game_t *g);

/* every pass of the loop: races nobody will make again are over; over ones
 * leave when their waiters had a poll to read it */
static void race_tick(long long now)
{
    int i, k, j;
    for (i = 0; s_races && i < MAX_GAMES; i++) {
        race_t *r = &s_races[i];
        if (!r->used)
            continue;
        if (race_live(r) && race_succ(r)) {
            /* the host's lobby is told when a slow racer arrives or stops coming */
            game_t *sg = race_succ(r);
            int n = race_coming(r, sg);
            if (n != r->coming_sent) {
                r->coming_sent = n;
                race_push_coming(r, sg);
            }
        }
        if (race_live(r)) {
            if (now - r->started_ms > (long long)s_race_hold_secs * 1000)
                race_over_why(r, "time", "the race's time ran out");
            else if (!race_succ(r) && r->host_back_ms) {
                long long since = r->host_back_ms > r->successor_ms ? r->host_back_ms : r->successor_ms;
                if (now - since > s_rehost_grace_ms) {
                    char why[96];
                    snprintf(why, sizeof why, "its host did not make a game within %lld s", s_rehost_grace_ms / 1000);
                    race_over(r, why);
                }
            } else if (!race_succ(r)) {
                int back = 1;
                for (k = 0; k < r->nmember && back; k++) {
                    int found = 0;
                    if (!strcmp(r->member[k], r->host))
                        continue;
                    for (j = 0; j < MAX_SESS && !found; j++)
                        found = s_sess[j].used && s_sess[j].authed && !strcmp(s_sess[j].name, r->member[k]);
                    back = found;
                }
                if (!back)
                    r->all_back_ms = 0;
                else if (!r->all_back_ms)
                    r->all_back_ms = now ? now : 1;
                else if (now - r->all_back_ms > s_host_slow_ms)
                    race_over_why(r, "absent", "the others are back, its host is not");
            }
        }
        if (r->gone) {
            /* stay together: a race over before its racers were back (its host
             * quit on the results while they still sat in the title's box)
             * keeps its record for COMING_MS until each racer that did not
             * leave has heard it, so their first poll still hears why
             * (TKWAITWHY), not only "gone" */
            int w = race_waiters(r), k, pending = 0;
            long long keep;
            for (k = 0; k < r->nmember; k++)
                if (strcmp(r->member[k], r->host) && !(r->left & (1u << k)) && !(r->heard & (1u << k)))
                    pending = 1;
            keep = r->why[0] && pending ? s_coming_ms : 0;
            if (r->fwd ? !race_by_ident(r->fwd) || (!w && now - r->gone_ms > s_race_tail_ms)
                       : (!w && now - r->gone_ms > keep) || now - r->gone_ms > (keep > s_race_tail_ms ? keep : s_race_tail_ms))
                memset(r, 0, sizeof *r);
        }
    }
}

/* Games a stranger could join now: public, not full, not under way. */
static int waiting_public(void)
{
    int i, n = 0;
    for (i = 0; i < MAX_GAMES; i++)
        n += game_open(&s_games[i]) && !s_games[i].priv;
    return n;
}

/* A room's record. TKONLINE and TKWAITING (players signed in, public games
 * waiting) ride along: the title reads the keys it knows and skips these;
 * the overlay reads them (BO-336). */
static void room_rec(body_t *b, int id)
{
    b->n = 0;
    kv_i(b, "I", id); kv_s(b, "N", room_name(id)); kv_s(b, "H", "Takedawn");
    kv_s(b, "D", room_name(id)); kv_s(b, "F", "C"); kv_i(b, "T", in_room_count(id));
    kv_i(b, "L", 50);
    kv_i(b, "TKONLINE", online_now()); kv_i(b, "TKWAITING", waiting_public());
    kv_i(b, "TKRACING", races_for_strangers());
}

/* How much of a game a record shows:
 *   REC_ROOM    a search or the room's list: no player's address (a searcher
 *               is not in the game)
 *   REC_LIST    a member's +gam/+agm: addresses
 *   REC_MEMBER  what only the game's own players get (+mgm, the gcre, gjoi
 *               and gset replies, gsta, +ses): addresses and TKSECRET */
enum { REC_ROOM, REC_LIST, REC_MEMBER };
static void game_rec(body_t *b, const game_t *g, int mode, int viewer)
{
    const int full = mode != REC_ROOM;
    char k[24];
    int i;
    b->n = 0;
    kv_i(b, "IDENT", g->ident); kv_s(b, "NAME", g->name);
    kv_s(b, "HOST", g->np ? s_sess[g->players[0]].name : "");
    kv_s(b, "PARAMS", g->params); kv_i(b, "ROOM", g->room);
    kv_s(b, "CUSTFLAGS", g->custflags); kv_s(b, "SYSFLAGS", g->sysflags);
    kv_i(b, "COUNT", g->np); kv_s(b, "MINSIZE", g->minsize); kv_s(b, "MAXSIZE", g->maxsize);
    kv_i(b, "NUMPART", 1); kv_s(b, "PARTSIZE0", g->maxsize); kv_s(b, "PARTPARAMS0", "");
    kv_s(b, "WHEN", "2004.9.7-0:00:00"); kv_i(b, "SEED", (long)g->ident * 7919L);
    if (mode == REC_MEMBER && g->secret[0])
        kv_s(b, "TKSECRET", g->secret);
    if (mode == REC_MEMBER && g->secret[0] && s_cfg.relay_port)
        kv_i(b, "TKRELAY", s_cfg.relay_port);
    if (mode == REC_MEMBER && g->gid[0])
        kv_s(b, "TKGID", g->gid);
    if (mode == REC_MEMBER) {
        /* stay together: players of the race this game was made again after
         * who are not back yet (the host's lobby waits for them) */
        const race_t *rr = race_of_successor(g->ident);
        if (rr)
            kv_i(b, "TKCOMING", race_coming(rr, g));
    }
    for (i = 0; i < g->np; i++) {
        const sess_t *p = &s_sess[g->players[i]];
        snprintf(k, sizeof k, "OPID%d", i);    kv_i(b, k, p->uid);
        snprintf(k, sizeof k, "OPPO%d", i);    kv_s(b, k, p->name);
        snprintf(k, sizeof k, "ADDR%d", i);    kv_s(b, k, full ? p->peer : "0.0.0.0");
        {
            char sh[16];
            addr_str(shown_addr(g->players[i], viewer), sh, sizeof sh);
            snprintf(k, sizeof k, "LADDR%d", i);   kv_s(b, k, full ? sh : "0.0.0.0");
            if (full && viewer >= 0 && viewer != g->players[i] && s_sess[viewer].tkid && p->tkid && p->synth) {
                snprintf(k, sizeof k, "TKRADDR%d", i);
                kv_s(b, k, p->laddr);
            }
        }
        snprintf(k, sizeof k, "MADDR%d", i);   kv_s(b, k, "");
        snprintf(k, sizeof k, "OPPART%d", i);  kv_i(b, k, 0);
        snprintf(k, sizeof k, "OPPARAM%d", i); kv_s(b, k, p->userparams);
        snprintf(k, sizeof k, "OPFLAG%d", i);  kv_i(b, k, 0);
    }
}

static game_t *game_by_ident(int ident)
{
    int i;
    for (i = 0; i < MAX_GAMES; i++)
        if (s_games[i].used && s_games[i].ident == ident)
            return &s_games[i];
    return NULL;
}

/* ── UDP relay (BO-381): forwards signed datagrams between the members of a game ──
 * "TKRL" header (32 bytes) + payload; see .handoff/relay-0.3.1/PLAN.md. The
 * relay only ever sees a game's own members (a SipHash tag under the game's
 * secret, and synthetic addresses of two different members), never logs or
 * keeps an address beyond the binding table in memory, and has no traffic cap:
 * only counters. */
#define RL_HDR        32
#define RL_MAX_PAYLOAD 1400
#define RL_STALE_MS   60000LL
#define RL_PPS        400.0         /* packets per second a binding may send ... */
#define RL_BPS        65536.0       /* ... and bytes per second (burst: one second's worth) */
#define RL_ANSWER_MS  2000LL
#define RL_GAME_TTL   (30LL * 60 * 1000)   /* a game's relay record outlives the game by this much silence */
#define RL_ANSWER_LEN (RL_HDR + 6)

typedef struct {
    int state;                      /* 0 empty, 1 bound, 2 deleted */
    uint32_t ident, id;             /* the game, the member's synthetic address (network order) */
    uint16_t port;                  /* the member's title port */
    struct sockaddr_in src;         /* where its datagrams come from */
    long long seen_ms;
    double tok_p, tok_b;
    long long tok_ms;
    long long last_answer_ms;
    uint32_t last_answer_to;
} rl_bind_t;

/* What the relay remembers of a game: the lobby game leaves s_games a few seconds after the race
 * starts (the players drop their lobby links), but the race goes on over the relay. */
typedef struct {
    int used;
    uint32_t ident;
    uint8_t key[16];
    char gid[13];
    uint32_t members[MAX_PLAYERS];  /* synthetic addresses (network order) */
    int nm;
    long long last_ms;              /* the last valid packet (or the last sync with the live game) */
    int relayed;                    /* a packet was forwarded (logged and counted once) */
} rl_game_t;

static rl_bind_t *s_rl;
static size_t s_rl_n;
static rl_game_t *s_rlg;
static size_t s_rlg_n;
static long long s_rl_game_ttl_ms = RL_GAME_TTL;   /* a knob for the tests */
static sock_t s_rl_sock = BAD_SOCK;

/* SipHash-2-4 (Aumasson, Bernstein), the tag under the game's secret (same code as the toolkit's) */
#define ROTL(x, b) (uint64_t)(((x) << (b)) | ((x) >> (64 - (b))))
#define SIPROUND do { v0 += v1; v1 = ROTL(v1, 13); v1 ^= v0; v0 = ROTL(v0, 32); \
                      v2 += v3; v3 = ROTL(v3, 16); v3 ^= v2; \
                      v0 += v3; v3 = ROTL(v3, 21); v3 ^= v0; \
                      v2 += v1; v1 = ROTL(v1, 17); v1 ^= v2; v2 = ROTL(v2, 32); } while (0)
static uint64_t sip_le64(const uint8_t *p)
{
    uint64_t v = 0;
    int i;
    for (i = 7; i >= 0; i--)
        v = v << 8 | p[i];
    return v;
}
static uint64_t relay_siphash(const uint8_t key[16], const uint8_t *m, int n)
{
    uint64_t k0 = sip_le64(key), k1 = sip_le64(key + 8);
    uint64_t v0 = 0x736f6d6570736575ull ^ k0, v1 = 0x646f72616e646f6dull ^ k1;
    uint64_t v2 = 0x6c7967656e657261ull ^ k0, v3 = 0x7465646279746573ull ^ k1;
    uint64_t b = (uint64_t)n << 56, mi;
    int i, left = n & 7;
    for (i = 0; i + 8 <= n; i += 8) {
        mi = sip_le64(m + i);
        v3 ^= mi; SIPROUND; SIPROUND; v0 ^= mi;
    }
    for (i = 0; i < left; i++)
        b |= (uint64_t)m[n - left + i] << (8 * i);
    v3 ^= b; SIPROUND; SIPROUND; v0 ^= b;
    v2 ^= 0xff;
    SIPROUND; SIPROUND; SIPROUND; SIPROUND;
    return v0 ^ v1 ^ v2 ^ v3;
}
/* the tag of bytes 0..23, written little-endian into 24..31 (as the toolkit's hello_tag) */
static void relay_tag(const uint8_t key[16], uint8_t *hdr)
{
    uint64_t t = relay_siphash(key, hdr, 24);
    int i;
    for (i = 0; i < 8; i++)
        hdr[24 + i] = (uint8_t)(t >> (8 * i));
}

static void relay_init(void)
{
    free(s_rl);
    s_rl_n = (size_t)MAX_SESS * 2;
    s_rl = (rl_bind_t *)calloc(s_rl_n, sizeof *s_rl);
    if (!s_rl)
        s_rl_n = 0;
    free(s_rlg);
    s_rlg_n = (size_t)(MAX_GAMES > 0 ? MAX_GAMES : 1) * 8;
    s_rlg = (rl_game_t *)calloc(s_rlg_n, sizeof *s_rlg);
    if (!s_rlg)
        s_rlg_n = 0;
}

static size_t rl_hash(uint32_t ident, uint32_t id, uint16_t port)
{
    uint32_t h = ident * 2654435761u;
    h ^= (id + 0x9e3779b9u) * 2246822519u;
    h ^= (uint32_t)port * 3266489917u;
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    return (size_t)h;
}

/* The binding for the key, or (create) a free slot for it: an empty or deleted one, or a stale one. */
static rl_bind_t *rl_find(uint32_t ident, uint32_t id, uint16_t port, long long now, int create)
{
    size_t i, h;
    rl_bind_t *spare = NULL;
    if (!s_rl_n)
        return NULL;
    h = rl_hash(ident, id, port) % s_rl_n;
    for (i = 0; i < s_rl_n; i++) {
        rl_bind_t *e = &s_rl[(h + i) % s_rl_n];
        if (e->state == 0) {
            if (!spare) spare = e;
            break;
        }
        if (e->state == 1 && e->ident == ident && e->id == id && e->port == port)
            return e;
        if (!spare && (e->state == 2 || now - e->seen_ms > RL_STALE_MS))
            spare = e;
    }
    if (create && spare) {
        memset(spare, 0, sizeof *spare);
        spare->state = 1;
        spare->ident = ident; spare->id = id; spare->port = port;
        spare->tok_p = RL_PPS; spare->tok_b = RL_BPS;
        return spare;
    }
    return NULL;
}

/* The record of a game (open addressing on the ident); create: a free slot, or one whose game has been silent
 * for the TTL. Expired records are never returned. */
static rl_game_t *rlg_find(uint32_t ident, long long now, int create)
{
    size_t i, h;
    rl_game_t *spare = NULL;
    if (!s_rlg_n)
        return NULL;
    h = (size_t)((ident * 2654435761u) >> 7) % s_rlg_n;
    for (i = 0; i < s_rlg_n; i++) {
        rl_game_t *e = &s_rlg[(h + i) % s_rlg_n];
        if (e->used && e->ident == ident) {
            if (now - e->last_ms > s_rl_game_ttl_ms) {     /* expired: reusable, not valid */
                if (!spare) spare = e;
                continue;
            }
            return e;
        }
        if (!spare && (!e->used || now - e->last_ms > s_rl_game_ttl_ms))
            spare = e;
        if (!e->used)
            break;
    }
    if (create && spare) {
        memset(spare, 0, sizeof *spare);
        spare->used = 1;
        spare->ident = ident;
        spare->last_ms = now;
        return spare;
    }
    return NULL;
}

static int rlg_member(const rl_game_t *r, uint32_t id)
{
    int i;
    if (!id)
        return -1;
    for (i = 0; i < r->nm; i++)
        if (r->members[i] == id)
            return i;
    return -1;
}

/* Copy a live game's key, gid and members into its record (members are only added: sessions that
 * dropped their lobby link for the race stay members). */
static void rlg_sync(rl_game_t *r, const game_t *g)
{
    int i;
    memcpy(r->key, g->key, sizeof r->key);
    memcpy(r->gid, g->gid, sizeof r->gid);
    for (i = 0; i < g->np; i++) {
        const sess_t *p = &s_sess[g->players[i]];
        if (p->used && p->tkid && p->synth && rlg_member(r, p->synth) < 0 && r->nm < MAX_PLAYERS)
            r->members[r->nm++] = p->synth;
    }
}

static void relay_send(const void *b, int n, const struct sockaddr_in *to)
{
    if (s_rl_sock != BAD_SOCK)
        sendto(s_rl_sock, (const char *)b, (int)n, 0, (const struct sockaddr *)to, (socklen_t)sizeof *to);
}

/* One datagram from src. Anything that is not a valid packet of a live game's
 * member is dropped without a word (and counted); nothing is ever answered to it. */
static void relay_handle(const uint8_t *b, int n, const struct sockaddr_in *src, long long now)
{
    uint32_t ident, from, to;
    uint16_t fport, tport;
    game_t *g;
    rl_game_t *rg;
    rl_bind_t *sb, *db;
    uint8_t want[RL_HDR];
    int fi, ti;
    if (n < RL_HDR || n > RL_HDR + RL_MAX_PAYLOAD || memcmp(b, "TKRL", 4) || b[4] != 1 || b[5] != 1)
        goto drop;
    ident = (uint32_t)b[6] << 24 | (uint32_t)b[7] << 16 | (uint32_t)b[8] << 8 | b[9];
    memcpy(&from, b + 10, 4);
    memcpy(&to, b + 14, 4);
    fport = (uint16_t)(b[18] << 8 | b[19]);
    tport = (uint16_t)(b[20] << 8 | b[21]);
    if (b[22] || b[23])
        goto drop;
    g = game_by_ident((int)ident);
    if (g) {                                /* a live game: its record is refreshed from it */
        rg = rlg_find(ident, now, 1);
        if (!rg)
            goto drop;
        rlg_sync(rg, g);
    } else {                                /* the race goes on after the game has left the lobby */
        rg = rlg_find(ident, now, 0);
        if (!rg)
            goto drop;
    }
    memcpy(want, b, 24);
    relay_tag(rg->key, want);
    {   /* compare the whole tag, no early exit */
        int i, diff = 0;
        for (i = 24; i < 32; i++)
            diff |= want[i] ^ b[i];
        if (diff)
            goto drop;
    }
    fi = rlg_member(rg, from);
    if (fi < 0)
        goto drop;
    if (to) {
        ti = rlg_member(rg, to);
        if (ti < 0 || ti == fi)
            goto drop;
    }
    rg->last_ms = now;                      /* a valid packet: the game is alive */
    sb = rl_find(ident, from, fport, now, 1);
    if (!sb)
        goto drop;                          /* the table is full of live bindings */
    sb->src = *src;
    sb->seen_ms = now;
    if (!to)
        return;                             /* bind only: nothing forwarded, nothing answered */
    db = rl_find(ident, to, tport, now, 0);
    if (!db || now - db->seen_ms > RL_STALE_MS)
        goto drop;
    {   /* the sender's token bucket (per binding) */
        double dt = sb->tok_ms ? (double)(now - sb->tok_ms) / 1000.0 : 0;
        if (dt < 0) dt = 0;
        sb->tok_ms = now;
        sb->tok_p += dt * RL_PPS; if (sb->tok_p > RL_PPS) sb->tok_p = RL_PPS;
        sb->tok_b += dt * RL_BPS; if (sb->tok_b > RL_BPS) sb->tok_b = RL_BPS;
        if (sb->tok_p < 1.0 || sb->tok_b < (double)n)
            goto drop;
        sb->tok_p -= 1.0;
        sb->tok_b -= (double)n;
    }
    relay_send(b, n, &db->src);
    st_relay(0, (unsigned long)n);
    if (!rg->relayed) {
        rg->relayed = 1;
        st_relay(2, 0);
        logf_("game %d gid=%s relayed", (int)rg->ident, rg->gid);
    }
    /* the answer (kind 2): the destination's endpoint, to the sender, never bigger than the packet that caused it */
    if (n >= RL_ANSWER_LEN && (sb->last_answer_to != to || now - sb->last_answer_ms >= RL_ANSWER_MS)) {
        uint8_t a[RL_ANSWER_LEN];
        memcpy(a, "TKRL", 4);
        a[4] = 1; a[5] = 2;
        memcpy(a + 6, b + 6, 4);
        memcpy(a + 10, &to, 4);             /* from: the peer */
        memcpy(a + 14, &from, 4);           /* to: the asker */
        a[18] = (uint8_t)(tport >> 8); a[19] = (uint8_t)tport;
        a[20] = (uint8_t)(fport >> 8); a[21] = (uint8_t)fport;
        a[22] = a[23] = 0;
        relay_tag(rg->key, a);
        memcpy(a + 32, &db->src.sin_addr.s_addr, 4);
        memcpy(a + 36, &db->src.sin_port, 2);   /* already network order = big-endian */
        sb->last_answer_ms = now;
        sb->last_answer_to = to;
        relay_send(a, RL_ANSWER_LEN, &sb->src);
    }
    return;
drop:
    st_relay(1, 0);
}

/* up to 64 datagrams per wake */
static void relay_pump(void)
{
    static uint8_t buf[2048];
    int k;
    for (k = 0; k < 64; k++) {
        struct sockaddr_in from;
        socklen_t fl = (socklen_t)sizeof from;
        int n = (int)recvfrom(s_rl_sock, (char *)buf, sizeof buf, 0, (struct sockaddr *)&from, &fl);
        if (n < 0)
            break;                          /* would block, or an error: next wake */
        relay_handle(buf, n, &from, now_ms());
    }
}

static int game_has(const game_t *g, int si)
{
    int i;
    for (i = 0; i < g->np; i++)
        if (g->players[i] == si)
            return 1;
    return 0;
}

/* A session that gave this game's code is told of it, like the room is of a public game. */
static int sees_private(const sess_t *s, const game_t *g)
{
    return g->priv && s->code[0] && !strcmp(s->code, g->pass);
}

/* --crowd-test (BO-366): the load tool's players and everyone else live side
 * by side on one test lobby without seeing each other's games (no search,
 * room push or join finds one of the other kind), so a real copy's QUICK PLAY
 * never lands in a fake game and a fake guest never sits in a real lobby.
 * Off: everyone sees everything, as on the public server. */
static int crowd_sees(const sess_t *s, const game_t *g)
{
    return !s_cfg.crowd_test || s->crowd == g->crowd;
}

/* only: one session gets what it would get from a full push (its own
 * request that changed nothing still gets the answer it always had); -1:
 * everyone in the room and the game */
static void broadcast_game_to(const game_t *g, int only)
{
    body_t t, bp, w;
    int i;
    game_rec(&bp, g, REC_ROOM, -1);   /* the room's: no addresses */
    bp.b[bp.n] = 0;
    for (i = 0; i < MAX_SESS; i++)
        if ((only < 0 || i == only) && s_sess[i].used && s_sess[i].authed &&
            ((s_sess[i].room == g->room && (!g->priv || sees_private(&s_sess[i], g)) && crowd_sees(&s_sess[i], g)) || game_has(g, i))) {
            const char *rec = bp.b;
            if (game_has(g, i)) {          /* a member's own view: addresses, no secret */
                game_rec(&t, g, REC_LIST, i);
                t.b[t.n] = 0;
                rec = t.b;
            }
            send_pkt(i, "+gam", NULL, rec);
            send_pkt(i, "+agm", NULL, rec);
        }
    for (i = 0; i < g->np; i++) {
        int j, me = g->players[i];
        if (only >= 0 && me != only)
            continue;
        game_rec(&t, g, REC_MEMBER, me);   /* +mgm: the players only: addresses and the secret */
        t.b[t.n] = 0;
        send_pkt(me, "+mgm", NULL, t.b);
        who(&w, me, me);
        reply(me, "+who", &w);
        /* "+usr": the other players' records (the lobby's user list), which
         * the peer setup reads their XUIDs from (MA). */
        for (j = 0; j < g->np; j++)
            if (j != i) {
                who(&w, g->players[j], me);
                reply(me, "+usr", &w);
            }
    }
}

static void broadcast_game(const game_t *g) { broadcast_game_to(g, -1); }

/* the member record alone, to the players of g, when its TKCOMING changed */
static void race_push_coming(const race_t *r, const game_t *g)
{
    body_t t;
    int i;
    for (i = 0; i < g->np; i++) {
        game_rec(&t, g, REC_MEMBER, g->players[i]);
        t.b[t.n] = 0;
        send_pkt(g->players[i], "+mgm", NULL, t.b);
    }
    logf_("game %d gid=%s: %d player(s) of the race of game %d still on the way", g->ident, g->gid, r->coming_sent, r->ident);
}

/* A game a search can offer: not full, not under way. */
static int game_open(const game_t *g)
{
    return g->used && g->np < s_max_players && g->np < atoi(g->maxsize) && !game_racing(g);
}

static void game_gone(const game_t *g)
{
    body_t b;
    int i;
    b.n = 0;
    kv_i(&b, "IDENT", g->ident);   /* no NAME: off the lists */
    b.b[b.n] = 0;
    for (i = 0; i < MAX_SESS; i++)
        if (s_sess[i].used && s_sess[i].authed &&
            ((s_sess[i].room == g->room && (!g->priv || sees_private(&s_sess[i], g)) && crowd_sees(&s_sess[i], g)) || game_has(g, i))) {
            send_pkt(i, "+gam", NULL, b.b);
            send_pkt(i, "+agm", NULL, b.b);
        }
}

/* Everyone still in the game is told it is gone (an empty game record and
 * their own record with no game). TKWHY=closed (BO-352): the host left or
 * deleted the game, so it was not a kick of that player. */
static void close_members(game_t *g)
{
    int i;
    for (i = 0; i < g->np; i++) {
        int k = g->players[i];
        body_t w;
        s_sess[k].game = 0;
        send_pkt(k, "+mgm", NULL, "IDENT=0\nNAME=\nHOST=\nCOUNT=0\nTKWHY=closed");
        who(&w, k, k);
        reply(k, "+who", &w);
    }
}

static void leave_game(int si, int tell)
{
    sess_t *s = &s_sess[si];
    game_t *g = game_by_ident(s->game);
    int i, j, was_host, alone;
    s->game = 0;
    if (!g)
        return;
    was_host = g->np && g->players[0] == si;
    for (i = j = 0; i < g->np; i++)
        if (g->players[i] != si)
            g->players[j++] = g->players[i];
    g->np = j;
    alone = was_host && !j;
    if (tell) {
        body_t w;
        who(&w, si, si);
        reply(si, "+who", &w);
    }
    if (g->np && was_host && game_racing(g)) {
        /* The host of a started game left. Every player drops its lobby link
         * at the start, so the others normally go within seconds and the
         * game with them; nothing is sent now (a member list during the race
         * start cuts peer links, see below). A guest still here after
         * s_host_gone_secs missed the start or hangs: host_gone_tick closes
         * the game for it. BO-358's handover goes in host_gone_tick. */
        if (!g->host_gone_ms)
            g->host_gone_ms = now_ms() ? now_ms() : 1;
    }
    if (g->np && was_host && !game_racing(g)) {
        /* The host left a game that has not started: the game closes, and
         * everyone still in it gets an empty game record, which takes them
         * back out of the game's lobby. */
        close_members(g);
        logf_("game %d gid=%s closed: its host left", g->ident, g->gid);
        g->np = 0;
    }
    if (!g->np) {
        /* A host alone in a game that never started (left it, or made
         * another): one line, so the log tells "left, made another" from a
         * game made twice (BO-362). A started game ends with the race start's
         * disconnects: no line. */
        if (alone && !game_racing(g))
            logs_(s, "%s left game %d gid=%s (no one else in it: it closes)", s->name, g->ident, g->gid);
        game_gone(g);
        g->used = 0;
        race_game_closed(g->ident);
    } else if (!game_racing(g)) {
        broadcast_game(g);
    }
    /* A started game (bit 19) is not re-sent: every player drops its lobby
     * link at the race start, and a member list without the one who dropped
     * first makes the others tear down their peer link to it (0xFD6B0), so
     * the race reports "lost connection". */
}

/* A started game whose host left (leave_game) and that still has a member
 * connected after s_host_gone_secs: it closes for them as an unstarted one
 * does (close_members, TKWHY=closed) and leaves the list. This is the one
 * place that decides what a game without its host becomes; the host
 * handover (BO-358) would hand it to players[0] and re-send the record here
 * instead of closing it. */
static void host_gone_tick(long long now)
{
    int i;
    for (i = 0; i < MAX_GAMES; i++) {
        game_t *g = &s_games[i];
        if (!g->used || !g->host_gone_ms || now - g->host_gone_ms < (long long)s_host_gone_secs * 1000)
            continue;
        logf_("game %d gid=%s closed: its host left after the start and %d player(s) stayed", g->ident, g->gid, g->np);
        close_members(g);
        g->np = 0;
        game_gone(g);
        g->used = 0;
        race_game_closed(g->ident);
    }
}

/* ── requests ──────────────────────────────────────────────────────── */

/* Room lists: a "move" changes a room's head count; the other players hear
 * of it at most once a second per room, not once per request. */
static void drop(int si);
static void pen_set(uint32_t ip, time_t now, int secs);
static int s_room_dirty[8];
static long long s_rooms_sent_ms;
static long s_throttled_total;        /* requests dropped by the rate limit (tests read it) */

static int room_index(int id)
{
    int i;
    for (i = 0; i < N_ROOMS; i++)
        if (k_rooms[i].id == id)
            return i;
    return -1;
}

static void rooms_flush(int force)
{
    long long t = now_ms();
    int r, i;
    if (!force && t - s_rooms_sent_ms < 1000)
        return;
    for (r = 0; r < N_ROOMS; r++)
        if (s_room_dirty[r]) {
            body_t b;
            s_room_dirty[r] = 0;
            room_rec(&b, k_rooms[r].id);
            b.b[b.n] = 0;
            for (i = 0; i < MAX_SESS; i++)
                if (s_sess[i].used && s_sess[i].authed)
                    send_pkt(i, "+rom", NULL, b.b);
            s_rooms_sent_ms = t;
        }
}

static int next_ident(void)
{
    int tries;
    for (tries = 0; tries < 100000; tries++) {
        int id = s_next_game++;
        if (s_next_game > 999999)
            s_next_game = 1;
        if (!game_by_ident(id))
            return id;
    }
    return 0;
}

static void bad_request(int si, const char *cmd, const char *what)
{
    logs_(&s_sess[si], "%s sent a bad %s in %.4s: dropped", who_log(&s_sess[si]), what, cmd);
    s_sess[si].why = "bad request";
    drop(si);
}

/* The TKCODE of a request, upper-cased ("" when there is none). Codes are
 * not case sensitive: friends type them on different keyboards. */
static void get_code(const char *body, char *out, size_t n)
{
    size_t i;
    field(body, "TKCODE", out, n);
    for (i = 0; out[i]; i++)
        if (out[i] >= 'a' && out[i] <= 'z')
            out[i] = (char)(out[i] - 32);
}

/* The code a request carries is remembered for the session (the key present
 * and empty clears it): private games with that code are pushed to it. */
static void note_code(sess_t *s, const char *body)
{
    char c[24];
    size_t i;
    if (!field(body, "TKCODE", c, sizeof c))
        return;
    for (i = 0; c[i]; i++)
        if (c[i] >= 'a' && c[i] <= 'z')
            c[i] = (char)(c[i] - 32);
    snprintf(s->code, sizeof s->code, "%s", c);
}

/* TKID=1: the player takes part in the synthetic address scheme (once, and
 * not while it is in a game). */
static void note_tkid(int si, const char *body)
{
    sess_t *s = &s_sess[si];
    char v[40], nm[40], leased[32];
    if (s->game || !field(body, "TKID", v, sizeof v))
        return;
    if (atoi(v) == 1 && !s->tkid) {
        int idx, reused = 0;
        if (!field(body, "TKNAME", nm, sizeof nm))
            nm[0] = 0;
        if (!nm[0])
            snprintf(nm, sizeof nm, "%s", s->name);        /* at auth the gamertag is known */
        idx = lease_take(s, nm, leased, sizeof leased);     /* the same player coming back: its old address */
        if (idx)
            reused = 1;
        else {
            idx = synth_alloc();
            if (!idx && lease_evict_oldest())               /* the pool is full: the oldest lease goes */
                idx = synth_alloc();
        }
        if (idx) {
            s->tkid = 1;
            s->synth_idx = idx;
            s->synth = synth_net(idx);
            if (reused)
                logs_(s, "address reused for %s", leased);
        }
    }
}

static int xuid_eq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++)
        if ((*a | 32) != (*b | 32))
            return 0;
    return !*a && !*b;
}

/* A legacy player may not join a game that has a member it cannot be told apart
 * from: the same real address, the same XUID, or a member's synthetic address. */
static int identity_clash(const game_t *g, int si)
{
    const sess_t *j = &s_sess[si];
    uint32_t jl = ip_of(j->laddr);
    int i;
    for (i = 0; i < g->np; i++) {
        const sess_t *m = &s_sess[g->players[i]];
        if (g->players[i] == si)
            continue;
        /* the title tells players apart by name (the kick's KICK=, the
         * record's HOST): two of one name in a game cannot be told apart */
        if (j->name[0] && !strcmp(j->name, m->name))
            return 1;
        if (j->tkid && m->tkid)
            continue;                  /* both have unique synthetic addresses */
        /* a legacy player is involved: the real addresses must differ, and a
         * legacy player may not hold a member's synthetic address or XUID */
        if (jl == ip_of(m->laddr) || (!j->tkid && m->tkid && jl == m->synth) || (j->tkid && !m->tkid && j->synth == ip_of(m->laddr)))
            return 1;
        if (!j->tkid && !m->tkid && j->xuid[0] && xuid_eq(j->xuid, m->xuid))
            return 1;
    }
    return 0;
}

/* The game was made with this code (private, or public with TKPUBLIC=1). */
static int has_code(const game_t *g, const char *code)
{
    return code[0] && g->code[0] && !strcmp(g->code, code);
}

/* Is there a game with this code, in whatever state (full, under way)? */
static int code_exists(const char *code)
{
    int i;
    for (i = 0; i < MAX_GAMES; i++)
        if (s_games[i].used && s_games[i].np > 0 && has_code(&s_games[i], code))
            return 1;
    for (i = 0; s_races && i < MAX_GAMES; i++)      /* a game whose race is under way (BO-365) */
        if (race_live(&s_races[i]) && s_races[i].code[0] && !strcmp(s_races[i].code, code))
            return 1;
    return 0;
}

/* Looking a code up costs: at most one lookup a CODE_SEARCH_MS per session
 * (a search, or a move that gave a code), apart from the strikes below. */
static int code_search_ok(sess_t *s)
{
    long long t = now_ms();
    if (t - s->last_code_ms < s_code_search_ms)
        return 0;
    s->last_code_ms = t;
    return 1;
}

/* gset with KICK and nothing else that changes the game */
static int kick_only(const char *body)
{
    char v[256];
    static const char *const other[] = { "USERPARAMS", "PARAMS", "SYSFLAGS", "CUSTFLAGS", "MINSIZE", "MAXSIZE" };
    size_t i;
    if (!field(body, "KICK", v, sizeof v) || !v[0])
        return 0;
    for (i = 0; i < sizeof other / sizeof other[0]; i++)
        if (field(body, other[i], v, sizeof v))
            return 0;
    return 1;
}

/* The index in its game of the player that session si's host would kick with
 * KICK=name, or 0 when that kicks nobody (not the host, not a member, the host itself). */
static int kick_target(int si, const char *name)
{
    const game_t *g = game_by_ident(s_sess[si].game);
    int k;
    if (!g || !g->np || g->players[0] != si || game_racing(g))
        return 0;
    for (k = 1; k < g->np; k++)
        if (!strcmp(s_sess[g->players[k]].name, name))
            return k;
    return 0;
}

/* Requests that change nothing and that the title repeats at frame rate: a
 * private message ("mesg"), a kick of a player that is no member (any more).
 * They are answered at once and do not use the flood bucket, but have a
 * bucket of their own. */
static int request_is_noop(int si, const char *cmd, const char *body)
{
    char v[256];
    if (!memcmp(cmd, "mesg", 4))
        return 1;
    if (!memcmp(cmd, "gset", 4) && kick_only(body) && field(body, "KICK", v, sizeof v) && v[0] && v_name(v, NAME_MAX_CHARS))
        return kick_target(si, v) == 0;
    return 0;
}

static int take_noop_token(sess_t *s)
{
    long long t = now_ms();
    if (!s->ntok_ms) {
        s->ntok = NOOP_RATE;
        s->ntok_ms = t;
    }
    s->ntok += (double)(t - s->ntok_ms) * NOOP_RATE / 1000.0;
    if (s->ntok > NOOP_RATE)
        s->ntok = NOOP_RATE;
    s->ntok_ms = t;
    if (s->ntok >= 1.0) {
        s->ntok -= 1.0;
        return 1;
    }
    return 0;
}

/* A wrong code was tried (a search that found nothing, a join with the
 * wrong one). More than CODE_MISSES in a minute: the session ends and its
 * address is not served for a minute. 1 when the session is gone. */
static int code_miss(int si)
{
    sess_t *s = &s_sess[si];
    time_t now = mono_s();
    if (now - s->wrong_t0 >= 60) {
        s->wrong_t0 = now;
        s->wrong_n = 0;
    }
    if (++s->wrong_n > CODE_MISSES) {
        logs_(s, "%s: too many wrong game codes, dropped", who_log(s));
        pen_set(s->peer_net, now, 60);
        s->why = "too many wrong game codes";
        drop(si);
        return 1;
    }
    return 0;
}

/* The host kicked this session's player from g (and it is not back in it) */
static int kicked_from(const game_t *g, int si)
{
    int k;
    if (si < 0 || game_has(g, si))
        return 0;
    for (k = 0; k < 16 && k < g->nkicked; k++)
        if (g->kicked[k] == s_sess[si].uid)
            return 1;
    return 0;
}

/* The indices of the open games a search may list, best first: the public
 * ones for a search without a code (a public game with a code too), only the
 * games with that code for one with. A game that kicked the searcher (si) is
 * left out: its join would only be refused (BO-364). *kicked (if not NULL)
 * counts the games left out for that alone. */
static int search_games(const char *code, int *idx, int si, int *kicked)
{
    int i, n = 0;
    if (kicked)
        *kicked = 0;
    for (i = 0; i < MAX_GAMES; i++) {
        const game_t *g = &s_games[i];
        if (!game_open(g))
            continue;
        if (code[0] ? !has_code(g, code) : g->priv)
            continue;
        if (si >= 0 && !crowd_sees(&s_sess[si], g))
            continue;
        if (kicked_from(g, si)) {
            if (kicked)
                ++*kicked;
            continue;
        }
        if (!race_seat_for(g, si))
            continue;
        idx[n++] = i;
    }
    s_cmp_games = s_games;
    qsort(idx, (size_t)n, sizeof *idx, cmp_games);
    return n;
}

/* ── BROWSE GAMES (BO-372) ──────────────────────────────────────────
 * A browse search (gsea TKBROWSE=1, sent only by BROWSE GAMES of a newer
 * client, never with a code or a wait) lists every public game the player
 * could see, not only the open ones: open games first (as a plain search
 * lists them), then the races under way (most players first, then the one
 * started longest ago), then the full games. Each +gam record says its state
 * in TKSTATE (open, racing, full) and a race how long it has run in TKSECS;
 * the client shows them and joins only an open one itself (a racing one is
 * waited for with TKWAITFOR, a full one is not joined). A race is listed as
 * the game it was (its record from gsta: name, PARAMS, the racers) until the
 * host makes it again; then that new game is listed instead. Private games,
 * games that kicked the player, the player's own game and the other kind's
 * games (--crowd-test) are left out, as from every search. Old clients and
 * QUICK PLAY never send the key and get the open games only. At most BR_MAX
 * games are listed, in that order. */
#define BR_MAX 9                /* the game's list holds nine (its page's rows): no more are sent */
typedef struct { const game_t *g; const race_t *r; const char *state; } br_ent;

static int cmp_races(const void *a, const void *b)
{
    const race_t *x = ((const br_ent *)a)->r, *y = ((const br_ent *)b)->r;
    if (x->nmember != y->nmember)
        return y->nmember - x->nmember;         /* most players first */
    if (x->started_ms != y->started_ms)
        return x->started_ms < y->started_ms ? -1 : 1;
    return x->ident - y->ident;
}

/* a race as a game record (REC_ROOM's fields: no address, no secret) */
static void race_rec(body_t *b, const race_t *r)
{
    char k[24];
    int i;
    b->n = 0;
    kv_i(b, "IDENT", r->ident); kv_s(b, "NAME", r->name); kv_s(b, "HOST", r->host);
    kv_s(b, "PARAMS", r->params); kv_i(b, "ROOM", r->room);
    kv_s(b, "CUSTFLAGS", r->custflags); kv_s(b, "SYSFLAGS", "0");
    kv_i(b, "COUNT", r->nmember); kv_s(b, "MINSIZE", "2"); kv_i(b, "MAXSIZE", race_cap(r));
    kv_i(b, "NUMPART", 1); kv_i(b, "PARTSIZE0", race_cap(r)); kv_s(b, "PARTPARAMS0", "");
    kv_s(b, "WHEN", "2004.9.7-0:00:00"); kv_i(b, "SEED", (long)r->ident * 7919L);
    for (i = 0; i < r->nmember && i < MAX_PLAYERS; i++) {
        snprintf(k, sizeof k, "OPID%d", i);    kv_i(b, k, 0);
        snprintf(k, sizeof k, "OPPO%d", i);    kv_s(b, k, r->member[i]);
        snprintf(k, sizeof k, "ADDR%d", i);    kv_s(b, k, "0.0.0.0");
        snprintf(k, sizeof k, "LADDR%d", i);   kv_s(b, k, "0.0.0.0");
        snprintf(k, sizeof k, "MADDR%d", i);   kv_s(b, k, "");
        snprintf(k, sizeof k, "OPPART%d", i);  kv_i(b, k, 0);
        snprintf(k, sizeof k, "OPPARAM%d", i); kv_s(b, k, "");
        snprintf(k, sizeof k, "OPFLAG%d", i);  kv_i(b, k, 0);
    }
}

/* the browse list for si, in its order; its length (at most max) */
static int browse_list(int si, br_ent *out, int max)
{
    int *idx = (int *)malloc((size_t)MAX_GAMES * sizeof *idx);
    br_ent *rc;
    int n = 0, nopen, nr = 0, i, k;
    if (!idx)
        return 0;
    nopen = search_games("", idx, si, NULL);
    for (k = 0; k < nopen && n < max; k++) {
        out[n].g = &s_games[idx[k]];
        out[n].r = NULL;
        out[n++].state = "open";
    }
    free(idx);
    /* the races under way, after the open games (sorted on their own) */
    rc = out + n;
    for (i = 0; s_races && i < MAX_GAMES && n + nr < max; i++) {
        const race_t *r = &s_races[i];
        if (!race_live(r) || r->priv || !race_kind(r, si) || race_kicked(r, si) || race_succ(r))
            continue;
        rc[nr].g = NULL;
        rc[nr].r = r;
        rc[nr++].state = r->nmember < race_cap(r) ? "racing" : "full";
    }
    qsort(rc, (size_t)nr, sizeof *rc, cmp_races);
    /* a full race goes with the full games: stable, keeping the order above */
    for (i = k = 0; i < nr; i++)
        if (!strcmp(rc[i].state, "racing")) {
            br_ent t = rc[i];
            memmove(rc + k + 1, rc + k, (size_t)(i - k) * sizeof *rc);
            rc[k++] = t;
        }
    n += nr;
    /* the full games (and the ones that cannot take this player: their last
     * seats are a race's racers'), fullest first, then oldest; a game
     * starting now (its race record is there) is that race */
    {
        int *fi = (int *)malloc((size_t)MAX_GAMES * sizeof *fi), nf = 0, j;
        const br_ent *racing_end;
        if (fi) {
            for (i = 0; i < MAX_GAMES; i++) {
                const game_t *g = &s_games[i];
                race_t *gr;
                if (!g->used || !g->np || g->priv || !crowd_sees(&s_sess[si], g) || kicked_from(g, si) || game_has(g, si))
                    continue;
                if (game_open(g) && race_seat_for(g, si))
                    continue;                   /* listed as open */
                if (game_racing(g) && (gr = race_by_ident(g->ident)) != NULL && race_live(gr))
                    continue;                   /* listed as its race */
                fi[nf++] = i;
            }
            s_cmp_games = s_games;
            qsort(fi, (size_t)nf, sizeof *fi, cmp_games);
            /* the full races are at the end of rc: the full games go before them */
            racing_end = rc;
            while (racing_end < rc + nr && !strcmp(racing_end->state, "racing"))
                racing_end++;
            j = (int)(racing_end - out);
            for (k = 0; k < nf && n < max; k++) {
                memmove(out + j + 1, out + j, (size_t)(n - j) * sizeof *out);
                out[j].g = &s_games[fi[k]];
                out[j].r = NULL;
                out[j++].state = "full";
                n++;
            }
            free(fi);
        }
    }
    return n;
}

static void browse_reply(int si, int skip, int want)
{
    br_ent *e = (br_ent *)malloc((size_t)BR_MAX * sizeof *e);
    body_t b;
    int n = e ? browse_list(si, e, BR_MAX) : 0, k, sent = 0, no = 0, nr = 0, nf = 0;
    long long now = now_ms();
    for (k = 0; k < n; k++) {
        no += !strcmp(e[k].state, "open");
        nr += !strcmp(e[k].state, "racing");
    }
    nf = n - no - nr;
    b.n = 0;
    for (k = skip; k < n && sent < want; k++)
        sent++;
    kv_i(&b, "COUNT", sent);
    kv_i(&b, "TKBROWSE", 1);
    reply(si, "gsea", &b);
    for (k = skip, sent = 0; k < n && sent < want; k++, sent++) {
        if (e[k].g)
            game_rec(&b, e[k].g, REC_ROOM, -1);
        else
            race_rec(&b, e[k].r);
        kv_s(&b, "TKSTATE", e[k].state);
        if (e[k].r)
            kv_i(&b, "TKSECS", (long)((now - e[k].r->started_ms) / 1000));
        reply(si, "+gam", &b);
    }
    if (s_cfg.verbose)
        logs_(&s_sess[si], "%s browses: %d open, %d racing, %d full", s_sess[si].name, no, nr, nf);
    free(e);
}

static void handle(int si, const char *cmd, const char *body)
{
    sess_t *s = &s_sess[si];
    body_t b;
    char v[256];
    const char *bad;
    int i;
    b.n = 0;
    if (s_cfg.verbose)
        logs_(s, "<- %s %.4s %s", s->name[0] ? s->name : s->peer, cmd, oneline(body));

    /* Before sign-in only the sign-in requests are answered. */
    if (!s->authed && memcmp(cmd, "@dir", 4) && memcmp(cmd, "addr", 4) && memcmp(cmd, "news", 4) &&
        memcmp(cmd, "sele", 4) && memcmp(cmd, "auth", 4) && memcmp(cmd, "skey", 4) &&
        memcmp(cmd, "@tic", 4) && memcmp(cmd, "sviw", 4)) {
        bad_request(si, cmd, "request before sign-in");
        return;
    }
    if ((bad = validate(cmd, body)) != NULL) {
        bad_request(si, cmd, bad);
        return;
    }
    /* A probe (the online info line, Part 4): it asks @dir and closes, never
     * signing in. TKPROBE=1 keeps this connection out of the session log. */
    if (field(body, "TKPROBE", v, sizeof v) && v[0] == '1' && !v[1])
        s->probe = 1;
    if (!memcmp(cmd, "sele", 4) || !memcmp(cmd, "move", 4) || !memcmp(cmd, "gsea", 4) ||
        !memcmp(cmd, "gjoi", 4) || !memcmp(cmd, "gcre", 4))
        note_code(s, body);

    if (!memcmp(cmd, "@dir", 4)) {
        /* The directory sends the client on to the lobby: here, ourselves.
         * TKONLINE and TKWAITING (players signed in, public games waiting)
         * ride along for the online info line (the title skips unknown keys). */
        kv_s(&b, "ADDR", s_public); kv_i(&b, "PORT", s_cfg.port);
        kv_i(&b, "SESS", 1); kv_s(&b, "MASK", "ffffffffffffffffffffffffffffffff");
        kv_i(&b, "TKONLINE", online_now()); kv_i(&b, "TKWAITING", waiting_public());
        reply(si, "@dir", &b);
    } else if (!memcmp(cmd, "addr", 4)) {
        uint32_t a;
        /* The player's own address (a private one behind a router): taken as
         * said when it is believable, else the connection's address stays.
         * Not while the player is in a game: its identity is frozen then. */
        if (!s->game && field(body, "ADDR", v, sizeof v) && v[0] && parse_ip(v, &a) && addr_believable(s->peer_net, a))
            addr_str(a, s->laddr, sizeof s->laddr);
        note_tkid(si, body);
        /* a load-tool player (BO-366); said back so the tool knows the server
         * keeps the two kinds apart. Only before sign-in: a session does not
         * change kind */
        if (s_cfg.crowd_test && !s->authed && field(body, "TKCROWD", v, sizeof v) && !strcmp(v, "1"))
            s->crowd = 1;
        if (s->crowd)
            kv_i(&b, "TKCROWD", 1);
        if (s->tkid && s->synth) {
            char sy[16];
            addr_str(s->synth, sy, sizeof sy);
            kv_s(&b, "TKADDR", sy);
        }
        if (s->sid[0])
            kv_s(&b, "TKSID", s->sid);
        reply(si, "addr", &b);
    } else if (!memcmp(cmd, "news", 4)) {
        char code[4] = { 'n', 'e', 'w', '7' };
        if (field(body, "NAME", v, sizeof v) && v[0] >= '0' && v[0] <= '9')
            code[3] = v[0];
        /* no BUDDY_SERVER / BUDDY_PORT: nothing serves them, and with them the
         * title's buddy client opens a new socket on every frame for ever */
        b.b[b.n] = 0;
        send_pkt(si, "news", code, b.b);
    } else if (!memcmp(cmd, "sele", 4)) {
        kv_i(&b, "MORE", 0); kv_i(&b, "SLOTS", 32); kv_i(&b, "STATS", 0);
        reply(si, "sele", &b);
        for (i = 0; i < N_ROOMS; i++) {
            room_rec(&b, k_rooms[i].id);
            reply(si, "+rom", &b);
        }
    } else if (!memcmp(cmd, "auth", 4)) {
        char name[32];
        if (s->authed) {                /* signed in once per session */
            if (++s->nname > 4) { bad_request(si, cmd, "repeated sign-in"); return; }
        }
        if (!field(body, "GTAG", name, sizeof name) || !name[0])
            if (!field(body, "NAME", name, sizeof name) || !name[0])
                snprintf(name, sizeof name, "Player%u", s->uid);
        if (!s->game)                   /* identity is frozen in a game */
            snprintf(s->name, sizeof s->name, "%s", name);
        note_tkid(si, body);
        if (!s->game && field(body, "XUID", v, sizeof v) && v[0]) {
            /* The title's XUID is 8 bytes u[0..7] (2 of a name hash, the port
             * offset little-endian, the player's address in u[4..7], the last
             * one xor the user index 0..3), written as a 64-bit number: u[7]
             * first. The address is kept when it is believable, else it is
             * replaced by the one the server has; nothing else is touched. */
            unsigned char u[8], lab[4] = { 0, 0, 0, 0 };
            uint32_t ia, la = 0;
            int k, upper = 0;
            for (k = 0; k < 8; k++) {
                char t[3];
                t[0] = v[1 + 2 * k]; t[1] = v[2 + 2 * k]; t[2] = 0;
                u[7 - k] = (unsigned char)strtoul(t, NULL, 16);
            }
            for (k = 1; k < 17; k++)
                if (v[k] >= 'A' && v[k] <= 'F')
                    upper = 1;
            parse_ip(s->laddr, &la);
            memcpy(lab, &la, 4);
            memcpy(&ia, u + 4, 4);
            if (!(u[4] == lab[0] && u[5] == lab[1] && u[6] == lab[2] && (u[7] ^ lab[3]) < 4) &&
                !addr_believable(s->peer_net, ia)) {
                memcpy(u + 4, lab, 4);
                logs_(s, "%s: the XUID named another address; the connection's own is used", s->name);
            }
            snprintf(s->xuid, sizeof s->xuid, upper ? "$%02X%02X%02X%02X%02X%02X%02X%02X"
                                                    : "$%02x%02x%02x%02x%02x%02x%02x%02x",
                     u[7], u[6], u[5], u[4], u[3], u[2], u[1], u[0]);
        }
        kv_s(&b, "NAME", s->name); kv_s(&b, "PERSONAS", s->name); kv_s(&b, "MAIL", "");
        kv_s(&b, "BORN", "19700101"); kv_s(&b, "GEND", "M"); kv_s(&b, "FROM", "US");
        kv_s(&b, "LANG", "en"); kv_s(&b, "SPAM", "NN"); kv_s(&b, "SINCE", "2004.9.7-0:00:00");
        kv_s(&b, "LAST", "2004.9.7-0:00:00"); kv_i(&b, "TOS", 1);
        reply(si, "auth", &b);
        if (s_cfg.verbose)
            logs_(s, "%s logged in from %s", s->name, s->peer);
        else
            logs_(s, "%s logged in", s->name);
        if (!s->authed) {
            s->authed = 1;
            s_players++;
            racer_take(s->name);
            st_signin_cc(s->name, geo_lookup(s->peer_net));   /* once per session; the country is not kept */
        }
        s->auth_ms = now_ms() ? now_ms() : 1;
        race_host_back(si);
        /* stay together: the player quit on the results (the client says so
         * on its sign-in after the race): the group does not wait for it */
        if (field(body, "TKSTAYQUIT", v, sizeof v) && v[0] == '1')
            race_quit(si);
    } else if (!memcmp(cmd, "pers", 4)) {
        if (!s->game && field(body, "PERS", v, sizeof v) && v[0] && strcmp(v, s->name)) {
            if (++s->nname > 4) { bad_request(si, cmd, "repeated name change"); return; }
            snprintf(s->name, sizeof s->name, "%s", v);
            racer_take(s->name);
            st_player_cc(s->name, geo_lookup(s->peer_net));
        }
        kv_s(&b, "PERS", s->name); kv_s(&b, "LKEY", "0123456789abcdef0123456789abcdef");
        kv_s(&b, "LAST", "2004.9.7-0:00:00"); kv_s(&b, "PLAST", "2004.9.7-0:00:00");
        kv_s(&b, "LOC", "enUS"); kv_s(&b, "A", s->peer); kv_s(&b, "LA", s->laddr);
        kv_i(&b, "IDLE", 60000);
        reply(si, "pers", &b);
        who(&b, si, si);
        reply(si, "+who", &b);
    } else if (!memcmp(cmd, "move", 4)) {
        int old = s->room, ri, want;
        field(body, "IDENT", v, sizeof v);
        want = atoi(v);
        if (want != 0 && !room_name(want)[0]) {       /* a room that does not exist */
            bad_request(si, cmd, "room");
            return;
        }
        if (s->game && want != s->room) {             /* not out of the game's room while in a game */
            bad_request(si, cmd, "room change in a game");
            return;
        }
        s->room = want;
        kv_i(&b, "IDENT", s->room); kv_s(&b, "NAME", room_name(s->room));
        kv_i(&b, "COUNT", in_room_count(s->room));
        reply(si, "move", &b);
        who(&b, si, si);
        reply(si, "+who", &b);
        /* The mover gets both room records at once; everyone else gets them
         * in the next batch (rooms_flush). */
        if (old) { room_rec(&b, old); reply(si, "+rom", &b); }
        if (s->room) { room_rec(&b, s->room); reply(si, "+rom", &b); }
        if ((ri = room_index(old)) >= 0) s_room_dirty[ri] = 1;
        if ((ri = room_index(s->room)) >= 0) s_room_dirty[ri] = 1;
        /* Entering a room: the open public games already there are pushed as
         * +gam/+agm, fullest then oldest, at most GAME_LIST_REPLY (the JOIN
         * GAME screen shows the room's list and the pushes; it does not
         * search again, so a game made before the player came would never
         * show). Games with a password or the host's start lock are left
         * out, and no address or secret is in the records. A move into the
         * room it is already in, or again within 5 s, pushes nothing. */
        if (s->room && s->room != old && mono_s() - s->last_roompush >= 5) {
            int *idx = (int *)malloc((size_t)MAX_GAMES * sizeof *idx), tot, k, sent = 0;
            s->last_roompush = mono_s();
            tot = idx ? search_games("", idx, si, NULL) : 0;
            for (k = 0; k < tot && sent < GAME_LIST_REPLY; k++) {
                const game_t *g = &s_games[idx[k]];
                if (g->room != s->room || g->pass[0] || (strtoul(g->sysflags, NULL, 10) & 0x1000ul) || game_has(g, si))
                    continue;
                game_rec(&b, g, REC_ROOM, -1);
                b.b[b.n] = 0;
                send_pkt(si, "+gam", NULL, b.b);
                send_pkt(si, "+agm", NULL, b.b);
                sent++;
            }
            free(idx);
        }
        /* A player who gave a game code and enters the room hears of the open
         * private games with that code that are already there. */
        if (s->code[0] && s->room && code_search_ok(s)) {
            int sent = 0;
            for (i = 0; i < MAX_GAMES && sent < GAME_LIST_REPLY && s->outlen - s->outoff < OUT_CAP / 2; i++)
                if (game_open(&s_games[i]) && s_games[i].room == s->room && sees_private(s, &s_games[i]) &&
                    crowd_sees(s, &s_games[i]) && !game_has(&s_games[i], si) && !kicked_from(&s_games[i], si)) {
                    game_rec(&b, &s_games[i], REC_ROOM, -1);
                    b.b[b.n] = 0;
                    send_pkt(si, "+gam", NULL, b.b);
                    send_pkt(si, "+agm", NULL, b.b);
                    sent++;
                }
            if (!sent && !code_exists(s->code) && code_miss(si))
                return;
        }
    } else if (!memcmp(cmd, "gsea", 4)) {
        int n = 0, skip = 0, want = GAME_LIST_REPLY, k, total, kicked = 0, waitfor = 0;
        int *idx;
        char code[24];
        race_t *wr = NULL;
        const char *wstate = NULL;
        int wrank = 0;
        /* Full and started games are left out: Quick Match would pick
         * one and the game waits on "retrieving the list" for ever.
         * Private games (made with a TKCODE) are listed only for a search
         * that carries their code, and then only the games with that code
         * are (a public one made with TKPUBLIC=1 too). The rest come
         * fullest first, then oldest, so strangers end up in one game.
         * START and COUNT (the game sends 0 and 500) page the list, and a
         * reply holds at most GAME_LIST_REPLY games. Addresses are left
         * out of the records: a search is not a join. */
        get_code(body, code, sizeof code);
        /* BO-365: TKWAITFOR=<ident>: the player waits for that race; any
         * other search ends a wait */
        if (field(body, "TKWAITFOR", v, sizeof v) && v[0])
            waitfor = atoi(v);
        race_wait_note(si, waitfor);
        if (field(body, "START", v, sizeof v)) skip = atoi(v);
        if (field(body, "COUNT", v, sizeof v) && atoi(v) > 0 && atoi(v) < want) want = atoi(v);
        if (skip < 0) skip = 0;
        /* BO-372: BROWSE GAMES lists every game with its state (browse_reply) */
        if (!code[0] && !waitfor && field(body, "TKBROWSE", v, sizeof v) && !strcmp(v, "1")) {
            browse_reply(si, skip, want);
            return;
        }
        if (code[0] && !code_search_ok(s)) {         /* too many lookups: an empty answer, no strike */
            kv_i(&b, "COUNT", 0);
            reply(si, "gsea", &b);
            return;
        }
        idx = (int *)malloc((size_t)MAX_GAMES * sizeof *idx);
        total = idx ? search_games(code, idx, si, &kicked) : 0;
        if (waitfor) {
            /* The race's successor goes first in the list when this waiter
             * has a seat in it, and is left out while it has none (the
             * racers' seats, the waiters before it). */
            game_t *sg;
            wr = race_by_ident(waitfor);
            if (!wr || !race_live(wr) || !race_visible(wr, code) || race_kicked(wr, si) || !race_kind(wr, si))
                wstate = "gone";
            else if ((wrank = race_rank(wr, si)), !(sg = race_succ(wr)))
                /* a race that started with every seat taken (the game a
                 * waiter missed raced again full) has none for it */
                wstate = wr->nmember >= race_cap(wr) ? "full" : "racing";
            else {
                int pos = -1, listable;
                for (k = 0; k < total && pos < 0; k++)
                    if (&s_games[idx[k]] == sg)
                        pos = k;
                /* a racer of it has its own seat */
                listable = pos >= 0 && (race_member(wr, s->name) || wrank < race_free_seats(wr, sg));
                if (pos >= 0) {
                    int keep = idx[pos];
                    memmove(idx + 1, idx, (size_t)pos * sizeof *idx);    /* to the front ... */
                    idx[0] = keep;
                    if (!listable) {                                    /* ... or out */
                        memmove(idx, idx + 1, (size_t)(total - 1) * sizeof *idx);
                        total--;
                    }
                }
                wstate = listable ? "open"
                       : now_ms() - wr->successor_ms < s_reserve_ms || race_coming(wr, sg) > 0 ? "lobby" : "full";
            }
        }
        for (k = skip; k < total && n < want; k++)
            n++;
        kv_i(&b, "COUNT", n);
        /* a wait that ended for good: the race is gone, or raced again full */
        if (wstate && s->wait_begun == waitfor && s->wait_counted != waitfor &&
            (!strcmp(wstate, "gone") || (!strcmp(wstate, "full") && wr && race_live(wr) && !race_succ(wr)))) {
            s->wait_counted = waitfor;
            st_wait(!strcmp(wstate, "gone") ? 2 : 3);
        }
        if (wstate) {
            game_t *sg = wr && race_live(wr) ? race_succ(wr) : NULL;
            kv_s(&b, "TKWAITSTATE", wstate);
            /* stay together: why it is over (TKWAITWHY), and that its host
             * has signed in again and is making the game (TKWAITHOST) */
            if (!strcmp(wstate, "gone") && wr && wr->why[0]) {
                kv_s(&b, "TKWAITWHY", wr->why);
                for (k = 0; k < wr->nmember; k++)
                    if (!strcmp(wr->member[k], s->name))
                        wr->heard |= 1u << k;
            }
            if (!strcmp(wstate, "racing") && wr->host_back_ms)
                kv_i(&b, "TKWAITHOST", 1);
            if (strcmp(wstate, "gone")) {
                kv_i(&b, "TKWAITPLAYERS", sg ? sg->np : wr->nmember);
                kv_i(&b, "TKWAITSECS", (long)((now_ms() - wr->started_ms) / 1000));
                kv_i(&b, "TKWAITQUEUE", wrank);
                if (sg)
                    kv_s(&b, "TKWAITNAME", sg->name);
            }
        } else if (!total && s_races) {
            /* nothing open: the race this player could wait for, most
             * players first, then the one started longest ago */
            race_t *best = NULL;
            int nr = 0;
            for (k = 0; k < MAX_GAMES; k++) {
                race_t *r = &s_races[k];
                if (!race_candidate(r, code, si))
                    continue;
                nr++;
                if (!best || r->nmember > best->nmember || (r->nmember == best->nmember && r->started_ms < best->started_ms))
                    best = r;
            }
            if (best) {
                kv_i(&b, "TKRACING", nr);
                kv_i(&b, "TKRACEID", best->ident);
                kv_s(&b, "TKRACENAME", best->name);
                kv_i(&b, "TKRACEPLAYERS", best->nmember);
                kv_i(&b, "TKRACEMAX", race_cap(best));
                kv_i(&b, "TKRACESECS", (long)((now_ms() - best->started_ms) / 1000));
            }
        }
        /* the code's only open games kicked this player: the client says so
         * (the title skips the key) */
        if (code[0] && !total && kicked)
            kv_i(&b, "TKKICKED", 1);
        reply(si, "gsea", &b);
        for (k = skip; k < total && want > 0; k++, want--) {
            game_rec(&b, &s_games[idx[k]], REC_ROOM, -1);
            reply(si, "+gam", &b);
        }
        free(idx);
        if (code[0] && total == 0 && !code_exists(code) && code_miss(si))   /* a code that no game has: a strike */
            return;
    } else if (!memcmp(cmd, "gcre", 4)) {
        game_t *g = NULL;
        int r, ident;
        unsigned long f;
        s->wait_for = 0;
        leave_game(si, 0);
        for (i = 0; i < MAX_GAMES && !g; i++)
            if (!s_games[i].used)
                g = &s_games[i];
        ident = g ? next_ident() : 0;
        if (!g || !ident) {
            send_pkt(si, "gcre", "full", "");
            logs_(s, "%s cannot create a game: the server's %d games are open", s->name, MAX_GAMES);
            return;
        }
        memset(g, 0, sizeof *g);
        g->used = 1;
        g->ident = ident;
        if (!field(body, "NAME", g->name, sizeof g->name) || !g->name[0])
            snprintf(g->name, sizeof g->name, "%s", s->name);
        {
            char code[24];
            get_code(body, code, sizeof code);
            snprintf(g->code, sizeof g->code, "%s", code);
            /* TKPUBLIC=1 (the game's CREATE GAME): a public game that also
             * has a code, listed and joinable like any other and found with
             * the code too. Without it a code makes a private game (the System
             * page's Game code row): the code is its password. */
            if (code[0] && !(field(body, "TKPUBLIC", v, sizeof v) && !strcmp(v, "1"))) {
                g->priv = 1;
                snprintf(g->pass, sizeof g->pass, "%s", code);
            } else
                field(body, "PASS", g->pass, sizeof g->pass);
        }
        g->t_ms = now_ms();
        field(body, "PARAMS", g->params, sizeof g->params);
        if (field(body, "SYSFLAGS", v, sizeof v) && v[0]) {
            /* Only the server sets "started" (bit 19), with gsta. */
            f = strtoul(v, NULL, 10) & ~BIT_STARTED;
            snprintf(g->sysflags, sizeof g->sysflags, "%lu", f);
        } else strcpy(g->sysflags, "0");
        if (!field(body, "CUSTFLAGS", g->custflags, sizeof g->custflags) || !g->custflags[0]) strcpy(g->custflags, "0");
        if (!field(body, "MINSIZE", g->minsize, sizeof g->minsize) || !g->minsize[0]) strcpy(g->minsize, "2");
        if (!field(body, "MAXSIZE", g->maxsize, sizeof g->maxsize) || !g->maxsize[0]) strcpy(g->maxsize, "6");
        /* ROOM is the room the client believes it is in; without a "move"
         * first (Quick Match) it is not a room at all. */
        field(body, "ROOM", v, sizeof v);
        r = atoi(v);
        if (s->room)
            r = s->room;                        /* a game is made in the room its maker is in */
        else if (!room_name(r)[0])
            r = k_rooms[0].id;
        if (!s->room)
            s->room = r;
        g->room = r;
        if (!make_secret(g->secret, g->key) || !new_gid(g->gid)) {   /* no randomness: no game (never a guessable secret) */
            g->used = 0;
            send_pkt(si, "gcre", "full", "");
            logs_(s, "cannot create a game: no random numbers");
            return;
        }
        g->players[0] = si;
        g->np = 1;
        g->crowd = s->crowd;
        s->game = g->ident;
        st_game_created(g->params);
        race_link(si, g);               /* before the answer: its record says who is still on the way (TKCOMING) */
        game_rec(&b, g, REC_MEMBER, si);
        reply(si, "gcre", &b);
        broadcast_game(g);
        if (g->priv)                    /* no name and no code of a private game in the log */
            logs_(s, "%s created a private game (%d gid=%s)", s->name, g->ident, g->gid);
        else                            /* nor a public game's code */
            logs_(s, "%s created game %d gid=%s '%s'%s", s->name, g->ident, g->gid, g->name, g->code[0] ? " with a code" : "");
    } else if (!memcmp(cmd, "gjoi", 4)) {
        game_t *g = NULL;
        char up[128], code[24], pw[64];
        int hidden = 0, again;
        /* Quick Match joins by the game's NAME, a listed game by IDENT. A
         * game under way is not joinable, and a name that several games
         * carry (a lingering old game) finds the open one first. A private
         * game (made with a TKCODE) is found only with its code, by name or
         * by ident, whatever else the request carries (PASS is not the
         * code): without it the answer is the same as for no game, and it
         * is a strike. TKCODE does not touch a public game's password. */
        get_code(body, code, sizeof code);
        if (!field(body, "PASS", pw, sizeof pw))
            pw[0] = 0;
        if (field(body, "IDENT", v, sizeof v) && v[0])
            g = game_by_ident(atoi(v));
        else if (field(body, "NAME", v, sizeof v)) {
            for (i = 0; i < MAX_GAMES && !g; i++)        /* with a code: the open game of that code first */
                if (game_open(&s_games[i]) && !strcmp(s_games[i].name, v) && has_code(&s_games[i], code) &&
                    crowd_sees(s, &s_games[i]))
                    g = &s_games[i];
            for (i = 0; i < MAX_GAMES && !g; i++)
                if (game_open(&s_games[i]) && !strcmp(s_games[i].name, v) && crowd_sees(s, &s_games[i]) &&
                    (!s_games[i].priv || (code[0] && !strcmp(s_games[i].pass, code))))
                    g = &s_games[i];
            for (i = 0; i < MAX_GAMES && !g; i++)
                if (s_games[i].used && !strcmp(s_games[i].name, v) && crowd_sees(s, &s_games[i]) &&
                    (!s_games[i].priv || (code[0] && !strcmp(s_games[i].pass, code))))
                    g = &s_games[i];
        }
        if (g && !crowd_sees(s, g) && !game_has(g, si))
            g = NULL;                                        /* --crowd-test: the other kind's game (BO-366) */
        if (g && g->priv && !game_has(g, si) && !(code[0] && !strcmp(g->pass, code))) {
            g = NULL;                                        /* it does not exist for this player */
            hidden = 1;
        }
        /* a strike: a private game was asked for without its code, or a code was given that no game has */
        if ((hidden || (!g && code[0] && !code_exists(code))) && code_miss(si))
            return;
        if (g && kicked_from(g, si)) {      /* kicked from this game: it is gone for this player */
            send_pkt(si, "gjoi", "gone", "TKKICKED=1");     /* the client's text (BO-364); the title skips it */
            return;
        }
        if (g && game_racing(g) && !game_has(g, si)) {
            send_pkt(si, "gjoi", "gone", "");
            return;
        }
        /* a member asking again (the title retries joins) is answered as in:
         * the game's size and password were checked when it joined */
        if (g && !game_has(g, si) && (g->np >= s_max_players || g->np >= atoi(g->maxsize))) {
            send_pkt(si, "gjoi", "full", "");
            logs_(s, "%s cannot join game %d gid=%s: full (%d players)", s->name, g->ident, g->gid, g->np);
            return;
        }
        if (!g) {
            send_pkt(si, "gjoi", "gone", "");
            return;
        }
        if (!game_has(g, si) && !g->priv && g->pass[0] && strcmp(pw, g->pass) && !has_code(g, code)) {
            send_pkt(si, "gjoi", "pass", "");
            return;
        }
        /* BO-365: the game made again after a race keeps its racers' seats
         * for a while; a waiter gets one in arrival order. "full" is what
         * a waiting client reads as "keep waiting". */
        if (!game_has(g, si)) {
            race_t *rr = race_of_successor(g->ident);
            if (rr && !race_member(rr, s->name)) {
                int rank = s->wait_for && race_by_ident(s->wait_for) == rr ? race_rank(rr, si) : 0;
                if (rank >= race_free_seats(rr, g)) {
                    send_pkt(si, "gjoi", "full", "");
                    logs_(s, "%s cannot join game %d gid=%s yet: its racers' seats are kept (%d before it)", s->name, g->ident,
                          g->gid, rank);
                    return;
                }
            }
        }
        if (s->game != g->ident && identity_clash(g, si)) {
            send_pkt(si, "gjoi", "full", "");
            logs_(s, "%s cannot join game %d gid=%s: the same address or identity as a player in it", s->name, g->ident, g->gid);
            return;
        }
        if (s->game != g->ident)
            leave_game(si, 0);
        again = game_has(g, si) && s->game == g->ident;
        /* USERPARAMS only when sent: without them the player keeps its own. */
        if (field(body, "USERPARAMS", up, sizeof up)) {
            again &= !strcmp(s->userparams, up);
            snprintf(s->userparams, sizeof s->userparams, "%s", up);
        }
        if (!game_has(g, si))
            g->players[g->np++] = si;
        s->game = g->ident;
        if (!s->room)
            s->room = g->room;
        game_rec(&b, g, REC_MEMBER, si);
        reply(si, "gjoi", &b);
        if (again) {
            broadcast_game_to(g, si);   /* a member's repeat that changed nothing: to it alone, no log line */
            return;
        }
        broadcast_game(g);
        {
            race_t *wr = s->wait_for ? race_by_ident(s->wait_for) : NULL;
            int i2;
            if (wr && wr->successor == g->ident && s->wait_begun == s->wait_for && s->wait_counted != s->wait_for) {
                s->wait_counted = s->wait_for;
                st_wait(1);
            }
            if (wr && wr->successor == g->ident)
                logs_(s, "%s joined %s%d gid=%s after waiting %lld s", s->name, g->priv ? "a private game " : "game ", g->ident,
                      g->gid, (now_ms() - s->wait_since) / 1000);
            else
                logs_(s, "%s joined %s%d gid=%s", s->name, g->priv ? "a private game " : "game ", g->ident, g->gid);
            s->wait_for = 0;
            /* the host of a race joined another game: nobody makes that one again */
            for (i2 = 0; s_races && i2 < MAX_GAMES; i2++) {
                race_t *r = &s_races[i2];
                if (race_live(r) && r->successor != g->ident && !strcmp(r->host, s->name) && r->host_net == s->peer_net)
                    race_over(r, "its host joined another game");
            }
        }
    } else if (!memcmp(cmd, "glea", 4)) {
        s->wait_for = 0;
        leave_game(si, 1);
        reply(si, "glea", &b);
    } else if (!memcmp(cmd, "gdel", 4)) {
        game_t *g = game_by_ident(s->game);
        s->wait_for = 0;
        if (g && g->np && g->players[0] == si) {
            game_gone(g);
            if (!game_racing(g)) {      /* everyone, the host too, is told (an empty game record and their own record) */
                /* the title's own way out of a game it hosts: logged, as
                 * leave_game logs one (BO-362) */
                logs_(s, "%s closed game %d gid=%s (it had %d player(s))", s->name, g->ident, g->gid, g->np);
                close_members(g);
            } else {
                for (i = 0; i < g->np; i++)
                    s_sess[g->players[i]].game = 0;
            }
            g->used = 0;
            race_game_closed(g->ident);
        }
        reply(si, "gdel", &b);
    } else if (!memcmp(cmd, "gset", 4)) {
        /* gset NAME is the player sending it, not the game. A gset without
         * USERPARAMS (the host's lock, SYSFLAGS only) keeps the player's own;
         * clearing them left the host with no car at the race start. Only
         * the host changes the game itself; a guest changes its own
         * USERPARAMS. */
        game_t *g = game_by_ident(s->game);
        char before[sizeof s->userparams + sizeof g->params + sizeof g->sysflags + sizeof g->custflags +
                    sizeof g->maxsize + sizeof g->minsize + 8];
        int np0 = g ? g->np : 0;
        if (g)
            snprintf(before, sizeof before, "%s|%s|%s|%s|%s|%s", s->userparams, g->params, g->sysflags, g->custflags,
                     g->maxsize, g->minsize);
        if (field(body, "USERPARAMS", v, sizeof v))
            snprintf(s->userparams, sizeof s->userparams, "%s", v);
        if (g && game_racing(g)) {
            /* Under way: nothing about the game changes and no member list
             * goes out. Everyone dropped their lobby link at the start, so
             * the list is short, and a short list makes the others cut their
             * peer link (a "lost connection" in the middle of a race). An
             * empty success is the answer. */
            reply(si, "gset", &b);
        } else if (g && g->players[0] != si && kick_only(body)) {
            reply(si, "gset", &b);                 /* a guest cannot kick: an empty success to the requester only */
        } else if (g) {
            if (g->np && g->players[0] == si) {
                /* KICK=<name>: the host removes a player from its game. The title
                 * sends it (with a "mesg" to the player) for a player whose peer
                 * link does not come up, and repeats it until the player is gone
                 * from the member list: without it, it floods the server. The
                 * kicked player is told like a guest whose host left (an empty
                 * game record and its own record with no game).
                 * TKWHY (BO-352): the host's client tags the kick with the reason
                 * -- "nolink" (its own automatic kick of a peer it cannot reach)
                 * or "host" (a player it chose) -- and it is copied into the
                 * kicked player's empty record. Anything else is dropped. */
                char why[16];
                int have_why = field(body, "TKWHY", why, sizeof why) &&
                               (!strcmp(why, "nolink") || !strcmp(why, "host"));
                if (field(body, "KICK", v, sizeof v) && v[0]) {
                    int k;
                    if (kick_only(body) && kick_target(si, v) == 0) {   /* nothing to kick: an empty success to the requester only */
                        reply(si, "gset", &b);
                        return;
                    }
                    for (k = 1; k < g->np; k++)
                        if (!strcmp(s_sess[g->players[k]].name, v)) {
                            int t = g->players[k];
                            body_t w;
                            char mgm[64];
                            g->kicked[g->nkicked++ % 16] = s_sess[t].uid;
                            s_sess[t].kicked_until = mono_s() + 30;
                            for (; k + 1 < g->np; k++)
                                g->players[k] = g->players[k + 1];
                            g->np--;
                            s_sess[t].game = 0;
                            if (have_why)
                                snprintf(mgm, sizeof mgm, "IDENT=0\nNAME=\nHOST=\nCOUNT=0\nTKWHY=%s", why);
                            else
                                snprintf(mgm, sizeof mgm, "IDENT=0\nNAME=\nHOST=\nCOUNT=0");
                            send_pkt(t, "+mgm", NULL, mgm);
                            who(&w, t, t);
                            reply(t, "+who", &w);
                            logs_(s, "%s kicked %s from game %d gid=%s%s%s", s->name, s_sess[t].name, g->ident, g->gid,
                                  have_why ? " (" : "", have_why ? why : "", have_why ? ")" : "");
                            break;
                        }
                }
                if (field(body, "PARAMS", v, sizeof v)) snprintf(g->params, sizeof g->params, "%s", v);
                if (field(body, "SYSFLAGS", v, sizeof v) && v[0]) {
                    /* Bit 12 from the host locks the game for the race
                     * (0x1BD8F0); it is stored and echoed as sent. Bit 19
                     * (started) is the server's: kept as it is. */
                    unsigned long f = strtoul(v, NULL, 10) & ~BIT_STARTED;
                    if (game_racing(g))
                        f |= BIT_STARTED;
                    snprintf(g->sysflags, sizeof g->sysflags, "%lu", f);
                }
                if (field(body, "CUSTFLAGS", v, sizeof v) && v[0]) snprintf(g->custflags, sizeof g->custflags, "%s", v);
                if (field(body, "MAXSIZE", v, sizeof v) && v[0]) snprintf(g->maxsize, sizeof g->maxsize, "%s", v);
                if (field(body, "MINSIZE", v, sizeof v) && v[0]) snprintf(g->minsize, sizeof g->minsize, "%s", v);
            }
            game_rec(&b, g, REC_MEMBER, si);
            reply(si, "gset", &b);
            {
                /* a gset that changed nothing (the title repeats them) is not
                 * pushed to the room: two records to every session in it */
                char after[sizeof before];
                snprintf(after, sizeof after, "%s|%s|%s|%s|%s|%s", s->userparams, g->params, g->sysflags, g->custflags,
                         g->maxsize, g->minsize);
                broadcast_game_to(g, g->np != np0 || strcmp(before, after) ? -1 : si);
            }
        } else {
            reply(si, "gset", &b);
        }
    } else if (!memcmp(cmd, "gsta", 4)) {
        /* The host starts the race: it locks the game first (gset with
         * SYSFLAGS bit 12, 0x1BD8F0) and sends gsta when the lock comes back
         * (0x9F4EB). The answer is the game record, the record to every
         * player, then "+ses" (the title's "play" event). */
        game_t *g = game_by_ident(s->game);
        if (g && g->np && g->players[0] == si && game_racing(g)) {
            reply(si, "gsta", &b);          /* a repeated start is a no-op: no record, no +ses, not counted */
        } else if (g && g->np && g->players[0] == si) {
            /* The game is under way: lock off, bit 19 on. The guests leave
             * their lobbies for the race on bit 19 (0xA1604); the host is on
             * its way already (state 0xB). */
            unsigned long f = (strtoul(g->sysflags, NULL, 10) & ~0x1000ul) | BIT_STARTED;
            snprintf(g->sysflags, sizeof g->sysflags, "%lu", f);
            game_rec(&b, g, REC_MEMBER, si);
            reply(si, "gsta", &b);
            broadcast_game(g);
            for (i = 0; i < g->np; i++) {          /* each player's own view of the record */
                game_rec(&b, g, REC_MEMBER, g->players[i]);
                b.b[b.n] = 0;
                send_pkt(g->players[i], "+ses", NULL, b.b);
                if (g->np >= 2)
                    s_sess[g->players[i]].race_at = time(NULL);
            }
            logs_(s, "game %d gid=%s started by %s", g->ident, g->gid, s->name);
            st_game_started(g->params, g->np);
            race_started(g);
        } else {
            reply(si, "gsta", &b);
        }
    } else if (!memcmp(cmd, "mesg", 4)) {
        /* A private message to a player (the title sends one before it kicks):
         * nothing is delivered; an empty success, and no log line per message. */
        reply(si, cmd, &b);
    } else if (!memcmp(cmd, "rank", 4)) {
        /* A race result report: counted, answered with an empty success. */
        st_result();
        reply(si, cmd, &b);
    } else if (!memcmp(cmd, "@tic", 4) || !memcmp(cmd, "skey", 4) || !memcmp(cmd, "sviw", 4)) {
        /* @tic offers RC4+MD5 session crypto, skey its key; an empty answer
         * keeps the session in plain text. sviw sets the client's view. */
        reply(si, cmd, &b);
    } else {
        logs_(s, "unknown request %.4s from %s: %s (empty success)", cmd,
              who_log(s), oneline(body));
        reply(si, cmd, &b);
    }
}

/* ── the loop ──────────────────────────────────────────────────────── */

static void drop(int si)
{
    sess_t *s = &s_sess[si];
    if (!s->used)
        return;
    if (!s->probe)
        logs_(s, "%s left%s%s%s", who_log(s), s->why ? " (" : "", s->why ? s->why : "", s->why ? ")" : "");
    s->room = 0;          /* nothing more is sent to it */
    racer_add(s);
    leave_game(si, 0);
    sock_close(s->fd);
    free(s->in);
    free(s->out);
    synth_free(s);
    if (s->authed)
        s_players--;
    s_nsess--;
    memset(s, 0, sizeof *s);
}

static void set_nonblock(sock_t fd)
{
#ifdef _WIN32
    u_long one = 1;
    ioctlsocket(fd, FIONBIO, &one);
#else
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl >= 0)
        fcntl(fd, F_SETFL, fl | O_NONBLOCK);
#endif
}

/* The requests a session may send: TOK_BURST at once, TOK_RATE a second. */
static int take_token(sess_t *s)
{
    long long t = now_ms();
    if (!s->tok_ms) {
        s->tok = TOK_BURST;
        s->tok_ms = t;
    }
    s->tok += (double)(t - s->tok_ms) * TOK_RATE / 1000.0;
    if (s->tok > TOK_BURST)
        s->tok = TOK_BURST;
    s->tok_ms = t;
    if (s->tok >= 1.0) {
        s->tok -= 1.0;
        return 1;
    }
    return 0;
}

static void on_data(int si)
{
    sess_t *s = &s_sess[si];
    char tmp[4096];
    int n = (int)recv(s->fd, tmp, sizeof tmp, 0);
    if (n < 0 && (sock_wouldblock() || sock_interrupted()))
        return;
    if (n <= 0) {
        s->why = n == 0 ? "closed by the player" : sock_reset() ? "connection reset" : "connection error";
        drop(si);
        return;
    }
    if (!s->inlen)
        s->t_partial = mono_s();            /* the first byte of a request */
    if (s->inlen + (size_t)n > s->incap) {
        size_t cap = ((s->inlen + (size_t)n) + 4095) & ~(size_t)4095;
        unsigned char *p = (unsigned char *)realloc(s->in, cap);
        if (!p) { s->why = "out of memory"; drop(si); return; }
        s->in = p;
        s->incap = cap;
    }
    memcpy(s->in + s->inlen, tmp, (size_t)n);
    s->inlen += (size_t)n;
    while (s->used && s->inlen >= 12) {
        uint32_t len = ((uint32_t)s->in[8] << 24) | ((uint32_t)s->in[9] << 16) |
                       ((uint32_t)s->in[10] << 8) | s->in[11];
        char cmd[5], body[BODY_MAX];
        size_t bl;
        if (len < 12 || len > 12 + BODY_MAX) {   /* the game's requests are far smaller */
            logs_(s, "%s: not a lobby packet, dropped", who_log(s));
            s->why = "not a lobby packet";
            drop(si);
            return;
        }
        if (s->inlen < len)
            break;
        memcpy(cmd, s->in, 4);
        cmd[4] = 0;
        bl = len - 12 < sizeof body - 1 ? len - 12 : sizeof body - 1;
        memcpy(body, s->in + 12, bl);
        body[bl] = 0;
        memmove(s->in, s->in + len, s->inlen - len);
        s->inlen -= len;
        s->last_rx = mono_s();              /* a whole request: only that counts as the player being there */
        s->t_partial = mono_s();
        if (!s->inlen && s->incap > 4096) {   /* an idle session keeps no big buffer */
            free(s->in);
            s->in = NULL;
            s->incap = 0;
        }
        if (strcmp(cmd, "~png") && request_is_noop(si, cmd, body)) {
            /* a repeat that changes nothing: answered at once, not counted in the flood bucket */
            if ((long)(s->outlen - s->outoff) > s_noop_queue) {
                /* what it was sent has not been read: no more answers to repeats (they must never fill the queue) */
            } else if (take_noop_token(s)) {
                if (s->noop_logged < 4) {
                    s->noop_logged++;
                    logs_(s, "no-op repeat %.4s: %s", cmd, oneline(body));
                }
                handle(si, cmd, body);
            } else {                              /* over 200 a second: ignored silently */
                time_t now = mono_s();
                if (now - s->noop_t0 >= 10) {
                    s->noop_t0 = now;
                    s->noop_excess = 0;
                }
                if (++s->noop_excess > NOOP_DROP) {
                    logs_(s, "%s: sending requests much too fast, dropped", who_log(s));
                    s->why = "too many requests";
                    drop(si);
                    return;
                }
            }
            continue;
        }
        if (!take_token(s)) {                 /* over the rate: not answered */
            s_throttled_total++;
            if (mono_s() - s->throttle_t0 >= s_throttle_window) {
                s->throttle_t0 = mono_s();
                s->throttled = 0;
            }
            if (mono_s() >= s->kicked_until && ++s->throttled > THROTTLE_DROP) {
                logs_(s, "%s: sending requests much too fast, dropped", who_log(s));
                s->why = "too many requests";
                drop(si);
                return;
            }
            continue;
        }
        if (strcmp(cmd, "~png"))      /* the client's answer to our ping */
            handle(si, cmd, body);
    }
}

/* An address that reached its connection cap is not served for a while:
 * its next connections are closed at once, without even a "full". */
#define PEN_N 256
#define PEN_SECS 10
static struct { uint32_t ip; time_t until; } s_pen[PEN_N];
static unsigned pen_slot(uint32_t ip) { return (unsigned)((ip * 2654435761u) >> 24) % PEN_N; }
static int pen_active(uint32_t ip, time_t now) { unsigned k = pen_slot(ip); return s_pen[k].ip == ip && s_pen[k].until > now; }
static void pen_set(uint32_t ip, time_t now, int secs) { unsigned k = pen_slot(ip); s_pen[k].ip = ip; s_pen[k].until = now + secs; }

/* Sessions already open from one address (loopback is not counted). */
static int sessions_from(const char *peer)
{
    int i, n = 0;
    for (i = 0; i < MAX_SESS; i++)
        if (s_sess[i].used && !strcmp(s_sess[i].peer, peer))
            n++;
    return n;
}

/* Takes the waiting connections (up to a burst); 0 when the accept failed
 * for a reason that will not go away by itself (out of file descriptors). */
static int on_accept(sock_t ls)
{
    static time_t last_full;
    static int skipped;
    int taken;
    for (taken = 0; taken < ACCEPT_BURST; taken++) {
        struct sockaddr_in sa;
        socklen_t n = sizeof sa;
        sock_t fd = accept(ls, (struct sockaddr *)&sa, &n);
        int i, one = 1, loop;
        char peer[16];
        if (fd == BAD_SOCK)
            return !sock_nofd();
        set_nonblock(fd);
        for (i = 0; i < MAX_SESS && s_sess[i].used; i++)
            ;
        loop = s_loop_exempt && ntohl(sa.sin_addr.s_addr) >> 24 == 127;
        addr_str(sa.sin_addr.s_addr, peer, sizeof peer);
        if (!loop && pen_active(sa.sin_addr.s_addr, mono_s())) {
            sock_close(fd);
            s_backoff_total++;
            continue;
        }
        if (s_cfg.beacon_port && addr_class(sa.sin_addr.s_addr) == ADDR_PUBLIC) {
            /* A LAN lobby is for the local network. */
            sock_close(fd);
            s_backoff_total++;
            continue;
        }
        if (i == MAX_SESS || (s_per_ip > 0 && !loop && sessions_from(peer) >= s_per_ip)) {
            if (i != MAX_SESS)
                pen_set(sa.sin_addr.s_addr, mono_s(), PEN_SECS);
            /* Server full: the client's first request is "@dir"; it gets its
             * answer with the status "full" and the connection ends. */
            static const unsigned char full[12] = { '@', 'd', 'i', 'r', 'f', 'u', 'l', 'l', 0, 0, 0, 12 };
            time_t now = mono_s();
            send(fd, (const char *)full, sizeof full, SEND_FLAGS);
            sock_close(fd);
            st_refused();
            if (now - last_full >= 10) {
                logf_("%s: %d connections turned away since the last message",
                      i == MAX_SESS ? "server full" : "too many from one address", skipped + 1);
                last_full = now;
                skipped = 0;
            } else
                skipped++;
            continue;
        }
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof one);
        if (s_sndbuf > 0)
            setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (const char *)&s_sndbuf, sizeof s_sndbuf);
        setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (const char *)&one, sizeof one);
#ifdef TCP_KEEPIDLE
        {
            int idle = 60, intvl = 15, cnt = 4;
            setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, (const char *)&idle, sizeof idle);
            setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, (const char *)&intvl, sizeof intvl);
            setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, (const char *)&cnt, sizeof cnt);
        }
#endif
#ifdef SO_NOSIGPIPE
        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, (const char *)&one, sizeof one);
#endif
        {
            char sid[9];
            if (!new_sid(sid)) {        /* no randomness: no session (never a guessable id) */
                sock_close(fd);
                logf_("cannot accept a connection: no random numbers");
                continue;
            }
            memset(&s_sess[i], 0, sizeof s_sess[i]);
            memcpy(s_sess[i].sid, sid, sizeof sid);
        }
        s_sess[i].used = 1;
        s_sess[i].fd = fd;
        s_sess[i].uid = s_next_uid++;
        s_sess[i].peer_net = sa.sin_addr.s_addr;
        s_sess[i].t_conn = mono_s();
        s_nsess++;
        /* A player on this machine is, to everyone else, at this machine's
         * public address. (Other loopback addresses are kept: copies pinned to
         * 127.0.0.2, .3 ... on one machine for tests.) */
        if (ntohl(sa.sin_addr.s_addr) == 0x7F000001u)
            snprintf(s_sess[i].peer, sizeof s_sess[i].peer, "%s", s_public);
        else
            snprintf(s_sess[i].peer, sizeof s_sess[i].peer, "%s", peer);
        snprintf(s_sess[i].laddr, sizeof s_sess[i].laddr, "%s", s_sess[i].peer);
        s_sess[i].last_ping = s_sess[i].last_rx = mono_s();
        st_connection();
        if (s_cfg.verbose)
            logs_(&s_sess[i], "connection from %s", s_sess[i].peer);
    }
    return 1;
}

/* The first IPv4 address in a resolver answer that is not loopback (Debian
 * maps the machine's own host name to 127.0.1.1 in /etc/hosts). 1: found;
 * -1: there were IPv4 addresses and all were loopback; 0: none. */
static int pick_ipv4(const struct addrinfo *res, char *out, size_t n)
{
    const struct addrinfo *ai;
    int loop = 0;
    for (ai = res; ai; ai = ai->ai_next) {
        uint32_t a;
        if (ai->ai_family != AF_INET || !ai->ai_addr)
            continue;
        a = ((const struct sockaddr_in *)ai->ai_addr)->sin_addr.s_addr;
        if (addr_class(a) == ADDR_LOOPBACK) {
            loop = 1;
            continue;
        }
        addr_str(a, out, n);
        return 1;
    }
    return loop ? -1 : 0;
}

/* --public as a name: its IPv4 address, from the resolver. Players are
 * always handed the dotted address (the game parses it as one).
 * Returns pick_ipv4's answer; 0 also when the lookup fails. */
static int resolve_name(const char *name, char *out, size_t n)
{
    struct addrinfo hints, *res = NULL;
    int rc;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(name, NULL, &hints, &res) != 0 || !res)
        return 0;
    rc = pick_ipv4(res, out, n);
    freeaddrinfo(res);
    return rc;
}

static void warn_if_private(const char *ip)
{
    uint32_t a = inet_addr(ip);
    int c = a == (uint32_t)-1 ? ADDR_PUBLIC : addr_class(a);
    if (c == ADDR_PRIVATE)
        logf_("warning: %s is a private address: players on the internet cannot reach it "
              "(fine for a server on a local network)", ip);
    else if (c == ADDR_LOOPBACK)
        logf_("warning: %s is a loopback address: only players on this machine can reach it", ip);
}

static int s_public_named;    /* --public is a host name: looked up again now and then */

/* Every 10 minutes a named --public is resolved again (off the event loop,
 * in a thread: a slow resolver must not stall the players); a change is
 * logged and used from then on. A failed lookup, or one that finds only
 * loopback addresses, keeps the last good address. */
static void apply_public(int rc, const char *ip)
{
    if (rc == 0) {
        logf_("cannot resolve %s: keeping %s", s_cfg.public_ip, s_public);
    } else if (rc < 0) {
        logf_("%s now resolves only to a loopback address (check /etc/hosts): keeping %s",
              s_cfg.public_ip, s_public);
    } else if (strcmp(ip, s_public)) {
        logf_("%s now resolves to %s (was %s)", s_cfg.public_ip, ip, s_public);
        snprintf(s_public, sizeof s_public, "%s", ip);
        warn_if_private(s_public);
    }
}

#ifdef __GNUC__
__attribute__((unused))
#endif
static void refresh_public(void)       /* the same, here and now (tests) */
{
    char ip[16] = "";
    int rc = resolve_name(s_cfg.public_ip, ip, sizeof ip);
    apply_public(rc, ip);
}

static volatile int s_res_state;       /* 0 idle, 1 looking up, 2 answer ready */
static char s_res_ip[16];
static int s_res_rc;
#ifdef _WIN32
static DWORD WINAPI res_thread(LPVOID p)
{
    (void)p;
    s_res_rc = resolve_name(s_cfg.public_ip, s_res_ip, sizeof s_res_ip);
    MemoryBarrier();
    s_res_state = 2;
    return 0;
}
static int res_start(void)
{
    HANDLE h = CreateThread(NULL, 0, res_thread, NULL, 0, NULL);
    if (!h)
        return 0;
    CloseHandle(h);
    return 1;
}
#else
static void *res_thread(void *p)
{
    (void)p;
    s_res_rc = resolve_name(s_cfg.public_ip, s_res_ip, sizeof s_res_ip);
    __sync_synchronize();
    s_res_state = 2;
    return NULL;
}
static int res_start(void)
{
    pthread_t t;
    if (pthread_create(&t, NULL, res_thread, NULL) != 0)
        return 0;
    pthread_detach(t);
    return 1;
}
#endif

/* Called each pass of the loop: start a lookup every 10 minutes, apply an answer. */
static void public_tick(time_t now, time_t *last)
{
    if (!s_public_named)
        return;
    if (s_res_state == 2) {
#ifndef _WIN32
        __sync_synchronize();
#endif
        apply_public(s_res_rc, s_res_ip);
        s_res_state = 0;
    } else if (s_res_state == 0 && now - *last >= 600) {
        *last = now;
        s_res_state = 1;
        if (!res_start())
            s_res_state = 0;
    }
}

/* 0 when the address is known; 1 when --public is a name that does not resolve. */
static int find_public(void)
{
    struct sockaddr_in sa;
    socklen_t n = sizeof sa;
    sock_t s;
    s_public_named = 0;
    if (s_cfg.public_ip && *s_cfg.public_ip) {
        int rc;
        if (is_dotted(s_cfg.public_ip)) {
            snprintf(s_public, sizeof s_public, "%s", s_cfg.public_ip);
            warn_if_private(s_public);
            return 0;
        }
        rc = resolve_name(s_cfg.public_ip, s_public, sizeof s_public);
        if (rc == 0) {
            logf_("cannot resolve --public %s to an IPv4 address", s_cfg.public_ip);
            return 1;
        }
        if (rc < 0) {
            logf_("--public %s resolves only to a loopback address (127.x.x.x), which players "
                  "cannot reach. Check /etc/hosts: Debian maps the machine's own host name to "
                  "127.0.1.1 there. Fix that line or give --public the IP address.", s_cfg.public_ip);
            return 1;
        }
        warn_if_private(s_public);
        s_public_named = 1;
        return 0;
    }
    s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == BAD_SOCK)
        return 0;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(9);
    sa.sin_addr.s_addr = htonl(0x08080808);  /* a route, nothing is sent */
    if (connect(s, (struct sockaddr *)&sa, sizeof sa) == 0 &&
        getsockname(s, (struct sockaddr *)&sa, &n) == 0)
        addr_str(sa.sin_addr.s_addr, s_public, sizeof s_public);
    sock_close(s);
    return 0;
}

static void beacon(sock_t bs)
{
    struct sockaddr_in to;
    char msg[64];
    memset(&to, 0, sizeof to);
    to.sin_family = AF_INET;
    to.sin_port = htons(s_cfg.beacon_port);
    to.sin_addr.s_addr = htonl(0xFFFFFFFFu);
    snprintf(msg, sizeof msg, "TAKEDAWN-LOBBY %s %u", s_public, (unsigned)s_cfg.port);
    sendto(bs, msg, (int)strlen(msg), 0, (struct sockaddr *)&to, sizeof to);
}

#ifdef _WIN32
static void net_init(void) { WSADATA d; WSAStartup(MAKEWORD(2, 2), &d); }
#else
static void net_init(void) {}
#endif

/* The sessions each hold a file descriptor: raise the soft limit to what the
 * table needs (as far as the hard limit allows) and shrink the table to
 * what is left. */
static void fit_fd_limit(void)
{
#ifndef _WIN32
    struct rlimit rl;
    rlim_t need = (rlim_t)MAX_SESS + 64;
    if (getrlimit(RLIMIT_NOFILE, &rl) != 0)
        return;
    if (rl.rlim_cur != RLIM_INFINITY && rl.rlim_cur < need) {
        struct rlimit want = rl;
        want.rlim_cur = need;
        if (rl.rlim_max != RLIM_INFINITY && want.rlim_cur > rl.rlim_max)
            want.rlim_cur = rl.rlim_max;
        if (setrlimit(RLIMIT_NOFILE, &want) != 0) {
#ifdef OPEN_MAX
            if (want.rlim_cur > OPEN_MAX) {      /* macOS: capped below an unlimited hard limit */
                want.rlim_cur = OPEN_MAX;
                setrlimit(RLIMIT_NOFILE, &want);
            }
#endif
        }
        getrlimit(RLIMIT_NOFILE, &rl);
    }
    if (rl.rlim_cur != RLIM_INFINITY && (rlim_t)MAX_SESS + 64 > rl.rlim_cur) {
        int fit = rl.rlim_cur > 80 ? (int)rl.rlim_cur - 64 : 16;
        logf_("open-file limit %lu: sessions cut from %d to %d (raise LimitNOFILE / ulimit -n)",
              (unsigned long)rl.rlim_cur, MAX_SESS, fit);
        MAX_SESS = fit;
    }
#endif
}

int aries_run(const aries_config *cfg)
{
    struct sockaddr_in sa;
    sock_t ls, bs = BAD_SOCK;
    int one = 1, i;
    time_t last_beacon = 0, last_stats = 0, last_resolve = time(NULL);
    pfd_t *pf;
    int *pi, first;
    s_cfg = *cfg;
    if (!s_cfg.port)
        s_cfg.port = 21810;
    if (s_cfg.crowd_test)
        logf_("--crowd-test: load-tool players (TKCROWD=1) and other players do not see each other's games; "
              "a test lobby only");
    MAX_SESS = s_cfg.max_sessions > 0 ? s_cfg.max_sessions : DEF_SESSIONS;
    MAX_GAMES = s_cfg.max_games > 0 ? s_cfg.max_games : DEF_GAMES;
    s_max_players = s_cfg.max_players > 0 ? s_cfg.max_players : MAX_PLAYERS;
    if (s_max_players < 2) s_max_players = 2;
    if (s_max_players > MAX_PLAYERS) s_max_players = MAX_PLAYERS;
    /* 0: the default; negative: none. (A copy of the game that runs a lobby
     * for its network passes zeros and gets the defaults.) */
    s_idle_secs = s_cfg.idle_secs > 0 ? s_cfg.idle_secs : s_cfg.idle_secs == 0 ? DEF_IDLE_SECS : 0;
    s_auth_secs = s_cfg.auth_secs > 0 ? s_cfg.auth_secs : s_cfg.auth_secs == 0 ? DEF_AUTH_SECS : 0;
    s_per_ip = s_cfg.max_per_ip > 0 ? s_cfg.max_per_ip : s_cfg.max_per_ip == 0 ? DEF_PER_IP : 0;
    s_res_state = 0;
    memset(s_pen, 0, sizeof s_pen);
    memset(s_room_dirty, 0, sizeof s_room_dirty);
    fit_fd_limit();
    free(s_sess); free(s_games);
    s_sess = (sess_t *)calloc((size_t)MAX_SESS, sizeof *s_sess);
    free(s_lease);
    free(s_racer);
    s_racer = (racer_t *)calloc((size_t)MAX_SESS, sizeof *s_racer);
    s_racers = 0;
    s_lease = (lease_t *)calloc((size_t)MAX_SESS, sizeof *s_lease);   /* a lease table as big as the session table; none if it fails */
    s_games = (game_t *)calloc((size_t)MAX_GAMES, sizeof *s_games);
    free(s_races);
    s_races = (race_t *)calloc((size_t)MAX_GAMES, sizeof *s_races);   /* none if it fails: no waiting (BO-365) */
    pf = (pfd_t *)calloc((size_t)MAX_SESS + 2, sizeof *pf);
    pi = (int *)calloc((size_t)MAX_SESS + 2, sizeof *pi);
    relay_init();
    s_nsess = s_players = 0;
    memset(s_synth_used, 0, sizeof s_synth_used);
    s_lease_seq = 0;
    {   /* the synthetic addresses do not start at 10.64.0.1 after a restart: a client's old map must not meet them */
        unsigned char r[2];
        s_synth_next = 1;
        if (rand_bytes(r, 2))
            s_synth_next = 1 + (((int)r[0] << 8 | r[1]) % (SYNTH_N - 1));
    }
    s_next_uid = 1;
    s_next_game = 1;
    if (!s_sess || !s_games || !pf || !pi) {
        free(pf); free(pi);
        logf_("out of memory for %d sessions and %d games", MAX_SESS, MAX_GAMES);
        return 1;
    }
    net_init();
    if (find_public()) {
        free(s_sess); free(s_games); s_sess = NULL; s_games = NULL;
        free(pf); free(pi);
        return 1;
    }
    ls = socket(AF_INET, SOCK_STREAM, 0);
    if (ls == BAD_SOCK) {
        free(pf); free(pi);
        return 1;
    }
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof one);
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(s_cfg.port);
    sa.sin_addr.s_addr = s_cfg.bind_ip ? inet_addr(s_cfg.bind_ip) : htonl(INADDR_ANY);
    if (bind(ls, (struct sockaddr *)&sa, sizeof sa) != 0 || listen(ls, 512) != 0) {
        logf_("cannot listen on TCP port %u", (unsigned)s_cfg.port);
        sock_close(ls);
        free(pf); free(pi);
        return 1;
    }
    set_nonblock(ls);
    s_rl_sock = BAD_SOCK;
    if (s_cfg.relay_port) {             /* BO-381: the UDP relay */
        sock_t us = socket(AF_INET, SOCK_DGRAM, 0);
        int rcv = 1 << 20;
        struct sockaddr_in ua = sa;
        ua.sin_port = htons(s_cfg.relay_port);
        if (us != BAD_SOCK) {
            setsockopt(us, SOL_SOCKET, SO_RCVBUF, (const char *)&rcv, sizeof rcv);
            if (bind(us, (struct sockaddr *)&ua, sizeof ua) != 0) {
                logf_("cannot listen on UDP port %u (relay off)", (unsigned)s_cfg.relay_port);
                sock_close(us);
            } else {
                set_nonblock(us);
                s_rl_sock = us;
            }
        }
    }
    st_init(s_cfg.stats_dir, s_cfg.public_dir, s_cfg.page_html);
    if (s_cfg.beacon_port) {
        bs = socket(AF_INET, SOCK_DGRAM, 0);
        if (bs != BAD_SOCK)
            setsockopt(bs, SOL_SOCKET, SO_BROADCAST, (const char *)&one, sizeof one);
    }
    logf_("Takedawn lobby on TCP %u, players reach it at %s%s%s%s%s; up to %d sessions, %d games",
          (unsigned)s_cfg.port, s_public, s_public_named ? " (" : "",
          s_public_named ? s_cfg.public_ip : "", s_public_named ? ")" : "",
          s_cfg.beacon_port ? " (LAN beacon on)" : "", MAX_SESS, MAX_GAMES);
    s_stop = 0;
    while (!s_stop) {
        int n = 0, r, accept_ok = 1;
        time_t now, mnow;
        pf[0].fd = ls; pf[0].events = POLLIN; pf[0].revents = 0;
        n = 1;
        if (s_rl_sock != BAD_SOCK) {
            pf[1].fd = s_rl_sock; pf[1].events = POLLIN; pf[1].revents = 0;
            n = 2;
        }
        first = n;                      /* the sessions' entries start here */
        for (i = 0; i < MAX_SESS; i++)
            if (s_sess[i].used) {
                pf[n].fd = s_sess[i].fd;
                pf[n].events = (short)(POLLIN | (s_sess[i].outlen > s_sess[i].outoff ? POLLOUT : 0));
                pf[n].revents = 0;
                pi[n++] = i;
            }
        r = poll_(pf, n, 1000);
        if (r < 0) {
            if (!sock_interrupted()) {
#ifdef _WIN32
                Sleep(100);
#else
                usleep(100000);
#endif
            }
            continue;
        }
        if (r > 0) {
            if (pf[0].revents & POLLIN)
                accept_ok = on_accept(ls);
            if (first == 2 && (pf[1].revents & POLLIN))
                relay_pump();
            for (i = first; i < n; i++) {
                int si = pi[i];
                if (pf[i].revents && s_sess[si].used && s_sess[si].fd == pf[i].fd) {
                    if (pf[i].revents & POLLOUT)
                        out_flush(&s_sess[si]);
                    if (pf[i].revents & ~POLLOUT)
                        on_data(si);
                }
            }
            if (!accept_ok) {     /* out of descriptors: do not spin on the listener */
#ifdef _WIN32
                Sleep(50);
#else
                usleep(50000);
#endif
            }
        }
        for (i = 0; i < MAX_SESS; i++)
            if (s_sess[i].used && s_sess[i].dead)
                drop(i);
        now = time(NULL);
        mnow = mono_s();
        host_gone_tick(now_ms());
        race_tick(now_ms());
        for (i = 0; i < MAX_SESS; i++) {
            if (!s_sess[i].used)
                continue;
            if (s_idle_secs > 0 && mnow - s_sess[i].last_rx >= s_idle_secs) {
                logs_(&s_sess[i], "%s: nothing received for %d s, dropped", who_log(&s_sess[i]), s_idle_secs);
                s_sess[i].why = "idle";
                drop(i);
            } else if (s_sess[i].inlen > 0 && mnow - s_sess[i].t_partial >= s_partial_secs) {
                logs_(&s_sess[i], "%s: a request left unfinished for %d s, dropped", who_log(&s_sess[i]), s_partial_secs);
                s_sess[i].why = "unfinished request";
                drop(i);
            } else if (s_auth_secs > 0 && !s_sess[i].authed && mnow - s_sess[i].t_conn >= s_auth_secs) {
                logs_(&s_sess[i], "%s: not signed in after %d s, dropped", who_log(&s_sess[i]), s_auth_secs);
                s_sess[i].why = "not signed in in time";
                drop(i);
            } else if (mnow - s_sess[i].last_ping >= PING_SECS) {
                s_sess[i].last_ping = mnow;
                send_pkt(i, "~png", NULL, "REF=1\nTIME=1");
            }
        }
        for (i = 0; i < MAX_SESS; i++)
            if (s_sess[i].used && s_sess[i].dead)
                drop(i);
        st_roll();
        if (s_cfg.stats_secs > 0 && now - last_stats >= s_cfg.stats_secs) {
            int ng = 0, k;
            last_stats = now;
            for (k = 0; k < MAX_GAMES; k++) ng += s_games[k].used;
            char rl[96] = "";
            if (s_rl_sock != BAD_SOCK)
                snprintf(rl, sizeof rl, "; relay %lu packets, %lu bytes, %lu dropped",
                         s_st.relay_packets, s_st.relay_bytes, s_st.relay_dropped);
            logf_("stats: %d of %d sessions (%d signed in, %d in a race, peak today %d), %d of %d games; today %lu sign-ins, %lu games started%s",
                  s_nsess, MAX_SESS, s_players, s_racers, s_st.peak_sessions, ng, MAX_GAMES,
                  s_st.signins, s_st.games_started, rl);
        }
        public_tick(now, &last_resolve);
        lease_expire(now);
        racer_expire(now);
        {   /* the counts every room record carries: pushed when they changed, at most every STATUS_SECS */
            static int last_online = -1, last_waiting = -1;
            static time_t last_push;
            if (now - last_push >= s_status_secs) {
                int w = waiting_public();
                last_push = now;
                if (online_now() != last_online || w != last_waiting) {
                    int r;
                    last_online = online_now();
                    last_waiting = w;
                    for (r = 0; r < N_ROOMS; r++)
                        s_room_dirty[r] = 1;
                }
            }
        }
        rooms_flush(0);
        if (bs != BAD_SOCK && now - last_beacon >= 2) {
            last_beacon = now;
            beacon(bs);
        }
    }
    for (i = 0; i < MAX_SESS; i++) {
        if (s_sess[i].used)
            s_sess[i].why = "server stopping";
        drop(i);
    }
    free(s_lease);
    s_lease = NULL;
    free(s_racer);
    s_racer = NULL;
    s_racers = 0;
    free(s_races);
    s_races = NULL;
    if (s_rl_sock != BAD_SOCK)
        sock_close(s_rl_sock);
    s_rl_sock = BAD_SOCK;
    free(s_rl);
    s_rl = NULL;
    s_rl_n = 0;
    free(s_rlg);
    s_rlg = NULL;
    s_rlg_n = 0;
    st_write_current();
    st_free();
    free(pf);
    free(pi);
    if (bs != BAD_SOCK)
        sock_close(bs);
    sock_close(ls);
    return 0;
}

void aries_stop(void) { s_stop = 1; }

static aries_config s_thread_cfg;

/* One server per process: aries_run works on the process's one set of
 * tables, so a second thread would free them under the first one. A start
 * waits up to 5 s for a stopping server to finish, and fails while one runs. */
#ifdef _WIN32
static volatile LONG s_thread_up;
static int thread_claim(void)
{
    int k;
    for (k = 0; k < 500; k++) {
        if (InterlockedCompareExchange(&s_thread_up, 1, 0) == 0)
            return 1;
        if (!s_stop)
            return 0;
        Sleep(10);
    }
    return 0;
}
#else
static volatile int s_thread_up;
static int thread_claim(void)
{
    int k;
    for (k = 0; k < 500; k++) {
        struct timespec ts = { 0, 10000000 };
        if (__sync_bool_compare_and_swap(&s_thread_up, 0, 1))
            return 1;
        if (!s_stop)
            return 0;
        nanosleep(&ts, NULL);
    }
    return 0;
}
#endif

#ifdef _WIN32
static DWORD WINAPI thread_main(LPVOID p) { (void)p; aries_run(&s_thread_cfg); InterlockedExchange(&s_thread_up, 0); return 0; }
int aries_start_thread(const aries_config *cfg)
{
    HANDLE h;
    if (!thread_claim())
        return 1;
    s_thread_cfg = *cfg;
    h = CreateThread(NULL, 0, thread_main, NULL, 0, NULL);
    if (!h) {
        InterlockedExchange(&s_thread_up, 0);
        return 1;
    }
    CloseHandle(h);
    return 0;
}
#else
static void *thread_main(void *p) { (void)p; aries_run(&s_thread_cfg); __sync_lock_release(&s_thread_up); return NULL; }
int aries_start_thread(const aries_config *cfg)
{
    pthread_t t;
    if (!thread_claim())
        return 1;
    s_thread_cfg = *cfg;
    if (pthread_create(&t, NULL, thread_main, NULL) != 0) {
        __sync_lock_release(&s_thread_up);
        return 1;
    }
    pthread_detach(t);
    return 0;
}
#endif

int aries_find_lan(uint16_t beacon_port, unsigned wait_ms, char *ip, int n)
{
    struct sockaddr_in sa;
    sock_t s;
    int one = 1, found = 0;
    unsigned waited = 0;
    net_init();
    s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == BAD_SOCK)
        return 0;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof one);
#ifdef SO_REUSEPORT
    setsockopt(s, SOL_SOCKET, SO_REUSEPORT, (const char *)&one, sizeof one);
#endif
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(beacon_port);
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(s, (struct sockaddr *)&sa, sizeof sa) != 0) {
        sock_close(s);
        return 0;
    }
    while (!found && waited < wait_ms) {
        pfd_t pf;
        char buf[128];
        pf.fd = s; pf.events = POLLIN; pf.revents = 0;
        if (poll_(&pf, 1, 200) > 0) {
            struct sockaddr_in from;
            socklen_t fl = sizeof from;
            int k = (int)recvfrom(s, buf, sizeof buf - 1, 0, (struct sockaddr *)&from, &fl);
            char addr[32];
            unsigned port;
            uint32_t claimed;
            if (k > 0) {
                buf[k] = 0;
                /* The lobby named must be the one that sent it, and on the
                 * local network: a beacon cannot send players elsewhere. */
                if (sscanf(buf, "TAKEDAWN-LOBBY %31s %u", addr, &port) == 2 && parse_ip(addr, &claimed) &&
                    claimed == from.sin_addr.s_addr && addr_class(claimed) != ADDR_PUBLIC) {
                    snprintf(ip, (size_t)n, "%s", addr);
                    found = 1;
                }
            }
        }
        waited += 200;
    }
    sock_close(s);
    return found;
}
