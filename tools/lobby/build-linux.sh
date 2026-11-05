#!/usr/bin/env bash
# build-linux.sh [OUT]: a static Linux x86-64 takedawn-lobby binary, built in
# the Dockerfile's build stage (any Linux with docker; no compiler needed on
# the machine). OUT defaults to ./takedawn-lobby-linux-x86_64.
# DOCKER_BUILD_ARGS adds options to docker build (e.g. --cpu-shares=128).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="${1:-$PWD/takedawn-lobby-linux-x86_64}"
docker build -q ${DOCKER_BUILD_ARGS:-} --target build -t takedawn-lobby-build "$HERE" >/dev/null
id=$(docker create takedawn-lobby-build)
docker cp "$id:/src/takedawn-lobby" "$OUT"
docker rm "$id" >/dev/null
echo "$OUT"
