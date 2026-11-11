# Validates a clean consumer against the installed GameWIP package set.

cmake_minimum_required(VERSION "${GAMEWIP_CMAKE_MINIMUM_VERSION}")

project(GameWIPInstalledConsumer LANGUAGES CXX)

if(WIN32)
    enable_language(RC)
endif()

if(NOT DEFINED GAMEWIP_PACKAGE_VERSION)
    message(FATAL_ERROR "GAMEWIP_PACKAGE_VERSION is required.")
endif()

# ------------------------------------------------------------
# Package discovery
# ------------------------------------------------------------

find_package(Unicode ${GAMEWIP_PACKAGE_VERSION} EXACT CONFIG REQUIRED)
find_package(IO ${GAMEWIP_PACKAGE_VERSION} EXACT CONFIG REQUIRED)
find_package(FileSystem ${GAMEWIP_PACKAGE_VERSION} EXACT CONFIG REQUIRED)
find_package(Terminal ${GAMEWIP_PACKAGE_VERSION} EXACT CONFIG REQUIRED)
find_package(Desktop ${GAMEWIP_PACKAGE_VERSION} EXACT CONFIG REQUIRED)
find_package(Input ${GAMEWIP_PACKAGE_VERSION} EXACT CONFIG REQUIRED)
find_package(Logger ${GAMEWIP_PACKAGE_VERSION} EXACT CONFIG REQUIRED)
find_package(Assert ${GAMEWIP_PACKAGE_VERSION} EXACT CONFIG REQUIRED)
find_package(TestSupport ${GAMEWIP_PACKAGE_VERSION} EXACT CONFIG REQUIRED)

# ------------------------------------------------------------
# Consumer target
# ------------------------------------------------------------

add_executable(GameWIPInstalledConsumer main.cpp consumer_probe.h)
target_compile_features(GameWIPInstalledConsumer PRIVATE cxx_std_23)
target_compile_definitions(GameWIPInstalledConsumer PRIVATE GAMEWIP_CONSUMER_COMBINED=1)
target_link_libraries(
    GameWIPInstalledConsumer
    PRIVATE
        GameWIP::Unicode
        GameWIP::IO
        GameWIP::FileSystem
        GameWIP::Terminal
        GameWIP::Desktop
        GameWIP::Input
        GameWIP::Logger
        GameWIP::Assert
        GameWIP::TestSupport
)

gamewip_attach_application_manifest(TARGET GameWIPInstalledConsumer COMMON_CONTROLS_V6 PER_MONITOR_V2)

# ------------------------------------------------------------
# Optional instrumentation
# ------------------------------------------------------------

if(GAMEWIP_CONSUMER_LINK_COVERAGE)
    target_link_options(GameWIPInstalledConsumer PRIVATE --coverage)
endif()

if(GAMEWIP_CONSUMER_ENABLE_ADDRESS_SANITIZER)
    target_compile_options(GameWIPInstalledConsumer PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(GameWIPInstalledConsumer PRIVATE -fsanitize=address)
endif()

if(GAMEWIP_CONSUMER_ENABLE_UNDEFINED_BEHAVIOR_SANITIZER)
    target_compile_options(
        GameWIPInstalledConsumer
        PRIVATE -fsanitize=undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer -fno-sanitize-merge
    )
    target_link_options(GameWIPInstalledConsumer PRIVATE -fsanitize=undefined)
endif()
