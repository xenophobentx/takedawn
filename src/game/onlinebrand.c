/* Replace service references in the loaded title string table. The main
 * pad poll watches for a fully relocated table before menus build. The main
 * menu mark is a fallback and updates any labels already copied by the page.
 * Both run after a table reload. No title UI setter is called. */
#include "onlinebrand.h"
#include "region.h"
#include "recomp_types.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

extern RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp;
extern ptrdiff_t g_xbox_mem_offset;
extern void BO3_SUB(sub_001D2879, sub_001D2B99, sub_001D2C39)(void);
#ifdef BO3_ONLINEBRAND_TEST
extern void *bo3_brand_test_ptr(uint32_t);
extern void bo3_brand_test_push(uint32_t);
#define M32(v) (*(uint32_t *)bo3_brand_test_ptr((uint32_t)(v)))
#define PTR(v) ((uint16_t *)bo3_brand_test_ptr((uint32_t)(v)))
#undef PUSH32
#define PUSH32(sp, v) do { (void)(sp); bo3_brand_test_push((v)); } while (0)
#else
#define M32(v) (*(uint32_t *)((uintptr_t)g_xbox_mem_offset + (uint32_t)(v)))
#define PTR(v) ((uint16_t *)((uintptr_t)g_xbox_mem_offset + (uint32_t)(v)))
#endif
#define TABLE BO3_VA(0x004D532Cu, 0x004D564Cu, 0x004D56ECu)
#define PAGE BO3_VA(0x00559390u, 0x005596B0u, 0x00559750u)
#define MAX_FILE 0x40000u  /* installed language files are below 256 KiB */
#define MAX_ENTRIES 4096u
#define TAG 0x54414B42u

static uint32_t s_table, s_sentinel;

void bo3_onlinebrand_apply(void)
{
    uint32_t offsets[MAX_ENTRIES], sizes[MAX_ENTRIES];
    uint32_t t = M32(TABLE), count, slots, total = 0, changed = 0;
    /* Language files are allocated in the title heap in the 64 MiB RAM alias. */
    if (t < 0x80010000u || t > 0x84000000u - MAX_FILE) return;
    count = M32(t + 8); slots = M32(t + 12);
    if (count <= 143 || count > MAX_ENTRIES || slots != t + 16) return;
    uint32_t sentinel = M32(slots + 143 * 4);
    if (t == s_table && sentinel == s_sentinel) return;
    /* The file is not ready until its offsets have been relocated. */
    if (sentinel - t < 16 + count * 4 || sentinel - t >= MAX_FILE) return;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t p = M32(slots + i * 4), delta = p - t;
        sizes[i] = 0;
        if ((p & 1u) || delta < 16 + count * 4 || delta >= MAX_FILE) continue;
        size_t limit = (MAX_FILE - delta) / 2;
        size_t n = bo3_onlinebrand_replace(PTR(p), limit, NULL, 0);
        if (!n || n > (MAX_FILE - total) / 2) continue;
        offsets[i] = total; sizes[i] = (uint32_t)n;
        total += (uint32_t)n * 2; ++changed;
    }
    if (!changed) { s_table = t; s_sentinel = sentinel; return; }
    uint32_t a = g_eax, c = g_ecx, d = g_edx, sp = g_esp;
    PUSH32(g_esp, TAG); PUSH32(g_esp, total); PUSH32(g_esp, 0);
    BO3_SUB(sub_001D2879, sub_001D2B99, sub_001D2C39)();
    uint32_t block = g_eax;
    g_eax = a; g_ecx = c; g_edx = d; g_esp = sp;
    if (!block) return;
    for (uint32_t i = 0; i < count; ++i) {
        if (!sizes[i]) continue;
        uint32_t old = M32(slots + i * 4), replacement = block + offsets[i];
        size_t limit = (MAX_FILE - (old - t)) / 2;
        if (!bo3_onlinebrand_replace(PTR(old), limit, PTR(replacement), sizes[i])) continue;
        M32(slots + i * 4) = replacement;
        /* The title copied these before mq_items; update the copies before
         * the list widget sees them, including descriptions. */
        for (uint32_t j = 0; j < 5; ++j) {
            if (M32(PAGE + 0x18 + j * 8) == old) M32(PAGE + 0x18 + j * 8) = replacement;
            if (M32(PAGE + 0x40 + j * 4) == old) M32(PAGE + 0x40 + j * 4) = replacement;
        }
    }
    s_table = t; s_sentinel = M32(slots + 143 * 4);
    fprintf(stderr, "[MENU] online branding: replaced %u strings (%u bytes)\n", changed, total);
}
