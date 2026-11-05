/* upnp_ctl.c: the online tests' UPnP helper (BO-134), the same miniupnpc
 * calls as the toolkit's src/kernel/xnet_upnp.c.
 *
 *   upnp_ctl list [PREFIX]              the router's UDP mappings (all, or those
 *                                        whose description starts with PREFIX)
 *   upnp_ctl add EXT INT LEASE DESC     UDP EXT -> this machine:INT for LEASE s
 *   upnp_ctl del EXT                    remove UDP EXT
 *   upnp_ctl cleanup PREFIX             remove every UDP mapping whose
 *                                        description starts with PREFIX
 *
 * cleanup only ever deletes mappings with the given description prefix
 * ("Takedawn test" in the scripts), never anyone else's.
 *
 * Windows: x86_64-w64-mingw32-clang ... -lws2_32 -liphlpapi (llvm-mingw).
 * Build (Linux): cc -O2 -DMINIUPNP_STATICLIB -I$TK/third_party/miniupnpc/include
 *   -I$TK/third_party/miniupnpc/src upnp_ctl.c $TK/third_party/miniupnpc/src/(the
 *   same files as src/kernel/CMakeLists.txt).c -o upnp_ctl */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "miniupnpc.h"
#include "upnpcommands.h"
#include "upnperrors.h"
#ifdef _WIN32
#include <winsock2.h>
#endif

static struct UPNPUrls u;
static struct IGDdatas d;
static char lan[64];

static int open_igd(void)
{
    struct UPNPDev *devs;
    char wan[64] = "";
    int err = 0, r;
    devs = upnpDiscover(2000, NULL, NULL, UPNP_LOCAL_PORT_ANY, 0, 2, &err);
    if (!devs) {
        printf("no UPnP router answered (%d)\n", err);
        return 0;
    }
    r = UPNP_GetValidIGD(devs, &u, &d, lan, sizeof lan, wan, sizeof wan);
    freeUPNPDevlist(devs);
    if (r != 1) {
        printf("no usable router (%d)\n", r);
        return 0;
    }
    return 1;
}

/* walk the generic table; act(): 1 = deleted (the index stays) */
static int walk(const char *prefix, int del)
{
    int i = 0, n = 0, guard = 0;
    for (;;) {
        char idx[8], ext[8] = "", ic[64] = "", ip[8] = "", proto[8] = "", desc[96] = "", en[8] = "",
             rh[64] = "", dur[16] = "";
        int r;
        if (++guard > 512)
            break;
        snprintf(idx, sizeof idx, "%d", i);
        r = UPNP_GetGenericPortMappingEntry(u.controlURL, d.first.servicetype, idx, ext, ic, ip, proto,
                                            desc, en, rh, dur);
        if (r != UPNPCOMMAND_SUCCESS)
            break;
        if (!prefix || !strncmp(desc, prefix, strlen(prefix))) {
            if (del && !strcmp(proto, "UDP")) {
                r = UPNP_DeletePortMapping(u.controlURL, d.first.servicetype, ext, "UDP", NULL);
                printf("deleted %s %s -> %s:%s '%s': %d %s\n", proto, ext, ic, ip, desc, r, strupnperror(r));
                if (r == UPNPCOMMAND_SUCCESS) {
                    n++;
                    continue;
                }
            } else {
                printf("%s %s -> %s:%s '%s' enabled %s lease %s\n", proto, ext, ic, ip, desc, en, dur);
                n++;
            }
        }
        i++;
    }
    return n;
}

int main(int argc, char **argv)
{
    int r;
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    if (argc < 2)
        goto usage;
    if (!open_igd())
        return 1;
    if (!strcmp(argv[1], "list")) {
        printf("%d mapping(s)\n", walk(argc > 2 ? argv[2] : NULL, 0));
    } else if (!strcmp(argv[1], "add") && argc == 6) {
        r = UPNP_AddPortMapping(u.controlURL, d.first.servicetype, argv[2], argv[3], lan, argv[5], "UDP",
                                NULL, argv[4]);
        printf("add UDP %s -> %s:%s lease %s '%s': %d %s\n", argv[2], lan, argv[3], argv[4], argv[5], r,
               strupnperror(r));
        return r != UPNPCOMMAND_SUCCESS;
    } else if (!strcmp(argv[1], "del") && argc == 3) {
        r = UPNP_DeletePortMapping(u.controlURL, d.first.servicetype, argv[2], "UDP", NULL);
        printf("del UDP %s: %d %s\n", argv[2], r, strupnperror(r));
        return r != UPNPCOMMAND_SUCCESS;
    } else if (!strcmp(argv[1], "cleanup") && argc == 3 && argv[2][0]) {
        printf("%d deleted\n", walk(argv[2], 1));
    } else {
        goto usage;
    }
    FreeUPNPUrls(&u);
    return 0;
usage:
    fprintf(stderr, "usage: upnp_ctl list [PREFIX] | add EXT INT LEASE DESC | del EXT | cleanup PREFIX\n");
    return 2;
}
