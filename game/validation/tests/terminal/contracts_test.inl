/// @file contracts_test.inl
/// @brief Focused terminal contracts correctness suites.

/// @brief Verifies passive enum validators, style helpers, and line-ending text.
void testPassiveHelpers(TestSupport::Context &context)
{
    const Terminal::Types::Style::Color defaultColor = Terminal::defaultColor();
    static_cast<void>(context.expectEq("defaultColor kind", Terminal::Types::Style::ColorKind::Default, defaultColor.kind()));

    const Terminal::Types::Style::Color basic = Terminal::basicColor(Terminal::Types::Style::BasicColor::BrightCyan);
    static_cast<void>(context.expectEq("basicColor kind", Terminal::Types::Style::ColorKind::Basic, basic.kind()));
    static_cast<void>(context.expectEq("basicColor value", Terminal::Types::Style::BasicColor::BrightCyan, basic.basic()));

    const Terminal::Types::Style::Color invalidBasic = Terminal::basicColor(static_cast<Terminal::Types::Style::BasicColor>(-1));
    static_cast<void>(context.expectEq("invalid basicColor falls back to default", Terminal::Types::Style::ColorKind::Default, invalidBasic.kind()));

    const Terminal::Types::Style::Color rgb = Terminal::rgbColor(1, 2, 3);
    static_cast<void>(context.expectEq("rgbColor kind", Terminal::Types::Style::ColorKind::Rgb, rgb.kind()));
    static_cast<void>(context.expectEq("rgbColor red", std::uint8_t{1}, rgb.red()));
    static_cast<void>(context.expectEq("rgbColor green", std::uint8_t{2}, rgb.green()));
    static_cast<void>(context.expectEq("rgbColor blue", std::uint8_t{3}, rgb.blue()));

    const Terminal::Types::SessionOptions sessionDefaults;
    static_cast<void>(context.expectEq("session defaults to stdin", Terminal::Types::Input::Stream::Stdin, sessionDefaults.input));
    static_cast<void>(context.expectEq("session defaults to stdout", Terminal::Types::Output::Stream::Stdout, sessionDefaults.output));
    static_cast<void>(
        context.expectEq("session defaults to event delivery", Terminal::Types::Input::DeliveryMode::Events, sessionDefaults.deliveryMode));
    static_cast<void>(context.expectEq(
        "session defaults to native control processing",
        Terminal::Types::Input::ControlKeyMode::NativeProcessing,
        sessionDefaults.controlKeyMode));

    constexpr Terminal::Types::Events::KeyModifier modifiers =
        Terminal::Types::Events::KeyModifier::Shift | Terminal::Types::Events::KeyModifier::Control;
    static_cast<void>(context.expectTrue(
        "key modifier bitmask reports present shift",
        Terminal::Types::Events::hasModifier(modifiers, Terminal::Types::Events::KeyModifier::Shift)));
    static_cast<void>(context.expectTrue(
        "key modifier bitmask reports present control",
        Terminal::Types::Events::hasModifier(modifiers, Terminal::Types::Events::KeyModifier::Control)));
    static_cast<void>(context.expectFalse(
        "key modifier bitmask reports absent alt",
        Terminal::Types::Events::hasModifier(modifiers, Terminal::Types::Events::KeyModifier::Alt)));

    Terminal::Types::Events::Key keyEvent;
    keyEvent.key = Terminal::Types::Events::CharacterKey{U'\u03BB'};
    keyEvent.modifiers = modifiers;
    keyEvent.action = Terminal::Types::Events::KeyAction::Press;
    keyEvent.location = Terminal::Types::Events::KeyLocation::Standard;
    Terminal::Types::Event event{.data = keyEvent};

    const Terminal::Types::Events::Key *storedKeyEvent = event.getIf<Terminal::Types::Events::Key>();
    static_cast<void>(context.expectTrue("event getIf returns matching key event", storedKeyEvent != nullptr));
    static_cast<void>(context.expectTrue("event getIf rejects paste alternative", event.getIf<Terminal::Types::Events::Paste>() == nullptr));
    if (storedKeyEvent != nullptr)
    {
        const auto *character = std::get_if<Terminal::Types::Events::CharacterKey>(&storedKeyEvent->key);
        static_cast<void>(context.expectTrue("key variant stores character alternative", character != nullptr));
        if (character != nullptr)
        {
            static_cast<void>(context.expectTrue("character key preserves Unicode scalar", character->value == U'\u03BB'));
        }
        static_cast<void>(context.expectEq("key event preserves modifiers", modifiers, storedKeyEvent->modifiers));
        static_cast<void>(context.expectEq("key event preserves action", Terminal::Types::Events::KeyAction::Press, storedKeyEvent->action));
        static_cast<void>(context.expectEq("key event preserves location", Terminal::Types::Events::KeyLocation::Standard, storedKeyEvent->location));
        static_cast<void>(context.expectEq("key event defaults to one occurrence", std::uint32_t{1}, storedKeyEvent->repeatCount));
    }

    keyEvent.key = Terminal::Types::Events::FunctionKey{24};
    event.data = keyEvent;
    storedKeyEvent = event.getIf<Terminal::Types::Events::Key>();
    static_cast<void>(context.expectTrue("event getIf preserves function-key event", storedKeyEvent != nullptr));
    if (storedKeyEvent != nullptr)
    {
        const auto *functionKey = std::get_if<Terminal::Types::Events::FunctionKey>(&storedKeyEvent->key);
        static_cast<void>(context.expectTrue("key variant stores function-key alternative", functionKey != nullptr));
        if (functionKey != nullptr)
        {
            static_cast<void>(context.expectEq("function key preserves numeric value", std::uint16_t{24}, functionKey->number));
        }
    }

    event.data = Terminal::Types::Events::Paste{.text = "paste"};
    const Terminal::Types::Events::Paste *paste = event.getIf<Terminal::Types::Events::Paste>();
    static_cast<void>(context.expectTrue("event getIf returns paste event", paste != nullptr));
    if (paste != nullptr)
    {
        static_cast<void>(context.expectEq("paste event preserves UTF-8 text", std::string{"paste"}, paste->text));
    }

    event.data = Terminal::Types::Events::Resize{.size = {.columns = 120, .rows = 40}};
    const Terminal::Types::Events::Resize *resize = event.getIf<Terminal::Types::Events::Resize>();
    static_cast<void>(context.expectTrue("event getIf returns resize event", resize != nullptr));
    if (resize != nullptr)
    {
        static_cast<void>(context.expectEq("resize event preserves columns", std::uint32_t{120}, resize->size.columns));
        static_cast<void>(context.expectEq("resize event preserves rows", std::uint32_t{40}, resize->size.rows));
    }

    const Terminal::Types::Output::Segment text = Terminal::textSegment("text");
    static_cast<void>(context.expectEq("text segment kind", Terminal::Types::Output::SegmentKind::Text, text.kind()));
    static_cast<void>(context.expectEq("text segment view", std::string_view{"text"}, text.text()));

    Terminal::Types::Style::Request style;
    style.bold = true;
    const Terminal::Types::Output::Segment styled = Terminal::styledTextSegment("styled", style);
    static_cast<void>(context.expectEq("styled segment kind", Terminal::Types::Output::SegmentKind::StyledText, styled.kind()));
    static_cast<void>(context.expectTrue("styled segment stores style", styled.style().bold));

    const std::string byteText = "bytes";
    const Terminal::Types::Output::Segment bytes = Terminal::byteSegment(bytesOf(byteText));
    static_cast<void>(context.expectEq("byte segment kind", Terminal::Types::Output::SegmentKind::Bytes, bytes.kind()));
    static_cast<void>(context.expectEq("byte segment size", byteText.size(), bytes.bytes().size()));

    static_cast<void>(
        context.expectEq("formatted print failure returns status", ErrorCode::InvalidArgument, Terminal::print("{}", TerminalThrowingFormat{}).code));
    Terminal::Types::Output::LineOptions lineOptions;
    lineOptions.lineEnding = Terminal::Types::Output::LineEnding::Lf;
    static_cast<void>(context.expectEq(
        "formatted println failure returns status",
        ErrorCode::InvalidArgument,
        Terminal::println(lineOptions, "{}", TerminalThrowingFormat{}).code));
}

