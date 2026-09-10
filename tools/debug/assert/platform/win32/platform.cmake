# Own Assert's Win32 runtime, Common Controls resource, library, and installed resource templates.
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/AssertCommonControls.cmake")

if(ASSERT_RUNTIME_TARGET_ENABLED)
    target_sources(Assert PRIVATE "${CMAKE_CURRENT_LIST_DIR}/win32_assert.cpp")
endif()

if(ASSERT_ENABLE_COMMON_CONTROLS_MANIFEST)
    set(ASSERT_INTERNAL_COMMON_CONTROLS_MANIFEST "${CMAKE_CURRENT_LIST_DIR}/../../cmake/common_controls_v6.manifest")

    set(ASSERT_INTERNAL_COMMON_CONTROLS_RC "${CMAKE_CURRENT_BINARY_DIR}/common_controls_v6.rc")

    configure_file("${CMAKE_CURRENT_LIST_DIR}/../../cmake/common_controls_v6.rc.in" "${ASSERT_INTERNAL_COMMON_CONTROLS_RC}" @ONLY)

    # The helper can be called from another directory after backend configuration.
    # The global property preserves the generated resource path beyond this directory scope.
    set_property(GLOBAL PROPERTY ASSERT_INTERNAL_COMMON_CONTROLS_RC "${ASSERT_INTERNAL_COMMON_CONTROLS_RC}")
endif()

if(ASSERT_RUNTIME_TARGET_ENABLED)
    target_link_libraries(Assert PRIVATE comctl32)
endif()

# Installed consumers configure their own resource, independently of source-tree preparation.
install(
    FILES "${CMAKE_CURRENT_LIST_DIR}/../../cmake/common_controls_v6.manifest" "${CMAKE_CURRENT_LIST_DIR}/../../cmake/common_controls_v6.rc.in"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/Assert
)
