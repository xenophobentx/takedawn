"""Windows backend of the installer: portable tools, build, package, shortcuts.

Runs natively on Windows. Nothing is installed system-wide and no PATH is
changed: llvm-mingw, CMake, Ninja, SDL2, the Vulkan headers and glslang live in
one tools directory (`--tools-dir`, default <root>/tools), found there when they
are, fetched there when they are not (https, sha256 pinned in pins.json), and are
put on PATH for the processes the installer starts, not for the system.

The build is the repo's Windows (SDL2 + Vulkan, llvm-mingw) build, docs/windows.md
and tools/win/build-mingw.sh, done natively: same toolchain file and options,
with the pieces tools/win/fetch-deps.sh fetches (including glslang built static
with the same llvm-mingw) done here in Python. The user's .ico is embedded in
the exe by linking a windres object through CMAKE_EXE_LINKER_FLAGS, so the
CMake files need no change.

Python: the driver runs under some Python (the installer package carries an
embeddable one); capstone, the recompiler's one dependency, is taken from its
PyPI wheel (a zip) into <root>/install/site and put on sys.path by regen.py.
"""

import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile
import time
import urllib.request
import zipfile

from common import EXIT_TOOLCHAIN, StepError, capture, check_location, run

HERE = os.path.dirname(os.path.abspath(__file__))
TRIPLE = "x86_64-w64-mingw32"
SHORTCUT = "Burnout 3 Takedown.lnk"
NINJA_PROGRESS = re.compile(r"^\[(\d+)/(\d+)\]")
CAPSTONE = "5.0.9"
UA = {"User-Agent": "burnout3-installer"}


def pins():
    with open(os.path.join(HERE, "pins.json")) as f:
        return json.load(f)


# ----------------------------------------------------------------- fetching
def download(url, dest, sha256, progress=None):
    """Fetch url to dest (via .part), refusing a file whose sha256 differs."""
    if os.path.isfile(dest) and (not sha256 or hashlib.sha256(open(dest, "rb").read()).hexdigest() == sha256):
        return
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    tmp = dest + ".part"
    h = hashlib.sha256()
    req = urllib.request.Request(url, headers=UA)
    with urllib.request.urlopen(req, timeout=60) as r, open(tmp, "wb") as f:
        total = int(r.headers.get("Content-Length") or 0)
        got = 0
        while True:
            b = r.read(1 << 20)
            if not b:
                break
            f.write(b)
            h.update(b)
            got += len(b)
            if progress and total:
                progress(got / total)
    if sha256 and h.hexdigest() != sha256:
        os.remove(tmp)
        raise StepError("download-corrupt", "%s has sha256 %s, expected %s" % (url, h.hexdigest(), sha256),
                        EXIT_TOOLCHAIN)
    os.replace(tmp, dest)


def extract(archive, dest, strip=0):
    """Unpack a .zip or .tar.gz into dest, dropping `strip` leading path parts; no
    member may land outside dest."""
    dest = os.path.abspath(dest)
    os.makedirs(dest, exist_ok=True)

    def target(name):
        parts = [p for p in name.replace("\\", "/").split("/") if p][strip:]
        if not parts:
            return None
        t = os.path.abspath(os.path.join(dest, *parts))
        if t != dest and not t.startswith(dest + os.sep):
            raise StepError("unsafe-archive", "%s contains %s, outside its folder" % (archive, name))
        return t

    if archive.endswith(".zip") or archive.endswith(".whl"):
        with zipfile.ZipFile(archive) as z:
            for m in z.infolist():
                t = target(m.filename)
                if t is None:
                    continue
                if m.is_dir():
                    os.makedirs(t, exist_ok=True)
                else:
                    os.makedirs(os.path.dirname(t), exist_ok=True)
                    with z.open(m) as src, open(t, "wb") as out:
                        shutil.copyfileobj(src, out)
    else:
        with tarfile.open(archive) as z:
            for m in z.getmembers():
                t = target(m.name)
                if t is None or not (m.isfile() or m.isdir()):
                    continue                              # no links or devices from an archive
                if m.isdir():
                    os.makedirs(t, exist_ok=True)
                else:
                    os.makedirs(os.path.dirname(t), exist_ok=True)
                    with z.extractfile(m) as src, open(t, "wb") as out:
                        shutil.copyfileobj(src, out)


