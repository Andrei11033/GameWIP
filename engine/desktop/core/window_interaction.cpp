/// @file window_interaction.cpp
/// @brief Window presentation, interaction, and cursor operations.

#include "desktop/window.h"
#include "desktop/internal/window_state.h"
#include "desktop/internal/helpers/window_validation.h"
#include "desktop/internal/desktop_test_hooks.h"
#include "desktop/platform/window.h"

#include <cmath>
#include <new>
#include <utility>
#include <vector>

namespace GameWIP::Desktop
{
    namespace
    {
        using Internal::Helpers::error;
        using Internal::Helpers::requireState;
        using Internal::Helpers::validEnum;
        using Internal::Helpers::validModeRequest;
        using Internal::Helpers::validRect;
        using ErrorCode = GameWIP::Status::Types::Code;
    } // namespace

    /// ------------------------------------------------------------
    /// Presentation and interaction
    /// ------------------------------------------------------------

    GameWIP::Status::Types::Result<void> Window::show() noexcept
    {
        GameWIP::Status::Types::Status status = requireState(state_.get());
        return status.ok() ? Platform::show(*state_) : status;
    }

    GameWIP::Status::Types::Result<void> Window::hide() noexcept
    {
        GameWIP::Status::Types::Status status = requireState(state_.get());
        return status.ok() ? Platform::hide(*state_) : status;
    }

    GameWIP::Status::Types::Result<void> Window::requestFocus() noexcept
    {
        GameWIP::Status::Types::Status status = requireState(state_.get());
        return status.ok() ? Platform::requestFocus(*state_) : status;
    }

    GameWIP::Status::Types::Result<void> Window::requestAttention() noexcept
    {
        GameWIP::Status::Types::Status status = requireState(state_.get());
        return status.ok() ? Platform::requestAttention(*state_) : status;
    }

    GameWIP::Status::Types::Result<void> Window::minimize() noexcept
    {
        GameWIP::Status::Types::Status status = requireState(state_.get());
        return status.ok() ? Platform::minimize(*state_) : status;
    }

    GameWIP::Status::Types::Result<void> Window::maximize() noexcept
    {
        GameWIP::Status::Types::Status status = requireState(state_.get());
        return status.ok() ? Platform::maximize(*state_) : status;
    }

    GameWIP::Status::Types::Result<void> Window::restore() noexcept
    {
        GameWIP::Status::Types::Status status = requireState(state_.get());
        return status.ok() ? Platform::restore(*state_) : status;
    }

    GameWIP::Status::Types::Result<void> Window::setWindowMode(const Types::WindowModeRequest &request) noexcept
    {
        GameWIP::Status::Types::Status status = requireState(state_.get());

        if(!status.ok())
        {
            return status;
        }

        if(!validModeRequest(request))
        {
            return error(ErrorCode::InvalidArgument);
        }
        return Platform::setWindowMode(*state_, request);
    }

    GameWIP::Status::Types::Result<void> Window::setResizable(bool resizable) noexcept
    {
        GameWIP::Status::Types::Status status = requireState(state_.get());

        if(!status.ok())
        {
            return status;
        }

        if(!resizable && state_->controls.maximizable)
        {
            return error(ErrorCode::InvalidArgument);
        }
        return Platform::setResizable(*state_, resizable);
    }

    GameWIP::Status::Types::Result<void> Window::setDecorationMode(Types::DecorationMode mode) noexcept
    {
        GameWIP::Status::Types::Status status = requireState(state_.get());

        if(!status.ok())
        {
            return status;
        }

        if(!validEnum(mode))
        {
            return error(ErrorCode::InvalidArgument);
        }
        return Platform::setDecorationMode(*state_, mode);
    }

    GameWIP::Status::Types::Result<void> Window::setControls(const Types::Controls &controls) noexcept
    {
        GameWIP::Status::Types::Status status = requireState(state_.get());

        if(!status.ok())
        {
            return status;
        }

        if(controls.maximizable && !state_->resizable)
        {
            return error(ErrorCode::InvalidArgument);
        }
        return Platform::setControls(*state_, controls);
    }

    GameWIP::Status::Types::Result<void> Window::setFocusable(bool focusable) noexcept
    {
        GameWIP::Status::Types::Status status = requireState(state_.get());
        return status.ok() ? Platform::setFocusable(*state_, focusable) : status;
    }

    GameWIP::Status::Types::Result<void> Window::setUserInteractionEnabled(bool enabled) noexcept
    {
        GameWIP::Status::Types::Status status = requireState(state_.get());
        return status.ok() ? Platform::setUserInteractionEnabled(*state_, enabled) : status;
    }

