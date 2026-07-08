foreach(required_variable IN ITEMS
        PROJECT_BUILD_DIR
        PROJECT_SOURCE_DIR
        INSTALL_PREFIX
        CONSUMER_SOURCE_DIR
        CONSUMER_BUILD_DIR
        GENERATOR
        BUILD_TYPE
        PACKAGE_VERSION
        MULTI_CONFIG
        CXX_COMPILER
        COVERAGE_ENABLED
        ADDRESS_SANITIZER_ENABLED
        EXECUTABLE_SUFFIX)
    if(NOT DEFINED ${required_variable})
        message(FATAL_ERROR "${required_variable} is required for installed-consumer validation.")
    endif()
endforeach()

file(REMOVE_RECURSE "${INSTALL_PREFIX}" "${CONSUMER_BUILD_DIR}")

execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${PROJECT_BUILD_DIR}" --prefix "${INSTALL_PREFIX}" --config "${BUILD_TYPE}"
    RESULT_VARIABLE install_result
    OUTPUT_VARIABLE install_output
    ERROR_VARIABLE install_error
)
if(NOT install_result EQUAL 0)
    message(FATAL_ERROR "GameWIP package installation failed.\n${install_output}\n${install_error}")
endif()

set(expected_gamewip_headers
    debug/assert/assert.h
    debug/assert/assert_export.h
    filesystem/filesystem.h
    io/io.h
    logger/logger.h
    logger/logger_export.h
    logger/logger_macros.h
    terminal/terminal.h
    terminal/terminal_export.h
    test_support/test_support.h
)
list(SORT expected_gamewip_headers)

file(GLOB_RECURSE installed_gamewip_headers
    RELATIVE "${INSTALL_PREFIX}/include"
    "${INSTALL_PREFIX}/include/*"
)
list(FILTER installed_gamewip_headers EXCLUDE REGEX "^tracy/")
list(SORT installed_gamewip_headers)

if(NOT "${installed_gamewip_headers}" STREQUAL "${expected_gamewip_headers}")
    message(FATAL_ERROR
        "Installed GameWIP headers differ from the public allowlist.\n"
        "Expected: ${expected_gamewip_headers}\n"
        "Actual: ${installed_gamewip_headers}"
    )
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        -S "${CONSUMER_SOURCE_DIR}"
        -B "${CONSUMER_BUILD_DIR}"
        -G "${GENERATOR}"
        "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}"
        "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}"
        "-DCMAKE_PREFIX_PATH=${INSTALL_PREFIX}"
        "-DGAMEWIP_PACKAGE_VERSION=${PACKAGE_VERSION}"
        "-DGAMEWIP_CONSUMER_LINK_COVERAGE=${COVERAGE_ENABLED}"
        "-DGAMEWIP_CONSUMER_ENABLE_ADDRESS_SANITIZER=${ADDRESS_SANITIZER_ENABLED}"
    RESULT_VARIABLE configure_result
    OUTPUT_VARIABLE configure_output
    ERROR_VARIABLE configure_error
)
if(NOT configure_result EQUAL 0)
    message(FATAL_ERROR "Installed consumer configuration failed.\n${configure_output}\n${configure_error}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${CONSUMER_BUILD_DIR}" --config "${BUILD_TYPE}"
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_output
    ERROR_VARIABLE build_error
)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "Installed consumer build failed.\n${build_output}\n${build_error}")
endif()

if(MULTI_CONFIG)
    set(consumer_executable "${CONSUMER_BUILD_DIR}/${BUILD_TYPE}/GameWIPInstalledConsumer${EXECUTABLE_SUFFIX}")
else()
    set(consumer_executable "${CONSUMER_BUILD_DIR}/GameWIPInstalledConsumer${EXECUTABLE_SUFFIX}")
endif()
cmake_path(GET CXX_COMPILER PARENT_PATH compiler_runtime_directory)
set(runtime_path "${INSTALL_PREFIX}/bin;${compiler_runtime_directory};$ENV{PATH}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "PATH=${runtime_path}" "${consumer_executable}"
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_output
    ERROR_VARIABLE run_error
)
if(NOT run_result EQUAL 0)
    message(FATAL_ERROR "Installed consumer execution failed.\n${run_output}\n${run_error}")
endif()
