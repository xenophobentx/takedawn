# modmanifest - a mod's mod.toml (src/game/modmanifest.c over the toolkit's
# TOML reader): the fields, every rejection, and a mutation run on damaged
# input. No files, no game data.
#   cmake --build <build> --target bo3_modmanifest_test && ctest -R bo3_modmanifest
add_executable(bo3_modmanifest_test test_main.c
    ${CMAKE_CURRENT_LIST_DIR}/../../src/game/modmanifest.c
    ${XBOXRECOMP_DIR}/src/enhance/recomp_cfg.c)
target_include_directories(bo3_modmanifest_test PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../src/game
    ${XBOXRECOMP_DIR}/src/enhance)
enable_testing()
add_test(NAME bo3_modmanifest COMMAND bo3_modmanifest_test)
