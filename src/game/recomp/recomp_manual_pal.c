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
#include "../traffic_bp.h"
#include "../hirefresh.h"

/* Custom music (BO-108, BO-230): NTSC-U sub_001D0554, sub_0030211F,
 * sub_001537C0, sub_00155420. */
extern void sub_001D0874_gen(void);
extern void sub_0030241F_gen(void);
extern void sub_00153A60_gen(void);
extern void sub_001556C0_gen(void);

void sub_001D0874(void) { bo3_soundtrack_open_song(sub_001D0874_gen); }   /* NTSC-U sub_001D0554 */
void sub_0030241F(void) { bo3_soundtrack_create_decoder(sub_0030241F_gen); }   /* NTSC-U sub_0030211F */
void sub_00153A60(void) { bo3_soundtrack_start_track(sub_00153A60_gen); }   /* NTSC-U sub_001537C0 */
void sub_001556C0(void) { bo3_soundtrack_start_source(sub_001556C0_gen); }   /* NTSC-U sub_00155420 */

/* DirectSound's "voice done" (BO-173): NTSC-U sub_002FAAEF. */
extern void sub_002FADEF_gen(void);

void sub_002FADEF(void) { bo3_dsound_voice_done(sub_002FADEF_gen); }   /* NTSC-U sub_002FAAEF */

/* A sound bank that did not load (BO-218): NTSC-U sub_001F65A0. */
extern void sub_001F6890_gen(void);

void sub_001F6890(void) { bo3_wave_dict_count(sub_001F6890_gen); }   /* NTSC-U sub_001F65A0 */

/* A traffic car out of the collision broadphase (BO-346): NTSC-U sub_00111680. */
extern void sub_001119E0_gen(void);

void sub_001119E0(void) { bo3_traffic_bp_remove(sub_001119E0_gen); }   /* NTSC-U sub_00111680 */

/* Pictures between the simulation's steps (../hirefresh.c). Each PAL
 * function was matched by the data it reads and the functions it calls:
 * the same as the NTSC-U one's, at their PAL addresses. */
extern void sub_000166B0_gen(void);
extern void sub_001E1900_gen(void);
extern void sub_001DD5A0_gen(void);
extern void sub_00030590_gen(void);
extern void sub_00053790_gen(void);
extern void sub_003510B0_gen(void);
extern void sub_00167BE0_gen(void);
extern void sub_000383C0_gen(void);
extern void sub_00036980_gen(void);
extern void sub_0013F1E0_gen(void);
extern void sub_00188A50_gen(void);

void sub_000166B0(void) { bo3_update(sub_000166B0_gen); }   /* NTSC-U sub_000165F0 */
void sub_001E1900(void) { bo3_frame_sync_dirty(sub_001E1900_gen); }   /* NTSC-U sub_001E15E0 */
void sub_001DD5A0(void) { bo3_frame_get_ltm(sub_001DD5A0_gen); }   /* NTSC-U sub_001DD280 */
void sub_00030590(void) { bo3_draw_vehicle(sub_00030590_gen); }   /* NTSC-U sub_000303D0 */
void sub_00053790(void) { bo3_hud_update(sub_00053790_gen); }   /* NTSC-U sub_000535E0 */
void sub_003510B0(void) { bo3_swap(sub_003510B0_gen); }   /* NTSC-U sub_00350DB0 */
void sub_00167BE0(void) { bo3_player_frame(sub_00167BE0_gen); }   /* NTSC-U sub_00167940 */
void sub_000383C0(void) { bo3_spark_history(sub_000383C0_gen); }   /* NTSC-U sub_00038200 */
void sub_00036980(void) { bo3_spark_draw(sub_00036980_gen); }   /* NTSC-U sub_000367C0 */
void sub_0013F1E0(void) { bo3_race_audio(sub_0013F1E0_gen); }   /* NTSC-U sub_0013EF40 */
void sub_00188A50(void) { bo3_fx_clock(sub_00188A50_gen); }   /* NTSC-U sub_00188750 */

/* Online play (BO-134, ../online.c): the XNet and XOnline entry points the
 * title links (its XDK library code, which moved as one block: each PAL
 * function reads the same bytes as its NTSC-U one, tools/region/xbemap.py),
 * and the lobby screen's start (0xA05B0) and start-lock check (0xFD660). */
#include "../online.h"

