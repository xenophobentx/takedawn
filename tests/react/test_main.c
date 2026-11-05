/* react test: see CMakeLists.txt. The phrases and the d-pad mapping are pure
 * (react.h), so every host checks them. */
#include <stdio.h>
#include <string.h>

#include "react.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

/* id 0..4 -> the fixed English phrases (decision 5), nothing past them */
static void test_phrase(void)
{
    CHECK(!strcmp(bo3_react_phrase(0), "LET'S GO!"));
    CHECK(!strcmp(bo3_react_phrase(1), "GG"));
    CHECK(!strcmp(bo3_react_phrase(2), "SORRY!"));
    CHECK(!strcmp(bo3_react_phrase(3), "ONE MORE?"));
    CHECK(!strcmp(bo3_react_phrase(4), "NICE TAKEDOWN!"));
    CHECK(!strcmp(bo3_react_phrase(-1), ""));
    CHECK(!strcmp(bo3_react_phrase(XR_REACT_N), ""));
}

/* the d-pad maps four directions to ids, differently on the two screens */
static void test_dpad(void)
{
    /* lobby: up LET'S GO!, right GG, left SORRY!, down ONE MORE? */
    CHECK(bo3_react_dpad_id(OB_SC_LOBBY, 0) == 0);
    CHECK(bo3_react_dpad_id(OB_SC_LOBBY, 1) == 1);
    CHECK(bo3_react_dpad_id(OB_SC_LOBBY, 2) == 2);
    CHECK(bo3_react_dpad_id(OB_SC_LOBBY, 3) == 3);
    /* results: up GG, right NICE TAKEDOWN!, left SORRY!, down ONE MORE? */
    CHECK(bo3_react_dpad_id(OB_SC_RESULTS, 0) == 1);
    CHECK(bo3_react_dpad_id(OB_SC_RESULTS, 1) == 4);
    CHECK(bo3_react_dpad_id(OB_SC_RESULTS, 2) == 2);
    CHECK(bo3_react_dpad_id(OB_SC_RESULTS, 3) == 3);
    /* no direction, or not one of the two screens: no reaction */
    CHECK(bo3_react_dpad_id(OB_SC_LOBBY, -1) == -1);
    CHECK(bo3_react_dpad_id(OB_SC_LOBBY, 4) == -1);
    CHECK(bo3_react_dpad_id(OB_SC_MAIN, 0) == -1);
    CHECK(bo3_react_dpad_id(OB_SC_RACE, 1) == -1);
}

static void test_active(void)
{
    CHECK(bo3_react_active_screen(OB_SC_LOBBY));
    CHECK(bo3_react_active_screen(OB_SC_RESULTS));
    CHECK(!bo3_react_active_screen(OB_SC_MAIN));
    CHECK(!bo3_react_active_screen(OB_SC_RACE));
}

/* online.reactions: unset and "on"/"1" are on, "off"/"0" off */
static void test_setting(void)
{
    CHECK(bo3_react_setting_on(NULL));
    CHECK(bo3_react_setting_on(""));
    CHECK(bo3_react_setting_on("on"));
    CHECK(bo3_react_setting_on("1"));
    CHECK(!bo3_react_setting_on("off"));
    CHECK(!bo3_react_setting_on("0"));
}

/* the reaction text is upper-case ASCII for the ticker */
static void test_upper(void)
{
    char b[32];
    strcpy(b, "Racer42: gg");
    bo3_react_upper_ascii(b);
    CHECK(!strcmp(b, "RACER42: GG"));
    strcpy(b, "ab\xC3\xA9 cd");      /* a UTF-8 e-acute: dropped */
    bo3_react_upper_ascii(b);
    CHECK(!strcmp(b, "AB CD"));
    strcpy(b, "one more?");
    bo3_react_upper_ascii(b);
    CHECK(!strcmp(b, "ONE MORE?"));
}

/* the bubble's swap/restore bookkeeping (react.h) */
static void test_bubble(void)
{
    bo3_react_bubble b = { -1, 0, 0 };
    int row = -1;
    uint32_t orig = 0;
    /* nothing to do */
    CHECK(bo3_react_bubble_step(&b, 1000, &row, &orig) == BO3_REACT_BUBBLE_NONE);
    /* a bubble for row 2, remembering pointer 0x1000, until 3000 */
    bo3_react_bubble_begin(&b, 2, 0x1000u, 3000);
    CHECK(bo3_react_bubble_step(&b, 1000, &row, &orig) == BO3_REACT_BUBBLE_SWAP && row == 2);
    CHECK(bo3_react_bubble_step(&b, 2900, &row, &orig) == BO3_REACT_BUBBLE_SWAP && row == 2);
    /* at 3000: restore the title's pointer, and the bubble ends */
    CHECK(bo3_react_bubble_step(&b, 3000, &row, &orig) == BO3_REACT_BUBBLE_RESTORE && row == 2 && orig == 0x1000u);
    CHECK(b.row == -1 && b.until == 0);
    CHECK(bo3_react_bubble_step(&b, 3001, &row, &orig) == BO3_REACT_BUBBLE_NONE);
    /* a new bubble replaces the old one */
    bo3_react_bubble_begin(&b, 0, 0x2000u, 5000);
    bo3_react_bubble_begin(&b, 4, 0x3000u, 6000);
    CHECK(bo3_react_bubble_step(&b, 5500, &row, &orig) == BO3_REACT_BUBBLE_SWAP && row == 4);
    CHECK(bo3_react_bubble_step(&b, 6000, &row, &orig) == BO3_REACT_BUBBLE_RESTORE && orig == 0x3000u);
}

int main(void)
{
    test_phrase();
    test_dpad();
    test_active();
    test_setting();
    test_upper();
    test_bubble();
    printf("%d checks, %d failed\n", checks, fails);
    return fails != 0;
}
