include(GNUInstallDirs)

if(NOT COMMAND gamewip_install_package)
    include("${PROJECT_SOURCE_DIR}/cmake/GameWIPPackage.cmake")
endif()

option(TEST_SUPPORT_ENABLE_TEST_HOOKS "Enable TestSupport internal test hooks" OFF)

if(NOT COMMAND gamewip_target_platform_backend)
    include("${PROJECT_SOURCE_DIR}/cmake/GameWIPPlatform.cmake")
    gamewip_resolve_platform_id(GAMEWIP_PLATFORM_ID)
endif()

add_library(TestSupport STATIC)
add_library(GameWIP::TestSupport ALIAS TestSupport)

target_sources(
    TestSupport
    PRIVATE core/test_support.cpp core/reporting.cpp core/files.cpp core/process_state.cpp core/stress.cpp core/test_support_test_hooks.cpp
    PUBLIC
        FILE_SET public_headers
            TYPE HEADERS
            BASE_DIRS "${PROJECT_SOURCE_DIR}/tools"
            FILES
                "${CMAKE_CURRENT_SOURCE_DIR}/types.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/reporting.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/files.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/process.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/stress.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/test_support.h"
)

gamewip_target_platform_backend(TARGET TestSupport ROOT "${CMAKE_CURRENT_SOURCE_DIR}/platform")

target_include_directories(TestSupport PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/tools> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

target_compile_features(TestSupport PUBLIC cxx_std_23)
target_compile_definitions(
    TestSupport
    PRIVATE TEST_SUPPORT_INTERNAL_TEST_HOOKS=$<BOOL:${TEST_SUPPORT_ENABLE_TEST_HOOKS}>
    INTERFACE $<BUILD_INTERFACE:TEST_SUPPORT_INTERNAL_TEST_HOOKS=$<BOOL:${TEST_SUPPORT_ENABLE_TEST_HOOKS}>>
)

target_link_libraries(TestSupport PRIVATE Unicode)

# TestSupport may use foundational Unicode for real text semantics, but must not
# acquire IO, FileSystem, Logger, Assert, engine, or other higher-level GameWIP dependencies.
set(test_support_forbidden_gamewip_targets
    IO
    FileSystem
    Terminal
    Desktop
    Logger
    Assert
    GameWIP::IO
    GameWIP::FileSystem
    GameWIP::Terminal
    GameWIP::Desktop
    GameWIP::Logger
    GameWIP::Assert
)
foreach(property_name LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
    get_target_property(property_value TestSupport ${property_name})
    if(property_value)
        foreach(link_item IN LISTS property_value)
            if(link_item IN_LIST test_support_forbidden_gamewip_targets)
                message(FATAL_ERROR "TestSupport must not depend on '${link_item}'.")
            endif()
            if(link_item MATCHES "^GameWIP::" AND NOT link_item STREQUAL "GameWIP::Unicode")
                message(FATAL_ERROR "TestSupport may depend only on foundational Unicode, but ${property_name} contains '${link_item}'.")
            endif()
        endforeach()
    endif()
endforeach()

set_target_properties(TestSupport PROPERTIES ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

if(GAMEWIP_BUILD_DOCS AND COMMAND gamewip_register_doxygen_library)
    gamewip_register_doxygen_library(
        NAME TestSupport
        PAGE_ID test_support
        PUBLIC_HEADERS
            "${CMAKE_CURRENT_SOURCE_DIR}/types.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/reporting.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/files.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/process.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/stress.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/test_support.h"
        DOCS "${CMAKE_CURRENT_SOURCE_DIR}/docs"
    )
endif()

install(
    TARGETS TestSupport
    EXPORT TestSupportTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    FILE_SET public_headers DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

gamewip_install_package(TARGET TestSupport CONFIG_TEMPLATE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/TestSupportConfig.cmake.in")
