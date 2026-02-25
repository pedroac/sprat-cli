find_program(SH_EXE NAMES sh bash)

add_test(
    NAME pipeline
    COMMAND ${SH_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/pipeline_test.sh
            $<TARGET_FILE:spratlayout>
            $<TARGET_FILE:spratpack>
)

add_test(
    NAME convert
    COMMAND ${SH_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/convert_test.sh
            $<TARGET_FILE:spratconvert>
)

