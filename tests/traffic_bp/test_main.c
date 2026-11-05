/* A traffic car out of the collision broadphase (src/game/traffic_bp.c,
 * BO-346) on a made-up broadphase in a host buffer. The title's
 * sub_00111680 is played here as far as the wrapper's contract goes: it
 * takes out the slot the car's handle names (index from the handle, as the
 * title computes it) and records an index out of range, which is where the
 * title's axis-list search runs away. No game data, no guest code. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "recomp_types.h"
#include "traffic_bp.h"

/* ---- the runtime's registers and memory ---- */
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi, g_ebp, g_seh_ebp;
ptrdiff_t g_xbox_mem_offset;
#define GMEM 0x00400000u
static uint8_t *s_mem;
#define M8(va)  (*(uint8_t *)(s_mem + (uint32_t)(va)))
#define M32(va) (*(uint32_t *)(s_mem + (uint32_t)(va)))

#define WORLD   0x00100000u
#define CARS    0x00200000u
#define CAR(i)  (CARS + (uint32_t)(i) * 0x180u)
#define STACK   0x003FF000u
#define RET     0x001A39B5u
#define SLOT(i) (WORLD + BO3_BP_SLOTS + (uint32_t)(i) * BO3_BP_STRIDE)

static int s_fail;
#define CHECK(c, ...) do { if (!(c)) { s_fail++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
                                       printf(__VA_ARGS__); printf("\n"); } } while (0)

/* ---- the title's sub_00111680, as far as it matters here ---- */
static int s_title_calls, s_title_ran_away;
static uint32_t s_title_handle;

static void title_remove(void)
{
    uint32_t world = g_eax, car = g_ebx, h = M32(car + BO3_BP_CAR_SLOT);
    int32_t index = (int32_t)(h - (world + BO3_BP_SLOTS)) / (int32_t)BO3_BP_STRIDE;
    uint32_t count = M32(world + BO3_BP_COUNT);

    s_title_calls++;
    s_title_handle = h;
    if (index < 0 || (uint32_t)index >= count) {
        s_title_ran_away++;             /* sub_00111BD0 would never find it */
    } else {
        count--;
        M32(world + BO3_BP_COUNT) = count;
        if ((uint32_t)index != count) {
            memcpy(s_mem + SLOT(index), s_mem + SLOT(count), BO3_BP_STRIDE);
            if (M8(SLOT(index)) == BO3_BP_TRAFFIC)
                M32(M32(SLOT(index) + BO3_BP_OWNER) + BO3_BP_CAR_SLOT) = SLOT(index);
        }
    }
    M32(car + BO3_BP_CAR_SLOT) = 0;
    g_esp += 4;                         /* ret */
}

/* ---- a broadphase: n traffic slots, slot i naming car i ---- */
static void world_with(uint32_t n)
{
    memset(s_mem, 0, GMEM);
    for (uint32_t i = 0; i < n; i++) {
        M8(SLOT(i)) = BO3_BP_TRAFFIC;
        M32(SLOT(i) + BO3_BP_OWNER) = CAR(i);
        M32(CAR(i) + BO3_BP_CAR_SLOT) = SLOT(i);
    }
    M32(WORLD + BO3_BP_COUNT) = n;
}

static uint32_t rd32(uint32_t va) { return M32(va); }
static uint8_t rd8(uint32_t va) { return M8(va); }

/* Call the wrapper as sub_001A3970 calls sub_00111680. */
static void remove_car(uint32_t car)
{
    s_title_calls = s_title_ran_away = 0;
    s_title_handle = 0xDEADBEEFu;
    g_esp = STACK;
    PUSH32(g_esp, RET);
    g_eax = WORLD;
    g_ebx = car;
    g_esi = 0x51515151u;
    g_edi = 0xD1D1D1D1u;
    g_ebp = 0xB9B9B9B9u;
    bo3_traffic_bp_remove(title_remove);
}

static void regs_kept(const char *what)
{
    CHECK(g_esp == STACK, "%s: esp %08X, want %08X (the ret)", what, g_esp, STACK);
    CHECK(g_esi == 0x51515151u && g_edi == 0xD1D1D1D1u && g_ebp == 0xB9B9B9B9u,
          "%s: callee-saved registers changed", what);
}

