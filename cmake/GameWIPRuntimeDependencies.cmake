function(gamewip_copy_runtime_dependencies target_name)
    get_filename_component(gamewip_runtime_search_dir
        "${CMAKE_CXX_COMPILER}"
        DIRECTORY
    )

    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            "-DGAMEWIP_EXECUTABLE=$<TARGET_FILE:${target_name}>"
            "-DGAMEWIP_OUTPUT_DIR=$<TARGET_FILE_DIR:${target_name}>"
            "-DGAMEWIP_RUNTIME_SEARCH_DIR=${gamewip_runtime_search_dir}"
            -P "${PROJECT_SOURCE_DIR}/cmake/copy_runtime_dependencies.cmake"
        VERBATIM
    )
endfunction()
