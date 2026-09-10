# The tools layer owns reusable developer-facing libraries. This file wires the
# libraries together and propagates the top-level test/assertion policy; each
# library owns its own sources, dependencies, and platform backend.

# ------------------------------------------------------------
# Logger
# ------------------------------------------------------------

set(LOGGER_ENABLE_TEST_HOOKS ${GAMEWIP_TESTS_REQUIRED} CACHE BOOL "Enable Logger internal test hooks" FORCE)
add_subdirectory(logger)

# ------------------------------------------------------------
# Assert
# ------------------------------------------------------------

if(GAMEWIP_ENABLE_ASSERTS)
    set(ASSERT_ENABLED ON CACHE STRING "Force assertions on, off, or let NDEBUG choose" FORCE)
    set(ASSERT_CHECKS_ENABLED ON CACHE STRING "Force recoverable checks on, off, or let NDEBUG choose" FORCE)
else()
    set(ASSERT_ENABLED OFF CACHE STRING "Force assertions on, off, or let NDEBUG choose" FORCE)
    set(ASSERT_CHECKS_ENABLED OFF CACHE STRING "Force recoverable checks on, off, or let NDEBUG choose" FORCE)
endif()

set(ASSERT_ENABLE_TEST_HOOKS ${GAMEWIP_TESTS_REQUIRED} CACHE BOOL "Enable Assert internal test hooks" FORCE)
add_subdirectory(debug/assert)

# ------------------------------------------------------------
# TestSupport
# ------------------------------------------------------------

set(TEST_SUPPORT_ENABLE_TEST_HOOKS ${GAMEWIP_TESTS_REQUIRED} CACHE BOOL "Enable TestSupport internal test hooks" FORCE)
add_subdirectory(test_support)
