if(GAMEWIP_TESTS_REQUIRED)
    add_subdirectory(public_headers)
    add_subdirectory(tests)
endif()

if(GAMEWIP_BENCHMARKS_REQUIRED)
    add_subdirectory(benchmarks)
endif()
