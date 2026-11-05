# modtex - a texture pack's pack.toml (src/game/modtex.c, docs/mod-folders.md,
# Texture packs). The toolkit's nv2a_texpack_set is a stub that records what
# bo3_modtex_init hands it.
#   cmake --build <build> --target bo3_modtex_test && ctest -R bo3_modtex
add_executable(bo3_modtex_test test_main.c
    ${CMAKE_CURRENT_LIST_DIR}/../../src/game/modtex.c
    ${CMAKE_CURRENT_LIST_DIR}/../../src/game/modfs.c
    ${CMAKE_CURRENT_LIST_DIR}/../../src/game/modmanifest.c
    ${XBOXRECOMP_DIR}/src/enhance/recomp_cfg.c)
target_include_directories(bo3_modtex_test PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../src/game
    ${XBOXRECOMP_DIR}/src
    ${XBOXRECOMP_DIR}/src/kernel
    ${XBOXRECOMP_DIR}/src/enhance)
if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(bo3_modtex_test PRIVATE Threads::Threads)
endif()
enable_testing()
add_test(NAME bo3_modtex COMMAND bo3_modtex_test)
