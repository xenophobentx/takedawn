/* The online page's four actions and title case mapping. */
#include <stdio.h>

#include "xboxlive.h"

static int s_fail;

#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); s_fail++; } } while (0)

static void mapping(void)
{
    CHECK(bo3_xboxlive_back_direct(1, 0, 1, 1));
    CHECK(!bo3_xboxlive_back_direct(0, 0, 1, 1));
    CHECK(!bo3_xboxlive_back_direct(1, 1, 1, 1));
    CHECK(!bo3_xboxlive_back_direct(1, 0, 0, 1));
    CHECK(!bo3_xboxlive_back_direct(1, 0, 1, 0));
    CHECK(bo3_xboxlive_prompt_flags(0x01000144u) == 0x105u);
    CHECK(bo3_xboxlive_prompt_flags(0x01000145u) == 0x01000145u);
    CHECK(bo3_xboxlive_bar_flags(1, 0x41u) == 0x1u);
    CHECK(bo3_xboxlive_bar_flags(1, 0x145u) == 0x105u);
    CHECK(bo3_xboxlive_bar_flags(0, 0x145u) == 0x145u);
    CHECK(bo3_xboxlive_leave_target(XL_FLOW_ROOM_LO, 1, 0x0Au) == XL_FLOW_ONLINE_LO);
    CHECK(bo3_xboxlive_leave_target(XL_FLOW_ROOM_LO, 1, 0x01u) == XL_FLOW_ROOM_LO);     /* not by the player's leave */
    CHECK(bo3_xboxlive_leave_target(XL_FLOW_ROOM_LO, 0, 0x0Au) == XL_FLOW_ROOM_LO);
    CHECK(bo3_xboxlive_leave_target(0x12345678u, 1, 0x0Au) == 0x12345678u);              /* another transition */
    CHECK(bo3_xboxlive_create_target(XL_FLOW_ROOM_LO, 1) == XL_FLOW_ONLINE_LO);
    CHECK(bo3_xboxlive_create_target(XL_FLOW_ROOM_LO, 0) == XL_FLOW_ROOM_LO);           /* the room's own CREATE GAME */
    CHECK(bo3_xboxlive_create_target(0x12345678u, 1) == 0x12345678u);
    CHECK(bo3_xboxlive_code_over(XL_FLOW_ST_ROOM, 1));
    CHECK(bo3_xboxlive_code_over(XL_FLOW_ST_ONLINE, 1));
    CHECK(!bo3_xboxlive_code_over(XL_FLOW_ST_ROOM, 0));         /* the System page's code stays */
    CHECK(!bo3_xboxlive_code_over(0x5D0u, 1));                  /* on its way into the game */
    CHECK(!bo3_xboxlive_code_over(0x590u, 1));
    /* CREATE GAME's gcre makes a public game while its own code is the one sent */
    CHECK(bo3_xboxlive_gcre_public("TKDW3", "TKDW3"));
    CHECK(bo3_xboxlive_gcre_public("tkdw3", "TKDW3"));         /* the lobby's codes have no case */
    CHECK(!bo3_xboxlive_gcre_public("", ""));                  /* QUICK PLAY / BROWSE: no CREATE GAME code */
    CHECK(!bo3_xboxlive_gcre_public("", "PRIV1"));             /* the System page's code: private */
    CHECK(!bo3_xboxlive_gcre_public("TKDW3", "PRIV1"));        /* the overlay changed it */
    CHECK(!bo3_xboxlive_gcre_public("TKDW3", ""));             /* the code is over */
    CHECK(!bo3_xboxlive_gcre_public("TKDW3", "TKDW"));
    CHECK(!bo3_xboxlive_gcre_public("TKDW", "TKDW3"));
    CHECK(!bo3_xboxlive_gcre_public(NULL, "TKDW3") && !bo3_xboxlive_gcre_public("TKDW3", NULL));
    CHECK(XL_LINE_COUNT == 4 && XL_QUICK == 0 && XL_CREATE == 1 && XL_JOIN == 2 && XL_BROWSE == 3);
    CHECK(bo3_xboxlive_case(XL_BROWSE, 0) == 0);
    CHECK(bo3_xboxlive_case(XL_QUICK, 0) == 6);
    CHECK(bo3_xboxlive_case(XL_CREATE, 0) == 6);
    CHECK(bo3_xboxlive_case(XL_JOIN, 0) == 6);
    CHECK(bo3_xboxlive_case(0, 1) == 0 && bo3_xboxlive_case(2, 1) == 2);
    CHECK(bo3_xboxlive_case(1, 1) == 6 && bo3_xboxlive_case(3, 1) == 6);
}

static void clamp(void)
{
    /* the cursor is clamped to 0 .. count-1, so a stale one never indexes a
     * line the list does not have */
    CHECK(bo3_xboxlive_clamp(0, 7) == 0);
    CHECK(bo3_xboxlive_clamp(6, 7) == 6);
    CHECK(bo3_xboxlive_clamp(7, 7) == 6);       /* past the last line */
    CHECK(bo3_xboxlive_clamp(6, 5) == 4);       /* a shorter list (the picker) */
    CHECK(bo3_xboxlive_clamp(-1, 5) == 0);
    CHECK(bo3_xboxlive_clamp(3, 0) == 0);       /* empty list */
}

/* The main menu's A: only the XBOX LIVE line, and only while the title would
 * still open its notice, sets the notice-shown byte. */
static void notice(void)
{
    CHECK(bo3_xboxlive_notice_skip(3, 0));
    CHECK(!bo3_xboxlive_notice_skip(3, 1));
    for (uint32_t line = 0; line < 6; line++)
        if (line != 3)
            CHECK(!bo3_xboxlive_notice_skip(line, 0));
}

int main(void)
{
    mapping();
    clamp();
    notice();
    if (s_fail) {
        printf("%d failed\n", s_fail);
        return 1;
    }
    printf("xboxlive: all passed\n");
    return 0;
}
