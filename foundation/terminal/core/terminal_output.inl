/// @file terminal_output.inl
/// @brief Included implementation for Terminal segments, capabilities, and stream writes.

#if defined(GAMEWIP_TERMINAL_CORE_IMPLEMENTATION) && !defined(__INTELLISENSE__)

Types::Output::Segment::Segment(
    Types::Output::SegmentKind kind,
    std::string_view text,
    std::span<const std::byte> bytes,
    const Types::Style::Request &style) noexcept
    : kind_(kind)
    , text_(text)
    , bytes_(bytes)
    , style_(style)
{
}

// ------------------------------------------------------------
// Output segment construction
// ------------------------------------------------------------

Types::Output::Segment textSegment(std::string_view text) noexcept
{
    return Types::Output::Segment(Types::Output::SegmentKind::Text, text, {}, {});
}

Types::Output::Segment styledTextSegment(std::string_view text, const Types::Style::Request &style) noexcept
{
    return Types::Output::Segment(Types::Output::SegmentKind::StyledText, text, {}, style);
}

Types::Output::Segment byteSegment(std::span<const std::byte> bytes) noexcept
{
    return Types::Output::Segment(Types::Output::SegmentKind::Bytes, {}, bytes, {});
}

AlternateScreenScope::AlternateScreenScope() noexcept = default;

// ------------------------------------------------------------
// Scoped terminal state
// ------------------------------------------------------------

AlternateScreenScope::AlternateScreenScope(AlternateScreenScope &&other) noexcept
    : stream_(other.stream_)
    , options_(other.options_)
    , status_(std::move(other.status_))
    , active_(other.active_)
    , restorationEmitted_(other.restorationEmitted_)
    , restoreOnDestruction_(other.restoreOnDestruction_)
{
    other.active_ = false;
    other.restorationEmitted_ = false;
    other.restoreOnDestruction_ = true;
}

AlternateScreenScope &AlternateScreenScope::operator=(AlternateScreenScope &&other) noexcept
{
    if (this != &other)
    {
        if (active_)
        {
            // Preserve ownership on failure instead of silently losing an alternate-screen leave obligation.
            static_cast<void>(leave());
            if (active_)
            {
                return *this;
            }
        }
        stream_ = other.stream_;
        options_ = other.options_;
        status_ = std::move(other.status_);
        active_ = other.active_;
        restorationEmitted_ = other.restorationEmitted_;
        restoreOnDestruction_ = other.restoreOnDestruction_;
        other.active_ = false;
        other.restorationEmitted_ = false;
        other.restoreOnDestruction_ = true;
    }

    return *this;
}

AlternateScreenScope::~AlternateScreenScope() noexcept
{
    if (restoreOnDestruction_)
    {
        static_cast<void>(leave());
    }
    if (active_)
    {
        abandonAlternateScreenScope(stream_);
        active_ = false;
        restorationEmitted_ = false;
    }
}

bool AlternateScreenScope::active() const noexcept
{
    return active_;
}

const IO::Types::Status &AlternateScreenScope::status() const noexcept
{
    return status_;
}

IO::Types::Status AlternateScreenScope::leave() noexcept
{
    if (active_)
    {
        if (restorationEmitted_)
        {
            status_ = Terminal::flush(stream_, options_.flushMode);
            if (status_.ok())
            {
                active_ = false;
                restorationEmitted_ = false;
            }
        }
        else
        {
            bool restored = false;
            status_ = leaveAlternateScreenScope(stream_, options_, restored);
            if (restored)
            {
                if (status_.ok())
                {
                    active_ = false;
                }
                else
                {
                    restorationEmitted_ = true;
                }
            }
        }
    }

    return copyStatus(status_);
}

CursorHiddenScope::CursorHiddenScope() noexcept = default;

CursorHiddenScope::CursorHiddenScope(CursorHiddenScope &&other) noexcept
    : stream_(other.stream_)
    , options_(other.options_)
    , status_(std::move(other.status_))
    , active_(other.active_)
    , restorationEmitted_(other.restorationEmitted_)
    , restoreOnDestruction_(other.restoreOnDestruction_)
{
    other.active_ = false;
    other.restorationEmitted_ = false;
    other.restoreOnDestruction_ = true;
}

CursorHiddenScope &CursorHiddenScope::operator=(CursorHiddenScope &&other) noexcept
{
    if (this != &other)
    {
        if (active_)
        {
            // Preserve ownership on failure instead of silently losing a cursor-visibility restoration obligation.
            static_cast<void>(restore());
            if (active_)
            {
                return *this;
            }
        }
        stream_ = other.stream_;
        options_ = other.options_;
        status_ = std::move(other.status_);
        active_ = other.active_;
        restorationEmitted_ = other.restorationEmitted_;
        restoreOnDestruction_ = other.restoreOnDestruction_;
        other.active_ = false;
        other.restorationEmitted_ = false;
        other.restoreOnDestruction_ = true;
    }

    return *this;
}

