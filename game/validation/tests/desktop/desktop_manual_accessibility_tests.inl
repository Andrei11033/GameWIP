/// @file desktop_manual_accessibility_tests.inl
/// @brief Opt-in assistive-client validation over a renderer-independent semantic fixture.
void testManualAccessibility(TestSupport::Context &context, const GameWIP::Test::DesktopTestOptions &options)
{
    if (!beginManualSuite(context, options, "Desktop accessibility UI Automation"))
        return;
    namespace A = Desktop::Types::Accessibility;
    Desktop::Window window;
    Desktop::Types::Description description;
    description.title = "GameWIP accessibility semantic demo";
    description.visible = true;
    description.requestFocus = true;
    description.clientSize = {800, 450};
    if (!requireManualStatus(context, "accessibility demo opens", window.open(description)))
        return;
    auto bridge = window.accessibility();
    if (!requireManualStatus(context, "accessibility demo enables", bridge.enable()))
        return;
    const std::array<A::NodeId, 3> children{2, 3, 4};
    std::array<A::Node, 4> nodes{};
    nodes[0].id = 1;
    nodes[0].role = A::Role::Window;
    nodes[0].name = "Accessibility demo";
    nodes[0].children = children;
    nodes[1].id = 2;
    nodes[1].parent = 1;
    nodes[1].role = A::Role::Button;
    nodes[1].name = "Announce demo";
    nodes[1].language = "en-US";
    nodes[1].states.flags = static_cast<std::uint64_t>(A::State::Focusable) | static_cast<std::uint64_t>(A::State::Focused);
    nodes[1].actions.flags =
        (std::uint64_t{1} << static_cast<unsigned>(A::ActionKind::Focus)) | (std::uint64_t{1} << static_cast<unsigned>(A::ActionKind::Invoke));
    nodes[1].geometry = A::Geometry{{24, 24, 180, 40}};
    nodes[2].id = 3;
    nodes[2].parent = 1;
    nodes[2].role = A::Role::Document;
    nodes[2].name = "Sample document";
    constexpr std::string_view sample = "Read-only sample text with combining e\xCC\x81 and a non-BMP smile \xF0\x9F\x99\x82.";
    const A::TextFragment fragment{{0, static_cast<std::uint32_t>(sample.size())}, {24, 100, 700, 40}};
    nodes[2].text = A::TextContent{sample, A::TextDirection::LeftToRight, "en-US", {}, {&fragment, 1}};
    nodes[2].geometry = A::Geometry{{24, 100, 700, 40}};
    nodes[3].id = 4;
    nodes[3].parent = 1;
    nodes[3].role = A::Role::PasswordField;
    nodes[3].name = "Password";
    nodes[3].value = "MUST NOT BE EXPOSED";
    nodes[3].geometry = A::Geometry{{24, 180, 180, 40}};
    A::Generation generation = 1;
    if (!requireManualStatus(context, "accessibility semantic fixture publishes", bridge.publish({generation, 1, nodes})))
        return;
    std::size_t invocations = 0;
    std::string count;
    const auto observe = [&]
    {
        A::ActionRequest request;
        while (bridge.popAction(request))
        {
            if (request.node != 2)
                continue;
            if (request.action == A::ActionKind::Focus)
                static_cast<void>(window.requestFocus());
            if (request.action == A::ActionKind::Invoke)
            {
                count = std::format("Invoked {} times", ++invocations);
                nodes[1].description = count;
                static_cast<void>(bridge.publish({++generation, 1, nodes}));
                if (bridge.features().supports(A::Feature::Announcements))
                    static_cast<void>(bridge.announce({2, "Accessibility action received", "en-US"}));
            }
        }
        if (manualStatusWindow)
            manualStatusWindow->setObservation(
                std::format("Semantic-only fixture over the renderer-free host; invoke requests received={}", invocations));
    };
    context.manual(
        "Use Narrator and a UIA inspector (Accessibility Insights or Inspect). This fixture publishes semantic controls independently of the "
        "blue/cyan renderer-free surface; it is not a UI toolkit.");
    recordManualCheck(
        context,
        window,
        "accessibility roles and names",
        "Does the UIA tree contain Accessibility demo, Announce demo (Button), Sample document (Document/Text), and Password, in that order?",
        observe);
    recordManualCheck(
        context,
        window,
        "accessibility native action and announcement",
        "Invoke Announce demo from the client. Does the diagnostic invocation count increase, and does the listening client receive the announcement "
        "if supported? Skip speech if no listening client is available.",
        observe);
    recordManualCheck(
        context,
        window,
        "accessibility text and redaction",
        "Can the client read the sample document with its combining/emoji text while Password exposes no value or text content?",
        observe);
    recordManualCheck(
        context,
        window,
        "accessibility DPI and screen bounds",
        "Move/resize the Window and, if available, move it across differently scaled monitors. Do inspector screen bounds follow the Window and clip "
        "to its client area?",
        observe);
    if (!requireManualStatus(context, "accessibility demo disables", bridge.disable()))
        return;
    recordManualCheck(
        context,
        window,
        "accessibility provider teardown",
        "Refresh the inspector: are the semantic children gone, with any retained semantic provider reporting unavailable rather than crashing?",
        observe);
    static_cast<void>(window.close());
}
