/* The PAL En/Es/Nl/Sv build's wrapped functions (BO3_REGION=PAL-ENSV, the
 * Europe/Australia disc): recomp_manual.c's, at that default.xbe's
 * addresses (tools/region/xbemap.py, NTSC-U to that disc; each the same
 * function as the NTSC-U one it stands for). regen.sh hands this file to
 * the recompiler as --exclude-manual when BO3_REGION=PAL-ENSV, and
 * src/game/CMakeLists.txt compiles it only then. What each one does is in
 * recomp_manual.c. Each definition names its NTSC-U function on its line:
 * tools/region/check.py pairs them. */
#if defined(BO3_REGION_PAL_ENSV) && BO3_REGION_PAL_ENSV
#include <stdint.h>
#include <stdio.h>
#include "../soundtrack.h"
#include "../dsound_notify.h"
#include "../soundbank.h"
#include "../traffic_bp.h"
#include "../hirefresh.h"

/* Custom music (BO-108, BO-230): NTSC-U sub_001D0554, sub_0030211F,
 * sub_001537C0, sub_00155420. */
extern void sub_001D0914_gen(void);
extern void sub_003024BF_gen(void);
extern void sub_00153B00_gen(void);
extern void sub_00155760_gen(void);

void sub_001D0914(void) { bo3_soundtrack_open_song(sub_001D0914_gen); }   /* NTSC-U sub_001D0554 */
void sub_003024BF(void) { bo3_soundtrack_create_decoder(sub_003024BF_gen); }   /* NTSC-U sub_0030211F */
void sub_00153B00(void) { bo3_soundtrack_start_track(sub_00153B00_gen); }   /* NTSC-U sub_001537C0 */
void sub_00155760(void) { bo3_soundtrack_start_source(sub_00155760_gen); }   /* NTSC-U sub_00155420 */

/* DirectSound's "voice done" (BO-173): NTSC-U sub_002FAAEF. */
extern void sub_002FAE8F_gen(void);

void sub_002FAE8F(void) { bo3_dsound_voice_done(sub_002FAE8F_gen); }   /* NTSC-U sub_002FAAEF */

/* A sound bank that did not load (BO-218): NTSC-U sub_001F65A0. */
extern void sub_001F6930_gen(void);

void sub_001F6930(void) { bo3_wave_dict_count(sub_001F6930_gen); }   /* NTSC-U sub_001F65A0 */

/* A traffic car out of the collision broadphase (BO-346): NTSC-U sub_00111680. */
extern void sub_00111A80_gen(void);

void sub_00111A80(void) { bo3_traffic_bp_remove(sub_00111A80_gen); }   /* NTSC-U sub_00111680 */

/* Pictures between the simulation's steps (../hirefresh.c). Each
 * function was matched by the data it reads and the functions it calls:
 * the same as the NTSC-U one's, at this disc's addresses. */
extern void sub_000166B0_gen(void);
extern void sub_001E19A0_gen(void);
extern void sub_001DD640_gen(void);
extern void sub_000305E0_gen(void);
extern void sub_000537E0_gen(void);
extern void sub_00351150_gen(void);
extern void sub_00167C80_gen(void);
extern void sub_00038410_gen(void);
extern void sub_000369D0_gen(void);
extern void sub_0013F280_gen(void);
extern void sub_00188AF0_gen(void);

void sub_000166B0(void) { bo3_update(sub_000166B0_gen); }   /* NTSC-U sub_000165F0 */
void sub_001E19A0(void) { bo3_frame_sync_dirty(sub_001E19A0_gen); }   /* NTSC-U sub_001E15E0 */
void sub_001DD640(void) { bo3_frame_get_ltm(sub_001DD640_gen); }   /* NTSC-U sub_001DD280 */
void sub_000305E0(void) { bo3_draw_vehicle(sub_000305E0_gen); }   /* NTSC-U sub_000303D0 */
void sub_000537E0(void) { bo3_hud_update(sub_000537E0_gen); }   /* NTSC-U sub_000535E0 */
void sub_00351150(void) { bo3_swap(sub_00351150_gen); }   /* NTSC-U sub_00350DB0 */
void sub_00167C80(void) { bo3_player_frame(sub_00167C80_gen); }   /* NTSC-U sub_00167940 */
void sub_00038410(void) { bo3_spark_history(sub_00038410_gen); }   /* NTSC-U sub_00038200 */
void sub_000369D0(void) { bo3_spark_draw(sub_000369D0_gen); }   /* NTSC-U sub_000367C0 */
void sub_0013F280(void) { bo3_race_audio(sub_0013F280_gen); }   /* NTSC-U sub_0013EF40 */
void sub_00188AF0(void) { bo3_fx_clock(sub_00188AF0_gen); }   /* NTSC-U sub_00188750 */

