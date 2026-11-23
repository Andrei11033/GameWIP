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
        using IO::Types::ErrorCode;
    } // namespace

    /// ------------------------------------------------------------
    /// Presentation and interaction
    /// ------------------------------------------------------------

    IO::Types::Status Window::show() noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        return status.ok() ? Platform::show(*state_) : status;
    }

    IO::Types::Status Window::hide() noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        return status.ok() ? Platform::hide(*state_) : status;
    }

    IO::Types::Status Window::requestFocus() noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        return status.ok() ? Platform::requestFocus(*state_) : status;
    }

    IO::Types::Status Window::requestAttention() noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        return status.ok() ? Platform::requestAttention(*state_) : status;
    }

    IO::Types::Status Window::minimize() noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        return status.ok() ? Platform::minimize(*state_) : status;
    }

    IO::Types::Status Window::maximize() noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        return status.ok() ? Platform::maximize(*state_) : status;
    }

    IO::Types::Status Window::restore() noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        return status.ok() ? Platform::restore(*state_) : status;
    }

    IO::Types::Status Window::setWindowMode(const Types::WindowModeRequest &request) noexcept
    {
        IO::Types::Status status = requireState(state_.get());

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

    IO::Types::Status Window::setResizable(bool resizable) noexcept
    {
        IO::Types::Status status = requireState(state_.get());

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

    IO::Types::Status Window::setDecorationMode(Types::DecorationMode mode) noexcept
    {
        IO::Types::Status status = requireState(state_.get());

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

    IO::Types::Status Window::setControls(const Types::Controls &controls) noexcept
    {
        IO::Types::Status status = requireState(state_.get());

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

    IO::Types::Status Window::setFocusable(bool focusable) noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        return status.ok() ? Platform::setFocusable(*state_, focusable) : status;
    }

    IO::Types::Status Window::setUserInteractionEnabled(bool enabled) noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        return status.ok() ? Platform::setUserInteractionEnabled(*state_, enabled) : status;
    }

    IO::Types::Status Window::setAlwaysOnTop(bool alwaysOnTop) noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        return status.ok() ? Platform::setAlwaysOnTop(*state_, alwaysOnTop) : status;
    }

    IO::Types::Status Window::setOpacity(float opacity) noexcept
    {
        IO::Types::Status status = requireState(state_.get());

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

    IO::Types::Status Window::setBackdropEffect(Types::BackdropEffect effect) noexcept
    {
        IO::Types::Status status = requireState(state_.get());

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

    IO::Types::Status Window::setFileDropEnabled(bool enabled) noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        return status.ok() ? Platform::setFileDropEnabled(*state_, enabled) : status;
    }

    IO::Types::Status Window::setCustomChromeLayout(const Types::CustomChromeLayout &layout) noexcept
    {
        IO::Types::Status status = requireState(state_.get());

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

        if(!capabilities.ok() || regionCount > capabilities->maximumCustomChromeRegions)
        {
            return capabilities.ok() ? error(ErrorCode::InvalidArgument) : capabilities.status;
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

    IO::Types::Status Window::clearCustomChromeLayout() noexcept
    {
        return setCustomChromeLayout({});
    }

    IO::Types::Status Window::setPointerInputLayout(const Types::PointerInputLayout &layout) noexcept
    {
        IO::Types::Status status = requireState(state_.get());

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
            return capabilities.status;
        }

        if(layout.mode == Types::PointerInputMode::ClickThrough && !capabilities->supports(Types::Capability::PointerClickThrough))
        {
            return error(ErrorCode::Unsupported);
        }

        if(regionMode && !capabilities->supports(Types::Capability::PointerRegions))
        {
            return error(ErrorCode::Unsupported);
        }

        if(layout.mode == Types::PointerInputMode::HitMask && !capabilities->supports(Types::Capability::PointerHitMask))
        {
            return error(ErrorCode::Unsupported);
        }

        if(layout.regions.size() > capabilities->maximumPointerInputRegions)
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
                IO::Types::Status rollback = Platform::setPointerInputLayout(*state_);
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

    IO::Types::Status Window::setCursorMode(Types::CursorMode mode) noexcept
    {
        IO::Types::Status status = requireState(state_.get());

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

    IO::Types::Status Window::setCursorShape(Types::CursorShape shape) noexcept
    {
        IO::Types::Status status = requireState(state_.get());

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

    IO::Types::Status Window::setCursorPosition(Types::LogicalPosition position) noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        return status.ok() ? Platform::setCursorPosition(*state_, position) : status;
    }

    Types::Result<Types::LogicalPosition> Window::cursorPosition() const noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        return status.ok() ? Platform::cursorPosition(*state_) : Types::Result<Types::LogicalPosition>{status, std::nullopt};
    }
} // namespace GameWIP::Desktop
