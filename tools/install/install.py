#!/usr/bin/env python3
"""Burnout 3: Takedown installer driver (BO-92).

Takes the user's own disc image or extracted folder and produces a runnable app
on the user's machine: toolchain check -> disc check/extract -> icon -> regen
(recompile) -> build -> package. Nothing of the game, no generated C and no
prebuilt binary is ever shipped; everything below is made from the user's disc.
The structure follows Xerenge's installer (a Qt front-end over setup scripts
that verify the image, extract it, build and make a shortcut); no Xerenge code
is used.

    python3 tools/install/install.py --source "Burnout 3.iso" --toolkit ../xboxrecomp-apex

Standard library only. stdout carries the step/progress protocol below and
nothing else, so a GUI can read it line by line; the tools' own output goes to
<root>/install/logs/<step>.log.

Protocol (version 1). One event per line; the first word is the event, fields
are separated by single spaces and the last field is free text:

    PROTOCOL 1
    PLAN <step> <step> ...                the steps this run will consider
    STEP <step> BEGIN <description>
    PROGRESS <step> <0.000-1.000> <text>
    LOG <step> <text>                     also WARN <step> <text>
    MISSING <step> <what> <how to get it>  a toolchain item that is not there
    STEP <step> SKIP <why>                already done, nothing redone
    STEP <step> OK <summary>
    STEP <step> FAIL <code> <message>     code: a word a GUI can switch on
    RESULT OK <path of the app>
    RESULT FAIL <step> <code>

The opt-in step `shaders` (--shaders) is the one step whose failure is not fatal:
it is reported as `STEP shaders FAIL <code> <text>` and a WARN line, and the run
goes on to `RESULT OK` (the game works without its shader cache).

Exit status: 0 ok, 1 a step failed, 2 bad usage, 3 toolchain incomplete,
4 unsupported or unreadable disc.

Restartable: every finished step records what it was made from in
<root>/install/state.json and is skipped while that still matches (the disc by
the XBE hash, regen by the XBE, the toolkit's tools and the configs, the
package by the binary and the icon). Interrupted extraction resumes file by
file. --force <step> redoes a step.
"""

import argparse
import hashlib
import json
import os
import plistlib
import re
import shutil
import stat
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import bundle        # noqa: E402
from common import (EXIT_DISC, EXIT_FAIL, EXIT_TOOLCHAIN, EXIT_USAGE, StepError, capture,  # noqa: E402
                    check_location, ensure_capstone, python_for_regen, run)
import disc          # noqa: E402
import icons         # noqa: E402
import linux         # noqa: E402
import regen         # noqa: E402
import shaders       # noqa: E402
import win           # noqa: E402
import xbe as xbe_mod  # noqa: E402

STEPS = ["toolchain", "disc", "icon", "regen", "build", "package"]
OPTIONAL_STEPS = ["desktop", "steam", "shaders"]      # integration and the shader pre-build; only when asked for
DESCRIPTIONS = {
    "toolchain": "Checking the build tools",
    "disc": "Checking and extracting the disc",
    "icon": "Making the icon from the disc's XBE",
    "regen": "Recompiling the game's code (regen)",
    "build": "Building the game",
    "package": "Packaging the app",
    "desktop": "Installing the desktop entry and icon",
    "steam": "Adding the Steam shortcut",
    "shaders": "Preparing graphics (builds every shader and pipeline the game needs, a few seconds)",
}
APP_NAME = "Burnout 3 Takedown"
GAME_DIR_NAME = "Burnout 3 Takedown"
BUNDLE_ID = "org.burnout3recomp.takedown"      # placeholder until the project has a home
LAUNCHER_VERSION = 2          # bumped when the bundle's layout changes

# ------------------------------------------------------------------ protocol
class Out:
    def __init__(self):
        self.stream = sys.stdout
        self._last = {}

    def _w(self, *parts):
        line = " ".join(str(p) for p in parts).replace("\r", " ").replace("\n", " ")
        self.stream.write(line + "\n")
        self.stream.flush()

    def begin(self, step):
        self._w("STEP", step, "BEGIN", DESCRIPTIONS[step])

    def ok(self, step, text=""):
        self._w("STEP", step, "OK", text)

    def skip(self, step, text):
        self._w("STEP", step, "SKIP", text)

    def fail(self, step, code, text):
        self._w("STEP", step, "FAIL", code, text)

    def progress(self, step, frac, text=""):
        """Throttled: at most about one event per percent."""
        frac = max(0.0, min(1.0, frac))
        last = self._last.get(step)
        if last is not None and frac < 1.0 and abs(frac - last) < 0.01:
            return
        self._last[step] = frac
        self._w("PROGRESS", step, "%.3f" % frac, text)

    def log(self, step, text):
        self._w("LOG", step, text)

    def warn(self, step, text):
        self._w("WARN", step, text)

    def missing(self, step, what, hint):
        self._w("MISSING", step, what, hint)


