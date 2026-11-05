# release_info.cmake - what a build says it is (burnout3 --version, the
# overlay's Advanced page, the installed data's .complete marker), made at
# configure time into <build>/release/:
#
#   bo3_release.h    BO3_VERSION, BO3_CODENAME   the VERSION file: the version
#                    on its first line, the release's codename on its second
#                    (a song title from the game's soundtrack; shown beside
#                    the version and nowhere in a file or folder name)
#                    BO3_REGEN_KEY       what the generated code was made from
#                    BO3_MANIFEST_ID ... the disc manifest's own lines
#                    BO3_GEN_TREE, BO3_GEN_PATCHES   the generated code's stamp
#   bo3_manifest.inc the manifest's entries as C initialisers
#                    BO3_BUILD_ID        this program's build (supported.json)
#   bo3_package.inc  the package's builds (supported.json "package"), in its
#                    order, as k_package[]: id, program suffix, per-user
#                    folder, disc name, manifest id and entries (the others'
#                    in full, this one's are k_files); BO3_PACKAGE_COUNT,
#                    BO3_PACKAGE_SELF
#
# Run as a script (cmake -D... -P release_info.cmake) it writes the same
# files and nothing else: tools/release/common.sh reads the key that way.
#
# The regen key is the first 16 hex digits of the SHA-256 over the supported
# default.xbe's hash and the recompiler's inputs, each as "<name> <sha256>":
# config/*.json, regen.sh, recomp_manual.c and the toolkit's five recompiler
# stages (their .py files, tests left out). Carriage returns are dropped
# first, so a checkout with CRLF line ends has the same key. The same commits
# of both repositories give the same key on every machine.
#
# Wants BO3_ROOT (the game repository), XBOXRECOMP_DIR and BO3_RELEASE_DIR.

file(STRINGS "${BO3_ROOT}/VERSION" _bo3_version_lines)
list(GET _bo3_version_lines 0 BO3_VERSION)
list(LENGTH _bo3_version_lines _n)
set(BO3_CODENAME "")
if(_n GREATER 1)
    list(GET _bo3_version_lines 1 BO3_CODENAME)
endif()
string(STRIP "${BO3_VERSION}" BO3_VERSION)
string(STRIP "${BO3_CODENAME}" BO3_CODENAME)
# <major>.<minor>.<patch>, or a pre-release of it: 0.1.2-beta, 0.1.2-beta.2.
# BO3_VERSION_NUM is the three numbers alone (Windows' version resource).
if(NOT BO3_VERSION MATCHES "^([0-9]+\\.[0-9]+\\.[0-9]+)(-[0-9A-Za-z.]+)?$")
    message(FATAL_ERROR "VERSION: the first line must be <major>.<minor>.<patch>[-<pre-release>], not '${BO3_VERSION}'")
endif()
set(BO3_VERSION_NUM "${CMAKE_MATCH_1}")
if(BO3_CODENAME MATCHES "[\"\\\\]")
    message(FATAL_ERROR "VERSION: the codename cannot hold a quote or a backslash")
endif()

# ── the regen key ────────────────────────────────────────────────────
# The disc: BO3_REGION (NTSC-U, the default, PAL or PAL-ENSV) picks the build
# of tools/install/supported.json, its recompiler inputs, its manifest and
# the folder of its generated code.
if(NOT DEFINED BO3_REGION OR BO3_REGION STREQUAL "")
    set(BO3_REGION "NTSC-U")
endif()
set(_gen_name "gen")
if(BO3_REGION STREQUAL "PAL")
    set(_build_id "pal-retail")
    set(_cfg "config/pal")
    set(_manual_names src/game/recomp/recomp_manual.c src/game/recomp/recomp_manual_pal.c)
    set(_gen_name "gen.pal")
elseif(BO3_REGION STREQUAL "PAL-ENSV")
    set(_build_id "pal-ensv-retail")
    set(_cfg "config/pal-ensv")
    set(_manual_names src/game/recomp/recomp_manual.c src/game/recomp/recomp_manual_pal_ensv.c)
    set(_gen_name "gen.pal-ensv")
else()
    set(_build_id "ntsc-u-retail")
    set(_cfg "config")
    set(_manual_names src/game/recomp/recomp_manual.c)
endif()
file(READ "${BO3_ROOT}/tools/install/supported.json" _supported)
string(JSON _nb LENGTH "${_supported}" builds)
set(_xbe_sha "")
set(_package "")
math(EXPR _nb_last "${_nb} - 1")
foreach(_b RANGE ${_nb_last})
    string(JSON _id GET "${_supported}" builds ${_b} id)
    if(_id STREQUAL _build_id)
        string(JSON _xbe_sha GET "${_supported}" builds ${_b} xbe sha256)
        string(JSON _package ERROR_VARIABLE _no_package GET "${_supported}" builds ${_b} package)
        if(_no_package)
            set(_package "")
        endif()
    endif()
