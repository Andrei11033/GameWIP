/// @file input_test.inl
/// @brief Focused terminal input correctness suites.

#if TERMINAL_INTERNAL_TEST_HOOKS
/// @brief Verifies byte, text, and line reads across UTF-8, timeout, truncation, and EOF paths.
void testInputReads(TestSupport::Context &context)
{
    Hooks::reset();

    setupInput("abcd");
    std::array<std::byte, 4> buffer{};
    Terminal::Types::Input::ByteOptions byteOptions;
    byteOptions.timeout = Terminal::kNoWait;
    byteOptions.allowPartial = false;
    const Terminal::Types::Input::ByteResult bytes = Terminal::readBytes(std::span<std::byte>(buffer), byteOptions);
    static_cast<void>(context.expectTrue("byte read status", bytes.status.ok()));
    static_cast<void>(context.expectEq("byte read outcome", Terminal::Types::Input::ReadOutcome::Completed, bytes.outcome));
    static_cast<void>(context.expectEq("byte read count", buffer.size(), bytes.bytesRead));
    static_cast<void>(context.expectEq("byte read contents", copyBytes("abcd"), std::vector<std::byte>(buffer.begin(), buffer.end())));

    const Terminal::Types::Input::ByteResult eof = Terminal::readBytes(std::span<std::byte>(buffer), byteOptions);
    static_cast<void>(context.expectTrue("byte EOF status", eof.status.ok()));
    static_cast<void>(context.expectEq("byte EOF outcome", Terminal::Types::Input::ReadOutcome::EndOfStream, eof.outcome));

    setupInput("one\r\ntwo\rc");
    Terminal::Types::Input::LineOptions keepOptions;
    keepOptions.lineEndingMode = Terminal::Types::Input::LineEndingMode::Keep;
    const Terminal::Types::Input::LineResult firstLine = Terminal::readLine(keepOptions);
    static_cast<void>(context.expectTrue("first line status", firstLine.status.ok()));
    static_cast<void>(context.expectEq("first line text", std::string{"one\r\n"}, firstLine.line));
    static_cast<void>(context.expectEq("first line ending", Terminal::Types::Input::ConsumedLineEnding::CrLf, firstLine.consumedLineEnding));

    Terminal::Types::Input::LineOptions normalizeOptions;
    normalizeOptions.lineEndingMode = Terminal::Types::Input::LineEndingMode::NormalizeToLf;
    const Terminal::Types::Input::LineResult secondLine = Terminal::readLine(normalizeOptions);
    static_cast<void>(context.expectTrue("second line status", secondLine.status.ok()));
    static_cast<void>(context.expectEq("second line text", std::string{"two\n"}, secondLine.line));
    static_cast<void>(context.expectEq("second line ending", Terminal::Types::Input::ConsumedLineEnding::Cr, secondLine.consumedLineEnding));

    const Terminal::Types::Input::LineResult finalLine = Terminal::readLine();
    static_cast<void>(context.expectTrue("final line status", finalLine.status.ok()));
    static_cast<void>(context.expectEq("final line text", std::string{"c"}, finalLine.line));
    static_cast<void>(context.expectEq("final line outcome", Terminal::Types::Input::ReadOutcome::EndOfStream, finalLine.outcome));

    setupInput("kept\n");
    Terminal::Types::Input::LineOptions invalidLineModeOptions;
    invalidLineModeOptions.lineEndingMode = static_cast<Terminal::Types::Input::LineEndingMode>(-1);
    static_cast<void>(
        context.expectEq("invalid line read mode is rejected", ErrorCode::InvalidArgument, Terminal::readLine(invalidLineModeOptions).status.code));
    static_cast<void>(context.expectEq("invalid line read mode consumes no input", std::string{"kept"}, Terminal::readLine().line));

    setupInput("line\n");
    Terminal::Types::Input::LineOptions zeroLineLimitOptions;
    zeroLineLimitOptions.maxReturnedBytes = 0;
    static_cast<void>(
        context.expectEq("zero line limit is rejected", ErrorCode::InvalidArgument, Terminal::readLine(zeroLineLimitOptions).status.code));
    static_cast<void>(context.expectEq("zero line limit consumes no input", std::string{"line"}, Terminal::readLine().line));

    setupInput("text");
    Terminal::Types::Input::TextOptions zeroTextLimitOptions;
    zeroTextLimitOptions.maxReturnedBytes = 0;
    static_cast<void>(
        context.expectEq("zero text limit is rejected", ErrorCode::InvalidArgument, Terminal::readText(zeroTextLimitOptions).status.code));
    static_cast<void>(context.expectEq("zero text limit consumes no input", std::string{"text"}, Terminal::readText().text));

    setupInput("abc\nnext\n");
    Terminal::Types::Input::LineOptions keepLimitedLfOptions;
    keepLimitedLfOptions.maxReturnedBytes = 3;
    keepLimitedLfOptions.lineEndingMode = Terminal::Types::Input::LineEndingMode::Keep;
    const Terminal::Types::Input::LineResult keepLimitedLf = Terminal::readLine(keepLimitedLfOptions);
    static_cast<void>(context.expectTrue("limited LF keep status", keepLimitedLf.status.ok()));
    static_cast<void>(context.expectEq("limited LF keep text", std::string{"abc"}, keepLimitedLf.line));
    static_cast<void>(context.expectEq("limited LF keep size", std::size_t{3}, keepLimitedLf.line.size()));
    static_cast<void>(context.expectTrue("limited LF keep reports truncation", keepLimitedLf.wasTruncated));
    static_cast<void>(
        context.expectEq("limited LF keep ending consumed", Terminal::Types::Input::ConsumedLineEnding::Lf, keepLimitedLf.consumedLineEnding));
    const Terminal::Types::Input::LineResult afterLimitedLf = Terminal::readLine();
    static_cast<void>(context.expectTrue("after limited LF status", afterLimitedLf.status.ok()));
    static_cast<void>(context.expectEq("after limited LF text", std::string{"next"}, afterLimitedLf.line));

    setupInput("abc\r\nnext\n");
    Terminal::Types::Input::LineOptions keepLimitedCrLfOptions;
    keepLimitedCrLfOptions.maxReturnedBytes = 4;
    keepLimitedCrLfOptions.lineEndingMode = Terminal::Types::Input::LineEndingMode::Keep;
    const Terminal::Types::Input::LineResult keepLimitedCrLf = Terminal::readLine(keepLimitedCrLfOptions);
    static_cast<void>(context.expectTrue("limited CRLF keep status", keepLimitedCrLf.status.ok()));
    static_cast<void>(context.expectEq("limited CRLF keep text avoids partial ending", std::string{"abc"}, keepLimitedCrLf.line));
    static_cast<void>(context.expectTrue("limited CRLF keep reports truncation", keepLimitedCrLf.wasTruncated));
    static_cast<void>(
        context.expectEq("limited CRLF keep ending consumed", Terminal::Types::Input::ConsumedLineEnding::CrLf, keepLimitedCrLf.consumedLineEnding));
    const Terminal::Types::Input::LineResult afterLimitedCrLf = Terminal::readLine();
    static_cast<void>(context.expectTrue("after limited CRLF status", afterLimitedCrLf.status.ok()));
    static_cast<void>(context.expectEq("after limited CRLF text", std::string{"next"}, afterLimitedCrLf.line));

    setupInput("abcd\r\nnext\n");
    Terminal::Types::Input::LineOptions normalizeLimitedOptions;
    normalizeLimitedOptions.maxReturnedBytes = 4;
    normalizeLimitedOptions.lineEndingMode = Terminal::Types::Input::LineEndingMode::NormalizeToLf;
    const Terminal::Types::Input::LineResult normalizeLimited = Terminal::readLine(normalizeLimitedOptions);
    static_cast<void>(context.expectTrue("limited normalize status", normalizeLimited.status.ok()));
    static_cast<void>(context.expectEq("limited normalize text", std::string{"abcd"}, normalizeLimited.line));
    static_cast<void>(context.expectEq("limited normalize size", std::size_t{4}, normalizeLimited.line.size()));
    static_cast<void>(context.expectTrue("limited normalize reports truncation", normalizeLimited.wasTruncated));
    static_cast<void>(
        context.expectEq("limited normalize ending consumed", Terminal::Types::Input::ConsumedLineEnding::CrLf, normalizeLimited.consumedLineEnding));
    const Terminal::Types::Input::LineResult afterLimitedNormalize = Terminal::readLine();
    static_cast<void>(context.expectTrue("after limited normalize status", afterLimitedNormalize.status.ok()));
    static_cast<void>(context.expectEq("after limited normalize text", std::string{"next"}, afterLimitedNormalize.line));

    std::string longCrLfLine(4095, 'x');
    longCrLfLine.append("\r\nnext\n");
    setupInput(longCrLfLine);
    const Terminal::Types::Input::LineResult longLine = Terminal::readLine();
    static_cast<void>(context.expectTrue("long line status", longLine.status.ok()));
    static_cast<void>(context.expectEq("long line size", std::size_t{4095}, longLine.line.size()));
    static_cast<void>(
        context.expectEq("long line detects CRLF across read chunks", Terminal::Types::Input::ConsumedLineEnding::CrLf, longLine.consumedLineEnding));
    static_cast<void>(context.expectEq("line after chunk-boundary CRLF", std::string{"next"}, Terminal::readLine().line));

    setupInput("\xc3\xa9z");
    Terminal::Types::Input::TextOptions textOptions;
    textOptions.maxReturnedBytes = 2;
    const Terminal::Types::Input::TextResult utf8First = Terminal::readText(textOptions);
    static_cast<void>(context.expectTrue("UTF-8 text status", utf8First.status.ok()));
    static_cast<void>(context.expectEq("UTF-8 text preserves boundary", std::string{"\xc3\xa9"}, utf8First.text));
    static_cast<void>(context.expectTrue("UTF-8 text reports truncation", utf8First.wasTruncated));

    textOptions.maxReturnedBytes = 8;
    const Terminal::Types::Input::TextResult utf8Second = Terminal::readText(textOptions);
    static_cast<void>(context.expectTrue("pending UTF-8 text status", utf8Second.status.ok()));
    static_cast<void>(context.expectEq("pending UTF-8 text", std::string{"z"}, utf8Second.text));

    setupInput("\xc3(");
    const Terminal::Types::Input::TextResult invalidText = Terminal::readText(textOptions);
    static_cast<void>(context.expectEq("invalid UTF-8 text fails", ErrorCode::EncodingFailed, invalidText.status.code));

    setupInput("", false);
    const Terminal::Types::Input::ByteResult wouldBlock = Terminal::readBytes(std::span<std::byte>(buffer), byteOptions);
    static_cast<void>(context.expectTrue("would-block status", wouldBlock.status.ok()));
    static_cast<void>(context.expectEq("would-block outcome", Terminal::Types::Input::ReadOutcome::WouldBlock, wouldBlock.outcome));

    Terminal::Types::Input::TextOptions timeoutOptions;
    timeoutOptions.timeout = std::chrono::milliseconds{1};
    const Terminal::Types::Input::TextResult timedOut = Terminal::readText(timeoutOptions);
    static_cast<void>(context.expectTrue("timed-out status", timedOut.status.ok()));
    static_cast<void>(context.expectEq("timed-out outcome", Terminal::Types::Input::ReadOutcome::TimedOut, timedOut.outcome));

    Hooks::forceNextReadFailure(ErrorCode::PermissionDenied);
    const Terminal::Types::Input::TextResult failedRead = Terminal::readText(textOptions);
    static_cast<void>(context.expectEq("forced read failure", ErrorCode::PermissionDenied, failedRead.status.code));

    Hooks::reset();
}
#endif

