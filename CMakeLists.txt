cmake_minimum_required(VERSION 3.20)
project(burnout3_recomp C)

# Burnout 3: Takedown (Xbox, 2004) - Static Recompilation
#
# Built on the shared xboxrecomp runtime, the same way hl2 is. The in-tree
# src/{kernel,d3d,audio,input,apu,nv2a,platform} fork is no longer built: it
# carried hand-written menus and render paths, and the goal now is the title's
# own boot sequence on the toolkit every other Xbox recomp uses.

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Half the cores for /MP: each cl.exe holds a whole generated chunk at /O2,
# and saturating cores and RAM is what makes the desktop unusable.
cmake_host_system_information(RESULT _bo3_cores QUERY NUMBER_OF_LOGICAL_CORES)
math(EXPR _bo3_default_jobs "${_bo3_cores} / 2")
if(_bo3_default_jobs LESS 1)
    set(_bo3_default_jobs 1)
endif()
set(BO3_COMPILE_JOBS "${_bo3_default_jobs}" CACHE STRING
    "Parallel cl.exe processes for the recompiled sources (/MP<n>)")

add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX)

# Windows: SDL2 window/audio/pad and the Vulkan backend, as on Linux (the
# toolkit's XBOXRECOMP_WIN_VULKAN); OFF is the older GDI + Direct3D 11 path.
# See docs/windows.md.
option(BO3_WIN_VULKAN "Windows: build the SDL2 + Vulkan host (default)" ON)
if(WIN32)
    set(XBOXRECOMP_WIN_VULKAN ${BO3_WIN_VULKAN} CACHE BOOL
        "Windows: SDL2 window/audio/pad and the Vulkan backend" FORCE)
endif()

# A release build (tools/release/build-*.sh): the program is named takedawn
# (Takedawn.exe), the generated code must carry tools/release/gen.sh's stamp
# for this checkout's regen key with the guest patches in
# (cmake/release_info.cmake), Windows gets a window program with the icon and
# version resource, macOS finds its libraries in the bundle's Frameworks.
# The target stays `burnout3`, and a build without this is what it always was
# (bin/burnout3).
option(BO3_RELEASE "Release build: named takedawn, stamped generated code required" OFF)
if(BO3_RELEASE)
    # What a packaged program does differently at run time (src/game/main.c:
    # the display defaults of a first start on a desktop).
    add_compile_definitions(BO3_RELEASE_BUILD=1)
endif()

option(BO3_ICALL_FEEDBACK "Record indirect-branch targets for codegen feedback" OFF)
if(BO3_ICALL_FEEDBACK)
    add_compile_definitions(RECOMP_ICALL_FEEDBACK)
endif()

option(BO3_ABI_CHECK "Report functions that clobber callee-saved registers" OFF)
if(BO3_ABI_CHECK)
    add_compile_definitions(RECOMP_ABI_CHECK)
endif()

set(XBOXRECOMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../xboxrecomp" CACHE PATH "Path to xboxrecomp toolkit")

# Off Windows the kernel links OpenSSL::Crypto but finds it in its own
# subdirectory, where the imported target is invisible to the executable;
# finding it here first puts it in scope for both (as blinx2-recomp does).
if(NOT WIN32 AND NOT ANDROID)
    find_package(OpenSSL REQUIRED)
endif()

# The toolkit's enhancements layer: render.scale, present.filter,
# present.fullscreen, and the dashboard video settings the title reads
# (display.widescreen, display.hdtv). enhance.toml beside the executable,
# RECOMP_* over it, every key stock by default (config/enhance.toml.default).
# -DBO3_ENHANCE=OFF builds a layer-free binary for A/B. Forced from
# BO3_ENHANCE, so a build folder configured before this (where the toolkit's
# option cached OFF) turns it on too. Only the apexJCL runtime has the layer.
option(BO3_ENHANCE "Build the toolkit's enhancements layer (enhance.toml)" ON)
if(EXISTS "${XBOXRECOMP_DIR}/src/enhance/enhance.h")
    set(XBOXRECOMP_ENHANCE ${BO3_ENHANCE} CACHE BOOL
        "Build the enhancements layer (enhance.toml, RECOMP_RENDER_SCALE, ...)" FORCE)
endif()

# The toolkit's in-game settings overlay (Dear ImGui over the Metal and Vulkan
# presenters; Back+Start, F1, Cmd+,): the pages edit the RECOMP_* settings and
# keep them in settings.toml in the save folder. C++ and Objective-C++, so the
# languages are enabled here for the executable's link too. -DBO3_OVERLAY=OFF
# builds without it. On Windows it rides on the Vulkan presenter.
option(BO3_OVERLAY "Build the toolkit's in-game settings overlay" ON)
if((NOT WIN32 OR BO3_WIN_VULKAN) AND EXISTS "${XBOXRECOMP_DIR}/src/overlay/overlay.h")
    set(XBOXRECOMP_OVERLAY ${BO3_OVERLAY} CACHE BOOL
        "Build the in-game settings overlay (Dear ImGui)" FORCE)
    if(BO3_OVERLAY)
        enable_language(CXX)
    endif()
endif()

add_subdirectory(${XBOXRECOMP_DIR} ${CMAKE_BINARY_DIR}/xboxrecomp)

add_subdirectory(src/game)

# The PS2 driving layout's unit test (src/game/controls.h, BO-106).
if(NOT ANDROID)
    add_subdirectory(tests/controls)
    # Which sound goes to which mixer bus (src/game/mixer.c, BO-114).
    add_subdirectory(tests/mixer)
    # The GP effects image as wired for the APU's model (BO-119).
    add_subdirectory(tests/gp)
    # Custom music at start: when the switch is due (src/game/soundtrack_start.h, BO-230).
    add_subdirectory(tests/start)
    # When a start-up failure may show a message box (src/game/fatal.c).
    add_subdirectory(tests/fatal)
    # The mod folders' lookup (src/game/modfs.c, docs/mod-folders.md).
    add_subdirectory(tests/modfs)
    # A mod's mod.toml (src/game/modmanifest.c).
    add_subdirectory(tests/modmanifest)
endif()

# Line-level symbols for the runtime libraries, so a hang sample inside one
# names the file and line rather than the nearest public function.
if(MSVC)
    foreach(_lib xbox_apu xbox_usb xbox_kernel)
        if(TARGET ${_lib})
            target_compile_options(${_lib} PRIVATE /Z7)
        endif()
    endforeach()
endif()
