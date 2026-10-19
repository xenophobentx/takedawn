"""Linux backend of the installer (Steam Deck, Bazzite, desktops): toolchain
check, build, package, and the two opt-in integration steps (desktop entry,
Steam shortcut).

Build: by default inside the Steam Runtime "sniper" SDK container with podman,
which is how the Deck build is already made (the binary then runs on SteamOS and
Bazzite alike: glibc and libstdc++ of the SDK are older than both). A host
toolchain (clang or gcc, cmake, ninja, pkg-config and the dev libraries) is the
second choice for a desktop that has one: --linux-build host.

Output (`--app-dir`, default <root>/app):

    burnout3            the binary
    lib/                libraries the host lacks (SteamOS has no libcrypto 1.1),
                        taken from the container the binary was linked in
    burnout3.sh         launcher: LD_LIBRARY_PATH, enhance.toml, cd to the data
    install.conf        root=<where Game/ and enhance.toml live>
    burnout3.png        the icon from the user's XBE (256x256)
    burnout3.desktop    a desktop entry with absolute paths (not installed by
                        the package step)

An AppImage was not chosen: what this installer makes cannot be distributed
(it is built from the user's disc), so the AppImage's one advantage, a single
file to hand around, does not apply; it would add appimagetool (a download) and
need the host's SDL2 and Vulkan bundled to be portable, for the same result as
this directory plus a desktop entry and a Steam shortcut.
"""

import os
import re
import shutil
import stat

from common import EXIT_TOOLCHAIN, StepError, capture, run

try:
    import steam
except ImportError:
    from . import steam

IMAGE = "registry.gitlab.steamos.cloud/steamrt/sniper/sdk:latest"
SHORTCUT_NAME = "Burnout 3: Takedown"
DESKTOP_ID = "burnout3-takedown"
NINJA_PROGRESS = re.compile(r"^\[(\d+)/(\d+)\]")


# ---------------------------------------------------------------- containers
def podman(ctx, *args):
    cmd = ["podman"]
    if ctx.podman_root:
        # podman records the run root in the store's database and refuses a different
        # one, so an existing store needs its own (--podman-runroot).
        runroot = ctx.podman_runroot or os.path.join(os.environ.get("XDG_RUNTIME_DIR", "/tmp"),
                                                      "bo3-installer-runroot")
        cmd += ["--root", ctx.podman_root, "--runroot", runroot]
    return cmd + list(args)


def container_run(ctx, step, script, mounts, **kw):
    """Run `sh -c script` in the SDK image with the given {host: container[:opts]} mounts."""
    cmd = podman(ctx, "run", "--rm", "--security-opt", "label=disable")
    for host, guest in mounts.items():
        cmd += ["-v", "%s:%s" % (host, guest)]
    workdir = "/work" if "/work" in mounts.values() else "/"
    return run(ctx, step, cmd + ["-w", workdir, ctx.image, "sh", "-c", script], **kw)


def os_hint():
    try:
        ident = dict(l.strip().split("=", 1) for l in open("/etc/os-release") if "=" in l)
    except OSError:
        ident = {}
    d = (ident.get("ID", "") + " " + ident.get("ID_LIKE", "")).lower()
    if "arch" in d or "steamos" in d:
        return "pacman -S --needed %s"
    if "fedora" in d or "bazzite" in d:
        return "dnf install %s   (on Bazzite: in a distrobox/toolbox)"
    return "apt install %s"


# ----------------------------------------------------------------- toolchain
def tools(ctx, need):
    """The Linux part of the toolchain step. `need(name, ok, detail, hint)` records."""
    mode = ctx.linux_build
    have_podman = shutil.which("podman") is not None
    if mode == "auto":
        mode = "container" if have_podman else "host"
    ctx.build_mode = mode
    if mode == "container":
        rc, v = capture(["podman", "--version"])
        need("podman", rc == 0, v, "install podman (SteamOS and Bazzite have it; otherwise "
             "--linux-build host with a native toolchain)")
        if rc != 0:
            return
        exists = capture(podman(ctx, "image", "exists", ctx.image))[0] == 0
        if not exists and not ctx.no_pull:
            ctx.out.log("toolchain", "pulling the Steam Runtime SDK image %s (several GB, network)" % ctx.image)
            if run(ctx, "toolchain", podman(ctx, "pull", ctx.image), nice=False,
                   on_line=lambda l: ctx.out.log("toolchain", l[:140])) == 0:
                exists = True
        need("sdk-image", exists, ctx.image,
             "podman pull %s%s" % (ctx.image, " --root " + ctx.podman_root if ctx.podman_root else ""))
        return
    for tool, hint in (("cmake", "cmake (3.20 or newer)"), ("ninja", "ninja"), ("pkg-config", "pkgconf")):
        need(tool, shutil.which(tool) is not None, shutil.which(tool) or "", os_hint() % hint)
    cc = shutil.which("clang") or shutil.which("gcc")
    need("c-compiler", cc is not None, cc or "", os_hint() % "clang")
    for mod, hint in (("sdl2", "sdl2 (dev)"), ("epoxy", "libepoxy (dev)"), ("vulkan", "vulkan-icd-loader and "
                      "vulkan-headers"), ("openssl", "openssl (dev)")):
        ok = capture(["pkg-config", "--exists", mod])[0] == 0
        need("lib-" + mod, ok, mod, os_hint() % hint)


