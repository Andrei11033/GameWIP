if(GAMEWIP_BUILD_GAME)
    # Generate the build identity before compiling the executable that embeds it.
    add_custom_target(
        GameWIPVersionHeader
        COMMAND
            "${CMAKE_COMMAND}" "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}" "-DBINARY_DIR=${PROJECT_BINARY_DIR}" "-DPROJECT_VERSION_VALUE=${PROJECT_VERSION}"
            -P "${PROJECT_SOURCE_DIR}/cmake/GenerateGameWIPVersionHeader.cmake"
        BYPRODUCTS "${PROJECT_BINARY_DIR}/generated/gamewip/version.h"
        COMMENT "Refreshing generated GameWIP build identity"
        VERBATIM
    )

    add_executable(GameWIP main.cpp runtime/game.cpp)
    add_dependencies(GameWIP GameWIPVersionHeader)

    # Keep executable-only compile settings and generated include paths local to GameWIP.
    target_compile_features(GameWIP PRIVATE cxx_std_23)
    target_compile_definitions(
        GameWIP
        PRIVATE
            GAMEWIP_STARTUP_TESTS_ENABLED=$<BOOL:${GAMEWIP_ENABLE_STARTUP_TESTS}>
            GAMEWIP_STARTUP_BENCHMARKS_ENABLED=$<BOOL:${GAMEWIP_RUN_BENCHMARKS_AT_STARTUP}>
            GAMEWIP_TRACY_ENABLED=$<BOOL:${GAMEWIP_ENABLE_TRACY}>
    )
    target_include_directories(GameWIP PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${PROJECT_BINARY_DIR}/generated")

    # Tracy is optional; the runtime and validation targets remain usable without it.
    if(GAMEWIP_ENABLE_TRACY)
        target_link_libraries(GameWIP PRIVATE Tracy::TracyClient)
    endif()

    target_link_libraries(GameWIP PRIVATE GameWIP::Logger GameWIP::Desktop)

    set_target_properties(GameWIP PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

    # Package the shared-library runtime beside the executable for local runs and tests.
    gamewip_copy_runtime_dependencies(GameWIP)

    if(GAMEWIP_BUILD_TESTS)
        # Utility invocations are tested independently so metadata checks do not start the runtime.
        add_test(
            NAME validation.game.version
            COMMAND
                "${CMAKE_COMMAND}" "-DEXECUTABLE=$<TARGET_FILE:GameWIP>" "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}"
                "-DPROJECT_VERSION_VALUE=${PROJECT_VERSION}" -P "${PROJECT_SOURCE_DIR}/cmake/ValidateGameWIPVersionOutput.cmake"
        )
        add_test(NAME validation.game.help COMMAND "$<TARGET_FILE:GameWIP>" --help)
        set_tests_properties(validation.game.help PROPERTIES PASS_REGULAR_EXPRESSION "Usage:;GameWIP.exe --version" TIMEOUT 60)
        set_tests_properties(validation.game.version PROPERTIES TIMEOUT 60)
    endif()
endif()

# Validation modules are object libraries shared by standalone and startup runners.
if(GAMEWIP_TESTS_REQUIRED OR GAMEWIP_BENCHMARKS_REQUIRED)
    add_subdirectory(validation)
endif()

if(GAMEWIP_ENABLE_STARTUP_TESTS)
    target_link_libraries(GameWIP PRIVATE GameWIPTestModules)
endif()

if(GAMEWIP_RUN_BENCHMARKS_AT_STARTUP)
    target_link_libraries(GameWIP PRIVATE GameWIPBenchmarkModules)
endif()

if(GAMEWIP_BUILD_GAME)
    # Desktop requires process-wide DPI policy; Assert's preferred dialog uses Common Controls v6.
    gamewip_attach_application_manifest(TARGET GameWIP COMMON_CONTROLS_V6 PER_MONITOR_V2)
endif()
