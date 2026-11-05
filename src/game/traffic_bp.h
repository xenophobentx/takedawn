/*
 * traffic_bp.h - a traffic car taken out of the collision broadphase by a
 * handle that is not its slot (BO-346, traffic_bp.c). The lookup is apart
 * from the title's memory so a unit test can run it (tests/traffic_bp).
 *
 * The broadphase (NTSC-U world 0x64B310) keeps one 0x30-byte slot per
 * object from world + 0x70, the count at world + 0x1CB70. A traffic slot is
 * type 3 (slot + 0) and names its car at slot + 0xC; the car keeps its slot
 * at car + 0x16C (sub_00111620 sets both).
 */
#ifndef BO3_TRAFFIC_BP_H
#define BO3_TRAFFIC_BP_H

#include <stdint.h>

#define BO3_BP_SLOTS     0x70u      /* first slot, from the world */
#define BO3_BP_COUNT     0x1CB70u   /* slot count, from the world (region: not an address) */
#define BO3_BP_STRIDE    0x30u
#define BO3_BP_OWNER     0x0Cu      /* the slot's object */
#define BO3_BP_TRAFFIC   3u         /* slot type of a traffic car */
#define BO3_BP_CAR_SLOT  0x16Cu     /* the car's slot */
#define BO3_BP_MAX       0x4C8u     /* room for slots below the sweep lists at world + 0xE5F4 */

typedef uint32_t (*bo3_bp_rd32)(uint32_t va);
typedef uint8_t (*bo3_bp_rd8)(uint32_t va);

/* The slot sub_00111680 may take car out by: its handle when that is a slot
 * in use, of a traffic car, naming this car; else the first such slot that
 * names it; 0 when the broadphase has none (nothing to take out). */
static inline uint32_t bo3_bp_slot_of(bo3_bp_rd32 rd32, bo3_bp_rd8 rd8,
                                      uint32_t world, uint32_t car)
{
    uint32_t first = world + BO3_BP_SLOTS;
    uint32_t count = rd32(world + BO3_BP_COUNT);
    uint32_t h = rd32(car + BO3_BP_CAR_SLOT), i, s;

    if (count > BO3_BP_MAX)
        return 0;                   /* no world we know; leave it alone */
    if (h >= first && h < first + count * BO3_BP_STRIDE
        && (h - first) % BO3_BP_STRIDE == 0
        && rd8(h) == BO3_BP_TRAFFIC && rd32(h + BO3_BP_OWNER) == car)
        return h;
    for (i = 0, s = first; i < count; i++, s += BO3_BP_STRIDE)
        if (rd8(s) == BO3_BP_TRAFFIC && rd32(s + BO3_BP_OWNER) == car)
            return s;
    return 0;
}

/* sub_00111680 (wrapped in recomp/recomp_manual.c), a traffic car out of
 * the broadphase: by the slot bo3_bp_slot_of finds, or not at all. */
void bo3_traffic_bp_remove(void (*title_fn)(void));

#endif
