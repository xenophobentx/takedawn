# racewait - QUICK PLAY / JOIN WITH CODE waiting for a race under way (src/game/racewait.c, BO-365).
#   cmake -S tests/racewait -B <build> && cmake --build <build> && ctest --test-dir <build>
cmake_minimum_required(VERSION 3.20)
if(NOT TARGET burnout3)
    project(bo3_racewait_test C)
    set(CMAKE_C_STANDARD 11)
endif()
add_executable(bo3_racewait_test test_main.c ${CMAKE_CURRENT_LIST_DIR}/../../src/game/racewait.c ${CMAKE_CURRENT_LIST_DIR}/../../src/game/statustext.c ${CMAKE_CURRENT_LIST_DIR}/../../src/game/languages.c)
target_include_directories(bo3_racewait_test PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../src/game)
enable_testing()
add_test(NAME bo3_racewait COMMAND bo3_racewait_test)
