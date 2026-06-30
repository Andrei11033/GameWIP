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

    library_register_doxygen_inputs(
        "${GAMEWIP_DOXYGEN_ROOT}/index.md"
        "${GAMEWIP_DOXYGEN_ROOT}/build.md"
        "${GAMEWIP_DOXYGEN_ROOT}/library_compatibility.md"
        "${GAMEWIP_DOXYGEN_ROOT}/validation.md"
        "${GAMEWIP_DOXYGEN_ROOT}/testing.md"
        "${GAMEWIP_DOXYGEN_ROOT}/benchmarking.md"
        "${GAMEWIP_DOXYGEN_ROOT}/coverage.md"
        "${GAMEWIP_DOXYGEN_ROOT}/static_analysis.md"
        "${GAMEWIP_DOXYGEN_ROOT}/repository_automation.md"
        "${GAMEWIP_DOXYGEN_ROOT}/documentation.md"
        "${GAMEWIP_DOXYGEN_ROOT}/doxygen_notes.md"
    )
endif()

function(gamewip_create_documentation_target)
    if(GAMEWIP_BUILD_DOCS)
        library_create_doxygen_target(MAINPAGE "${GAMEWIP_DOXYGEN_MAINPAGE}")
    endif()
endfunction()
