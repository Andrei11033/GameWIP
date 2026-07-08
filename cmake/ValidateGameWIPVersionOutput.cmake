foreach(required_variable IN ITEMS EXECUTABLE SOURCE_DIR PROJECT_VERSION_VALUE)
    if(NOT DEFINED ${required_variable})
        message(FATAL_ERROR "${required_variable} is required.")
    endif()
endforeach()

include("${SOURCE_DIR}/cmake/GameWIPVersion.cmake")
gamewip_detect_version(
    expected
    SOURCE_DIR "${SOURCE_DIR}"
    PROJECT_VERSION "${PROJECT_VERSION_VALUE}"
)

execute_process(
    COMMAND "${EXECUTABLE}" --version
    RESULT_VARIABLE version_result
    OUTPUT_VARIABLE version_output
    ERROR_VARIABLE version_error
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

if(NOT version_result EQUAL 0)
    message(FATAL_ERROR
        "GameWIP --version failed with exit code ${version_result}.\n"
        "stdout:\n${version_output}\n"
        "stderr:\n${version_error}"
    )
endif()

set(expected_output "GameWIP ${expected_DISPLAY}")
if(NOT version_output STREQUAL expected_output)
    message(FATAL_ERROR
        "GameWIP --version output mismatch.\n"
        "Expected: ${expected_output}\n"
        "Actual:   ${version_output}"
    )
endif()
