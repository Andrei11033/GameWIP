/// @file desktop_manual_child_surface_tests.inl
/// @brief Visible native ChildSurface manual validation.

/// ------------------------------------------------------------
/// Manual child-surface coverage
/// ------------------------------------------------------------

/// Manual child-surface coverage
/// ------------------------------------------------------------

/// @brief Visually validates a real Win32 descendant hosted below a ChildSurface HWND.
void testManualChildSurface(TestSupport::Context &context, const GameWIP::Test::DesktopTestOptions &options)
{
    if(!beginManualSuite(context, options, "Window native child surface"))
    {
        return;
    }
    // Open a visible parent and embedded ChildSurface.
    Desktop::Window parent;

    if(!openManualWindow(context, parent, "GameWIP ChildSurface native-host validation"))
    {
        return;
    }
    Desktop::ChildSurface surface;
    Desktop::Types::ChildSurface::Description description;
    description.rect = {{180, 140}, {600, 260}};
    description.visible = true;

    if(!requireManualStatus(
           context,
           "manual ChildSurface opens",
           surface.open(parent, description, Desktop::Types::ChildSurface::QueueConfiguration::owned(32))))
    {
        static_cast<void>(parent.close());
        return;
    }
    // Add a real native child control to verify HWND embedding.
    const auto native = Desktop::Native::Win32::getHandle(surface);
    HWND button = nullptr;

    if(native.status.ok())
    {
        button = CreateWindowExW(
            0,
            L"BUTTON",
            L"Real Win32 descendant hosted by GameWIP ChildSurface",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            60,
            80,
            480,
            80,
            native->window,
            nullptr,
            native->instance,
            nullptr);
    }
    static_cast<void>(context.expectTrue("manual native descendant is created", button != nullptr));

    if(button != nullptr)
    {
        recordManualCheck(
            context,
            parent,
            "native descendant embedding",
            "Is a native button labeled 'Real Win32 descendant hosted by GameWIP ChildSurface' visible inside the validation Window?");
        static_cast<void>(DestroyWindow(button));
    }
    static_cast<void>(surface.close());
    static_cast<void>(parent.close());
}
