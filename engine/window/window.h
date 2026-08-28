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
            MultipleWindows,           ///< More than one Window may be open in the process.
            MultipleWindowThreads,     ///< Independent threads may own different Windows.
            OwnedWindows,              ///< Native owner/owned relationships are supported.
            RuntimeOwnerChange,        ///< An open Window's owner relationship may change.
            WindowPositioning,         ///< Programmatic desktop positioning is supported.
            ProgrammaticFocus,         ///< Focus and activation may be requested programmatically.
            AttentionRequest,          ///< Platform attention may be requested without focus.
            RuntimeDecorationChange,   ///< Decoration mode may change after open.
            CustomChrome,              ///< Declarative custom drag and caption-control regions are supported.
            WindowIcon,                ///< Runtime custom icon images are supported.
            AspectRatioConstraint,     ///< Native user resizing can enforce a client aspect ratio.
            RuntimeInteractionControl, ///< Native user interaction can be enabled or disabled at runtime.
            AlwaysOnTop,               ///< Topmost desktop ordering is supported.
            Opacity,                   ///< Whole-Window opacity is supported.
            TransparentFramebuffer,    ///< Framebuffer alpha can reach desktop composition.
            SystemBackdrop,            ///< Optional native backdrop effects are supported.
            PointerClickThrough,       ///< Whole-client pointer click-through is supported.
            PointerRegions,            ///< Declarative logical pointer regions are supported.
            PointerHitMask,            ///< Renderer-published pixel hit masks are supported.
            CursorConfinement,         ///< Cursor confinement to the client area is supported.
            RelativeCursor,            ///< Relative cursor mode is supported.
            CursorWarping,             ///< Programmatic client-local cursor positioning is supported.
            CustomCursor,              ///< Application-provided native cursor images are supported.
            FileDrop,                  ///< Portable file-drop events are supported.
            ExclusiveFullscreen,       ///< Exclusive display-mode fullscreen is supported.
            OcclusionReporting,        ///< Renderer occlusion-provider feedback is supported.
            ChildSurface,              ///< Managed native child-window hosts are supported.
            Count                      ///< Enumerator count used to bound capability bit indexes.
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
    /// @return The query status and capability flags available in the current process environment.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::CapabilitiesResult getCapabilities() noexcept;
    /// @brief Returns whether the backend advertises a known capability.
    /// @param capability Capability to test.
    /// @return true when the capability is currently advertised; otherwise false.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT bool supports(Types::Capability capability) noexcept;

    /// @brief Non-copyable, non-movable RAII owner of one native top-level desktop Window.
    /// @details Except wakeEventWait(), open-object operations require the opening thread. Cached
    /// getters do not query the backend and are not synchronized. The stable object address and
    /// thread affinity last until explicit close or destruction.
    class GAMEWIP_WINDOW_EXPORT Window final
    {
    public:
        /// @name Lifecycle
        /// @{

        /// @brief Constructs a closed Window owner.
        Window() noexcept;
        Window(const Window &) = delete;
        Window &operator=(const Window &) = delete;
        Window(Window &&) = delete;
        Window &operator=(Window &&) = delete;
        /// @brief Closes the native lifetime and releases owned state.
        ~Window() noexcept;

        /// @brief Opens with Events::kDefaultQueueCapacity internal slots.
        /// @param description Initial Window properties and policies.
        /// @return Success, or a status explaining why no native Window was opened.
        [[nodiscard]] IO::Types::Status open(const Types::Description &description = {}) noexcept;
        /// @brief Opens with a requested number of internally owned event slots.
        /// @param description Initial Window properties and policies.
        /// @param eventQueueCapacity Number of event slots to allocate; must be greater than zero.
        /// @return Success, or a status explaining why no native Window was opened.
        [[nodiscard]] IO::Types::Status open(const Types::Description &description, std::size_t eventQueueCapacity) noexcept;
        /// @brief Opens while borrowing caller-owned event storage until close.
        /// @param description Initial Window properties and policies.
        /// @param eventStorage Non-empty storage that must remain alive and unmoved until close.
        /// @return Success, or a status explaining why no native Window was opened.
        [[nodiscard]] IO::Types::Status open(const Types::Description &description, std::span<Types::Event> eventStorage) noexcept;

        /// @brief Returns whether the Window has a usable native lifetime.
        /// @return true while native operations are available; otherwise false.
        [[nodiscard]] bool isOpen() const noexcept;
        /// @brief Returns the portable native-resource lifecycle state.
        /// @return The current lifecycle state, including pending owner-thread finalization.
        [[nodiscard]] Types::LifetimeState lifetimeState() const noexcept;
        /// @brief Closes the native lifetime on its owner thread.
        /// @return Success when closed or already closed; ResourceBusy on the wrong thread; otherwise the native cleanup failure.
        [[nodiscard]] IO::Types::Status close() noexcept;
        /// @}

        /// @name Identity and ownership
        /// @{

        /// @brief Returns the current open-lifetime identity.
        /// @return The current identity, or an invalid ID while closed.
        [[nodiscard]] Types::WindowId id() const noexcept;
        /// @brief Returns the current owner Window identity.
        /// @return The owner identity, or an invalid ID when this Window has no owner.
        [[nodiscard]] Types::WindowId ownerId() const noexcept;
        /// @brief Returns whether the caller is the current open lifetime's owner thread.
        /// @return true only when open and called by the opening thread.
        [[nodiscard]] bool isOwnedByCurrentThread() const noexcept;
        /// @brief Returns whether the backend supports a capability for Window objects.
        /// @param capability Capability to test.
        /// @return true when the capability is advertised for Window objects; otherwise false.
        [[nodiscard]] bool supports(Types::Capability capability) const noexcept;
        /// @brief Changes or clears the native owner relationship.
        /// @param owner New owner identity, or an invalid ID to remove the relationship.
        /// @return Success, or the validation, ownership, thread, or native failure.
        [[nodiscard]] IO::Types::Status setOwner(Types::WindowId owner) noexcept;
        /// @}

        /// @name Close requests
        /// @{

        /// @brief Returns whether a close request is pending.
        /// @return true after a close request and before it is cleared or the Window closes.
        [[nodiscard]] bool hasCloseRequest() const noexcept;
        /// @brief Queues a programmatic close request without destroying the native Window.
        /// @return Success, or the open-state, thread, or native queueing failure.
        [[nodiscard]] IO::Types::Status requestClose() noexcept;
        /// @brief Clears the cached close-request flag.
        /// @return Success, or the open-state or wrong-thread failure.
        [[nodiscard]] IO::Types::Status clearCloseRequest() noexcept;
        /// @}

        /// @name Event queue
        /// @{

        /// @brief Removes the oldest queued event when available.
        /// @param outEvent Receives the removed event on success and is unchanged when the queue is empty.
        /// @return true when an event was removed; otherwise false.
        [[nodiscard]] bool popEvent(Types::Event &outEvent) noexcept;
        /// @brief Removes up to the destination size oldest queued events.
        /// @param destination Storage that receives events in queue order.
        /// @return The number of events copied and removed.
        [[nodiscard]] std::size_t popEvents(std::span<Types::Event> destination) noexcept;
        /// @brief Removes all currently queued events.
        void clearEvents() noexcept;
        /// @brief Returns event-queue storage and counter information.
        /// @return A snapshot of capacity, queued count, and dropped-event count.
        [[nodiscard]] Types::Events::QueueInfo eventQueueInfo() const noexcept;
        /// @brief Resets the current lifetime's dropped-event counter.
        void clearDroppedEventCount() noexcept;
        /// @brief Wakes a thread blocked in Events::wait without queuing an event.
        /// @return Success, or the open-state or native wake failure.
        [[nodiscard]] IO::Types::Status wakeEventWait() const noexcept;
        /// @}

        /// @name Cached state
        /// @{

        /// @brief Returns the cached UTF-8 title.
        /// @return A view valid until the title changes or the Window closes.
        [[nodiscard]] std::string_view title() const noexcept;
        /// @brief Returns the cached logical client extent.
        /// @return Client width and height in logical units.
        [[nodiscard]] Types::LogicalSize clientSize() const noexcept;
        /// @brief Returns the cached physical framebuffer extent.
        /// @return Framebuffer width and height in physical pixels.
        [[nodiscard]] Types::PixelSize framebufferSize() const noexcept;
        /// @brief Returns the cached virtual-screen client position.
        /// @return Client-origin position in physical virtual-screen coordinates.
        [[nodiscard]] Types::ScreenPosition clientPosition() const noexcept;
        /// @brief Returns the cached complete native frame rectangle.
        /// @return Outer frame rectangle in physical virtual-screen coordinates.
        [[nodiscard]] Types::ScreenRect frameRect() const noexcept;
        /// @brief Returns cached client-to-frame edge distances.
        /// @return Physical inset from each outer frame edge to the client area.
        [[nodiscard]] Types::Insets frameInsets() const noexcept;
        /// @brief Returns the cached logical-to-physical scale.
        /// @return Horizontal and vertical content scale.
        [[nodiscard]] Types::ContentScale contentScale() const noexcept;
        /// @brief Returns the cached effective content DPI.
        /// @return Horizontal and vertical effective DPI.
        [[nodiscard]] Types::Dpi effectiveDpi() const noexcept;
        /// @brief Returns the cached DPI resize policy.
        /// @return The policy applied when the effective DPI changes.
        [[nodiscard]] Types::DpiResizePolicy dpiResizePolicy() const noexcept;
        /// @brief Returns the cached current monitor identity.
        /// @return Current monitor ID, or an invalid ID when no monitor is known.
        [[nodiscard]] Types::Display::MonitorId currentMonitor() const noexcept;
        /// @brief Returns the cached top-level mode.
        /// @return Current windowed, borderless-fullscreen, or exclusive-fullscreen mode.
        [[nodiscard]] Types::Mode mode() const noexcept;
        /// @brief Returns cached fullscreen details.
        /// @return Current monitor and display-mode selection for the fullscreen state.
        [[nodiscard]] Types::FullscreenInfo fullscreenInfo() const noexcept;
        /// @brief Returns the cached presentation state.
        /// @return Current normal, minimized, or maximized presentation state.
        [[nodiscard]] Types::PresentationState presentationState() const noexcept;
        /// @brief Returns the cached decoration policy.
        /// @return Current system or custom decoration mode.
        [[nodiscard]] Types::DecorationMode decorationMode() const noexcept;
        /// @brief Returns cached standard-control availability.
        /// @return Current minimize, maximize, and close-control policy.
        [[nodiscard]] Types::Controls controls() const noexcept;
        /// @brief Returns cached logical client-size limits.
        /// @return Current minimum and maximum logical client-size constraints.
        [[nodiscard]] Types::SizeLimits sizeLimits() const noexcept;
        /// @brief Returns the cached optional client aspect-ratio constraint.
        /// @return The active ratio, or std::nullopt when unconstrained.
        [[nodiscard]] std::optional<Types::AspectRatio> aspectRatio() const noexcept;
        /// @brief Returns the cached cursor mode.
        /// @return Current cursor visibility, confinement, or relative-input mode.
        [[nodiscard]] Types::CursorMode cursorMode() const noexcept;
        /// @brief Returns the cached cursor shape.
        /// @return Current standard system cursor shape.
        [[nodiscard]] Types::CursorShape cursorShape() const noexcept;
        /// @brief Returns the cached pointer hit-test policy.
        /// @return Current whole-client, declarative-region, or renderer-mask policy.
        [[nodiscard]] Types::PointerInputMode pointerInputMode() const noexcept;
        /// @brief Returns the cached pointer-input region count.
        /// @return Number of copied declarative pointer-input regions.
        [[nodiscard]] std::size_t pointerInputRegionCount() const noexcept;
        /// @brief Returns the cached backdrop treatment.
        /// @return Current optional platform backdrop effect.
        [[nodiscard]] Types::BackdropEffect backdropEffect() const noexcept;
        /// @brief Returns the cached opacity in the inclusive range [0, 1].
        /// @return Current whole-Window opacity.
        [[nodiscard]] float opacity() const noexcept;
        /// @brief Returns the cached visibility state.
        /// @return true when the Window is requested visible; otherwise false.
        [[nodiscard]] bool isVisible() const noexcept;
        /// @brief Returns the cached keyboard-focus state.
        /// @return true when the Window currently has keyboard focus.
        [[nodiscard]] bool isFocused() const noexcept;
        /// @brief Returns whether the cached presentation state is minimized.
        /// @return true when presentationState() is Minimized.
        [[nodiscard]] bool isMinimized() const noexcept;
        /// @brief Returns whether the cached presentation state is maximized.
        /// @return true when presentationState() is Maximized.
        [[nodiscard]] bool isMaximized() const noexcept;
        /// @brief Returns renderer-supplied occlusion state when a provider is attached.
        /// @return The last attached renderer report, or false without a provider.
        [[nodiscard]] bool isOccluded() const noexcept;
        /// @brief Returns whether the cursor is cached inside the client area.
        /// @return true when the latest native pointer state is inside the client area.
        [[nodiscard]] bool isCursorInside() const noexcept;
        /// @brief Returns the cached user-resizable policy.
        /// @return true when user-driven resizing is enabled.
        [[nodiscard]] bool isResizable() const noexcept;
        /// @brief Returns the cached focusable policy.
        /// @return true when the Window is eligible for keyboard focus.
        [[nodiscard]] bool isFocusable() const noexcept;
        /// @brief Returns the cached user-interaction policy.
        /// @return true when native user interaction is enabled.
        [[nodiscard]] bool isUserInteractionEnabled() const noexcept;
        /// @brief Returns the cached topmost-ordering policy.
        /// @return true when topmost ordering is requested.
        [[nodiscard]] bool isAlwaysOnTop() const noexcept;
        /// @brief Returns the cached portable file-drop policy.
        /// @return true when portable file-drop events are enabled.
        [[nodiscard]] bool isFileDropEnabled() const noexcept;
        /// @brief Returns whether framebuffer alpha is configured to reach the desktop.
        /// @return true when transparent framebuffer composition is enabled.
        [[nodiscard]] bool hasTransparentFramebuffer() const noexcept;
        /// @}

        /// @name Geometry and content
        /// @{

        /// @brief Replaces the UTF-8 native and cached title.
        /// @param utf8Title Complete valid UTF-8 without embedded U+0000.
        /// @return Success, or the validation, open-state, thread, or native failure.
        [[nodiscard]] IO::Types::Status setTitle(std::string_view utf8Title) noexcept;
        /// @brief Replaces platform icon images from call-scoped RGBA8 views.
        /// @param images Non-empty icon views copied before return.
        /// @return Success, or the validation, open-state, thread, allocation, or native failure.
        [[nodiscard]] IO::Types::Status setIcon(std::span<const Types::IconImageView> images) noexcept;
        /// @brief Restores the platform-default Window icon.
        /// @return Success, or the open-state, thread, or native failure.
        [[nodiscard]] IO::Types::Status clearIcon() noexcept;
        /// @brief Changes the logical client extent.
        /// @param size Requested nonzero client size in logical units.
        /// @return Success, or the validation, open-state, thread, or native failure.
        [[nodiscard]] IO::Types::Status setClientSize(Types::LogicalSize size) noexcept;
        /// @brief Changes the virtual-screen client position.
        /// @param position Requested client origin in physical virtual-screen coordinates.
        /// @return Success, or the open-state, thread, or native failure.
        [[nodiscard]] IO::Types::Status setClientPosition(Types::ScreenPosition position) noexcept;
        /// @brief Changes the virtual-screen client position and logical extent together.
        /// @param position Requested client origin in physical virtual-screen coordinates.
        /// @param size Requested nonzero client size in logical units.
        /// @return Success, or the validation, open-state, thread, or native failure.
        [[nodiscard]] IO::Types::Status setClientRect(Types::ScreenPosition position, Types::LogicalSize size) noexcept;
        /// @brief Centers the Window on a requested or current monitor.
        /// @param monitor Target monitor, or an invalid ID to use the current/nearest monitor.
        /// @return Success, or the monitor, open-state, thread, or native failure.
        [[nodiscard]] IO::Types::Status centerOn(Types::Display::MonitorId monitor = {}) noexcept;
        /// @brief Replaces logical client-size constraints.
        /// @param limits Minimum and optional maximum client size in logical units.
        /// @return Success, or the validation, open-state, thread, or native failure.
        [[nodiscard]] IO::Types::Status setSizeLimits(const Types::SizeLimits &limits) noexcept;
        /// @brief Replaces or clears the logical client aspect-ratio constraint.
        /// @param aspectRatio Positive ratio to enforce, or std::nullopt to clear it.
        /// @return Success, or the validation, open-state, thread, or native failure.
        [[nodiscard]] IO::Types::Status setAspectRatio(std::optional<Types::AspectRatio> aspectRatio) noexcept;
        /// @brief Converts a client-local logical position to virtual-screen pixels.
        /// @param position Position relative to the logical client origin.
        /// @return Status and converted physical virtual-screen position.
        [[nodiscard]] Types::ScreenPositionResult clientToScreen(Types::LogicalPosition position) const noexcept;
        /// @brief Converts a virtual-screen pixel position to client-local logical units.
        /// @param position Position in physical virtual-screen coordinates.
        /// @return Status and converted logical client position.
        [[nodiscard]] Types::LogicalPositionResult screenToClient(Types::ScreenPosition position) const noexcept;
        /// @brief Changes how logical and physical client size respond to DPI transitions.
        /// @param policy Logical-size or physical-pixel preservation policy.
        /// @return Success, or the validation, open-state, thread, or native failure.
        [[nodiscard]] IO::Types::Status setDpiResizePolicy(Types::DpiResizePolicy policy) noexcept;
        /// @}

        /// @name Presentation and interaction
        /// @{

        /// @brief Shows the native Window.
        /// @return Success, or the open-state, thread, or native failure.
        [[nodiscard]] IO::Types::Status show() noexcept;
        /// @brief Hides the native Window.
        /// @return Success, or the open-state, thread, or native failure.
        [[nodiscard]] IO::Types::Status hide() noexcept;
        /// @brief Requests keyboard focus and activation.
        /// @return Success when the request was submitted, or the open-state, thread, policy, or native failure.
        [[nodiscard]] IO::Types::Status requestFocus() noexcept;
        /// @brief Requests platform attention without requiring focus.
        /// @return Success when the request was submitted, or the open-state, thread, capability, or native failure.
        [[nodiscard]] IO::Types::Status requestAttention() noexcept;
        /// @brief Requests minimized presentation.
        /// @return Success, or the open-state, thread, capability, or native failure.
        [[nodiscard]] IO::Types::Status minimize() noexcept;
        /// @brief Requests maximized presentation.
        /// @return Success, or the open-state, thread, capability, or native failure.
        [[nodiscard]] IO::Types::Status maximize() noexcept;
        /// @brief Restores normal presentation.
        /// @return Success, or the open-state, thread, capability, or native failure.
        [[nodiscard]] IO::Types::Status restore() noexcept;
        /// @brief Applies a validated top-level and optional exclusive display-mode request.
        /// @param request Target mode, monitor, and optional exclusive display mode.
        /// @return Success, or the validation, monitor, capability, open-state, thread, or native failure.
        [[nodiscard]] IO::Types::Status setMode(const Types::ModeRequest &request) noexcept;
        /// @brief Enables or disables user resizing.
        /// @param resizable Whether the user may resize the Window.
        /// @return Success, or the open-state, thread, capability, or native failure.
        [[nodiscard]] IO::Types::Status setResizable(bool resizable) noexcept;
        /// @brief Changes the non-client decoration policy.
        /// @param mode System or custom decoration mode.
        /// @return Success, or the validation, open-state, thread, capability, or native failure.
        [[nodiscard]] IO::Types::Status setDecorationMode(Types::DecorationMode mode) noexcept;
        /// @brief Replaces standard system-control availability.
        /// @param controls Requested minimize, maximize, and close-control policy.
        /// @return Success, or the validation, open-state, thread, capability, or native failure.
        [[nodiscard]] IO::Types::Status setControls(const Types::Controls &controls) noexcept;
        /// @brief Enables or disables keyboard focus eligibility.
        /// @param focusable Whether the Window may receive keyboard focus.
        /// @return Success, or the open-state, thread, capability, or native failure.
        [[nodiscard]] IO::Types::Status setFocusable(bool focusable) noexcept;
        /// @brief Enables or disables user interaction.
        /// @param enabled Whether native user interaction is enabled.
        /// @return Success, or the open-state, thread, capability, or native failure.
        [[nodiscard]] IO::Types::Status setUserInteractionEnabled(bool enabled) noexcept;
        /// @brief Enables or disables topmost ordering.
        /// @param alwaysOnTop Whether to request topmost ordering.
        /// @return Success, or the open-state, thread, capability, or native failure.
        [[nodiscard]] IO::Types::Status setAlwaysOnTop(bool alwaysOnTop) noexcept;
        /// @brief Changes opacity in the inclusive range [0, 1].
        /// @param opacity Fully transparent 0 through fully opaque 1.
        /// @return Success, or the validation, open-state, thread, capability, or native failure.
        [[nodiscard]] IO::Types::Status setOpacity(float opacity) noexcept;
        /// @brief Changes the optional platform backdrop treatment.
        /// @param effect Requested backdrop effect, including None.
        /// @return Success, or the validation, open-state, thread, capability, or native failure.
        [[nodiscard]] IO::Types::Status setBackdropEffect(Types::BackdropEffect effect) noexcept;
        /// @brief Enables or disables portable file-drop events.
        /// @param enabled Whether accepted native drops become portable events.
        /// @return Success, or the open-state, thread, capability, or native failure.
        [[nodiscard]] IO::Types::Status setFileDropEnabled(bool enabled) noexcept;
        /// @brief Copies a declarative custom-chrome layout into Window-owned state.
        /// @param layout Drag and caption-control regions in logical client coordinates.
        /// @return Success, or the validation, allocation, open-state, thread, capability, or native failure.
        [[nodiscard]] IO::Types::Status setCustomChromeLayout(const Types::CustomChromeLayout &layout) noexcept;
        /// @brief Clears all custom-chrome regions while retaining custom decoration mode.
        /// @return Success, or the open-state, thread, capability, or native failure.
        [[nodiscard]] IO::Types::Status clearCustomChromeLayout() noexcept;
        /// @brief Copies a declarative pointer-input layout into Window-owned state.
        /// @param layout Pointer policy and logical client regions to copy.
        /// @return Success, or the validation, allocation, open-state, thread, capability, or native failure.
        [[nodiscard]] IO::Types::Status setPointerInputLayout(const Types::PointerInputLayout &layout) noexcept;
        /// @}

        /// @name Cursor controls
        /// @{

        /// @brief Changes cursor visibility, confinement, or relative behavior.
        /// @param mode Requested cursor mode.
        /// @return Success, or the validation, open-state, focus, thread, capability, or native failure.
        [[nodiscard]] IO::Types::Status setCursorMode(Types::CursorMode mode) noexcept;
        /// @brief Changes the standard system cursor shape.
        /// @param shape Requested standard cursor shape.
        /// @return Success, or the validation, open-state, thread, capability, or native failure.
        [[nodiscard]] IO::Types::Status setCursorShape(Types::CursorShape shape) noexcept;
        /// @brief Warps the cursor to a client-local logical position.
        /// @param clientPosition Destination in logical client coordinates.
        /// @return Success, or the validation, open-state, thread, capability, or native failure.
        [[nodiscard]] IO::Types::Status setCursorPosition(Types::LogicalPosition clientPosition) noexcept;
        /// @brief Queries the current cursor position in client-local logical units.
        /// @return Status and current logical client position.
        [[nodiscard]] Types::LogicalPositionResult cursorPosition() const noexcept;
        /// @}

    private:
        friend struct Detail::WindowAccess;
        std::unique_ptr<Detail::WindowState> state_;
        std::unique_ptr<Detail::RendererIntegrationState> rendererIntegration_;
    };
} // namespace GameWIP::Window
