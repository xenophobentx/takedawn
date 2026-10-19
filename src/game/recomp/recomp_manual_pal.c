/* The PAL build's wrapped functions: recomp_manual.c's, at the PAL
 * default.xbe's addresses (tools/region/xbemap.py; each the same function,
 * of the same size, as the NTSC-U one it stands for). regen.sh hands this
 * file to the recompiler as --exclude-manual when BO3_REGION=PAL, and
 * src/game/CMakeLists.txt compiles it only then. What each one does is in
 * recomp_manual.c. Each definition names its NTSC-U function on its line:
 * tools/region/check.py pairs them. */
#if defined(BO3_REGION_PAL) && BO3_REGION_PAL
#include <stdint.h>
#include "../soundtrack.h"
#include "../dsound_notify.h"
#include "../soundbank.h"

/* Custom music (BO-108): NTSC-U sub_001D0554, sub_0030211F, sub_001537C0. */
extern void sub_001D0874_gen(void);
extern void sub_0030241F_gen(void);
extern void sub_00153A60_gen(void);

void sub_001D0874(void) { bo3_soundtrack_open_song(sub_001D0874_gen); }   /* NTSC-U sub_001D0554 */
void sub_0030241F(void) { bo3_soundtrack_create_decoder(sub_0030241F_gen); }   /* NTSC-U sub_0030211F */
void sub_00153A60(void) { bo3_soundtrack_start_track(sub_00153A60_gen); }   /* NTSC-U sub_001537C0 */

/* DirectSound's "voice done" (BO-173): NTSC-U sub_002FAAEF. */
extern void sub_002FADEF_gen(void);

void sub_002FADEF(void) { bo3_dsound_voice_done(sub_002FADEF_gen); }   /* NTSC-U sub_002FAAEF */

/* A sound bank that did not load (BO-218): NTSC-U sub_001F65A0. */
extern void sub_001F6890_gen(void);

void sub_001F6890(void) { bo3_wave_dict_count(sub_001F6890_gen); }   /* NTSC-U sub_001F65A0 */
#endif
