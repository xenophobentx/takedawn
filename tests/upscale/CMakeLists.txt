# upscale - the parts of the menu picture upscale that need no network, no
# process and no GPU (src/game/upscale_art.c, BO-311), and the in-process
# upscaler's tiles and backend choice (src/game/upscale_tile.c, BO-326). The toolkit's
# nv2a_texpack_key is a stub here; the real keys are checked against the game's
# texture dump in docs/upscale-art.md.
#   cmake --build <build> --target bo3_upscale_test && ctest -R bo3_upscale
add_executable(bo3_upscale_test test_main.c
    ${CMAKE_CURRENT_LIST_DIR}/../../src/game/upscale_art.c
    ${CMAKE_CURRENT_LIST_DIR}/../../src/game/upscale_tile.c)
target_include_directories(bo3_upscale_test PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../src/game
    ${XBOXRECOMP_DIR}/src)
enable_testing()
add_test(NAME bo3_upscale COMMAND bo3_upscale_test ${CMAKE_CURRENT_BINARY_DIR})
