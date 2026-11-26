# Foundation libraries are layered from portable primitives to platform-facing
# services. Keep this order aligned with their link dependencies.
add_subdirectory(status)
add_subdirectory(base)
add_subdirectory(unicode)

add_subdirectory(io)

# Enable Terminal and FileSystem failure-path hooks in validation builds.
set(TERMINAL_ENABLE_TEST_HOOKS ${GAMEWIP_TESTS_REQUIRED} CACHE BOOL "Enable Terminal internal test hooks" FORCE)
add_subdirectory(terminal)
set(FILESYSTEM_ENABLE_TEST_HOOKS ${GAMEWIP_TESTS_REQUIRED} CACHE BOOL "Enable FileSystem internal test hooks" FORCE)
add_subdirectory(filesystem)
