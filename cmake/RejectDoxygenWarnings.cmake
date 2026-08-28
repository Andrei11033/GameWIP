# Rejects missing or non-empty Doxygen warning logs after documentation generation.

if(NOT DEFINED WARNING_LOG)
    message(FATAL_ERROR "WARNING_LOG is required for Doxygen warning validation.")
endif()
if(NOT EXISTS "${WARNING_LOG}")
    message(FATAL_ERROR "Doxygen did not create its warning log: ${WARNING_LOG}")
endif()

file(SIZE "${WARNING_LOG}" warning_log_size)
if(NOT warning_log_size EQUAL 0)
    file(READ "${WARNING_LOG}" warning_log_contents)
    message(FATAL_ERROR "Doxygen emitted warnings:\n${warning_log_contents}")
endif()
