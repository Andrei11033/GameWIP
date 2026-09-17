/// @file progress_dialog.cpp
/// @brief Portable ProgressDialog validation and owner-thread lifecycle.

#include "desktop/dialogs.h"

#include "desktop/internal/desktop_test_hooks.h"
#include "desktop/internal/dialogs_platform.h"
#include "desktop/internal/window_platform.h"
#include "desktop/internal/window_state.h"

#include <atomic>
#include <cmath>
#include <new>

namespace GameWIP::Desktop
{
    namespace
    {
        using IO::Types::ErrorCode;

        std::atomic_uint64_t nextThreadToken{1};
        thread_local std::uint64_t callingThreadToken = 0;

        [[nodiscard]] std::uint64_t currentThreadToken() noexcept
        {
            if (callingThreadToken == 0)
            {
                callingThreadToken = nextThreadToken.fetch_add(1, std::memory_order_relaxed);
                if (callingThreadToken == 0)
                {
                    callingThreadToken = nextThreadToken.fetch_add(1, std::memory_order_relaxed);
                }
            }
            return callingThreadToken;
        }

        [[nodiscard]] IO::Types::Status error(ErrorCode code) noexcept
        {
            return IO::makeStatus(code);
        }

        [[nodiscard]] IO::Types::Status validateText(std::string_view text) noexcept
        {
            return text.contains('\0') ? error(ErrorCode::InvalidArgument) : IO::successStatus();
        }

        [[nodiscard]] bool validMode(Types::Dialogs::Progress::Mode mode) noexcept
        {
            return mode == Types::Dialogs::Progress::Mode::Determinate || mode == Types::Dialogs::Progress::Mode::Indeterminate;
        }

        [[nodiscard]] IO::Types::Status requireLiveState(const Detail::ProgressDialogState *state, bool owned) noexcept
        {
            if (state == nullptr)
            {
                return error(ErrorCode::NotOpen);
            }
            if (!owned)
            {
                return error(ErrorCode::ResourceBusy);
            }
            return state->nativeDestroyedPendingFinalize || !state->platform ? error(ErrorCode::NotOpen) : IO::successStatus();
        }
    } // namespace

    ProgressDialog::ProgressDialog() noexcept = default;

    ProgressDialog::~ProgressDialog() noexcept
    {
        if (!state_)
        {
            return;
        }

        if (state_->platform && !ownedByCurrentThread())
        {
            if (Detail::Platform::deferProgressCleanupToOwner(state_))
            {
                ownerThreadToken_.store(0, std::memory_order_release);
                return;
            }
            static_cast<void>(Detail::Platform::closeProgressBestEffort(*state_));
        }
        else
        {
            if (!Detail::Platform::closeProgressBestEffort(*state_) && Detail::Platform::deferProgressCleanupToOwner(state_))
            {
                ownerThreadToken_.store(0, std::memory_order_release);
                return;
            }
            state_.reset();
        }
        ownerThreadToken_.store(0, std::memory_order_release);
    }

