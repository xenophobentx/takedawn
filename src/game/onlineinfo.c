/*
 * onlineinfo.c - the online info line (BO-352, step 8). See onlineinfo.h.
 * The parser and text are pure; the probe is a plain host TCP exchange with
 * the lobby (the same server the title talks to), a 12-byte header {cmd[4],
 * code[4], len[4 BE]} then a NUL-terminated "KEY=VALUE\n" body, as
 * tools/lobby/aries_server.c speaks.
 */
#include "onlineinfo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int bo3_onlineinfo_field_int(const char *body, const char *key, int *out)
{
    size_t kl = strlen(key);
    const char *p = body;
    while (*p) {
        char v[32];
        size_t i = 0;
        while (*p == '\n' || *p == ' ' || *p == '\r' || *p == '\t')
            p++;
        if (!strncmp(p, key, kl) && p[kl] == '=') {
            p += kl + 1;
            while (*p && *p != '\n' && *p != '\r' && *p != ' ' && i + 1 < sizeof v)
                v[i++] = *p++;
            v[i] = 0;
            if (out)
                *out = atoi(v);
            return 1;
        }
        while (*p && *p != '\n' && *p != ' ')
            p++;
    }
    return 0;
}

int bo3_onlineinfo_parse_status(const char *body, int *online, int *waiting)
{
    int mask = 0;
    if (bo3_onlineinfo_field_int(body, "TKONLINE", online))
        mask |= 1;
    if (bo3_onlineinfo_field_int(body, "TKWAITING", waiting))
        mask |= 2;
    return mask;
}

void bo3_onlineinfo_text(char *out, size_t n, int online, int waiting, int ping_ms)
{
    if (online < 0) {
        snprintf(out, n, "ONLINE");
        return;
    }
    if (ping_ms >= 0)
        snprintf(out, n, "%d ONLINE - %d OPEN GAMES - SERVER %d MS", online, waiting < 0 ? 0 : waiting, ping_ms);
    else
        snprintf(out, n, "%d ONLINE - %d OPEN GAMES", online, waiting < 0 ? 0 : waiting);
}

void bo3_onlineinfo_counts(char *out, size_t n, int mode, int have, int online, int waiting)
{
    if (!out || !n)
        return;
    out[0] = 0;
    if (mode == 2) {
        snprintf(out, n, "LOCAL NETWORK");
        return;
    }
    if (mode != 1 || !have || online < 0)
        return;
    if (waiting < 0)
        waiting = 0;
    snprintf(out, n, "%d PLAYER%s ONLINE - %d GAME%s OPEN", online, online == 1 ? "" : "S",
             waiting, waiting == 1 ? "" : "S");
}

void bo3_onlineinfo_counts_racing(char *out, size_t n, int mode, int have, int online, int waiting, int racing)
{
    size_t k;
    bo3_onlineinfo_counts(out, n, mode, have, online, waiting);
    if (!out || !n || mode != 1 || !out[0] || racing <= 0)
        return;
    k = strlen(out);
    snprintf(out + k, n - k, " - %d RACING", racing);
}

const char *bo3_onlineinfo_browse_hint(const char *hint, int have, int waiting, int racing)
{
    return have && waiting == 0 && racing > 0 ? "RACING NOW: PICK ONE TO JOIN AFTER ITS RACE" : hint;
}

void bo3_onlineinfo_menu_line(char *out, size_t n, const char *counts)
{
    if (!out || !n)
        return;
    if (counts && counts[0])
        snprintf(out, n, "RACE ONLINE - %s", counts);
    else
        snprintf(out, n, "RACE ONLINE");
}

void bo3_onlineinfo_page_line(char *out, size_t n, const char *counts, const char *hint)
{
    if (!out || !n)
        return;
    if (counts && counts[0] && hint && hint[0])
        snprintf(out, n, "%s - %s", counts, hint);
    else
        snprintf(out, n, "%s", counts && counts[0] ? counts : hint ? hint : "");
}

#ifndef ONLINEINFO_PURE_ONLY

#include <time.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR   (-1)
#define closesocket    close
#endif

#include "xnet_hle.h"           /* xnet_hle_lobby_ip / xnet_hle_lobby_port */

static unsigned long long info_ms(void)
{
#ifdef _WIN32
    return (unsigned long long)GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000ull + (unsigned long long)ts.tv_nsec / 1000000ull;
#endif
}

