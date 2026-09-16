# Action consumes Input. Both are deprecated source-tree targets; Desktop is the
# independently consumable, supported engine boundary.
set(INPUT_ENABLE_TEST_HOOKS ${GAMEWIP_TESTS_REQUIRED} CACHE BOOL "Enable Input internal test hooks" FORCE)
add_subdirectory(input)
add_subdirectory(action)

# Test presets force Desktop hooks on so validation exercises deterministic
# failure paths in the same target configuration used by the test runners.
set(DESKTOP_ENABLE_TEST_HOOKS ${GAMEWIP_TESTS_REQUIRED} CACHE BOOL "Enable Desktop internal test hooks" FORCE)
add_subdirectory(desktop)

# WindowManager is deprecated and intentionally not compiled. Its source is
# preserved for a later coordination-layer migration.
