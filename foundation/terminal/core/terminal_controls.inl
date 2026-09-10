/// @file terminal_controls.inl
/// @brief Included implementation for Terminal cursor, screen, title, and bell controls.

#if defined(GAMEWIP_TERMINAL_CORE_IMPLEMENTATION) && !defined(__INTELLISENSE__)

// ------------------------------------------------------------
// Terminal controls
// ------------------------------------------------------------

IO::Types::Status resetStyle(const Types::Output::ControlOptions &options) noexcept
{
    return resetStyle(Types::Output::Stream::Stdout, options);
}

IO::Types::Status resetStyle(Types::Output::Stream stream, const Types::Output::ControlOptions &options) noexcept
{
    try
    {
        if (!validOutputStream(stream))
        {
            return invalidArgumentStatus("Unknown terminal output stream.");
        }

        std::lock_guard lock(outputState(stream).mutex);
        return writeControlSequenceUnlocked(
            stream,
            "\x1b[0m",
            ControlFeature::StyleReset,
            "Terminal style reset is unsupported for this output stream.",
            options.flushMode);
    }
    catch (...)
    {
        return exceptionStatus();
    }
}

IO::Types::Status moveCursor(Types::Cursor::MoveDirection direction, std::uint32_t amount, const Types::Output::ControlOptions &options) noexcept
{
    return moveCursor(Types::Output::Stream::Stdout, direction, amount, options);
}

IO::Types::Status moveCursor(
    Types::Output::Stream stream,
    Types::Cursor::MoveDirection direction,
    std::uint32_t amount,
    const Types::Output::ControlOptions &options) noexcept
{
    try
    {
        if (!validOutputStream(stream))
        {
            return invalidArgumentStatus("Unknown terminal output stream.");
        }

        OutputState &state = outputState(stream);
        std::lock_guard lock(state.mutex);

        char command = 'A';
        switch (direction)
        {
        case Types::Cursor::MoveDirection::Up:
            command = 'A';
            break;
        case Types::Cursor::MoveDirection::Down:
            command = 'B';
            break;
        case Types::Cursor::MoveDirection::Left:
            command = 'D';
            break;
        case Types::Cursor::MoveDirection::Right:
            command = 'C';
            break;
        default:
            return invalidArgumentStatus("Unknown terminal cursor movement direction.");
        }

        if (amount == 0)
        {
            return flushIfRequested(stream, options.flushMode);
        }
        IO::Types::Status validationStatus = Detail::Platform::validateCursorMovement(stream, amount);
        if (!validationStatus.ok())
        {
            return validationStatus;
        }

        state.assembly.clear();
        state.assembly.append("\x1b[");
        appendUnsigned(state.assembly, amount);
        state.assembly.push_back(command);
        return writeAssembledControlSequenceUnlocked(
            stream,
            state,
            ControlFeature::CursorMovement,
            "Terminal cursor movement is unsupported for this output stream.",
            options.flushMode);
    }
    catch (...)
    {
        return exceptionStatus();
    }
}

IO::Types::Status setCursorPosition(Types::Cursor::Position position, const Types::Output::ControlOptions &options) noexcept
{
    return setCursorPosition(Types::Output::Stream::Stdout, position, options);
}

IO::Types::Status setCursorPosition(
    Types::Output::Stream stream,
    Types::Cursor::Position position,
    const Types::Output::ControlOptions &options) noexcept
{
    try
    {
        if (!validOutputStream(stream))
        {
            return invalidArgumentStatus("Unknown terminal output stream.");
        }
        IO::Types::Status validationStatus = Detail::Platform::validateCursorPosition(stream, position);
        if (!validationStatus.ok())
        {
            return validationStatus;
        }

        OutputState &state = outputState(stream);
        std::lock_guard lock(state.mutex);
        state.assembly.clear();
        state.assembly.append("\x1b[");
        appendUnsigned(state.assembly, static_cast<std::uint64_t>(position.row) + 1);
        state.assembly.push_back(';');
        appendUnsigned(state.assembly, static_cast<std::uint64_t>(position.column) + 1);
        state.assembly.push_back('H');

        return writeAssembledControlSequenceUnlocked(
            stream,
            state,
            ControlFeature::CursorMovement,
            "Terminal cursor positioning is unsupported for this output stream.",
            options.flushMode);
    }
    catch (...)
    {
        return exceptionStatus();
    }
}

