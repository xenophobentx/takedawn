# mainquit - the main menu's added lines' layout (src/game/mainquit.h, BO-352).
# The mark itself is scene-tested; this checks the pure line plan.
#   cmake --build <build> --target bo3_mainquit_test && ctest -R bo3_mainquit
add_executable(bo3_mainquit_test test_main.c)
target_include_directories(bo3_mainquit_test PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../src/game)
enable_testing()
add_test(NAME bo3_mainquit COMMAND bo3_mainquit_test)
