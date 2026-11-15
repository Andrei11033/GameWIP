# Keep runtime implementation sources registered beside their domain folders.
target_sources(
    GameWIP
    PRIVATE
        game.cpp

        # Runtime diagnostics
        diagnostics/display_report.cpp
        diagnostics/input_control_events.cpp
        diagnostics/input_devices.cpp
        diagnostics/input_event_format.cpp
        diagnostics/input_event_labels.cpp
        diagnostics/input_events.cpp
        diagnostics/runtime_diagnostics.cpp

        # Runtime lifecycle
        lifecycle/input_lifecycle.cpp
        lifecycle/logger_lifecycle.cpp

        # Runtime options
        options/runtime_options.cpp

        # Window event loop
        window/window_loop.cpp
)
