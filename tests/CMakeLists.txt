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
    NAME edge_cases
    COMMAND ${BASH_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/edge_cases_test.sh
            $<TARGET_FILE:spratlayout>
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
