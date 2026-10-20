if(GAMEWIP_TESTS_REQUIRED OR GAMEWIP_BENCHMARKS_REQUIRED)
    add_library(GameWIPValidationProcess STATIC)
    target_compile_features(GameWIPValidationProcess PUBLIC cxx_std_23)
    target_include_directories(GameWIPValidationProcess PUBLIC "${PROJECT_SOURCE_DIR}/game")
    gamewip_target_platform_backend(TARGET GameWIPValidationProcess ROOT "${CMAKE_CURRENT_SOURCE_DIR}/platform")
endif()

# Public-header checks are part of the test requirement because they validate the installed API boundary.
if(GAMEWIP_TESTS_REQUIRED)
    add_subdirectory(public_headers)
    add_subdirectory(tests)
endif()

# Benchmark modules are configured separately so test-only builds do not require Google Benchmark.
if(GAMEWIP_BENCHMARKS_REQUIRED)
    add_subdirectory(benchmarks)
endif()
