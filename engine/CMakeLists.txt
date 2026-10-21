# Input and Action are retained, unbuilt reference sources. Desktop is the
# maintained, independently consumable engine library.

# Test presets force Desktop hooks on so validation exercises deterministic
# failure paths in the same target configuration used by the test runners.
set(DESKTOP_ENABLE_TEST_HOOKS ${GAMEWIP_TESTS_REQUIRED} CACHE BOOL "Enable Desktop internal test hooks" FORCE)
add_subdirectory(desktop)

# WindowManager is retained as reference source and is not compiled.
