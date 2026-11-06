/*
 * takedawn-lobby: the lobby server for Takedawn's online play (Burnout 3's
 * Xbox Live menus), as its own program. MIT. See aries_server.h and
 * docs/online.md.
 *
 *   takedawn-lobby [--port 21810] [--public ADDRESS] [--bind ADDRESS]
 *                  [--lan] [--relay-port N] [--max-sessions N] [--max-games N]
 *                  [--max-players N] [--max-per-ip N] [--idle-secs S]
 *                  [--stats S] [--stats-dir DIR] [--public-dir DIR] [--crowd-test]
 *                  [--geoip FILE] [--admin-token-file FILE]
 *                  [--verbose]
 *
 * The limits can also come from TAKEDAWN_LOBBY_MAX_SESSIONS,
 * TAKEDAWN_LOBBY_MAX_GAMES and TAKEDAWN_LOBBY_MAX_PLAYERS, the statistics
 * directory from TAKEDAWN_LOBBY_STATS_DIR and the public page's from
 * TAKEDAWN_LOBBY_PUBLIC_DIR (a flag wins); the relay port from
 * TAKEDAWN_LOBBY_RELAY_PORT.
 */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aries_server.h"
#include "stats_page.h"     /* k_stats_page: public/index.html (embed_page.py) */
#include "admin_page.h"     /* k_admin_page: the owner's page (embed_page.py) */

static void on_signal(int sig) { (void)sig; aries_stop(); }

static int env_int(const char *name)
{
    const char *v = getenv(name);
    return v && *v ? atoi(v) : 0;
}

static void usage(void)
{
    fputs("takedawn-lobby: the lobby server for Takedawn's online play\n"
          "  --port N         TCP port (default 21810, the game's own)\n"
          "  --public ADDR    the address players reach this server at: an IPv4 address\n"
          "                   or a name (resolved at start and every 10 minutes)\n"
          "                   (default: this machine's address on its default route)\n"
          "  --bind ADDR      listen on one local address only\n"
          "  --lan            announce the lobby on the local network (UDP 21809)\n"
          "  --relay-port N   UDP port of the relay between the members of a game (the\n"
          "                   toolkit's online play falls back to it when a direct link\n"
          "                   fails; 21811 in the unit file; 0: off, the default; also\n"
          "                   TAKEDAWN_LOBBY_RELAY_PORT). Counts only, no addresses kept\n"
          "  --max-sessions N players connected at once (default 4096; the open-file\n"
          "                   limit, ulimit -n / LimitNOFILE, caps it)\n"
          "  --max-games N    games at once (default 2048)\n"
          "  --max-players N  players per game, 2 to 8 (default 8; the game asks for 6)\n"
          "                   (also TAKEDAWN_LOBBY_MAX_SESSIONS / _MAX_GAMES / _MAX_PLAYERS)\n"
          "  --max-per-ip N   connections from one address (default 64; 0: any number)\n"
          "  --idle-secs S    drop a connection that sent nothing for S seconds\n"
          "                   (default 90, two missed pings; 0: never). A connection that\n"
          "                   has not signed in within 25 s is dropped as well.\n"
          "  --stats S        log the session and game counts every S seconds\n"
          "  --stats-dir DIR  write usage totals (counts only: no addresses, no names)\n"
          "                   to DIR/daily.jsonl and DIR/current.json (also\n"
          "                   TAKEDAWN_LOBBY_STATS_DIR); without it nothing is written\n"
          "  --public-dir DIR write the public page there: index.html and stats.json\n"
          "                   (totals and open games, no names, no addresses; for a web\n"
          "                   server to serve). Default with --stats-dir: DIR/public\n"
          "                   (also TAKEDAWN_LOBBY_PUBLIC_DIR)\n"
          "  --geoip FILE     a DB-IP Lite country CSV: sign-ins are counted per country\n"
          "                   (the address is looked up, never kept; also TAKEDAWN_LOBBY_GEOIP)\n"
          "                   Without it, or if it is missing, countries are not known\n"
          "  --admin-token-file FILE  one line: the secret folder name of the owner's page\n"
          "                   (all-time history, every country) in the public folder,\n"
          "                   which is then not listable (also TAKEDAWN_LOBBY_ADMIN_TOKEN_FILE)\n"
          "  --crowd-test     test lobbies only: the load tool's players (tools/lobby/crowd)\n"
          "                   and real players do not see each other's games\n"
          "  --verbose        log every request and answer (and the players' addresses)\n", stderr);
}

int main(int argc, char **argv)
{
    aries_config c;
    int i;
    memset(&c, 0, sizeof c);
    c.port = 21810;
    c.max_sessions = env_int("TAKEDAWN_LOBBY_MAX_SESSIONS");
    c.max_games = env_int("TAKEDAWN_LOBBY_MAX_GAMES");
    if (c.max_sessions <= 0) c.max_sessions = 4096;
    if (c.max_games <= 0) c.max_games = 2048;
    c.stats_dir = getenv("TAKEDAWN_LOBBY_STATS_DIR");
    c.public_dir = getenv("TAKEDAWN_LOBBY_PUBLIC_DIR");
    c.page_html = k_stats_page;
    c.admin_html = k_admin_page;
    c.geoip_file = getenv("TAKEDAWN_LOBBY_GEOIP");
    c.admin_token_file = getenv("TAKEDAWN_LOBBY_ADMIN_TOKEN_FILE");
    c.max_players = env_int("TAKEDAWN_LOBBY_MAX_PLAYERS");
    c.relay_port = (unsigned short)env_int("TAKEDAWN_LOBBY_RELAY_PORT");
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--port") && i + 1 < argc) c.port = (unsigned short)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--public") && i + 1 < argc) c.public_ip = argv[++i];
        else if (!strcmp(argv[i], "--bind") && i + 1 < argc) c.bind_ip = argv[++i];
        else if (!strcmp(argv[i], "--lan")) c.beacon_port = 21809;
        else if (!strcmp(argv[i], "--relay-port") && i + 1 < argc) c.relay_port = (unsigned short)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--max-sessions") && i + 1 < argc) c.max_sessions = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--max-games") && i + 1 < argc) c.max_games = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--max-players") && i + 1 < argc) c.max_players = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--max-per-ip") && i + 1 < argc) { c.max_per_ip = atoi(argv[++i]); if (!c.max_per_ip) c.max_per_ip = -1; }
        else if (!strcmp(argv[i], "--idle-secs") && i + 1 < argc) { c.idle_secs = atoi(argv[++i]); if (!c.idle_secs) c.idle_secs = -1; }
        else if (!strcmp(argv[i], "--stats-dir") && i + 1 < argc) c.stats_dir = argv[++i];
        else if (!strcmp(argv[i], "--public-dir") && i + 1 < argc) c.public_dir = argv[++i];
        else if (!strcmp(argv[i], "--stats") && i + 1 < argc) c.stats_secs = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--geoip") && i + 1 < argc) c.geoip_file = argv[++i];
        else if (!strcmp(argv[i], "--admin-token-file") && i + 1 < argc) c.admin_token_file = argv[++i];
        else if (!strcmp(argv[i], "--crowd-test")) c.crowd_test = 1;
        else if (!strcmp(argv[i], "--verbose") || !strcmp(argv[i], "-v")) c.verbose = 1;
        else { usage(); return strcmp(argv[i], "--help") ? 2 : 0; }
    }
#ifdef SIGPIPE
    signal(SIGPIPE, SIG_IGN);
#endif
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    return aries_run(&c);
}
