#!/usr/bin/env bash
# Fetch and build what the Android build needs besides the NDK, on a Linux or
# macOS host, into one prefix outside the repos:
#
#   $BO3_ANDROID_DEPS  (default ~/.cache/bo3-android-deps)
#     SDL2/      libSDL2.so for arm64-v8a, headers, its CMake package, and
#                java/: SDL's activity sources (org.libsdl.app), same version
#     glslang/   glslang built static (no SPIRV-Tools optimizer), as on Windows
#     adrenotools/  libadrenotools (BSD-2-Clause, a pinned commit): the static
#                library and its four hook libraries, which load a replacement
#                GPU driver into the app
#     turnip-a725/  libvulkan_freedreno.so: the Turnip build the APK carries by
#                default (prebuilt, see BO-152 below), and its SOURCE.txt
#     turnip/    libvulkan_freedreno.so: Mesa's Turnip driver for Adreno GPUs
#                (MIT), built here from Mesa's own release tarball (pinned by
#                the sha256 of its release notes) the way docs.mesa3d.org's
#                android page says; BO3_ANDROID_TURNIP=0 skips it
#     ncnn/      ncnn (Tencent, BSD-3-Clause), built static from its release's
#                full source (pinned by sha256) for the menu pictures' upscale
#                in the app (BO-326, docs/upscale-art.md): CPU and Vulkan, on
#                the glslang above, only the layers the model uses
#     upscaler/  the fast model of that upscale (realesr-general-x4v3 at
#                denoise 0.3, Real-ESRGAN's weights, BSD-3-Clause): the files
#                tools/release/pins.env pins, blended as the desktop packages'
#
# The Vulkan headers and libvulkan.so are the NDK's own.
#
# Needs: curl, tar, git, cmake (3.20+), ninja, python3 (with venv; meson, mako
# and pyyaml are installed into a venv in the prefix), bison, flex,
# pkg-config, a host C++ compiler (for glslangValidator, which Mesa's build
# runs), and the NDK:
#   ANDROID_NDK_HOME, or the newest under $ANDROID_HOME/ndk (default ~/android-sdk).
# ANDROID_API (default 33) is the lowest Android the libraries will load on.
# Each step is skipped when its output exists; delete the directory to redo it.
set -euo pipefail

PREFIX="${BO3_ANDROID_DEPS:-$HOME/.cache/bo3-android-deps}"
SDK="${ANDROID_HOME:-$HOME/android-sdk}"
NDK="${ANDROID_NDK_HOME:-$(ls -d "$SDK"/ndk/* 2>/dev/null | sort -V | tail -1)}"
API="${ANDROID_API:-33}"
ABI=arm64-v8a
SDL2_VER=2.32.10
GLSLANG_TAG=16.6.0
ADRENOTOOLS_URL=https://github.com/bylaws/libadrenotools.git
ADRENOTOOLS_COMMIT=8fae8ce254dfc1344527e05301e43f37dea2df80     # 2024-09-10, master
MESA_VER=26.2.4
MESA_SHA256=bce5f7fbebb934373b86c999a064d52fb5065878dc57f287f95346648ec832e9
NCNN_VER=20260526
NCNN_SHA256=754659d6fe65545cf2ef4483ffb84526fea631f8764c44b150f1601d0fb4004b
# The layers of the fast model (Input, Split, Convolution, PReLU,
# PixelShuffle, Interp, BinaryOp) and the ones ncnn makes for itself
# (packing, casts, padding, the fused activations); every other is left out.
NCNN_LAYERS="input split convolution prelu pixelshuffle interp binaryop padding packing cast crop flatten
             reshape innerproduct bias scale relu clip sigmoid mish hardswish noop memorydata"
JOBS="${JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu)}"

[ -f "$NDK/build/cmake/android.toolchain.cmake" ] || { echo "NDK not found (ANDROID_NDK_HOME)" >&2; exit 1; }
TC=(-G Ninja -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake"
    -DANDROID_ABI=$ABI -DANDROID_PLATFORM=android-$API -DANDROID_STL=c++_shared)
