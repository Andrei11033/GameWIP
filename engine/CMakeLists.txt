add_subdirectory(input)
add_subdirectory(action)

set(DESKTOP_ENABLE_TEST_HOOKS ${GAMEWIP_TESTS_REQUIRED} CACHE BOOL "Enable Desktop internal test hooks" FORCE)
add_subdirectory(desktop)

# WindowManager still targets the retired coordination-layer surface. Its source is
# intentionally preserved for the later coordination-layer migration, but it is not compiled
# while Desktop establishes the standalone public boundary.
