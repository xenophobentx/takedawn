#!/usr/bin/env bash
# Fetch and build everything the Windows (Vulkan-only) cross build needs, on a
# macOS or Linux host, into one prefix outside the repos:
#
#   $BO3_WINDEPS  (default ~/.cache/bo3-windeps)
#     llvm-mingw/   clang + lld + mingw-w64 (UCRT), pinned
#     SDL2/         SDL2 mingw development package (headers, import lib, SDL2.dll)
#     openxr/       the OpenXR SDK (headers used; the loader is the runtime's)
#     vulkan/       Vulkan-Headers + a generated vulkan-1 import library
#                   (the loader itself, vulkan-1.dll, is the user's GPU driver's)
#     glslang/      glslang built static for mingw (no SPIRV-Tools optimizer)
#
# Needs: curl, tar, cmake, ninja, python3. Each step is skipped when its output
# exists; delete the directory to redo it.
set -euo pipefail

HERE="$(cd "$(dirname "$0")/../.." && pwd)"
PREFIX="${BO3_WINDEPS:-$HOME/.cache/bo3-windeps}"
LLVM_MINGW_VER=20260922
SDL2_VER=2.32.10
VK_HEADERS_TAG=vulkan-sdk-1.4.363.0
GLSLANG_TAG=16.6.0
OPENXR_TAG=release-1.1.47

case "$(uname -s)" in
  Darwin) LLVM_ASSET="llvm-mingw-$LLVM_MINGW_VER-ucrt-macos-universal.tar.xz" ;;
  Linux)  LLVM_ASSET="llvm-mingw-$LLVM_MINGW_VER-ucrt-ubuntu-22.04-$(uname -m | sed s/aarch64/aarch64/).tar.xz" ;;
  # Native Windows build from Git Bash/MSYS: the toolchain's Windows release.
  MINGW*|MSYS*|CYGWIN*) LLVM_ASSET="llvm-mingw-$LLVM_MINGW_VER-ucrt-x86_64.zip" ;;
  *) echo "unsupported host" >&2; exit 1 ;;
esac
# CMake is a native program on Windows: hand it C:/... paths (see build-mingw.sh).
wp() { if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else printf '%s' "$1"; fi; }

mkdir -p "$PREFIX/dl"
cd "$PREFIX"
fetch() { [ -s "dl/$2" ] || curl -fL --retry 3 -o "dl/$2" "$1"; }

if [ ! -x llvm-mingw/bin/x86_64-w64-mingw32-clang ]; then
  fetch "https://github.com/mstorsjo/llvm-mingw/releases/download/$LLVM_MINGW_VER/$LLVM_ASSET" "$LLVM_ASSET"
  mkdir -p llvm-mingw
  case "$LLVM_ASSET" in
    *.zip) rm -rf llvm-mingw.tmp && unzip -q "dl/$LLVM_ASSET" -d llvm-mingw.tmp \
             && mv llvm-mingw.tmp/*/* llvm-mingw/ && rm -rf llvm-mingw.tmp ;;
    *) tar -xf "dl/$LLVM_ASSET" -C llvm-mingw --strip-components 1 ;;
  esac
fi
export PATH="$PREFIX/llvm-mingw/bin:$PATH"

if [ ! -d SDL2/x86_64-w64-mingw32 ]; then
  A="SDL2-devel-$SDL2_VER-mingw.tar.gz"
  fetch "https://github.com/libsdl-org/SDL/releases/download/release-$SDL2_VER/$A" "$A"
  mkdir -p SDL2 && tar -xf "dl/$A" -C SDL2 --strip-components 1
fi

if [ ! -f vulkan/lib/libvulkan-1.dll.a ]; then
  A="Vulkan-Headers-$VK_HEADERS_TAG.tar.gz"
  fetch "https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/$VK_HEADERS_TAG.tar.gz" "$A"
  rm -rf vulkan && mkdir -p vulkan/lib vulkan/src
  tar -xf "dl/$A" -C vulkan/src --strip-components 1
  mkdir -p vulkan/include && cp -R vulkan/src/include/vulkan vulkan/src/include/vk_video vulkan/include/
  # vulkan-1.dll exports every Vulkan entry point; list the ones the headers
  # declare. The import library only binds names the program uses.
  { echo "LIBRARY vulkan-1.dll"; echo "EXPORTS";
    grep -h -o -E 'VKAPI_CALL +vk[A-Za-z0-9_]+' vulkan/include/vulkan/vulkan_core.h \
        vulkan/include/vulkan/vulkan_win32.h | awk '{print $2}' | sort -u; } > vulkan/lib/vulkan-1.def
  x86_64-w64-mingw32-dlltool -m i386:x86-64 -d vulkan/lib/vulkan-1.def -l vulkan/lib/libvulkan-1.dll.a
fi

# The OpenXR headers (Apache-2.0 or MIT; src/video/oxr.c: a VR headset; the loader is opened at run time, so only
# the headers are needed to build).
if [ ! -f openxr/include/openxr/openxr.h ]; then
  A="OpenXR-SDK-$OPENXR_TAG.tar.gz"
  fetch "https://github.com/KhronosGroup/OpenXR-SDK/archive/refs/tags/$OPENXR_TAG.tar.gz" "$A"
  rm -rf openxr && mkdir -p openxr
  tar -xf "dl/$A" -C openxr --strip-components 1
fi

if [ ! -f glslang/lib/libglslang.a ]; then
  A="glslang-$GLSLANG_TAG.tar.gz"
  fetch "https://github.com/KhronosGroup/glslang/archive/refs/tags/$GLSLANG_TAG.tar.gz" "$A"
  rm -rf glslang-src && mkdir glslang-src && tar -xf "dl/$A" -C glslang-src --strip-components 1
  cmake -S glslang-src -B glslang-build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$(wp "$HERE/cmake/llvm-mingw-x86_64.cmake")" -DLLVM_MINGW_ROOT="$(wp "$PREFIX/llvm-mingw")" \
    -DCMAKE_INSTALL_PREFIX="$(wp "$PREFIX/glslang")" -DBUILD_SHARED_LIBS=OFF \
    -DENABLE_OPT=OFF -DENABLE_GLSLANG_BINARIES=OFF -DENABLE_CTEST=OFF -DBUILD_EXTERNAL=OFF
  cmake --build glslang-build -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc)"
  cmake --install glslang-build
fi
echo "deps ready in $PREFIX"
