/// @file desktop_renderer_header.cpp
/// @brief Verifies the source-tree focused Window renderer bridge header.

#include "desktop/renderer_bridge.h"

#include <type_traits>

static_assert(std::is_same_v<GameWIP::Desktop::Types::Renderer::PointerHitMaskWord, std::uint32_t>);
static_assert(noexcept(GameWIP::Desktop::Renderer::requiredPointerHitMaskWords({})));
static_assert(noexcept(GameWIP::Desktop::Renderer::hasOcclusionProvider(std::declval<const GameWIP::Desktop::Window &>())));
