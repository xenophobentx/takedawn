/*
 * test_fuzz.c: a fuzz harness around handle(), field() and on_data() of the
 * lobby server (BO-334). MIT. POSIX only. It includes aries_server.c, gives
 * the server a few sessions on socket pairs (no listening socket) and throws
 * random and hostile requests at them: valid ones, near misses (out of range
 * numbers, bad names, long values, control bytes), random commands, and raw
 * random bytes on the wire. After every step it checks the server's own
 * books: sessions, games, who is in which game, the counts, and that every
 * stored string is terminated. Build it with the sanitizers:
 *
 *   gcc -O1 -g -fsanitize=address,undefined -o test_fuzz test_fuzz.c -lpthread
 *   ./test_fuzz [iterations [seed]]
 */
#include "aries_server.c"

#include <signal.h>
#include <stdio.h>

#define NS 8
static int g_peer[NS];            /* the far end of each session's socket */
static unsigned long long rs = 88172645463325252ull;
static long s_bad, s_peak_games, s_peak_players, s_fz_started, s_peak_priv;

static unsigned rnd(void)
{
    rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17;
    return (unsigned)(rs >> 11);
}
static const char *pick(const char *const *a, int n) { return a[rnd() % (unsigned)n]; }

static void quiet(const char *l) { (void)l; }

static const char *const k_keys[] = {
    "ADDR", "PORT", "GTAG", "NAME", "PERS", "XUID", "IDENT", "START", "COUNT", "PASS", "PARAMS",
    "USERPARAMS", "SYSFLAGS", "CUSTFLAGS", "MINSIZE", "MAXSIZE", "ROOM", "MASK", "SKEY", "TKSECRET",
    "TKCODE", "TKCODE", "TKCODE", "TKONLINE", "TKFOO", "TKID", "TKID", "KICK", "KICK", "TKNAME",
    "TKWAITFOR", "TKWAITFOR",
};
static const char *const k_vals[] = {
    "Alice", "Bob", "", "a", "0123456789abcde", "0123456789abcdef", "a=b", "x y", "\x1b[0m", "caf\xc3\xa9",
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "-1", "2147483647", "4294967295", "4294967296",
    "-2147483649", "99999999999", "1,2,3,", "-1,-1,-1", "1,,2", "abc", ",", "1,2,0,3,", "1,2,1,3,", "1,2,4,3,",
    "-1622913024,0,0,16842788,50331648,-994224003,0,0,0,", "262464", "524288", "528448", "0",
    "1.2.3.4", "10.0.0.1", "127.0.0.2", "100.64.1.1", "8.8.8.8", "256.1.1.1", "a.b.c.d", "1.2.3",
    "$0123456789abcdef", "$0123456789ABCDEF", "$zz", "$0123456789abcdef0", "TakedawnGame",
    "this name is much longer than thirty-two characters for sure",
    "KZ7MQ", "kz7mq", "ABC12", "a b", "12345678901234567", "ABC-1", "Z", "ZZZ",
};
static const char *const k_cmds[] = {
    "auth", "pers", "move", "gsea", "gcre", "gjoi", "glea", "gdel", "gset", "gsta", "addr", "news",
    "sele", "skey", "@tic", "@dir", "sviw", "rank", "gcre", "gjoi", "gset", "gcre", "move", "gsta", "mesg",
};

static int ssz(char *dst, size_t n, const char *s)
{
    return snprintf(dst, n, "%s", s);
}

