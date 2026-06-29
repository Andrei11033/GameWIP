if(NOT DEFINED GAMEWIP_EXECUTABLE)
    message(FATAL_ERROR "GAMEWIP_EXECUTABLE was not provided.")
endif()

if(NOT DEFINED GAMEWIP_OUTPUT_DIR)
    message(FATAL_ERROR "GAMEWIP_OUTPUT_DIR was not provided.")
endif()

file(MAKE_DIRECTORY "${GAMEWIP_OUTPUT_DIR}")
file(LOCK
    "${GAMEWIP_OUTPUT_DIR}/.gamewip_runtime_dependencies.lock"
    GUARD PROCESS
    TIMEOUT 120
)

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
    CONFLICTING_DEPENDENCIES_PREFIX conflicting_dependencies
    EXECUTABLES "${GAMEWIP_EXECUTABLE}"
    ${runtime_search_dirs}
    PRE_EXCLUDE_REGEXES
        "^api-ms-.*"
        "^ext-ms-.*"
    POST_EXCLUDE_REGEXES
        ".*[/\\][Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\].*"
        ".*[/\\][Ss]ystem32[/\\].*"
)

# Multiple executables share one output folder. A previous target may already
# have copied a runtime DLL there, so prefer the matching compiler-directory
# candidate when CMake reports both paths as a conflict.
foreach(filename IN LISTS conflicting_dependencies_FILENAMES)
    set(conflict_variable "conflicting_dependencies_${filename}")
    set(preferred_dependency "")

    foreach(candidate IN LISTS ${conflict_variable})
        file(TO_CMAKE_PATH "${candidate}" candidate_path)
        file(TO_CMAKE_PATH "${GAMEWIP_RUNTIME_SEARCH_DIR}" runtime_search_path)
        string(FIND "${candidate_path}" "${runtime_search_path}/" runtime_path_position)
        if(runtime_path_position EQUAL 0)
            set(preferred_dependency "${candidate}")
            break()
        endif()
    endforeach()

    if(NOT preferred_dependency)
        list(GET ${conflict_variable} 0 preferred_dependency)
    endif()
    list(APPEND resolved_dependencies "${preferred_dependency}")
endforeach()

list(REMOVE_DUPLICATES resolved_dependencies)

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
