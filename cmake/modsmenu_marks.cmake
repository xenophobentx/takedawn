# modsmenu_marks.cmake - whether the generated code has the marks the MODS
# page needs, made by the build (src/game/CMakeLists.txt) whenever a
# generated file or config/patches.json changes:
#
#   cmake -DPATCHES=<config/patches.json> -DGEN=<src/game/recomp/gen>
#         -DOUT=<modsmenu_marks.c> [-DRELEASE=ON] -P modsmenu_marks.cmake
#
# On MODS the page lets A through to the title only because "mm_flow_sel" and
# "mm_flow_back" (sub_000696D0) turn the title's change of state into a change
# to the current one, and on the page it keeps A from the title through
# menuentry.c's "si_select" ("si_items" lists the lines). Generated code made
# before config/patches.json had them (a checkout built without regen.sh: a
# build that is not a release only calls such code stale) would have the
# title change state with the entry it read past its four-entry next-page
# table. So OUT's bo3_modsmenu_gen_marks() returns 1 only when the generated
# C calls each of these marks at every va config/patches.json gives it, on a
# line of its own as tools/recomp writes it; modsmenu.c lists MODS only then.
# A release build without them stops.
cmake_minimum_required(VERSION 3.20)

foreach(_v PATCHES GEN OUT)
    if(NOT ${_v})
        message(FATAL_ERROR "modsmenu_marks.cmake: -D${_v}= is missing")
    endif()
endforeach()

set(_want si_items si_select mm_flow_sel mm_flow_back)
set(_found 1)
set(_why "")
file(READ "${PATCHES}" _json)
string(JSON _n LENGTH "${_json}" patches)
math(EXPR _last "${_n} - 1")
# tools/recomp/patches.py: RECOMP_PATCH_MARK("name"); /* patch 0x%08X, mark */
# on a line of its own. One read of the generated files: "name@va" seen.
file(GLOB _files "${GEN}/recomp_*.c")
list(SORT _files)
list(JOIN _want "|" _alt)
set(_seen "")
foreach(_f ${_files})
    file(STRINGS "${_f}" _lines
         REGEX "^[ \t]*RECOMP_PATCH_MARK\\(\"(${_alt})\"\\); /\\* patch 0x[0-9A-F]+, mark \\*/[ \t\r]*$")
    foreach(_l ${_lines})
        if(_l MATCHES "\"([a-z_]+)\"\\); /\\* patch 0x([0-9A-F]+), mark")
            list(APPEND _seen "${CMAKE_MATCH_1}@${CMAKE_MATCH_2}")
        endif()
    endforeach()
endforeach()
foreach(_name ${_want})
    # Every va patches.json gives the mark: "0x..." as tools/recomp reads it.
    set(_vas "")
    foreach(_i RANGE ${_last})
        string(JSON _kind ERROR_VARIABLE _e GET "${_json}" patches ${_i} kind)
        string(JSON _pname ERROR_VARIABLE _e GET "${_json}" patches ${_i} name)
        if(_kind STREQUAL "mark" AND _pname STREQUAL _name)
            string(JSON _va GET "${_json}" patches ${_i} va)
            if(_va MATCHES "^0[xX]([0-9A-Fa-f]+)$")
                string(TOUPPER "${CMAKE_MATCH_1}" _hex)
                string(LENGTH "${_hex}" _len)
                while(_len LESS 8)
                    string(PREPEND _hex "0")
                    math(EXPR _len "${_len} + 1")
                endwhile()
                list(APPEND _vas "${_hex}")
            else()
                set(_found 0)
                string(APPEND _why " ${_name}: va '${_va}' in config/patches.json is not 0x...;")
            endif()
        endif()
    endforeach()
    if(NOT _vas)
        set(_found 0)
        string(APPEND _why " ${_name}: not a mark in config/patches.json;")
        continue()
    endif()
    foreach(_hex ${_vas})
        if(NOT "${_name}@${_hex}" IN_LIST _seen)
            set(_found 0)
            string(APPEND _why " ${_name}: no call at 0x${_hex} in ${GEN};")
        endif()
    endforeach()
endforeach()

list(JOIN _want ", " _list)
if(_found)
    file(WRITE "${OUT}" "/* Made by cmake/modsmenu_marks.cmake: the generated code calls ${_list}. */\n"
                        "int bo3_modsmenu_gen_marks(void) { return 1; }\n")
else()
    file(WRITE "${OUT}" "/* Made by cmake/modsmenu_marks.cmake: the generated code lacks the MODS page's marks:${_why} */\n"
                        "int bo3_modsmenu_gen_marks(void) { return 0; }\n")
    if(RELEASE)
        message(FATAL_ERROR "BO3_RELEASE: the generated code lacks the MODS page's marks (${_why} ): run regen.sh")
    endif()
    message(WARNING "burnout3: the MODS page is left out of the Settings menu: the generated code lacks its "
                    "marks (${_why} ): run regen.sh and build again")
endif()
