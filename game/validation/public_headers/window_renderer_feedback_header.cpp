/// @file window_renderer_feedback_header.cpp
/// @brief Renderer-feedback public-header self-containment compile check.

#include "window/integration/renderer_feedback.h"

#include <utility>

static_assert(noexcept(GameWIP::Window::Integration::Renderer::reportOcclusion(std::declval<GameWIP::Window::Window &>(), false)));
