#include "onlinebrand.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <string.h>
#include <stdio.h>
static void test(const char *in, const char *expected)
{
    uint16_t src[256], dst[512]; size_t n = strlen(in), need;
    for (size_t i = 0; i <= n; ++i) src[i] = (unsigned char)in[i];
    for (size_t i = 0; i < 512; ++i) dst[i] = 0x1234;
    need = bo3_onlinebrand_replace(src, n+1, NULL, 0);
    if (!expected) { assert(!need); return; }
    assert(need == strlen(expected)+1);
    assert(bo3_onlinebrand_replace(src, n+1, dst, need-1) == need);
    assert(dst[0] == 0x1234);
    assert(bo3_onlinebrand_replace(src, n+1, dst, need) == need);
    for (size_t i = 0; i < need; ++i) assert(dst[i] == (unsigned char)expected[i]);
    assert(dst[need] == 0x1234);
    assert(bo3_onlinebrand_replace(src, n, dst, 512) == 0);
}
int main(void)
{
    test("XBOX LIVE", "TAKEDAWN ONLINE");
    test("Sign in to Xbox Live to join.", "Sign in to Takedawn Online to join.");
    test("Xbox Live / Xbox Live!", "Takedawn Online / Takedawn Online!");
    test("No service here", NULL);
    test("RETRIEVING LIST OF GAMES FROM EA", "SEARCHING FOR A GAME...");
    test("CONNECTING TO EA", "CONNECTING TO TAKEDAWN ONLINE");
    test("QUICK MATCH", "QUICK PLAY");
    test("Are you sure you want to Sign out? All unread messages will be lost.", "Are you sure you want to sign out?");
    test("RETRIEVING LIST OF GAMES FROM EAX", NULL);        /* exact strings only */
    test("EA GAMES TRAX", NULL);
    {
        const uint16_t src[] = { 'C','O','N','N','E','C','T','I','N','G',' ','T','O',' ','E','A',0x2026,0 };
        uint16_t dst[40];
        size_t need = bo3_onlinebrand_replace(src, 18, dst, 40);
        assert(need == 31 && dst[29] == 0x2026 && dst[30] == 0);
    }
    {
        const uint16_t src[] = { 0x00e9, ' ', 'X', 'b', 'o', 'x', ' ', 'L', 'i', 'v', 'e', 0x2026, 0 };
        uint16_t dst[32];
        assert(bo3_onlinebrand_replace(src, 13, dst, 32) == 19);
        assert(dst[0] == 0x00e9 && dst[17] == 0x2026 && dst[18] == 0);
    }
    puts("online branding tests passed");
    return 0;
}
