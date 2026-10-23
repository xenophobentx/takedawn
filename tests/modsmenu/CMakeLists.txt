# modsmenu - the MODS page in the title's Settings menu (src/game/modsmenu.c,
# with menuentry.c and the mod folders as they are) against a stand-in for
# the title: guest memory is a buffer, the title's set-up, heap, sound and the
# flow's marks are played by the test. Mods are folders it makes in the
# temporary directory. No game data, no guest code.
#   cmake --build <build> --target bo3_modsmenu_test && ctest -R bo3_modsmenu
add_executable(bo3_modsmenu_test test_main.c
    ${CMAKE_CURRENT_LIST_DIR}/../../src/game/modsmenu.c
    ${CMAKE_CURRENT_LIST_DIR}/../../src/game/menuentry.c
    ${CMAKE_CURRENT_LIST_DIR}/../../src/game/modfs.c
    ${CMAKE_CURRENT_LIST_DIR}/../../src/game/modmanifest.c
    ${XBOXRECOMP_DIR}/src/enhance/recomp_cfg.c)
target_compile_definitions(bo3_modsmenu_test PRIVATE XBOXRECOMP_HAVE_OVERLAY)
target_include_directories(bo3_modsmenu_test PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../src/game
    ${XBOXRECOMP_DIR}/src
    ${XBOXRECOMP_DIR}/src/kernel
    ${XBOXRECOMP_DIR}/src/platform
    ${XBOXRECOMP_DIR}/src/overlay
    ${XBOXRECOMP_DIR}/src/enhance
    ${XBOXRECOMP_DIR}/templates/runtime)
if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(bo3_modsmenu_test PRIVATE Threads::Threads)
endif()
enable_testing()
add_test(NAME bo3_modsmenu COMMAND bo3_modsmenu_test)
# The build's check that the generated code has the page's marks
# (cmake/modsmenu_marks.cmake), on made-up generated code.
add_test(NAME bo3_modsmenu_marks COMMAND ${CMAKE_COMMAND} -DWORK=${CMAKE_CURRENT_BINARY_DIR}/marks_test
         -P ${CMAKE_CURRENT_LIST_DIR}/marks_test.cmake)