CursorHiddenScope::~CursorHiddenScope() noexcept
{
    if (restoreOnDestruction_)
    {
        static_cast<void>(restore());
    }
    if (active_)
    {
        abandonCursorHiddenScope(stream_);
        active_ = false;
        restorationEmitted_ = false;
    }
}

bool CursorHiddenScope::active() const noexcept
{
    return active_;
}

const IO::Types::Status &CursorHiddenScope::status() const noexcept
{
    return status_;
}

IO::Types::Status CursorHiddenScope::restore() noexcept
{
    if (active_)
    {
        if (restorationEmitted_)
        {
            status_ = Terminal::flush(stream_, options_.flushMode);
            if (status_.ok())
            {
                active_ = false;
                restorationEmitted_ = false;
            }
        }
        else
        {
            bool restored = false;
            status_ = restoreCursorHiddenScope(stream_, options_, restored);
            if (restored)
            {
                if (status_.ok())
                {
                    active_ = false;
                }
                else
                {
                    restorationEmitted_ = true;
                }
            }
        }
    }

    return copyStatus(status_);
}

// ------------------------------------------------------------
// Capabilities and geometry
// ------------------------------------------------------------

Types::Input::CapabilitiesResult getInputCapabilities() noexcept
{
    return getInputCapabilities(Types::Input::Stream::Stdin);
}

Types::Input::CapabilitiesResult getInputCapabilities(Types::Input::Stream stream) noexcept
{
    try
    {
        if (!validInputStream(stream))
        {
            return {.status = invalidArgumentStatus("Unknown terminal input stream."), .capabilities = {}};
        }

        std::lock_guard lock(Detail::inputIoMutex(stream));
        return Detail::Platform::getInputCapabilities(stream);
    }
    catch (...)
    {
        return {.status = exceptionStatus(), .capabilities = {}};
    }
}

Types::Output::CapabilitiesResult getOutputCapabilities() noexcept
{
    return getOutputCapabilities(Types::Output::Stream::Stdout);
}

Types::Output::CapabilitiesResult getOutputCapabilities(Types::Output::Stream stream) noexcept
{
    try
    {
        if (!validOutputStream(stream))
        {
            return {.status = invalidArgumentStatus("Unknown terminal output stream."), .capabilities = {}};
        }

        std::lock_guard lock(outputState(stream).mutex);
        return Detail::Platform::getOutputCapabilities(stream);
    }
    catch (...)
    {
        return {.status = exceptionStatus(), .capabilities = {}};
    }
}

Types::Output::CapabilitiesResult prepareOutput() noexcept
{
    return prepareOutput(Types::Output::Stream::Stdout);
}

Types::Output::CapabilitiesResult prepareOutput(Types::Output::Stream stream) noexcept
{
    try
    {
        if (!validOutputStream(stream))
        {
            return {.status = invalidArgumentStatus("Unknown terminal output stream."), .capabilities = {}};
        }

        std::lock_guard lock(outputState(stream).mutex);
        return Detail::Platform::prepareOutput(stream);
    }
    catch (...)
    {
        return {.status = exceptionStatus(), .capabilities = {}};
    }
}

Types::SizeResult getTerminalSize() noexcept
{
    return getTerminalSize(Types::Output::Stream::Stdout);
}

Types::SizeResult getTerminalSize(Types::Output::Stream stream) noexcept
{
    try
    {
        if (!validOutputStream(stream))
        {
            return {.status = invalidArgumentStatus("Unknown terminal output stream."), .size = {}};
        }

        std::lock_guard lock(outputState(stream).mutex);
        return Detail::Platform::getTerminalSize(stream);
    }
    catch (...)
    {
        return {.status = exceptionStatus(), .size = {}};
    }
}

// ------------------------------------------------------------
// Output
// ------------------------------------------------------------

IO::Types::Status writeText(std::string_view utf8Text, const Types::Output::TextOptions &options) noexcept
{
    return writeText(Types::Output::Stream::Stdout, utf8Text, options);
}

IO::Types::Status writeText(Types::Output::Stream stream, std::string_view utf8Text, const Types::Output::TextOptions &options) noexcept
{
    try
    {
        if (!validOutputStream(stream))
        {
            return invalidArgumentStatus("Unknown terminal output stream.");
        }

        OutputState &state = outputState(stream);
        std::lock_guard lock(state.mutex);
        return writeTextUnlocked(stream, state, utf8Text, options);
    }
    catch (...)
    {
        return exceptionStatus();
    }
}