Types::Cursor::PositionResult getCursorPosition(const Types::Cursor::QueryOptions &options) noexcept
{
    return getCursorPosition(Types::Output::Stream::Stdout, Types::Input::Stream::Stdin, options);
}

Types::Cursor::PositionResult getCursorPosition(
    Types::Output::Stream outputStream,
    Types::Input::Stream responseStream,
    const Types::Cursor::QueryOptions &options) noexcept
{
    try
    {
        if (!validOutputStream(outputStream) || !validInputStream(responseStream))
        {
            return {.status = invalidArgumentStatus("Unknown terminal stream selected for cursor position query."), .position = {}};
        }

        std::scoped_lock lock(outputState(outputStream).mutex, Detail::inputIoMutex(responseStream));

        IO::Types::Status status = flushIfRequested(outputStream, options.flushMode);
        if (!status.ok())
        {
            return {.status = status, .position = {}};
        }

        return Detail::Platform::getCursorPosition(outputStream, responseStream, options);
    }
    catch (...)
    {
        return {.status = exceptionStatus(), .position = {}};
    }
}

Types::Cursor::PositionResult Detail::getLineRenderingCursorPosition(Types::Output::Stream outputStream, Types::Input::Stream inputStream) noexcept
{
    try
    {
        if (!validOutputStream(outputStream) || !validInputStream(inputStream))
        {
            return {.status = invalidArgumentStatus("Unknown terminal stream selected for line rendering."), .position = {}};
        }

        std::scoped_lock lock(outputState(outputStream).mutex, Detail::inputIoMutex(inputStream));
        return Detail::Platform::getLineRenderingCursorPosition(outputStream);
    }
    catch (...)
    {
        return {.status = exceptionStatus(), .position = {}};
    }
}

IO::Types::Status Detail::setLineRenderingCursorPosition(Types::Output::Stream outputStream, Types::Cursor::Position position) noexcept
{
    try
    {
        if (!validOutputStream(outputStream))
        {
            return invalidArgumentStatus("Unknown terminal output stream selected for line rendering.");
        }

        std::lock_guard lock(outputState(outputStream).mutex);
        return Detail::Platform::setLineRenderingCursorPosition(outputStream, position);
    }
    catch (...)
    {
        return exceptionStatus();
    }
}

IO::Types::Status saveCursorPosition(const Types::Output::ControlOptions &options) noexcept
{
    return saveCursorPosition(Types::Output::Stream::Stdout, options);
}

IO::Types::Status saveCursorPosition(Types::Output::Stream stream, const Types::Output::ControlOptions &options) noexcept
{
    try
    {
        if (!validOutputStream(stream))
        {
            return invalidArgumentStatus("Unknown terminal output stream.");
        }

        std::lock_guard lock(outputState(stream).mutex);
        return writeControlSequenceUnlocked(
            stream,
            "\x1b[s",
            ControlFeature::CursorSaveRestore,
            "Terminal cursor save is unsupported for this output stream.",
            options.flushMode);
    }
    catch (...)
    {
        return exceptionStatus();
    }
}

IO::Types::Status restoreCursorPosition(const Types::Output::ControlOptions &options) noexcept
{
    return restoreCursorPosition(Types::Output::Stream::Stdout, options);
}

