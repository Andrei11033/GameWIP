/// @file session_test.inl
/// @brief Runs the stateful Terminal Session test suite.

#if TERMINAL_INTERNAL_TEST_HOOKS
#include "session_operations_test.inl"
#include "session_input_test.inl"
#include "session_output_test.inl"
#include "session_concurrency_test.inl"
#include "session_lifetime_test.inl"

/// @brief Verifies Session ownership, cancellation, and exact restoration.
void testSessions(TestSupport::Context &context)
{
    // Configure one console-like fixture for the full Session suite.
    Hooks::reset();
    Hooks::setInputCapabilitiesOverride(Terminal::Types::Input::Stream::Stdin, terminalInputCapabilities());
    Hooks::setInputModeOverride(Terminal::Types::Input::Stream::Stdin, true, true, true);
    setupCapturedOutput(Terminal::Types::Output::Stream::Stdout);
    Hooks::setTerminalSizeOverride(Terminal::Types::Output::Stream::Stdout, {.columns = 100, .rows = 30});
    Hooks::setCursorPositionOverride(Terminal::Types::Output::Stream::Stdout, {.column = 3, .row = 4});

    testClosedSessionErrors(context);

    Terminal::Types::SessionOptions streamOptions;
    streamOptions.deliveryMode = Terminal::Types::Input::DeliveryMode::Stream;
    Terminal::Session session;

    // Preserve this owner across input and restoration phases.
    testBoundStreamsAndOutput(context, session, streamOptions);

    Terminal::Session competing;
    testSessionInputOwnership(context, session, competing, streamOptions);
    testSessionInputRestorationFailure(context, session, competing, streamOptions);
    testSessionOutputStateRestoration(context, competing, streamOptions);
    testSessionOutputBinding(context, streamOptions);
    testSessionConcurrency(context, streamOptions);
    testSessionMoveOwnership(context, competing, streamOptions);
    testSessionDestructorCleanup(context, streamOptions);
    testSessionEventDelivery(context, competing, streamOptions);

    Hooks::reset();
}
#endif