# --------------------------------------------------------------------- state
class Ctx:
    def __init__(self, a, out):
        self.out = out
        self.work = os.path.abspath(a.work)
        sysname = sys.platform
        default_root = (os.path.expanduser("~/Library/Application Support/Burnout3")
                        if sysname == "darwin" else
                        os.path.join(os.environ.get("LOCALAPPDATA", os.path.expanduser("~")), "Burnout3")
                        if sysname.startswith("win") else
                        os.path.join(os.environ.get("XDG_DATA_HOME",
                                                    os.path.expanduser("~/.local/share")), "burnout3"))
        self.root = os.path.abspath(os.path.expanduser(a.root or default_root))
        self.toolkit = os.path.abspath(os.path.expanduser(
            a.toolkit or os.environ.get("XBOXRECOMP_DIR") or os.path.join(self.work, "..", "xboxrecomp")))
        self.mac = sys.platform == "darwin"
        self.linux = sys.platform.startswith("linux")
        self.win = sys.platform == "win32"
        self.app_dir = os.path.abspath(os.path.expanduser(
            a.app_dir or ("~/Applications" if self.mac else os.path.join(self.root, "app"))))
        self.tools_dir = os.path.abspath(os.path.expanduser(a.tools_dir)) if a.tools_dir else \
            os.path.join(self.root, "tools")
        self.no_fetch = a.no_fetch
        self.start_menu_dir = a.start_menu_dir
        self.desktop_dir = a.desktop_dir
        self.py_site = None
        self.linux_build = a.linux_build
        self.podman_root = os.path.expanduser(a.podman_root) if a.podman_root else None
        self.podman_runroot = a.podman_runroot
        self.image = a.image
        self.no_pull = a.no_pull
        self.build_mode = None
        self.xdg_data_home = os.path.abspath(os.path.expanduser(
            a.xdg_data_home or os.environ.get("XDG_DATA_HOME") or "~/.local/share"))
        self.steam_config = os.path.abspath(os.path.expanduser(a.steam_config)) if a.steam_config else None
        self.steam_user = a.steam_user
        self.shader_cache = os.path.abspath(os.path.expanduser(a.shader_cache)) if a.shader_cache else None
        self.source = os.path.abspath(os.path.expanduser(a.source)) if a.source else None
        self.python = a.python
        self.jobs = a.jobs or max(1, (os.cpu_count() or 2) // 2)
        self.nice = a.nice
        self.force = set(x for f in (a.force or []) for x in f.split(","))
        self.no_pip = a.no_pip
        self.game_parent = os.path.join(self.root, "Game")
        self.game_dir = os.path.join(self.game_parent, GAME_DIR_NAME)
        self.inst = os.path.join(self.root, "install")
        self.icons_dir = os.path.join(self.inst, "icons")
        self.logs = os.path.join(self.inst, "logs")
        self.state_path = os.path.join(self.inst, "state.json")
        self.build_dir = os.path.join(self.work, "build", "mac" if self.mac else "win" if self.win else "linux")
        self.bin_dir = os.path.join(self.build_dir, "bin")
        self.binary = os.path.join(self.bin_dir, "burnout3")
        self.gen_dir = os.path.join(self.work, "src", "game", "recomp", "gen")
        self.app = (os.path.join(self.app_dir, APP_NAME + ".app") if self.mac else self.app_dir)
        if self.win and os.path.isdir(os.path.join(self.root, "install", "site")):
            self.py_site = os.path.join(self.root, "install", "site")
        self.env = dict(os.environ)
        self.env["PYTHONDONTWRITEBYTECODE"] = "1"     # the toolkit is read-only to us
        self.state = {}
        try:
            with open(self.state_path) as f:
                self.state = json.load(f)
        except (OSError, ValueError):
            pass

    def save_state(self):
        os.makedirs(self.inst, exist_ok=True)
        tmp = self.state_path + ".tmp"
        with open(tmp, "w") as f:
            json.dump(self.state, f, indent=2, sort_keys=True)
        os.replace(tmp, self.state_path)

    def done(self, step, key):
        return step not in self.force and self.state.get(step, {}).get("key") == key

    def mark(self, step, key, **extra):
        self.state[step] = dict(key=key, at=time.strftime("%Y-%m-%dT%H:%M:%S"), **extra)
        self.save_state()


def hash_files(paths):
    h = hashlib.sha256()
    for p in sorted(paths):
        h.update(p.encode("utf-8", "replace") + b"\0")
        try:
            with open(p, "rb") as f:
                h.update(f.read())
        except OSError:
            h.update(b"<missing>")
    return h.hexdigest()


def tree_files(d, suffix=(".py",)):
    out = []
    for dp, dn, fn in os.walk(d):
        dn[:] = [x for x in dn if x not in ("__pycache__", "output", ".git") and not x.startswith("test")]
        out += [os.path.join(dp, n) for n in fn if n.endswith(suffix) and not n.startswith("test_")]
    return out


# ------------------------------------------------------------------ toolchain
def brew_prefix(formula):
    rc, out = capture(["brew", "--prefix", formula])
    return out if rc == 0 and os.path.isdir(out) else None


def mac_tools(ctx, need):
    rc, path = capture(["xcode-select", "-p"])
    rc2, cc = capture(["cc", "--version"])
    need("clang", rc == 0 and rc2 == 0, cc.splitlines()[0] if rc2 == 0 else "",
         "xcode-select --install   (Command Line Tools; full Xcode is not needed)")
    rc, v = capture(["cmake", "--version"])
    ver = re.search(r"(\d+)\.(\d+)", v) if rc == 0 else None
    need("cmake", bool(ver) and (int(ver.group(1)), int(ver.group(2))) >= (3, 20), v.splitlines()[0] if rc == 0 else "",
         "brew install cmake   (3.20 or newer)")
    rc, v = capture(["ninja", "--version"])
    need("ninja", rc == 0, v, "brew install ninja")
    # Libraries the toolkit links on macOS.
    rc, _ = capture(["brew", "--version"])
    if rc != 0:
        need("homebrew", False, "", "install Homebrew from https://brew.sh (the libraries below come from it)")
        ctx.libs = {}
        return
    libs = {}
    for key, formulas, hint in (("sdl2", ("sdl2", "sdl2-compat"), "brew install sdl2"),
                                ("openssl", ("openssl@3",), "brew install openssl@3"),
                                ("epoxy", ("libepoxy",), "brew install libepoxy (build-time; the app does not carry it)")):
        found = next((brew_prefix(f) for f in formulas if brew_prefix(f)), None)
        libs[key] = found
        need(key, bool(found), found or "", hint)
    # sdl2-compat (the brew "sdl2") loads libSDL3 by name at run time; the
    # app has to carry it too.
    if libs["sdl2"] and sdl2_needs_sdl3(libs["sdl2"]):
        libs["sdl3"] = brew_prefix("sdl3")
        need("sdl3", bool(libs["sdl3"]), libs["sdl3"] or "", "brew install sdl3 (sdl2-compat loads it)")
    ctx.libs = libs


def step_toolchain(ctx):
    out = ctx.out
    if not (ctx.mac or ctx.linux or ctx.win):
        raise StepError("unsupported-platform", "macOS, Linux and Windows are supported", EXIT_TOOLCHAIN)
    missing = []

    def need(name, ok, detail, hint):
        if ok:
            out.log("toolchain", "ok %s %s" % (name, detail))
        elif name not in missing:
            missing.append(name)
            out.missing("toolchain", name, hint)

    # Where the installer writes: the disc (~2.4 GB) and settings in the root, the generated
    # C and the build (~1 GB) in the work tree, the app. None of them need exist yet; the
    # nearest existing parent is measured.
    for name, path, gb, hint in (
            ("disk-space", ctx.root, 8, "free at least 8 GB (disc data ~2.4 GB, build ~1 GB) or pick another --root"),
            ("work-folder", ctx.work, 0, "the source tree is built in must be writable (--work)"),
            ("app-folder", ctx.app_dir, 0, "the folder for the app must be writable (--app-dir)")):
        ok, detail = check_location(path, gb)
        need(name, ok, detail, "%s (%s)" % (hint, detail))
    if missing:                                  # nowhere to write: the other checks would only fail the same way
        raise StepError("toolchain-incomplete", "missing: " + ", ".join(missing), EXIT_TOOLCHAIN)

    (mac_tools if ctx.mac else win.tools if ctx.win else linux.tools)(ctx, need)

    # Python for the recompiler: 3.10+ with capstone. A problem here is recorded like
    # any other and the checks below still run, so one pass reports everything missing.
    try:
        if ctx.win:
            if sys.version_info < (3, 10):
                raise StepError("python-too-old", "the installer needs Python 3.10 or newer", EXIT_TOOLCHAIN)
            py = win.ensure_capstone(ctx)
            note = "capstone from " + (ctx.py_site or "the interpreter")
        else:
            py = python_for_regen(ctx)
            py, note = ensure_capstone(ctx, py)
            python_for_regen(ctx, py)
        ctx.regen_python = py
        out.log("toolchain", "ok python %s (%s)" % (py, note))
    except StepError as e:
        if e.exit_code != EXIT_TOOLCHAIN:
            raise
        hints = {"missing-capstone": ("python-capstone", "pip install capstone"),
                 "python-too-old": ("python", "install Python 3.10 or newer"),
                 "python-missing": ("python", "install Python 3.10 or newer (or pass --python)"),
                 "venv-failed": ("python-venv", "python3 -m venv must work (the python3-venv package)"),
                 "pip-failed": ("python-capstone", "pip install capstone (needs the network)")}
        name, hint = hints.get(e.code, ("python", "see " + os.path.join(ctx.logs, "toolchain.log")))
        need(name, False, "", "%s (%s)" % (hint, e))

    # Source side.
    need("toolkit", os.path.isfile(os.path.join(ctx.toolkit, "CMakeLists.txt"))
         and os.path.isdir(os.path.join(ctx.toolkit, "tools", "recomp")), ctx.toolkit,
         "the xboxrecomp toolkit checkout: pass --toolkit <dir> (or XBOXRECOMP_DIR)")
    need("source", os.path.isfile(os.path.join(ctx.work, "regen.sh"))
         and os.path.isfile(os.path.join(ctx.work, "CMakeLists.txt")), ctx.work,
         "run this from the burnout3 source tree (or --work <dir>)")

    if missing:
        raise StepError("toolchain-incomplete", "missing: " + ", ".join(missing), EXIT_TOOLCHAIN)
    return "all tools present"


# ----------------------------------------------------------------------- disc
def step_disc(ctx):
    builds = disc.load_supported()
    have = os.path.join(ctx.game_dir, "default.xbe")
    if os.path.isfile(have) and ctx.state.get("disc", {}).get("key") and "disc" not in ctx.force:
        try:
            b = disc.verify_installed(ctx.game_dir, builds)
            if ctx.state["disc"]["key"] == b["xbe"]["sha256"]:
                return "SKIP", "game data already in %s (%s)" % (ctx.game_dir, b["name"])
        except disc.DiscError:
            pass
    if not ctx.source:
        raise StepError("no-source", "no disc to install from: pass --source <image or folder>", EXIT_USAGE)
    ctx.out.log("disc", "reading " + ctx.source)
    try:
        info = disc.probe(ctx.source, builds)
        ctx.out.log("disc", "%s: %s, %d files, %.2f GB%s" % (
            info["kind"], info["build"]["name"], info["files"], info["bytes"] / 1e9,
            ", game partition at 0x%X" % info["base"] if info["kind"] == "image" else ""))
        disc.install(ctx.source, ctx.game_dir, builds,
                     progress=lambda f, t: ctx.out.progress("disc", f, t))
        b = disc.verify_installed(ctx.game_dir, builds)
    except disc.DiscError as e:
        raise StepError(e.code, str(e), EXIT_DISC)
    ctx.mark("disc", b["xbe"]["sha256"], source=ctx.source, build=b["id"])
    return "OK", "%s: %d files in %s" % (b["name"], info["files"], ctx.game_dir)


# ----------------------------------------------------------------------- icon
def step_icon(ctx):
    data = open(os.path.join(ctx.game_dir, "default.xbe"), "rb").read()
    key = xbe_mod.sha256(data)
    want = [os.path.join(ctx.icons_dir, n) for n in ("AppIcon.icns", "AppIcon.ico", "icon-256.png")]
    if ctx.done("icon", key) and all(os.path.isfile(p) for p in want):
        return "SKIP", "icons already made in " + ctx.icons_dir
    try:
        paths = icons.make_icons(xbe_mod.Xbe(data), ctx.icons_dir,
                                 progress=lambda f: ctx.out.progress("icon", f))
    except (icons.IconError, xbe_mod.XbeError) as e:
        raise StepError("icon-failed", str(e))
    ctx.mark("icon", key)
    return "OK", ", ".join(sorted(os.path.basename(p) for p in paths.values()))


# ---------------------------------------------------------------------- regen
def regen_key(ctx, xbe_sha):
    files = tree_files(os.path.join(ctx.toolkit, "tools"))
    files = [f for f in files if any(("/" + d + "/") in f for d in
                                     ("xbe_parser", "disasm", "func_id", "abi_analysis", "recomp"))]
    files += [os.path.join(ctx.work, "regen.sh"), os.path.join(HERE, "regen.py")]
    seeds, spin, manual = regen.inputs(ctx.work, region_of(ctx))
    files += [f for f in (seeds, spin) if os.path.exists(f)]
    files += [manual]
    return xbe_sha + ":" + hash_files(files)


def region_of(ctx):
    """The disc's region (supported.json bo3_region), from the installed
    default.xbe: what regen and the build are made for."""
    xbe = os.path.join(ctx.game_dir, "default.xbe")
    if os.path.isfile(xbe):
        sha = xbe_mod.sha256_file(xbe)
        for b in disc.load_supported():
            if b["xbe"]["sha256"] == sha:
                return b.get("bo3_region", "NTSC-U")
    return "NTSC-U"


REGEN_STAGES = {"xbe_parser": 0.0, "disasm": 0.03, "func_id": 0.45, "abi_analysis": 0.55, "recomp": 0.65}


def step_regen(ctx):
    py = getattr(ctx, "regen_python", None) or (ctx.python or sys.executable if ctx.win else python_for_regen(ctx))
    xbe_path = os.path.join(ctx.game_dir, "default.xbe")
    xbe_sha = xbe_mod.sha256_file(xbe_path)
    key = regen_key(ctx, xbe_sha)
    gen_ok = os.path.isdir(ctx.gen_dir) and any(n.startswith("recomp_") for n in os.listdir(ctx.gen_dir))
    if gen_ok and ctx.done("regen", key):
        return "SKIP", "generated code is current (%s)" % ctx.gen_dir
    out_dir = os.path.join(ctx.work, "build", "xr")

    def stage(name):
        ctx.out.progress("regen", REGEN_STAGES[name], name)

    # An earlier disassembly is reused only when nothing it was made from changed;
    # a changed key means inputs changed, so redo it from the XBE.
    regen.regen(ctx, py, xbe_path, out_dir, ctx.gen_dir, ctx.toolkit, ctx.work,
                force_disasm=os.path.isdir(os.path.join(out_dir, "disasm")), on_stage=stage,
                site=getattr(ctx, "py_site", None), region=region_of(ctx))
    gen = os.path.isdir(ctx.gen_dir) and [x for x in os.listdir(ctx.gen_dir) if x.startswith("recomp_")]
    if not gen:
        raise StepError("regen-failed", "the recompiler finished but produced no code in " + ctx.gen_dir)
    ctx.mark("regen", key, chunks=len(gen))
    return "OK", "%d generated source files" % len(gen)


# ---------------------------------------------------------------------- build
NINJA_PROGRESS = re.compile(r"^\[(\d+)/(\d+)\]")


def step_build(ctx):
    ctx.region = region_of(ctx)         # -DBO3_REGION for the disc's build (docs/pal.md)
    if ctx.win:
        return build_win(ctx)
    if not ctx.mac:
        return build_linux(ctx)
    libs = getattr(ctx, "libs", None)
    if libs is None:
        step_toolchain_libs(ctx)
        libs = ctx.libs
    os.makedirs(ctx.build_dir, exist_ok=True)
    cfg = ["cmake", "-S", ctx.work, "-B", ctx.build_dir, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
           "-DXBOXRECOMP_DIR=" + ctx.toolkit, "-DBO3_BIN_DIR=" + ctx.bin_dir,
           "-DBO3_REGION=" + ctx.region]
    if libs.get("openssl"):
        cfg.append("-DOPENSSL_ROOT_DIR=" + libs["openssl"])
    if libs.get("sdl2"):
        cfg.append("-DSDL2_DIR=" + os.path.join(libs["sdl2"], "lib", "cmake", "SDL2"))
    rc, prefix = capture(["brew", "--prefix"])
    if rc == 0:
        cfg.append("-DCMAKE_PREFIX_PATH=" + prefix)
    # Metal only: the Vulkan backend (glslang, spirv-tools, vulkan-loader) is
    # compiled out, and ld drops libraries nothing uses (libepoxy is linked by
    # the toolkit on macOS but has no symbol imported). See docs/installer.md.
    cfg += ["-DXBOXRECOMP_VULKAN=OFF", "-DCMAKE_EXE_LINKER_FLAGS=-Wl,-dead_strip_dylibs"]
    ctx.out.progress("build", 0.0, "configuring")
    if run(ctx, "build", cfg, cwd=ctx.work) != 0:
        raise StepError("configure-failed", "cmake could not configure the build")
    seen = {"n": 0, "work": False}

    def line(s):
        m = NINJA_PROGRESS.match(s)
        if m:
            seen["work"] = True
            seen["n"] = int(m.group(1))
            ctx.out.progress("build", int(m.group(1)) / max(int(m.group(2)), 1), s[:120])

    rc = run(ctx, "build", ["ninja", "-C", ctx.build_dir, "-j%d" % ctx.jobs, "burnout3"], cwd=ctx.work, on_line=line)
    if rc != 0:
        raise StepError("build-failed", "ninja failed with status %d" % rc)
    if not os.path.isfile(ctx.binary):
        raise StepError("build-failed", "the build finished but %s is not there" % ctx.binary)
    if not seen["work"]:
        return "SKIP", "binary is up to date: " + ctx.binary
    return "OK", "%s (%d steps)" % (ctx.binary, seen["n"])


def build_win(ctx):
    seen = {"n": 0, "work": False}

    def line(s):
        m = NINJA_PROGRESS.match(s)
        if m:
            seen["work"] = True
            seen["n"] = int(m.group(1))
            ctx.out.progress("build", int(m.group(1)) / max(int(m.group(2)), 1), s[:120])

    ctx.out.progress("build", 0.0, "configuring")
    exe = win.build(ctx, line)
    if not seen["work"]:
        return "SKIP", "binary is up to date: " + exe
    return "OK", "%s (%d steps)" % (exe, seen["n"])


def build_linux(ctx):
    if ctx.build_mode is None:                       # the toolchain step did not run
        ctx.build_mode = ctx.linux_build if ctx.linux_build != "auto" else (
            "container" if shutil.which("podman") else "host")
    seen = {"n": 0, "work": False}

    def line(s):
        m = NINJA_PROGRESS.match(s)
        if m:
            seen["work"] = True
            seen["n"] = int(m.group(1))
            ctx.out.progress("build", int(m.group(1)) / max(int(m.group(2)), 1), s[:120])

    ctx.out.progress("build", 0.0, "configuring (%s)" % ctx.build_mode)
    binary = linux.build(ctx, line)
    if not seen["work"]:
        return "SKIP", "binary is up to date: " + binary
    return "OK", "%s (%d steps, %s)" % (binary, seen["n"], ctx.build_mode)


def step_toolchain_libs(ctx):
    ctx.libs = {}
    for key, formulas in (("sdl2", ("sdl2", "sdl2-compat")), ("openssl", ("openssl@3",))):
        ctx.libs[key] = next((brew_prefix(f) for f in formulas if brew_prefix(f)), None)
    if ctx.libs["sdl2"] and sdl2_needs_sdl3(ctx.libs["sdl2"]):
        ctx.libs["sdl3"] = brew_prefix("sdl3")


def sdl2_needs_sdl3(prefix):
    try:
        with open(os.path.join(prefix, "lib", "libSDL2-2.0.0.dylib"), "rb") as f:
            return b"libSDL3.dylib" in f.read()
    except OSError:
        return False


# -------------------------------------------------------------------- package
LAUNCHER = r'''#!/bin/bash
# Burnout 3: Takedown launcher. Written by tools/install/install.py.
# The game reads "Burnout 3 Takedown/" from its working directory, saves to
# ~/Library/Application Support/Burnout3 (RECOMP_SAVE_DIR moves them) and its
# enhancement settings from enhance.toml, which lives with the data below.
here="$(cd "$(dirname "$0")" && pwd)"
res="$here/../Resources"
root="${BURNOUT3_ROOT:-}"
if [ -z "$root" ] && [ -f "$res/install.conf" ]; then
    root="$(sed -n 's/^root=//p' "$res/install.conf" | head -n 1)"
fi
[ -n "$root" ] || root="$HOME/Library/Application Support/Burnout3"
game="$root/Game"
if [ ! -f "$game/Burnout 3 Takedown/default.xbe" ]; then
    /usr/bin/osascript - "$game" <<'EOS' >/dev/null 2>&1
on run argv
    display alert "Burnout 3 Takedown cannot find its game data" message ("Expected the disc's files in:" & return & item 1 of argv & return & return & "Run the installer again to extract them.") as critical
end run
EOS
    echo "game data not found in $game" >&2
    exit 1
fi
cfg="$root/enhance.toml"
[ -f "$cfg" ] || cp "$res/enhance.toml.default" "$cfg" 2>/dev/null
if [ -f "$cfg" ]; then
    export RECOMP_ENHANCE_CONFIG="${RECOMP_ENHANCE_CONFIG:-$cfg}"
fi
cd "$game" || exit 1
exec "$here/burnout3" "$@"
'''


def step_package(ctx):
    if ctx.linux or ctx.win:
        return package_linux(ctx)
    if not os.path.isfile(ctx.binary):
        raise StepError("no-binary", "no built game at %s: run the build step first" % ctx.binary)
    icns = os.path.join(ctx.icons_dir, "AppIcon.icns")
    if not os.path.isfile(icns):
        raise StepError("no-icon", "no icon at %s: run the icon step first" % icns)
    key = "%d:%s:%s:%s" % (LAUNCHER_VERSION, xbe_mod.sha256_file(ctx.binary), xbe_mod.sha256_file(icns), ctx.root)
    exe = os.path.join(ctx.app, "Contents", "MacOS", "burnout3")
    if ctx.done("package", key) and os.path.isfile(exe):
        return "SKIP", "app is current: " + ctx.app

    xbe = xbe_mod.Xbe(open(os.path.join(ctx.game_dir, "default.xbe"), "rb").read())
    contents = os.path.join(ctx.app, "Contents")
    if os.path.isdir(ctx.app):
        shutil.rmtree(ctx.app)
    for d in ("MacOS", "Resources"):
        os.makedirs(os.path.join(contents, d))
    ctx.out.progress("package", 0.2, "binary")
    shutil.copy2(ctx.binary, exe)
    launcher = os.path.join(contents, "MacOS", APP_NAME)
    with open(launcher, "w") as f:
        f.write(LAUNCHER)
    os.chmod(launcher, 0o755)
    res = os.path.join(contents, "Resources")
    shutil.copy2(icns, os.path.join(res, "AppIcon.icns"))
    default_cfg = os.path.join(ctx.work, "config", "enhance.toml.default")
    if os.path.isfile(default_cfg):
        shutil.copy2(default_cfg, os.path.join(res, "enhance.toml.default"))
    with open(os.path.join(res, "install.conf"), "w") as f:
        f.write("# Written by the installer. The launcher reads root= (BURNOUT3_ROOT overrides it).\n"
                "root=%s\n" % ctx.root)
    plist = {
        "CFBundleName": APP_NAME,
        "CFBundleDisplayName": "Burnout 3: Takedown",
        "CFBundleIdentifier": BUNDLE_ID,
        "CFBundleExecutable": APP_NAME,
        "CFBundleIconFile": "AppIcon",
        "CFBundlePackageType": "APPL",
        "CFBundleVersion": "1",
        "CFBundleShortVersionString": "1.0",
        "LSMinimumSystemVersion": "11.0",
        "LSApplicationCategoryType": "public.app-category.racing-games",
        "NSHighResolutionCapable": True,
        "NSSupportsAutomaticGraphicsSwitching": True,
        "CFBundleGetInfoString": "Built on this machine from %s (title id %s)"
                                % (xbe.title_name, "0x%08X" % xbe.title_id),
    }
    with open(os.path.join(contents, "Info.plist"), "wb") as f:
        plistlib.dump(plist, f)
    # Self-contained: the Homebrew libraries the binary needs go into
    # Contents/Frameworks with @rpath names, then everything is signed ad hoc
    # (Apple silicon refuses unsigned code; there is no developer identity here,
    # and an app made and run on one Mac does not need one).
    ctx.out.progress("package", 0.4, "bundling libraries")
    if getattr(ctx, "libs", None) is None:
        step_toolchain_libs(ctx)
    frameworks = os.path.join(contents, "Frameworks")
    extra = {}
    sdl2 = (ctx.libs.get("sdl2") or "")
    if sdl2 and sdl2_needs_sdl3(sdl2):
        sdl3 = ctx.libs.get("sdl3")
        if not sdl3:
            raise StepError("toolchain-incomplete", "sdl2-compat needs sdl3 at run time: brew install sdl3",
                            EXIT_TOOLCHAIN)
        extra["libSDL3.dylib"] = os.path.join(sdl3, "lib", "libSDL3.0.dylib")
    try:
        names = bundle.bundle(exe, frameworks, extra, log=lambda m: ctx.out.log("package", m))
        ctx.out.progress("package", 0.6, "signing")
        bundle.sign(ctx.app, frameworks)
        files = [exe] + [os.path.join(frameworks, n) for n in names]
        bad = bundle.audit(files)
    except bundle.BundleError as e:
        raise StepError("bundle-failed", str(e))
    if bad:
        raise StepError("bundle-failed", "still linked outside the app: " +
                        ", ".join("%s -> %s" % (os.path.basename(f), d) for f, d in bad))
    ctx.out.log("package", "libraries in the app: " + ", ".join(names))
    summary = ctx.app
    ctx.mark("package", key, app=ctx.app)
    return "OK", summary


def package_linux(ctx):
    """Linux and Windows: an install directory."""
    binary = os.path.join(ctx.work, "build", "win" if ctx.win else "linux", "bin",
                          "burnout3.exe" if ctx.win else "burnout3")
    png = os.path.join(ctx.icons_dir, "AppIcon.ico" if ctx.win else "icon-256.png")
    if not os.path.isfile(binary):
        raise StepError("no-binary", "no built game at %s: run the build step first" % binary)
    if not os.path.isfile(png):
        raise StepError("no-icon", "no icon at %s: run the icon step first" % png)
    key = "%d:%s:%s:%s" % (LAUNCHER_VERSION, xbe_mod.sha256_file(binary), xbe_mod.sha256_file(png), ctx.root)
    marker = "burnout3.exe" if ctx.win else "burnout3.sh"
    if ctx.done("package", key) and os.path.isfile(os.path.join(ctx.app_dir, marker)):
        return "SKIP", "install directory is current: " + ctx.app_dir
    xbe = xbe_mod.Xbe(open(os.path.join(ctx.game_dir, "default.xbe"), "rb").read())
    ctx.out.progress("package", 0.1, "binary")
    if ctx.win:
        win.package(ctx, binary)
        libs = []
    else:
        libs = linux.package(ctx, binary, xbe)
    ctx.mark("package", key, app=ctx.app_dir)
    return "OK", "%s%s" % (ctx.app_dir, " (+ " + ", ".join(libs) + ")" if libs else "")


def step_desktop(ctx):
    if ctx.win:
        return "OK", ", ".join(win.shortcuts(ctx))
    return "OK", linux.desktop(ctx)


def step_shaders(ctx):
    return shaders.run(ctx)


def step_steam(ctx):
    if not ctx.linux:
        raise StepError("unsupported-platform", "the Steam shortcut step is implemented for Linux (the Deck)")
    xbe = xbe_mod.Xbe(open(os.path.join(ctx.game_dir, "default.xbe"), "rb").read())
    text, added = linux.steam_shortcut(ctx, xbe)
    return ("OK" if added else "SKIP"), text


# ------------------------------------------------------------------- driver
FUNCS = {"toolchain": step_toolchain, "disc": step_disc, "icon": step_icon,
         "regen": step_regen, "build": step_build, "package": step_package,
         "desktop": step_desktop, "steam": step_steam, "shaders": step_shaders}


def parse_args(argv):
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0],
                                formatter_class=argparse.RawDescriptionHelpFormatter,
                                epilog="steps: " + ", ".join(STEPS) + "; opt-in: " + ", ".join(OPTIONAL_STEPS))
    p.add_argument("--source", help="the disc: an .iso/.xiso image or an extracted folder "
                                    "(not needed once the disc step is done)")
    p.add_argument("--root", help="where the game data, settings and install state live "
                                  "(default: ~/Library/Application Support/Burnout3 on macOS)")
    p.add_argument("--work", default=os.path.abspath(os.path.join(HERE, "..", "..")),
                   help="the burnout3 source tree to regenerate and build in (default: this one)")
    p.add_argument("--toolkit", help="the xboxrecomp toolkit checkout (default: $XBOXRECOMP_DIR, else <work>/../xboxrecomp)")
    p.add_argument("--app-dir", help="where the .app goes (default: ~/Applications)")
    p.add_argument("--python", help="interpreter for the recompiler when <work>/.venv does not exist "
                                    "(needs capstone)")
    p.add_argument("--jobs", type=int, help="parallel compiles (default: half the cores)")
    p.add_argument("--nice", type=int, default=0, help="run regen and the build at this nice level")
    p.add_argument("--only", help="comma-separated steps to run (default: all)")
    p.add_argument("--from", dest="from_step", choices=STEPS + OPTIONAL_STEPS, help="start at this step")
    p.add_argument("--force", action="append", help="redo these steps even if done (comma-separated)")
    p.add_argument("--no-pip", action="store_true", help="do not install capstone; only report it missing")
    p.add_argument("--linux-build", choices=("auto", "container", "host"), default="auto",
                   help="Linux: build in the Steam Runtime sniper SDK container (podman) or with the host's "
                        "toolchain (auto: the container when podman exists)")
    p.add_argument("--podman-root", help="Linux: podman image storage (default: podman's own; the Deck keeps it "
                                         "on the SD card)")
    p.add_argument("--podman-runroot", help="Linux: the run root an existing --podman-root store was made with")
    p.add_argument("--image", default=linux.IMAGE, help="Linux: the SDK image")
    p.add_argument("--no-pull", action="store_true", help="Linux: do not pull a missing SDK image")
    p.add_argument("--desktop", action="store_true", help="Linux: also install the desktop entry and icon "
                                                          "(step `desktop`)")
    p.add_argument("--steam", action="store_true", help="Linux: also add the Steam shortcut (step `steam`; "
                                                        "Steam must be closed)")
    p.add_argument("--shaders", action="store_true", help="also let the installed game build its shader cache "
                                                          "(step `shaders`, a few seconds; optional, never "
                                                          "on by itself; a failure there does not fail the install)")
    p.add_argument("--shader-cache", help="the shader cache the `shaders` step fills (default: the game's own; "
                                          "for tests, a folder of their own)")
    p.add_argument("--xdg-data-home", help="Linux: where the desktop step installs (default: $XDG_DATA_HOME "
                                           "or ~/.local/share)")
    p.add_argument("--steam-user", help="Linux: Steam user id when several are found")
    p.add_argument("--steam-config", help="Linux: the Steam userdata/<id>/config folder to change (default: "
                                          "found automatically)")
    p.add_argument("--tools-dir", help="Windows: where the portable tools live (llvm-mingw, cmake, ninja, SDL2, "
                                       "Vulkan headers, glslang); fetched there when missing (default: <root>/tools)")
    p.add_argument("--no-fetch", action="store_true", help="Windows: do not download missing tools; report them")
    p.add_argument("--start-menu-dir", help="Windows: where the desktop step puts the Start menu shortcut")
    p.add_argument("--desktop-dir", help="Windows: where the desktop step puts the Desktop shortcut")
    p.add_argument("--check-disc", action="store_true", help="only identify --source and print what it is")
    return p.parse_args(argv)


