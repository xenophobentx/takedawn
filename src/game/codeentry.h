/*
 * codeentry.h - JOIN WITH CODE's code entry on the TAKEDAWN ONLINE page
 * (Takedawn Online, step 5). Pure: the model, the lines the page lists and
 * what a press does, so tests/codeentry can run them without the title.
 *
 * The entry is the online page's own list widget with other lines (see
 * xboxlive.c): the code on the first line, the letter grid on the next four
 * (eight letters each, the column under the cursor in brackets of its own),
 * DELETE last. The title moves the list's cursor up and down; left and right
 * move the column, A types, X deletes, B deletes or, with nothing typed,
 * leaves. A on the first line with five letters is the title's own select:
 * the title's Quick Match runs with the code (quickplay.c).
 *
 * Codes use 32 characters: A-Z and 2-9 without I and O (and so without 0 and
 * 1), which never look alike. The lobby takes any letters and digits
 * (TKCODE, tools/lobby), so a code made here is always a valid one.
 */
#ifndef BO3_CODEENTRY_H
#define BO3_CODEENTRY_H

#include <stddef.h>
#include <stdint.h>

#define CE_LEN        5                 /* letters in a code */
#define CE_COLS       8                 /* letters on a grid line */
#define CE_GRID_ROWS  4
#define CE_ROW_CODE   0                 /* the code itself; A joins when it is complete */
#define CE_ROW_GRID   1                 /* the first letter line */
#define CE_ROW_DEL    (CE_ROW_GRID + CE_GRID_ROWS)
#define CE_ROWS       (CE_ROW_DEL + 1)

extern const char bo3_ce_alphabet[CE_COLS * CE_GRID_ROWS + 1];

typedef struct {
    char code[CE_LEN + 1];              /* what is typed, NUL-terminated */
    int len;
    int col;                            /* the grid column under the cursor */
} bo3_ce;

/* a fresh entry; keep (may be NULL) is a code to start from: only the
 * alphabet's characters are kept, upper-cased, at most five */
void bo3_ce_reset(bo3_ce *e, const char *keep);
int  bo3_ce_complete(const bo3_ce *e);
/* the letter at grid line row (CE_ROW_GRID..), column e->col, appended; 1
 * when typed (0: full, or row is not a grid line) */
int  bo3_ce_type(bo3_ce *e, int row);
/* the last letter removed; 0 when there was none */
int  bo3_ce_delete(bo3_ce *e);
/* the column one step left (dir < 0) or right, wrapping */
void bo3_ce_move(bo3_ce *e, int dir);

/* The line the list shows for row (cursor_row: the list's cursor). The code
 * line reads "CODE   7  K  2  -  -", "JOIN   7  K  2  M  X" once complete; a
 * grid line puts two spaces between its letters (the title font's space is
 * narrow), and on the cursor's line the column's letter is bracketed, the
 * brackets taking a space each: "A  B  C (D) E  F  G  H". */
void bo3_ce_line(const bo3_ce *e, int row, int cursor_row, char *out, size_t n);

/* what a fresh press does, by the button and the list's cursor row */
enum { CE_BTN_A, CE_BTN_B, CE_BTN_X, CE_BTN_LEFT, CE_BTN_RIGHT };
enum {
    CE_ACT_NONE,        /* swallowed, nothing happens */
    CE_ACT_TYPE,        /* a letter typed (to the code line when it is the fifth) */
    CE_ACT_DEL,         /* the last letter removed */
    CE_ACT_MOVE,        /* the column moved */
    CE_ACT_JOIN,        /* A passes to the title: its select runs the join */
    CE_ACT_LEAVE        /* back to the four lines */
};
/* applies the press to e and says what happened */
int bo3_ce_press(bo3_ce *e, int button, int row);

/* A new code for a game: five characters of the alphabet, each from five
 * bits of rnd (25 bits used). */
void bo3_ce_make_code(uint32_t rnd, char out[CE_LEN + 1]);

#endif
