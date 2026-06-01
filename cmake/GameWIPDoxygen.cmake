include_guard(GLOBAL)

# Project-wide Doxygen helpers. Library-specific CMake files should only
# declare their own public headers/docs; the root docs target merges everything
# into one generated GameWIP documentation site.

function(gamewip_register_doxygen_inputs)
    foreach(doxygen_input IN LISTS ARGN)
        if(NOT doxygen_input)
            continue()
        endif()

        get_filename_component(doxygen_input_absolute "${doxygen_input}" ABSOLUTE)

        if(IS_DIRECTORY "${doxygen_input_absolute}")
            # Register documentation files explicitly instead of passing folders
            # to Doxygen. This avoids accidental recursive crawls through build
            # output, generated HTML, private notes, or future helper folders.
            file(GLOB doxygen_directory_inputs CONFIGURE_DEPENDS
                "${doxygen_input_absolute}/*.h"
                "${doxygen_input_absolute}/*.hpp"
                "${doxygen_input_absolute}/*.md"
            )

            foreach(doxygen_directory_input IN LISTS doxygen_directory_inputs)
                set_property(GLOBAL APPEND PROPERTY GAMEWIP_DOXYGEN_INPUTS "${doxygen_directory_input}")
            endforeach()
        else()
            get_filename_component(doxygen_input_extension "${doxygen_input_absolute}" EXT)
            string(TOLOWER "${doxygen_input_extension}" doxygen_input_extension_lower)
            if(NOT doxygen_input_extension_lower STREQUAL ".h"
                AND NOT doxygen_input_extension_lower STREQUAL ".hpp"
                AND NOT doxygen_input_extension_lower STREQUAL ".md")
                message(FATAL_ERROR
                    "Unsupported Doxygen input '${doxygen_input_absolute}'. "
                    "Register only public headers and Markdown manual pages."
                )
            endif()
            set_property(GLOBAL APPEND PROPERTY GAMEWIP_DOXYGEN_INPUTS "${doxygen_input_absolute}")
        endif()
    endforeach()
endfunction()

function(gamewip_register_doxygen_library)
    set(options)
    set(one_value_args NAME PAGE_ID)
    set(multi_value_args PUBLIC_HEADERS DOCS)
    cmake_parse_arguments(GAMEWIP_DOXYGEN_LIBRARY
        "${options}"
        "${one_value_args}"
        "${multi_value_args}"
        ${ARGN}
    )

    if(GAMEWIP_DOXYGEN_LIBRARY_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "gamewip_register_doxygen_library received unknown arguments: "
            "${GAMEWIP_DOXYGEN_LIBRARY_UNPARSED_ARGUMENTS}"
        )
    endif()

    if(NOT GAMEWIP_DOXYGEN_LIBRARY_NAME)
        message(FATAL_ERROR "gamewip_register_doxygen_library requires NAME.")
    endif()

    if(NOT GAMEWIP_DOXYGEN_LIBRARY_PAGE_ID)
        message(FATAL_ERROR "gamewip_register_doxygen_library requires PAGE_ID.")
    endif()

    if(NOT GAMEWIP_DOXYGEN_LIBRARY_DOCS)
        message(FATAL_ERROR
            "gamewip_register_doxygen_library(${GAMEWIP_DOXYGEN_LIBRARY_NAME}) "
            "requires DOCS containing ${GAMEWIP_DOXYGEN_LIBRARY_PAGE_ID}.md."
        )
    endif()

    if(NOT GAMEWIP_DOXYGEN_LIBRARY_PUBLIC_HEADERS AND NOT GAMEWIP_DOXYGEN_LIBRARY_DOCS)
        message(FATAL_ERROR
            "gamewip_register_doxygen_library(${GAMEWIP_DOXYGEN_LIBRARY_NAME}) "
            "must register at least one public header or docs folder."
        )
    endif()

    set(GAMEWIP_DOXYGEN_LIBRARY_LANDING_PAGE_FOUND OFF)
    foreach(doxygen_docs_input IN LISTS GAMEWIP_DOXYGEN_LIBRARY_DOCS)
        get_filename_component(doxygen_docs_input_absolute "${doxygen_docs_input}" ABSOLUTE)
        if(IS_DIRECTORY "${doxygen_docs_input_absolute}")
            if(EXISTS "${doxygen_docs_input_absolute}/${GAMEWIP_DOXYGEN_LIBRARY_PAGE_ID}.md")
                set(GAMEWIP_DOXYGEN_LIBRARY_LANDING_PAGE_FOUND ON)
            endif()
        else()
            get_filename_component(doxygen_docs_input_name "${doxygen_docs_input_absolute}" NAME)
            if(doxygen_docs_input_name STREQUAL "${GAMEWIP_DOXYGEN_LIBRARY_PAGE_ID}.md")
                set(GAMEWIP_DOXYGEN_LIBRARY_LANDING_PAGE_FOUND ON)
            endif()
        endif()
    endforeach()

    if(NOT GAMEWIP_DOXYGEN_LIBRARY_LANDING_PAGE_FOUND)
        message(FATAL_ERROR
            "gamewip_register_doxygen_library(${GAMEWIP_DOXYGEN_LIBRARY_NAME}) "
            "requires a ${GAMEWIP_DOXYGEN_LIBRARY_PAGE_ID}.md landing page."
        )
    endif()

    set_property(GLOBAL APPEND PROPERTY GAMEWIP_DOXYGEN_LIBRARIES
        "${GAMEWIP_DOXYGEN_LIBRARY_NAME}:${GAMEWIP_DOXYGEN_LIBRARY_PAGE_ID}"
    )

    gamewip_register_doxygen_inputs(
        ${GAMEWIP_DOXYGEN_LIBRARY_PUBLIC_HEADERS}
        ${GAMEWIP_DOXYGEN_LIBRARY_DOCS}
    )