# Configure the Android libraries with source paths relative to the prefix, so
# __FILE__ in them (SDL's logs and asserts) does not carry this machine's folder.
# CMake reads CFLAGS and CXXFLAGS once, when it configures; delete a library's
# folder in the prefix to rebuild it with this.
tc_cmake() {
  CFLAGS="-ffile-prefix-map=$PREFIX/= ${CFLAGS:-}" CXXFLAGS="-ffile-prefix-map=$PREFIX/= ${CXXFLAGS:-}" cmake "$@"
}

mkdir -p "$PREFIX/dl"
HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$PREFIX"
fetch() { [ -s "dl/$2" ] || curl -fL --retry 3 -o "dl/$2" "$1"; }

if [ ! -f SDL2/lib/libSDL2.so ]; then
  A="SDL2-$SDL2_VER.tar.gz"
  fetch "https://github.com/libsdl-org/SDL/releases/download/release-$SDL2_VER/$A" "$A"
  rm -rf SDL2-src SDL2-build && mkdir SDL2-src && tar -xf "dl/$A" -C SDL2-src --strip-components 1
  tc_cmake -S SDL2-src -B SDL2-build "${TC[@]}" -DCMAKE_INSTALL_PREFIX="$PREFIX/SDL2" \
    -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST=OFF
  cmake --build SDL2-build -j"$JOBS"
  cmake --install SDL2-build
  # The activity that loads it: the Java half must be the same version.
  rm -rf SDL2/java && mkdir -p SDL2/java
  cp -R SDL2-src/android-project/app/src/main/java/org SDL2/java/
fi

if [ ! -f glslang/lib/libglslang.a ]; then
  A="glslang-$GLSLANG_TAG.tar.gz"
  fetch "https://github.com/KhronosGroup/glslang/archive/refs/tags/$GLSLANG_TAG.tar.gz" "$A"
  rm -rf glslang-src glslang-build && mkdir glslang-src && tar -xf "dl/$A" -C glslang-src --strip-components 1
  tc_cmake -S glslang-src -B glslang-build "${TC[@]}" -DCMAKE_INSTALL_PREFIX="$PREFIX/glslang" \
    -DBUILD_SHARED_LIBS=OFF -DENABLE_OPT=OFF -DENABLE_GLSLANG_BINARIES=OFF \
    -DENABLE_CTEST=OFF -DBUILD_EXTERNAL=OFF -DGLSLANG_TESTS=OFF
  cmake --build glslang-build -j"$JOBS"
  cmake --install glslang-build
fi
if [ ! -f adrenotools/lib/libadrenotools.a ]; then
  if [ ! -d adrenotools-src/.git ]; then
    rm -rf adrenotools-src
    git clone -q --recursive "$ADRENOTOOLS_URL" adrenotools-src
  fi
  git -C adrenotools-src checkout -q "$ADRENOTOOLS_COMMIT"
  git -C adrenotools-src submodule update -q --init --recursive
  rm -rf adrenotools-build
  tc_cmake -S adrenotools-src -B adrenotools-build "${TC[@]}" -DCMAKE_INSTALL_PREFIX="$PREFIX/adrenotools" \
    -DBUILD_SHARED_LIBS=OFF -DGEN_INSTALL_TARGET=ON
  cmake --build adrenotools-build -j"$JOBS"
  cmake --install adrenotools-build
  # Its install leaves out the library it embeds.
  cp adrenotools-build/lib/linkernsbypass/liblinkernsbypass.a adrenotools/lib/
  mkdir -p adrenotools/licenses
  cp adrenotools-src/LICENSE adrenotools/licenses/libadrenotools-LICENSE
  cp adrenotools-src/lib/linkernsbypass/LICENSE adrenotools/licenses/linkernsbypass-LICENSE
fi

