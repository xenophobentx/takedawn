# lobbystatus - the game lobby's status line and kick reason (BO-352 step 5a).
#   cmake -S tests/lobbystatus -B <build> && cmake --build <build> && ctest --test-dir <build>
cmake_minimum_required(VERSION 3.20)
if(NOT TARGET burnout3)
    project(bo3_lobbystatus_test C)
    set(CMAKE_C_STANDARD 11)
endif()
add_executable(bo3_lobbystatus_test test_main.c ${CMAKE_CURRENT_LIST_DIR}/../../src/game/lobbystatus.c ${CMAKE_CURRENT_LIST_DIR}/../../src/game/statustext.c ${CMAKE_CURRENT_LIST_DIR}/../../src/game/languages.c)
target_include_directories(bo3_lobbystatus_test PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../src/game)
enable_testing()
add_test(NAME bo3_lobbystatus COMMAND bo3_lobbystatus_test)
