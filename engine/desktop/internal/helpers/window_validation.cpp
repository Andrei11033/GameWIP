/// @file window_validation.cpp
/// @brief Shared private Window validation and ownership checks.

#include "desktop/internal/helpers/window_validation.h"
#include "desktop/internal/window_state.h"
#include "desktop/platform/window.h"

#include <cstdint>
#include <limits>
#include <type_traits>
#include <variant>

namespace GameWIP::Desktop::Internal::Helpers
{
    namespace
    {
        template <typename Size> [[nodiscard]] bool validExtent(Size size) noexcept
        {
            constexpr auto nativeMaximum = static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max());
            return size.width != 0 && size.height != 0 && size.width <= nativeMaximum && size.height <= nativeMaximum;
        }
    } // namespace

    /// ------------------------------------------------------------
    /// Window request validation
    /// ------------------------------------------------------------

    IO::Types::Status error(IO::Types::ErrorCode code) noexcept
    {
        return IO::makeStatus(code);
    }

    bool validSize(Types::LogicalSize size) noexcept
    {
        return validExtent(size);
    }

    bool validPixelSize(Types::PixelSize size) noexcept
    {
        return validExtent(size);
    }

    bool validRect(const Types::LogicalRect &rect) noexcept
    {
        return validSize(rect.size);
    }

    bool validLimits(const Types::SizeLimits &limits) noexcept
    {
        if((limits.minimum && !validSize(*limits.minimum)) || (limits.maximum && !validSize(*limits.maximum)))
        {
            return false;
        }
        return !limits.minimum || !limits.maximum ||
               (limits.minimum->width <= limits.maximum->width && limits.minimum->height <= limits.maximum->height);
    }

    bool sizeWithin(Types::LogicalSize size, const Types::SizeLimits &limits) noexcept
    {
        return (!limits.minimum || (size.width >= limits.minimum->width && size.height >= limits.minimum->height)) &&
               (!limits.maximum || (size.width <= limits.maximum->width && size.height <= limits.maximum->height));
    }

    bool validRatio(const std::optional<Types::AspectRatio> &ratio) noexcept
    {
        return !ratio || (ratio->numerator != 0 && ratio->denominator != 0);
    }

    bool validEnum(Types::PresentationState value) noexcept
    {
        return value == Types::PresentationState::Normal || value == Types::PresentationState::Minimized ||
               value == Types::PresentationState::Maximized;
    }

    bool validEnum(Types::DecorationMode value) noexcept
    {
        return value == Types::DecorationMode::System || value == Types::DecorationMode::Borderless || value == Types::DecorationMode::Custom;
    }

    bool validEnum(Types::PlacementKind value) noexcept
    {
        return value == Types::PlacementKind::PlatformDefault || value == Types::PlacementKind::Centered || value == Types::PlacementKind::Explicit;
    }

    bool validEnum(Types::CursorMode value) noexcept
    {
        return value == Types::CursorMode::Normal || value == Types::CursorMode::Hidden || value == Types::CursorMode::Confined ||
               value == Types::CursorMode::HiddenConfined || value == Types::CursorMode::Relative;
    }

    bool validEnum(Types::CursorShape value) noexcept
    {
        return static_cast<unsigned int>(value) <= static_cast<unsigned int>(Types::CursorShape::NotAllowed);
    }

    bool validEnum(Types::PointerInputMode value) noexcept
    {
        return value == Types::PointerInputMode::Normal || value == Types::PointerInputMode::ClickThrough ||
               value == Types::PointerInputMode::AcceptRegions || value == Types::PointerInputMode::IgnoreRegions ||
               value == Types::PointerInputMode::HitMask;
    }

    bool validEnum(Types::BackdropEffect value) noexcept
    {
        return value == Types::BackdropEffect::None || value == Types::BackdropEffect::Automatic || value == Types::BackdropEffect::MainWindow ||
               value == Types::BackdropEffect::TransientWindow || value == Types::BackdropEffect::TabbedWindow;
    }

    bool validEnum(Types::DpiResizePolicy value) noexcept
    {
        return value == Types::DpiResizePolicy::PreserveLogicalClientSize || value == Types::DpiResizePolicy::PreservePhysicalClientSize;
    }

    bool validDisplayMode(const Types::Display::Mode &mode) noexcept
    {
        return validPixelSize(mode.resolution) && mode.refreshRateMillihertz != 0 && mode.bitsPerPixel != 0;
    }

    bool validModeRequest(const Types::WindowModeRequest &request) noexcept
    {
        return std::visit(
            [](const auto &value) noexcept
            {
                using Request = std::decay_t<decltype(value)>;
                if constexpr(std::is_same_v<Request, Types::WindowedModeRequest>)
                {
                    return true;
                }
                else if constexpr(std::is_same_v<Request, Types::BorderlessFullscreenRequest>)
                {
                    return !value.targetMonitor || value.targetMonitor->isValid();
                }
                else
                {
                    return (!value.targetMonitor || value.targetMonitor->isValid()) && (!value.displayMode || validDisplayMode(*value.displayMode));
                }
            },
            request);
    }

    /// ------------------------------------------------------------
    /// Window access validation
    /// ------------------------------------------------------------

    IO::Types::Status requireState(WindowState *state) noexcept
    {
        if(state == nullptr || !state->platform || !Platform::hasLiveNativeWindow(*state))
        {
            return error(IO::Types::ErrorCode::NotOpen);
        }

        if(!Platform::ownedByCurrentThread(*state))
        {
            return error(IO::Types::ErrorCode::ResourceBusy);
        }

        return IO::successStatus();
    }

} // namespace GameWIP::Desktop::Internal::Helpers
