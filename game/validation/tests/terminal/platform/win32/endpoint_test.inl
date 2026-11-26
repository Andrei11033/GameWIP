/// @file endpoint_test.inl
/// @brief Win32 Terminal stdin endpoint tests.

#if TERMINAL_INTERNAL_TEST_HOOKS

/// ------------------------------------------------------------
/// Stdin endpoint ownership
/// ------------------------------------------------------------

/// @brief Verifies buffered bytes are owned by native stdin identity, including numeric handle reuse.
void testInputEndpointReplacement(TestSupport::Context &context)
{
    Hooks::reset();
    const HANDLE originalInput = GetStdHandle(STD_INPUT_HANDLE);

    // Install an initial endpoint with buffered input.
    HANDLE firstRead = nullptr;
    HANDLE firstWrite = nullptr;
    static_cast<void>(context.expectTrue("create first stdin pipe", CreatePipe(&firstRead, &firstWrite, nullptr, 0) != FALSE));

    if(firstRead == nullptr || firstWrite == nullptr)
    {
        return;
    }
    static_cast<void>(context.expectTrue("install first stdin pipe", SetStdHandle(STD_INPUT_HANDLE, firstRead) != FALSE));
    DWORD bytesWritten = 0;
    static_cast<void>(context.expectTrue("write first stdin pipe", WriteFile(firstWrite, "AB", 2, &bytesWritten, nullptr) != FALSE));
    CloseHandle(firstWrite);

    Terminal::Types::Input::TextOptions oneByte;
    oneByte.maxReturnedBytes = 1;
    oneByte.timeout = std::chrono::milliseconds{0};
    Hooks::forceNextEndpointIdentityFailure(ErrorCode::PermissionDenied);
    const Terminal::Types::Input::TextResult initialIdentityFailure = Terminal::readText(oneByte);
    static_cast<void>(
        context.expectEq("initial endpoint identity failure is returned", StatusCode::PermissionDenied, initialIdentityFailure.status().code()));
    static_cast<void>(context.expectFalse(
        "identity failure is not reported as cancellation",
        initialIdentityFailure.hasValue() && initialIdentityFailure.value().outcome == Terminal::Types::Input::ReadOutcome::Cancelled));

    const Terminal::Types::Input::TextResult retriedInitialIdentity = Terminal::readText(oneByte);
    static_cast<void>(context.expectTrue("retry after identity failure succeeds", retriedInitialIdentity.ok()));
    if(retriedInitialIdentity.hasValue())
    {
        static_cast<void>(context.expectEq("zero-timeout poll reads ready input", std::string{"A"}, retriedInitialIdentity.value().text));
    }

    // Preserve decoder state when endpoint identity checks fail.
    Hooks::Win32::setPendingHighSurrogate(Terminal::Types::Input::Stream::Stdin, UINT16_C(0xD83D));
    static_cast<void>(context.expectTrue(
        "first endpoint retains seeded high surrogate",
        Hooks::Win32::hasPendingHighSurrogate(Terminal::Types::Input::Stream::Stdin)));

    HANDLE failedProbeRead = nullptr;
    HANDLE failedProbeWrite = nullptr;
    static_cast<void>(context.expectTrue("create failed-probe stdin pipe", CreatePipe(&failedProbeRead, &failedProbeWrite, nullptr, 0) != FALSE));

    if(failedProbeRead == nullptr || failedProbeWrite == nullptr)
    {
        static_cast<void>(SetStdHandle(STD_INPUT_HANDLE, originalInput));
        CloseHandle(firstRead);
        Hooks::reset();
        return;
    }
    static_cast<void>(context.expectTrue("install failed-probe stdin pipe", SetStdHandle(STD_INPUT_HANDLE, failedProbeRead) != FALSE));
    bytesWritten = 0;
    static_cast<void>(context.expectTrue("write failed-probe stdin pipe", WriteFile(failedProbeWrite, "Z", 1, &bytesWritten, nullptr) != FALSE));
    CloseHandle(failedProbeWrite);

    Hooks::forceNextEndpointIdentityFailure(ErrorCode::NativeFailure);
    const Terminal::Types::Input::TextResult failedReplacementProbe = Terminal::readText(oneByte);
    static_cast<void>(context.expectEq("failed replacement probe is returned", StatusCode::NativeFailure, failedReplacementProbe.status().code()));
    static_cast<void>(context.expectTrue(
        "failed replacement probe preserves pending decoder state",
        Hooks::Win32::hasPendingHighSurrogate(Terminal::Types::Input::Stream::Stdin)));

    static_cast<void>(context.expectTrue("restore confirmed stdin endpoint", SetStdHandle(STD_INPUT_HANDLE, firstRead) != FALSE));
    const Terminal::Types::Input::TextResult restoredEndpoint = Terminal::readText(oneByte);
    static_cast<void>(context.expectTrue("restored endpoint clears stale probe failure", restoredEndpoint.ok()));
    static_cast<void>(context.expectTrue(
        "restored endpoint preserves pending decoder state",
        Hooks::Win32::hasPendingHighSurrogate(Terminal::Types::Input::Stream::Stdin)));
    static_cast<void>(context.expectTrue("retry failed-probe stdin endpoint", SetStdHandle(STD_INPUT_HANDLE, failedProbeRead) != FALSE));
    const Terminal::Types::Input::TextResult completedReplacementProbe = Terminal::readText(oneByte);
    static_cast<void>(context.expectTrue("replacement after recovery succeeds", completedReplacementProbe.ok()));
    if(completedReplacementProbe.hasValue())
    {
        static_cast<void>(context.expectEq("replacement after recovery reads normally", std::string{"Z"}, completedReplacementProbe.value().text));
    }
    CloseHandle(failedProbeRead);

    // Reuse the numeric handle value and discard state from the closed endpoint.
    static_cast<void>(context.expectTrue("restore first stdin endpoint before reuse", SetStdHandle(STD_INPUT_HANDLE, firstRead) != FALSE));
    Hooks::Win32::setPendingHighSurrogate(Terminal::Types::Input::Stream::Stdin, UINT16_C(0xD83D));
    const HANDLE reusedValue = firstRead;
    CloseHandle(firstRead);

    HANDLE replacementRead = nullptr;
    HANDLE replacementWrite = nullptr;

    for(std::size_t attempt = 0; attempt < 256; ++attempt)
    {
        HANDLE candidateRead = nullptr;
        HANDLE candidateWrite = nullptr;

        if(CreatePipe(&candidateRead, &candidateWrite, nullptr, 0) == FALSE)
        {
            break;
        }

        if(candidateRead == reusedValue)
        {
            replacementRead = candidateRead;
            replacementWrite = candidateWrite;
            break;
        }
        CloseHandle(candidateRead);
        CloseHandle(candidateWrite);
    }

    if(replacementRead == nullptr)
    {
        context.skip("stdin numeric handle reuse", "Win32 did not reuse the released handle value within 256 attempts");
        static_cast<void>(SetStdHandle(STD_INPUT_HANDLE, originalInput));
        Hooks::reset();
        return;
    }
    static_cast<void>(context.expectTrue("install reused stdin handle", SetStdHandle(STD_INPUT_HANDLE, replacementRead) != FALSE));
    bytesWritten = 0;
    static_cast<void>(context.expectTrue("write replacement stdin pipe", WriteFile(replacementWrite, "C", 1, &bytesWritten, nullptr) != FALSE));
    CloseHandle(replacementWrite);

    const Terminal::Types::Input::TextResult replacement = Terminal::readText(oneByte);
    static_cast<void>(context.expectTrue("replacement endpoint read succeeds", replacement.ok()));
    if(replacement.hasValue())
    {
        static_cast<void>(context.expectEq("replacement endpoint discards stale byte", std::string{"C"}, replacement.value().text));
    }
    static_cast<void>(context.expectFalse(
        "replacement endpoint discards stale high surrogate",
        Hooks::Win32::hasPendingHighSurrogate(Terminal::Types::Input::Stream::Stdin)));

    // Detaching stdin clears endpoint-owned decoder state.
    static_cast<void>(context.expectTrue("detach stdin", SetStdHandle(STD_INPUT_HANDLE, nullptr) != FALSE));
    static_cast<void>(context.expectEq("detached stdin reports NotOpen after state reset", StatusCode::NotOpen, Terminal::readText().status().code()));

    static_cast<void>(SetStdHandle(STD_INPUT_HANDLE, originalInput));
    CloseHandle(replacementRead);
    Hooks::reset();
}
#endif
