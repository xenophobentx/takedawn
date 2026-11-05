/*
 * traffic_bp.c - a traffic car taken out of the collision broadphase by a
 * handle that is not its slot (BO-346).
 *
 * sub_00111680 (eax = the broadphase world, ebx = the car; its one caller is
 * the traffic car release, sub_001A3970) takes the car's slot out: index =
 * (car + 0x16C - (world + 0x70)) / 0x30, the count down by one, then
 * sub_00111BD0 takes that index out of the three sweep-and-prune axis lists
 * (world + 0xE5F4, 0x4C80 apart, 8-byte entries, the object index a signed
 * 16-bit word at +0). The search for the index there has no end test: it
 * stops at the entry that holds it. The last slot then moves into the hole
 * and its car's handle follows; the car's own handle becomes 0.
 *
 * sub_001A3970 asks for this when the car's flags (+0x174, +0x173) say it is
 * in the broadphase, not by its handle. int037 soak, Mac2 (07:52:40), a
 * client in a six-player online race: a car at the end of its route
 * (sub_001A6B40 -> sub_001A75A0 -> sub_001A3970) had handle 0. The index
 * came out as (0 - 0x64B380) / 0x30 = -137490 = 0xFFFDE6EE, which no 16-bit
 * entry can hold, so the search read on through the end of RAM and its 28
 * mirrors (the port's layout) to guest 0x74000004, host 0x374000004, and
 * faulted. The console has no mirrors (xemu: hw/xbox/xbox.c,
 * xbox_memory_init, one RAM region at physical 0) and its title pages end
 * where the title's allocations do: it faults sooner, at the first unmapped
 * page past the lists. The same state crashes the original.
 *
 * So the slot is found before the title's code runs (traffic_bp.h): the
 * handle when it is a traffic slot in use that names this car, else the
 * slot that does; with none, the car has nothing in the broadphase, its
 * handle is set to 0 as the title would, and the title's code is skipped.
 * The race goes on; the log says so.
 */
#include "traffic_bp.h"

#include <stdio.h>

#include "recomp_types.h"

static uint32_t rd32(uint32_t va) { return MEM32(va); }
static uint8_t rd8(uint32_t va) { return MEM8(va); }

void bo3_traffic_bp_remove(void (*title_fn)(void))
{
    uint32_t world = g_eax, car = g_ebx;
    uint32_t handle = MEM32(car + BO3_BP_CAR_SLOT);
    uint32_t slot = bo3_bp_slot_of(rd32, rd8, world, car);
    static int said;

    if (slot && slot == handle) {
        title_fn();
        return;
    }
    if (said < 8) {
        said++;
        fprintf(stderr, "[TRAFFIC] car 0x%08X leaves with broadphase handle 0x%08X, not its slot"
                        " (%u slots; flags %02X, type %u, links %08X/%08X, route %08X): %s\n",
                (unsigned)car, (unsigned)handle, (unsigned)MEM32(world + BO3_BP_COUNT),
                (unsigned)MEM8(car + 0x174), (unsigned)MEM8(car + 0x173),
                (unsigned)MEM32(car + 0x10C), (unsigned)MEM32(car + 0x110),
                (unsigned)MEM32(car + 0x114),
                slot ? "its own slot taken out instead" : "it has none, nothing taken out");
    }
    if (slot) {
        MEM32(car + BO3_BP_CAR_SLOT) = slot;
        title_fn();
        return;
    }
    /* What the title's body leaves for this car, without the removal:
     * the handle 0, and its ret. */
    MEM32(car + BO3_BP_CAR_SLOT) = 0;
    g_esp += 4;
}
