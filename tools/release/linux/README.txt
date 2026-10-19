Takedawn @VERSION@ for Linux (x86-64): desktop distributions and the Steam Deck

Run
  Give the file a name without a version, in a folder of your own, and keep
  that name for every later update (see "How to update"):

  mkdir -p ~/Applications
  mv Takedawn-@VERSION@-linux-x86_64.AppImage ~/Applications/Takedawn.AppImage
  chmod +x ~/Applications/Takedawn.AppImage
  ~/Applications/Takedawn.AppImage

  It needs a Vulkan driver and glibc 2.29 or newer (any distribution from
  2020 on; SteamOS 3). Without FUSE:  --appimage-extract-and-run

Troubleshooting
  "No GPU access, running on the CPU" (in the setup window and the log), or a
  game far too slow to play: your user cannot open the graphics device. Check
  that it can use /dev/dri (the render group: "groups" lists it; add yourself
  with sudo usermod -aG render $USER, then log in again).

Steam Deck
  Copy the file somewhere under /home/deck (Desktop Mode), name it
  Takedawn.AppImage, make it executable, and add it in Steam with "Add a
  Non-Steam Game"; then it starts from Game Mode like any other game.

Game data
  Takedawn needs the game's data from your own disc: a disc image
  (.iso / .xiso) or an extracted disc folder of Burnout 3: Takedown,
  @DISC@. At the first start a setup window asks for it,
  checks every file and copies them to ~/.local/share/takedawn/Game.
  From a terminal instead:

      ~/Applications/Takedawn.AppImage --install <image-or-folder>

  The setup window can add Takedawn to your Steam library. Steam has to be
  closed for that (it keeps its list of shortcuts while it runs), so the
  window asks, closes Steam for a moment and starts it again. If you say no,
  or Steam cannot be closed: in Desktop Mode right-click Takedawn.AppImage in
  the file manager and choose "Add to Steam", or use "Add a Non-Steam Game"
  (above).

Where things are
  ~/.local/share/takedawn    game data, saves, settings, logs
  ~/.cache/takedawn          shader cache
  An update is a new file in place of the old one; those folders stay.

How to update
  1. Close Takedawn.
  2. Put the new download in place of the old file, under the old name:

       mv ~/Downloads/Takedawn-<new version>-linux-x86_64.AppImage \
          ~/Applications/Takedawn.AppImage
       chmod +x ~/Applications/Takedawn.AppImage

  Saves, settings, game data and the shader cache are in the two folders
  above and stay as they are; the data is not asked for again.
  A Steam shortcut (also on the Steam Deck) stores the file's path, not its
  content: while the new file has the same path and name, the shortcut starts
  the new version. If you saved it under another name, either rename it to the
  old one or edit the shortcut in Steam (Properties: Target, Start In) to the
  new name; on the Deck do this in Desktop Mode, where the file manager
  replaces the file the same way.
  Takedawn says when a newer release exists: Settings > Advanced > Check for
  updates (only when you press it; nothing is downloaded).

The licences of everything inside are in usr/share/licenses/takedawn
(--appimage-extract shows them).