# --------------------------------------------------------------------- build
def build(ctx, ninja_line):
    """Configure and build; returns (built something, steps). ninja_line(s) gets output lines."""
    build_dir = os.path.join(ctx.work, "build", "linux")
    os.makedirs(build_dir, exist_ok=True)
    nice = "nice -n %d " % ctx.nice if ctx.nice else ""
    if ctx.build_mode == "container":
        cfg = ("CC=clang cmake -S /work -B /work/build/linux -G Ninja -DCMAKE_BUILD_TYPE=Release "
               "-DXBOXRECOMP_DIR=/toolkit -DBO3_BIN_DIR=/work/build/linux/bin -DBO3_REGION=%s" % ctx.region)
        mounts = {ctx.work: "/work", ctx.toolkit: "/toolkit:ro"}
        if container_run(ctx, "build", cfg, mounts, nice=False) != 0:
            raise StepError("configure-failed", "cmake could not configure the build in the container")
        rc = container_run(ctx, "build", "%sninja -C /work/build/linux -j%d burnout3" % (nice, ctx.jobs), mounts,
                           on_line=ninja_line, nice=False)
    else:
        cfg = ["cmake", "-S", ctx.work, "-B", build_dir, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
               "-DXBOXRECOMP_DIR=" + ctx.toolkit, "-DBO3_BIN_DIR=" + os.path.join(build_dir, "bin"),
               "-DBO3_REGION=" + ctx.region]
        if run(ctx, "build", cfg, cwd=ctx.work) != 0:
            raise StepError("configure-failed", "cmake could not configure the build")
        rc = run(ctx, "build", ["ninja", "-C", build_dir, "-j%d" % ctx.jobs, "burnout3"], cwd=ctx.work,
                 on_line=ninja_line)
    if rc != 0:
        raise StepError("build-failed", "ninja failed with status %d" % rc)
    binary = os.path.join(build_dir, "bin", "burnout3")
    if not os.path.isfile(binary):
        raise StepError("build-failed", "the build finished but %s is not there" % binary)
    return binary


# ------------------------------------------------------------------- package
LAUNCHER = r'''#!/bin/sh
# Burnout 3: Takedown launcher. Written by tools/install/install.py.
# The game reads "Burnout 3 Takedown/" from its working directory, keeps its
# saves in ${XDG_DATA_HOME:-~/.local/share}/burnout3 (RECOMP_SAVE_DIR moves
# them) and the enhancement settings in enhance.toml under the install root.
# Tuning without editing this file: put KEY=VALUE lines (RECOMP_*) in
# <root>/launch.env.
here=$(cd "$(dirname "$0")" && pwd)
root=${BURNOUT3_ROOT:-}
if [ -z "$root" ] && [ -f "$here/install.conf" ]; then
    root=$(sed -n 's/^root=//p' "$here/install.conf" | head -n 1)
fi
[ -n "$root" ] || root=${XDG_DATA_HOME:-$HOME/.local/share}/burnout3
if [ ! -f "$root/Game/Burnout 3 Takedown/default.xbe" ]; then
    echo "Burnout 3: game data not found in $root/Game - run the installer again" >&2
    exit 1
fi
LD_LIBRARY_PATH="$here/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export LD_LIBRARY_PATH
cfg="$root/enhance.toml"
[ -f "$cfg" ] || cp "$here/enhance.toml.default" "$cfg" 2>/dev/null
[ -f "$cfg" ] && export RECOMP_ENHANCE_CONFIG="${RECOMP_ENHANCE_CONFIG:-$cfg}"
if [ -f "$root/launch.env" ]; then
    while IFS= read -r line; do
        case "$line" in ''|'#'*) continue ;; *=*) export "$line" ;; esac
    done < "$root/launch.env"
fi
cd "$root/Game" || exit 1
exec "$here/burnout3" "$@"
'''

DESKTOP = """[Desktop Entry]
Type=Application
Name=Burnout 3: Takedown
Comment=Static recompilation, built from your own disc
Exec="{exe}"
Path={path}
Icon={icon}
Terminal=false
Categories=Game;
StartupWMClass=burnout3
"""


def missing_libs(binary):
    """Sonames the host's loader cannot find for `binary` (ldd's "not found")."""
    rc, out = capture(["ldd", binary])
    return [l.split("=>")[0].strip() for l in out.splitlines() if "not found" in l]