# ------------------------------------------------------------------- layout
def paths(ctx):
    t = ctx.tools_dir
    return {
        "llvm": os.path.join(t, "llvm-mingw"),
        "cmake": os.path.join(t, "cmake"),
        "ninja": os.path.join(t, "ninja"),
        "sdl2": os.path.join(t, "SDL2", TRIPLE),
        "vulkan": os.path.join(t, "vulkan"),
        "glslang": os.path.join(t, "glslang"),
    }


def tool_exe(ctx, name):
    """Full path of a portable tool: Windows looks a bare name up on the parent's PATH, not the child's."""
    p = paths(ctx)
    return {"cmake": os.path.join(p["cmake"], "bin", "cmake.exe"), "ninja": os.path.join(p["ninja"], "ninja.exe")}[name]


def tool_env(ctx):
    """The environment for the tools: portable bin directories first on PATH, for
    these processes only."""
    p = paths(ctx)
    env = dict(ctx.env)
    env["PATH"] = os.pathsep.join([os.path.join(p["llvm"], "bin"), os.path.join(p["cmake"], "bin"),
                                   p["ninja"], env.get("PATH", "")])
    return env


MARKERS = {
    "llvm-mingw": lambda p: os.path.join(p["llvm"], "bin", TRIPLE + "-clang.exe"),
    "cmake": lambda p: os.path.join(p["cmake"], "bin", "cmake.exe"),
    "ninja": lambda p: os.path.join(p["ninja"], "ninja.exe"),
    "sdl2": lambda p: os.path.join(p["sdl2"], "bin", "SDL2.dll"),
    "vulkan-headers": lambda p: os.path.join(p["vulkan"], "lib", "libvulkan-1.dll.a"),
    "glslang": lambda p: os.path.join(p["glslang"], "lib", "libglslang.a"),
}


def tools(ctx, need):
    """The Windows part of the toolchain step: make sure every portable tool is in
    the tools directory (fetching what is missing unless --no-fetch)."""
    pin, p = pins(), paths(ctx)
    if len(ctx.work) > 140:                      # tested to 138; the deepest build file adds ~90
        ctx.out.warn("toolchain", "the work folder path is %d characters long; Windows paths over 260 break "
                                  "the build, keep it under about 140" % len(ctx.work))
    dl = os.path.join(ctx.tools_dir, "dl")
    ok, detail = check_location(ctx.tools_dir, 3)       # before anything is fetched; the folder may not exist yet
    need("tools-space", ok, detail, "the tools folder needs to be writable with 3 GB free; pick another --tools-dir (%s)" % detail)
    if not ok:
        return

    def fetch(name, dest, strip):
        e = pin[name]
        arc = os.path.join(dl, os.path.basename(e["url"]))
        ctx.out.log("toolchain", "fetching %s (%.0f MB) from %s" % (name, e["size"] / 1e6, e["url"]))
        download(e["url"], arc, e["sha256"], lambda f: ctx.out.progress("toolchain", f * 0.9, "fetching " + name))
        extract(arc, dest, strip)

    plan = (("llvm-mingw", p["llvm"], 1), ("cmake", p["cmake"], 1), ("ninja", p["ninja"], 0),
            ("sdl2", os.path.dirname(p["sdl2"]), 1))
    for name, dest, strip in plan:
        marker = MARKERS[name](p)
        if not os.path.isfile(marker) and not ctx.no_fetch:
            try:
                fetch(name, dest, strip)
            except (OSError, StepError) as e:
                need(name, False, "", "download failed (%s); put it in %s by hand" % (e, dest))
                continue
        need(name, os.path.isfile(marker), marker, "run without --no-fetch, or unpack it into " + dest)
    if os.path.isfile(MARKERS["llvm-mingw"](p)) and not os.path.isfile(MARKERS["vulkan-headers"](p)) \
            and not ctx.no_fetch:
        try:
            build_vulkan(ctx, fetch, dl)
        except (OSError, StepError, subprocess.SubprocessError) as e:
            need("vulkan-headers", False, "", "could not set up the Vulkan headers: %s" % e)
    elif os.path.isfile(MARKERS["vulkan-headers"](p)):
        pass
    need("vulkan-headers", os.path.isfile(MARKERS["vulkan-headers"](p)), p["vulkan"], "needs llvm-mingw first")
    if all(os.path.isfile(MARKERS[k](p)) for k in ("llvm-mingw", "cmake", "ninja")) \
            and not os.path.isfile(MARKERS["glslang"](p)) and not ctx.no_fetch:
        try:
            build_glslang(ctx, fetch, dl)
        except (OSError, StepError) as e:
            need("glslang", False, "", "could not build glslang: %s" % e)
    need("glslang", os.path.isfile(MARKERS["glslang"](p)), p["glslang"], "fetched and built by the installer")