extern void sub_0020FCBE_gen(void);
extern void sub_00339A22_gen(void);
extern void sub_00339A39_gen(void);
extern void sub_00339B27_gen(void);
extern void sub_0033962E_gen(void);
extern void sub_003395A9_gen(void);
extern void sub_003395B4_gen(void);
extern void sub_003395BF_gen(void);
extern void sub_003395D6_gen(void);
extern void sub_003395ED_gen(void);
extern void sub_003395F8_gen(void);
extern void sub_00339607_gen(void);
extern void sub_00339811_gen(void);
extern void sub_00339623_gen(void);
extern void sub_003397FB_gen(void);
extern void sub_00339806_gen(void);
extern void sub_00339639_gen(void);
extern void sub_00339644_gen(void);
extern void sub_0033964F_gen(void);
extern void sub_0033965A_gen(void);
extern void sub_00339665_gen(void);
extern void sub_00339674_gen(void);
extern void sub_0033967F_gen(void);
extern void sub_0033968A_gen(void);
extern void sub_00339695_gen(void);
extern void sub_003396A0_gen(void);
extern void sub_003396AB_gen(void);
extern void sub_003396D4_gen(void);
extern void sub_003396EE_gen(void);
extern void sub_003396FD_gen(void);
extern void sub_00339717_gen(void);
extern void sub_003396DF_gen(void);
extern void sub_00339708_gen(void);
extern void sub_003396BA_gen(void);
extern void sub_003396C9_gen(void);
extern void sub_00339726_gen(void);
extern void sub_0031B283_gen(void);
extern void sub_0031B380_gen(void);
extern void sub_0031AD96_gen(void);
extern void sub_0031ADA1_gen(void);
extern void sub_0031AD80_gen(void);
extern void sub_0031AD8B_gen(void);
extern void sub_0031ADB0_gen(void);
extern void sub_0031ADBB_gen(void);
extern void sub_0031ADC6_gen(void);
extern void sub_0031AE0B_gen(void);
extern void sub_0031ADD1_gen(void);
extern void sub_0031ADDC_gen(void);
extern void sub_0031ADE7_gen(void);
extern void sub_0031AE16_gen(void);
extern void sub_0031AE43_gen(void);
extern void sub_0031AE4E_gen(void);
extern void sub_0031AE59_gen(void);
extern void sub_0031AE64_gen(void);
extern void sub_0031AE6F_gen(void);
extern void sub_0031AE7A_gen(void);
extern void sub_0031AE85_gen(void);
extern void sub_0031AE90_gen(void);
extern void sub_0031AEB1_gen(void);
extern void sub_0031AED2_gen(void);
extern void sub_0031AEDD_gen(void);
extern void sub_0031AEE8_gen(void);
extern void sub_0031AEF3_gen(void);
extern void sub_0031AEFE_gen(void);
extern void sub_0031AF0D_gen(void);

