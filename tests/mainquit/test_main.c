/* QUIT is the only added main-menu line in both online modes. */
#include <stdio.h>

#include "mainquit.h"

static int s_fail;

#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); s_fail++; } } while (0)

static void online(void)
{
    int lines, quick, quit;
    bo3_mainquit_layout(1, &lines, &quick, &quit);
    CHECK(lines == 6 && quick == -1 && quit == 5);
}

static void offline(void)
{
    int lines, quick, quit;
    bo3_mainquit_layout(0, &lines, &quick, &quit);
    CHECK(lines == 6 && quick == -1 && quit == 5);
}

int main(void)
{
    online();
    offline();
    if (s_fail) {
        printf("%d failed\n", s_fail);
        return 1;
    }
    printf("mainquit: all passed\n");
    return 0;
}
