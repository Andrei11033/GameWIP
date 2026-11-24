/// @file desktop_manual_files_shell_tests.inl
/// @brief Coordinates manual file, shell, and Window policy checks.

/// ------------------------------------------------------------
/// File and shell manual suite
/// ------------------------------------------------------------

void testManualFilesAndShell(
    TestSupport::Context &context,
    const GameWIP::Test::DesktopTestOptions &options,
    bool includeWindow = true,
    bool includeShell = true)
{
    if(!beginManualSuite(context, options, "Window files and shell behavior"))
    {
        return;
    }

    // Open only the capabilities selected for this manual run.
    const auto capabilitiesResult = Desktop::getCapabilities();
    const Desktop::Types::Capabilities capabilities = capabilitiesResult.value_or({});
    Desktop::Types::Description description;
    description.fileDropEnabled = includeWindow && capabilities.supports(Desktop::Types::Capability::FileDrop);
    Desktop::Window window;

    if(!openManualWindow(context, window, "GameWIP file and shell validation", description))
    {
        return;
    }

    // Run file-drop and icon checks on the shared Window.
    if(includeWindow)
    {
        testManualFileDropAndIcon(context, window, capabilities);
    }

    if(!testManualShellIntegrations(context, window, includeShell))
    {
        return;
    }

    // Apply Window-specific checks only when requested.
    if(!includeWindow)
    {
        static_cast<void>(window.close());
        return;
    }

    testManualWindowPolicyAndControls(context, window);
}
