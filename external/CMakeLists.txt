# Provides optional third-party dependencies through the shared GameWIP
# FetchContent cache.

if(GAMEWIP_ENABLE_TRACY)
    set(TRACY_ENABLE ON CACHE BOOL "" FORCE)
    set(TRACY_ON_DEMAND OFF CACHE BOOL "" FORCE)

    gamewip_make_dependency_available(tracy)
endif()

if(GAMEWIP_BENCHMARKS_REQUIRED)
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)

    gamewip_make_dependency_available(benchmark)
endif()
