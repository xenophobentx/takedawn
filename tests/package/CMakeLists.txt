# package - which of the package's three programs runs (src/game/package.c,
# BO-341), and the package table this build compiled in (bo3_package.inc,
# cmake/release_info.cmake from tools/install/supported.json and the disc
# manifests): every disc's executable names its own program.
#   cmake --build <build> --target bo3_package_test && ctest -R bo3_package
add_executable(bo3_package_test test_main.c ${CMAKE_CURRENT_LIST_DIR}/../../src/game/package.c)
target_include_directories(bo3_package_test PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../src/game
                           ${CMAKE_BINARY_DIR}/src/game/release)
enable_testing()
add_test(NAME bo3_package COMMAND bo3_package_test)
