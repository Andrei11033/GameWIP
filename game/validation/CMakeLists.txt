# Public-header checks are part of the test requirement because they validate the installed API boundary.
if(GAMEWIP_TESTS_REQUIRED)
    add_subdirectory(public_headers)
    add_subdirectory(tests)
endif()

# Benchmark modules are configured separately so test-only builds do not require Google Benchmark.
if(GAMEWIP_BENCHMARKS_REQUIRED)
    add_subdirectory(benchmarks)
endif()
