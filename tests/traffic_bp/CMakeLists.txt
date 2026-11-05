# traffic_bp - a traffic car out of the collision broadphase by its own slot,
# or not at all (src/game/traffic_bp.c, BO-346): the slot lookup, and the
# wrapper against a stand-in for the title's sub_00111680 on a made-up
# broadphase in a host buffer. The int037 crash's state (handle 0) is one of
# the cases. No game data, no guest code.
#   cmake --build <build> --target bo3_traffic_bp_test && ctest -R bo3_traffic_bp
cmake_minimum_required(VERSION 3.20)
if(NOT TARGET burnout3)
    project(bo3_traffic_bp_test C)
    set(CMAKE_C_STANDARD 11)
    if(NOT XBOXRECOMP_DIR)
        set(XBOXRECOMP_DIR ${CMAKE_CURRENT_LIST_DIR}/../../../xboxrecomp)
    endif()
endif()
add_executable(bo3_traffic_bp_test test_main.c ${CMAKE_CURRENT_LIST_DIR}/../../src/game/traffic_bp.c)
target_include_directories(bo3_traffic_bp_test PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../src/game
    ${XBOXRECOMP_DIR}/src
    ${XBOXRECOMP_DIR}/src/kernel
    ${XBOXRECOMP_DIR}/templates/runtime)
enable_testing()
add_test(NAME bo3_traffic_bp COMMAND bo3_traffic_bp_test)
