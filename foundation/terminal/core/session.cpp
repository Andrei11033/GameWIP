/// @file session.cpp
/// @brief Managed Terminal input ownership, Session lifecycle, and checked read entry points.

#include "terminal/session.h"

#include "terminal/internal/input.h"
#include "terminal/internal/results.h"
#include "terminal/platform/backend.h"
#include "terminal/internal/status.h"
#include "terminal/internal/session_state.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <condition_variable>
#include <format>
#include <mutex>
#include <new>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace GameWIP::Terminal
{
    namespace Internal
    {
        GameWIP::Status::Types::Status invalidArgumentStatus() noexcept
        {
            return GameWIP::Terminal::Internal::backendStatus(GameWIP::Status::Types::Code::InvalidArgument);
        }

        GameWIP::Status::Types::Status notOpenStatus() noexcept
        {
            return GameWIP::Terminal::Internal::backendStatus(GameWIP::Status::Types::Code::NotOpen);
        }

        [[nodiscard]] GameWIP::Status::Types::Status claimDirectInput(Types::Input::Stream stream) noexcept;
        void releaseDirectInput(Types::Input::Stream stream) noexcept;
    } // namespace Internal

    using Internal::invalidArgumentStatus;
    using Internal::notOpenStatus;
    using Internal::PreparedInputOwnership;

    namespace
    {
        using ErrorCode = GameWIP::Status::Types::Code;

        /// ------------------------------------------------------------
        /// Input ownership state
        /// ------------------------------------------------------------

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
            bool directLease = false;
        };

        struct ReadDecision
        {
            GameWIP::Status::Types::Status status = GameWIP::Status::Types::Status{};
            bool cancelled = false;
        };

        [[nodiscard]] InputCoordinator &inputCoordinator([[maybe_unused]] Types::Input::Stream stream) noexcept
        {
            static InputCoordinator stdinCoordinator;
            return stdinCoordinator;
        }

        /// ------------------------------------------------------------
        /// Input contract validation
        /// ------------------------------------------------------------

        [[nodiscard]] bool validInputStream(Types::Input::Stream stream) noexcept
        {
            return stream == Types::Input::Stream::Stdin;
        }

        [[nodiscard]] bool validOutputStream(Types::Output::Stream stream) noexcept
        {
            return stream == Types::Output::Stream::Stdout || stream == Types::Output::Stream::Stderr;
        }

        [[nodiscard]] bool validDeliveryMode(Types::Input::DeliveryMode mode) noexcept
        {
            return mode == Types::Input::DeliveryMode::Events || mode == Types::Input::DeliveryMode::Stream;
        }

        [[nodiscard]] bool validControlKeyMode(Types::Input::ControlKeyMode mode) noexcept
        {
            return mode == Types::Input::ControlKeyMode::NativeProcessing || mode == Types::Input::ControlKeyMode::ReportAsInput;
        }

        [[nodiscard]] bool validReadLineEndingMode(Types::Input::LineEndingMode mode) noexcept
        {
            return mode == Types::Input::LineEndingMode::Strip || mode == Types::Input::LineEndingMode::Keep ||
                   mode == Types::Input::LineEndingMode::NormalizeToLf;
        }

        [[nodiscard]] GameWIP::Status::Types::Status unsupportedStatus() noexcept
        {
            return GameWIP::Terminal::Internal::backendStatus(ErrorCode::Unsupported);
        }

        [[nodiscard]] bool operationSupported(const Types::Input::Capabilities &capabilities, ReadOperation operation) noexcept
        {
            switch(operation)
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

        [[nodiscard]] bool deliverySupported(const Types::Input::Capabilities &capabilities, Types::Input::DeliveryMode deliveryMode) noexcept
        {
            if(deliveryMode == Types::Input::DeliveryMode::Events)
            {
                return capabilities.supportsEventInput;
            }

            return capabilities.supportsByteInput || capabilities.supportsUtf8Text || capabilities.supportsLineInput;
        }

        [[nodiscard]] GameWIP::Status::Types::Status validateTimeout(const std::optional<std::chrono::milliseconds> &timeout)
        {
            if(timeout.has_value() && timeout->count() < 0)
            {
                return invalidArgumentStatus();
            }

            return GameWIP::Status::Types::Status{};
        }

        [[nodiscard]] ReadDecision validateReadContract(
            const Types::Input::Capabilities &capabilities,
            ReadOperation operation,
            const std::optional<std::chrono::milliseconds> &timeout,
            const std::stop_token &stopToken)
        {
            ReadDecision decision;

            // Validate the wait options before checking backend support.
            decision.status = validateTimeout(timeout);

            if(!decision.status.ok())
            {
                return decision;
            }

            // Honor cancellation before inspecting the selected operation.
            if(stopToken.stop_requested())
            {
                decision.cancelled = true;
                return decision;
            }

            // Confirm the stream is attached and supports this read operation.
            if(capabilities.kind == Types::StreamKind::Detached)
            {
                decision.status = notOpenStatus();
                return decision;
            }

            if(!operationSupported(capabilities, operation))
            {
                decision.status = unsupportedStatus();
                return decision;
            }

            // Check that the backend supports the requested timeout behavior.
            if(timeout.has_value())
            {
                if(timeout->count() == 0 && !capabilities.supportsNonBlockingReads)
                {
                    decision.status = unsupportedStatus();
                    return decision;
                }

                if(timeout->count() > 0 && !capabilities.supportsFiniteTimeouts)
                {
                    decision.status = unsupportedStatus();
                    return decision;
                }
            }

            const bool canBlock = !timeout.has_value() || timeout->count() > 0;

            // A cancellable blocking read needs backend cancellation support.
            if(canBlock && stopToken.stop_possible() && !capabilities.supportsCancellation)
            {
                decision.status = unsupportedStatus();
            }

            return decision;
        }

        [[nodiscard]] Platform::InputMode managedMode(
            const Types::SessionOptions &options,
            Platform::InputMode previousMode,
            const Types::Input::Capabilities &capabilities) noexcept
        {
            Platform::InputMode mode = previousMode;
            mode.processControlKeys = options.controlKeyMode == Types::Input::ControlKeyMode::NativeProcessing;

            // Event delivery, and Stream delivery on event-capable terminals, use one immediate native engine.
            // A backend without structured-event support keeps its existing Stream line discipline until its
            // own event decoder exists.
            if(options.deliveryMode == Types::Input::DeliveryMode::Events || capabilities.supportsEventInput)
            {
                mode.lineBuffered = false;
                mode.echoInput = false;
                mode.reportResizeEvents = capabilities.supportsResizeEvents;
                mode.reportPointerEvents = false;
                mode.exclusiveEventDelivery = true;
            }

            return mode;
        }

        void appendRestorationDiagnostic(GameWIP::Status::Types::Status &primary, const GameWIP::Status::Types::Status &restoration) noexcept
        {
            if(primary.ok() || restoration.ok())
            {
                return;
            }

            try
            {
                const GameWIP::Status::Types::Diagnostic *primaryDiagnostic = primary.diagnostic();
                const GameWIP::Status::Types::Diagnostic *restorationDiagnostic = restoration.diagnostic();

                std::string message = primaryDiagnostic != nullptr ? primaryDiagnostic->message : std::string{};
                if(!message.empty())
                {
                    message.append(" ");
                }
                message.append("Terminal input restoration also failed");

                if(restorationDiagnostic != nullptr && !restorationDiagnostic->message.empty())
                {
                    message.append(": ");
                    message.append(restorationDiagnostic->message);
                }
                message.push_back('.');

                if(primaryDiagnostic != nullptr && primaryDiagnostic->nativeCode.has_value())
                {
                    GameWIP::Status::Types::Status enriched =
                        GameWIP::Status::Types::Status::withDiagnostic(primary.code(), *primaryDiagnostic->nativeCode, message);
                    if(enriched.diagnostic() != nullptr)
                    {
                        primary = std::move(enriched);
                    }
                }
                else
                {
                    GameWIP::Status::Types::Status enriched = GameWIP::Status::Types::Status::withDiagnostic(primary.code(), message);
                    if(enriched.diagnostic() != nullptr)
                    {
                        primary = std::move(enriched);
                    }
                }
            }
            // NOLINTNEXTLINE(bugprone-empty-catch) -- Diagnostic enrichment must not replace the primary failure.
            catch(...)
            {
                // The primary failure remains authoritative when diagnostic enrichment cannot allocate.
            }
        }

        /// ------------------------------------------------------------
        /// Managed input acquisition and restoration
        /// ------------------------------------------------------------

        [[nodiscard]] GameWIP::Status::Types::Status restoreManagedInput(
            Types::Input::Stream stream,
            PreparedInputOwnership &ownership,
            const void *owner,
            bool retainOwnershipOnFailure) noexcept
        {
            GameWIP::Status::Types::Status restoration = GameWIP::Status::Types::Status{};

            // Restore the previous terminal mode while stream I/O is serialized.
            try
            {
                if(ownership.hasPreviousMode)
                {
                    std::lock_guard lock(Internal::inputIoMutex(stream));
                    restoration = Platform::restoreInputMode(stream, ownership.previousMode);
                }
            }
            catch(const std::bad_alloc &)
            {
                restoration = GameWIP::Terminal::Internal::backendStatus(ErrorCode::OutOfMemory);
            }
            catch(...)
            {
                restoration = GameWIP::Terminal::Internal::backendStatus(ErrorCode::Unknown);
            }

            if(!restoration.ok() && retainOwnershipOnFailure)
            {
                return restoration;
            }

            // Release the stream claim unless restoration must be retried by the caller.
            if(ownership.claimed)
            {
                if(ownership.directClaimed)
                {
                    Internal::releaseDirectInput(stream);
                    ownership.directClaimed = false;
                }
                else
                {
                    Internal::releaseInput(stream, owner);
                }
                ownership.claimed = false;
            }

            // Retain the saved mode only when the caller will retry restoration.
            if(restoration.ok() || !retainOwnershipOnFailure)
            {
                ownership.hasPreviousMode = false;
            }

            return restoration;
        }

        [[nodiscard]] GameWIP::Status::Types::Status acquireManagedInput(
            const Types::SessionOptions &options,
            const void *owner,
            PreparedInputOwnership &ownership) noexcept
        {
            // Claim the stream before reading capabilities or changing its mode.
            ownership = {};

            const bool directClaim = owner == nullptr;
            GameWIP::Status::Types::Status status =
                directClaim ? Internal::claimDirectInput(options.input) : Internal::claimInput(options.input, owner);

            if(!status.ok())
            {
                return status;
            }

            ownership.claimed = true;
            ownership.directClaimed = directClaim;

            // Inspect capabilities and configure terminal input under the stream lock.
            try
            {
                {
                    std::lock_guard lock(Internal::inputIoMutex(options.input));
                    Types::Input::CapabilitiesResult capabilityResult = Platform::getInputCapabilities(options.input);

                    if(!capabilityResult.ok())
                    {
                        status = capabilityResult.status();
                    }
                    else
                    {
                        ownership.capabilities = capabilityResult.value();

                        if(ownership.capabilities.kind == Types::StreamKind::Detached)
                        {
                            status = notOpenStatus();
                        }
                        else if(!deliverySupported(ownership.capabilities, options.deliveryMode))
                        {
                            status = unsupportedStatus();
                        }
                        else if(ownership.capabilities.kind == Types::StreamKind::Terminal)
                        {
                            Platform::InputModeSnapshotResult snapshot = Platform::captureInputMode(options.input);
                            status = snapshot.status();

                            if(status.ok())
                            {
                                ownership.previousMode = snapshot.value();
                                ownership.hasPreviousMode = true;
                                status =
                                    Platform::setInputMode(options.input, managedMode(options, ownership.previousMode.mode, ownership.capabilities));
                            }
                        }
                    }
                }
            }
            catch(const std::bad_alloc &)
            {
                status = GameWIP::Terminal::Internal::backendStatus(ErrorCode::OutOfMemory);
            }
            catch(...)
            {
                status = GameWIP::Terminal::Internal::backendStatus(ErrorCode::Unknown);
            }

            if(status.ok())
            {
                return status;
            }

            // Roll back the claim and any mode change if setup failed.
            const GameWIP::Status::Types::Status restoration = restoreManagedInput(options.input, ownership, owner, false);
            appendRestorationDiagnostic(status, restoration);
            return status;
        }

        /// ------------------------------------------------------------
        /// Direct reads and result construction
        /// ------------------------------------------------------------

        class DirectInputLease final
        {
        public:
            explicit DirectInputLease(Types::Input::Stream stream) noexcept
                : options_{.input = stream, .output = Types::Output::Stream::Stdout}
            {
            }

            DirectInputLease(const DirectInputLease &) = delete;
            DirectInputLease &operator=(const DirectInputLease &) = delete;

            ~DirectInputLease() noexcept
            {
                static_cast<void>(restoreManagedInput(options_.input, ownership_, nullptr, false));
            }

            [[nodiscard]] GameWIP::Status::Types::Status open(
                Types::Input::DeliveryMode deliveryMode,
                Types::Input::ControlKeyMode controlKeyMode = Types::Input::ControlKeyMode::NativeProcessing) noexcept
            {
                options_.deliveryMode = deliveryMode;
                options_.controlKeyMode = controlKeyMode;
                return acquireManagedInput(options_, nullptr, ownership_);
            }

            [[nodiscard]] const Types::Input::Capabilities &capabilities() const noexcept
            {
                return ownership_.capabilities;
            }

            [[nodiscard]] GameWIP::Status::Types::Status finish(GameWIP::Status::Types::Status primary) noexcept
            {
                const GameWIP::Status::Types::Status restoration = restoreManagedInput(options_.input, ownership_, nullptr, false);

                if(primary.ok())
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

        template <typename Result> [[nodiscard]] Result failedResult(GameWIP::Status::Types::Status status)
        {
            return Internal::failedResult<Result>(std::move(status));
        }

        template <typename Result> [[nodiscard]] Result cancelledResult()
        {
            return Internal::cancelledResult<Result>();
        }

        template <typename Options> [[nodiscard]] ReadDecision validateDirectReadOptions(const Options &options)
        {
            ReadDecision decision;
            decision.status = validateTimeout(options.timeout);

            if(!decision.status.ok())
            {
                return decision;
            }

            if(options.stopToken.stop_requested())
            {
                decision.cancelled = true;
            }

            return decision;
        }

    } // namespace

    namespace Internal
    {
        /// ------------------------------------------------------------
        /// Input coordinator operations
        /// ------------------------------------------------------------

        std::mutex &inputIoMutex(Types::Input::Stream stream) noexcept
        {
            return inputCoordinator(stream).ioMutex;
        }

        GameWIP::Status::Types::Status claimInput(Types::Input::Stream stream, const void *owner) noexcept
        {
            if(!validInputStream(stream) || owner == nullptr)
            {
                return GameWIP::Terminal::Internal::backendStatus(ErrorCode::InvalidArgument);
            }

            try
            {
                InputCoordinator &coordinator = inputCoordinator(stream);
                std::lock_guard lock(coordinator.ownershipMutex);

                if(coordinator.owner != nullptr || coordinator.directLease)
                {
                    return GameWIP::Terminal::Internal::backendStatus(ErrorCode::ResourceBusy);
                }
                coordinator.owner = owner;
                return GameWIP::Status::Types::Status{};
            }
            catch(...)
            {
                return GameWIP::Terminal::Internal::backendStatus(ErrorCode::Unknown);
            }
        }

        GameWIP::Status::Types::Status claimDirectInput(Types::Input::Stream stream) noexcept
        {
            if(!validInputStream(stream))
            {
                return GameWIP::Terminal::Internal::backendStatus(ErrorCode::InvalidArgument);
            }

            try
            {
                InputCoordinator &coordinator = inputCoordinator(stream);
                std::lock_guard lock(coordinator.ownershipMutex);

                if(coordinator.owner != nullptr || coordinator.directLease)
                {
                    return GameWIP::Terminal::Internal::backendStatus(ErrorCode::ResourceBusy);
                }
                coordinator.directLease = true;
                return GameWIP::Status::Types::Status{};
            }
            catch(...)
            {
                return GameWIP::Terminal::Internal::backendStatus(ErrorCode::Unknown);
            }
        }

        void releaseDirectInput(Types::Input::Stream stream) noexcept
        {
            if(!validInputStream(stream))
            {
                return;
            }

            try
            {
                InputCoordinator &coordinator = inputCoordinator(stream);
                std::lock_guard lock(coordinator.ownershipMutex);
                coordinator.directLease = false;
            }
            catch(...)
            {
                // Ownership release is best effort at a noexcept cleanup boundary.
                return;
            }
        }

        void releaseInput(Types::Input::Stream stream, const void *owner) noexcept
        {
            if(!validInputStream(stream) || owner == nullptr)
            {
                return;
            }

            try
            {
                InputCoordinator &coordinator = inputCoordinator(stream);
                std::lock_guard lock(coordinator.ownershipMutex);

                if(coordinator.owner == owner)
                {
                    coordinator.owner = nullptr;
                }
            }
            catch(...)
            {
                // Ownership release is best effort at a noexcept cleanup boundary.
                return;
            }
        }
    } // namespace Internal

    Session::Session() noexcept = default;

    Session::Session(Session &&other) noexcept = default;

    /// ------------------------------------------------------------
    /// Lifecycle
    /// ------------------------------------------------------------

    GameWIP::Status::Types::Status Session::restoreOutputState(bool retainOnFailure) noexcept
    {
        if(!state_)
        {
            return GameWIP::Status::Types::Status{};
        }

        try
        {
            std::lock_guard lock(state_->persistentOutputMutex);
            GameWIP::Status::Types::Status firstFailure = GameWIP::Status::Types::Status{};

            // Restore persistent scopes in reverse activation order.
            while(state_->outputRestoreCount > 0)
            {
                const State::PersistentOutputState restore = state_->outputRestoreOrder[state_->outputRestoreCount - 1];

                GameWIP::Status::Types::Status status = GameWIP::Status::Types::Status{};

                switch(restore)
                {
                case State::PersistentOutputState::CursorHidden:
                    status = state_->cursorHiddenScope.restore().status();
                    break;
                case State::PersistentOutputState::AlternateScreen:
                    status = state_->alternateScreenScope.leave().status();
                    break;
                }

                if(!status.ok())
                {
                    if(retainOnFailure)
                    {
                        return status;
                    }

                    switch(restore)
                    {
                    case State::PersistentOutputState::CursorHidden:
                        state_->cursorHiddenScope.restoreOnDestruction_ = false;
                        break;
                    case State::PersistentOutputState::AlternateScreen:
                        state_->alternateScreenScope.restoreOnDestruction_ = false;
                        break;
                    }

                    if(firstFailure.ok())
                    {
                        firstFailure = std::move(status);
                    }
                }

                --state_->outputRestoreCount;
            }

            return firstFailure;
        }
        catch(const std::bad_alloc &)
        {
            return GameWIP::Terminal::Internal::backendStatus(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return GameWIP::Terminal::Internal::backendStatus(ErrorCode::Unknown);
        }
    }

    Session::~Session() noexcept
    {
        if(!state_)
        {
            return;
        }

        try
        {
            State::Mutation mutation(*state_);

            if(!state_->open.load(std::memory_order_acquire))
            {
                return;
            }
            static_cast<void>(restoreOutputState(false));
            static_cast<void>(restoreManagedInput(state_->options.input, state_->ownership, state_.get(), false));
            state_->open.store(false, std::memory_order_release);
        }
        catch(...)
        {
            static_cast<void>(restoreOutputState(false));
            Internal::releaseInput(state_->options.input, state_.get());
            state_->open.store(false, std::memory_order_release);
        }
    }

    bool Session::isOpen() const noexcept
    {
        return state_ && state_->open.load(std::memory_order_acquire);
    }

    GameWIP::Status::Types::Result<void> Session::open(const Types::SessionOptions &options) noexcept
    {
        if(!validInputStream(options.input) || !validOutputStream(options.output) || !validDeliveryMode(options.deliveryMode) ||
           !validControlKeyMode(options.controlKeyMode))
        {
            return Internal::result(GameWIP::Terminal::Internal::backendStatus(ErrorCode::InvalidArgument));
        }

        try
        {
            // Allocate state before acquiring the lifecycle mutation gate.
            if(!state_)
            {
                state_ = std::make_unique<State>();
            }

            if(State::Operation::activeOnCurrentThread(*state_))
            {
                return Internal::result(GameWIP::Terminal::Internal::backendStatus(ErrorCode::AlreadyOpen));
            }
            State::Mutation mutation(*state_);

            if(state_->open.load(std::memory_order_acquire))
            {
                return Internal::result(GameWIP::Terminal::Internal::backendStatus(ErrorCode::AlreadyOpen));
            }

            // Acquire input ownership and configure its mode before publishing the session.
            PreparedInputOwnership ownership;
            GameWIP::Status::Types::Status status = acquireManagedInput(options, state_.get(), ownership);

            if(!status.ok())
            {
                return Internal::result(std::move(status));
            }
            state_->options = options;
            state_->ownership = ownership;
            state_->open.store(true, std::memory_order_release);
            return GameWIP::Status::Types::Result<void>::success();
        }
        catch(const std::bad_alloc &)
        {
            return Internal::result(GameWIP::Terminal::Internal::backendStatus(ErrorCode::OutOfMemory));
        }
        catch(...)
        {
            return Internal::result(GameWIP::Terminal::Internal::backendStatus(ErrorCode::Unknown));
        }
    }

    GameWIP::Status::Types::Result<void> Session::close() noexcept
    {
        if(!state_)
        {
            return GameWIP::Status::Types::Result<void>::success();
        }

        try
        {
            // Prevent reentrant close while waiting for active operations to drain.
            if(State::Operation::activeOnCurrentThread(*state_))
            {
                return Internal::result(GameWIP::Terminal::Internal::backendStatus(ErrorCode::ResourceBusy));
            }
            State::Mutation mutation(*state_);

            if(!state_->open.load(std::memory_order_acquire))
            {
                return GameWIP::Status::Types::Result<void>::success();
            }

            // Restore output modes before releasing the managed input session.
            GameWIP::Status::Types::Status status = restoreOutputState(true);

            if(!status.ok())
            {
                return Internal::result(std::move(status));
            }

            status = restoreManagedInput(state_->options.input, state_->ownership, state_.get(), true);

            if(!status.ok())
            {
                return Internal::result(std::move(status));
            }

            state_->open.store(false, std::memory_order_release);
            return GameWIP::Status::Types::Result<void>::success();
        }
        catch(const std::bad_alloc &)
        {
            return Internal::result(GameWIP::Terminal::Internal::backendStatus(ErrorCode::OutOfMemory));
        }
        catch(...)
        {
            return Internal::result(GameWIP::Terminal::Internal::backendStatus(ErrorCode::Unknown));
        }
    }

    /// ------------------------------------------------------------
    /// Input
    /// ------------------------------------------------------------

    Types::Input::EventResult Session::readEvent(const Types::Input::EventOptions &options) noexcept
    {
        if(!state_)
        {
            return failedResult<Types::Input::EventResult>(notOpenStatus());
        }

        try
        {
            State::Operation operation(*state_);
            std::lock_guard inputOperationLock(state_->inputOperationMutex);

            if(!state_->open.load(std::memory_order_acquire))
            {
                return failedResult<Types::Input::EventResult>(notOpenStatus());
            }

            if(state_->options.deliveryMode != Types::Input::DeliveryMode::Events)
            {
                return failedResult<Types::Input::EventResult>(unsupportedStatus());
            }
            const ReadDecision decision =
                validateReadContract(state_->ownership.capabilities, ReadOperation::Event, options.timeout, options.stopToken);

            if(!decision.status.ok())
            {
                return failedResult<Types::Input::EventResult>(decision.status);
            }

            if(decision.cancelled)
            {
                return cancelledResult<Types::Input::EventResult>();
            }
            std::lock_guard inputLock(Internal::inputIoMutex(state_->options.input));
            return Internal::result(Platform::readEvent(state_->options.input, state_->options.output, options));
        }
        catch(const std::bad_alloc &)
        {
            return failedResult<Types::Input::EventResult>(GameWIP::Terminal::Internal::backendStatus(ErrorCode::OutOfMemory));
        }
        catch(...)
        {
            return failedResult<Types::Input::EventResult>(GameWIP::Terminal::Internal::backendStatus(ErrorCode::Unknown));
        }
    }

    Types::Input::ByteResult Session::readBytes(std::span<std::byte> outputBuffer, const Types::Input::ByteOptions &options) noexcept
    {
        if(!state_)
        {
            return failedResult<Types::Input::ByteResult>(notOpenStatus());
        }

        try
        {
            State::Operation operation(*state_);
            std::lock_guard inputOperationLock(state_->inputOperationMutex);

            if(!state_->open.load(std::memory_order_acquire))
            {
                return failedResult<Types::Input::ByteResult>(notOpenStatus());
            }

            if(state_->options.deliveryMode != Types::Input::DeliveryMode::Stream)
            {
                return failedResult<Types::Input::ByteResult>(unsupportedStatus());
            }
            const ReadDecision decision =
                validateReadContract(state_->ownership.capabilities, ReadOperation::Bytes, options.timeout, options.stopToken);

            if(!decision.status.ok())
            {
                return failedResult<Types::Input::ByteResult>(decision.status);
            }

            if(decision.cancelled)
            {
                return cancelledResult<Types::Input::ByteResult>();
            }
            std::lock_guard inputLock(Internal::inputIoMutex(state_->options.input));
            return Internal::result(Platform::readBytes(state_->options.input, outputBuffer, options));
        }
        catch(const std::bad_alloc &)
        {
            return failedResult<Types::Input::ByteResult>(GameWIP::Terminal::Internal::backendStatus(ErrorCode::OutOfMemory));
        }
        catch(...)
        {
            return failedResult<Types::Input::ByteResult>(GameWIP::Terminal::Internal::backendStatus(ErrorCode::Unknown));
        }
    }

    Types::Input::TextResult Session::readText(const Types::Input::TextOptions &options) noexcept
    {
        if(!state_)
        {
            return failedResult<Types::Input::TextResult>(notOpenStatus());
        }

        if(options.maxReturnedBytes == 0)
        {
            return failedResult<Types::Input::TextResult>(invalidArgumentStatus());
        }

        try
        {
            State::Operation operation(*state_);
            std::lock_guard inputOperationLock(state_->inputOperationMutex);

            if(!state_->open.load(std::memory_order_acquire))
            {
                return failedResult<Types::Input::TextResult>(notOpenStatus());
            }

            if(state_->options.deliveryMode != Types::Input::DeliveryMode::Stream)
            {
                return failedResult<Types::Input::TextResult>(unsupportedStatus());
            }
            const ReadDecision decision =
                validateReadContract(state_->ownership.capabilities, ReadOperation::Text, options.timeout, options.stopToken);

            if(!decision.status.ok())
            {
                return failedResult<Types::Input::TextResult>(decision.status);
            }

            if(decision.cancelled)
            {
                return cancelledResult<Types::Input::TextResult>();
            }
            std::lock_guard inputLock(Internal::inputIoMutex(state_->options.input));
            return Internal::result(Platform::readText(state_->options.input, options));
        }
        catch(const std::bad_alloc &)
        {
            return failedResult<Types::Input::TextResult>(GameWIP::Terminal::Internal::backendStatus(ErrorCode::OutOfMemory));
        }
        catch(...)
        {
            return failedResult<Types::Input::TextResult>(GameWIP::Terminal::Internal::backendStatus(ErrorCode::Unknown));
        }
    }

    Types::Input::LineResult Session::readLine(const Types::Input::LineOptions &options) noexcept
    {
        if(!state_)
        {
            return failedResult<Types::Input::LineResult>(notOpenStatus());
        }

        if(options.maxReturnedBytes == 0 || !validReadLineEndingMode(options.lineEndingMode))
        {
            return failedResult<Types::Input::LineResult>(invalidArgumentStatus());
        }

        try
        {
            State::Operation operation(*state_);
            std::lock_guard inputOperationLock(state_->inputOperationMutex);

            if(!state_->open.load(std::memory_order_acquire))
            {
                return failedResult<Types::Input::LineResult>(notOpenStatus());
            }

            if(state_->options.deliveryMode != Types::Input::DeliveryMode::Stream)
            {
                return failedResult<Types::Input::LineResult>(unsupportedStatus());
            }
            const ReadDecision decision =
                validateReadContract(state_->ownership.capabilities, ReadOperation::Line, options.timeout, options.stopToken);

            if(!decision.status.ok())
            {
                return failedResult<Types::Input::LineResult>(decision.status);
            }

            if(decision.cancelled)
            {
                return cancelledResult<Types::Input::LineResult>();
            }

            if(state_->ownership.capabilities.kind == Types::StreamKind::Terminal && state_->ownership.capabilities.supportsEventInput)
            {
                return Internal::result(
                    Internal::managedTerminalLineRead(state_->options.input, state_->options.output, options, state_->lineBoundaryStorage));
            }
            std::lock_guard inputLock(Internal::inputIoMutex(state_->options.input));
            return Internal::result(Platform::readLine(state_->options.input, options));
        }
        catch(const std::bad_alloc &)
        {
            return failedResult<Types::Input::LineResult>(GameWIP::Terminal::Internal::backendStatus(ErrorCode::OutOfMemory));
        }
        catch(...)
        {
            return failedResult<Types::Input::LineResult>(GameWIP::Terminal::Internal::backendStatus(ErrorCode::Unknown));
        }
    }

    Types::Input::CapabilitiesResult Session::getInputCapabilities() const noexcept
    {
        if(!state_)
        {
            return Internal::failure<Types::Input::Capabilities>(notOpenStatus());
        }

        try
        {
            State::Operation operation(*state_);

            if(!state_->open.load(std::memory_order_acquire))
            {
                return Internal::failure<Types::Input::Capabilities>(notOpenStatus());
            }

            return GameWIP::Status::Types::Result<Types::Input::Capabilities>::success(state_->ownership.capabilities);
        }
        catch(...)
        {
            return Internal::failure<Types::Input::Capabilities>(Internal::exceptionStatus());
        }
    }

    /// Temporary managed input
    /// ------------------------------------------------------------

    Types::Input::EventResult readEvent(const Types::Input::EventOptions &options) noexcept
    {
        return readEvent(Types::Input::Stream::Stdin, options);
    }

    Types::Input::EventResult readEvent(Types::Input::Stream stream, const Types::Input::EventOptions &options) noexcept
    {
        if(!validInputStream(stream))
        {
            return failedResult<Types::Input::EventResult>(invalidArgumentStatus());
        }

        const ReadDecision initial = validateDirectReadOptions(options);

        if(!initial.status.ok())
        {
            return failedResult<Types::Input::EventResult>(initial.status);
        }

        if(initial.cancelled)
        {
            return cancelledResult<Types::Input::EventResult>();
        }

        try
        {
            DirectInputLease lease(stream);
            GameWIP::Status::Types::Status status = lease.open(Types::Input::DeliveryMode::Events);

            if(!status.ok())
            {
                return failedResult<Types::Input::EventResult>(std::move(status));
            }

            const ReadDecision decision = validateReadContract(lease.capabilities(), ReadOperation::Event, options.timeout, options.stopToken);

            if(!decision.status.ok())
            {
                status = lease.finish(decision.status);
                return failedResult<Types::Input::EventResult>(std::move(status));
            }

            if(decision.cancelled)
            {
                Types::Input::EventResult result = cancelledResult<Types::Input::EventResult>();
                status = lease.finish(result.status());
                return Internal::withStatus(std::move(result), std::move(status));
            }
            Types::Input::EventResult result = [&]
            {
                std::lock_guard lock(Internal::inputIoMutex(stream));
                return Platform::readEvent(stream, Types::Output::Stream::Stdout, options);
            }();
            status = lease.finish(result.status());
            return Internal::withStatus(std::move(result), std::move(status));
        }
        catch(const std::bad_alloc &)
        {
            return failedResult<Types::Input::EventResult>(GameWIP::Terminal::Internal::backendStatus(ErrorCode::OutOfMemory));
        }
        catch(...)
        {
            return failedResult<Types::Input::EventResult>(GameWIP::Terminal::Internal::backendStatus(ErrorCode::Unknown));
        }
    }

    Types::Input::ByteResult readBytes(std::span<std::byte> outputBuffer, const Types::Input::ByteOptions &options) noexcept
    {
        return readBytes(Types::Input::Stream::Stdin, outputBuffer, options);
    }

    Types::Input::ByteResult readBytes(
        Types::Input::Stream stream,
        std::span<std::byte> outputBuffer,
        const Types::Input::ByteOptions &options) noexcept
    {
        if(!validInputStream(stream))
        {
            return failedResult<Types::Input::ByteResult>(invalidArgumentStatus());
        }

        const ReadDecision initial = validateDirectReadOptions(options);

        if(!initial.status.ok())
        {
            return failedResult<Types::Input::ByteResult>(initial.status);
        }

        if(initial.cancelled)
        {
            return cancelledResult<Types::Input::ByteResult>();
        }

        try
        {
            DirectInputLease lease(stream);
            GameWIP::Status::Types::Status status = lease.open(Types::Input::DeliveryMode::Stream);

            if(!status.ok())
            {
                return failedResult<Types::Input::ByteResult>(std::move(status));
            }

            const ReadDecision decision = validateReadContract(lease.capabilities(), ReadOperation::Bytes, options.timeout, options.stopToken);

            if(!decision.status.ok())
            {
                status = lease.finish(decision.status);
                return failedResult<Types::Input::ByteResult>(std::move(status));
            }
            Types::Input::ByteResult result = [&]
            {
                std::lock_guard lock(Internal::inputIoMutex(stream));
                return Platform::readBytes(stream, outputBuffer, options);
            }();
            status = lease.finish(result.status());
            return Internal::withStatus(std::move(result), std::move(status));
        }
        catch(const std::bad_alloc &)
        {
            return failedResult<Types::Input::ByteResult>(GameWIP::Terminal::Internal::backendStatus(ErrorCode::OutOfMemory));
        }
        catch(...)
        {
            return failedResult<Types::Input::ByteResult>(GameWIP::Terminal::Internal::backendStatus(ErrorCode::Unknown));
        }
    }

    Types::Input::TextResult readText(const Types::Input::TextOptions &options) noexcept
    {
        return readText(Types::Input::Stream::Stdin, options);
    }

    Types::Input::TextResult readText(Types::Input::Stream stream, const Types::Input::TextOptions &options) noexcept
    {
        if(!validInputStream(stream) || options.maxReturnedBytes == 0)
        {
            return failedResult<Types::Input::TextResult>(invalidArgumentStatus());
        }

        const ReadDecision initial = validateDirectReadOptions(options);

        if(!initial.status.ok())
        {
            return failedResult<Types::Input::TextResult>(initial.status);
        }

        if(initial.cancelled)
        {
            return cancelledResult<Types::Input::TextResult>();
        }

        try
        {
            DirectInputLease lease(stream);
            GameWIP::Status::Types::Status status = lease.open(Types::Input::DeliveryMode::Stream);

            if(!status.ok())
            {
                return failedResult<Types::Input::TextResult>(std::move(status));
            }

            const ReadDecision decision = validateReadContract(lease.capabilities(), ReadOperation::Text, options.timeout, options.stopToken);

            if(!decision.status.ok())
            {
                status = lease.finish(decision.status);
                return failedResult<Types::Input::TextResult>(std::move(status));
            }
            Types::Input::TextResult result = [&]
            {
                std::lock_guard lock(Internal::inputIoMutex(stream));
                return Platform::readText(stream, options);
            }();
            status = lease.finish(result.status());
            return Internal::withStatus(std::move(result), std::move(status));
        }
        catch(const std::bad_alloc &)
        {
            return failedResult<Types::Input::TextResult>(GameWIP::Terminal::Internal::backendStatus(ErrorCode::OutOfMemory));
        }
        catch(...)
        {
            return failedResult<Types::Input::TextResult>(GameWIP::Terminal::Internal::backendStatus(ErrorCode::Unknown));
        }
    }

    Types::Input::LineResult readLine(const Types::Input::LineOptions &options) noexcept
    {
        return readLine(Types::Input::Stream::Stdin, options);
    }

    Types::Input::LineResult readLine(Types::Input::Stream stream, const Types::Input::LineOptions &options) noexcept
    {
        if(!validInputStream(stream) || options.maxReturnedBytes == 0 || !validReadLineEndingMode(options.lineEndingMode))
        {
            return failedResult<Types::Input::LineResult>(invalidArgumentStatus());
        }

        const ReadDecision initial = validateDirectReadOptions(options);

        if(!initial.status.ok())
        {
            return failedResult<Types::Input::LineResult>(initial.status);
        }

        if(initial.cancelled)
        {
            return cancelledResult<Types::Input::LineResult>();
        }

        try
        {
            DirectInputLease lease(stream);
            GameWIP::Status::Types::Status status = lease.open(Types::Input::DeliveryMode::Stream);

            if(!status.ok())
            {
                return failedResult<Types::Input::LineResult>(std::move(status));
            }

            const ReadDecision decision = validateReadContract(lease.capabilities(), ReadOperation::Line, options.timeout, options.stopToken);

            if(!decision.status.ok())
            {
                status = lease.finish(decision.status);
                return failedResult<Types::Input::LineResult>(std::move(status));
            }
            Types::Input::LineResult result = [&]
            {
                if(lease.capabilities().kind == Types::StreamKind::Terminal && lease.capabilities().supportsEventInput)
                {
                    std::vector<std::size_t> graphemeStorage;
                    return Internal::managedTerminalLineRead(stream, Types::Output::Stream::Stdout, options, graphemeStorage);
                }

                std::lock_guard lock(Internal::inputIoMutex(stream));
                return Platform::readLine(stream, options);
            }();

            status = lease.finish(result.status());
            return Internal::withStatus(std::move(result), std::move(status));
        }
        catch(const std::bad_alloc &)
        {
            return failedResult<Types::Input::LineResult>(GameWIP::Terminal::Internal::backendStatus(ErrorCode::OutOfMemory));
        }
        catch(...)
        {
            return failedResult<Types::Input::LineResult>(GameWIP::Terminal::Internal::backendStatus(ErrorCode::Unknown));
        }
    }
} // namespace GameWIP::Terminal