#if TERMINAL_INTERNAL_TEST_HOOKS
#if defined(_WIN32)
/// @brief Verifies Win32 native key records normalize into the portable Terminal key contract.
void testWin32EventDecoder(TestSupport::Context &context)
{
    const auto expectKeyEvent = [&context](std::string_view label, const Hooks::Win32KeyDecodeResult &decoded) -> const Terminal::Types::Events::Key *
    {
        static_cast<void>(context.expectTrue(std::format("{} status", label), decoded.status.ok()));
        static_cast<void>(context.expectEq(std::format("{} disposition", label), Hooks::Win32KeyDecodeDisposition::Produced, decoded.disposition));

        if (!decoded.event.has_value())
        {
            context.fail(std::string(label), "decoder produced no portable event");
            return nullptr;
        }

        const Terminal::Types::Events::Key *key = decoded.event->getIf<Terminal::Types::Events::Key>();
        static_cast<void>(context.expectTrue(std::format("{} payload", label), key != nullptr));
        return key;
    };

    Hooks::resetWin32KeyDecoder();
    {
        const Hooks::Win32KeyDecodeResult decoded = Hooks::decodeWin32KeyRecord(true, 'A', u'a');
        const Terminal::Types::Events::Key *key = expectKeyEvent("character A", decoded);
        if (key != nullptr)
        {
            const auto *character = std::get_if<Terminal::Types::Events::CharacterKey>(&key->key);
            static_cast<void>(context.expectTrue("character A alternative", character != nullptr));
            if (character != nullptr)
            {
                static_cast<void>(context.expectEq("character A scalar", U'a', character->value));
            }
            static_cast<void>(context.expectEq("character A press", Terminal::Types::Events::KeyAction::Press, key->action));
            static_cast<void>(context.expectEq("character A standard location", Terminal::Types::Events::KeyLocation::Standard, key->location));
        }
    }

    Hooks::resetWin32KeyDecoder();
    {
        const Hooks::Win32KeyDecodeResult decoded = Hooks::decodeWin32KeyRecord(true, 'A', static_cast<char16_t>(0x0001), LEFT_CTRL_PRESSED);
        const Terminal::Types::Events::Key *key = expectKeyEvent("Ctrl+A", decoded);
        if (key != nullptr)
        {
            const auto *character = std::get_if<Terminal::Types::Events::CharacterKey>(&key->key);
            static_cast<void>(context.expectTrue("Ctrl+A character alternative", character != nullptr));
            if (character != nullptr)
            {
                static_cast<void>(context.expectEq("Ctrl+A normalized scalar", U'a', character->value));
            }
            static_cast<void>(context.expectTrue(
                "Ctrl+A control modifier",
                Terminal::Types::Events::hasModifier(key->modifiers, Terminal::Types::Events::KeyModifier::Control)));
        }
    }

    Hooks::resetWin32KeyDecoder();
    {
        const Hooks::Win32KeyDecodeResult decoded = Hooks::decodeWin32KeyRecord(true, 'Q', u'@', RIGHT_ALT_PRESSED | LEFT_CTRL_PRESSED);
        const Terminal::Types::Events::Key *key = expectKeyEvent("AltGr character", decoded);
        if (key != nullptr)
        {
            const auto *character = std::get_if<Terminal::Types::Events::CharacterKey>(&key->key);
            static_cast<void>(context.expectTrue("AltGr character alternative", character != nullptr));
            if (character != nullptr)
            {
                static_cast<void>(context.expectEq("AltGr translated scalar", U'@', character->value));
            }
            static_cast<void>(context.expectFalse(
                "AltGr removes synthetic control",
                Terminal::Types::Events::hasModifier(key->modifiers, Terminal::Types::Events::KeyModifier::Control)));
            static_cast<void>(context.expectFalse(
                "AltGr removes synthetic alt",
                Terminal::Types::Events::hasModifier(key->modifiers, Terminal::Types::Events::KeyModifier::Alt)));
        }
    }

    Hooks::resetWin32KeyDecoder();
    {
        const Hooks::Win32KeyDecodeResult decoded = Hooks::decodeWin32KeyRecord(true, VK_LCONTROL, u'\0', LEFT_CTRL_PRESSED);
        const Terminal::Types::Events::Key *key = expectKeyEvent("left control", decoded);
        if (key != nullptr)
        {
            const auto *modifier = std::get_if<Terminal::Types::Events::ModifierKey>(&key->key);
            static_cast<void>(context.expectTrue("left control modifier alternative", modifier != nullptr));
            if (modifier != nullptr)
            {
                static_cast<void>(context.expectEq("left control logical key", Terminal::Types::Events::ModifierKey::Control, *modifier));
            }
            static_cast<void>(context.expectEq("left control location", Terminal::Types::Events::KeyLocation::Left, key->location));
            static_cast<void>(context.expectFalse(
                "standalone modifier excludes itself",
                Terminal::Types::Events::hasModifier(key->modifiers, Terminal::Types::Events::KeyModifier::Control)));
        }
    }

    Hooks::resetWin32KeyDecoder();
    {
        const Hooks::Win32KeyDecodeResult left = Hooks::decodeWin32KeyRecord(true, VK_CONTROL, u'\0', LEFT_CTRL_PRESSED);
        const Terminal::Types::Events::Key *key = expectKeyEvent("generic left control", left);
        if (key != nullptr)
        {
            static_cast<void>(context.expectEq("generic left control press", Terminal::Types::Events::KeyAction::Press, key->action));
            static_cast<void>(context.expectEq("generic left control location", Terminal::Types::Events::KeyLocation::Left, key->location));
        }

        const Hooks::Win32KeyDecodeResult right =
            Hooks::decodeWin32KeyRecord(true, VK_CONTROL, u'\0', LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED | ENHANCED_KEY);
        key = expectKeyEvent("generic right control", right);
        if (key != nullptr)
        {
            static_cast<void>(context.expectEq("other-side control is a press", Terminal::Types::Events::KeyAction::Press, key->action));
            static_cast<void>(context.expectEq("generic right control location", Terminal::Types::Events::KeyLocation::Right, key->location));
        }
    }

    Hooks::resetWin32KeyDecoder();
    {
        const Hooks::Win32KeyDecodeResult super = Hooks::decodeWin32KeyRecord(true, VK_LWIN);
        static_cast<void>(expectKeyEvent("left super", super));

        const Hooks::Win32KeyDecodeResult character = Hooks::decodeWin32KeyRecord(true, 'A', u'a');
        const Terminal::Types::Events::Key *key = expectKeyEvent("Super+A", character);
        if (key != nullptr)
        {
            static_cast<void>(context.expectTrue(
                "tracked super accompanies following key",
                Terminal::Types::Events::hasModifier(key->modifiers, Terminal::Types::Events::KeyModifier::Super)));
        }
    }

    Hooks::resetWin32KeyDecoder();
    {
        const Hooks::Win32KeyDecodeResult dedicated = Hooks::decodeWin32KeyRecord(true, VK_LEFT, u'\0', ENHANCED_KEY);
        const Terminal::Types::Events::Key *key = expectKeyEvent("dedicated left arrow", dedicated);
        if (key != nullptr)
        {
            const auto *named = std::get_if<Terminal::Types::Events::NamedKey>(&key->key);
            static_cast<void>(context.expectTrue("left arrow named alternative", named != nullptr));
            if (named != nullptr)
            {
                static_cast<void>(context.expectEq("left arrow logical key", Terminal::Types::Events::NamedKey::ArrowLeft, *named));
            }
            static_cast<void>(context.expectEq("dedicated arrow location", Terminal::Types::Events::KeyLocation::Standard, key->location));
        }

        Hooks::resetWin32KeyDecoder();
        const Hooks::Win32KeyDecodeResult numpad = Hooks::decodeWin32KeyRecord(true, VK_LEFT);
        key = expectKeyEvent("numpad left arrow", numpad);
        if (key != nullptr)
        {
            static_cast<void>(context.expectEq("numpad arrow location", Terminal::Types::Events::KeyLocation::Numpad, key->location));
        }
    }

    Hooks::resetWin32KeyDecoder();
    {
        const Hooks::Win32KeyDecodeResult decoded = Hooks::decodeWin32KeyRecord(true, VK_F24);
        const Terminal::Types::Events::Key *key = expectKeyEvent("F24", decoded);
        if (key != nullptr)
        {
            const auto *function = std::get_if<Terminal::Types::Events::FunctionKey>(&key->key);
            static_cast<void>(context.expectTrue("F24 function alternative", function != nullptr));
            if (function != nullptr)
            {
                static_cast<void>(context.expectEq("F24 number", std::uint16_t{24}, function->number));
            }
        }
    }

    Hooks::resetWin32KeyDecoder();
    {
        const Hooks::Win32KeyDecodeResult first = Hooks::decodeWin32KeyRecord(true, 'A', u'a', 0, 3);
        const Terminal::Types::Events::Key *key = expectKeyEvent("combined repeat press", first);
        if (key != nullptr)
        {
            static_cast<void>(context.expectEq("combined repeat first action", Terminal::Types::Events::KeyAction::Press, key->action));
            static_cast<void>(context.expectEq("combined repeat press count", std::uint32_t{1}, key->repeatCount));
        }

        const std::optional<Terminal::Types::Event> pending = Hooks::takePendingWin32KeyEvent();
        static_cast<void>(context.expectTrue("combined repeat retains follow-up event", pending.has_value()));
        if (pending.has_value())
        {
            const Terminal::Types::Events::Key *repeat = pending->getIf<Terminal::Types::Events::Key>();
            static_cast<void>(context.expectTrue("combined repeat follow-up payload", repeat != nullptr));
            if (repeat != nullptr)
            {
                static_cast<void>(context.expectEq("combined repeat action", Terminal::Types::Events::KeyAction::Repeat, repeat->action));
                static_cast<void>(context.expectEq("combined repeat count", std::uint32_t{2}, repeat->repeatCount));
            }
        }

        const Hooks::Win32KeyDecodeResult released = Hooks::decodeWin32KeyRecord(false, 'A', u'a');
        key = expectKeyEvent("character release", released);
        if (key != nullptr)
        {
            static_cast<void>(context.expectEq("character release action", Terminal::Types::Events::KeyAction::Release, key->action));
        }
    }

    Hooks::resetWin32KeyDecoder();
    {
        const Hooks::Win32KeyDecodeResult high = Hooks::decodeWin32KeyRecord(true, VK_PACKET, static_cast<char16_t>(0xd83d));
        static_cast<void>(context.expectEq("surrogate high waits", Hooks::Win32KeyDecodeDisposition::Pending, high.disposition));

        const Hooks::Win32KeyDecodeResult low = Hooks::decodeWin32KeyRecord(true, VK_PACKET, static_cast<char16_t>(0xde00));
        const Terminal::Types::Events::Key *key = expectKeyEvent("surrogate pair", low);
        if (key != nullptr)
        {
            const auto *character = std::get_if<Terminal::Types::Events::CharacterKey>(&key->key);
            static_cast<void>(context.expectTrue("surrogate pair character alternative", character != nullptr));
            if (character != nullptr)
            {
                static_cast<void>(context.expectEq("surrogate pair scalar", static_cast<char32_t>(0x1f600), character->value));
            }
        }

        Hooks::resetWin32KeyDecoder();
        const Hooks::Win32KeyDecodeResult malformed = Hooks::decodeWin32KeyRecord(true, VK_PACKET, static_cast<char16_t>(0xde00));
        static_cast<void>(context.expectEq("lone low surrogate fails", ErrorCode::EncodingFailed, malformed.status.code));
        static_cast<void>(context.expectEq("lone low surrogate disposition", Hooks::Win32KeyDecodeDisposition::Failed, malformed.disposition));
    }
}
#endif
#endif

