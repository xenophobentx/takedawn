# codeentry - JOIN WITH CODE's code entry (src/game/codeentry.c, Takedawn
# Online step 5): typing, deleting, the column, the lines the list shows and
# what each press does. The page itself is scene-tested.
#   cmake --build <build> --target bo3_codeentry_test && ctest -R bo3_codeentry
add_executable(bo3_codeentry_test test_main.c ${CMAKE_CURRENT_LIST_DIR}/../../src/game/codeentry.c)
target_include_directories(bo3_codeentry_test PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../src/game)
enable_testing()
add_test(NAME bo3_codeentry COMMAND bo3_codeentry_test)
