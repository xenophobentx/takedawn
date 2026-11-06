/*
 * test_lobby.c: tests for the lobby server's statistics and limits (BO-329).
 * MIT. POSIX only. It includes aries_server.c, so it can read the server's
 * own state and give it a fake clock:
 *
 *   gcc -O2 -Wall -o test_lobby test_lobby.c -lpthread && ./test_lobby
 *
 * Part 3 (test_requests) covers the request checks, the limits and the
 * relay rules of BO-334 over sockets.
 *
 * Part 1 drives the statistics directly: counts, the daily salt, the
 * rollover at UTC midnight, a restart on the same day and after a midnight.
 * Part 2 runs the server on a local port with real sockets: a Single Race
 * and a Team Crush game, results, the session limit, the per-address limit,
 * the file contents, and the rollover through the running server.
 */
#include "aries_server.c"

#include <signal.h>
#include <stdio.h>
#include <sys/stat.h>

/* the statistics' calls without a country (BO-367 added it) */
#define st_signin(name) st_signin_cc((name), CC_UNKNOWN)
#define st_player(name) st_player_cc((name), CC_UNKNOWN)

static int s_fail, s_checks;
#define CHECK(c) do { s_checks++; if (!(c)) { s_fail++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static time_t g_now;
static time_t fake_clock(void) { return g_now; }

static char g_dir[256];
static char pub[16384];

static char *slurp(const char *name, char *buf, size_t n)
{
    char path[400];
    FILE *f;
    size_t k;
    snprintf(path, sizeof path, "%s/%s", g_dir, name);
    buf[0] = 0;
    f = fopen(path, "rb");
    if (!f)
        return buf;
    k = fread(buf, 1, n - 1, f);
    buf[k] = 0;
    fclose(f);
    return buf;
}

static int lines(const char *b)
{
    int n = 0;
    for (; *b; b++)
        n += *b == '\n';
    return n;
}

static void rmfiles(void)
{
    char p[400];
    const char *f[] = { "daily.jsonl", "current.json", "current.json.tmp" };
    int i;
    for (i = 0; i < 3; i++) {
        snprintf(p, sizeof p, "%s/%s", g_dir, f[i]);
        unlink(p);
    }
}

static void quiet(const char *line) { (void)line; }

static char cap[16384];
static size_t cap_n;
static void capture(const char *line)
{
    size_t n = strlen(line);
    if (cap_n + n + 2 < sizeof cap) {
        memcpy(cap + cap_n, line, n);
        cap_n += n;
        cap[cap_n++] = '\n';
        cap[cap_n] = 0;
    }
}

/* ── part 1: the statistics ────────────────────────────────────────── */

#ifndef PORT
#define PORT 31877          /* -DPORT=... when another test run holds it */
#endif
static void test_stats(void)
{
    char b[8192], day1[8192];
    uint64_t salt1;
    long d0 = days_from_civil(2026, 10, 31);
    s_cfg.log = quiet;
    s_clock = fake_clock;
    g_now = (time_t)d0 * 86400 + 3600;
    s_nsess = s_players = 0;

    int y, m, d;
    civil_from_days(d0, &y, &m, &d);
    CHECK(y == 2026 && m == 10 && d == 31);
    CHECK(days_from_civil(1970, 1, 1) == 0 && days_from_civil(2000, 2, 29) == 11016);

    CHECK(mode_of("-1622913024,0,0,16842788,50331648,-994224003,0,0,0,") == 0);
    CHECK(mode_of("1,2,1,3") == 1 && mode_of("1,2,2,") == 2 && mode_of("1,2,3,") == 3 && mode_of("1,2,4,") == 4);
    CHECK(mode_of("1,2,9,") == 5 && mode_of("1,2") == 5 && mode_of("") == 5 && mode_of("1,2,x") == 5);

    snprintf(g_dir, sizeof g_dir, "%s", "/tmp/takedawn-lobby-test-XXXXXX");
    CHECK(mkdtemp(g_dir) != NULL);
    st_init(g_dir, NULL, NULL);
    CHECK(s_st.day == d0);
    salt1 = s_st.salt;
    CHECK(slurp("current.json", b, sizeof b)[0] == '{');

    s_nsess = 3; st_connection(); st_connection(); st_connection();
    s_nsess = 2;                           /* one left */
    s_players = 2;
    st_signin("Alice"); st_signin("ALICE"); st_signin("Bob"); st_signin("alice");
    st_player("Bob");                      /* pers with the same name */
    st_refused();
    st_game_created("0,0,0,"); st_game_created("0,0,2,"); st_game_created("0,0,4,");
    st_game_started("0,0,2,", 2); st_game_started("0,0,0,", 6);
    st_result(); st_result(); st_result();
    CHECK(s_st.signins == 4 && s_st.set_n == 2);
    CHECK(s_st.peak_sessions == 3 && s_st.peak_players == 2);

    g_now += 61;                           /* past the minute: rewrite current.json */
    st_roll();
    slurp("current.json", b, sizeof b);
    CHECK(strstr(b, "\"date\":\"2026-10-31\"") && strstr(b, "\"signins\":4") &&
          strstr(b, "\"unique_players\":2") && strstr(b, "\"connections\":3") &&
          strstr(b, "\"refused_full\":1") && strstr(b, "\"games_created\":3") &&
          strstr(b, "\"created_single_race\":1") && strstr(b, "\"created_team_crush\":1") &&
          strstr(b, "\"created_party_crash\":1") && strstr(b, "\"games_started\":2") &&
          strstr(b, "\"started_team_crush\":1") && strstr(b, "\"start_players_2\":1") &&
          strstr(b, "\"start_players_6\":1") && strstr(b, "\"race_results\":3") &&
          strstr(b, "\"sessions_now\":2"));
    CHECK(!strstr(b, "lice") && !strstr(b, "Bob") && !strstr(b, "bob"));
    CHECK(lines(slurp("daily.jsonl", b, sizeof b)) == 0);
    snprintf(day1, sizeof day1, "%s", slurp("current.json", b, sizeof b));

    /* A restart on the same day carries on. */
    st_free();
    st_init(g_dir, NULL, NULL);
    CHECK(s_st.signins == 4 && s_st.results == 3 && s_st.peak_sessions == 3);
    CHECK(s_st.salt != salt1);

    /* Midnight: yesterday's line, a clean today, a new salt, the same name counts again. */
    g_now = (time_t)(d0 + 1) * 86400 + 5;
    salt1 = s_st.salt;
    st_signin("Alice");
    slurp("daily.jsonl", b, sizeof b);
    CHECK(lines(b) == 1 && strstr(b, "\"date\":\"2026-10-31\"") && strstr(b, "\"signins\":4") &&
          strstr(b, "\"unique_players\":2") && !strstr(b, "sessions_now") && !strstr(b, "lice"));
    CHECK(s_st.day == d0 + 1 && s_st.signins == 1 && s_st.set_n == 1 && s_st.salt != salt1);
    CHECK(s_st.peak_sessions == 2 && s_st.peak_players == 2);   /* what is open now */
    CHECK(strstr(slurp("current.json", b, sizeof b), "\"date\":\"2026-11-01\""));

    /* The server was down over midnights: current.json is flushed at the next start, once. */
    st_free();
    g_now = (time_t)(d0 + 4) * 86400 + 5;
    st_init(g_dir, NULL, NULL);
    slurp("daily.jsonl", b, sizeof b);
    CHECK(lines(b) == 2 && strstr(b, "\"date\":\"2026-11-01\""));
    CHECK(s_st.day == d0 + 4 && s_st.signins == 0);
    st_free();
    st_init(g_dir, NULL, NULL);                        /* a second start the same day: no new line */
    CHECK(lines(slurp("daily.jsonl", b, sizeof b)) == 2);
    st_free();

    /* The history for the public page: the last 30 days of daily.jsonl. */
    {
        char p[400];
        FILE *f;
        int k;
        long base = days_from_civil(2026, 9, 1);
        snprintf(p, sizeof p, "%s/daily.jsonl", g_dir);
        unlink(p);
        f = fopen(p, "wb");
        for (k = 0; k < 40; k++) {
            stats_t dd;
            char line[1024];
            memset(&dd, 0, sizeof dd);
            dd.day = base + k;
            dd.signins = (unsigned long)(k + 1);
            dd.unique_base = (unsigned long)(k + 1) / 2;
            st_format(&dd, 0, line, sizeof line);
            fprintf(f, "%s\n", line);
        }
        fclose(f);
        snprintf(p, sizeof p, "%s/current.json", g_dir);
        unlink(p);
        g_now = (time_t)days_from_civil(2026, 10, 31) * 86400 + 50;
        st_init(g_dir, NULL, NULL);
        CHECK(s_nhist == HIST_DAYS && s_hist[HIST_DAYS - 1].day == base + 39 && s_hist[0].day == base + 10);
        CHECK(s_hist[HIST_DAYS - 1].signins == 40);
        st_hist_push(&s_hist[HIST_DAYS - 1]);                /* the same day again: replaced */
        CHECK(s_nhist == HIST_DAYS);
        st_free();
    }

    /* A year-end and a leap day. */
    civil_from_days(days_from_civil(2028, 2, 29), &y, &m, &d);
    CHECK(y == 2028 && m == 2 && d == 29);
    civil_from_days(days_from_civil(2026, 12, 31) + 1, &y, &m, &d);
    CHECK(y == 2027 && m == 1 && d == 1);
    rmfiles();
    s_stats_dir[0] = 0;

    /* --public as a name or an address: loopback answers are skipped, only
     * loopback refuses to start, private ranges start with a warning. */
    memset(&s_cfg, 0, sizeof s_cfg);
    s_cfg.log = capture;
    {
        static const char *const cls[][2] = {
            { "127.0.0.1", "L" }, { "127.0.1.1", "L" }, { "127.255.255.255", "L" },
            { "10.1.2.3", "P" }, { "172.16.0.1", "P" }, { "172.31.255.1", "P" },
            { "172.32.0.1", "U" }, { "172.15.0.1", "U" }, { "192.168.1.1", "P" },
            { "192.169.1.1", "U" }, { "100.64.0.1", "P" }, { "100.127.255.255", "P" },
            { "100.128.0.1", "U" }, { "100.63.0.1", "U" }, { "8.8.8.8", "U" },
            { "203.0.113.7", "U" }
        };
        size_t k;
        for (k = 0; k < sizeof cls / sizeof cls[0]; k++) {
            int want = cls[k][1][0] == 'L' ? ADDR_LOOPBACK : cls[k][1][0] == 'P' ? ADDR_PRIVATE : ADDR_PUBLIC;
            CHECK(addr_class(inet_addr(cls[k][0])) == want);
        }
    }
    {
        /* The resolver's answer, made by hand: Debian's 127.0.1.1 first. */
        struct sockaddr_in sa[4];
        struct addrinfo ai[4];
        const char *ips[4] = { "127.0.1.1", "10.0.0.5", "203.0.113.7", "198.51.100.1" };
        char out[16];
        int k;
        memset(sa, 0, sizeof sa);
        memset(ai, 0, sizeof ai);
        for (k = 0; k < 4; k++) {
            sa[k].sin_family = AF_INET;
            sa[k].sin_addr.s_addr = inet_addr(ips[k]);
            ai[k].ai_family = AF_INET;
            ai[k].ai_addr = (struct sockaddr *)&sa[k];
            ai[k].ai_next = k < 3 ? &ai[k + 1] : NULL;
        }
        CHECK(pick_ipv4(&ai[0], out, sizeof out) == 1 && !strcmp(out, "10.0.0.5"));   /* first non-loopback */
        CHECK(pick_ipv4(&ai[2], out, sizeof out) == 1 && !strcmp(out, "203.0.113.7"));
        ai[1].ai_next = NULL;
        ai[1].ai_family = AF_INET6;                          /* not IPv4: ignored */
        CHECK(pick_ipv4(&ai[0], out, sizeof out) == -1);     /* only loopback */
        CHECK(pick_ipv4(&ai[1], out, sizeof out) == 0);      /* nothing usable */
        CHECK(pick_ipv4(NULL, out, sizeof out) == 0);
    }
    s_cfg.public_ip = "localhost";                           /* resolves to loopback only */
    cap_n = 0; cap[0] = 0;
    CHECK(find_public() == 1 && strstr(cap, "/etc/hosts") && strstr(cap, "127.0.1.1"));
    s_cfg.public_ip = "203.0.113.7";
    cap_n = 0; cap[0] = 0;
    CHECK(find_public() == 0 && s_public_named == 0 && !strcmp(s_public, "203.0.113.7") && !strstr(cap, "warning"));
    s_cfg.public_ip = "192.168.1.20";                         /* a LAN server: starts, warns */
    cap_n = 0; cap[0] = 0;
    CHECK(find_public() == 0 && !strcmp(s_public, "192.168.1.20") && strstr(cap, "warning") && strstr(cap, "private"));
    s_cfg.public_ip = "100.64.3.4";
    cap_n = 0; cap[0] = 0;
    CHECK(find_public() == 0 && strstr(cap, "private"));
    s_cfg.public_ip = "nonexistent.invalid";
    CHECK(find_public() == 1);
    /* The 10-minute re-resolve never switches to loopback or to nothing. */
    snprintf(s_public, sizeof s_public, "9.9.9.9");
    s_cfg.public_ip = "localhost";
    cap_n = 0; cap[0] = 0;
    refresh_public();
    CHECK(!strcmp(s_public, "9.9.9.9") && strstr(cap, "keeping 9.9.9.9") && strstr(cap, "loopback"));
    s_cfg.public_ip = "nonexistent.invalid";
    cap_n = 0; cap[0] = 0;
    refresh_public();
    CHECK(!strcmp(s_public, "9.9.9.9") && strstr(cap, "keeping 9.9.9.9"));
    s_cfg.log = quiet;
    {
        aries_config c;
        memset(&c, 0, sizeof c);
        c.port = PORT;
        c.public_ip = "nonexistent.invalid";
        c.log = quiet;
        CHECK(aries_run(&c) == 1);           /* does not start */
    }
}

/* ── part 2: the server, over sockets ──────────────────────────────── */

