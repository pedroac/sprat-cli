cmake_minimum_required(VERSION 3.15)
project(sprat LANGUAGES CXX)
include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options(-fpermissive)
endif()

if(WIN32)
    add_definitions(-DNOMINMAX)
    option(SPRAT_STATIC "Prefer static linking for dependencies on Windows" OFF)
    if(MINGW)
        add_link_options(-static -static-libgcc -static-libstdc++)
    endif()
    if(SPRAT_STATIC)
        # Prefer .lib/.a over .dll/.dll.a
        set(CMAKE_FIND_LIBRARY_SUFFIXES .lib .a ${CMAKE_FIND_LIBRARY_SUFFIXES})
    endif()
endif()

# Version detection
set(SPRAT_VERSION "0.0.0")

# Try to get version from git tag first
execute_process(
    COMMAND git describe --tags --always --dirty
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_VERSION
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

if(GIT_VERSION)
    string(REPLACE "\n" " " GIT_VERSION "${GIT_VERSION}")
    string(STRIP "${GIT_VERSION}" GIT_VERSION)
    # Take only the first word in case of multiple lines
    string(REPLACE " " ";" GIT_VERSION_LIST "${GIT_VERSION}")
    list(GET GIT_VERSION_LIST 0 GIT_VERSION)
    set(SPRAT_VERSION ${GIT_VERSION})
endif()

# Override with VERSION if it exists and is not empty (e.g. from build.sh)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" VERSION_FROM_FILE)
    string(REPLACE "\n" " " VERSION_FROM_FILE "${VERSION_FROM_FILE}")
    string(STRIP "${VERSION_FROM_FILE}" VERSION_FROM_FILE_STRIP)
    if(NOT VERSION_FROM_FILE_STRIP STREQUAL "")
        # Take only the first word/line
        string(REPLACE " " ";" VERSION_LIST "${VERSION_FROM_FILE_STRIP}")
        list(GET VERSION_LIST 0 VERSION_FIRST)
        set(SPRAT_VERSION ${VERSION_FIRST})
    endif()
endif()

message(STATUS "Sprat version: ${SPRAT_VERSION}")
add_definitions(-DSPRAT_VERSION="${SPRAT_VERSION}")

# External headers
set(STB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/stb)
file(MAKE_DIRECTORY ${STB_DIR})

option(SPRAT_DOWNLOAD_STB "Download stb headers when missing" OFF)
set(STB_REF "master" CACHE STRING "Git ref (branch, tag, or commit) used when downloading stb headers")
set(STB_BASE_URL https://raw.githubusercontent.com/nothings/stb/${STB_REF})

function(ensure_stb_header header_name)
    set(target_path ${STB_DIR}/${header_name})
    if(EXISTS ${target_path})
        file(SIZE ${target_path} target_size)
        if(target_size GREATER 0)
            return()
        endif()
        message(WARNING "Found empty ${header_name}")
    endif()

    if(NOT SPRAT_DOWNLOAD_STB)
        message(FATAL_ERROR
            "Missing ${header_name} in ${STB_DIR}. "
            "Either provide vendored headers or configure with "
            "-DSPRAT_DOWNLOAD_STB=ON (optionally set -DSTB_REF=<tag-or-commit>).")
    endif()

    set(download_path ${target_path}.tmp)
    if(EXISTS ${download_path})
        file(REMOVE ${download_path})
    endif()

    message(STATUS "Downloading ${header_name} from ${STB_BASE_URL}")
    file(
        DOWNLOAD
        ${STB_BASE_URL}/${header_name}
        ${download_path}
        STATUS download_status
        TLS_VERIFY ON
    )

    list(GET download_status 0 status_code)
    list(GET download_status 1 status_message)
    if(NOT status_code EQUAL 0)
        file(REMOVE ${download_path})
        message(FATAL_ERROR "Failed to download ${header_name}: ${status_message}")
    endif()

    file(SIZE ${download_path} download_size)
    if(download_size EQUAL 0)
        file(REMOVE ${download_path})
        message(FATAL_ERROR "Downloaded ${header_name} is empty")
    endif()

    file(RENAME ${download_path} ${target_path})
endfunction()

ensure_stb_header(stb_image.h)
ensure_stb_header(stb_image_write.h)
ensure_stb_header(stb_image_resize2.h)

option(SPRAT_ENABLE_NLS "Enable gettext-based translations when available" ON)
option(SPRAT_REQUIRE_GETTEXT "Fail configure if gettext support is requested but unavailable" OFF)

set(SPRAT_GETTEXT_AVAILABLE FALSE)
if(SPRAT_ENABLE_NLS)
    # Prefer Intl package if available
    find_package(Intl QUIET)
    if(Intl_FOUND)
        set(SPRAT_GETTEXT_AVAILABLE TRUE)
        if(TARGET Intl::Intl)
            set(SPRAT_INTL_LIBRARIES Intl::Intl)
        else()
            set(SPRAT_INTL_LIBRARIES ${Intl_LIBRARIES})
            set(SPRAT_INTL_INCLUDE_DIRS ${Intl_INCLUDE_DIRS})
        endif()
        message(STATUS "gettext support enabled (via Intl)")
    else()
        # Fallback to Gettext package (often only finds tools, but sometimes more)
        find_package(Gettext QUIET)
        if(Gettext_FOUND)
            # Some platforms provide libintl through gettext
            if(TARGET Gettext::Gettext)
                set(SPRAT_GETTEXT_AVAILABLE TRUE)
                set(SPRAT_INTL_LIBRARIES Gettext::Gettext)
                message(STATUS "gettext support enabled (via Gettext target)")
            elseif(GETTEXT_LIBRARIES)
                set(SPRAT_GETTEXT_AVAILABLE TRUE)
                set(SPRAT_INTL_LIBRARIES ${GETTEXT_LIBRARIES})
                message(STATUS "gettext support enabled (via GETTEXT_LIBRARIES)")
            endif()
        endif()
    endif()

    if(NOT SPRAT_GETTEXT_AVAILABLE AND SPRAT_REQUIRE_GETTEXT)
        message(FATAL_ERROR "gettext was requested (SPRAT_REQUIRE_GETTEXT=ON) but libintl/gettext was not found")
    elseif(NOT SPRAT_GETTEXT_AVAILABLE)
        message(STATUS "gettext not found; translations will fallback to source strings")
    endif()
endif()

# LibArchive detection
if(TARGET LibArchive::LibArchive)
    set(LIBARCHIVE_LIBRARIES LibArchive::LibArchive)
    set(LIBARCHIVE_FOUND TRUE)
    if(NOT LIBARCHIVE_INCLUDE_DIRS)
        get_target_property(LIBARCHIVE_INCLUDE_DIRS LibArchive::LibArchive INTERFACE_INCLUDE_DIRECTORIES)
    endif()
elseif(WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Windows" OR EMSCRIPTEN)
    find_package(LibArchive QUIET)
    if(LibArchive_FOUND)
        set(LIBARCHIVE_LIBRARIES LibArchive::LibArchive)
        if(NOT LIBARCHIVE_INCLUDE_DIRS)
            find_path(LIBARCHIVE_INCLUDE_DIR NAMES archive.h)
            set(LIBARCHIVE_INCLUDE_DIRS ${LIBARCHIVE_INCLUDE_DIR})
        else()
            set(LIBARCHIVE_INCLUDE_DIR ${LIBARCHIVE_INCLUDE_DIRS})
        endif()
    else()
        # Fallback for manual identification if find_package fails
        find_library(LIBARCHIVE_LIBRARY NAMES archive libarchive)
        find_path(LIBARCHIVE_INCLUDE_DIR NAMES archive.h)
        if(LIBARCHIVE_LIBRARY AND LIBARCHIVE_INCLUDE_DIR)
            set(LIBARCHIVE_LIBRARIES ${LIBARCHIVE_LIBRARY})
            set(LIBARCHIVE_INCLUDE_DIRS ${LIBARCHIVE_INCLUDE_DIR})
        else()
            message(FATAL_ERROR "LibArchive not found. Please provide -DLibArchive_LIBRARY and -DLibArchive_INCLUDE_DIR")
        endif()
    endif()
    
    # Attempt to locate libarchive DLL for copying
    if(NOT SPRAT_STATIC AND LIBARCHIVE_INCLUDE_DIR)
        get_filename_component(LIBARCHIVE_ROOT "${LIBARCHIVE_INCLUDE_DIR}/.." ABSOLUTE)
        find_file(LIBARCHIVE_DLL NAMES archive.dll libarchive.dll
                  HINTS "${LIBARCHIVE_ROOT}/bin" "${LIBARCHIVE_ROOT}" "${LIBARCHIVE_INCLUDE_DIR}"
                  DOC "Path to libarchive DLL")
    endif()
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBARCHIVE REQUIRED libarchive)
endif()

# Zopfli detection
if(PkgConfig_FOUND)
    pkg_check_modules(ZOPFLIPNG zopflipng)
endif()

if(NOT ZOPFLIPNG_FOUND)
    find_library(ZOPFLIPNG_LIBRARY NAMES zopflipng libzopflipng)
    find_path(ZOPFLIPNG_INCLUDE_DIR NAMES zopflipng/zopflipng_lib.h)
    if(ZOPFLIPNG_LIBRARY AND ZOPFLIPNG_INCLUDE_DIR)
        set(ZOPFLIPNG_LIBRARIES ${ZOPFLIPNG_LIBRARY})
        set(ZOPFLIPNG_INCLUDE_DIRS ${ZOPFLIPNG_INCLUDE_DIR})
        set(ZOPFLIPNG_FOUND TRUE)

        # Attempt to locate zopflipng DLL for copying
        if(WIN32 AND NOT SPRAT_STATIC)
            get_filename_component(ZOPFLIPNG_ROOT "${ZOPFLIPNG_INCLUDE_DIR}/.." ABSOLUTE)
            find_file(ZOPFLIPNG_DLL NAMES zopflipng.dll libzopflipng.dll
                      HINTS "${ZOPFLIPNG_ROOT}/bin" "${ZOPFLIPNG_ROOT}" "${ZOPFLIPNG_INCLUDE_DIR}"
                      DOC "Path to zopflipng DLL")
        endif()
    endif()
endif()

# Squish (libsquish) detection for GPU compression
if(PkgConfig_FOUND)
    pkg_check_modules(SQUISH squish)
endif()

if(NOT SQUISH_FOUND)
    find_library(SQUISH_LIBRARY NAMES squish libsquish)
    find_path(SQUISH_INCLUDE_DIR NAMES squish.h)
    if(SQUISH_LIBRARY AND SQUISH_INCLUDE_DIR)
        set(SQUISH_LIBRARIES ${SQUISH_LIBRARY})
        set(SQUISH_INCLUDE_DIRS ${SQUISH_INCLUDE_DIR})
        set(SQUISH_FOUND TRUE)

        # Attempt to locate squish DLL for copying
        if(WIN32 AND NOT SPRAT_STATIC)
            get_filename_component(SQUISH_ROOT "${SQUISH_INCLUDE_DIR}/.." ABSOLUTE)
            find_file(SQUISH_DLL NAMES squish.dll libsquish.dll
                      HINTS "${SQUISH_ROOT}/bin" "${SQUISH_ROOT}" "${SQUISH_INCLUDE_DIR}"
                      DOC "Path to squish DLL")
        endif()
    endif()
endif()

# Jsonnet dependency
include(FetchContent)
FetchContent_Declare(
    jsonnet
    GIT_REPOSITORY https://github.com/google/jsonnet.git
    GIT_TAG f45e01d632b29e4c0757ec7ba188ce759298e6d3  # v0.20.0
    GIT_SUBMODULES ""
)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
# Under Emscripten the SHARED→STATIC patch already makes libjsonnet[++] static,
# so building the _static variants too would produce two targets with the same
# OUTPUT_NAME (both → libjsonnet.a / libjsonnet++.a) and cause a parallel-link
# race condition.  Only enable BUILD_STATIC_LIBS on non-Emscripten platforms.
if(EMSCRIPTEN)
    set(BUILD_STATIC_LIBS OFF CACHE BOOL "" FORCE)
else()
    set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
endif()
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
# Jsonnet's CMakeLists.txt uses cmake_minimum_required < 3.5; suppress the error
set(CMAKE_POLICY_VERSION_MINIMUM "3.5" CACHE INTERNAL "")
FetchContent_GetProperties(jsonnet)
if(NOT jsonnet_POPULATED)
    # Silence FetchContent_Populate() deprecation warning on CMake >= 3.30.
    if(POLICY CMP0169)
        cmake_policy(SET CMP0169 OLD)
    endif()
    FetchContent_Populate(jsonnet)
    # jsonnet hardcodes CMAKE_CXX_STANDARD 11; its headers use nested namespaces
    # (a C++17 feature), causing -Wc++17-extensions warnings on GCC/Clang and
    # build failures on MSVC. Raise the standard to 17 unconditionally.
    file(READ "${jsonnet_SOURCE_DIR}/CMakeLists.txt" _jcml)
    string(REPLACE
        "set(CMAKE_CXX_STANDARD 11)"
        "set(CMAKE_CXX_STANDARD 17)"
        _jcml "${_jcml}")
    # jsonnet's test_suite/CMakeLists.txt unconditionally registers a
    # regression_test via add_test(), even when BUILD_TESTS is OFF.
    # Guard it so CTest doesn't try to run `../jsonnet` (which doesn't exist).
    string(REPLACE
        "add_subdirectory(test_suite)"
        "if(BUILD_TESTS)\n    add_subdirectory(test_suite)\nendif()"
        _jcml "${_jcml}")
    if(MSVC)
        # jsonnet v0.20.0 rejects non-GCC/Clang compilers with a FATAL_ERROR in
        # the else() branch of its compiler-flags check (CMakeLists.txt:43).
        # Downgrade it to STATUS so MSVC can configure and build with defaults.
        string(REGEX REPLACE
            "message[(]FATAL_ERROR \"Compiler [^\"]+not supported\"[)]"
            "message(STATUS \"MSVC detected; using default compiler flags\")"
            _jcml "${_jcml}")
    endif()
    file(WRITE "${jsonnet_SOURCE_DIR}/CMakeLists.txt" "${_jcml}")
    if(MSVC)
        # jsonnet's stdlib/CMakeLists.txt invokes to_c_array via
        # ${GLOBAL_OUTPUT_PATH}/to_c_array, which is wrong for MSVC multi-config
        # generators: those place the executable in a per-config subdirectory
        # (e.g. Release/) that the plain path doesn't include.
        # Replace it with $<TARGET_FILE:to_c_array>, a generator expression that
        # resolves to the correct full path (config subdir + .exe) at build time.
        file(READ "${jsonnet_SOURCE_DIR}/stdlib/CMakeLists.txt" _jstdlib)
        string(REGEX REPLACE
            "\\$\\{GLOBAL_OUTPUT_PATH\\}/to_c_array"
            "$<TARGET_FILE:to_c_array>"
            _jstdlib "${_jstdlib}")
        file(WRITE "${jsonnet_SOURCE_DIR}/stdlib/CMakeLists.txt" "${_jstdlib}")
    endif()
    if(EMSCRIPTEN)
        # c4core's cpu.hpp doesn't recognize WebAssembly; add WASM32/64 support.
        set(_c4cpu "${jsonnet_SOURCE_DIR}/third_party/rapidyaml/rapidyaml/ext/c4core/src/c4/cpu.hpp")
        if(EXISTS "${_c4cpu}")
            file(READ "${_c4cpu}" _c4cpu_content)
            string(REPLACE
[[#elif defined(SWIG)
#else
#   error "unknown CPU architecture"]]
[[#elif defined(SWIG)
#elif defined(__wasm__)
#   if defined(__wasm64__)
#       define C4_CPU_WASM64
#       define C4_WORDSIZE 8
#   else
#       define C4_CPU_WASM32
#       define C4_WORDSIZE 4
#   endif
#   define C4_BYTE_ORDER _C4EL
#else
#   error "unknown CPU architecture"]]
                _c4cpu_content "${_c4cpu_content}")
            file(WRITE "${_c4cpu}" "${_c4cpu_content}")
        endif()
        # fast_float doesn't recognize WebAssembly; Clang/Emscripten supports
        # __uint128_t so treating WASM as a 64-bit platform is correct here.
        set(_ff "${jsonnet_SOURCE_DIR}/third_party/rapidyaml/rapidyaml/ext/c4core/src/c4/ext/fast_float/include/fast_float/float_common.h")
        if(EXISTS "${_ff}")
            file(READ "${_ff}" _ff_content)
            string(REPLACE
                "|| defined(__s390x__)"
                "|| defined(__s390x__) || defined(__wasm__)"
                _ff_content "${_ff_content}")
            file(WRITE "${_ff}" "${_ff_content}")
        endif()
        # libjsonnet and libjsonnet++ are hardcoded as SHARED in their own
        # CMakeLists.txt files; under Emscripten a SHARED build produces a full
        # WASM bundle that wasm-ld cannot consume as a link input, so force STATIC.
        foreach(_jlib
            "${jsonnet_SOURCE_DIR}/core/CMakeLists.txt"
            "${jsonnet_SOURCE_DIR}/cpp/CMakeLists.txt")
            file(READ "${_jlib}" _jlib_content)
            string(REPLACE "SHARED" "STATIC" _jlib_content "${_jlib_content}")
            file(WRITE "${_jlib}" "${_jlib_content}")
        endforeach()
        # to_c_array is compiled by Emscripten to .js and cannot do real file I/O
        # in Node (Emscripten uses a virtual filesystem). Replace the COMMAND with
        # a Python script that has the same positional-argument interface. Python
        # is a hard dependency of Emscripten so it is always present.
        find_program(_PYTHON3 NAMES python3 python REQUIRED)
        set(_gen_py "${jsonnet_BINARY_DIR}/gen_std_h.py")
        file(WRITE "${_gen_py}" [[
import sys
data = open(sys.argv[1], 'rb').read()
with open(sys.argv[2], 'w') as f:
    f.write(','.join(str(b) for b in data) + ',0')
]])
        file(READ "${jsonnet_SOURCE_DIR}/stdlib/CMakeLists.txt" _jstdlib)
        string(REPLACE
            [[${GLOBAL_OUTPUT_PATH}/to_c_array]]
            "\"${_PYTHON3}\" \"${_gen_py}\""
            _jstdlib "${_jstdlib}")
        file(WRITE "${jsonnet_SOURCE_DIR}/stdlib/CMakeLists.txt" "${_jstdlib}")
    endif()
    add_subdirectory("${jsonnet_SOURCE_DIR}" "${jsonnet_BINARY_DIR}")
endif()

# jsonnet v0.20.0 builds libjsonnet++ (shared) without declaring its dependency
# on libjsonnet, causing undefined-symbol errors on macOS where dylibs must have
# all symbols resolved at link time. Patch the target here when it is shared.
if(TARGET libjsonnet++ AND TARGET libjsonnet)
    get_target_property(_ljnpp_type libjsonnet++ TYPE)
    if(_ljnpp_type STREQUAL "SHARED_LIBRARY")
        target_link_libraries(libjsonnet++ PRIVATE libjsonnet)
    endif()
endif()

# Suppress common MSVC warnings in jsonnet targets (written for GCC/Clang).
if(MSVC)
    foreach(_jt libjsonnet_static libjsonnet++_static)
        if(TARGET ${_jt})
            target_compile_options(${_jt} PRIVATE
                /wd4100   # unreferenced formal parameter
                /wd4127   # conditional expression is constant
                /wd4244   # conversion, possible loss of data
                /wd4267   # conversion from size_t
                /wd4702   # unreachable code
                /wd4706   # assignment within conditional expression
                /wd4996   # deprecated CRT function
            )
            target_compile_definitions(${_jt} PRIVATE _CRT_SECURE_NO_WARNINGS)
        endif()
    endforeach()
endif()

# Target definitions
add_library(spratcore STATIC
    src/core/cli_parse.cpp
    src/core/layout_parser.cpp
    src/core/output_pattern.cpp
    src/core/i18n.cpp
    src/core/stb_impl.cpp
    src/commands/spratlayout_command.cpp
    src/commands/spratpack_command.cpp
    src/commands/spratconvert_command.cpp
    src/commands/spratframes_command.cpp
    src/commands/spratunpack_command.cpp
)

target_include_directories(spratcore PUBLIC src)
target_include_directories(spratcore SYSTEM PRIVATE ${STB_DIR})
target_include_directories(spratcore PRIVATE ${LIBARCHIVE_INCLUDE_DIRS})
target_link_libraries(spratcore PRIVATE ${LIBARCHIVE_LIBRARIES})
# Under Emscripten BUILD_STATIC_LIBS is OFF (see above), so the _static alias
# targets are not defined; use the main (already-STATIC) targets instead.
if(EMSCRIPTEN)
    target_link_libraries(spratcore PRIVATE libjsonnet++ libjsonnet)
else()
    target_link_libraries(spratcore PRIVATE libjsonnet++_static libjsonnet_static)
endif()
if(SPRAT_GETTEXT_AVAILABLE)
    target_compile_definitions(spratcore PUBLIC SPRAT_ENABLE_GETTEXT)
    target_link_libraries(spratcore PUBLIC ${SPRAT_INTL_LIBRARIES})
    if(SPRAT_INTL_INCLUDE_DIRS)
        target_include_directories(spratcore PUBLIC ${SPRAT_INTL_INCLUDE_DIRS})
    endif()
endif()
target_compile_definitions(spratcore PUBLIC SPRAT_LOCALE_DIR="${CMAKE_INSTALL_FULL_LOCALEDIR}")

add_executable(spratlayout src/spratlayout.cpp)
target_link_libraries(spratlayout PRIVATE spratcore ${LIBARCHIVE_LIBRARIES})
target_include_directories(spratlayout PRIVATE ${LIBARCHIVE_INCLUDE_DIRS})
target_include_directories(spratlayout SYSTEM PRIVATE ${STB_DIR})

add_executable(spratpack src/spratpack.cpp)
target_link_libraries(spratpack PRIVATE spratcore ${LIBARCHIVE_LIBRARIES})
if(ZOPFLIPNG_FOUND)
    target_link_libraries(spratpack PRIVATE ${ZOPFLIPNG_LIBRARIES})
    target_include_directories(spratpack PRIVATE ${ZOPFLIPNG_INCLUDE_DIRS})
    target_compile_definitions(spratpack PRIVATE SPRAT_HAS_ZOPFLI)
endif()
if(SQUISH_FOUND)
    target_link_libraries(spratpack PRIVATE ${SQUISH_LIBRARIES})
    target_include_directories(spratpack PRIVATE ${SQUISH_INCLUDE_DIRS})
    target_compile_definitions(spratpack PRIVATE SPRAT_HAS_SQUISH)
endif()
target_include_directories(spratpack PRIVATE ${LIBARCHIVE_INCLUDE_DIRS})
target_include_directories(spratpack SYSTEM PRIVATE ${STB_DIR})

add_executable(spratconvert src/spratconvert.cpp)
target_link_libraries(spratconvert PRIVATE spratcore)
target_include_directories(spratconvert SYSTEM PRIVATE ${STB_DIR})

add_executable(spratframes src/spratframes.cpp)
target_link_libraries(spratframes PRIVATE spratcore)
target_include_directories(spratframes SYSTEM PRIVATE ${STB_DIR})

add_executable(spratunpack src/spratunpack.cpp)
target_link_libraries(spratunpack PRIVATE spratcore ${LIBARCHIVE_LIBRARIES})
target_include_directories(spratunpack PRIVATE ${LIBARCHIVE_INCLUDE_DIRS})
target_include_directories(spratunpack SYSTEM PRIVATE ${STB_DIR})

target_compile_definitions(spratconvert PRIVATE SPRAT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(spratlayout PRIVATE
    SPRAT_GLOBAL_PROFILE_CONFIG="${CMAKE_INSTALL_FULL_DATADIR}/sprat/spratprofiles.cfg")

# Keep a default profile config beside built binaries (e.g. build-win/) so
# spratlayout can resolve it via exe-directory lookup without extra setup.
# Use a single custom target for the transforms copy to prevent parallel copy races
# when multiple executables complete simultaneously in a parallel build.
# NOTE: Use a static path instead of $<TARGET_FILE_DIR:spratlayout> to avoid a
# cyclic dependency (sprat_copy_transforms -> spratlayout -> sprat_copy_transforms).
# On multi-config generators (MSVC, Xcode) binaries land in a per-config subdir
# (e.g. build/Release/), so append /$<CONFIG> to match $<TARGET_FILE_DIR:...>.
if(CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    set(_sprat_bin_dir ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
else()
    set(_sprat_bin_dir ${CMAKE_CURRENT_BINARY_DIR})
endif()
get_property(_sprat_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(_sprat_multi_config)
    string(APPEND _sprat_bin_dir "/$<CONFIG>")
endif()
add_custom_target(sprat_copy_transforms ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/transforms
        ${_sprat_bin_dir}/transforms
    COMMENT "Copying transforms/ to ${_sprat_bin_dir}"
    VERBATIM)

foreach(target spratlayout spratpack spratconvert spratframes spratunpack)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/spratprofiles.cfg
            $<TARGET_FILE_DIR:${target}>/spratprofiles.cfg
        COMMENT "Copying spratprofiles.cfg to $<TARGET_FILE_DIR:${target}>"
        VERBATIM)
    add_dependencies(${target} sprat_copy_transforms)

    if(WIN32)
        if(SPRAT_STATIC)
            if(target MATCHES "spratlayout|spratpack|spratunpack")
                target_compile_definitions(${target} PRIVATE LIBARCHIVE_STATIC)
            endif()
        else()
            if(LIBARCHIVE_DLL AND (target MATCHES "spratlayout|spratpack|spratunpack"))
                add_custom_command(TARGET ${target} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${LIBARCHIVE_DLL}"
                        $<TARGET_FILE_DIR:${target}>/
                    COMMENT "Copying libarchive DLL to $<TARGET_FILE_DIR:${target}>"
                    VERBATIM)
            endif()
            if(ZOPFLIPNG_DLL AND (target STREQUAL "spratpack"))
                add_custom_command(TARGET ${target} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${ZOPFLIPNG_DLL}"
                        $<TARGET_FILE_DIR:${target}>/
                    COMMENT "Copying zopflipng DLL to $<TARGET_FILE_DIR:${target}>"
                    VERBATIM)
            endif()
            if(SQUISH_DLL AND (target STREQUAL "spratpack"))
                add_custom_command(TARGET ${target} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${SQUISH_DLL}"
                        $<TARGET_FILE_DIR:${target}>/
                    COMMENT "Copying squish DLL to $<TARGET_FILE_DIR:${target}>"
                    VERBATIM)
            endif()
        endif()
    endif()
endforeach()

install(TARGETS spratlayout spratpack spratconvert spratframes spratunpack
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES spratprofiles.cfg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/sprat)
install(CODE "
    # Remove any stale compiled .transform files left by older installations;
    # the current format is .jsonnet, which carries richer metadata (icons, etc.).
    file(GLOB _old_transforms
        \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/sprat/transforms/*.transform\")
    foreach(_f IN LISTS _old_transforms)
        message(STATUS \"Removing obsolete transform file: \${_f}\")
        file(REMOVE \"\${_f}\")
    endforeach()
")
install(DIRECTORY transforms/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/sprat/transforms)
install(FILES man/sprat-cli.1
    DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)

if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
