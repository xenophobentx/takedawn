/*
 * onlinescreens.c - the front-end screens, by (flow state offset, page), for
 * all three discs. See onlinescreens.h. BO-352 (step 1): the table moved out
 * of onlinebot.c so the player navigator and the debug one share it, with a
 * PAL and a PAL-ENSV address for every page (tools/region/xbemap.py).
 *
 * The flow offsets are region-free (the state machine's layout does not
 * change); the page objects are the three discs' own statics. A page 0 is
 * "any page of that state".
 */
#include "onlinescreens.h"
#include "onlinebot.h"          /* OB_SC_* */
#include "region.h"

const bo3_online_screen bo3_online_screens[] = {
    { 0x000, 0, 0, 0, OB_SC_LOADING },
    { 0x018, 0, 0, 0, OB_SC_LOADING },            /* between modules (loading, race start) */
    { 0x508, 0, 0, 0, OB_SC_BOOT },               /* the films */
    { 0x568, 0, 0, 0, OB_SC_BOOT },               /* attract */
    { 0x528, 0, 0, 0, OB_SC_BOOT },               /* press START */
    { 0x4F0, 0, 0, 0, OB_SC_BOOT },               /* memory unit check */
    { 0x350, 0, 0, 0, OB_SC_BOOT },               /* load profile */
    { 0x0C0, 0x00559390u, 0x005596B0u, 0x00559750u, OB_SC_MAIN },    /* SELECT OPTION: World Tour .. Xbox Live, Driver Details */
    { 0x630, 0x0055FBDCu, 0x0055FEFCu, 0x0055FF9Cu, OB_SC_GAMERTAG }, /* SELECT GAMERTAG */
    { 0x630, 0x0055FBD0u, 0x0055FEF0u, 0x0055FF90u, OB_SC_SIGNIN },   /* CONNECTING... */
    { 0x630, 0x0055FFCCu, 0x005602ECu, 0x0056038Cu, OB_SC_SIGNIN },   /* SIGNING IN TO EA */
    { 0x630, 0x00565060u, 0x00565380u, 0x00565420u, OB_SC_RANKINGS }, /* UPDATE RANKINGS (after a race) */
    { 0x630, 0x00561100u, 0x00561420u, 0x005614C0u, OB_SC_LOBBYSEL }, /* LOBBY SELECT, after a race */
    { 0x5B0, 0x0055DF50u, 0x0055E270u, 0x0055E310u, OB_SC_LIVE },     /* XBOX LIVE: Quick Match, OptiMatch, Lobby Select, Records, News */
    { 0x5B0, 0x00561100u, 0x00561420u, 0x005614C0u, OB_SC_LOBBYSEL }, /* LOBBY SELECT: the rooms (Takedawn) */
    { 0x928, 0x00564490u, 0x005647B0u, 0x00564850u, OB_SC_ROOM },     /* the room: Create Game, Join Game, Chat, View Players */
    { 0x5D0, 0x0055DF8Cu, 0x0055E2ACu, 0x0055E34Cu, OB_SC_CREATE },   /* CREATE GAME: name, password, rank, player limit */
    { 0x5D0, 0x00564900u, 0x00564C20u, 0x00564CC0u, OB_SC_MODE },     /* CREATE GAME: mode, region, track, laps, series */
    { 0x5D0, 0x0055E07Cu, 0x0055E39Cu, 0x0055E43Cu, OB_SC_CREATING },
    { 0x808, 0x005628E0u, 0x00562C00u, 0x00562CA0u, OB_SC_QUICK },    /* QUICK MATCH: searching / NO GAMES FOUND */
    { 0x590, 0x00559648u, 0x00559968u, 0x00559A08u, OB_SC_CAR },      /* SELECT CAR */
    { 0x590, 0x00564900u, 0x00564C20u, 0x00564CC0u, OB_SC_OPTIONS },  /* the lobby's Options: the mode page on the lobby's request (0x6DCA1) */
    { 0x590, 0x0055CBA0u, 0x0055CEC0u, 0x0055CF60u, OB_SC_LOBBY },    /* the game lobby (with the lobby screen object) */
    { 0x1C8, 0, 0, 0, OB_SC_RACE },               /* race intro */
    { 0x030, 0, 0, 0, OB_SC_RACE },
    { 0x178, 0x0055A0A0u, 0x0055A3C0u, 0x0055A460u, OB_SC_RACE_END }, /* "you finished" */
    { 0x178, 0x00559FA8u, 0x0055A2C8u, 0x0055A368u, OB_SC_RESULTS },  /* RESULTS: Continue To Lobby (A) / Quit (B) */
    { 0x178, 0x0055A0B8u, 0x0055A3D8u, 0x0055A478u, OB_SC_RESULTS },  /* Party Crash: ROUND n OF 3 STANDINGS (A) */
    { 0x178, 0, 0, 0, OB_SC_RESULTS },            /* another page of the event's end: A, never B (Quit) */
    { 0x4D8, 0, 0, 0, OB_SC_AUTOSAVE },
    { 0x260, 0, 0, 0, OB_SC_POINTS },             /* BURNOUT POINTS and the like */
};

const int bo3_online_screens_n = (int)(sizeof bo3_online_screens / sizeof bo3_online_screens[0]);

uint32_t bo3_online_screen_page_for(const bo3_online_screen *s, int region)
{
    return region == 1 ? s->pal : region == 2 ? s->ensv : s->ntsc;
}

uint32_t bo3_online_screen_page(const bo3_online_screen *s)
{
#if defined(BO3_REGION_PAL_ENSV) && BO3_REGION_PAL_ENSV
    return s->ensv;
#elif defined(BO3_REGION_PAL) && BO3_REGION_PAL
    return s->pal;
#else
    return s->ntsc;
#endif
}

int bo3_online_screen_for_region(uint32_t flow, uint32_t page, int region)
{
    int i;
    for (i = 0; i < bo3_online_screens_n; i++) {
        const bo3_online_screen *s = &bo3_online_screens[i];
        uint32_t p = bo3_online_screen_page_for(s, region);
        if (s->flow == flow && (!p || p == page))
            return s->sc;
    }
    return OB_SC_UNKNOWN;
}

int bo3_online_screen_for(uint32_t flow, uint32_t page)
{
#if defined(BO3_REGION_PAL_ENSV) && BO3_REGION_PAL_ENSV
    return bo3_online_screen_for_region(flow, page, 2);
#elif defined(BO3_REGION_PAL) && BO3_REGION_PAL
    return bo3_online_screen_for_region(flow, page, 1);
#else
    return bo3_online_screen_for_region(flow, page, 0);
#endif
}
