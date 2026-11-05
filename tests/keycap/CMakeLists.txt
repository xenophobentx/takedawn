# keycap - the key caps drawn for the player's own keys (src/game/keycap.c, BO-267): labels of keys, the
# cell rectangles of the keyboard prompt sheet, what a cap looks like (opaque face, transparent corners,
# a label in the cap's own colour), that a long label shrinks to fit, that nothing is drawn outside the
# cell's rectangle, and that the stroke table is the one tools/glyphs/draw.py makes.
#   cmake --build <build> --target bo3_keycap_test && ctest -R bo3_keycap
add_executable(bo3_keycap_test test_main.c ${CMAKE_CURRENT_LIST_DIR}/../../src/game/keycap.c)
target_include_directories(bo3_keycap_test PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../src/game)
if(NOT MSVC)
    target_link_libraries(bo3_keycap_test PRIVATE m)
endif()
enable_testing()
add_test(NAME bo3_keycap COMMAND bo3_keycap_test)
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME bo3_keycap_font COMMAND ${Python3_EXECUTABLE}
             ${CMAKE_CURRENT_LIST_DIR}/../../tools/glyphs/gen_keyfont.py --check)
endif()
