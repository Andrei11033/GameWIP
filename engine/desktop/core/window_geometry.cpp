/// @file window_geometry.cpp
/// @brief Window title, icon, and geometry operations.

#include "desktop/window.h"
#include "desktop/internal/window_state.h"
#include "desktop/internal/helpers/window_validation.h"
#include "desktop/platform/window.h"

#include "base/checked_arithmetic.h"

#include <cstddef>

namespace GameWIP::Desktop
{
    namespace
    {
        using Internal::Helpers::error;
        using Internal::Helpers::requireState;
        using Internal::Helpers::sizeWithin;
        using Internal::Helpers::validEnum;
        using Internal::Helpers::validLimits;
        using Internal::Helpers::validPixelSize;
        using Internal::Helpers::validRatio;
        using Internal::Helpers::validSize;
        using IO::Types::ErrorCode;
    } // namespace

    /// ------------------------------------------------------------
    /// Geometry and content mutations
    /// ------------------------------------------------------------

    IO::Types::Status Window::setTitle(std::string_view utf8Title) noexcept
    {
        IO::Types::Status status = requireState(state_.get());

        if(!status.ok())
        {
            return status;
        }

        if(utf8Title.contains('\0'))
        {
            return error(ErrorCode::InvalidArgument);
        }
        return Platform::setTitle(*state_, utf8Title);
    }

    IO::Types::Status Window::setIcon(std::span<const Types::IconImageView> images) noexcept
    {
        IO::Types::Status status = requireState(state_.get());

        if(!status.ok())
        {
            return status;
        }

        if(images.empty())
        {
            return error(ErrorCode::InvalidArgument);
        }

        for(const Types::IconImageView &image : images)
        {
            if(!validPixelSize(image.size))
            {
                return error(ErrorCode::InvalidArgument);
            }
            constexpr std::size_t channels = 4;
            const std::size_t width = static_cast<std::size_t>(image.size.width);
            const std::size_t height = static_cast<std::size_t>(image.size.height);

            if(width != image.size.width || height != image.size.height || GameWIP::Base::wouldMultiplyOverflow(width, height))
            {
                return error(ErrorCode::InvalidArgument);
            }
            const std::size_t pixels = width * height;

            if(GameWIP::Base::wouldMultiplyOverflow(pixels, channels))
            {
                return error(ErrorCode::InvalidArgument);
            }

            if(image.rgba8.size() != pixels * channels)
            {
                return error(ErrorCode::InvalidArgument);
            }
        }

        return Platform::setIcon(*state_, images);
    }

    IO::Types::Status Window::clearIcon() noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        return status.ok() ? Platform::clearIcon(*state_) : status;
    }

    IO::Types::Status Window::setClientSize(Types::LogicalSize size) noexcept
    {
        IO::Types::Status status = requireState(state_.get());

        if(!status.ok())
        {
            return status;
        }

        if(state_->mode != Types::WindowMode::Windowed)
        {
            return error(ErrorCode::ResourceBusy);
        }

        if(!validSize(size) || !sizeWithin(size, state_->sizeLimits))
        {
            return error(ErrorCode::InvalidArgument);
        }
        return Platform::setClientSize(*state_, size);
    }

    IO::Types::Status Window::setClientPosition(Types::ScreenPosition position) noexcept
    {
        IO::Types::Status status = requireState(state_.get());

        if(!status.ok())
        {
            return status;
        }

        return state_->mode == Types::WindowMode::Windowed ? Platform::setClientPosition(*state_, position) : error(ErrorCode::ResourceBusy);
    }

    IO::Types::Status Window::setClientRect(Types::ScreenPosition position, Types::LogicalSize size) noexcept
    {
        IO::Types::Status status = requireState(state_.get());

        if(!status.ok())
        {
            return status;
        }

        if(state_->mode != Types::WindowMode::Windowed)
        {
            return error(ErrorCode::ResourceBusy);
        }

        if(!validSize(size) || !sizeWithin(size, state_->sizeLimits))
        {
            return error(ErrorCode::InvalidArgument);
        }
        return Platform::setClientRect(*state_, position, size);
    }

    IO::Types::Status Window::centerOn(std::optional<Types::Display::MonitorId> monitor) noexcept
    {
        IO::Types::Status status = requireState(state_.get());

        if(!status.ok())
        {
            return status;
        }

        return state_->mode == Types::WindowMode::Windowed ? Platform::centerOn(*state_, monitor.value_or(Types::Display::MonitorId::invalid()))
                                                           : error(ErrorCode::ResourceBusy);
    }

    IO::Types::Status Window::setSizeLimits(const Types::SizeLimits &limits) noexcept
    {
        IO::Types::Status status = requireState(state_.get());

        if(!status.ok())
        {
            return status;
        }

        if(state_->mode != Types::WindowMode::Windowed)
        {
            return error(ErrorCode::ResourceBusy);
        }

        if(!validLimits(limits))
        {
            return error(ErrorCode::InvalidArgument);
        }
        return Platform::setSizeLimits(*state_, limits);
    }

    IO::Types::Status Window::setAspectRatio(std::optional<Types::AspectRatio> ratio) noexcept
    {
        IO::Types::Status status = requireState(state_.get());

        if(!status.ok())
        {
            return status;
        }

        if(state_->mode != Types::WindowMode::Windowed)
        {
            return error(ErrorCode::ResourceBusy);
        }

        if(!validRatio(ratio))
        {
            return error(ErrorCode::InvalidArgument);
        }
        return Platform::setAspectRatio(*state_, ratio);
    }

    Types::Result<Types::ScreenPosition> Window::clientToScreen(Types::LogicalPosition position) const noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        return status.ok() ? Platform::clientToScreen(*state_, position) : Types::Result<Types::ScreenPosition>{status, std::nullopt};
    }

    Types::Result<Types::LogicalPosition> Window::screenToClient(Types::ScreenPosition position) const noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        return status.ok() ? Platform::screenToClient(*state_, position) : Types::Result<Types::LogicalPosition>{status, std::nullopt};
    }

    IO::Types::Status Window::setDpiResizePolicy(Types::DpiResizePolicy policy) noexcept
    {
        IO::Types::Status status = requireState(state_.get());

        if(!status.ok())
        {
            return status;
        }

        if(!validEnum(policy))
        {
            return error(ErrorCode::InvalidArgument);
        }
        state_->dpiResizePolicy = policy;
        return IO::successStatus();
    }

} // namespace GameWIP::Desktop