#if TERMINAL_INTERNAL_TEST_HOOKS
#if defined(_WIN32)
/// @brief Verifies buffered bytes are owned by native stdin identity, including numeric handle reuse.
void testInputEndpointReplacement(TestSupport::Context &context)
{
    Hooks::reset();
    const HANDLE originalInput = GetStdHandle(STD_INPUT_HANDLE);

    HANDLE firstRead = nullptr;
    HANDLE firstWrite = nullptr;
    static_cast<void>(context.expectTrue("create first stdin pipe", CreatePipe(&firstRead, &firstWrite, nullptr, 0) != FALSE));
    if (firstRead == nullptr || firstWrite == nullptr)
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
        context.expectEq("initial endpoint identity failure is returned", ErrorCode::PermissionDenied, initialIdentityFailure.status.code));
    static_cast<void>(context.expectFalse(
        "identity failure is not reported as cancellation",
        initialIdentityFailure.outcome == Terminal::Types::Input::ReadOutcome::Cancelled));

    const Terminal::Types::Input::TextResult retriedInitialIdentity = Terminal::readText(oneByte);
    static_cast<void>(context.expectTrue("one-shot identity failure is consumed", retriedInitialIdentity.status.ok()));
    static_cast<void>(context.expectEq("zero-timeout poll reads ready input", std::string{"A"}, retriedInitialIdentity.text));

    Hooks::setPendingHighSurrogate(Terminal::Types::Input::Stream::Stdin, UINT16_C(0xD83D));
    static_cast<void>(
        context.expectTrue("first endpoint retains seeded high surrogate", Hooks::hasPendingHighSurrogate(Terminal::Types::Input::Stream::Stdin)));

    HANDLE failedProbeRead = nullptr;
    HANDLE failedProbeWrite = nullptr;
    static_cast<void>(context.expectTrue("create failed-probe stdin pipe", CreatePipe(&failedProbeRead, &failedProbeWrite, nullptr, 0) != FALSE));
    if (failedProbeRead == nullptr || failedProbeWrite == nullptr)
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
    static_cast<void>(context.expectEq("failed replacement probe is returned", ErrorCode::NativeFailure, failedReplacementProbe.status.code));
    static_cast<void>(context.expectTrue(
        "failed replacement probe preserves pending decoder state",
        Hooks::hasPendingHighSurrogate(Terminal::Types::Input::Stream::Stdin)));

    static_cast<void>(context.expectTrue("restore confirmed stdin endpoint", SetStdHandle(STD_INPUT_HANDLE, firstRead) != FALSE));
    const Terminal::Types::Input::TextResult restoredEndpoint = Terminal::readText(oneByte);
    static_cast<void>(context.expectTrue("restored endpoint clears stale probe failure", restoredEndpoint.status.ok()));
    static_cast<void>(context.expectTrue(
        "restored endpoint preserves pending decoder state",
        Hooks::hasPendingHighSurrogate(Terminal::Types::Input::Stream::Stdin)));
    static_cast<void>(context.expectTrue("retry failed-probe stdin endpoint", SetStdHandle(STD_INPUT_HANDLE, failedProbeRead) != FALSE));
    const Terminal::Types::Input::TextResult completedReplacementProbe = Terminal::readText(oneByte);
    static_cast<void>(context.expectEq("replacement after recovery reads normally", std::string{"Z"}, completedReplacementProbe.text));
    CloseHandle(failedProbeRead);

    static_cast<void>(context.expectTrue("restore first stdin endpoint before reuse", SetStdHandle(STD_INPUT_HANDLE, firstRead) != FALSE));
    Hooks::setPendingHighSurrogate(Terminal::Types::Input::Stream::Stdin, UINT16_C(0xD83D));
    const HANDLE reusedValue = firstRead;
    CloseHandle(firstRead);

    HANDLE replacementRead = nullptr;
    HANDLE replacementWrite = nullptr;
    for (std::size_t attempt = 0; attempt < 256; ++attempt)
    {
        HANDLE candidateRead = nullptr;
        HANDLE candidateWrite = nullptr;
        if (CreatePipe(&candidateRead, &candidateWrite, nullptr, 0) == FALSE)
        {
            break;
        }
        if (candidateRead == reusedValue)
        {
            replacementRead = candidateRead;
            replacementWrite = candidateWrite;
            break;
        }
        CloseHandle(candidateRead);
        CloseHandle(candidateWrite);
    }

    if (replacementRead == nullptr)
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
    static_cast<void>(context.expectTrue("replacement endpoint read succeeds", replacement.status.ok()));
    static_cast<void>(context.expectEq("replacement endpoint discards stale byte", std::string{"C"}, replacement.text));
    static_cast<void>(context.expectFalse(
        "replacement endpoint discards stale high surrogate",
        Hooks::hasPendingHighSurrogate(Terminal::Types::Input::Stream::Stdin)));

    static_cast<void>(context.expectTrue("detach stdin", SetStdHandle(STD_INPUT_HANDLE, nullptr) != FALSE));
    static_cast<void>(context.expectEq("detached stdin reports NotOpen after state reset", ErrorCode::NotOpen, Terminal::readText().status.code));

    static_cast<void>(SetStdHandle(STD_INPUT_HANDLE, originalInput));
    CloseHandle(replacementRead);
    Hooks::reset();
}