void sub_0020FCBE(void) { bo3_online_stub(0x0020FCBEu, sub_0020FCBE_gen); }   /* NTSC-U sub_0020F9AE */
void sub_00339A22(void) { bo3_online_stub(0x00339A22u, sub_00339A22_gen); }   /* NTSC-U sub_00339722 */
void sub_00339A39(void) { bo3_online_stub(0x00339A39u, sub_00339A39_gen); }   /* NTSC-U sub_00339739 */
void sub_00339B27(void) { bo3_online_stub(0x00339B27u, sub_00339B27_gen); }   /* NTSC-U sub_00339827 */
void sub_0033962E(void) { bo3_online_stub(0x0033962Eu, sub_0033962E_gen); }   /* NTSC-U sub_0033932E */
void sub_003395A9(void) { bo3_online_stub(0x003395A9u, sub_003395A9_gen); }   /* NTSC-U sub_003392A9 */
void sub_003395B4(void) { bo3_online_stub(0x003395B4u, sub_003395B4_gen); }   /* NTSC-U sub_003392B4 */
void sub_003395BF(void) { bo3_online_stub(0x003395BFu, sub_003395BF_gen); }   /* NTSC-U sub_003392BF */
void sub_003395D6(void) { bo3_online_stub(0x003395D6u, sub_003395D6_gen); }   /* NTSC-U sub_003392D6 */
void sub_003395ED(void) { bo3_online_stub(0x003395EDu, sub_003395ED_gen); }   /* NTSC-U sub_003392ED */
void sub_003395F8(void) { bo3_online_stub(0x003395F8u, sub_003395F8_gen); }   /* NTSC-U sub_003392F8 */
void sub_00339607(void) { bo3_online_stub(0x00339607u, sub_00339607_gen); }   /* NTSC-U sub_00339307 */
void sub_00339811(void) { bo3_online_stub(0x00339811u, sub_00339811_gen); }   /* NTSC-U sub_00339511 */
void sub_00339623(void) { bo3_online_stub(0x00339623u, sub_00339623_gen); }   /* NTSC-U sub_00339323 */
void sub_003397FB(void) { bo3_online_stub(0x003397FBu, sub_003397FB_gen); }   /* NTSC-U sub_003394FB */
void sub_00339806(void) { bo3_online_stub(0x00339806u, sub_00339806_gen); }   /* NTSC-U sub_00339506 */
void sub_00339639(void) { bo3_online_stub(0x00339639u, sub_00339639_gen); }   /* NTSC-U sub_00339339 */
void sub_00339644(void) { bo3_online_stub(0x00339644u, sub_00339644_gen); }   /* NTSC-U sub_00339344 */
void sub_0033964F(void) { bo3_online_stub(0x0033964Fu, sub_0033964F_gen); }   /* NTSC-U sub_0033934F */
void sub_0033965A(void) { bo3_online_stub(0x0033965Au, sub_0033965A_gen); }   /* NTSC-U sub_0033935A */
void sub_00339665(void) { bo3_online_stub(0x00339665u, sub_00339665_gen); }   /* NTSC-U sub_00339365 */
void sub_00339674(void) { bo3_online_stub(0x00339674u, sub_00339674_gen); }   /* NTSC-U sub_00339374 */
void sub_0033967F(void) { bo3_online_stub(0x0033967Fu, sub_0033967F_gen); }   /* NTSC-U sub_0033937F */
void sub_0033968A(void) { bo3_online_stub(0x0033968Au, sub_0033968A_gen); }   /* NTSC-U sub_0033938A */
void sub_00339695(void) { bo3_online_stub(0x00339695u, sub_00339695_gen); }   /* NTSC-U sub_00339395 */
void sub_003396A0(void) { bo3_online_stub(0x003396A0u, sub_003396A0_gen); }   /* NTSC-U sub_003393A0 */
void sub_003396AB(void) { bo3_online_stub(0x003396ABu, sub_003396AB_gen); }   /* NTSC-U sub_003393AB */
void sub_003396D4(void) { bo3_online_stub(0x003396D4u, sub_003396D4_gen); }   /* NTSC-U sub_003393D4 */
void sub_003396EE(void) { bo3_online_stub(0x003396EEu, sub_003396EE_gen); }   /* NTSC-U sub_003393EE */
void sub_003396FD(void) { bo3_online_stub(0x003396FDu, sub_003396FD_gen); }   /* NTSC-U sub_003393FD */
void sub_00339717(void) { bo3_online_stub(0x00339717u, sub_00339717_gen); }   /* NTSC-U sub_00339417 */
void sub_003396DF(void) { bo3_online_stub(0x003396DFu, sub_003396DF_gen); }   /* NTSC-U sub_003393DF */
void sub_00339708(void) { bo3_online_stub(0x00339708u, sub_00339708_gen); }   /* NTSC-U sub_00339408 */
void sub_003396BA(void) { bo3_online_stub(0x003396BAu, sub_003396BA_gen); }   /* NTSC-U sub_003393BA */
void sub_003396C9(void) { bo3_online_stub(0x003396C9u, sub_003396C9_gen); }   /* NTSC-U sub_003393C9 */
void sub_00339726(void) { bo3_online_stub(0x00339726u, sub_00339726_gen); }   /* NTSC-U sub_00339426 */
void sub_0031B283(void) { bo3_online_stub(0x0031B283u, sub_0031B283_gen); }   /* NTSC-U sub_0031AF83 */
void sub_0031B380(void) { bo3_online_stub(0x0031B380u, sub_0031B380_gen); }   /* NTSC-U sub_0031B080 */
void sub_0031AD96(void) { bo3_online_stub(0x0031AD96u, sub_0031AD96_gen); }   /* NTSC-U sub_0031AA96 */
void sub_0031ADA1(void) { bo3_online_stub(0x0031ADA1u, sub_0031ADA1_gen); }   /* NTSC-U sub_0031AAA1 */
void sub_0031AD80(void) { bo3_online_stub(0x0031AD80u, sub_0031AD80_gen); }   /* NTSC-U sub_0031AA80 */
void sub_0031AD8B(void) { bo3_online_stub(0x0031AD8Bu, sub_0031AD8B_gen); }   /* NTSC-U sub_0031AA8B */
void sub_0031ADB0(void) { bo3_online_stub(0x0031ADB0u, sub_0031ADB0_gen); }   /* NTSC-U sub_0031AAB0 */
void sub_0031ADBB(void) { bo3_online_stub(0x0031ADBBu, sub_0031ADBB_gen); }   /* NTSC-U sub_0031AABB */
void sub_0031ADC6(void) { bo3_online_stub(0x0031ADC6u, sub_0031ADC6_gen); }   /* NTSC-U sub_0031AAC6 */
void sub_0031AE0B(void) { bo3_online_stub(0x0031AE0Bu, sub_0031AE0B_gen); }   /* NTSC-U sub_0031AB0B */
void sub_0031ADD1(void) { bo3_online_stub(0x0031ADD1u, sub_0031ADD1_gen); }   /* NTSC-U sub_0031AAD1 */
void sub_0031ADDC(void) { bo3_online_stub(0x0031ADDCu, sub_0031ADDC_gen); }   /* NTSC-U sub_0031AADC */
void sub_0031ADE7(void) { bo3_online_stub(0x0031ADE7u, sub_0031ADE7_gen); }   /* NTSC-U sub_0031AAE7 */
void sub_0031AE16(void) { bo3_online_stub(0x0031AE16u, sub_0031AE16_gen); }   /* NTSC-U sub_0031AB16 */
void sub_0031AE43(void) { bo3_online_stub(0x0031AE43u, sub_0031AE43_gen); }   /* NTSC-U sub_0031AB43 */
void sub_0031AE4E(void) { bo3_online_stub(0x0031AE4Eu, sub_0031AE4E_gen); }   /* NTSC-U sub_0031AB4E */
void sub_0031AE59(void) { bo3_online_stub(0x0031AE59u, sub_0031AE59_gen); }   /* NTSC-U sub_0031AB59 */
void sub_0031AE64(void) { bo3_online_stub(0x0031AE64u, sub_0031AE64_gen); }   /* NTSC-U sub_0031AB64 */
void sub_0031AE6F(void) { bo3_online_stub(0x0031AE6Fu, sub_0031AE6F_gen); }   /* NTSC-U sub_0031AB6F */
void sub_0031AE7A(void) { bo3_online_stub(0x0031AE7Au, sub_0031AE7A_gen); }   /* NTSC-U sub_0031AB7A */
void sub_0031AE85(void) { bo3_online_stub(0x0031AE85u, sub_0031AE85_gen); }   /* NTSC-U sub_0031AB85 */
void sub_0031AE90(void) { bo3_online_stub(0x0031AE90u, sub_0031AE90_gen); }   /* NTSC-U sub_0031AB90 */
void sub_0031AEB1(void) { bo3_online_stub(0x0031AEB1u, sub_0031AEB1_gen); }   /* NTSC-U sub_0031ABB1 */
void sub_0031AED2(void) { bo3_online_stub(0x0031AED2u, sub_0031AED2_gen); }   /* NTSC-U sub_0031ABD2 */
void sub_0031AEDD(void) { bo3_online_stub(0x0031AEDDu, sub_0031AEDD_gen); }   /* NTSC-U sub_0031ABDD */
void sub_0031AEE8(void) { bo3_online_stub(0x0031AEE8u, sub_0031AEE8_gen); }   /* NTSC-U sub_0031ABE8 */
void sub_0031AEF3(void) { bo3_online_stub(0x0031AEF3u, sub_0031AEF3_gen); }   /* NTSC-U sub_0031ABF3 */
void sub_0031AEFE(void) { bo3_online_stub(0x0031AEFEu, sub_0031AEFE_gen); }   /* NTSC-U sub_0031ABFE */
void sub_0031AF0D(void) { bo3_online_stub(0x0031AF0Du, sub_0031AF0D_gen); }   /* NTSC-U sub_0031AC0D */

