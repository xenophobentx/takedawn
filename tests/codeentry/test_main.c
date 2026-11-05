/* JOIN WITH CODE's code entry (codeentry.c, Takedawn Online step 5). */
#include <stdio.h>
#include <string.h>

#include "codeentry.h"

static int s_fail;

#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); s_fail++; } } while (0)

static void alphabet(void)
{
    int i;
    CHECK(strlen(bo3_ce_alphabet) == CE_COLS * CE_GRID_ROWS);
    /* never two characters that look alike */
    CHECK(!strchr(bo3_ce_alphabet, 'I') && !strchr(bo3_ce_alphabet, 'O') &&
          !strchr(bo3_ce_alphabet, '0') && !strchr(bo3_ce_alphabet, '1'));
    for (i = 0; bo3_ce_alphabet[i]; i++)
        CHECK(strchr(bo3_ce_alphabet + i + 1, bo3_ce_alphabet[i]) == NULL);
}

static void typing(void)
{
    bo3_ce e;
    bo3_ce_reset(&e, NULL);
    CHECK(e.len == 0 && e.col == 0 && !bo3_ce_complete(&e));
    /* A on the first grid line, column 0: 'A' */
    CHECK(bo3_ce_press(&e, CE_BTN_A, CE_ROW_GRID) == CE_ACT_TYPE && !strcmp(e.code, "A"));
    /* right three times, line 2: 'M' (J K L M) */
    bo3_ce_press(&e, CE_BTN_RIGHT, CE_ROW_GRID + 1);
    bo3_ce_press(&e, CE_BTN_RIGHT, CE_ROW_GRID + 1);
    bo3_ce_press(&e, CE_BTN_RIGHT, CE_ROW_GRID + 1);
    CHECK(e.col == 3);
    bo3_ce_press(&e, CE_BTN_A, CE_ROW_GRID + 1);
    CHECK(!strcmp(e.code, "AM"));
    /* left wraps from column 0 to 7; the last line ends in 9 */
    e.col = 0;
    CHECK(bo3_ce_press(&e, CE_BTN_LEFT, CE_ROW_GRID + 3) == CE_ACT_MOVE && e.col == 7);
    bo3_ce_press(&e, CE_BTN_A, CE_ROW_GRID + 3);
    CHECK(!strcmp(e.code, "AM9"));
    /* left and right do nothing off the grid */
    CHECK(bo3_ce_press(&e, CE_BTN_RIGHT, CE_ROW_CODE) == CE_ACT_NONE && e.col == 7);
    /* A on the code line before five letters: nothing */
    CHECK(bo3_ce_press(&e, CE_BTN_A, CE_ROW_CODE) == CE_ACT_NONE);
    bo3_ce_press(&e, CE_BTN_A, CE_ROW_GRID);
    bo3_ce_press(&e, CE_BTN_A, CE_ROW_GRID);
    CHECK(bo3_ce_complete(&e) && !strcmp(e.code, "AM9HH"));
    /* full: no sixth letter */
    CHECK(bo3_ce_press(&e, CE_BTN_A, CE_ROW_GRID) == CE_ACT_NONE && !strcmp(e.code, "AM9HH"));
    /* A on the complete code: the join */
    CHECK(bo3_ce_press(&e, CE_BTN_A, CE_ROW_CODE) == CE_ACT_JOIN);
}

static void deleting(void)
{
    bo3_ce e;
    bo3_ce_reset(&e, "k7m");
    CHECK(!strcmp(e.code, "K7M") && e.len == 3);
    /* X deletes, A on DELETE deletes, B deletes while there is something */
    CHECK(bo3_ce_press(&e, CE_BTN_X, CE_ROW_GRID) == CE_ACT_DEL && !strcmp(e.code, "K7"));
    CHECK(bo3_ce_press(&e, CE_BTN_A, CE_ROW_DEL) == CE_ACT_DEL && !strcmp(e.code, "K"));
    CHECK(bo3_ce_press(&e, CE_BTN_B, CE_ROW_CODE) == CE_ACT_DEL && !strcmp(e.code, ""));
    /* B with nothing typed: leave; X: nothing */
    CHECK(bo3_ce_press(&e, CE_BTN_B, CE_ROW_GRID) == CE_ACT_LEAVE);
    CHECK(bo3_ce_press(&e, CE_BTN_X, CE_ROW_GRID) == CE_ACT_NONE);
    /* a kept code drops what is not in the alphabet, keeps five at most */
    bo3_ce_reset(&e, "o1-ab2cdef");
    CHECK(!strcmp(e.code, "AB2CD"));
}

static void lines(void)
{
    bo3_ce e;
    char t[48];
    bo3_ce_reset(&e, "7K2");
    bo3_ce_line(&e, CE_ROW_CODE, CE_ROW_GRID, t, sizeof t);
    CHECK(!strcmp(t, "CODE   7  K  2  -  -"));
    bo3_ce_reset(&e, "7K2MX");
    bo3_ce_line(&e, CE_ROW_CODE, CE_ROW_CODE, t, sizeof t);
    CHECK(!strcmp(t, "JOIN   7  K  2  M  X"));
    e.col = 3;
    bo3_ce_line(&e, CE_ROW_GRID, CE_ROW_GRID, t, sizeof t);
    CHECK(!strcmp(t, "A  B  C (D) E  F  G  H"));
    /* the column shows only on the cursor's line */
    bo3_ce_line(&e, CE_ROW_GRID + 1, CE_ROW_GRID, t, sizeof t);
    CHECK(!strcmp(t, "J  K  L  M  N  P  Q  R"));
    bo3_ce_line(&e, CE_ROW_GRID + 3, CE_ROW_GRID + 3, t, sizeof t);
    CHECK(!strcmp(t, "2  3  4 (5) 6  7  8  9"));
    e.col = 0;
    bo3_ce_line(&e, CE_ROW_GRID, CE_ROW_GRID, t, sizeof t);
    CHECK(!strcmp(t, "(A) B  C  D  E  F  G  H"));
    e.col = 7;
    bo3_ce_line(&e, CE_ROW_GRID, CE_ROW_GRID, t, sizeof t);
    CHECK(!strcmp(t, "A  B  C  D  E  F  G (H)"));
    bo3_ce_line(&e, CE_ROW_DEL, CE_ROW_DEL, t, sizeof t);
    CHECK(!strcmp(t, "DELETE"));
    /* a short buffer is cut, never overrun */
    bo3_ce_line(&e, CE_ROW_GRID, CE_ROW_GRID, t, 6);
    CHECK(strlen(t) < 6);
}

static void codes(void)
{
    char c[CE_LEN + 1];
    bo3_ce e;
    bo3_ce_make_code(0, c);
    CHECK(!strcmp(c, "AAAAA"));
    bo3_ce_make_code(0x01FFFFFFu, c);
    CHECK(!strcmp(c, "99999"));
    bo3_ce_make_code(0x12345678u, c);
    CHECK(strlen(c) == CE_LEN);
    /* a made code is one the entry can type */
    bo3_ce_reset(&e, c);
    CHECK(!strcmp(e.code, c));
}

int main(void)
{
    alphabet();
    typing();
    deleting();
    lines();
    codes();
    if (s_fail) {
        printf("%d check(s) failed\n", s_fail);
        return 1;
    }
    printf("codeentry: all checks passed\n");
    return 0;
}