/* Dutch and Swedish (game.language; ../languages.h). This disc's title has
 * them, but no dashboard does: XGetLanguage can only say English or Spanish
 * here, and the start (sub_00017470) takes Spanish when it says so and the
 * disc's English otherwise, with the text files of that language. The
 * title's own language menu (sub_00077940) sets the language value
 * (0x45BD7C: 0 the disc's English, 5 Spanish as on the dashboard, 10 Dutch,
 * 11 Swedish) and the text files the same way. With game.language Dutch or
 * Swedish the start ends the way that menu would for them, and the title's
 * XGetLanguage (sub_001B6000, also asked for a new profile's defaults and
 * the menu's first choice) answers 10 or 11. This build only: the other
 * discs have neither language. Addresses of this disc's default.xbe. */
#include "recomp_types.h"
#include "platform/recomp_env.h"
#include "../languages.h"

extern RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp, g_edi, g_esi;
extern void sub_00017470_gen(void);
extern void sub_001B6000_gen(void);
extern void sub_000174C0(void);         /* the language's layout values */

#define ENSV_LANGUAGE      0x0045BD7Cu  /* the title's language value */
#define ENSV_GLOBAL_DU     0x003AA894u  /* "Data/GlobalDu.bin" */
#define ENSV_HEAD_DU       0x003AA7E8u  /* "Data/HeadDu.bin" */
#define ENSV_GLOBAL_SW     0x003AA86Cu  /* "Data/GlobalSw.bin" */
#define ENSV_HEAD_SW       0x003AA7C8u  /* "Data/HeadSw.bin" */

/* 10 Dutch, 11 Swedish, 0 neither. */
static uint32_t title_language(void)
{
    int want = bo3_language_index(recomp_env(RENV_GAME_LANGUAGE));
    return want == BO3_LANG_DUTCH ? 10u : want == BO3_LANG_SWEDISH ? 11u : 0u;
}

/* The start's choice: edi and esi point at where the text files' names go. */
static void language_start(void (*gen)(void))
{
    uint32_t text = g_edi, head = g_esi, lang = title_language();

    gen();
    if (lang) {
        uint32_t a = g_eax, c = g_ecx, d = g_edx, sp = g_esp;
        MEM32(ENSV_LANGUAGE) = lang;
        MEM32(text) = lang == 10u ? ENSV_GLOBAL_DU : ENSV_GLOBAL_SW;
        MEM32(head) = lang == 10u ? ENSV_HEAD_DU : ENSV_HEAD_SW;
        PUSH32(g_esp, 0);               /* the return address its ret takes */
        sub_000174C0();
        g_eax = a; g_ecx = c; g_edx = d; g_esp = sp;
        fprintf(stderr, "[LANG] the title's own language: %s\n", lang == 10u ? "Dutch" : "Swedish");
    }
}

static void language_query(void (*gen)(void))
{
    uint32_t lang = title_language();

    gen();
    if (lang)
        g_eax = lang;
}

void sub_00017470(void) { language_start(sub_00017470_gen); }   /* region: PAL-ENSV only */
void sub_001B6000(void) { language_query(sub_001B6000_gen); }   /* region: PAL-ENSV only */

/* Online play (BO-134, ../online.c): the XNet and XOnline entry points the
 * title links (its XDK library code, which moved as one block: each En/Es/Nl/Sv
 * function reads the same bytes as its NTSC-U one, tools/region/xbemap.py),
 * and the lobby screen's start (0xA05B0) and start-lock check (0xFD660). */
#include "../online.h"