if [ "${BO3_ANDROID_TURNIP:-1}" != 0 ] && [ ! -f turnip/libvulkan_freedreno.so ]; then
  HOST_TAG="$(ls "$NDK/toolchains/llvm/prebuilt" | head -1)"
  BIN="$NDK/toolchains/llvm/prebuilt/$HOST_TAG/bin"
  [ -x venv/bin/meson ] || { python3 -m venv venv && venv/bin/pip -q install meson mako pyyaml packaging; }
  # Mesa compiles Turnip's ray-tracing helpers with glslangValidator at build
  # time: a host build of the same glslang source.
  if [ ! -x glslang-host/bin/glslangValidator ]; then
    A="glslang-$GLSLANG_TAG.tar.gz"
    fetch "https://github.com/KhronosGroup/glslang/archive/refs/tags/$GLSLANG_TAG.tar.gz" "$A"
    [ -d glslang-src ] || { mkdir glslang-src && tar -xf "dl/$A" -C glslang-src --strip-components 1; }
    rm -rf glslang-host-build
    cmake -S glslang-src -B glslang-host-build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX="$PREFIX/glslang-host" -DBUILD_SHARED_LIBS=OFF -DENABLE_OPT=OFF \
      -DENABLE_CTEST=OFF -DBUILD_EXTERNAL=OFF -DGLSLANG_TESTS=OFF
    cmake --build glslang-host-build -j"$JOBS"
    cmake --install glslang-host-build
    [ -x glslang-host/bin/glslangValidator ] || ln -s glslang glslang-host/bin/glslangValidator
  fi
  A="mesa-$MESA_VER.tar.xz"
  fetch "https://archive.mesa3d.org/$A" "$A"
  echo "$MESA_SHA256  dl/$A" | sha256sum -c - >/dev/null || { echo "mesa: checksum mismatch" >&2; exit 1; }
  rm -rf mesa-src mesa-build && mkdir mesa-src && tar -xf "dl/$A" -C mesa-src --strip-components 1
  # Our patches to Mesa's source (each file says what it is and where it is
  # from): tools/android/patches/mesa-$MESA_VER-*.patch.
  for p in "$HERE"/patches/mesa-"$MESA_VER"-*.patch; do
    [ -f "$p" ] || continue
    (cd mesa-src && git apply -p1 "$p") || { echo "mesa: $p does not apply" >&2; exit 1; }
    MESA_PATCHES="${MESA_PATCHES:+$MESA_PATCHES, }$(basename "$p")"
  done
  mkdir -p mesa-pkgconfig
  # After the cross file of https://docs.mesa3d.org/android.html (no ccache).
  cat > mesa-cross.ini <<CROSS
[binaries]
ar = '$BIN/llvm-ar'
c = ['$BIN/aarch64-linux-android$API-clang']
cpp = ['$BIN/aarch64-linux-android$API-clang++', '-fno-exceptions', '-fno-unwind-tables', '-fno-asynchronous-unwind-tables', '--start-no-unused-arguments', '-static-libstdc++', '--end-no-unused-arguments']
c_ld = 'lld'
cpp_ld = 'lld'
strip = '$BIN/llvm-strip'
pkg-config = ['env', 'PKG_CONFIG_LIBDIR=$PREFIX/mesa-pkgconfig', 'pkg-config']

[host_machine]
system = 'android'
cpu_family = 'aarch64'
cpu = 'armv8'
endian = 'little'
CROSS
  PATH="$PREFIX/venv/bin:$PREFIX/glslang-host/bin:$PATH" meson setup mesa-build mesa-src --cross-file mesa-cross.ini \
    --buildtype release -Dplatforms=android -Dplatform-sdk-version="$API" \
    -Dandroid-stub=true -Dandroid-libbacktrace=disabled -Degl=disabled \
    -Dgallium-drivers= -Dvulkan-drivers=freedreno -Dfreedreno-kmds=kgsl
  PATH="$PREFIX/venv/bin:$PREFIX/glslang-host/bin:$PATH" meson compile -C mesa-build -j "$JOBS"
  mkdir -p turnip
  "$BIN/llvm-strip" --strip-unneeded -o turnip/libvulkan_freedreno.so \
    mesa-build/src/freedreno/vulkan/libvulkan_freedreno.so
  printf 'Mesa %s Turnip (freedreno Vulkan, KGSL), built from %s\nsha256 of the tarball: %s\nNDK %s, android-%s\npatches: %s\n' \
    "$MESA_VER" "https://archive.mesa3d.org/$A" "$MESA_SHA256" "$(basename "$NDK")" "$API" "${MESA_PATCHES:-none}" > turnip/SOURCE.txt
  cp mesa-src/docs/license.rst turnip/mesa-license.rst