/* a value that is valid for the key (most requests are, so games get made and joined) */
static const char *good_val(const char *key)
{
    static const char *const nm[] = { "Alice", "Bob", "TakedawnGame", "Hot lap" };
    static const char *const pr[] = { "1,2,0,3,", "1,2,1,3,", "1,2,2,3,", "1,2,4,3,", "-1,-1,-1", "1,1,3," };
    static const char *const sz[] = { "2", "3", "4", "6", "8" };
    static const char *const fl[] = { "0", "262464", "4096", "528448", "524288" };
    static const char *const ad[] = { "10.0.0.5", "127.0.0.2", "100.64.1.1", "192.168.1.9" };
    if (!strcmp(key, "NAME") || !strcmp(key, "GTAG") || !strcmp(key, "PERS") || !strcmp(key, "TKNAME")) return pick(nm, 4);
    if (!strcmp(key, "KICK")) { static char kb[8]; snprintf(kb, sizeof kb, "P%d", (int)(rnd() % NS)); return kb; }
    if (!strcmp(key, "PARAMS") || !strcmp(key, "USERPARAMS")) return pick(pr, 6);
    if (!strcmp(key, "MAXSIZE") || !strcmp(key, "MINSIZE")) return pick(sz, 5);
    if (!strcmp(key, "SYSFLAGS") || !strcmp(key, "CUSTFLAGS")) return pick(fl, 5);
    if (!strcmp(key, "ADDR")) return pick(ad, 4);
    if (!strcmp(key, "XUID")) { static const char *const xs[] = { "$0123456789abcdef", "$0500000a77241111", "$0500000a77242222", "$0600000a77243333" }; return pick(xs, 4); }
    if (!strcmp(key, "TKID")) return rnd() % 5 ? "1" : "0";
    if (!strcmp(key, "PASS")) return rnd() % 4 ? "" : "pw";
    if (!strcmp(key, "TKCODE")) {
        static const char *const cd[] = { "KZ7MQ", "kz7mq", "ABC12", "ZZZ", "", "k7x" };
        return pick(cd, 6);
    }
    return "1";
}

static void make_body(char *out, size_t n)
{
    size_t o = 0;
    int k = (int)(rnd() % 8), i;
    out[0] = 0;
    for (i = 0; i < k && o + 300 < n; i++) {
        const char *key = rnd() % 12 ? pick(k_keys, (int)(sizeof k_keys / sizeof k_keys[0])) : "ZZ";
        const char *val = pick(k_vals, (int)(sizeof k_vals / sizeof k_vals[0]));
        static char live[64];
        if (rnd() % 100 < 85)
            val = good_val(key);
        if (rnd() % 3 == 0) {                 /* an ident or a name of a game that exists */
            int g = (int)(rnd() % (unsigned)MAX_GAMES);
            if (s_games[g].used) {
                if (!strcmp(key, "IDENT")) { snprintf(live, sizeof live, "%d", s_games[g].ident); val = live; }
                else if (!strcmp(key, "NAME")) { snprintf(live, sizeof live, "%s", s_games[g].name); val = live; }
            }
        }
        o += (size_t)snprintf(out + o, n - o, "%s=%s%s", key, val, rnd() % 6 ? "\n" : " ");
    }
    if (rnd() % 20 == 0 && o + 40 < n) {      /* a run of random bytes */
        int m = (int)(rnd() % 30), j;
        for (j = 0; j < m; j++)
            out[o++] = (char)(1 + rnd() % 254);
        out[o] = 0;
    }
    (void)ssz;
}

static void new_sess(int i)
{
    int sv[2];
    sess_t *s = &s_sess[i];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) { perror("socketpair"); exit(2); }
    set_nonblock(sv[0]);
    set_nonblock(sv[1]);
    {
        int sz = 1 << 20;
        setsockopt(sv[0], SOL_SOCKET, SO_SNDBUF, &sz, sizeof sz);
    }
    if (g_peer[i] > 0)
        close(g_peer[i]);
    g_peer[i] = sv[1];
    memset(s, 0, sizeof *s);
    s->used = 1;
    s->fd = sv[0];
    s->uid = s_next_uid++;
    if (!new_sid(s->sid)) { printf("BAD new_sid failed\n"); s_bad++; }
    snprintf(s->peer, sizeof s->peer, "10.0.0.%d", i + 1);
    s->peer_net = inet_addr(s->peer);
    snprintf(s->laddr, sizeof s->laddr, "%s", s->peer);
    s->last_rx = s->last_ping = s->t_conn = time(NULL);
    s_nsess++;
    if (rnd() % 4) {                 /* most are signed in, so the later requests get somewhere */
        s->authed = 1;
        s_players++;
        snprintf(s->name, sizeof s->name, "P%d", i);
        s->room = 1;
    }
}

