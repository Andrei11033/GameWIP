if(NOT DEFINED GAMEWIP_EXECUTABLE)
    message(FATAL_ERROR "GAMEWIP_EXECUTABLE was not provided.")
endif()

if(NOT DEFINED GAMEWIP_OUTPUT_DIR)
    message(FATAL_ERROR "GAMEWIP_OUTPUT_DIR was not provided.")
endif()

# Add the compiler runtime folder so CMake finds the matching MSYS2 DLLs.
set(runtime_search_dirs)
if(DEFINED GAMEWIP_RUNTIME_SEARCH_DIR AND NOT GAMEWIP_RUNTIME_SEARCH_DIR STREQUAL "")
    list(APPEND runtime_search_dirs DIRECTORIES "${GAMEWIP_RUNTIME_SEARCH_DIR}")
endif()

# Scan the executable and resolve the DLLs it needs at runtime.
if(POLICY CMP0207)
    cmake_policy(PUSH)
    cmake_policy(SET CMP0207 NEW)
endif()

file(GET_RUNTIME_DEPENDENCIES
    RESOLVED_DEPENDENCIES_VAR resolved_dependencies
    UNRESOLVED_DEPENDENCIES_VAR unresolved_dependencies
    EXECUTABLES "${GAMEWIP_EXECUTABLE}"
    ${runtime_search_dirs}
    PRE_EXCLUDE_REGEXES
        "^api-ms-.*"
        "^ext-ms-.*"
    POST_EXCLUDE_REGEXES
        ".*[/\\][Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\].*"
        ".*[/\\][Ss]ystem32[/\\].*"
)

if(POLICY CMP0207)
    cmake_policy(POP)
endif()

# Copy every resolved runtime DLL beside GameWIP.exe.
foreach(dependency IN LISTS resolved_dependencies)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${dependency}"
            "${GAMEWIP_OUTPUT_DIR}"
        RESULT_VARIABLE copy_result
    )

    if(NOT copy_result EQUAL 0)
        message(FATAL_ERROR "Failed to copy runtime dependency: ${dependency}")
    endif()
endforeach()

if(unresolved_dependencies)
    message(WARNING "Unresolved runtime dependencies: ${unresolved_dependencies}")
endif()
