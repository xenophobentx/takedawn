/* region.h - the build's disc: North America (NTSC-U, the default), Europe
 * (PAL, the Fr/De/It disc, -DBO3_REGION=PAL) or Europe/Australia (the
 * En/Es/Nl/Sv disc, -DBO3_REGION=PAL-ENSV).
 *
 * The three default.xbe are one program built three times: the PAL code
 * sits a few hundred bytes further on, its data a little more, and the two
 * PAL discs differ again by a little (docs/pal.md). Every guest address the
 * port's own C names is written once with all three values,
 *
 *     #define CTL_PADS  BO3_VA(0x004AE20Cu, 0x004AE52Cu, 0x004AE5CCu)
 *     BO3_SUB(sub_001D2879, sub_001D2B99, sub_001D2C39)();
 *
 * the NTSC-U one first (the one the notes and comments speak of), then the
 * Fr/De/It one, then the En/Es/Nl/Sv one, and the build picks one; all are
 * constants, so they stay usable in static initialisers. The PAL values
 * come from tools/region/xbemap.py (the executables aligned, each site
 * checked), as do config/pal/ and config/pal-ensv/.
 *
 * BO3_REGION_PAL is set for both PAL programs (they are one package: its
 * name, folders and the PAL-only behaviour); BO3_REGION_PAL_ENSV picks the
 * second disc's addresses. */
#ifndef BO3_REGION_H
#define BO3_REGION_H

#if defined(BO3_REGION_PAL_ENSV) && BO3_REGION_PAL_ENSV
#define BO3_VA(ntsc, pal, ensv)  (ensv)
#define BO3_SUB(ntsc, pal, ensv) ensv
#define BO3_REGION_NAME          "PAL-ENSV"
#elif defined(BO3_REGION_PAL) && BO3_REGION_PAL
#define BO3_VA(ntsc, pal, ensv)  (pal)
#define BO3_SUB(ntsc, pal, ensv) pal
#define BO3_REGION_NAME          "PAL"
#else
#define BO3_VA(ntsc, pal, ensv)  (ntsc)
#define BO3_SUB(ntsc, pal, ensv) ntsc
#define BO3_REGION_NAME          "NTSC-U"
#endif

#endif
