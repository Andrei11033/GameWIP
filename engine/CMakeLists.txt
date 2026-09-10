# Action consumes Input. Both contracts remain provisional; Desktop is the
# independently consumable, supported engine boundary.
add_subdirectory(input)
add_subdirectory(action)

# Test presets force Desktop hooks on so validation exercises deterministic
# failure paths in the same target configuration used by the test runners.
set(DESKTOP_ENABLE_TEST_HOOKS ${GAMEWIP_TESTS_REQUIRED} CACHE BOOL "Enable Desktop internal test hooks" FORCE)
add_subdirectory(desktop)

# WindowManager still targets the retired coordination-layer surface. Its source is
# intentionally preserved for the later coordination-layer migration, but it is not compiled
# while Desktop establishes the standalone public boundary.
