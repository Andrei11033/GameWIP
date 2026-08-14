/// @file renderer_integration_state.h
/// @brief Private state allocated only after optional renderer integration is used.

#pragma once

#include "window/renderer_bridge.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace GameWIP::Window::Detail
{
    struct RendererIntegrationState
    {
        std::vector<Types::Renderer::PointerHitMaskWord> pointerHitMask;
        Types::PixelSize pointerHitMaskSize;
        Types::PixelSize pointerHitMaskTargetSize;
        std::size_t pointerHitMaskTargetWordCount = 0;
        std::uint64_t pointerHitMaskGeneration = 0;
        std::uint64_t pointerHitMaskActiveGeneration = 0;
        std::uint64_t pointerHitMaskTargetGeneration = 0;
        bool pointerHitMaskGenerationExhausted = false;
        bool pointerHitMaskBackendSupportedForTesting = false;
        bool occlusionProviderAttached = false;
        bool occluded = false;

        void invalidatePointerHitMask() noexcept
        {
            pointerHitMask.clear();
            pointerHitMaskSize = {};
            pointerHitMaskActiveGeneration = 0;
            pointerHitMaskTargetGeneration = 0;
            pointerHitMaskTargetSize = {};
            pointerHitMaskTargetWordCount = 0;
            if (pointerHitMaskGeneration == std::numeric_limits<std::uint64_t>::max())
                pointerHitMaskGenerationExhausted = true;
            else
                ++pointerHitMaskGeneration;
        }

        void finishWindowLifetime() noexcept
        {
            pointerHitMask.clear();
            pointerHitMaskSize = {};
            pointerHitMaskActiveGeneration = 0;
            pointerHitMaskTargetGeneration = 0;
            pointerHitMaskTargetSize = {};
            pointerHitMaskTargetWordCount = 0;
            pointerHitMaskBackendSupportedForTesting = false;
            occlusionProviderAttached = false;
            occluded = false;
        }
    };
} // namespace GameWIP::Window::Detail
