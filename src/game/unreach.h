/*
 * unreach.h - the message a player sees when a link in the game does not
 * come up for 20 s (online.c link_watch), apart from the title so a unit
 * test can read it (tests/unreach, BO-349).
 *
 * It names the copy's own race port: the title's 1000 moved up by the port
 * offset (TAKEDAWN_PORT_OFFSET, the System page; 30000 gives 31000). In Local
 * network mode there is no router to open, so it points at the firewall.
 */
#ifndef BO3_UNREACH_H
#define BO3_UNREACH_H

#include <stdint.h>
#include <stdio.h>

/* lan: Local network mode; offset: the copy's port offset */
static inline void bo3_unreach_text(char *out, size_t cap, int lan, uint16_t offset)
{
    unsigned port = 1000u + offset;
    if (port > 65535u)
        port = 1000u;   /* an offset the toolkit would not use */
    if (lan)
        snprintf(out, cap, "Can't reach another player in this game. Check that the firewall on each computer lets"
                 " UDP port %u through (this copy's race port).", port);
    else
        snprintf(out, cap, "Can't reach another player in this game. If you are hosting, open UDP port %u on your"
                 " router (port forwarding or UPnP); if you joined, the host's port may be closed.", port);
}

#endif