def build_vulkan(ctx, fetch, dl):
    """Vulkan headers and an import library for vulkan-1.dll (the loader is the GPU
    driver's): tools/win/fetch-deps.sh does the same with grep and dlltool."""
    p = paths(ctx)
    src = os.path.join(p["vulkan"], "src")
    shutil.rmtree(p["vulkan"], ignore_errors=True)
    fetch("vulkan-headers", src, 1)
    inc = os.path.join(p["vulkan"], "include")
    os.makedirs(os.path.join(p["vulkan"], "lib"))
    for d in ("vulkan", "vk_video"):
        shutil.copytree(os.path.join(src, "include", d), os.path.join(inc, d))
    names = set()
    for h in ("vulkan_core.h", "vulkan_win32.h"):
        names |= set(re.findall(r"VKAPI_CALL +(vk[A-Za-z0-9_]+)", open(os.path.join(inc, "vulkan", h)).read()))
    defp = os.path.join(p["vulkan"], "lib", "vulkan-1.def")
    with open(defp, "w") as f:
        f.write("LIBRARY vulkan-1.dll\nEXPORTS\n" + "\n".join(sorted(names)) + "\n")
    rc = run(ctx, "toolchain", [os.path.join(p["llvm"], "bin", TRIPLE + "-dlltool.exe"), "-m", "i386:x86-64",
                                "-d", defp, "-l", os.path.join(p["vulkan"], "lib", "libvulkan-1.dll.a")],
             env=tool_env(ctx), nice=False)
    if rc != 0:
        raise StepError("vulkan-lib-failed", "dlltool could not make the vulkan-1 import library", EXIT_TOOLCHAIN)


def build_glslang(ctx, fetch, dl):
    """glslang built static with llvm-mingw (no SPIRV-Tools optimizer), as fetch-deps.sh."""
    p = paths(ctx)
    src = os.path.join(ctx.tools_dir, "glslang-src")
    bld = os.path.join(ctx.tools_dir, "glslang-build")
    shutil.rmtree(src, ignore_errors=True)
    fetch("glslang", src, 1)
    env = tool_env(ctx)
    f = lambda x: x.replace("\\", "/")
    cfg = [tool_exe(ctx, "cmake"), "-S", f(src), "-B", f(bld), "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
           "-DCMAKE_TOOLCHAIN_FILE=" + f(os.path.join(ctx.work, "cmake", "llvm-mingw-x86_64.cmake")),
           "-DLLVM_MINGW_ROOT=" + f(p["llvm"]), "-DCMAKE_INSTALL_PREFIX=" + f(p["glslang"]),
           "-DBUILD_SHARED_LIBS=OFF", "-DENABLE_OPT=OFF", "-DENABLE_GLSLANG_BINARIES=OFF",
           "-DENABLE_CTEST=OFF", "-DBUILD_EXTERNAL=OFF"]
    ctx.out.log("toolchain", "building glslang (static, once)")
    for step, cmd in (("configure", cfg), ("build", [tool_exe(ctx, "cmake"), "--build", f(bld), "-j", str(ctx.jobs)]),
                      ("install", [tool_exe(ctx, "cmake"), "--install", f(bld)])):
        if run(ctx, "toolchain", cmd, env=env) != 0:
            raise StepError("glslang-failed", "glslang %s failed; see %s" % (step, os.path.join(ctx.logs, "toolchain.log")),
                            EXIT_TOOLCHAIN)
    shutil.rmtree(src, ignore_errors=True)
    shutil.rmtree(bld, ignore_errors=True)


