# Rejects missing or non-empty unexpected Doxygen warning logs after documentation generation.

if(NOT DEFINED WARNING_LOG)
    message(FATAL_ERROR "WARNING_LOG is required for Doxygen warning validation.")
endif()
if(NOT EXISTS "${WARNING_LOG}")
    message(FATAL_ERROR "Doxygen did not create its warning log: ${WARNING_LOG}")
endif()

file(STRINGS "${WARNING_LOG}" warning_log_lines)
set(unexpected_warning_log "")
foreach(warning_line IN LISTS warning_log_lines)
    # Approved equality and bitmask operators (|, |=, &, &=) stay visible in the
    # generated reference without source prose; do not turn that intentional
    # policy into a build failure. Keep this list explicit so new operators still
    # require a documentation decision.
    if(warning_line MATCHES "warning: Member operator(==|\\|=|\\||&=|&)\\([^)]*\\).* is not documented\\.")
        continue()
    endif()
    string(APPEND unexpected_warning_log "${warning_line}\n")
endforeach()

file(WRITE "${WARNING_LOG}" "${unexpected_warning_log}")
if(unexpected_warning_log)
    message(FATAL_ERROR "Doxygen emitted unexpected warnings:\n${unexpected_warning_log}")
endif()
