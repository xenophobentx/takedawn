/* QUIT layout for the main menu (pure so tests need no title memory). */
#ifndef BO3_MAINQUIT_H
#define BO3_MAINQUIT_H

/* The title's five lines, followed by QUIT; quick is never listed. */
static inline void bo3_mainquit_layout(int online, int *lines, int *quick, int *quit)
{
    (void)online;
    *lines = 6;
    *quick = -1;
    *quit = 5;
}

#endif