static void drain(void)
{
    char buf[65536];
    int i;
    for (i = 0; i < NS; i++)
        if (g_peer[i] > 0)
            while (recv(g_peer[i], buf, sizeof buf, 0) > 0)
                ;
}

static void check(const char *what)
{
    int i, j, used = 0, authed = 0;
    for (i = 0; i < MAX_SESS; i++) {
        const sess_t *s = &s_sess[i];
        if (!s->used)
            continue;
        used++;
        authed += s->authed;
        if (memchr(s->name, 0, sizeof s->name) == NULL || memchr(s->userparams, 0, sizeof s->userparams) == NULL ||
            memchr(s->laddr, 0, sizeof s->laddr) == NULL || memchr(s->xuid, 0, sizeof s->xuid) == NULL) {
            printf("BAD %s: unterminated session string\n", what); s_bad++;
        }
        if (s->game) {
            const game_t *g = game_by_ident(s->game);
            if (!g || !game_has(g, i)) {
                printf("BAD %s: session %d in game %d which does not hold it\n", what, i, s->game); s_bad++;
            }
        }
        if (s->outlen < s->outoff || s->outlen > s->outcap) {
            printf("BAD %s: queue bookkeeping\n", what); s_bad++;
        }
    }
    {   /* BO-340: a session id is 8 hex digits, unique among live sessions; a game id 12, unique among live games;
         * the game id is in member records only, never in a room or list record, and no id is in the public page */
        int q, w, k;
        for (q = 0; q < MAX_SESS; q++) {
            const sess_t *a = &s_sess[q];
            if (!a->used)
                continue;
            if (strlen(a->sid) != 8 || strspn(a->sid, "0123456789abcdef") != 8) {
                printf("BAD %s: session id malformed\n", what); s_bad++;
            }
            for (w = q + 1; w < MAX_SESS; w++)
                if (s_sess[w].used && !strcmp(s_sess[w].sid, a->sid)) {
                    printf("BAD %s: two sessions with one session id\n", what); s_bad++;
                }
        }
        for (q = 0; q < MAX_GAMES; q++) {
            const game_t *g = &s_games[q];
            body_t rec;
            if (!g->used)
                continue;
            if (strlen(g->gid) != 12 || strspn(g->gid, "0123456789abcdef") != 12) {
                printf("BAD %s: game id malformed\n", what); s_bad++;
            }
            for (w = q + 1; w < MAX_GAMES; w++)
                if (s_games[w].used && !strcmp(s_games[w].gid, g->gid)) {
                    printf("BAD %s: two games with one game id\n", what); s_bad++;
                }
            for (k = REC_ROOM; k <= REC_MEMBER; k++) {
                game_rec(&rec, g, k, g->np ? g->players[0] : -1);
                rec.b[rec.n] = 0;
                if ((strstr(rec.b, "TKGID=") != NULL) != (k == REC_MEMBER) || strstr(rec.b, "TKSID")) {
                    printf("BAD %s: game id in the wrong record kind %d\n", what, k); s_bad++;
                }
            }
        }
    }
    {   /* leases: each holds a pool address nobody live has, no two the same, and the pool bits are exactly live + leased */
        int q, w, used = 0, live = 0, leased = 0;
        for (q = 1; q < SYNTH_N; q++)
            used += s_synth_used[q];
        for (q = 0; q < MAX_SESS; q++)
            live += s_sess[q].used && s_sess[q].synth_idx;
        for (q = 0; q < MAX_SESS; q++) {
            if (!s_lease[q].used)
                continue;
            leased++;
            for (w = 0; w < MAX_SESS; w++) {
                if (s_sess[w].used && s_sess[w].synth_idx == s_lease[q].idx) {
                    printf("BAD %s: a leased address is held by a live session\n", what); s_bad++;
                }
                if (w != q && s_lease[w].used && s_lease[w].idx == s_lease[q].idx) {
                    printf("BAD %s: one address leased twice\n", what); s_bad++;
                }
            }
        }
        if (used != live + leased) {
            printf("BAD %s: pool bits %d, live %d, leased %d\n", what, used, live, leased); s_bad++;
        }
    }
    {   /* identity: synthetic addresses are unique; from every viewer, the members of a game have different addresses */
        int q, w, v;
        for (q = 0; q < MAX_SESS; q++) {
            const sess_t *a = &s_sess[q];
            if (!a->used || !a->tkid)
                continue;
            if (!a->synth || a->synth != synth_net(a->synth_idx) || !s_synth_used[a->synth_idx]) {
                printf("BAD %s: synthetic address bookkeeping\n", what); s_bad++;
            }
            for (w = q + 1; w < MAX_SESS; w++)
                if (s_sess[w].used && s_sess[w].tkid && s_sess[w].synth == a->synth) {
                    printf("BAD %s: two sessions with one synthetic address\n", what); s_bad++;
                }
        }
        for (q = 0; q < MAX_GAMES; q++) {
            const game_t *g = &s_games[q];
            if (!g->used)
                continue;
            for (v = 0; v < g->np; v++)
                for (w = 0; w < g->np; w++) {
                    int x;
                    if (w == v)
                        continue;
                    for (x = w + 1; x < g->np; x++)
                        if (x != v && shown_addr(g->players[w], g->players[v]) == shown_addr(g->players[x], g->players[v])) {
                            printf("BAD %s: two members of game %d have one address for a viewer\n", what, g->ident); s_bad++;
                        }
                    if (shown_addr(g->players[w], g->players[v]) == shown_addr(g->players[v], g->players[v]) && g->np > 1) {
                        printf("BAD %s: a member shows the viewer's own address\n", what); s_bad++;
                    }
                }
        }
    }
    {   /* private games: never in a search without a code; a search with a code finds only theirs */
        int idx[8], n, q;
        n = search_games("", idx, -1, NULL);
        for (q = 0; q < n; q++)
            if (s_games[idx[q]].priv) { printf("BAD %s: a search without a code lists a private game\n", what); s_bad++; }
        for (i = 0; i < MAX_GAMES; i++)
            if (s_games[i].used && s_games[i].priv) {
                n = search_games(s_games[i].pass, idx, -1, NULL);
                for (q = 0; q < n; q++)
                    if (!s_games[idx[q]].priv || strcmp(s_games[idx[q]].pass, s_games[i].pass)) {
                        printf("BAD %s: a search with a code lists another game\n", what); s_bad++;
                    }
            }
        if (waiting_public() < 0 || waiting_public() > MAX_GAMES) { printf("BAD %s: waiting count\n", what); s_bad++; }
        for (i = 0; i < MAX_GAMES; i++) {    /* race records (BO-365): sane, a live one has a host and its players */
            const race_t *r = &s_races[i];
            if (r->used && (r->nmember < 2 || r->nmember > MAX_PLAYERS || !r->host[0] || r->ident <= 0 ||
                            (r->fwd && !r->gone))) {
                printf("BAD %s: race record %d\n", what, r->ident); s_bad++;
            }
            if (!r->used && r->ident) { printf("BAD %s: a free race record keeps data\n", what); s_bad++; }
        }
    }
    if (used != s_nsess || authed != s_players) {
        printf("BAD %s: counts %d/%d sessions %d/%d signed in\n", what, used, s_nsess, authed, s_players); s_bad++;
    }
    for (i = 0; i < MAX_GAMES; i++) {
        const game_t *g = &s_games[i];
        if (!g->used)
            continue;
        if (g->np < 1 || g->np > MAX_PLAYERS || g->np > s_max_players) {
            printf("BAD %s: game %d holds %d players\n", what, g->ident, g->np); s_bad++;
            continue;
        }
        for (j = 0; j < g->np; j++) {
            int p = g->players[j];
            if (p < 0 || p >= MAX_SESS || !s_sess[p].used || s_sess[p].game != g->ident) {
                printf("BAD %s: game %d player %d is not in it\n", what, g->ident, j); s_bad++;
            }
        }
        if (memchr(g->name, 0, sizeof g->name) == NULL || memchr(g->pass, 0, sizeof g->pass) == NULL ||
            memchr(g->params, 0, sizeof g->params) == NULL || memchr(g->sysflags, 0, sizeof g->sysflags) == NULL ||
            memchr(g->custflags, 0, sizeof g->custflags) == NULL || memchr(g->minsize, 0, sizeof g->minsize) == NULL ||
            memchr(g->maxsize, 0, sizeof g->maxsize) == NULL || strlen(g->secret) != 32) {
            printf("BAD %s: game string\n", what); s_bad++;
        }
        if (g->priv && (!v_code(g->pass) || strpbrk(g->pass, "abcdefghijklmnopqrstuvwxyz"))) {
            printf("BAD %s: a private game holds a code a client may not send: %s\n", what, g->pass); s_bad++;
        }
        if (!v_list(g->params, 16) || !v_int(g->sysflags, 0, 4294967295LL) || !v_int(g->minsize, 1, 8) ||
            !v_int(g->maxsize, 2, 8) || strlen(g->name) > GAME_NAME_MAX) {
            printf("BAD %s: a game holds a value a client may not send: %s|%s|%s|%s\n", what, g->params,
                   g->sysflags, g->minsize, g->maxsize); s_bad++;
        }
    }
}