/// @brief Verifies bounded cancellation observation when the stop callback cannot signal its event.
int runCancellationSignalFailureChild()
{
    Hooks::reset();
    Hooks::forceNextCancellationSignalFailure();
    std::stop_source stopSource;
    const std::size_t initialWaits = Hooks::consoleWaitCallCount();
    PlatformHooks::Win32ConsoleWaitResult result;
    std::thread reader(
        [&]
        {
            result = PlatformHooks::waitForConsoleRecordForTest(std::chrono::milliseconds{-1}, stopSource.get_token());
        });
    const auto setupDeadline = std::chrono::steady_clock::now() + std::chrono::seconds{1};
    while (Hooks::consoleWaitCallCount() == initialWaits && std::chrono::steady_clock::now() < setupDeadline)
    {
        std::this_thread::yield();
    }
    if (Hooks::consoleWaitCallCount() == initialWaits)
    {
        stopSource.request_stop();
        reader.join();
        return 3;
    }
    stopSource.request_stop();
    reader.join();
    const bool passed = result.status.ok() && result.outcome == Terminal::Types::Input::ReadOutcome::Cancelled;
    const bool hookConsumed = !GameWIP::Terminal::Detail::TestHooks::consumeFailure(
                                   GameWIP::Terminal::Detail::TestHooks::terminalTestHookState.nextCancellationSignalFailure)
                                   .has_value();
    if (!hookConsumed)
    {
        Hooks::reset();
        return 4;
    }
    Hooks::reset();
    return passed ? 0 : 1;
}

