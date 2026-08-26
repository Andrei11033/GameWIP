# Configures informational first-party coverage instrumentation and the gcovr report target.
# The module has no effect unless GAMEWIP_ENABLE_COVERAGE is enabled; unsupported compilers warn rather than fabricate coverage.

if(GAMEWIP_ENABLE_COVERAGE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(--coverage -O0 -g)
        add_link_options(--coverage)
        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            add_compile_options(-fprofile-update=atomic)
        endif()
    else()
        message(WARNING "GAMEWIP_ENABLE_COVERAGE is currently configured only for GCC and Clang toolchains.")
    endif()

    set(GAMEWIP_COVERAGE_OUTPUT_DIR "${CMAKE_BINARY_DIR}/coverage")
    find_program(GAMEWIP_GCOVR_EXECUTABLE gcovr)
endif()

function(gamewip_create_coverage_target)
    if(NOT GAMEWIP_ENABLE_COVERAGE)
        return()
    endif()

    if(GAMEWIP_GCOVR_EXECUTABLE)
        set(gamewip_coverage_common_args
            --root
            "${PROJECT_SOURCE_DIR}"
            --filter
            "${PROJECT_SOURCE_DIR}/foundation/base"
            --filter
            "${PROJECT_SOURCE_DIR}/foundation/unicode"
            --filter
            "${PROJECT_SOURCE_DIR}/foundation/io"
            --filter
            "${PROJECT_SOURCE_DIR}/foundation/terminal"
            --filter
            "${PROJECT_SOURCE_DIR}/foundation/filesystem"
            --filter
            "${PROJECT_SOURCE_DIR}/tools/logger"
            --filter
            "${PROJECT_SOURCE_DIR}/tools/debug/assert"
            --filter
            "${PROJECT_SOURCE_DIR}/tools/test_support"
            --filter
            "${PROJECT_SOURCE_DIR}/engine/window"
            --filter
            "${PROJECT_SOURCE_DIR}/game/validation/tests"
            --exclude
            "${PROJECT_SOURCE_DIR}/external"
        )

        add_custom_target(
            coverage
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${GAMEWIP_COVERAGE_OUTPUT_DIR}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${GAMEWIP_COVERAGE_OUTPUT_DIR}"
            COMMAND "${GAMEWIP_GCOVR_EXECUTABLE}" ${gamewip_coverage_common_args} --html-details --output "${GAMEWIP_COVERAGE_OUTPUT_DIR}/index.html"
            COMMAND "${GAMEWIP_GCOVR_EXECUTABLE}" ${gamewip_coverage_common_args} --xml-pretty --output "${GAMEWIP_COVERAGE_OUTPUT_DIR}/coverage.xml"
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
            COMMENT "Generating GameWIP coverage reports"
            VERBATIM
        )
    else()
        add_custom_target(
            coverage
            COMMAND ${CMAKE_COMMAND} -E echo "GAMEWIP_ENABLE_COVERAGE is ON, but gcovr was not found."
            COMMAND ${CMAKE_COMMAND} -E echo "Install it with: python -m pip install gcovr"
            COMMAND ${CMAKE_COMMAND} -E false
            COMMENT "Coverage report unavailable because gcovr is missing"
            VERBATIM
        )
    endif()
endfunction()
