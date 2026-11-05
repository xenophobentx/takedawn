# xboxlive - the Xbox Live menu's added lines' layout (src/game/xboxlive.h,
# BO-352). The marks themselves are scene-tested; this checks the pure plan.
#   cmake --build <build> --target bo3_xboxlive_test && ctest -R bo3_xboxlive
add_executable(bo3_xboxlive_test test_main.c)
target_include_directories(bo3_xboxlive_test PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../src/game)
enable_testing()
add_test(NAME bo3_xboxlive COMMAND bo3_xboxlive_test)
