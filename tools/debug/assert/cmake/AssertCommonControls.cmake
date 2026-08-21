# AssertCommonControls.cmake
#
# Library-local helper for explicit Common Controls v6 attachment in source-tree
# builds. Linking the Assert target alone does not attach this resource.

function(assert_enable_common_controls_v6 target_name)
    if(NOT WIN32)
        return()
    endif()

    if(NOT ASSERT_ENABLE_COMMON_CONTROLS_MANIFEST)
        message(FATAL_ERROR "ASSERT_ENABLE_COMMON_CONTROLS_MANIFEST is OFF, so no Common Controls v6 resource was configured.")
    endif()

    get_property(
        common_controls_rc
        GLOBAL PROPERTY ASSERT_INTERNAL_COMMON_CONTROLS_RC
    )

    if(NOT common_controls_rc)
        message(FATAL_ERROR "Assert Common Controls v6 resource was not configured.")
    endif()

    target_sources("${target_name}" PRIVATE
        "${common_controls_rc}"
    )
endfunction()