extern void sub_0020FD5E_gen(void);
extern void sub_00339AC2_gen(void);
extern void sub_00339AD9_gen(void);
extern void sub_00339BC7_gen(void);
extern void sub_003396CE_gen(void);
extern void sub_00339649_gen(void);
extern void sub_00339654_gen(void);
extern void sub_0033965F_gen(void);
extern void sub_00339676_gen(void);
extern void sub_0033968D_gen(void);
extern void sub_00339698_gen(void);
extern void sub_003396A7_gen(void);
extern void sub_003398B1_gen(void);
extern void sub_003396C3_gen(void);
extern void sub_0033989B_gen(void);
extern void sub_003398A6_gen(void);
extern void sub_003396D9_gen(void);
extern void sub_003396E4_gen(void);
extern void sub_003396EF_gen(void);
extern void sub_003396FA_gen(void);
extern void sub_00339705_gen(void);
extern void sub_00339714_gen(void);
extern void sub_0033971F_gen(void);
extern void sub_0033972A_gen(void);
extern void sub_00339735_gen(void);
extern void sub_00339740_gen(void);
extern void sub_0033974B_gen(void);
extern void sub_00339774_gen(void);
extern void sub_0033978E_gen(void);
extern void sub_0033979D_gen(void);
extern void sub_003397B7_gen(void);
extern void sub_0033977F_gen(void);
extern void sub_003397A8_gen(void);
extern void sub_0033975A_gen(void);
extern void sub_00339769_gen(void);
extern void sub_003397C6_gen(void);
extern void sub_0031B323_gen(void);
extern void sub_0031B420_gen(void);
extern void sub_0031AE36_gen(void);
extern void sub_0031AE41_gen(void);
extern void sub_0031AE20_gen(void);
extern void sub_0031AE2B_gen(void);
extern void sub_0031AE50_gen(void);
extern void sub_0031AE5B_gen(void);
extern void sub_0031AE66_gen(void);
extern void sub_0031AEAB_gen(void);
extern void sub_0031AE71_gen(void);
extern void sub_0031AE7C_gen(void);
extern void sub_0031AE87_gen(void);
extern void sub_0031AEB6_gen(void);
extern void sub_0031AEE3_gen(void);
extern void sub_0031AEEE_gen(void);
extern void sub_0031AEF9_gen(void);
extern void sub_0031AF04_gen(void);
extern void sub_0031AF0F_gen(void);
extern void sub_0031AF1A_gen(void);
extern void sub_0031AF25_gen(void);
extern void sub_0031AF30_gen(void);
extern void sub_0031AF51_gen(void);
extern void sub_0031AF72_gen(void);
extern void sub_0031AF7D_gen(void);
extern void sub_0031AF88_gen(void);
extern void sub_0031AF93_gen(void);
extern void sub_0031AF9E_gen(void);
extern void sub_0031AFAD_gen(void);

