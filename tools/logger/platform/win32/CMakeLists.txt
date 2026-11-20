target_sources(
    Logger
    PRIVATE
        diagnostics.cpp
        format_scratch.cpp
        process.cpp
        time.cpp
)
target_link_libraries(Logger PRIVATE psapi)
