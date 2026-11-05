# onlinebot - the online test navigator's screen table, mode list and start
# gate (src/game/onlinebot.c, BO-134; the screen table itself lives in
# src/game/onlinescreens.c, BO-352). The navigator itself reads the title's
# memory and is tested on real copies (tools/online/bot/README.md).
#   cmake --build <build> --target bo3_onlinebot_test && ctest -R bo3_onlinebot
add_executable(bo3_onlinebot_test test_main.c
    ${CMAKE_CURRENT_LIST_DIR}/../../src/game/onlinebot.c
    ${CMAKE_CURRENT_LIST_DIR}/../../src/game/onlinescreens.c
    ${CMAKE_CURRENT_LIST_DIR}/../../src/game/codeentry.c)
target_compile_definitions(bo3_onlinebot_test PRIVATE ONLINEBOT_TABLE_ONLY)
target_include_directories(bo3_onlinebot_test PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../src/game)
enable_testing()
add_test(NAME bo3_onlinebot COMMAND bo3_onlinebot_test)
