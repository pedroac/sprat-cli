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

# Binaries
find_package(LibArchive)
if(LibArchive_FOUND)
    set(LIBARCHIVE_LIBRARIES LibArchive::LibArchive)
    set(LIBARCHIVE_INCLUDE_DIRS "")
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBARCHIVE REQUIRED libarchive)
endif()

add_executable(spratlayout src/spratlayout.cpp)
target_link_libraries(spratlayout ${LIBARCHIVE_LIBRARIES})
target_include_directories(spratlayout PRIVATE ${LIBARCHIVE_INCLUDE_DIRS})
target_include_directories(spratlayout SYSTEM PRIVATE ${STB_DIR})

add_executable(spratpack src/spratpack.cpp)
target_include_directories(spratpack SYSTEM PRIVATE ${STB_DIR})

add_executable(spratconvert src/spratconvert.cpp)
target_include_directories(spratconvert SYSTEM PRIVATE ${STB_DIR})

add_executable(spratframes src/spratframes.cpp)
target_include_directories(spratframes SYSTEM PRIVATE ${STB_DIR})

add_executable(spratunpack src/spratunpack.cpp)
target_link_libraries(spratunpack ${LIBARCHIVE_LIBRARIES})
target_include_directories(spratunpack PRIVATE ${LIBARCHIVE_INCLUDE_DIRS})
target_include_directories(spratunpack SYSTEM PRIVATE ${STB_DIR})
target_compile_definitions(spratconvert PRIVATE SPRAT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(spratlayout PRIVATE
    SPRAT_GLOBAL_PROFILE_CONFIG="${CMAKE_INSTALL_FULL_DATADIR}/sprat/spratprofiles.cfg")

install(TARGETS spratlayout spratpack spratconvert
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES spratprofiles.cfg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/sprat)
install(FILES man/sprat-cli.1
    DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)

enable_testing()
add_subdirectory(tests)
