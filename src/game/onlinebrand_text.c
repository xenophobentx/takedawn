#include "onlinebrand.h"

/* Whole-string rules for the US table (the title's own English text, matched
 * exactly; other languages have other texts and take only the Xbox Live
 * rule below). UTF-8 in this file, "\xe2\x80\xa6" is the ellipsis. The list
 * and the strings left alone are in .handoff/takedawn-online/step3b/STRINGS.md. */
static const struct { const char *from, *to; } k_exact[] = {
    { "RETRIEVING LIST OF GAMES FROM EA", "SEARCHING FOR A GAME..." },
    { "QUICK MATCH", "QUICK PLAY" },                /* the search / join page's title (BROWSE GAMES shares it) */
    { "CONNECTING TO EA", "CONNECTING TO TAKEDAWN ONLINE" },
    { "CONNECTING TO EA\xe2\x80\xa6", "CONNECTING TO TAKEDAWN ONLINE\xe2\x80\xa6" },
    { "SIGNING IN TO EA", "SIGNING IN TO TAKEDAWN ONLINE" },
    { "SIGNING IN TO EA\xe2\x80\xa6.", "SIGNING IN TO TAKEDAWN ONLINE\xe2\x80\xa6" },
    { "Failed to sign in to EA.", "Failed to sign in to Takedawn Online." },
    { "User already signed in to EA. Please try a different account.",
      "User already signed in to Takedawn Online. Please try a different account." },
    { "Failed to sign in to EA - but error seems to be set wrong.", "Failed to sign in to Takedawn Online." },
    { "Retrieving news from the EA server...", "Retrieving news from Takedawn Online..." },
    { "There was an error retrieving news from the EA server", "There was an error retrieving news from Takedawn Online" },
    { "No EA server available.", "No Takedawn Online server available." },
    { "EA server is busy.", "The Takedawn Online server is busy." },
    { "The EA server connection has timed out. Would you like to reconnect?",
      "The connection to Takedawn Online has timed out. Would you like to reconnect?" },
    { "The EA Server connection has timed out. Would you like to reconnect?",
      "The connection to Takedawn Online has timed out. Would you like to reconnect?" },
    { "A cheat device has been detected on your system.  You will now be disconnected from the EA server.  Remove the device if you want to reconnect.",
      "A cheat device has been detected on your system.  You will now be disconnected from Takedawn Online.  Remove the device if you want to reconnect." },
    { "Are you sure you want to Sign out? All unread messages will be lost.", "Are you sure you want to sign out?" },
};

/* the string's UTF-16 units equal the UTF-8 text (ASCII and the ellipsis) */
static int equals_utf8(const uint16_t *s, size_t n, const char *t)
{
    size_t i = 0;
    while (*t) {
        uint16_t c;
        if ((unsigned char)t[0] == 0xE2 && (unsigned char)t[1] == 0x80 && (unsigned char)t[2] == 0xA6) { c = 0x2026; t += 3; }
        else c = (uint8_t)*t++;
        if (i >= n || s[i++] != c) return 0;
    }
    return i == n;
}
static size_t utf8_len(const char *t)
{
    size_t n = 0;
    while (*t) { t += ((unsigned char)*t == 0xE2) ? 3 : 1; ++n; }
    return n;
}
static void put_utf8(uint16_t *d, const char *t)
{
    while (*t) {
        if ((unsigned char)*t == 0xE2) { *d++ = 0x2026; t += 3; } else *d++ = (uint8_t)*t++;
    }
    *d = 0;
}

static int matches(const uint16_t *s, size_t n)
{
    static const char term[] = "xbox live";
    if (n < sizeof term - 1) return 0;
    for (size_t i = 0; i < sizeof term - 1; ++i) {
        uint16_t c = s[i];
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (c != (uint8_t)term[i]) return 0;
    }
    return 1;
}
size_t bo3_onlinebrand_replace(const uint16_t *src, size_t limit, uint16_t *dst, size_t capacity)
{
    static const char upper[] = "TAKEDAWN ONLINE", title[] = "Takedawn Online";
    size_t n = 0, count = 0, out = 0;
    if (!src) return 0;
    while (n < limit && src[n]) ++n;
    if (n == limit) return 0;
    for (size_t r = 0; r < sizeof k_exact / sizeof k_exact[0]; ++r)
        if (equals_utf8(src, n, k_exact[r].from)) {
            size_t need = utf8_len(k_exact[r].to) + 1;
            if (!dst || capacity < need) return need;
            put_utf8(dst, k_exact[r].to);
            return need;
        }
    for (size_t i = 0; i < n;) {
        if (matches(src + i, n - i)) { ++count; i += 9; } else ++i;
    }
    if (!count || n > SIZE_MAX - 1 - count * 6) return 0;
    size_t need = n + count * 6 + 1;
    if (!dst || capacity < need) return need;
    for (size_t i = 0; i < n;) {
        if (matches(src + i, n - i)) {
            int all_upper = 1;
            for (size_t j = 0; j < 9; ++j)
                if (src[i+j] >= 'a' && src[i+j] <= 'z') all_upper = 0;
            const char *replacement = all_upper ? upper : title;
            for (size_t j = 0; j < 15; ++j) dst[out++] = (uint8_t)replacement[j];
            i += 9;
        } else dst[out++] = src[i++];
    }
    dst[out] = 0;
    return need;
}
