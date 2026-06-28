if(GAMEWIP_BUILD_GAME)
    add_executable(GameWIP
        main.cpp
        runtime/game.cpp
    )

    target_compile_features(GameWIP PRIVATE cxx_std_23)
    target_compile_definitions(GameWIP PRIVATE
        GAMEWIP_STARTUP_TESTS_ENABLED=$<BOOL:${GAMEWIP_RUN_TESTS_AT_STARTUP}>
        GAMEWIP_STARTUP_BENCHMARKS_ENABLED=$<BOOL:${GAMEWIP_RUN_BENCHMARKS_AT_STARTUP}>
    )
    target_include_directories(GameWIP PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(GameWIP PRIVATE
        IO
        FileSystem
        Terminal
        Logger
        Assert
        Input
        Window
        WindowManager
        Action
        Tracy::TracyClient
    )
    set_target_properties(GameWIP PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    )

    if(GAMEWIP_ENABLE_TOOLS)
        target_compile_definitions(GameWIP PRIVATE GAMEWIP_ENABLE_TOOLS=1)
        if(GAMEWIP_OPEN_TOOLS_AT_STARTUP)
            target_compile_definitions(GameWIP PRIVATE GAMEWIP_OPEN_TOOLS_AT_STARTUP=1)
        endif()
    endif()

    gamewip_copy_runtime_dependencies(GameWIP)
endif()

if(GAMEWIP_TESTS_REQUIRED OR GAMEWIP_BENCHMARKS_REQUIRED)
    add_subdirectory(validation)
endif()

if(GAMEWIP_RUN_TESTS_AT_STARTUP)
    target_link_libraries(GameWIP PRIVATE GameWIPTestModules)
endif()

if(GAMEWIP_RUN_BENCHMARKS_AT_STARTUP)
    target_link_libraries(GameWIP PRIVATE GameWIPBenchmarkModules)
endif()