void sub_0020FD5E(void) { bo3_online_stub(0x0020FD5Eu, sub_0020FD5E_gen); }   /* NTSC-U sub_0020F9AE */
void sub_00339AC2(void) { bo3_online_stub(0x00339AC2u, sub_00339AC2_gen); }   /* NTSC-U sub_00339722 */
void sub_00339AD9(void) { bo3_online_stub(0x00339AD9u, sub_00339AD9_gen); }   /* NTSC-U sub_00339739 */
void sub_00339BC7(void) { bo3_online_stub(0x00339BC7u, sub_00339BC7_gen); }   /* NTSC-U sub_00339827 */
void sub_003396CE(void) { bo3_online_stub(0x003396CEu, sub_003396CE_gen); }   /* NTSC-U sub_0033932E */
void sub_00339649(void) { bo3_online_stub(0x00339649u, sub_00339649_gen); }   /* NTSC-U sub_003392A9 */
void sub_00339654(void) { bo3_online_stub(0x00339654u, sub_00339654_gen); }   /* NTSC-U sub_003392B4 */
void sub_0033965F(void) { bo3_online_stub(0x0033965Fu, sub_0033965F_gen); }   /* NTSC-U sub_003392BF */
void sub_00339676(void) { bo3_online_stub(0x00339676u, sub_00339676_gen); }   /* NTSC-U sub_003392D6 */
void sub_0033968D(void) { bo3_online_stub(0x0033968Du, sub_0033968D_gen); }   /* NTSC-U sub_003392ED */
void sub_00339698(void) { bo3_online_stub(0x00339698u, sub_00339698_gen); }   /* NTSC-U sub_003392F8 */
void sub_003396A7(void) { bo3_online_stub(0x003396A7u, sub_003396A7_gen); }   /* NTSC-U sub_00339307 */
void sub_003398B1(void) { bo3_online_stub(0x003398B1u, sub_003398B1_gen); }   /* NTSC-U sub_00339511 */
void sub_003396C3(void) { bo3_online_stub(0x003396C3u, sub_003396C3_gen); }   /* NTSC-U sub_00339323 */
void sub_0033989B(void) { bo3_online_stub(0x0033989Bu, sub_0033989B_gen); }   /* NTSC-U sub_003394FB */
void sub_003398A6(void) { bo3_online_stub(0x003398A6u, sub_003398A6_gen); }   /* NTSC-U sub_00339506 */
void sub_003396D9(void) { bo3_online_stub(0x003396D9u, sub_003396D9_gen); }   /* NTSC-U sub_00339339 */
void sub_003396E4(void) { bo3_online_stub(0x003396E4u, sub_003396E4_gen); }   /* NTSC-U sub_00339344 */
void sub_003396EF(void) { bo3_online_stub(0x003396EFu, sub_003396EF_gen); }   /* NTSC-U sub_0033934F */
void sub_003396FA(void) { bo3_online_stub(0x003396FAu, sub_003396FA_gen); }   /* NTSC-U sub_0033935A */
void sub_00339705(void) { bo3_online_stub(0x00339705u, sub_00339705_gen); }   /* NTSC-U sub_00339365 */
void sub_00339714(void) { bo3_online_stub(0x00339714u, sub_00339714_gen); }   /* NTSC-U sub_00339374 */
void sub_0033971F(void) { bo3_online_stub(0x0033971Fu, sub_0033971F_gen); }   /* NTSC-U sub_0033937F */
void sub_0033972A(void) { bo3_online_stub(0x0033972Au, sub_0033972A_gen); }   /* NTSC-U sub_0033938A */
void sub_00339735(void) { bo3_online_stub(0x00339735u, sub_00339735_gen); }   /* NTSC-U sub_00339395 */
void sub_00339740(void) { bo3_online_stub(0x00339740u, sub_00339740_gen); }   /* NTSC-U sub_003393A0 */
void sub_0033974B(void) { bo3_online_stub(0x0033974Bu, sub_0033974B_gen); }   /* NTSC-U sub_003393AB */
void sub_00339774(void) { bo3_online_stub(0x00339774u, sub_00339774_gen); }   /* NTSC-U sub_003393D4 */
void sub_0033978E(void) { bo3_online_stub(0x0033978Eu, sub_0033978E_gen); }   /* NTSC-U sub_003393EE */
void sub_0033979D(void) { bo3_online_stub(0x0033979Du, sub_0033979D_gen); }   /* NTSC-U sub_003393FD */
void sub_003397B7(void) { bo3_online_stub(0x003397B7u, sub_003397B7_gen); }   /* NTSC-U sub_00339417 */
void sub_0033977F(void) { bo3_online_stub(0x0033977Fu, sub_0033977F_gen); }   /* NTSC-U sub_003393DF */
void sub_003397A8(void) { bo3_online_stub(0x003397A8u, sub_003397A8_gen); }   /* NTSC-U sub_00339408 */
void sub_0033975A(void) { bo3_online_stub(0x0033975Au, sub_0033975A_gen); }   /* NTSC-U sub_003393BA */
void sub_00339769(void) { bo3_online_stub(0x00339769u, sub_00339769_gen); }   /* NTSC-U sub_003393C9 */
void sub_003397C6(void) { bo3_online_stub(0x003397C6u, sub_003397C6_gen); }   /* NTSC-U sub_00339426 */
void sub_0031B323(void) { bo3_online_stub(0x0031B323u, sub_0031B323_gen); }   /* NTSC-U sub_0031AF83 */
void sub_0031B420(void) { bo3_online_stub(0x0031B420u, sub_0031B420_gen); }   /* NTSC-U sub_0031B080 */
void sub_0031AE36(void) { bo3_online_stub(0x0031AE36u, sub_0031AE36_gen); }   /* NTSC-U sub_0031AA96 */
void sub_0031AE41(void) { bo3_online_stub(0x0031AE41u, sub_0031AE41_gen); }   /* NTSC-U sub_0031AAA1 */
void sub_0031AE20(void) { bo3_online_stub(0x0031AE20u, sub_0031AE20_gen); }   /* NTSC-U sub_0031AA80 */
void sub_0031AE2B(void) { bo3_online_stub(0x0031AE2Bu, sub_0031AE2B_gen); }   /* NTSC-U sub_0031AA8B */
void sub_0031AE50(void) { bo3_online_stub(0x0031AE50u, sub_0031AE50_gen); }   /* NTSC-U sub_0031AAB0 */
void sub_0031AE5B(void) { bo3_online_stub(0x0031AE5Bu, sub_0031AE5B_gen); }   /* NTSC-U sub_0031AABB */
void sub_0031AE66(void) { bo3_online_stub(0x0031AE66u, sub_0031AE66_gen); }   /* NTSC-U sub_0031AAC6 */
void sub_0031AEAB(void) { bo3_online_stub(0x0031AEABu, sub_0031AEAB_gen); }   /* NTSC-U sub_0031AB0B */
void sub_0031AE71(void) { bo3_online_stub(0x0031AE71u, sub_0031AE71_gen); }   /* NTSC-U sub_0031AAD1 */
void sub_0031AE7C(void) { bo3_online_stub(0x0031AE7Cu, sub_0031AE7C_gen); }   /* NTSC-U sub_0031AADC */
void sub_0031AE87(void) { bo3_online_stub(0x0031AE87u, sub_0031AE87_gen); }   /* NTSC-U sub_0031AAE7 */
void sub_0031AEB6(void) { bo3_online_stub(0x0031AEB6u, sub_0031AEB6_gen); }   /* NTSC-U sub_0031AB16 */
void sub_0031AEE3(void) { bo3_online_stub(0x0031AEE3u, sub_0031AEE3_gen); }   /* NTSC-U sub_0031AB43 */
void sub_0031AEEE(void) { bo3_online_stub(0x0031AEEEu, sub_0031AEEE_gen); }   /* NTSC-U sub_0031AB4E */
void sub_0031AEF9(void) { bo3_online_stub(0x0031AEF9u, sub_0031AEF9_gen); }   /* NTSC-U sub_0031AB59 */
void sub_0031AF04(void) { bo3_online_stub(0x0031AF04u, sub_0031AF04_gen); }   /* NTSC-U sub_0031AB64 */
void sub_0031AF0F(void) { bo3_online_stub(0x0031AF0Fu, sub_0031AF0F_gen); }   /* NTSC-U sub_0031AB6F */
void sub_0031AF1A(void) { bo3_online_stub(0x0031AF1Au, sub_0031AF1A_gen); }   /* NTSC-U sub_0031AB7A */
void sub_0031AF25(void) { bo3_online_stub(0x0031AF25u, sub_0031AF25_gen); }   /* NTSC-U sub_0031AB85 */
void sub_0031AF30(void) { bo3_online_stub(0x0031AF30u, sub_0031AF30_gen); }   /* NTSC-U sub_0031AB90 */
void sub_0031AF51(void) { bo3_online_stub(0x0031AF51u, sub_0031AF51_gen); }   /* NTSC-U sub_0031ABB1 */
void sub_0031AF72(void) { bo3_online_stub(0x0031AF72u, sub_0031AF72_gen); }   /* NTSC-U sub_0031ABD2 */
void sub_0031AF7D(void) { bo3_online_stub(0x0031AF7Du, sub_0031AF7D_gen); }   /* NTSC-U sub_0031ABDD */
void sub_0031AF88(void) { bo3_online_stub(0x0031AF88u, sub_0031AF88_gen); }   /* NTSC-U sub_0031ABE8 */
void sub_0031AF93(void) { bo3_online_stub(0x0031AF93u, sub_0031AF93_gen); }   /* NTSC-U sub_0031ABF3 */
void sub_0031AF9E(void) { bo3_online_stub(0x0031AF9Eu, sub_0031AF9E_gen); }   /* NTSC-U sub_0031ABFE */
void sub_0031AFAD(void) { bo3_online_stub(0x0031AFADu, sub_0031AFAD_gen); }   /* NTSC-U sub_0031AC0D */