endforeach()
if(NOT _xbe_sha)
    message(FATAL_ERROR "tools/install/supported.json has no build '${_build_id}'")
endif()
# The package (supported.json "package"): every build of it, in its order,
# goes into every program of it (bo3_package.inc: id, program suffix, per-user
# folder, disc name, manifest), so that each program knows every disc of the
# package and starts the program whose disc is installed (src/game/package.c,
# src/game/install.c). The first is the main program; the others lie beside it
# with their suffix after its name.
set(BO3_PACKAGE_IDS "")
set(BO3_PACKAGE_SUFFIXES "")
set(BO3_PACKAGE_USER_DIRS "")
set(BO3_PACKAGE_NAMES "")
if(_package)
    foreach(_b RANGE ${_nb_last})
        string(JSON _id GET "${_supported}" builds ${_b} id)
        string(JSON _p ERROR_VARIABLE _no_package GET "${_supported}" builds ${_b} package)
        if(NOT _no_package AND _p STREQUAL _package)
            string(JSON _sfx ERROR_VARIABLE _no_sfx GET "${_supported}" builds ${_b} program_suffix)
            string(JSON _ud ERROR_VARIABLE _no_ud GET "${_supported}" builds ${_b} user_dir)
            string(JSON _nm GET "${_supported}" builds ${_b} name)
            if(_no_sfx OR _no_ud)
                message(FATAL_ERROR "supported.json: build '${_id}' of package '${_package}' needs program_suffix and user_dir")
            endif()
            string(REGEX REPLACE "^Burnout 3: Takedown, " "" _nm "${_nm}")
            list(APPEND BO3_PACKAGE_IDS "${_id}")
            list(APPEND BO3_PACKAGE_SUFFIXES "x${_sfx}")   # (an empty list item would vanish)
            list(APPEND BO3_PACKAGE_USER_DIRS "${_ud}")
            list(APPEND BO3_PACKAGE_NAMES "${_nm}")
            if(NOT CMAKE_SCRIPT_MODE_FILE)
                set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                    "${BO3_ROOT}/config/disc/${_id}.manifest")
            endif()
        endif()
    endforeach()
endif()
set(_key_text "default.xbe ${_xbe_sha}\n")
set(_names ${_cfg}/patches.json ${_cfg}/seed_functions.json ${_cfg}/spin_waits.json regen.sh
    ${_manual_names})
set(_inputs "")
foreach(_n ${_names})
    list(APPEND _inputs "${BO3_ROOT}/${_n}")
endforeach()
foreach(_stage xbe_parser disasm func_id abi_analysis recomp)
    file(GLOB_RECURSE _py RELATIVE "${XBOXRECOMP_DIR}" "${XBOXRECOMP_DIR}/tools/${_stage}/*.py")
    list(SORT _py)
    foreach(_f ${_py})
        if(NOT _f MATCHES "(^|/)test[^/]*(/|$)" AND NOT _f MATCHES "__pycache__")
            list(APPEND _inputs "${XBOXRECOMP_DIR}/${_f}")
            list(APPEND _names "toolkit/${_f}")
        endif()
    endforeach()
endforeach()
list(LENGTH _inputs _n)
math(EXPR _last "${_n} - 1")
foreach(_i RANGE ${_last})
    list(GET _inputs ${_i} _f)
    list(GET _names ${_i} _name)
    if(EXISTS "${_f}")
        file(READ "${_f}" _text)
        string(REPLACE "\r" "" _text "${_text}")
        string(SHA256 _h "${_text}")
        if(NOT CMAKE_SCRIPT_MODE_FILE)
            set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_f}")
        endif()
    else()
        set(_h "missing")
    endif()
    string(APPEND _key_text "${_name} ${_h}\n")
endforeach()
string(SHA256 BO3_REGEN_KEY "${_key_text}")
string(SUBSTRING "${BO3_REGEN_KEY}" 0 16 BO3_REGEN_KEY)

