# Keep runtime implementation sources registered beside their domain folders.
target_sources(GameWIP PRIVATE runtime_input.h game.cpp)

add_subdirectory(diagnostics)
add_subdirectory(lifecycle)
add_subdirectory(options)
add_subdirectory(window)
add_subdirectory(platform)