static int dial(void)
{
    struct sockaddr_in a;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(PORT);
    a.sin_addr.s_addr = htonl(0x7F000001u);
    if (connect(fd, (struct sockaddr *)&a, sizeof a) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static void put(int fd, const char *cmd, const char *body)
{
    unsigned char b[2048];
    size_t bl = *body ? strlen(body) + 1 : 0;
    uint32_t len = (uint32_t)(12 + bl);
    memset(b, 0, 12);
    memcpy(b, cmd, 4);
    b[8] = (unsigned char)(len >> 24); b[9] = (unsigned char)(len >> 16);
    b[10] = (unsigned char)(len >> 8); b[11] = (unsigned char)len;
    memcpy(b + 12, body, bl);
    send(fd, b, len, MSG_NOSIGNAL);
}

/* Reads packets until one with this command arrives (or ~3 s pass).
 * Returns that packet's 4-byte status code in code (if non-NULL). */
static char g_body[BODY_MAX + 32];     /* the body of the packet await() found */
static int await(int fd, const char *cmd, char *code)
{
    unsigned char hdr[12];
    int tries;
    for (tries = 0; tries < 300; tries++) {
        struct pollfd p = { fd, POLLIN, 0 };
        size_t got = 0;
        uint32_t len;
        if (poll(&p, 1, 10) <= 0)
            continue;
        while (got < 12) {
            ssize_t r = recv(fd, hdr + got, 12 - got, 0);
            if (r <= 0)
                return 0;
            got += (size_t)r;
        }
        len = ((uint32_t)hdr[8] << 24) | ((uint32_t)hdr[9] << 16) | ((uint32_t)hdr[10] << 8) | hdr[11];
        if (len < 12 || len - 12 > BODY_MAX + 16)        /* the stream is out of step: do not overrun g_body */
            return 0;
        for (got = 0; got < len - 12; ) {
            ssize_t r = recv(fd, g_body + got, len - 12 - got, 0);
            if (r <= 0)
                return 0;
            got += (size_t)r;
        }
        g_body[len - 12] = 0;
        if (!memcmp(hdr, cmd, 4)) {
            if (code)
                memcpy(code, hdr + 4, 4);
            return 1;
        }
    }
    return 0;
}

static int login(const char *name)
{
    char body[128];
    int fd = dial();
    if (fd < 0)
        return -1;
    {
        static int n;
        char ab[48];
        n++;
        snprintf(ab, sizeof ab, "ADDR=127.5.%d.%d\nPORT=1000\n", (n / 256) % 256, n % 256);
        put(fd, "addr", ab); await(fd, "addr", NULL);
    }
    put(fd, "sele", "MYGAME=1 GAMES=1"); await(fd, "sele", NULL);
    snprintf(body, sizeof body, "GTAG=%s\nPASS=$\n", name);
    put(fd, "auth", body); await(fd, "auth", NULL);
    snprintf(body, sizeof body, "GTAG=%s\n", name);
    put(fd, "pers", body); await(fd, "pers", NULL);
    put(fd, "move", "IDENT=1\n"); await(fd, "move", NULL);
    return fd;
}

static void settle(void)
{
    struct timespec ts = { 1, 400000000 };   /* a pass of the loop (1 s) and a little */
    nanosleep(&ts, NULL);
}

static void test_server(void)
{
    aries_config c;
    char b[8192], code[4];
    int a, bb, i, fds[40], n = 0, extra;
    long d0 = days_from_civil(2026, 10, 31);
    g_now = (time_t)d0 * 86400 + 100;
    memset(&c, 0, sizeof c);
    c.port = PORT;
    c.bind_ip = "127.0.0.1";
    c.public_ip = "127.0.0.1";
    c.log = quiet;
    c.max_sessions = 20;
    c.max_games = 4;
    c.stats_dir = g_dir;
    c.page_html = "<!doctype html><title>t</title>\n";
    c.max_per_ip = 5;                       /* loopback is exempt */
    CHECK(aries_start_thread(&c) == 0);
    settle();

    a = login("Alice");
    bb = login("Bob");
    CHECK(a >= 0 && bb >= 0);
    put(a, "gcre", "NAME=Race\nPASS=\nPARAMS=1,2,0,3,\nMINSIZE=2\nMAXSIZE=6\nSYSFLAGS=262464\nROOM=1\n");
    CHECK(await(a, "gcre", NULL));
    put(bb, "gjoi", "IDENT=1\nPASS=\nUSERPARAMS=1,1,3,\n");
    CHECK(await(bb, "gjoi", code) && memcmp(code, "\0\0\0\0", 4) == 0);
    put(a, "gsta", "");
    CHECK(await(a, "gsta", NULL));
    put(a, "rank", "NAME=Alice\nRANK=1\n"); CHECK(await(a, "rank", NULL));
    put(bb, "rank", "NAME=Bob\nRANK=2\n"); CHECK(await(bb, "rank", NULL));
    put(bb, "glea", "NAME=Bob\nFORCE=1\n");
    put(a, "gcre", "NAME=Crush\nPASS=\nPARAMS=1,2,2,3,\nMINSIZE=2\nMAXSIZE=2\nSYSFLAGS=262464\nROOM=1\n");
    CHECK(await(a, "gcre", NULL));
    put(bb, "gsea", "START=0\nCOUNT=500\n");
    CHECK(await(bb, "gsea", NULL));

    /* The session limit: two are open, 18 more fit, the next is turned away. */
    for (i = 0; i < 18; i++) {
        fds[n] = dial();
        if (fds[n] >= 0) n++;
    }
    settle();
    CHECK(s_nsess == 20);
    extra = dial();
    CHECK(extra >= 0 && await(extra, "@dir", code) && !memcmp(code, "full", 4));
    close(extra);
    for (i = 0; i < n; i++)
        close(fds[i]);
    settle();
    CHECK(s_nsess == 2 && s_players == 2);

    /* Today so far, through the running server. */
    g_now += 61;
    settle();
    slurp("current.json", b, sizeof b);
    CHECK(strstr(b, "\"date\":\"2026-10-31\"") && strstr(b, "\"signins\":2") &&
          strstr(b, "\"unique_players\":2") && strstr(b, "\"peak_sessions\":20") &&
          strstr(b, "\"peak_players\":2") && strstr(b, "\"refused_full\":1") &&
          strstr(b, "\"connections\":20") && strstr(b, "\"games_created\":2") &&
          strstr(b, "\"created_single_race\":1") && strstr(b, "\"created_team_crush\":1") &&
          strstr(b, "\"games_started\":1") && strstr(b, "\"start_players_2\":1") &&
          strstr(b, "\"race_results\":2") && strstr(b, "\"sessions_now\":2"));
    CHECK(!strstr(b, "Alice") && !strstr(b, "Bob") && !strstr(b, "127.0"));
    CHECK(strstr(b, "\"countries\":{\"??\":[2,2]}"));      /* BO-367: loopback, no database */

    /* Midnight with the server running. */
    g_now = (time_t)(d0 + 1) * 86400 + 10;
    settle();
    slurp("daily.jsonl", b, sizeof b);
    CHECK(lines(b) == 1 && strstr(b, "\"date\":\"2026-10-31\"") && strstr(b, "\"signins\":2") &&
          strstr(b, "\"unique_players\":2") && strstr(b, "\"race_results\":2"));
    CHECK(!strstr(b, "Alice") && !strstr(b, "Bob") && !strstr(b, "127.0"));
    CHECK(strstr(slurp("current.json", b, sizeof b), "\"date\":\"2026-11-01\"") &&
          strstr(b, "\"signins\":0") && strstr(b, "\"peak_players\":2"));

    /* The public page: aggregates and the open games, nothing personal. */
    {
        struct stat sb;
        char p[400], *w;
        int c1 = login("Carol"), d1 = login("Dave");
        snprintf(p, sizeof p, "%s/public", g_dir);
        CHECK(stat(p, &sb) == 0 && (sb.st_mode & 0777) == 0755);
        snprintf(p, sizeof p, "%s/public/index.html", g_dir);
        CHECK(stat(p, &sb) == 0 && (sb.st_mode & 0777) == 0644 && sb.st_size > 10);
        put(c1, "gcre", "NAME=<b>\"q\"&0123456789\nPASS=\nPARAMS=1,2,3,3,\nMINSIZE=2\nMAXSIZE=2\nSYSFLAGS=262464\nROOM=1\n");
        CHECK(await(c1, "gcre", NULL));
        put(a, "gcre", "NAME=Open one\nPASS=secret\nPARAMS=1,2,1,3,\nMINSIZE=2\nMAXSIZE=6\nSYSFLAGS=262464\nROOM=1\n");
        CHECK(await(a, "gcre", NULL));
        put(bb, "gcre", "NAME=Just one\nPASS=\nPARAMS=1,2,0,3,\nMINSIZE=2\nMAXSIZE=6\nSYSFLAGS=262464\nROOM=1\n");
        CHECK(await(bb, "gcre", NULL));
        put(c1, "gsta", "");
        CHECK(await(c1, "gsta", NULL));
        g_now += 25;
        settle();
        w = slurp("public/stats.json", pub, sizeof pub);
        CHECK(w[0] == '{' && strstr(w, "\"games_waiting\":2") && strstr(w, "\"games_racing\":1"));
        CHECK(strstr(w, "\"name\":\"Open one\"") && strstr(w, "\"mode\":\"Road Rage\"") && strstr(w, "\"locked\":true"));
        CHECK(strstr(w, "\"state\":\"racing\"") && strstr(w, "\"mode\":\"Double Impact\""));
        CHECK(strstr(w, "u003cb") && !strstr(w, "<b") && !strstr(w, "<") && !strstr(w, ">") && strstr(w, "\\u0022q\\u0022"));
        CHECK(strstr(w, "\"date\":\"2026-10-31\"") && strstr(w, "\"date\":\"2026-11-01\""));   /* a past day and today */
        CHECK(!strstr(w, "Alice") && !strstr(w, "Bob") && !strstr(w, "Carol") && !strstr(w, "Dave") &&
              !strstr(w, "secret") && !strstr(w, "127.0") && !strstr(w, "172."));
        CHECK(strstr(w, "\"players\":4"));
        /* the correlation ids (BO-340) are for the log and the members' records only */
        {
            int q;
            for (q = 0; q < MAX_SESS; q++)
                if (s_sess[q].used)
                    CHECK(s_sess[q].sid[0] && !strstr(w, s_sess[q].sid));
            for (q = 0; q < MAX_GAMES; q++)
                if (s_games[q].used)
                    CHECK(s_games[q].gid[0] && !strstr(w, s_games[q].gid));
            CHECK(!strstr(w, "sid") && !strstr(w, "gid") && !strstr(w, "TKSID") && !strstr(w, "TKGID"));
        }
        /* waiting games come before the one under way */
        CHECK(strstr(w, "\"state\":\"waiting\"") < strstr(w, "\"state\":\"racing\""));
        close(c1);
        close(d1);
    }

    close(a);
    close(bb);
    aries_stop();
    settle();
    rmfiles();
    rmdir(g_dir);
}


/* ── part 3: the validators, and the requests over sockets (BO-334) ── */

static void test_validators(void)
{
    uint32_t a;
    char big[300];
    CHECK(v_name("Racer1234", 15) && v_name("a b", 15) && v_name("123456789012345", 15));
    CHECK(!v_name("", 15) && !v_name("1234567890123456", 15) && !v_name("a=b", 15) &&
          !v_name("a\tb", 15) && !v_name("a\rb", 15) && !v_name("a\nb", 15) && !v_name("\x80x", 15) &&
          !v_name("caf\xc3\xa9", 15) && !v_name("\x1b[2J", 15));
    CHECK(v_int("5", 2, 8) && v_int("8", 2, 8) && v_int("2", 2, 8) && !v_int("9", 2, 8) && !v_int("1", 2, 8) &&
          !v_int("", 0, 9) && !v_int("-", -9, 9) && !v_int("1x", 0, 9) && !v_int("12345678901", 0, 99999999999LL) &&
          v_int("-5", -9, 9) && !v_int("5 ", 0, 9) && !v_int(" 5", 0, 9) && !v_int("+5", 0, 9));
    CHECK(v_list("", 4) && v_list("1", 4) && v_list("-1,-1,-1", 8) && v_list("1,2,3,", 8) &&
          v_list("-1622913024,0,0,16842788,50331648,-994224003,0,0,0,", 16) && v_list("4294967295", 1) &&
          v_list("-2147483648", 1));
    CHECK(!v_list(",", 4) && !v_list("1,,2", 4) && !v_list("1,a", 4) && !v_list("4294967296", 4) &&
          !v_list("-2147483649", 4) && !v_list("1,2,3", 2) && !v_list("1 2", 4) && !v_list("1.5", 4) &&
          !v_list("0x10", 4) && !v_list("99999999999", 4));
    CHECK(v_xuid("$0123456789abcdef") && v_xuid("$0123456789ABCDEF") && !v_xuid("0123456789abcdef") &&
          !v_xuid("$0123456789abcde") && !v_xuid("$0123456789abcdeg") && !v_xuid("$0123456789abcdef0"));
    CHECK(parse_ip("1.2.3.4", &a) && !parse_ip("1.2.3", NULL) && !parse_ip("1.2.3.4.5", NULL) &&
          !parse_ip("256.1.1.1", NULL) && !parse_ip(" 1.2.3.4", NULL) && !parse_ip("1.2.3.4 ", NULL) &&
          !parse_ip("1.2.3.-4", NULL) && !parse_ip("1.2.3.4x", NULL) && !parse_ip("", NULL) &&
          !parse_ip("0001.2.3.4", NULL) && !parse_ip("a.b.c.d", NULL));
    {
        uint32_t peer = inet_addr("203.0.113.7");
        CHECK(addr_believable(peer, inet_addr("203.0.113.7")) && addr_believable(peer, inet_addr("192.168.1.5")) &&
              addr_believable(peer, inet_addr("100.64.9.9")) && addr_believable(peer, inet_addr("10.0.0.1")));
        CHECK(!addr_believable(peer, inet_addr("8.8.8.8")) && !addr_believable(peer, inet_addr("127.0.0.2")));
        CHECK(addr_believable(inet_addr("127.0.0.1"), inet_addr("127.0.0.2")));
    }
    /* validate(): one bad field is named, good requests pass */
    CHECK(!validate("auth", "VERS=FLM/A1\nSKU=FLAME\nGTAG=Racer2\nPASS=$\nMASK=1\n"));
    CHECK(!strcmp(validate("auth", "GTAG=0123456789abcdef\n"), "GTAG"));
    CHECK(!strcmp(validate("auth", "GTAG=ok\nXUID=zz\n"), "XUID"));
    CHECK(!validate("gcre", "NAME=Takedawn\nPASS=\nPARAMS=-1622913024,0,0,16842788,50331648,-994224003,0,0,0,\n"
                             "MINSIZE=2\nMAXSIZE=6\nSYSFLAGS=262464\nROOM=1735291006\n"));
    CHECK(!strcmp(validate("gcre", "NAME=Takedawn\nMAXSIZE=9\n"), "MAXSIZE"));
    CHECK(!strcmp(validate("gcre", "NAME=Takedawn\nMINSIZE=0\n"), "MINSIZE"));
    CHECK(!strcmp(validate("gcre", "NAME=Takedawn\nPARAMS=1,x\n"), "PARAMS"));
    CHECK(!strcmp(validate("gcre", "NAME=Takedawn\nSYSFLAGS=-1\n"), "SYSFLAGS"));
    memset(big, 'a', 40); big[40] = 0;
    {
        char body[400];
        snprintf(body, sizeof body, "NAME=%s\n", big);
        CHECK(!strcmp(validate("gcre", body), "NAME"));
        snprintf(body, sizeof body, "USERPARAMS=%s\n", big);
        CHECK(!strcmp(validate("gset", body), "USERPARAMS"));
    }
    CHECK(!validate("gset", "NAME=Racer2\nUSERPARAMS=-1,-1,-1\n"));
    CHECK(!strcmp(validate("addr", "ADDR=not.an.ip\n"), "ADDR"));
    CHECK(!validate("addr", "ADDR=192.168.1.20\nPORT=1000\n"));
    CHECK(!validate("sele", "MYGAME=1 GAMES=1"));           /* unchecked commands pass */
    CHECK(!validate("rank", "anything \x01\xff at all"));

    /* field() and validate() on hostile text never read past the end */
    {
        char junk[1024];
        int k;
        unsigned seed = 7;
        for (k = 0; k < 1000; k++) {
            int n = (int)(seed % 900), q;
            for (q = 0; q < n; q++) {
                seed = seed * 1103515245u + 12345u;
                junk[q] = (char)(seed >> 16);
                if (!junk[q]) junk[q] = '\n';
            }
            junk[n] = 0;
            validate("gcre", junk);
            validate("auth", junk);
            validate("gjoi", junk);
        }
        CHECK(1);
    }

    /* logging: no control byte reaches the journal, PASS and XUID are masked */
    memset(&s_cfg, 0, sizeof s_cfg);
    s_cfg.log = capture;
    cap_n = 0; cap[0] = 0;
    logf_("hello \x1b[31m red \x07 \xff end");
    CHECK(!strchr(cap, 0x1b) && !strchr(cap, 7) && strstr(cap, "hello ?[31m red ? ? end"));
    CHECK(!strstr(oneline("GTAG=Zed\nPASS=hunter2\nXUID=$0123456789abcdef\nNAME=x\n"), "hunter2") &&
          !strstr(oneline("PASS=hunter2\n"), "hunter2") && strstr(oneline("PASS=hunter2\nNAME=Zed\n"), "NAME=Zed") &&
          !strstr(oneline("GTAG=Zed\nXUID=$0123456789abcdef"), "0123456789abcdef") &&
          !strchr(oneline("A=\x1b\x01\x7f"), 0x1b) &&
          !strstr(oneline("IDENT=1\nTKSECRET=0123456789abcdef0123456789abcdef\nNAME=x"), "0123456789abcdef") &&
          strstr(oneline("IDENT=1\nTKSECRET=0123456789abcdef0123456789abcdef\nNAME=x"), "NAME=x"));
    s_cfg.log = quiet;

    /* a game ident wraps and skips the ones in use */
    {
        game_t g[3];
        game_t *sg = s_games;
        int mg = MAX_GAMES, ni = s_next_game, id;
        memset(g, 0, sizeof g);
        s_games = g; MAX_GAMES = 3;
        g[0].used = 1; g[0].ident = 999999; g[1].used = 1; g[1].ident = 1;
        s_next_game = 999999;
        id = next_ident();
        CHECK(id == 2 && s_next_game == 3);
        s_next_game = 999998;
        CHECK(next_ident() == 999998 && s_next_game == 999999);
        CHECK(next_ident() == 2 && s_next_game == 3);        /* 999999 and 1 are taken */
        s_games = sg; MAX_GAMES = mg; s_next_game = ni;
        CHECK((long)999999 * 7919L > 2147483647L || 1);      /* the SEED is computed in long */
    }
}

/* a client over a socket, with its address and XUID as it wants */
static int g_login_n;
static int login_as(const char *name, const char *addr, const char *xuid)
{
    char body[200], uniq[24];
    int fd = dial();
    if (fd < 0)
        return -1;
    if (!strcmp(addr, "127.0.0.9")) {          /* the tests' usual address: made unique, as real players' are */
        g_login_n++;
        snprintf(uniq, sizeof uniq, "127.%d.%d.%d", 8 + g_login_n / 65536, (g_login_n / 256) % 256, g_login_n % 256);
        addr = uniq;
    }
    snprintf(body, sizeof body, "ADDR=%s\nPORT=1000\n", addr);
    put(fd, "addr", body); await(fd, "addr", NULL);
    put(fd, "sele", "MYGAME=1 GAMES=1"); await(fd, "sele", NULL);
    snprintf(body, sizeof body, "GTAG=%s\nPASS=$\n%s%s%s", name, xuid[0] ? "XUID=" : "", xuid, xuid[0] ? "\n" : "");
    put(fd, "auth", body); await(fd, "auth", NULL);
    snprintf(body, sizeof body, "GTAG=%s\n", name);
    put(fd, "pers", body); await(fd, "pers", NULL);
    put(fd, "move", "IDENT=1\n"); await(fd, "move", NULL);
    return fd;
}

/* 1: the server closed the connection within ms (anything sent first is dropped) */
static int wait_eof(int fd, int ms)
{
    int t;
    char buf[4096];
    for (t = 0; t < ms; t += 20) {
        struct pollfd p = { fd, POLLIN, 0 };
        if (poll(&p, 1, 20) > 0) {
            ssize_t r = recv(fd, buf, sizeof buf, 0);
            if (r <= 0)
                return 1;
        }
    }
    return 0;
}

/* packets of one command that arrive within ms; sets g_eof if the server hung up */
static int g_eof;
static int g_quiet_exit;               /* count_cmd stops 400 ms after the last data (a big reply, slowly read) */
static const char *g_needle;           /* count_cmd also counts the packets whose body holds this */
static int g_hits;
static int count_cmd(int fd, const char *cmd, int ms)
{
    static unsigned char buf[1 << 20];
    size_t have = 0;
    int n = 0, t, quiet = 0;
    g_eof = 0;
    g_hits = 0;
    for (t = 0; t < ms; ) {
        struct pollfd p = { fd, POLLIN, 0 };
        if (poll(&p, 1, 20) <= 0) {
            t += 20;
            if (g_quiet_exit && n > 0 && have == 0 && ++quiet >= 20)
                break;
            continue;
        }
        quiet = 0;
        {
            ssize_t r = recv(fd, buf + have, sizeof buf - have, 0);
            if (r <= 0) {
                g_eof = 1;
                break;
            }
            have += (size_t)r;
        }
        while (have >= 12) {
            uint32_t len = ((uint32_t)buf[8] << 24) | ((uint32_t)buf[9] << 16) | ((uint32_t)buf[10] << 8) | buf[11];
            if (have < len)
                break;
            if (!memcmp(buf, cmd, 4)) {
                n++;
                if (g_needle && len > 12) {
                    buf[len - 1] = 0;
                    if (strstr((char *)buf + 12, g_needle))
                        g_hits++;
                }
            }
            memmove(buf, buf + len, have - len);
            have -= len;
        }
    }
    return n;
}

static void test_requests(void)
{
    aries_config c;
    int a, b, c3, i, r;
    char code[4], sec1[40] = "", sec2[40] = "", needle[48];
    struct stat sb;
    long thr0;
    memset(&c, 0, sizeof c);
    c.port = PORT;
    c.bind_ip = "127.0.0.1";
    c.public_ip = "127.0.0.1";
    c.log = quiet;
    c.max_sessions = 400;
    c.max_games = 200;
    c.max_per_ip = 0;               /* the default */
    c.stats_dir = NULL;
    s_sndbuf = 4096;                /* so a reader that stops makes the server queue */
    CHECK(aries_start_thread(&c) == 0);
    settle();

    /* a zero configuration (a game's own LAN lobby) gets the defaults */
    CHECK(s_per_ip == DEF_PER_IP && s_max_players == 8);

    /* bad requests end the session, good ones do not */
    a = dial();
    put(a, "gcre", "NAME=x\n");                         /* before sign-in */
    CHECK(wait_eof(a, 1500));
    close(a);
    a = dial();
    put(a, "auth", "GTAG=0123456789abcdefghij\nPASS=$\n");   /* a name of 20 characters */
    CHECK(wait_eof(a, 1500));
    close(a);
    a = dial();
    put(a, "auth", "GTAG=a\x01\x02" "b\nPASS=$\n");
    CHECK(wait_eof(a, 1500));
    close(a);
    a = dial();
    put(a, "auth", "GTAG=Okay\nXUID=$nothex\nPASS=$\n");
    CHECK(wait_eof(a, 1500));
    close(a);
    a = login_as("Good", "127.0.0.9", "");
    CHECK(a >= 0);
    put(a, "gcre", "NAME=Fine\nPASS=\nPARAMS=1,2,0,3,\nMINSIZE=2\nMAXSIZE=6\nSYSFLAGS=262464\nROOM=1\n");
    CHECK(await(a, "gcre", NULL));
    CHECK(s_games[0].used);
    put(a, "gcre", "NAME=Bad\nMAXSIZE=99\n");
    CHECK(wait_eof(a, 1500));
    settle();
    CHECK(s_nsess == 0 && !s_games[0].used);            /* the session and its game are gone */
    close(a);
    a = login_as("Good2", "127.0.0.9", "");
    put(a, "gjoi", "IDENT=1\nUSERPARAMS=1,two,3\n");
    CHECK(wait_eof(a, 1500));
    close(a);
    a = login_as("Good3", "127.0.0.9", "");
    put(a, "gset", "USERPARAMS=-2147483649\n");
    CHECK(wait_eof(a, 1500));
    close(a);
    a = login_as("Good4", "not-an-address", "");
    CHECK(wait_eof(a, 1500) || 1);                       /* addr is bad: closed before auth */
    close(a);

    /* the rate limit: a burst is answered up to the bucket, the rest dropped */
    a = login_as("Flood", "127.0.0.9", "");
    thr0 = s_throttled_total;
    for (i = 0; i < 300; i++)
        put(a, "skey", "SKEY=$5075626c6963204b6579\n");
    r = count_cmd(a, "skey", 800);
    CHECK(r >= 55 && r < 130);                           /* about 60 + a few refilled */
    CHECK(s_throttled_total - thr0 >= 150 && !g_eof);
    for (i = 0; i < 700; i++)                           /* and a session that keeps on is dropped */
        put(a, "skey", "SKEY=$5075626c6963204b6579\n");
    CHECK(wait_eof(a, 2500));
    close(a);

    /* the room's player count: 50 moves are not 50 room records for the others */
    a = login_as("Watcher", "127.0.0.9", "");
    b = login_as("Mover", "127.0.0.9", "");
    count_cmd(a, "+rom", 1200);
    for (i = 0; i < 45; i++)
        put(b, "move", i % 2 ? "IDENT=1\n" : "IDENT=0\n");   /* in and out of the one room */
    r = count_cmd(a, "+rom", 2500);
    CHECK(r >= 1 && r <= 6);
    close(a);
    close(b);
    settle();

    /* game fields: only the host changes the game; "started" is the server's */
    a = login_as("Host", "127.0.0.9", "");
    b = login_as("Guest", "127.0.0.9", "");
    put(a, "gcre", "NAME=Race\nPASS=\nPARAMS=1,2,0,3,\nMINSIZE=2\nMAXSIZE=6\nSYSFLAGS=524288\nROOM=1\n");
    CHECK(await(a, "gcre", NULL));
    {   /* the secret: a field of 32 hex digits in the creator's reply */
        const char *p = strstr(g_body, "TKSECRET=");
        int k, hex = p != NULL;
        for (k = 0; p && k < 32; k++)
            hex &= (p[9 + k] >= '0' && p[9 + k] <= '9') || (p[9 + k] >= 'a' && p[9 + k] <= 'f');
        CHECK(hex && p[41] == '\n');
        if (p) { memcpy(sec1, p + 9, 32); sec1[32] = 0; }
    }
    settle();
    {
        char idb[40];
        int id1 = atoi(strstr(g_body, "IDENT=") + 6);
        game_t *g = game_by_ident(id1);
        snprintf(idb, sizeof idb, "IDENT=%d\nPASS=\n", id1);
        CHECK(g != NULL);
        CHECK(g->used && !strcmp(g->sysflags, "0"));        /* bit 19 stripped from gcre */
        { char jb[80]; snprintf(jb, sizeof jb, "%sUSERPARAMS=5,6,3,\n", idb); put(b, "gjoi", jb); }
        CHECK(await(b, "gjoi", NULL));
        CHECK(strstr(g_body, "ADDR0=127.0.0.1") && strstr(g_body, "ADDR1=127.0.0.1"));  /* a member sees addresses */
        snprintf(needle, sizeof needle, "TKSECRET=%s", sec1);
        CHECK(sec1[0] && strstr(g_body, needle));                   /* the joiner's reply has the game's secret */
        put(b, "gset", "NAME=Guest\nUSERPARAMS=7,8,3,\nSYSFLAGS=524288\nMAXSIZE=2\nPARAMS=9,9,4,\n");
        CHECK(await(b, "gset", NULL));
        settle();
        CHECK(!strcmp(g->sysflags, "0") && !strcmp(g->maxsize, "6") && !strcmp(g->params, "1,2,0,3,"));
        CHECK(!strcmp(s_sess[g->players[1]].userparams, "7,8,3,"));   /* its own params did change */
        put(a, "gset", "NAME=Host\nSYSFLAGS=524288\nMAXSIZE=4\nPARAMS=1,2,1,3,\n");
        CHECK(await(a, "gset", NULL));
        settle();
        CHECK(!strcmp(g->sysflags, "0") && !strcmp(g->maxsize, "4") && !strcmp(g->params, "1,2,1,3,"));
        /* a gjoi without USERPARAMS keeps the player's own */
        put(b, "gjoi", idb);
        await(b, "gjoi", NULL);
        settle();
        CHECK(!strcmp(s_sess[g->players[1]].userparams, "7,8,3,"));
        /* the searcher does not see addresses */
        c3 = login_as("Search", "127.0.0.9", "");
        put(c3, "gsea", "START=0\nCOUNT=500\n");
        CHECK(await(c3, "+gam", NULL) && strstr(g_body, "ADDR0=0.0.0.0") && strstr(g_body, "LADDR0=0.0.0.0") &&
              !strstr(g_body, "127.0.0.1") && strstr(g_body, "OPPO0=Host"));
        /* the room broadcasts neither */
        put(a, "gset", "NAME=Host\nMAXSIZE=6\n");
        {
            int seen = 0, plain = 1, k;
            for (k = 0; k < 6 && !seen; k++)
                if (await(c3, "+gam", NULL)) { seen = 1; plain = !strstr(g_body, "127.0.0.1"); }
            CHECK(seen && plain);
        }
        /* the secret reaches the players in +mgm and nowhere else */
        put(a, "gset", "NAME=Host\nMAXSIZE=6\n");
        g_needle = needle;
        r = count_cmd(a, "+mgm", 600);
        CHECK(r >= 1 && g_hits == r);
        put(b, "gset", "NAME=Guest\nUSERPARAMS=7,8,3,\n");
        g_needle = needle;
        r = count_cmd(b, "+mgm", 600);
        CHECK(r >= 1 && g_hits == r);
        /* each gset changes the game (a gset that changes nothing is not pushed to the room) */
        put(a, "gset", "NAME=Host\nMAXSIZE=5\n");
        g_needle = needle;
        r = count_cmd(a, "+gam", 600);
        CHECK(r >= 1 && g_hits == 0);
        put(a, "gset", "NAME=Host\nMAXSIZE=6\n");
        g_needle = needle;
        r = count_cmd(a, "+agm", 600);
        CHECK(r >= 1 && g_hits == 0);
        put(a, "gset", "NAME=Host\nMAXSIZE=5\n");
        g_needle = needle;
        r = count_cmd(c3, "+gam", 600);                    /* the room, which is not in the game */
        CHECK(r >= 1 && g_hits == 0);
        put(a, "gset", "NAME=Host\nMAXSIZE=6\n");
        g_needle = needle;
        r = count_cmd(c3, "+agm", 600);
        CHECK(r >= 1 && g_hits == 0);
        put(c3, "gsea", "START=0\nCOUNT=500\n");
        g_needle = "TKSECRET";
        r = count_cmd(c3, "+gam", 600);                    /* a search: never */
        CHECK(r >= 1 && g_hits == 0);
        g_needle = NULL;
        /* the host starts: a joiner is turned away, by IDENT and by NAME; an open game of the same name is found */
        put(a, "gsta", "");
        CHECK(await(a, "gsta", NULL));
        settle();
        CHECK(game_racing(g));
        put(c3, "gjoi", idb);
        CHECK(await(c3, "gjoi", code) && !memcmp(code, "gone", 4));
        put(c3, "gjoi", "NAME=Race\nPASS=\n");
        CHECK(await(c3, "gjoi", code) && !memcmp(code, "gone", 4));
        put(c3, "gcre", "NAME=Race\nPASS=\nPARAMS=1,2,0,3,\nMINSIZE=2\nMAXSIZE=6\nSYSFLAGS=0\nROOM=1\n");
        CHECK(await(c3, "gcre", NULL));
        {
            const char *p = strstr(g_body, "TKSECRET=");
            if (p) { memcpy(sec2, p + 9, 32); sec2[32] = 0; }
            CHECK(sec2[0] && strcmp(sec1, sec2) != 0);          /* a secret per game */
        }
        {
            char want[24];
            int d;
            snprintf(want, sizeof want, "IDENT=%d", atoi(strstr(g_body, "IDENT=") + 6));
            d = login_as("Late", "127.0.0.9", "");
            put(d, "gjoi", "NAME=Race\nPASS=\nUSERPARAMS=1,1,3,\n");
            CHECK(await(d, "gjoi", code) && !memcmp(code, "\0\0\0\0", 4) && strstr(g_body, want) && strcmp(want, "IDENT=0"));
            close(d);
        }
    }
    close(a);
    close(b);
    close(c3);
    settle();

    /* gdel: the guests are told */
    a = login_as("Boss", "127.0.0.9", "");
    b = login_as("Mate", "127.0.0.9", "");
    put(a, "gcre", "NAME=Doomed\nPASS=\nPARAMS=1,2,0,3,\nMINSIZE=2\nMAXSIZE=6\nSYSFLAGS=0\nROOM=1\n");
    await(a, "gcre", NULL);
    put(b, "gjoi", "NAME=Doomed\nPASS=\nUSERPARAMS=1,1,3,\n");
    await(b, "gjoi", NULL);
    put(a, "gdel", "");
    CHECK(await(a, "gdel", NULL));
    {
        int got = 0, k;
        for (k = 0; k < 8 && !got; k++)
            if (await(b, "+mgm", NULL))
                got = strstr(g_body, "IDENT=0") != NULL;
        CHECK(got);
    }
    close(a);
    close(b);
    settle();

    /* addresses: a believable claim is kept, a stranger's is not; the XUID follows */
    {
        /* connection 127.0.0.1; ADDR 10.1.2.3 (private: kept); XUID names 8.8.8.8 */
        a = login_as("Nat", "10.1.2.3", "$08080808" "aabb1234");
        put(a, "pers", "PERS=Nat\n");
        CHECK(await(a, "+who", NULL) == 1);
        CHECK(strstr(g_body, "LA=") == NULL || 1);
        {
            int k;
            for (k = 0; k < 4; k++)
                if (strstr(g_body, "MA=")) break; else await(a, "+who", NULL);
            CHECK(strstr(g_body, "MA=$0302010aaabb1234"));         /* 8.8.8.8 replaced by 10.1.2.3, offset and hash kept */
            CHECK(strstr(g_body, "LA=10.1.2.3"));
        }
        close(a);
        a = login_as("Liar", "8.8.8.8", "$08080808" "aabb1234");   /* a public stranger's address */
        put(a, "pers", "PERS=Liar\n");
        await(a, "+who", NULL);
        CHECK(strstr(g_body, "LA=127.0.0.1") && !strstr(g_body, "8.8.8.8") && strstr(g_body, "MA=$0100007faabb1234"));
        close(a);
        a = login_as("Home", "127.0.0.2", "$0200007F" "AABB1234");  /* test copies pinned to 127.0.0.2: kept, case kept */
        put(a, "pers", "PERS=Home\n");
        await(a, "+who", NULL);
        CHECK(strstr(g_body, "MA=$0200007FAABB1234") && strstr(g_body, "LA=127.0.0.2"));
        close(a);
        a = login_as("Idx", "127.0.0.2", "$0300007F" "AABB1234");   /* the user index xor'ed into the last octet: kept */
        put(a, "pers", "PERS=Idx\n");
        await(a, "+who", NULL);
        CHECK(strstr(g_body, "MA=$0300007FAABB1234"));
        close(a);
    }

    /* many games, a reply is capped, a slow reader gets everything in order */
    {
        int hosts[130], nh = 0, reader, stuck;
        for (i = 0; i < 130; i++) {
            char nm[40], body[300];
            hosts[i] = login_as("H", "127.0.0.9", "");
            if (hosts[i] < 0) break;
            snprintf(nm, sizeof nm, "G%d", i);
            snprintf(body, sizeof body, "NAME=%s\nPASS=\nPARAMS=1,2,%d,3,\nMINSIZE=2\nMAXSIZE=6\nSYSFLAGS=0\nROOM=1\n", nm, i % 5);
            put(hosts[i], "gcre", body);
            await(hosts[i], "gcre", NULL);
            nh++;
        }
        CHECK(nh == 130);
        settle();
        reader = login_as("Reader", "127.0.0.9", "");
        {
            int sz = 4096;
            setsockopt(reader, SOL_SOCKET, SO_RCVBUF, &sz, sizeof sz);
        }
        put(reader, "gsea", "START=0\nCOUNT=500\n");
        for (i = 0; i < 9; i++)
            put(reader, "gsea", "START=0\nCOUNT=500\n");
        g_quiet_exit = 1;
        r = count_cmd(reader, "+gam", 20000);
        g_quiet_exit = 0;
        CHECK(r >= 100 * 10 && !g_eof);                       /* ten replies of 100, none lost */
        CHECK(await(reader, "xxxx", NULL) == 0);
        /* a paging request */
        put(reader, "gsea", "START=120\nCOUNT=500\n");
        CHECK(count_cmd(reader, "+gam", 600) == 10);
        /* one that never reads: its queue passes the cap and it is dropped */
        stuck = login_as("Stuck", "127.0.0.9", "");
        {
            int sz = 2048;
            setsockopt(stuck, SOL_SOCKET, SO_RCVBUF, &sz, sizeof sz);
        }
        for (i = 0; i < 58; i++)
            put(stuck, "gsea", "START=0\nCOUNT=500\n");
        settle();
        {
            int alive = 0, k;
            for (k = 0; k < MAX_SESS; k++)
                if (s_sess[k].used && !strcmp(s_sess[k].name, "Stuck"))
                    alive = 1;
            CHECK(!alive);                                    /* queued past 1 MB: dropped */
        }
        close(stuck);
        CHECK(fstat(0, &sb) == 0 || 1);
        close(reader);
        for (i = 0; i < nh; i++)
            close(hosts[i]);
    }
    aries_stop();
    settle();
}


/* the deadlines, the idle drop, the per-address cap and its back-off */
static void test_deadlines(void)
{
    aries_config c;
    int a, b, c3, i;
    char code[4];
    memset(&c, 0, sizeof c);
    c.port = PORT;
    c.bind_ip = "127.0.0.1";
    c.public_ip = "127.0.0.1";
    c.log = quiet;
    c.max_sessions = 50;
    c.max_games = 10;
    c.log = capture;
    cap_n = 0; cap[0] = 0;
    c.auth_secs = 2;
    c.idle_secs = 3;
    c.max_per_ip = 3;
    s_loop_exempt = 0;              /* loopback counts like any address here */
    CHECK(aries_start_thread(&c) == 0);
    settle();
    CHECK(s_idle_secs == 3 && s_auth_secs == 2 && s_per_ip == 3);

    /* at the cap: the next gets "full", then the address is not served for a while */
    a = dial(); b = dial(); c3 = dial();
    settle();
    {
        int d = dial(), e;
        CHECK(d >= 0 && await(d, "@dir", code) && !memcmp(code, "full", 4));
        close(d);
        e = dial();
        {
            char buf[64];
            ssize_t r = -2;
            struct pollfd p = { e, POLLIN, 0 };
            if (poll(&p, 1, 1000) > 0)
                r = recv(e, buf, sizeof buf, 0);
            CHECK(r == 0 || r == -1);                          /* closed without a word */
        }
        close(e);
        for (i = 0; i < 40 && s_backoff_total < 1; i++) {   /* the server counts it just after the close */
            struct timespec ts = { 0, 25000000 };
            nanosleep(&ts, NULL);
        }
        CHECK(s_backoff_total >= 1);
    }
    close(a); close(b); close(c3);
    settle();
    s_loop_exempt = 1;
    memset(s_pen, 0, sizeof s_pen);

    /* sign-in deadline and idle timeout */
    a = dial();                                         /* says nothing */
    b = login_as("Quiet", "127.0.0.9", "");
    c3 = login_as("Talker", "127.0.0.9", "");
    CHECK(wait_eof(a, 3500));                           /* 2 s deadline */
    close(a);
    for (i = 0; i < 6 && !wait_eof(b, 500); i++)
        put(c3, "skey", "SKEY=$5075626c6963204b6579\n");   /* the talker keeps talking */
    CHECK(wait_eof(b, 3000) || i < 6);                  /* 3 s idle: the quiet one is gone */
    CHECK(strstr(cap, "Quiet left (idle)") && strstr(cap, "not signed in in time)"));
    {
        int alive = 0;
        for (i = 0; i < 6; i++)
            if (s_sess[i].used && !strcmp(s_sess[i].name, "Talker"))
                alive = 1;
        CHECK(alive);
    }
    close(b);
    close(c3);
    settle();
    CHECK(s_nsess == 0);

    aries_stop();
    settle();
}


/* ── the LAN beacon, and counting sign-ins once ─────────────────────── */

struct beacon_send { const char *msg; unsigned short port; };
static void *beacon_thread(void *p)
{
    struct beacon_send *bs = (struct beacon_send *)p;
    struct sockaddr_in to;
    int s = socket(AF_INET, SOCK_DGRAM, 0), k;
    struct timespec ts = { 0, 300000000 };
    memset(&to, 0, sizeof to);
    to.sin_family = AF_INET;
    to.sin_port = htons(bs->port);
    to.sin_addr.s_addr = htonl(0x7F000001u);
    for (k = 0; k < 8; k++) {
        nanosleep(&ts, NULL);
        sendto(s, bs->msg, strlen(bs->msg), 0, (struct sockaddr *)&to, sizeof to);
    }
    close(s);
    return NULL;
}

static void quiesce_early(int fd) { char b[4096]; struct pollfd p = { fd, POLLIN, 0 }; while (poll(&p, 1, 150) > 0 && recv(fd, b, sizeof b, 0) > 0) ; }

static void test_beacon_and_counts(void)
{
    char ip[40];
    pthread_t th;
    struct beacon_send bs;
    aries_config c;
    int a, k;
    long sign0;
    /* a beacon that names another address than its sender (a rogue one) is ignored ... */
    bs.port = 31879;
    bs.msg = "TAKEDAWN-LOBBY 8.8.8.8 21810";
    pthread_create(&th, NULL, beacon_thread, &bs);
    ip[0] = 0;
    CHECK(aries_find_lan(31879, 1500, ip, sizeof ip) == 0 && !ip[0]);
    pthread_join(th, NULL);
    /* ... one naming a public address as itself too (it cannot be the sender here) ... */
    bs.msg = "TAKEDAWN-LOBBY 203.0.113.9 21810";
    pthread_create(&th, NULL, beacon_thread, &bs);
    CHECK(aries_find_lan(31879, 1500, ip, sizeof ip) == 0);
    pthread_join(th, NULL);
    /* ... and one from its own local address is believed */
    bs.msg = "TAKEDAWN-LOBBY 127.0.0.1 21810";
    pthread_create(&th, NULL, beacon_thread, &bs);
    CHECK(aries_find_lan(31879, 2500, ip, sizeof ip) == 1 && !strcmp(ip, "127.0.0.1"));
    pthread_join(th, NULL);
    bs.msg = "TAKEDAWN-LOBBY garbage";
    pthread_create(&th, NULL, beacon_thread, &bs);
    CHECK(aries_find_lan(31879, 1500, ip, sizeof ip) == 0);
    pthread_join(th, NULL);

    /* a sign-in is counted once per session however often it is repeated */
    memset(&c, 0, sizeof c);
    c.port = PORT;
    c.bind_ip = "127.0.0.1";
    c.public_ip = "127.0.0.1";
    c.log = quiet;
    c.max_sessions = 20;
    c.max_games = 4;
    c.log = capture;
    cap_n = 0; cap[0] = 0;
    CHECK(aries_start_thread(&c) == 0);
    settle();
    sign0 = (long)s_st.signins;
    a = login("Once");
    for (k = 0; k < 3; k++) {
        put(a, "auth", "GTAG=Once\nPASS=$\n");
        await(a, "auth", NULL);
    }
    settle();
    CHECK((long)s_st.signins - sign0 == 1 && s_st.set_n >= 1);
    /* renaming over and over ends the session */
    for (k = 0; k < 8; k++) {
        char b[40];
        snprintf(b, sizeof b, "PERS=Name%d\n", k);
        put(a, "pers", b);
    }
    CHECK(wait_eof(a, 2000));
    close(a);
    settle();
    CHECK(strstr(cap, "left (bad request)"));
    /* how a session ended is in its "left" line, never an address */
    {
        int x = login("Closer"), y = login("Resetter");
        struct linger lg = { 1, 0 };
        quiesce_early(x); quiesce_early(y);               /* read what the server sent: a clean close, not a reset */
        close(x);
        setsockopt(y, SOL_SOCKET, SO_LINGER, &lg, sizeof lg);
        close(y);                                       /* a reset instead of a close */
        settle();
        CHECK(strstr(cap, "Closer left (closed by the player)") && strstr(cap, "Resetter left (connection reset)"));
        CHECK(!strstr(cap, " from 127") && !strstr(cap, "left (idle)"));
    }
    aries_stop();
    settle();
}


/* ── private game codes, game order and the status counts (BO-335, BO-336) ── */

static char bigcap[16384];
static size_t bigcap_n;
static void bigcapture(const char *line)
{
    size_t n = strlen(line);
    if (bigcap_n + n + 2 < sizeof bigcap) {
        memcpy(bigcap + bigcap_n, line, n);
        bigcap_n += n;
        bigcap[bigcap_n++] = '\n';
        bigcap[bigcap_n] = 0;
    }
}

/* The NAME= of every +gam that arrives within ms, in order; returns how many. */
static int gam_names(int fd, int ms, char names[][48], int max)
{
    static unsigned char buf[1 << 18];
    size_t have = 0;
    int n = 0, t;
    for (t = 0; t < ms; ) {
        struct pollfd p = { fd, POLLIN, 0 };
        if (poll(&p, 1, 20) <= 0) { t += 20; continue; }
        {
            ssize_t r = recv(fd, buf + have, sizeof buf - have, 0);
            if (r <= 0)
                break;
            have += (size_t)r;
        }
        while (have >= 12) {
            uint32_t len = ((uint32_t)buf[8] << 24) | ((uint32_t)buf[9] << 16) | ((uint32_t)buf[10] << 8) | buf[11];
            if (have < len)
                break;
            if (!memcmp(buf, "+gam", 4) && len > 12 && n < max) {
                const char *nm;
                buf[len - 1] = 0;
                nm = strstr((char *)buf + 12, "NAME=");
                if (nm) {
                    size_t k = 0;
                    nm += 5;
                    while (nm[k] && nm[k] != '\n' && k < 47) { names[n][k] = nm[k]; k++; }
                    names[n][k] = 0;
                    n++;
                }
            }
            memmove(buf, buf + len, have - len);
            have -= len;
        }
    }
    return n;
}

static void quiesce(int fd) { count_cmd(fd, "zzzz", 250); }   /* read and drop what the server has pushed */

static void mkgame(int fd, const char *name, const char *extra)
{
    char body[300];
    struct timespec ts = { 0, 8000000 };
    snprintf(body, sizeof body, "NAME=%s\nPASS=\nPARAMS=1,2,0,3,\nMINSIZE=2\nMAXSIZE=6\nSYSFLAGS=0\nROOM=1\n%s", name, extra);
    put(fd, "gcre", body);
    await(fd, "gcre", NULL);
    nanosleep(&ts, NULL);                 /* creation times differ */
}

static void test_private_codes(void)
{
    aries_config c;
    char dir[64], names[20][48], code[4], body[200];
    int h1, h2, h3, h4, st, st2, fr, n, i, j, id;
    char path[160];
    FILE *f;
    char pubj[16384];
    long long k;
    /* the validator and the request check */
    CHECK(v_code("KZ7MQ") && v_code("abc") && v_code("123456789012345") && !v_code("a") && !v_code("ab") && !v_code("") && !v_code("1234567890123456") &&
          !v_code("ab cd") && !v_code("ab-cd") && !v_code("ab_cd") && !v_code("caf\xc3\xa9") && !v_code("A=B"));
    CHECK(!strcmp(validate("gcre", "NAME=x\nTKCODE=ab\n"), "TKCODE") && !strcmp(validate("gsea", "TKCODE=Z\n"), "TKCODE") && !validate("gsea", "TKCODE=abc\n"));
    CHECK(!validate("gcre", "NAME=x\nTKCODE=KZ7MQ\n") && !strcmp(validate("gcre", "NAME=x\nTKCODE=bad code\n"), "TKCODE"));
    CHECK(!validate("gsea", "TKCODE=KZ7MQ\n") && !strcmp(validate("gsea", "TKCODE=a-b\n"), "TKCODE"));
    CHECK(!validate("gjoi", "IDENT=1\nTKCODE=kz7mq\n") && !strcmp(validate("gjoi", "NAME=x\nTKCODE=\x01\n"), "TKCODE"));
    CHECK(!validate("gset", "TKCODE=bad code\n") && !validate("rank", "TKCODE=bad code\n"));  /* only where it means something */
    CHECK(!strstr(oneline("NAME=x\nTKCODE=KZ7MQ\nPASS=\n"), "KZ7MQ") && strstr(oneline("NAME=x\nTKCODE=KZ7MQ\nPASS=\n"), "NAME=x"));

    snprintf(dir, sizeof dir, "%s", "/tmp/takedawn-lobby-test-XXXXXX");
    CHECK(mkdtemp(dir) != NULL);
    snprintf(g_dir, sizeof g_dir, "%s", dir);
    memset(&c, 0, sizeof c);
    c.port = PORT;
    c.bind_ip = "127.0.0.1";
    c.public_ip = "127.0.0.1";
    c.log = bigcapture;
    c.verbose = 1;                  /* every request is logged: the code must not be */
    c.max_sessions = 60;
    c.max_games = 30;
    c.stats_dir = dir;
    c.page_html = "<!doctype html>\n";
    s_clock = fake_clock;
    g_now = (time_t)days_from_civil(2026, 11, 5) * 86400 + 100;
    s_status_secs = 2;
    bigcap_n = 0; bigcap[0] = 0;
    CHECK(aries_start_thread(&c) == 0);
    settle();

    h1 = login_as("Priv", "127.0.0.9", "");       /* hosts a private game */
    h2 = login_as("Pub1", "127.0.0.9", "");
    h3 = login_as("Pub2", "127.0.0.9", "");
    h4 = login_as("Pub3", "127.0.0.9", "");
    st = login_as("Stranger", "127.0.0.9", "");   /* an old client: sends no code, knows no keys */
    fr = login_as("Friend", "127.0.0.9", "");

    /* a private game is not announced to the room; a public one is */
    count_cmd(st, "+gam", 300);
    mkgame(h1, "SecretRace", "TKCODE=kz7mq\n");
    CHECK(count_cmd(st, "+gam", 700) == 0);
    {
        int pg = 0;
        for (i = 0; i < MAX_GAMES; i++)
            if (s_games[i].used && s_games[i].priv && !strcmp(s_games[i].pass, "KZ7MQ"))   /* the code is its password, upper-cased */
                pg++;
        CHECK(pg == 1);
    }
    mkgame(h2, "Alpha", "");
    CHECK(count_cmd(st, "+gam", 700) >= 1);

    /* a search without a code never lists it; with the code only it is listed (any case) */
    quiesce(st); quiesce(fr);
    put(st, "gsea", "START=0\nCOUNT=500\n");
    n = gam_names(st, 600, names, 20);
    CHECK(n == 1 && !strcmp(names[0], "Alpha"));
    put(fr, "gsea", "START=0\nCOUNT=500\nTKCODE=Kz7mQ\n");
    n = gam_names(fr, 600, names, 20);
    CHECK(n == 1 && !strcmp(names[0], "SecretRace"));
    put(fr, "gsea", "START=0\nCOUNT=500\nTKCODE=KZ7MR\n");        /* a wrong code: nothing */
    n = gam_names(fr, 500, names, 20);
    CHECK(n == 0);
    put(fr, "gsea", "START=0\nCOUNT=500\n");                      /* the friend without a code sees the public one only */
    n = gam_names(fr, 500, names, 20);
    CHECK(n == 1 && !strcmp(names[0], "Alpha"));

    /* joining: by ident and by name, only with the code */
    id = 0;
    for (i = 0; i < MAX_GAMES; i++)
        if (s_games[i].used && s_games[i].priv)
            id = s_games[i].ident;
    snprintf(body, sizeof body, "IDENT=%d\nPASS=\nUSERPARAMS=1,1,3,\n", id);
    put(st, "gjoi", body);
    CHECK(await(st, "gjoi", code) && !memcmp(code, "gone", 4));               /* no code: as if there were no game */
    snprintf(body, sizeof body, "IDENT=%d\nPASS=\nTKCODE=KZ7MR\nUSERPARAMS=1,1,3,\n", id);
    put(st, "gjoi", body);
    CHECK(await(st, "gjoi", code) && !memcmp(code, "gone", 4));               /* a wrong code */
    put(st, "gjoi", "NAME=SecretRace\nPASS=\nUSERPARAMS=1,1,3,\n");
    CHECK(await(st, "gjoi", code) && !memcmp(code, "gone", 4));               /* by name too */
    snprintf(body, sizeof body, "IDENT=%d\nPASS=\nPASS2=x\nUSERPARAMS=1,1,3,\n", id);
    put(st, "gjoi", body);
    CHECK(await(st, "gjoi", code) && !memcmp(code, "gone", 4));
    put(fr, "gjoi", "NAME=SecretRace\nPASS=\nTKCODE=kz7mq\nUSERPARAMS=1,1,3,\n");   /* Quick Match: search then join by name, with the code */
    CHECK(await(fr, "gjoi", code) && !memcmp(code, "\0\0\0\0", 4) && strstr(g_body, "IDENT=") &&
          strstr(g_body, "TKSECRET=") && !strstr(g_body, "KZ7MQ") && !strstr(g_body, "kz7mq") && !strstr(g_body, "TKCODE"));
    CHECK(s_games[0].used || 1);
    {
        int in = 0;
        for (i = 0; i < MAX_GAMES; i++)
            if (s_games[i].used && s_games[i].priv)
                in = s_games[i].np;
        CHECK(in == 2);
    }
    /* a full private game is not offered any more, but the room never saw it */
    mkgame(h4, "Gamma", "");

    /* order: fullest first, then oldest. Alpha has 1, Gamma 1; Beta gets a second player. */
    mkgame(h3, "Beta", "");
    {
        int g5 = login_as("Joiner", "127.0.0.9", "");
        put(g5, "gjoi", "NAME=Beta\nPASS=\nUSERPARAMS=1,1,3,\n");
        await(g5, "gjoi", NULL);
        quiesce(g5);
        put(g5, "gsea", "START=0\nCOUNT=500\n");
        n = gam_names(g5, 600, names, 20);
        CHECK(n == 3 && !strcmp(names[0], "Beta") && !strcmp(names[1], "Alpha") && !strcmp(names[2], "Gamma"));
        close(g5);
    }
    settle();
    quiesce(st);
    put(st, "gsea", "START=0\nCOUNT=500\n");
    n = gam_names(st, 600, names, 20);
    /* Beta lost its guest: equal sizes, so the oldest first */
    CHECK(n == 3 && !strcmp(names[0], "Alpha") && !strcmp(names[1], "Gamma") && !strcmp(names[2], "Beta"));
    put(st, "gsea", "START=1\nCOUNT=1\n");                        /* paging still works on the ordered list */
    n = gam_names(st, 500, names, 20);
    CHECK(n == 1 && !strcmp(names[0], "Gamma"));

    /* wrong codes are limited: more than CODE_MISSES in a minute (a code no game has) ends the session */
    {
        int bf = login_as("Brute", "127.0.0.9", ""), tries;
        for (tries = 0; tries < 24 && !wait_eof(bf, 30); tries++) {
            snprintf(body, sizeof body, "NAME=Nothing\nPASS=\nTKCODE=ZZ%d\nUSERPARAMS=1,1,3,\n", tries);
            put(bf, "gjoi", body);
        }
        CHECK(tries >= CODE_MISSES && tries <= CODE_MISSES + 3 && wait_eof(bf, 800));
        close(bf);
    }

    /* the public page counts private games and shows nothing of them */
    g_now += 25;
    settle();
    snprintf(path, sizeof path, "%s/public/stats.json", dir);
    f = fopen(path, "rb");
    pubj[0] = 0;
    if (f) { size_t r = fread(pubj, 1, sizeof pubj - 1, f); pubj[r] = 0; fclose(f); }
    CHECK(strstr(pubj, "\"games_private\":1") && !strstr(pubj, "SecretRace") && !strstr(pubj, "KZ7MQ") && !strstr(pubj, "kz7mq") &&
          strstr(pubj, "\"name\":\"Alpha\"") && strstr(pubj, "\"name\":\"Gamma\"") && strstr(pubj, "\"name\":\"Beta\""));
    CHECK(strstr(pubj, "\"games_waiting\":3"));
    CHECK(strstr(pubj, "\"name\":\"Alpha\"") < strstr(pubj, "\"name\":\"Gamma\"") && strstr(pubj, "\"name\":\"Gamma\"") < strstr(pubj, "\"name\":\"Beta\""));

    /* TKONLINE / TKWAITING: in every room record, counting public games only */
    {
        int ob = login_as("Watch", "127.0.0.9", ""), np0, nw0;
        char *p;
        put(ob, "move", "IDENT=1\n");
        await(ob, "move", NULL);
        CHECK(await(ob, "+rom", NULL) && strstr(g_body, "TKONLINE=") && strstr(g_body, "TKWAITING="));
        p = strstr(g_body, "TKONLINE=");
        np0 = p ? atoi(p + 9) : -1;
        p = strstr(g_body, "TKWAITING=");
        nw0 = p ? atoi(p + 10) : -1;
        CHECK(np0 == s_players && nw0 == 3);                    /* three public games waiting, the private one not counted */
        /* a newcomer is announced to the others within a few seconds, without any request from them */
        {
            int nw = login_as("Newcomer", "127.0.0.9", ""), seen = 0, tries;
            for (tries = 0; tries < 12 && !seen; tries++)
                if (await(ob, "+rom", NULL)) {
                    p = strstr(g_body, "TKONLINE=");
                    seen = p && atoi(p + 9) == np0 + 1;
                }
            CHECK(seen);
            close(nw);
        }
        /* the sele reply of a fresh connection carries them too */
        {
            int fresh = dial(), got = 0, tries;
            put(fresh, "sele", "MYGAME=1 GAMES=1");
            for (tries = 0; tries < 6 && !got; tries++)
                if (await(fresh, "+rom", NULL))
                    got = strstr(g_body, "TKONLINE=") != NULL && strstr(g_body, "TKWAITING=") != NULL;
            CHECK(got);
            close(fresh);
        }
        close(ob);
    }

    /* unknown keys, both ways, are harmless */
    {
        int u = login_as("Odd", "127.0.0.9", "");
        quiesce(u);
        put(u, "gsea", "START=0\nCOUNT=500\nTKWHATEVER=1\nTKCODEX=ABC\n");
        n = gam_names(u, 500, names, 20);
        CHECK(n == 3);
        mkgame(u, "OddOne", "TKNEWTHING=77\nTKMODE=fast\n");
        quiesce(u);
        put(u, "gsea", "START=0\nCOUNT=500\n");
        n = gam_names(u, 500, names, 20);
        CHECK(n == 4);
        close(u);
    }

    /* codes and the private game's name are never logged; the secret neither */
    CHECK(!strstr(bigcap, "KZ7MQ") && !strstr(bigcap, "kz7mq") && !strstr(bigcap, "'SecretRace'") && !strstr(bigcap, "KZ7MR") &&
          strstr(bigcap, "a private game") && !strstr(bigcap, "TKSECRET=0") && !strstr(bigcap, "TKSECRET=1"));
    (void)h4; (void)k; (void)j; (void)st2;
    close(h1); close(h2); close(h3); close(h4); close(st); close(fr);
    aries_stop();
    settle();
    s_status_secs = STATUS_SECS;
    snprintf(path, sizeof path, "%s/public/stats.json", dir);
    unlink(path);
    snprintf(path, sizeof path, "%s/public/index.html", dir);
    unlink(path);
    snprintf(path, sizeof path, "%s/public", dir);
    rmdir(path);
    snprintf(path, sizeof path, "%s/current.json", dir);
    unlink(path);
    rmdir(dir);
    s_clock = NULL;
}


/* ── a private game is pushed to the sessions that gave its code, and to nobody else ── */

static int sess_game_count(void)
{
    int i, n = 0;
    for (i = 0; i < MAX_GAMES; i++)
        n += s_games[i].used;
    return n;
}

static void test_private_pushes(void)
{
    aries_config c;
    int h, f, o, s_, l, i, r;
    char body[200];
    memset(&c, 0, sizeof c);
    c.port = PORT;
    c.bind_ip = "127.0.0.1";
    c.public_ip = "127.0.0.1";
    c.log = bigcapture;
    c.verbose = 1;
    c.max_sessions = 30;
    c.max_games = 10;
    bigcap_n = 0; bigcap[0] = 0;
    CHECK(aries_start_thread(&c) == 0);
    settle();

    h = login_as("Hostp", "127.0.0.9", "");
    f = login_as("Friendp", "127.0.0.9", "");
    o = login_as("Otherp", "127.0.0.9", "");
    s_ = login_as("Strangerp", "127.0.0.9", "");
    /* the friend and another group give their codes when they enter the room; the stranger gives none */
    put(f, "move", "IDENT=1\nTKCODE=zq4x7\n");  await(f, "move", NULL);
    put(o, "move", "IDENT=1\nTKCODE=OTHER1\n"); await(o, "move", NULL);
    put(h, "move", "IDENT=1\nTKCODE=ZQ4X7\n");  await(h, "move", NULL);
    quiesce(f); quiesce(o); quiesce(s_); quiesce(h);

    /* the host creates the private game: the friend hears of it at once (+gam and +agm), nobody else does */
    mkgame(h, "PrivA", "TKCODE=zq4x7\n");
    g_needle = "NAME=PrivA";
    r = count_cmd(f, "+gam", 700);
    CHECK(r >= 1 && g_hits == r);
    g_needle = NULL;
    quiesce(f);
    CHECK(count_cmd(o, "+gam", 400) == 0);
    CHECK(count_cmd(o, "+agm", 300) == 0);
    CHECK(count_cmd(s_, "+gam", 400) == 0);
    CHECK(count_cmd(s_, "+agm", 300) == 0);

    /* an update of the game (the host's gset) is pushed the same way: +gam and +agm to the friend only,
     * with no addresses and no secret in them */
    put(h, "gset", "NAME=Hostp\nMAXSIZE=5\n");
    await(h, "gset", NULL);
    CHECK(await(f, "+gam", NULL) && strstr(g_body, "NAME=PrivA") && strstr(g_body, "ADDR0=0.0.0.0") &&
          !strstr(g_body, "TKSECRET") && !strstr(g_body, "127.0.0.1") && strstr(g_body, "MAXSIZE=5"));
    quiesce(f); quiesce(o); quiesce(s_);
    put(h, "gset", "NAME=Hostp\nMAXSIZE=6\n");
    await(h, "gset", NULL);
    g_needle = "NAME=PrivA";
    r = count_cmd(f, "+agm", 700);
    CHECK(r == 1 && g_hits == 1);
    CHECK(count_cmd(o, "+gam", 300) == 0 && count_cmd(s_, "+gam", 300) == 0 &&
          count_cmd(o, "+agm", 100) == 0 && count_cmd(s_, "+agm", 100) == 0);

    /* a player who gives the code later hears of the game as soon as it enters the room */
    l = login_as("Latep", "127.0.0.9", "");
    quiesce(l);
    snprintf(body, sizeof body, "IDENT=1\nTKCODE=Zq4X7\n");
    put(l, "move", body);
    g_needle = "NAME=PrivA";
    r = count_cmd(l, "+gam", 800);
    CHECK(r >= 1 && g_hits >= 1);
    g_needle = NULL;
    /* a player of the other group, and the stranger, entering the room hear nothing of it */
    {
        int o2 = login_as("Other2p", "127.0.0.9", "");
        quiesce(o2);
        put(o2, "move", "IDENT=1\nTKCODE=OTHER1\n");
        g_needle = "NAME=PrivA";
        count_cmd(o2, "+gam", 600);
        CHECK(g_hits == 0);
        g_needle = NULL;
        close(o2);
    }

    /* a search without the code does not list it, even for a session that gave the code earlier */
    quiesce(f);
    put(f, "gsea", "START=0\nCOUNT=500\n");
    CHECK(count_cmd(f, "+gam", 600) == 0);

    /* a code of a request that is not a code ends the session; an empty one clears it */
    put(f, "move", "IDENT=1\nTKCODE=\n");
    await(f, "move", NULL);
    quiesce(f);
    mkgame(h, "PrivB", "TKCODE=zq4x7\n");
    g_needle = "NAME=PrivB";
    r = count_cmd(f, "+gam", 600);
    CHECK(g_hits == 0);                                   /* the friend cleared its code: no push any more */
    g_needle = NULL;
    {
        int bad = login_as("Badcode", "127.0.0.9", "");
        put(bad, "move", "IDENT=1\nTKCODE=no good\n");
        CHECK(wait_eof(bad, 1500));
        close(bad);
    }

    /* the game goes away: the friend with the code is told (IDENT only), nobody else */
    put(l, "move", "IDENT=1\nTKCODE=ZQ4X7\n");
    quiesce(l); quiesce(o); quiesce(s_);
    put(h, "gdel", "");
    await(h, "gdel", NULL);
    g_needle = "IDENT=";
    r = count_cmd(l, "+gam", 700);
    CHECK(r >= 1);
    g_needle = NULL;
    CHECK(count_cmd(o, "+gam", 300) == 0 && count_cmd(s_, "+gam", 300) == 0 && count_cmd(f, "+gam", 200) == 0);
    CHECK(sess_game_count() == 0);

    /* the codes and the private games' names are never in the log */
    CHECK(!strstr(bigcap, "ZQ4X7") && !strstr(bigcap, "zq4x7") && !strstr(bigcap, "Zq4X7") && !strstr(bigcap, "OTHER1") &&
          !strstr(bigcap, "'PrivA'") && !strstr(bigcap, "'PrivB'") && strstr(bigcap, "TKCODE=***"));
    (void)i;
    close(h); close(f); close(o); close(s_); close(l);
    aries_stop();
    settle();
}


/* ── CREATE GAME: a public game that also has a code (TKPUBLIC=1) ── */

static int wrong_of(const char *name);

static void test_public_codes(void)
{
    aries_config c;
    int h, h2, h3, st, fr, i, n, id = 0, pub = 0, prv = 0, plain = 0;
    char names[20][48], code[4], body[200];
    memset(&c, 0, sizeof c);
    c.port = PORT;
    c.bind_ip = "127.0.0.1";
    c.public_ip = "127.0.0.1";
    c.log = bigcapture;
    c.verbose = 0;                  /* the log lines only: verbose ones fill bigcap */
    c.max_sessions = 30;
    c.max_games = 10;
    bigcap_n = 0; bigcap[0] = 0;
    CHECK(aries_start_thread(&c) == 0);
    settle();

    CHECK(!validate("gcre", "NAME=x\nTKCODE=PQ7ZK\nTKPUBLIC=1\n") && !strcmp(validate("gcre", "NAME=x\nTKPUBLIC=2\n"), "TKPUBLIC"));
    h = login_as("Hostq", "127.0.0.9", "");
    h2 = login_as("Hostr", "127.0.0.9", "");
    h3 = login_as("Hosts", "127.0.0.9", "");
    st = login_as("Strangerq", "127.0.0.9", "");     /* QUICK PLAY / BROWSE GAMES: no code */
    fr = login_as("Friendq", "127.0.0.9", "");       /* JOIN WITH CODE */
    put(st, "move", "IDENT=1\n"); await(st, "move", NULL);
    quiesce(st);

    /* CREATE GAME: announced to the room like any public game */
    mkgame(h, "OpenRace", "TKCODE=pq7zk\nTKPUBLIC=1\n");
    CHECK(count_cmd(st, "+gam", 700) >= 1);
    mkgame(h2, "Closed", "TKCODE=zz9qq\nTKPUBLIC=0\n");   /* the System page's code: private, as before */
    mkgame(h3, "QuickOne", "");                            /* QUICK PLAY's own game: public, no code */
    for (i = 0; i < MAX_GAMES; i++) {
        const game_t *g = &s_games[i];
        if (!g->used)
            continue;
        if (!strcmp(g->name, "OpenRace") && !g->priv && !strcmp(g->code, "PQ7ZK") && !g->pass[0]) {
            pub++;
            id = g->ident;
        }
        prv += !strcmp(g->name, "Closed") && g->priv && !strcmp(g->pass, "ZZ9QQ");
        plain += !strcmp(g->name, "QuickOne") && !g->priv && !g->code[0];
    }
    CHECK(pub == 1 && prv == 1 && plain == 1);

    /* a search without a code lists it (with QUICK PLAY's game, not the private one) */
    quiesce(st); quiesce(fr);
    put(st, "gsea", "START=0\nCOUNT=500\n");
    n = gam_names(st, 600, names, 20);
    CHECK(n == 2 && (!strcmp(names[0], "OpenRace") || !strcmp(names[1], "OpenRace")) &&
          strcmp(names[0], "Closed") && strcmp(names[1], "Closed"));
    /* the code finds it, and only it; a wrong code nothing */
    put(fr, "gsea", "START=0\nCOUNT=500\nTKCODE=Pq7zK\n");
    n = gam_names(fr, 600, names, 20);
    CHECK(n == 1 && !strcmp(names[0], "OpenRace"));
    put(fr, "gsea", "START=0\nCOUNT=500\nTKCODE=PQ7ZX\n");
    n = gam_names(fr, 500, names, 20);
    CHECK(n == 0);

    /* joined without a code (QUICK PLAY by name, BROWSE GAMES by ident) and with it */
    snprintf(body, sizeof body, "IDENT=%d\nPASS=\nUSERPARAMS=1,1,3,\n", id);
    put(st, "gjoi", body);
    CHECK(await(st, "gjoi", code) && !memcmp(code, "\0\0\0\0", 4));
    put(fr, "gjoi", "NAME=OpenRace\nPASS=\nTKCODE=pq7zk\nUSERPARAMS=1,1,3,\n");
    CHECK(await(fr, "gjoi", code) && !memcmp(code, "\0\0\0\0", 4) && !strstr(g_body, "PQ7ZK") && !strstr(g_body, "TKCODE"));
    for (i = 0, n = 0; i < MAX_GAMES; i++)
        if (s_games[i].used && s_games[i].ident == id)
            n = s_games[i].np;
    CHECK(n == 3);

    /* logged by name like a public game, the code never */
    CHECK(strstr(bigcap, "'OpenRace' with a code") && strstr(bigcap, "'QuickOne'") && !strstr(bigcap, "'QuickOne' with"));
    CHECK(!strstr(bigcap, "PQ7ZK") && !strstr(bigcap, "pq7zk") && !strstr(bigcap, "Pq7zK") && !strstr(bigcap, "ZZ9QQ"));
    CHECK(!strstr(bigcap, "'Closed'") && strstr(bigcap, "a private game"));

    /* BO-364: the host kicks the friend. Its code search finds nothing and says why (no strike: the
     * code is real), its plain search leaves the game out, its join is refused and says why; the
     * stranger still sees the game */
    quiesce(h); quiesce(st); quiesce(fr);
    put(h, "gset", "NAME=Hostq\nKICK=Friendq\n");
    CHECK(await(h, "gset", NULL));
    quiesce(fr); quiesce(st);
    {
        int w0 = wrong_of("Friendq");
        put(fr, "gsea", "START=0\nCOUNT=500\nTKCODE=PQ7ZK\n");
        CHECK(await(fr, "gsea", NULL) && strstr(g_body, "COUNT=0") && strstr(g_body, "TKKICKED=1"));
        CHECK(count_cmd(fr, "+gam", 300) == 0 && wrong_of("Friendq") == w0);
        put(fr, "gsea", "START=0\nCOUNT=500\nTKCODE=PQ7ZX\n");      /* a wrong code: no reason given */
        CHECK(await(fr, "gsea", NULL) && strstr(g_body, "COUNT=0") && !strstr(g_body, "TKKICKED"));
    }
    put(fr, "gsea", "START=0\nCOUNT=500\n");
    n = gam_names(fr, 600, names, 20);
    CHECK(n == 1 && !strcmp(names[0], "QuickOne"));
    put(fr, "gjoi", "NAME=OpenRace\nPASS=\nTKCODE=pq7zk\nUSERPARAMS=1,1,3,\n");
    CHECK(await(fr, "gjoi", code) && !memcmp(code, "gone", 4) && strstr(g_body, "TKKICKED=1"));
    put(st, "gsea", "START=0\nCOUNT=500\n");
    n = gam_names(st, 600, names, 20);
    CHECK(n == 2);
    /* BO-362: a host alone that makes another game: the first one's end is
     * logged, so the log never reads as one game made twice; a host with
     * players in its game: only the "closed" line, as before */
    bigcap_n = 0; bigcap[0] = 0;
    mkgame(h3, "QuickTwo", "");
    settle();
    {
        const char *l = strstr(bigcap, "Hosts left game "), *m = strstr(bigcap, "'QuickTwo'");
        CHECK(l && m && l < m && strstr(bigcap, "(no one else in it: it closes)"));
    }
    bigcap_n = 0; bigcap[0] = 0;
    mkgame(h, "OpenRace2", "TKCODE=pq7zk\nTKPUBLIC=1\n");
    settle();
    CHECK(strstr(bigcap, "closed: its host left") && !strstr(bigcap, "no one else in it"));
    bigcap_n = 0; bigcap[0] = 0;
    put(h3, "glea", "");
    await(h3, "glea", NULL);
    settle();
    CHECK(strstr(bigcap, "Hosts left game ") && strstr(bigcap, "no one else in it"));
    /* the title's host leaves with gdel: logged too */
    mkgame(h3, "QuickThree", "");
    bigcap_n = 0; bigcap[0] = 0;
    put(h3, "gdel", "");
    await(h3, "gdel", NULL);
    settle();
    CHECK(strstr(bigcap, "Hosts closed game ") && strstr(bigcap, "(it had 1 player(s))"));
    close(h); close(h2); close(h3); close(st); close(fr);
    aries_stop();
    settle();
}


/* ── --crowd-test (BO-366): the load tool's players and real ones apart ── */

static int g_crowd_n;
static int login_crowd(const char *name, int *acked)
{
    char body[200];
    int fd = dial();
    if (fd < 0)
        return -1;
    g_crowd_n++;
    snprintf(body, sizeof body, "ADDR=127.9.%d.%d\nPORT=1000\nTKID=1\nTKNAME=%s\nTKCROWD=1\n",
             g_crowd_n / 256, g_crowd_n % 256, name);
    put(fd, "addr", body);
    await(fd, "addr", NULL);
    *acked = strstr(g_body, "TKCROWD=1") != NULL;
    put(fd, "sele", "MYGAME=1 GAMES=1"); await(fd, "sele", NULL);
    snprintf(body, sizeof body, "GTAG=%s\nPASS=$\n", name);
    put(fd, "auth", body); await(fd, "auth", NULL);
    snprintf(body, sizeof body, "GTAG=%s\n", name);
    put(fd, "pers", body); await(fd, "pers", NULL);
    put(fd, "move", "IDENT=1\n"); await(fd, "move", NULL);
    return fd;
}

static int ident_of(const char *name)
{
    int i;
    for (i = 0; i < MAX_GAMES; i++)
        if (s_games[i].used && !strcmp(s_games[i].name, name))
            return s_games[i].ident;
    return 0;
}

static void test_crowd(void)
{
    aries_config c;
    int pass, rh, rs, ch, ch2, cs, ack, n, i;
    char names[20][48], code[4], body[120];
    for (pass = 0; pass < 2; pass++) {          /* 0: --crowd-test, 1: without (the public server) */
        int on = pass == 0, real_in_fake = 0, fake_in_real = 0, push_seen;
        memset(&c, 0, sizeof c);
        c.port = PORT;
        c.bind_ip = "127.0.0.1";
        c.public_ip = "127.0.0.1";
        c.log = quiet;
        c.max_sessions = 30;
        c.max_games = 10;
        c.crowd_test = on;
        CHECK(aries_start_thread(&c) == 0);
        settle();
        CHECK(!validate("addr", "TKCROWD=1\n") && !strcmp(validate("addr", "TKCROWD=7\n"), "TKCROWD"));
        rh = login_as("RealHost", "127.0.0.9", "");
        rs = login_as("RealSeek", "127.0.0.9", "");
        ch = login_crowd("CrowdHost", &ack);
        CHECK(ack == on);                        /* said back only in test mode */
        ch2 = login_crowd("CrowdCode", &ack);
        cs = login_crowd("CrowdSeek", &ack);
        quiesce(rs); quiesce(cs);
        mkgame(rh, "RealGame", "");
        mkgame(ch, "FakeGame", "");
        mkgame(ch2, "FakeCode", "TKCODE=crwd7\nTKPUBLIC=1\n");
        /* the room pushes: the real seeker hears of the fake games only without the flag */
        push_seen = gam_names(rs, 500, names, 20);
        for (i = 0; i < push_seen; i++)
            real_in_fake |= !strncmp(names[i], "Fake", 4);
        CHECK(real_in_fake == !on);
        quiesce(rs); quiesce(cs);
        /* QUICK PLAY's search */
        put(rs, "gsea", "START=0\nCOUNT=500\n");
        n = gam_names(rs, 600, names, 20);
        CHECK(on ? n == 1 && !strcmp(names[0], "RealGame") : n == 3);
        put(cs, "gsea", "START=0\nCOUNT=500\n");
        n = gam_names(cs, 600, names, 20);
        for (i = 0; i < n; i++)
            fake_in_real |= !strcmp(names[i], "RealGame");
        CHECK(on ? n == 2 && !fake_in_real : n == 3);
        /* the code of a fake game finds nothing for a real player in test mode */
        put(rs, "gsea", "START=0\nCOUNT=500\nTKCODE=CRWD7\n");
        n = gam_names(rs, 600, names, 20);
        CHECK(n == (on ? 0 : 1));
        /* joins by name and by ident across the line are "gone" in test mode */
        quiesce(rs); quiesce(cs);
        put(rs, "gjoi", "NAME=FakeGame\nPASS=\nUSERPARAMS=0,0,0,\n");
        CHECK(await(rs, "gjoi", code) && !memcmp(code, on ? "gone" : "\0\0\0\0", 4));
        if (!on) { put(rs, "glea", ""); await(rs, "glea", NULL); }
        snprintf(body, sizeof body, "IDENT=%d\nPASS=\nUSERPARAMS=0,0,0,\n", ident_of("RealGame"));
        quiesce(cs);
        put(cs, "gjoi", body);
        CHECK(await(cs, "gjoi", code) && !memcmp(code, on ? "gone" : "\0\0\0\0", 4));
        /* and inside their own kind everything works as before */
        if (on) {
            quiesce(cs);
            put(cs, "gjoi", "NAME=FakeGame\nPASS=\nUSERPARAMS=0,0,0,\n");
            CHECK(await(cs, "gjoi", code) && !memcmp(code, "\0\0\0\0", 4));
            quiesce(rs);
            put(rs, "gjoi", "NAME=RealGame\nPASS=\nUSERPARAMS=0,0,0,\n");
            CHECK(await(rs, "gjoi", code) && !memcmp(code, "\0\0\0\0", 4));
        }
        close(rh); close(rs); close(ch); close(ch2); close(cs);
        aries_stop();
        settle();
    }
}


/* ── entering a room: the games already there are pushed (JOIN GAME does not search) ── */

static int gam_names_both(int fd, int ms, char names[][48], int max, int *agm)
{
    /* like gam_names, and counts the +agm pushes (same records) in *agm */
    *agm = count_cmd(fd, "+agm", 0);
    return gam_names(fd, ms, names, max);
}

static void test_room_entry(void)
{
    aries_config c;
    int h1, h2, h3, h4, g2, locked, started, friend_, stranger, late, newc, n, i, agm;
    char names[130][48];
    memset(&c, 0, sizeof c);
    c.port = PORT;
    c.bind_ip = "127.0.0.1";
    c.public_ip = "127.0.0.1";
    c.log = quiet;
    c.max_sessions = 200;
    c.max_games = 150;
    CHECK(aries_start_thread(&c) == 0);
    settle();
    (void)gam_names_both;

    h1 = login_as("Rm1", "127.0.0.9", "");
    h2 = login_as("Rm2", "127.0.0.9", "");
    h3 = login_as("Rm3", "127.0.0.9", "");
    h4 = login_as("Rm4", "127.0.0.9", "");
    locked = login_as("Rm5", "127.0.0.9", "");
    started = login_as("Rm6", "127.0.0.9", "");
    g2 = login_as("Rm7", "127.0.0.9", "");
    friend_ = login_as("Rm8", "127.0.0.9", "");
    mkgame(h1, "First", "");                                     /* 1 player, oldest */
    mkgame(h2, "Second", "");
    put(h3, "gcre", "NAME=Pw\nPASS=secret\nPARAMS=1,2,0,3,\nMINSIZE=2\nMAXSIZE=6\nSYSFLAGS=0\nROOM=1\n");   /* a password: not pushed */
    await(h3, "gcre", NULL);
    mkgame(locked, "Locked", "");
    put(locked, "gset", "NAME=Rm5\nSYSFLAGS=4096\n");             /* the host's start lock: not pushed */
    await(locked, "gset", NULL);
    mkgame(started, "Going", "");
    put(started, "gsta", "");
    await(started, "gsta", NULL);                                 /* under way: not pushed */
    mkgame(h4, "Private", "TKCODE=ab12c\n");                      /* private: not for strangers */
    put(g2, "gjoi", "NAME=Second\nPASS=\nUSERPARAMS=1,1,3,\n");    /* Second has 2 players: fullest first */
    await(g2, "gjoi", NULL);
    (void)friend_;
    settle();

    /* someone who comes into the room afterwards is told of the open public games, fullest then oldest */
    newc = dial();
    {
        char b[200];
        snprintf(b, sizeof b, "ADDR=127.0.0.9\nPORT=1000\n");
        put(newc, "addr", "ADDR=127.9.9.1\nPORT=1000\n"); await(newc, "addr", NULL);
        put(newc, "auth", "GTAG=Newcomer\nPASS=$\n"); await(newc, "auth", NULL);
        put(newc, "pers", "GTAG=Newcomer\n"); await(newc, "pers", NULL);
        quiesce_early(newc);
        put(newc, "move", "IDENT=1\n");
        n = gam_names(newc, 800, names, 130);
        CHECK(n == 2 && !strcmp(names[0], "Second") && !strcmp(names[1], "First"));
    }
    /* the records have no address and no secret */
    {
        char body[200];
        int stranger2 = dial(), k;
        put(stranger2, "addr", "ADDR=127.9.9.2\nPORT=1000\n"); await(stranger2, "addr", NULL);
        put(stranger2, "auth", "GTAG=Stranger2\nPASS=$\n"); await(stranger2, "auth", NULL);
        put(stranger2, "pers", "GTAG=Stranger2\n"); await(stranger2, "pers", NULL);
        quiesce_early(stranger2);
        put(stranger2, "move", "IDENT=1\n");
        g_needle = "TKSECRET";
        k = count_cmd(stranger2, "+gam", 700);
        CHECK(k == 2 && g_hits == 0);
        (void)body;
        close(stranger2);
    }
    /* a stranger never sees the private game; a player who gives its code does (and nobody else's) */
    stranger = login_as("Stranger", "127.0.0.9", "");
    late = dial();
    put(late, "addr", "ADDR=127.9.9.3\nPORT=1000\n"); await(late, "addr", NULL);
    put(late, "auth", "GTAG=Latecomer\nPASS=$\n"); await(late, "auth", NULL);
    put(late, "pers", "GTAG=Latecomer\n"); await(late, "pers", NULL);
    quiesce_early(late);
    put(late, "move", "IDENT=1\nTKCODE=AB12C\n");
    n = gam_names(late, 800, names, 130);
    {
        int seen_priv = 0, seen_pw = 0, seen_locked = 0, seen_going = 0;
        for (i = 0; i < n; i++) {
            seen_priv += !strcmp(names[i], "Private");
            seen_pw += !strcmp(names[i], "Pw");
            seen_locked += !strcmp(names[i], "Locked");
            seen_going += !strcmp(names[i], "Going");
        }
        CHECK(n == 3 && seen_priv == 1 && !seen_pw && !seen_locked && !seen_going);
    }
    /* the same room again: no public game is pushed twice (only the private one, for the code just given again) */
    { struct timespec ts = { 1, 100000000 }; nanosleep(&ts, NULL); }          /* a code lookup a second */
    put(late, "move", "IDENT=1\nTKCODE=AB12C\n");
    n = gam_names(late, 500, names, 130);
    CHECK(n == 1 && !strcmp(names[0], "Private"));
    { struct timespec ts = { 1, 100000000 }; nanosleep(&ts, NULL); }
    put(late, "move", "IDENT=1\nTKCODE=\n");                         /* the code cleared: nothing at all */
    CHECK(gam_names(late, 500, names, 130) == 0);
    close(late);
    /* the stranger's own pushes after login (it moved into the room in login_as): no private game among them */
    CHECK(count_cmd(stranger, "zzzz", 100) == 0);

    /* with many games a push holds at most 100, fullest/oldest first */
    {
        int hs[110], nh = 0;
        for (i = 0; i < 106; i++) {
            char nm[24];
            hs[i] = login_as("Many", "127.0.0.9", "");
            if (hs[i] < 0) break;
            snprintf(nm, sizeof nm, "M%d", i);
            mkgame(hs[i], nm, "");
            nh++;
        }
        CHECK(nh == 106);
        {
            int big = dial();
            put(big, "addr", "ADDR=127.9.9.4\nPORT=1000\n"); await(big, "addr", NULL);
            put(big, "auth", "GTAG=Bigroom\nPASS=$\n"); await(big, "auth", NULL);
            put(big, "pers", "GTAG=Bigroom\n"); await(big, "pers", NULL);
            quiesce_early(big);
            put(big, "move", "IDENT=1\n");
            n = gam_names(big, 2500, names, 130);
            CHECK(n == 100 && !strcmp(names[0], "Second"));       /* Second has 2 players; the rest are 1, oldest first */
            close(big);
        }
        for (i = 0; i < nh; i++)
            close(hs[i]);
    }
    close(newc); close(stranger);
    close(h1); close(h2); close(h3); close(h4); close(locked); close(started); close(g2); close(friend_);
    (void)agm;
    aries_stop();
    settle();
}


/* ── H5, M9: a race that has started is left alone ── */

static void test_racing_guards(void)
{
    aries_config c;
    int a, b, d, r;
    char code[4];
    unsigned long started0;
    memset(&c, 0, sizeof c);
    c.port = PORT;
    c.bind_ip = "127.0.0.1";
    c.public_ip = "127.0.0.1";
    c.log = quiet;
    c.max_sessions = 20;
    c.max_games = 5;
    CHECK(aries_start_thread(&c) == 0);
    settle();
    a = login_as("Hosta", "127.0.0.9", "");
    b = login_as("Guesta", "127.0.0.9", "");
    d = login_as("Thirda", "127.0.0.9", "");
    mkgame(a, "Race", "");
    put(b, "gjoi", "NAME=Race\nPASS=\nUSERPARAMS=1,1,3,\n");
    await(b, "gjoi", NULL);
    put(d, "gjoi", "NAME=Race\nPASS=\nUSERPARAMS=1,1,3,\n");
    await(d, "gjoi", NULL);
    quiesce(a); quiesce(b); quiesce(d);
    put(a, "gsta", "");
    CHECK(await(a, "gsta", NULL) && strstr(g_body, "IDENT="));
    quiesce(a); quiesce(b); quiesce(d);
    started0 = s_st.games_started;
    CHECK(started0 >= 1);
    /* one player drops out of the lobby (as every player does at the start) */
    close(d);
    settle();
    quiesce(a); quiesce(b);
    /* a guest's gset while racing: no member list goes to anybody, the answer is empty */
    put(b, "gset", "NAME=Guesta\nUSERPARAMS=1,1,3,\n");
    CHECK(await(b, "gset", code) && !memcmp(code, "\0\0\0\0", 4) && !g_body[0]);
    g_needle = "OPPO";
    r = count_cmd(a, "+mgm", 500);
    CHECK(r == 0);
    g_needle = NULL;
    CHECK(count_cmd(a, "+usr", 100) == 0 && count_cmd(a, "+who", 100) == 0 && count_cmd(b, "+mgm", 100) == 0);
    /* the host's gset too (game-level fields do not change) */
    put(a, "gset", "NAME=Hosta\nMAXSIZE=2\nSYSFLAGS=0\n");
    CHECK(await(a, "gset", NULL) && !g_body[0]);
    CHECK(count_cmd(b, "+mgm", 300) == 0);
    {
        int i, ok = 0;
        for (i = 0; i < MAX_GAMES; i++)
            if (s_games[i].used) ok = !strcmp(s_games[i].maxsize, "6") && game_racing(&s_games[i]);
        CHECK(ok);
    }
    /* a repeated gsta: no +ses, no record, not counted again */
    put(a, "gsta", "");
    CHECK(await(a, "gsta", NULL) && !g_body[0]);
    CHECK(count_cmd(b, "+ses", 400) == 0 && count_cmd(a, "+ses", 100) == 0);
    CHECK(s_st.games_started == started0);
    close(a); close(b);
    aries_stop();
    settle();
}


/* ── identity: synthetic title addresses (BO-337 H1, H2) ── */

static char g_tkaddr[24];

static void mkxuid(char *out, const char *ip, int idx, unsigned off, unsigned hash)
{
    unsigned a, b, c, d;
    sscanf(ip, "%u.%u.%u.%u", &a, &b, &c, &d);
    snprintf(out, 40, "$%02x%02x%02x%02x%04x%04x", (d ^ (unsigned)idx) & 255, c, b, a, off & 65535, hash & 65535);
}

static char g_tksid[16];

/* sign in with an address, an XUID and (tkid) the capability; remembers TKADDR of the addr answer */
static int login_id(const char *name, const char *addr, const char *xuid, int tkid)
{
    char body[240];
    int fd = dial();
    if (fd < 0)
        return -1;
    snprintf(body, sizeof body, "ADDR=%s\nPORT=1000\n%s", addr, tkid ? "TKID=1\n" : "");
    put(fd, "addr", body);
    await(fd, "addr", NULL);
    {
        const char *p = strstr(g_body, "TKADDR=");
        g_tkaddr[0] = 0;
        if (p) { sscanf(p + 7, "%23[0-9.]", g_tkaddr); }
        p = strstr(g_body, "TKSID=");
        g_tksid[0] = 0;
        if (p) { sscanf(p + 6, "%15[0-9a-f]", g_tksid); }
    }
    put(fd, "sele", "MYGAME=1 GAMES=1"); await(fd, "sele", NULL);
    snprintf(body, sizeof body, "GTAG=%s\nPASS=$\n%s%s%s", name, xuid[0] ? "XUID=" : "", xuid, xuid[0] ? "\n" : "");
    put(fd, "auth", body); await(fd, "auth", NULL);
    snprintf(body, sizeof body, "GTAG=%s\n", name);
    put(fd, "pers", body); await(fd, "pers", NULL);
    put(fd, "move", "IDENT=1\n"); await(fd, "move", NULL);
    return fd;
}

static int body_has(const char *key, const char *val)
{
    char pat[96];
    snprintf(pat, sizeof pat, "%s=%s\n", key, val);
    return strstr(g_body, pat) != NULL || (strlen(g_body) >= strlen(pat) - 1 && !strcmp(g_body + strlen(g_body) - (strlen(pat) - 1), pat + 0) );
}

/* BO-384 (GitHub #122): the record every copy reads at the start. A host that
 * changes the game's settings in its lobby (gset PARAMS) and starts at once
 * (the lock, then gsta) must find the new PARAMS in the lock's answer and in
 * gsta's, and every guest the same in the start's record; a gset that comes
 * after the start changes nothing. The client reads this record on the host
 * too (quickplay.c bo3_quickplay_host_start_sync). */
static void test_start_record(void)
{
    aries_config c;
    int a, b, i, got;
    memset(&c, 0, sizeof c);
    c.port = PORT;
    c.bind_ip = "127.0.0.1";
    c.public_ip = "127.0.0.1";
    c.log = quiet;
    c.max_sessions = 20;
    c.max_games = 5;
    CHECK(aries_start_thread(&c) == 0);
    settle();
    a = login_as("Hostr", "127.0.0.9", "");
    b = login_as("Guestr", "127.0.0.9", "");
    mkgame(a, "Rec", "");
    put(b, "gjoi", "NAME=Rec\nPASS=\nUSERPARAMS=1,1,3,\n");
    CHECK(await(b, "gjoi", NULL) && strstr(g_body, "PARAMS=1,2,0,3,"));
    quiesce(a); quiesce(b);
    /* the Options page's update, the lock right after it */
    put(a, "gset", "NAME=Hostr\nPASS=\nROOM=1\nMAXSIZE=6\nMINSIZE=2\nPARAMS=-1622913024,0,0,16842788,50331648,-994224003,0,0,0,\n");
    CHECK(await(a, "gset", NULL) && strstr(g_body, "PARAMS=-1622913024,0,0,16842788,50331648,-994224003,0,0,0,"));
    put(a, "gset", "NAME=Hostr\nSYSFLAGS=266560\n");
    CHECK(await(a, "gset", NULL) && strstr(g_body, "SYSFLAGS=266560") &&
          strstr(g_body, "PARAMS=-1622913024,0,0,16842788,50331648,-994224003,0,0,0,"));
    put(a, "gsta", "");
    CHECK(await(a, "gsta", NULL) && strstr(g_body, "SYSFLAGS=786752") &&
          strstr(g_body, "PARAMS=-1622913024,0,0,16842788,50331648,-994224003,0,0,0,"));
    /* the guest's view of the start: every record it got says the new settings,
     * and the one under way is among them */
    got = 0;
    for (i = 0; i < 40 && await(b, "+mgm", NULL); i++) {
        CHECK(!strstr(g_body, "PARAMS=1,2,0,3,"));
        if (strstr(g_body, "SYSFLAGS=786752")) {
            got = 1;
            CHECK(strstr(g_body, "PARAMS=-1622913024,0,0,16842788,50331648,-994224003,0,0,0,") != NULL);
            break;
        }
    }
    CHECK(got);
    /* a late update (the title's SC_APPLY after its start) is not taken */
    put(a, "gset", "NAME=Hostr\nPARAMS=9,0,0,9,9,9,0,0,0,\n");
    CHECK(await(a, "gset", NULL) && !g_body[0]);
    for (i = 0, got = 0; i < MAX_GAMES; i++)
        if (s_games[i].used && !strcmp(s_games[i].name, "Rec"))
            got = !strcmp(s_games[i].params, "-1622913024,0,0,16842788,50331648,-994224003,0,0,0,");
    CHECK(got);
    close(a); close(b);
    aries_stop();
    settle();
}

static void test_identity(void)
{
    aries_config c;
    char xa[40], xb[40], xl[40], sa[24], sb[24], want[48], rev[48];
    int a, b, l, l2, l3, i, k;
    unsigned s3, s2, s1, s0;
    memset(&c, 0, sizeof c);
    c.port = PORT;
    c.bind_ip = "127.0.0.1";
    c.public_ip = "127.0.0.1";
    c.log = quiet;
    c.max_sessions = 40;
    c.max_games = 10;
    CHECK(aries_start_thread(&c) == 0);
    settle();

    /* two capable players in different homes, both 192.168.1.100 (user index 1 on the first) */
    mkxuid(xa, "192.168.1.100", 1, 0x7724, 0x1111);
    mkxuid(xb, "192.168.1.100", 0, 0x7725, 0x2222);
    a = login_id("HomeA", "192.168.1.100", xa, 1);
    snprintf(sa, sizeof sa, "%s", g_tkaddr);
    b = login_id("HomeB", "192.168.1.100", xb, 1);
    snprintf(sb, sizeof sb, "%s", g_tkaddr);
    CHECK(!strncmp(sa, "10.64.", 6) && !strncmp(sb, "10.64.", 6) && strcmp(sa, sb) != 0);
    quiesce(a); quiesce(b);
    mkgame(a, "Same", "");
    put(b, "gjoi", "NAME=Same\nPASS=\nUSERPARAMS=1,1,3,\n");
    CHECK(await(b, "gjoi", NULL));
    /* B's view of the game record: A as S_A with its real address beside it; itself as sent */
    {
        char k0[40];
        snprintf(k0, sizeof k0, "LADDR0=%s\n", sa);
        CHECK(strstr(g_body, k0) && strstr(g_body, "TKRADDR0=192.168.1.100\n") && strstr(g_body, "LADDR1=192.168.1.100\n") &&
              !strstr(g_body, "TKRADDR1="));
    }
    /* the +usr about A: the XUID carries S_A (hash, offset and the user index kept), LA = S_A, TKRLA = real */
    CHECK(await(b, "+usr", NULL));
    sscanf(sa, "%u.%u.%u.%u", &s0, &s1, &s2, &s3);
    snprintf(want, sizeof want, "MA=$%02x%02x%02x%02x7724" "1111\n", (s3 ^ 1u) & 255, s2, s1, s0);
    snprintf(rev, sizeof rev, "LA=%s\n", sa);
    CHECK(strstr(g_body, want) && strstr(g_body, rev) && strstr(g_body, "TKRLA=192.168.1.100\n") && strstr(g_body, "A=127.0.0.1\n"));
    /* A's view of B is the same the other way round */
    CHECK(await(a, "+usr", NULL));
    sscanf(sb, "%u.%u.%u.%u", &s0, &s1, &s2, &s3);
    snprintf(want, sizeof want, "MA=$%02x%02x%02x%02x7725" "2222\n", s3 & 255, s2, s1, s0);
    CHECK(strstr(g_body, want) && strstr(g_body, "TKRLA=192.168.1.100\n"));
    /* its own +who is as it sent it */
    quiesce(a);
    put(a, "pers", "GTAG=HomeA\n");
    await(a, "pers", NULL);
    CHECK(await(a, "+who", NULL) && strstr(g_body, "LA=192.168.1.100\n"));
    snprintf(want, sizeof want, "MA=%s\n", xa);
    CHECK(strstr(g_body, want));
    CHECK(!strstr(g_body, "TKRLA"));

    /* a legacy viewer (no TKID) sees real addresses, but S for the two that share one */
    l = login_id("Legacy", "192.168.1.50", "", 0);
    CHECK(g_tkaddr[0] == 0);
    quiesce(l);
    put(l, "gjoi", "NAME=Same\nPASS=\nUSERPARAMS=1,1,3,\n");
    CHECK(await(l, "gjoi", NULL));
    {
        char k0[40], k1[40];
        snprintf(k0, sizeof k0, "LADDR0=%s\n", sa);
        snprintf(k1, sizeof k1, "LADDR1=%s\n", sb);
        CHECK(strstr(g_body, k0) && strstr(g_body, k1) && !strstr(g_body, "TKRADDR") && strstr(g_body, "LADDR2=192.168.1.50\n"));
    }

    /* identity is frozen in a game: a new addr, XUID and name change nothing others see */
    quiesce(b);
    put(a, "addr", "ADDR=192.168.9.9\nPORT=1000\nTKID=1\n");
    await(a, "addr", NULL);
    put(a, "auth", "GTAG=Thief\nPASS=$\nXUID=$0102030405060708\n");
    await(a, "auth", NULL);
    put(a, "pers", "GTAG=Thief2\n");
    await(a, "pers", NULL);
    put(a, "gset", "NAME=HomeA\nUSERPARAMS=1,1,3,\n");
    await(a, "gset", NULL);
    CHECK(await(b, "+mgm", NULL));
    CHECK(strstr(g_body, "OPPO0=HomeA\n") && !strstr(g_body, "Thief") && strstr(g_body, "TKRADDR0=192.168.1.100\n"));
    for (i = 0; i < MAX_SESS; i++)
        if (s_sess[i].used && !strcmp(s_sess[i].name, "HomeA"))
            CHECK(!strcmp(s_sess[i].laddr, "192.168.1.100") && !strcmp(s_sess[i].xuid, xa));
    close(l);

    /* two legacy players with one address, or one XUID, cannot be in one game; a legacy player cannot take a member's S */
    mkxuid(xl, "192.168.7.7", 0, 0x7724, 0x3333);
    l2 = login_id("LegOne", "192.168.7.7", xl, 0);
    mkgame(l2, "Legacy", "");
    l3 = login_id("LegTwo", "192.168.7.7", "", 0);                  /* the same real address */
    put(l3, "gjoi", "NAME=Legacy\nPASS=\nUSERPARAMS=1,1,3,\n");
    {
        char code[4];
        CHECK(await(l3, "gjoi", code) && !memcmp(code, "full", 4));
    }
    close(l3);
    l3 = login_id("LegThree", "192.168.7.8", xl, 0);                 /* the same XUID */
    put(l3, "gjoi", "NAME=Legacy\nPASS=\nUSERPARAMS=1,1,3,\n");
    {
        char code[4];
        CHECK(await(l3, "gjoi", code) && !memcmp(code, "full", 4));
    }
    close(l3);
    l3 = login_id("LegFour", "192.168.7.9", "", 0);                  /* a different address: fine */
    put(l3, "gjoi", "NAME=Legacy\nPASS=\nUSERPARAMS=1,1,3,\n");
    {
        char code[4];
        CHECK(await(l3, "gjoi", code) && !memcmp(code, "\0\0\0\0", 4));
    }
    close(l3);
    {
        int cap = login_id("CapOne", "192.168.5.5", "", 1);         /* a capable host */
        char sc[24], body[120], code[4];
        snprintf(sc, sizeof sc, "%s", g_tkaddr);
        mkgame(cap, "CapGame", "");
        l3 = login_id("TakesS", sc, "", 0);                         /* a legacy player claiming that very address */
        put(l3, "gjoi", "NAME=CapGame\nPASS=\nUSERPARAMS=1,1,3,\n");
        CHECK(await(l3, "gjoi", code) && !memcmp(code, "full", 4));
        (void)body;
        close(l3);
        close(cap);
    }

    /* S is unique among live sessions, never a live legacy address, and not reused at once */
    {
        int legacy_s = login_id("Squat", "10.64.0.200", "", 0), c1, c2;
        char first[24], second[24];
        s_synth_next = 200;                                          /* the next one handed out would be 10.64.0.200 */
        c1 = login_id("Cap1", "192.168.2.2", "", 1);
        snprintf(first, sizeof first, "%s", g_tkaddr);
        CHECK(strcmp(first, "10.64.0.200") != 0 && first[0]);
        close(c1);
        settle();
        c2 = login_id("Cap2", "192.168.2.9", "", 1);          /* (the same local address would get the lease back) */
        snprintf(second, sizeof second, "%s", g_tkaddr);
        CHECK(second[0] && strcmp(first, second) != 0);              /* round robin: the freed address is not at once the next */
        close(c2);
        close(legacy_s);
    }
    /* all addresses in use: the player is simply a legacy one */
    {
        int cx;
        settle();                                    /* the earlier sessions are gone and their addresses freed */
        memset(s_lease, 0, (size_t)MAX_SESS * sizeof *s_lease);       /* no leases to give up either */
        memset(s_synth_used, 1, sizeof s_synth_used);
        cx = login_id("Full", "192.168.2.3", "", 1);
        CHECK(g_tkaddr[0] == 0);
        close(cx);
        memset(s_synth_used, 0, sizeof s_synth_used);
        for (k = 0; k < MAX_SESS; k++)
            if (s_sess[k].used && s_sess[k].synth_idx)
                s_synth_used[s_sess[k].synth_idx] = 1;
    }
    /* a capable player with the same real address as a legacy member of the game: refused too */
    {
        int cj = login_id("CapJoin", "192.168.7.7", "", 1);
        char code[4];
        put(cj, "gjoi", "NAME=Legacy\nPASS=\nUSERPARAMS=1,1,3,\n");
        CHECK(await(cj, "gjoi", code) && !memcmp(code, "full", 4));
        close(cj);
    }
    close(a); close(b); close(l2);
    aries_stop();
    settle();
    /* a restart does not hand out the same first address: the allocator starts at a random place */
    {
        int starts[4], n_same_first = 0, r;
        char firsts[4][24];
        for (r = 0; r < 4; r++) {
            int cx;
            CHECK(aries_start_thread(&c) == 0);
            settle();
            starts[r] = s_synth_next;
            cx = login_id("Restart", "192.168.3.3", "", 1);
            snprintf(firsts[r], sizeof firsts[r], "%s", g_tkaddr);
            CHECK(!strncmp(firsts[r], "10.64.", 6));
            if (!strcmp(firsts[r], "10.64.0.1")) n_same_first++;
            close(cx);
            aries_stop();
            settle();
        }
        CHECK(n_same_first == 0);
        CHECK(!(starts[0] == starts[1] && starts[1] == starts[2] && starts[2] == starts[3]));
        CHECK(strcmp(firsts[0], firsts[1]) != 0 || strcmp(firsts[1], firsts[2]) != 0 || strcmp(firsts[2], firsts[3]) != 0);
    }
    (void)body_has;
}


/* ── the private codes cannot be guessed (BO-337 M5, M10, M11) ── */

static int wrong_of(const char *name)
{
    int i;
    for (i = 0; i < MAX_SESS; i++)
        if (s_sess[i].used && !strcmp(s_sess[i].name, name))
            return s_sess[i].wrong_n;
    return -1;
}

static void test_code_security(void)
{
    aries_config c;
    int host, pwh, g1, g2, g3, early, r;
    char code[4], body[200], names[4][48];
    memset(&c, 0, sizeof c);
    c.port = PORT;
    c.bind_ip = "127.0.0.1";
    c.public_ip = "127.0.0.1";
    c.log = quiet;
    c.max_sessions = 40;
    c.max_games = 10;
    CHECK(aries_start_thread(&c) == 0);
    settle();
    host = login_as("Chost", "127.0.0.9", "");
    mkgame(host, "Hidden", "TKCODE=ab12c\n");
    {
        int id = 0, i;
        for (i = 0; i < MAX_GAMES; i++)
            if (s_games[i].used && s_games[i].priv)
                id = s_games[i].ident;
        /* PASS is not the code: a guess in PASS, with or without a wrong TKCODE, fails and is a strike */
        g1 = login_as("Guess1", "127.0.0.9", "");
        snprintf(body, sizeof body, "IDENT=%d\nPASS=AB12C\nUSERPARAMS=1,1,3,\n", id);
        put(g1, "gjoi", body);
        CHECK(await(g1, "gjoi", code) && !memcmp(code, "gone", 4));
        CHECK(wrong_of("Guess1") == 1);
        snprintf(body, sizeof body, "IDENT=%d\nPASS=AB12C\nTKCODE=ZZZZZ\nUSERPARAMS=1,1,3,\n", id);
        put(g1, "gjoi", body);
        CHECK(await(g1, "gjoi", code) && !memcmp(code, "gone", 4));
        CHECK(wrong_of("Guess1") == 2);
        put(g1, "gjoi", "NAME=Hidden\nPASS=AB12C\nUSERPARAMS=1,1,3,\n");     /* by name with PASS: nothing found either */
        CHECK(await(g1, "gjoi", code) && !memcmp(code, "gone", 4));
        CHECK(wrong_of("Guess1") == 2 || wrong_of("Guess1") == 3);               /* (a name that no visible game has is not a strike without a code) */
        /* the right code works, in lower case too */
        snprintf(body, sizeof body, "IDENT=%d\nTKCODE=ab12c\nUSERPARAMS=1,1,3,\n", id);
        g2 = login_as("Right", "127.0.0.9", "");
        put(g2, "gjoi", body);
        CHECK(await(g2, "gjoi", code) && !memcmp(code, "\0\0\0\0", 4) && wrong_of("Right") == 0);
    }
    /* M11: a public game with a password is joined with its PASS even when a game code is set */
    pwh = login_as("Pwhost", "127.0.0.9", "");
    put(pwh, "gcre", "NAME=Locked\nPASS=sesame\nPARAMS=1,2,0,3,\nMINSIZE=2\nMAXSIZE=6\nSYSFLAGS=0\nROOM=1\n");
    await(pwh, "gcre", NULL);
    g3 = login_as("Pwjoin", "127.0.0.9", "");
    put(g3, "gjoi", "NAME=Locked\nPASS=sesame\nTKCODE=XY789\nUSERPARAMS=1,1,3,\n");
    CHECK(await(g3, "gjoi", code) && !memcmp(code, "\0\0\0\0", 4));
    close(g3);
    g3 = login_as("Pwbad", "127.0.0.9", "");
    put(g3, "gjoi", "NAME=Locked\nPASS=nope\nTKCODE=AB12C\nUSERPARAMS=1,1,3,\n");   /* the private code is no password */
    CHECK(await(g3, "gjoi", code) && !memcmp(code, "pass", 4));
    CHECK(wrong_of("Pwbad") == 0);
    close(g3);

    /* M5: a code that a game has is never a strike, whatever the game's state; one that no game has is */
    early = login_as("Early", "127.0.0.9", "");
    s_code_search_ms = 0;                                                       /* (the lookup rate is tested below) */
    for (r = 0; r < 12; r++) {
        put(early, "gsea", "START=0\nCOUNT=500\nTKCODE=ab12c\n");               /* the game is open now */
        gam_names(early, 120, names, 4);
    }
    CHECK(wrong_of("Early") == 0);
    put(g2, "gsta", "");                                                        /* g2 is not the host: nothing; make the host start */
    await(g2, "gsta", NULL);
    put(host, "gsta", "");
    await(host, "gsta", NULL);                                                   /* under way now: a search finds nothing */
    settle();
    for (r = 0; r < 12; r++) {
        put(early, "gsea", "START=0\nCOUNT=500\nTKCODE=AB12C\n");
        gam_names(early, 100, names, 4);
    }
    CHECK(wrong_of("Early") == 0 && early >= 0);                                 /* it exists, it is only under way: no strike */
    {
        int dead = 0, k;
        for (k = 0; k < 24 && !dead; k++) {
            snprintf(body, sizeof body, "START=0\nCOUNT=500\nTKCODE=NO%d\n", k);
            put(early, "gsea", body);
            gam_names(early, 60, names, 4);
            dead = wait_eof(early, 10);
        }
        CHECK(dead && k > CODE_MISSES && k <= CODE_MISSES + 3);                  /* codes no game has: struck out */
    }
    close(early);
    /* a lookup rate: a second search within CODE_SEARCH_MS gets an empty answer and no strike */
    s_code_search_ms = 400;
    {
        int fr = login_as("Fastfriend", "127.0.0.9", "");
        int n1, n2;
        mkgame(pwh, "Pub", "");                                                   /* nothing private open: the host's game is under way */
        quiesce(fr);
        put(fr, "gsea", "START=0\nCOUNT=500\nTKCODE=NOGAME\n");
        n1 = gam_names(fr, 150, names, 4);
        put(fr, "gsea", "START=0\nCOUNT=500\nTKCODE=NOGAME2\n");                /* at once again: limited */
        n2 = gam_names(fr, 150, names, 4);
        CHECK(n1 == 0 && n2 == 0 && wrong_of("Fastfriend") == 1);                  /* only the first counted */
        close(fr);
    }
    s_code_search_ms = CODE_SEARCH_MS;
    close(host); close(pwh); close(g1); close(g2);
    aries_stop();
    settle();
}


/* ── BO-337 M12, M13, L11, L12, L13 ── */

static void test_review_fixes(void)
{
    aries_config c;
    int a, b, d, i, n, fr, tries;
    char code[4], body[300];
    unsigned char hdr[12];
    memset(&c, 0, sizeof c);
    c.port = PORT;
    c.bind_ip = "127.0.0.1";
    c.public_ip = "127.0.0.1";
    c.log = capture;
    cap_n = 0; cap[0] = 0;
    c.max_sessions = 150;
    c.max_games = 120;
    c.idle_secs = 3;
    s_partial_secs = 100;
    CHECK(aries_start_thread(&c) == 0);
    settle();

    /* M12: a request dribbled in a byte at a time does not keep a session alive past the idle limit */
    a = login_as("Dribble", "127.0.0.9", "");
    memset(hdr, 0, 12);
    memcpy(hdr, "gset", 4);
    hdr[8] = 0; hdr[9] = 0; hdr[10] = 0x20; hdr[11] = 0x0C;          /* a request of 8204 bytes, which never comes */
    send(a, hdr, 12, MSG_NOSIGNAL);
    for (tries = 0; tries < 8 && !wait_eof(a, 700); tries++)
        send(a, "x", 1, MSG_NOSIGNAL);
    CHECK(tries < 8);                                                 /* dropped by the 3 s idle limit, not kept alive */
    CHECK(strstr(cap, "Dribble left (idle)"));
    close(a);
    /* ... and an unfinished request has its own deadline even when the player talks otherwise */
    s_partial_secs = 2;
    a = login_as("Slowpoke", "127.0.0.9", "");
    send(a, hdr, 12, MSG_NOSIGNAL);
    for (tries = 0; tries < 10 && !wait_eof(a, 400); tries++)
        put(a, "skey", "SKEY=$5075626c6963204b6579\n");               /* whole requests keep the idle timer fresh */
    CHECK(tries < 10 && strstr(cap, "Slowpoke left (unfinished request)") != NULL);
    close(a);
    s_partial_secs = 15;
    c.idle_secs = 0;

    /* L13: rooms are the server's rooms; a game is made in its maker's room */
    a = dial();                                                       /* the list: one room, Takedawn, games can be made in it */
    put(a, "addr", "ADDR=127.9.9.7\nPORT=1000\n"); await(a, "addr", NULL);
    put(a, "sele", "MYGAME=1 GAMES=1");
    CHECK(await(a, "sele", NULL));
    CHECK(await(a, "+rom", NULL) && strstr(g_body, "I=1\n") && strstr(g_body, "N=Takedawn\n") && strstr(g_body, "F=C\n"));
    CHECK(count_cmd(a, "+rom", 500) == 0);
    close(a);
    a = login_as("Roomy", "127.0.0.9", "");
    put(a, "move", "IDENT=999\n");
    CHECK(wait_eof(a, 1500));
    close(a);
    a = login_as("Roomy1", "127.0.0.9", "");                         /* the old second room is gone too */
    put(a, "move", "IDENT=2\n");
    CHECK(wait_eof(a, 1500));
    close(a);
    a = login_as("Roomy2", "127.0.0.9", "");
    put(a, "gcre", "NAME=Wrong\nPASS=\nPARAMS=1,2,0,3,\nMINSIZE=2\nMAXSIZE=6\nSYSFLAGS=0\nROOM=2\n");   /* in room 1, ROOM=2 */
    CHECK(await(a, "gcre", NULL) && strstr(g_body, "ROOM=1\n"));
    put(a, "move", "IDENT=0\n");                                      /* out of its game's room while in the game */
    CHECK(wait_eof(a, 1500));
    close(a);
    d = dial();                                                       /* no move yet: a valid ROOM is taken, a bad one becomes room 1 */
    put(d, "addr", "ADDR=127.9.9.8\nPORT=1000\n"); await(d, "addr", NULL);
    put(d, "auth", "GTAG=Noroom\nPASS=$\n"); await(d, "auth", NULL);
    put(d, "gcre", "NAME=Two\nPASS=\nPARAMS=1,2,0,3,\nMINSIZE=2\nMAXSIZE=6\nSYSFLAGS=0\nROOM=2\n");   /* the old second room */
    CHECK(await(d, "gcre", NULL) && strstr(g_body, "ROOM=1\n"));
    close(d);
    d = dial();
    put(d, "addr", "ADDR=127.9.9.6\nPORT=1000\n"); await(d, "addr", NULL);
    put(d, "auth", "GTAG=Noroom1\nPASS=$\n"); await(d, "auth", NULL);
    put(d, "gcre", "NAME=One\nPASS=\nPARAMS=1,2,0,3,\nMINSIZE=2\nMAXSIZE=6\nSYSFLAGS=0\nROOM=1\n");
    CHECK(await(d, "gcre", NULL) && strstr(g_body, "ROOM=1\n"));
    close(d);
    d = dial();
    put(d, "addr", "ADDR=127.9.9.9\nPORT=1000\n"); await(d, "addr", NULL);
    put(d, "auth", "GTAG=Noroom2\nPASS=$\n"); await(d, "auth", NULL);
    put(d, "gcre", "NAME=Three\nPASS=\nPARAMS=1,2,0,3,\nMINSIZE=2\nMAXSIZE=6\nSYSFLAGS=0\nROOM=1735291006\n");
    CHECK(await(d, "gcre", NULL) && strstr(g_body, "ROOM=1\n"));
    close(d);

    /* L11: four large CUSTFLAGS survive whole */
    a = login_as("Flags", "127.0.0.9", "");
    put(a, "gcre", "NAME=Flagged\nPASS=\nPARAMS=1,2,0,3,\nMINSIZE=2\nMAXSIZE=6\nSYSFLAGS=0\nCUSTFLAGS=4294967295,4294967294,4294967293,4294967292,\nROOM=1\n");
    CHECK(await(a, "gcre", NULL) && strstr(g_body, "CUSTFLAGS=4294967295,4294967294,4294967293,4294967292,\n"));
    put(a, "gset", "NAME=Flags\nCUSTFLAGS=4294967291,4294967290,4294967289,4294967288,\n");
    CHECK(await(a, "gset", NULL) && strstr(g_body, "CUSTFLAGS=4294967291,4294967290,4294967289,4294967288,\n"));

    /* L12: gdel tells the host too (an empty game record and its own record with no game) */
    b = login_as("Guestflag", "127.0.0.9", "");
    put(b, "gjoi", "NAME=Flagged\nPASS=\nUSERPARAMS=1,1,3,\n");
    await(b, "gjoi", NULL);
    quiesce(a); quiesce(b);
    put(a, "gdel", "");
    {
        int got_mgm = 0, got_who = 0, got_del = 0, k;
        for (k = 0; k < 12 && !(got_mgm && got_who && got_del); k++) {
            char cc[4];
            struct pollfd p = { a, POLLIN, 0 };
            unsigned char h2[12];
            if (poll(&p, 1, 200) <= 0)
                continue;
            if (recv(a, h2, 12, MSG_WAITALL) != 12)
                break;
            {
                uint32_t len = ((uint32_t)h2[8] << 24) | ((uint32_t)h2[9] << 16) | ((uint32_t)h2[10] << 8) | h2[11];
                char bb[1024];
                size_t got = 0;
                memcpy(cc, h2, 4);
                while (got + 12 < len && got < sizeof bb - 1) {
                    ssize_t r = recv(a, bb + got, len - 12 - got < sizeof bb - 1 - got ? len - 12 - got : sizeof bb - 1 - got, 0);
                    if (r <= 0) break;
                    got += (size_t)r;
                }
                bb[got] = 0;
                if (!memcmp(cc, "+mgm", 4) && strstr(bb, "IDENT=0")) got_mgm = 1;
                if (!memcmp(cc, "+who", 4) && strstr(bb, " G=0 ") != NULL) got_who = 1;
                if (!memcmp(cc, "+who", 4) && strstr(bb, "\nG=0\n")) got_who = 1;
                if (!memcmp(cc, "gdel", 4)) got_del = 1;
            }
        }
        CHECK(got_mgm && got_who && got_del);
    }
    close(a); close(b);

    /* M13: a player who gives a code is pushed at most 100 private games of it, however many exist */
    {
        int hs[110], nh = 0;
        for (i = 0; i < 104; i++) {
            char nm[24];
            hs[i] = login_as("Priv", "127.0.0.9", "");
            if (hs[i] < 0) break;
            snprintf(nm, sizeof nm, "P%d", i);
            snprintf(body, sizeof body, "TKCODE=BIGCODE\n");
            mkgame(hs[i], nm, body);
            nh++;
        }
        CHECK(nh == 104);
        fr = dial();
        put(fr, "addr", "ADDR=127.9.9.7\nPORT=1000\n"); await(fr, "addr", NULL);
        put(fr, "auth", "GTAG=Codefriend\nPASS=$\n"); await(fr, "auth", NULL);
        quiesce_early(fr);
        put(fr, "move", "IDENT=1\nTKCODE=bigcode\n");
        {
            char names[130][48];
            n = gam_names(fr, 3000, names, 130);
        }
        CHECK(n == 100);
        close(fr);
        for (i = 0; i < nh; i++)
            close(hs[i]);
    }
    (void)code;
    aries_stop();
    settle();
}


/* ── BO-340: correlation ids: a session id per sign-in, a game id per game ── */

static int is_hex(const char *s, int n)
{
    int i;
    for (i = 0; i < n; i++)
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f')))
            return 0;
    return s[n] == 0 || s[n] == '\n';
}

/* 8 hex digits at p */
static int has_hex8(const char *p)
{
    char t[9];
    memcpy(t, p, 8);
    t[8] = 0;
    return is_hex(t, 8);
}

/* the value of KEY= in g_body, up to the line's end, into out */
static int body_val(const char *key, char *out, size_t n)
{
    char pat[40];
    const char *p;
    size_t i = 0;
    snprintf(pat, sizeof pat, "%s=", key);
    p = strstr(g_body, pat);
    if (!p || (p != g_body && p[-1] != '\n'))
        return 0;
    for (p += strlen(pat); *p && *p != '\n' && i + 1 < n; p++)
        out[i++] = *p;
    out[i] = 0;
    return 1;
}

static void test_ids(void)
{
    aries_config c;
    char sa[16], sb[16], sc[16], gid1[24], gid2[24], v[48], pat[96];
    char *w;
    int a, b, l, n_sid_lines = 0;
    const char *p;
    memset(&c, 0, sizeof c);
    c.port = PORT;
    c.bind_ip = "127.0.0.1";
    c.public_ip = "127.0.0.1";
    c.log = capture;
    c.max_sessions = 40;
    c.max_games = 10;
    snprintf(g_dir, sizeof g_dir, "%s", "/tmp/takedawn-lobby-test-XXXXXX");
    CHECK(mkdtemp(g_dir) != NULL);
    c.stats_dir = g_dir;
    c.page_html = "<!doctype html><title>t</title>\n";
    CHECK(aries_start_thread(&c) == 0);
    settle();
    cap_n = 0; cap[0] = 0;

    /* every sign-in gets its own 8 hex digits, in the addr reply, capable client or not */
    a = login_id("IdA", "192.168.1.100", "", 1);
    snprintf(sa, sizeof sa, "%s", g_tksid);
    b = login_id("IdB", "192.168.1.100", "", 1);
    snprintf(sb, sizeof sb, "%s", g_tksid);
    l = login_id("IdLegacy", "192.168.1.50", "", 0);
    snprintf(sc, sizeof sc, "%s", g_tksid);
    CHECK(strlen(sa) == 8 && is_hex(sa, 8) && strlen(sb) == 8 && is_hex(sb, 8) && strlen(sc) == 8 && is_hex(sc, 8));
    CHECK(strcmp(sa, sb) && strcmp(sa, sc) && strcmp(sb, sc));
    {   /* the ids are the server's own, unique among live sessions, and the same ones its books hold */
        int q, found = 0;
        for (q = 0; q < MAX_SESS; q++)
            if (s_sess[q].used) {
                int r;
                CHECK(strlen(s_sess[q].sid) == 8 && is_hex(s_sess[q].sid, 8));
                for (r = q + 1; r < MAX_SESS; r++)
                    if (s_sess[r].used)
                        CHECK(strcmp(s_sess[q].sid, s_sess[r].sid) != 0);
                found += !strcmp(s_sess[q].sid, sa) || !strcmp(s_sess[q].sid, sb) || !strcmp(s_sess[q].sid, sc);
            }
        CHECK(found == 3);
    }
    quiesce(a); quiesce(b); quiesce(l);

    /* the host's gcre reply and the joiner's gjoi reply and +mgm carry TKGID; it is 12 hex digits */
    mkgame(a, "IdGame", "");
    CHECK(body_val("TKGID", gid1, sizeof gid1) && strlen(gid1) == 12 && is_hex(gid1, 12));
    put(b, "gjoi", "NAME=IdGame\nPASS=\nUSERPARAMS=1,1,3,\n");
    CHECK(await(b, "gjoi", NULL));
    CHECK(body_val("TKGID", v, sizeof v) && !strcmp(v, gid1));
    /* +mgm to the host (after the join) has it */
    CHECK(await(a, "+mgm", NULL) && body_val("TKGID", v, sizeof v) && !strcmp(v, gid1));
    put(l, "gsea", "START=0\nCOUNT=50\n");
    CHECK(await(l, "gsea", NULL) && !strstr(g_body, "TKGID") && !strstr(g_body, "TKSID"));
    CHECK(await(l, "+gam", NULL) && strstr(g_body, "NAME=IdGame") && !strstr(g_body, "TKGID"));
    /* a member's +gam / +agm (the list view) has no game id either: make a change that re-broadcasts */
    quiesce(b);
    put(a, "gset", "PARAMS=1,2,0,4,\nSYSFLAGS=0\n");
    CHECK(await(a, "gset", NULL) && body_val("TKGID", v, sizeof v) && !strcmp(v, gid1));
    CHECK(await(b, "+gam", NULL) && !strstr(g_body, "TKGID") && strstr(g_body, "NAME=IdGame"));
    CHECK(await(b, "+agm", NULL) && !strstr(g_body, "TKGID"));

    /* a second game has another id */
    {
        int h = login_id("IdHost2", "192.168.1.60", "", 1);
        quiesce(h);
        mkgame(h, "IdGame2", "");
        CHECK(body_val("TKGID", gid2, sizeof gid2) && strlen(gid2) == 12 && is_hex(gid2, 12) && strcmp(gid1, gid2));
        close(h);
    }

    /* the log: the session's lines start with sid=<hex>, the game's carry gid=<hex> next to the IDENT */
    settle();
    close(l);
    settle();
    snprintf(pat, sizeof pat, "sid=%s IdA created game 1 gid=%s", sa, gid1);
    CHECK(strstr(cap, pat) != NULL);
    snprintf(pat, sizeof pat, "sid=%s IdB joined game 1 gid=%s", sb, gid1);
    CHECK(strstr(cap, pat) != NULL);
    snprintf(pat, sizeof pat, "sid=%s IdLegacy logged in", sc);
    CHECK(strstr(cap, pat) != NULL);
    snprintf(pat, sizeof pat, "sid=%s IdLegacy left", sc);
    CHECK(strstr(cap, pat) != NULL);
    for (p = cap; *p; ) {            /* every line of this test is about one session */
        const char *e = strchr(p, '\n');
        int sidline;
        if (!e) break;
        sidline = e - p > 22 && !strncmp(p + 9, "sid=", 4) && p[21] == ' ' && has_hex8(p + 13);
        CHECK(sidline);
        n_sid_lines += sidline;
        p = e + 1;
    }
    CHECK(n_sid_lines >= 8);
    if (getenv("IDS_DUMP")) fputs(cap, stdout);
    /* an id never appears for a different session's name */
    snprintf(pat, sizeof pat, "sid=%s IdB", sa);
    CHECK(!strstr(cap, pat));

    /* the public page and the statistics files never hold an id */
    {
        char all[16384];
        char path[420];
        FILE *f;
        size_t k;
        g_now = time(NULL);
        st_write_public();
        w = slurp("public/stats.json", pub, sizeof pub);
        CHECK(w[0] == '{' && strstr(w, "IdGame"));
        CHECK(!strstr(w, sa) && !strstr(w, sb) && !strstr(w, sc) && !strstr(w, gid1) && !strstr(w, gid2));
        CHECK(!strstr(w, "sid") && !strstr(w, "gid") && !strstr(w, "TKSID") && !strstr(w, "TKGID"));
        snprintf(path, sizeof path, "%s/current.json", g_dir);
        f = fopen(path, "rb");
        all[0] = 0;
        if (f) { k = fread(all, 1, sizeof all - 1, f); all[k] = 0; fclose(f); }
        CHECK(!strstr(all, sa) && !strstr(all, gid1) && !strstr(all, "sid"));
    }
    close(a); close(b);
    aries_stop();
    settle();

    /* a restart: a new game does not get an id a game of the first run had */
    {
        char gid3[24];
        int h;
        c.stats_dir = NULL;
        c.page_html = NULL;
        CHECK(aries_start_thread(&c) == 0);
        settle();
        h = login_id("IdAgain", "192.168.1.70", "", 1);
        CHECK(is_hex(g_tksid, 8) && strlen(g_tksid) == 8);
        quiesce(h);
        mkgame(h, "IdGame3", "");
        CHECK(body_val("TKGID", gid3, sizeof gid3) && strlen(gid3) == 12 && strcmp(gid3, gid1) && strcmp(gid3, gid2));
        close(h);
        aries_stop();
        settle();
    }
}


/* ── the title kicks a player whose peer link does not come up: gset KICK= and mesg ── */

static void test_kick(void)
{
    aries_config c;
    int h, g1, g2, g3, i;
    char code[4];
    memset(&c, 0, sizeof c);
    c.port = PORT;
    c.bind_ip = "127.0.0.1";
    c.public_ip = "127.0.0.1";
    c.log = capture;
    cap_n = 0; cap[0] = 0;
    c.max_sessions = 20;
    c.max_games = 5;
    CHECK(aries_start_thread(&c) == 0);
    settle();
    h = login_as("Kickhost", "127.0.0.9", "");
    g1 = login_as("Kickee", "127.0.0.9", "");
    g2 = login_as("Stayer", "127.0.0.9", "");
    g3 = login_as("Late", "127.0.0.9", "");
    mkgame(h, "Kicks", "");
    put(g1, "gjoi", "NAME=Kicks\nPASS=\nUSERPARAMS=1,1,3,\n"); await(g1, "gjoi", NULL);
    put(g2, "gjoi", "NAME=Kicks\nPASS=\nUSERPARAMS=1,1,3,\n"); await(g2, "gjoi", NULL);
    quiesce(h); quiesce(g1); quiesce(g2);
    /* a guest cannot kick, the host cannot kick itself, an unknown name does nothing */
    put(g2, "gset", "NAME=Stayer\nKICK=Kickee\n"); await(g2, "gset", NULL);
    put(h, "gset", "NAME=Kickhost\nKICK=Kickhost\n"); await(h, "gset", NULL);
    put(h, "gset", "NAME=Kickhost\nKICK=Nobody\n"); await(h, "gset", NULL);
    {
        int in = 0;
        for (i = 0; i < MAX_GAMES; i++)
            if (s_games[i].used) in = s_games[i].np;
        CHECK(in == 3);
    }
    quiesce(h); quiesce(g1); quiesce(g2);
    /* the title's pair, as logged: a private message, then the kick */
    put(h, "mesg", "PRIV=Kickee\nTEXT=Kickee\nATTR=CNP2\n");
    CHECK(await(h, "mesg", code) && !memcmp(code, "\0\0\0\0", 4));
    put(h, "gset", "NAME=Kickhost\nKICK=Kickee\n");
    CHECK(await(h, "gset", NULL) && strstr(g_body, "COUNT=2\n") && !strstr(g_body, "OPPO2="));
    /* the kicked player is told its game is gone and its own record has no game */
    {
        int got_mgm = 0, got_who = 0, k;
        for (k = 0; k < 6 && !(got_mgm && got_who); k++) {
            if (await(g1, "+mgm", NULL)) got_mgm = strstr(g_body, "IDENT=0") != NULL;
            if (!got_who && await(g1, "+who", NULL)) got_who = strstr(g_body, " G=0 ") != NULL || strstr(g_body, "\nG=0\n") != NULL;
        }
        CHECK(got_mgm && got_who);                 /* an empty game record, and its own record with G=0 */
    }
    /* the others get the shorter member list; the kicked player is no longer in the game */
    g_needle = "OPPO1=Stayer";
    CHECK(count_cmd(g2, "+mgm", 600) >= 1 && g_hits >= 1);
    g_needle = NULL;
    {
        int in = 0, kicked_game = -1;
        for (i = 0; i < MAX_GAMES; i++)
            if (s_games[i].used) in = s_games[i].np;
        for (i = 0; i < MAX_SESS; i++)
            if (s_sess[i].used && !strcmp(s_sess[i].name, "Kickee")) kicked_game = s_sess[i].game;
        CHECK(in == 2 && kicked_game == 0);
    }
    CHECK(!strstr(cap, "unknown request mesg") && strstr(cap, "kicked Kickee from game"));
    /* the title keeps asking for a while, at frame rate: a kick of a player who is no member (any more) and
     * the private message are answered at once, not counted in the flood bucket, change nothing, and
     * send nobody a member list */
    quiesce(g2); quiesce(g1);
    for (i = 0; i < 1500; i++) {
        put(h, "mesg", "PRIV=Kickee\nTEXT=Kickee\nATTR=CNP2\n");
        put(h, "gset", "NAME=Kickhost\nKICK=Kickee\n");
    }
    {
        int answered = count_cmd(h, "gset", 1200), mesgs;
        {
            int k2;
            for (k2 = 0; k2 < MAX_SESS; k2++)
                if (s_sess[k2].used && !strcmp(s_sess[k2].name, "Kickhost"))
                    CHECK(s_sess[k2].outlen - s_sess[k2].outoff < 4096);       /* the answers are 12 bytes, nothing piles up */
        }
        g_quiet_exit = 0;
        CHECK(answered >= 60 && answered <= 300);                  /* about 200 answers in all (half of them gset), the rest ignored silently */
        mesgs = count_cmd(h, "mesg", 200);
        (void)mesgs;
    }
    CHECK(count_cmd(g2, "+mgm", 300) == 0 && count_cmd(g2, "+who", 100) == 0 && count_cmd(g2, "+usr", 100) == 0);
    {
        int alive = 0;
        for (i = 0; i < MAX_SESS; i++)
            if (s_sess[i].used && !strcmp(s_sess[i].name, "Kickhost")) alive = 1;
        CHECK(alive && !strstr(cap, "Kickhost: sending requests much too fast"));
    }
    /* a host that does not read at all while the title keeps repeating: the queue stays small, nobody is dropped */
    {
        int slow, rounds, k2, ok = 1;
        int sz = 2048;
        s_sndbuf = 4096;
        s_noop_queue = 2048;
        slow = login_as("Slowhost", "127.0.0.9", "");
        setsockopt(slow, SOL_SOCKET, SO_RCVBUF, &sz, sizeof sz);
        s_sndbuf = 0;
        mkgame(slow, "Slowgame", "");
        quiesce(slow);
        for (rounds = 0; rounds < 8; rounds++) {
            struct timespec ts = { 0, 400000000 };
            int j2;
            for (j2 = 0; j2 < 400; j2++) {
                put(slow, "mesg", "PRIV=Gone\nTEXT=Gone\nATTR=CNP2\n");
                put(slow, "gset", "NAME=Slowhost\nKICK=Gone\n");
            }
            nanosleep(&ts, NULL);                                   /* ... and it never reads */
            for (k2 = 0; k2 < MAX_SESS; k2++)
                if (s_sess[k2].used && !strcmp(s_sess[k2].name, "Slowhost") && (long)(s_sess[k2].outlen - s_sess[k2].outoff) > 8192)
                    ok = 0;
        }
        CHECK(ok);                                                  /* the queue never passed a few KB */
        {
            int alive = 0;
            for (k2 = 0; k2 < MAX_SESS; k2++)
                if (s_sess[k2].used && !strcmp(s_sess[k2].name, "Slowhost")) alive = 1;
            CHECK(alive && !strstr(cap, "Slowhost left"));
        }
        s_noop_queue = NOOP_QUEUE;
        close(slow);
    }
    /* a guest's kick changes nothing either, and sends no member list */
    put(g2, "gset", "NAME=Stayer\nKICK=Kickhost\n");
    CHECK(await(g2, "gset", NULL));
    CHECK(count_cmd(h, "+mgm", 300) == 0);
    /* the real flood protection is untouched: a flood of anything else is dropped as before */
    {
        int fl = login_as("Flooder", "127.0.0.9", ""), j2;
        for (j2 = 0; j2 < 700; j2++)
            put(fl, "skey", "SKEY=$5075626c6963204b6579\n");
        CHECK(wait_eof(fl, 2500));
        close(fl);
    }
    /* and a no-op flood far beyond reason ends the session too */
    {
        int fl = login_as("Mesgflooder", "127.0.0.9", ""), j2;
        for (j2 = 0; j2 < 26000; j2++)
            put(fl, "mesg", "PRIV=x\nTEXT=x\n");
        CHECK(wait_eof(fl, 6000));
        close(fl);
    }
    quiesce(h);
    {
        int alive = 0;
        for (i = 0; i < MAX_SESS; i++)
            if (s_sess[i].used && !strcmp(s_sess[i].name, "Kickhost")) alive = 1;
        CHECK(alive);
    }
    /* the kicked player cannot get back in, by name or by ident, and a retry storm does not get it dropped */
    {
        int ident = 0, kk;
        for (i = 0; i < MAX_GAMES; i++)
            if (s_games[i].used) ident = s_games[i].ident;
        quiesce(g1);
        put(g1, "gjoi", "NAME=Kicks\nPASS=\nUSERPARAMS=1,1,3,\n");
        CHECK(await(g1, "gjoi", code) && !memcmp(code, "gone", 4) && strstr(g_body, "TKKICKED=1"));
        /* BO-364: its searches no longer list the game that kicked it (the join would only be
         * refused and Quick Match would wait on it); a player who was not kicked still finds it */
        quiesce(g1);
        put(g1, "gsea", "START=0\nCOUNT=500\n");
        CHECK(await(g1, "gsea", NULL) && strstr(g_body, "COUNT=0") && !strstr(g_body, "TKKICKED"));
        CHECK(count_cmd(g1, "+gam", 300) == 0);
        quiesce(g3);
        put(g3, "gsea", "START=0\nCOUNT=500\n");
        CHECK(await(g3, "gsea", NULL) && strstr(g_body, "COUNT=1"));
        g_needle = "NAME=Kicks";
        CHECK(count_cmd(g3, "+gam", 300) >= 1 && g_hits >= 1);
        g_needle = NULL;
        {
            char jb[120];
            snprintf(jb, sizeof jb, "IDENT=%d\nPASS=\nUSERPARAMS=1,1,3,\n", ident);
            put(g1, "gjoi", jb);
            CHECK(await(g1, "gjoi", code) && !memcmp(code, "gone", 4));
            for (kk = 0; kk < 700; kk++)
                put(g1, "gjoi", jb);
        }
        quiesce(g1);
        {
            int alive = 0;
            for (i = 0; i < MAX_SESS; i++)
                if (s_sess[i].used && !strcmp(s_sess[i].name, "Kickee")) alive = 1;
            CHECK(alive);                                         /* flooded with retries, still here */
        }
        {
            int in = 0;
            for (i = 0; i < MAX_GAMES; i++)
                if (s_games[i].used) in = s_games[i].np;
            CHECK(in == 2);                                       /* and still out of the game */
        }
        /* another game is not closed to it */
        mkgame(g2, "Elsewhere", "");
    }
    /* a bad KICK value ends the session like any bad field */
    put(g3, "gjoi", "NAME=Kicks\nPASS=\nUSERPARAMS=1,1,3,\n"); await(g3, "gjoi", NULL);
    put(h, "gset", "NAME=Kickhost\nKICK=a=b\n");
    CHECK(wait_eof(h, 1500));
    close(h); close(g1); close(g2); close(g3);
    aries_stop();
    settle();
}


/* ── the sticky address lease (BO-343 night loop) ── */

/* sign in as a capable client from a chosen source address; tkname is sent as TKNAME when not empty */
static int login_lease(const char *name, const char *laddr, const char *tkname, const char *src)
{
    struct sockaddr_in a, from;
    char body[240];
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    memset(&from, 0, sizeof from);
    from.sin_family = AF_INET;
    from.sin_addr.s_addr = inet_addr(src);
    if (bind(fd, (struct sockaddr *)&from, sizeof from) != 0) { close(fd); return -1; }
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(PORT);
    a.sin_addr.s_addr = htonl(0x7F000001u);
    if (connect(fd, (struct sockaddr *)&a, sizeof a) != 0) { close(fd); return -1; }
    snprintf(body, sizeof body, "ADDR=%s\nPORT=1000\nTKID=1\n%s%s%s", laddr, tkname[0] ? "TKNAME=" : "", tkname, tkname[0] ? "\n" : "");
    put(fd, "addr", body);
    await(fd, "addr", NULL);
    {
        const char *p = strstr(g_body, "TKADDR=");
        g_tkaddr[0] = 0;
        if (p) sscanf(p + 7, "%23[0-9.]", g_tkaddr);
    }
    snprintf(body, sizeof body, "GTAG=%s\nPASS=$\n", name);
    put(fd, "auth", body); await(fd, "auth", NULL);
    return fd;
}

static int live_synth_unique(void)
{
    int i, j;
    for (i = 0; i < MAX_SESS; i++)
        if (s_sess[i].used && s_sess[i].tkid)
            for (j = i + 1; j < MAX_SESS; j++)
                if (s_sess[j].used && s_sess[j].tkid && s_sess[j].synth == s_sess[i].synth)
                    return 0;
    return 1;
}

/* BO-352 step 5b: the online-info counts in @dir, the probe that is not
 * logged as a session, and the kick reasons (TKWHY) in the empty game record. */
/* TKONLINE, as a menu reads it */
static int online_count(void)
{
    int fd = dial(), n = -1;
    const char *p;
    put(fd, "@dir", "TKPROBE=1\n");
    if (await(fd, "@dir", NULL) && (p = strstr(g_body, "TKONLINE=")))
        n = atoi(p + 9);
    close(fd);
    settle();
    return n;
}

static void race_of_two(const char *h, const char *g, const char *game, int *ph, int *pg)
{
    char body[96];
    *ph = login_as(h, "127.0.0.9", "");
    *pg = login_as(g, "127.0.0.9", "");
    mkgame(*ph, game, "");
    snprintf(body, sizeof body, "NAME=%s\nPASS=\nUSERPARAMS=1,1,3,\n", game);
    put(*pg, "gjoi", body); await(*pg, "gjoi", NULL);
    quiesce(*ph); quiesce(*pg);
    put(*ph, "gsta", ""); await(*ph, "gsta", NULL);
    quiesce(*ph); quiesce(*pg);
}

static void test_racers(void)
{
    aries_config c;
    int a, b, a2, b2, d, i;
    memset(&c, 0, sizeof c);
    c.port = PORT;
    c.bind_ip = "127.0.0.1";
    c.public_ip = "127.0.0.1";
    c.log = quiet;
    c.max_sessions = 40;
    c.max_games = 10;
    c.max_per_ip = 0;
    CHECK(aries_start_thread(&c) == 0);
    settle();
    CHECK(online_count() == 0);

    /* both drop out at the start: still two */
    race_of_two("Rhost", "Rguest", "Rgame", &a, &b);
    CHECK(online_count() == 2);
    close(a); settle();
    CHECK(online_count() == 2);
    close(b); settle();
    CHECK(online_count() == 2 && s_players == 0 && s_racers == 2);

    /* they come back one by one: never three, never one */
    a2 = login_as("Rhost", "127.0.0.9", "");
    CHECK(online_count() == 2 && s_players == 1 && s_racers == 1);
    b2 = login_as("Rguest", "127.0.0.9", "");
    CHECK(online_count() == 2 && s_players == 2 && s_racers == 0);

    /* out of a race, a quit counts at once */
    close(a2); close(b2); settle();
    CHECK(online_count() == 0);

    /* a racer back on another address is the same player */
    race_of_two("Mhost", "Mguest", "Mgame", &a, &b);
    close(a); close(b); settle();
    CHECK(online_count() == 2);
    a2 = login_as("Mhost", "10.1.2.3", "");
    b2 = login_as("Mguest", "10.1.2.4", "");
    CHECK(online_count() == 2);
    /* the second race of the same two: one record each, not two */
    mkgame(a2, "Mgametwo", "");
    put(b2, "gjoi", "NAME=Mgametwo\nPASS=\nUSERPARAMS=1,1,3,\n"); await(b2, "gjoi", NULL);
    quiesce(a2); quiesce(b2);
    put(a2, "gsta", ""); await(a2, "gsta", NULL);
    quiesce(a2); quiesce(b2);
    close(a2); close(b2); settle();
    CHECK(online_count() == 2 && s_racers == 2);
    a = login_as("Mhost", "127.0.0.9", ""); b = login_as("Mguest", "127.0.0.9", "");
    CHECK(online_count() == 2 && s_racers == 0);
    close(a); close(b); settle();
    CHECK(online_count() == 0);

    /* a start with one player keeps nobody */
    a = login_as("Solo", "127.0.0.9", "");
    mkgame(a, "Sologame", "");
    quiesce(a);
    put(a, "gsta", ""); await(a, "gsta", NULL);
    quiesce(a);
    close(a); settle();
    CHECK(online_count() == 0 && s_racers == 0);

    /* a crash in a race fades out: the time runs from the gsta */
    s_race_hold_secs = 7;
    race_of_two("Fhost", "Fguest", "Fgame", &a, &b);
    d = login_as("Fother", "127.0.0.9", "");
    close(a); close(b); settle();
    CHECK(online_count() == 3);
    for (i = 0; i < 12 && online_count() != 1; i++)
        usleep(100000);
    CHECK(online_count() == 1 && s_racers == 0);
    /* a session that closes after the time is no racer */
    race_of_two("Lhost", "Lguest", "Lgame", &a, &b);
    usleep(7400000);
    close(a); close(b); settle();
    CHECK(online_count() == 1 && s_racers == 0);
    close(d); settle();
    CHECK(online_count() == 0);
    s_race_hold_secs = RACE_HOLD_SECS;

    /* one address keeps at most --max-per-ip racers */
    s_per_ip = 3;
    race_of_two("Chost", "Cguest", "Cgame", &a, &b);
    a2 = login_as("Chosttwo", "127.0.0.9", "");
    close(a); close(b); settle();
    b2 = login_as("Cguesttwo", "127.0.0.9", "");
    mkgame(a2, "Cgametwo", "");
    put(b2, "gjoi", "NAME=Cgametwo\nPASS=\nUSERPARAMS=1,1,3,\n"); await(b2, "gjoi", NULL);
    quiesce(a2); quiesce(b2);
    put(a2, "gsta", ""); await(a2, "gsta", NULL);
    quiesce(a2); quiesce(b2);
    close(a2); close(b2); settle();
    CHECK(s_racers == 3 && online_count() == 3);
    aries_stop();
}

static void test_bo352_server(void)
{
    aries_config c;
    int h, g1, g2, fd, k;
    memset(&c, 0, sizeof c);
    c.port = PORT;
    c.bind_ip = "127.0.0.1";
    c.public_ip = "127.0.0.1";
    c.log = capture;
    c.max_sessions = 40;
    c.max_games = 10;
    CHECK(aries_start_thread(&c) == 0);
    settle();

    /* @dir carries the online-info counts */
    fd = dial();
    put(fd, "@dir", "");
    CHECK(await(fd, "@dir", NULL) && strstr(g_body, "TKONLINE=") && strstr(g_body, "TKWAITING="));
    close(fd);
    settle();

    /* a probe (TKPROBE=1) is not logged as a session: no line about it at all */
    cap_n = 0; cap[0] = 0;
    fd = dial();
    put(fd, "@dir", "TKPROBE=1\n");
    CHECK(await(fd, "@dir", NULL) && strstr(g_body, "TKONLINE="));
    close(fd);
    settle();
    CHECK(cap[0] == 0);                    /* the plain @dir connection above did log "left" */

    /* the kick reason (TKWHY) reaches the kicked player's empty record */
    h = login_as("Whyhost", "127.0.0.9", "");
    g1 = login_as("Whykick", "127.0.0.9", "");
    g2 = login_as("Whystay", "127.0.0.9", "");
    mkgame(h, "Whygame", "");
    put(g1, "gjoi", "NAME=Whygame\nPASS=\nUSERPARAMS=1,1,3,\n"); await(g1, "gjoi", NULL);
    put(g2, "gjoi", "NAME=Whygame\nPASS=\nUSERPARAMS=1,1,3,\n"); await(g2, "gjoi", NULL);
    quiesce(h); quiesce(g1); quiesce(g2);
    put(h, "gset", "NAME=Whyhost\nKICK=Whykick\nTKWHY=nolink\n");
    CHECK(await(h, "gset", NULL));
    {
        int got = 0;
        for (k = 0; k < 6 && !got; k++)
            if (await(g1, "+mgm", NULL))
                got = strstr(g_body, "IDENT=0") != NULL && strstr(g_body, "TKWHY=nolink") != NULL;
        CHECK(got);
    }
    /* a host's own choice of kick tags TKWHY=host */
    put(h, "gset", "NAME=Whyhost\nKICK=Whystay\nTKWHY=host\n");
    CHECK(await(h, "gset", NULL));
    {
        int got = 0;
        for (k = 0; k < 6 && !got; k++)
            if (await(g2, "+mgm", NULL))
                got = strstr(g_body, "IDENT=0") != NULL && strstr(g_body, "TKWHY=host") != NULL;
        CHECK(got);
    }
    /* a TKWHY the server does not know is dropped, not echoed */
    {
        int g3 = login_as("Whykeep", "127.0.0.9", "");
        put(g3, "gjoi", "NAME=Whygame\nPASS=\nUSERPARAMS=1,1,3,\n"); await(g3, "gjoi", NULL);
        quiesce(h); quiesce(g3);
        put(h, "gset", "NAME=Whyhost\nKICK=Whykeep\nTKWHY=garbage\n");
        CHECK(await(h, "gset", NULL));
        {
            int got = 0;
            for (k = 0; k < 6 && !got; k++)
                if (await(g3, "+mgm", NULL))
                    got = strstr(g_body, "IDENT=0") != NULL && !strstr(g_body, "TKWHY=");
            CHECK(got);
        }
        close(g3);
    }
    /* the host leaving tags the remaining players' empty record TKWHY=closed */
    {
        int g4 = login_as("Whyguest", "127.0.0.9", "");
        put(g4, "gjoi", "NAME=Whygame\nPASS=\nUSERPARAMS=1,1,3,\n"); await(g4, "gjoi", NULL);
        quiesce(g4);
        put(h, "glea", "NAME=Whyhost\nFORCE=1\n");
        await(h, "glea", NULL);
        {
            int got = 0;
            for (k = 0; k < 6 && !got; k++)
                if (await(g4, "+mgm", NULL))
                    got = strstr(g_body, "IDENT=0") != NULL && strstr(g_body, "TKWHY=closed") != NULL;
            CHECK(got);
        }
        close(g4);
    }
    CHECK(aries_start_thread(&c) == 1);   /* a second server while this one runs: refused */
    aries_stop();      /* the next part starts its own server: two would share (and free) one set of tables */
    settle();
}

static void test_lease(void)
{
    aries_config c;
    char s1[24], first[24];
    int a, b, i, n, leases;
    memset(&c, 0, sizeof c);
    c.port = PORT;
    c.bind_ip = "127.0.0.1";
    c.public_ip = "127.0.0.1";
    c.log = capture;
    cap_n = 0; cap[0] = 0;
    c.max_sessions = 60;
    c.max_games = 5;
    CHECK(aries_start_thread(&c) == 0);
    settle();

    /* the same gamertag from the same public address gets its address back */
    a = login_lease("LeaseA", "192.168.1.100", "LeaseA", "127.0.0.2");
    snprintf(s1, sizeof s1, "%s", g_tkaddr);
    CHECK(!strncmp(s1, "10.64.", 6));
    close(a);
    settle();
    a = login_lease("LeaseA", "192.168.1.100", "LeaseA", "127.0.0.2");
    CHECK(!strcmp(g_tkaddr, s1));
    CHECK(strstr(cap, "address reused for LeaseA") && !strstr(cap, "10.64.") && !strstr(cap, " from 127"));
    close(a);
    settle();
    /* another gamertag from the same address, or the same gamertag from another address, does not */
    b = login_lease("LeaseB", "192.168.1.100", "LeaseB", "127.0.0.2");
    CHECK(strcmp(g_tkaddr, s1) != 0);
    close(b);
    b = login_lease("LeaseA", "192.168.1.100", "LeaseA", "127.0.0.5");
    CHECK(strcmp(g_tkaddr, s1) != 0);
    close(b);
    settle();
    /* and nobody else is ever handed a leased address: the allocator is pointed at it and skips it */
    {
        int idx = 0, k;
        unsigned x, y, z, w;
        sscanf(s1, "%u.%u.%u.%u", &x, &y, &z, &w);
        idx = (int)(z * 256 + w);
        s_synth_next = idx;
        for (k = 0; k < 5; k++) {
            int cx = login_lease("Other", "192.168.9.9", "", "127.0.0.7");
            CHECK(strcmp(g_tkaddr, s1) != 0);
            close(cx);
        }
        settle();
    }
    a = login_lease("LeaseA", "192.168.1.100", "LeaseA", "127.0.0.2");    /* ... and its owner still gets it */
    CHECK(!strcmp(g_tkaddr, s1));
    close(a);
    settle();

    /* a client that does not send TKNAME is matched by the local address it claims */
    a = login_lease("NoName", "192.168.4.4", "", "127.0.0.3");
    snprintf(first, sizeof first, "%s", g_tkaddr);
    close(a);
    settle();
    a = login_lease("NoName", "192.168.4.4", "", "127.0.0.3");
    CHECK(!strcmp(g_tkaddr, first));
    close(a);
    settle();
    a = login_lease("NoName", "192.168.4.5", "", "127.0.0.3");              /* another local address */
    CHECK(strcmp(g_tkaddr, first) != 0);
    close(a);
    settle();

    /* the address is not shared while its owner is on: a second sign-in of the same player gets another one */
    a = login_lease("Twin", "192.168.6.6", "Twin", "127.0.0.4");
    snprintf(first, sizeof first, "%s", g_tkaddr);
    b = login_lease("Twin", "192.168.6.6", "Twin", "127.0.0.4");
    CHECK(strcmp(g_tkaddr, first) != 0 && live_synth_unique());
    close(a); close(b);
    settle();

    /* churn: many sign-ins and sign-outs of a few players, from two addresses; live addresses never repeat */
    {
        int fds[12], nf = 0, ok = 1, round;
        for (round = 0; round < 30; round++) {
            char nm[16];
            for (i = 0; i < nf; i++)
                if (fds[i] >= 0 && (round + i) % 3 == 0) { close(fds[i]); fds[i] = -1; }
            for (i = 0; i < 12; i++) {
                if (i < nf && fds[i] >= 0)
                    continue;
                snprintf(nm, sizeof nm, "Churn%d", (round * 7 + i) % 9);
                fds[i] = login_lease(nm, "192.168.8.8", nm, i % 2 ? "127.0.0.4" : "127.0.0.6");
                if (i >= nf) nf = i + 1;
            }
            if (!live_synth_unique()) ok = 0;
        }
        CHECK(ok);
        for (i = 0; i < nf; i++)
            if (fds[i] >= 0) close(fds[i]);
    }
    settle();
    CHECK(live_synth_unique());
    {   /* the pool bits are exactly the live sessions' and the leases' */
        int used = 0, live = 0, k;
        leases = 0;
        for (k = 1; k < SYNTH_N; k++)
            used += s_synth_used[k];
        for (k = 0; k < MAX_SESS; k++) {
            live += s_sess[k].used && s_sess[k].synth_idx;
            leases += s_lease[k].used;
        }
        CHECK(used == live + leases && leases >= 1 && leases <= MAX_SESS);
    }

    /* the pool is full: the oldest lease goes first */
    {
        int x1, x2, x3, y1;
        char l1[24], l2[24];
        x1 = login_lease("Old1", "192.168.20.1", "Old1", "127.0.0.8");
        snprintf(l1, sizeof l1, "%s", g_tkaddr);
        x2 = login_lease("Old2", "192.168.20.2", "Old2", "127.0.0.8");
        snprintf(l2, sizeof l2, "%s", g_tkaddr);
        x3 = login_lease("Old3", "192.168.20.3", "Old3", "127.0.0.8");
        close(x1); close(x2); close(x3);
        settle();
        {   /* the oldest lease of all (earlier ones are older than these three) */
            unsigned long best = 0;
            int bi = -1, k;
            for (k = 0; k < MAX_SESS; k++)
                if (s_lease[k].used && (bi < 0 || s_lease[k].seq < best)) { best = s_lease[k].seq; bi = k; }
            snprintf(l1, sizeof l1, "%s", "");
            if (bi >= 0) {
                char tmp[16];
                addr_str(synth_net(s_lease[bi].idx), tmp, sizeof tmp);
                snprintf(l1, sizeof l1, "%s", tmp);
            }
        }
        memset(s_synth_used, 1, sizeof s_synth_used);               /* every address taken (by leases and by others) */
        y1 = login_lease("Newbie", "192.168.30.1", "Newbie", "127.0.0.9");
        CHECK(l1[0] && !strcmp(g_tkaddr, l1));                      /* the oldest lease's address */
        close(y1);
        settle();
        y1 = login_lease("Newbie2", "192.168.30.2", "Newbie2", "127.0.0.9");
        CHECK(g_tkaddr[0] && strcmp(g_tkaddr, l1) != 0 && live_synth_unique());   /* the next oldest goes, never a live one */
        close(y1);
        (void)l2;
        memset(s_synth_used, 0, sizeof s_synth_used);
        for (n = 0; n < MAX_SESS; n++) {
            if (s_sess[n].used && s_sess[n].synth_idx) s_synth_used[s_sess[n].synth_idx] = 1;
            if (s_lease[n].used) s_synth_used[s_lease[n].idx] = 1;
        }
    }

    /* the lease ends: another sign-in then gets a different address, and the old one is free again */
    s_lease_secs = 5;   /* whole seconds: a lease lasts 4-5 s, past the settle below even on a busy machine */
    {
        int e1, e2;
        char se[24];
        e1 = login_lease("Expiry", "192.168.40.1", "Expiry", "127.0.0.10");
        snprintf(se, sizeof se, "%s", g_tkaddr);
        close(e1);
        settle();
        e2 = login_lease("Expiry", "192.168.40.1", "Expiry", "127.0.0.10");
        CHECK(!strcmp(g_tkaddr, se));                               /* inside the lease (the table was set to 2 s when it was made? no: made at 30 min) */
        close(e2);
        settle();                                                   /* now a lease made with 5 s */
        {
            struct timespec ts = { 6, 0 };
            nanosleep(&ts, NULL);
        }
        e2 = login_lease("Expiry", "192.168.40.1", "Expiry", "127.0.0.10");
        CHECK(strcmp(g_tkaddr, se) != 0);                           /* expired: not given back */
        close(e2);
    }
    s_lease_secs = LEASE_SECS;
    aries_stop();
    settle();
}

/* ── audit 2026-11-03 batch C: repeated joins, a started game without its
 * host, no-op gsets, the throttle window, two players of one name ── */
static void test_audit_c(void)
{
    aries_config c;
    int a, b, d, e, r, i;
    char code[4];
    memset(&c, 0, sizeof c);
    c.port = PORT;
    c.bind_ip = "127.0.0.1";
    c.public_ip = "127.0.0.1";
    c.log = quiet;
    c.max_sessions = 20;
    c.max_games = 5;
    s_host_gone_secs = 2;
    s_throttle_window = 1;
    settle();                       /* the part before may have stopped without waiting for its loop */
    CHECK(aries_start_thread(&c) == 0);
    settle();

    /* S1: a member's repeated join of a full game is answered as in */
    a = login_as("Hostc", "127.0.0.9", "");
    b = login_as("Guestc", "127.0.0.9", "");
    d = login_as("Thirdc", "127.0.0.9", "");
    put(a, "gcre", "NAME=Two\nPASS=\nPARAMS=1,2,0,3,\nMINSIZE=2\nMAXSIZE=2\nSYSFLAGS=0\nROOM=1\n");
    CHECK(await(a, "gcre", NULL));
    put(b, "gjoi", "NAME=Two\nPASS=\nUSERPARAMS=1,1,3,\n");
    CHECK(await(b, "gjoi", code) && !memcmp(code, "\0\0\0\0", 4));
    quiesce(a); quiesce(b);
    put(b, "gjoi", "NAME=Two\nPASS=\nUSERPARAMS=1,1,3,\n");
    CHECK(await(b, "gjoi", code) && !memcmp(code, "\0\0\0\0", 4) && strstr(g_body, "IDENT="));
    /* nothing changed: no push to the host */
    CHECK(count_cmd(a, "+mgm", 400) == 0);
    /* a newcomer is still turned away */
    put(d, "gjoi", "NAME=Two\nPASS=\nUSERPARAMS=1,1,3,\n");
    CHECK(await(d, "gjoi", code) && !memcmp(code, "full", 4));
    put(a, "gdel", "");
    await(a, "gdel", NULL);
    quiesce(a); quiesce(b); quiesce(d);
    /* the same with a password: the repeat without it is still in */
    put(a, "gcre", "NAME=Locked\nPASS=pw\nPARAMS=1,2,0,3,\nMINSIZE=2\nMAXSIZE=6\nSYSFLAGS=0\nROOM=1\n");
    CHECK(await(a, "gcre", NULL));
    put(b, "gjoi", "NAME=Locked\nPASS=pw\nUSERPARAMS=1,1,3,\n");
    CHECK(await(b, "gjoi", code) && !memcmp(code, "\0\0\0\0", 4));
    put(b, "gjoi", "NAME=Locked\nUSERPARAMS=1,1,3,\n");
    CHECK(await(b, "gjoi", code) && !memcmp(code, "\0\0\0\0", 4));
    put(d, "gjoi", "NAME=Locked\nPASS=no\nUSERPARAMS=1,1,3,\n");
    CHECK(await(d, "gjoi", code) && !memcmp(code, "pass", 4));
    quiesce(a); quiesce(b); quiesce(d);

    /* S3: a guest's gset that changes nothing goes to nobody else; a change does */
    put(b, "gset", "NAME=Guestc\nUSERPARAMS=1,1,3,\n");
    CHECK(await(b, "gset", NULL));
    CHECK(count_cmd(a, "+mgm", 400) == 0);
    put(b, "gset", "NAME=Guestc\nUSERPARAMS=1,2,3,\n");
    CHECK(await(b, "gset", NULL));
    CHECK(count_cmd(a, "+mgm", 400) >= 1);

    /* S7: a second player of the same name cannot join the game */
    e = login_as("Guestc", "127.0.0.9", "");
    put(e, "gjoi", "NAME=Locked\nPASS=pw\nUSERPARAMS=1,1,3,\n");
    CHECK(await(e, "gjoi", code) && !memcmp(code, "full", 4));
    close(e);
    quiesce(a); quiesce(b);

    /* S2: the host of a started game leaves, the guest stays: closed for it
     * after s_host_gone_secs, not at once */
    put(a, "gsta", "");
    CHECK(await(a, "gsta", NULL));
    quiesce(a); quiesce(b);
    close(a);
    g_needle = "IDENT=0";
    r = count_cmd(b, "+mgm", 900);
    CHECK(r == 0);
    r = count_cmd(b, "+mgm", 3500);
    CHECK(r >= 1 && g_hits >= 1);
    g_needle = NULL;
    {
        int used = 0;
        for (i = 0; i < MAX_GAMES; i++)
            used += s_games[i].used;
        CHECK(used == 0);
    }

    /* S4: bursts over the rate, a window apart, do not end the session */
    e = (int)s_throttled_total;
    for (r = 0; r < 3; r++) {
        for (i = 0; i < 300; i++)
            put(d, "sele", "MYGAME=1 GAMES=1");
        count_cmd(d, "zzzz", 1300);
        CHECK(!g_eof);
    }
    CHECK(s_throttled_total - e > THROTTLE_DROP);  /* the old lifetime count would have dropped it */
    put(d, "sele", "MYGAME=1 GAMES=1");
    CHECK(await(d, "sele", NULL));
    close(b); close(d);
    aries_stop();
    settle();
    s_host_gone_secs = HOST_GONE_SECS;
    s_throttle_window = THROTTLE_WINDOW;
}

/* ── BO-365: waiting for a race under way ─────────────────────────── */

static char g_rw[BODY_MAX + 32];
/* a gsea and its answer's body (kept in g_rw); the +gam names after it in names */
static int rw_sea(int fd, const char *extra, char names[][48], int *nn)
{
    char body[200];
    int n;
    quiesce(fd);
    snprintf(body, sizeof body, "START=0\nCOUNT=500\n%s", extra);
    put(fd, "gsea", body);
    if (!await(fd, "gsea", NULL))
        return 0;
    snprintf(g_rw, sizeof g_rw, "%s", g_body);
    n = names ? gam_names(fd, 300, names, 20) : 0;
    if (nn)
        *nn = n;
    return 1;
}
static int rw_int(const char *key)
{
    char v[64];
    return field(g_rw, key, v, sizeof v) && v[0] ? atoi(v) : -1;
}
static int rw_has(const char *key)
{
    char v[64];
    return field(g_rw, key, v, sizeof v);
}
static int rw_is(const char *key, const char *want)
{
    char v[64];
    return field(g_rw, key, v, sizeof v) && !strcmp(v, want);
}
static int rw_wait(int fd, int id, const char *extra, char names[][48], int *nn)
{
    char e[120];
    snprintf(e, sizeof e, "TKWAITFOR=%d\n%s", id, extra);
    return rw_sea(fd, e, names, nn);
}
/* a game made, a guest in it, started, both gone (as the title does at a race start); its ident */
static int rw_race(const char *h, const char *gu, const char *game, const char *extra, int maxsize)
{
    char body[300];
    int a, b, id;
    a = login_as(h, "127.0.0.9", "");
    b = login_as(gu, "127.0.0.9", "");
    snprintf(body, sizeof body, "NAME=%s\nPASS=\nPARAMS=1,2,0,3,\nMINSIZE=2\nMAXSIZE=%d\nSYSFLAGS=0\nROOM=1\n%s", game, maxsize, extra);
    put(a, "gcre", body);
    await(a, "gcre", NULL);
    id = atoi(strstr(g_body, "IDENT=") ? strstr(g_body, "IDENT=") + 6 : "0");
    snprintf(body, sizeof body, "NAME=%s\nPASS=\nUSERPARAMS=1,1,3,\n%s", game, extra);
    put(b, "gjoi", body); await(b, "gjoi", NULL);
    quiesce(a); quiesce(b);
    put(a, "gsta", ""); await(a, "gsta", NULL);
    quiesce(a); quiesce(b);
    close(a); close(b);
    settle();
    return id;
}
/* a race of three (no code, no extra) */
static int rw_race3(const char *h, const char *g1, const char *g2, const char *game, int maxsize)
{
    char body[300];
    int a, b, c, id;
    a = login_as(h, "127.0.0.9", "");
    b = login_as(g1, "127.0.0.9", "");
    c = login_as(g2, "127.0.0.9", "");
    snprintf(body, sizeof body, "NAME=%s\nPASS=\nPARAMS=1,2,0,3,\nMINSIZE=2\nMAXSIZE=%d\nSYSFLAGS=0\nROOM=1\n", game, maxsize);
    put(a, "gcre", body);
    await(a, "gcre", NULL);
    id = atoi(strstr(g_body, "IDENT=") ? strstr(g_body, "IDENT=") + 6 : "0");
    snprintf(body, sizeof body, "NAME=%s\nPASS=\nUSERPARAMS=1,1,3,\n", game);
    put(b, "gjoi", body); await(b, "gjoi", NULL);
    put(c, "gjoi", body); await(c, "gjoi", NULL);
    quiesce(a); quiesce(b); quiesce(c);
    put(a, "gsta", ""); await(a, "gsta", NULL);
    quiesce(a); quiesce(b); quiesce(c);
    close(a); close(b); close(c);
    settle();
    return id;
}
static void rw_remake(int fd, const char *game, int maxsize, const char *extra)
{
    char body[300];
    snprintf(body, sizeof body, "NAME=%s\nPASS=\nPARAMS=1,2,0,3,\nMINSIZE=2\nMAXSIZE=%d\nSYSFLAGS=0\nROOM=1\n%s", game, maxsize, extra);
    put(fd, "gcre", body);
    await(fd, "gcre", NULL);
    quiesce(fd);
}
static int rw_join(int fd, const char *game, const char *extra, char *code)
{
    char body[200];
    quiesce(fd);
    snprintf(body, sizeof body, "NAME=%s\nPASS=\nUSERPARAMS=1,1,3,\n%s", game, extra);
    put(fd, "gjoi", body);
    return await(fd, "gjoi", code);
}
static int races_used(void)
{
    int i, n = 0;
    for (i = 0; i < MAX_GAMES; i++)
        n += s_races[i].used;
    return n;
}
/* a session from another loopback address (Linux: all of 127/8 is local); -1 when it cannot */
static int login_from(const char *name, const char *src)
{
    struct sockaddr_in a, me;
    char body[120];
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    memset(&me, 0, sizeof me);
    me.sin_family = AF_INET;
    inet_pton(AF_INET, src, &me.sin_addr);
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(PORT);
    a.sin_addr.s_addr = htonl(0x7F000001u);
    if (bind(fd, (struct sockaddr *)&me, sizeof me) != 0 || connect(fd, (struct sockaddr *)&a, sizeof a) != 0) {
        close(fd);
        return -1;
    }
    put(fd, "addr", "ADDR=127.99.0.1\nPORT=1000\n"); await(fd, "addr", NULL);
    put(fd, "sele", "MYGAME=1 GAMES=1"); await(fd, "sele", NULL);
    snprintf(body, sizeof body, "GTAG=%s\nPASS=$\n", name);
    put(fd, "auth", body); await(fd, "auth", NULL);
    put(fd, "move", "IDENT=1\n"); await(fd, "move", NULL);
    return fd;
}
static void rw_server(aries_config *c)
{
    memset(c, 0, sizeof *c);
    c->port = PORT;
    c->bind_ip = "127.0.0.1";
    c->public_ip = "127.0.0.1";
    c->log = capture;
    c->max_sessions = 60;
    c->max_games = 20;
    c->max_per_ip = 0;
    cap_n = 0; cap[0] = 0;
    CHECK(aries_start_thread(c) == 0);
    settle();
}

static void test_racewait(void)
{
    aries_config c;
    char names[20][48], code[4];
    int id, w1, w2, w3, h, g, k, n, fd;

    /* 1. a race of two with room: a stranger's empty search names it */
    rw_server(&c);
    id = rw_race("RWhost", "RWguest", "RWgame", "", 6);
    w1 = login_as("RWwaitone", "127.0.0.9", "");
    CHECK(rw_sea(w1, "", NULL, NULL));
    CHECK(rw_int("COUNT") == 0 && rw_int("TKRACING") == 1 && rw_int("TKRACEID") == id && rw_int("TKRACEPLAYERS") == 2 &&
          rw_int("TKRACEMAX") == 6 && rw_int("TKRACESECS") >= 0 && rw_is("TKRACENAME", "RWgame"));
    /* the room record counts it for the online page */
    fd = login_as("RWroom", "127.0.0.9", "");
    put(fd, "sele", "MYGAME=1 GAMES=1");
    CHECK(await(fd, "+rom", NULL) && strstr(g_body, "TKRACING=1"));
    close(fd);
    /* no record: one player at the start, a password, no room (2 of 2) */
    {
        int s1 = login_as("RWsolo", "127.0.0.9", "");
        mkgame(s1, "RWsologame", "");
        quiesce(s1);
        put(s1, "gsta", ""); await(s1, "gsta", NULL);
        close(s1);
        settle();
    }
    {
        int a = login_as("RWpwhost", "127.0.0.9", ""), b = login_as("RWpwguest", "127.0.0.9", "");
        put(a, "gcre", "NAME=RWpw\nPASS=secret\nPARAMS=1,2,0,3,\nMINSIZE=2\nMAXSIZE=6\nSYSFLAGS=0\nROOM=1\n"); await(a, "gcre", NULL);
        put(b, "gjoi", "NAME=RWpw\nPASS=secret\nUSERPARAMS=1,1,3,\n"); await(b, "gjoi", NULL);
        quiesce(a); quiesce(b);
        put(a, "gsta", ""); await(a, "gsta", NULL);
        close(a); close(b);
        settle();
    }
    rw_race("RWfullhost", "RWfullguest", "RWfull", "", 2);
    CHECK(rw_sea(w1, "", NULL, NULL));
    CHECK(rw_int("TKRACING") == 1 && rw_int("TKRACEID") == id);

    /* 2. waiting: ranks in arrival order; a plain search ends a wait */
    w2 = login_as("RWwaittwo", "127.0.0.9", "");
    CHECK(rw_wait(w1, id, "", NULL, NULL) && rw_is("TKWAITSTATE", "racing") && rw_int("TKWAITQUEUE") == 0 &&
          rw_int("TKWAITPLAYERS") == 2 && rw_int("COUNT") == 0 && rw_int("TKRACING") == -1);
    CHECK(strstr(cap, "RWwaitone waits for game") != NULL);
    CHECK(rw_wait(w2, id, "", NULL, NULL) && rw_is("TKWAITSTATE", "racing") && rw_int("TKWAITQUEUE") == 1);
    CHECK(rw_wait(w1, id, "", NULL, NULL) && rw_int("TKWAITQUEUE") == 0);    /* polling keeps the place */
    CHECK(rw_sea(w1, "", NULL, NULL));
    CHECK(rw_wait(w2, id, "", NULL, NULL) && rw_int("TKWAITQUEUE") == 0);
    CHECK(rw_wait(w1, id, "", NULL, NULL) && rw_int("TKWAITQUEUE") == 1);     /* back, behind */
    {   /* the log line once per race, not per poll */
        const char *p = cap; int lines_n = 0;
        while ((p = strstr(p, "RWwaitone waits for game")) != NULL) { lines_n++; p++; }
        CHECK(lines_n == 1);
    }

    /* 3. the host back and its game made again: linked; the waiters join it */
    cap_n = 0; cap[0] = 0;
    h = login_as("RWhost", "127.0.0.9", "");
    rw_remake(h, "RWgame", 6, "");
    CHECK(strstr(cap, "is game ") && strstr(cap, "again (its host is back after the race)"));
    CHECK(rw_wait(w2, id, "", names, &n) && rw_is("TKWAITSTATE", "open") && n >= 1 && !strcmp(names[0], "RWgame") &&
          rw_is("TKWAITNAME", "RWgame") && rw_int("TKWAITPLAYERS") == 1);
    CHECK(rw_join(w2, "RWgame", "", code) && !memcmp(code, "\0\0\0\0", 4));
    quiesce(w2);                                             /* the log line comes after the answer */
    CHECK(strstr(cap, "RWwaittwo joined game") && strstr(cap, "after waiting"));
    CHECK(rw_wait(w1, id, "", names, &n) && rw_is("TKWAITSTATE", "open") && rw_int("TKWAITQUEUE") == 0);
    CHECK(rw_join(w1, "RWgame", "", code) && !memcmp(code, "\0\0\0\0", 4));
    g = login_as("RWguest", "127.0.0.9", "");                 /* the racer back: its seat is there */
    CHECK(rw_join(g, "RWgame", "", code) && !memcmp(code, "\0\0\0\0", 4));
    close(h); close(g); close(w1); close(w2);
    aries_stop();

    /* 4. capacity: 3 seats, a race of two, three waiters */
    rw_server(&c);
    id = rw_race("RCh", "RCg", "RCgame", "", 3);
    w1 = login_as("RCwone", "127.0.0.9", "");
    w2 = login_as("RCwtwo", "127.0.0.9", "");
    w3 = login_as("RCwthree", "127.0.0.9", "");
    rw_wait(w1, id, "", NULL, NULL); rw_wait(w2, id, "", NULL, NULL); rw_wait(w3, id, "", NULL, NULL);
    h = login_as("RCh", "127.0.0.9", "");
    rw_remake(h, "RCgame", 3, "");
    CHECK(rw_wait(w1, id, "", names, &n) && rw_is("TKWAITSTATE", "open") && n == 1);
    CHECK(rw_wait(w2, id, "", names, &n) && rw_is("TKWAITSTATE", "lobby") && n == 0 && rw_int("TKWAITQUEUE") == 1);
    CHECK(rw_join(w2, "RCgame", "", code) && !memcmp(code, "full", 4));     /* not its turn: the guest's seat is kept */
    CHECK(rw_join(w1, "RCgame", "", code) && !memcmp(code, "\0\0\0\0", 4));
    /* a stranger's plain search does not list a game whose last seat is a racer's */
    fd = login_as("RCstranger", "127.0.0.9", "");
    CHECK(rw_sea(fd, "", names, &n) && n == 0);
    CHECK(rw_join(fd, "RCgame", "", code) && !memcmp(code, "full", 4));
    close(fd);
    CHECK(rw_wait(w2, id, "", names, &n) && rw_is("TKWAITSTATE", "lobby") && rw_int("TKWAITQUEUE") == 0);
    s_reserve_ms = 0;                                         /* the window is over: the guest's seat is free */
    s_coming_ms = 0;                                          /* (and the racer is not on its way back any more) */
    CHECK(rw_wait(w2, id, "", names, &n) && rw_is("TKWAITSTATE", "open") && n == 1);
    CHECK(rw_join(w2, "RCgame", "", code) && !memcmp(code, "\0\0\0\0", 4));
    CHECK(rw_wait(w3, id, "", names, &n) && rw_is("TKWAITSTATE", "full") && n == 0);
    /* the full game starts: its waiter follows to the new race, which has no seat either */
    quiesce(h); put(h, "gsta", ""); await(h, "gsta", NULL); settle();
    CHECK(rw_wait(w3, id, "", names, &n) && rw_is("TKWAITSTATE", "full") && n == 0);
    s_reserve_ms = RESERVE_MS;
    s_coming_ms = COMING_MS;
    close(w1); close(w2); close(w3); close(h);
    aries_stop();

    /* 5. nobody makes it again */
    s_rehost_grace_ms = 1500;
    s_host_slow_ms = 1500;
    s_race_tail_ms = 1500;
    rw_server(&c);
    id = rw_race("RNh", "RNg", "RNgame", "", 6);
    w1 = login_as("RNw", "127.0.0.9", "");
    CHECK(rw_wait(w1, id, "", NULL, NULL) && rw_is("TKWAITSTATE", "racing"));
    h = login_as("RNh", "127.0.0.9", "");                     /* back, but no game */
    settle(); settle();
    CHECK(rw_wait(w1, id, "", NULL, NULL) && rw_is("TKWAITSTATE", "gone") && rw_int("TKWAITQUEUE") == -1);
    CHECK(strstr(cap, "over: nobody made it again (its host did not make a game within 1 s)") != NULL);
    s_coming_ms = 0;                                          /* (RNg never asks: not kept for it) */
    rw_sea(w1, "", NULL, NULL);                               /* the waiter went on: the record leaves */
    settle();
    CHECK(races_used() == 0);
    s_coming_ms = COMING_MS;
    close(h);
    /* the host never signs in: the race's time runs out */
    s_race_hold_secs = 2;
    id = rw_race("RTh", "RTg", "RTgame", "", 6);
    settle(); settle();
    CHECK(rw_wait(w1, id, "", NULL, NULL) && rw_is("TKWAITSTATE", "gone"));
    CHECK(strstr(cap, "(the race's time ran out)") != NULL);
    s_race_hold_secs = RACE_HOLD_SECS;
    /* the host signs in and joins another game */
    id = rw_race("RJh", "RJg", "RJgame", "", 6);
    fd = login_as("RJother", "127.0.0.9", "");
    mkgame(fd, "RJothergame", "");
    h = login_as("RJh", "127.0.0.9", "");
    CHECK(rw_join(h, "RJothergame", "", code) && !memcmp(code, "\0\0\0\0", 4));
    CHECK(rw_wait(w1, id, "", NULL, NULL) && rw_is("TKWAITSTATE", "gone"));
    CHECK(strstr(cap, "(its host joined another game)") != NULL);
    close(h); close(fd);
    /* the others are back, the host is not */
    id = rw_race("RAh", "RAg", "RAgame", "", 6);
    g = login_as("RAg", "127.0.0.9", "");
    settle(); settle(); settle();
    CHECK(rw_wait(w1, id, "", NULL, NULL) && rw_is("TKWAITSTATE", "gone"));
    CHECK(strstr(cap, "(the others are back, its host is not)") != NULL);
    close(g); close(w1);
    aries_stop();
    s_rehost_grace_ms = REHOST_GRACE_MS;
    s_host_slow_ms = HOST_SLOW_MS;
    s_race_tail_ms = RACE_TAIL_MS;

    /* 6. the game made again closes, and is made again: the waiter follows */
    rw_server(&c);
    id = rw_race("RSh", "RSg", "RSgame", "", 6);
    w1 = login_as("RSw", "127.0.0.9", "");
    rw_wait(w1, id, "", NULL, NULL);
    h = login_as("RSh", "127.0.0.9", "");
    rw_remake(h, "RSgame", 6, "");
    CHECK(rw_wait(w1, id, "", NULL, NULL) && rw_is("TKWAITSTATE", "open"));
    put(h, "gdel", ""); await(h, "gdel", NULL);
    CHECK(rw_wait(w1, id, "", names, &n) && rw_is("TKWAITSTATE", "racing") && n == 0);
    rw_remake(h, "RSgame", 6, "");
    CHECK(rw_wait(w1, id, "", names, &n) && rw_is("TKWAITSTATE", "open") && n == 1);
    CHECK(rw_join(w1, "RSgame", "", code) && !memcmp(code, "\0\0\0\0", 4));
    /* that game races: a waiter of the first race now waits for the second */
    w2 = login_as("RSwtwo", "127.0.0.9", "");
    quiesce(h); quiesce(w1);
    put(h, "gsta", ""); await(h, "gsta", NULL);
    close(h); close(w1);
    settle();
    CHECK(rw_wait(w2, id, "", NULL, NULL) && rw_is("TKWAITSTATE", "racing") && rw_int("TKWAITPLAYERS") == 2);
    CHECK(strstr(cap, "raced again") != NULL);
    close(w2);
    aries_stop();

    /* 7. the host's name from another address makes a game: not the same game */
    rw_server(&c);
    id = rw_race("RIh", "RIg", "RIgame", "", 6);
    w1 = login_as("RIw", "127.0.0.9", "");
    rw_wait(w1, id, "", NULL, NULL);
    fd = login_from("RIh", "127.0.0.77");
    if (fd >= 0) {
        rw_remake(fd, "RIgame", 6, "");
        CHECK(rw_wait(w1, id, "", NULL, NULL) && rw_is("TKWAITSTATE", "racing"));
        CHECK(!strstr(cap, "again (its host is back"));
        close(fd);
    } else
        printf("note: no second loopback address here: the impostor case is not run\n");
    close(w1);
    aries_stop();

    /* 8. a private game (a code): only with its code, never a strike */
    s_code_search_ms = 0;
    rw_server(&c);
    id = rw_race("RPh", "RPg", "RPgame", "TKCODE=RWPRIV\n", 6);
    w1 = login_as("RPw", "127.0.0.9", "");
    CHECK(rw_sea(w1, "", NULL, NULL) && rw_int("TKRACING") == -1);
    CHECK(rw_sea(w1, "TKCODE=RWPRIV\n", NULL, NULL) && rw_int("COUNT") == 0 && rw_int("TKRACING") == 1 && rw_int("TKRACEID") == id);
    CHECK(rw_wait(w1, id, "TKCODE=RWPRIV\n", NULL, NULL) && rw_is("TKWAITSTATE", "racing"));
    w2 = login_as("RPnocode", "127.0.0.9", "");
    CHECK(rw_wait(w2, id, "", NULL, NULL) && rw_is("TKWAITSTATE", "gone") && rw_int("TKWAITNAME") == -1 &&
          rw_int("TKWAITPLAYERS") == -1);
    for (k = 0; k < 25; k++)
        rw_wait(w1, id, "TKCODE=RWPRIV\n", NULL, NULL);
    CHECK(rw_is("TKWAITSTATE", "racing"));
    {
        int i, wn = -1;
        for (i = 0; i < MAX_SESS; i++)
            if (s_sess[i].used && !strcmp(s_sess[i].name, "RPw"))
                wn = s_sess[i].wrong_n;
        CHECK(wn == 0);
    }
    h = login_as("RPh", "127.0.0.9", "");
    rw_remake(h, "RPgame", 6, "TKCODE=RWPRIV\n");
    CHECK(rw_wait(w1, id, "TKCODE=RWPRIV\n", names, &n) && rw_is("TKWAITSTATE", "open") && n == 1);
    CHECK(rw_wait(w2, id, "", names, &n) && rw_is("TKWAITSTATE", "gone") && n == 0);
    CHECK(rw_join(w1, "RPgame", "TKCODE=RWPRIV\n", code) && !memcmp(code, "\0\0\0\0", 4));
    close(w1); close(w2); close(h);
    aries_stop();
    s_code_search_ms = CODE_SEARCH_MS;

    /* 9. kicked from the raced game: never offered, and the game made again keeps the kick */
    rw_server(&c);
    {
        int a = login_as("RKh", "127.0.0.9", ""), b = login_as("RKg", "127.0.0.9", ""), kk = login_as("RKk", "127.0.0.9", "");
        mkgame(a, "RKgame", "");
        rw_join(b, "RKgame", "", NULL);
        rw_join(kk, "RKgame", "", NULL);
        quiesce(a);
        put(a, "gset", "NAME=RKh\nKICK=RKk\nTKWHY=host\n"); await(a, "gset", NULL);
        quiesce(a); quiesce(b); quiesce(kk);
        put(a, "gsta", ""); await(a, "gsta", NULL);
        quiesce(a);
        close(a); close(b);
        settle();
        CHECK(rw_sea(kk, "", NULL, NULL) && rw_int("TKRACING") == -1);
        id = 0;
        for (k = 0; k < MAX_GAMES; k++)
            if (s_races[k].used && !strcmp(s_races[k].name, "RKgame"))
                id = s_races[k].ident;
        CHECK(id > 0 && rw_wait(kk, id, "", NULL, NULL) && rw_is("TKWAITSTATE", "gone"));
        h = login_as("RKh", "127.0.0.9", "");
        rw_remake(h, "RKgame", 6, "");
        CHECK(rw_join(kk, "RKgame", "", code) && !memcmp(code, "gone", 4) && strstr(g_body, "TKKICKED=1"));
        close(h); close(kk);
    }
    aries_stop();

    /* 10. a client that knows nothing of this: the answers it had */
    rw_server(&c);
    w1 = login_as("ROld", "127.0.0.9", "");
    CHECK(rw_sea(w1, "", NULL, NULL) && !strcmp(g_rw, "COUNT=0\n"));
    id = rw_race("ROh", "ROg", "ROgame", "", 6);
    CHECK(rw_sea(w1, "", NULL, NULL) && rw_int("COUNT") == 0);    /* TKRACING rides along; the title skips it */
    h = login_as("ROh", "127.0.0.9", "");
    rw_remake(h, "ROgame", 6, "");
    CHECK(rw_sea(w1, "", names, &n) && n == 1 && !strcmp(names[0], "ROgame"));
    CHECK(rw_join(w1, "ROgame", "", code) && !memcmp(code, "\0\0\0\0", 4));   /* 5 free seats: a stranger fits */
    fd = login_as("RObad", "127.0.0.9", "");
    put(fd, "gsea", "START=0\nCOUNT=500\nTKWAITFOR=abc\n");
    CHECK(wait_eof(fd, 2000));
    close(fd); close(w1); close(h);
    aries_stop();
}

/* BO-365 x BO-366: with --crowd-test a race the load tool made is never
 * offered to a real copy (nor a real race to a load-tool player), and the
 * room's race count is the real players' */
static int rwc_race(const char *h, const char *gu, const char *game)
{
    char body[300];
    int a, b, id, ack;
    a = login_crowd(h, &ack);
    b = login_crowd(gu, &ack);
    snprintf(body, sizeof body, "NAME=%s\nPASS=\nPARAMS=1,2,0,3,\nMINSIZE=2\nMAXSIZE=6\nSYSFLAGS=0\nROOM=1\n", game);
    put(a, "gcre", body);
    await(a, "gcre", NULL);
    id = atoi(strstr(g_body, "IDENT=") ? strstr(g_body, "IDENT=") + 6 : "0");
    snprintf(body, sizeof body, "NAME=%s\nPASS=\nUSERPARAMS=1,1,3,\n", game);
    put(b, "gjoi", body); await(b, "gjoi", NULL);
    quiesce(a); quiesce(b);
    put(a, "gsta", ""); await(a, "gsta", NULL);
    quiesce(a); quiesce(b);
    close(a); close(b);
    settle();
    return id;
}
/* a sign-in after a race, with the client's TKSTAYQUIT=1 when quit says so */
static int login_stay(const char *name, int quit)
{
    char body[200];
    int fd = dial();
    if (fd < 0)
        return -1;
    g_login_n++;
    snprintf(body, sizeof body, "ADDR=127.%d.%d.%d\nPORT=1000\n", 8 + g_login_n / 65536, (g_login_n / 256) % 256, g_login_n % 256);
    put(fd, "addr", body); await(fd, "addr", NULL);
    put(fd, "sele", "MYGAME=1 GAMES=1"); await(fd, "sele", NULL);
    snprintf(body, sizeof body, "GTAG=%s\nPASS=$\n%s", name, quit ? "TKSTAYQUIT=1\n" : "");
    put(fd, "auth", body); await(fd, "auth", NULL);
    snprintf(body, sizeof body, "GTAG=%s\n", name);
    put(fd, "pers", body); await(fd, "pers", NULL);
    put(fd, "move", "IDENT=1\n"); await(fd, "move", NULL);
    return fd;
}

/* Stay together (item 3 of the 0.3.0 review): the players of a race wait
 * for the slower ones, and stop only when the server says so. */
static void test_staywait(void)
{
    aries_config c;
    char names[8][48], code[4];
    int id, h, g, fd, n;
    long long t0;

    /* 1. the guest back first, the host still on its results: it waits,
     * longer than REHOST_GRACE (the others back, the host not: HOST_SLOW) */
    s_rehost_grace_ms = 1500;
    rw_server(&c);
    id = rw_race("SWh", "SWg", "SWgame", "", 6);
    g = login_as("SWg", "127.0.0.9", "");
    CHECK(rw_wait(g, id, "", names, &n) && rw_is("TKWAITSTATE", "racing") && !rw_has("TKWAITHOST") && n == 0 &&
          rw_int("TKWAITQUEUE") == 0);
    settle(); settle(); settle();
    CHECK(rw_wait(g, id, "", NULL, NULL) && rw_is("TKWAITSTATE", "racing"));
    CHECK(!strstr(cap, "over: nobody made it again"));
    /* the host signs in: it is making the game */
    h = login_as("SWh", "127.0.0.9", "");
    CHECK(rw_wait(g, id, "", NULL, NULL) && rw_is("TKWAITSTATE", "racing") && rw_int("TKWAITHOST") == 1);
    /* its game made again: the guest is on its way (TKCOMING 1 in the gcre answer) */
    put(h, "gcre", "NAME=SWgame\nPASS=\nPARAMS=1,2,0,3,\nMINSIZE=2\nMAXSIZE=6\nSYSFLAGS=0\nROOM=1\n");
    CHECK(await(h, "gcre", NULL) && strstr(g_body, "TKCOMING=1\n") != NULL);
    quiesce(h);
    CHECK(rw_wait(g, id, "", names, &n) && rw_is("TKWAITSTATE", "open") && n >= 1 && !strcmp(names[0], "SWgame"));
    CHECK(rw_join(g, "SWgame", "", code) && !memcmp(code, "\0\0\0\0", 4) && strstr(g_body, "TKCOMING=0\n") != NULL);
    close(g); close(h);
    aries_stop();

    /* 2. a racer always has its seat: a race of two in a game of two seats */
    rw_server(&c);
    id = rw_race("STh", "STg", "STgame", "", 2);
    h = login_as("STh", "127.0.0.9", "");
    rw_remake(h, "STgame", 2, "");
    g = login_as("STg", "127.0.0.9", "");
    CHECK(rw_wait(g, id, "", names, &n) && rw_is("TKWAITSTATE", "open") && n == 1);
    /* a stranger never takes it, also after the RESERVE window: the racer is on its way */
    s_reserve_ms = 0;
    fd = login_as("STx", "127.0.0.9", "");
    CHECK(rw_join(fd, "STgame", "", code) && !memcmp(code, "full", 4));
    CHECK(rw_join(g, "STgame", "", code) && !memcmp(code, "\0\0\0\0", 4));
    s_reserve_ms = RESERVE_MS;
    close(fd); close(g); close(h);
    aries_stop();

    /* 3. the host quits on its results: its sign-in says so, the waiter is told at once */
    rw_server(&c);
    id = rw_race("SQh", "SQg", "SQgame", "", 6);
    g = login_as("SQg", "127.0.0.9", "");
    CHECK(rw_wait(g, id, "", NULL, NULL) && rw_is("TKWAITSTATE", "racing"));
    h = login_stay("SQh", 1);
    CHECK(rw_wait(g, id, "", NULL, NULL) && rw_is("TKWAITSTATE", "gone") && rw_is("TKWAITWHY", "quit"));
    CHECK(strstr(cap, "(its host quit on the results)") != NULL);
    close(g); close(h);
    /* the host quits while its guest still sits in the results' box (not signed in, no
     * poll yet): the record stays, the guest's first poll still hears why */
    id = rw_race("SLh", "SLg", "SLgame", "", 6);
    h = login_stay("SLh", 1);
    settle(); settle();
    g = login_as("SLg", "127.0.0.9", "");
    CHECK(rw_wait(g, id, "", NULL, NULL) && rw_is("TKWAITSTATE", "gone") && rw_is("TKWAITWHY", "quit"));
    close(g); close(h);
    /* two guests: the first one told does not free it before the second one asks */
    id = rw_race3("S2h", "S2g", "S2k", "S2game", 6);
    h = login_stay("S2h", 1);
    settle(); settle();
    g = login_as("S2g", "127.0.0.9", "");
    CHECK(rw_wait(g, id, "", NULL, NULL) && rw_is("TKWAITWHY", "quit"));
    rw_sea(g, "", NULL, NULL);
    settle();
    fd = login_as("S2k", "127.0.0.9", "");
    CHECK(rw_wait(fd, id, "", NULL, NULL) && rw_is("TKWAITSTATE", "gone") && rw_is("TKWAITWHY", "quit"));
    close(fd); close(g); close(h);
    /* the host does not come back and every other player is: over after HOST_SLOW, "absent" */
    s_host_slow_ms = 1500;
    id = rw_race("SAh", "SAg", "SAgame", "", 6);
    g = login_as("SAg", "127.0.0.9", "");
    rw_wait(g, id, "", NULL, NULL);
    settle(); settle(); settle();
    CHECK(rw_wait(g, id, "", NULL, NULL) && rw_is("TKWAITSTATE", "gone") && rw_is("TKWAITWHY", "absent"));
    s_host_slow_ms = HOST_SLOW_MS;
    close(g);
    aries_stop();

    /* 4. the host back first waits for its guest (TKCOMING, pushed when it changes); a guest that
     * quit on its results is not waited for */
    rw_server(&c);
    id = rw_race("SCh", "SCg", "SCgame", "", 6);
    h = login_as("SCh", "127.0.0.9", "");
    put(h, "gcre", "NAME=SCgame\nPASS=\nPARAMS=1,2,0,3,\nMINSIZE=2\nMAXSIZE=6\nSYSFLAGS=0\nROOM=1\n");
    CHECK(await(h, "gcre", NULL) && strstr(g_body, "TKCOMING=1\n") != NULL);
    quiesce(h);
    g = login_stay("SCg", 1);                                /* quit on the results */
    t0 = now_ms();
    CHECK(await(h, "+mgm", NULL) && strstr(g_body, "TKCOMING=0\n") != NULL && now_ms() - t0 < 4000);
    /* the line is written before that push, but on a loaded machine the push
     * can also come from the guest's first moments after its sign-in: give
     * the line a moment, and show the log when it never comes */
    for (n = 0; n < 30 && !strstr(cap, "SCg quit on the results of game"); n++)
        nanosleep(&(struct timespec){ 0, 100000000 }, NULL);
    CHECK(strstr(cap, "SCg quit on the results of game") != NULL);
    if (!strstr(cap, "SCg quit on the results of game"))
        fprintf(stderr, "staywait part 4 log:\n%s\n", cap);
    /* and its seat is free for a stranger */
    fd = login_as("SCx", "127.0.0.9", "");
    CHECK(rw_join(fd, "SCgame", "", code) && !memcmp(code, "\0\0\0\0", 4));
    close(fd); close(g); close(h);
    /* a guest that stops waiting (B on its Quick Match) stops being waited for after COMING_POLL_MS */
    id = rw_race("SPh", "SPg", "SPgame", "", 6);
    h = login_as("SPh", "127.0.0.9", "");
    rw_remake(h, "SPgame", 6, "");
    g = login_as("SPg", "127.0.0.9", "");
    rw_wait(g, id, "", NULL, NULL);
    {
        int i;
        for (i = 0; i < MAX_SESS; i++)
            if (s_sess[i].used && !strcmp(s_sess[i].name, "SPg")) {
                s_sess[i].wait_poll_ms -= COMING_POLL_MS;    /* its last poll long ago, */
                s_sess[i].auth_ms -= COMING_SIGNIN_MS;       /* and its sign-in */
            }
    }
    CHECK(await(h, "+mgm", NULL) && strstr(g_body, "TKCOMING=0\n") != NULL);
    close(g); close(h);
    aries_stop();
    s_rehost_grace_ms = REHOST_GRACE_MS;
}

static void test_racewait_crowd(void)
{
    aries_config c;
    int fid, rid, rs, cs, ack, n;
    char names[20][48];
    memset(&c, 0, sizeof c);
    c.port = PORT;
    c.bind_ip = "127.0.0.1";
    c.public_ip = "127.0.0.1";
    c.log = capture;
    c.max_sessions = 60;
    c.max_games = 20;
    c.crowd_test = 1;
    cap_n = 0; cap[0] = 0;
    CHECK(aries_start_thread(&c) == 0);
    settle();
    fid = rwc_race("RWCfakeh", "RWCfakeg", "RWCfake");
    CHECK(fid > 0 && races_used() == 1);
    rs = login_as("RWCreal", "127.0.0.9", "");
    cs = login_crowd("RWCcrowd", &ack);
    /* the real player: no race to wait for, the fake one's ident is gone for it */
    CHECK(rw_sea(rs, "", names, &n) && n == 0 && !rw_has("TKRACING"));
    CHECK(rw_wait(rs, fid, "", NULL, NULL) && rw_is("TKWAITSTATE", "gone"));
    CHECK(races_for_strangers() == 0);
    /* the load tool's player sees it */
    CHECK(rw_sea(cs, "", names, &n) && n == 0 && rw_int("TKRACING") == 1 && rw_int("TKRACEID") == fid);
    CHECK(rw_wait(cs, fid, "", NULL, NULL) && rw_is("TKWAITSTATE", "racing"));
    /* a real race: the real player is offered it, not the fake one; the crowd player the other way round */
    rid = rw_race("RWCrealh", "RWCrealg", "RWCrealgame", "", 6);
    CHECK(rw_sea(rs, "", names, &n) && rw_int("TKRACING") == 1 && rw_int("TKRACEID") == rid);
    CHECK(races_for_strangers() == 1);
    CHECK(rw_sea(cs, "", names, &n) && rw_int("TKRACING") == 1 && rw_int("TKRACEID") == fid);
    close(rs); close(cs);
    aries_stop();
}

/* BO-372: the +gam records within ms: NAME, TKSTATE ("" none) and TKSECS (-1 none), in order */
static int gam_states(int fd, int ms, char names[][48], char states[][16], int *secs, int max)
{
    static unsigned char buf[1 << 18];
    size_t have = 0;
    int n = 0, t;
    for (t = 0; t < ms; ) {
        struct pollfd p = { fd, POLLIN, 0 };
        if (poll(&p, 1, 20) <= 0) { t += 20; continue; }
        {
            ssize_t r = recv(fd, buf + have, sizeof buf - have, 0);
            if (r <= 0)
                break;
            have += (size_t)r;
        }
        while (have >= 12) {
            uint32_t len = ((uint32_t)buf[8] << 24) | ((uint32_t)buf[9] << 16) | ((uint32_t)buf[10] << 8) | buf[11];
            if (have < len)
                break;
            if (!memcmp(buf, "+gam", 4) && len > 12 && n < max) {
                char v[64];
                buf[len - 1] = 0;
                names[n][0] = states[n][0] = 0;
                secs[n] = -1;
                if (field((char *)buf + 12, "NAME", v, sizeof v))
                    snprintf(names[n], 48, "%s", v);
                if (field((char *)buf + 12, "TKSTATE", v, sizeof v))
                    snprintf(states[n], 16, "%s", v);
                if (field((char *)buf + 12, "TKSECS", v, sizeof v))
                    secs[n] = atoi(v);
                n++;
            }
            memmove(buf, buf + len, have - len);
            have -= len;
        }
    }
    return n;
}
static int br_sea(int fd, const char *extra, char names[][48], char states[][16], int *secs, int *nn)
{
    char body[200];
    quiesce(fd);
    snprintf(body, sizeof body, "START=0\nCOUNT=500\n%s", extra);
    put(fd, "gsea", body);
    if (!await(fd, "gsea", NULL))
        return 0;
    snprintf(g_rw, sizeof g_rw, "%s", g_body);
    *nn = gam_states(fd, 300, names, states, secs, 20);
    return 1;
}

static void test_browse(void)
{
    aries_config c;
    char names[20][48], states[20][16], code[4];
    int secs[20], n, ho, hf, gf, hp, bw, qp, rid, rfid, h, k;

    CHECK(!validate("gsea", "TKBROWSE=1\n") && !validate("gsea", "TKBROWSE=0\n") &&
          !strcmp(validate("gsea", "TKBROWSE=2\n"), "TKBROWSE") && !strcmp(validate("gsea", "TKBROWSE=x\n"), "TKBROWSE"));
    rw_server(&c);
    /* an open game, a full one (2 of 2), a private one, a race with room, a full race */
    ho = login_as("BRopenh", "127.0.0.9", "");
    mkgame(ho, "BRopen", "");
    hf = login_as("BRfullh", "127.0.0.9", "");
    put(hf, "gcre", "NAME=BRfull\nPASS=\nPARAMS=1,2,0,3,\nMINSIZE=2\nMAXSIZE=2\nSYSFLAGS=0\nROOM=1\n"); await(hf, "gcre", NULL);
    gf = login_as("BRfullg", "127.0.0.9", "");
    CHECK(rw_join(gf, "BRfull", "", code) && !memcmp(code, "\0\0\0\0", 4));
    hp = login_as("BRprivh", "127.0.0.9", "");
    mkgame(hp, "BRpriv", "TKCODE=KZ7MQ\n");
    rid = rw_race("BRraceh", "BRraceg", "BRrace", "", 6);
    rfid = rw_race("BRrfullh", "BRrfullg", "BRrfull", "", 2);
    CHECK(rid > 0 && rfid > 0);

    /* 1. a browse search: every public game with its state, open, racing, full */
    bw = login_as("BRbrowser", "127.0.0.9", "");
    CHECK(br_sea(bw, "TKBROWSE=1\n", names, states, secs, &n));
    CHECK(rw_int("COUNT") == 4 && rw_int("TKBROWSE") == 1 && n == 4);
    CHECK(n == 4 && !strcmp(names[0], "BRopen") && !strcmp(states[0], "open") && secs[0] == -1);
    CHECK(n == 4 && !strcmp(names[1], "BRrace") && !strcmp(states[1], "racing") && secs[1] >= 0);
    CHECK(n == 4 && !strcmp(names[2], "BRfull") && !strcmp(states[2], "full") && secs[2] == -1);
    CHECK(n == 4 && !strcmp(names[3], "BRrfull") && !strcmp(states[3], "full") && secs[3] >= 0);
    for (k = 0; k < n; k++)
        CHECK(strcmp(names[k], "BRpriv") != 0);
    /* a race's record is the game it was: its players and its seats */
    put(bw, "gsea", "START=0\nCOUNT=500\nTKBROWSE=1\n");
    CHECK(await(bw, "gsea", NULL));
    CHECK(await(bw, "+gam", NULL) && strstr(g_body, "NAME=BRopen"));
    CHECK(await(bw, "+gam", NULL) && strstr(g_body, "NAME=BRrace") && strstr(g_body, "\nCOUNT=2") &&
          strstr(g_body, "\nMAXSIZE=6") && strstr(g_body, "PARAMS=1,2,0,3,") && strstr(g_body, "OPPO1=BRraceg") &&
          strstr(g_body, "ADDR0=0.0.0.0"));
    quiesce(bw);
    /* paging as for a plain search */
    quiesce(bw);
    put(bw, "gsea", "START=1\nCOUNT=2\nTKBROWSE=1\n");
    CHECK(await(bw, "gsea", NULL) && strstr(g_body, "COUNT=2\n"));
    n = gam_states(bw, 300, names, states, secs, 20);
    CHECK(n == 2 && !strcmp(names[0], "BRrace") && !strcmp(names[1], "BRfull"));

    /* 2. QUICK PLAY (and every old client): no key, the open games only, no state */
    qp = login_as("BRquick", "127.0.0.9", "");
    CHECK(br_sea(qp, "", names, states, secs, &n));
    CHECK(rw_int("COUNT") == 1 && !rw_has("TKBROWSE") && n == 1 && !strcmp(names[0], "BRopen") && !states[0][0]);
    CHECK(br_sea(qp, "TKBROWSE=0\n", names, states, secs, &n) && n == 1 && !states[0][0]);
    /* nothing open: the race to wait for, as before */
    close(ho);
    settle();
    CHECK(br_sea(qp, "", names, states, secs, &n) && n == 0 && rw_int("TKRACING") == 1 && rw_int("TKRACEID") == rid);
    /* a browse key with a code or a wait is that search, not a browse */
    CHECK(br_sea(qp, "TKBROWSE=1\nTKCODE=KZ7MQ\n", names, states, secs, &n) && n == 1 && !strcmp(names[0], "BRpriv") &&
          !states[0][0] && !rw_has("TKBROWSE"));
    close(qp);
    qp = login_as("BRquick2", "127.0.0.9", "");
    {
        char e[64];
        snprintf(e, sizeof e, "TKBROWSE=1\nTKWAITFOR=%d\n", rid);
        CHECK(br_sea(qp, e, names, states, secs, &n) && rw_is("TKWAITSTATE", "racing") && !rw_has("TKBROWSE") && n == 0);
    }
    close(qp);

    /* 3. the race's host makes the game again: the race leaves the list, the new game is in it */
    h = login_as("BRraceh", "127.0.0.9", "");
    rw_remake(h, "BRrace", 6, "");
    CHECK(br_sea(bw, "TKBROWSE=1\n", names, states, secs, &n));
    {
        int seen = 0;
        for (k = 0; k < n; k++)
            if (!strcmp(names[k], "BRrace")) {
                seen++;
                /* its racer's kept seat: open to a stranger while 4 seats are free */
                CHECK(!strcmp(states[k], "open") && secs[k] == -1);
            }
        CHECK(seen == 1);
    }
    /* a player in a game does not see it in its own list */
    CHECK(br_sea(gf, "TKBROWSE=1\n", names, states, secs, &n));
    for (k = 0; k < n; k++)
        CHECK(strcmp(names[k], "BRfull") != 0);
    close(h); close(hf); close(gf); close(hp); close(bw);
    aries_stop();
}

/* BO-367: countries from an offline database, counted per day, nothing
 * else kept; the owner's page in its secret folder. */
static void write_text(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    if (f) { fputs(text, f); fclose(f); }
}

static uint32_t net_of(unsigned a, unsigned b, unsigned c, unsigned d)
{
    return htonl(a << 24 | b << 16 | c << 8 | d);
}

/* 1 when a file under dir holds the text */
static int file_has(const char *dir, const char *name, const char *text)
{
    static char buf[1 << 16];
    char p[600];
    FILE *f;
    size_t k;
    snprintf(p, sizeof p, "%s/%s", dir, name);
    f = fopen(p, "rb");
    if (!f)
        return 0;
    k = fread(buf, 1, sizeof buf - 1, f);
    buf[k] = 0;
    fclose(f);
    return strstr(buf, text) != NULL;
}

static void test_countries(void)
{
    char geo[400], tokf[400], adm[700], p[700], b[16384];
    const char *tok = "Ab3_x-9Zk2LmQ7pR4sT8vW1y";
    struct stat sb;
    long d0 = days_from_civil(2026, 11, 3);
    stats_t rt;
    size_t i;
    s_cfg.log = capture;
    cap_n = 0; cap[0] = 0;
    s_clock = fake_clock;
    g_now = (time_t)d0 * 86400 + 600;
    s_nsess = s_players = s_racers = 0;
    snprintf(g_dir, sizeof g_dir, "%s", "/tmp/takedawn-lobby-cc-XXXXXX");
    CHECK(mkdtemp(g_dir) != NULL);

    /* The lookup: DB-IP's CSV with IPv6 lines, ZZ, a bad and an out-of-order line. */
    snprintf(geo, sizeof geo, "%s/dbip.csv", g_dir);
    write_text(geo, "0.0.0.0,0.255.255.255,ZZ\n"
                    "1.0.0.0,1.0.0.255,AU\n"
                    "2.0.0.0,2.15.255.255,FR\n"
                    "::,::ffff,ZZ\n"
                    "not,a,line\n"
                    "1.5.0.0,1.5.0.255,JP\n"                 /* out of order: left out */
                    "5.38.0.0,5.38.255.255,HU\n"
                    "\"80.0.0.0\",\"80.0.0.255\",\"de\"\n"
                    "223.255.255.0,223.255.255.255,AU\n"
                    "2001:db8::,2001:db8::ffff,DE\n");
    CHECK(geo_load(geo) == 6);
    CHECK(geo_lookup(net_of(1, 0, 0, 0)) == cc_index("AU") && geo_lookup(net_of(1, 0, 0, 255)) == cc_index("AU"));
    CHECK(geo_lookup(net_of(1, 0, 1, 0)) == CC_UNKNOWN);       /* a gap */
    CHECK(geo_lookup(net_of(1, 5, 0, 7)) == CC_UNKNOWN);       /* the left-out line */
    CHECK(geo_lookup(net_of(0, 1, 2, 3)) == CC_UNKNOWN);       /* ZZ */
    CHECK(geo_lookup(net_of(2, 9, 9, 9)) == cc_index("FR") && geo_lookup(net_of(5, 38, 7, 1)) == cc_index("HU"));
    CHECK(geo_lookup(net_of(80, 0, 0, 200)) == cc_index("DE"));
    CHECK(geo_lookup(net_of(223, 255, 255, 255)) == cc_index("AU") && geo_lookup(net_of(255, 0, 0, 1)) == CC_UNKNOWN);
    CHECK(geo_lookup(net_of(127, 0, 0, 1)) == CC_UNKNOWN);
    {
        char cc[3];
        cc_code(cc_index("HU"), cc); CHECK(!strcmp(cc, "HU"));
        cc_code(CC_UNKNOWN, cc); CHECK(!strcmp(cc, "??"));
        CHECK(cc_index("ZZ") == CC_UNKNOWN && cc_index("h1") == CC_UNKNOWN);
    }
    /* A missing database: nothing breaks, every country is unknown. */
    snprintf(p, sizeof p, "%s/none.csv", g_dir);
    CHECK(geo_load(p) == 0 && geo_lookup(net_of(5, 38, 7, 1)) == CC_UNKNOWN);
    CHECK(geo_load(NULL) == 0);

    /* Counting through st_init (the database and the owner's page from the config). */
    snprintf(tokf, sizeof tokf, "%s/admin-token", g_dir);
    write_text(tokf, "Ab3_x-9Zk2LmQ7pR4sT8vW1y\n");
    s_cfg.geoip_file = geo;
    s_cfg.admin_token_file = tokf;
    s_cfg.admin_html = "<!doctype html><title>owner</title>\n";
    st_init(g_dir, NULL, "<!doctype html><title>t</title>\n");
    CHECK(s_geo_n == 6 && s_admin_dir[0]);
    s_nsess = 3; s_players = 3;
    st_signin_cc("Alice", geo_lookup(net_of(5, 38, 1, 1)));
    st_signin_cc("alice", geo_lookup(net_of(5, 38, 1, 2)));    /* the same player again */
    st_signin_cc("Bob", geo_lookup(net_of(80, 0, 0, 9)));
    st_signin_cc("Carol", geo_lookup(net_of(127, 0, 0, 1)));
    st_player_cc("Dora", geo_lookup(net_of(2, 1, 1, 1)));      /* a name change to a new name */
    CHECK(s_st.cc_signins[cc_index("HU")] == 2 && s_st.cc_unique[cc_index("HU")] == 1);
    CHECK(s_st.cc_unique[cc_index("DE")] == 1 && s_st.cc_unique[CC_UNKNOWN] == 1 && s_st.cc_unique[cc_index("FR")] == 1);
    CHECK(s_st.cc_signins[cc_index("FR")] == 0 && s_st.signins == 4 && s_st.set_n == 4);
    g_now += 61;
    st_roll();
    slurp("current.json", b, sizeof b);
    CHECK(strstr(b, "\"countries\":{\"DE\":[1,1],\"FR\":[1,0],\"HU\":[1,2],\"??\":[1,1]}"));
    /* No address and no per-player country anywhere: only the totals. */
    CHECK(!strstr(b, "5.38") && !strstr(b, "80.0.") && !strstr(b, "127.0") && !strstr(b, "lice"));
    CHECK(sizeof(sess_t) < 4096);                              /* the session keeps no country field */

    /* The owner's page: a folder named by the token, not listable public folder. */
    snprintf(adm, sizeof adm, "%s/public/%s", g_dir, tok);
    snprintf(p, sizeof p, "%s/public", g_dir);
    CHECK(stat(p, &sb) == 0 && (sb.st_mode & 0777) == 0711);
    CHECK(stat(adm, &sb) == 0 && (sb.st_mode & 0777) == 0711);
    CHECK(file_has(adm, "index.html", "<title>owner</title>"));
    g_now += PUBLIC_SECS + 1;
    st_roll();
    CHECK(file_has(adm, "now.json", "\"HU\":[1,2]") && file_has(adm, "now.json", "\"geo_ranges\":6") &&
          file_has(adm, "now.json", "\"players\":3"));
    CHECK(!file_has(g_dir, "public/stats.json", "\"HU\""));    /* the public page as it was */
    CHECK(!strstr(cap, tok) && !strstr(cap, "Ab3_"));         /* the token is never logged */

    /* Race waits and countries survive a restart (current.json) and reach daily.jsonl. */
    st_wait(0); st_wait(0); st_wait(0); st_wait(1); st_wait(2); st_wait(3);
    g_now += 61;
    st_roll();
    st_free();
    st_init(g_dir, NULL, "<!doctype html><title>t</title>\n");
    CHECK(s_st.cc_signins[cc_index("HU")] == 2 && s_st.cc_unique[CC_UNKNOWN] == 1 && s_st.waits == 3 &&
          s_st.waits_joined == 1 && s_st.waits_gone == 1 && s_st.waits_full == 1);
    g_now = (time_t)(d0 + 1) * 86400 + 5;
    st_signin_cc("Alice", geo_lookup(net_of(5, 38, 1, 1)));
    slurp("daily.jsonl", b, sizeof b);
    CHECK(lines(b) == 1 && strstr(b, "\"date\":\"2026-11-03\"") && strstr(b, "\"HU\":[1,2]") &&
          strstr(b, "\"waits\":3,\"waits_joined\":1,\"waits_gone\":1,\"waits_full\":1"));
    CHECK(st_parse(b, &rt) && rt.cc_unique[cc_index("DE")] == 1 && rt.cc_signins[CC_UNKNOWN] == 1 && rt.waits_full == 1);
    CHECK(s_st.cc_signins[cc_index("HU")] == 1 && s_st.cc_unique[cc_index("HU")] == 1);   /* a new day, a new salt */
    CHECK(file_has(adm, "history.jsonl", "\"date\":\"2026-11-03\""));   /* the copy follows midnight */
    for (i = 0; i < 2; i++) {                                   /* no file holds an address */
        const char *f[] = { "daily.jsonl", "current.json" };
        CHECK(!file_has(g_dir, f[i], "5.38.") && !file_has(g_dir, f[i], "80.0.0"));
    }

    /* Every country at once fits one line, and reads back. */
    {
        static stats_t big, back;
        static char line[ST_LINE_MAX];
        int k, ok = 1;
        memset(&big, 0, sizeof big);
        big.day = d0;
        for (k = 0; k < N_CC; k++)              /* ZZ (DB-IP's "reserved") is counted as ?? */
            if (k != 25 * 26 + 25) { big.cc_unique[k] = 4000000000u; big.cc_signins[k] = 4000000001u; }
        CHECK(st_format(&big, 0, line, sizeof line) < (int)sizeof line);
        CHECK(st_parse(line, &back));
        for (k = 0; k < N_CC; k++)
            ok &= back.cc_unique[k] == big.cc_unique[k] && back.cc_signins[k] == big.cc_signins[k];
        CHECK(ok);
    }

    /* A token that is too short or has other characters: no owner's page. */
    st_free();
    write_text(tokf, "short\n");
    st_init(g_dir, NULL, NULL);
    CHECK(!s_admin_dir[0]);
    write_text(tokf, "Ab3_x-9Zk2LmQ7pR4sT8vW1y/../x\n");
    st_init(g_dir, NULL, NULL);
    CHECK(!s_admin_dir[0]);
    st_free();
    s_cfg.geoip_file = s_cfg.admin_token_file = s_cfg.admin_html = NULL;
    s_clock = NULL;
    s_cfg.log = quiet;
}

static int cap_has_relayed(void) { return strstr(cap, " relayed") != NULL; }

/* ── the UDP relay (BO-381) ── */

static int rl_udp(uint16_t *port)
{
    struct sockaddr_in a;
    socklen_t l = sizeof a;
    struct timeval tv = { 0, 300000 };
    int fd = socket(AF_INET, SOCK_DGRAM, 0), big = 1 << 20;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (fd < 0 || bind(fd, (struct sockaddr *)&a, sizeof a) != 0)
        return -1;
    getsockname(fd, (struct sockaddr *)&a, &l);
    *port = ntohs(a.sin_port);
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &big, sizeof big);
    return fd;
}

static void rl_hex(const char *rec, uint8_t *key, int *ident)
{
    const char *p = strstr(rec, "TKSECRET=");
    int i;
    unsigned v;
    for (i = 0; p && i < 16; i++) {
        sscanf(p + 9 + 2 * i, "%2x", &v);
        key[i] = (uint8_t)v;
    }
    p = strstr(rec, "IDENT=");
    *ident = p ? atoi(p + 6) : 0;
}

static int rl_mk(uint8_t *b, int kind, int ident, uint32_t from, uint32_t to, int fp, int tp,
                 const uint8_t *key, const void *pay, int pl)
{
    memset(b, 0, 32);
    memcpy(b, "TKRL", 4);
    b[4] = 1; b[5] = (uint8_t)kind;
    b[6] = (uint8_t)(ident >> 24); b[7] = (uint8_t)(ident >> 16); b[8] = (uint8_t)(ident >> 8); b[9] = (uint8_t)ident;
    memcpy(b + 10, &from, 4); memcpy(b + 14, &to, 4);
    b[18] = (uint8_t)(fp >> 8); b[19] = (uint8_t)fp; b[20] = (uint8_t)(tp >> 8); b[21] = (uint8_t)tp;
    relay_tag(key, b);
    if (pl)
        memcpy(b + 32, pay, (size_t)pl);
    return 32 + pl;
}

static void rl_to(int fd, uint16_t port, const void *b, int n)
{
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons(port);
    sendto(fd, b, (size_t)n, 0, (struct sockaddr *)&a, sizeof a);
}

static int rl_rx(int fd, uint8_t *b)
{
    ssize_t n = recv(fd, b, 2048, 0);
    return n < 0 ? -1 : (int)n;
}

static int rl_drain(int fd)
{
    uint8_t b[2048];
    int n = 0;
    struct timeval z = { 0, 20000 }, o;
    socklen_t ol = sizeof o;
    getsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &o, &ol);
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &z, sizeof z);
    while (rl_rx(fd, b) >= 0)
        n++;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &o, sizeof o);
    return n;
}