#if TERMINAL_INTERNAL_TEST_HOOKS
/// @brief Verifies capability observation plus output preparation, size, and position queries.
void testCapabilitiesAndQueries(TestSupport::Context &context)
{
    Hooks::reset();

    Hooks::setInputCapabilitiesOverride(Terminal::Types::Input::Stream::Stdin, terminalInputCapabilities());
    const Terminal::Types::Input::CapabilitiesResult inputCapabilities = Terminal::getInputCapabilities();
    static_cast<void>(context.expectTrue("input capabilities status", inputCapabilities.status.ok()));
    static_cast<void>(context.expectEq("input capability kind", Terminal::Types::StreamKind::Terminal, inputCapabilities.capabilities.kind));
    static_cast<void>(context.expectTrue("input capability event support", inputCapabilities.capabilities.supportsEventInput));
    static_cast<void>(context.expectTrue("input capability cancellation support", inputCapabilities.capabilities.supportsCancellation));

    Hooks::forceNextInputCapabilityFailure(ErrorCode::PermissionDenied);
    static_cast<void>(context.expectEq("input capability forced failure", ErrorCode::PermissionDenied, Terminal::getInputCapabilities().status.code));

    const auto invalidInputStream = static_cast<Terminal::Types::Input::Stream>(99);
    static_cast<void>(context.expectEq(
        "invalid input capability stream is rejected",
        ErrorCode::InvalidArgument,
        Terminal::getInputCapabilities(invalidInputStream).status.code));
    static_cast<void>(
        context.expectEq("invalid input read stream is rejected", ErrorCode::InvalidArgument, Terminal::readText(invalidInputStream).status.code));

    setupCapturedOutput(Terminal::Types::Output::Stream::Stdout);
    const Terminal::Types::Output::CapabilitiesResult outputCapabilities = Terminal::getOutputCapabilities();
    static_cast<void>(context.expectTrue("output capabilities status", outputCapabilities.status.ok()));
    static_cast<void>(context.expectTrue("output style capability", outputCapabilities.capabilities.style.rgbColor));
    static_cast<void>(context.expectTrue("output cursor capability", outputCapabilities.capabilities.supportsCursorMovement));
    static_cast<void>(context.expectEq(
        "capability query does not prepare output",
        std::size_t{0},
        Hooks::outputPreparationCallCount(Terminal::Types::Output::Stream::Stdout)));

    Hooks::forceNextOutputCapabilityFailure(ErrorCode::StatFailed);
    static_cast<void>(context.expectEq("output capability forced failure", ErrorCode::StatFailed, Terminal::getOutputCapabilities().status.code));

    Hooks::setOutputCapabilitiesOverride(Terminal::Types::Output::Stream::Stdout, unpreparedTerminalOutputCapabilities());
    Hooks::setPreparedOutputCapabilitiesOverride(Terminal::Types::Output::Stream::Stdout, terminalOutputCapabilities());
    const Terminal::Types::Output::CapabilitiesResult prepared = Terminal::prepareOutput();
    static_cast<void>(context.expectTrue("explicit output preparation succeeds", prepared.status.ok()));
    static_cast<void>(context.expectTrue("explicit output preparation enables styling", prepared.capabilities.style.rgbColor));
    static_cast<void>(context.expectEq(
        "explicit output preparation count",
        std::size_t{1},
        Hooks::outputPreparationCallCount(Terminal::Types::Output::Stream::Stdout)));
    static_cast<void>(context.expectTrue("prepared capabilities remain active", Terminal::getOutputCapabilities().capabilities.style.bold));
    static_cast<void>(context.expectEq(
        "query after preparation remains observational",
        std::size_t{1},
        Hooks::outputPreparationCallCount(Terminal::Types::Output::Stream::Stdout)));
    const Terminal::Types::Output::CapabilitiesResult preparedAgain = Terminal::prepareOutput();
    static_cast<void>(context.expectTrue("repeated output preparation succeeds", preparedAgain.status.ok()));
    static_cast<void>(context.expectTrue("repeated output preparation preserves capabilities", preparedAgain.capabilities.style.bold));

    Hooks::setOutputCapabilitiesOverride(Terminal::Types::Output::Stream::Stderr, redirectedOutputCapabilities());
    const Terminal::Types::Output::CapabilitiesResult redirectedPrepared = Terminal::prepareOutput(Terminal::Types::Output::Stream::Stderr);
    static_cast<void>(context.expectTrue("redirected output preparation succeeds", redirectedPrepared.status.ok()));
    static_cast<void>(context.expectEq(
        "redirected output preparation preserves stream kind",
        Terminal::Types::StreamKind::Redirected,
        redirectedPrepared.capabilities.kind));

    Terminal::Types::Output::Capabilities detachedCapabilities;
    detachedCapabilities.kind = Terminal::Types::StreamKind::Detached;
    Hooks::setOutputCapabilitiesOverride(Terminal::Types::Output::Stream::Stderr, detachedCapabilities);
    static_cast<void>(context.expectEq(
        "detached output preparation reports not open",
        ErrorCode::NotOpen,
        Terminal::prepareOutput(Terminal::Types::Output::Stream::Stderr).status.code));

    Hooks::setTerminalSizeOverride(Terminal::Types::Output::Stream::Stdout, {.columns = 120, .rows = 40});
    const Terminal::Types::SizeResult size = Terminal::getTerminalSize();
    static_cast<void>(context.expectTrue("terminal size status", size.status.ok()));
    static_cast<void>(context.expectEq("terminal size columns", std::uint32_t{120}, size.size.columns));
    static_cast<void>(context.expectEq("terminal size rows", std::uint32_t{40}, size.size.rows));

    Hooks::setCursorPositionOverride(Terminal::Types::Output::Stream::Stdout, {.column = 7, .row = 9});
    const Terminal::Types::Cursor::PositionResult position = Terminal::getCursorPosition();
    static_cast<void>(context.expectTrue("cursor position status", position.status.ok()));
    static_cast<void>(context.expectEq("cursor position column", std::uint32_t{7}, position.position.column));
    static_cast<void>(context.expectEq("cursor position row", std::uint32_t{9}, position.position.row));

    const Terminal::Types::Cursor::PositionResult explicitPosition = Terminal::getCursorPosition(
        Terminal::Types::Output::Stream::Stdout,
        Terminal::Types::Input::Stream::Stdin,
        {.timeout = Terminal::kNoWait, .flushMode = IO::Types::FlushMode::None});
    static_cast<void>(context.expectTrue("explicit cursor position status", explicitPosition.status.ok()));
    static_cast<void>(context.expectEq("explicit cursor position column", std::uint32_t{7}, explicitPosition.position.column));
    static_cast<void>(context.expectEq("explicit cursor position row", std::uint32_t{9}, explicitPosition.position.row));

    Hooks::forceNextTerminalSizeFailure(ErrorCode::StatFailed);
    static_cast<void>(context.expectEq("terminal size forced failure", ErrorCode::StatFailed, Terminal::getTerminalSize().status.code));

    Hooks::forceNextCursorPositionFailure(ErrorCode::StatFailed);
    static_cast<void>(context.expectEq("cursor position forced failure", ErrorCode::StatFailed, Terminal::getCursorPosition().status.code));

    Hooks::reset();
}
#endif

/// @brief Records a clear skip when Terminal test hooks were not compiled.
void testHookDependentSuitesSkipped(TestSupport::Context &context)
{
    context.skip("Terminal hook-dependent suites", "TERMINAL_INTERNAL_TEST_HOOKS=0");
}
