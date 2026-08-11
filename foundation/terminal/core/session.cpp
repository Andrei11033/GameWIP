/// @file session.cpp
/// @brief Managed Terminal input ownership, Session lifecycle, and checked read entry points.

#include "terminal/terminal.h"

#include "terminal/internal/terminal_input.h"
#include "terminal/internal/terminal_platform.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <mutex>
#include <new>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <utility>

namespace GameWIP::Terminal
{
    namespace
    {
        using ErrorCode = IO::Types::ErrorCode;

        enum class ReadOperation
        {
            Event,
            Bytes,
            Text,
            Line
        };

        struct InputCoordinator
        {
            std::mutex ownershipMutex;
            std::mutex ioMutex;
            const void *owner = nullptr;
        };

        struct PreparedInputOwnership
        {
            Types::InputCapabilities capabilities{};
            Detail::Platform::InputModeSnapshot previousMode{};
            bool hasPreviousMode = false;
            bool claimed = false;
        };

        struct ReadDecision
        {
            IO::Types::Status status = IO::successStatus();
            bool cancelled = false;
        };

        [[nodiscard]] InputCoordinator &inputCoordinator([[maybe_unused]] Types::InputStream stream) noexcept
        {
            static InputCoordinator stdinCoordinator;
            return stdinCoordinator;
        }

        [[nodiscard]] bool validInputStream(Types::InputStream stream) noexcept
        {
            return stream == Types::InputStream::Stdin;
        }

        [[nodiscard]] bool validOutputStream(Types::OutputStream stream) noexcept
        {
            return stream == Types::OutputStream::Stdout || stream == Types::OutputStream::Stderr;
        }

        [[nodiscard]] bool validDeliveryMode(Types::InputDeliveryMode mode) noexcept
        {
            return mode == Types::InputDeliveryMode::Events || mode == Types::InputDeliveryMode::Stream;
        }

        [[nodiscard]] bool validControlKeyMode(Types::ControlKeyMode mode) noexcept
        {
            return mode == Types::ControlKeyMode::NativeProcessing || mode == Types::ControlKeyMode::ReportAsInput;
        }

        [[nodiscard]] bool validReadLineEndingMode(Types::ReadLineEndingMode mode) noexcept
        {
            return mode == Types::ReadLineEndingMode::Strip || mode == Types::ReadLineEndingMode::Keep ||
                   mode == Types::ReadLineEndingMode::NormalizeToLf;
        }

        [[nodiscard]] IO::Types::Status invalidArgumentStatus() noexcept
        {
            return IO::makeStatus(ErrorCode::InvalidArgument);
        }

        [[nodiscard]] IO::Types::Status unsupportedStatus() noexcept
        {
            return IO::makeStatus(ErrorCode::Unsupported);
        }

        [[nodiscard]] IO::Types::Status notOpenStatus() noexcept
        {
            return IO::makeStatus(ErrorCode::NotOpen);
        }

        [[nodiscard]] bool operationSupported(const Types::InputCapabilities &capabilities, ReadOperation operation) noexcept
        {
            switch (operation)
            {
            case ReadOperation::Event:
                return capabilities.supportsEventInput;
            case ReadOperation::Bytes:
                return capabilities.supportsByteInput;
            case ReadOperation::Text:
                return capabilities.supportsUtf8Text;
            case ReadOperation::Line:
                return capabilities.supportsLineInput;
            }

            return false;
        }

        [[nodiscard]] bool deliverySupported(const Types::InputCapabilities &capabilities, Types::InputDeliveryMode deliveryMode) noexcept
        {
            if (deliveryMode == Types::InputDeliveryMode::Events)
            {
                return capabilities.supportsEventInput;
            }

            return capabilities.supportsByteInput || capabilities.supportsUtf8Text || capabilities.supportsLineInput;
        }

        [[nodiscard]] IO::Types::Status validateTimeout(const std::optional<std::chrono::milliseconds> &timeout)
        {
            if (timeout.has_value() && timeout->count() < 0)
            {
                return invalidArgumentStatus();
            }

            return IO::successStatus();
        }