    GameWIP::Status::Types::Result<void> Window::setAlwaysOnTop(bool alwaysOnTop) noexcept
    {
        GameWIP::Status::Types::Status status = requireState(state_.get());
        return status.ok() ? Platform::setAlwaysOnTop(*state_, alwaysOnTop) : status;
    }

    GameWIP::Status::Types::Result<void> Window::setOpacity(float opacity) noexcept
    {
        GameWIP::Status::Types::Status status = requireState(state_.get());

        if(!status.ok())
        {
            return status;
        }

        if(!std::isfinite(opacity) || opacity < 0.0F || opacity > 1.0F)
        {
            return error(ErrorCode::InvalidArgument);
        }
        return Platform::setOpacity(*state_, opacity);
    }

    GameWIP::Status::Types::Result<void> Window::setBackdropEffect(Types::BackdropEffect effect) noexcept
    {
        GameWIP::Status::Types::Status status = requireState(state_.get());

        if(!status.ok())
        {
            return status;
        }

        if(!validEnum(effect))
        {
            return error(ErrorCode::InvalidArgument);
        }
        return Platform::setBackdropEffect(*state_, effect);
    }

    GameWIP::Status::Types::Result<void> Window::setFileDropEnabled(bool enabled) noexcept
    {
        GameWIP::Status::Types::Status status = requireState(state_.get());
        return status.ok() ? Platform::setFileDropEnabled(*state_, enabled) : status;
    }

