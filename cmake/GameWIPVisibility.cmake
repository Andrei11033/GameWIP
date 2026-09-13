# Minimal installed DLL visibility-header generation for GameWIP shared libraries.
# Test-only validation visibility remains in source-tree-only module headers.

include(CMakeParseArguments)

function(gamewip_generate_export_header)
    cmake_parse_arguments(ARG "INTERFACE" "TARGET;OUTPUT;EXPORT_MACRO;BUILD_DEFINE" "" ${ARGN})

    if(NOT ARG_OUTPUT OR NOT ARG_EXPORT_MACRO)
        message(FATAL_ERROR "gamewip_generate_export_header requires OUTPUT and EXPORT_MACRO.")
    endif()

    if(ARG_INTERFACE)
        set(GAMEWIP_VISIBILITY_EXPORT_DEFINITION "#define ${ARG_EXPORT_MACRO}\n")
    else()
        if(NOT ARG_TARGET OR NOT ARG_BUILD_DEFINE)
            message(FATAL_ERROR "Shared visibility headers require TARGET and BUILD_DEFINE.")
        endif()

        target_compile_definitions(${ARG_TARGET} PRIVATE "${ARG_BUILD_DEFINE}")
        string(
            CONCAT GAMEWIP_VISIBILITY_EXPORT_DEFINITION
            "#if defined(_WIN32) || defined(__CYGWIN__)\n"
            "#if defined(${ARG_BUILD_DEFINE})\n"
            "#define ${ARG_EXPORT_MACRO} __declspec(dllexport)\n"
            "#else\n"
            "#define ${ARG_EXPORT_MACRO} __declspec(dllimport)\n"
            "#endif\n"
            "#elif defined(__clang__) || defined(__GNUC__)\n"
            "#define ${ARG_EXPORT_MACRO} __attribute__((visibility(\"default\")))\n"
            "#else\n"
            "#define ${ARG_EXPORT_MACRO}\n"
            "#endif\n"
        )
    endif()

    configure_file("${PROJECT_SOURCE_DIR}/cmake/GameWIPVisibility.h.in" "${ARG_OUTPUT}" @ONLY)
endfunction()