/* one @dir exchange with the lobby, 2 s at most; -1 on failure */
static int probe_exchange(char *body, size_t n, unsigned long long *elapsed)
{
    uint32_t ip = xnet_hle_lobby_ip();
    uint16_t port = xnet_hle_lobby_port();
    int s, r;
    unsigned char hdr[12];
    uint32_t len;
    unsigned long long t0, t1;
    struct sockaddr_in a;
    struct timeval tv;

    if (!ip || !port)
        return -1;
    s = (int)socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET)
        return -1;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = ip;                 /* already network order */
    a.sin_port = htons(port);
    tv.tv_sec = 2; tv.tv_usec = 0;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof tv);
    t0 = info_ms();
    if (connect(s, (struct sockaddr *)&a, sizeof a) == SOCKET_ERROR) {
        closesocket(s);
        return -1;
    }
    memcpy(hdr, "@dir", 4);                 /* cmd */
    memset(hdr + 4, 0, 4);                  /* code */
    len = 12u + 10u;                        /* header + "TKPROBE=1\0" */
    hdr[8] = (unsigned char)(len >> 24); hdr[9] = (unsigned char)(len >> 16);
    hdr[10] = (unsigned char)(len >> 8); hdr[11] = (unsigned char)len;
    if (send(s, (const char *)hdr, 12, 0) != 12 ||
        send(s, "TKPROBE=1\0", 10, 0) != 10) {
        closesocket(s);
        return -1;
    }
    r = (int)recv(s, (char *)hdr, 12, 0);
    if (r != 12) {
        closesocket(s);
        return -1;
    }
    len = ((uint32_t)hdr[8] << 24) | ((uint32_t)hdr[9] << 16) | ((uint32_t)hdr[10] << 8) | hdr[11];
    if (len < 12 || len - 12 >= n) {
        closesocket(s);
        return -1;
    }
    r = (int)recv(s, body, (int)(len - 12), 0);
    t1 = info_ms();
    closesocket(s);
    if (r <= 0)
        return -1;
    body[r] = 0;
    if (elapsed)
        *elapsed = t1 > t0 ? t1 - t0 : 0;
    return 0;
}

int bo3_onlineinfo_probe(int *online, int *waiting, int *ping_ms)
{
    char body[1024];
    unsigned long long elapsed = 0;
    int mask = 0;
    if (ping_ms)
        *ping_ms = -1;
    if (probe_exchange(body, sizeof body, &elapsed) < 0)
        return 0;
    mask = bo3_onlineinfo_parse_status(body, online, waiting);
    if (ping_ms)
        *ping_ms = (int)(elapsed > 0x7FFFFFFFull ? 0x7FFFFFFF : elapsed);
    return mask;
}

/* ---- the counts kept fresh (bo3_onlineinfo_want / _latest) ----
 * One thread, started the first time a menu wants the counts. It never
 * touches the title's memory: it asks the lobby over a socket of its own
 * and leaves three numbers. */
#define INFO_EVERY_MS 30000ull
#define INFO_KICK_MS  3000ull           /* a new page asks again, at most this often */

static volatile int s_info_want, s_info_thread, s_info_kick;
static volatile int s_probe_ok, s_probe_on, s_probe_wt;
static volatile unsigned long long s_probe_ms, s_want_ms;
static int s_stat_on = -1, s_stat_wt = -1;
static unsigned long long s_stat_ms;

#ifdef _WIN32
static DWORD WINAPI info_thread(LPVOID arg)
#else
#include <pthread.h>
static void *info_thread(void *arg)
#endif
{
    unsigned long long last = 0;
    (void)arg;
    for (;;) {
        unsigned long long now = info_ms();
        if (now - s_want_ms < 2000ull &&
            (!last || now - last >= INFO_EVERY_MS || (s_info_kick && now - last >= INFO_KICK_MS))) {
            int on = -1, wt = -1, ping = -1;
            int mask;
            s_info_kick = 0;
            mask = bo3_onlineinfo_probe(&on, &wt, &ping);
            last = info_ms();
            if (mask & 1) {
                s_probe_on = on;
                s_probe_wt = (mask & 2) ? wt : 0;
                s_probe_ms = last;
                s_probe_ok = 1;
            } else {
                s_probe_ok = 0;         /* no server (or an old one): nothing to show */
                s_probe_ms = last;
            }
        }
#ifdef _WIN32
        Sleep(250);
#else
        usleep(250000);
#endif
    }
#ifndef _WIN32
    return NULL;
#endif
}

void bo3_onlineinfo_want(int want)
{
    if (!want)
        return;
    s_want_ms = info_ms();
    if (!s_info_thread && xnet_hle_on() == 1) {
        s_info_thread = 1;
#ifdef _WIN32
        {
            HANDLE h = CreateThread(NULL, 0, info_thread, NULL, 0, NULL);
            if (h)
                CloseHandle(h);
        }
#else
        {
            pthread_t t;
            if (pthread_create(&t, NULL, info_thread, NULL) == 0)
                pthread_detach(t);
        }
#endif
    }
}

void bo3_onlineinfo_kick(void)
{
    s_info_kick = 1;
}

int bo3_onlineinfo_latest(int *online, int *waiting)
{
    int on = -1, wt = -1;
    /* the counts the lobby pushes to the signed-in title: newest when they change */
    if (xnet_hle_lobby_status(&on, &wt) && on >= 0 && (on != s_stat_on || wt != s_stat_wt)) {
        s_stat_on = on;
        s_stat_wt = wt;
        s_stat_ms = info_ms();
    }
    if (s_stat_ms && s_stat_ms >= s_probe_ms) {
        if (online) *online = s_stat_on;
        if (waiting) *waiting = s_stat_wt < 0 ? 0 : s_stat_wt;
        return 1;
    }
    if (s_probe_ok) {
        if (online) *online = s_probe_on;
        if (waiting) *waiting = s_probe_wt;
        return 1;
    }
    return 0;
}

#endif /* ONLINEINFO_PURE_ONLY */
