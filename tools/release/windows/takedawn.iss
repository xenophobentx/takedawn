; Takedawn's Windows setup: one exe, per user, no administrator rights.
;
;   ISCC.exe /DPackage=<program folder> /DVersion=<x.y.z> /DIcon=<takedawn.ico> /O<out dir> takedawn.iss
;
; <program folder> is what tools/release/build-windows.sh makes: Takedawn.exe,
; SDL2.dll, licenses\, README.txt and, when it was built, the setup window
; (takedawn-setup.exe with its Qt libraries). It holds no game data: the
; program installs that from the player's own disc at first start.
;
; Installed into %LOCALAPPDATA%\Programs\Takedawn, with a Start menu entry,
; an optional desktop icon and an uninstaller. Nothing is written outside the
; user's profile. The game's data, saves, settings and shader cache live in
; %LOCALAPPDATA%\Takedawn and %APPDATA%\Takedawn: an update (this setup run
; again) and the uninstaller leave them alone.
;
; An update is this setup run over an existing install (BO-100). Same AppId,
; so Windows' uninstall entry and the folder are found again; the shortcuts
; keep their target. Before any file is written the setup
;   - refuses while Takedawn (or its setup window) runs: Takedawn.exe,
;     takedawn-setup.exe and SDL2.dll cannot be opened for writing then, and
;     the folder cannot be renamed if anything else has it open; nothing has
;     been changed when it says so (exit code 7 with /SILENT);
;   - keeps that folder as the previous version (one copy; the uninstaller
;     removes it) and installs into a fresh one;
;   - puts the previous folder back if the installation does not finish.
; CloseApplications is off on purpose: the Restart Manager would close a
; running game for the player, in /SILENT mode without asking.
;
; The exe is not signed (decided), so Windows SmartScreen warns at the first
; start: "More info", then "Run anyway".
;
; /DPayloadTest makes a variant for the build machine's own check: no
; uninstaller, no registry entry, no shortcuts; with /DIR= it writes into that
; folder and nowhere else. It has the same update code (the folder swap, the
; refusal, the rollback; the installed version is read from Takedawn.exe when
; there is no registry entry) and, only there, /TESTFAIL=1 to make the
; installation fail while the files are written (tools/release/update-test).

#ifndef Package
  #error Package (the folder with Takedawn.exe) is required
#endif
#ifndef Version
  #error Version is required
#endif
#ifndef Icon
  #error Icon (takedawn.ico) is required
#endif

; /DRegion=PAL: the PAL (Europe) build (BO3_REGION=PAL, docs/pal.md), installed
; beside the NTSC-U one under its own name, folder and uninstall entry; its
; game keeps its own per-user folders ("Takedawn PAL").
#ifdef Region
  #if Region == "PAL"
    #define AppTitle "Takedawn PAL"
    #define AppGuid "72D2A8B1-201D-518F-8BD8-FF15282D97C4"
    #define FileTag "-pal"
  #endif
#endif
#ifndef AppTitle
  #define AppTitle "Takedawn"
  #define AppGuid "8C1E5B0A-64D3-4C7E-A1F2-0D9B7E53A6C4"
  #define FileTag ""
#endif

; VersionInfoVersion takes numbers only: "0.1.2-beta" is 0.1.2 there.
#define DashAt Pos("-", Version)
#if DashAt > 0
  #define VersionNumbers Copy(Version, 1, DashAt - 1)
#else
  #define VersionNumbers Version
#endif

