# react - the quick reactions' phrases and d-pad mapping (src/game/react.h,
# BO-352 step 7). Pure: no guest, no device, every host.
#   cmake --build <build> --target bo3_react_test && ctest -R bo3_react
add_executable(bo3_react_test test_main.c)
target_include_directories(bo3_react_test PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../src/game
    ${XBOXRECOMP_DIR}/src/kernel)
enable_testing()
add_test(NAME bo3_react COMMAND bo3_react_test)
