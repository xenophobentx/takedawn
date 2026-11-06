# Build environment for Takedawn on the Steam Frame: Linux ARM64, run in the
# Steam Linux Runtime 4 (glibc 2.41), so built on Ubuntu 22.04 (glibc 2.35).
# The libraries Takedawn's Linux release carries, built from the same pins
# (tools/release/pins.env, tools/install/pins.json): SDL3 and sdl2-compat
# with the release's SDL3 patches, Vulkan headers and glslang. The Vulkan
# loader and driver come from the runtime on the Frame. Plus the Khronos
# OpenXR loader for the headset build.
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive CC=gcc-12 CXX=g++-12
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential gcc-12 g++-12 ninja-build pkg-config git ca-certificates wget python3 patch \
        libepoxy-dev libssl-dev libvulkan-dev patchelf file \
        libasound2-dev libpulse-dev libpipewire-0.3-dev libudev-dev libdbus-1-dev libibus-1.0-dev \
        libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxinerama-dev libxfixes-dev \
        libxss-dev libxtst-dev libxkbcommon-dev libdrm-dev libgbm-dev libegl1-mesa-dev libgl1-mesa-dev \
        libwayland-dev wayland-protocols libdecor-0-dev \
    && rm -rf /var/lib/apt/lists/*

ARG CMAKE_VERSION=3.31.6
RUN set -eux; arch="$(uname -m)"; \
    wget -q "https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-linux-${arch}.tar.gz"; \
    mkdir -p /opt/cmake; tar -xzf cmake-*.tar.gz -C /opt/cmake --strip-components=1; rm cmake-*.tar.gz
ENV PATH=/opt/cmake/bin:$PATH

# SDL3 + sdl2-compat (the game links SDL2; sdl2-compat runs it on SDL3)
ARG SDL3_VERSION=3.4.18
ARG SDL2COMPAT_VERSION=2.32.74
COPY sdl-patches/ /tmp/patches/
RUN set -eux; cd /tmp; \
    wget -q "https://github.com/libsdl-org/SDL/releases/download/release-${SDL3_VERSION}/SDL3-${SDL3_VERSION}.tar.gz"; \
    wget -q "https://github.com/libsdl-org/sdl2-compat/releases/download/release-${SDL2COMPAT_VERSION}/sdl2-compat-${SDL2COMPAT_VERSION}.tar.gz"; \
    tar -xzf SDL3-*.tar.gz; tar -xzf sdl2-compat-*.tar.gz; \
    for p in /tmp/patches/sdl3-*.patch; do [ -f "$p" ] && patch -d "SDL3-${SDL3_VERSION}" -p1 < "$p"; done; \
    cmake -S "SDL3-${SDL3_VERSION}" -B b3 -GNinja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/opt/sdl \
        -DCMAKE_INSTALL_LIBDIR=lib -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF \
        -DSDL_TEST_LIBRARY=OFF -DSDL_RPATH=OFF; \
    cmake --build b3 --target install; \
    cmake -S "sdl2-compat-${SDL2COMPAT_VERSION}" -B b2 -GNinja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/opt/sdl -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_PREFIX_PATH=/opt/sdl \
        -DSDL2COMPAT_TESTS=OFF -DSDL2COMPAT_STATIC=OFF; \
    cmake --build b2 --target install; rm -rf /tmp/*

# Vulkan headers and glslang (static), as tools/install/pins.json pins them
ARG VULKAN_SDK_TAG=vulkan-sdk-1.4.363.0
ARG GLSLANG_VERSION=16.6.0
RUN set -eux; cd /tmp; \
    wget -q -O vh.tar.gz "https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/${VULKAN_SDK_TAG}.tar.gz"; \
    tar -xzf vh.tar.gz; cmake -S Vulkan-Headers-* -B bvh -GNinja -DCMAKE_INSTALL_PREFIX=/opt/vulkan; \
    cmake --build bvh --target install; \
    wget -q -O gl.tar.gz "https://github.com/KhronosGroup/glslang/archive/refs/tags/${GLSLANG_VERSION}.tar.gz"; \
    tar -xzf gl.tar.gz; \
    cmake -S "glslang-${GLSLANG_VERSION}" -B bgl -GNinja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/opt/glslang \
        -DBUILD_SHARED_LIBS=OFF -DENABLE_OPT=OFF -DGLSLANG_TESTS=OFF -DENABLE_GLSLANG_BINARIES=OFF \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON; \
    cmake --build bgl --target install; rm -rf /tmp/*

# The Khronos OpenXR loader (release-1.1.63, as the other Frame ports use)
ARG OPENXR_VERSION=1.1.63
RUN set -eux; cd /tmp; \
    wget -q -O openxr.tar.gz "https://github.com/KhronosGroup/OpenXR-SDK/archive/refs/tags/release-${OPENXR_VERSION}.tar.gz"; \
    tar -xzf openxr.tar.gz; \
    cmake -S "OpenXR-SDK-release-${OPENXR_VERSION}" -B bxr -GNinja -DCMAKE_BUILD_TYPE=Release \
        -DDYNAMIC_LOADER=ON -DBUILD_TESTS=OFF; \
    cmake --build bxr --target install; rm -rf /tmp/*

# clang, as Takedawn's Linux release builds with: gcc 12 does not take the
# per-function "+sha2" target its installer's ARM SHA-256 path asks for.
RUN apt-get update && apt-get install -y --no-install-recommends clang-15 lld-15 \
    && rm -rf /var/lib/apt/lists/*
ENV CC=clang-15 CXX=clang++-15

# SPIRV-Tools (static, in /usr/local): the toolkit links SPIRV-Tools-opt and
# SPIRV-Tools after a static glslang on Linux, as the Steam Runtime SDK's
# glslang carries them. Same Vulkan SDK tag as the headers.
RUN set -eux; cd /tmp; \
    wget -q -O sh.tar.gz "https://github.com/KhronosGroup/SPIRV-Headers/archive/refs/tags/${VULKAN_SDK_TAG}.tar.gz"; \
    wget -q -O st.tar.gz "https://github.com/KhronosGroup/SPIRV-Tools/archive/refs/tags/${VULKAN_SDK_TAG}.tar.gz"; \
    tar -xzf sh.tar.gz; tar -xzf st.tar.gz; \
    cmake -S SPIRV-Tools-* -B bst -GNinja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local \
        -DSPIRV-Headers_SOURCE_DIR="$(ls -d /tmp/SPIRV-Headers-*)" -DSPIRV_SKIP_TESTS=ON -DSPIRV_SKIP_EXECUTABLES=ON \
        -DSPIRV_WERROR=OFF -DBUILD_SHARED_LIBS=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON; \
    cmake --build bst --target install; rm -rf /tmp/*
