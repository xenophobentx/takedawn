/* unreach test: see CMakeLists.txt. */
#include <stdio.h>
#include <string.h>

#include "unreach.h"

static int fails, checks;

#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

int main(void)
{
    char m[256];
    /* the default offset: 31000, as before */
    bo3_unreach_text(m, sizeof m, 0, 30000);
    CHECK(strstr(m, "open UDP port 31000 on your router") != NULL);
    /* BO-349: another offset names its own port, never 31000 */
    bo3_unreach_text(m, sizeof m, 0, 30700);
    CHECK(strstr(m, "UDP port 31700") != NULL && !strstr(m, "31000"));
    bo3_unreach_text(m, sizeof m, 0, 0);
    CHECK(strstr(m, "UDP port 1000 ") != NULL);
    /* Local network mode: no router, the firewall, the same port */
    bo3_unreach_text(m, sizeof m, 1, 30900);
    CHECK(strstr(m, "firewall") && strstr(m, "UDP port 31900") && !strstr(m, "router"));
    /* an offset past the last port does not wrap into a wrong number */
    bo3_unreach_text(m, sizeof m, 0, 65000);
    CHECK(strstr(m, "UDP port 1000 ") != NULL);
    /* the whole text fits the toolkit's message (256 bytes) */
    bo3_unreach_text(m, sizeof m, 0, 30000);
    CHECK(strlen(m) < 200);
    bo3_unreach_text(m, sizeof m, 1, 30000);
    CHECK(strlen(m) < 200);
    printf("unreach: %d/%d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