int main(void)
{
    s_mem = calloc(1, GMEM);
    if (!s_mem)
        return 2;
    g_xbox_mem_offset = (ptrdiff_t)(uintptr_t)s_mem;

    /* 1. A car with its own slot goes through the title's code as it is. */
    world_with(5);
    CHECK(bo3_bp_slot_of(rd32, rd8, WORLD, CAR(2)) == SLOT(2), "own slot not taken");
    remove_car(CAR(2));
    CHECK(s_title_calls == 1 && s_title_handle == SLOT(2) && !s_title_ran_away,
          "valid handle: title calls %d, handle %08X", s_title_calls, s_title_handle);
    CHECK(M32(WORLD + BO3_BP_COUNT) == 4, "valid handle: count %u", M32(WORLD + BO3_BP_COUNT));
    CHECK(M32(SLOT(2) + BO3_BP_OWNER) == CAR(4) && M32(CAR(4) + BO3_BP_CAR_SLOT) == SLOT(2),
          "valid handle: the last slot did not move into the hole");
    regs_kept("valid handle");

    /* 2. The int037 crash: handle 0, the car in no slot. Before: the index
     *    is -137490 for the real world, the search runs to the end of RAM. */
    world_with(5);
    M32(CAR(7) + BO3_BP_CAR_SLOT) = 0;
    CHECK(bo3_bp_slot_of(rd32, rd8, WORLD, CAR(7)) == 0, "no slot: found one");
    remove_car(CAR(7));
    CHECK(s_title_calls == 0, "no slot: the title's code ran (it would run away)");
    CHECK(M32(WORLD + BO3_BP_COUNT) == 5, "no slot: count %u", M32(WORLD + BO3_BP_COUNT));
    CHECK(M32(CAR(7) + BO3_BP_CAR_SLOT) == 0, "no slot: handle not 0");
    for (uint32_t i = 0; i < 5; i++)
        CHECK(M32(SLOT(i) + BO3_BP_OWNER) == CAR(i) && M32(CAR(i) + BO3_BP_CAR_SLOT) == SLOT(i),
              "no slot: slot %u touched", i);
    regs_kept("no slot");

    /* 3. Handle 0 but the car still has a slot: that slot goes. */
    world_with(5);
    M32(CAR(3) + BO3_BP_CAR_SLOT) = 0;
    remove_car(CAR(3));
    CHECK(s_title_calls == 1 && s_title_handle == SLOT(3) && !s_title_ran_away,
          "handle 0 with a slot: title calls %d, handle %08X", s_title_calls, s_title_handle);
    CHECK(M32(WORLD + BO3_BP_COUNT) == 4, "handle 0 with a slot: count %u", M32(WORLD + BO3_BP_COUNT));

    /* 4. A stale handle naming another car's slot: the car's own goes, the
     *    other car keeps its slot (the title would take out the other's). */
    world_with(5);
    M32(CAR(1) + BO3_BP_CAR_SLOT) = SLOT(0);
    remove_car(CAR(1));
    CHECK(s_title_handle == SLOT(1), "stale handle: took %08X, want %08X", s_title_handle, SLOT(1));
    CHECK(M32(SLOT(0) + BO3_BP_OWNER) == CAR(0) && M32(CAR(0) + BO3_BP_CAR_SLOT) == SLOT(0),
          "stale handle: the other car lost its slot");

    /* 5. A handle at the slot past the last (index == count). */
    world_with(5);
    M32(CAR(9) + BO3_BP_CAR_SLOT) = SLOT(5);
    M8(SLOT(5)) = BO3_BP_TRAFFIC;
    M32(SLOT(5) + BO3_BP_OWNER) = CAR(9);  /* a stale slot past the count */
    remove_car(CAR(9));
    CHECK(s_title_calls == 0 && M32(WORLD + BO3_BP_COUNT) == 5, "past the count: removed");

    /* 6. A handle inside the slots but not on a slot boundary. */
    world_with(5);
    M32(CAR(4) + BO3_BP_CAR_SLOT) = SLOT(4) + 4;
    remove_car(CAR(4));
    CHECK(s_title_handle == SLOT(4) && !s_title_ran_away, "misaligned: took %08X", s_title_handle);

    /* 7. A slot no longer a traffic car's (the hit path makes it a physics
     *    object, type 4) is not the car's. */
    world_with(5);
    M8(SLOT(2)) = 4;
    remove_car(CAR(2));
    CHECK(s_title_calls == 0 && M32(WORLD + BO3_BP_COUNT) == 5, "converted slot: removed");

    /* 8. A count no world has: nothing is taken out. */
    world_with(5);
    M32(WORLD + BO3_BP_COUNT) = BO3_BP_MAX + 1;
    remove_car(CAR(2));
    CHECK(s_title_calls == 0, "bad count: removed");

    /* 9. The last slot, and a world of one. */
    world_with(5);
    remove_car(CAR(4));
    CHECK(s_title_handle == SLOT(4) && M32(WORLD + BO3_BP_COUNT) == 4, "last slot");
    world_with(1);
    remove_car(CAR(0));
    CHECK(s_title_handle == SLOT(0) && M32(WORLD + BO3_BP_COUNT) == 0, "one slot");
    world_with(0);
    M32(CAR(0) + BO3_BP_CAR_SLOT) = SLOT(0);
    remove_car(CAR(0));
    CHECK(s_title_calls == 0, "empty world: removed");

    free(s_mem);
    if (s_fail) {
        printf("%d failed\n", s_fail);
        return 1;
    }
    printf("traffic_bp: all passed\n");
    return 0;
}
