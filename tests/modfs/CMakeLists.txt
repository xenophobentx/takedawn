# modfs - the mod folders' lookup (src/game/modfs.c, docs/mod-folders.md) on
# folders the test makes in the temporary directory: the name rules, case,
# links, several folders, installed mods (mod.toml), the hook chain, threads.
# No game data, no guest; the same cases on every host (the ones a host
# cannot show say SKIP).
#   cmake --build <build> --target bo3_modfs_test && ctest -R bo3_modfs
add_executable(bo3_modfs_test test_main.c
    ${CMAKE_CURRENT_LIST_DIR}/../../src/game/modfs.c
    ${CMAKE_CURRENT_LIST_DIR}/../../src/game/modmanifest.c
    ${XBOXRECOMP_DIR}/src/enhance/recomp_cfg.c)
target_include_directories(bo3_modfs_test PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../src/game
    ${XBOXRECOMP_DIR}/src
    ${XBOXRECOMP_DIR}/src/kernel
    ${XBOXRECOMP_DIR}/src/enhance)
if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(bo3_modfs_test PRIVATE Threads::Threads)
endif()
enable_testing()
add_test(NAME bo3_modfs COMMAND bo3_modfs_test)