#define CHECK_F(c) do { if (!(c)) { printf("BAD %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)
static long s_fz_races, s_fz_waits, s_fz_relay, s_fz_fwd;

/* BO-381: a TKRL datagram built from a live game (valid tag half the time, a member or a stranger as
 * from/to), then mutated; fed straight to the relay handler. */
static long long s_fz_vt = 1000000;
static void fuzz_relay(void)
{
    uint8_t b[2048];
    uint8_t zero[16] = { 0 };
    int k, n, pl, gi = -1, nm;
    uint32_t from = 0, to = 0, ident = (uint32_t)(rnd() % 40);
    const uint8_t *key = zero;
    struct sockaddr_in src;
    unsigned long before = s_st.relay_packets;
    for (k = 0; k < MAX_GAMES; k++)
        if (s_games[k].used && s_games[k].np > 0 && (gi < 0 || rnd() % 2))
            gi = k;
    if (gi >= 0) {
        const game_t *g = &s_games[gi];
        ident = (uint32_t)g->ident;
        if (rnd() % 8) key = g->key;
        nm = g->np;
        /* make some members capable, with synthetic addresses, so packets can be valid */
        for (k = 0; k < nm; k++) {
            sess_t *p = &s_sess[g->players[k]];
            if (!p->tkid && rnd() % 3 == 0) {
                int idx = synth_alloc();
                if (idx > 0) { p->tkid = 1; p->synth_idx = idx; p->synth = synth_net(idx); }
            }
        }
        from = s_sess[g->players[rnd() % (unsigned)nm]].synth;
        to = rnd() % 5 ? s_sess[g->players[rnd() % (unsigned)nm]].synth : 0;
    }
    if (rnd() % 6 == 0) from = (uint32_t)rnd();
    if (rnd() % 6 == 0) to = (uint32_t)rnd();
    pl = rnd() % 4 ? (int)(rnd() % 80) : (int)(rnd() % 1500);
    for (k = 0; k < pl; k++)
        b[32 + k] = (uint8_t)rnd();
    n = 32 + pl;
    memset(b, 0, 32);
    memcpy(b, "TKRL", 4);
    b[4] = 1; b[5] = rnd() % 8 ? 1 : (uint8_t)rnd();
    b[6] = (uint8_t)(ident >> 24); b[7] = (uint8_t)(ident >> 16); b[8] = (uint8_t)(ident >> 8); b[9] = (uint8_t)ident;
    memcpy(b + 10, &from, 4); memcpy(b + 14, &to, 4);
    b[19] = (uint8_t)(1000 + rnd() % 3); b[21] = (uint8_t)(1000 + rnd() % 3);
    relay_tag(key, b);
    if (rnd() % 4 == 0)                                   /* mutate */
        for (k = (int)(rnd() % 4); k >= 0; k--)
            b[rnd() % (unsigned)n] ^= (uint8_t)(1u << (rnd() % 8));
    if (rnd() % 10 == 0)
        n = (int)(rnd() % (unsigned)(n + 1));              /* truncated */
    if (rnd() % 20 == 0) {                                 /* pure noise */
        n = (int)(rnd() % 200);
        for (k = 0; k < n; k++) b[k] = (uint8_t)rnd();
    }
    memset(&src, 0, sizeof src);
    src.sin_family = AF_INET;
    src.sin_addr.s_addr = htonl(0x7f000001u);
    src.sin_port = htons((uint16_t)(2000 + rnd() % 6));
    {   /* a clock of its own: a few ms a step, now and then a jump past the 60 s of staleness */
        s_fz_vt += (long long)(rnd() % 12) + (rnd() % 400 == 0 ? 70000 : 0);
        relay_handle(b, n, &src, s_fz_vt);
    }
    s_fz_relay++;
    s_fz_fwd += s_st.relay_packets != before;
    if (s_st.relay_packets != before) {   /* a forwarded packet: its game's record is fresh and the destination was heard within RL_STALE_MS */
        rl_game_t *r = rlg_find(ident, s_fz_vt, 0);
        rl_bind_t *d;
        memcpy(&to, b + 14, 4);
        d = rl_find(ident, to, (uint16_t)(b[20] << 8 | b[21]), s_fz_vt, 0);
        if (!r || r->last_ms != s_fz_vt || s_fz_vt - r->last_ms > s_rl_game_ttl_ms ||
            !d || s_fz_vt - d->seen_ms > RL_STALE_MS) {
            printf("BAD relay: forwarded on an expired record or binding\n"); s_bad++;
        }
    }
}

