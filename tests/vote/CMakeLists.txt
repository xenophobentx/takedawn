# vote - the track vote's rules (src/game/vote.c, BO-356): the rows, the tally,
# ties, the clock, a player leaving or joining, a lost packet, no votes at all.
#   cmake --build <build> --target bo3_vote_test && ctest -R bo3_vote
add_executable(bo3_vote_test test_main.c ${CMAKE_CURRENT_LIST_DIR}/../../src/game/vote.c)
target_compile_definitions(bo3_vote_test PRIVATE VOTE_PURE_ONLY)
target_include_directories(bo3_vote_test PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../src/game)
enable_testing()
add_test(NAME bo3_vote COMMAND bo3_vote_test)
