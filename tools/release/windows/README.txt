Takedawn @VERSION@ for Windows (x64)

Windows 10 or 11, 64-bit, and a graphics driver with Vulkan (any current
AMD, Intel or NVIDIA driver).

First start
  Takedawn is not code-signed. When the setup or the program is started for
  the first time, Windows SmartScreen may say "Windows protected your PC":
  choose "More info", then "Run anyway".

Game data
  Takedawn needs the game's data from your own disc: a disc image
  (.iso / .xiso) or an extracted disc folder of Burnout 3: Takedown,
  @DISC@. It checks every file and copies them to
  %LOCALAPPDATA%\Takedawn\Game (a European disc's to
  %LOCALAPPDATA%\Takedawn PAL\Game), and the game starts that disc from then
  on. Installing another disc later switches to it; the other disc's data and
  saves stay where they are. Without the setup window, from a terminal:

      Takedawn.exe --install <image-or-folder>

Where things are
  %LOCALAPPDATA%\Takedawn    game data, shader cache
  %APPDATA%\Takedawn         saves, settings, logs, bug reports
  (a European disc: the same under Takedawn PAL)
  An update (a newer setup run over this one) and the uninstaller leave
  both alone.

Portable
  To keep everything next to the program, install to a folder of your choice
  and make an empty file portable.txt (or a folder data) beside Takedawn.exe.
  Saves, settings, the shader cache and the game data then go to the data
  folder there (data\Saves, data\cache, data\Game), not to the two folders
  above. Nothing is moved for you.

How to update
  1. Close Takedawn (the setup refuses to touch a running game).
  2. Run the new setup. It finds the installed copy and says "Update
     Takedawn X to Y"; choose Update. Nothing else to answer.
  The program files are replaced; the saves, settings, game data and shader
  cache (the two folders above), the Start menu entry, the desktop icon and a
  Steam shortcut to Takedawn.exe stay as they are, and the data is not asked
  for again. The version that was replaced is kept in
  %LOCALAPPDATA%\Programs\Takedawn.prev (the uninstaller removes it); if an
  update does not finish, the old version is put back by the setup.
  Windows SmartScreen may warn about the new setup too: More info, Run anyway.
  Takedawn says when a newer release exists: Settings > System > Check for
  updates (only when you press it; nothing is downloaded).

The licences of everything inside are in the licenses folder.
