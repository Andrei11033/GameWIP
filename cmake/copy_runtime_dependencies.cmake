if(NOT DEFINED GAMEWIP_EXECUTABLE)
    message(FATAL_ERROR "GAMEWIP_EXECUTABLE was not provided.")
endif()

if(NOT DEFINED GAMEWIP_OUTPUT_DIR)
    message(FATAL_ERROR "GAMEWIP_OUTPUT_DIR was not provided.")
endif()

file(MAKE_DIRECTORY "${GAMEWIP_OUTPUT_DIR}")
file(LOCK "${GAMEWIP_OUTPUT_DIR}/.gamewip_runtime_dependencies.lock" GUARD PROCESS TIMEOUT 120)

# CMake's Windows dependency resolver checks the depending binary's directory
# before its explicit search directories. Scanning an executable in-place can
# therefore select an old app-local compiler runtime before the current MSYS2
# runtime. Scan a clean shadow copy instead. Project DLLs are copied into the
# shadow directory, while any DLL that also exists in the compiler runtime
# directory is deliberately omitted so the compiler copy wins.
set(runtime_scan_executable "${GAMEWIP_EXECUTABLE}")
set(runtime_scan_dir "")
if(WIN32)
    set(runtime_scan_dir "${GAMEWIP_OUTPUT_DIR}/.gamewip_runtime_dependency_scan")
    file(REMOVE_RECURSE "${runtime_scan_dir}")
    file(MAKE_DIRECTORY "${runtime_scan_dir}")

    get_filename_component(executable_name "${GAMEWIP_EXECUTABLE}" NAME)
    set(runtime_scan_executable "${runtime_scan_dir}/${executable_name}")
    file(COPY_FILE "${GAMEWIP_EXECUTABLE}" "${runtime_scan_executable}" ONLY_IF_DIFFERENT)

    file(GLOB app_local_dlls LIST_DIRECTORIES FALSE "${GAMEWIP_OUTPUT_DIR}/*.dll")
    foreach(app_local_dll IN LISTS app_local_dlls)
        get_filename_component(dll_name "${app_local_dll}" NAME)
        set(compiler_runtime_candidate "")
        if(DEFINED GAMEWIP_RUNTIME_SEARCH_DIR AND NOT GAMEWIP_RUNTIME_SEARCH_DIR STREQUAL "")
            set(compiler_runtime_candidate "${GAMEWIP_RUNTIME_SEARCH_DIR}/${dll_name}")
        endif()

        if(compiler_runtime_candidate AND EXISTS "${compiler_runtime_candidate}")
            continue()
        endif()

        file(COPY_FILE "${app_local_dll}" "${runtime_scan_dir}/${dll_name}" ONLY_IF_DIFFERENT)
    endforeach()
endif()

# Add the compiler runtime folder first so omitted app-local runtimes resolve to
# the compiler that produced the executable.
set(runtime_search_dirs)
if(DEFINED GAMEWIP_RUNTIME_SEARCH_DIR AND NOT GAMEWIP_RUNTIME_SEARCH_DIR STREQUAL "")
    list(APPEND runtime_search_dirs DIRECTORIES "${GAMEWIP_RUNTIME_SEARCH_DIR}")
endif()

if(DEFINED GAMEWIP_OBJDUMP AND NOT GAMEWIP_OBJDUMP STREQUAL "")
    set(CMAKE_GET_RUNTIME_DEPENDENCIES_TOOL "objdump")
    set(CMAKE_GET_RUNTIME_DEPENDENCIES_COMMAND "${GAMEWIP_OBJDUMP}")
endif()

# Scan the executable and resolve the DLLs it needs at runtime.
if(POLICY CMP0207)
    cmake_policy(PUSH)
    cmake_policy(SET CMP0207 NEW)
endif()

file(
    GET_RUNTIME_DEPENDENCIES
    RESOLVED_DEPENDENCIES_VAR resolved_dependencies
    UNRESOLVED_DEPENDENCIES_VAR unresolved_dependencies
    CONFLICTING_DEPENDENCIES_PREFIX conflicting_dependencies
    EXECUTABLES "${runtime_scan_executable}" ${runtime_search_dirs}
    PRE_EXCLUDE_REGEXES "^api-ms-.*" "^ext-ms-.*"
    POST_EXCLUDE_REGEXES ".*[/\\][Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\].*" ".*[/\\][Ss]ystem32[/\\].*"
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
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${dependency}" "${GAMEWIP_OUTPUT_DIR}"
        RESULT_VARIABLE copy_result
    )

    if(NOT copy_result EQUAL 0)
        message(FATAL_ERROR "Failed to copy runtime dependency: ${dependency}")
    endif()
endforeach()

if(runtime_scan_dir)
    file(REMOVE_RECURSE "${runtime_scan_dir}")
endif()

if(unresolved_dependencies)
    if(GAMEWIP_FAIL_ON_UNRESOLVED_DEPENDENCIES)
        message(FATAL_ERROR "Unresolved runtime dependencies: ${unresolved_dependencies}")
    else()
        message(WARNING "Unresolved runtime dependencies: ${unresolved_dependencies}")
    endif()
endif()
