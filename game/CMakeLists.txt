if(GAMEWIP_BUILD_GAME)
    add_custom_target(GameWIPVersionHeader
        COMMAND "${CMAKE_COMMAND}"
            "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}"
            "-DBINARY_DIR=${PROJECT_BINARY_DIR}"
            "-DPROJECT_VERSION_VALUE=${PROJECT_VERSION}"
            -P "${PROJECT_SOURCE_DIR}/cmake/GenerateGameWIPVersionHeader.cmake"
        BYPRODUCTS "${PROJECT_BINARY_DIR}/generated/gamewip/version.h"
        COMMENT "Refreshing generated GameWIP build identity"
        VERBATIM
    )

    add_executable(GameWIP
        main.cpp
        runtime/game.cpp
    )
    add_dependencies(GameWIP GameWIPVersionHeader)

    target_compile_features(GameWIP PRIVATE cxx_std_23)
    target_compile_definitions(GameWIP PRIVATE
        GAMEWIP_STARTUP_TESTS_ENABLED=$<BOOL:${GAMEWIP_RUN_TESTS_AT_STARTUP}>
        GAMEWIP_STARTUP_BENCHMARKS_ENABLED=$<BOOL:${GAMEWIP_RUN_BENCHMARKS_AT_STARTUP}>
        GAMEWIP_TRACY_ENABLED=$<BOOL:${GAMEWIP_ENABLE_TRACY}>
    )
    target_include_directories(GameWIP PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${PROJECT_BINARY_DIR}/generated"
    )

    if(GAMEWIP_ENABLE_TRACY)
        target_link_libraries(GameWIP PRIVATE
            Tracy::TracyClient
        )
    endif()

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

    if(GAMEWIP_BUILD_TESTS)
        add_test(
            NAME validation.game.version
            COMMAND "${CMAKE_COMMAND}"
                "-DEXECUTABLE=$<TARGET_FILE:GameWIP>"
                "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}"
                "-DPROJECT_VERSION_VALUE=${PROJECT_VERSION}"
                -P "${PROJECT_SOURCE_DIR}/cmake/ValidateGameWIPVersionOutput.cmake"
        )
    endif()
endif()

if(GAMEWIP_TESTS_REQUIRED OR GAMEWIP_BENCHMARKS_REQUIRED)
    add_subdirectory(validation)
endif()

if(GAMEWIP_RUN_TESTS_AT_STARTUP)
    # Assert test objects use AssertCompileInterface to avoid attaching
    # executable resources to the object library. Each runner links the
    # concrete Assert runtime explicitly.
    target_link_libraries(GameWIP PRIVATE
        GameWIPTestModules
        Assert
    )
endif()

if(GAMEWIP_RUN_BENCHMARKS_AT_STARTUP)
    target_link_libraries(GameWIP PRIVATE GameWIPBenchmarkModules)
endif()
