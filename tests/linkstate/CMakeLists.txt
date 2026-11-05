# linkstate - each peer's link state followed and every change logged, for
# the online lobby (src/game/linkstate.h, BO-345). No network, no title.
#   cmake -S tests/linkstate -B <build> && cmake --build <build> && ctest --test-dir <build>
cmake_minimum_required(VERSION 3.20)
if(NOT TARGET burnout3)
    project(bo3_linkstate_test C)
    set(CMAKE_C_STANDARD 11)
endif()
add_executable(bo3_linkstate_test test_main.c)
target_include_directories(bo3_linkstate_test PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../src/game)
enable_testing()
add_test(NAME bo3_linkstate COMMAND bo3_linkstate_test)
