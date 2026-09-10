include_guard(GLOBAL)

# Attaches one process-wide Windows manifest to an application executable.
# Library targets never own this resource because DPI and Common Controls are
# executable policy, not library ABI.
#
# Public helper:
# - gamewip_attach_application_manifest(TARGET <executable> [COMMON_CONTROLS_V6] [PER_MONITOR_V2])
#
# Repeated calls merge requirements and reuse the same generated resource.
# Non-Windows builds validate the target and then perform no platform-specific work.

if(NOT DEFINED GAMEWIP_APPLICATION_MANIFEST_TEMPLATE)
    set(GAMEWIP_APPLICATION_MANIFEST_TEMPLATE "${CMAKE_CURRENT_LIST_DIR}/application_manifest.manifest.in")
endif()
if(NOT DEFINED GAMEWIP_APPLICATION_RC_TEMPLATE)
    set(GAMEWIP_APPLICATION_RC_TEMPLATE "${CMAKE_CURRENT_LIST_DIR}/application_manifest.rc.in")
endif()

function(gamewip_attach_application_manifest)
    cmake_parse_arguments(gamewip_manifest "COMMON_CONTROLS_V6;PER_MONITOR_V2" "TARGET" "" ${ARGN})

    if(gamewip_manifest_UNPARSED_ARGUMENTS OR NOT gamewip_manifest_TARGET)
        message(FATAL_ERROR "gamewip_attach_application_manifest requires TARGET <executable> and one or more manifest requirements.")
    endif()
    if(NOT TARGET "${gamewip_manifest_TARGET}")
        message(FATAL_ERROR "Application manifest target does not exist: ${gamewip_manifest_TARGET}")
    endif()

    get_target_property(gamewip_manifest_type "${gamewip_manifest_TARGET}" TYPE)
    if(NOT gamewip_manifest_type STREQUAL "EXECUTABLE")
        message(FATAL_ERROR "Application manifest target must be an executable: ${gamewip_manifest_TARGET}")
    endif()
    if(NOT gamewip_manifest_COMMON_CONTROLS_V6 AND NOT gamewip_manifest_PER_MONITOR_V2)
        message(FATAL_ERROR "gamewip_attach_application_manifest requires COMMON_CONTROLS_V6 or PER_MONITOR_V2.")
    endif()

    if(gamewip_manifest_COMMON_CONTROLS_V6)
        set(gamewip_manifest_common_controls 1)
    else()
        set(gamewip_manifest_common_controls 0)
    endif()
    if(gamewip_manifest_PER_MONITOR_V2)
        set(gamewip_manifest_per_monitor_v2 1)
    else()
        set(gamewip_manifest_per_monitor_v2 0)
    endif()

    get_property(gamewip_manifest_existing TARGET "${gamewip_manifest_TARGET}" PROPERTY GAMEWIP_APPLICATION_MANIFEST_REQUIREMENTS)
    if(NOT gamewip_manifest_existing)
        set(gamewip_manifest_existing "")
    endif()
    if(gamewip_manifest_common_controls)
        list(APPEND gamewip_manifest_existing COMMON_CONTROLS_V6)
    endif()
    if(gamewip_manifest_per_monitor_v2)
        list(APPEND gamewip_manifest_existing PER_MONITOR_V2)
    endif()
    list(REMOVE_DUPLICATES gamewip_manifest_existing)
    list(SORT gamewip_manifest_existing)

    if(NOT WIN32)
        set_property(TARGET "${gamewip_manifest_TARGET}" PROPERTY GAMEWIP_APPLICATION_MANIFEST_REQUIREMENTS "${gamewip_manifest_existing}")
        return()
    endif()

    if(NOT CMAKE_RC_COMPILER)
        enable_language(RC)
    endif()

    list(FIND gamewip_manifest_existing COMMON_CONTROLS_V6 gamewip_manifest_common_index)
    list(FIND gamewip_manifest_existing PER_MONITOR_V2 gamewip_manifest_dpi_index)
    set(GAMEWIP_APPLICATION_COMMON_CONTROLS_V6 "")
    set(GAMEWIP_APPLICATION_PER_MONITOR_V2 "")
    if(NOT gamewip_manifest_common_index EQUAL -1)
        set(GAMEWIP_APPLICATION_COMMON_CONTROLS_V6
            "  <dependency>\n    <dependentAssembly>\n      <assemblyIdentity type=\"win32\" name=\"Microsoft.Windows.Common-Controls\" version=\"6.0.0.0\" processorArchitecture=\"*\" publicKeyToken=\"6595b64144ccf1df\" language=\"*\" />\n    </dependentAssembly>\n  </dependency>"
        )
    endif()
    if(NOT gamewip_manifest_dpi_index EQUAL -1)
        set(GAMEWIP_APPLICATION_PER_MONITOR_V2
            "  <application xmlns=\"urn:schemas-microsoft-com:asm.v3\">\n    <windowsSettings>\n      <dpiAwareness xmlns=\"http://schemas.microsoft.com/SMI/2016/WindowsSettings\">PerMonitorV2</dpiAwareness>\n      <dpiAware xmlns=\"http://schemas.microsoft.com/SMI/2005/WindowsSettings\">true/pm</dpiAware>\n    </windowsSettings>\n  </application>"
        )
    endif()

    string(MAKE_C_IDENTIFIER "${gamewip_manifest_TARGET}" gamewip_manifest_stem)
    get_property(gamewip_manifest_path TARGET "${gamewip_manifest_TARGET}" PROPERTY GAMEWIP_APPLICATION_MANIFEST_PATH)
    if(gamewip_manifest_path)
        get_filename_component(gamewip_manifest_dir "${gamewip_manifest_path}" DIRECTORY)
        get_property(gamewip_manifest_rc TARGET "${gamewip_manifest_TARGET}" PROPERTY GAMEWIP_APPLICATION_MANIFEST_RC)
    else()
        set(gamewip_manifest_dir "${CMAKE_CURRENT_BINARY_DIR}/gamewip_application_manifests")
        set(gamewip_manifest_path "${gamewip_manifest_dir}/${gamewip_manifest_stem}.manifest")
        set(gamewip_manifest_rc "${gamewip_manifest_dir}/${gamewip_manifest_stem}.rc")
        set_property(TARGET "${gamewip_manifest_TARGET}" PROPERTY GAMEWIP_APPLICATION_MANIFEST_PATH "${gamewip_manifest_path}")
        set_property(TARGET "${gamewip_manifest_TARGET}" PROPERTY GAMEWIP_APPLICATION_MANIFEST_RC "${gamewip_manifest_rc}")
    endif()
    set(GAMEWIP_APPLICATION_MANIFEST "${gamewip_manifest_path}")
    file(MAKE_DIRECTORY "${gamewip_manifest_dir}")
    configure_file("${GAMEWIP_APPLICATION_MANIFEST_TEMPLATE}" "${gamewip_manifest_path}" @ONLY)
    configure_file("${GAMEWIP_APPLICATION_RC_TEMPLATE}" "${gamewip_manifest_rc}" @ONLY)

    get_property(gamewip_manifest_attached TARGET "${gamewip_manifest_TARGET}" PROPERTY GAMEWIP_APPLICATION_MANIFEST_ATTACHED)
    if(NOT gamewip_manifest_attached)
        target_sources("${gamewip_manifest_TARGET}" PRIVATE "${gamewip_manifest_rc}")
        set_property(TARGET "${gamewip_manifest_TARGET}" PROPERTY GAMEWIP_APPLICATION_MANIFEST_ATTACHED TRUE)
    endif()
    set_property(TARGET "${gamewip_manifest_TARGET}" PROPERTY GAMEWIP_APPLICATION_MANIFEST_REQUIREMENTS "${gamewip_manifest_existing}")
endfunction()