extern void sub_000A07B0_gen(void);
extern void sub_000FD9C0_gen(void);
void sub_000A07B0(void) { bo3_online_lobby_screen(sub_000A07B0_gen); }   /* NTSC-U sub_000A05B0 */
void sub_000FD9C0(void) { bo3_online_fd660(sub_000FD9C0_gen); }   /* NTSC-U sub_000FD660 */

/* High refresh (../hirefresh.c): the audio world (BO-297) and the car's
 * reflection cube (BO-282.1). */
extern void sub_001CA850_gen(void);
extern void sub_0003F970_gen(void);
void sub_001CA850(void) { bo3_audio_world(sub_001CA850_gen); }   /* NTSC-U sub_001CA530 */
void sub_0003F970(void) { bo3_reflection_cube(sub_0003F970_gen); }   /* NTSC-U sub_0003FA20 */

/* The front end's picture callbacks (BO-315, display.pictures in ../main.c):
 * the PAL function's own address is passed, as main.c compares it. */
extern void sub_001C1D80_gen(void);
extern void sub_001C5CC0_gen(void);
extern void bo3_fe_picture_draw(void (*fn)(void), uint32_t va);
void sub_001C1D80(void) { bo3_fe_picture_draw(sub_001C1D80_gen, 0x001C1D80u); }   /* NTSC-U sub_001C1A60 */
void sub_001C5CC0(void) { bo3_fe_picture_draw(sub_001C5CC0_gen, 0x001C5CC0u); }   /* NTSC-U sub_001C59A0 */