IO::Types::Status writeLine(std::string_view utf8Text, const Types::Output::LineOptions &options) noexcept
{
    return writeLine(Types::Output::Stream::Stdout, utf8Text, options);
}

IO::Types::Status writeLine(Types::Output::Stream stream, std::string_view utf8Text, const Types::Output::LineOptions &options) noexcept
{
    try
    {
        if (!validOutputStream(stream))
        {
            return invalidArgumentStatus("Unknown terminal output stream.");
        }

        OutputState &state = outputState(stream);
        std::lock_guard lock(state.mutex);
        return writeLineUnlocked(stream, state, utf8Text, options);
    }
    catch (...)
    {
        return exceptionStatus();
    }
}

IO::Types::WriteResult writeBytes(std::span<const std::byte> bytes, const Types::Output::ByteOptions &options) noexcept
{
    return writeBytes(Types::Output::Stream::Stdout, bytes, options);
}

IO::Types::WriteResult writeBytes(Types::Output::Stream stream, std::span<const std::byte> bytes, const Types::Output::ByteOptions &options) noexcept
{
    try
    {
        if (!validOutputStream(stream))
        {
            return {.status = invalidArgumentStatus("Unknown terminal output stream."), .bytesWritten = 0};
        }

        std::lock_guard lock(outputState(stream).mutex);
        return writeBytesUnlocked(stream, bytes, options);
    }
    catch (...)
    {
        return {.status = exceptionStatus(), .bytesWritten = 0};
    }
}

IO::Types::Status writeSegments(std::span<const Types::Output::Segment> segments, const Types::Output::SegmentOptions &options) noexcept
{
    return writeSegments(Types::Output::Stream::Stdout, segments, options);
}

IO::Types::Status writeSegments(
    Types::Output::Stream stream,
    std::span<const Types::Output::Segment> segments,
    const Types::Output::SegmentOptions &options) noexcept
{
    try
    {
        if (!validOutputStream(stream))
        {
            return invalidArgumentStatus("Unknown terminal output stream.");
        }

        OutputState &state = outputState(stream);
        std::lock_guard lock(state.mutex);
        return writeSegmentsUnlocked(stream, state, segments, options);
    }
    catch (...)
    {
        return exceptionStatus();
    }
}

namespace Detail
{
    IO::Types::Status vprint(
        Types::Output::Stream stream,
        const Types::Output::TextOptions &options,
        std::string_view format,
        std::format_args arguments) noexcept
    {
        try
        {
            if (!validOutputStream(stream))
            {
                return invalidArgumentStatus("Unknown terminal output stream.");
            }
            if (!IO::isValidFlushMode(options.flushMode))
            {
                return invalidArgumentStatus("Unknown IO flush mode.");
            }

            OutputState &state = outputState(stream);
            FormatScratchLease scratch(state);
            std::vformat_to(std::back_inserter(scratch.text()), format, arguments);

            std::lock_guard lock(state.mutex);
            return writeTextUnlocked(stream, state, scratch.text(), options);
        }
        catch (...)
        {
            return exceptionStatus();
        }
    }

    IO::Types::Status vprintln(
        Types::Output::Stream stream,
        const Types::Output::LineOptions &options,
        std::string_view format,
        std::format_args arguments) noexcept
    {
        try
        {
            if (!validOutputStream(stream))
            {
                return invalidArgumentStatus("Unknown terminal output stream.");
            }
            if (!validLineEnding(options.lineEnding))
            {
                return invalidArgumentStatus("Unknown terminal line ending.");
            }
            if (!IO::isValidFlushMode(options.flushMode))
            {
                return invalidArgumentStatus("Unknown IO flush mode.");
            }

            OutputState &state = outputState(stream);
            FormatScratchLease scratch(state);
            std::vformat_to(std::back_inserter(scratch.text()), format, arguments);

            std::lock_guard lock(state.mutex);
            return writeLineUnlocked(stream, state, scratch.text(), options);
        }
        catch (...)
        {
            return exceptionStatus();
        }
    }
} // namespace Detail

IO::Types::Status flush(IO::Types::FlushMode mode) noexcept
{
    return flush(Types::Output::Stream::Stdout, mode);
}

IO::Types::Status flush(Types::Output::Stream stream, IO::Types::FlushMode mode) noexcept
{
    try
    {
        if (!validOutputStream(stream))
        {
            return invalidArgumentStatus("Unknown terminal output stream.");
        }
        if (!IO::isValidFlushMode(mode))
        {
            return invalidArgumentStatus("Unknown IO flush mode.");
        }

        std::lock_guard lock(outputState(stream).mutex);
        return Detail::Platform::flush(stream, mode);
    }
    catch (...)
    {
        return exceptionStatus();
    }
}

#endif // defined(GAMEWIP_TERMINAL_CORE_IMPLEMENTATION) && !defined(__INTELLISENSE__)
