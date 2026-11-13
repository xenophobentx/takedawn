cmake_minimum_required(VERSION 3.20)
project(burnout3_game C)

# Burnout 3 - recompiled game executable. main.c brings up the runtime and
# jumps to the XBE entry point; recomp_manual.c is where a function the lifter
# got wrong is replaced. Everything else is generated (gen/, gitignored).

# The generated C: recomp/gen, or recomp/gen.pal-ensv for the PAL En/Es/Nl/Sv
# disc (BO3_GEN_NAME, ../../CMakeLists.txt).
if(NOT BO3_GEN_NAME)
    set(BO3_GEN_NAME gen)
endif()
set(BO3_GEN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/recomp/${BO3_GEN_NAME}")
file(GLOB RECOMP_GEN_SOURCES CONFIGURE_DEPENDS
    "${BO3_GEN_DIR}/recomp_*.c"
)
# The PAL builds' wrapped functions are named by their own addresses
# (recomp_manual_pal.c, recomp_manual_pal_ensv.c; recomp_manual.c has the
# NTSC-U ones).
if(BO3_REGION STREQUAL "PAL")
    list(APPEND RECOMP_GEN_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/recomp/recomp_manual_pal.c")
elseif(BO3_REGION STREQUAL "PAL-ENSV")
    list(APPEND RECOMP_GEN_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/recomp/recomp_manual_pal_ensv.c")
endif()

if(ANDROID)
    # Android: the game is libmain.so, which SDL's activity loads and whose
    # SDL_main it calls (docs/android.md). No shader pre-build: that one
    # starts the executable again, and an app has none.
    add_library(burnout3 SHARED
        main.c
        install.c
        package.c
        languages.c
        fatal.c
        controls.c
        autopilot.c
        softbody.c
        menuentry.c
        tutorial.c
        scoreboard.c
        unlockall.c
        glyphs.c
        soundtrack.c
        dsound_notify.c
        soundbank.c
        traffic_bp.c
        hirefresh.c
        hraudit.c
    mixer.c
    gp_image.c
    pcmanifest.c
        recomp/recomp_manual.c
        ${RECOMP_GEN_SOURCES}
    )
    # One app carries the three programs (package.h): libmain.so (NTSC-U),
    # libmain_pal.so, libmain_ensv.so; the app loads the installed disc's.
    if(BO3_REGION STREQUAL "PAL")
        set_target_properties(burnout3 PROPERTIES OUTPUT_NAME main_pal)
    elseif(BO3_REGION STREQUAL "PAL-ENSV")
        set_target_properties(burnout3 PROPERTIES OUTPUT_NAME main_ensv)
    else()
        set_target_properties(burnout3 PROPERTIES OUTPUT_NAME main)
    endif()
    # Sharper menu pictures in the app (BO-326, upscale.c): the fast model run
    # in this library with ncnn (tools/android/fetch-deps.sh builds it), as no
    # program of its own can be started. Without ncnn the step is not offered.
    target_sources(burnout3 PRIVATE upscale.c upscale_art.c upscale_tile.c)
    find_package(ncnn CONFIG QUIET)
    if(TARGET ncnn)
        enable_language(CXX)
        # ncnn's package names "pthread" to link, which bionic has in libc (no
        # libpthread on Android).
        get_target_property(_ncnn_libs ncnn INTERFACE_LINK_LIBRARIES)
        list(REMOVE_ITEM _ncnn_libs pthread)
        set_target_properties(ncnn PROPERTIES INTERFACE_LINK_LIBRARIES "${_ncnn_libs}")
        # A library of its own: ncnn's target asks its users for -fno-rtti
        # and -fno-exceptions, which are for this file alone.
        add_library(bo3_upscale_ncnn STATIC upscale_ncnn.cpp)
        target_link_libraries(bo3_upscale_ncnn PRIVATE ncnn)
        target_link_libraries(burnout3 PRIVATE bo3_upscale_ncnn)
        target_compile_definitions(burnout3 PRIVATE BO3_HAVE_NCNN=1)
        if(NCNN_VULKAN)
            target_compile_definitions(burnout3 PRIVATE BO3_NCNN_VULKAN=1)
        endif()
        message(STATUS "menu picture upscale: ncnn ${ncnn_VERSION} (Vulkan: ${NCNN_VULKAN}) from ${ncnn_DIR}")
    else()
        message(WARNING "no ncnn (tools/android/fetch-deps.sh): the app will not offer the menu picture upscale")
    endif()
    # Calls and data inside the library bind to it at link time: 24 000
    # lifted functions call each other directly, not through the PLT.
    # --as-needed: no libvulkan.so among the dependencies. The runtime calls
    # Vulkan through pointers from the loader it opens itself (the toolkit's
    # src/d3d/android_vk.h), which may be a replacement driver's.
    target_link_options(burnout3 PRIVATE -Wl,-Bsymbolic -Wl,--no-undefined -Wl,--as-needed)
else()
add_executable(burnout3
    main.c
    install.c
    package.c
    languages.c
    fatal.c
    controls.c
    autopilot.c
    softbody.c
    menuentry.c
    tutorial.c
    scoreboard.c
    unlockall.c
    glyphs.c
    shaderbuild.c
    upscale.c
    upscale_art.c
    soundtrack.c
    dsound_notify.c
    soundbank.c
    traffic_bp.c
    hirefresh.c
    hraudit.c
    mixer.c
    gp_image.c
    pcmanifest.c
    recomp/recomp_manual.c
    ${RECOMP_GEN_SOURCES}
)
endif()

# Optional mod folders (TAKEDAWN_MODS, docs/mod-folders.md): the Win32 file
# API on Windows, POSIX (openat, fdopendir) everywhere else. A mod's mod.toml
# is read with the toolkit's TOML reader, which is part of its enhancements
# library; a build without that library gets the reader's one file. The
# MODS page in the title's Settings menu (modsmenu.c) and the main menu's QUIT
# line (mainquit.c) and the keyboard prompts' key caps drawn for the player's own
# keys (keycap.c, used by glyphs.c) on every platform.
target_sources(burnout3 PRIVATE modfs.c modmanifest.c modsmenu.c modtex.c modstab.c mainquit.c keycap.c xboxlive.c codeentry.c onlinedesc.c quickplay.c racewait.c vote.c onlinebrand.c onlinebrand_text.c headfont.c stay.c nodash.c)
# graphics.native_rate and game.loading_min (simrate.c).
target_sources(burnout3 PRIVATE simrate.c)
target_include_directories(burnout3 PRIVATE ${XBOXRECOMP_DIR}/src/enhance)
if(NOT TARGET xbox_enhance)
    if(NOT EXISTS ${XBOXRECOMP_DIR}/src/enhance/recomp_cfg.c)
        message(FATAL_ERROR "mod.toml needs the toolkit's TOML reader: ${XBOXRECOMP_DIR}/src/enhance/recomp_cfg.c is missing")
    endif()
    target_sources(burnout3 PRIVATE ${XBOXRECOMP_DIR}/src/enhance/recomp_cfg.c)
endif()
# The MODS page is listed only when the generated code calls its two marks
# (cmake/modsmenu_marks.cmake): checked again whenever a generated file or
# config/patches.json changes. A release build without them stops. The PAL
# build's code has them at the PAL addresses, so it is checked against
# config/pal/patches.json.
if(BO3_REGION STREQUAL "PAL")
    set(_mm_patches ${CMAKE_CURRENT_SOURCE_DIR}/../../config/pal/patches.json)
elseif(BO3_REGION STREQUAL "PAL-ENSV")
    set(_mm_patches ${CMAKE_CURRENT_SOURCE_DIR}/../../config/pal-ensv/patches.json)
else()
    set(_mm_patches ${CMAKE_CURRENT_SOURCE_DIR}/../../config/patches.json)
endif()
set(_mm_marks_c ${CMAKE_CURRENT_BINARY_DIR}/modsmenu_marks.c)
add_custom_command(OUTPUT ${_mm_marks_c}
    COMMAND ${CMAKE_COMMAND} -DPATCHES=${_mm_patches}
            -DGEN=${BO3_GEN_DIR} -DOUT=${_mm_marks_c} -DRELEASE=${BO3_RELEASE}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/modsmenu_marks.cmake
    DEPENDS ${RECOMP_GEN_SOURCES} ${_mm_patches}
            ${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/modsmenu_marks.cmake
    VERBATIM)
target_sources(burnout3 PRIVATE ${_mm_marks_c})

# Online play (BO-134 spike): the title's XNet/XOnline on host sockets, off
# unless TAKEDAWN_ONLINE=1.
target_sources(burnout3 PRIVATE online.c onlinebot.c lobbystatus.c statustext.c react.c onlinenav.c onlinescreens.c onlineinfo.c ${CMAKE_CURRENT_SOURCE_DIR}/../../tools/lobby/aries_server.c)
# BO-352: react.c provides bo3_react_ticker_text; status_tick prefers it.
target_compile_definitions(burnout3 PRIVATE BO3_HAVE_REACT=1)

# The shipped shader coverage manifest (BO-208, docs/shader-prebuild.md):
# graphics/pipelines-<backend>.manifest, compiled in as bytes so that it is
# the same on every platform (an APK has no folder to read it from). No game
# data is in it: vertex programs by hash, combiner states and pipeline keys.
# A missing file is an empty manifest.
set(_pcm_c "${CMAKE_CURRENT_BINARY_DIR}/pcmanifest_data.c")
set(_pcm_out "/* generated by src/game/CMakeLists.txt: the shipped shader manifests */\n#include <stddef.h>\n")
set(_pcm_check "")
foreach(_b vulkan metal)
    set(_f "${CMAKE_CURRENT_SOURCE_DIR}/../../graphics/pipelines-${_b}.manifest")
    set(_hex "")
    set(_len 0)
    if(EXISTS "${_f}")
        file(READ "${_f}" _hex HEX)
        string(LENGTH "${_hex}" _len)
        math(EXPR _len "${_len} / 2")
        string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _hex "${_hex}")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_f}")
        # Its key format names the size of the toolkit's NV2ACombinerState. At
        # run time a manifest of another size is only logged ("of another key
        # format: ignored") and nothing is pre-built from it, as 0.2.1 shipped
        # (GitHub #108), so the build stops on it instead.
        file(STRINGS "${_f}" _fmt LIMIT_COUNT 1 REGEX "NV2ACombinerState [0-9]+")
        if(_fmt MATCHES "NV2ACombinerState ([0-9]+)")
            string(APPEND _pcm_check "_Static_assert(sizeof(NV2ACombinerState) == ${CMAKE_MATCH_1},\n"
                   "    \"graphics/pipelines-${_b}.manifest has combiner states of ${CMAKE_MATCH_1} bytes, \"\n"
                   "    \"not the toolkit's: widen it (tools/headless/pcache_keys.py widen) or sweep a new one\");\n")
        endif()
    endif()
    string(APPEND _pcm_out "const unsigned char bo3_pcm_${_b}[] = { ${_hex} 0 };\nconst size_t bo3_pcm_${_b}_size = ${_len};\n")
endforeach()
if(_pcm_check)
    string(APPEND _pcm_out "\n#include \"d3d/d3d8_combiners_parse.h\"\n${_pcm_check}")
endif()
file(WRITE "${_pcm_c}.new" "${_pcm_out}")
execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_pcm_c}.new" "${_pcm_c}")
target_sources(burnout3 PRIVATE "${_pcm_c}")

# The button prompts for the pad in use (glyphs.c, BO-107): this project's own
# drawings, assets/glyphs/*.svg, rendered here at the textures' own size and
# compiled in. tools/glyphs/build.py needs resvg, rsvg-convert or the resvg_py
# module (as assets/icon does); BO3_GLYPHS_PYTHON names a Python that has the
# module, else the TAKEDAWN_PYTHON the release scripts render the icon with.
# Without a renderer a developer build is made without the artwork and
# input.glyphs changes nothing (a warning); a release build (BO3_RELEASE) or
# BO3_GLYPHS=ON stops with an error instead.
set(BO3_GLYPHS "AUTO" CACHE STRING "Button prompts for other pads: AUTO (when an SVG renderer is found), ON, OFF")
set(BO3_GLYPHS_PYTHON "" CACHE FILEPATH "Python for tools/glyphs/build.py (default: python3 on the PATH)")
# BO3_GLYPHS_ART_DIR: a folder that already holds bo3_glyph_art.c and .h, made
# by tools/glyphs/build.py somewhere else (the Linux release is compiled in a
# container that has no SVG renderer; tools/release/build-linux.sh renders
# them first). Nothing is rendered here then.
set(BO3_GLYPHS_ART_DIR "" CACHE PATH "Folder with a rendered bo3_glyph_art.c/.h, instead of rendering here")
if(BO3_GLYPHS_ART_DIR AND NOT BO3_GLYPHS STREQUAL "OFF")
    if(NOT EXISTS "${BO3_GLYPHS_ART_DIR}/bo3_glyph_art.c" OR NOT EXISTS "${BO3_GLYPHS_ART_DIR}/bo3_glyph_art.h")
        message(FATAL_ERROR "BO3_GLYPHS_ART_DIR=${BO3_GLYPHS_ART_DIR} holds no bo3_glyph_art.c/.h")
    endif()
    target_sources(burnout3 PRIVATE ${BO3_GLYPHS_ART_DIR}/bo3_glyph_art.c)
    target_include_directories(burnout3 PRIVATE ${BO3_GLYPHS_ART_DIR})
    target_compile_definitions(burnout3 PRIVATE BO3_HAVE_GLYPH_ART)
    message(STATUS "burnout3: button prompts for other pads on (rendered art from ${BO3_GLYPHS_ART_DIR})")
elseif(NOT BO3_GLYPHS STREQUAL "OFF")
    # The Python, in this order: BO3_GLYPHS_PYTHON, TAKEDAWN_PYTHON, one on the
    # PATH. The search result has a cache entry of its own (BO3_GLYPHS_PYTHON_FOUND):
    # under the name of the working variable it would be read back at the next
    # configure in place of the two that come before it.
    set(_glyph_python "${BO3_GLYPHS_PYTHON}")
    if(NOT _glyph_python AND DEFINED ENV{TAKEDAWN_PYTHON} AND NOT "$ENV{TAKEDAWN_PYTHON}" STREQUAL "")
        set(_glyph_python "$ENV{TAKEDAWN_PYTHON}")
    endif()
    if(NOT _glyph_python)
        find_program(BO3_GLYPHS_PYTHON_FOUND NAMES python3 python)
        mark_as_advanced(BO3_GLYPHS_PYTHON_FOUND)
        if(BO3_GLYPHS_PYTHON_FOUND)
            set(_glyph_python "${BO3_GLYPHS_PYTHON_FOUND}")
        endif()
    endif()
    unset(_glyph_python CACHE)          # a build tree configured before this fix
    set(_glyph_tool ${CMAKE_CURRENT_SOURCE_DIR}/../../tools/glyphs/build.py)
    set(_glyph_rc 1)
    if(_glyph_python)
        execute_process(COMMAND ${_glyph_python} ${_glyph_tool} --check
                        RESULT_VARIABLE _glyph_rc OUTPUT_QUIET ERROR_QUIET)
    endif()
    if(_glyph_rc EQUAL 0)
        set(_glyph_src ${CMAKE_CURRENT_SOURCE_DIR}/../../assets/glyphs)
        set(_glyph_dir ${CMAKE_CURRENT_BINARY_DIR}/glyphs)
        file(GLOB_RECURSE _glyph_svgs CONFIGURE_DEPENDS "${_glyph_src}/*.svg")
        add_custom_command(
            OUTPUT ${_glyph_dir}/bo3_glyph_art.c ${_glyph_dir}/bo3_glyph_art.h
            COMMAND ${_glyph_python} ${_glyph_tool} ${_glyph_src}
                    ${_glyph_dir}/bo3_glyph_art.c ${_glyph_dir}/bo3_glyph_art.h
            DEPENDS ${_glyph_svgs} ${_glyph_tool} VERBATIM)
        target_sources(burnout3 PRIVATE ${_glyph_dir}/bo3_glyph_art.c)
        target_include_directories(burnout3 PRIVATE ${_glyph_dir})
        target_compile_definitions(burnout3 PRIVATE BO3_HAVE_GLYPH_ART)
        message(STATUS "burnout3: button prompts for other pads (assets/glyphs) on")
    elseif(BO3_GLYPHS STREQUAL "ON" OR BO3_RELEASE)
        message(FATAL_ERROR "assets/glyphs (the button prompts, required in a release build and with BO3_GLYPHS=ON) cannot be rendered: no resvg, no rsvg-convert, and "
                            "the Python found has no resvg_py module (pip install resvg-py, -DBO3_GLYPHS_PYTHON= or TAKEDAWN_PYTHON)")
    else()
        message(WARNING "burnout3: built WITHOUT the button prompts for other pads (input.glyphs will change "
                        "nothing): no resvg, no rsvg-convert, no resvg_py module. See tools/glyphs/build.py.")
    endif()
endif()

# The version, the regen key and the disc manifest (install.c, BO-103).
set(BO3_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(BO3_RELEASE_DIR ${CMAKE_CURRENT_BINARY_DIR}/release)
include(${BO3_ROOT}/cmake/release_info.cmake)
target_include_directories(burnout3 PRIVATE ${BO3_RELEASE_DIR})

# The game's commit for the bug report (main.c, BO-99) and both commits for
# --version (install.c): headers made at every build by the toolkit's script
# and rewritten only when a commit changes.
if(EXISTS "${XBOXRECOMP_DIR}/cmake/build_id.cmake")
    set(BO3_BUILD_ID_DIR ${CMAKE_CURRENT_BINARY_DIR}/build_id)
    add_custom_target(bo3_build_id
        COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../..
                -DOUT=${BO3_BUILD_ID_DIR}/bo3_build_id.h -DMACRO=BO3_BUILD_COMMIT
                -P ${XBOXRECOMP_DIR}/cmake/build_id.cmake
        COMMAND ${CMAKE_COMMAND} -DSRC=${XBOXRECOMP_DIR}
                -DOUT=${BO3_BUILD_ID_DIR}/bo3_toolkit_id.h -DMACRO=BO3_TOOLKIT_COMMIT
                -P ${XBOXRECOMP_DIR}/cmake/build_id.cmake
        BYPRODUCTS ${BO3_BUILD_ID_DIR}/bo3_build_id.h ${BO3_BUILD_ID_DIR}/bo3_toolkit_id.h
        VERBATIM)
    add_dependencies(burnout3 bo3_build_id)
    target_include_directories(burnout3 PRIVATE ${BO3_BUILD_ID_DIR})
    set_source_files_properties(main.c install.c PROPERTIES COMPILE_DEFINITIONS BO3_HAVE_BUILD_ID)
endif()

if(WIN32)
    # The umbrella target carries the print gate's force-include; this branch
    # links the libraries one by one, so it is added here (BO-125).
    if(MSVC)
        target_compile_options(burnout3 PRIVATE "/FI${XBOXRECOMP_DIR}/src/platform/xr_log.h")
    else()
        target_compile_options(burnout3 PRIVATE "SHELL:-include \"${XBOXRECOMP_DIR}/src/platform/xr_log.h\"")
    endif()
    target_link_libraries(burnout3 PRIVATE
        xbox_kernel xbox_d3d8 xbox_dsound xbox_input xbox_usb xbox_video xbox_apu
        xbox_install xbox_soundtrack kernel32 user32 gdi32 dbghelp
    )
    if(TARGET xbox_enhance)
        target_link_libraries(burnout3 PRIVATE xbox_enhance)
    endif()
    if(XBOXRECOMP_WIN_VULKAN)
        # SDL2 (window, audio, pad), Vulkan and the overlay's Dear ImGui (C++).
        find_package(SDL2 REQUIRED)
        target_link_libraries(burnout3 PRIVATE SDL2::SDL2)
        if(TARGET xbox_overlay)
            target_link_libraries(burnout3 PRIVATE xbox_overlay)
        endif()
    endif()
else()
    # The umbrella: the same libraries, with SDL2, Metal and OpenSSL.
    target_link_libraries(burnout3 PRIVATE xboxrecomp)
endif()

target_include_directories(burnout3 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/recomp
    ${BO3_GEN_DIR}
    ${XBOXRECOMP_DIR}/src
    ${XBOXRECOMP_DIR}/src/kernel
    # The template runtime headers, not a local copy: BO3's old copy had
    # drifted from the template by hundreds of lines.
    ${XBOXRECOMP_DIR}/templates/runtime
)

if(MSVC)
    target_compile_options(burnout3 PRIVATE /W4 /WX- /O2 /bigobj /MP${BO3_COMPILE_JOBS})
    set_source_files_properties(${RECOMP_GEN_SOURCES} PROPERTIES COMPILE_FLAGS "/w")
    target_compile_definitions(burnout3 PRIVATE _CRT_SECURE_NO_WARNINGS)
    # Fixed image base so Xbox memory can be mapped at fixed addresses; no ICF
    # so every generated function keeps its own symbol for fault reports.
    target_link_options(burnout3 PRIVATE
        /DYNAMICBASE:NO /FIXED:NO /MAP /DEBUG /STACK:16777216 /OPT:NOICF /OPT:REF)
else()
    if(WIN32)
        # llvm-mingw: the 16 MiB main-thread stack the guest runs deep on, and
        # a fixed image (the guest's memory is placed by the runtime, not here).
        target_link_options(burnout3 PRIVATE
            -Wl,--stack,16777216 -Wl,--disable-dynamicbase -Wl,--disable-high-entropy-va)
        # The console subsystem (SDL2's mingw config asks for -mwindows): the
        # log goes to the console that started it.
        target_link_options(burnout3 PRIVATE -Wl,--subsystem,console)
        target_compile_definitions(burnout3 PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    # Clang: the generated chunks warn by the thousand. The executable stays
    # unstripped so the fault handler's dladdr names sub_XXXXXXXX.
    set_source_files_properties(${RECOMP_GEN_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")
    set(BO3_GEN_OPT "" CACHE STRING
        "Extra compile options for the generated sources only (e.g. -O1)")
    if(BO3_GEN_OPT)
        separate_arguments(_bo3_gen_opt NATIVE_COMMAND "${BO3_GEN_OPT}")
        set_property(SOURCE ${RECOMP_GEN_SOURCES} APPEND PROPERTY
            COMPILE_OPTIONS ${_bo3_gen_opt})
    endif()
    # macOS, Linux, the Deck and Android (clang): the lifted code in its own
    # section (__TEXT,__guest on macOS, guest_text on ELF), so that
    # RECOMP_DEBUG=guest_cpus=token can preempt a guest thread there (the
    # toolkit's kernel_thread.c and docs/runtime/guest-cpus.md; the Def Jam
    # port does the same). The toolkit's header, when it has one; without the
    # switch nothing reads it. GCC and Windows have no such section.
    set(_bo3_guest_section
        "${XBOXRECOMP_DIR}/templates/runtime/recomp_guest_section.h")
    if(NOT WIN32 AND CMAKE_C_COMPILER_ID MATCHES "Clang"
       AND EXISTS "${_bo3_guest_section}")
        set_property(SOURCE ${RECOMP_GEN_SOURCES} APPEND PROPERTY
            COMPILE_OPTIONS "-include" "${_bo3_guest_section}")
    endif()
endif()

# Windows: the manifest that makes the process's code page UTF-8, in every
# build (tools/release/windows/takedawn.exe.manifest; main.c's first call,
# recomp_win_utf8_args, relies on it). A release has it in its own resource
# file below; any other build gets a resource file with just that.
if(WIN32)
    file(TO_CMAKE_PATH "${BO3_ROOT}/tools/release/windows/takedawn.exe.manifest" BO3_RC_MANIFEST)
    if(NOT BO3_RELEASE)
        configure_file(${BO3_ROOT}/tools/release/windows/manifest.rc.in
                       ${CMAKE_CURRENT_BINARY_DIR}/manifest.rc @ONLY)
        enable_language(RC)
        target_sources(burnout3 PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/manifest.rc)
    endif()
    if(MSVC)
        # The linker would add a manifest of its own beside this one.
        target_link_options(burnout3 PRIVATE /MANIFEST:NO)
    endif()
endif()

# The release's name and platform dressing (the top CMakeLists.txt,
# BO3_RELEASE). Android keeps libmain.so: its package is the Gradle build's.
if(BO3_RELEASE AND NOT ANDROID)
    if(WIN32)
        set_target_properties(burnout3 PROPERTIES OUTPUT_NAME Takedawn)
        if(NOT MSVC)
            # A window program: no console opens beside the game (main.c
            # attaches to the terminal that started it, when there is one).
            target_link_options(burnout3 PRIVATE -Wl,--subsystem,windows)
        endif()
        # The icon (BO3_WIN_ICON, made from assets/icon by the release
        # script) and the version resource.
        string(REPLACE "." "," BO3_VERSION_COMMAS "${BO3_VERSION_NUM}")
        set(BO3_WIN_ICON "" CACHE FILEPATH "The .ico for the release executable")
        if(BO3_WIN_ICON)
            file(TO_CMAKE_PATH "${BO3_WIN_ICON}" _bo3_ico)
            set(BO3_RC_ICON_LINE "1 ICON \"${_bo3_ico}\"")
        else()
            set(BO3_RC_ICON_LINE "")
        endif()
        configure_file(${BO3_ROOT}/tools/release/windows/takedawn.rc.in
                       ${CMAKE_CURRENT_BINARY_DIR}/takedawn.rc @ONLY)
        enable_language(RC)
        target_sources(burnout3 PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/takedawn.rc)
    else()
        set_target_properties(burnout3 PROPERTIES OUTPUT_NAME takedawn)
    endif()
    if(APPLE)
        # Takedawn.app/Contents/MacOS/takedawn beside ../Frameworks.
        set_target_properties(burnout3 PROPERTIES
            BUILD_WITH_INSTALL_RPATH ON
            INSTALL_RPATH "@executable_path/../Frameworks")
    elseif(UNIX)
        # The AppImage: usr/bin/takedawn beside ../lib.
        set_target_properties(burnout3 PROPERTIES
            BUILD_WITH_INSTALL_RPATH ON
            INSTALL_RPATH "\$ORIGIN/../lib")
    endif()
endif()

# bin/ beside the game data by default; a second build tree (a sanitizer
# build, an experiment) names its own so it does not replace the one in use.
set(BO3_BIN_DIR "${CMAKE_SOURCE_DIR}/bin" CACHE PATH
    "Where the burnout3 executable is written")
set_target_properties(burnout3 PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY         "${BO3_BIN_DIR}"
    RUNTIME_OUTPUT_DIRECTORY         "${BO3_BIN_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${BO3_BIN_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${BO3_BIN_DIR}"
)
