/// @file progress_dialog.cpp
/// @brief Portable ProgressDialog validation and owner-thread lifecycle.

#include "desktop/internal/results.h"
#include "desktop/dialogs.h"

#include "desktop/internal/desktop_test_hooks.h"
#include "desktop/internal/window_state.h"
#include "desktop/platform/dialogs.h"
#include "desktop/platform/window.h"

#include <atomic>
#include <cmath>
#include <new>
#include <utility>

namespace GameWIP::Desktop
{
    namespace
    {
        using ErrorCode = GameWIP::Status::Types::Code;

        std::atomic_uint64_t nextThreadToken{1};
        thread_local std::uint64_t callingThreadToken = 0;

        /// @brief Returns a stable nonzero identity for the calling thread.
        [[nodiscard]] std::uint64_t currentThreadToken() noexcept
        {
            if(callingThreadToken == 0)
            {
                callingThreadToken = nextThreadToken.fetch_add(1, std::memory_order_relaxed);

                if(callingThreadToken == 0)
                {
                    callingThreadToken = nextThreadToken.fetch_add(1, std::memory_order_relaxed);
                }
            }

            return callingThreadToken;
        }

        /// @brief Builds a portable status from a single error code.
        [[nodiscard]] GameWIP::Status::Types::Status error(ErrorCode code) noexcept
        {
            return GameWIP::Desktop::Internal::statusFor(code);
        }

        /// @brief Rejects embedded NUL characters before text reaches a native dialog API.
        [[nodiscard]] GameWIP::Status::Types::Status validateText(std::string_view text) noexcept
        {
            return text.contains('\0') ? error(ErrorCode::InvalidArgument) : GameWIP::Status::Types::Status{};
        }

        /// @brief Validates the determinate and indeterminate progress modes.
        [[nodiscard]] bool validMode(Types::Dialogs::Progress::Mode mode) noexcept
        {
            return mode == Types::Dialogs::Progress::Mode::Determinate || mode == Types::Dialogs::Progress::Mode::Indeterminate;
        }

        /// @brief Validates state presence, owner-thread access, and native liveness.
        [[nodiscard]] GameWIP::Status::Types::Status requireLiveState(const Internal::ProgressDialogState *state, bool owned) noexcept
        {
            if(state == nullptr)
            {
                return error(ErrorCode::NotOpen);
            }

            if(!owned)
            {
                return error(ErrorCode::ResourceBusy);
            }

            return state->nativeDestroyedPendingFinalize || !state->platform ? error(ErrorCode::NotOpen) : GameWIP::Status::Types::Status{};
        }
    } // namespace

    /// ------------------------------------------------------------
    /// Lifecycle and owner-thread state
    /// ------------------------------------------------------------

    ProgressDialog::ProgressDialog() noexcept = default;

    ProgressDialog::~ProgressDialog() noexcept
    {
        if(!state_)
        {
            return;
        }

        if(state_->platform && !ownedByCurrentThread())
        {
            if(Platform::deferProgressCleanupToOwner(state_))
            {
                ownerThreadToken_.store(0, std::memory_order_release);
                return;
            }
            static_cast<void>(Platform::closeProgressBestEffort(*state_));
        }
        else
        {
            if(!Platform::closeProgressBestEffort(*state_) && Platform::deferProgressCleanupToOwner(state_))
            {
                ownerThreadToken_.store(0, std::memory_order_release);
                return;
            }
            state_.reset();
        }

        ownerThreadToken_.store(0, std::memory_order_release);
    }

