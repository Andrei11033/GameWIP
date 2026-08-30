# Installs GameWIP and validates clean consumers across supported package boundaries.

foreach(
    required_variable
    IN
    ITEMS
        PROJECT_BUILD_DIR
        PROJECT_SOURCE_DIR
        INSTALL_PREFIX
        CONSUMER_SOURCE_DIR
        CONSUMER_BUILD_DIR
        GENERATOR
        BUILD_TYPE
        PACKAGE_VERSION
        GAMEWIP_CMAKE_MINIMUM_VERSION
        MULTI_CONFIG
        CXX_COMPILER
        COVERAGE_ENABLED
        ADDRESS_SANITIZER_ENABLED
        EXECUTABLE_SUFFIX
)
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
    filesystem/directory.h
    filesystem/entry.h
    filesystem/file.h
    filesystem/filesystem.h
    filesystem/path.h
    io/memory.h
    io/io.h
    io/status.h
    io/stream.h
    io/transfer.h
    logger/config.h
    logger/detail/formatting.h
    logger/logger.h
    logger/logger_export.h
    logger/logger_macros.h
    logger/types.h
    terminal/input.h
    terminal/output.h
    terminal/session.h
    terminal/style.h
    terminal/terminal.h
    terminal/terminal_export.h
    terminal/types.h
    test_support/files.h
    test_support/process.h
    test_support/reporting.h
    test_support/stress.h
    test_support/test_support.h
    test_support/types.h
    unicode/unicode.h
    desktop/child_surface.h
    desktop/clipboard.h
    desktop/cursor.h
    desktop/data_transfer.h
    desktop/description.h
    desktop/display.h
    desktop/display_info.h
    desktop/events.h
    desktop/native/win32.h
    desktop/renderer_bridge.h
    desktop/types.h
    desktop/window.h
    desktop/desktop_export.h
)
list(SORT expected_gamewip_headers)

file(GLOB_RECURSE installed_gamewip_headers RELATIVE "${INSTALL_PREFIX}/include" "${INSTALL_PREFIX}/include/*")
list(FILTER installed_gamewip_headers EXCLUDE REGEX "^tracy/")
list(SORT installed_gamewip_headers)

if(NOT "${installed_gamewip_headers}" STREQUAL "${expected_gamewip_headers}")
    message(
        FATAL_ERROR
        "Installed GameWIP headers differ from the public allowlist.\n"
        "Expected: ${expected_gamewip_headers}\n"
        "Actual: ${installed_gamewip_headers}"
    )
endif()

cmake_path(GET CXX_COMPILER PARENT_PATH compiler_runtime_directory)
set(runtime_path "${INSTALL_PREFIX}/bin;${compiler_runtime_directory};$ENV{PATH}")

