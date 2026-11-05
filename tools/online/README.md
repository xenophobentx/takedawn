# Online test tools

Scripts for testing online play with two or more headless copies of the game
on one Linux machine. They expect this layout on the test machine (the one
used so far is the owner's server, `ssh ubuntu`):

    ~/bo134/                 these scripts, the lobby server binary, AppImages
    ~/bo134/src/             a checkout (for tools/headless and tools/lobby)
    ~/bo134/gd, gd2          game folders of the two copies
    ~/bo134/runs/cur, cur2   the copies' run folders (log.txt, pad.txt, pid)

| What it does |
| --- |
| starts one copy (`I=` or `I=2`), presses, waits for the main menu |
| a lobby server and two copies (Host on 127.0.0.2, Guest on 127.0.0.3) |
| a press and a frame of one copy |
| stops only the copies these scripts started (by their pid files) |
| the online soak: many races in the same two processes, as a systemd user unit; logs results, memory, open files, packets |
| the soak's race loop |
| one race with every datagram timed (`TAKEDAWN_NET_TS=1`) |
| three races at different frame rates |
| one game in another mode (Right presses on the mode row) |
| the error cases: server down, server full, game full, host gone |
| where each copy draws the other car, against its own position |
| a one-off UPnP check of a router (adds and deletes one mapping) |

Runs that measure timing (`lag_test.sh`, `fps_chain.sh`) take the test
machine's lock; functional runs do not need it. The scripts delete files only
by fixed paths.
