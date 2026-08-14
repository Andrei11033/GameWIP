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

        [[nodiscard]] Detail::RendererIntegrationState *ensureIntegration(Window &window) noexcept
        {
            try
            {
                if (Detail::consumeFailure(TestHooks::FailurePoint::Allocation))
                    return nullptr;
                return Detail::WindowAccess::ensureRendererIntegration(window);
            }
            catch (...)
            {
                return nullptr;
            }
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
        Detail::RendererIntegrationState *renderer = Detail::WindowAccess::rendererIntegration(window);
        if (renderer != nullptr && renderer->occlusionProviderAttached)
            return IO::makeStatus(IO::Types::ErrorCode::AlreadyOpen);
        if (renderer == nullptr)
            renderer = ensureIntegration(window);
        if (renderer == nullptr)
            return IO::makeStatus(IO::Types::ErrorCode::OutOfMemory);
        renderer->occlusionProviderAttached = true;
        renderer->occluded = false;
        return IO::successStatus();
    }

    bool hasOcclusionProvider(const Window &window) noexcept
    {
        const Detail::RendererIntegrationState *renderer = Detail::WindowAccess::rendererIntegration(window);
        return window.isOpen() && window.isOwnedByCurrentThread() && renderer != nullptr && renderer->occlusionProviderAttached;
    }

    IO::Types::Status reportOcclusion(Window &window, bool occluded) noexcept
    {
        Detail::WindowState *state = nullptr;
        IO::Types::Status status = requireOwner(window, state);
        if (!status.ok())
            return status;
        Detail::RendererIntegrationState *renderer = Detail::WindowAccess::rendererIntegration(window);
        if (renderer == nullptr || !renderer->occlusionProviderAttached)
            return IO::makeStatus(IO::Types::ErrorCode::NotOpen);
        if (renderer->occluded == occluded)
            return IO::successStatus();

        renderer->occluded = occluded;
        static_cast<void>(Detail::enqueueEvent(*state, Types::Events::OcclusionChanged{occluded}));
        return IO::successStatus();
    }

    IO::Types::Status detachOcclusionProvider(Window &window) noexcept
    {
        Detail::WindowState *state = nullptr;
        IO::Types::Status status = requireOwner(window, state);
        if (!status.ok())
            return status;
        Detail::RendererIntegrationState *renderer = Detail::WindowAccess::rendererIntegration(window);
        if (renderer == nullptr || !renderer->occlusionProviderAttached)
            return IO::successStatus();

        renderer->occlusionProviderAttached = false;
        if (renderer->occluded)
        {
            renderer->occluded = false;
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
        Detail::RendererIntegrationState *renderer = Detail::WindowAccess::rendererIntegration(window);
        const bool enabledForTesting = renderer != nullptr && renderer->pointerHitMaskBackendSupportedForTesting;
        if (!window.supports(Types::Capability::PointerHitMask) && !enabledForTesting)
            return {.status = IO::makeStatus(IO::Types::ErrorCode::Unsupported)};
        if (state->nativeDestroyedPendingFinalize)
            return {.status = IO::makeStatus(IO::Types::ErrorCode::ResourceBusy)};
        if (renderer == nullptr)
            renderer = ensureIntegration(window);
        if (renderer == nullptr)
            return {.status = IO::makeStatus(IO::Types::ErrorCode::OutOfMemory)};
        if (renderer->pointerHitMaskGenerationExhausted || renderer->pointerHitMaskGeneration == std::numeric_limits<std::uint64_t>::max())
        {
            renderer->pointerHitMaskGenerationExhausted = true;
            renderer->pointerHitMaskTargetGeneration = 0;
            return {.status = IO::makeStatus(IO::Types::ErrorCode::ResourceBusy)};
        }

        const std::size_t required = requiredPointerHitMaskWords(state->framebufferSize);
        if (required == 0)
            return {.status = IO::makeStatus(IO::Types::ErrorCode::InvalidArgument)};

        const std::uint64_t generation = ++renderer->pointerHitMaskGeneration;
        renderer->pointerHitMaskTargetGeneration = generation;
        renderer->pointerHitMaskTargetSize = state->framebufferSize;
        renderer->pointerHitMaskTargetWordCount = required;
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
        Detail::RendererIntegrationState *renderer = Detail::WindowAccess::rendererIntegration(window);
        if (renderer == nullptr || generation == 0 || generation != renderer->pointerHitMaskTargetGeneration)
            return IO::makeStatus(IO::Types::ErrorCode::Interrupted);
        const Types::PixelSize size = renderer->pointerHitMaskTargetSize;
        const std::size_t required = renderer->pointerHitMaskTargetWordCount;
        if (required == 0 || required != words.size() || size != state->framebufferSize)
            return IO::makeStatus(IO::Types::ErrorCode::InvalidArgument);

        constexpr std::size_t bitsPerWord = std::numeric_limits<Types::Renderer::PointerHitMaskWord>::digits;
        const std::size_t wordsPerRow = static_cast<std::size_t>(size.width) / bitsPerWord + (size.width % bitsPerWord != 0 ? 1U : 0U);
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

        if (renderer->pointerHitMask.size() == required)
        {
            std::copy(words.begin(), words.end(), renderer->pointerHitMask.begin());
            renderer->pointerHitMaskActiveGeneration = generation;
            renderer->pointerHitMaskSize = size;
            renderer->pointerHitMaskTargetGeneration = 0;
            renderer->pointerHitMaskTargetSize = {};
            renderer->pointerHitMaskTargetWordCount = 0;
            return IO::successStatus();
        }

        try
        {
            if (Detail::consumeFailure(TestHooks::FailurePoint::Allocation))
                return IO::makeStatus(IO::Types::ErrorCode::OutOfMemory);
            std::vector<Types::Renderer::PointerHitMaskWord> replacement(words.begin(), words.end());
            renderer->pointerHitMask.swap(replacement);
            renderer->pointerHitMaskActiveGeneration = generation;
            renderer->pointerHitMaskSize = size;
            renderer->pointerHitMaskTargetGeneration = 0;
            renderer->pointerHitMaskTargetSize = {};
            renderer->pointerHitMaskTargetWordCount = 0;
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
        const Detail::RendererIntegrationState *renderer = Detail::WindowAccess::rendererIntegration(window);
        return state != nullptr && window.isOpen() && window.isOwnedByCurrentThread() && renderer != nullptr && !renderer->pointerHitMask.empty() &&
               renderer->pointerHitMaskActiveGeneration != 0 && renderer->pointerHitMaskSize == state->framebufferSize;
    }
} // namespace GameWIP::Window::Renderer
