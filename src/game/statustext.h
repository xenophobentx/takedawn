/*
 * statustext.h - the texts this program shows in the title's own menus
 * (BO-352): the lobby status lines, the kick reasons and the quick reactions.
 * One id per text, one table per language (languages.h indices). English is
 * always present and is the fallback for a language whose table is not filled
 * in yet (the placeholders of step 9).
 *
 * The reactions travel as an id only (Part 3), so the receiver shows them in
 * its own language. The status texts are built locally, so they follow the
 * player's own language the same way.
 */
#ifndef BO3_STATUSTEXT_H
#define BO3_STATUSTEXT_H

enum {
    BO3_TXT_STARTING_IN,    /* "STARTING IN %d S" */
    BO3_TXT_STARTING,       /* "STARTING..." */
    BO3_TXT_CANT_REACH,     /* "CAN'T REACH %s - OPEN UDP PORT %u" */
    BO3_TXT_CODE,           /* "CODE %s": in front of every status line of a coded game */
    BO3_TXT_HOST_START,     /* "HOST: PRESS START WHEN READY" */
    BO3_TXT_WAIT_HOST,      /* "WAITING FOR THE HOST" */
    BO3_TXT_WAIT_PICKING,   /* "WAITING FOR PLAYERS %d/%d - %d PICKING A CAR" */
    BO3_TXT_WAIT,           /* "WAITING FOR PLAYERS %d/%d" */
    BO3_TXT_REMOVED_NOLINK, /* "REMOVED: COULD NOT CONNECT TO THE HOST" (either side's port may be the closed one) */
    BO3_TXT_REMOVED_HOST,   /* "REMOVED: THE HOST REMOVED YOU" */
    BO3_TXT_GAME_ENDED,     /* "THE GAME ENDED OR THE HOST REMOVED YOU" */
    BO3_TXT_REACT_GO,       /* "LET'S GO!" */
    BO3_TXT_REACT_GG,       /* "GG" */
    BO3_TXT_REACT_SORRY,    /* "SORRY!" */
    BO3_TXT_REACT_ONE_MORE, /* "ONE MORE?" */
    BO3_TXT_REACT_TAKEDOWN, /* "NICE TAKEDOWN!" */
    BO3_TXT_VOTE,           /* "VOTE FOR THE NEXT TRACK": the vote box's heading */
    BO3_TXT_VOTING,         /* "VOTING ON THE NEXT TRACK": the lobby line during a vote */
    BO3_TXT_RETURNING,      /* "RETURNING TO THE LOBBY..." */
    /* BO-365: the Quick Match page's line while it waits for a race (racewait.c) */
    BO3_TXT_RACEWAIT,       /* "RACE IN PROGRESS - %d PLAYERS - %d:%02d - WAITING" */
    BO3_TXT_RACEWAIT_LOBBY, /* "THE RACE IS OVER - JOINING THE GAME..." */
    BO3_TXT_RACEWAIT_QUEUE, /* "THE RACE IS OVER - %d AHEAD OF YOU" */
    BO3_TXT_RACEWAIT_GONE,  /* "THE RACE ENDED AND NOBODY MADE THE GAME AGAIN" */
    BO3_TXT_RACEWAIT_FULL,  /* "THE GAME IS FULL AFTER THE RACE" */
    /* ... and the code entry's message when a JOIN WITH CODE wait ends (xboxlive.c) */
    BO3_TXT_RACEWAIT_CODE_GONE, /* "GAME %s ENDED - NOBODY MADE IT AGAIN" */
    BO3_TXT_RACEWAIT_CODE_FULL, /* "GAME %s IS FULL" */
    /* stay together: a guest back first waits for its host's game (racewait.c) */
    BO3_TXT_STAY_WAIT,      /* "WAITING FOR THE HOST TO COME BACK %d:%02d (B: LEAVE)" */
    BO3_TXT_STAY_HOST_BACK, /* "THE HOST IS BACK - MAKING THE GAME AGAIN" */
    BO3_TXT_STAY_JOINING,   /* "THE HOST IS BACK - JOINING THE GAME..." */
    BO3_TXT_STAY_HOST_LEFT, /* "THE HOST LEFT - THE GAME IS OVER" */
    BO3_TXT_STAY_HOST_GONE, /* "THE HOST DID NOT COME BACK - THE GAME IS OVER" */
    BO3_TXT_STAY_FULL,      /* "THE GAME IS FULL" */
    BO3_TXT_STAY_OVER,      /* "THE GAME IS OVER" */
    /* ... and the game lobby's line while players of the race are still on the way */
    BO3_TXT_WAIT_RACERS,    /* "WAITING FOR %d MORE FROM THE RACE" */
    /* the title's own kick box ("You have been kicked out of the room."), said
     * with the reason instead (lobbystatus.c bo3_status_kick_box). Sentence
     * case: the box's font has both, and it wraps the text itself. */
    BO3_TXT_BOX_NOLINK,     /* "Your connection to %s did not come up, so the game removed you." (%s: a name, or "the host") */
    BO3_TXT_BOX_HOST,       /* "The host removed you from the game." */
    BO3_TXT_BOX_HINT_MAC,   /* "Allow Takedawn under Local Network in System Settings, Privacy and Security." */
    BO3_TXT_BOX_HINT_WIN,   /* "Set this network to Private in the Windows settings." */
    BO3_TXT_BOX_HINT_LINUX, /* "Check the firewall on this computer." */
    BO3_TXT_BOX_HINT_PORTS, /* "If this keeps happening, open UDP ports %u and %u on your router (...), or ... UDP 21811 (the relay)." */
    BO3_TXT_N
};

/* The text for `id` in `lang` (a languages.h index, BO3_LANG_*). Upper-case
 * for the title's font; English when the language has none of its own. "" for
 * an id the program does not know. */
const char *bo3_statustext(int lang, int id);

#endif
