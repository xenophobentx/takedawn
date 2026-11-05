# headfont - the heading font's W and the TAKEDAWN ONLINE heading (src/game/headfont.c).
#   cmake --build <build> --target bo3_headfont_test && ctest -R bo3_headfont
add_executable(bo3_headfont_test test_main.c ../../src/game/headfont.c ../../src/game/onlinebrand_text.c)
target_compile_definitions(bo3_headfont_test PRIVATE BO3_HEADFONT_TEST=1)
target_include_directories(bo3_headfont_test PRIVATE ../../src/game ${XBOXRECOMP_DIR}/templates/runtime ${XBOXRECOMP_DIR}/src)
if(NOT MSVC)
    target_link_libraries(bo3_headfont_test PRIVATE m)
endif()
enable_testing()
add_test(NAME bo3_headfont COMMAND bo3_headfont_test)
