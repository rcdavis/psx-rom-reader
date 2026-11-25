execute_process(
    COMMAND "${GUI_EXECUTABLE}" one two
    RESULT_VARIABLE result
    ERROR_VARIABLE error
)
if(result EQUAL 0 OR NOT error MATCHES "usage: .+ \\[image\\]")
    message(FATAL_ERROR "Extra arguments should fail with usage; got ${result}: ${error}")
endif()

execute_process(
    COMMAND "${GUI_EXECUTABLE}" "${MISSING_IMAGE}"
    RESULT_VARIABLE result
    ERROR_VARIABLE error
)
if(result EQUAL 0 OR NOT error MATCHES "Failed to open image .*I/O error")
    message(FATAL_ERROR "Missing image should report an I/O error; got ${result}: ${error}")
endif()