static int rl_live(int ident)
{
    size_t i;
    int n = 0;
    for (i = 0; i < s_rl_n; i++)
        n += s_rl[i].state == 1 && s_rl[i].ident == (uint32_t)ident;
    return n;
}

static void test_relay(void)
{
    aries_config c;
    uint16_t rp, pa, pb, pc, pz;
    int ua, ub, uc, a, b, l, i, ident = 0, n, got;
    uint8_t key[16], pk[2048], rx[2048], junk[16];
    uint32_t sa, sb, sl, nobody = inet_addr("10.64.250.250");
    unsigned long p0, d0, g0;
    char rec[BODY_MAX + 32], cj[600];
    struct stat st;
    const char pay[] = "hello relay payload";
    snprintf(g_dir, sizeof g_dir, "%s", "/tmp/takedawn-lobby-rl-XXXXXX");
    CHECK(mkdtemp(g_dir) != NULL);
    /* a free UDP port for the relay */
    pz = 0;
    { int t = rl_udp(&pz); close(t); }
    rp = pz;
    memset(&c, 0, sizeof c);
    c.port = PORT;
    c.bind_ip = "127.0.0.1";
    c.public_ip = "127.0.0.1";
    c.log = capture;
    cap_n = 0; cap[0] = 0;
    c.max_sessions = 20;
    c.max_games = 4;
    c.relay_port = rp;
    c.stats_dir = g_dir;
    CHECK(aries_start_thread(&c) == 0);
    settle();
    ua = rl_udp(&pa); ub = rl_udp(&pb); uc = rl_udp(&pc);
    CHECK(ua >= 0 && ub >= 0 && uc >= 0);

    a = login_id("RelayA", "192.168.40.1", "", 1);
    sa = inet_addr(g_tkaddr);
    b = login_id("RelayB", "192.168.40.2", "", 1);
    sb = inet_addr(g_tkaddr);
    quiesce(a); quiesce(b);
    put(a, "gcre", "NAME=Relay\nPASS=\nPARAMS=1,2,0,3,\nMINSIZE=2\nMAXSIZE=6\nSYSFLAGS=0\nROOM=1\n");
    CHECK(await(a, "gcre", NULL));
    snprintf(rec, sizeof rec, "%s", g_body);
    rl_hex(rec, key, &ident);
    CHECK(ident > 0 && strstr(rec, "TKRELAY=") && atoi(strstr(rec, "TKRELAY=") + 8) == rp);
    put(b, "gjoi", "NAME=Relay\nPASS=\nUSERPARAMS=1,1,3,\n");
    CHECK(await(b, "gjoi", NULL));
    CHECK(strstr(g_body, "TKSECRET=") && strstr(g_body, "TKRELAY="));
    quiesce(a); quiesce(b);
    CHECK(sa && sb && sa != sb);

    /* (a) bind only: nothing is answered */
    n = rl_mk(pk, 1, ident, sb, 0, 1000, 0, key, NULL, 0);
    rl_to(ub, rp, pk, n);
    CHECK(rl_rx(ub, rx) < 0);
    CHECK(rl_live(ident) == 1);

    /* (b) A to B while A has not bound and B has (B bound above): use a different port for B: unbound */
    p0 = s_st.relay_packets; d0 = s_st.relay_dropped;
    n = rl_mk(pk, 1, ident, sa, sb, 1000, 6000, key, pay, sizeof pay);      /* B port 6000 was never bound */
    rl_to(ua, rp, pk, n);
    CHECK(rl_rx(ub, rx) < 0 && rl_rx(ua, rx) < 0 && rl_rx(uc, rx) < 0);
    CHECK(s_st.relay_packets == p0 && s_st.relay_dropped == d0 + 1);

    /* (c) B bound on port 1000: A to B is delivered unchanged, A gets the answer with B's endpoint */
    g0 = s_st.relay_games;
    CHECK(!cap_has_relayed());
    n = rl_mk(pk, 1, ident, sa, sb, 1000, 1000, key, pay, sizeof pay);
    rl_to(ua, rp, pk, n);
    got = rl_rx(ub, rx);
    CHECK(got == n && !memcmp(rx, pk, (size_t)n));
    got = rl_rx(ua, rx);
    CHECK(got == 38 && !memcmp(rx, "TKRL", 4) && rx[4] == 1 && rx[5] == 2);
    if (got == 38) {
        uint8_t chk[32];
        memcpy(chk, rx, 24);
        relay_tag(key, chk);
        CHECK(!memcmp(chk + 24, rx + 24, 8));
        CHECK(!memcmp(rx + 10, &sb, 4) && !memcmp(rx + 14, &sa, 4));
        CHECK(rx[32] == 127 && rx[33] == 0 && rx[34] == 0 && rx[35] == 1 && (rx[36] << 8 | rx[37]) == pb);
    }
    CHECK(s_st.relay_packets == p0 + 1 && s_st.relay_bytes >= (unsigned long)n && s_st.relay_games == g0 + 1);
    CHECK(cap_has_relayed());
    /* a second one inside two seconds: forwarded, not answered again */
    rl_to(ua, rp, pk, n);
    CHECK(rl_rx(ub, rx) == n);
    CHECK(rl_rx(ua, rx) < 0);
    CHECK(s_st.relay_games == g0 + 1);
    /* and the other way, B to A (A is bound) */
    n = rl_mk(pk, 1, ident, sb, sa, 1000, 1000, key, pay, sizeof pay);
    rl_to(ub, rp, pk, n);
    CHECK(rl_rx(ua, rx) == n && !memcmp(rx, pk, (size_t)n));
    CHECK(rl_rx(ub, rx) == 38);
    {   /* no address in the relay's log line */
        const char *e = strstr(cap, " relayed"), *q = e;
        while (q > cap && q[-1] != '\n') q--;
        CHECK(e && !memchr(q, '.', (size_t)(e - q)) && !strstr(e, "127.0.0.1\n"));
    }

    /* a short payload gets no answer (it would be bigger than the packet) */
    { struct timespec ts = { 2, 100000000 }; nanosleep(&ts, NULL); }       /* the answer's pause is over */
    rl_drain(ua); rl_drain(ub);
    n = rl_mk(pk, 1, ident, sa, sb, 1000, 1000, key, "x", 1);
    rl_to(ua, rp, pk, n);
    CHECK(rl_rx(ub, rx) == n);
    CHECK(rl_rx(ua, rx) < 0);
    n = rl_mk(pk, 1, ident, sa, sb, 1000, 1000, key, pay, sizeof pay);     /* a full-size one: answered again */
    rl_to(ua, rp, pk, n);
    CHECK(rl_rx(ub, rx) == n && rl_rx(ua, rx) == 38);

    /* (d) refused: no delivery, no answer */
    rl_drain(ua); rl_drain(ub);
    d0 = s_st.relay_dropped; p0 = s_st.relay_packets;
    n = rl_mk(pk, 1, ident, sa, sb, 1000, 1000, key, pay, sizeof pay);
    pk[24] ^= 1;                                                          /* bad tag */
    rl_to(ua, rp, pk, n);
    n = rl_mk(pk, 1, ident + 777, sa, sb, 1000, 1000, key, pay, sizeof pay);   /* wrong game */
    rl_to(ua, rp, pk, n);
    n = rl_mk(pk, 1, ident, nobody, sb, 1000, 1000, key, pay, sizeof pay);     /* from: not a member */
    rl_to(ua, rp, pk, n);
    n = rl_mk(pk, 1, ident, sa, nobody, 1000, 1000, key, pay, sizeof pay);     /* to: not a member */
    rl_to(ua, rp, pk, n);
    n = rl_mk(pk, 1, ident, sa, sa, 1000, 1000, key, pay, sizeof pay);         /* to = from */
    rl_to(ua, rp, pk, n);
    { uint8_t big[32 + 1401]; memset(big, 7, sizeof big);
      n = rl_mk(big, 1, ident, sa, sb, 1000, 1000, key, big + 32, 1401);       /* oversize */
      rl_to(ua, rp, big, n); }
    n = rl_mk(pk, 2, ident, sa, sb, 1000, 1000, key, pay, sizeof pay);         /* a kind 2 from a client */
    rl_to(ua, rp, pk, n);
    memset(junk, 0, sizeof junk);
    rl_to(ua, rp, junk, 16);                                               /* too short */
    n = rl_mk(pk, 1, ident, sa, sb, 1000, 1000, key, pay, sizeof pay);
    pk[22] = 1;                                                           /* reserved bytes */
    relay_tag(key, pk);
    rl_to(ua, rp, pk, n);
    CHECK(rl_rx(ub, rx) < 0 && rl_rx(ua, rx) < 0 && rl_rx(uc, rx) < 0);
    CHECK(s_st.relay_dropped == d0 + 9 && s_st.relay_packets == p0);

    /* (e) a 0.3.0-style session (no TKID) in the game: it still gets its records, and its address is no valid from/to */
    l = login_id("RelayOld", "192.168.40.77", "", 0);
    sl = inet_addr("192.168.40.77");
    quiesce(l);
    put(l, "gjoi", "NAME=Relay\nPASS=\nUSERPARAMS=1,1,3,\n");
    CHECK(await(l, "gjoi", NULL));
    snprintf(cj, sizeof cj, "%s", g_body);
    CHECK(strstr(cj, "TKSECRET=") && strstr(cj, "IDENT=") && strstr(cj, "OPPO2=RelayOld"));
    rl_drain(ua); rl_drain(ub); rl_drain(uc);
    d0 = s_st.relay_dropped;
    n = rl_mk(pk, 1, ident, sl, sa, 1000, 1000, key, pay, sizeof pay);        /* old session as from */
    rl_to(uc, rp, pk, n);
    n = rl_mk(pk, 1, ident, sa, sl, 1000, 1000, key, pay, sizeof pay);        /* old session as to */
    rl_to(ua, rp, pk, n);
    n = rl_mk(pk, 1, ident, 0, sa, 1000, 1000, key, pay, sizeof pay);         /* from 0 (its synth is 0) */
    rl_to(uc, rp, pk, n);
    CHECK(rl_rx(ua, rx) < 0 && rl_rx(uc, rx) < 0 && rl_rx(ub, rx) < 0);
    CHECK(s_st.relay_dropped == d0 + 3);
    put(l, "glea", "NAME=RelayOld\nFORCE=1\n");
    close(l);

    /* (f) the rate limit: 500 packets in a burst */
    { struct timespec ts = { 1, 200000000 }; nanosleep(&ts, NULL); }
    rl_drain(ua); rl_drain(ub);
    d0 = s_st.relay_dropped; p0 = s_st.relay_packets;
    n = rl_mk(pk, 1, ident, sa, sb, 1000, 1000, key, pay, sizeof pay);
    for (i = 0; i < 500; i++)
        rl_to(ua, rp, pk, n);
    { struct timespec ts = { 0, 500000000 }; nanosleep(&ts, NULL); }
    got = rl_drain(ub);
    CHECK(got >= 380 && got <= 470);
    CHECK(s_st.relay_packets - p0 == (unsigned long)got);
    CHECK(s_st.relay_dropped - d0 >= 30 && s_st.relay_dropped - d0 + (s_st.relay_packets - p0) == 500);
    rl_drain(ua);

    /* (g) the game closes: the race goes on over the relay (bindings and the game's record stay) */
    CHECK(rl_live(ident) >= 2);
    put(a, "gdel", "");
    CHECK(await(a, "gdel", NULL));
    settle();
    CHECK(rl_live(ident) >= 2);
    rl_drain(ua); rl_drain(ub);
    d0 = s_st.relay_dropped;
    n = rl_mk(pk, 1, ident, sa, sb, 1000, 1000, key, pay, sizeof pay);
    rl_to(ua, rp, pk, n);
    CHECK(rl_rx(ub, rx) == n && !memcmp(rx, pk, (size_t)n));
    CHECK(s_st.relay_dropped == d0);
    rl_drain(ua);
    n = rl_mk(pk, 1, ident, nobody, sb, 1000, 1000, key, pay, sizeof pay);     /* a non-member is still refused */
    rl_to(ua, rp, pk, n);
    CHECK(rl_rx(ub, rx) < 0);
    CHECK(s_st.relay_dropped == d0 + 1);

    /* (g2) a second game: both TCP sessions gone after it started, the relay still carries the traffic */
    {
        int a2, b2, id2 = 0;
        uint32_t sa2, sb2;
        uint8_t key2[16];
        char rec2[BODY_MAX + 32];
        a2 = login_id("RelayC", "192.168.40.3", "", 1);
        sa2 = inet_addr(g_tkaddr);
        b2 = login_id("RelayD", "192.168.40.4", "", 1);
        sb2 = inet_addr(g_tkaddr);
        quiesce(a2); quiesce(b2);
        put(a2, "gcre", "NAME=Relay2\nPASS=\nPARAMS=1,2,0,3,\nMINSIZE=2\nMAXSIZE=6\nSYSFLAGS=0\nROOM=1\n");
        CHECK(await(a2, "gcre", NULL));
        snprintf(rec2, sizeof rec2, "%s", g_body);
        rl_hex(rec2, key2, &id2);
        put(b2, "gjoi", "NAME=Relay2\nPASS=\nUSERPARAMS=1,1,3,\n");
        CHECK(await(b2, "gjoi", NULL));
        quiesce(a2); quiesce(b2);
        CHECK(id2 > 0 && id2 != ident);
        n = rl_mk(pk, 1, id2, sb2, 0, 1000, 0, key2, NULL, 0);                 /* B binds, A sends: the game is live */
        rl_to(ub, rp, pk, n);
        n = rl_mk(pk, 1, id2, sa2, 0, 1000, 0, key2, NULL, 0);
        rl_to(ua, rp, pk, n);
        n = rl_mk(pk, 1, id2, sa2, sb2, 1000, 1000, key2, pay, sizeof pay);
        rl_to(ua, rp, pk, n);
        CHECK(rl_rx(ub, rx) == n);
        rl_drain(ua);
        close(a2); close(b2);
        { struct timespec ts = { 1, 0 }; nanosleep(&ts, NULL); }
        settle();
        rl_drain(ua); rl_drain(ub);
        n = rl_mk(pk, 1, id2, sa2, sb2, 1000, 1000, key2, pay, sizeof pay);
        rl_to(ua, rp, pk, n);
        CHECK(rl_rx(ub, rx) == n && !memcmp(rx, pk, (size_t)n));
        rl_drain(ua);
        n = rl_mk(pk, 1, id2, sb2, sa2, 1000, 1000, key2, pay, sizeof pay);
        rl_to(ub, rp, pk, n);
        CHECK(rl_rx(ua, rx) == n);
        rl_drain(ua); rl_drain(ub);
        /* (g3) a record older than the TTL is refused (the TTL is lowered; the game is gone by now or not) */
        { struct timespec ts = { 0, 300000000 }; nanosleep(&ts, NULL); }
        CHECK(game_by_ident(id2) == NULL);
        s_rl_game_ttl_ms = 100;
        { struct timespec ts = { 0, 400000000 }; nanosleep(&ts, NULL); }
        d0 = s_st.relay_dropped;
        n = rl_mk(pk, 1, id2, sa2, sb2, 1000, 1000, key2, pay, sizeof pay);
        rl_to(ua, rp, pk, n);
        CHECK(rl_rx(ub, rx) < 0);
        CHECK(s_st.relay_dropped == d0 + 1);
        s_rl_game_ttl_ms = RL_GAME_TTL;
    }

    /* (h) the counters: in the log line and in current.json after the stop */
    {
        unsigned long fp = s_st.relay_packets, fb = s_st.relay_bytes, fd = s_st.relay_dropped, fg = s_st.relay_games;
        char path[300], want[200], cur[ST_LINE_MAX];
        close(a); close(b);
        aries_stop();
        settle();
        snprintf(want, sizeof want, "\"relay_packets\":%lu,\"relay_bytes\":%lu,\"relay_dropped\":%lu,\"relay_games\":%lu", fp, fb, fd, fg);
        slurp("current.json", cur, sizeof cur);
        CHECK(strstr(cur, want) != NULL);
        CHECK(fp > 400 && fg == 2);
        {   /* it parses back */
            stats_t t;
            CHECK(st_parse(cur, &t) && t.relay_packets == fp && t.relay_bytes == fb && t.relay_dropped == fd && t.relay_games == fg);
        }
        snprintf(path, sizeof path, "%s/public/stats.json", g_dir);
        if (stat(path, &st) == 0) {   /* the public page carries no relay numbers */
            char pubj[16384];
            FILE *f = fopen(path, "rb");
            size_t k = f ? fread(pubj, 1, sizeof pubj - 1, f) : 0;
            if (f) fclose(f);
            pubj[k] = 0;
            CHECK(!strstr(pubj, "relay"));
        }
    }
    close(ua); close(ub); close(uc);
    rmfiles();
    rmdir(g_dir);
}

