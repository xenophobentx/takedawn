/* nodash.h - no way out to the Xbox Dashboard.
 *
 * On a console a few of the title's lines leave the game for the Xbox
 * Dashboard: SELECT GAMERTAG's CREATE NEW ACCOUNT and MANAGE ACCOUNTS, and
 * the YES of the "Continuing to the Xbox Dashboard ..." question they (and
 * some sign-in errors) open. Here there is no dashboard: the title's
 * XLaunchNewImage ends in HalReturnToFirmware, and the game quit on the spot.
 *
 * The gamertag page lists the gamertags only (the title's own list: its rows
 * moved up by two, the count and the cursor with them, and the two rows'
 * picks put back where the title reads them), and every question that would
 * leave for the dashboard takes YES as the title's own NO. See nodash.c for
 * the sites. The functions below are the pure part, on a copy of the page's
 * bytes (little-endian, offsets as in the title), for tests/nodash. */
#ifndef BO3_NODASH_H
#define BO3_NODASH_H

#include <stdint.h>

/* SELECT GAMERTAG's page object (sub_000B3020), its fields */
#define NODASH_USERS    0x06Cu   /* int: the gamertags listed (XOnlineGetUsers) */
#define NODASH_ITEMS    0x17Cu   /* the list widget's rows, 8 bytes each: text, flag */
#define NODASH_CURSOR   0x2CCu   /* uint16: the row the cursor is on */
#define NODASH_COUNT    0x2CEu   /* uint16: the rows */
#define NODASH_NAMES    0x2D0u   /* the gamertags' text, 4 bytes each */
#define NODASH_FIXED    2u       /* CREATE NEW ACCOUNT, MANAGE ACCOUNTS */
#define NODASH_MAX_USERS 8u      /* XOnlineGetUsers' most (the console's 8 accounts) */

/* The title's answers to its YES / NO question (sub_000CD2A0): 4 / 5 on the
 * NTSC-U disc, 5 / 6 on both European ones (their box has one more answer
 * before them). */
#define NODASH_ANSWER_YES_NTSC 4u
#define NODASH_ANSWER_YES_PAL  5u

/* The list as the title just built it (rows CREATE NEW ACCOUNT, MANAGE
 * ACCOUNTS, then the gamertags): the gamertags only, the cursor on the same
 * gamertag. 1 if the page was changed. A list with no gamertag (CREATE NEW
 * ACCOUNT alone) or one already changed stays as it is. */
int bo3_nodash_list(uint8_t *page);

/* The list is the gamertags only (bo3_nodash_list changed it). */
int bo3_nodash_hidden(const uint8_t *page);

/* At the title's A: the cursor as the title's own list counts it (two more),
 * so the A reads the gamertag it shows, and the page leaves with the cursor
 * the title would have (the sign-in state reads it). 1 if it was moved; the
 * title's own list (not changed by bo3_nodash_list) is left alone. */
int bo3_nodash_pick(uint8_t *page);

/* An answer to a question whose YES leaves for the dashboard: YES becomes NO
 * (the disc's YES code, its NO the next one). */
uint32_t bo3_nodash_answer(uint32_t answer, uint32_t yes);

/* The memory unit question at boot (sub_00070CB0): its step 4 (the
 * dashboard question) answered 0 (YES) is answered 1, the title's way back to
 * its step 3. */
uint32_t bo3_nodash_mu_answer(uint32_t answer, uint32_t step);

void bo3_nodash_start(void);

#endif
