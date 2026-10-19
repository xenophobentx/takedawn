# remote.ps1: the Windows half of tools/release/build-windows.sh, run on the
# Windows build machine inside the one folder the release may use there.
#
#   powershell -ExecutionPolicy Bypass -File remote.ps1 -Dir <folder> -Version <x.y.z> [-Disc <folder-or-image>]
#
# <folder> holds what build-windows.sh copied: Takedawn\ (the program
# folder), takedawn.iss, takedawn.ico. This script
#   1. runs Takedawn.exe --version                      -> version.txt
#   2. runs Takedawn.exe --install <disc> --check       -> check.txt  (with -Disc)
#   3. finds Inno Setup's compiler in <folder>\tools\innosetup, fetching the
#      pinned version in its portable mode when it is not there (no registry
#      entry, no uninstaller, no Start menu entry; the download is kept in
#      <folder>\tools too)
#   4. compiles the setup exe                           -> out\
#   5. compiles the payload-test variant, installs it into <folder>\itest,
#      compares every installed file with the program folder, runs the
#      installed Takedawn.exe --version, removes itest
# Nothing outside <folder> is written (the setup's own temporary files in
# %TEMP% are removed by it).
param(
    [Parameter(Mandatory = $true)][string]$Dir,
    [Parameter(Mandatory = $true)][string]$Version,
    [string]$Disc = "",
    [string]$Region = "NTSC-U",
    [string]$InnoUrl = "https://github.com/jrsoftware/issrc/releases/download/is-6_7_3/innosetup-6.7.3.exe",
    [string]$InnoSha256 = "9c73c3bae7ed48d44112a0f48e66742c00090bdb5bef71d9d3c056c66e97b732"
)
$ErrorActionPreference = "Stop"
$pkg = Join-Path $Dir "Takedawn"
$exe = Join-Path $pkg "Takedawn.exe"
$out = Join-Path $Dir "out"
New-Item -ItemType Directory -Force $out | Out-Null

# The program is run only with a scratch profile: it keeps saves, settings,
# game data and the shader cache under %APPDATA% and %LOCALAPPDATA% (it reads
# the two variables), and a start or an install would move or make folders in
# the real ones. The first release build's check here moved the machine
# owner's save folder; this is why.
$realRoaming = [Environment]::GetFolderPath('ApplicationData')
$realLocal = [Environment]::GetFolderPath('LocalApplicationData')
$scratch = Join-Path $Dir "profile"

