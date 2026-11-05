# peertask - the online layer's peer lookup tasks (src/game/peertask.h,
# BO-344): one handle per lookup, a lookup storm without Close, lookups at the
# same time each with its own result. No guest, no network.
#   cmake -S tests/peertask -B <build> && cmake --build <build> && ctest --test-dir <build>
cmake_minimum_required(VERSION 3.20)
if(NOT TARGET burnout3)
    project(bo3_peertask_test C)
    set(CMAKE_C_STANDARD 11)
endif()
add_executable(bo3_peertask_test test_main.c)
target_include_directories(bo3_peertask_test PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../src/game)
enable_testing()
add_test(NAME bo3_peertask COMMAND bo3_peertask_test)
