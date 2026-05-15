add_library(Assert STATIC)

option(GAMEWIP_ASSERTS_ALWAYS_ON "Force debug assertions on in all configurations" OFF)
option(GAMEWIP_ASSERTS_ALWAYS_OFF "Force debug assertions off in all configurations" OFF)
option(GAMEWIP_CHECKS_ALWAYS_ON "Force recoverable debug checks on in all configurations" OFF)
option(GAMEWIP_CHECKS_ALWAYS_OFF "Force recoverable debug checks off in all configurations" OFF)

if(GAMEWIP_ASSERTS_ALWAYS_ON AND GAMEWIP_ASSERTS_ALWAYS_OFF)
    message(FATAL_ERROR "GAMEWIP_ASSERTS_ALWAYS_ON and GAMEWIP_ASSERTS_ALWAYS_OFF cannot both be enabled.")
endif()

if(GAMEWIP_CHECKS_ALWAYS_ON AND GAMEWIP_CHECKS_ALWAYS_OFF)
    message(FATAL_ERROR "GAMEWIP_CHECKS_ALWAYS_ON and GAMEWIP_CHECKS_ALWAYS_OFF cannot both be enabled.")
endif()

target_sources(Assert PRIVATE
    core/assert.cpp
)

if(WIN32)
    target_sources(Assert PRIVATE
        platform/win32/win32_assert.cpp
    )
else()
    message(FATAL_ERROR "Assert has no platform backend for this operating system yet.")
endif()

target_include_directories(Assert PUBLIC
    "${PROJECT_SOURCE_DIR}/tools"
)

target_compile_features(Assert PUBLIC cxx_std_20)

if(GAMEWIP_ASSERTS_ALWAYS_ON)
    target_compile_definitions(Assert PUBLIC GAMEWIP_ASSERTS_ALWAYS_ON=1)
endif()

if(GAMEWIP_ASSERTS_ALWAYS_OFF)
    target_compile_definitions(Assert PUBLIC GAMEWIP_ASSERTS_ALWAYS_OFF=1)
endif()

if(GAMEWIP_CHECKS_ALWAYS_ON)
    target_compile_definitions(Assert PUBLIC GAMEWIP_CHECKS_ALWAYS_ON=1)
endif()

if(GAMEWIP_CHECKS_ALWAYS_OFF)
    target_compile_definitions(Assert PUBLIC GAMEWIP_CHECKS_ALWAYS_OFF=1)
endif()

target_link_libraries(Assert PUBLIC
    Logger
)
