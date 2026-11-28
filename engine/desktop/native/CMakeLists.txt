target_sources(Desktop PUBLIC FILE_SET public_headers FILES win32.h)

if(GAMEWIP_BUILD_DOCS AND COMMAND gamewip_register_doxygen_inputs)
    gamewip_register_doxygen_inputs("${CMAKE_CURRENT_SOURCE_DIR}/win32.h")
endif()