        [[nodiscard]] ReadDecision validateReadContract(
            const Types::InputCapabilities &capabilities,
            ReadOperation operation,
            const std::optional<std::chrono::milliseconds> &timeout,
            std::stop_token stopToken)
        {
            ReadDecision decision;
            decision.status = validateTimeout(timeout);
            if (!decision.status.ok())
            {
                return decision;
            }

            if (stopToken.stop_requested())
            {
                decision.cancelled = true;
                return decision;
            }

            if (capabilities.kind == Types::StreamKind::Detached)
            {
                decision.status = notOpenStatus();
                return decision;
            }

            if (!operationSupported(capabilities, operation))
            {
                decision.status = unsupportedStatus();
                return decision;
            }

            if (timeout.has_value())
            {
                if (timeout->count() == 0 && !capabilities.supportsNonBlockingReads)
                {
                    decision.status = unsupportedStatus();
                    return decision;
                }
                if (timeout->count() > 0 && !capabilities.supportsFiniteTimeouts)
                {
                    decision.status = unsupportedStatus();
                    return decision;
                }
            }

            const bool canBlock = !timeout.has_value() || timeout->count() > 0;
            if (canBlock && stopToken.stop_possible() && !capabilities.supportsCancellation)
            {
                decision.status = unsupportedStatus();
            }

            return decision;
        }

        [[nodiscard]] Detail::Platform::InputMode managedMode(const Types::SessionOptions &options, Detail::Platform::InputMode previousMode) noexcept
        {
            Detail::Platform::InputMode mode = previousMode;
            mode.processControlKeys = options.controlKeyMode == Types::ControlKeyMode::NativeProcessing;
            if (options.deliveryMode == Types::InputDeliveryMode::Events)
            {
                mode.lineBuffered = false;
                mode.echoInput = false;
            }
            return mode;
        }

        void appendRestorationDiagnostic(IO::Types::Status &primary, const IO::Types::Status &restoration) noexcept
        {
            if (primary.ok() || restoration.ok())
            {
                return;
            }

            try
            {
                if (!primary.message.empty())
                {
                    primary.message.append(" ");
                }
                primary.message.append("Terminal input restoration also failed");
                if (!restoration.message.empty())
                {
                    primary.message.append(": ");
                    primary.message.append(restoration.message);
                }
                primary.message.push_back('.');
            }
            catch (...)
            {
                // The primary failure remains authoritative when diagnostic enrichment cannot allocate.
            }
        }

        [[nodiscard]] IO::Types::Status restoreManagedInput(
            Types::InputStream stream,
            PreparedInputOwnership &ownership,
            const void *owner,
            bool retainOwnershipOnFailure) noexcept
        {
            IO::Types::Status restoration = IO::successStatus();

            try
            {
                if (ownership.hasPreviousMode)
                {
                    std::lock_guard lock(Detail::inputIoMutex(stream));
                    restoration = Detail::Platform::restoreInputMode(stream, ownership.previousMode);
                }
            }
            catch (const std::bad_alloc &)
            {
                restoration = IO::makeStatus(ErrorCode::OutOfMemory);
            }
            catch (...)
            {
                restoration = IO::makeStatus(ErrorCode::Unknown);
            }

            if (!restoration.ok() && retainOwnershipOnFailure)
            {
                return restoration;
            }

            if (ownership.claimed)
            {
                Detail::releaseInput(stream, owner);
                ownership.claimed = false;
            }
            if (restoration.ok() || !retainOwnershipOnFailure)
            {
                ownership.hasPreviousMode = false;
            }
            return restoration;
        }

