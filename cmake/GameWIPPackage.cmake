include_guard(GLOBAL)

# Provides the repeated mechanical package-config/export installation ceremony.
#
# Public helper:
# - gamewip_install_package(TARGET <target> CONFIG_TEMPLATE <file> [PATH_VARS <variables>...])
#
# Inputs:
# - TARGET names an existing library whose install(TARGETS) declaration remains in its CMakeLists.
# - CONFIG_TEMPLATE names that library's package configuration template.
# - PATH_VARS forwards package-relative install variables to configure_package_config_file.
#
# Side effects:
# - Generates and installs the exact-version config files and installs the target export set.
#
# Failure contract:
# - Missing arguments, targets, or templates stop configuration with a descriptive fatal error.

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

function(gamewip_install_package)
    cmake_parse_arguments(PARSE_ARGV 0 gamewip_package "" "TARGET;CONFIG_TEMPLATE" "PATH_VARS")
    if(gamewip_package_UNPARSED_ARGUMENTS OR NOT gamewip_package_TARGET OR NOT gamewip_package_CONFIG_TEMPLATE)
        message(FATAL_ERROR "gamewip_install_package requires TARGET and CONFIG_TEMPLATE; optional arguments are PATH_VARS.")
    endif()
    if(NOT TARGET "${gamewip_package_TARGET}")
        message(FATAL_ERROR "gamewip_install_package target does not exist: ${gamewip_package_TARGET}")
    endif()
    if(NOT EXISTS "${gamewip_package_CONFIG_TEMPLATE}")
        message(FATAL_ERROR "gamewip_install_package config template does not exist: ${gamewip_package_CONFIG_TEMPLATE}")
    endif()

    set(gamewip_package_directory "${CMAKE_INSTALL_LIBDIR}/cmake/${gamewip_package_TARGET}")
    set(gamewip_package_config "${CMAKE_CURRENT_BINARY_DIR}/${gamewip_package_TARGET}Config.cmake")
    set(gamewip_package_version "${CMAKE_CURRENT_BINARY_DIR}/${gamewip_package_TARGET}ConfigVersion.cmake")
    set(gamewip_package_path_arguments)
    if(gamewip_package_PATH_VARS)
        list(APPEND gamewip_package_path_arguments PATH_VARS ${gamewip_package_PATH_VARS})
    endif()

    configure_package_config_file(
        "${gamewip_package_CONFIG_TEMPLATE}"
        "${gamewip_package_config}"
        INSTALL_DESTINATION "${gamewip_package_directory}"
        ${gamewip_package_path_arguments}
    )
    write_basic_package_version_file("${gamewip_package_version}" VERSION "${PROJECT_VERSION}" COMPATIBILITY ExactVersion)
    install(EXPORT "${gamewip_package_TARGET}Targets" DESTINATION "${gamewip_package_directory}" NAMESPACE GameWIP::)
    install(FILES "${gamewip_package_config}" "${gamewip_package_version}" DESTINATION "${gamewip_package_directory}")
endfunction()
