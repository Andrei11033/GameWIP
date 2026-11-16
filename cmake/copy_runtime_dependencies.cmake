# Stages the resolved runtime dependency closure beside one built executable.

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
# therefore select an old app-local compiler runtime before the active
# toolchain. Scan a clean shadow copy instead. Project DLLs are copied into the
# shadow directory, while DLLs provided by the active toolchain are omitted.
set(runtime_scan_executable "${GAMEWIP_EXECUTABLE}")
set(runtime_scan_dir "")
set(runtime_search_paths ${GAMEWIP_RUNTIME_SEARCH_DIRS})

if(DEFINED GAMEWIP_RUNTIME_SEARCH_DIR AND NOT GAMEWIP_RUNTIME_SEARCH_DIR STREQUAL "")
    list(APPEND runtime_search_paths "${GAMEWIP_RUNTIME_SEARCH_DIR}")
endif()

list(REMOVE_DUPLICATES runtime_search_paths)

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
        set(runtime_candidate_exists OFF)

        foreach(runtime_search_path IN LISTS runtime_search_paths)
            if(EXISTS "${runtime_search_path}/${dll_name}")
                set(runtime_candidate_exists ON)
                break()
            endif()
        endforeach()

        if(runtime_candidate_exists)
            continue()
        endif()

        file(COPY_FILE "${app_local_dll}" "${runtime_scan_dir}/${dll_name}" ONLY_IF_DIFFERENT)
    endforeach()
endif()

# Add toolchain runtime folders first so omitted app-local runtimes resolve to
# the toolchain that produced the executable.
set(runtime_search_dirs)
if(runtime_search_paths)
    list(APPEND runtime_search_dirs DIRECTORIES ${runtime_search_paths})
endif()

if(DEFINED GAMEWIP_OBJDUMP AND NOT GAMEWIP_OBJDUMP STREQUAL "")
    set(CMAKE_GET_RUNTIME_DEPENDENCIES_TOOL "objdump")
    set(CMAKE_GET_RUNTIME_DEPENDENCIES_COMMAND "${GAMEWIP_OBJDUMP}")
endif()

if(POLICY CMP0207)
    cmake_policy(PUSH)
    cmake_policy(SET CMP0207 NEW)
endif()

# Resolve runtime DLLs used by the executable.
file(
    GET_RUNTIME_DEPENDENCIES
    RESOLVED_DEPENDENCIES_VAR resolved_dependencies
    UNRESOLVED_DEPENDENCIES_VAR unresolved_dependencies
    CONFLICTING_DEPENDENCIES_PREFIX conflicting_dependencies
    EXECUTABLES "${runtime_scan_executable}" ${runtime_search_dirs}
    PRE_EXCLUDE_REGEXES "^api-ms-.*" "^ext-ms-.*"
    POST_EXCLUDE_REGEXES ".*[/\\][Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\].*" ".*[/\\][Ss]ystem32[/\\].*"
)

# Copy resolved runtime DLLs beside the executable. Multiple executables share
# one output folder, so prefer the matching toolchain-directory candidate when
# CMake reports both paths as a conflict.
foreach(filename IN LISTS conflicting_dependencies_FILENAMES)
    set(conflict_variable "conflicting_dependencies_${filename}")
    set(preferred_dependency "")

    foreach(candidate IN LISTS ${conflict_variable})
        file(TO_CMAKE_PATH "${candidate}" candidate_path)

        foreach(runtime_search_path IN LISTS runtime_search_paths)
            file(TO_CMAKE_PATH "${runtime_search_path}" normalized_runtime_search_path)
            string(TOLOWER "${candidate_path}" normalized_candidate_path)
            string(TOLOWER "${normalized_runtime_search_path}/" normalized_runtime_search_prefix)
            string(FIND "${normalized_candidate_path}" "${normalized_runtime_search_prefix}" runtime_path_position)

            if(runtime_path_position EQUAL 0)
                set(preferred_dependency "${candidate}")
                break()
            endif()
        endforeach()

        if(preferred_dependency)
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
