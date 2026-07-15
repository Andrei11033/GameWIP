cmake_minimum_required(VERSION 3.23)

project(GameWIPIsolatedConsumer LANGUAGES CXX)

if(NOT DEFINED GAMEWIP_PACKAGE_VERSION)
    message(FATAL_ERROR "GAMEWIP_PACKAGE_VERSION is required.")
endif()

set(supported_packages IO FileSystem Terminal Logger Assert TestSupport)
if(NOT GAMEWIP_CONSUMER_PACKAGE IN_LIST supported_packages)
    message(FATAL_ERROR
        "GAMEWIP_CONSUMER_PACKAGE must be one of: ${supported_packages}."
    )
endif()

# This is intentionally the only find_package() call. It proves that each
# package config discovers every imported target in its transitive interface.
find_package(${GAMEWIP_CONSUMER_PACKAGE} ${GAMEWIP_PACKAGE_VERSION} EXACT CONFIG REQUIRED)

add_executable(GameWIPIsolatedConsumer main.cpp)
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
