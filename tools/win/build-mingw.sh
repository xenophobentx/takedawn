#!/usr/bin/env bash
# Cross-build burnout3.exe (Windows x86-64, SDL2 + Vulkan) from macOS or Linux
# with llvm-mingw. Run tools/win/fetch-deps.sh once first.
#
#   tools/win/build-mingw.sh [extra cmake -D options]
#
# Environment: BO3_WINDEPS (deps prefix, default ~/.cache/bo3-windeps),
# XBOXRECOMP_DIR (toolkit checkout, default ../xboxrecomp-win next to the repo
# or ../xboxrecomp), BUILD_DIR (default build-mingw), JOBS.
# Output: bin/burnout3.exe, and SDL2.dll beside it.
set -euo pipefail
HERE="$(cd "$(dirname "$0")/../.." && pwd)"
D="${BO3_WINDEPS:-$HOME/.cache/bo3-windeps}"
TK="${XBOXRECOMP_DIR:-}"
if [ -z "$TK" ]; then
  for c in "$HERE/../xboxrecomp-win" "$HERE/../xboxrecomp"; do [ -d "$c" ] && TK="$c" && break; done
fi
[ -d "$TK" ] || { echo "toolkit checkout not found; set XBOXRECOMP_DIR" >&2; exit 1; }
[ -x "$D/llvm-mingw/bin/x86_64-w64-mingw32-clang" ] || "$HERE/tools/win/fetch-deps.sh"
B="${BUILD_DIR:-$HERE/build-mingw}"

# On a Windows host (Git Bash/MSYS) CMake is a native program: give it C:/...
# paths, which MSYS leaves alone, rather than /c/... ones it may not rewrite
# inside a ;-separated list.
wp() { if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else printf '%s' "$1"; fi; }

cmake -S "$(wp "$HERE")" -B "$(wp "$B")" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$(wp "$HERE/cmake/llvm-mingw-x86_64.cmake")" \
  -DLLVM_MINGW_ROOT="$(wp "$D/llvm-mingw")" \
  -DXBOXRECOMP_DIR="$(wp "$TK")" \
  -DCMAKE_PREFIX_PATH="$(wp "$D/SDL2/x86_64-w64-mingw32");$(wp "$D/glslang")" \
  -DVulkan_INCLUDE_DIR="$(wp "$D/vulkan/include")" \
  -DVulkan_LIBRARY="$(wp "$D/vulkan/lib/libvulkan-1.dll.a")" \
  -DXBOXRECOMP_OPENXR_INCLUDE_DIR="$(wp "$D/openxr/include")" \
  "$@"
cmake --build "$B" --target burnout3 -j"${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || nproc)}"
cp -f "$D/SDL2/x86_64-w64-mingw32/bin/SDL2.dll" "$HERE/bin/"
ls -l "$HERE/bin/burnout3.exe"