function Run-Captured([string]$file, [string[]]$arguments, [string]$to, [string]$profile = $scratch) {
    $roaming = Join-Path $profile "Roaming"; $local = Join-Path $profile "Local"
    New-Item -ItemType Directory -Force $roaming, $local | Out-Null
    if ((Resolve-Path $roaming).Path -eq $realRoaming -or (Resolve-Path $local).Path -eq $realLocal -or
        -not (Resolve-Path $profile).Path.StartsWith((Resolve-Path $Dir).Path)) {
        throw "refusing to run $file without a scratch profile inside $Dir"
    }
    $keepR = $env:APPDATA; $keepL = $env:LOCALAPPDATA
    $env:APPDATA = $roaming; $env:LOCALAPPDATA = $local
    try {
        # A window program writes to the pipes it is given, not to this console.
        $p = Start-Process -FilePath $file -ArgumentList $arguments -NoNewWindow -Wait -PassThru `
             -RedirectStandardOutput $to -RedirectStandardError "$to.err"
        return $p.ExitCode
    } finally { $env:APPDATA = $keepR; $env:LOCALAPPDATA = $keepL }
}

# The folders of the port's earlier name are moved by a game start that has
# its data and by a real --install, by nothing else (src/game/install.h;
# tools/release/test-user-dirs.sh is the same test for the other systems and
# for Git Bash here).
function Test-UserDirs([string]$program) {
    $prof = Join-Path $Dir "profile-dirs"
    if (Test-Path $prof) { Remove-Item -Recurse -Force $prof }
    $oldSave = Join-Path $prof "Roaming\Burnout3"; $newSave = Join-Path $prof "Roaming\Takedawn"
    $oldCache = Join-Path $prof "Local\xboxrecomp\cache"; $newCache = Join-Path $prof "Local\Takedawn\cache"
    $oldData = Join-Path $prof "Local\Burnout3\Game"
    New-Item -ItemType Directory -Force (Join-Path $oldSave "UDATA"), $oldCache, $oldData, (Join-Path $prof "notadisc") | Out-Null
    Set-Content (Join-Path $oldSave "settings.toml") 'saved = "old"'
    Set-Content (Join-Path $oldSave "UDATA\profile.bin") 'save'
    Set-Content (Join-Path $oldCache "vk_pipelines.bin") 'cache'
    Set-Content (Join-Path $oldData "readme.txt") 'data'
    Set-Content (Join-Path $prof "notadisc\file.txt") 'x'
    $log = Join-Path $prof "out.txt"
    $src = "`"$(Join-Path $prof 'notadisc')`""
    $queries = @(@("--version"), @("--help"), @("--data-status"), @("--install", $src, "--check"),
                 @("--bug-report", "--out", "`"$prof`""), @("--no-setup"), @("--build-shaders", "--status"))
    foreach ($q in $queries) {
        $null = Run-Captured $program $q $log $prof
        if (-not (Test-Path (Join-Path $oldSave "settings.toml")) -or -not (Test-Path (Join-Path $oldCache "vk_pipelines.bin")) -or
            -not (Test-Path (Join-Path $oldData "readme.txt")) -or (Test-Path (Join-Path $newSave "settings.toml")) -or
            (Test-Path (Join-Path $newCache "vk_pipelines.bin"))) { throw "$($q -join ' ') moved a folder of the old name" }
        if ($q[0] -eq "--data-status" -and -not (Select-String -Path $log -Pattern '^LOG data pending-move .*Burnout3 -> ' -Quiet)) {
            throw "--data-status does not say a move is pending"
        }
    }
    $null = Run-Captured $program @("--install", $src) $log $prof     # no disc: nothing is copied, the folders move
    if ((Get-Content (Join-Path $newSave "settings.toml") -ErrorAction SilentlyContinue) -ne 'saved = "old"' -or
        -not (Test-Path (Join-Path $newSave "UDATA\profile.bin")) -or -not (Test-Path (Join-Path $newCache "vk_pipelines.bin")) -or
        -not (Test-Path (Join-Path $prof "Local\Takedawn\Game\readme.txt")) -or (Test-Path $oldSave)) {
        throw "a real --install did not move the folders of the old name"
    }
    Remove-Item -Recurse -Force $prof
    Write-Host "STEP userdirs OK $($queries.Count) queries moved nothing, a real --install moved them"
}

$rc = Run-Captured $exe @("--version") (Join-Path $Dir "version.txt")
if ($rc -ne 0) { throw "Takedawn.exe --version ended with status $rc" }
Write-Host "STEP version OK"
Test-UserDirs $exe

if ($Disc -ne "") {
    $rc = Run-Captured $exe @("--install", "`"$Disc`"", "--check") (Join-Path $Dir "check.txt")
    if ($rc -ne 0) { throw "Takedawn.exe --install --check ended with status $rc" }
    Write-Host "STEP check OK"
}

$inno = Join-Path $Dir "tools\innosetup"
$iscc = Join-Path $inno "ISCC.exe"
if (-not (Test-Path $iscc)) {
    New-Item -ItemType Directory -Force (Join-Path $Dir "tools") | Out-Null
    $dl = Join-Path $Dir "tools\innosetup-setup.exe"
    Invoke-WebRequest -UseBasicParsing -Uri $InnoUrl -OutFile $dl
    $got = (Get-FileHash $dl -Algorithm SHA256).Hash.ToLower()
    if ($got -ne $InnoSha256) { Remove-Item $dl; throw "Inno Setup download has sha256 $got, expected $InnoSha256" }
    Start-Process -Wait -FilePath $dl -ArgumentList "/PORTABLE=1", "/CURRENTUSER", "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/SP-", "/DIR=`"$inno`""
}
if (-not (Test-Path $iscc)) { throw "ISCC.exe was not found in $inno" }

$iss = Join-Path $Dir "takedawn.iss"
$common = @("/Q", "/DPackage=$pkg", "/DVersion=$Version", "/DIcon=$(Join-Path $Dir 'takedawn.ico')", "/O$out")
$tag = ""
if ($Region -eq "PAL") { $common += "/DRegion=PAL"; $tag = "-pal" }
& $iscc @common $iss
if ($LASTEXITCODE -ne 0) { throw "ISCC failed with status $LASTEXITCODE" }
Write-Host "STEP setup OK"
& $iscc @common "/DPayloadTest" $iss
if ($LASTEXITCODE -ne 0) { throw "ISCC (payload test) failed with status $LASTEXITCODE" }

$itest = Join-Path $Dir "itest"
if (Test-Path $itest) { Remove-Item -Recurse -Force $itest }
$test = Join-Path $out "Takedawn-$Version$tag-windows-x64-payloadtest.exe"
$p = Start-Process -Wait -PassThru -FilePath $test -ArgumentList "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/SP-", "/CURRENTUSER", "/DIR=`"$itest`"", "/LOG=`"$(Join-Path $Dir 'itest.log')`""
if ($p.ExitCode -ne 0) { throw "the payload-test setup ended with status $($p.ExitCode)" }
$n = 0
Get-ChildItem -Recurse -File $pkg | ForEach-Object {
    $rel = $_.FullName.Substring($pkg.Length + 1)
    $there = Join-Path $itest $rel
    if (-not (Test-Path $there)) { throw "not installed: $rel" }
    if ((Get-FileHash $_.FullName).Hash -ne (Get-FileHash $there).Hash) { throw "installed file differs: $rel" }
    $n++
}
$extra = @(Get-ChildItem -Recurse -File $itest).Count - $n
if ($extra -ne 0) { throw "$extra files installed that are not in the program folder" }
$rc = Run-Captured (Join-Path $itest "Takedawn.exe") @("--version") (Join-Path $Dir "version-installed.txt")
if ($rc -ne 0) { throw "the installed Takedawn.exe --version ended with status $rc" }
Remove-Item -Recurse -Force $itest
Remove-Item -Force $test
Write-Host "STEP payload OK $n files"
Get-FileHash (Join-Path $out "Takedawn-$Version$tag-windows-x64-setup.exe") -Algorithm SHA256 | ForEach-Object { Write-Host "RESULT OK $($_.Hash.ToLower())" }
