/// @file desktop_manual_dpi_cursor_tests.inl
/// @brief DPI and cursor manual validation.

/// ------------------------------------------------------------
/// DPI and cursor behavior
/// ------------------------------------------------------------

/// @brief Exercises mixed-DPI policies, coordinate conversion, and physical monitor geometry.
void testManualDpiAndCoordinates(TestSupport::Context &context, const GameWIP::Test::DesktopTestOptions &options)
{
    if(!beginManualSuite(context, options, "Window DPI and coordinates"))
    {
        return;
    }
    Desktop::Window window;
    Desktop::Types::Description description;
    description.dpiResizePolicy = Desktop::Types::DpiResizePolicy::PreserveLogicalClientSize;

    if(!openManualWindow(context, window, "GameWIP mixed-DPI validation", description))
    {
        return;
    }

    recordManualCheck(
        context,
        window,
        "mixed-DPI logical-size policy",
        "With differently scaled monitors arranged on either side of primary when available, move the Window across boundaries. Does logical "
        "size remain stable while framebuffer pixels change?");

    static_cast<void>(context.expectTrue(
        "physical-size DPI policy applies",
        window.setDpiResizePolicy(Desktop::Types::DpiResizePolicy::PreservePhysicalClientSize).ok()));
    recordManualCheck(
        context,
        window,
        "mixed-DPI physical-size policy",
        "Move across the same DPI boundaries. Do framebuffer pixels remain stable while logical size changes?");

    const Desktop::Types::LogicalSize size = window.clientSize();
    const std::array points{
        Desktop::Types::LogicalPosition{0, 0},
        Desktop::Types::LogicalPosition{static_cast<std::int32_t>(size.width - 1), 0},
        Desktop::Types::LogicalPosition{0, static_cast<std::int32_t>(size.height - 1)},
        Desktop::Types::LogicalPosition{static_cast<std::int32_t>(size.width - 1), static_cast<std::int32_t>(size.height - 1)}};
    bool conversionsRoundTrip = true;

    for(const Desktop::Types::LogicalPosition point : points)
    {
        const auto screen = window.clientToScreen(point);

        if(!screen.status.ok())
        {
            conversionsRoundTrip = false;
            break;
        }
        const auto logical = window.screenToClient(*screen);
        conversionsRoundTrip = conversionsRoundTrip && logical.status.ok() && *logical == point;
    }
    static_cast<void>(context.expectTrue("client/screen edge conversions round-trip", conversionsRoundTrip));
    recordManualCheck(
        context,
        window,
        "mixed-DPI coordinate rounding",
        "At each available DPI scale, do client/screen edge coordinates follow the expected integral-pixel rounding without visible drift?");

    const auto monitors = Desktop::Display::getMonitors();
    bool monitorRectsValid = monitors.ok() && !monitors->empty();

    for(const Desktop::Types::Display::Info &monitor : *monitors)
    {
        monitorRectsValid = monitorRectsValid && monitor.bounds.size.width > 0 && monitor.bounds.size.height > 0 && monitor.workArea.size.width > 0 &&
                            monitor.workArea.size.height > 0;
    }
    static_cast<void>(context.expectTrue("physical monitor bounds and work areas are valid", monitorRectsValid));
    recordManualCheck(
        context,
        window,
        "physical virtual-screen monitor rectangles",
        "Do reported monitor bounds and work areas match the Windows virtual-screen arrangement, including negative origins, without independent "
        "scaling?");
    static_cast<void>(window.close());
}