    GameWIP::Status::Types::Result<void> ProgressDialog::open(const Types::Dialogs::Progress::Description &description) noexcept
    {
        if(state_)
        {
            return error(ErrorCode::AlreadyOpen);
        }

        // Validate progress values and visible text before native setup.
        if(!validMode(description.presentationMode) || !std::isfinite(description.progress) || description.progress < 0.0 ||
           description.progress > 1.0)
        {
            return error(ErrorCode::InvalidArgument);
        }

        for(std::string_view text : {description.title, description.heading, description.message})
        {
            GameWIP::Status::Types::Status status = validateText(text);
            if(!status.ok())
            {
                return status;
            }
        }

        // Validate the optional owner Window and its calling thread.
        Internal::WindowState *ownerState = nullptr;

        if(description.owner != nullptr)
        {
            ownerState = Internal::WindowAccess::state(*description.owner);

            if(ownerState == nullptr || !Platform::hasLiveNativeWindow(*ownerState))
            {
                return error(ErrorCode::NotOpen);
            }

            if(!Platform::ownedByCurrentThread(*ownerState))
            {
                return error(ErrorCode::ResourceBusy);
            }
        }

        try
        {
            // Open a private candidate and publish it only after native setup succeeds.
            if(Internal::consumeFailure(Internal::TestHooks::FailurePoint::Allocation))
            {
                return error(ErrorCode::OutOfMemory);
            }
            auto candidate = std::make_unique<Internal::ProgressDialogState>();
            candidate->ownerToken = currentThreadToken();
            candidate->ownerId = ownerState != nullptr ? ownerState->id : Types::WindowId{};
            candidate->mode = description.presentationMode;
            candidate->progress = description.progress;
            candidate->cancelable = description.cancelable;
            candidate->blocksOwner = description.blocksOwner;
            candidate->cancelRequested = false;

            GameWIP::Status::Types::Status status = Platform::openProgress(*candidate, description);

            if(!status.ok())
            {
                if(!Platform::closeProgressBestEffort(*candidate))
                {
                    // The candidate remains private to this failed open. Its
                    // owner-thread dispatcher retains any cleanup that could
                    // not complete synchronously.
                    static_cast<void>(Platform::deferProgressCleanupToOwner(candidate));
                }

                return status;
            }
            const std::uint64_t token = candidate->ownerToken;
            state_ = std::move(candidate);
            ownerThreadToken_.store(token, std::memory_order_release);
            return GameWIP::Status::Types::Status{};
        }
        catch(const std::bad_alloc &)
        {
            return error(ErrorCode::OutOfMemory);
        }
        catch(...)
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

    /// ------------------------------------------------------------
    /// Native mutations and cancellation
    /// ------------------------------------------------------------

    GameWIP::Status::Types::Result<void> ProgressDialog::setTitle(std::string_view title) noexcept
    {
        GameWIP::Status::Types::Status status = requireLiveState(state_.get(), ownedByCurrentThread());

        if(!status.ok())
        {
            return status;
        }

        status = validateText(title);
        return status.ok() ? Platform::setProgressTitle(*state_, title) : status;
    }

    GameWIP::Status::Types::Result<void> ProgressDialog::setHeading(std::string_view heading) noexcept
    {
        GameWIP::Status::Types::Status status = requireLiveState(state_.get(), ownedByCurrentThread());

        if(!status.ok())
        {
            return status;
        }

        status = validateText(heading);
        return status.ok() ? Platform::setProgressHeading(*state_, heading) : status;
    }

    GameWIP::Status::Types::Result<void> ProgressDialog::setMessage(std::string_view message) noexcept
    {
        GameWIP::Status::Types::Status status = requireLiveState(state_.get(), ownedByCurrentThread());

        if(!status.ok())
        {
            return status;
        }

        status = validateText(message);
        return status.ok() ? Platform::setProgressMessage(*state_, message) : status;
    }

    GameWIP::Status::Types::Result<void> ProgressDialog::setMode(Types::Dialogs::Progress::Mode mode) noexcept
    {
        GameWIP::Status::Types::Status status = requireLiveState(state_.get(), ownedByCurrentThread());

        if(!status.ok())
        {
            return status;
        }

        if(!validMode(mode))
        {
            return error(ErrorCode::InvalidArgument);
        }

        status = Platform::setProgressMode(*state_, mode);

        if(status.ok())
        {
            state_->mode = mode;
        }

        return status;
    }

    GameWIP::Status::Types::Result<void> ProgressDialog::setProgress(double progress) noexcept
    {
        GameWIP::Status::Types::Status status = requireLiveState(state_.get(), ownedByCurrentThread());

        if(!status.ok())
        {
            return status;
        }

        if(!std::isfinite(progress) || progress < 0.0 || progress > 1.0)
        {
            return error(ErrorCode::InvalidArgument);
        }

        status = Platform::setProgressValue(*state_, progress);

        if(status.ok())
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
        if(ownedByCurrentThread() && state_)
        {
            state_->cancelRequested = false;
        }
    }

    GameWIP::Status::Types::Result<void> ProgressDialog::close() noexcept
    {
        if(!state_)
        {
            return GameWIP::Status::Types::Status{};
        }

        if(!ownedByCurrentThread())
        {
            return error(ErrorCode::ResourceBusy);
        }

        if(!state_->platform)
        {
            state_.reset();
            ownerThreadToken_.store(0, std::memory_order_release);
            return GameWIP::Status::Types::Status{};
        }

        const Platform::ProgressCloseResult result = Platform::closeProgress(*state_);

        if(result.hasValue() && result.value())
        {
            state_.reset();
            ownerThreadToken_.store(0, std::memory_order_release);
        }

        return Internal::result(result.status());
    }
} // namespace GameWIP::Desktop