extern void sub_000A08D0_gen(void);
extern void sub_000FDA60_gen(void);
void sub_000A08D0(void) { bo3_online_lobby_screen(sub_000A08D0_gen); }   /* NTSC-U sub_000A05B0 */
void sub_000FDA60(void) { bo3_online_fd660(sub_000FDA60_gen); }   /* NTSC-U sub_000FD660 */

/* High refresh (../hirefresh.c): the audio world (BO-297) and the car's
 * reflection cube (BO-282.1). */
extern void sub_001CA8F0_gen(void);
extern void sub_0003FC30_gen(void);
void sub_001CA8F0(void) { bo3_audio_world(sub_001CA8F0_gen); }   /* NTSC-U sub_001CA530 */
void sub_0003FC30(void) { bo3_reflection_cube(sub_0003FC30_gen); }   /* NTSC-U sub_0003FA20 */

/* The front end's picture callbacks (BO-315, display.pictures in ../main.c):
 * this disc's function address is passed, as main.c compares it. */
extern void sub_001C1E20_gen(void);
extern void sub_001C5D60_gen(void);
extern void bo3_fe_picture_draw(void (*fn)(void), uint32_t va);
void sub_001C1E20(void) { bo3_fe_picture_draw(sub_001C1E20_gen, 0x001C1E20u); }   /* NTSC-U sub_001C1A60 */
void sub_001C5D60(void) { bo3_fe_picture_draw(sub_001C5D60_gen, 0x001C5D60u); }   /* NTSC-U sub_001C59A0 */

