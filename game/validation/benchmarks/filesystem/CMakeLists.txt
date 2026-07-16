target_sources(GameWIPBenchmarkModules PRIVATE
    filesystem_benchmark.cpp
)
target_link_libraries(GameWIPBenchmarkModules PUBLIC
    FileSystem
    TestSupport
)
