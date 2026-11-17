# Provides optional third-party dependencies through the shared GameWIP
# FetchContent cache.

if(GAMEWIP_ENABLE_TRACY)
    set(TRACY_ENABLE ON CACHE BOOL "" FORCE)
    set(TRACY_ON_DEMAND OFF CACHE BOOL "" FORCE)

    gamewip_make_dependency_available(tracy)

    if(MINGW AND TARGET TracyClient)
        # Keep diagnostics from the pinned Tracy client local to its target.
        target_compile_options(TracyClient PRIVATE -Wno-format -Wno-unused-value)
    endif()
endif()

if(GAMEWIP_BENCHMARKS_REQUIRED)
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)

    gamewip_make_dependency_available(benchmark)
endif()
