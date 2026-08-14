/// @file renderer_bridge.cpp
/// @brief Owner-thread renderer bridge state transitions.

#include "window/renderer_bridge.h"

#include "window/internal/window_platform.h"
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
        if (!window.supports(Types::Capability::OcclusionReporting))
            return IO::makeStatus(IO::Types::ErrorCode::Unsupported);
        if (state->occlusionProviderAttached)
            return IO::makeStatus(IO::Types::ErrorCode::AlreadyOpen);
        state->occlusionProviderAttached = true;
        state->occluded = false;
        return IO::successStatus();
    }

    bool hasOcclusionProvider(const Window &window) noexcept
    {
        const Detail::WindowState *state = Detail::WindowAccess::state(window);
        return state != nullptr && window.isOpen() && window.isOwnedByCurrentThread() && state->occlusionProviderAttached;
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
        static_cast<void>(Detail::enqueueEvent(*state, Types::Events::OcclusionChanged{occluded}));
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
            static_cast<void>(Detail::enqueueEvent(*state, Types::Events::OcclusionChanged{false}));
        }
        return IO::successStatus();
    }

    std::size_t requiredPointerHitMaskWords(Types::PixelSize size) noexcept
    {
        constexpr std::size_t bitsPerWord = std::numeric_limits<Types::Renderer::PointerHitMaskWord>::digits;
        const std::size_t width = size.width;
        const std::size_t height = size.height;
        if (width == 0 || height == 0)
            return 0;
        const std::size_t wordsPerRow = width / bitsPerWord + (width % bitsPerWord != 0 ? 1U : 0U);
        if (height > std::numeric_limits<std::size_t>::max() / wordsPerRow)
            return 0;
        return wordsPerRow * height;
    }

    Types::Renderer::PointerHitMaskResult beginPointerHitMaskUpdate(Window &window) noexcept
    {
        Detail::WindowState *state = nullptr;
        IO::Types::Status status = requireOwner(window, state);
        if (!status.ok())
            return {.status = status};
        if (!window.supports(Types::Capability::PointerHitMask) && !state->pointerHitMaskBackendSupportedForTesting)
            return {.status = IO::makeStatus(IO::Types::ErrorCode::Unsupported)};
        if (state->nativeDestroyedPendingFinalize)
            return {.status = IO::makeStatus(IO::Types::ErrorCode::ResourceBusy)};
        if (state->pointerHitMaskGeneration == nullptr || state->pointerHitMaskGenerationExhausted == nullptr ||
            *state->pointerHitMaskGenerationExhausted || *state->pointerHitMaskGeneration == std::numeric_limits<std::uint64_t>::max())
        {
            if (state->pointerHitMaskGenerationExhausted != nullptr)
                *state->pointerHitMaskGenerationExhausted = true;
            state->pointerHitMaskTargetGeneration = 0;
            return {.status = IO::makeStatus(IO::Types::ErrorCode::ResourceBusy)};
        }

        const std::size_t required = requiredPointerHitMaskWords(state->framebufferSize);
        if (required == 0)
            return {.status = IO::makeStatus(IO::Types::ErrorCode::InvalidArgument)};

        const std::uint64_t generation = ++*state->pointerHitMaskGeneration;
        state->pointerHitMaskTargetGeneration = generation;
        state->pointerHitMaskTargetSize = state->framebufferSize;
        state->pointerHitMaskTargetWordCount = required;
        return {
            .status = IO::successStatus(),
            .target = {.generation = generation, .framebufferSize = state->framebufferSize, .requiredWordCount = required}};
    }

    IO::Types::Status publishPointerHitMask(
        Window &window,
        std::uint64_t generation,
        std::span<const Types::Renderer::PointerHitMaskWord> words) noexcept
    {
        Detail::WindowState *state = nullptr;
        IO::Types::Status status = requireOwner(window, state);
        if (!status.ok())
            return status;
        if (generation == 0 || generation != state->pointerHitMaskTargetGeneration)
            return IO::makeStatus(IO::Types::ErrorCode::Interrupted);
        const Types::PixelSize size = state->pointerHitMaskTargetSize;
        const std::size_t required = state->pointerHitMaskTargetWordCount;
        if (required == 0 || required != words.size() || size != state->framebufferSize)
            return IO::makeStatus(IO::Types::ErrorCode::InvalidArgument);

        constexpr std::size_t bitsPerWord = std::numeric_limits<Types::Renderer::PointerHitMaskWord>::digits;
        const std::size_t wordsPerRow =
            static_cast<std::size_t>(size.width) / bitsPerWord + (size.width % bitsPerWord != 0 ? 1U : 0U);
        const unsigned int validBitsInLastWord = static_cast<unsigned int>(size.width % bitsPerWord);
        if (validBitsInLastWord != 0)
        {
            const Types::Renderer::PointerHitMaskWord validBits =
                (Types::Renderer::PointerHitMaskWord{1} << validBitsInLastWord) - Types::Renderer::PointerHitMaskWord{1};
            for (std::size_t row = 0; row < size.height; ++row)
            {
                const std::size_t finalWord = row * wordsPerRow + wordsPerRow - 1U;
                if ((words[finalWord] & ~validBits) != 0)
                    return IO::makeStatus(IO::Types::ErrorCode::InvalidArgument);
            }
        }

        if (state->pointerHitMask.size() == required)
        {
            std::copy(words.begin(), words.end(), state->pointerHitMask.begin());
            state->pointerHitMaskActiveGeneration = generation;
            state->pointerHitMaskSize = size;
            state->pointerHitMaskTargetGeneration = 0;
            state->pointerHitMaskTargetSize = {};
            state->pointerHitMaskTargetWordCount = 0;
            return IO::successStatus();
        }

        try
        {
            if (Detail::consumeFailure(TestHooks::FailurePoint::Allocation))
                return IO::makeStatus(IO::Types::ErrorCode::OutOfMemory);
            std::vector<Types::Renderer::PointerHitMaskWord> replacement(words.begin(), words.end());
            state->pointerHitMask.swap(replacement);
            state->pointerHitMaskActiveGeneration = generation;
            state->pointerHitMaskSize = size;
            state->pointerHitMaskTargetGeneration = 0;
            state->pointerHitMaskTargetSize = {};
            state->pointerHitMaskTargetWordCount = 0;
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
        Detail::invalidatePointerHitMask(*state);
        return IO::successStatus();
    }

    bool hasPointerHitMask(const Window &window) noexcept
    {
        const Detail::WindowState *state = Detail::WindowAccess::state(window);
        return state != nullptr && window.isOpen() && window.isOwnedByCurrentThread() && !state->pointerHitMask.empty() &&
               state->pointerHitMaskActiveGeneration != 0 && state->pointerHitMaskSize == state->framebufferSize;
    }
} // namespace GameWIP::Window::Renderer
