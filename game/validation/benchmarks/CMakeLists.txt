add_library(GameWIPBenchmarkModules OBJECT runner.cpp)
target_compile_features(GameWIPBenchmarkModules PUBLIC cxx_std_23)
target_include_directories(GameWIPBenchmarkModules PUBLIC "${PROJECT_SOURCE_DIR}/game")
target_link_libraries(GameWIPBenchmarkModules PUBLIC benchmark::benchmark)

if(GAMEWIP_BUILD_BENCHMARKS)
    add_executable(GameWIPBenchmarks standalone_main.cpp)
    target_compile_features(GameWIPBenchmarks PRIVATE cxx_std_23)
    target_link_libraries(GameWIPBenchmarks PRIVATE GameWIPBenchmarkModules Assert)
    set_target_properties(GameWIPBenchmarks PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    gamewip_copy_runtime_dependencies(GameWIPBenchmarks)
endif()

gamewip_add_validation_module_directories("${CMAKE_CURRENT_SOURCE_DIR}")
