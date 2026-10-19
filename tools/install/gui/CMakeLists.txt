# Takedawn's setup window: Qt 6 Widgets over the installer inside the game's own
# executable (`<game> --data-status`, `--install <disc>`, `--build-shaders`; the
# game does the work, this runs it and shows what it reports). Kept out of the
# game's own build so the game does not need Qt.
#
#   cmake -S tools/install/gui -B tools/install/gui/build -DCMAKE_BUILD_TYPE=Release \
#         -DCMAKE_PREFIX_PATH=<Qt 6 kit>
#   cmake --build tools/install/gui/build
#   ctest --test-dir tools/install/gui/build --output-on-failure
#
# The program is tools/install/gui/build/takedawn-setup. A release has it beside
# the game (macOS: Takedawn.app/Contents/MacOS/takedawn-setup, see
# packaging/make-macos-bundle.sh), where the game looks for it.
cmake_minimum_required(VERSION 3.21)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../../../VERSION" _version_lines)
list(GET _version_lines 0 TAKEDAWN_VERSION)
string(STRIP "${TAKEDAWN_VERSION}" TAKEDAWN_VERSION)
# project() takes only numbers, so a "-beta" suffix stays out of it.
string(REGEX MATCH "^[0-9]+(\\.[0-9]+)*" _numeric_version "${TAKEDAWN_VERSION}")
project(takedawn_setup VERSION ${_numeric_version} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(Qt6 QUIET COMPONENTS Test)

# Everything but main(), for the program and the window test.
add_library(takedawn_setup_ui STATIC
    installer_window.cpp
    installer_window.h
    driver_process.cpp
    driver_process.h
    protocol.cpp
    protocol.h
    messages.cpp
    messages.h
    app_icon.cpp
    app_icon.h
    elided_label.h
    theme.cpp
    theme.h
    ui_widgets.cpp
    ui_widgets.h
    steam_shortcut.cpp
    steam_shortcut.h
    steam_artwork.cpp
    steam_artwork.h
    disc_browser.cpp
    disc_browser.h
    pad_input.cpp
    pad_input.h
    pad_nav.cpp
    pad_nav.h
)
target_include_directories(takedawn_setup_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(takedawn_setup_ui PUBLIC TAKEDAWN_SETUP_VERSION="${TAKEDAWN_VERSION}")
# The disc the package is for, as the program's BO3_REGION (docs/pal.md).
set(TAKEDAWN_REGION "NTSC-U" CACHE STRING "Disc region named in the window: NTSC-U or PAL")
if(TAKEDAWN_REGION STREQUAL "PAL")
    target_compile_definitions(takedawn_setup_ui PUBLIC TAKEDAWN_DISC_SHORT="PAL"
                               TAKEDAWN_DISC_LONG="Europe (PAL)")
endif()
target_link_libraries(takedawn_setup_ui PUBLIC Qt6::Widgets)

# The controller (pad_input.h): SDL's game controller API, the SDL the game
# itself runs on. On by default where the window has to work without a mouse
# or keyboard (Linux: a Steam Deck in Game Mode); elsewhere the window is built
# without it unless asked, so the macOS and Windows packages carry what they did.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_gamepad_default ON)
else()
    set(_gamepad_default OFF)
endif()
option(TAKEDAWN_SETUP_GAMEPAD "Controller navigation in the setup window (needs SDL2)" ${_gamepad_default})
if(TAKEDAWN_SETUP_GAMEPAD)
    find_package(SDL2 REQUIRED)
    if(TARGET SDL2::SDL2)
        target_link_libraries(takedawn_setup_ui PUBLIC SDL2::SDL2)
    else()
        target_include_directories(takedawn_setup_ui PUBLIC ${SDL2_INCLUDE_DIRS})
        target_link_libraries(takedawn_setup_ui PUBLIC ${SDL2_LIBRARIES})
    endif()
    target_compile_definitions(takedawn_setup_ui PUBLIC TAKEDAWN_SETUP_SDL=1)
endif()

# The icon: the project's own SVG (assets/icon), rendered here to the PNG sizes a
# window icon wants and compiled in. render_icon.py needs resvg, rsvg-convert or
# the resvg_py module; TAKEDAWN_ICON_PYTHON names a Python that has the module.
# TAKEDAWN_ICON_DIR names a folder that already holds icon-<n>.png instead.
set(TAKEDAWN_ICON_PYTHON "" CACHE FILEPATH "Python for render_icon.py (default: python3 on the PATH)")
set(TAKEDAWN_ICON_DIR "" CACHE PATH "Folder with icon-16.png ... icon-512.png, instead of rendering them")
set(_icon_sizes 16 32 64 128 256 512)
set(_icon_svg ${CMAKE_CURRENT_SOURCE_DIR}/../../../assets/icon/takedawn.svg)
if(TAKEDAWN_ICON_DIR)
    set(_icon_dir ${TAKEDAWN_ICON_DIR})
else()
    set(_icon_dir ${CMAKE_BINARY_DIR}/icon)
    set(_icon_python ${TAKEDAWN_ICON_PYTHON})
    if(NOT _icon_python)
        find_program(_icon_python NAMES python3 python REQUIRED)
    endif()
    set(_icon_args "")
    set(_icon_out "")
    foreach(_px ${_icon_sizes})
        list(APPEND _icon_args ${_icon_dir}/icon-${_px}.png ${_px})
        list(APPEND _icon_out ${_icon_dir}/icon-${_px}.png)
    endforeach()
    execute_process(COMMAND ${_icon_python} ${CMAKE_CURRENT_SOURCE_DIR}/render_icon.py --check
                    RESULT_VARIABLE _icon_rc OUTPUT_QUIET)
    if(NOT _icon_rc EQUAL 0)
        message(FATAL_ERROR "The icon cannot be rendered: no resvg, no rsvg-convert, and ${_icon_python} has no "
                            "resvg_py module. Install one (brew install resvg; pip install resvg-py), or pass "
                            "-DTAKEDAWN_ICON_PYTHON=<a python with resvg_py> or -DTAKEDAWN_ICON_DIR=<rendered PNGs>.")
    endif()
    file(MAKE_DIRECTORY ${_icon_dir})
    add_custom_command(OUTPUT ${_icon_out}
        COMMAND ${_icon_python} ${CMAKE_CURRENT_SOURCE_DIR}/render_icon.py ${_icon_svg} ${_icon_args}
        DEPENDS ${_icon_svg} ${CMAKE_CURRENT_SOURCE_DIR}/render_icon.py VERBATIM)
endif()
set(_icon_files "")
foreach(_px ${_icon_sizes})
    set_source_files_properties(${_icon_dir}/icon-${_px}.png PROPERTIES QT_RESOURCE_ALIAS icon-${_px}.png)
    list(APPEND _icon_files ${_icon_dir}/icon-${_px}.png)
endforeach()

qt_add_executable(takedawn-setup main.cpp shots.cpp shots.h)
qt_add_resources(takedawn-setup "takedawn_icon" PREFIX "/takedawn" FILES ${_icon_files})
target_link_libraries(takedawn-setup PRIVATE takedawn_setup_ui ${CMAKE_DL_LIBS})

if(APPLE)
    # Not a bundle of its own: it lives in the game's (Contents/MacOS), whose
    # Frameworks folder holds Qt.
    set_target_properties(takedawn-setup PROPERTIES
        MACOSX_BUNDLE FALSE
        INSTALL_RPATH "@executable_path/../Frameworks")
    target_link_libraries(takedawn-setup PRIVATE "-framework ApplicationServices")
elseif(WIN32)
    set_target_properties(takedawn-setup PROPERTIES WIN32_EXECUTABLE TRUE)
    if(TAKEDAWN_ICO)
        # The exe's own icon: an .ico the packaging made from assets/icon.
        enable_language(RC)
        file(WRITE "${CMAKE_BINARY_DIR}/setup.rc" "IDI_ICON1 ICON \"${TAKEDAWN_ICO}\"\n")
        target_sources(takedawn-setup PRIVATE "${CMAKE_BINARY_DIR}/setup.rc")
    endif()
endif()

if(Qt6Test_FOUND)
    enable_testing()
    qt_add_executable(protocol_test tests/protocol_test.cpp protocol.cpp protocol.h messages.cpp messages.h)
    target_link_libraries(protocol_test PRIVATE Qt6::Test Qt6::Core)
    target_include_directories(protocol_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME protocol COMMAND protocol_test)

    # The Steam shortcut writer, on files the test makes.
    qt_add_executable(steam_test tests/steam_test.cpp steam_shortcut.cpp steam_shortcut.h)
    target_link_libraries(steam_test PRIVATE Qt6::Test Qt6::Core)
    target_include_directories(steam_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    set_target_properties(steam_test PROPERTIES MACOSX_BUNDLE FALSE)
    add_test(NAME steam COMMAND steam_test)

    # The Steam library art: its file names, the fallback's sizes, the texture reader
    # on made-up dictionaries, the backups. No game data: TAKEDAWN_STEAMART_GAME (a
    # folder with Data/) adds a run on the real files, writing into a temporary folder.
    qt_add_executable(steamart_test tests/steamart_test.cpp steam_artwork.cpp steam_artwork.h steam_shortcut.cpp
                      steam_shortcut.h)
    qt_add_resources(steamart_test "takedawn_icon" PREFIX "/takedawn" FILES ${_icon_files})
    target_link_libraries(steamart_test PRIVATE Qt6::Test Qt6::Gui)
    target_include_directories(steamart_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    set_target_properties(steamart_test PROPERTIES MACOSX_BUNDLE FALSE)
    add_test(NAME steamart COMMAND steamart_test)
    set_tests_properties(steamart PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    # The window against a fake installer process, with every failure code.
    add_executable(fake_installer tests/fake_installer.cpp)
    qt_add_executable(window_test tests/window_test.cpp)
    qt_add_resources(window_test "takedawn_icon" PREFIX "/takedawn" FILES ${_icon_files})
    target_link_libraries(window_test PRIVATE takedawn_setup_ui Qt6::Test)
    add_dependencies(window_test fake_installer)
    set_target_properties(window_test fake_installer PROPERTIES
        MACOSX_BUNDLE FALSE RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
    add_test(NAME window COMMAND window_test)
    set_tests_properties(window PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    # The controller and the disc browser: the key repeat from made-up controller
    # states, the browser over a made-up tree of folders, and the whole install
    # driven by presses alone.
    qt_add_executable(pad_test tests/pad_test.cpp)
    qt_add_resources(pad_test "takedawn_icon" PREFIX "/takedawn" FILES ${_icon_files})
    target_link_libraries(pad_test PRIVATE takedawn_setup_ui Qt6::Test)
    add_dependencies(pad_test fake_installer)
    set_target_properties(pad_test PROPERTIES MACOSX_BUNDLE FALSE RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
    add_test(NAME pad COMMAND pad_test)
    set_tests_properties(pad PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