fi
# BO-326: ncnn for the menu pictures' upscale in the app. Its own glslang is
# not built: it takes the one above (NCNN_SYSTEM_GLSLANG), so libmain.so has
# one copy. SIMPLEVK: ncnn opens the Vulkan loader itself, and the game hands
# it its own (upscale.c), the one that reaches the app's Turnip. SIMPLEOMP:
# its threads without an OpenMP runtime. No int8 or bf16 paths (the model is
# float).
if [ ! -f ncnn/lib/libncnn.a ]; then
  A="ncnn-$NCNN_VER-full-source.zip"
  fetch "https://github.com/Tencent/ncnn/releases/download/$NCNN_VER/$A" "$A"
  echo "$NCNN_SHA256  dl/$A" | sha256sum -c - >/dev/null || { echo "ncnn: checksum mismatch" >&2; exit 1; }
  rm -rf ncnn-src ncnn-build && mkdir ncnn-src && unzip -q "dl/$A" -d ncnn-src
  NCNN_OFF=()
  for l in $(grep -o 'ncnn_add_layer([A-Za-z0-9_]*' ncnn-src/src/CMakeLists.txt | sed 's/ncnn_add_layer(//' | tr 'A-Z' 'a-z'); do
    case " $(echo $NCNN_LAYERS) " in *" $l "*) ;; *) NCNN_OFF+=(-DWITH_LAYER_$l=OFF) ;; esac
  done
  tc_cmake -S ncnn-src -B ncnn-build "${TC[@]}" -DCMAKE_INSTALL_PREFIX="$PREFIX/ncnn" \
    -DNCNN_VULKAN=ON -DNCNN_SYSTEM_GLSLANG=ON -Dglslang_DIR="$PREFIX/glslang/lib/cmake/glslang" \
    -DNCNN_SIMPLEVK=ON -DNCNN_OPENMP=ON -DNCNN_SIMPLEOMP=ON -DNCNN_INT8=OFF -DNCNN_BF16=OFF \
    -DNCNN_C_API=OFF -DNCNN_PLATFORM_API=OFF -DNCNN_PIXEL_ROTATE=OFF -DNCNN_PIXEL_AFFINE=OFF \
    -DNCNN_PIXEL_DRAWING=OFF -DNCNN_BUILD_TOOLS=OFF -DNCNN_BUILD_EXAMPLES=OFF -DNCNN_BUILD_BENCHMARK=OFF \
    -DNCNN_BUILD_TESTS=OFF "${NCNN_OFF[@]}"
  cmake --build ncnn-build -j"$JOBS"
  cmake --install ncnn-build
  cp ncnn-src/LICENSE.txt ncnn/LICENSE.txt
  printf 'ncnn %s, built from https://github.com/Tencent/ncnn/releases/download/%s/%s\nsha256 of the archive: %s\n' \
    "$NCNN_VER" "$NCNN_VER" "$A" "$NCNN_SHA256" > ncnn/SOURCE.txt
