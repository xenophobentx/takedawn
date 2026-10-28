# plugins - native plugins in installed mods (src/game/plugins.c over the mod
# folders and mod.toml as they are) on folders the test makes in the
# temporary directory: which modules are loaded, approved, started and
# refused. The modules are test_module.c built once per kind, and the example
# plugin (examples/plugins/hello). No game data, no guest.
#   cmake --build <build> --target bo3_plugins_test && ctest -R bo3_plugins
set(_kinds GOOD NOINIT NOVERSION NEWVERSION INITFAIL MARKER ORDER_A ORDER_B BIGINFO)
set(_mods_dir ${CMAKE_CURRENT_BINARY_DIR}/modules)
if(WIN32)
    set(_ext .asi)
elseif(APPLE)
    set(_ext .dylib)
else()
    set(_ext .so)
endif()
set(_targets)
foreach(k IN LISTS _kinds)
    string(TOLOWER ${k} _name)
    add_library(bo3_plugin_tm_${_name} MODULE test_module.c)
    target_compile_definitions(bo3_plugin_tm_${_name} PRIVATE TP_KIND_${k})
    target_include_directories(bo3_plugin_tm_${_name} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../include)
    set_target_properties(bo3_plugin_tm_${_name} PROPERTIES PREFIX "" OUTPUT_NAME ${_name} SUFFIX ${_ext}
                          LIBRARY_OUTPUT_DIRECTORY ${_mods_dir} C_VISIBILITY_PRESET hidden)
    if(NOT WIN32)
        target_link_libraries(bo3_plugin_tm_${_name} PRIVATE ${CMAKE_DL_LIBS})
    endif()
    list(APPEND _targets bo3_plugin_tm_${_name})
endforeach()
add_library(bo3_plugin_hello MODULE ${CMAKE_CURRENT_LIST_DIR}/../../examples/plugins/hello/hello.c)
target_include_directories(bo3_plugin_hello PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../include)
set_target_properties(bo3_plugin_hello PROPERTIES PREFIX "" OUTPUT_NAME hello SUFFIX ${_ext}
                      LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR} C_VISIBILITY_PRESET hidden)

add_executable(bo3_plugins_test test_main.c
    ${CMAKE_CURRENT_LIST_DIR}/../../src/game/plugins.c
    ${CMAKE_CURRENT_LIST_DIR}/../../src/game/modfs.c
    ${CMAKE_CURRENT_LIST_DIR}/../../src/game/modmanifest.c
    ${XBOXRECOMP_DIR}/src/enhance/recomp_cfg.c
    ${XBOXRECOMP_DIR}/src/install/xr_sha256.c)
if(WIN32)
    target_sources(bo3_plugins_test PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../src/game/plugins_win32.c)
else()
    target_sources(bo3_plugins_test PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../src/game/plugins_posix.c)
    find_package(Threads REQUIRED)
    target_link_libraries(bo3_plugins_test PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
endif()
target_include_directories(bo3_plugins_test PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../src/game
    ${CMAKE_CURRENT_LIST_DIR}/../../include
    ${XBOXRECOMP_DIR}/src
    ${XBOXRECOMP_DIR}/src/kernel
    ${XBOXRECOMP_DIR}/src/enhance
    ${XBOXRECOMP_DIR}/src/install)
add_dependencies(bo3_plugins_test ${_targets} bo3_plugin_hello)
enable_testing()
add_test(NAME bo3_plugins COMMAND bo3_plugins_test ${_mods_dir} ${CMAKE_CURRENT_BINARY_DIR}/hello${_ext})
