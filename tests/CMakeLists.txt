find_program(BASH_EXE NAMES bash)
if(NOT BASH_EXE)
    set(BASH_EXE "sh")
endif()

add_executable(core_test core_test.cpp)
target_link_libraries(core_test PRIVATE spratcore)
target_include_directories(core_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)

add_executable(layout_test layout_test.cpp)
target_link_libraries(layout_test PRIVATE spratcore)
target_include_directories(layout_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)

add_test(NAME core_test COMMAND core_test)
add_test(NAME layout_test COMMAND layout_test)

add_test(
    NAME pipeline
    COMMAND ${BASH_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/pipeline_test.sh
            $<TARGET_FILE:spratlayout>
            $<TARGET_FILE:spratpack>
)

add_test(
    NAME convert
    COMMAND ${BASH_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/convert_test.sh
            $<TARGET_FILE:spratconvert>
)

add_test(
    NAME collision_atlas
    COMMAND ${BASH_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/collision_atlas_test.sh
            $<TARGET_FILE:spratconvert>
)

add_test(
    NAME perceptual_dedup
    COMMAND ${BASH_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/perceptual_dedup_test.sh
            $<TARGET_FILE:spratlayout>
)

add_test(
    NAME extrude
    COMMAND ${BASH_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/extrude_test.sh
            $<TARGET_FILE:spratlayout>
            $<TARGET_FILE:spratpack>
)

add_test(
    NAME output_scale
    COMMAND ${BASH_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/output_scale_test.sh
            $<TARGET_FILE:spratpack>
)

add_test(
    NAME assets_regression
    COMMAND ${BASH_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/assets_regression_test.sh
            $<TARGET_FILE:spratlayout>
            $<TARGET_FILE:spratpack>
            $<TARGET_FILE:spratframes>
)

add_test(
    NAME multipack
    COMMAND ${BASH_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/multipack_test.sh
            $<TARGET_FILE:spratlayout>
            $<TARGET_FILE:spratpack>
)

add_test(
    NAME profile_resolution
    COMMAND ${BASH_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/profile_resolution_test.sh
            $<TARGET_FILE:spratlayout>
)

add_test(
    NAME profile_keys
    COMMAND ${BASH_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/profile_keys_test.sh
            $<TARGET_FILE:spratlayout>
)

add_test(
    NAME edge_cases
    COMMAND ${BASH_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/edge_cases_test.sh
            $<TARGET_FILE:spratlayout>
            $<TARGET_FILE:spratconvert>
)

add_test(
    NAME output_pattern
    COMMAND ${BASH_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/output_pattern_test.sh
            $<TARGET_FILE:spratpack>
            $<TARGET_FILE:spratconvert>
)

add_test(
    NAME unpack_error
    COMMAND ${BASH_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/unpack_error_test.sh
            $<TARGET_FILE:spratunpack>
)

add_test(
    NAME spratunpack_stdin
    COMMAND ${BASH_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/spratunpack_stdin_test.sh
            $<TARGET_FILE:spratunpack>
            $<TARGET_FILE:spratframes>
)

add_test(
    NAME spratunpack_json
    COMMAND ${BASH_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/spratunpack_json_test.sh
            $<TARGET_FILE:spratunpack>
)

add_test(
    NAME spratpack_args
    COMMAND ${BASH_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/spratpack_args_test.sh
            $<TARGET_FILE:spratpack>
)

add_test(
    NAME codec_output
    COMMAND ${BASH_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/codec_output_test.sh
            $<TARGET_FILE:spratpack>
)

add_test(
    NAME layout_pack_regression
    COMMAND ${BASH_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/layout_pack_regression_test.sh
            $<TARGET_FILE:spratlayout>
            $<TARGET_FILE:spratpack>
            $<TARGET_FILE:spratconvert>
)

add_test(
    NAME spratframes
    COMMAND ${BASH_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/spratframes_test.sh
            $<TARGET_FILE:spratframes>
)

add_test(
    NAME unpack_roundtrip
    COMMAND ${BASH_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/unpack_roundtrip_test.sh
            $<TARGET_FILE:spratpack>
            $<TARGET_FILE:spratconvert>
            $<TARGET_FILE:spratunpack>
)

add_test(
    NAME rotation_direction
    COMMAND ${BASH_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/rotation_direction_test.sh
            $<TARGET_FILE:spratpack>
            $<TARGET_FILE:spratunpack>
)

add_test(
    NAME cache_keys
    COMMAND ${BASH_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/cache_keys_test.sh
            $<TARGET_FILE:spratlayout>
)

add_test(
    NAME spratlayout_archive
    COMMAND ${BASH_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/spratlayout_archive_test.sh
            $<TARGET_FILE:spratlayout>
)

add_test(
    NAME unity
    COMMAND ${BASH_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/unity_test.sh
            $<TARGET_FILE:spratconvert>
)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/sort_behavior_test.sh")
    add_test(
        NAME sort_behavior
        COMMAND ${BASH_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/sort_behavior_test.sh
                $<TARGET_FILE:spratlayout>
    )
else()
    message(WARNING "Skipping sort_behavior test: tests/sort_behavior_test.sh not found")
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/layout_limits_and_multipack_optimize_test.sh")
    add_test(
        NAME layout_limits_and_multipack_optimize
        COMMAND ${BASH_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/layout_limits_and_multipack_optimize_test.sh
                $<TARGET_FILE:spratlayout>
    )
else()
    message(WARNING "Skipping layout_limits_and_multipack_optimize test: script not found")
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/compact_seed_search_regression_test.sh")
    add_test(
        NAME compact_seed_search_regression
        COMMAND ${BASH_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/compact_seed_search_regression_test.sh
                $<TARGET_FILE:spratlayout>
    )
else()
    message(WARNING "Skipping compact_seed_search_regression test: script not found")
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/recursive_dir_test.sh")
    add_test(
        NAME recursive_dir
        COMMAND ${BASH_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/recursive_dir_test.sh
                $<TARGET_FILE:spratlayout>
    )
else()
    message(WARNING "Skipping recursive_dir test: script not found")
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/incremental_test.sh")
    add_test(
        NAME incremental
        COMMAND ${BASH_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/incremental_test.sh
                $<TARGET_FILE:spratlayout>
    )
else()
    message(WARNING "Skipping incremental test: tests/incremental_test.sh not found")
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/spratlayout_exclude_test.sh")
    add_test(
        NAME spratlayout_exclude
        COMMAND ${BASH_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/spratlayout_exclude_test.sh
                $<TARGET_FILE:spratlayout>
    )
else()
    message(WARNING "Skipping spratlayout_exclude test: script not found")
endif()

add_test(
    NAME reused_layout_bounds
    COMMAND ${BASH_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/reused_layout_bounds_test.sh
            $<TARGET_FILE:spratlayout>
)

add_test(
    NAME locale_numeric
    COMMAND ${BASH_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/locale_numeric_test.sh
            $<TARGET_FILE:spratlayout>
            $<TARGET_FILE:spratpack>
            $<TARGET_FILE:spratconvert>
)

add_test(
    NAME spratlayout_unreadable_dir
    COMMAND ${BASH_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/spratlayout_unreadable_dir_test.sh
            $<TARGET_FILE:spratlayout>
)

# These scripts exit 77 where the platform can't set up their scenario.
set_tests_properties(locale_numeric spratlayout_unreadable_dir PROPERTIES SKIP_RETURN_CODE 77)

add_test(
    NAME write_failure
    COMMAND ${BASH_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/write_failure_test.sh
            $<TARGET_FILE:spratlayout>
            $<TARGET_FILE:spratpack>
            $<TARGET_FILE:spratconvert>
            $<TARGET_FILE:spratunpack>
            $<TARGET_FILE:spratframes>
)
set_tests_properties(write_failure PROPERTIES SKIP_RETURN_CODE 77)

add_test(
    NAME folder_order
    COMMAND ${BASH_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/folder_order_test.sh
            $<TARGET_FILE:spratlayout>
)

# spratlayout caches layouts in the temp directory, keyed by VERSION only, so a test
# could reuse a layout cached by an earlier build. Every test gets its own temp
# directory, emptied before each ctest run by the reset_test_cache fixture.
set(SPRAT_TEST_CACHE_ROOT "${CMAKE_CURRENT_BINARY_DIR}/test-cache")
get_property(_sprat_tests DIRECTORY PROPERTY TESTS)
string(REPLACE ";" "|" _sprat_test_names "${_sprat_tests}")
add_test(
    NAME reset_test_cache
    COMMAND ${CMAKE_COMMAND} -DROOT=${SPRAT_TEST_CACHE_ROOT} -DNAMES=${_sprat_test_names}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/reset_test_cache.cmake
)
set_tests_properties(reset_test_cache PROPERTIES FIXTURES_SETUP sprat_test_cache)
foreach(_test IN LISTS _sprat_tests)
    set(_dir "${SPRAT_TEST_CACHE_ROOT}/${_test}")
    set_tests_properties(${_test} PROPERTIES
        FIXTURES_REQUIRED sprat_test_cache
        ENVIRONMENT "TMPDIR=${_dir};TMP=${_dir};TEMP=${_dir}")
endforeach()
