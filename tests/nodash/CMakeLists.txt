# nodash - SELECT GAMERTAG's list without CREATE NEW ACCOUNT / MANAGE ACCOUNTS,
# the A on what the player sees, and the dashboard questions' YES
# (src/game/nodash.c). The marks themselves are scene-tested
# (tools/headless/nodash_scene.sh); this checks the pure page edits.
#   cmake --build <build> --target bo3_nodash_test && ctest -R bo3_nodash
add_executable(bo3_nodash_test test_main.c ${CMAKE_CURRENT_LIST_DIR}/../../src/game/nodash.c)
target_compile_definitions(bo3_nodash_test PRIVATE BO3_NODASH_TEST=1)
target_include_directories(bo3_nodash_test PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../src/game)
enable_testing()
add_test(NAME bo3_nodash COMMAND bo3_nodash_test)
