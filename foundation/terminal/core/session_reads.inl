/// @file session_reads.inl
/// @brief Included implementation for temporary managed input entry points.

#if defined(GAMEWIP_TERMINAL_CORE_IMPLEMENTATION) && !defined(__INTELLISENSE__)

// ------------------------------------------------------------
// Temporary managed input
// ------------------------------------------------------------

Types::Input::EventResult readEvent(const Types::Input::EventOptions &options) noexcept
{
    return readEvent(Types::Input::Stream::Stdin, options);
}

Types::Input::EventResult readEvent(Types::Input::Stream stream, const Types::Input::EventOptions &options) noexcept
{
    if (!validInputStream(stream))
    {
        return failedResult<Types::Input::EventResult>(invalidArgumentStatus());
    }

    const ReadDecision initial = validateDirectReadOptions(options);
    if (!initial.status.ok())
    {
        return failedResult<Types::Input::EventResult>(initial.status);
    }
    if (initial.cancelled)
    {
        return cancelledResult<Types::Input::EventResult>();
    }

    try
    {
        DirectInputLease lease(stream);
        IO::Types::Status status = lease.open(Types::Input::DeliveryMode::Events);
        if (!status.ok())
        {
            return failedResult<Types::Input::EventResult>(std::move(status));
        }

        const ReadDecision decision = validateReadContract(lease.capabilities(), ReadOperation::Event, options.timeout, options.stopToken);
        if (!decision.status.ok())
        {
            status = lease.finish(decision.status);
            return failedResult<Types::Input::EventResult>(std::move(status));
        }
        if (decision.cancelled)
        {
            Types::Input::EventResult result = cancelledResult<Types::Input::EventResult>();
            result.status = lease.finish(std::move(result.status));
            return result;
        }

        Types::Input::EventResult result;
        {
            std::lock_guard lock(Detail::inputIoMutex(stream));
            result = Detail::Platform::readEvent(stream, Types::Output::Stream::Stdout, options);
        }
        result.status = lease.finish(std::move(result.status));
        return result;
    }
    catch (const std::bad_alloc &)
    {
        return failedResult<Types::Input::EventResult>(IO::makeStatus(ErrorCode::OutOfMemory));
    }
    catch (...)
    {
        return failedResult<Types::Input::EventResult>(IO::makeStatus(ErrorCode::Unknown));
    }
}

Types::Input::ByteResult readBytes(std::span<std::byte> outputBuffer, const Types::Input::ByteOptions &options) noexcept
{
    return readBytes(Types::Input::Stream::Stdin, outputBuffer, options);
}

Types::Input::ByteResult readBytes(Types::Input::Stream stream, std::span<std::byte> outputBuffer, const Types::Input::ByteOptions &options) noexcept
{
    if (!validInputStream(stream))
    {
        return failedResult<Types::Input::ByteResult>(invalidArgumentStatus());
    }

    const ReadDecision initial = validateDirectReadOptions(options);
    if (!initial.status.ok())
    {
        return failedResult<Types::Input::ByteResult>(initial.status);
    }
    if (initial.cancelled)
    {
        return cancelledResult<Types::Input::ByteResult>();
    }

    try
    {
        DirectInputLease lease(stream);
        IO::Types::Status status = lease.open(Types::Input::DeliveryMode::Stream);
        if (!status.ok())
        {
            return failedResult<Types::Input::ByteResult>(std::move(status));
        }

        const ReadDecision decision = validateReadContract(lease.capabilities(), ReadOperation::Bytes, options.timeout, options.stopToken);
        if (!decision.status.ok())
        {
            status = lease.finish(decision.status);
            return failedResult<Types::Input::ByteResult>(std::move(status));
        }

        Types::Input::ByteResult result;
        {
            std::lock_guard lock(Detail::inputIoMutex(stream));
            result = Detail::Platform::readBytes(stream, outputBuffer, options);
        }
        result.status = lease.finish(std::move(result.status));
        return result;
    }
    catch (const std::bad_alloc &)
    {
        return failedResult<Types::Input::ByteResult>(IO::makeStatus(ErrorCode::OutOfMemory));
    }
    catch (...)
    {
        return failedResult<Types::Input::ByteResult>(IO::makeStatus(ErrorCode::Unknown));
    }
}

