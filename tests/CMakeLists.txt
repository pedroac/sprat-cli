find_program(BASH_EXE NAMES bash)
if(NOT BASH_EXE)
    set(BASH_EXE "sh")
endif()

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
    NAME profile_resolution
    COMMAND ${BASH_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/profile_resolution_test.sh
            $<TARGET_FILE:spratlayout>
)
