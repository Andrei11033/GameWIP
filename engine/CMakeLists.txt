add_subdirectory(input)
add_subdirectory(action)

set(WINDOW_ENABLE_TEST_HOOKS ${GAMEWIP_TESTS_REQUIRED}
    CACHE BOOL "Enable Window internal test hooks" FORCE
)
add_subdirectory(window)

# WindowManager still targets the retired Window namespace/class surface. Its source is
# intentionally preserved for the later coordination-layer migration, but it is not compiled
# while Window establishes the standalone public boundary from issue #26.
