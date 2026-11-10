# Action and WindowManager are retained, unbuilt reference sources. Desktop and
# Input are maintained, independently consumable engine libraries.

add_subdirectory(input)

# Test presets force Desktop hooks on so validation exercises deterministic
# failure paths in the same target configuration used by the test runners.
set(DESKTOP_ENABLE_TEST_HOOKS ${GAMEWIP_TESTS_REQUIRED} CACHE BOOL "Enable Desktop internal test hooks" FORCE)
add_subdirectory(desktop)