IO::Types::Status restoreCursorPosition(Types::Output::Stream stream, const Types::Output::ControlOptions &options) noexcept
{
    try
    {
        if (!validOutputStream(stream))
        {
            return invalidArgumentStatus("Unknown terminal output stream.");
        }

        std::lock_guard lock(outputState(stream).mutex);
        return writeControlSequenceUnlocked(
            stream,
            "\x1b[u",
            ControlFeature::CursorSaveRestore,
            "Terminal cursor restore is unsupported for this output stream.",
            options.flushMode);
    }
    catch (...)
    {
        return exceptionStatus();
    }
}

IO::Types::Status setCursorVisible(bool visible, const Types::Output::ControlOptions &options) noexcept
{
    return setCursorVisible(Types::Output::Stream::Stdout, visible, options);
}

IO::Types::Status setCursorVisible(Types::Output::Stream stream, bool visible, const Types::Output::ControlOptions &options) noexcept
{
    try
    {
        if (!validOutputStream(stream))
        {
            return invalidArgumentStatus("Unknown terminal output stream.");
        }

        std::lock_guard lock(outputState(stream).mutex);
        return writeControlSequenceUnlocked(
            stream,
            visible ? "\x1b[?25h" : "\x1b[?25l",
            ControlFeature::CursorVisibility,
            "Terminal cursor visibility is unsupported for this output stream.",
            options.flushMode);
    }
    catch (...)
    {
        return exceptionStatus();
    }
}

CursorHiddenScope scopedCursorHidden(const Types::Output::ControlOptions &options) noexcept
{
    return scopedCursorHidden(Types::Output::Stream::Stdout, options);
}

CursorHiddenScope scopedCursorHidden(Types::Output::Stream stream, const Types::Output::ControlOptions &options) noexcept
{
    CursorHiddenScope scope;
    scope.stream_ = stream;
    scope.options_ = options;
    if (!validOutputStream(stream))
    {
        scope.status_ = IO::makeStatus(ErrorCode::InvalidArgument);
        return scope;
    }
    if (!IO::isValidFlushMode(options.flushMode))
    {
        scope.status_ = IO::makeStatus(ErrorCode::InvalidArgument);
        return scope;
    }

    try
    {
        OutputState &state = outputState(stream);
        std::lock_guard lock(state.mutex);
        if (state.cursorHiddenScopeDepth == std::numeric_limits<std::size_t>::max())
        {
            scope.status_ = IO::makeStatus(ErrorCode::SizeLimitExceeded);
            return scope;
        }

        if (state.cursorHiddenScopeDepth == 0)
        {
            bool emitted = false;
            scope.status_ = writeControlSequenceUnlocked(
                stream,
                "\x1b[?25l",
                ControlFeature::CursorVisibility,
                "Terminal cursor visibility is unsupported for this output stream.",
                options.flushMode,
                &emitted);
            if (!emitted)
            {
                return scope;
            }
        }

        ++state.cursorHiddenScopeDepth;
        scope.active_ = true;
    }
    catch (...)
    {
        scope.status_ = exceptionStatus();
    }
    return scope;
}

IO::Types::Status clear(Types::Output::ClearTarget target, const Types::Output::ControlOptions &options) noexcept
{
    return clear(Types::Output::Stream::Stdout, target, options);
}

IO::Types::Status clear(Types::Output::Stream stream, Types::Output::ClearTarget target, const Types::Output::ControlOptions &options) noexcept
{
    try
    {
        if (!validOutputStream(stream))
        {
            return invalidArgumentStatus("Unknown terminal output stream.");
        }

        std::lock_guard lock(outputState(stream).mutex);

        std::string_view sequence;
        switch (target)
        {
        case Types::Output::ClearTarget::EntireScreen:
            sequence = "\x1b[2J";
            break;
        case Types::Output::ClearTarget::ScreenBeforeCursor:
            sequence = "\x1b[1J";
            break;
        case Types::Output::ClearTarget::ScreenAfterCursor:
            sequence = "\x1b[0J";
            break;
        case Types::Output::ClearTarget::EntireScreenAndScrollback:
            sequence = "\x1b[3J";
            break;
        case Types::Output::ClearTarget::EntireLine:
            sequence = "\x1b[2K";
            break;
        case Types::Output::ClearTarget::LineBeforeCursor:
            sequence = "\x1b[1K";
            break;
        case Types::Output::ClearTarget::LineAfterCursor:
            sequence = "\x1b[0K";
            break;
        default:
            return invalidArgumentStatus("Unknown terminal clear target.");
        }

        return writeControlSequenceUnlocked(
            stream,
            sequence,
            ControlFeature::Clear,
            "Terminal clear controls are unsupported for this output stream.",
            options.flushMode);
    }
    catch (...)
    {
        return exceptionStatus();
    }
}

