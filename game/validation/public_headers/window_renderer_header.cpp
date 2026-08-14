#include "window/renderer_bridge.h"

#include <type_traits>

static_assert(std::is_same_v<GameWIP::Window::Types::Renderer::PointerHitMaskWord, std::uint32_t>);
static_assert(noexcept(GameWIP::Window::Renderer::requiredPointerHitMaskWords({})));
static_assert(noexcept(GameWIP::Window::Renderer::hasOcclusionProvider(std::declval<const GameWIP::Window::Window &>())));
