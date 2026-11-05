# onlinenav - the player's online navigator's pure decisions (BO-352, step 2):
# the intent switch, the Quick Play auto-start countdown and its text. The
# navigator itself reads the title's memory and is tested on real copies.
#   cmake --build <build> --target bo3_onlinenav_test && ctest -R bo3_onlinenav
add_executable(bo3_onlinenav_test test_main.c ${CMAKE_CURRENT_LIST_DIR}/../../src/game/onlinenav.c)
target_compile_definitions(bo3_onlinenav_test PRIVATE ONLINENAV_PURE_ONLY)
target_include_directories(bo3_onlinenav_test PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../src/game)
enable_testing()
add_test(NAME bo3_onlinenav COMMAND bo3_onlinenav_test)
