# unreach - the "can't reach another player" message names the copy's own
# race port, and in Local network mode points at the firewall
# (src/game/unreach.h, BO-349).
#   cmake -S tests/unreach -B <build> && cmake --build <build> && ctest --test-dir <build>
cmake_minimum_required(VERSION 3.20)
if(NOT TARGET burnout3)
    project(bo3_unreach_test C)
    set(CMAKE_C_STANDARD 11)
endif()
add_executable(bo3_unreach_test test_main.c)
target_include_directories(bo3_unreach_test PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../src/game)
enable_testing()
add_test(NAME bo3_unreach COMMAND bo3_unreach_test)
