# Exercises generated GameWIP versions against clean, tagged, and dirty repositories.

cmake_minimum_required(VERSION "${GAMEWIP_CMAKE_MINIMUM_VERSION}")

include("${CMAKE_CURRENT_LIST_DIR}/GameWIPVersion.cmake")

find_package(Git REQUIRED)

function(expect_version expected)
    gamewip_compose_display_version(actual ${ARGN})
    if(NOT actual STREQUAL expected)
        message(FATAL_ERROR "Expected '${expected}', got '${actual}'.")
    endif()
endfunction()

expect_version(
    "0.1.0"
    PROJECT_VERSION
    "0.1.0"
    BUILD_NUMBER
    "247"
    GIT_COMMIT
    "2238b22"
    RELEASE
)
expect_version(
    "0.1.0-dev.247+g2238b22"
    PROJECT_VERSION
    "0.1.0"
    BUILD_NUMBER
    "247"
    GIT_COMMIT
    "2238b22"
)
expect_version(
    "0.1.0-dev.247+g2238b22.dirty"
    PROJECT_VERSION
    "0.1.0"
    BUILD_NUMBER
    "247"
    GIT_COMMIT
    "2238b22"
    RELEASE
    DIRTY
)
expect_version(
    "0.1.0-dev+gunknown"
    PROJECT_VERSION
    "0.1.0"
    BUILD_NUMBER
    "unknown"
    GIT_COMMIT
    "unknown"
)

function(expect_value label actual expected)
    if(NOT actual STREQUAL expected)
        message(FATAL_ERROR "${label}: expected '${expected}', got '${actual}'.")
    endif()
endfunction()

set(test_repository "${CMAKE_CURRENT_BINARY_DIR}/gamewip-version-test-repository")
file(REMOVE_RECURSE "${test_repository}")
file(MAKE_DIRECTORY "${test_repository}")

function(run_test_git)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${test_repository}" ${ARGN}
        RESULT_VARIABLE git_result
        OUTPUT_VARIABLE git_output
        ERROR_VARIABLE git_error
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(NOT git_result EQUAL 0)
        message(FATAL_ERROR "Test Git command failed: ${ARGN}\n" "stdout:\n${git_output}\n" "stderr:\n${git_error}")
    endif()
endfunction()

run_test_git(init --quiet)
run_test_git(config user.name "GameWIP Version Test")
run_test_git(config user.email "version-test@gamewip.invalid")
run_test_git(config commit.gpgsign false)
run_test_git(config tag.gpgsign false)
file(WRITE "${test_repository}/tracked.txt" "version test\n")
run_test_git(add tracked.txt)
run_test_git(commit --quiet -m "version test")

gamewip_detect_version(clean_development SOURCE_DIR "${test_repository}" PROJECT_VERSION "0.1.0")
expect_value("clean development build number" "${clean_development_BUILD_NUMBER}" "1")
expect_value("clean development dirty state" "${clean_development_DIRTY}" "FALSE")
expect_value("clean development release state" "${clean_development_RELEASE}" "FALSE")
expect_value("clean development display" "${clean_development_DISPLAY}" "0.1.0-dev.1+g${clean_development_GIT_COMMIT}")

file(APPEND "${test_repository}/tracked.txt" "dirty\n")
gamewip_detect_version(dirty_development SOURCE_DIR "${test_repository}" PROJECT_VERSION "0.1.0")
expect_value("dirty development dirty state" "${dirty_development_DIRTY}" "TRUE")
expect_value("dirty development display" "${dirty_development_DISPLAY}" "0.1.0-dev.1+g${dirty_development_GIT_COMMIT}.dirty")
run_test_git(restore tracked.txt)

run_test_git(tag v0.1.0)
gamewip_detect_version(lightweight_tag SOURCE_DIR "${test_repository}" PROJECT_VERSION "0.1.0")
expect_value("lightweight tag release state" "${lightweight_tag_RELEASE}" "FALSE")
run_test_git(tag --delete v0.1.0)

run_test_git(
    tag
    --annotate
    v0.1.0
    --message
    "release"
)
gamewip_detect_version(annotated_tag SOURCE_DIR "${test_repository}" PROJECT_VERSION "0.1.0")
expect_value("annotated tag release state" "${annotated_tag_RELEASE}" "TRUE")
expect_value("annotated tag display" "${annotated_tag_DISPLAY}" "0.1.0")

gamewip_detect_version(unavailable_git SOURCE_DIR "${test_repository}" PROJECT_VERSION "0.1.0" DISABLE_GIT)
expect_value("unavailable Git build number" "${unavailable_git_BUILD_NUMBER}" "unknown")
expect_value("unavailable Git commit" "${unavailable_git_GIT_COMMIT}" "unknown")
expect_value("unavailable Git display" "${unavailable_git_DISPLAY}" "0.1.0-dev+gunknown")

file(REMOVE_RECURSE "${test_repository}")
