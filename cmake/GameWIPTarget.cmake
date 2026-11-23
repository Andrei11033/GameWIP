include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/GameWIPStorage.cmake")

# Selects a configured target before project() initializes the compiler.

# Normalize host architecture names for toolchain selection.
function(gamewip_get_host_architecture output_variable)
    string(TOLOWER "${CMAKE_HOST_SYSTEM_PROCESSOR}" host_processor)

    if(host_processor STREQUAL "" AND CMAKE_HOST_WIN32)
        if(NOT "$ENV{PROCESSOR_ARCHITEW6432}" STREQUAL "")
            set(host_processor "$ENV{PROCESSOR_ARCHITEW6432}")
        elseif(NOT "$ENV{PROCESSOR_ARCHITECTURE}" STREQUAL "")
            set(host_processor "$ENV{PROCESSOR_ARCHITECTURE}")
        endif()
        string(TOLOWER "${host_processor}" host_processor)
    endif()

    if(host_processor STREQUAL "" AND CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
        execute_process(COMMAND uname -m RESULT_VARIABLE uname_result OUTPUT_VARIABLE host_processor OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)

        if(NOT "${uname_result}" STREQUAL "0")
            set(host_processor "")
        endif()
        string(TOLOWER "${host_processor}" host_processor)
    endif()

    if(host_processor MATCHES "^(amd64|x86_64|x64)$")
        set(host_architecture "x64")
    elseif(host_processor MATCHES "^(arm64|aarch64)$")
        set(host_architecture "arm64")
    else()
        set(host_architecture "")
    endif()

    set("${output_variable}" "${host_architecture}" PARENT_SCOPE)
endfunction()

function(gamewip_prepare_target)
    set(GAMEWIP_TARGET_PLATFORM "native" CACHE STRING "GameWIP build target platform")
    set_property(CACHE GAMEWIP_TARGET_PLATFORM PROPERTY STRINGS native windows linux)
    set(GAMEWIP_TARGET_ARCHITECTURE "native" CACHE STRING "GameWIP build target architecture")
    set_property(CACHE GAMEWIP_TARGET_ARCHITECTURE PROPERTY STRINGS native x64 arm64)

    if(GAMEWIP_TARGET_PLATFORM STREQUAL "native" AND GAMEWIP_TARGET_ARCHITECTURE STREQUAL "native")
        return()
    endif()

    if(NOT GAMEWIP_TARGET_PLATFORM MATCHES "^(windows|linux)$")
        message(FATAL_ERROR "Unsupported GameWIP target platform '${GAMEWIP_TARGET_PLATFORM}'. Use 'native', 'windows', or 'linux'.")
    endif()

    if(GAMEWIP_TARGET_ARCHITECTURE STREQUAL "native")
        message(FATAL_ERROR "Set GAMEWIP_TARGET_ARCHITECTURE when GAMEWIP_TARGET_PLATFORM is '${GAMEWIP_TARGET_PLATFORM}'.")
    endif()

    # Resolve the platform and architecture through the shared target catalog.
    set(gamewip_target_catalog "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../scripts/config/build-targets.json")
    file(READ "${gamewip_target_catalog}" gamewip_target_json)
    string(JSON gamewip_target_count LENGTH "${gamewip_target_json}" targets)
    set(gamewip_target_index -1)

    math(EXPR gamewip_target_last "${gamewip_target_count} - 1")
    foreach(index RANGE 0 ${gamewip_target_last})
        string(JSON candidate_platform GET "${gamewip_target_json}" targets ${index} platform)
        string(JSON candidate_architecture GET "${gamewip_target_json}" targets ${index} architecture)

        if("${candidate_platform}" STREQUAL "${GAMEWIP_TARGET_PLATFORM}" AND "${candidate_architecture}" STREQUAL "${GAMEWIP_TARGET_ARCHITECTURE}")
            set(gamewip_target_index ${index})
            break()
        endif()
    endforeach()

    if(gamewip_target_index LESS 0)
        message(FATAL_ERROR "Unsupported GameWIP target '${GAMEWIP_TARGET_PLATFORM}/${GAMEWIP_TARGET_ARCHITECTURE}'.")
    endif()

    # Reject unsupported target features before project() selects a compiler.
    if(GAMEWIP_TARGET_PLATFORM STREQUAL "windows" AND GAMEWIP_TARGET_ARCHITECTURE STREQUAL "arm64" AND GAMEWIP_ENABLE_ADDRESS_SANITIZER)
        message(
            FATAL_ERROR
            "AddressSanitizer is not available for Windows ARM64 with LLVM-MinGW. "
            "Select Windows x64 or disable GAMEWIP_ENABLE_ADDRESS_SANITIZER."
        )
    endif()

    string(JSON gamewip_target_backend GET "${gamewip_target_json}" targets ${gamewip_target_index} backend)
    string(JSON gamewip_target_toolchain GET "${gamewip_target_json}" targets ${gamewip_target_index} toolchain)
    string(JSON gamewip_target_triple GET "${gamewip_target_json}" targets ${gamewip_target_index} compilerTriple)
    string(JSON gamewip_target_processor GET "${gamewip_target_json}" targets ${gamewip_target_index} processor)

    # Set the platform identity used by the project and platform modules.
    if(DEFINED GAMEWIP_PLATFORM_ID AND NOT GAMEWIP_PLATFORM_ID STREQUAL "" AND NOT GAMEWIP_PLATFORM_ID STREQUAL gamewip_target_backend)
        message(FATAL_ERROR "GAMEWIP_PLATFORM_ID='${GAMEWIP_PLATFORM_ID}' conflicts with target backend '${gamewip_target_backend}'.")
    endif()

    set(GAMEWIP_PLATFORM_ID "${gamewip_target_backend}" CACHE STRING "GameWIP normalized platform backend id" FORCE)
    set(GAMEWIP_TARGET_COMPILER_TRIPLE "${gamewip_target_triple}" CACHE INTERNAL "GameWIP target compiler triple" FORCE)
    set(GAMEWIP_TARGET_PROCESSOR "${gamewip_target_processor}" CACHE INTERNAL "GameWIP target processor" FORCE)
    set(GAMEWIP_TARGET_NATIVE_ARCHITECTURE OFF CACHE INTERNAL "Whether the target matches the host architecture" FORCE)

    # Resolve the selected toolchain and its required files.
    file(READ "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../scripts/config/project-tools.json" gamewip_tools_json)
    string(JSON gamewip_tool_count LENGTH "${gamewip_tools_json}" tools)
    set(gamewip_tool_index -1)
    math(EXPR gamewip_tool_last "${gamewip_tool_count} - 1")

    foreach(index RANGE 0 ${gamewip_tool_last})
        string(JSON candidate_id GET "${gamewip_tools_json}" tools ${index} id)

        if("${candidate_id}" STREQUAL "${gamewip_target_toolchain}")
            set(gamewip_tool_index ${index})
            break()
        endif()
    endforeach()

    # Verify that the selected toolchain is declared.
    if(gamewip_tool_index LESS 0)
        message(FATAL_ERROR "Target toolchain '${gamewip_target_toolchain}' is not declared in scripts/config/project-tools.json.")
    endif()

    string(JSON gamewip_tool_version GET "${gamewip_tools_json}" tools ${gamewip_tool_index} requiredVersion)
    string(JSON gamewip_tool_directory GET "${gamewip_tools_json}" tools ${gamewip_tool_index} provider managedDirectory)
    string(JSON gamewip_tool_binary_directory GET "${gamewip_tools_json}" tools ${gamewip_tool_index} provider binaryDirectory)
    string(
        JSON gamewip_tool_compiler
        ERROR_VARIABLE gamewip_tool_compiler_error
        GET "${gamewip_tools_json}"
        tools
        ${gamewip_tool_index}
        provider
        compiler
    )

    if(gamewip_tool_compiler_error)
        set(gamewip_tool_compiler "")
    endif()

    # Select the verified toolchain archive for this host architecture.
    if(CMAKE_HOST_WIN32)
        gamewip_get_host_architecture(gamewip_host_architecture)
        if(gamewip_host_architecture STREQUAL "x64")
            set(gamewip_host_asset_key "windows-amd64")
        elseif(gamewip_host_architecture STREQUAL "arm64")
            set(gamewip_host_asset_key "windows-arm64")
        else()
            message(FATAL_ERROR "Unsupported Windows host architecture '${CMAKE_HOST_SYSTEM_PROCESSOR}'.")
        endif()
    elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
        gamewip_get_host_architecture(gamewip_host_architecture)

        if(gamewip_host_architecture STREQUAL "x64")
            set(gamewip_host_asset_key "linux-amd64")
        elseif(gamewip_host_architecture STREQUAL "arm64")
            set(gamewip_host_asset_key "linux-arm64")
        else()
            message(FATAL_ERROR "Unsupported Linux host architecture '${CMAKE_HOST_SYSTEM_PROCESSOR}'.")
        endif()
    else()
        message(FATAL_ERROR "Cross-target toolchains are not configured for host '${CMAKE_HOST_SYSTEM_NAME}'.")
    endif()

    # Read the toolchain's host asset and its validation files.
    string(
        JSON gamewip_asset_type
        ERROR_VARIABLE gamewip_asset_error
        TYPE "${gamewip_tools_json}"
        tools
        ${gamewip_tool_index}
        provider
        assets
        "${gamewip_host_asset_key}"
    )
    if(gamewip_asset_error OR gamewip_asset_type STREQUAL "NULL")
        message(FATAL_ERROR "Toolchain '${gamewip_target_toolchain}' has no verified asset for host '${gamewip_host_asset_key}'.")
    endif()

    string(
        JSON gamewip_tool_validation_count
        ERROR_VARIABLE gamewip_asset_validation_error
        LENGTH "${gamewip_tools_json}"
        tools
        ${gamewip_tool_index}
        provider
        assets
        "${gamewip_host_asset_key}"
        validationFiles
    )
    if(gamewip_asset_validation_error)
        string(JSON gamewip_tool_validation_count LENGTH "${gamewip_tools_json}" tools ${gamewip_tool_index} provider validationFiles)
        set(gamewip_validation_json_path provider validationFiles)
    else()
        set(gamewip_validation_json_path provider assets "${gamewip_host_asset_key}" validationFiles)
    endif()

    # Resolve the managed root from the shared tool registry.
    string(TOUPPER "${gamewip_target_toolchain}" gamewip_toolchain_variable)
    string(REPLACE "-" "_" gamewip_toolchain_variable "${gamewip_toolchain_variable}")
    set(gamewip_toolchain_root_variable "GAMEWIP_${gamewip_toolchain_variable}_ROOT")
    if(NOT DEFINED ${gamewip_toolchain_root_variable} OR "${${gamewip_toolchain_root_variable}}" STREQUAL "")
        gamewip_get_managed_environment_path(gamewip_managed_tools_root gameWipToolsRoot)
        set(${gamewip_toolchain_root_variable}
            "${gamewip_managed_tools_root}/${gamewip_tool_directory}/${gamewip_tool_version}"
            CACHE PATH
            "${gamewip_target_toolchain} toolchain root"
        )
    endif()

    string(REPLACE "\\" "/" gamewip_toolchain_root "${${gamewip_toolchain_root_variable}}")
    set(${gamewip_toolchain_root_variable} "${gamewip_toolchain_root}" CACHE PATH "${gamewip_target_toolchain} toolchain root" FORCE)
    set(GAMEWIP_${gamewip_toolchain_variable}_BINARY_DIRECTORY
        "${gamewip_tool_binary_directory}"
        CACHE INTERNAL
        "${gamewip_target_toolchain} binary directory"
        FORCE
    )
    set(GAMEWIP_${gamewip_toolchain_variable}_COMPILER "${gamewip_tool_compiler}" CACHE INTERNAL "${gamewip_target_toolchain} compiler command" FORCE)

    # Check providers with an explicit compiler entry point.
    if(gamewip_tool_compiler)
        set(gamewip_compiler_suffix "")

        if(CMAKE_HOST_WIN32)
            set(gamewip_compiler_suffix ".exe")
        endif()

        set(gamewip_compiler_path "${gamewip_toolchain_root}/${gamewip_tool_binary_directory}/${gamewip_tool_compiler}${gamewip_compiler_suffix}")

        if(NOT EXISTS "${gamewip_compiler_path}")
            message(FATAL_ERROR "Toolchain '${gamewip_target_toolchain}' is missing compiler '${gamewip_compiler_path}'.")
        endif()
    endif()

    # Verify every file declared by the selected host asset.
    math(EXPR gamewip_tool_validation_last "${gamewip_tool_validation_count} - 1")

    foreach(index RANGE 0 ${gamewip_tool_validation_last})
        string(JSON validation_file GET "${gamewip_tools_json}" tools ${gamewip_tool_index} ${gamewip_validation_json_path} ${index})

        if(NOT EXISTS "${gamewip_toolchain_root}/${validation_file}")
            if(CMAKE_HOST_WIN32)
                set(gamewip_setup_hint "Run '.\\setup.bat repair'.")
            else()
                set(gamewip_setup_hint "Run 'bash ./setup.sh repair'.")
            endif()

            message(
                FATAL_ERROR
                "Toolchain '${gamewip_target_toolchain}' is incomplete for ${GAMEWIP_TARGET_PLATFORM}/${GAMEWIP_TARGET_ARCHITECTURE}; missing '${validation_file}'. ${gamewip_setup_hint}"
            )
        endif()
    endforeach()

    string(JSON gamewip_toolchain_file GET "${gamewip_tools_json}" tools ${gamewip_tool_index} provider cmakeToolchain)
    set(gamewip_toolchain_file "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/${gamewip_toolchain_file}")

    if(NOT EXISTS "${gamewip_toolchain_file}")
        message(FATAL_ERROR "Toolchain '${gamewip_target_toolchain}' references missing CMake file '${gamewip_toolchain_file}'.")
    endif()

    # Load the selected compiler and target system during project().
    if(DEFINED CMAKE_TOOLCHAIN_FILE AND NOT CMAKE_TOOLCHAIN_FILE STREQUAL "" AND NOT CMAKE_TOOLCHAIN_FILE STREQUAL gamewip_toolchain_file)
        message(FATAL_ERROR "The selected GameWIP target conflicts with CMAKE_TOOLCHAIN_FILE='${CMAKE_TOOLCHAIN_FILE}'.")
    endif()

    set(CMAKE_TOOLCHAIN_FILE "${gamewip_toolchain_file}" CACHE FILEPATH "GameWIP target toolchain" FORCE)
endfunction()

# ------------------------------------------------------------
# Host architecture detection
# ------------------------------------------------------------

function(gamewip_update_target_native_architecture)
    if(GAMEWIP_TARGET_PLATFORM STREQUAL "native")
        return()
    endif()

    if(CMAKE_HOST_WIN32)
        set(gamewip_host_platform windows)
    elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
        set(gamewip_host_platform linux)
    else()
        return()
    endif()

    if(NOT GAMEWIP_TARGET_PLATFORM STREQUAL gamewip_host_platform)
        return()
    endif()

    gamewip_get_host_architecture(gamewip_host_architecture)
    set(gamewip_target_native_architecture OFF)

    if(GAMEWIP_TARGET_ARCHITECTURE STREQUAL gamewip_host_architecture)
        set(gamewip_target_native_architecture ON)
    endif()

    set(GAMEWIP_TARGET_NATIVE_ARCHITECTURE
        "${gamewip_target_native_architecture}"
        CACHE INTERNAL
        "Whether the target matches the host architecture"
        FORCE
    )
endfunction()
