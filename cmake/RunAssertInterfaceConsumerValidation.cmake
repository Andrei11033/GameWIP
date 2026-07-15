foreach(required_variable IN ITEMS
        PROJECT_SOURCE_DIR
        WORK_DIR
        CONSUMER_SOURCE_DIR
        GENERATOR
        BUILD_TYPE
        PACKAGE_VERSION
        GAMEWIP_CMAKE_MINIMUM_VERSION
        MULTI_CONFIG
        CXX_COMPILER
        EXECUTABLE_SUFFIX)
    if(NOT DEFINED ${required_variable})
        message(FATAL_ERROR "${required_variable} is required for Assert interface-package validation.")
    endif()
endforeach()

set(project_build_dir "${WORK_DIR}/project")
set(install_prefix "${WORK_DIR}/prefix")
set(absolute_data_dir "${WORK_DIR}/absolute-data")
set(consumer_build_dir "${WORK_DIR}/consumer")
file(REMOVE_RECURSE "${WORK_DIR}")

set(configure_command
    "${CMAKE_COMMAND}"
    -S "${PROJECT_SOURCE_DIR}"
    -B "${project_build_dir}"
    -G "${GENERATOR}"
    "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}"
    "-DCMAKE_INSTALL_DATADIR=${absolute_data_dir}"
    -DGAMEWIP_BUILD_GAME=OFF
    -DGAMEWIP_BUILD_TESTS=OFF
    -DGAMEWIP_BUILD_BENCHMARKS=OFF
    -DGAMEWIP_RUN_TESTS_AT_STARTUP=OFF
    -DGAMEWIP_RUN_BENCHMARKS_AT_STARTUP=OFF
    -DGAMEWIP_ENABLE_ASSERTS=OFF
    -DGAMEWIP_ENABLE_TRACY=OFF
)
if(NOT MULTI_CONFIG)
    list(APPEND configure_command "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}")
endif()

execute_process(
    COMMAND ${configure_command}
    RESULT_VARIABLE configure_result
    OUTPUT_VARIABLE configure_output
    ERROR_VARIABLE configure_error
)
if(NOT configure_result EQUAL 0)
    message(FATAL_ERROR "Interface-only Assert configuration failed.\n${configure_output}\n${configure_error}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${project_build_dir}"
        --config "${BUILD_TYPE}" --parallel
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_output
    ERROR_VARIABLE build_error
)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "Interface-only Assert build failed.\n${build_output}\n${build_error}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${project_build_dir}"
        --prefix "${install_prefix}" --config "${BUILD_TYPE}"
    RESULT_VARIABLE prefix_install_result
    OUTPUT_VARIABLE prefix_install_output
    ERROR_VARIABLE prefix_install_error
)
if(NOT prefix_install_result EQUAL 0)
    message(FATAL_ERROR
        "Interface-only Assert prefix installation failed.\n"
        "${prefix_install_output}\n${prefix_install_error}"
    )
endif()

set(consumer_configure_command
    "${CMAKE_COMMAND}"
    -S "${CONSUMER_SOURCE_DIR}"
    -B "${consumer_build_dir}"
    -G "${GENERATOR}"
    "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}"
    "-DCMAKE_PREFIX_PATH=${install_prefix}"
    "-DGAMEWIP_PACKAGE_VERSION=${PACKAGE_VERSION}"
    "-DGAMEWIP_CMAKE_MINIMUM_VERSION=${GAMEWIP_CMAKE_MINIMUM_VERSION}"
    -DGAMEWIP_CONSUMER_PACKAGE=Assert
)
if(NOT MULTI_CONFIG)
    list(APPEND consumer_configure_command "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}")
endif()
execute_process(
    COMMAND ${consumer_configure_command}
    RESULT_VARIABLE consumer_configure_result
    OUTPUT_VARIABLE consumer_configure_output
    ERROR_VARIABLE consumer_configure_error
)
if(NOT consumer_configure_result EQUAL 0)
    message(FATAL_ERROR
        "Interface-only Assert consumer configuration failed.\n"
        "${consumer_configure_output}\n${consumer_configure_error}"
    )
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${consumer_build_dir}" --config "${BUILD_TYPE}"
    RESULT_VARIABLE consumer_build_result
    OUTPUT_VARIABLE consumer_build_output
    ERROR_VARIABLE consumer_build_error
)
if(NOT consumer_build_result EQUAL 0)
    message(FATAL_ERROR
        "Interface-only Assert consumer build failed.\n"
        "${consumer_build_output}\n${consumer_build_error}"
    )
endif()

if(MULTI_CONFIG)
    set(consumer_executable "${consumer_build_dir}/${BUILD_TYPE}/GameWIPIsolatedConsumer${EXECUTABLE_SUFFIX}")
else()
    set(consumer_executable "${consumer_build_dir}/GameWIPIsolatedConsumer${EXECUTABLE_SUFFIX}")
endif()
execute_process(
    COMMAND "${consumer_executable}"
    RESULT_VARIABLE consumer_run_result
    OUTPUT_VARIABLE consumer_run_output
    ERROR_VARIABLE consumer_run_error
)
if(NOT consumer_run_result EQUAL 0)
    message(FATAL_ERROR
        "Interface-only Assert consumer execution failed.\n"
        "${consumer_run_output}\n${consumer_run_error}"
    )
endif()
