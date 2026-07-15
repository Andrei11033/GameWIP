foreach(required_variable IN ITEMS SOURCE_DIR BINARY_DIR PROJECT_VERSION OUTPUT_FILE)
    if(NOT DEFINED ${required_variable})
        message(FATAL_ERROR "${required_variable} is required to refresh Doxygen version metadata.")
    endif()
endforeach()

include("${SOURCE_DIR}/cmake/GameWIPVersion.cmake")
gamewip_write_version_header(
    refreshed
    SOURCE_DIR "${SOURCE_DIR}"
    BINARY_DIR "${BINARY_DIR}"
    PROJECT_VERSION "${PROJECT_VERSION}"
)
file(WRITE "${OUTPUT_FILE}" "PROJECT_NUMBER = \"${refreshed_DISPLAY}\"\n")
