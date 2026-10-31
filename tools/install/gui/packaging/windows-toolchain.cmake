# Cross-compiling the installer's window for Windows with llvm-mingw (set LLVM_MINGW
# to its folder). Used by make-windows-cross.sh and tools/release/build-windows.sh.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(_llvm "$ENV{LLVM_MINGW}")
# On a Windows host the compilers are .exe files, and CMake looks for a full
# path as it is given.
set(_exe "")
if(CMAKE_HOST_WIN32)
    set(_exe ".exe")
endif()
set(CMAKE_C_COMPILER "${_llvm}/bin/x86_64-w64-mingw32-clang${_exe}")
set(CMAKE_CXX_COMPILER "${_llvm}/bin/x86_64-w64-mingw32-clang++${_exe}")
set(CMAKE_RC_COMPILER "${_llvm}/bin/x86_64-w64-mingw32-windres${_exe}")
set(CMAKE_FIND_ROOT_PATH "${_llvm}/x86_64-w64-mingw32")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