IO::Types::Status scroll(Types::Output::ScrollDirection direction, std::uint32_t lines, const Types::Output::ControlOptions &options) noexcept
{
    return scroll(Types::Output::Stream::Stdout, direction, lines, options);
}

IO::Types::Status scroll(
    Types::Output::Stream stream,
    Types::Output::ScrollDirection direction,
    std::uint32_t lines,
    const Types::Output::ControlOptions &options) noexcept
{
    try
    {
        if (!validOutputStream(stream))
        {
            return invalidArgumentStatus("Unknown terminal output stream.");
        }

        OutputState &state = outputState(stream);
        std::lock_guard lock(state.mutex);

        char command = 'S';
        switch (direction)
        {
        case Types::Output::ScrollDirection::Up:
            command = 'S';
            break;
        case Types::Output::ScrollDirection::Down:
            command = 'T';
            break;
        default:
            return invalidArgumentStatus("Unknown terminal scroll direction.");
        }

        if (lines == 0)
        {
            return flushIfRequested(stream, options.flushMode);
        }
        IO::Types::Status validationStatus = Detail::Platform::validateScroll(stream, lines);
        if (!validationStatus.ok())
        {
            return validationStatus;
        }

        state.assembly.clear();
        state.assembly.append("\x1b[");
        appendUnsigned(state.assembly, lines);
        state.assembly.push_back(command);
        return writeAssembledControlSequenceUnlocked(
            stream,
            state,
            ControlFeature::Scroll,
            "Terminal scrolling is unsupported for this output stream.",
            options.flushMode);
    }
    catch (...)
    {
        return exceptionStatus();
    }
}

IO::Types::Status enterAlternateScreen(const Types::Output::ControlOptions &options) noexcept
{
    return enterAlternateScreen(Types::Output::Stream::Stdout, options);
}

IO::Types::Status enterAlternateScreen(Types::Output::Stream stream, const Types::Output::ControlOptions &options) noexcept
{
    try
    {
        if (!validOutputStream(stream))
        {
            return invalidArgumentStatus("Unknown terminal output stream.");
        }

        std::lock_guard lock(outputState(stream).mutex);
        return writeControlSequenceUnlocked(
            stream,
            "\x1b[?1049h",
            ControlFeature::AlternateScreen,
            "Terminal alternate screen is unsupported for this output stream.",
            options.flushMode);
    }
    catch (...)
    {
        return exceptionStatus();
    }
}

IO::Types::Status leaveAlternateScreen(const Types::Output::ControlOptions &options) noexcept
{
    return leaveAlternateScreen(Types::Output::Stream::Stdout, options);
}

IO::Types::Status leaveAlternateScreen(Types::Output::Stream stream, const Types::Output::ControlOptions &options) noexcept
{
    try
    {
        if (!validOutputStream(stream))
        {
            return invalidArgumentStatus("Unknown terminal output stream.");
        }

        std::lock_guard lock(outputState(stream).mutex);
        return writeControlSequenceUnlocked(
            stream,
            "\x1b[?1049l",
            ControlFeature::AlternateScreen,
            "Terminal alternate screen is unsupported for this output stream.",
            options.flushMode);
    }
    catch (...)
    {
        return exceptionStatus();
    }
}