/* QUICK PLAY (src/game/quickplay.c, Takedawn Online step 3): the create
 * state's name page and mode page builders. The generated bodies are the _gen
 * names; they are all that runs unless QUICK PLAY's create branch is on. */
#include "../quickplay.h"
#include "../vote.h"
extern void sub_00098350_gen(void);
extern void sub_000998F0_gen(void);
void sub_00098350(void) { bo3_quickplay_name_page(sub_00098350_gen); }   /* NTSC-U sub_00098050 */
void sub_000998F0(void) { bo3_quickplay_mode_page(sub_000998F0_gen); }   /* NTSC-U sub_000995F0 */
extern void sub_000CC1E0_gen(void);
extern void sub_000CD2F0_gen(void);
extern void sub_000A0F10_gen(void);
void sub_000CC1E0(void) { bo3_vote_box_open(sub_000CC1E0_gen); }   /* NTSC-U sub_000CBE40 */
void sub_000CD2F0(void) { bo3_vote_row(sub_000CD2F0_gen); }   /* NTSC-U sub_000CCF00 */
void sub_000A0F10(void) { bo3_vote_input(sub_000A0F10_gen); }   /* NTSC-U sub_000A0BF0 */
extern void sub_000A1880_gen(void);
void sub_000A1880(void) { bo3_vote_start_check(sub_000A1880_gen); }   /* NTSC-U sub_000A1560 */

/* After a raced online game (src/game/stay.c, Takedawn Online step 4). */
#include "../stay.h"
extern void sub_0006F0F0_gen(void);
extern void sub_0006E060_gen(void);
extern void sub_000A9E80_gen(void);
void sub_0006F0F0(void) { bo3_stay_room_state(sub_0006F0F0_gen); }     /* NTSC-U sub_0006EEE0 */
void sub_0006E060(void) { bo3_stay_car_page(sub_0006E060_gen); }       /* PAL-ENSV sub_0006DE50 */
extern void sub_000C95E0_gen(void);
extern void sub_000A4CF0_gen(void);
void sub_000C95E0(void) { bo3_stay_heading(sub_000C95E0_gen); }
void sub_000A4CF0(void) { bo3_stay_lobby_select(sub_000A4CF0_gen); }
extern void sub_000CB0A0_gen(void);
void sub_000CB0A0(void) { bo3_stay_line_text(sub_000CB0A0_gen); }
extern void sub_000CA430_gen(void);
void sub_000CA430(void) { bo3_stay_line_first(sub_000CA430_gen); }
void sub_000A9E80(void) { bo3_quickplay_record(sub_000A9E80_gen); }    /* NTSC-U sub_000A9A90 */
#endif
