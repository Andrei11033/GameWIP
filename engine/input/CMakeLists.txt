include(GNUInstallDirs)

if(NOT COMMAND gamewip_generate_export_header)
    include("${PROJECT_SOURCE_DIR}/cmake/GameWIPVisibility.cmake")
endif()

if(NOT COMMAND gamewip_install_package)
    include("${PROJECT_SOURCE_DIR}/cmake/GameWIPPackage.cmake")
endif()

if(NOT COMMAND gamewip_target_platform_backend)
    include("${PROJECT_SOURCE_DIR}/cmake/GameWIPPlatform.cmake")
    gamewip_resolve_platform_id(GAMEWIP_PLATFORM_ID)
endif()

set(INPUT_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${INPUT_GENERATED_INCLUDE_DIR}/input")

add_library(Input SHARED)
add_library(GameWIP::Input ALIAS Input)

gamewip_generate_export_header(
    TARGET Input
    OUTPUT "${INPUT_GENERATED_INCLUDE_DIR}/input/input_export.h"
    EXPORT_MACRO INPUT_EXPORT
    BUILD_DEFINE INPUT_BUILDING_DLL
)

target_sources(
    Input
    PRIVATE core/frame.cpp core/synthetic.cpp core/system.cpp core/value.cpp
    PUBLIC
        FILE_SET public_headers
            TYPE HEADERS
            BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/.." "${INPUT_GENERATED_INCLUDE_DIR}"
            FILES
                "${CMAKE_CURRENT_SOURCE_DIR}/frame.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/input.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/query.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/standard.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/synthetic.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/system.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/types.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/win32.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/native/desktop_win32.h"
                "${INPUT_GENERATED_INCLUDE_DIR}/input/input_export.h"
)

target_include_directories(
    Input
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
        $<BUILD_INTERFACE:${INPUT_GENERATED_INCLUDE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(Input PUBLIC cxx_std_23)
target_link_libraries(Input PUBLIC IO PRIVATE Unicode)

gamewip_target_platform_backend(TARGET Input ROOT "${CMAKE_CURRENT_SOURCE_DIR}/platform")

set_target_properties(
    Input
    PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
        POSITION_INDEPENDENT_CODE ON
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

if(GAMEWIP_BUILD_DOCS AND COMMAND gamewip_register_doxygen_library)
    gamewip_register_doxygen_library(
        NAME Input
        PAGE_ID input_library
        PUBLIC_HEADERS
            "${CMAKE_CURRENT_SOURCE_DIR}/frame.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/input.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/query.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/standard.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/synthetic.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/system.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/types.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/win32.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/native/desktop_win32.h"
        DOCS "${CMAKE_CURRENT_SOURCE_DIR}/docs"
    )
endif()

install(
    TARGETS Input
    EXPORT InputTargets
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    FILE_SET public_headers DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

gamewip_install_package(TARGET Input CONFIG_TEMPLATE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/InputConfig.cmake.in")
