#!/usr/bin/env bash
# smoke-linux.sh: run the Linux AppImage on the Linux build machine the way a
# player's machine would, in a scratch HOME there, without touching that
# machine's session, its Steam or anybody's folders.
#
#   tools/release/smoke-linux.sh [<AppImage>] [--keep]
#
# What runs there is tools/release/linux/smoke-remote.sh (its header lists the
# checks): --version; no data and no display (exit status 3, the RESULT
# line, no window); no data and a display (the game becomes the setup window,
# inside the mounted AppImage); the window's pages off screen, and on X11
# under a headless gamescope of its own; --install from TAKEDAWN_LINUX_DISC
# (a disc folder or image on that machine, read only); a headless boot to the
# main menu on the machine's Vulkan driver, with the reference saves copied
# from here (TAKEDAWN_REF_SAVES, default ./saves or the main checkout's).
#
# The machine is shared: the run holds the test-machine lock (TESTHOST_LOCK),
# starts one game at most, and refuses while another one runs. The scratch
# folder (<dir>/smoke, 2.5 GB after the install) is removed at the end unless
# --keep.
source "$(dirname "$0")/common.sh"
# A release build is quiet unless told (the toolkit's xr_log.h, BO-125): the
# checks below read the game's log, so it is switched on for every run here
# (and in the remote command below: ssh does not carry the environment).
export RECOMP_LOG_DIAG=1
APP=""; KEEP=0
for a in "$@"; do case "$a" in --keep) KEEP=1 ;; *) APP="$a" ;; esac; done
NAME="Takedawn-$VERSION-linux-x86_64.AppImage"
# the package's discs (any of them may be the one given to the install)
package_manifest_ids() {
    local r ids=""
    for r in $PKG_REGIONS; do use_region "$r"; ids="$ids${ids:+,}$(manifest_id)"; done
    use_region "${PKG_REGIONS%% *}"
    printf '%s' "$ids"
}
APP="${APP:-$DIST/$NAME}"
[ -f "$APP" ] || die "no AppImage at $APP"
HOST="${TAKEDAWN_LINUX_HOST:?TAKEDAWN_LINUX_HOST: the Linux machine}"
LDIR="${TAKEDAWN_LINUX_DIR:-takedawn-build/release}"
ssh "$HOST" "mkdir -p $LDIR/smoke-in/saves $LDIR/smoke-in/src/tools/headless" || die "cannot reach the Linux machine"
rsync -a "$APP" "$HOST:$LDIR/smoke-in/$NAME"
rsync -a "$REPO/tools/headless/" "$HOST:$LDIR/smoke-in/src/tools/headless/"
rsync -a "$REPO/tools/release/linux/smoke-remote.sh" "$HOST:$LDIR/smoke-in/smoke-remote.sh"
rsync -a "$REPO/tools/release/test-user-dirs.sh" "$HOST:$LDIR/smoke-in/test-user-dirs.sh"
rsync -a "$REPO/tools/release/test-arg-order.sh" "$HOST:$LDIR/smoke-in/test-arg-order.sh"
SAVES_REF="${TAKEDAWN_REF_SAVES:-}"
for c in "$REPO/saves" "$(git -C "$REPO" worktree list --porcelain | sed -n '1s/^worktree //p')/saves"; do
    [ -z "$SAVES_REF" ] && [ -d "$c/UDATA" ] && SAVES_REF="$c"
done
saves_arg=""
if [ -n "$SAVES_REF" ]; then
    # The profile only: the partition images are remade by the game.
    rsync -a --delete --include 'UDATA/***' --include 'TDATA/***' --exclude '*' "$SAVES_REF/" "$HOST:$LDIR/smoke-in/saves/"
    saves_arg="\$PWD/$LDIR/smoke-in/saves"
fi
cmd="cd ~ && export RECOMP_LOG_DIAG=1 && bash $LDIR/smoke-in/smoke-remote.sh $LDIR/smoke-in/$NAME $LDIR/smoke $(package_manifest_ids) \$PWD/$LDIR/smoke-in/src '${TAKEDAWN_LINUX_DISC:-}' \"$saves_arg\"; st=\$?; [ $KEEP = 1 ] || rm -rf $LDIR/smoke; exit \$st"
if [ -n "${TESTHOST_LOCK:-}" ] && [ -x "$TESTHOST_LOCK" ]; then
    "$TESTHOST_LOCK" run "$HOST" burnout3/release "release smoke run (Linux AppImage)" 900 -- ssh "$HOST" "$cmd" \
        | grep -v "^acquired\|^released"
    exit "${PIPESTATUS[0]}"
else
    ssh "$HOST" "$cmd"
fi