/// @brief Verifies bounded cancellation observation when the stop callback cannot signal its event.
void testCancellationSignalFailure(TestSupport::Context &context)
{
    Hooks::reset();
    TestSupport::Types::Process::Options child;
    child.executablePath = std::filesystem::path(context.executablePath);
    child.arguments = {std::string(kCancellationSignalFailureChildArgument)};
    child.timeout = std::chrono::milliseconds{1500};
    child.captureOutput = false;
    const TestSupport::Types::Process::Result childResult = TestSupport::runChildProcess(child);
    static_cast<void>(context.expectTrue("infinite cancellation child infrastructure succeeds", childResult.status.ok()));
    static_cast<void>(context.expectEq("infinite cancellation child exits", TestSupport::Types::Process::Outcome::Exited, childResult.outcome));
    static_cast<void>(context.expectEq("infinite cancellation child returns success", std::uint32_t{0}, childResult.exitCode));

    const auto run = [&context](std::string_view label, bool forceSignalFailure)
    {
        std::stop_source stopSource;
        if (forceSignalFailure)
        {
            Hooks::forceNextCancellationSignalFailure();
        }

        const std::size_t initialWaits = Hooks::consoleWaitCallCount();
        PlatformHooks::Win32ConsoleWaitResult result;
        std::thread reader(
            [&]
            {
                result = PlatformHooks::waitForConsoleRecordForTest(std::chrono::milliseconds{500}, stopSource.get_token());
            });
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
        while (Hooks::consoleWaitCallCount() == initialWaits && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::yield();
        }
        static_cast<void>(
            context.expectTrue(std::format("{} reaches a native wait before stop", label), Hooks::consoleWaitCallCount() != initialWaits));
        stopSource.request_stop();
        reader.join();
        static_cast<void>(
            context.expectEq(std::format("{} returns Cancelled", label), Terminal::Types::Input::ReadOutcome::Cancelled, result.outcome));
        static_cast<void>(context.expectTrue(std::format("{} status succeeds", label), result.status.ok()));
        const bool hookConsumed = !GameWIP::Terminal::Detail::TestHooks::consumeFailure(
                                       GameWIP::Terminal::Detail::TestHooks::terminalTestHookState.nextCancellationSignalFailure)
                                       .has_value();
        static_cast<void>(context.expectTrue(std::format("{} consumes signal-failure hook", label), hookConsumed));
    };

    Hooks::reset();
    run("finite wait with signal failure", true);

    Hooks::reset();
    Hooks::forceNextCancellationSignalFailure();
    run("first one-shot signal callback", false);
    run("second one-shot signal callback", false);
    Hooks::reset();
}