endfunction()

function(gamewip_create_doxygen_target)
    set(options)
    set(one_value_args MAINPAGE)
    set(multi_value_args)
    cmake_parse_arguments(GAMEWIP_DOXYGEN
        "${options}"
        "${one_value_args}"
        "${multi_value_args}"
        ${ARGN}
    )

    if(NOT GAMEWIP_DOXYGEN_MAINPAGE)
        message(FATAL_ERROR "gamewip_create_doxygen_target requires MAINPAGE.")
    endif()

    get_property(GAMEWIP_DOXYGEN_INPUT_LIST GLOBAL PROPERTY GAMEWIP_DOXYGEN_INPUTS)
    if(NOT GAMEWIP_DOXYGEN_INPUT_LIST)
        message(FATAL_ERROR "GAMEWIP_BUILD_DOCS is ON, but no Doxygen inputs were registered.")
    endif()

    list(REMOVE_DUPLICATES GAMEWIP_DOXYGEN_INPUT_LIST)
    list(SORT GAMEWIP_DOXYGEN_INPUT_LIST)

    set(GAMEWIP_DOXYGEN_INPUTS "")
    foreach(doxygen_input IN LISTS GAMEWIP_DOXYGEN_INPUT_LIST)
        string(APPEND GAMEWIP_DOXYGEN_INPUTS " \"${doxygen_input}\"")
    endforeach()

    get_property(GAMEWIP_DOXYGEN_LIBRARY_LIST GLOBAL PROPERTY GAMEWIP_DOXYGEN_LIBRARIES)
    if(GAMEWIP_DOXYGEN_LIBRARY_LIST)
        message(STATUS "Registered Doxygen libraries: ${GAMEWIP_DOXYGEN_LIBRARY_LIST}")
    endif()

    set(GAMEWIP_DOXYGEN_MAINPAGE "${GAMEWIP_DOXYGEN_MAINPAGE}")
    set(GAMEWIP_DOXYGEN_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/docs/doxygen")
    set(GAMEWIP_DOXYGEN_HTML_INDEX "${GAMEWIP_DOXYGEN_OUTPUT_DIR}/html/index.html")
    set(GAMEWIP_DOXYGEN_WARNING_LOG "${GAMEWIP_DOXYGEN_OUTPUT_DIR}/doxygen_warnings.log")

    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/docs/doxygen/Doxyfile.in"
        "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile"
        @ONLY
    )

    add_custom_target(docs
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${GAMEWIP_DOXYGEN_OUTPUT_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${GAMEWIP_DOXYGEN_OUTPUT_DIR}/html"
        COMMAND "${CMAKE_COMMAND}" -E echo "Doxygen input is explicit public headers + Markdown manual pages only."
        COMMAND "${CMAKE_COMMAND}" -E echo "Generating Doxygen HTML into: ${GAMEWIP_DOXYGEN_OUTPUT_DIR}/html"
        COMMAND Doxygen::doxygen "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile"
        COMMAND "${CMAKE_COMMAND}" -E echo "Generated Doxygen HTML: ${GAMEWIP_DOXYGEN_HTML_INDEX}"
        COMMAND "${CMAKE_COMMAND}" -E echo "Doxygen warnings: ${GAMEWIP_DOXYGEN_WARNING_LOG}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Generating GameWIP API and guide documentation"
        VERBATIM
    )

    if(GAMEWIP_INSTALL_DOCS)
        install(DIRECTORY "${GAMEWIP_DOXYGEN_OUTPUT_DIR}/html"
            DESTINATION "share/doc/GameWIP/doxygen"
            OPTIONAL
        )
    endif()
endfunction()