fi
# The fast model, as the desktop packages make it (tools/release/common.sh
# stage_upscaler): the files and hashes of tools/release/pins.env, blended
# 0.3 / 0.7 by tools/release/upscaler/blend_ncnn.py (the standard library's
# Python only), and the result checked against its own pinned hash.
if [ ! -f upscaler/realesr-general-dn030-x4v3.bin ]; then
  PINS="$HERE/../release/pins.env"
  UPSCALER_FAST_BASE="$(sed -n 's/^UPSCALER_FAST_BASE=//p' "$PINS")"
  pin() { sed -n "s/^$1=//p" "$PINS"; }
  fetch "$UPSCALER_FAST_BASE/RealESRGAN_General_x4_v3.param" RealESRGAN_General_x4_v3.param
  fetch "$UPSCALER_FAST_BASE/RealESRGAN_General_x4_v3.bin" RealESRGAN_General_x4_v3.bin
  fetch "$UPSCALER_FAST_BASE/RealESRGAN_General_WDN_x4_v3.bin" RealESRGAN_General_WDN_x4_v3.bin
  echo "$(pin UPSCALER_GENERAL_PARAM_SHA256)  dl/RealESRGAN_General_x4_v3.param" | sha256sum -c - >/dev/null \
    && echo "$(pin UPSCALER_GENERAL_BIN_SHA256)  dl/RealESRGAN_General_x4_v3.bin" | sha256sum -c - >/dev/null \
    && echo "$(pin UPSCALER_WDN_BIN_SHA256)  dl/RealESRGAN_General_WDN_x4_v3.bin" | sha256sum -c - >/dev/null \
    || { echo "upscaler: a model file's checksum is not the pinned one" >&2; exit 1; }
  mkdir -p upscaler.new
  "${TAKEDAWN_PYTHON:-python3}" "$HERE/../release/upscaler/blend_ncnn.py" dl/RealESRGAN_General_x4_v3.param \
    dl/RealESRGAN_General_x4_v3.bin dl/RealESRGAN_General_WDN_x4_v3.bin 0.3 upscaler.new/realesr-general-dn030-x4v3.bin
  echo "$(pin UPSCALER_FAST_SHA256)  upscaler.new/realesr-general-dn030-x4v3.bin" | sha256sum -c - >/dev/null \
    || { rm -rf upscaler.new; echo "upscaler: the blended model is not the pinned one" >&2; exit 1; }
  cp dl/RealESRGAN_General_x4_v3.param upscaler.new/realesr-general-dn030-x4v3.param
  rm -rf upscaler && mv upscaler.new upscaler
fi

# BO-152: the Turnip the APK carries by default, a prebuilt one. The Mesa
# 26.2.4 build above hangs the Retroid Pocket 5's Adreno 650 every few
# minutes in races; this build ran without a hang. It is whitebelyash's
# experimental Mesa 26.2.0-devel (git-7fdde2f8f8, his gen8 work on
# https://github.com/whitebelyash/mesa-tu8) as UnleashedRecomp-Android ships it
# for its "A725 Performance" driver setting, taken out of that app's release
# APK and checked against the sha256 its driver/README.md gives. Mesa: MIT.
A725_APK_URL="https://github.com/SansNope/UnleashedRecomp-Android/releases/download/v0.6.1/UnleashedRecomp_0.6.1.apk"
A725_APK_SHA256=05717f167db907ca0d0f8e45112613c7e24ef0737ecdc5e192a430a047f74c34
A725_ENTRY=assets/bundled_driver/vulkan.wb26_2_rp_pair_ccu_color_a725.so
A725_SHA256=87f371a26a4678a092efa0d209be550ccaae0d18c96436aec181d558e53b22b0
if [ "${BO3_ANDROID_TURNIP:-1}" != 0 ] && [ ! -f turnip-a725/libvulkan_freedreno.so ]; then
  A=UnleashedRecomp_0.6.1.apk
  fetch "$A725_APK_URL" "$A"
  echo "$A725_APK_SHA256  dl/$A" | sha256sum -c - >/dev/null || { echo "turnip-a725: APK checksum mismatch" >&2; exit 1; }
  mkdir -p turnip-a725
  unzip -p "dl/$A" "$A725_ENTRY" > turnip-a725/libvulkan_freedreno.so
  echo "$A725_SHA256  turnip-a725/libvulkan_freedreno.so" | sha256sum -c - >/dev/null \
    || { rm -f turnip-a725/libvulkan_freedreno.so; echo "turnip-a725: driver checksum mismatch" >&2; exit 1; }
  cat > turnip-a725/SOURCE.txt <<SRC
Mesa 26.2.0-devel Turnip (freedreno Vulkan, KGSL), git-7fdde2f8f8: whitebelyash's experimental build (his gen8 work, https://github.com/whitebelyash/mesa-tu8), with a render-pass dependency of CCU_CLEAN_COLOR | WAIT_FOR_ME
prebuilt, not built here: $A725_ENTRY of $A725_APK_URL (sha256 of the APK $A725_APK_SHA256), where UnleashedRecomp-Android ships it as its "A725 Performance" driver (its driver/README.md)
sha256 of the driver: $A725_SHA256
run with TU_DEBUG=sysmem,nobin, as that app does
licence: MIT (Mesa's docs/license.rst: Mesa-license.txt, Mesa-MIT.txt)
SRC
fi
echo "deps ready in $PREFIX (NDK $(basename "$NDK"), android-$API, $ABI)"
