# Foundation libraries are layered from portable primitives to platform-facing
# services. Keep this order aligned with their link dependencies.
add_subdirectory(base)
add_subdirectory(unicode)

add_subdirectory(io)

# Terminal test hooks are forced on by the test presets so its internal
# failure-path checks are compiled into the same target that the test runner exercises.
set(TERMINAL_ENABLE_TEST_HOOKS ${GAMEWIP_TESTS_REQUIRED} CACHE BOOL "Enable Terminal internal test hooks" FORCE)
add_subdirectory(terminal)

set(FILESYSTEM_ENABLE_TEST_HOOKS ${GAMEWIP_TESTS_REQUIRED} CACHE BOOL "Enable FileSystem internal test hooks" FORCE)
add_subdirectory(filesystem)