# ── the disc manifest ────────────────────────────────────────────────
# _bo3_manifest(<build id> <prefix>): config/disc/<build id>.manifest's
# entries as C initialisers in <prefix>_INC, its lines in <prefix>_ID,
# <prefix>_BUILD, <prefix>_TITLE_ID and <prefix>_COUNT.
function(_bo3_manifest _id _pfx)
    set(_manifest "${BO3_ROOT}/config/disc/${_id}.manifest")
    file(STRINGS "${_manifest}" _lines)
    set(_inc "/* Made by cmake/release_info.cmake from config/disc/${_id}.manifest. */\n")
    set(_count 0)
    set(_mid "")
    foreach(_line ${_lines})
        if(_line MATCHES "^# id ([0-9a-f]+)")
            set(_mid "${CMAKE_MATCH_1}")
        elseif(_line MATCHES "^# build (.+)$")
            set(${_pfx}_BUILD "${CMAKE_MATCH_1}" PARENT_SCOPE)
        elseif(_line MATCHES "^# title_id (0x[0-9A-Fa-f]+)")
            set(${_pfx}_TITLE_ID "${CMAKE_MATCH_1}" PARENT_SCOPE)
        elseif(_line MATCHES "^# files ([0-9]+)")
            set(_want "${CMAKE_MATCH_1}")
        elseif(_line MATCHES "^([0-9a-f]+) ([0-9]+) ([^\"\\\\]+)$")
            string(APPEND _inc "{ \"${CMAKE_MATCH_3}\", ${CMAKE_MATCH_2}ull, \"${CMAKE_MATCH_1}\" },\n")
            math(EXPR _count "${_count} + 1")
        elseif(NOT _line MATCHES "^#")
            message(FATAL_ERROR "${_manifest}: not a manifest line: ${_line}")
        endif()
    endforeach()
    if(NOT _count EQUAL _want OR NOT _mid)
        message(FATAL_ERROR "${_manifest}: ${_count} entries where its header says ${_want}, or no id")
    endif()
    set(${_pfx}_INC "${_inc}" PARENT_SCOPE)
    set(${_pfx}_ID "${_mid}" PARENT_SCOPE)
    set(${_pfx}_COUNT "${_count}" PARENT_SCOPE)
endfunction()
_bo3_manifest("${_build_id}" BO3_MANIFEST)
set(_inc "${BO3_MANIFEST_INC}")
set(_count "${BO3_MANIFEST_COUNT}")
set(_manifest "${BO3_ROOT}/config/disc/${_build_id}.manifest")
# The package's table: the other builds' manifests in full (a disc given to
# --install is told by them), this one's by bo3_manifest.inc's k_files.
set(_pkg_inc "/* Made by cmake/release_info.cmake from tools/install/supported.json and config/disc/. */\n")
list(LENGTH BO3_PACKAGE_IDS _np)
set(_pkg_rows "")
set(_self -1)
if(_np GREATER 0)
    math(EXPR _np_last "${_np} - 1")
    foreach(_i RANGE ${_np_last})
        list(GET BO3_PACKAGE_IDS ${_i} _id)
        list(GET BO3_PACKAGE_SUFFIXES ${_i} _sfx)
        list(GET BO3_PACKAGE_USER_DIRS ${_i} _ud)
        list(GET BO3_PACKAGE_NAMES ${_i} _nm)
        string(SUBSTRING "${_sfx}" 1 -1 _sfx)
        if(_id STREQUAL _build_id)
            set(_self ${_i})
            set(_files "k_files")
            set(_n "${_count}")
            set(_mid "${BO3_MANIFEST_ID}")
        else()
            _bo3_manifest("${_id}" _PKG)
            string(APPEND _pkg_inc "static const xr_manifest_file k_pkg_files_${_i}[] = {\n${_PKG_INC}};\n")
            set(_files "k_pkg_files_${_i}")
            set(_n "${_PKG_COUNT}")
            set(_mid "${_PKG_ID}")
        endif()
        string(APPEND _pkg_rows "    { \"${_id}\", \"${_sfx}\", \"${_ud}\", \"${_nm}\", \"${_mid}\", ${_files}, ${_n}u },\n")
    endforeach()
endif()
if(_np GREATER 0 AND _self LESS 0)
    message(FATAL_ERROR "supported.json: build '${_build_id}' is not in its own package")
endif()
if(_np EQUAL 0)
    set(_np 1)
    set(_self 0)
    set(_pkg_rows "    { \"${_build_id}\", \"\", \"\", \"${_build_id}\", \"${BO3_MANIFEST_ID}\", k_files, ${_count}u },\n")
endif()
string(APPEND _pkg_inc "#define BO3_PACKAGE_COUNT ${_np}\n#define BO3_PACKAGE_SELF ${_self}\n"
       "static const bo3_package_build k_package[BO3_PACKAGE_COUNT] = {\n${_pkg_rows}};\n")
if(NOT CMAKE_SCRIPT_MODE_FILE)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${BO3_ROOT}/VERSION" "${_manifest}" "${BO3_ROOT}/tools/install/supported.json")
endif()

