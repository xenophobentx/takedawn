#!/usr/bin/env bash
# Takedawn for the Steam Frame: a Linux ARM64 build in Docker (Colima on the
# Mac), packaged with the libraries the Frame's Steam Linux Runtime 4 lacks.
#
#   tools/frame/build.sh [image|build|package|all]
#
# Expects the toolkit beside this checkout as ../xboxrecomp (the takedawn
# branch of xenophobentx/xboxrecomp; another folder name in XBOXRECOMP_NAME),
# Docker (Colima on a Mac: colima start --memory 4 --cpu 4), and the
# generated code from ./regen.sh (SPLIT=250 keeps each file small enough for
# a 4 GB build VM). Output: build-frame/bin/burnout3 and the
# package build-frame/package/Takedawn. JOBS (default 3) compiles in parallel.
# The Frame's CPU has the ARMv8.2 crypto extensions; the installer's SHA-256
# needs them enabled (neither gcc 12 nor clang 15 take its per-function
# target attribute).
set -euo pipefail
repo="$(cd "$(dirname "$0")/../.." && pwd)"
parent="$(dirname "$repo")"
game="$(basename "$repo")"
toolkit="${XBOXRECOMP_NAME:-xboxrecomp}"
image="${TAKEDAWN_FRAME_IMAGE:-takedawn-frame-linux}"
step="${1:-all}"
jobs="${JOBS:-3}"

run() {
	docker run --rm -v "$parent":/src -w "/src/$game" "$image" bash -euo pipefail -c "$1"
}

do_image() { docker build -t "$image" -f "$repo/tools/frame/Dockerfile" "$repo/tools/frame"; }

do_build() {
	run "mkdir -p build-frame && cmake -S . -B build-frame -G Ninja -DCMAKE_BUILD_TYPE=Release \
		-DXBOXRECOMP_DIR=/src/$toolkit -DBO3_BIN_DIR=/src/$game/build-frame/bin \
		-DSDL2_DIR=/opt/sdl/lib/cmake/SDL2 -DSDL3_DIR=/opt/sdl/lib/cmake/SDL3 \
		-DCMAKE_PREFIX_PATH='/opt/glslang;/opt/vulkan;/opt/sdl' \
		-DVulkan_INCLUDE_DIR=/opt/vulkan/include \
		-DVulkan_LIBRARY=/usr/lib/aarch64-linux-gnu/libvulkan.so \
		-DCMAKE_C_FLAGS=-march=armv8.2-a+crypto -DCMAKE_CXX_FLAGS=-march=armv8.2-a+crypto \
		-DXBOXRECOMP_VK_XR=ON \
		${TAKEDAWN_CMAKE_ARGS:-} \
		> build-frame/configure.log 2>&1 || { tail -30 build-frame/configure.log; exit 1; }
	grep -E 'Vulkan backend|glslang|OpenXR' build-frame/configure.log || true
	nice -n 10 cmake --build build-frame --target burnout3 -j $jobs"
}

do_package() {
	run "pkg=build-frame/package/Takedawn; rm -rf \$pkg; mkdir -p \$pkg/lib; \
		cp build-frame/bin/burnout3 \$pkg/burnout3; cp tools/frame/LAUNCH-STEAM-FRAME.sh \$pkg/; \
		for l in libSDL2-2.0.so.0 libSDL3.so.0 libepoxy.so.0 libcrypto.so.3 libssl.so.3 libopenxr_loader.so.1; do \
			f=\$(LD_LIBRARY_PATH=/opt/sdl/lib:/usr/local/lib ldd \$pkg/burnout3 | awk -v L=\$l '\$1==L{print \$3}' || true); \
			[ -n \"\$f\" ] || f=\$(ls /opt/sdl/lib/\$l /usr/local/lib/\$l 2>/dev/null | head -1 || true); \
			[ -z \"\$f\" ] || cp -L \"\$f\" \$pkg/lib/; done; \
		patchelf --set-rpath '\$ORIGIN/lib' \$pkg/burnout3; \
		patchelf --set-rpath '\$ORIGIN' \$pkg/lib/libSDL2-2.0.so.0; \
		LD_LIBRARY_PATH=\$pkg/lib ldd \$pkg/burnout3 | grep -E 'not found|SDL|vulkan|openxr' || true; \
		objdump -T \$pkg/burnout3 | grep -o 'GLIBC_[0-9.]*' | sort -Vu | tail -1; ls -la \$pkg \$pkg/lib"
}

case "$step" in
	image) do_image ;;
	build) do_build ;;
	package) do_package ;;
	all) do_image; do_build; do_package ;;
	*) echo "unknown step $step" >&2; exit 2 ;;
esac
