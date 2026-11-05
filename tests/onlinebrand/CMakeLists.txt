add_executable(bo3_onlinebrand_test test_main.c ../../src/game/onlinebrand_text.c)
target_include_directories(bo3_onlinebrand_test PRIVATE ../../src/game)
enable_testing()
add_test(NAME bo3_onlinebrand COMMAND bo3_onlinebrand_test)

add_executable(bo3_onlinebrand_slots_test test_slots.c ../../src/game/onlinebrand.c ../../src/game/onlinebrand_text.c)
target_compile_definitions(bo3_onlinebrand_slots_test PRIVATE BO3_ONLINEBRAND_TEST=1)
target_include_directories(bo3_onlinebrand_slots_test PRIVATE ../../src/game ${XBOXRECOMP_DIR}/templates/runtime ${XBOXRECOMP_DIR}/src)
add_test(NAME bo3_onlinebrand_slots COMMAND bo3_onlinebrand_slots_test)
