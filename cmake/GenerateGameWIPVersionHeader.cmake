foreach(required_variable IN ITEMS SOURCE_DIR BINARY_DIR PROJECT_VERSION_VALUE)
    if(NOT DEFINED ${required_variable})
        message(FATAL_ERROR "${required_variable} is required.")
    endif()
endforeach()

include("${SOURCE_DIR}/cmake/GameWIPVersion.cmake")
gamewip_write_version_header(
    generated
    SOURCE_DIR "${SOURCE_DIR}"
    BINARY_DIR "${BINARY_DIR}"
    PROJECT_VERSION "${PROJECT_VERSION_VALUE}"
)