AlternateScreenScope scopedAlternateScreen(const Types::Output::ControlOptions &options) noexcept
{
    return scopedAlternateScreen(Types::Output::Stream::Stdout, options);
}

AlternateScreenScope scopedAlternateScreen(Types::Output::Stream stream, const Types::Output::ControlOptions &options) noexcept
{
    AlternateScreenScope scope;
    scope.stream_ = stream;
    scope.options_ = options;
    if (!validOutputStream(stream))
    {
        scope.status_ = IO::makeStatus(ErrorCode::InvalidArgument);
        return scope;
    }
    if (!IO::isValidFlushMode(options.flushMode))
    {
        scope.status_ = IO::makeStatus(ErrorCode::InvalidArgument);
        return scope;
    }

    try
    {
        OutputState &state = outputState(stream);
        std::lock_guard lock(state.mutex);
        if (state.alternateScreenScopeDepth == std::numeric_limits<std::size_t>::max())
        {
            scope.status_ = IO::makeStatus(ErrorCode::SizeLimitExceeded);
            return scope;
        }

        if (state.alternateScreenScopeDepth == 0)
        {
            bool emitted = false;
            scope.status_ = writeControlSequenceUnlocked(
                stream,
                "\x1b[?1049h",
                ControlFeature::AlternateScreen,
                "Terminal alternate screen is unsupported for this output stream.",
                options.flushMode,
                &emitted);
            if (!emitted)
            {
                return scope;
            }
        }

        ++state.alternateScreenScopeDepth;
        scope.active_ = true;
    }
    catch (...)
    {
        scope.status_ = exceptionStatus();
    }
    return scope;
}

IO::Types::Status setTitle(std::string_view utf8Title, const Types::Output::ControlOptions &options) noexcept
{
    return setTitle(Types::Output::Stream::Stdout, utf8Title, options);
}

IO::Types::Status setTitle(Types::Output::Stream stream, std::string_view utf8Title, const Types::Output::ControlOptions &options) noexcept
{
    try
    {
        if (!validOutputStream(stream))
        {
            return invalidArgumentStatus("Unknown terminal output stream.");
        }
        IO::Types::Status validationStatus = validateUtf8Text(utf8Title);
        if (!validationStatus.ok())
        {
            return validationStatus;
        }
        validationStatus = Detail::Platform::validateTitle(stream, utf8Title);
        if (!validationStatus.ok())
        {
            return validationStatus;
        }

        OutputState &state = outputState(stream);
        std::lock_guard lock(state.mutex);
        state.assembly.clear();
        state.assembly.append("\x1b]0;");
        appendSanitizedTitle(state.assembly, utf8Title);
        state.assembly.push_back('\x07');
        return writeAssembledControlSequenceUnlocked(
            stream,
            state,
            ControlFeature::Title,
            "Terminal title controls are unsupported for this output stream.",
            options.flushMode);
    }
    catch (...)
    {
        return exceptionStatus();
    }
}

IO::Types::Status ringBell(const Types::Output::ControlOptions &options) noexcept
{
    return ringBell(Types::Output::Stream::Stdout, options);
}

IO::Types::Status ringBell(Types::Output::Stream stream, const Types::Output::ControlOptions &options) noexcept
{
    try
    {
        if (!validOutputStream(stream))
        {
            return invalidArgumentStatus("Unknown terminal output stream.");
        }

        std::lock_guard lock(outputState(stream).mutex);
        return writeControlSequenceUnlocked(
            stream,
            "\a",
            ControlFeature::Bell,
            "Terminal bell output is unsupported for this output stream.",
            options.flushMode);
    }
    catch (...)
    {
        return exceptionStatus();
    }
}

#endif // defined(GAMEWIP_TERMINAL_CORE_IMPLEMENTATION) && !defined(__INTELLISENSE__)
