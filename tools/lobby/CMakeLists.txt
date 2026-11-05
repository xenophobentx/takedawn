# takedawn-lobby: the lobby server for Takedawn's online play, as its own
# program (MIT). Builds anywhere with a C compiler:
#   cmake -S tools/lobby -B build/lobby && cmake --build build/lobby
cmake_minimum_required(VERSION 3.16)
project(takedawn_lobby C)
add_executable(takedawn-lobby main.c aries_server.c)
if(WIN32)
    target_link_libraries(takedawn-lobby PRIVATE ws2_32)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(takedawn-lobby PRIVATE Threads::Threads)
endif()
if(NOT WIN32)
    # Statistics and limits (POSIX): ctest, or ./test_lobby
    add_executable(test_lobby EXCLUDE_FROM_ALL test_lobby.c)
    target_link_libraries(test_lobby PRIVATE Threads::Threads)
    enable_testing()
    add_test(NAME lobby COMMAND test_lobby)
endif()
install(TARGETS takedawn-lobby RUNTIME DESTINATION bin)