def log_exception(ctx, step):
    """The traceback of the exception being handled, appended to the step's log."""
    import traceback
    try:
        os.makedirs(ctx.logs, exist_ok=True)
        with open(os.path.join(ctx.logs, step + ".log"), "a") as f:
            f.write("\n==== %s: unexpected exception\n%s" % (time.strftime("%F %T"), traceback.format_exc()))
    except OSError:
        pass                                    # the log location itself is the problem: the message still goes out


def main(argv=None):
    a = parse_args(argv)
    out = Out()
    out._w("PROTOCOL", 1)
    ctx = Ctx(a, out)
    if a.check_disc:
        if not a.source:
            out._w("RESULT", "FAIL", "disc", "no-source")
            return EXIT_USAGE
        out.begin("disc")
        try:
            info = disc.probe(ctx.source)
        except disc.DiscError as e:
            out.fail("disc", e.code, str(e))
            return EXIT_DISC
        out.ok("disc", "%s, %s, %d files, %.2f GB" % (info["build"]["name"], info["kind"],
                                                      info["files"], info["bytes"] / 1e9))
        return 0
    steps = STEPS + (["desktop"] if a.desktop else []) + (["steam"] if a.steam else []) \
        + (["shaders"] if a.shaders else [])
    if a.only:
        steps = [s for s in a.only.split(",") if s]
        bad = [s for s in steps if s not in FUNCS]
        if bad:
            out._w("RESULT", "FAIL", bad[0], "unknown-step")
            return EXIT_USAGE
    elif a.from_step:
        steps = steps[steps.index(a.from_step):] if a.from_step in steps else [a.from_step]
    out._w("PLAN", *steps)
    for s in steps:
        out.begin(s)
        t0 = time.time()
        try:
            r = FUNCS[s](ctx)
        except shaders.StepWarning as e:
            # An optional step that did not work: reported as a failed step and a warning,
            # but the install is complete and RESULT stays OK. Run the step again to retry.
            out.fail(s, e.code, str(e))
            out.warn(s, "the install is complete without it; run the %s step again to retry (%s)" % (s, e))
            continue
        except StepError as e:
            out.fail(s, e.code, str(e))
            out._w("RESULT", "FAIL", s, e.code)
            return e.exit_code
        except Exception as e:                  # nothing surfaces as a bare traceback
            code = ("io-error" if isinstance(e, OSError) else "bad-xbe" if isinstance(e, xbe_mod.XbeError)
                    else "internal-error")
            log_exception(ctx, s)
            out.fail(s, code, "%s: %s (traceback in %s)" % (type(e).__name__, e, os.path.join(ctx.logs, s + ".log")))
            out._w("RESULT", "FAIL", s, code)
            return EXIT_FAIL
        kind, text = r if isinstance(r, tuple) else ("OK", r)
        if kind == "SKIP":
            out.skip(s, text)
        else:
            out.progress(s, 1.0, "")
            out.ok(s, "%s (%.0fs)" % (text, time.time() - t0))
    out._w("RESULT", "OK", ctx.app if "package" in steps else "-")
    return 0


if __name__ == "__main__":
    sys.exit(main())
