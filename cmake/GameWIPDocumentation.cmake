if(GAMEWIP_BUILD_DOCS)
    find_package(Doxygen QUIET)
    if(NOT DOXYGEN_FOUND)
        message(FATAL_ERROR
            "GAMEWIP_BUILD_DOCS is ON, but Doxygen was not found. Install Doxygen or disable documentation."
        )
    endif()

    include(LibraryDoxygen)

    set(GAMEWIP_DOXYGEN_ROOT "${PROJECT_SOURCE_DIR}/docs/doxygen")
    set(GAMEWIP_DOXYGEN_MAINPAGE "${GAMEWIP_DOXYGEN_ROOT}/index.md")

    gamewip_register_doxygen_inputs(
        "${GAMEWIP_DOXYGEN_ROOT}/index.md"
        "${GAMEWIP_DOXYGEN_ROOT}/getting_started.md"
        "${GAMEWIP_DOXYGEN_ROOT}/environment_setup.md"
        "${GAMEWIP_DOXYGEN_ROOT}/project_reusable_libraries.md"
        "${GAMEWIP_DOXYGEN_ROOT}/project_manual.md"
        "${GAMEWIP_DOXYGEN_ROOT}/project_contracts.md"
        "${GAMEWIP_DOXYGEN_ROOT}/project_public_api_contract.md"
        "${GAMEWIP_DOXYGEN_ROOT}/project_quality_workflows.md"
        "${GAMEWIP_DOXYGEN_ROOT}/project_planning.md"
        "${GAMEWIP_DOXYGEN_ROOT}/project_structure.md"
        "${GAMEWIP_DOXYGEN_ROOT}/command_line_tools.md"
        "${GAMEWIP_DOXYGEN_ROOT}/game_executable.md"
        "${GAMEWIP_DOXYGEN_ROOT}/build.md"
        "${GAMEWIP_DOXYGEN_ROOT}/library_compatibility.md"
        "${GAMEWIP_DOXYGEN_ROOT}/validation.md"
        "${GAMEWIP_DOXYGEN_ROOT}/testing.md"
        "${GAMEWIP_DOXYGEN_ROOT}/benchmarking.md"
        "${GAMEWIP_DOXYGEN_ROOT}/profiling.md"
        "${GAMEWIP_DOXYGEN_ROOT}/coverage.md"
        "${GAMEWIP_DOXYGEN_ROOT}/static_analysis.md"
        "${GAMEWIP_DOXYGEN_ROOT}/repository_automation.md"
        "${GAMEWIP_DOXYGEN_ROOT}/repository_maintenance.md"
        "${GAMEWIP_DOXYGEN_ROOT}/release_automation.md"
        "${GAMEWIP_DOXYGEN_ROOT}/documentation.md"
        "${GAMEWIP_DOXYGEN_ROOT}/extending.md"
        "${GAMEWIP_DOXYGEN_ROOT}/cmake_infrastructure.md"
        "${PROJECT_SOURCE_DIR}/docs/contributing.md"
        "${PROJECT_SOURCE_DIR}/docs/decisions.md"
        "${GAMEWIP_DOXYGEN_ROOT}/platform_backend_contract.md"
        "${PROJECT_SOURCE_DIR}/foundation/base/docs/base.md"
        "${PROJECT_SOURCE_DIR}/docs/roadmap.md"
        "${PROJECT_SOURCE_DIR}/docs/versioning.md"
        "${PROJECT_SOURCE_DIR}/docs/vision.md"

        # Executable-owned source interfaces are documented for contributors and
        # maintainers. They are not installed consumer APIs.
        "${PROJECT_SOURCE_DIR}/game/runtime/game.h"
        "${PROJECT_BINARY_DIR}/generated/gamewip/version.h"
        "${PROJECT_SOURCE_DIR}/game/validation/types.h"
        "${PROJECT_SOURCE_DIR}/game/validation/validation.h"
        "${PROJECT_SOURCE_DIR}/game/validation/benchmarks/runner.h"
        "${PROJECT_SOURCE_DIR}/game/validation/tests/runner.h"
        "${PROJECT_SOURCE_DIR}/game/validation/tests/registry.h"
        "${PROJECT_SOURCE_DIR}/game/validation/tests/assert/assert_test.h"
        "${PROJECT_SOURCE_DIR}/game/validation/tests/base/base_test.h"
        "${PROJECT_SOURCE_DIR}/game/validation/tests/filesystem/filesystem_test.h"
        "${PROJECT_SOURCE_DIR}/game/validation/tests/io/io_test.h"
        "${PROJECT_SOURCE_DIR}/game/validation/tests/logger/logger_test.h"
        "${PROJECT_SOURCE_DIR}/game/validation/tests/runner/runner_test.h"
        "${PROJECT_SOURCE_DIR}/game/validation/tests/terminal/terminal_test.h"
        "${PROJECT_SOURCE_DIR}/game/validation/tests/window/window_test.h"
        "${PROJECT_SOURCE_DIR}/game/validation/tests/test_support/test_support_test.h"
    )
endif()

function(gamewip_create_documentation_target)
    if(GAMEWIP_BUILD_DOCS)
        gamewip_create_doxygen_target(MAINPAGE "${GAMEWIP_DOXYGEN_MAINPAGE}")
    endif()
endfunction()