def copy_missing_libs(ctx, binary, lib_dir):
    """Libraries the host lacks, copied out of the SDK image the binary was
    linked in. Returns the names copied."""
    need = missing_libs(binary)
    if not need or ctx.build_mode != "container":
        return []
    os.makedirs(lib_dir, exist_ok=True)
    script = " ; ".join('cp -L "$(ldconfig -p | sed -n "s/.*%s .*=> //p" | head -n 1)" /out/' % re.escape(n)
                        for n in need)
    rc = container_run(ctx, "package", script, {lib_dir: "/out"}, nice=False)
    got = [n for n in need if os.path.exists(os.path.join(lib_dir, n))]
    if rc != 0 or len(got) != len(need):
        raise StepError("lib-copy-failed", "could not take %s out of the SDK image"
                        % ", ".join(n for n in need if n not in got))
    return got


def package(ctx, binary, xbe):
    app = ctx.app_dir
    os.makedirs(app, exist_ok=True)
    exe = os.path.join(app, "burnout3")
    shutil.copy2(binary, exe)
    ctx.out.progress("package", 0.3, "libraries")
    libs = copy_missing_libs(ctx, exe, os.path.join(app, "lib"))
    for name, text, mode in (("burnout3.sh", LAUNCHER, 0o755),
                             ("install.conf", "# Written by the installer. root= is where Game/ and "
                              "enhance.toml are.\nroot=%s\n" % ctx.root, 0o644)):
        p = os.path.join(app, name)
        with open(p, "w") as f:
            f.write(text)
        os.chmod(p, mode)
    default_cfg = os.path.join(ctx.work, "config", "enhance.toml.default")
    if os.path.isfile(default_cfg):
        shutil.copy2(default_cfg, os.path.join(app, "enhance.toml.default"))
    icon = os.path.join(app, "burnout3.png")
    shutil.copy2(os.path.join(ctx.icons_dir, "icon-256.png"), icon)
    with open(os.path.join(app, "burnout3.desktop"), "w") as f:
        f.write(DESKTOP.format(exe=os.path.join(app, "burnout3.sh"), path=app, icon=icon))
    return libs


# --------------------------------------------------- opt-in: desktop and Steam
def desktop(ctx):
    """Install the desktop entry and icon into the XDG data directory."""
    src = os.path.join(ctx.app_dir, "burnout3.desktop")
    if not os.path.isfile(src):
        raise StepError("no-package", "no %s: run the package step first" % src)
    data = ctx.xdg_data_home
    icon_dst = os.path.join(data, "icons", "hicolor", "256x256", "apps", DESKTOP_ID + ".png")
    os.makedirs(os.path.dirname(icon_dst), exist_ok=True)
    shutil.copy2(os.path.join(ctx.app_dir, "burnout3.png"), icon_dst)
    app_dst = os.path.join(data, "applications", DESKTOP_ID + ".desktop")
    os.makedirs(os.path.dirname(app_dst), exist_ok=True)
    with open(src) as f:
        text = re.sub(r"^Icon=.*$", "Icon=" + DESKTOP_ID, f.read(), flags=re.M)
    with open(app_dst, "w") as f:
        f.write(text)
    os.chmod(app_dst, 0o755)
    if shutil.which("update-desktop-database"):
        capture(["update-desktop-database", os.path.dirname(app_dst)])
    return app_dst


def steam_shortcut(ctx, xbe):
    """Add the non-Steam shortcut and its art. Never edits existing entries;
    refuses while Steam is running. Returns (summary, added)."""
    exe = os.path.join(ctx.app_dir, "burnout3.sh")
    if not os.path.isfile(exe):
        raise StepError("no-package", "no %s: run the package step first" % exe)
    if ctx.steam_config:
        cfg = ctx.steam_config
    else:
        users = steam.userdata_configs()
        if ctx.steam_user:
            users = [u for u in users if u[0] == ctx.steam_user]
            why = "--steam-user"
            chosen = users[0] if users else None
        else:
            chosen, why = steam.pick_user(users)
        if chosen is None:
            for uid, path, _ in users:
                ctx.out.log("steam", "user %s: %s" % (uid, path))
            code = "steam-not-found" if not users else "steam-user-ambiguous"
            raise StepError(code, why + (" (choose with --steam-user <id>)" if users else
                                         " (--steam-config <dir> names one)"))
        cfg = chosen[1]
        ctx.out.log("steam", "using Steam user %s: %s" % (chosen[0], why))
    if not ctx.steam_config and steam.steam_running():
        raise StepError("steam-running", "close Steam first: it rewrites shortcuts.vdf when it exits and "
                        "would drop the new entry")
    vdf = os.path.join(cfg, "shortcuts.vdf")
    grid = os.path.join(cfg, "grid")
    aid = steam.app_id('"%s"' % exe, SHORTCUT_NAME)
    aid, added = steam.add_shortcut(vdf, SHORTCUT_NAME, exe, ctx.app_dir, os.path.join(grid, "%d_icon.png" % aid))
    if not added:
        return "shortcut already in %s (app id %d), nothing changed" % (vdf, aid), False
    steam.make_art(xbe, grid, aid)
    return "added \"%s\" (app id %d) to %s, art in %s" % (SHORTCUT_NAME, aid, vdf, grid), True