    GameWIP::Status::Types::Result<void> Window::setCustomChromeLayout(const Types::CustomChromeLayout &layout) noexcept
    {
        GameWIP::Status::Types::Status status = requireState(state_.get());

        if(!status.ok())
        {
            return status;
        }

        // Validate region count against the current platform limit.
        const std::size_t regionCount = layout.draggableRegions.size() + static_cast<std::size_t>(layout.systemMenuRegion.has_value()) +
                                        static_cast<std::size_t>(layout.minimizeButtonRegion.has_value()) +
                                        static_cast<std::size_t>(layout.maximizeButtonRegion.has_value()) +
                                        static_cast<std::size_t>(layout.closeButtonRegion.has_value());
        const auto capabilities = getCapabilities();

        if(!capabilities.ok() || regionCount > capabilities.value().maximumCustomChromeRegions)
        {
            return capabilities.ok() ? error(ErrorCode::InvalidArgument) : capabilities.status();
        }

        // Validate every region before copying or mutating cached state.
        for(const Types::LogicalRect &rect : layout.draggableRegions)
        {
            if(!validRect(rect))
            {
                return error(ErrorCode::InvalidArgument);
            }
        }

        if((layout.systemMenuRegion && !validRect(*layout.systemMenuRegion)) ||
           (layout.minimizeButtonRegion && !validRect(*layout.minimizeButtonRegion)) ||
           (layout.maximizeButtonRegion && !validRect(*layout.maximizeButtonRegion)) ||
           (layout.closeButtonRegion && !validRect(*layout.closeButtonRegion)))
        {
            return error(ErrorCode::InvalidArgument);
        }

        try
        {
            if(Internal::consumeFailure(Internal::TestHooks::FailurePoint::RegionCopy))
            {
                return error(ErrorCode::OutOfMemory);
            }

            // Preserve the current layout before applying the replacement.
            std::vector<Types::LogicalRect> copied(layout.draggableRegions.begin(), layout.draggableRegions.end());
            std::vector<Types::LogicalRect> previous = std::move(state_->draggableRegions);
            const auto previousSystem = state_->systemMenuRegion;
            const auto previousMinimize = state_->minimizeButtonRegion;
            const auto previousMaximize = state_->maximizeButtonRegion;
            const auto previousClose = state_->closeButtonRegion;

            state_->draggableRegions = std::move(copied);
            state_->systemMenuRegion = layout.systemMenuRegion;
            state_->minimizeButtonRegion = layout.minimizeButtonRegion;
            state_->maximizeButtonRegion = layout.maximizeButtonRegion;
            state_->closeButtonRegion = layout.closeButtonRegion;
            status = Platform::setCustomChromeLayout(*state_);

            // Restore cached state if the native update fails.
            if(!status.ok())
            {
                state_->draggableRegions = std::move(previous);
                state_->systemMenuRegion = previousSystem;
                state_->minimizeButtonRegion = previousMinimize;
                state_->maximizeButtonRegion = previousMaximize;
                state_->closeButtonRegion = previousClose;
            }

            return status;
        }
        catch(const std::bad_alloc &)
        {
            return error(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return error(ErrorCode::Unknown);
        }
    }

    GameWIP::Status::Types::Result<void> Window::clearCustomChromeLayout() noexcept
    {
        return setCustomChromeLayout({});
    }

    GameWIP::Status::Types::Result<void> Window::setPointerInputLayout(const Types::PointerInputLayout &layout) noexcept
    {
        GameWIP::Status::Types::Status status = requireState(state_.get());

        if(!status.ok())
        {
            return status;
        }

        // Validate the mode and whether its region list is required.
        if(!validEnum(layout.mode))
        {
            return error(ErrorCode::InvalidArgument);
        }
        const bool regionMode = layout.mode == Types::PointerInputMode::AcceptRegions || layout.mode == Types::PointerInputMode::IgnoreRegions;

        if(regionMode == layout.regions.empty())
        {
            return error(ErrorCode::InvalidArgument);
        }

        // Check platform support and region limits before copying.
        const auto capabilities = getCapabilities();

        if(!capabilities.ok())
        {
            return capabilities.status();
        }

        if(layout.mode == Types::PointerInputMode::ClickThrough && !capabilities.value().supports(Types::Capability::PointerClickThrough))
        {
            return error(ErrorCode::Unsupported);
        }

        if(regionMode && !capabilities.value().supports(Types::Capability::PointerRegions))
        {
            return error(ErrorCode::Unsupported);
        }

        if(layout.mode == Types::PointerInputMode::HitMask && !capabilities.value().supports(Types::Capability::PointerHitMask))
        {
            return error(ErrorCode::Unsupported);
        }

        if(layout.regions.size() > capabilities.value().maximumPointerInputRegions)
        {
            return error(ErrorCode::InvalidArgument);
        }

        for(const Types::LogicalRect &rect : layout.regions)
        {
            if(!validRect(rect))
            {
                return error(ErrorCode::InvalidArgument);
            }
        }

        try
        {
            if(Internal::consumeFailure(Internal::TestHooks::FailurePoint::RegionCopy))
            {
                return error(ErrorCode::OutOfMemory);
            }

            // Apply the new regions, then restore and republish the old layout on failure.
            std::vector<Types::LogicalRect> copied(layout.regions.begin(), layout.regions.end());
            std::vector<Types::LogicalRect> previous = std::move(state_->pointerInputRegions);
            const Types::PointerInputMode previousMode = state_->pointerInputMode;

            state_->pointerInputRegions = std::move(copied);
            state_->pointerInputMode = layout.mode;
            status = Platform::setPointerInputLayout(*state_);

            if(!status.ok())
            {
                state_->pointerInputRegions = std::move(previous);
                state_->pointerInputMode = previousMode;
                GameWIP::Status::Types::Status rollback = Platform::setPointerInputLayout(*state_);
                return rollback.ok() ? std::move(status) : std::move(rollback);
            }
            return status;
        }
        catch(const std::bad_alloc &)
        {
            return error(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return error(ErrorCode::Unknown);
        }
    }

    /// ------------------------------------------------------------
    /// Cursor controls
    /// ------------------------------------------------------------

    GameWIP::Status::Types::Result<void> Window::setCursorMode(Types::CursorMode mode) noexcept
    {
        GameWIP::Status::Types::Status status = requireState(state_.get());

        if(!status.ok())
        {
            return status;
        }

        if(!validEnum(mode))
        {
            return error(ErrorCode::InvalidArgument);
        }
        return Platform::setCursorMode(*state_, mode);
    }

    GameWIP::Status::Types::Result<void> Window::setCursorShape(Types::CursorShape shape) noexcept
    {
        GameWIP::Status::Types::Status status = requireState(state_.get());

        if(!status.ok())
        {
            return status;
        }

        if(!validEnum(shape))
        {
            return error(ErrorCode::InvalidArgument);
        }
        return Platform::setCursorShape(*state_, shape);
    }

    GameWIP::Status::Types::Result<void> Window::setCursorPosition(Types::LogicalPosition position) noexcept
    {
        GameWIP::Status::Types::Status status = requireState(state_.get());
        return status.ok() ? Platform::setCursorPosition(*state_, position) : status;
    }

    GameWIP::Status::Types::Result<Types::LogicalPosition> Window::cursorPosition() const noexcept
    {
        GameWIP::Status::Types::Status status = requireState(state_.get());
        return status.ok() ? Platform::cursorPosition(*state_) : GameWIP::Status::Types::Result<Types::LogicalPosition>::failure(status);
    }
} // namespace GameWIP::Desktop