/// @brief Verifies native cancellation-event reset failure is reported before any blocking wait.
void testCancellationResetFailure(TestSupport::Context &context)
{
    Hooks::reset();
    std::stop_source stopSource;
    Hooks::forceNextCancellationResetFailure();
    const PlatformHooks::Win32ConsoleWaitResult failed =
        PlatformHooks::waitForConsoleRecordForTest(std::chrono::milliseconds{0}, stopSource.get_token());
    static_cast<void>(context.expectEq("ResetEvent failure is returned", ErrorCode::NativeFailure, failed.status.code));
    static_cast<void>(
        context.expectFalse("ResetEvent failure is not cancellation", failed.outcome == Terminal::Types::Input::ReadOutcome::Cancelled));
    static_cast<void>(context.expectEq("ResetEvent failure enters no native wait", std::size_t{0}, Hooks::consoleWaitCallCount()));

    const PlatformHooks::Win32ConsoleWaitResult retry =
        PlatformHooks::waitForConsoleRecordForTest(std::chrono::milliseconds{0}, stopSource.get_token());
    static_cast<void>(context.expectTrue("cancellation reset hook is one-shot", retry.status.ok()));
    static_cast<void>(context.expectTrue("next operation reaches normal wait path", Hooks::consoleWaitCallCount() > 0));
    Hooks::reset();
}
#endif
#endif
