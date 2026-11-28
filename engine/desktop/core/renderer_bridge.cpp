/// @file renderer_bridge.cpp
/// @brief Owner-thread renderer bridge state transitions.

#include "desktop/internal/results.h"
#include "desktop/renderer_bridge.h"

#include "base/checked_arithmetic.h"

#include "desktop/internal/desktop_test_hooks.h"
#include "desktop/internal/window_state.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <new>

namespace GameWIP::Desktop::Renderer
{
    namespace
    {
        /// @brief Validates that a renderer bridge operation runs on a live Window owner thread.
        [[nodiscard]] GameWIP::Status::Types::Status requireOwner(Window &window, Internal::WindowState *&state) noexcept
        {
            state = Internal::WindowAccess::state(window);

            if(state == nullptr || !window.isOpen())
            {
                return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::NotOpen);
            }

            if(!window.ownedByCurrentThread())
            {
                return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::ResourceBusy);
            }

            return GameWIP::Status::Types::Status{};
        }

        /// @brief Lazily creates optional renderer state while converting allocation failures to null.
        [[nodiscard]] Internal::RendererIntegrationState *ensureIntegration(Window &window) noexcept
        {
            try
            {
                if(Internal::consumeFailure(Internal::TestHooks::FailurePoint::Allocation))
                {
                    return nullptr;
                }
                return Internal::WindowAccess::ensureRendererIntegration(window);
            }
            catch(...)
            {
                return nullptr;
            }
        }
    } // namespace

    /// ------------------------------------------------------------
    /// Concurrent presentation reads
    /// ------------------------------------------------------------

    GameWIP::Status::Types::Result<void> enableConcurrentPresentationReads(Window &window) noexcept
    {
        Internal::WindowState *state = nullptr;
        GameWIP::Status::Types::Status status = requireOwner(window, state);

        if(!status.ok())
        {
            return status;
        }

        if(Internal::WindowAccess::presentationPublication(window) != nullptr)
        {
            return GameWIP::Status::Types::Status{};
        }

        if(Internal::consumeFailure(Internal::TestHooks::FailurePoint::Allocation))
        {
            return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::OutOfMemory);
        }

        try
        {
            // Snapshot owner-thread values before exposing the concurrent read view.
            auto publication = std::make_unique<Internal::PresentationPublicationState>();
            Internal::publishCachedPresentationState(*publication, *state);
            const Internal::RendererIntegrationState *renderer = Internal::WindowAccess::rendererIntegration(window);
            publication->publishOccluded(renderer != nullptr && renderer->occlusionProviderAttached && renderer->occluded);
            Internal::WindowAccess::presentationPublicationOwner(window) = std::move(publication);
            Internal::WindowAccess::bindPresentationPublication(window, *state);
            return GameWIP::Status::Types::Status{};
        }
        catch(const std::bad_alloc &)
        {
            return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::OutOfMemory);
        }
        catch(...)
        {
            return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::Unknown);
        }
    }

    bool concurrentPresentationReadsEnabled(const Window &window) noexcept
    {
        return Internal::WindowAccess::presentationPublication(window) != nullptr;
    }

    /// ------------------------------------------------------------
    /// Occlusion reporting
    /// ------------------------------------------------------------

    GameWIP::Status::Types::Result<void> attachOcclusionProvider(Window &window) noexcept
    {
        Internal::WindowState *state = nullptr;
        GameWIP::Status::Types::Status status = requireOwner(window, state);

        if(!status.ok())
        {
            return status;
        }

        if(!window.supports(Types::Capability::OcclusionReporting))
        {
            return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::Unsupported);
        }
        Internal::RendererIntegrationState *renderer = Internal::WindowAccess::rendererIntegration(window);

        if(renderer != nullptr && renderer->occlusionProviderAttached)
        {
            return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::AlreadyOpen);
        }

        if(renderer == nullptr)
        {
            // Allocate the optional bridge only after capability validation.
            renderer = ensureIntegration(window);
        }

        if(renderer == nullptr)
        {
            return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::OutOfMemory);
        }
        renderer->occlusionProviderAttached = true;
        renderer->occluded = false;

        if(Internal::PresentationPublicationState *publication = Internal::WindowAccess::presentationPublication(window))
        {
            publication->publishOccluded(false);
        }

        return GameWIP::Status::Types::Status{};
    }

    bool hasOcclusionProvider(const Window &window) noexcept
    {
        const Internal::RendererIntegrationState *renderer = Internal::WindowAccess::rendererIntegration(window);
        return window.isOpen() && window.ownedByCurrentThread() && renderer != nullptr && renderer->occlusionProviderAttached;
    }

    GameWIP::Status::Types::Result<void> reportOcclusion(Window &window, bool occluded) noexcept
    {
        Internal::WindowState *state = nullptr;
        GameWIP::Status::Types::Status status = requireOwner(window, state);

        if(!status.ok())
        {
            return status;
        }

        Internal::RendererIntegrationState *renderer = Internal::WindowAccess::rendererIntegration(window);

        if(renderer == nullptr || !renderer->occlusionProviderAttached)
        {
            return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::NotOpen);
        }

        if(renderer->occluded == occluded)
        {
            return GameWIP::Status::Types::Status{};
        }
        renderer->occluded = occluded;

        if(Internal::PresentationPublicationState *publication = Internal::WindowAccess::presentationPublication(window))
        {
            publication->publishOccluded(occluded);
        }

        static_cast<void>(Internal::enqueueEvent(*state, Types::Events::OcclusionChanged{occluded}));
        return GameWIP::Status::Types::Status{};
    }

    GameWIP::Status::Types::Result<void> detachOcclusionProvider(Window &window) noexcept
    {
        Internal::WindowState *state = nullptr;
        GameWIP::Status::Types::Status status = requireOwner(window, state);

        if(!status.ok())
        {
            return status;
        }

        Internal::RendererIntegrationState *renderer = Internal::WindowAccess::rendererIntegration(window);

        if(renderer == nullptr || !renderer->occlusionProviderAttached)
        {
            return GameWIP::Status::Types::Status{};
        }
        renderer->occlusionProviderAttached = false;

        if(Internal::PresentationPublicationState *publication = Internal::WindowAccess::presentationPublication(window))
        {
            publication->publishOccluded(false);
        }

        if(renderer->occluded)
        {
            renderer->occluded = false;
            static_cast<void>(Internal::enqueueEvent(*state, Types::Events::OcclusionChanged{false}));
        }

        return GameWIP::Status::Types::Status{};
    }

    /// ------------------------------------------------------------
    /// Pointer hit masks
    /// ------------------------------------------------------------

    /// @brief Returns the packed word count required for one framebuffer-sized hit mask.
    std::size_t requiredPointerHitMaskWords(Types::PixelSize size) noexcept
    {
        constexpr std::size_t bitsPerWord = std::numeric_limits<Types::Renderer::PointerHitMaskWord>::digits;
        const std::size_t width = size.width;
        const std::size_t height = size.height;

        if(width == 0 || height == 0)
        {
            return 0;
        }
        const std::size_t wordsPerRow = width / bitsPerWord + (width % bitsPerWord != 0 ? 1U : 0U);

        if(GameWIP::Base::wouldMultiplyOverflow(height, wordsPerRow))
        {
            return 0;
        }
        return wordsPerRow * height;
    }

    /// @brief Reserves a generation and exact target size for a two-phase hit-mask publication.
    GameWIP::Status::Types::Result<Types::Renderer::PointerHitMaskTarget> beginPointerHitMaskUpdate(Window &window) noexcept
    {
        Internal::WindowState *state = nullptr;
        GameWIP::Status::Types::Status status = requireOwner(window, state);

        if(!status.ok())
        {
            return GameWIP::Status::Types::Result<Types::Renderer::PointerHitMaskTarget>::failure(std::move(status));
        }

        Internal::RendererIntegrationState *renderer = Internal::WindowAccess::rendererIntegration(window);
        const bool enabledForTesting = renderer != nullptr && renderer->pointerHitMaskBackendSupportedForTesting;

        if(!window.supports(Types::Capability::PointerHitMask) && !enabledForTesting)
        {
            return GameWIP::Status::Types::Result<Types::Renderer::PointerHitMaskTarget>::failure(
                GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::Unsupported));
        }

        if(state->nativeDestroyedPendingFinalize)
        {
            return GameWIP::Status::Types::Result<Types::Renderer::PointerHitMaskTarget>::failure(
                GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::ResourceBusy));
        }

        if(renderer == nullptr)
        {
            renderer = ensureIntegration(window);
        }

        if(renderer == nullptr)
        {
            return GameWIP::Status::Types::Result<Types::Renderer::PointerHitMaskTarget>::failure(
                GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::OutOfMemory));
        }

        if(renderer->pointerHitMaskGenerationExhausted || renderer->pointerHitMaskGeneration == std::numeric_limits<std::uint64_t>::max())
        {
            renderer->pointerHitMaskGenerationExhausted = true;
            renderer->pointerHitMaskTargetGeneration = 0;
            return GameWIP::Status::Types::Result<Types::Renderer::PointerHitMaskTarget>::failure(
                GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::ResourceBusy));
        }

        const std::size_t required = requiredPointerHitMaskWords(state->framebufferSize);

        if(required == 0)
        {
            return GameWIP::Status::Types::Result<Types::Renderer::PointerHitMaskTarget>::failure(
                GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::InvalidArgument));
        }

        // Reserve the generation and dimensions for the next publication.
        const std::uint64_t generation = ++renderer->pointerHitMaskGeneration;
        renderer->pointerHitMaskTargetGeneration = generation;
        renderer->pointerHitMaskTargetSize = state->framebufferSize;
        renderer->pointerHitMaskTargetWordCount = required;
        return GameWIP::Status::Types::Result<Types::Renderer::PointerHitMaskTarget>::success(
            Types::Renderer::PointerHitMaskTarget{
                .generation = generation,
                .framebufferSize = state->framebufferSize,
                .requiredWordCount = required});
    }

    /// @brief Validates and publishes the reserved hit-mask generation atomically from the owner thread.
    GameWIP::Status::Types::Result<void> publishPointerHitMask(
        Window &window,
        std::uint64_t generation,
        std::span<const Types::Renderer::PointerHitMaskWord> words) noexcept
    {
        Internal::WindowState *state = nullptr;
        GameWIP::Status::Types::Status status = requireOwner(window, state);

        if(!status.ok())
        {
            return status;
        }

        Internal::RendererIntegrationState *renderer = Internal::WindowAccess::rendererIntegration(window);

        if(renderer == nullptr || generation == 0 || generation != renderer->pointerHitMaskTargetGeneration)
        {
            return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::Interrupted);
        }

        // Verify the reserved generation and target size before reading mask data.
        const Types::PixelSize size = renderer->pointerHitMaskTargetSize;
        const std::size_t required = renderer->pointerHitMaskTargetWordCount;

        if(required == 0 || required != words.size() || size != state->framebufferSize)
        {
            return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::InvalidArgument);
        }

        constexpr std::size_t bitsPerWord = std::numeric_limits<Types::Renderer::PointerHitMaskWord>::digits;
        const std::size_t wordsPerRow = static_cast<std::size_t>(size.width) / bitsPerWord + (size.width % bitsPerWord != 0 ? 1U : 0U);
        const unsigned int validBitsInLastWord = static_cast<unsigned int>(size.width % bitsPerWord);

        // Reject nonzero padding bits at the end of each framebuffer row.
        if(validBitsInLastWord != 0)
        {
            const Types::Renderer::PointerHitMaskWord validBits =
                (Types::Renderer::PointerHitMaskWord{1} << validBitsInLastWord) - Types::Renderer::PointerHitMaskWord{1};

            for(std::size_t row = 0; row < size.height; ++row)
            {
                const std::size_t finalWord = row * wordsPerRow + wordsPerRow - 1U;

                if((words[finalWord] & ~validBits) != 0)
                {
                    return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::InvalidArgument);
                }
            }
        }

        if(renderer->pointerHitMask.size() == required)
        {
            std::copy(words.begin(), words.end(), renderer->pointerHitMask.begin());
            renderer->commitPointerHitMask(generation, size);
            return GameWIP::Status::Types::Status{};
        }

        // Allocate a replacement before discarding the currently published mask.
        try
        {
            if(Internal::consumeFailure(Internal::TestHooks::FailurePoint::Allocation))
            {
                return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::OutOfMemory);
            }
            std::vector<Types::Renderer::PointerHitMaskWord> replacement(words.begin(), words.end());
            renderer->pointerHitMask.swap(replacement);
            renderer->commitPointerHitMask(generation, size);
            return GameWIP::Status::Types::Status{};
        }
        catch(const std::bad_alloc &)
        {
            return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::OutOfMemory);
        }
        catch(...)
        {
            return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::Unknown);
        }
    }

    /// @brief Invalidates the active hit mask without replacing the renderer integration state.
    GameWIP::Status::Types::Result<void> clearPointerHitMask(Window &window) noexcept
    {
        Internal::WindowState *state = nullptr;
        GameWIP::Status::Types::Status status = requireOwner(window, state);

        if(!status.ok())
        {
            return status;
        }

        Internal::invalidatePointerHitMask(*state);
        return GameWIP::Status::Types::Status{};
    }

    bool hasPointerHitMask(const Window &window) noexcept
    {
        const Internal::WindowState *state = Internal::WindowAccess::state(window);
        const Internal::RendererIntegrationState *renderer = Internal::WindowAccess::rendererIntegration(window);
        return state != nullptr && window.isOpen() && window.ownedByCurrentThread() && renderer != nullptr && !renderer->pointerHitMask.empty() &&
               renderer->pointerHitMaskActiveGeneration != 0 && renderer->pointerHitMaskSize == state->framebufferSize;
    }
} // namespace GameWIP::Desktop::Renderer
