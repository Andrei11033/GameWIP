cmake_minimum_required(VERSION "${GAMEWIP_CMAKE_MINIMUM_VERSION}")

project(GameWIPIsolatedConsumer LANGUAGES CXX)

if(NOT DEFINED GAMEWIP_PACKAGE_VERSION)
    message(FATAL_ERROR "GAMEWIP_PACKAGE_VERSION is required.")
endif()

set(supported_packages Unicode IO FileSystem Terminal Window Logger Assert TestSupport)
if(NOT GAMEWIP_CONSUMER_PACKAGE IN_LIST supported_packages)
    message(FATAL_ERROR
        "GAMEWIP_CONSUMER_PACKAGE must be one of: ${supported_packages}."
    )
endif()

# This is intentionally the only find_package() call. It proves that each
# package config discovers every imported target in its transitive interface.
find_package(${GAMEWIP_CONSUMER_PACKAGE} ${GAMEWIP_PACKAGE_VERSION} EXACT CONFIG REQUIRED)

add_executable(GameWIPIsolatedConsumer main.cpp)
if(GAMEWIP_CONSUMER_PACKAGE STREQUAL "Logger")
    target_sources(GameWIPIsolatedConsumer PRIVATE
        logger_config.cpp
        logger_macros.cpp
        logger_types.cpp
    )
elseif(GAMEWIP_CONSUMER_PACKAGE STREQUAL "Terminal")
    target_sources(GameWIPIsolatedConsumer PRIVATE
        terminal_style.cpp
    )
elseif(GAMEWIP_CONSUMER_PACKAGE STREQUAL "Window")
    target_sources(GameWIPIsolatedConsumer PRIVATE
        window_types.cpp
        window_description.cpp
        window_events.cpp
        window_display.cpp
        window_display_info.cpp
        window_window.cpp
        window_renderer_bridge.cpp
    )
    if(WIN32)
        target_sources(GameWIPIsolatedConsumer PRIVATE window_native_win32.cpp)
    endif()
elseif(GAMEWIP_CONSUMER_PACKAGE STREQUAL "TestSupport")
    target_sources(GameWIPIsolatedConsumer PRIVATE
        test_support_types.cpp
        test_support_reporting.cpp
        test_support_files.cpp
        test_support_process.cpp
        test_support_stress.cpp
    )
endif()
target_compile_features(GameWIPIsolatedConsumer PRIVATE cxx_std_23)
target_compile_definitions(GameWIPIsolatedConsumer PRIVATE
    "GAMEWIP_CONSUMER_${GAMEWIP_CONSUMER_PACKAGE}=1"
)
target_link_libraries(GameWIPIsolatedConsumer PRIVATE
    "GameWIP::${GAMEWIP_CONSUMER_PACKAGE}"
)

if(GAMEWIP_CONSUMER_LINK_COVERAGE)
    target_link_options(GameWIPIsolatedConsumer PRIVATE --coverage)
endif()

if(GAMEWIP_CONSUMER_ENABLE_ADDRESS_SANITIZER)
    target_compile_options(GameWIPIsolatedConsumer PRIVATE
        -fsanitize=address
        -fno-omit-frame-pointer
    )
    target_link_options(GameWIPIsolatedConsumer PRIVATE -fsanitize=address)
endif()