        [[nodiscard]] IO::Types::Status acquireManagedInput(
            const Types::SessionOptions &options,
            const void *owner,
            PreparedInputOwnership &ownership) noexcept
        {
            ownership = {};

            IO::Types::Status status = Detail::claimInput(options.input, owner);
            if (!status.ok())
            {
                return status;
            }
            ownership.claimed = true;

            try
            {
                {
                    std::lock_guard lock(Detail::inputIoMutex(options.input));
                    Types::InputCapabilitiesResult capabilityResult = Detail::Platform::getInputCapabilities(options.input);
                    if (!capabilityResult.status.ok())
                    {
                        status = std::move(capabilityResult.status);
                    }
                    else
                    {
                        ownership.capabilities = capabilityResult.capabilities;
                        if (ownership.capabilities.kind == Types::StreamKind::Detached)
                        {
                            status = notOpenStatus();
                        }
                        else if (!deliverySupported(ownership.capabilities, options.deliveryMode))
                        {
                            status = unsupportedStatus();
                        }
                        else if (ownership.capabilities.kind == Types::StreamKind::Terminal)
                        {
                            Detail::Platform::InputModeSnapshotResult snapshot = Detail::Platform::captureInputMode(options.input);
                            status = snapshot.status;
                            if (status.ok())
                            {
                                ownership.previousMode = snapshot.snapshot;
                                ownership.hasPreviousMode = true;
                                status = Detail::Platform::setInputMode(options.input, managedMode(options, ownership.previousMode.mode));
                            }
                        }
                    }
                }
            }
            catch (const std::bad_alloc &)
            {
                status = IO::makeStatus(ErrorCode::OutOfMemory);
            }
            catch (...)
            {
                status = IO::makeStatus(ErrorCode::Unknown);
            }

            if (status.ok())
            {
                return status;
            }

            const IO::Types::Status restoration = restoreManagedInput(options.input, ownership, owner, false);
            appendRestorationDiagnostic(status, restoration);
            return status;
        }

        class DirectInputLease final
        {
        public:
            explicit DirectInputLease(Types::InputStream stream) noexcept
                : options_{.input = stream, .output = Types::OutputStream::Stdout}
            {
            }

            DirectInputLease(const DirectInputLease &) = delete;
            DirectInputLease &operator=(const DirectInputLease &) = delete;

            ~DirectInputLease() noexcept
            {
                static_cast<void>(restoreManagedInput(options_.input, ownership_, this, false));
            }

            [[nodiscard]] IO::Types::Status open(
                Types::InputDeliveryMode deliveryMode,
                Types::ControlKeyMode controlKeyMode = Types::ControlKeyMode::NativeProcessing) noexcept
            {
                options_.deliveryMode = deliveryMode;
                options_.controlKeyMode = controlKeyMode;
                return acquireManagedInput(options_, this, ownership_);
            }

            [[nodiscard]] const Types::InputCapabilities &capabilities() const noexcept
            {
                return ownership_.capabilities;
            }

            [[nodiscard]] IO::Types::Status finish(IO::Types::Status primary) noexcept
            {
                const IO::Types::Status restoration = restoreManagedInput(options_.input, ownership_, this, false);
                if (primary.ok())
                {
                    return restoration;
                }

                appendRestorationDiagnostic(primary, restoration);
                return primary;
            }

        private:
            Types::SessionOptions options_{};
            PreparedInputOwnership ownership_{};
        };

        template <typename Result> [[nodiscard]] Result failedResult(IO::Types::Status status)
        {
            Result result;
            result.status = std::move(status);
            return result;
        }

        template <typename Result> [[nodiscard]] Result cancelledResult()
        {
            Result result;
            result.status = IO::successStatus();
            result.outcome = Types::ReadOutcome::Cancelled;
            return result;
        }

        template <typename Options> [[nodiscard]] ReadDecision validateDirectReadOptions(const Options &options)
        {
            ReadDecision decision;
            decision.status = validateTimeout(options.timeout);
            if (!decision.status.ok())
            {
                return decision;
            }
            if (options.stopToken.stop_requested())
            {
                decision.cancelled = true;
            }
            return decision;
        }
    } // namespace

    namespace Detail
    {
        std::mutex &inputIoMutex(Types::InputStream stream) noexcept
        {
            return inputCoordinator(stream).ioMutex;
        }

        IO::Types::Status claimInput(Types::InputStream stream, const void *owner) noexcept
        {
            if (!validInputStream(stream) || owner == nullptr)
            {
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }

            try
            {
                InputCoordinator &coordinator = inputCoordinator(stream);
                std::lock_guard lock(coordinator.ownershipMutex);
                if (coordinator.owner != nullptr)
                {
                    return IO::makeStatus(ErrorCode::ResourceBusy);
                }

                coordinator.owner = owner;
                return IO::successStatus();
            }
            catch (...)
            {
                return IO::makeStatus(ErrorCode::Unknown);
            }
        }