[Setup]
AppId={{{#AppGuid}}
AppName={#AppTitle}
AppVersion={#Version}
AppVerName={#AppTitle} {#Version}
AppPublisher=Takedawn
VersionInfoVersion={#VersionNumbers}
VersionInfoProductName={#AppTitle}
VersionInfoDescription=Takedawn setup
PrivilegesRequired=lowest
DefaultDirName={localappdata}\Programs\{#AppTitle}
DisableProgramGroupPage=yes
DisableWelcomePage=yes
DisableReadyPage=no
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
Compression=lzma2/max
SolidCompression=yes
SetupIconFile={#Icon}
UninstallDisplayIcon={app}\Takedawn.exe
UninstallDisplayName={#AppTitle}
WizardStyle=modern
CloseApplications=no
#ifdef PayloadTest
OutputBaseFilename=Takedawn-{#Version}{#FileTag}-windows-x64-payloadtest
Uninstallable=no
CreateUninstallRegKey=no
#else
OutputBaseFilename=Takedawn-{#Version}{#FileTag}-windows-x64-setup
#endif

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop icon"; Flags: unchecked

[Files]
Source: "{#Package}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

#ifndef PayloadTest
[Icons]
Name: "{userprograms}\{#AppTitle}"; Filename: "{app}\Takedawn.exe"; WorkingDir: "{app}"
Name: "{userdesktop}\{#AppTitle}"; Filename: "{app}\Takedawn.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[UninstallDelete]
Type: filesandordirs; Name: "{app}.prev"

[Run]
Filename: "{app}\Takedawn.exe"; WorkingDir: "{app}"; Description: "Start Takedawn"; Flags: nowait postinstall skipifsilent
#endif

[Code]
const
  UninstKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{{#AppGuid}}_is1';
  NewVersion = '{#Version}';

var
  SwapApp, SwapPrev: String;
  Swapped, InstallDone: Boolean;
  WasUpdate: Boolean;  // decided before the files are written

// The leading digits of the next dotted part, as a number ("1-rc1" is 1).
function TakePart(var S: String): Integer;
var
  P, N: Integer;
  Part: String;
begin
  P := Pos('.', S);
  if P = 0 then begin Part := S; S := ''; end
  else begin Part := Copy(S, 1, P - 1); Delete(S, 1, P); end;
  N := 0;
  while (Part <> '') and (Part[1] >= '0') and (Part[1] <= '9') do
  begin
    N := N * 10 + Ord(Part[1]) - Ord('0');
    Delete(Part, 1, 1);
  end;
  Result := N;
end;

function CompareVersions(A, B: String): Integer;
var
  X, Y: Integer;
begin
  Result := 0;
  while (Result = 0) and ((A <> '') or (B <> '')) do
  begin
    X := TakePart(A);
    Y := TakePart(B);
    if X < Y then Result := -1 else if X > Y then Result := 1;
  end;
end;

function InstallDir(): String;
begin
  Result := RemoveBackslashUnlessRoot(WizardDirValue);
end;

function IsUpdate(): Boolean;
begin
  Result := FileExists(InstallDir() + '\Takedawn.exe');
end;

// The version that is installed there: the uninstall entry's, else the
// program's own (the payload-test variant has no entry).
function InstalledVersion(): String;
var
  V, Path: String;
begin
  Result := '';
  if RegQueryStringValue(HKCU, UninstKey, 'DisplayVersion', V) and
     RegQueryStringValue(HKCU, UninstKey, 'Inno Setup: App Path', Path) and
     (CompareText(RemoveBackslashUnlessRoot(Path), InstallDir()) = 0) then
    Result := V
  else if GetVersionNumbersString(InstallDir() + '\Takedawn.exe', V) then
  begin
    while (Length(V) > 4) and (Copy(V, Length(V) - 1, 2) = '.0') do
      Delete(V, Length(V) - 1, 2);
    Result := V;
  end;
  if Result = '' then Result := 'an unknown version';
end;

function UpdateTitle(): String;
var
  Old: String;
begin
  Old := InstalledVersion();
  case CompareVersions(Old, NewVersion) of
    0: Result := 'Reinstall Takedawn ' + NewVersion;
    1: Result := 'Downgrade Takedawn ' + Old + ' to ' + NewVersion;
  else
    Result := 'Update Takedawn ' + Old + ' to ' + NewVersion;
  end;
end;

procedure InitializeWizard();
begin
  if IsUpdate() then WizardForm.Caption := UpdateTitle();
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := (PageID = wpReady) and not IsUpdate();
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if (CurPageID = wpFinished) and WasUpdate then
    WizardForm.FinishedHeadingLabel.Caption := 'Takedawn was updated to ' + NewVersion;
  if not IsUpdate() then Exit;
  if CurPageID = wpReady then
  begin
    WizardForm.PageNameLabel.Caption := UpdateTitle();
    WizardForm.PageDescriptionLabel.Caption := 'Takedawn is installed already.';
    WizardForm.NextButton.Caption := 'Update';
    WizardForm.ReadyLabel.Caption := 'Click Update to continue, or Back to change the folder.';
  end;
end;

function UpdateReadyMemo(Space, NewLine, MemoUserInfoInfo, MemoDirInfo, MemoTypeInfo,
  MemoComponentsInfo, MemoGroupInfo, MemoTasksInfo: String): String;
begin
  if not IsUpdate() then
  begin
    Result := MemoDirInfo;
    Exit;
  end;
  Result := UpdateTitle() + NewLine + NewLine +
    'In the folder:' + NewLine + Space + InstallDir() + NewLine + NewLine +
    'Kept as they are (they are not in that folder):' + NewLine +
    Space + 'your saves and settings,' + NewLine +
    Space + 'the game data installed from your disc,' + NewLine +
    Space + 'the shader cache.' + NewLine + NewLine +
    'The program files are replaced. The version being replaced' + NewLine +
    'stays in ' + InstallDir() + '.prev' + NewLine +
    'until the next update.' + NewLine + NewLine +
    'Takedawn must be closed. If the update does not finish,' + NewLine +
    'the old version is put back.';
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if (CurPageID = wpReady) and IsUpdate() and (CompareVersions(InstalledVersion(), NewVersion) > 0) then
    Result := MsgBox('Takedawn ' + InstalledVersion() + ' is installed; this setup is the older ' + NewVersion + '. Install it anyway?',
                     mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES;
end;

// A program or library that is loaded cannot be opened for writing. Appending
// nothing opens the file that way and changes nothing in it.
function InUse(const Path: String): Boolean;
begin
  Result := FileExists(Path) and not SaveStringToFile(Path, '', True);
end;

// Before a file is written: see that Takedawn is closed, then move the program
// folder aside. (Windows lets a folder be renamed while a program from it
// runs, as long as that program does not stand in it; hence the explicit check
// first. The rename also fails if anything else has the folder open.)
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  App: String;
begin
  Result := '';
  App := RemoveBackslashUnlessRoot(ExpandConstant('{app}'));
  WasUpdate := FileExists(App + '\Takedawn.exe');
  if not WasUpdate then Exit;
  if InUse(App + '\Takedawn.exe') or InUse(App + '\takedawn-setup.exe') or InUse(App + '\SDL2.dll') then
  begin
    Result := 'Takedawn cannot be updated while it is running. Close Takedawn (and its setup window), then try again. Nothing was changed.';
    Exit;
  end;
  if DirExists(App + '.prev') and not DelTree(App + '.prev', True, True, True) then
  begin
    Result := 'The older copy ' + App + '.prev could not be removed. Close anything that has it open (Explorer, a terminal), then try again. Nothing was changed.';
    Exit;
  end;
  if not RenameFile(App, App + '.prev') then
  begin
    Result := 'Takedawn cannot be updated while it is running. Close Takedawn (and its setup window), then try again. Nothing was changed.';
    Exit;
  end;
  SwapApp := App;
  SwapPrev := App + '.prev';
  Swapped := True;
#ifdef PayloadTest
  // /TESTFAIL=1: a folder where Takedawn.exe has to go, so that writing it
  // fails the way a locked or unwritable file would.
  if ExpandConstant('{param:TESTFAIL|0}') = '1' then ForceDirectories(App + '\Takedawn.exe');
#endif
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then InstallDone := True;
end;

// The installation did not finish (an error, Cancel, a failed copy): Setup has
// taken back what it wrote; put the previous folder in its place.
procedure DeinitializeSetup();
begin
  if Swapped and not InstallDone then
  begin
    DelTree(SwapApp, True, True, True);
    RenameFile(SwapPrev, SwapApp);
  end;
end;

#ifndef PayloadTest
// The folder chosen for the game data in the setup window (BO-197): the first
// line of data-folder.txt, or '' for the default.
function ChosenDataFolder(): String;
var
  Lines: TArrayOfString;
begin
  Result := '';
  if LoadStringsFromFile(ExpandConstant('{localappdata}') + '\{#AppTitle}\data-folder.txt', Lines) and
     (GetArrayLength(Lines) > 0) then
    Result := Trim(Lines[0]);
end;

// The uninstaller removes the program only; say where the rest is.
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Chosen: String;
begin
  if (CurUninstallStep = usDone) and not UninstallSilent then
  begin
    Chosen := ChosenDataFolder();
    if Chosen <> '' then
      Chosen := Chosen + '  (game data, the folder you chose)' + #13#10;
    MsgBox('Takedawn was removed.' + #13#10#13#10 +
           'The game data installed from your disc, your saves and your settings were not removed:' + #13#10 +
           Chosen +
           ExpandConstant('{localappdata}') + '\{#AppTitle}  (game data, shader cache)' + #13#10 +
           ExpandConstant('{userappdata}') + '\{#AppTitle}  (saves, settings, logs)' + #13#10#13#10 +
           'Delete those folders yourself if you want them gone.',
           mbInformation, MB_OK);
  end;
end;
#endif