Types::Input::TextResult readText(const Types::Input::TextOptions &options) noexcept
{
    return readText(Types::Input::Stream::Stdin, options);
}

Types::Input::TextResult readText(Types::Input::Stream stream, const Types::Input::TextOptions &options) noexcept
{
    if (!validInputStream(stream) || options.maxReturnedBytes == 0)
    {
        return failedResult<Types::Input::TextResult>(invalidArgumentStatus());
    }

    const ReadDecision initial = validateDirectReadOptions(options);
    if (!initial.status.ok())
    {
        return failedResult<Types::Input::TextResult>(initial.status);
    }
    if (initial.cancelled)
    {
        return cancelledResult<Types::Input::TextResult>();
    }

    try
    {
        DirectInputLease lease(stream);
        IO::Types::Status status = lease.open(Types::Input::DeliveryMode::Stream);
        if (!status.ok())
        {
            return failedResult<Types::Input::TextResult>(std::move(status));
        }

        const ReadDecision decision = validateReadContract(lease.capabilities(), ReadOperation::Text, options.timeout, options.stopToken);
        if (!decision.status.ok())
        {
            status = lease.finish(decision.status);
            return failedResult<Types::Input::TextResult>(std::move(status));
        }

        Types::Input::TextResult result;
        {
            std::lock_guard lock(Detail::inputIoMutex(stream));
            result = Detail::Platform::readText(stream, options);
        }
        result.status = lease.finish(std::move(result.status));
        return result;
    }
    catch (const std::bad_alloc &)
    {
        return failedResult<Types::Input::TextResult>(IO::makeStatus(ErrorCode::OutOfMemory));
    }
    catch (...)
    {
        return failedResult<Types::Input::TextResult>(IO::makeStatus(ErrorCode::Unknown));
    }
}

Types::Input::LineResult readLine(const Types::Input::LineOptions &options) noexcept
{
    return readLine(Types::Input::Stream::Stdin, options);
}

Types::Input::LineResult readLine(Types::Input::Stream stream, const Types::Input::LineOptions &options) noexcept
{
    if (!validInputStream(stream) || options.maxReturnedBytes == 0 || !validReadLineEndingMode(options.lineEndingMode))
    {
        return failedResult<Types::Input::LineResult>(invalidArgumentStatus());
    }

    const ReadDecision initial = validateDirectReadOptions(options);
    if (!initial.status.ok())
    {
        return failedResult<Types::Input::LineResult>(initial.status);
    }
    if (initial.cancelled)
    {
        return cancelledResult<Types::Input::LineResult>();
    }

    try
    {
        DirectInputLease lease(stream);
        IO::Types::Status status = lease.open(Types::Input::DeliveryMode::Stream);
        if (!status.ok())
        {
            return failedResult<Types::Input::LineResult>(std::move(status));
        }

        const ReadDecision decision = validateReadContract(lease.capabilities(), ReadOperation::Line, options.timeout, options.stopToken);
        if (!decision.status.ok())
        {
            status = lease.finish(decision.status);
            return failedResult<Types::Input::LineResult>(std::move(status));
        }

        Types::Input::LineResult result;
        if (lease.capabilities().kind == Types::StreamKind::Terminal && lease.capabilities().supportsEventInput)
        {
            std::vector<std::size_t> graphemeStorage;
            result = Detail::managedTerminalLineRead(stream, Types::Output::Stream::Stdout, options, graphemeStorage);
        }
        else
        {
            std::lock_guard lock(Detail::inputIoMutex(stream));
            result = Detail::Platform::readLine(stream, options);
        }

        result.status = lease.finish(std::move(result.status));
        return result;
    }
    catch (const std::bad_alloc &)
    {
        return failedResult<Types::Input::LineResult>(IO::makeStatus(ErrorCode::OutOfMemory));
    }
    catch (...)
    {
        return failedResult<Types::Input::LineResult>(IO::makeStatus(ErrorCode::Unknown));
    }
}

#endif // defined(GAMEWIP_TERMINAL_CORE_IMPLEMENTATION) && !defined(__INTELLISENSE__)
