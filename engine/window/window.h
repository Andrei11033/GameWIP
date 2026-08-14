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
        struct RendererIntegrationState;
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
            Display::MonitorId monitor;               ///< Active fullscreen monitor.
            std::optional<Display::Mode> displayMode; ///< Requested exclusive physical mode.
            bool exactDisplayMode = false;            ///< Whether the requested physical mode was matched exactly.
            bool suspended = false;                   ///< Whether fullscreen is temporarily suspended.
        };

        /// @brief Call-scoped declarative custom-chrome hit-test layout.
        struct CustomChromeLayout
        {
            std::span<const LogicalRect> draggableRegions;   ///< Client-local draggable regions.
            std::optional<LogicalRect> systemMenuRegion;     ///< Optional system-menu region.
            std::optional<LogicalRect> minimizeButtonRegion; ///< Optional minimize-button region.
            std::optional<LogicalRect> maximizeButtonRegion; ///< Optional maximize-button region.
            std::optional<LogicalRect> closeButtonRegion;    ///< Optional close-button region.
        };

        /// @brief Call-scoped declarative pointer-region layout.
        struct PointerInputLayout
        {
            PointerInputMode mode = PointerInputMode::Normal; ///< Hit-test policy.
            std::span<const LogicalRect> regions;             ///< Client-local acceptance or rejection regions.
        };

        /// @brief Call-scoped tightly packed RGBA8 icon image.
        struct IconImageView
        {
            PixelSize size;                   ///< Physical icon dimensions.
            std::span<const std::byte> rgba8; ///< Tightly packed row-major RGBA8 pixels.
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
            std::uint64_t flags = 0;                      ///< Bit set indexed by Capability.
            std::uint32_t maximumCustomChromeRegions = 0; ///< Maximum draggable custom-chrome regions.
            std::uint32_t maximumPointerInputRegions = 0; ///< Maximum pointer-input regions.

            /// @brief Returns whether a capability flag is present.
            [[nodiscard]] constexpr bool supports(Capability capability) const noexcept
            {
                const auto index = static_cast<std::uint8_t>(capability);
                return index < static_cast<std::uint8_t>(Capability::Count) && (flags & (std::uint64_t{1} << index)) != 0;
            }
        };

        /// @brief Status and cached backend capability snapshot.
        struct CapabilitiesResult
        {
            IO::Types::Status status;  ///< Capability-query status.
            Capabilities capabilities; ///< Cached capability values on success.
        };

        /// @brief Status and virtual-screen coordinate conversion result.
        struct ScreenPositionResult
        {
            IO::Types::Status status; ///< Conversion status.
            ScreenPosition position;  ///< Converted position on success.
        };

        /// @brief Status and client-local coordinate conversion result.
        struct LogicalPositionResult
        {
            IO::Types::Status status; ///< Conversion status.
            LogicalPosition position; ///< Converted position on success.
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
        /// @brief Constructs a closed Window owner.
        Window() noexcept;
        Window(const Window &) = delete;
        Window &operator=(const Window &) = delete;
        Window(Window &&) = delete;
        Window &operator=(Window &&) = delete;
        /// @brief Closes the native lifetime and releases owned state.
        ~Window() noexcept;

        /// @brief Opens with Events::kDefaultQueueCapacity internal slots.
        [[nodiscard]] IO::Types::Status open(const Types::Description &description = {}) noexcept;
        /// @brief Opens with a requested number of internally owned event slots.
        [[nodiscard]] IO::Types::Status open(const Types::Description &description, std::size_t eventQueueCapacity) noexcept;
        /// @brief Opens while borrowing caller-owned event storage until close.
        [[nodiscard]] IO::Types::Status open(const Types::Description &description, std::span<Types::Event> eventStorage) noexcept;

        /// @brief Returns whether the Window has a usable native lifetime.
        [[nodiscard]] bool isOpen() const noexcept;
        /// @brief Returns the portable native-resource lifecycle state.
        [[nodiscard]] Types::LifetimeState lifetimeState() const noexcept;
        /// @brief Closes the native lifetime on its owner thread.
        [[nodiscard]] IO::Types::Status close() noexcept;

        /// @brief Returns the current open-lifetime identity.
        [[nodiscard]] Types::WindowId id() const noexcept;
        /// @brief Returns the current owner Window identity.
        [[nodiscard]] Types::WindowId ownerId() const noexcept;
        /// @brief Returns whether the caller is the current open lifetime's owner thread.
        [[nodiscard]] bool isOwnedByCurrentThread() const noexcept;
        /// @brief Returns whether the backend supports a capability for Window objects.
        [[nodiscard]] bool supports(Types::Capability capability) const noexcept;
        /// @brief Changes or clears the native owner relationship.
        [[nodiscard]] IO::Types::Status setOwner(Types::WindowId owner) noexcept;

        /// @brief Returns whether a close request is pending.
        [[nodiscard]] bool hasCloseRequest() const noexcept;
        /// @brief Queues a programmatic close request without destroying the native Window.
        [[nodiscard]] IO::Types::Status requestClose() noexcept;
        /// @brief Clears the cached close-request flag.
        [[nodiscard]] IO::Types::Status clearCloseRequest() noexcept;

        /// @brief Removes the oldest queued event when available.
        [[nodiscard]] bool popEvent(Types::Event &outEvent) noexcept;
        /// @brief Removes up to the destination size oldest queued events.
        [[nodiscard]] std::size_t popEvents(std::span<Types::Event> destination) noexcept;
        /// @brief Removes all currently queued events.
        void clearEvents() noexcept;
        /// @brief Returns event-queue storage and counter information.
        [[nodiscard]] Types::Events::QueueInfo eventQueueInfo() const noexcept;
        /// @brief Resets the current lifetime's dropped-event counter.
        void clearDroppedEventCount() noexcept;
        /// @brief Wakes a thread blocked in Events::wait without queuing an event.
        [[nodiscard]] IO::Types::Status wakeEventWait() const noexcept;

        /// @brief Returns the cached UTF-8 title.
        [[nodiscard]] std::string_view title() const noexcept;
        /// @brief Returns the cached logical client extent.
        [[nodiscard]] Types::LogicalSize clientSize() const noexcept;
        /// @brief Returns the cached physical framebuffer extent.
        [[nodiscard]] Types::PixelSize framebufferSize() const noexcept;
        /// @brief Returns the cached virtual-screen client position.
        [[nodiscard]] Types::ScreenPosition clientPosition() const noexcept;
        /// @brief Returns the cached complete native frame rectangle.
        [[nodiscard]] Types::ScreenRect frameRect() const noexcept;
        /// @brief Returns cached client-to-frame edge distances.
        [[nodiscard]] Types::Insets frameInsets() const noexcept;
        /// @brief Returns the cached logical-to-physical scale.
        [[nodiscard]] Types::ContentScale contentScale() const noexcept;
        /// @brief Returns the cached effective content DPI.
        [[nodiscard]] Types::Dpi effectiveDpi() const noexcept;
        /// @brief Returns the cached DPI resize policy.
        [[nodiscard]] Types::DpiResizePolicy dpiResizePolicy() const noexcept;
        /// @brief Returns the cached current monitor identity.
        [[nodiscard]] Types::Display::MonitorId currentMonitor() const noexcept;
        /// @brief Returns the cached top-level mode.
        [[nodiscard]] Types::Mode mode() const noexcept;
        /// @brief Returns cached fullscreen details.
        [[nodiscard]] Types::FullscreenInfo fullscreenInfo() const noexcept;
        /// @brief Returns the cached presentation state.
        [[nodiscard]] Types::PresentationState presentationState() const noexcept;
        /// @brief Returns the cached decoration policy.
        [[nodiscard]] Types::DecorationMode decorationMode() const noexcept;
        /// @brief Returns cached standard-control availability.
        [[nodiscard]] Types::Controls controls() const noexcept;
        /// @brief Returns cached logical client-size limits.
        [[nodiscard]] Types::SizeLimits sizeLimits() const noexcept;
        /// @brief Returns the cached optional client aspect-ratio constraint.
        [[nodiscard]] std::optional<Types::AspectRatio> aspectRatio() const noexcept;
        /// @brief Returns the cached cursor mode.
        [[nodiscard]] Types::CursorMode cursorMode() const noexcept;
        /// @brief Returns the cached cursor shape.
        [[nodiscard]] Types::CursorShape cursorShape() const noexcept;
        /// @brief Returns the cached pointer hit-test policy.
        [[nodiscard]] Types::PointerInputMode pointerInputMode() const noexcept;
        /// @brief Returns the cached pointer-input region count.
        [[nodiscard]] std::size_t pointerInputRegionCount() const noexcept;
        /// @brief Returns the cached backdrop treatment.
        [[nodiscard]] Types::BackdropEffect backdropEffect() const noexcept;
        /// @brief Returns the cached opacity in the inclusive range [0, 1].
        [[nodiscard]] float opacity() const noexcept;
        /// @brief Returns the cached visibility state.
        [[nodiscard]] bool isVisible() const noexcept;
        /// @brief Returns the cached keyboard-focus state.
        [[nodiscard]] bool isFocused() const noexcept;
        /// @brief Returns whether the cached presentation state is minimized.
        [[nodiscard]] bool isMinimized() const noexcept;
        /// @brief Returns whether the cached presentation state is maximized.
        [[nodiscard]] bool isMaximized() const noexcept;
        /// @brief Returns renderer-supplied occlusion state when a provider is attached.
        [[nodiscard]] bool isOccluded() const noexcept;
        /// @brief Returns whether the cursor is cached inside the client area.
        [[nodiscard]] bool isCursorInside() const noexcept;
        /// @brief Returns the cached user-resizable policy.
        [[nodiscard]] bool isResizable() const noexcept;
        /// @brief Returns the cached focusable policy.
        [[nodiscard]] bool isFocusable() const noexcept;
        /// @brief Returns the cached user-interaction policy.
        [[nodiscard]] bool isUserInteractionEnabled() const noexcept;
        /// @brief Returns the cached topmost-ordering policy.
        [[nodiscard]] bool isAlwaysOnTop() const noexcept;
        /// @brief Returns the cached portable file-drop policy.
        [[nodiscard]] bool isFileDropEnabled() const noexcept;
        /// @brief Returns whether framebuffer alpha is configured to reach the desktop.
        [[nodiscard]] bool hasTransparentFramebuffer() const noexcept;

        /// @brief Replaces the UTF-8 native and cached title.
        [[nodiscard]] IO::Types::Status setTitle(std::string_view utf8Title) noexcept;
        /// @brief Replaces platform icon images from call-scoped RGBA8 views.
        [[nodiscard]] IO::Types::Status setIcon(std::span<const Types::IconImageView> images) noexcept;
        /// @brief Restores the platform-default Window icon.
        [[nodiscard]] IO::Types::Status clearIcon() noexcept;
        /// @brief Changes the logical client extent.
        [[nodiscard]] IO::Types::Status setClientSize(Types::LogicalSize size) noexcept;
        /// @brief Changes the virtual-screen client position.
        [[nodiscard]] IO::Types::Status setClientPosition(Types::ScreenPosition position) noexcept;
        /// @brief Changes the virtual-screen client position and logical extent together.
        [[nodiscard]] IO::Types::Status setClientRect(Types::ScreenPosition position, Types::LogicalSize size) noexcept;
        /// @brief Centers the Window on a requested or current monitor.
        [[nodiscard]] IO::Types::Status centerOn(Types::Display::MonitorId monitor = {}) noexcept;
        /// @brief Replaces logical client-size constraints.
        [[nodiscard]] IO::Types::Status setSizeLimits(const Types::SizeLimits &limits) noexcept;
        /// @brief Replaces or clears the logical client aspect-ratio constraint.
        [[nodiscard]] IO::Types::Status setAspectRatio(std::optional<Types::AspectRatio> aspectRatio) noexcept;
        /// @brief Converts a client-local logical position to virtual-screen pixels.
        [[nodiscard]] Types::ScreenPositionResult clientToScreen(Types::LogicalPosition position) const noexcept;
        /// @brief Converts a virtual-screen pixel position to client-local logical units.
        [[nodiscard]] Types::LogicalPositionResult screenToClient(Types::ScreenPosition position) const noexcept;
        /// @brief Changes how logical and physical client size respond to DPI transitions.
        [[nodiscard]] IO::Types::Status setDpiResizePolicy(Types::DpiResizePolicy policy) noexcept;
        /// @brief Shows the native Window.
        [[nodiscard]] IO::Types::Status show() noexcept;
        /// @brief Hides the native Window.
        [[nodiscard]] IO::Types::Status hide() noexcept;
        /// @brief Requests keyboard focus and activation.
        [[nodiscard]] IO::Types::Status requestFocus() noexcept;
        /// @brief Requests platform attention without requiring focus.
        [[nodiscard]] IO::Types::Status requestAttention() noexcept;
        /// @brief Requests minimized presentation.
        [[nodiscard]] IO::Types::Status minimize() noexcept;
        /// @brief Requests maximized presentation.
        [[nodiscard]] IO::Types::Status maximize() noexcept;
        /// @brief Restores normal presentation.
        [[nodiscard]] IO::Types::Status restore() noexcept;
        /// @brief Applies a validated top-level and optional exclusive display-mode request.
        [[nodiscard]] IO::Types::Status setMode(const Types::ModeRequest &request) noexcept;
        /// @brief Enables or disables user resizing.
        [[nodiscard]] IO::Types::Status setResizable(bool resizable) noexcept;
        /// @brief Changes the non-client decoration policy.
        [[nodiscard]] IO::Types::Status setDecorationMode(Types::DecorationMode mode) noexcept;
        /// @brief Replaces standard system-control availability.
        [[nodiscard]] IO::Types::Status setControls(const Types::Controls &controls) noexcept;
        /// @brief Enables or disables keyboard focus eligibility.
        [[nodiscard]] IO::Types::Status setFocusable(bool focusable) noexcept;
        /// @brief Enables or disables user interaction.
        [[nodiscard]] IO::Types::Status setUserInteractionEnabled(bool enabled) noexcept;
        /// @brief Enables or disables topmost ordering.
        [[nodiscard]] IO::Types::Status setAlwaysOnTop(bool alwaysOnTop) noexcept;
        /// @brief Changes opacity in the inclusive range [0, 1].
        [[nodiscard]] IO::Types::Status setOpacity(float opacity) noexcept;
        /// @brief Changes the optional platform backdrop treatment.
        [[nodiscard]] IO::Types::Status setBackdropEffect(Types::BackdropEffect effect) noexcept;
        /// @brief Enables or disables portable file-drop events.
        [[nodiscard]] IO::Types::Status setFileDropEnabled(bool enabled) noexcept;
        /// @brief Copies a declarative custom-chrome layout into Window-owned state.
        [[nodiscard]] IO::Types::Status setCustomChromeLayout(const Types::CustomChromeLayout &layout) noexcept;
        /// @brief Clears all custom-chrome regions while retaining custom decoration mode.
        [[nodiscard]] IO::Types::Status clearCustomChromeLayout() noexcept;
        /// @brief Copies a declarative pointer-input layout into Window-owned state.
        [[nodiscard]] IO::Types::Status setPointerInputLayout(const Types::PointerInputLayout &layout) noexcept;
        /// @brief Changes cursor visibility, confinement, or relative behavior.
        [[nodiscard]] IO::Types::Status setCursorMode(Types::CursorMode mode) noexcept;
        /// @brief Changes the standard system cursor shape.
        [[nodiscard]] IO::Types::Status setCursorShape(Types::CursorShape shape) noexcept;
        /// @brief Warps the cursor to a client-local logical position.
        [[nodiscard]] IO::Types::Status setCursorPosition(Types::LogicalPosition clientPosition) noexcept;
        /// @brief Queries the current cursor position in client-local logical units.
        [[nodiscard]] Types::LogicalPositionResult cursorPosition() const noexcept;

    private:
        friend struct Detail::WindowAccess;
        std::unique_ptr<Detail::WindowState> state_;
        std::unique_ptr<Detail::RendererIntegrationState> rendererIntegration_;
    };
} // namespace GameWIP::Window
