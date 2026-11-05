# languages - the Language row's list for each supported disc
# (src/game/languages.c), from the committed disc manifests (file names only).
#   cmake --build <build> --target bo3_languages_test && ctest -R bo3_languages
add_executable(bo3_languages_test test_main.c ${CMAKE_CURRENT_LIST_DIR}/../../src/game/languages.c)
target_include_directories(bo3_languages_test PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../src/game)
enable_testing()
add_test(NAME bo3_languages COMMAND bo3_languages_test ${CMAKE_CURRENT_LIST_DIR}/../../config/disc)