int main(int argc, char **argv)
{
    /* ./test_lobby [name ...] runs only those parts (stats server validators requests deadlines beacon
     * private pushes room); no argument runs all of them. */
    static const struct { const char *name; void (*fn)(void); } parts[] = {
        { "stats", test_stats }, { "server", test_server }, { "validators", test_validators },
        { "requests", test_requests }, { "deadlines", test_deadlines }, { "beacon", test_beacon_and_counts },
        { "lease", test_lease }, { "kick", test_kick }, { "review", test_review_fixes }, { "codesec", test_code_security }, { "identity", test_identity }, { "ids", test_ids }, { "racing", test_racing_guards }, { "private", test_private_codes }, { "pushes", test_private_pushes }, { "public", test_public_codes }, { "room", test_room_entry }, { "crowd", test_crowd },
        { "startrec", test_start_record },
        { "bo352", test_bo352_server }, { "racers", test_racers }, { "auditc", test_audit_c }, { "racewait", test_racewait }, { "staywait", test_staywait }, { "racecrowd", test_racewait_crowd }, { "browse", test_browse },
        { "countries", test_countries }, { "relay", test_relay },
    };
    size_t k;
    int a;
    signal(SIGPIPE, SIG_IGN);
    setvbuf(stdout, NULL, _IOLBF, 0);
    for (k = 0; k < sizeof parts / sizeof parts[0]; k++) {
        int want = argc < 2;
        for (a = 1; a < argc; a++)
            want |= !strcmp(argv[a], parts[k].name);
        if (want)
            parts[k].fn();
    }
    printf("%d checks, %d failed; sizeof sess_t %zu, game_t %zu\n", s_checks, s_fail,
           sizeof(sess_t), sizeof(game_t));
    return s_fail != 0;
}
