# onlineinfo - the online info line's parser and text (src/game/onlineinfo.c,
# BO-352 step 8). The probe itself talks to the lobby and is scene-tested.
#   cmake --build <build> --target bo3_onlineinfo_test && ctest -R bo3_onlineinfo
add_executable(bo3_onlineinfo_test test_main.c ${CMAKE_CURRENT_LIST_DIR}/../../src/game/onlineinfo.c)
target_compile_definitions(bo3_onlineinfo_test PRIVATE ONLINEINFO_PURE_ONLY)
target_include_directories(bo3_onlineinfo_test PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../src/game)
enable_testing()
add_test(NAME bo3_onlineinfo COMMAND bo3_onlineinfo_test)
