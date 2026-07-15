foreach(required_variable IN ITEMS PROJECT_SOURCE_DIR GAMEWIP_CMAKE_MINIMUM_VERSION)
    if(NOT DEFINED ${required_variable})
        message(FATAL_ERROR "${required_variable} is required for CMake requirement validation.")
    endif()
endforeach()

file(READ "${PROJECT_SOURCE_DIR}/CMakeLists.txt" root_cmake)
set(expected_root_requirement
    "cmake_minimum_required(VERSION ${GAMEWIP_CMAKE_MINIMUM_VERSION})"
)
string(FIND "${root_cmake}" "${expected_root_requirement}" root_requirement_offset)
if(root_requirement_offset EQUAL -1)
    message(FATAL_ERROR
        "The root CMake requirement must be exactly '${expected_root_requirement}'."
    )
endif()

set(propagated_entry_points
    "${PROJECT_SOURCE_DIR}/cmake/TestGameWIPVersion.cmake"
    "${PROJECT_SOURCE_DIR}/game/validation/installed_consumer/CMakeLists.txt"
    "${PROJECT_SOURCE_DIR}/game/validation/installed_consumer/isolated/CMakeLists.txt"
)
set(expected_propagated_requirement
    "cmake_minimum_required(VERSION \"\${GAMEWIP_CMAKE_MINIMUM_VERSION}\")"
)
foreach(entry_point IN LISTS propagated_entry_points)
    file(READ "${entry_point}" entry_point_contents)
    string(FIND
        "${entry_point_contents}"
        "${expected_propagated_requirement}"
        propagated_requirement_offset
    )
    if(propagated_requirement_offset EQUAL -1)
        message(FATAL_ERROR
            "${entry_point} must consume the root CMake requirement through "
            "GAMEWIP_CMAKE_MINIMUM_VERSION."
        )
    endif()
endforeach()

file(GLOB_RECURSE maintained_cmake_files LIST_DIRECTORIES FALSE
    "${PROJECT_SOURCE_DIR}/CMakeLists.txt"
    "${PROJECT_SOURCE_DIR}/*.cmake"
)
set(build_root "${PROJECT_SOURCE_DIR}/build")
set(external_root "${PROJECT_SOURCE_DIR}/external")
foreach(cmake_file IN LISTS maintained_cmake_files)
    cmake_path(IS_PREFIX build_root "${cmake_file}" NORMALIZE in_build)
    cmake_path(IS_PREFIX external_root "${cmake_file}" NORMALIZE in_external)
    if(in_build OR in_external OR cmake_file STREQUAL "${PROJECT_SOURCE_DIR}/CMakeLists.txt")
        continue()
    endif()

    file(READ "${cmake_file}" cmake_contents)
    if(cmake_contents MATCHES "cmake_minimum_required[ \t\r\n]*\\([ \t\r\n]*VERSION[ \t]+[0-9]")
        message(FATAL_ERROR
            "${cmake_file} hard-codes a second numeric CMake requirement; "
            "propagate the root requirement instead."
        )
    endif()
endforeach()
