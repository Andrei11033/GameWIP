/// @file desktop_renderer_bridge.cpp
/// @brief Verifies the installed focused Window renderer bridge header in isolation.

#include "desktop/renderer_bridge.h"

#include <type_traits>

static_assert(std::is_same_v<GameWIP::Desktop::Types::Renderer::PointerHitMaskWord, std::uint32_t>);
