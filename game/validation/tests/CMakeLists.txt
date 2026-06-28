add_library(GameWIPTestModules OBJECT
    registry.cpp
    runner.cpp
)
target_compile_features(GameWIPTestModules PUBLIC cxx_std_23)
target_include_directories(GameWIPTestModules PUBLIC "${PROJECT_SOURCE_DIR}/game")

if(GAMEWIP_BUILD_TESTS)
    add_executable(GameWIPTests standalone_main.cpp)
    target_compile_features(GameWIPTests PRIVATE cxx_std_23)
    target_link_libraries(GameWIPTests PRIVATE
        GameWIPTestModules
        Assert
    )
    set_target_properties(GameWIPTests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    )
    gamewip_copy_runtime_dependencies(GameWIPTests)
endif()

gamewip_add_validation_module_directories("${CMAKE_CURRENT_SOURCE_DIR}")
