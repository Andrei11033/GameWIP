# Base is header-only: consumers receive arithmetic helpers and the optional
# Windows dynamic-library declaration without linking another binary target.
add_library(GameWIPBase INTERFACE)
add_library(GameWIP::Base ALIAS GameWIPBase)

target_sources(
    GameWIPBase
    INTERFACE
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/checked_arithmetic.h>"
        "$<$<AND:$<BUILD_INTERFACE:1>,$<PLATFORM_ID:Windows>>:${CMAKE_CURRENT_SOURCE_DIR}/platform/win32/dynamic_library.h>"
)

# The interface carries the language and include-root requirements to users.
target_include_directories(GameWIPBase INTERFACE "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>")
target_compile_features(GameWIPBase INTERFACE cxx_std_23)

# Base has no compiled output; register its headers and manual as documentation
# inputs when the unified documentation site is enabled.
if(GAMEWIP_BUILD_DOCS AND COMMAND gamewip_register_doxygen_inputs)
    gamewip_register_doxygen_inputs(
        "${CMAKE_CURRENT_SOURCE_DIR}/checked_arithmetic.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/win32/dynamic_library.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/docs/base.md"
    )
endif()
