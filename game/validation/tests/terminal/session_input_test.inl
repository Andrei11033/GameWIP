/// @file session_input_test.inl
/// @brief Focused Terminal Session test phases.

#if TERMINAL_INTERNAL_TEST_HOOKS

/// @brief Checks exclusive Session ownership and stream-input validation.
void testSessionInputOwnership(
    TestSupport::Context &context,
    Terminal::Session &session,
    Terminal::Session &competing,
    const Terminal::Types::SessionOptions &streamOptions)
{
    static_cast<void>(context.expectEq("competing session reports ResourceBusy", ErrorCode::ResourceBusy, competing.open(streamOptions).code));
    static_cast<void>(context.expectEq("direct read conflicts with session ownership", ErrorCode::ResourceBusy, Terminal::readText().status.code));

    Hooks::setInputBytes(Terminal::Types::Input::Stream::Stdin, "session");
    const Terminal::Types::Input::TextResult sessionText = session.readText();
    static_cast<void>(context.expectTrue("session text read succeeds", sessionText.status.ok()));
    static_cast<void>(context.expectEq("session text read payload", std::string{"session"}, sessionText.text));

    Hooks::setInputBytes(Terminal::Types::Input::Stream::Stdin, "preserved");
    static_cast<void>(context.expectEq(
        "stream session rejects event consumer",
        ErrorCode::Unsupported,
        session.readEvent({.timeout = Terminal::kNoWait}).status.code));
    static_cast<void>(context.expectEq("incompatible event read consumes nothing", std::string{"preserved"}, session.readText().text));

    Hooks::setInputBytes(Terminal::Types::Input::Stream::Stdin, "deadline");
    Terminal::Types::Input::TextOptions negativeTimeout;
    negativeTimeout.timeout = std::chrono::milliseconds{-1};
    static_cast<void>(
        context.expectEq("negative read deadline is rejected", ErrorCode::InvalidArgument, session.readText(negativeTimeout).status.code));
    static_cast<void>(context.expectEq("negative deadline consumes nothing", std::string{"deadline"}, session.readText().text));

    Hooks::setInputBytes(Terminal::Types::Input::Stream::Stdin, "cancelled");
    std::stop_source stopSource;
    stopSource.request_stop();
    Terminal::Types::Input::TextOptions cancelledOptions;
    cancelledOptions.stopToken = stopSource.get_token();
    const Terminal::Types::Input::TextResult cancelled = session.readText(cancelledOptions);
    static_cast<void>(context.expectTrue("pre-cancelled read keeps success status", cancelled.status.ok()));
    static_cast<void>(context.expectEq("pre-cancelled read outcome", Terminal::Types::Input::ReadOutcome::Cancelled, cancelled.outcome));
    static_cast<void>(context.expectEq("pre-cancelled read consumes nothing", std::string{"cancelled"}, session.readText().text));
}

/// @brief Checks failed input restoration retains ownership for retry.
void testSessionInputRestorationFailure(
    TestSupport::Context &context,
    Terminal::Session &session,
    Terminal::Session &competing,
    const Terminal::Types::SessionOptions &streamOptions)
{
    Hooks::clearCapturedOutput(Terminal::Types::Output::Stream::Stdout);
    static_cast<void>(context.expectTrue("session owns hidden cursor state", session.setCursorVisible(false).ok()));
    static_cast<void>(context.expectTrue("session owns alternate-screen state", session.enterAlternateScreen().ok()));

    Hooks::forceNextInputModeFailure(ErrorCode::NativeFailure);
    static_cast<void>(context.expectEq("session input restoration failure propagates", ErrorCode::NativeFailure, session.close().code));
    static_cast<void>(context.expectTrue("failed close leaves session open", session.isOpen()));
    static_cast<void>(context.expectEq(
        "session restores persistent output in reverse order before input",
        std::string{"\x1b[?25l\x1b[?1049h\x1b[?1049l\x1b[?25h"},
        Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout)));
    static_cast<void>(context.expectEq("failed close retains ownership", ErrorCode::ResourceBusy, competing.open(streamOptions).code));
    static_cast<void>(context.expectTrue("session close retry succeeds", session.close().ok()));
    static_cast<void>(context.expectFalse("successful close clears open state", session.isOpen()));
    static_cast<void>(context.expectTrue(
        "successful close restores exact input mode",
        Hooks::inputModeOverrideMatches(Terminal::Types::Input::Stream::Stdin, true, true, true)));
    static_cast<void>(context.expectTrue(
        "successful close restores exact managed-event flags",
        Hooks::inputManagedEventModeOverrideMatches(Terminal::Types::Input::Stream::Stdin, false, false, false)));
    static_cast<void>(context.expectEq("session output after close reports NotOpen", ErrorCode::NotOpen, session.writeText("closed").code));
}

/// @brief Checks event delivery and Session option validation.
void testSessionEventDelivery(TestSupport::Context &context, Terminal::Session &competing, const Terminal::Types::SessionOptions &streamOptions)
{
    Terminal::Session events;
    static_cast<void>(context.expectTrue("default event session opens with event-capable hook", events.open().ok()));
    static_cast<void>(context.expectEq("event session rejects stream reads", ErrorCode::Unsupported, events.readText().status.code));

    std::stop_source eventStopSource;
    eventStopSource.request_stop();
    Terminal::Types::Input::EventOptions eventCancelledOptions;
    eventCancelledOptions.stopToken = eventStopSource.get_token();
    const Terminal::Types::Input::EventResult cancelledEvent = events.readEvent(eventCancelledOptions);
    static_cast<void>(context.expectTrue("event cancellation keeps success status", cancelledEvent.status.ok()));
    static_cast<void>(context.expectEq("event cancellation outcome", Terminal::Types::Input::ReadOutcome::Cancelled, cancelledEvent.outcome));
    static_cast<void>(context.expectTrue("event session closes", events.close().ok()));

    Terminal::Types::SessionOptions reportControlOptions = streamOptions;
    reportControlOptions.controlKeyMode = Terminal::Types::Input::ControlKeyMode::ReportAsInput;
    Terminal::Session reportControl;
    static_cast<void>(context.expectTrue("report-as-input session opens", reportControl.open(reportControlOptions).ok()));
    static_cast<void>(context.expectTrue(
        "report-as-input disables native control processing",
        Hooks::inputModeOverrideMatches(Terminal::Types::Input::Stream::Stdin, false, false, false)));
    static_cast<void>(context.expectTrue("report-as-input session closes", reportControl.close().ok()));

    Terminal::Types::SessionOptions invalidOptions = streamOptions;
    invalidOptions.deliveryMode = static_cast<Terminal::Types::Input::DeliveryMode>(99);
    static_cast<void>(context.expectEq("invalid session delivery mode is rejected", ErrorCode::InvalidArgument, competing.open(invalidOptions).code));
}

#endif
