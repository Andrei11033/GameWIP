/// @file window.h
/// @brief Portable top-level desktop-window API for GameWIP.

#pragma once

#include "io/io.h"
#include "window/description.h"
#include "window/display.h"
#include "window/events.h"
#include "window/types.h"
#include "window/window_export.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace GameWIP::Window
{
    namespace Detail
    {
        struct WindowState;
        struct WindowAccess;
    } // namespace Detail

    namespace Types
    {
        /// @brief Portable native-resource lifecycle state.
        enum class LifetimeState
        {
            Closed,
            Open,
            NativeDestroyedPendingFinalize
        };

        /// @brief Cached details for a non-windowed mode.
        struct FullscreenInfo
        {
            Display::MonitorId monitor;
            std::optional<Display::Mode> displayMode;
            bool exactDisplayMode = false;
            bool suspended = false;
        };

        /// @brief Call-scoped declarative custom-chrome hit-test layout.
        struct CustomChromeLayout
        {
            std::span<const LogicalRect> draggableRegions;
            std::optional<LogicalRect> systemMenuRegion;
            std::optional<LogicalRect> minimizeButtonRegion;
            std::optional<LogicalRect> maximizeButtonRegion;
            std::optional<LogicalRect> closeButtonRegion;
        };

        /// @brief Call-scoped declarative pointer-region layout.
        struct PointerInputLayout
        {
            PointerInputMode mode = PointerInputMode::Normal;
            std::span<const LogicalRect> regions;
        };

        /// @brief Call-scoped tightly packed RGBA8 icon image.
        struct IconImageView
        {
            PixelSize size;
            std::span<const std::byte> rgba8;
        };

        /// @brief Portable backend capability.
        enum class Capability : std::uint8_t
        {
            MultipleWindows,
            MultipleWindowThreads,
            OwnedWindows,
            RuntimeOwnerChange,
            WindowPositioning,
            ProgrammaticFocus,
            AttentionRequest,
            RuntimeDecorationChange,
            CustomChrome,
            WindowIcon,
            AspectRatioConstraint,
            RuntimeInteractionControl,
            AlwaysOnTop,
            Opacity,
            TransparentFramebuffer,
            SystemBackdrop,
            PointerClickThrough,
            PointerRegions,
            PointerHitMask,
            CursorConfinement,
            RelativeCursor,
            CursorWarping,
            FileDrop,
            ExclusiveFullscreen,
            OcclusionReporting,
            Count
        };

        /// @brief Backend capability flags and declarative-region limits.
        struct Capabilities
        {
            std::uint64_t flags = 0;
            std::uint32_t maximumCustomChromeRegions = 0;
            std::uint32_t maximumPointerInputRegions = 0;

            [[nodiscard]] constexpr bool supports(Capability capability) const noexcept
            {
                const auto index = static_cast<std::uint8_t>(capability);
                return index < static_cast<std::uint8_t>(Capability::Count) && (flags & (std::uint64_t{1} << index)) != 0;
            }
        };

        struct CapabilitiesResult
        {
            IO::Types::Status status;
            Capabilities capabilities;
        };

        struct ScreenPositionResult
        {
            IO::Types::Status status;
            ScreenPosition position;
        };

        struct LogicalPositionResult
        {
            IO::Types::Status status;
            LogicalPosition position;
        };
    } // namespace Types

    /// @brief Returns cached backend/environment capabilities.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::CapabilitiesResult getCapabilities() noexcept;
    /// @brief Returns whether the backend advertises a known capability.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT bool supports(Types::Capability capability) noexcept;

    /// @brief Non-copyable, non-movable RAII owner of one native top-level desktop Window.
    /// @details Except wakeEventWait(), open-object operations require the opening thread. Cached
    /// getters do not query the backend and are not synchronized. The stable object address and
    /// thread affinity last until explicit close or destruction.
    class GAMEWIP_WINDOW_EXPORT Window final
    {
    public:
        Window() noexcept;
        Window(const Window &) = delete;
        Window &operator=(const Window &) = delete;
        Window(Window &&) = delete;
        Window &operator=(Window &&) = delete;
        ~Window() noexcept;

        /// @brief Opens with Events::kDefaultQueueCapacity internal slots.
        [[nodiscard]] IO::Types::Status open(const Types::Description &description = {}) noexcept;
        [[nodiscard]] IO::Types::Status open(const Types::Description &description, std::size_t eventQueueCapacity) noexcept;
        [[nodiscard]] IO::Types::Status open(const Types::Description &description, std::span<Types::Event> eventStorage) noexcept;

        [[nodiscard]] bool isOpen() const noexcept;
        [[nodiscard]] Types::LifetimeState lifetimeState() const noexcept;
        [[nodiscard]] IO::Types::Status close() noexcept;

        [[nodiscard]] Types::WindowId id() const noexcept;
        [[nodiscard]] Types::WindowId ownerId() const noexcept;
        [[nodiscard]] bool isOwnedByCurrentThread() const noexcept;
        /// @brief Returns whether the backend supports a capability for Window objects.
        [[nodiscard]] bool supports(Types::Capability capability) const noexcept;
        [[nodiscard]] IO::Types::Status setOwner(Types::WindowId owner) noexcept;

        [[nodiscard]] bool hasCloseRequest() const noexcept;
        [[nodiscard]] IO::Types::Status requestClose() noexcept;
        [[nodiscard]] IO::Types::Status clearCloseRequest() noexcept;

        [[nodiscard]] bool popEvent(Types::Event &outEvent) noexcept;
        [[nodiscard]] std::size_t popEvents(std::span<Types::Event> destination) noexcept;
        void clearEvents() noexcept;
        [[nodiscard]] Types::Events::QueueInfo eventQueueInfo() const noexcept;
        void clearDroppedEventCount() noexcept;
        [[nodiscard]] IO::Types::Status wakeEventWait() const noexcept;

        [[nodiscard]] std::string_view title() const noexcept;
        [[nodiscard]] Types::LogicalSize clientSize() const noexcept;
        [[nodiscard]] Types::PixelSize framebufferSize() const noexcept;
        [[nodiscard]] Types::ScreenPosition clientPosition() const noexcept;
        [[nodiscard]] Types::ScreenRect frameRect() const noexcept;
        [[nodiscard]] Types::Insets frameInsets() const noexcept;
        [[nodiscard]] Types::ContentScale contentScale() const noexcept;
        [[nodiscard]] Types::Dpi effectiveDpi() const noexcept;
        [[nodiscard]] Types::DpiResizePolicy dpiResizePolicy() const noexcept;
        [[nodiscard]] Types::Display::MonitorId currentMonitor() const noexcept;
        [[nodiscard]] Types::Mode mode() const noexcept;
        [[nodiscard]] Types::FullscreenInfo fullscreenInfo() const noexcept;
        [[nodiscard]] Types::PresentationState presentationState() const noexcept;
        [[nodiscard]] Types::DecorationMode decorationMode() const noexcept;
        [[nodiscard]] Types::Controls controls() const noexcept;
        [[nodiscard]] Types::SizeLimits sizeLimits() const noexcept;
        [[nodiscard]] std::optional<Types::AspectRatio> aspectRatio() const noexcept;
        [[nodiscard]] Types::CursorMode cursorMode() const noexcept;
        [[nodiscard]] Types::CursorShape cursorShape() const noexcept;
        [[nodiscard]] Types::PointerInputMode pointerInputMode() const noexcept;
        [[nodiscard]] std::size_t pointerInputRegionCount() const noexcept;
        [[nodiscard]] Types::BackdropEffect backdropEffect() const noexcept;
        [[nodiscard]] float opacity() const noexcept;
        [[nodiscard]] bool isVisible() const noexcept;
        [[nodiscard]] bool isFocused() const noexcept;
        [[nodiscard]] bool isMinimized() const noexcept;
        [[nodiscard]] bool isMaximized() const noexcept;
        [[nodiscard]] bool isOccluded() const noexcept;
        [[nodiscard]] bool isCursorInside() const noexcept;
        [[nodiscard]] bool isResizable() const noexcept;
        [[nodiscard]] bool isFocusable() const noexcept;
        [[nodiscard]] bool isUserInteractionEnabled() const noexcept;
        [[nodiscard]] bool isAlwaysOnTop() const noexcept;
        [[nodiscard]] bool isFileDropEnabled() const noexcept;
        [[nodiscard]] bool hasTransparentFramebuffer() const noexcept;

        [[nodiscard]] IO::Types::Status setTitle(std::string_view utf8Title) noexcept;
        [[nodiscard]] IO::Types::Status setIcon(std::span<const Types::IconImageView> images) noexcept;
        [[nodiscard]] IO::Types::Status clearIcon() noexcept;
        [[nodiscard]] IO::Types::Status setClientSize(Types::LogicalSize size) noexcept;
        [[nodiscard]] IO::Types::Status setClientPosition(Types::ScreenPosition position) noexcept;
        [[nodiscard]] IO::Types::Status setClientRect(Types::ScreenPosition position, Types::LogicalSize size) noexcept;
        [[nodiscard]] IO::Types::Status centerOn(Types::Display::MonitorId monitor = {}) noexcept;
        [[nodiscard]] IO::Types::Status setSizeLimits(const Types::SizeLimits &limits) noexcept;
        [[nodiscard]] IO::Types::Status setAspectRatio(std::optional<Types::AspectRatio> aspectRatio) noexcept;
        [[nodiscard]] Types::ScreenPositionResult clientToScreen(Types::LogicalPosition position) const noexcept;
        [[nodiscard]] Types::LogicalPositionResult screenToClient(Types::ScreenPosition position) const noexcept;
        [[nodiscard]] IO::Types::Status setDpiResizePolicy(Types::DpiResizePolicy policy) noexcept;
        [[nodiscard]] IO::Types::Status show() noexcept;
        [[nodiscard]] IO::Types::Status hide() noexcept;
        [[nodiscard]] IO::Types::Status requestFocus() noexcept;
        [[nodiscard]] IO::Types::Status requestAttention() noexcept;
        [[nodiscard]] IO::Types::Status minimize() noexcept;
        [[nodiscard]] IO::Types::Status maximize() noexcept;
        [[nodiscard]] IO::Types::Status restore() noexcept;
        [[nodiscard]] IO::Types::Status setMode(const Types::ModeRequest &request) noexcept;
        [[nodiscard]] IO::Types::Status setResizable(bool resizable) noexcept;
        [[nodiscard]] IO::Types::Status setDecorationMode(Types::DecorationMode mode) noexcept;
        [[nodiscard]] IO::Types::Status setControls(const Types::Controls &controls) noexcept;
        [[nodiscard]] IO::Types::Status setFocusable(bool focusable) noexcept;
        [[nodiscard]] IO::Types::Status setUserInteractionEnabled(bool enabled) noexcept;
        [[nodiscard]] IO::Types::Status setAlwaysOnTop(bool alwaysOnTop) noexcept;
        [[nodiscard]] IO::Types::Status setOpacity(float opacity) noexcept;
        [[nodiscard]] IO::Types::Status setBackdropEffect(Types::BackdropEffect effect) noexcept;
        [[nodiscard]] IO::Types::Status setFileDropEnabled(bool enabled) noexcept;
        [[nodiscard]] IO::Types::Status setCustomChromeLayout(const Types::CustomChromeLayout &layout) noexcept;
        [[nodiscard]] IO::Types::Status clearCustomChromeLayout() noexcept;
        [[nodiscard]] IO::Types::Status setPointerInputLayout(const Types::PointerInputLayout &layout) noexcept;
        [[nodiscard]] IO::Types::Status setCursorMode(Types::CursorMode mode) noexcept;
        [[nodiscard]] IO::Types::Status setCursorShape(Types::CursorShape shape) noexcept;
        [[nodiscard]] IO::Types::Status setCursorPosition(Types::LogicalPosition clientPosition) noexcept;
        [[nodiscard]] Types::LogicalPositionResult cursorPosition() const noexcept;

    private:
        friend struct Detail::WindowAccess;
        std::unique_ptr<Detail::WindowState> state_;
        std::uint64_t pointerHitMaskGeneration_ = 0;
        bool pointerHitMaskGenerationExhausted_ = false;
    };
} // namespace GameWIP::Window
