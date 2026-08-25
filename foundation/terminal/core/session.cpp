/// @file session.cpp
/// @brief Managed Terminal input ownership, Session lifecycle, and checked read entry points.

#include "terminal/terminal.h"

#include "terminal/internal/terminal_input.h"
#include "terminal/internal/terminal_platform.h"
#include "unicode/unicode.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <condition_variable>
#include <exception>
#include <format>
#include <limits>
#include <mutex>
#include <new>
#include <optional>
#include <span>
#include <stop_token>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
            Types::Input::Capabilities capabilities{};
            Detail::Platform::InputModeSnapshot previousMode{};
            bool hasPreviousMode = false;
            bool claimed = false;
        };

        struct ReadDecision
        {
            IO::Types::Status status = IO::successStatus();
            bool cancelled = false;
        };

        [[nodiscard]] InputCoordinator &inputCoordinator([[maybe_unused]] Types::Input::Stream stream) noexcept
        {
            static InputCoordinator stdinCoordinator;
            return stdinCoordinator;
        }

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

        [[nodiscard]] IO::Types::Status exceptionStatus() noexcept
        {
            try
            {
                throw;
            }
            catch (const std::bad_alloc &)
            {
                return IO::makeStatus(ErrorCode::OutOfMemory);
            }
            catch (const std::length_error &)
            {
                return IO::makeStatus(ErrorCode::SizeLimitExceeded);
            }
            catch (const std::format_error &)
            {
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }
            catch (...)
            {
                return IO::makeStatus(ErrorCode::Unknown);
            }
        }

        [[nodiscard]] bool operationSupported(const Types::Input::Capabilities &capabilities, ReadOperation operation) noexcept
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

        [[nodiscard]] bool deliverySupported(const Types::Input::Capabilities &capabilities, Types::Input::DeliveryMode deliveryMode) noexcept
        {
            if (deliveryMode == Types::Input::DeliveryMode::Events)
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
            const Types::Input::Capabilities &capabilities,
            ReadOperation operation,
            const std::optional<std::chrono::milliseconds> &timeout,
            const std::stop_token &stopToken)
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

        [[nodiscard]] Detail::Platform::InputMode managedMode(
            const Types::SessionOptions &options,
            Detail::Platform::InputMode previousMode,
            const Types::Input::Capabilities &capabilities) noexcept
        {
            Detail::Platform::InputMode mode = previousMode;
            mode.processControlKeys = options.controlKeyMode == Types::Input::ControlKeyMode::NativeProcessing;

            // Event delivery, and Stream delivery on event-capable terminals, use one immediate native engine.
            // A backend without structured-event support keeps its existing Stream line discipline until its
            // own event decoder exists.
            if (options.deliveryMode == Types::Input::DeliveryMode::Events || capabilities.supportsEventInput)
            {
                mode.lineBuffered = false;
                mode.echoInput = false;
                mode.reportResizeEvents = capabilities.supportsResizeEvents;
                mode.reportPointerEvents = false;
                mode.exclusiveEventDelivery = true;
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
            // NOLINTNEXTLINE(bugprone-empty-catch) -- Diagnostic enrichment must not replace the primary failure.
            catch (...)
            {
                // The primary failure remains authoritative when diagnostic enrichment cannot allocate.
            }
        }

        [[nodiscard]] IO::Types::Status restoreManagedInput(
            Types::Input::Stream stream,
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
                    Types::Input::CapabilitiesResult capabilityResult = Detail::Platform::getInputCapabilities(options.input);
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
                                status = Detail::Platform::setInputMode(
                                    options.input,
                                    managedMode(options, ownership.previousMode.mode, ownership.capabilities));
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
            explicit DirectInputLease(Types::Input::Stream stream) noexcept
                : options_{.input = stream, .output = Types::Output::Stream::Stdout}
            {
            }

            DirectInputLease(const DirectInputLease &) = delete;
            DirectInputLease &operator=(const DirectInputLease &) = delete;

            ~DirectInputLease() noexcept
            {
                static_cast<void>(restoreManagedInput(options_.input, ownership_, this, false));
            }

            [[nodiscard]] IO::Types::Status open(
                Types::Input::DeliveryMode deliveryMode,
                Types::Input::ControlKeyMode controlKeyMode = Types::Input::ControlKeyMode::NativeProcessing) noexcept
            {
                options_.deliveryMode = deliveryMode;
                options_.controlKeyMode = controlKeyMode;
                return acquireManagedInput(options_, this, ownership_);
            }

            [[nodiscard]] const Types::Input::Capabilities &capabilities() const noexcept
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
            result.outcome = Types::Input::ReadOutcome::Cancelled;
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
        std::mutex &inputIoMutex(Types::Input::Stream stream) noexcept
        {
            return inputCoordinator(stream).ioMutex;
        }

        IO::Types::Status claimInput(Types::Input::Stream stream, const void *owner) noexcept
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

        void releaseInput(Types::Input::Stream stream, const void *owner) noexcept
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
                // Ownership release is best effort at a noexcept cleanup boundary.
                return;
            }
        }
    } // namespace Detail

    struct Session::State
    {
        enum class PersistentOutputState : std::uint8_t
        {
            CursorHidden,
            AlternateScreen
        };

        // Operations retain the open binding through a lightweight count without holding this mutex while backend
        // work or user formatters execute. Lifecycle mutation closes the admission gate and waits for the count.
        std::mutex lifecycleMutex;
        std::condition_variable lifecycleCondition;
        bool lifecycleMutation = false;
        std::size_t activeOperations = 0;
        std::mutex inputOperationMutex;
        std::mutex persistentOutputMutex;
        std::atomic_bool open = false;
        Types::SessionOptions options{};
        PreparedInputOwnership ownership{};

        // Retain caller-backed Unicode boundary storage across line reads so steady-state editing reuses capacity.
        std::vector<std::size_t> lineBoundaryStorage;

        CursorHiddenScope cursorHiddenScope;
        AlternateScreenScope alternateScreenScope;
        std::array<PersistentOutputState, 2> outputRestoreOrder{};
        std::size_t outputRestoreCount = 0;

        class Operation final
        {
        public:
            explicit Operation(State &requested)
                : previous_(current_)
            {
                bool reentrant = false;
                for (const Operation *operation = current_; operation != nullptr; operation = operation->previous_)
                {
                    if (operation->state_ == &requested)
                    {
                        reentrant = true;
                        break;
                    }
                }

                std::unique_lock lock(requested.lifecycleMutex);
                if (!reentrant)
                {
                    requested.lifecycleCondition.wait(
                        lock,
                        [&requested]
                        {
                            return !requested.lifecycleMutation;
                        });
                }
                if (!requested.open.load(std::memory_order_acquire))
                {
                    return;
                }

                ++requested.activeOperations;
                state_ = &requested;
                current_ = this;
            }

            Operation(const Operation &) = delete;
            Operation &operator=(const Operation &) = delete;

            ~Operation()
            {
                if (state_ == nullptr)
                {
                    return;
                }

                current_ = previous_;
                std::lock_guard lock(state_->lifecycleMutex);
                --state_->activeOperations;
                if (state_->activeOperations == 0)
                {
                    state_->lifecycleCondition.notify_all();
                }
            }

            [[nodiscard]] static bool activeOnCurrentThread(const State &state) noexcept
            {
                for (const Operation *operation = current_; operation != nullptr; operation = operation->previous_)
                {
                    if (operation->state_ == &state)
                    {
                        return true;
                    }
                }
                return false;
            }

        private:
            inline static thread_local Operation *current_ = nullptr;
            State *state_ = nullptr;
            Operation *previous_ = nullptr;
        };

        class Mutation final
        {
        public:
            explicit Mutation(State &state)
                : state_(state)
                , lock_(state.lifecycleMutex)
            {
                state_.lifecycleCondition.wait(
                    lock_,
                    [this]
                    {
                        return !state_.lifecycleMutation;
                    });
                state_.lifecycleMutation = true;
                try
                {
                    state_.lifecycleCondition.wait(
                        lock_,
                        [this]
                        {
                            return state_.activeOperations == 0;
                        });
                }
                catch (...)
                {
                    state_.lifecycleMutation = false;
                    state_.lifecycleCondition.notify_all();
                    throw;
                }
            }

            Mutation(const Mutation &) = delete;
            Mutation &operator=(const Mutation &) = delete;

            ~Mutation()
            {
                state_.lifecycleMutation = false;
                lock_.unlock();
                state_.lifecycleCondition.notify_all();
            }

        private:
            State &state_;
            std::unique_lock<std::mutex> lock_;
        };
    };

    Session::Session() noexcept = default;

    Session::Session(Session &&other) noexcept = default;

    IO::Types::Status Session::restoreOutputState(bool retainOnFailure) noexcept
    {
        if (!state_)
        {
            return IO::successStatus();
        }

        try
        {
            std::lock_guard lock(state_->persistentOutputMutex);
            IO::Types::Status firstFailure = IO::successStatus();

            while (state_->outputRestoreCount > 0)
            {
                const State::PersistentOutputState restore = state_->outputRestoreOrder[state_->outputRestoreCount - 1];

                IO::Types::Status status = IO::successStatus();
                switch (restore)
                {
                case State::PersistentOutputState::CursorHidden:
                    status = state_->cursorHiddenScope.restore();
                    break;
                case State::PersistentOutputState::AlternateScreen:
                    status = state_->alternateScreenScope.leave();
                    break;
                }

                if (!status.ok())
                {
                    if (retainOnFailure)
                    {
                        return status;
                    }
                    switch (restore)
                    {
                    case State::PersistentOutputState::CursorHidden:
                        state_->cursorHiddenScope.restoreOnDestruction_ = false;
                        break;
                    case State::PersistentOutputState::AlternateScreen:
                        state_->alternateScreenScope.restoreOnDestruction_ = false;
                        break;
                    }
                    if (firstFailure.ok())
                    {
                        firstFailure = std::move(status);
                    }
                }

                --state_->outputRestoreCount;
            }

            return firstFailure;
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

    Session::~Session() noexcept
    {
        if (!state_)
        {
            return;
        }

        try
        {
            State::Mutation mutation(*state_);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return;
            }

            static_cast<void>(restoreOutputState(false));
            static_cast<void>(restoreManagedInput(state_->options.input, state_->ownership, state_.get(), false));
            state_->open.store(false, std::memory_order_release);
        }
        catch (...)
        {
            static_cast<void>(restoreOutputState(false));
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

            if (State::Operation::activeOnCurrentThread(*state_))
            {
                return IO::makeStatus(ErrorCode::AlreadyOpen);
            }

            State::Mutation mutation(*state_);
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
            state_->ownership = ownership;
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
            if (State::Operation::activeOnCurrentThread(*state_))
            {
                return IO::makeStatus(ErrorCode::ResourceBusy);
            }

            State::Mutation mutation(*state_);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return IO::successStatus();
            }

            IO::Types::Status status = restoreOutputState(true);
            if (!status.ok())
            {
                return status;
            }

            status = restoreManagedInput(state_->options.input, state_->ownership, state_.get(), true);
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

    Types::Input::EventResult Session::readEvent(const Types::Input::EventOptions &options) noexcept
    {
        if (!state_)
        {
            return failedResult<Types::Input::EventResult>(notOpenStatus());
        }

        try
        {
            State::Operation operation(*state_);
            std::lock_guard inputOperationLock(state_->inputOperationMutex);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return failedResult<Types::Input::EventResult>(notOpenStatus());
            }
            if (state_->options.deliveryMode != Types::Input::DeliveryMode::Events)
            {
                return failedResult<Types::Input::EventResult>(unsupportedStatus());
            }

            const ReadDecision decision =
                validateReadContract(state_->ownership.capabilities, ReadOperation::Event, options.timeout, options.stopToken);
            if (!decision.status.ok())
            {
                return failedResult<Types::Input::EventResult>(decision.status);
            }
            if (decision.cancelled)
            {
                return cancelledResult<Types::Input::EventResult>();
            }

            std::lock_guard inputLock(Detail::inputIoMutex(state_->options.input));
            return Detail::Platform::readEvent(state_->options.input, state_->options.output, options);
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

    Types::Input::ByteResult Session::readBytes(std::span<std::byte> outputBuffer, const Types::Input::ByteOptions &options) noexcept
    {
        if (!state_)
        {
            return failedResult<Types::Input::ByteResult>(notOpenStatus());
        }

        try
        {
            State::Operation operation(*state_);
            std::lock_guard inputOperationLock(state_->inputOperationMutex);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return failedResult<Types::Input::ByteResult>(notOpenStatus());
            }
            if (state_->options.deliveryMode != Types::Input::DeliveryMode::Stream)
            {
                return failedResult<Types::Input::ByteResult>(unsupportedStatus());
            }

            const ReadDecision decision =
                validateReadContract(state_->ownership.capabilities, ReadOperation::Bytes, options.timeout, options.stopToken);
            if (!decision.status.ok())
            {
                return failedResult<Types::Input::ByteResult>(decision.status);
            }
            if (decision.cancelled)
            {
                return cancelledResult<Types::Input::ByteResult>();
            }

            std::lock_guard inputLock(Detail::inputIoMutex(state_->options.input));
            return Detail::Platform::readBytes(state_->options.input, outputBuffer, options);
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

    Types::Input::TextResult Session::readText(const Types::Input::TextOptions &options) noexcept
    {
        if (!state_)
        {
            return failedResult<Types::Input::TextResult>(notOpenStatus());
        }

        if (options.maxReturnedBytes == 0)
        {
            return failedResult<Types::Input::TextResult>(invalidArgumentStatus());
        }

        try
        {
            State::Operation operation(*state_);
            std::lock_guard inputOperationLock(state_->inputOperationMutex);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return failedResult<Types::Input::TextResult>(notOpenStatus());
            }
            if (state_->options.deliveryMode != Types::Input::DeliveryMode::Stream)
            {
                return failedResult<Types::Input::TextResult>(unsupportedStatus());
            }

            const ReadDecision decision =
                validateReadContract(state_->ownership.capabilities, ReadOperation::Text, options.timeout, options.stopToken);
            if (!decision.status.ok())
            {
                return failedResult<Types::Input::TextResult>(decision.status);
            }
            if (decision.cancelled)
            {
                return cancelledResult<Types::Input::TextResult>();
            }

            std::lock_guard inputLock(Detail::inputIoMutex(state_->options.input));
            return Detail::Platform::readText(state_->options.input, options);
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

    Types::Input::LineResult Session::readLine(const Types::Input::LineOptions &options) noexcept
    {
        if (!state_)
        {
            return failedResult<Types::Input::LineResult>(notOpenStatus());
        }

        if (options.maxReturnedBytes == 0 || !validReadLineEndingMode(options.lineEndingMode))
        {
            return failedResult<Types::Input::LineResult>(invalidArgumentStatus());
        }

        try
        {
            State::Operation operation(*state_);
            std::lock_guard inputOperationLock(state_->inputOperationMutex);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return failedResult<Types::Input::LineResult>(notOpenStatus());
            }
            if (state_->options.deliveryMode != Types::Input::DeliveryMode::Stream)
            {
                return failedResult<Types::Input::LineResult>(unsupportedStatus());
            }

            const ReadDecision decision =
                validateReadContract(state_->ownership.capabilities, ReadOperation::Line, options.timeout, options.stopToken);
            if (!decision.status.ok())
            {
                return failedResult<Types::Input::LineResult>(decision.status);
            }
            if (decision.cancelled)
            {
                return cancelledResult<Types::Input::LineResult>();
            }

            if (state_->ownership.capabilities.kind == Types::StreamKind::Terminal && state_->ownership.capabilities.supportsEventInput)
            {
                return Detail::managedTerminalLineRead(state_->options.input, state_->options.output, options, state_->lineBoundaryStorage);
            }

            std::lock_guard inputLock(Detail::inputIoMutex(state_->options.input));
            return Detail::Platform::readLine(state_->options.input, options);
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

    Types::Input::CapabilitiesResult Session::getInputCapabilities() const noexcept
    {
        if (!state_)
        {
            return {.status = notOpenStatus(), .capabilities = {}};
        }

        try
        {
            State::Operation operation(*state_);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return {.status = notOpenStatus(), .capabilities = {}};
            }
            return {.status = IO::successStatus(), .capabilities = state_->ownership.capabilities};
        }
        catch (...)
        {
            return {.status = exceptionStatus(), .capabilities = {}};
        }
    }

    Types::Output::CapabilitiesResult Session::getOutputCapabilities() const noexcept
    {
        if (!state_)
        {
            return {.status = notOpenStatus(), .capabilities = {}};
        }

        try
        {
            State::Operation operation(*state_);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return {.status = notOpenStatus(), .capabilities = {}};
            }
            return Terminal::getOutputCapabilities(state_->options.output);
        }
        catch (...)
        {
            return {.status = exceptionStatus(), .capabilities = {}};
        }
    }

    Types::Output::CapabilitiesResult Session::prepareOutput() noexcept
    {
        if (!state_)
        {
            return {.status = notOpenStatus(), .capabilities = {}};
        }

        try
        {
            State::Operation operation(*state_);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return {.status = notOpenStatus(), .capabilities = {}};
            }
            return Terminal::prepareOutput(state_->options.output);
        }
        catch (...)
        {
            return {.status = exceptionStatus(), .capabilities = {}};
        }
    }

    Types::SizeResult Session::getTerminalSize() const noexcept
    {
        if (!state_)
        {
            return {.status = notOpenStatus(), .size = {}};
        }

        try
        {
            State::Operation operation(*state_);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return {.status = notOpenStatus(), .size = {}};
            }
            return Terminal::getTerminalSize(state_->options.output);
        }
        catch (...)
        {
            return {.status = exceptionStatus(), .size = {}};
        }
    }

    IO::Types::Status Session::writeText(std::string_view utf8Text, const Types::Output::TextOptions &options) noexcept
    {
        if (!state_)
        {
            return notOpenStatus();
        }

        try
        {
            State::Operation operation(*state_);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return notOpenStatus();
            }
            return Terminal::writeText(state_->options.output, utf8Text, options);
        }
        catch (...)
        {
            return exceptionStatus();
        }
    }

    IO::Types::Status Session::writeLine(std::string_view utf8Text, const Types::Output::LineOptions &options) noexcept
    {
        if (!state_)
        {
            return notOpenStatus();
        }

        try
        {
            State::Operation operation(*state_);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return notOpenStatus();
            }
            return Terminal::writeLine(state_->options.output, utf8Text, options);
        }
        catch (...)
        {
            return exceptionStatus();
        }
    }

    IO::Types::WriteResult Session::writeBytes(std::span<const std::byte> bytes, const Types::Output::ByteOptions &options) noexcept
    {
        if (!state_)
        {
            return {.status = notOpenStatus(), .bytesWritten = 0};
        }

        try
        {
            State::Operation operation(*state_);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return {.status = notOpenStatus(), .bytesWritten = 0};
            }
            return Terminal::writeBytes(state_->options.output, bytes, options);
        }
        catch (...)
        {
            return {.status = exceptionStatus(), .bytesWritten = 0};
        }
    }

    IO::Types::Status Session::writeSegments(std::span<const Types::Output::Segment> segments, const Types::Output::SegmentOptions &options) noexcept
    {
        if (!state_)
        {
            return notOpenStatus();
        }

        try
        {
            State::Operation operation(*state_);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return notOpenStatus();
            }
            return Terminal::writeSegments(state_->options.output, segments, options);
        }
        catch (...)
        {
            return exceptionStatus();
        }
    }

    IO::Types::Status Session::vprint(const Types::Output::TextOptions &options, std::string_view format, std::format_args arguments) noexcept
    {
        if (!state_)
        {
            return notOpenStatus();
        }

        try
        {
            State::Operation operation(*state_);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return notOpenStatus();
            }
            return Detail::vprint(state_->options.output, options, format, arguments);
        }
        catch (...)
        {
            return exceptionStatus();
        }
    }

    IO::Types::Status Session::vprintln(const Types::Output::LineOptions &options, std::string_view format, std::format_args arguments) noexcept
    {
        if (!state_)
        {
            return notOpenStatus();
        }

        try
        {
            State::Operation operation(*state_);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return notOpenStatus();
            }
            return Detail::vprintln(state_->options.output, options, format, arguments);
        }
        catch (...)
        {
            return exceptionStatus();
        }
    }

    IO::Types::Status Session::flush(IO::Types::FlushMode mode) noexcept
    {
        if (!state_)
        {
            return notOpenStatus();
        }

        try
        {
            State::Operation operation(*state_);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return notOpenStatus();
            }
            return Terminal::flush(state_->options.output, mode);
        }
        catch (...)
        {
            return exceptionStatus();
        }
    }

    IO::Types::Status Session::resetStyle(const Types::Output::ControlOptions &options) noexcept
    {
        if (!state_)
        {
            return notOpenStatus();
        }

        try
        {
            State::Operation operation(*state_);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return notOpenStatus();
            }
            return Terminal::resetStyle(state_->options.output, options);
        }
        catch (...)
        {
            return exceptionStatus();
        }
    }

    IO::Types::Status Session::moveCursor(
        Types::Cursor::MoveDirection direction,
        std::uint32_t amount,
        const Types::Output::ControlOptions &options) noexcept
    {
        if (!state_)
        {
            return notOpenStatus();
        }

        try
        {
            State::Operation operation(*state_);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return notOpenStatus();
            }
            return Terminal::moveCursor(state_->options.output, direction, amount, options);
        }
        catch (...)
        {
            return exceptionStatus();
        }
    }

    IO::Types::Status Session::setCursorPosition(Types::Cursor::Position position, const Types::Output::ControlOptions &options) noexcept
    {
        if (!state_)
        {
            return notOpenStatus();
        }

        try
        {
            State::Operation operation(*state_);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return notOpenStatus();
            }
            return Terminal::setCursorPosition(state_->options.output, position, options);
        }
        catch (...)
        {
            return exceptionStatus();
        }
    }

    Types::Cursor::PositionResult Session::getCursorPosition(const Types::Cursor::QueryOptions &options) noexcept
    {
        if (!state_)
        {
            return {.status = notOpenStatus(), .position = {}};
        }

        try
        {
            State::Operation operation(*state_);
            std::lock_guard inputOperationLock(state_->inputOperationMutex);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return {.status = notOpenStatus(), .position = {}};
            }
            return Terminal::getCursorPosition(state_->options.output, state_->options.input, options);
        }
        catch (...)
        {
            return {.status = exceptionStatus(), .position = {}};
        }
    }

    IO::Types::Status Session::saveCursorPosition(const Types::Output::ControlOptions &options) noexcept
    {
        if (!state_)
        {
            return notOpenStatus();
        }

        try
        {
            State::Operation operation(*state_);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return notOpenStatus();
            }
            return Terminal::saveCursorPosition(state_->options.output, options);
        }
        catch (...)
        {
            return exceptionStatus();
        }
    }

    IO::Types::Status Session::restoreCursorPosition(const Types::Output::ControlOptions &options) noexcept
    {
        if (!state_)
        {
            return notOpenStatus();
        }

        try
        {
            State::Operation operation(*state_);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return notOpenStatus();
            }
            return Terminal::restoreCursorPosition(state_->options.output, options);
        }
        catch (...)
        {
            return exceptionStatus();
        }
    }

    IO::Types::Status Session::setCursorVisible(bool visible, const Types::Output::ControlOptions &options) noexcept
    {
        if (!state_)
        {
            return notOpenStatus();
        }

        try
        {
            State::Operation operation(*state_);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return notOpenStatus();
            }
            if (!IO::isValidFlushMode(options.flushMode))
            {
                return invalidArgumentStatus();
            }

            std::lock_guard persistentLock(state_->persistentOutputMutex);
            if (!visible)
            {
                if (state_->cursorHiddenScope.active())
                {
                    return IO::successStatus();
                }

                CursorHiddenScope scope = scopedCursorHidden(state_->options.output, options);
                if (scope.active())
                {
                    state_->cursorHiddenScope = std::move(scope);
                    state_->outputRestoreOrder[state_->outputRestoreCount++] = State::PersistentOutputState::CursorHidden;
                    return state_->cursorHiddenScope.status();
                }

                return scope.status();
            }

            if (!state_->cursorHiddenScope.active())
            {
                return Terminal::setCursorVisible(state_->options.output, true, options);
            }

            state_->cursorHiddenScope.options_ = options;
            IO::Types::Status status = state_->cursorHiddenScope.restore();
            if (!status.ok())
            {
                return status;
            }

            for (std::size_t index = 0; index < state_->outputRestoreCount; ++index)
            {
                if (state_->outputRestoreOrder[index] == State::PersistentOutputState::CursorHidden)
                {
                    for (std::size_t next = index + 1; next < state_->outputRestoreCount; ++next)
                    {
                        state_->outputRestoreOrder[next - 1] = state_->outputRestoreOrder[next];
                    }
                    --state_->outputRestoreCount;
                    break;
                }
            }
            return IO::successStatus();
        }
        catch (...)
        {
            return exceptionStatus();
        }
    }

    IO::Types::Status Session::clear(Types::Output::ClearTarget target, const Types::Output::ControlOptions &options) noexcept
    {
        if (!state_)
        {
            return notOpenStatus();
        }

        try
        {
            State::Operation operation(*state_);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return notOpenStatus();
            }
            return Terminal::clear(state_->options.output, target, options);
        }
        catch (...)
        {
            return exceptionStatus();
        }
    }

    IO::Types::Status Session::scroll(
        Types::Output::ScrollDirection direction,
        std::uint32_t lines,
        const Types::Output::ControlOptions &options) noexcept
    {
        if (!state_)
        {
            return notOpenStatus();
        }

        try
        {
            State::Operation operation(*state_);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return notOpenStatus();
            }
            return Terminal::scroll(state_->options.output, direction, lines, options);
        }
        catch (...)
        {
            return exceptionStatus();
        }
    }

    IO::Types::Status Session::enterAlternateScreen(const Types::Output::ControlOptions &options) noexcept
    {
        if (!state_)
        {
            return notOpenStatus();
        }

        try
        {
            State::Operation operation(*state_);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return notOpenStatus();
            }
            if (!IO::isValidFlushMode(options.flushMode))
            {
                return invalidArgumentStatus();
            }

            std::lock_guard persistentLock(state_->persistentOutputMutex);
            if (state_->alternateScreenScope.active())
            {
                return IO::successStatus();
            }

            AlternateScreenScope scope = scopedAlternateScreen(state_->options.output, options);
            if (scope.active())
            {
                state_->alternateScreenScope = std::move(scope);
                state_->outputRestoreOrder[state_->outputRestoreCount++] = State::PersistentOutputState::AlternateScreen;
                return state_->alternateScreenScope.status();
            }

            return scope.status();
        }
        catch (...)
        {
            return exceptionStatus();
        }
    }

    IO::Types::Status Session::leaveAlternateScreen(const Types::Output::ControlOptions &options) noexcept
    {
        if (!state_)
        {
            return notOpenStatus();
        }

        try
        {
            State::Operation operation(*state_);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return notOpenStatus();
            }
            if (!IO::isValidFlushMode(options.flushMode))
            {
                return invalidArgumentStatus();
            }

            std::lock_guard persistentLock(state_->persistentOutputMutex);
            if (!state_->alternateScreenScope.active())
            {
                return Terminal::leaveAlternateScreen(state_->options.output, options);
            }

            state_->alternateScreenScope.options_ = options;
            IO::Types::Status status = state_->alternateScreenScope.leave();
            if (!status.ok())
            {
                return status;
            }

            for (std::size_t index = 0; index < state_->outputRestoreCount; ++index)
            {
                if (state_->outputRestoreOrder[index] == State::PersistentOutputState::AlternateScreen)
                {
                    for (std::size_t next = index + 1; next < state_->outputRestoreCount; ++next)
                    {
                        state_->outputRestoreOrder[next - 1] = state_->outputRestoreOrder[next];
                    }
                    --state_->outputRestoreCount;
                    break;
                }
            }
            return IO::successStatus();
        }
        catch (...)
        {
            return exceptionStatus();
        }
    }

    IO::Types::Status Session::setTitle(std::string_view utf8Title, const Types::Output::ControlOptions &options) noexcept
    {
        if (!state_)
        {
            return notOpenStatus();
        }

        try
        {
            State::Operation operation(*state_);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return notOpenStatus();
            }
            return Terminal::setTitle(state_->options.output, utf8Title, options);
        }
        catch (...)
        {
            return exceptionStatus();
        }
    }

    IO::Types::Status Session::ringBell(const Types::Output::ControlOptions &options) noexcept
    {
        if (!state_)
        {
            return notOpenStatus();
        }

        try
        {
            State::Operation operation(*state_);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return notOpenStatus();
            }
            return Terminal::ringBell(state_->options.output, options);
        }
        catch (...)
        {
            return exceptionStatus();
        }
    }

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

    Types::Input::ByteResult readBytes(
        Types::Input::Stream stream,
        std::span<std::byte> outputBuffer,
        const Types::Input::ByteOptions &options) noexcept
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
} // namespace GameWIP::Terminal
