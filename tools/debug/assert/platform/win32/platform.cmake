include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/AssertCommonControls.cmake")

if(ASSERT_RUNTIME_TARGET_ENABLED)
    target_sources(Assert PRIVATE "${CMAKE_CURRENT_LIST_DIR}/win32_assert.cpp")
endif()

if(ASSERT_ENABLE_COMMON_CONTROLS_MANIFEST)
    enable_language(RC)

    set(
        ASSERT_INTERNAL_COMMON_CONTROLS_MANIFEST
        "${CMAKE_CURRENT_LIST_DIR}/../../cmake/common_controls_v6.manifest"
    )

    set(
        ASSERT_INTERNAL_COMMON_CONTROLS_RC
        "${CMAKE_CURRENT_BINARY_DIR}/common_controls_v6.rc"
    )

    configure_file(
        "${CMAKE_CURRENT_LIST_DIR}/../../cmake/common_controls_v6.rc.in"
        "${ASSERT_INTERNAL_COMMON_CONTROLS_RC}"
        @ONLY
    )

    set_property(
        GLOBAL PROPERTY
        ASSERT_INTERNAL_COMMON_CONTROLS_RC
        "${ASSERT_INTERNAL_COMMON_CONTROLS_RC}"
    )

endif()

if(ASSERT_RUNTIME_TARGET_ENABLED)
    target_link_libraries(Assert PRIVATE
        comctl32
    )
endif()
