# quickplay - QUICK PLAY's pure decisions (src/game/quickplay.c, Takedawn Online
# step 3): when the create branch starts, when it gives up, when B leaves, and
# the values the skipped name and mode pages would have stored.
#   cmake --build <build> --target bo3_quickplay_test && ctest -R bo3_quickplay
add_executable(bo3_quickplay_test test_main.c ${CMAKE_CURRENT_LIST_DIR}/../../src/game/quickplay.c)
target_compile_definitions(bo3_quickplay_test PRIVATE QUICKPLAY_PURE_ONLY)
target_include_directories(bo3_quickplay_test PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../src/game)
enable_testing()
add_test(NAME bo3_quickplay COMMAND bo3_quickplay_test)
