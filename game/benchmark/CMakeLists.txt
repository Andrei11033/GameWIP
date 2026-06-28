add_executable(GameWIPBenchmarks
    assert_benchmark.cpp
)

target_link_libraries(GameWIPBenchmarks PRIVATE
    Assert
    benchmark::benchmark_main
)

target_compile_features(GameWIPBenchmarks PRIVATE cxx_std_23)

set_target_properties(GameWIPBenchmarks PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

gamewip_copy_runtime_dependencies(GameWIPBenchmarks)
