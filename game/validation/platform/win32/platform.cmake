# Own native executable discovery for the internal validation runners.
target_sources(GameWIPValidationProcess PRIVATE "${CMAKE_CURRENT_LIST_DIR}/executable_path.cpp")