        void releaseInput(Types::InputStream stream, const void *owner) noexcept
        {
            if (!validInputStream(stream) || owner == nullptr)
            {
                return;
            }

            try
            {
                InputCoordinator &coordinator = inputCoordinator(stream);
                std::lock_guard lock(coordinator.ownershipMutex);
                if (coordinator.owner == owner)
                {
                    coordinator.owner = nullptr;
                }
            }
            catch (...)
            {
            }
        }
    } // namespace Detail

    struct Session::State
    {
        std::mutex operationMutex;
        std::atomic_bool open = false;
        Types::SessionOptions options{};
        PreparedInputOwnership ownership{};
    };

    Session::Session() noexcept = default;

    Session::Session(Session &&other) noexcept = default;

    Session::~Session() noexcept
    {
        if (!state_)
        {
            return;
        }

        try
        {
            std::lock_guard lock(state_->operationMutex);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return;
            }

            static_cast<void>(restoreManagedInput(state_->options.input, state_->ownership, state_.get(), false));
            state_->open.store(false, std::memory_order_release);
        }
        catch (...)
        {
            Detail::releaseInput(state_->options.input, state_.get());
            state_->open.store(false, std::memory_order_release);
        }
    }

    bool Session::isOpen() const noexcept
    {
        return state_ && state_->open.load(std::memory_order_acquire);
    }

    IO::Types::Status Session::open(const Types::SessionOptions &options) noexcept
    {
        if (!validInputStream(options.input) || !validOutputStream(options.output) || !validDeliveryMode(options.deliveryMode) ||
            !validControlKeyMode(options.controlKeyMode))
        {
            return IO::makeStatus(ErrorCode::InvalidArgument);
        }

        try
        {
            if (!state_)
            {
                state_ = std::make_unique<State>();
            }

            std::lock_guard lock(state_->operationMutex);
            if (state_->open.load(std::memory_order_acquire))
            {
                return IO::makeStatus(ErrorCode::AlreadyOpen);
            }

            PreparedInputOwnership ownership;
            IO::Types::Status status = acquireManagedInput(options, state_.get(), ownership);
            if (!status.ok())
            {
                return status;
            }

            state_->options = options;
            state_->ownership = std::move(ownership);
            state_->open.store(true, std::memory_order_release);
            return IO::successStatus();
        }
        catch (const std::bad_alloc &)
        {
            return IO::makeStatus(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return IO::makeStatus(ErrorCode::Unknown);
        }
    }

    IO::Types::Status Session::close() noexcept
    {
        if (!state_)
        {
            return IO::successStatus();
        }

        try
        {
            std::lock_guard lock(state_->operationMutex);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return IO::successStatus();
            }

            IO::Types::Status status = restoreManagedInput(state_->options.input, state_->ownership, state_.get(), true);
            if (!status.ok())
            {
                return status;
            }

            state_->open.store(false, std::memory_order_release);
            return IO::successStatus();
        }
        catch (const std::bad_alloc &)
        {
            return IO::makeStatus(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return IO::makeStatus(ErrorCode::Unknown);
        }
    }

    Types::EventReadResult Session::readEvent(const Types::EventReadOptions &options) noexcept
    {
        if (!state_)
        {
            return failedResult<Types::EventReadResult>(notOpenStatus());
        }

        try
        {
            std::lock_guard operationLock(state_->operationMutex);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return failedResult<Types::EventReadResult>(notOpenStatus());
            }
            if (state_->options.deliveryMode != Types::InputDeliveryMode::Events)
            {
                return failedResult<Types::EventReadResult>(unsupportedStatus());
            }

            const ReadDecision decision =
                validateReadContract(state_->ownership.capabilities, ReadOperation::Event, options.timeout, options.stopToken);
            if (!decision.status.ok())
            {
                return failedResult<Types::EventReadResult>(decision.status);
            }
            if (decision.cancelled)
            {
                return cancelledResult<Types::EventReadResult>();
            }

            std::lock_guard inputLock(Detail::inputIoMutex(state_->options.input));
            return Detail::Platform::readEvent(state_->options.input, options);
        }
        catch (const std::bad_alloc &)
        {
            return failedResult<Types::EventReadResult>(IO::makeStatus(ErrorCode::OutOfMemory));
        }
        catch (...)
        {
            return failedResult<Types::EventReadResult>(IO::makeStatus(ErrorCode::Unknown));
        }
    }

    Types::ByteReadResult Session::readBytes(std::span<std::byte> outputBuffer, const Types::ByteReadOptions &options) noexcept
    {
        if (!state_)
        {
            return failedResult<Types::ByteReadResult>(notOpenStatus());
        }

        try
        {
            std::lock_guard operationLock(state_->operationMutex);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return failedResult<Types::ByteReadResult>(notOpenStatus());
            }
            if (state_->options.deliveryMode != Types::InputDeliveryMode::Stream)
            {
                return failedResult<Types::ByteReadResult>(unsupportedStatus());
            }

            const ReadDecision decision =
                validateReadContract(state_->ownership.capabilities, ReadOperation::Bytes, options.timeout, options.stopToken);
            if (!decision.status.ok())
            {
                return failedResult<Types::ByteReadResult>(decision.status);
            }
            if (decision.cancelled)
            {
                return cancelledResult<Types::ByteReadResult>();
            }

            std::lock_guard inputLock(Detail::inputIoMutex(state_->options.input));
            return Detail::Platform::readBytes(state_->options.input, outputBuffer, options);
        }
        catch (const std::bad_alloc &)
        {
            return failedResult<Types::ByteReadResult>(IO::makeStatus(ErrorCode::OutOfMemory));
        }
        catch (...)
        {
            return failedResult<Types::ByteReadResult>(IO::makeStatus(ErrorCode::Unknown));
        }
    }

    Types::TextReadResult Session::readText(const Types::TextReadOptions &options) noexcept
    {
        if (!state_)
        {
            return failedResult<Types::TextReadResult>(notOpenStatus());
        }

        if (options.maxReturnedBytes == 0)
        {
            return failedResult<Types::TextReadResult>(invalidArgumentStatus());
        }

        try
        {
            std::lock_guard operationLock(state_->operationMutex);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return failedResult<Types::TextReadResult>(notOpenStatus());
            }
            if (state_->options.deliveryMode != Types::InputDeliveryMode::Stream)
            {
                return failedResult<Types::TextReadResult>(unsupportedStatus());
            }

            const ReadDecision decision =
                validateReadContract(state_->ownership.capabilities, ReadOperation::Text, options.timeout, options.stopToken);
            if (!decision.status.ok())
            {
                return failedResult<Types::TextReadResult>(decision.status);
            }
            if (decision.cancelled)
            {
                return cancelledResult<Types::TextReadResult>();
            }

            std::lock_guard inputLock(Detail::inputIoMutex(state_->options.input));
            return Detail::Platform::readText(state_->options.input, options);
        }
        catch (const std::bad_alloc &)
        {
            return failedResult<Types::TextReadResult>(IO::makeStatus(ErrorCode::OutOfMemory));
        }
        catch (...)
        {
            return failedResult<Types::TextReadResult>(IO::makeStatus(ErrorCode::Unknown));
        }
    }

    Types::LineReadResult Session::readLine(const Types::LineReadOptions &options) noexcept
    {
        if (!state_)
        {
            return failedResult<Types::LineReadResult>(notOpenStatus());
        }

        if (options.maxReturnedBytes == 0 || !validReadLineEndingMode(options.lineEndingMode))
        {
            return failedResult<Types::LineReadResult>(invalidArgumentStatus());
        }

        try
        {
            std::lock_guard operationLock(state_->operationMutex);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return failedResult<Types::LineReadResult>(notOpenStatus());
            }
            if (state_->options.deliveryMode != Types::InputDeliveryMode::Stream)
            {
                return failedResult<Types::LineReadResult>(unsupportedStatus());
            }

            const ReadDecision decision =
                validateReadContract(state_->ownership.capabilities, ReadOperation::Line, options.timeout, options.stopToken);
            if (!decision.status.ok())
            {
                return failedResult<Types::LineReadResult>(decision.status);
            }
            if (decision.cancelled)
            {
                return cancelledResult<Types::LineReadResult>();
            }

            std::lock_guard inputLock(Detail::inputIoMutex(state_->options.input));
            return Detail::Platform::readLine(state_->options.input, options);
        }
        catch (const std::bad_alloc &)
        {
            return failedResult<Types::LineReadResult>(IO::makeStatus(ErrorCode::OutOfMemory));
        }
        catch (...)
        {
            return failedResult<Types::LineReadResult>(IO::makeStatus(ErrorCode::Unknown));
        }
    }

    Types::EventReadResult readEvent(const Types::EventReadOptions &options) noexcept
    {
        return readEvent(Types::InputStream::Stdin, options);
    }

    Types::EventReadResult readEvent(Types::InputStream stream, const Types::EventReadOptions &options) noexcept
    {
        if (!validInputStream(stream))
        {
            return failedResult<Types::EventReadResult>(invalidArgumentStatus());
        }

        const ReadDecision initial = validateDirectReadOptions(options);
        if (!initial.status.ok())
        {
            return failedResult<Types::EventReadResult>(initial.status);
        }
        if (initial.cancelled)
        {
            return cancelledResult<Types::EventReadResult>();
        }

        try
        {
            DirectInputLease lease(stream);
            IO::Types::Status status = lease.open(Types::InputDeliveryMode::Events);
            if (!status.ok())
            {
                return failedResult<Types::EventReadResult>(std::move(status));
            }

            const ReadDecision decision = validateReadContract(lease.capabilities(), ReadOperation::Event, options.timeout, options.stopToken);
            if (!decision.status.ok())
            {
                status = lease.finish(decision.status);
                return failedResult<Types::EventReadResult>(std::move(status));
            }
            if (decision.cancelled)
            {
                Types::EventReadResult result = cancelledResult<Types::EventReadResult>();
                result.status = lease.finish(std::move(result.status));
                return result;
            }

            Types::EventReadResult result;
            {
                std::lock_guard lock(Detail::inputIoMutex(stream));
                result = Detail::Platform::readEvent(stream, options);
            }
            result.status = lease.finish(std::move(result.status));
            return result;
        }
        catch (const std::bad_alloc &)
        {
            return failedResult<Types::EventReadResult>(IO::makeStatus(ErrorCode::OutOfMemory));
        }
        catch (...)
        {
            return failedResult<Types::EventReadResult>(IO::makeStatus(ErrorCode::Unknown));
        }
    }

    Types::ByteReadResult readBytes(std::span<std::byte> outputBuffer, const Types::ByteReadOptions &options) noexcept
    {
        return readBytes(Types::InputStream::Stdin, outputBuffer, options);
    }

    Types::ByteReadResult readBytes(Types::InputStream stream, std::span<std::byte> outputBuffer, const Types::ByteReadOptions &options) noexcept
    {
        if (!validInputStream(stream))
        {
            return failedResult<Types::ByteReadResult>(invalidArgumentStatus());
        }

        const ReadDecision initial = validateDirectReadOptions(options);
        if (!initial.status.ok())
        {
            return failedResult<Types::ByteReadResult>(initial.status);
        }
        if (initial.cancelled)
        {
            return cancelledResult<Types::ByteReadResult>();
        }

        try
        {
            DirectInputLease lease(stream);
            IO::Types::Status status = lease.open(Types::InputDeliveryMode::Stream);
            if (!status.ok())
            {
                return failedResult<Types::ByteReadResult>(std::move(status));
            }

            const ReadDecision decision = validateReadContract(lease.capabilities(), ReadOperation::Bytes, options.timeout, options.stopToken);
            if (!decision.status.ok())
            {
                status = lease.finish(decision.status);
                return failedResult<Types::ByteReadResult>(std::move(status));
            }

            Types::ByteReadResult result;
            {
                std::lock_guard lock(Detail::inputIoMutex(stream));
                result = Detail::Platform::readBytes(stream, outputBuffer, options);
            }
            result.status = lease.finish(std::move(result.status));
            return result;
        }
        catch (const std::bad_alloc &)
        {
            return failedResult<Types::ByteReadResult>(IO::makeStatus(ErrorCode::OutOfMemory));
        }
        catch (...)
        {
            return failedResult<Types::ByteReadResult>(IO::makeStatus(ErrorCode::Unknown));
        }
    }

    Types::TextReadResult readText(const Types::TextReadOptions &options) noexcept
    {
        return readText(Types::InputStream::Stdin, options);
    }

    Types::TextReadResult readText(Types::InputStream stream, const Types::TextReadOptions &options) noexcept
    {
        if (!validInputStream(stream) || options.maxReturnedBytes == 0)
        {
            return failedResult<Types::TextReadResult>(invalidArgumentStatus());
        }

        const ReadDecision initial = validateDirectReadOptions(options);
        if (!initial.status.ok())
        {
            return failedResult<Types::TextReadResult>(initial.status);
        }
        if (initial.cancelled)
        {
            return cancelledResult<Types::TextReadResult>();
        }

        try
        {
            DirectInputLease lease(stream);
            IO::Types::Status status = lease.open(Types::InputDeliveryMode::Stream);
            if (!status.ok())
            {
                return failedResult<Types::TextReadResult>(std::move(status));
            }

            const ReadDecision decision = validateReadContract(lease.capabilities(), ReadOperation::Text, options.timeout, options.stopToken);
            if (!decision.status.ok())
            {
                status = lease.finish(decision.status);
                return failedResult<Types::TextReadResult>(std::move(status));
            }

            Types::TextReadResult result;
            {
                std::lock_guard lock(Detail::inputIoMutex(stream));
                result = Detail::Platform::readText(stream, options);
            }
            result.status = lease.finish(std::move(result.status));
            return result;
        }
        catch (const std::bad_alloc &)
        {
            return failedResult<Types::TextReadResult>(IO::makeStatus(ErrorCode::OutOfMemory));
        }
        catch (...)
        {
            return failedResult<Types::TextReadResult>(IO::makeStatus(ErrorCode::Unknown));
        }
    }

    Types::LineReadResult readLine(const Types::LineReadOptions &options) noexcept
    {
        return readLine(Types::InputStream::Stdin, options);
    }

    Types::LineReadResult readLine(Types::InputStream stream, const Types::LineReadOptions &options) noexcept
    {
        if (!validInputStream(stream) || options.maxReturnedBytes == 0 || !validReadLineEndingMode(options.lineEndingMode))
        {
            return failedResult<Types::LineReadResult>(invalidArgumentStatus());
        }

        const ReadDecision initial = validateDirectReadOptions(options);
        if (!initial.status.ok())
        {
            return failedResult<Types::LineReadResult>(initial.status);
        }
        if (initial.cancelled)
        {
            return cancelledResult<Types::LineReadResult>();
        }

        try
        {
            DirectInputLease lease(stream);
            IO::Types::Status status = lease.open(Types::InputDeliveryMode::Stream);
            if (!status.ok())
            {
                return failedResult<Types::LineReadResult>(std::move(status));
            }

            const ReadDecision decision = validateReadContract(lease.capabilities(), ReadOperation::Line, options.timeout, options.stopToken);
            if (!decision.status.ok())
            {
                status = lease.finish(decision.status);
                return failedResult<Types::LineReadResult>(std::move(status));
            }

            Types::LineReadResult result;
            {
                std::lock_guard lock(Detail::inputIoMutex(stream));
                result = Detail::Platform::readLine(stream, options);
            }
            result.status = lease.finish(std::move(result.status));
            return result;
        }
        catch (const std::bad_alloc &)
        {
            return failedResult<Types::LineReadResult>(IO::makeStatus(ErrorCode::OutOfMemory));
        }
        catch (...)
        {
            return failedResult<Types::LineReadResult>(IO::makeStatus(ErrorCode::Unknown));
        }
    }
} // namespace GameWIP::Terminal
