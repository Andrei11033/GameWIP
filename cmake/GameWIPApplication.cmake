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
# Windows callers must enable RC at their top-level directory before adding consumers.
# TARGET must be a local executable, not an imported target or alias. Invalid
# arguments or missing language setup stop configuration with a fatal error.
# Generated files stay in the target's binary directory; source and installed
# consumers use the templates beside this module without caller-scope variables.

function(gamewip_attach_application_manifest)
    cmake_parse_arguments(PARSE_ARGV 0 gamewip_manifest "COMMON_CONTROLS_V6;PER_MONITOR_V2" "TARGET" "")

    if(gamewip_manifest_UNPARSED_ARGUMENTS OR NOT gamewip_manifest_TARGET)
        message(FATAL_ERROR "gamewip_attach_application_manifest requires TARGET <executable> and one or more manifest requirements.")
    endif()
    if(NOT TARGET "${gamewip_manifest_TARGET}")
        message(FATAL_ERROR "Application manifest target does not exist: ${gamewip_manifest_TARGET}")
    endif()

    get_target_property(gamewip_manifest_type "${gamewip_manifest_TARGET}" TYPE)
    get_target_property(gamewip_manifest_imported "${gamewip_manifest_TARGET}" IMPORTED)
    get_target_property(gamewip_manifest_alias "${gamewip_manifest_TARGET}" ALIASED_TARGET)
    if(NOT gamewip_manifest_type STREQUAL "EXECUTABLE" OR gamewip_manifest_imported OR gamewip_manifest_alias)
        message(FATAL_ERROR "Application manifest target must be a local executable, not an alias: ${gamewip_manifest_TARGET}")
    endif()
    if(NOT gamewip_manifest_COMMON_CONTROLS_V6 AND NOT gamewip_manifest_PER_MONITOR_V2)
        message(FATAL_ERROR "gamewip_attach_application_manifest requires COMMON_CONTROLS_V6 or PER_MONITOR_V2.")
    endif()

    # Retain independent requests so separate application setup steps can compose policy.
    get_property(gamewip_manifest_existing TARGET "${gamewip_manifest_TARGET}" PROPERTY GAMEWIP_APPLICATION_MANIFEST_REQUIREMENTS)
    if(gamewip_manifest_COMMON_CONTROLS_V6)
        list(APPEND gamewip_manifest_existing COMMON_CONTROLS_V6)
    endif()
    if(gamewip_manifest_PER_MONITOR_V2)
        list(APPEND gamewip_manifest_existing PER_MONITOR_V2)
    endif()
    list(REMOVE_DUPLICATES gamewip_manifest_existing)
    list(SORT gamewip_manifest_existing)

    if(NOT WIN32)
        set_property(TARGET "${gamewip_manifest_TARGET}" PROPERTY GAMEWIP_APPLICATION_MANIFEST_REQUIREMENTS "${gamewip_manifest_existing}")
        return()
    endif()

    # CMake requires enable_language in file scope at the common ancestor of
    # every consumer. Enabling RC inside this function loses that setup on return.
    if(NOT CMAKE_RC_COMPILER_LOADED)
        message(FATAL_ERROR "Enable RC with enable_language(RC) at the application's top-level directory before attaching a manifest.")
    endif()

    set(GAMEWIP_APPLICATION_COMMON_CONTROLS_V6 "")
    set(GAMEWIP_APPLICATION_PER_MONITOR_V2 "")
    if("COMMON_CONTROLS_V6" IN_LIST gamewip_manifest_existing)
        set(GAMEWIP_APPLICATION_COMMON_CONTROLS_V6
            [=[
  <dependency>
    <dependentAssembly>
      <assemblyIdentity type="win32" name="Microsoft.Windows.Common-Controls"
        version="6.0.0.0" processorArchitecture="*"
        publicKeyToken="6595b64144ccf1df" language="*" />
    </dependentAssembly>
  </dependency>]=]
        )
    endif()
    if("PER_MONITOR_V2" IN_LIST gamewip_manifest_existing)
        set(GAMEWIP_APPLICATION_PER_MONITOR_V2
            [=[
  <application xmlns="urn:schemas-microsoft-com:asm.v3">
    <windowsSettings>
      <dpiAwareness xmlns="http://schemas.microsoft.com/SMI/2016/WindowsSettings">PerMonitorV2</dpiAwareness>
      <dpiAware xmlns="http://schemas.microsoft.com/SMI/2005/WindowsSettings">true/pm</dpiAware>
    </windowsSettings>
  </application>]=]
        )
    endif()

    # Target names are already valid path components. Preserve punctuation so
    # distinct names such as app-one and app_one never overwrite each other.
    get_target_property(gamewip_manifest_binary_dir "${gamewip_manifest_TARGET}" BINARY_DIR)
    set(gamewip_manifest_dir "${gamewip_manifest_binary_dir}/gamewip_application_manifests/${gamewip_manifest_TARGET}")
    set(gamewip_manifest_path "${gamewip_manifest_dir}/application.manifest")
    set(gamewip_manifest_rc "${gamewip_manifest_dir}/application.rc")
    set(GAMEWIP_APPLICATION_MANIFEST "${gamewip_manifest_path}")
    file(MAKE_DIRECTORY "${gamewip_manifest_dir}")
    configure_file("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/application_manifest.manifest.in" "${gamewip_manifest_path}" @ONLY)
    configure_file("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/application_manifest.rc.in" "${gamewip_manifest_rc}" @ONLY)

    get_property(gamewip_manifest_attached TARGET "${gamewip_manifest_TARGET}" PROPERTY GAMEWIP_APPLICATION_MANIFEST_ATTACHED)
    if(NOT gamewip_manifest_attached)
        target_sources("${gamewip_manifest_TARGET}" PRIVATE "${gamewip_manifest_rc}")
        set_property(TARGET "${gamewip_manifest_TARGET}" PROPERTY GAMEWIP_APPLICATION_MANIFEST_ATTACHED TRUE)
    endif()
    set_property(TARGET "${gamewip_manifest_TARGET}" PROPERTY GAMEWIP_APPLICATION_MANIFEST_PATH "${gamewip_manifest_path}")
    set_property(TARGET "${gamewip_manifest_TARGET}" PROPERTY GAMEWIP_APPLICATION_MANIFEST_RC "${gamewip_manifest_rc}")
    set_property(TARGET "${gamewip_manifest_TARGET}" PROPERTY GAMEWIP_APPLICATION_MANIFEST_REQUIREMENTS "${gamewip_manifest_existing}")
endfunction()
