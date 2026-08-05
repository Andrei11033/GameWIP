set(IO_ENABLE_TEST_HOOKS ${GAMEWIP_TESTS_REQUIRED}
    CACHE BOOL "Enable IO internal test hooks" FORCE
)
add_subdirectory(io)

set(TERMINAL_ENABLE_TEST_HOOKS ${GAMEWIP_TESTS_REQUIRED}
    CACHE BOOL "Enable Terminal internal test hooks" FORCE
)
add_subdirectory(terminal)

set(FILESYSTEM_ENABLE_TEST_HOOKS ${GAMEWIP_TESTS_REQUIRED}
    CACHE BOOL "Enable FileSystem internal test hooks" FORCE
)
add_subdirectory(filesystem)
