/// @file renderer_integration_state.h
/// @brief Private state allocated only after optional renderer integration is used.

#pragma once

#include "desktop/renderer_bridge.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace GameWIP::Desktop::Internal
{
    /// @brief Optional renderer-facing state shared by hit-mask and occlusion bridges.
    struct RendererIntegrationState
    {
        // Pointer hit-mask publication
        std::vector<Types::Renderer::PointerHitMaskWord> pointerHitMask;
        Types::PixelSize pointerHitMaskSize;
        Types::PixelSize pointerHitMaskTargetSize;
        std::size_t pointerHitMaskTargetWordCount = 0;
        std::uint64_t pointerHitMaskGeneration = 0;
        std::uint64_t pointerHitMaskActiveGeneration = 0;
        std::uint64_t pointerHitMaskTargetGeneration = 0;
        bool pointerHitMaskGenerationExhausted = false;
        bool pointerHitMaskBackendSupportedForTesting = false;

        // Renderer occlusion feedback
        bool occlusionProviderAttached = false;
        bool occluded = false;

        /// @brief Publishes a completed mask and clears its reserved target metadata.
        void commitPointerHitMask(std::uint64_t generation, Types::PixelSize size) noexcept
        {
            pointerHitMaskActiveGeneration = generation;
            pointerHitMaskSize = size;
            pointerHitMaskTargetGeneration = 0;
            pointerHitMaskTargetSize = {};
            pointerHitMaskTargetWordCount = 0;
        }

        /// @brief Clears the active mask and advances its generation for stale-publish rejection.
        void invalidatePointerHitMask() noexcept
        {
            clearPointerHitMask();

            if(pointerHitMaskGeneration == std::numeric_limits<std::uint64_t>::max())
            {
                pointerHitMaskGenerationExhausted = true;
            }
            else
            {
                ++pointerHitMaskGeneration;
            }
        }

        /// @brief Clears optional renderer state at the end of one Window lifetime.
        void finishWindowLifetime() noexcept
        {
            clearPointerHitMask();
            pointerHitMaskBackendSupportedForTesting = false;
            occlusionProviderAttached = false;
            occluded = false;
        }

    private:
        void clearPointerHitMask() noexcept
        {
            pointerHitMask.clear();
            pointerHitMaskSize = {};
            pointerHitMaskActiveGeneration = 0;
            pointerHitMaskTargetGeneration = 0;
            pointerHitMaskTargetSize = {};
            pointerHitMaskTargetWordCount = 0;
        }
    };
} // namespace GameWIP::Desktop::Internal
