# startwatch - the online lobby screen stuck in state 0xB "the game is about
# to launch" (src/game/startwatch.h, BO-344): when to say what it waits for
# and when to leave the game. No guest, no network.
#   cmake -S tests/startwatch -B <build> && cmake --build <build> && ctest --test-dir <build>
cmake_minimum_required(VERSION 3.20)
if(NOT TARGET burnout3)
    project(bo3_startwatch_test C)
    set(CMAKE_C_STANDARD 11)
endif()
add_executable(bo3_startwatch_test test_main.c)
target_include_directories(bo3_startwatch_test PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../src/game)
enable_testing()
add_test(NAME bo3_startwatch COMMAND bo3_startwatch_test)
