/// @file renderer_feedback.cpp
/// @brief Owner-thread renderer feedback state transitions.

#include "window/renderer.h"

#include "window/internal/window_state.h"
#include "window/internal/window_test_hooks.h"

#include <algorithm>
#include <limits>
#include <new>

namespace GameWIP::Window::Renderer
{
    namespace
    {
        [[nodiscard]] IO::Types::Status requireOwner(Window &window, Detail::WindowState *&state) noexcept
        {
            state = Detail::WindowAccess::state(window);
            if (state == nullptr || !window.isOpen())
                return IO::makeStatus(IO::Types::ErrorCode::NotOpen);
            if (!window.isOwnedByCurrentThread())
                return IO::makeStatus(IO::Types::ErrorCode::ResourceBusy);
            return IO::successStatus();
        }
    } // namespace

    IO::Types::Status attachOcclusionProvider(Window &window) noexcept
    {
        Detail::WindowState *state = nullptr;
        IO::Types::Status status = requireOwner(window, state);
        if (!status.ok())
            return status;
        if (state->occlusionProviderAttached)
            return IO::makeStatus(IO::Types::ErrorCode::AlreadyOpen);
        state->occlusionProviderAttached = true;
        state->occluded = false;
        return IO::successStatus();
    }

    IO::Types::Status reportOcclusion(Window &window, bool occluded) noexcept
    {
        Detail::WindowState *state = nullptr;
        IO::Types::Status status = requireOwner(window, state);
        if (!status.ok())
            return status;
        if (!state->occlusionProviderAttached)
            return IO::makeStatus(IO::Types::ErrorCode::NotOpen);
        if (state->occluded == occluded)
            return IO::successStatus();

        state->occluded = occluded;
        static_cast<void>(Detail::enqueueEvent(*state, Types::OcclusionChangedEvent{occluded}));
        return IO::successStatus();
    }

    IO::Types::Status detachOcclusionProvider(Window &window) noexcept
    {
        Detail::WindowState *state = nullptr;
        IO::Types::Status status = requireOwner(window, state);
        if (!status.ok())
            return status;
        if (!state->occlusionProviderAttached)
            return IO::successStatus();

        state->occlusionProviderAttached = false;
        if (state->occluded)
        {
            state->occluded = false;
            static_cast<void>(Detail::enqueueEvent(*state, Types::OcclusionChangedEvent{false}));
        }
        return IO::successStatus();
    }

    std::size_t requiredPointerHitMaskWords(Types::PixelSize size) noexcept
    {
        constexpr std::size_t bitsPerWord = 64;
        const std::size_t width = size.width;
        const std::size_t height = size.height;
        if (width == 0 || height == 0 || height > std::numeric_limits<std::size_t>::max() / width)
            return 0;
        const std::size_t pixels = width * height;
        if (pixels > std::numeric_limits<std::size_t>::max() - (bitsPerWord - 1))
            return 0;
        return (pixels + bitsPerWord - 1) / bitsPerWord;
    }

    IO::Types::Status publishPointerHitMask(
        Window &window,
        Types::PixelSize size,
        std::uint64_t revision,
        std::span<const std::uint64_t> words) noexcept
    {
        Detail::WindowState *state = nullptr;
        IO::Types::Status status = requireOwner(window, state);
        if (!status.ok())
            return status;
        const std::size_t required = requiredPointerHitMaskWords(size);
        if (required == 0 || required != words.size() || size != state->framebufferSize || revision == 0)
            return IO::makeStatus(IO::Types::ErrorCode::InvalidArgument);
        const std::size_t pixelCount = static_cast<std::size_t>(size.width) * size.height;
        const unsigned int trailingBitCount = static_cast<unsigned int>(pixelCount % 64U);
        if (trailingBitCount != 0)
        {
            const std::uint64_t validBits = (std::uint64_t{1} << trailingBitCount) - 1U;
            if ((words.back() & ~validBits) != 0)
                return IO::makeStatus(IO::Types::ErrorCode::InvalidArgument);
        }
        if (revision <= state->pointerHitMaskRevision)
            return IO::makeStatus(IO::Types::ErrorCode::ResourceBusy);
        if (state->pointerHitMask.size() == required)
        {
            std::copy(words.begin(), words.end(), state->pointerHitMask.begin());
            state->pointerHitMaskRevision = revision;
            state->pointerHitMaskSize = size;
            return IO::successStatus();
        }
        try
        {
            if (Detail::consumeFailure(TestHooks::FailurePoint::Allocation))
                return IO::makeStatus(IO::Types::ErrorCode::OutOfMemory);
            std::vector<std::uint64_t> replacement(words.begin(), words.end());
            state->pointerHitMask.swap(replacement);
            state->pointerHitMaskRevision = revision;
            state->pointerHitMaskSize = size;
            return IO::successStatus();
        }
        catch (const std::bad_alloc &)
        {
            return IO::makeStatus(IO::Types::ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return IO::makeStatus(IO::Types::ErrorCode::Unknown);
        }
    }

    IO::Types::Status clearPointerHitMask(Window &window) noexcept
    {
        Detail::WindowState *state = nullptr;
        IO::Types::Status status = requireOwner(window, state);
        if (!status.ok())
            return status;
        state->pointerHitMask.clear();
        state->pointerHitMaskSize = {};
        state->pointerHitMaskRevision = 0;
        return IO::successStatus();
    }
} // namespace GameWIP::Window::Renderer
