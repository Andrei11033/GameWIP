/// @file desktop_manual_window_setup.inl
/// @brief Manual test window setup and selection.

/// ------------------------------------------------------------
/// Manual window setup
/// ------------------------------------------------------------

/// @brief Reports a failed setup operation and returns whether the manual scenario may continue.
[[nodiscard]] bool requireManualStatus(TestSupport::Context &context, std::string_view name, const IO::Types::Status &status)
{
    if(status.ok())
    {
        return true;
    }

    if(manualStatusWindow != nullptr)
    {
        manualStatusWindow->setObservation(
            std::format(
                "Operation '{}' failed: portableCode={} nativeCode={} message={}",
                name,
                static_cast<int>(status.code),
                status.nativeCode,
                status.message));
    }
    context.fail(
        name,
        std::format(
            "portableCode={} nativeCode={} message={}",
            static_cast<int>(status.code),
            status.nativeCode,
            status.message.empty() ? "<none>" : status.message));
    return false;
}

/// @brief Keeps owner-thread native messages flowing for a bounded manual preparation interval.
void pumpManualPreparation(std::chrono::milliseconds duration)
{
    const auto deadline = std::chrono::steady_clock::now() + duration;

    while(std::chrono::steady_clock::now() < deadline)
    {
        static_cast<void>(Desktop::Events::wait(std::chrono::milliseconds{50}));
    }
}

/// @brief Opens a consistently sized visible Window for one manual scenario.
[[nodiscard]] bool openManualWindow(
    TestSupport::Context &context,
    Desktop::Window &window,
    std::string_view title,
    Desktop::Types::Description description = {})
{
    // Configure and present a fixed-size window for the selected scenario.
    description.title = title;
    description.windowed.clientSize = {960, 540};
    description.activation.visible = true;
    description.activation.requestFocus = true;

    if(!requireManualStatus(context, "manual Window setup", window.open(description)))
    {
        return false;
    }

    if(!requireManualStatus(context, "manual Window presentation", window.show()))
    {
        return false;
    }

    // Verify the window after its first event pump.
    const Desktop::Types::Events::PumpResult pump = Desktop::Events::poll();
    if(!requireManualStatus(context, "manual Window event pump", pump.status))
    {
        return false;
    }

    if(!window.isOpen() || !window.visible())
    {
        context.fail("manual Window presentation", "Window did not remain open and visible after the presentation pump");
        return false;
    }
    paintManualValidationSurface(window);

    if(manualStatusWindow != nullptr)
    {
        manualStatusWindow->refresh(window);
    }

    // Let native presentation settle before the user starts the check.
    pumpManualPreparation(std::chrono::milliseconds{150});

    paintManualValidationSurface(window);

    if(manualStatusWindow != nullptr)
    {
        manualStatusWindow->refresh(window);
    }

    return true;
}

/// @brief Returns whether an opt-in suite should run.
/// @note Skip reporting is consolidated at the registration level to avoid spam.
[[nodiscard]] bool beginManualSuite(TestSupport::Context &, const GameWIP::Test::DesktopTestOptions &options, std::string_view)
{
    return options.enableManualTests;
}
