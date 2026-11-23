/// @file shell.cpp
/// @brief Shared shell text, image, and queue ownership helpers.

#include "desktop/internal/shell_helpers.h"

#include "base/checked_arithmetic.h"

#include <cmath>
#include <mutex>
#include <new>
#include <thread>
#include <utility>

namespace GameWIP::Desktop::Internal
{
    /// ------------------------------------------------------------
    /// Shared shell validation and ownership helpers
    /// ------------------------------------------------------------

    namespace
    {
        using IO::Types::ErrorCode;

        /// @brief Builds a portable status from a single error code.
        [[nodiscard]] IO::Types::Status error(ErrorCode code) noexcept
        {
            return IO::makeStatus(code);
        }

        /// @brief Computes checked RGBA8 storage size for a shell image.
        [[nodiscard]] bool imageByteCount(Types::PixelSize size, std::size_t &count) noexcept
        {
            constexpr std::size_t bytesPerPixel = 4;
            const std::size_t width = static_cast<std::size_t>(size.width);
            const std::size_t height = static_cast<std::size_t>(size.height);

            if(width == 0 || height == 0 || GameWIP::Base::wouldMultiplyOverflow(width, height))
            {
                return false;
            }

            const std::size_t pixelCount = width * height;
            if(GameWIP::Base::wouldMultiplyOverflow(pixelCount, bytesPerPixel))
            {
                return false;
            }

            count = pixelCount * bytesPerPixel;
            return true;
        }
    } // namespace

    /// @brief Validates shell text and optionally requires a non-empty value.
    [[nodiscard]] bool validText(std::string_view text, bool required) noexcept
    {
        return (!required || !text.empty()) && !text.contains('\0');
    }

    /// @brief Validates optional progress state before native shell publication.
    [[nodiscard]] IO::Types::Status validateProgress(const std::optional<Types::Shell::Progress> &progress) noexcept
    {
        if(!progress.has_value())
        {
            return IO::successStatus();
        }

        if(!std::isfinite(progress->fraction))
        {
            return error(ErrorCode::InvalidArgument);
        }

        if(progress->state != Types::Shell::ProgressState::Indeterminate && (progress->fraction < 0.0 || progress->fraction > 1.0))
        {
            return error(ErrorCode::InvalidArgument);
        }

        return IO::successStatus();
    }

    /// @brief Validates and deep-copies a call-scoped icon view into retained state.
    [[nodiscard]] IO::Types::Status copyIcon(Types::IconImageView view, OwnedIconImage &out) noexcept
    {
        std::size_t byteCount = 0;

        if(!imageByteCount(view.size, byteCount) || view.rgba8.size() != byteCount)
        {
            return error(ErrorCode::InvalidArgument);
        }

        try
        {
            out.size = view.size;
            out.rgba8.assign(view.rgba8.begin(), view.rgba8.end());
            return IO::successStatus();
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

    // Queue ownership

    /// @brief Binds one shell resource to an owner-thread queue and increments its bind count.
    [[nodiscard]] IO::Types::Status bindQueue(ShellEventQueue &queue, std::thread::id owner, ShellEventQueueState *&outQueue) noexcept
    {
        ShellEventQueueState *candidate = ShellEventQueueAccess::state(queue);

        if(candidate == nullptr)
        {
            return error(ErrorCode::NotOpen);
        }
        std::lock_guard lock(candidate->mutex);

        if(!candidate->open)
        {
            return error(ErrorCode::NotOpen);
        }

        if(candidate->ownerThread != owner || owner != std::this_thread::get_id())
        {
            return error(ErrorCode::ResourceBusy);
        }
        ++candidate->boundResourceCount;
        outQueue = candidate;
        return IO::successStatus();
    }

    /// @brief Checks native liveness before enforcing shell-resource owner-thread access.
    [[nodiscard]] IO::Types::Status requireOwnedShellResource(bool live, bool owned) noexcept
    {
        if(!live)
        {
            return error(ErrorCode::NotOpen);
        }

        return owned ? IO::successStatus() : error(ErrorCode::ResourceBusy);
    }

    /// @brief Drops one resource binding and clears its non-owning queue pointer.
    void unbindQueue(ShellEventQueueState *&queue) noexcept
    {
        if(queue == nullptr)
        {
            return;
        }
        std::lock_guard lock(queue->mutex);

        if(queue->boundResourceCount != 0)
        {
            --queue->boundResourceCount;
        }

        queue = nullptr;
    }
} // namespace GameWIP::Desktop::Internal
