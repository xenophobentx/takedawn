#!/usr/bin/env bash
# run_local.sh LOBBY_BINARY PORT OUTDIR [crowd.py options...]
# Starts LOBBY_BINARY on 127.0.0.1:PORT with --crowd-test, runs crowd.py
# against it with the options given, stops the lobby, and leaves in OUTDIR:
# lobby.log, crowd.out, crowd.json. Exit status: crowd.py's, or 3 when the
# lobby process died during the run.
set -u
BIN=$1; PORT=$2; OUT=$3; shift 3
HERE="$(cd "$(dirname "$0")" && pwd)"
mkdir -p "$OUT"
ulimit -n 8192 2>/dev/null || true
"$BIN" --port "$PORT" --bind 127.0.0.1 --public 127.0.0.1 --crowd-test --stats 60 >"$OUT/lobby.log" 2>&1 &
LPID=$!
sleep 1
if ! kill -0 "$LPID" 2>/dev/null; then echo "lobby did not start"; cat "$OUT/lobby.log"; exit 3; fi
python3 "$HERE/crowd.py" --port "$PORT" --server-pid "$LPID" --json-out "$OUT/crowd.json" "$@" >"$OUT/crowd.out" 2>&1
RC=$?
if kill -0 "$LPID" 2>/dev/null; then
    kill -INT "$LPID"
    wait "$LPID"
    echo "lobby exit status $?" >>"$OUT/crowd.out"
else
    wait "$LPID"
    echo "LOBBY DIED during the run: exit status $?" >>"$OUT/crowd.out"
    RC=3
fi
exit $RC
