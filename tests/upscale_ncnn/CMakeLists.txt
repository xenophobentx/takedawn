# upscale_ncnn - the in-process upscaler (src/game/upscale_ncnn.cpp, BO-326)
# against the desktop's realesrgan-ncnn-vulkan, on pictures of the player's
# own disc. Standalone (it needs an ncnn build and the game's data, so it is
# not part of the game's build or ctest):
#
#   cmake -S tests/upscale_ncnn -B <b> -Dncnn_DIR=<ncnn>/lib/cmake/ncnn \
#         -DXBOXRECOMP_DIR=<toolkit>
#   <b>/bo3_upscale_ncnn make "<game>/Data" <work> # disc pictures -> <work>/in
#   realesrgan-ncnn-vulkan -i <work>/in -o <work>/desktop -n realesr-general-dn030-x4v3 \
#         -m <models> -s 4 -f png
#   <b>/bo3_upscale_ncnn run <models> <work> [--fp16 0|1|2] [--tile n] [--threads n] [--vulkan <loader>]
#         # -> <work>/ncnn
#   <b>/bo3_upscale_ncnn diff <work>/desktop <work>/ncnn
#
# The pictures stay in <work>: they are the game's art, not to be shared.
cmake_minimum_required(VERSION 3.20)
project(bo3_upscale_ncnn C CXX)
set(CMAKE_CXX_STANDARD 11)
find_package(ncnn REQUIRED)
if(NOT XBOXRECOMP_DIR)
    set(XBOXRECOMP_DIR ${CMAKE_CURRENT_LIST_DIR}/../../../xboxrecomp)
endif()
set(G ${CMAKE_CURRENT_LIST_DIR}/../../src/game)
add_executable(bo3_upscale_ncnn main.c ${G}/upscale_ncnn.cpp ${G}/upscale_tile.c ${G}/upscale_art.c)
target_include_directories(bo3_upscale_ncnn PRIVATE ${G} ${XBOXRECOMP_DIR}/src ${XBOXRECOMP_DIR}/third_party/stb)
target_link_libraries(bo3_upscale_ncnn PRIVATE ncnn)
