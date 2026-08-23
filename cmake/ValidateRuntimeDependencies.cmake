foreach(
    required_variable
    IN
    ITEMS TEST_EXECUTABLE SOURCE_OUTPUT_DIR VALIDATION_ROOT RUNTIME_SEARCH_DIR COPY_SCRIPT
)
    if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
        message(FATAL_ERROR "${required_variable} was not provided.")
    endif()
endforeach()

if(NOT EXISTS "${TEST_EXECUTABLE}")
    message(FATAL_ERROR "Test executable does not exist: ${TEST_EXECUTABLE}")
endif()

set(compiler_runtime "${RUNTIME_SEARCH_DIR}/libstdc++-6.dll")
if(NOT EXISTS "${compiler_runtime}")
    message(FATAL_ERROR "GNU compiler runtime does not exist: ${compiler_runtime}")
endif()

file(REMOVE_RECURSE "${VALIDATION_ROOT}")
file(MAKE_DIRECTORY "${VALIDATION_ROOT}")

get_filename_component(test_executable_name "${TEST_EXECUTABLE}" NAME)
set(staged_executable "${VALIDATION_ROOT}/${test_executable_name}")
file(COPY_FILE "${TEST_EXECUTABLE}" "${staged_executable}")

file(GLOB source_dlls LIST_DIRECTORIES FALSE "${SOURCE_OUTPUT_DIR}/*.dll")
if(NOT source_dlls)
    message(FATAL_ERROR "No application DLLs were available in ${SOURCE_OUTPUT_DIR}.")
endif()
foreach(source_dll IN LISTS source_dlls)
    get_filename_component(source_dll_name "${source_dll}" NAME)
    file(COPY_FILE "${source_dll}" "${VALIDATION_ROOT}/${source_dll_name}")
endforeach()

# An invalid DLL makes it impossible for the dependency scanner to recurse
# through the stale file. The validation only succeeds if the clean shadow scan
# ignores this app-local copy and stages the compiler's current runtime instead.
set(staged_runtime "${VALIDATION_ROOT}/libstdc++-6.dll")
file(WRITE "${staged_runtime}" "intentionally stale compiler runtime")

set(copy_command
    "${CMAKE_COMMAND}"
    "-DGAMEWIP_EXECUTABLE=${staged_executable}"
    "-DGAMEWIP_OUTPUT_DIR=${VALIDATION_ROOT}"
    "-DGAMEWIP_RUNTIME_SEARCH_DIR=${RUNTIME_SEARCH_DIR}"
    "-DGAMEWIP_FAIL_ON_UNRESOLVED_DEPENDENCIES=ON"
)
if(DEFINED OBJDUMP AND NOT OBJDUMP STREQUAL "")
    list(APPEND copy_command "-DGAMEWIP_OBJDUMP=${OBJDUMP}")
endif()
list(APPEND copy_command -P "${COPY_SCRIPT}")

execute_process(COMMAND ${copy_command} RESULT_VARIABLE copy_result OUTPUT_VARIABLE copy_output ERROR_VARIABLE copy_error)
if(NOT copy_result EQUAL 0)
    message(FATAL_ERROR "Runtime dependency staging failed (${copy_result}).\n" "stdout:\n${copy_output}\n" "stderr:\n${copy_error}")
endif()

file(SHA256 "${compiler_runtime}" compiler_runtime_hash)
file(SHA256 "${staged_runtime}" staged_runtime_hash)
if(NOT staged_runtime_hash STREQUAL compiler_runtime_hash)
    message(FATAL_ERROR "The staged libstdc++-6.dll did not come from the active compiler.")
endif()

execute_process(
    COMMAND "${staged_executable}" --test-module=runner
    WORKING_DIRECTORY "${VALIDATION_ROOT}"
    RESULT_VARIABLE launch_result
    OUTPUT_VARIABLE launch_output
    ERROR_VARIABLE launch_error
    TIMEOUT 60
)
if(NOT launch_result EQUAL 0)
    message(FATAL_ERROR "The staged executable failed to launch (${launch_result}).\n" "stdout:\n${launch_output}\n" "stderr:\n${launch_error}")
endif()

file(REMOVE_RECURSE "${VALIDATION_ROOT}")
message(
    STATUS
    "Runtime dependency validation passed: the active compiler runtime replaced "
    "the stale app-local DLL and the staged executable launched."
)
