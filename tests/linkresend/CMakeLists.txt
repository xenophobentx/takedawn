# linkresend - the online lobby's link reports sent again every half second
# (src/game/linkresend.h, BO-344): the cadence, which peers, and a model of
# the title's one-shot sender (0x102610) with a lost datagram. No network.
#   cmake -S tests/linkresend -B <build> && cmake --build <build> && ctest --test-dir <build>
cmake_minimum_required(VERSION 3.20)
if(NOT TARGET burnout3)
    project(bo3_linkresend_test C)
    set(CMAKE_C_STANDARD 11)
endif()
add_executable(bo3_linkresend_test test_main.c)
target_include_directories(bo3_linkresend_test PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../src/game)
enable_testing()
add_test(NAME bo3_linkresend COMMAND bo3_linkresend_test)