def ensure_capstone(ctx):
    """capstone for the recompiler: importable already, or unpacked from its PyPI
    wheel into <root>/install/site. Returns the python to use and sets ctx.py_site."""
    py = ctx.python or sys.executable
    site = os.path.join(ctx.inst, "site")

    def has(extra):
        env = dict(ctx.env, BO3_SITE=extra or "")
        code = "import sys, os; s = os.environ.get('BO3_SITE'); sys.path[:0] = [s] if s else []; import capstone"
        return subprocess.run([py, "-c", code], env=env, stdout=subprocess.DEVNULL,
                              stderr=subprocess.DEVNULL).returncode == 0

    if has(None):
        return py
    if not has(site):
        if ctx.no_pip:
            raise StepError("missing-capstone", "python-capstone is missing (--no-pip given)", EXIT_TOOLCHAIN)
        ctx.out.log("toolchain", "fetching capstone %s from PyPI into %s" % (CAPSTONE, site))
        url = "https://pypi.org/pypi/capstone/%s/json" % CAPSTONE
        with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=60) as r:
            files = json.load(r)["urls"]
        whl = next((f for f in files if f["filename"].endswith("win_amd64.whl") and "py3-none" in f["filename"]), None) \
            or next((f for f in files if f["filename"].endswith("win_amd64.whl")), None)
        if whl is None:
            raise StepError("pip-failed", "no Windows wheel of capstone %s on PyPI" % CAPSTONE, EXIT_TOOLCHAIN)
        arc = os.path.join(ctx.tools_dir, "dl", whl["filename"])
        download(whl["url"], arc, whl["digests"]["sha256"])
        shutil.rmtree(site, ignore_errors=True)
        extract(arc, site, 0)
        if not has(site):
            raise StepError("pip-failed", "capstone from %s does not import with this Python" % whl["filename"],
                            EXIT_TOOLCHAIN)
    ctx.py_site = site
    return py


# --------------------------------------------------------------------- build
def build(ctx, ninja_line):
    """Configure and build with the portable tools; returns the exe path."""
    p, env = paths(ctx), None
    env = tool_env(ctx)
    build_dir = os.path.join(ctx.work, "build", "win")
    bin_dir = os.path.join(build_dir, "bin")
    os.makedirs(bin_dir, exist_ok=True)
    f = lambda x: x.replace("\\", "/")
    # The user's icon in the exe: a resource object on the link line.
    ico = os.path.join(ctx.icons_dir, "AppIcon.ico")
    rc_file = os.path.join(ctx.inst, "icon.rc")
    obj = os.path.join(ctx.inst, "icon.o")
    with open(rc_file, "w") as fh:
        fh.write('1 ICON "%s"\n' % f(ico))
    if run(ctx, "build", [os.path.join(p["llvm"], "bin", TRIPLE + "-windres.exe"), rc_file, "-O", "coff", "-o", obj],
           env=env, nice=False) != 0:
        raise StepError("icon-embed-failed", "windres could not compile the icon resource")
    cfg = [tool_exe(ctx, "cmake"), "-S", f(ctx.work), "-B", f(build_dir), "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
           "-DCMAKE_TOOLCHAIN_FILE=" + f(os.path.join(ctx.work, "cmake", "llvm-mingw-x86_64.cmake")),
           "-DLLVM_MINGW_ROOT=" + f(p["llvm"]), "-DXBOXRECOMP_DIR=" + f(ctx.toolkit),
           "-DCMAKE_PREFIX_PATH=%s;%s" % (f(p["sdl2"]), f(p["glslang"])),
           # On Windows CMake does not find packages through CMAKE_PREFIX_PATH once the
           # toolchain file has set CMAKE_FIND_ROOT_PATH (drive-letter paths are not
           # re-rooted), so the package directories are named outright.
           "-DSDL2_DIR=" + f(os.path.join(p["sdl2"], "lib", "cmake", "SDL2")),
           "-Dglslang_DIR=" + f(os.path.join(p["glslang"], "lib", "cmake", "glslang")),
           "-DVulkan_INCLUDE_DIR=" + f(os.path.join(p["vulkan"], "include")),
           "-DVulkan_LIBRARY=" + f(os.path.join(p["vulkan"], "lib", "libvulkan-1.dll.a")),
           "-DBO3_BIN_DIR=" + f(bin_dir), "-DBO3_REGION=" + ctx.region,
           # linker flags are split at spaces, so a path with one is quoted
           "-DCMAKE_EXE_LINKER_FLAGS=" + ('"%s"' % f(obj) if " " in obj else f(obj))]
    if run(ctx, "build", cfg, cwd=ctx.work, env=env, nice=False) != 0:
        raise StepError("configure-failed", "cmake could not configure the build")
    rc = run(ctx, "build", [tool_exe(ctx, "ninja"), "-C", build_dir, "-j%d" % ctx.jobs, "burnout3"], cwd=ctx.work, env=env,
             on_line=ninja_line, nice=False)
    if rc != 0:
        raise StepError("build-failed", "ninja failed with status %d" % rc)
    exe = os.path.join(bin_dir, "burnout3.exe")
    if not os.path.isfile(exe):
        raise StepError("build-failed", "the build finished but %s is not there" % exe)
    shutil.copy2(os.path.join(p["sdl2"], "bin", "SDL2.dll"), os.path.join(bin_dir, "SDL2.dll"))
    return exe


