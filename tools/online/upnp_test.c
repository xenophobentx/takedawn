/* upnp_test.c: one UPnP forwarding test (BO-134, owner OK 2026-10-30):
 * discover the router, print its IGD answer and model, add UDP 31000 -> this
 * box for 600 s, read it back, delete it, read again to show it is gone.
 * Nothing else is changed on the router. Same miniupnpc calls as
 * src/kernel/xnet_upnp.c. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "miniupnpc.h"
#include "upnpcommands.h"
#include "upnperrors.h"
#include "miniwget.h"

static void show_entry(struct UPNPUrls *u, struct IGDdatas *d, const char *tag)
{
    char ic[64] = "", ip[64] = "", desc[80] = "", en[16] = "", rh[64] = "", dur[16] = "";
    int r = UPNP_GetSpecificPortMappingEntry(u->controlURL, d->first.servicetype, "31000", "UDP", rh,
                                             ic, ip, desc, en, dur);
    if (r == UPNPCOMMAND_SUCCESS)
        printf("%s: mapping present: UDP 31000 -> %s:%s '%s' enabled %s lease %s s\n", tag, ic, ip, desc, en, dur);
    else
        printf("%s: no mapping for UDP 31000 (%d %s)\n", tag, r, strupnperror(r));
}

int main(void)
{
    struct UPNPDev *devs;
    struct UPNPUrls u;
    struct IGDdatas d;
    char lan[64] = "", wan[64] = "", ext[64] = "";
    int err = 0, r;
    devs = upnpDiscover(2000, NULL, NULL, UPNP_LOCAL_PORT_ANY, 0, 2, &err);
    if (!devs) { printf("no UPnP device answered (%d)\n", err); return 1; }
    for (struct UPNPDev *p = devs; p; p = p->pNext)
        printf("device: %s (%s)\n", p->descURL, p->st);
    r = UPNP_GetValidIGD(devs, &u, &d, lan, sizeof lan, wan, sizeof wan);
    printf("GetValidIGD: %d, root description %s, our LAN address %s\n", r, u.rootdescURL ? u.rootdescURL : "-", lan);
    if (r <= 0) { freeUPNPDevlist(devs); return 1; }
    {
        int sz = 0, status = 0;
        char *xml = (char *)miniwget(u.rootdescURL, &sz, 0, &status);
        if (xml) {
            const char *keys[] = { "friendlyName", "manufacturer", "modelName", "modelNumber" };
            for (int k = 0; k < 4; k++) {
                char open[40], close[40]; char *a, *b;
                snprintf(open, sizeof open, "<%s>", keys[k]); snprintf(close, sizeof close, "</%s>", keys[k]);
                a = strstr(xml, open); b = a ? strstr(a, close) : NULL;
                if (a && b) printf("%s: %.*s\n", keys[k], (int)(b - a - strlen(open)), a + strlen(open));
            }
            free(xml);
        }
    }
    if (UPNP_GetExternalIPAddress(u.controlURL, d.first.servicetype, ext) == UPNPCOMMAND_SUCCESS)
        printf("external address: %s\n", ext);
    show_entry(&u, &d, "before");
    r = UPNP_AddPortMapping(u.controlURL, d.first.servicetype, "31000", "31000", lan,
                            "Takedawn UPnP test (BO-134)", "UDP", NULL, "600");
    printf("AddPortMapping UDP 31000 -> %s:31000 lease 600 s: %d %s\n", lan, r, strupnperror(r));
    show_entry(&u, &d, "after add");
    r = UPNP_DeletePortMapping(u.controlURL, d.first.servicetype, "31000", "UDP", NULL);
    printf("DeletePortMapping UDP 31000: %d %s\n", r, strupnperror(r));
    show_entry(&u, &d, "after delete");
    FreeUPNPUrls(&u);
    freeUPNPDevlist(devs);
    return 0;
}
