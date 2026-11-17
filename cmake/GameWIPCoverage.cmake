# Provides first-party coverage instrumentation and the gcovr report target.
# Call gamewip_enable_coverage() after external targets and before GameWIP targets.

set(GAMEWIP_COVERAGE_COMPILER_SUPPORTED OFF)
if(GAMEWIP_ENABLE_COVERAGE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        set(GAMEWIP_COVERAGE_COMPILER_SUPPORTED ON)
    else()
        message(WARNING "GAMEWIP_ENABLE_COVERAGE is configured only for GCC and Clang toolchains.")
    endif()

    set(GAMEWIP_COVERAGE_OUTPUT_DIR "${CMAKE_BINARY_DIR}/coverage")
    find_program(GAMEWIP_GCOVR_EXECUTABLE gcovr)

    get_filename_component(GAMEWIP_COMPILER_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        # Clang writes gcov data in LLVM's format, so use its matching frontend.
        find_program(GAMEWIP_LLVM_COV_EXECUTABLE NAMES llvm-cov HINTS "${GAMEWIP_COMPILER_BIN_DIR}")
        if(GAMEWIP_LLVM_COV_EXECUTABLE)
            set(GAMEWIP_GCOV_COMMAND "\"${GAMEWIP_LLVM_COV_EXECUTABLE}\" gcov")
        endif()
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        find_program(GAMEWIP_GCOV_EXECUTABLE NAMES gcov HINTS "${GAMEWIP_COMPILER_BIN_DIR}")
        if(GAMEWIP_GCOV_EXECUTABLE)
            set(GAMEWIP_GCOV_COMMAND "${GAMEWIP_GCOV_EXECUTABLE}")
        endif()
    endif()
endif()

function(gamewip_enable_coverage)
    if(NOT GAMEWIP_COVERAGE_COMPILER_SUPPORTED)
        return()
    endif()

    add_compile_options(--coverage -O0 -g)
    add_link_options(--coverage)

    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        add_compile_options(-fprofile-update=atomic)
    endif()
endfunction()

function(gamewip_create_coverage_target)
    if(NOT GAMEWIP_ENABLE_COVERAGE)
        return()
    endif()

    if(GAMEWIP_GCOVR_EXECUTABLE AND GAMEWIP_GCOV_COMMAND)
        set(gamewip_coverage_common_args
            --root
            "${PROJECT_SOURCE_DIR}"
            --merge-mode-functions
            merge-use-line-min
            --exclude
            "${PROJECT_SOURCE_DIR}/external"
            "${CMAKE_BINARY_DIR}"
        )
        set(gamewip_coverage_library_args
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
            "${PROJECT_SOURCE_DIR}/engine/desktop"
            --filter
            "${PROJECT_SOURCE_DIR}/engine/input"
        )

        add_custom_target(
            coverage
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${GAMEWIP_COVERAGE_OUTPUT_DIR}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${GAMEWIP_COVERAGE_OUTPUT_DIR}/tests"
            COMMAND
                "${GAMEWIP_GCOVR_EXECUTABLE}" --gcov-executable "${GAMEWIP_GCOV_COMMAND}" ${gamewip_coverage_common_args}
                ${gamewip_coverage_library_args} --html-details --output "${GAMEWIP_COVERAGE_OUTPUT_DIR}/index.html"
            COMMAND
                "${GAMEWIP_GCOVR_EXECUTABLE}" --gcov-executable "${GAMEWIP_GCOV_COMMAND}" ${gamewip_coverage_common_args}
                ${gamewip_coverage_library_args} --xml-pretty --output "${GAMEWIP_COVERAGE_OUTPUT_DIR}/coverage.xml"
            COMMAND
                "${GAMEWIP_GCOVR_EXECUTABLE}" --gcov-executable "${GAMEWIP_GCOV_COMMAND}" ${gamewip_coverage_common_args} --filter
                "${PROJECT_SOURCE_DIR}/game/validation/tests" --html-details --output "${GAMEWIP_COVERAGE_OUTPUT_DIR}/tests/index.html"
            COMMAND
                "${GAMEWIP_GCOVR_EXECUTABLE}" --gcov-executable "${GAMEWIP_GCOV_COMMAND}" ${gamewip_coverage_common_args} --filter
                "${PROJECT_SOURCE_DIR}/game/validation/tests" --xml-pretty --output "${GAMEWIP_COVERAGE_OUTPUT_DIR}/tests/coverage.xml"
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
            COMMENT "Generating GameWIP coverage reports"
            VERBATIM
        )
    else()
        add_custom_target(
            coverage
            COMMAND ${CMAKE_COMMAND} -E echo "Coverage reporting requires gcovr and a matching gcov frontend."
            COMMAND ${CMAKE_COMMAND} -E echo "Install gcovr and, for Clang, llvm-cov."
            COMMAND ${CMAKE_COMMAND} -E false
            COMMENT "Coverage report unavailable because required tools are missing"
            VERBATIM
        )
    endif()
endfunction()
