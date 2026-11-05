/*
 * onlinedesc.h - the description line for Takedawn Online (step 5): the main
 * menu's TAKEDAWN ONLINE line and the online page's line, with the server's
 * counts (onlineinfo.h), in the title's own description widget. See
 * onlinedesc.c for how the widget is fed.
 */
#ifndef BO3_ONLINEDESC_H
#define BO3_ONLINEDESC_H

#include <stdint.h>

/* the text for the main menu's TAKEDAWN ONLINE description (a title-memory
 * UTF-16 string); 0 when online play is off (the title's own text stays) */
uint32_t bo3_onlinedesc_main(void);
/* the online page's description array (8 pointers, one per list line), for
 * the four lines or the code entry (entry != 0); 0 when it cannot be made */
uint32_t bo3_onlinedesc_page(int entry);
/* the code entry's line: msg, or the hint for typing when msg is NULL / "" */
void bo3_onlinedesc_entry_msg(const char *msg);
/* BO-365: the Quick Match page's bottom line says t (every flip while
 * waiting for a race; a changed text is taken on the widget's next frame);
 * NULL or "" gives the page its own line back */
void bo3_onlinedesc_wait_line(const char *t);
/* BO-372: the online page's four lines say msg instead of what each line
 * does, for OD_PAGE_MSG_MS (BROWSE GAMES found no game); NULL clears it */
void bo3_onlinedesc_page_msg(const char *msg);
/* every flip: new counts or a new message reach the line shown */
void bo3_onlinedesc_flip(void);

#endif
