# peerqos - what the title's XNetQos block says about a peer (src/game/peerqos.h, BO-357).
#   cmake -S tests/peerqos -B <build> && cmake --build <build> && ctest --test-dir <build>
cmake_minimum_required(VERSION 3.20)
if(NOT TARGET burnout3)
    project(bo3_peerqos_test C)
    set(CMAKE_C_STANDARD 11)
endif()
add_executable(bo3_peerqos_test test_main.c)
target_include_directories(bo3_peerqos_test PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../src/game)
enable_testing()
add_test(NAME bo3_peerqos COMMAND bo3_peerqos_test)