# ── the generated code's stamp ───────────────────────────────────────
# tools/release/gen.sh writes gen/regen.stamp beside the generated C: the
# regen key it was made with, a hash of the files, and how many of the guest
# patches of config/patches.json went in. A release build (-DBO3_RELEASE=ON)
# refuses a tree without it, one made from other inputs, one whose files
# changed since, and one without the patches; any other build only repeats
# what the stamp says.
set(_gen "${BO3_ROOT}/src/game/recomp/${_gen_name}")
set(BO3_GEN_TREE "unstamped")
set(BO3_GEN_PATCHES "unknown")
set(_stamp_key "")
if(EXISTS "${_gen}/regen.stamp")
    file(STRINGS "${_gen}/regen.stamp" _stamp)
    foreach(_line ${_stamp})
        if(_line MATCHES "^key ([0-9a-f]+)$")
            set(_stamp_key "${CMAKE_MATCH_1}")
        elseif(_line MATCHES "^tree ([0-9a-f]+)$")
            set(BO3_GEN_TREE "${CMAKE_MATCH_1}")
        elseif(_line MATCHES "^patches ([0-9]+/[0-9]+)$")
            set(BO3_GEN_PATCHES "${CMAKE_MATCH_1}")
        endif()
    endforeach()
    if(NOT CMAKE_SCRIPT_MODE_FILE)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_gen}/regen.stamp")
    endif()
    if(NOT _stamp_key STREQUAL BO3_REGEN_KEY AND NOT BO3_RELEASE)
        set(BO3_GEN_TREE "stale")
    endif()
endif()
if(BO3_RELEASE)
    if(NOT _stamp_key)
        message(FATAL_ERROR "BO3_RELEASE: ${_gen}/regen.stamp is missing: the generated code "
                "must come from tools/release/gen.sh")
    endif()
    if(NOT _stamp_key STREQUAL BO3_REGEN_KEY)
        message(FATAL_ERROR "BO3_RELEASE: the generated code was made with regen key ${_stamp_key}, "
                "this checkout's inputs give ${BO3_REGEN_KEY}")
    endif()
    file(GLOB _gen_files RELATIVE "${_gen}" "${_gen}/recomp_*")
    list(SORT _gen_files)
    set(_tree_text "")
    foreach(_f ${_gen_files})
        file(SHA256 "${_gen}/${_f}" _h)
        string(APPEND _tree_text "${_h}  ${_f}\n")
    endforeach()
    string(SHA256 _tree "${_tree_text}")
    if(NOT _tree STREQUAL BO3_GEN_TREE)
        message(FATAL_ERROR "BO3_RELEASE: the generated files do not match their stamp "
                "(${_tree}, stamp ${BO3_GEN_TREE})")
    endif()
    if(NOT BO3_GEN_PATCHES MATCHES "^([1-9][0-9]*)/([0-9]+)$" OR
       NOT CMAKE_MATCH_1 STREQUAL CMAKE_MATCH_2)
        message(FATAL_ERROR "BO3_RELEASE: the generated code lacks the guest patches "
                "(stamp says ${BO3_GEN_PATCHES})")
    endif()
endif()

set(_header "/* Made by cmake/release_info.cmake at configure time. */
#define BO3_VERSION \"${BO3_VERSION}\"
#define BO3_CODENAME \"${BO3_CODENAME}\"
#define BO3_REGEN_KEY \"${BO3_REGEN_KEY}\"
#define BO3_MANIFEST_ID \"${BO3_MANIFEST_ID}\"
#define BO3_MANIFEST_BUILD \"${BO3_MANIFEST_BUILD}\"
#define BO3_MANIFEST_TITLE_ID ${BO3_MANIFEST_TITLE_ID}u
#define BO3_GEN_TREE \"${BO3_GEN_TREE}\"
#define BO3_GEN_PATCHES \"${BO3_GEN_PATCHES}\"
#define BO3_XBE_SHA256 \"${_xbe_sha}\"
#define BO3_BUILD_ID \"${_build_id}\"
")
foreach(_pair "bo3_release.h|_header" "bo3_manifest.inc|_inc" "bo3_package.inc|_pkg_inc")
    string(REPLACE "|" ";" _pair "${_pair}")
    list(GET _pair 0 _file)
    list(GET _pair 1 _var)
    set(_old "")
    if(EXISTS "${BO3_RELEASE_DIR}/${_file}")
        file(READ "${BO3_RELEASE_DIR}/${_file}" _old)
    endif()
    if(NOT _old STREQUAL "${${_var}}")
        file(WRITE "${BO3_RELEASE_DIR}/${_file}" "${${_var}}")
    endif()
endforeach()
message(STATUS "Takedawn ${BO3_VERSION} \"${BO3_CODENAME}\", regen key ${BO3_REGEN_KEY}, "
               "disc manifest ${BO3_MANIFEST_ID} (${_count} files), generated code ${BO3_GEN_TREE}, "
               "patches ${BO3_GEN_PATCHES}")
