/// @file window_state.cpp
/// @brief Cached Window state accessors.

#include "desktop/window.h"
#include "desktop/internal/window_state.h"

#include <optional>
#include <string_view>

namespace GameWIP::Desktop
{
    /// ------------------------------------------------------------
    /// Cached state
    /// ------------------------------------------------------------

    // Window title and geometry

    std::string_view Window::title() const noexcept
    {
        return state_ ? std::string_view(state_->title) : std::string_view{};
    }

    Types::LogicalSize Window::clientSize() const noexcept
    {
        return presentationPublication_ ? presentationPublication_->clientSize() : state_ ? state_->clientSize : Types::LogicalSize{};
    }

    Types::PixelSize Window::framebufferSize() const noexcept
    {
        return presentationPublication_ ? presentationPublication_->framebufferSize() : state_ ? state_->framebufferSize : Types::PixelSize{};
    }

    Types::ScreenPosition Window::clientPosition() const noexcept
    {
        return state_ ? state_->clientPosition : Types::ScreenPosition{};
    }

    Types::ScreenRect Window::frameRect() const noexcept
    {
        return state_ ? state_->frameRect : Types::ScreenRect{};
    }

    Types::Insets Window::frameInsets() const noexcept
    {
        return state_ ? state_->frameInsets : Types::Insets{};
    }

    // Display and window mode

    Types::ContentScale Window::contentScale() const noexcept
    {
        return presentationPublication_ ? presentationPublication_->contentScale() : state_ ? state_->contentScale : Types::ContentScale{};
    }

    Types::Dpi Window::effectiveDpi() const noexcept
    {
        return presentationPublication_ ? presentationPublication_->dpi() : state_ ? state_->dpi : Types::Dpi{};
    }

    Types::DpiResizePolicy Window::dpiResizePolicy() const noexcept
    {
        return state_ ? state_->dpiResizePolicy : Types::DpiResizePolicy::PreserveLogicalClientSize;
    }

    Types::Display::MonitorId Window::currentMonitor() const noexcept
    {
        return presentationPublication_ ? presentationPublication_->monitor() : state_ ? state_->monitor : Types::Display::MonitorId::invalid();
    }

    Types::WindowMode Window::windowMode() const noexcept
    {
        return state_ ? state_->mode : Types::WindowMode::Windowed;
    }

    Types::FullscreenState Window::fullscreenState() const noexcept
    {
        return state_ ? state_->fullscreen : Types::FullscreenState{};
    }

    Types::PresentationState Window::presentationState() const noexcept
    {
        return presentationPublication_ ? presentationPublication_->presentation() : state_ ? state_->presentation : Types::PresentationState::Normal;
    }

    Types::DecorationMode Window::decorationMode() const noexcept
    {
        return state_ ? state_->decoration : Types::DecorationMode::System;
    }

    // Window constraints

    Types::Controls Window::controls() const noexcept
    {
        return state_ ? state_->controls : Types::Controls{};
    }

    Types::SizeLimits Window::sizeLimits() const noexcept
    {
        return state_ ? state_->sizeLimits : Types::SizeLimits{};
    }

    std::optional<Types::AspectRatio> Window::aspectRatio() const noexcept
    {
        return state_ ? state_->aspectRatio : std::nullopt;
    }

    // Cursor and pointer input

    Types::CursorMode Window::cursorMode() const noexcept
    {
        return state_ ? state_->cursorMode : Types::CursorMode::Normal;
    }

    Types::CursorShape Window::cursorShape() const noexcept
    {
        return state_ ? state_->cursorShape : Types::CursorShape::Arrow;
    }

    Types::PointerInputMode Window::pointerInputMode() const noexcept
    {
        return state_ ? state_->pointerInputMode : Types::PointerInputMode::Normal;
    }

    std::size_t Window::pointerInputRegionCount() const noexcept
    {
        return state_ ? state_->pointerInputRegions.size() : 0;
    }

    // Appearance, visibility, and interaction

    Types::BackdropEffect Window::backdropEffect() const noexcept
    {
        return state_ ? state_->backdrop : Types::BackdropEffect::None;
    }

    float Window::opacity() const noexcept
    {
        return state_ ? state_->opacity : 1.0F;
    }

    bool Window::visible() const noexcept
    {
        return presentationPublication_ ? presentationPublication_->visible() : state_ && state_->visible;
    }

    bool Window::focused() const noexcept
    {
        return state_ && state_->focused;
    }

    bool Window::interactiveMoveResizeActive() const noexcept
    {
        return presentationPublication_ ? presentationPublication_->interactiveMoveResizeActive() : state_ && state_->interactiveMoveResizeActive;
    }

    bool Window::minimized() const noexcept
    {
        return presentationState() == Types::PresentationState::Minimized;
    }

    bool Window::maximized() const noexcept
    {
        return presentationState() == Types::PresentationState::Maximized;
    }

    bool Window::occluded() const noexcept
    {
        if(presentationPublication_)
        {
            return presentationPublication_->occluded();
        }

        const Internal::RendererIntegrationState *renderer = Internal::WindowAccess::rendererIntegration(*this);
        return state_ && renderer != nullptr && renderer->occluded;
    }

    bool Window::cursorInside() const noexcept
    {
        return state_ && state_->cursorInside;
    }

    bool Window::resizable() const noexcept
    {
        return state_ && state_->resizable;
    }

    bool Window::focusable() const noexcept
    {
        return state_ && state_->focusable;
    }

    bool Window::userInteractionEnabled() const noexcept
    {
        return state_ && state_->interactionEnabled;
    }

    bool Window::alwaysOnTop() const noexcept
    {
        return state_ && state_->alwaysOnTop;
    }

    bool Window::fileDropEnabled() const noexcept
    {
        return state_ && state_->fileDropEnabled;
    }

    bool Window::transparentFramebuffer() const noexcept
    {
        return state_ && state_->transparentFramebuffer;
    }

} // namespace GameWIP::Desktop
