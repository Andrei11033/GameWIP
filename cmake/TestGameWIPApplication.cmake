# Exercises application manifest composition through independent consumer directories.
cmake_minimum_required(VERSION "${GAMEWIP_CMAKE_MINIMUM_VERSION}")

foreach(
    required_variable
    IN
    ITEMS PROJECT_SOURCE_DIR WORK_DIR MODULE_DIR GENERATOR CXX_COMPILER BUILD_TYPE
)
    if(NOT DEFINED ${required_variable})
        message(FATAL_ERROR "${required_variable} is required for application manifest validation.")
    endif()
endforeach()

# Generated fixture projects keep invalid target and language cases isolated from
# the repository configuration. Reuse source files without copying library state.
file(MAKE_DIRECTORY "${WORK_DIR}/source/first" "${WORK_DIR}/source/second")
file(
    WRITE "${WORK_DIR}/source/CMakeLists.txt"
    [=[
cmake_minimum_required(VERSION "${GAMEWIP_CMAKE_MINIMUM_VERSION}")
project(ApplicationManifestConsumer LANGUAGES CXX)

if(CASE STREQUAL "non_windows")
    # Exercise the portable no-op contract even on the Windows validation host.
    set(WIN32 FALSE)
endif()
if(WIN32 AND NOT CASE STREQUAL "missing_rc")
    enable_language(RC)
endif()
if(CASE STREQUAL "missing_rc")
    # Some Windows toolchains load RC implicitly with CXX. Exercise the missing
    # prerequisite explicitly so the diagnostic is covered on those hosts too.
    set(CMAKE_RC_COMPILER_LOADED FALSE)
endif()

add_executable(first-app "${PROBE_SOURCE}")
add_executable(first_app "${PROBE_SOURCE}")
target_compile_features(first-app PRIVATE cxx_std_23)
target_compile_features(first_app PRIVATE cxx_std_23)
target_compile_definitions(first-app PRIVATE EXPECT_COMMON_CONTROLS=1)
target_compile_definitions(first_app PRIVATE EXPECT_COMMON_CONTROLS=0)
set_target_properties(first-app first_app PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")

if(CASE STREQUAL "composition" OR CASE STREQUAL "non_windows")
    # Include the module in a child first: the globally available function must
    # still find its templates when invoked from siblings or their parent.
    add_subdirectory(first)
    add_subdirectory(second)
    gamewip_attach_application_manifest(TARGET first-app PER_MONITOR_V2)
    gamewip_attach_application_manifest(TARGET first-app COMMON_CONTROLS_V6)

    foreach(application IN ITEMS first-app first_app)
        get_target_property(sources "${application}" SOURCES)
        list(FILTER sources INCLUDE REGEX "\\.rc$")
        list(LENGTH sources resource_count)
        get_property(manifest_path TARGET "${application}" PROPERTY GAMEWIP_APPLICATION_MANIFEST_PATH)
        if(WIN32)
            if(NOT resource_count EQUAL 1 OR NOT EXISTS "${manifest_path}")
                message(FATAL_ERROR "${application} must own exactly one generated resource.")
            endif()
        elseif(NOT resource_count EQUAL 0 OR manifest_path)
            message(FATAL_ERROR "Non-Windows attachment generated a platform resource.")
        endif()
    endforeach()
else()
    include("${MODULE_DIR}/GameWIPApplication.cmake")
    if(CASE STREQUAL "library")
        add_library(invalid INTERFACE)
        gamewip_attach_application_manifest(TARGET invalid PER_MONITOR_V2)
    elseif(CASE STREQUAL "alias")
        add_executable(invalid ALIAS first-app)
        gamewip_attach_application_manifest(TARGET invalid PER_MONITOR_V2)
    elseif(CASE STREQUAL "imported")
        add_executable(invalid IMPORTED)
        gamewip_attach_application_manifest(TARGET invalid PER_MONITOR_V2)
    elseif(CASE STREQUAL "missing_requirement")
        gamewip_attach_application_manifest(TARGET first-app)
    elseif(CASE STREQUAL "unknown_requirement")
        gamewip_attach_application_manifest(TARGET first-app MISSPELLED_REQUIREMENT)
    elseif(CASE STREQUAL "missing_target")
        gamewip_attach_application_manifest(TARGET absent PER_MONITOR_V2)
    else()
        gamewip_attach_application_manifest(TARGET first-app PER_MONITOR_V2)
    endif()
endif()
]=]
)
file(
    WRITE "${WORK_DIR}/source/first/CMakeLists.txt"
    [=[
include("${MODULE_DIR}/GameWIPApplication.cmake")
gamewip_attach_application_manifest(TARGET first-app COMMON_CONTROLS_V6)
]=]
)
file(
    WRITE "${WORK_DIR}/source/second/CMakeLists.txt"
    [=[
include("${MODULE_DIR}/GameWIPApplication.cmake")
gamewip_attach_application_manifest(TARGET first_app PER_MONITOR_V2)
]=]
)

function(configure_case case expected_error)
    execute_process(
        COMMAND
            "${CMAKE_COMMAND}" -S "${WORK_DIR}/source" -B "${WORK_DIR}/${case}" -G "${GENERATOR}" "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}"
            "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}" "-DGAMEWIP_CMAKE_MINIMUM_VERSION=${GAMEWIP_CMAKE_MINIMUM_VERSION}" "-DMODULE_DIR=${MODULE_DIR}"
            "-DPROBE_SOURCE=${PROJECT_SOURCE_DIR}/game/validation/application_manifest/main.cpp" "-DCASE=${case}"
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

# The compiled probes inspect the actual process resource and effective DPI
# policy, catching overwritten manifests that configure-time source counts miss.
execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${WORK_DIR}/composition" --config "${BUILD_TYPE}"
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_output
    ERROR_VARIABLE build_error
)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "Application manifest probes did not build.\n${build_output}\n${build_error}")
endif()
foreach(application IN ITEMS first-app first_app)
    set(executable "${WORK_DIR}/composition/bin/${BUILD_TYPE}/${application}")
    if(WIN32)
        string(APPEND executable ".exe")
    endif()
    execute_process(COMMAND "${executable}" RESULT_VARIABLE run_result OUTPUT_VARIABLE run_output ERROR_VARIABLE run_error)
    if(NOT run_result EQUAL 0)
        message(FATAL_ERROR "Application manifest probe '${application}' failed (${run_result}).\n${run_output}\n${run_error}")
    endif()
endforeach()
