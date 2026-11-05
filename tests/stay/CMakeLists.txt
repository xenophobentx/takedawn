# stay - "stay together" after an online race (src/game/stay.c, Takedawn
# Online step 4): when a raced game is followed, when it is given up, and what
# the room state does instead of its page.
#   cmake --build <build> --target bo3_stay_test && ctest -R bo3_stay
add_executable(bo3_stay_test test_main.c ${CMAKE_CURRENT_LIST_DIR}/../../src/game/stay.c)
target_compile_definitions(bo3_stay_test PRIVATE STAY_PURE_ONLY)
target_include_directories(bo3_stay_test PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../src/game)
enable_testing()
add_test(NAME bo3_stay COMMAND bo3_stay_test)
