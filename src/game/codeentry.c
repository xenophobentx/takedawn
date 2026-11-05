/*
 * codeentry.c - the code entry's model and lines (codeentry.h).
 */
#include "codeentry.h"

#include <stdio.h>
#include <string.h>

const char bo3_ce_alphabet[CE_COLS * CE_GRID_ROWS + 1] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";

static int ce_index(char c)
{
    const char *p;
    if (c >= 'a' && c <= 'z')
        c = (char)(c - 'a' + 'A');
    if (!c)
        return -1;
    p = strchr(bo3_ce_alphabet, c);
    return p ? (int)(p - bo3_ce_alphabet) : -1;
}

void bo3_ce_reset(bo3_ce *e, const char *keep)
{
    memset(e, 0, sizeof *e);
    for (; keep && *keep && e->len < CE_LEN; keep++) {
        int i = ce_index(*keep);
        if (i >= 0)
            e->code[e->len++] = bo3_ce_alphabet[i];
    }
    e->code[e->len] = 0;
}

int bo3_ce_complete(const bo3_ce *e)
{
    return e->len == CE_LEN;
}

int bo3_ce_type(bo3_ce *e, int row)
{
    int g = row - CE_ROW_GRID;
    if (g < 0 || g >= CE_GRID_ROWS || e->len >= CE_LEN || e->col < 0 || e->col >= CE_COLS)
        return 0;
    e->code[e->len++] = bo3_ce_alphabet[g * CE_COLS + e->col];
    e->code[e->len] = 0;
    return 1;
}

int bo3_ce_delete(bo3_ce *e)
{
    if (e->len <= 0)
        return 0;
    e->code[--e->len] = 0;
    return 1;
}

void bo3_ce_move(bo3_ce *e, int dir)
{
    e->col = (e->col + (dir < 0 ? CE_COLS - 1 : 1)) % CE_COLS;
}

void bo3_ce_line(const bo3_ce *e, int row, int cursor_row, char *out, size_t n)
{
    size_t o = 0;
    int i;
    if (!out || !n)
        return;
    out[0] = 0;
    if (row == CE_ROW_CODE) {
        /* the title font's space is narrow: two between the letters; '-' for
         * a letter still to type ('_' sits under the line, on the next one) */
        o = (size_t)snprintf(out, n, "%s ", bo3_ce_complete(e) ? "JOIN" : "CODE");
        for (i = 0; i < CE_LEN && o + 3 < n; i++) {
            out[o++] = ' ';
            out[o++] = ' ';
            out[o++] = i < e->len ? e->code[i] : '-';
        }
        out[o < n ? o : n - 1] = 0;
        return;
    }
    if (row == CE_ROW_DEL) {
        snprintf(out, n, "DELETE");
        return;
    }
    if (row < CE_ROW_GRID || row >= CE_ROW_DEL)
        return;
    for (i = 0; i < CE_COLS; i++) {
        char c = bo3_ce_alphabet[(row - CE_ROW_GRID) * CE_COLS + i];
        int sel = row == cursor_row && i == e->col;
        if (o + 6 >= n)
            break;
        if (i && !sel && !(row == cursor_row && i == e->col + 1)) {
            out[o++] = ' ';         /* two spaces; the brackets stand in for them */
            out[o++] = ' ';
        } else if (i) {
            out[o++] = ' ';
        }
        if (sel)
            out[o++] = '(';
        out[o++] = c;
        if (sel)
            out[o++] = ')';
    }
    out[o] = 0;
}

int bo3_ce_press(bo3_ce *e, int button, int row)
{
    int grid = row >= CE_ROW_GRID && row < CE_ROW_DEL;
    switch (button) {
    case CE_BTN_LEFT:
    case CE_BTN_RIGHT:
        if (!grid)
            return CE_ACT_NONE;
        bo3_ce_move(e, button == CE_BTN_LEFT ? -1 : 1);
        return CE_ACT_MOVE;
    case CE_BTN_X:
        return bo3_ce_delete(e) ? CE_ACT_DEL : CE_ACT_NONE;
    case CE_BTN_B:
        return bo3_ce_delete(e) ? CE_ACT_DEL : CE_ACT_LEAVE;
    case CE_BTN_A:
        if (grid)
            return bo3_ce_type(e, row) ? CE_ACT_TYPE : CE_ACT_NONE;
        if (row == CE_ROW_DEL)
            return bo3_ce_delete(e) ? CE_ACT_DEL : CE_ACT_NONE;
        if (row == CE_ROW_CODE && bo3_ce_complete(e))
            return CE_ACT_JOIN;
        return CE_ACT_NONE;
    }
    return CE_ACT_NONE;
}

void bo3_ce_make_code(uint32_t rnd, char out[CE_LEN + 1])
{
    int i;
    for (i = 0; i < CE_LEN; i++) {
        out[i] = bo3_ce_alphabet[rnd & 31u];
        rnd >>= 5;
    }
    out[CE_LEN] = 0;
}