/// @brief Exercises standard shapes, focus-sensitive cursor modes, and logical warping.
void testManualCursor(TestSupport::Context &context, const GameWIP::Test::DesktopTestOptions &options)
{
    if(!beginManualSuite(context, options, "Window cursor behavior"))
    {
        return;
    }

    const auto capabilitiesResult = Desktop::getCapabilities();
    const Desktop::Types::Capabilities capabilities = capabilitiesResult.value_or({});
    Desktop::Window window;

    if(!openManualWindow(context, window, "GameWIP cursor validation"))
    {
        return;
    }

    constexpr std::array shapes{
        Desktop::Types::CursorShape::Arrow,
        Desktop::Types::CursorShape::Text,
        Desktop::Types::CursorShape::Crosshair,
        Desktop::Types::CursorShape::Hand,
        Desktop::Types::CursorShape::Help,
        Desktop::Types::CursorShape::Wait,
        Desktop::Types::CursorShape::Progress,
        Desktop::Types::CursorShape::Move,
        Desktop::Types::CursorShape::ResizeAll,
        Desktop::Types::CursorShape::ResizeHorizontal,
        Desktop::Types::CursorShape::ResizeVertical,
        Desktop::Types::CursorShape::ResizeDiagonalNorthWestSouthEast,
        Desktop::Types::CursorShape::ResizeDiagonalNorthEastSouthWest,
        Desktop::Types::CursorShape::NotAllowed};
    constexpr std::array shapeNames{
        std::string_view{"Arrow"},
        std::string_view{"Text"},
        std::string_view{"Crosshair"},
        std::string_view{"Hand"},
        std::string_view{"Help"},
        std::string_view{"Wait"},
        std::string_view{"Progress"},
        std::string_view{"Move"},
        std::string_view{"ResizeAll"},
        std::string_view{"ResizeHorizontal"},
        std::string_view{"ResizeVertical"},
        std::string_view{"ResizeDiagonalNorthWestSouthEast"},
        std::string_view{"ResizeDiagonalNorthEastSouthWest"},
        std::string_view{"NotAllowed"}};

    for(std::size_t index = 0; index < shapes.size(); ++index)
    {
        static_cast<void>(context.expectTrue("standard cursor shape applies", window.setCursorShape(shapes[index]).ok()));
        const Desktop::Types::LogicalSize size = window.clientSize();
        static_cast<void>(context.expectTrue(
            "cursor moves into the visible validation surface",
            window.setCursorPosition({static_cast<std::int32_t>(size.width / 2), static_cast<std::int32_t>(size.height / 2)}).ok()));
        recordManualCheck(
            context,
            window,
            "standard cursor shape",
            std::format(
                "The diagnostics show cursorShape={}. Does the '{}' system cursor display correctly over the validation client?",
                static_cast<int>(shapes[index]),
                shapeNames[index]));
    }

    for(const auto &[mode, capability, name] : std::array{
            std::tuple{Desktop::Types::CursorMode::Hidden, Desktop::Types::Capability::Count, std::string_view{"hidden"}},
            std::tuple{Desktop::Types::CursorMode::Confined, Desktop::Types::Capability::CursorConfinement, std::string_view{"confined"}},
            std::tuple{
                Desktop::Types::CursorMode::HiddenConfined,
                Desktop::Types::Capability::CursorConfinement,
                std::string_view{"hidden-confined"}},
            std::tuple{Desktop::Types::CursorMode::Relative, Desktop::Types::Capability::RelativeCursor, std::string_view{"relative"}}})
    {
        if(capability != Desktop::Types::Capability::Count && !capabilities.supports(capability))
        {
            context.skip(std::format("{} cursor mode", name), "backend does not advertise the required capability");
            continue;
        }
        static_cast<void>(context.expectTrue("cursor mode applies", window.setCursorMode(mode).ok()));
        recordManualCheck(
            context,
            window,
            std::format("{} cursor mode", name),
            std::format(
                "Exercise {} mode while focused, then alt-tab, minimize, hide, restore, and refocus. Is the system cursor always released and "
                "reacquired correctly?",
                name));
        static_cast<void>(window.setCursorMode(Desktop::Types::CursorMode::Normal));
    }

    if(capabilities.supports(Desktop::Types::Capability::CursorWarping))
    {
        const Desktop::Types::LogicalSize size = window.clientSize();

        for(const Desktop::Types::LogicalPosition point :
            {Desktop::Types::LogicalPosition{0, 0},
             Desktop::Types::LogicalPosition{static_cast<std::int32_t>(size.width - 1), static_cast<std::int32_t>(size.height - 1)}})
        {
            static_cast<void>(context.expectTrue("cursor warp succeeds", window.setCursorPosition(point).ok()));
            const auto actual = window.cursorPosition();
            static_cast<void>(context.expectTrue("cursor warp query succeeds", actual.status.ok()));

            if(actual.status.ok())
            {
                static_cast<void>(context.expectEq("cursor warp reaches requested logical point", point, *actual));
            }
        }
        recordManualCheck(
            context,
            window,
            "cursor corner warping",
            "Did cursor warping reach both logical client corners correctly at the available DPI scales?");
    }
    else
    {
        context.skip("cursor corner warping", "backend does not advertise CursorWarping");
    }
    static_cast<void>(window.setCursorMode(Desktop::Types::CursorMode::Normal));
    static_cast<void>(window.close());
}