function(
    run_installed_consumer
    source_dir
    build_dir
    executable_name
    package_name
    prefix_path
)
    file(REMOVE_RECURSE "${build_dir}")
    string(REPLACE ";" "\\;" escaped_prefix_path "${prefix_path}")

    set(configure_command
        "${CMAKE_COMMAND}"
        -S
        "${source_dir}"
        -B
        "${build_dir}"
        -G
        "${GENERATOR}"
        "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}"
        "-DCMAKE_PREFIX_PATH=${escaped_prefix_path}"
        "-DGAMEWIP_PACKAGE_VERSION=${PACKAGE_VERSION}"
        "-DGAMEWIP_CMAKE_MINIMUM_VERSION=${GAMEWIP_CMAKE_MINIMUM_VERSION}"
        "-DGAMEWIP_CONSUMER_LINK_COVERAGE=${COVERAGE_ENABLED}"
        "-DGAMEWIP_CONSUMER_ENABLE_ADDRESS_SANITIZER=${ADDRESS_SANITIZER_ENABLED}"
    )
    if(NOT MULTI_CONFIG)
        list(APPEND configure_command "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}")
    endif()
    if(package_name)
        list(APPEND configure_command "-DGAMEWIP_CONSUMER_PACKAGE=${package_name}")
    endif()

    execute_process(COMMAND ${configure_command} RESULT_VARIABLE configure_result OUTPUT_VARIABLE configure_output ERROR_VARIABLE configure_error)
    if(NOT configure_result EQUAL 0)
        message(FATAL_ERROR "Installed ${package_name} consumer configuration failed.\n" "${configure_output}\n${configure_error}")
    endif()

    execute_process(
        COMMAND "${CMAKE_COMMAND}" --build "${build_dir}" --config "${BUILD_TYPE}"
        RESULT_VARIABLE build_result
        OUTPUT_VARIABLE build_output
        ERROR_VARIABLE build_error
    )
    if(NOT build_result EQUAL 0)
        message(FATAL_ERROR "Installed ${package_name} consumer build failed.\n${build_output}\n${build_error}")
    endif()

    if(MULTI_CONFIG)
        set(consumer_executable "${build_dir}/${BUILD_TYPE}/${executable_name}${EXECUTABLE_SUFFIX}")
    else()
        set(consumer_executable "${build_dir}/${executable_name}${EXECUTABLE_SUFFIX}")
    endif()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "PATH=${runtime_path}" "${consumer_executable}"
        RESULT_VARIABLE run_result
        OUTPUT_VARIABLE run_output
        ERROR_VARIABLE run_error
    )
    if(NOT run_result EQUAL 0)
        message(FATAL_ERROR "Installed ${package_name} consumer execution failed.\n${run_output}\n${run_error}")
    endif()
endfunction()

run_installed_consumer("${CONSUMER_SOURCE_DIR}" "${CONSUMER_BUILD_DIR}/combined" GameWIPInstalledConsumer "combined" "${INSTALL_PREFIX}")

foreach(
    package_name
    IN
    ITEMS Unicode IO FileSystem Terminal Desktop Logger Assert TestSupport
)
    string(TOLOWER "${package_name}" package_directory)
    run_installed_consumer(
        "${CONSUMER_SOURCE_DIR}/isolated"
        "${CONSUMER_BUILD_DIR}/isolated-${package_directory}"
        GameWIPIsolatedConsumer
        "${package_name}"
        "${INSTALL_PREFIX}"
    )
endforeach()

# Assert must retain its own resource prefix while Logger and its dependencies
# are discovered from a separate installation root on every supported CMake.
set(assert_prefix "${CONSUMER_BUILD_DIR}/assert-prefix")
file(REMOVE_RECURSE "${assert_prefix}")
file(MAKE_DIRECTORY "${assert_prefix}/include/debug" "${assert_prefix}/lib/cmake" "${assert_prefix}/share" "${assert_prefix}/bin")
file(COPY "${INSTALL_PREFIX}/include/debug/assert" DESTINATION "${assert_prefix}/include/debug")
file(COPY "${INSTALL_PREFIX}/lib/cmake/Assert" DESTINATION "${assert_prefix}/lib/cmake")
file(COPY "${INSTALL_PREFIX}/share/Assert" DESTINATION "${assert_prefix}/share")
file(GLOB assert_link_files "${INSTALL_PREFIX}/lib/*Assert*")
if(assert_link_files)
    file(COPY ${assert_link_files} DESTINATION "${assert_prefix}/lib")
endif()
file(GLOB assert_runtime_files "${INSTALL_PREFIX}/bin/*Assert*")
if(assert_runtime_files)
    file(COPY ${assert_runtime_files} DESTINATION "${assert_prefix}/bin")
endif()

set(split_prefix_path "${assert_prefix};${INSTALL_PREFIX}")
run_installed_consumer(
    "${CONSUMER_SOURCE_DIR}/isolated"
    "${CONSUMER_BUILD_DIR}/split-prefix-assert"
    GameWIPIsolatedConsumer
    Assert
    "${split_prefix_path}"
)
