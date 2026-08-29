cmake_minimum_required(VERSION "${GAMEWIP_CMAKE_MINIMUM_VERSION}")

project(GameWIPInstalledConsumer LANGUAGES CXX)

if(NOT DEFINED GAMEWIP_PACKAGE_VERSION)
    message(FATAL_ERROR "GAMEWIP_PACKAGE_VERSION is required.")
endif()

find_package(Unicode ${GAMEWIP_PACKAGE_VERSION} EXACT CONFIG REQUIRED)
find_package(IO ${GAMEWIP_PACKAGE_VERSION} EXACT CONFIG REQUIRED)
find_package(FileSystem ${GAMEWIP_PACKAGE_VERSION} EXACT CONFIG REQUIRED)
find_package(Terminal ${GAMEWIP_PACKAGE_VERSION} EXACT CONFIG REQUIRED)
find_package(Desktop ${GAMEWIP_PACKAGE_VERSION} EXACT CONFIG REQUIRED)
find_package(Logger ${GAMEWIP_PACKAGE_VERSION} EXACT CONFIG REQUIRED)
find_package(Assert ${GAMEWIP_PACKAGE_VERSION} EXACT CONFIG REQUIRED)
find_package(TestSupport ${GAMEWIP_PACKAGE_VERSION} EXACT CONFIG REQUIRED)

add_executable(GameWIPInstalledConsumer main.cpp)
target_compile_features(GameWIPInstalledConsumer PRIVATE cxx_std_23)
target_link_libraries(
    GameWIPInstalledConsumer
    PRIVATE GameWIP::Unicode GameWIP::IO GameWIP::FileSystem GameWIP::Terminal GameWIP::Desktop GameWIP::Logger GameWIP::Assert GameWIP::TestSupport
)

if(GAMEWIP_CONSUMER_LINK_COVERAGE)
    target_link_options(GameWIPInstalledConsumer PRIVATE --coverage)
endif()

if(GAMEWIP_CONSUMER_ENABLE_ADDRESS_SANITIZER)
    target_compile_options(GameWIPInstalledConsumer PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(GameWIPInstalledConsumer PRIVATE -fsanitize=address)
endif()
