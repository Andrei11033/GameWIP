set(TRACY_ENABLE ${GAMEWIP_ENABLE_TRACY} CACHE BOOL "" FORCE)
set(TRACY_ON_DEMAND OFF CACHE BOOL "" FORCE)
add_subdirectory(tracy EXCLUDE_FROM_ALL)

if(GAMEWIP_BENCHMARKS_REQUIRED)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/benchmark/CMakeLists.txt")
        message(FATAL_ERROR "Benchmarks require the Google Benchmark submodule. Run: git submodule update --init --recursive")
    endif()

    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    add_subdirectory(benchmark EXCLUDE_FROM_ALL)
endif()
