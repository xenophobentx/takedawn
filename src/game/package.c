/*
 * package.c - which program of the package runs (BO-341). See package.h.
 */
#include "package.h"

#include <stdio.h>
#include <string.h>

int bo3_package_pick(int count, int self, int recorded, const int *whole)
{
    int i;

    if (count <= 0 || self < 0 || self >= count)
        return self;
    if (recorded >= count)
        recorded = -1;
    if (recorded >= 0 && whole[recorded])
        return recorded;
    if (whole[self])
        return self;
    for (i = 0; i < count; i++)
        if (whole[i])
            return i;
    return recorded >= 0 ? recorded : self;
}

int bo3_package_record_parse(const char *text, char *id, unsigned n)
{
    const char *p = text, *end;
    size_t len;

    if (!text || !id || !n)
        return 0;
    if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF)
        p += 3;                         /* a byte order mark from an editor */
    while (*p) {
        while (*p == ' ' || *p == '\t')
            p++;
        end = p + strcspn(p, "\r\n");
        len = (size_t)(end - p);
        while (len && (p[len - 1] == ' ' || p[len - 1] == '\t'))
            len--;
        if (len && *p != '#') {
            if (len >= n)
                return 0;
            memcpy(id, p, len);
            id[len] = 0;
            return 1;
        }
        p = end;
        while (*p == '\r' || *p == '\n')
            p++;
    }
    return 0;
}

int bo3_package_record_format(const char *id, char *out, unsigned n)
{
    int w = snprintf(out, n,
                     "%s\n"
                     "# The disc Takedawn starts: the one installed last (its build in\n"
                     "# tools/install/supported.json). Written by the install of a disc when\n"
                     "# another disc's data is installed too. Remove this file and the game\n"
                     "# takes the North America disc's data when it is there.\n",
                     id);
    return w > 0 && (unsigned)w < n;
}

static int ends_with_nocase(const char *s, size_t len, const char *tail)
{
    size_t t = strlen(tail), i;

    if (t > len)
        return 0;
    for (i = 0; i < t; i++) {
        char a = s[len - t + i], b = tail[i];
        if (a >= 'A' && a <= 'Z')
            a = (char)(a + 32);
        if (b >= 'A' && b <= 'Z')
            b = (char)(b + 32);
        if (a != b)
            return 0;
    }
    return 1;
}

int bo3_package_program(const char *self, const char *self_suffix, const char *other_suffix,
                        char *out, unsigned n)
{
    size_t len, sfx;
    const char *ext = "";
    int w;

    if (!self || !*self || !self_suffix || !other_suffix || !out || !n)
        return 0;
    len = strlen(self);
    if (ends_with_nocase(self, len, ".exe")) {
        ext = self + len - 4;
        len -= 4;
    }
    sfx = strlen(self_suffix);
    if (len <= sfx || !ends_with_nocase(self, len, self_suffix))
        return 0;
    {
        /* the name before the suffix must not be empty: "/x/-pal" is no program of ours */
        char c = self[len - sfx - 1];
        if (c == '/' || c == '\\')
            return 0;
    }
    len -= sfx;
    w = snprintf(out, n, "%.*s%s%s", (int)len, self, other_suffix, ext);
    return w > 0 && (unsigned)w < n;
}
