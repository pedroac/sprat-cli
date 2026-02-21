add_test(
    NAME pipeline
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/pipeline_test.sh
            $<TARGET_FILE:spratlayout>
            $<TARGET_FILE:spratpack>
)

add_test(
    NAME convert
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/convert_test.sh
            $<TARGET_FILE:spratconvert>
)