    IO::Types::Status ProgressDialog::open(const Types::Dialogs::Progress::Description &description) noexcept
    {
        if (state_)
        {
            return error(ErrorCode::AlreadyOpen);
        }
        if (!validMode(description.mode) || !std::isfinite(description.progress) || description.progress < 0.0 || description.progress > 1.0)
        {
            return error(ErrorCode::InvalidArgument);
        }

        for (std::string_view text : {description.title, description.heading, description.message})
        {
            IO::Types::Status status = validateText(text);
            if (!status.ok())
            {
                return status;
            }
        }

        Detail::WindowState *ownerState = nullptr;
        if (description.owner != nullptr)
        {
            ownerState = Detail::WindowAccess::state(*description.owner);
            if (ownerState == nullptr || !Detail::Platform::hasLiveNativeWindow(*ownerState))
            {
                return error(ErrorCode::NotOpen);
            }
            if (!Detail::Platform::ownedByCurrentThread(*ownerState))
            {
                return error(ErrorCode::ResourceBusy);
            }
        }

        try
        {
            if (Detail::consumeFailure(TestHooks::FailurePoint::Allocation))
            {
                return error(ErrorCode::OutOfMemory);
            }
            auto candidate = std::make_unique<Detail::ProgressDialogState>();
            candidate->ownerToken = currentThreadToken();
            candidate->ownerId = ownerState != nullptr ? ownerState->id : Types::WindowId{};
            candidate->mode = description.mode;
            candidate->progress = description.progress;
            candidate->cancelable = description.cancelable;
            candidate->blocksOwner = description.blocksOwner;
            candidate->cancelRequested = false;

            IO::Types::Status status = Detail::Platform::openProgress(*candidate, description);
            if (!status.ok())
            {
                if (!Detail::Platform::closeProgressBestEffort(*candidate))
                {
                    // The candidate remains private to this failed open. Its
                    // owner-thread dispatcher retains any cleanup that could
                    // not complete synchronously.
                    static_cast<void>(Detail::Platform::deferProgressCleanupToOwner(candidate));
                }
                return status;
            }

            const std::uint64_t token = candidate->ownerToken;
            state_ = std::move(candidate);
            ownerThreadToken_.store(token, std::memory_order_release);
            return IO::successStatus();
        }
        catch (const std::bad_alloc &)
        {
            return error(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return error(ErrorCode::Unknown);
        }
    }

    bool ProgressDialog::isOpen() const noexcept
    {
        return ownedByCurrentThread() && state_ && state_->platform && !state_->nativeDestroyedPendingFinalize;
    }

    bool ProgressDialog::ownedByCurrentThread() const noexcept
    {
        const std::uint64_t token = ownerThreadToken_.load(std::memory_order_acquire);
        return token != 0 && token == currentThreadToken();
    }

    IO::Types::Status ProgressDialog::setTitle(std::string_view title) noexcept
    {
        IO::Types::Status status = requireLiveState(state_.get(), ownedByCurrentThread());
        if (!status.ok())
        {
            return status;
        }
        status = validateText(title);
        return status.ok() ? Detail::Platform::setProgressTitle(*state_, title) : status;
    }

    IO::Types::Status ProgressDialog::setHeading(std::string_view heading) noexcept
    {
        IO::Types::Status status = requireLiveState(state_.get(), ownedByCurrentThread());
        if (!status.ok())
        {
            return status;
        }
        status = validateText(heading);
        return status.ok() ? Detail::Platform::setProgressHeading(*state_, heading) : status;
    }

    IO::Types::Status ProgressDialog::setMessage(std::string_view message) noexcept
    {
        IO::Types::Status status = requireLiveState(state_.get(), ownedByCurrentThread());
        if (!status.ok())
        {
            return status;
        }
        status = validateText(message);
        return status.ok() ? Detail::Platform::setProgressMessage(*state_, message) : status;
    }

    IO::Types::Status ProgressDialog::setMode(Types::Dialogs::Progress::Mode mode) noexcept
    {
        IO::Types::Status status = requireLiveState(state_.get(), ownedByCurrentThread());
        if (!status.ok())
        {
            return status;
        }
        if (!validMode(mode))
        {
            return error(ErrorCode::InvalidArgument);
        }
        status = Detail::Platform::setProgressMode(*state_, mode);
        if (status.ok())
        {
            state_->mode = mode;
        }
        return status;
    }

    IO::Types::Status ProgressDialog::setProgress(double progress) noexcept
    {
        IO::Types::Status status = requireLiveState(state_.get(), ownedByCurrentThread());
        if (!status.ok())
        {
            return status;
        }
        if (!std::isfinite(progress) || progress < 0.0 || progress > 1.0)
        {
            return error(ErrorCode::InvalidArgument);
        }
        status = Detail::Platform::setProgressValue(*state_, progress);
        if (status.ok())
        {
            state_->progress = progress;
        }
        return status;
    }

    bool ProgressDialog::hasCancelRequest() const noexcept
    {
        return ownedByCurrentThread() && state_ && state_->cancelRequested;
    }

    void ProgressDialog::clearCancelRequest() noexcept
    {
        if (ownedByCurrentThread() && state_)
        {
            state_->cancelRequested = false;
        }
    }

    IO::Types::Status ProgressDialog::close() noexcept
    {
        if (!state_)
        {
            return IO::successStatus();
        }
        if (!ownedByCurrentThread())
        {
            return error(ErrorCode::ResourceBusy);
        }
        if (!state_->platform)
        {
            state_.reset();
            ownerThreadToken_.store(0, std::memory_order_release);
            return IO::successStatus();
        }

        const Detail::Platform::ProgressCloseResult result = Detail::Platform::closeProgress(*state_);
        if (result.resourceClosed)
        {
            state_.reset();
            ownerThreadToken_.store(0, std::memory_order_release);
        }
        return result.status;
    }
} // namespace GameWIP::Desktop
