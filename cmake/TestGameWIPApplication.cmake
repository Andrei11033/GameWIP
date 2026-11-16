# Exercises application manifest composition through independent consumer directories.
cmake_minimum_required(VERSION "${GAMEWIP_CMAKE_MINIMUM_VERSION}")

foreach(
    required_variable
    IN
    ITEMS PROJECT_SOURCE_DIR WORK_DIR MODULE_DIR GENERATOR CXX_COMPILER BUILD_TYPE RUN_RUNTIME_PROBE
)
    if(NOT DEFINED ${required_variable})
        message(FATAL_ERROR "${required_variable} is required for application manifest validation.")
    endif()
endforeach()

set(FIXTURE_DIR "${PROJECT_SOURCE_DIR}/game/validation/tests/application_manifest/platform/win32")

# ------------------------------------------------------------
# Validate manifest contracts
# ------------------------------------------------------------

function(configure_case case expected_error)
    execute_process(
        COMMAND
            "${CMAKE_COMMAND}" -S "${FIXTURE_DIR}" -B "${WORK_DIR}/projects/${case}" -G "${GENERATOR}" "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}"
            "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}" "-DGAMEWIP_CMAKE_MINIMUM_VERSION=${GAMEWIP_CMAKE_MINIMUM_VERSION}" "-DMODULE_DIR=${MODULE_DIR}"
            "-DCASE=${case}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
    )
    if(expected_error)
        string(REGEX REPLACE "[ \t\r\n]+" " " diagnostic "${output}\n${error}")
        if(result EQUAL 0 OR NOT diagnostic MATCHES "${expected_error}")
            message(FATAL_ERROR "Manifest case '${case}' did not reject the invalid contract as expected.\n${output}\n${error}")
        endif()
    elseif(NOT result EQUAL 0)
        message(FATAL_ERROR "Manifest case '${case}' failed.\n${output}\n${error}")
    endif()
endfunction()

configure_case(composition "")
configure_case(non_windows "")
configure_case(library "must be a local executable")
configure_case(alias "must be a local executable")
configure_case(imported "must be a local executable")
configure_case(missing_requirement "requires COMMON_CONTROLS_V6 or PER_MONITOR_V2")
configure_case(unknown_requirement "requires TARGET <executable>")
configure_case(missing_target "target does not exist")
if(WIN32)
    configure_case(missing_rc "Enable RC with enable_language")
endif()

# ------------------------------------------------------------
# Run compiled manifest probes
# ------------------------------------------------------------

if(RUN_RUNTIME_PROBE)
    # The compiled probes inspect the actual process resource and effective DPI
    # policy, catching overwritten manifests that configure-time source counts miss.
    execute_process(
        COMMAND "${CMAKE_COMMAND}" --build "${WORK_DIR}/projects/composition" --config "${BUILD_TYPE}"
        RESULT_VARIABLE build_result
        OUTPUT_VARIABLE build_output
        ERROR_VARIABLE build_error
    )
    if(NOT build_result EQUAL 0)
        message(FATAL_ERROR "Application manifest probes did not build.\n${build_output}\n${build_error}")
    endif()
    foreach(application IN ITEMS first-app first_app)
        set(executable "${WORK_DIR}/projects/composition/bin/${BUILD_TYPE}/${application}")
        if(WIN32)
            string(APPEND executable ".exe")
        endif()
        execute_process(COMMAND "${executable}" RESULT_VARIABLE run_result OUTPUT_VARIABLE run_output ERROR_VARIABLE run_error)
        if(NOT run_result EQUAL 0)
            message(FATAL_ERROR "Application manifest probe '${application}' failed (${run_result}).\n${run_output}\n${run_error}")
        endif()
    endforeach()
else()
    message(STATUS "Skipping compiled application manifest runtime probes; configure-time helper validation remains enabled.")
endif()