/* The Crash Nav globe (BO-385): NTSC-U sub_000F31E0. */
extern void sub_000F3540_gen(void);
extern void bo3_globe_draw(void (*fn)(void));
void sub_000F3540(void) { bo3_globe_draw(sub_000F3540_gen); }   /* NTSC-U sub_000F31E0 */

/* QUICK PLAY (src/game/quickplay.c, Takedawn Online step 3): the create
 * state's name page and mode page builders. The generated bodies are the _gen
 * names; they are all that runs unless QUICK PLAY's create branch is on. */
#include "../quickplay.h"
#include "../vote.h"
extern void sub_000981A0_gen(void);
extern void sub_00099740_gen(void);
void sub_000981A0(void) { bo3_quickplay_name_page(sub_000981A0_gen); }   /* NTSC-U sub_00098050 */
void sub_00099740(void) { bo3_quickplay_mode_page(sub_00099740_gen); }   /* NTSC-U sub_000995F0 */
extern void sub_000CC140_gen(void);
extern void sub_000CD200_gen(void);
extern void sub_000A0DF0_gen(void);
void sub_000CC140(void) { bo3_vote_box_open(sub_000CC140_gen); }   /* NTSC-U sub_000CBE40 */
void sub_000CD200(void) { bo3_vote_row(sub_000CD200_gen); }   /* NTSC-U sub_000CCF00 */
void sub_000A0DF0(void) { bo3_vote_input(sub_000A0DF0_gen); }   /* NTSC-U sub_000A0BF0 */
extern void sub_000A1760_gen(void);
void sub_000A1760(void) { bo3_vote_start_check(sub_000A1760_gen); }   /* NTSC-U sub_000A1560 */

/* After a raced online game (src/game/stay.c, Takedawn Online step 4). */
#include "../stay.h"
extern void sub_0006F080_gen(void);
extern void sub_0006DFF0_gen(void);
extern void sub_000A9DE0_gen(void);
void sub_0006F080(void) { bo3_stay_room_state(sub_0006F080_gen); }     /* NTSC-U sub_0006EEE0 */
void sub_0006DFF0(void) { bo3_stay_car_page(sub_0006DFF0_gen); }       /* PAL sub_0006DE50 */
extern void sub_000C9540_gen(void);
extern void sub_000A4C50_gen(void);
void sub_000C9540(void) { bo3_stay_heading(sub_000C9540_gen); }
void sub_000A4C50(void) { bo3_stay_lobby_select(sub_000A4C50_gen); }
extern void sub_000CB000_gen(void);
void sub_000CB000(void) { bo3_stay_line_text(sub_000CB000_gen); }
extern void sub_000CA390_gen(void);
void sub_000CA390(void) { bo3_stay_line_first(sub_000CA390_gen); }
void sub_000A9DE0(void) { bo3_quickplay_record(sub_000A9DE0_gen); }    /* NTSC-U sub_000A9A90 */
#endif
