/// @file window_renderer_header.cpp
/// @brief Renderer-integration public-header self-containment compile check.

#include "window/renderer.h"

#include <utility>

static_assert(noexcept(GameWIP::Window::Renderer::reportOcclusion(std::declval<GameWIP::Window::Window &>(), false)));
static_assert(noexcept(GameWIP::Window::Renderer::beginPointerHitMaskUpdate(std::declval<GameWIP::Window::Window &>())));
static_assert(noexcept(GameWIP::Window::Renderer::hasPointerHitMask(std::declval<const GameWIP::Window::Window &>())));
static_assert(noexcept(GameWIP::Window::Renderer::getDisplayColorInfo({})));
static_assert(noexcept(GameWIP::Window::Renderer::getWindowDisplayColorInfo(std::declval<const GameWIP::Window::Window &>())));
