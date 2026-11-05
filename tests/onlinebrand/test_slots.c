#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "recomp_types.h"
#include "region.h"
#include "onlinebrand.h"

#define T 0x80182000u
#define RAM 0x80180000u
#define TABLE 0x004d532cu
#define PAGE 0x00559390u
_Alignas(4) static uint8_t ram[0x100000], page[0x100];
static uint32_t table_ptr, allocations;
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp;
ptrdiff_t g_xbox_mem_offset = 1;
void *bo3_brand_test_ptr(uint32_t p)
{
    if (p == TABLE) return &table_ptr;
    if (p >= PAGE && p < PAGE + sizeof page) return page + p - PAGE;
    assert(p >= RAM && p < RAM + sizeof ram);
    return ram + p - RAM;
}
void bo3_brand_test_push(uint32_t n) { (void)n; }
void BO3_SUB(sub_001D2879, sub_001D2B99, sub_001D2C39)(void) { g_eax = T + 0x50000u + allocations++ * 0x1000u; }
static uint32_t *word(uint32_t p) { return bo3_brand_test_ptr(p); }
static void str(uint32_t p, const char *s) {
    uint16_t *d = bo3_brand_test_ptr(p);
    while ((*d++ = (uint8_t)*s++)) {}
}
static void eq(uint32_t p, const char *s) {
    uint16_t *d = bo3_brand_test_ptr(p);
    do { assert(*d++ == (uint8_t)*s); } while (*s++);
}
int main(void)
{
    const uint32_t slots = T + 16, a = T + 0x4000, b = T + 0x4040;
    static const unsigned other_slots[] = {4, 5, 6, 7, 10, 17, 18, 138, 144, 158, 184, 3391};
    table_ptr = T; *word(T + 8) = 3500; *word(T + 12) = slots;
    for (unsigned i = 0; i < sizeof other_slots / sizeof other_slots[0]; ++i) {
        uint32_t orig = T + 0x4100 + i * 0x40;
        str(orig, "Xbox Live message");
        *word(slots + other_slots[i] * 4) = orig;
    }
    str(a, "Xbox Live is ready"); str(b, "XBOX LIVE");
    *word(slots + 3 * 4) = a; *word(slots + 143 * 4) = b;
    *word(PAGE + 0x18) = b;
    bo3_onlinebrand_apply();
    assert(allocations == 1 && *word(slots + 3 * 4) != a && *word(slots + 143 * 4) != b);
    eq(*word(slots + 3 * 4), "Takedawn Online is ready");
    eq(*word(slots + 143 * 4), "TAKEDAWN ONLINE");
    eq(a, "Xbox Live is ready"); eq(b, "XBOX LIVE");
    for (unsigned i = 0; i < sizeof other_slots / sizeof other_slots[0]; ++i) {
        uint32_t orig = T + 0x4100 + i * 0x40;
        assert(*word(slots + other_slots[i] * 4) != orig);
        eq(orig, "Xbox Live message");
        eq(*word(slots + other_slots[i] * 4), "Takedawn Online message");
    }
    assert(*word(PAGE + 0x18) == *word(slots + 143 * 4));
    bo3_onlinebrand_apply(); assert(allocations == 1);
    /* A reload can reuse the same table address with new original slots. */
    *word(slots + 3 * 4) = a; *word(slots + 143 * 4) = b;
    for (unsigned i = 0; i < sizeof other_slots / sizeof other_slots[0]; ++i)
        *word(slots + other_slots[i] * 4) = T + 0x4100 + i * 0x40;
    bo3_onlinebrand_apply(); assert(allocations == 2);
    eq(*word(slots + 143 * 4), "TAKEDAWN ONLINE");
    return 0;
}