/* no binding is used after RL_STALE_MS of silence and no rl_game after its TTL: the handler is fed the
 * clock vt; a probe with a valid packet for a record or binding older than that must not be forwarded.
 * Here: the structural part, a record is never "used" with an empty or foreign slot, and members stay within bounds. */
static void check_relay(void)
{
    size_t i;
    for (i = 0; i < s_rlg_n; i++)
        if (s_rlg[i].used && (s_rlg[i].nm < 0 || s_rlg[i].nm > MAX_PLAYERS)) {
            printf("BAD relay: a game record with %d members\n", s_rlg[i].nm); s_bad++;
            return;
        }
}

int main(int argc, char **argv)
{
    long iters = argc > 1 ? atol(argv[1]) : 200000, it, drops = 0, handled = 0, wire = 0;
    int i;
    char body[2048];
    signal(SIGPIPE, SIG_IGN);
    if (argc > 2)
        rs ^= (unsigned long long)atoll(argv[2]) * 0x9E3779B97F4A7C15ull;
    memset(&s_cfg, 0, sizeof s_cfg);
    s_cfg.log = quiet;
    s_cfg.port = 21810;
    MAX_SESS = NS; MAX_GAMES = 5; s_max_players = 8;
    s_idle_secs = 0; s_auth_secs = 0; s_per_ip = 0;
    s_sess = (sess_t *)calloc(NS, sizeof *s_sess);
    s_lease = (lease_t *)calloc(NS, sizeof *s_lease);
    s_games = (game_t *)calloc(5, sizeof *s_games);
    s_races = (race_t *)calloc(5, sizeof *s_races);
    s_rehost_grace_ms = 3;               /* races end within a few steps too (BO-365) */
    s_reserve_ms = 2;
    s_race_tail_ms = 3;
    st_init(NULL, NULL, NULL);
    relay_init();
    s_rl_game_ttl_ms = 100000;           /* reachable with the clock's 70 s jumps */
    {   /* the relay alone, on a game of three capable members made by hand: most packets are valid here */
        game_t *g = &s_games[0];
        long q;
        memset(g, 0, sizeof *g);
        g->used = 1; g->ident = 4242; g->np = 3;
        snprintf(g->gid, sizeof g->gid, "%s", "000000000000");
        for (i = 0; i < 16; i++) g->key[i] = (uint8_t)rnd();
        for (i = 0; i < 3; i++) {
            sess_t *p = &s_sess[i];
            p->used = 1; p->tkid = 1; p->synth_idx = synth_alloc(); p->synth = synth_net(p->synth_idx);
            g->players[i] = i;
        }
        for (q = 0; q < iters / 4 + 100; q++)
            fuzz_relay();
        CHECK_F(s_fz_fwd > 100);
        for (i = 0; i < 3; i++) { synth_free(&s_sess[i]); memset(&s_sess[i], 0, sizeof s_sess[i]); }
        memset(g, 0, sizeof *g);
    }
    for (i = 0; i < NS; i++)
        new_sess(i);
    for (it = 0; it < iters; it++) {
        int si = (int)(rnd() % NS);
        if (!s_sess[si].used)
            new_sess(si);
        if (rnd() % 40 == 0) {                              /* raw bytes on the wire */
            unsigned char raw[256];
            size_t n = 1 + rnd() % sizeof raw, k;
            for (k = 0; k < n; k++)
                raw[k] = (unsigned char)rnd();
            if (rnd() % 2 && n >= 12) {                     /* a header that is nearly right */
                memcpy(raw, pick(k_cmds, (int)(sizeof k_cmds / sizeof k_cmds[0])), 4);
                memset(raw + 4, 0, 4);
                raw[8] = raw[9] = 0;
                raw[10] = (unsigned char)(n >> 8);
                raw[11] = (unsigned char)n;
            }
            send(g_peer[si], raw, n, MSG_NOSIGNAL);
            on_data(si);
            wire++;
        } else if (rnd() % 60 == 0) {
            drop(si);
            drops++;
        } else {
            const char *cmd = rnd() % 25 ? pick(k_cmds, (int)(sizeof k_cmds / sizeof k_cmds[0])) : "zzzz";
            make_body(body, sizeof body);
            handle(si, cmd, body);
            handled++;
        }
        for (i = 0; i < NS; i++)
            if (s_sess[i].used && s_sess[i].dead)
                drop(i);
        if (it % 16 == 0)
            drain();
        if (it % 7 == 0)
            race_tick(now_ms());
        if (rnd() % 2) { fuzz_relay(); fuzz_relay(); }
        check_relay();
        check("step");
        if (s_bad) {
            printf("stopped at iteration %ld\n", it);
            return 1;
        }
        {
            int g = 0, pl = 0, k;
            for (k = 0; k < MAX_GAMES; k++)
                if (s_games[k].used) { g++; pl += s_games[k].np; s_fz_started += game_racing(&s_games[k]); }
            for (k = 0; k < MAX_GAMES; k++) if (s_games[k].used && s_games[k].priv && s_games[k].np > s_peak_priv) s_peak_priv = s_games[k].np;
            if (g > s_peak_games) s_peak_games = g;
            if (pl > s_peak_players) s_peak_players = pl;
            for (k = 0; k < MAX_GAMES; k++) s_fz_races += s_races[k].used;
            for (k = 0; k < NS; k++) s_fz_waits += s_sess[k].used && s_sess[k].wait_for;
        }
        s_throttled_total = 0;
        for (i = 0; i < NS; i++)
            s_sess[i].tok = TOK_BURST;
    }
    printf("fuzz: %ld relay datagrams (%ld forwarded); %ld handled, %ld raw, %ld drops, no inconsistency; peak %ld games, %ld players in games, %ld started-game steps, private game seen with up to %ld players, %ld race-record steps, %ld waiter steps\n", s_fz_relay, s_fz_fwd, handled, wire, drops, s_peak_games, s_peak_players, s_fz_started, s_peak_priv, s_fz_races, s_fz_waits);
    return 0;
}