# ------------------------------------------------------------------- package
UNINSTALL = """Burnout 3: Takedown - removing it

Nothing was installed system-wide: no registry entries, no services, no PATH changes.
To remove everything, delete:

  1. this folder:                 {app}
  2. the game data and settings:  {root}
     (the disc's files, enhance.toml and the installer's state; saves are NOT in here)
  3. the shortcuts, if you installed them: "Burnout 3 Takedown" in the Start menu and on the Desktop
  4. the build tools (only if nothing else uses them):  {tools}

Your saves and profile are kept apart in %APPDATA%\\Burnout3. Delete that folder too only if you
want to lose them.
"""


def make_shortcut(ctx, lnk, exe, workdir, ico):
    """A .lnk through PowerShell's WScript.Shell (no extra tools)."""
    q = lambda s: s.replace("'", "''")
    script = ("$s = (New-Object -ComObject WScript.Shell).CreateShortcut('%s'); $s.TargetPath = '%s'; "
              "$s.WorkingDirectory = '%s'; $s.IconLocation = '%s'; $s.Description = 'Burnout 3: Takedown'; $s.Save()"
              % (q(lnk), q(exe), q(workdir), q(ico)))
    rc = run(ctx, "package", ["powershell", "-NoProfile", "-NonInteractive", "-Command", script], nice=False)
    if rc != 0 or not os.path.isfile(lnk):
        raise StepError("shortcut-failed", "could not create " + lnk)


def package(ctx, binary):
    app = ctx.app_dir
    os.makedirs(app, exist_ok=True)
    exe = os.path.join(app, "burnout3.exe")
    shutil.copy2(binary, exe)
    shutil.copy2(os.path.join(os.path.dirname(binary), "SDL2.dll"), os.path.join(app, "SDL2.dll"))
    ico = os.path.join(app, "burnout3.ico")
    shutil.copy2(os.path.join(ctx.icons_dir, "AppIcon.ico"), ico)
    cfg = os.path.join(app, "enhance.toml")
    default_cfg = os.path.join(ctx.work, "config", "enhance.toml.default")
    if not os.path.exists(cfg) and os.path.isfile(default_cfg):      # the exe reads it from beside itself
        shutil.copy2(default_cfg, cfg)
    with open(os.path.join(app, "uninstall.txt"), "w") as f:
        f.write(UNINSTALL.format(app=app, root=ctx.root, tools=ctx.tools_dir))
    # The exe runs from the folder that holds "Burnout 3 Takedown\".
    make_shortcut(ctx, os.path.join(app, SHORTCUT), exe, ctx.game_parent, ico)


def shortcuts(ctx):
    """Opt-in: copy the .lnk to the Start menu and the Desktop. Returns the paths."""
    src = os.path.join(ctx.app_dir, SHORTCUT)
    if not os.path.isfile(src):
        raise StepError("no-package", "no %s: run the package step first" % src)
    start = ctx.start_menu_dir or os.path.join(os.environ.get("APPDATA", ""), "Microsoft", "Windows",
                                               "Start Menu", "Programs")
    desk = ctx.desktop_dir
    if not desk:
        rc, out = capture(["powershell", "-NoProfile", "-NonInteractive", "-Command",
                           "[Environment]::GetFolderPath('Desktop')"])
        desk = out.strip() if rc == 0 and out.strip() else os.path.join(os.path.expanduser("~"), "Desktop")
    done = []
    for d in (start, desk):
        os.makedirs(d, exist_ok=True)
        shutil.copy2(src, os.path.join(d, SHORTCUT))
        done.append(os.path.join(d, SHORTCUT))
    return done
