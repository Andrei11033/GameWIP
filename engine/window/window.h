/// @file window.h
/// @brief Portable top-level desktop-window API for GameWIP.

#pragma once

#include "filesystem/filesystem.h"
#include "io/io.h"
#include "window/window_export.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

/// @brief Portable desktop-window ownership, state, event, monitor, and control APIs.
namespace GameWIP::Window
{
    /// @brief Default fixed event-queue capacity allocated by Window::open().
    inline constexpr std::size_t kDefaultEventQueueCapacity = 256;
    /// @brief Non-blocking timeout used by waitEvents().
    inline constexpr std::chrono::milliseconds kNoWait{0};
    /// @brief Sentinel requesting an unbounded wait in waitEvents().
    inline constexpr std::chrono::milliseconds kWaitForever{-1};

    /// @brief Private implementation declarations used by the exported pImpl boundary.
    namespace Detail
    {
        struct WindowState;
        struct WindowAccess;
    } // namespace Detail

    /// @brief Passive portable values, options, events, and operation results.
    namespace Types
    {
        /// @brief Process-local identity of one successful Window open lifetime.
        struct WindowId
        {
            std::uint64_t value = 0; ///< Opaque identity, or zero when invalid.
            /// @brief Returns whether this identity is nonzero.
            [[nodiscard]] constexpr bool valid() const noexcept
            {
                return value != 0;
            }
            /// @brief Compares opaque identity values.
            friend constexpr bool operator==(WindowId, WindowId) noexcept = default;
        };

        /// @brief Process-local identity of a currently known desktop monitor.
        struct MonitorId
        {
            std::uint64_t value = 0; ///< Opaque identity, or zero when invalid.
            /// @brief Returns whether this identity is nonzero.
            [[nodiscard]] constexpr bool valid() const noexcept
            {
                return value != 0;
            }
            /// @brief Compares opaque identity values.
            friend constexpr bool operator==(MonitorId, MonitorId) noexcept = default;
        };

        /// @brief Position in a Window's logical client coordinate space.
        struct LogicalPosition
        {
            std::int32_t x = 0; ///< Horizontal coordinate.
            std::int32_t y = 0; ///< Vertical coordinate.
            /// @brief Compares both logical coordinates.
            friend constexpr bool operator==(LogicalPosition, LogicalPosition) noexcept = default;
        };

        /// @brief Extent in a Window's DPI-independent logical client units.
        struct LogicalSize
        {
            std::uint32_t width = 0;  ///< Logical width.
            std::uint32_t height = 0; ///< Logical height.
            /// @brief Compares both logical dimensions.
            friend constexpr bool operator==(LogicalSize, LogicalSize) noexcept = default;
        };

        /// @brief Physical drawable extent in pixels.
        struct PixelSize
        {
            std::uint32_t width = 0;  ///< Physical pixel width.
            std::uint32_t height = 0; ///< Physical pixel height.
            /// @brief Compares both dimensions.
            friend constexpr bool operator==(PixelSize, PixelSize) noexcept = default;
        };

        /// @brief Client-local logical rectangle.
        struct LogicalRect
        {
            LogicalPosition position; ///< Rectangle top-left.
            LogicalSize size;         ///< Rectangle extent.
            /// @brief Compares logical position and extent.
            friend constexpr bool operator==(LogicalRect, LogicalRect) noexcept = default;
        };

        /// @brief Physical position in the shared Win32 virtual-screen coordinate space.
        /// @details Either coordinate may be negative.
        struct ScreenPosition
        {
            std::int32_t x = 0; ///< Physical virtual-screen x coordinate.
            std::int32_t y = 0; ///< Physical virtual-screen y coordinate.
            /// @brief Compares both physical screen coordinates.
            friend constexpr bool operator==(ScreenPosition, ScreenPosition) noexcept = default;
        };

        /// @brief Physical rectangle in the shared virtual-screen coordinate space.
        struct ScreenRect
        {
            ScreenPosition position; ///< Physical top-left, possibly negative.
            PixelSize size;          ///< Physical pixel extent.
            /// @brief Compares physical position and extent.
            friend constexpr bool operator==(ScreenRect, ScreenRect) noexcept = default;
        };

        /// @brief Logical distances between a client area and complete outer frame.
        struct Insets
        {
            std::uint32_t left = 0;   ///< Left frame thickness.
            std::uint32_t top = 0;    ///< Top frame thickness.
            std::uint32_t right = 0;  ///< Right frame thickness.
            std::uint32_t bottom = 0; ///< Bottom frame thickness.
            /// @brief Compares every inset.
            friend constexpr bool operator==(Insets, Insets) noexcept = default;
        };

        /// @brief Physical-pixel scale relative to baseline logical units.
        struct ContentScale
        {
            float x = 1.0F; ///< Horizontal scale.
            float y = 1.0F; ///< Vertical scale.
            /// @brief Compares both scale components.
            friend constexpr bool operator==(ContentScale, ContentScale) noexcept = default;
        };

        /// @brief Effective dots per inch reported for content.
        struct Dpi
        {
            float x = 0.0F; ///< Horizontal effective DPI.
            float y = 0.0F; ///< Vertical effective DPI.
            /// @brief Compares both DPI components.
            friend constexpr bool operator==(Dpi, Dpi) noexcept = default;
        };

        /// @brief Optional logical client-size constraints.
        struct SizeLimits
        {
            std::optional<LogicalSize> minimum; ///< Minimum client extent when present.
            std::optional<LogicalSize> maximum; ///< Maximum client extent when present.
            /// @brief Compares both optional constraints.
            friend bool operator==(const SizeLimits &, const SizeLimits &) noexcept = default;
        };

        /// @brief Positive width-to-height ratio.
        struct AspectRatio
        {
            std::uint32_t numerator = 1;   ///< Ratio numerator.
            std::uint32_t denominator = 1; ///< Ratio denominator.
            /// @brief Compares numerator and denominator.
            friend constexpr bool operator==(AspectRatio, AspectRatio) noexcept = default;
        };

        /// @brief Top-level window mode.
        enum class WindowMode
        {
            Windowed,             ///< Ordinary desktop window.
            BorderlessFullscreen, ///< Monitor-sized without changing the display mode.
            ExclusiveFullscreen   ///< Monitor-sized with an exclusive display mode.
        };

        /// @brief Portable native-resource lifecycle state.
        enum class LifetimeState
        {
            Closed,                         ///< No retained native or pending-finalization state.
            Open,                           ///< A live native Window exists.
            NativeDestroyedPendingFinalize ///< Native destruction was unexpected; events and cached state remain readable.
        };

        /// @brief Native presentation state independent from WindowMode.
        enum class PresentationState
        {
            Normal,    ///< Neither minimized nor maximized.
            Minimized, ///< Minimized.
            Maximized  ///< Maximized.
        };

        /// @brief Non-client decoration policy.
        enum class DecorationMode
        {
            System,     ///< Platform-provided frame and title bar.
            Borderless, ///< No visible platform frame.
            Custom      ///< Client-rendered chrome with declarative native hit testing.
        };

        /// @brief Initial desktop placement policy.
        enum class PlacementKind
        {
            PlatformDefault, ///< Let the platform choose.
            Centered,        ///< Center on a requested or primary monitor.
            Explicit         ///< Use Placement::position as client top-left.
        };

        /// @brief Initial placement request.
        struct Placement
        {
            PlacementKind kind = PlacementKind::PlatformDefault; ///< Placement policy.
            MonitorId monitor;                                   ///< Center target; invalid selects primary.
            ScreenPosition position;                             ///< Physical client top-left for Explicit.
        };

        /// @brief Physical monitor display mode.
        struct DisplayMode
        {
            PixelSize resolution;                    ///< Physical mode resolution.
            std::uint32_t refreshRateMillihertz = 0; ///< Thousandths of a hertz.
            std::uint16_t bitsPerPixel = 0;          ///< Color depth.
            bool interlaced = false;                 ///< Whether the mode is interlaced.
            /// @brief Compares every physical mode property.
            friend constexpr bool operator==(DisplayMode, DisplayMode) noexcept = default;
        };

        /// @brief Requested top-level mode and optional exclusive display mode.
        struct ModeRequest
        {
            WindowMode mode = WindowMode::Windowed; ///< Requested mode.
            MonitorId monitor;                      ///< Target; invalid selects current or primary.
            std::optional<DisplayMode> displayMode; ///< Exact exclusive mode only.
        };

        /// @brief Cached details for a non-windowed mode.
        struct FullscreenInfo
        {
            MonitorId monitor;                      ///< Active target, or invalid while windowed.
            std::optional<DisplayMode> displayMode; ///< Active exclusive mode.
            bool exactDisplayMode = false;          ///< Whether the exact mode was applied.
            bool suspended = false;                 ///< Whether exclusive mode is suspended.
        };

        /// @brief Availability of standard system window controls.
        struct WindowControls
        {
            bool closable = true;    ///< Native close action availability.
            bool minimizable = true; ///< Minimize availability.
            bool maximizable = true; ///< Maximize availability.
            /// @brief Compares all control-availability flags.
            friend constexpr bool operator==(WindowControls, WindowControls) noexcept = default;
        };

        /// @brief Behavior applied when the effective monitor DPI changes.
        enum class DpiResizePolicy
        {
            PreserveLogicalClientSize,  ///< Preserve logical extent and change framebuffer pixels.
            PreservePhysicalClientSize  ///< Preserve framebuffer pixels and recalculate logical extent.
        };

        /// @brief Window-owned cursor policy.
        enum class CursorMode
        {
            Normal,         ///< Visible and unconstrained.
            Hidden,         ///< Hidden over the client and unconstrained.
            Confined,       ///< Visible and confined while focused.
            HiddenConfined, ///< Hidden and confined while focused.
            Relative        ///< Hidden, confined, and centered for relative input.
        };

        /// @brief Standard system cursor shape.
        enum class CursorShape
        {
            Arrow,
            Text,
            Crosshair,
            Hand,
            Help,
            Wait,
            Progress,
            Move,
            ResizeAll,
            ResizeHorizontal,
            ResizeVertical,
            ResizeDiagonalNorthWestSouthEast,
            ResizeDiagonalNorthEastSouthWest,
            NotAllowed
        };

        /// @brief Pointer hit-test policy.
        enum class PointerInputMode
        {
            Normal,        ///< Accept throughout the client.
            ClickThrough,  ///< Pass the complete native Window through to underlying desktop windows.
            AcceptRegions, ///< Accept only inside configured rectangles.
            IgnoreRegions  ///< Pass through inside configured rectangles.
        };

        /// @brief Optional platform backdrop treatment.
        enum class BackdropEffect
        {
            None,            ///< No system backdrop.
            Automatic,       ///< Let the system choose for this Window.
            MainWindow,      ///< Backdrop intended for a long-lived main Window.
            TransientWindow, ///< Backdrop intended for a transient Window.
            TabbedWindow     ///< Backdrop intended for a tabbed title-bar treatment.
        };

        /// @brief Complete initial top-level window description.
        struct Description
        {
            std::string title = "GameWIP";                                ///< UTF-8; invalid text and embedded NUL are rejected.
            LogicalSize clientSize{1280, 720};                            ///< Positive initial logical client extent.
            DpiResizePolicy dpiResizePolicy = DpiResizePolicy::PreserveLogicalClientSize; ///< Future DPI-transition policy.
            Placement placement;                                          ///< Initial desktop placement.
            ModeRequest mode;                                             ///< Initial window/fullscreen mode.
            PresentationState presentation = PresentationState::Normal;   ///< Initial presentation.
            DecorationMode decoration = DecorationMode::System;           ///< Initial decorations.
            WindowControls controls;                                      ///< Initial standard controls.
            SizeLimits sizeLimits;                                        ///< Initial client-size constraints.
            std::optional<AspectRatio> aspectRatio;                       ///< Optional client ratio.
            WindowId owner;                                               ///< Optional existing same-thread owner.
            CursorMode cursorMode = CursorMode::Normal;                   ///< Initial cursor mode.
            CursorShape cursorShape = CursorShape::Arrow;                 ///< Initial cursor shape.
            PointerInputMode pointerInputMode = PointerInputMode::Normal; ///< Initial pointer policy.
            BackdropEffect backdropEffect = BackdropEffect::None;         ///< Initial backdrop.
            float opacity = 1.0F;                                         ///< Inclusive range [0, 1].
            bool visible = false;                                         ///< Show after successful hidden creation.
            bool requestFocus = false;                                    ///< Request focus after initial state.
            bool resizable = true;                                        ///< Enable user resizing.
            bool focusable = true;                                        ///< Permit native activation.
            bool userInteractionEnabled = true;                           ///< Enable native interaction.
            bool alwaysOnTop = false;                                     ///< Stay above ordinary windows.
            bool acceptsFileDrops = false;                                ///< Enable file-drop delivery.
            bool transparentFramebuffer = false;                          ///< Enable composited client transparency.
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
            PointerInputMode mode = PointerInputMode::Normal; ///< Pointer policy.
            std::span<const LogicalRect> regions;             ///< Logical client rectangles.
        };

        /// @brief Call-scoped tightly packed RGBA8 icon image.
        struct IconImageView
        {
            PixelSize size;                   ///< Positive physical image extent.
            std::span<const std::byte> rgba8; ///< Exactly width * height * 4 bytes.
        };

        /// @brief Snapshot describing one monitor.
        struct MonitorInfo
        {
            MonitorId id;                                ///< Opaque process-local monitor identity.
            std::string name;                            ///< UTF-8 display name.
            ScreenRect bounds;                           ///< Physical virtual-screen bounds.
            ScreenRect workArea;                         ///< Physical virtual-screen working area.
            ContentScale contentScale;                   ///< Physical-to-logical scale.
            Dpi effectiveDpi;                            ///< Effective monitor DPI.
            std::uint32_t physicalWidthMillimeters = 0;  ///< Physical width or zero.
            std::uint32_t physicalHeightMillimeters = 0; ///< Physical height or zero.
            bool primary = false;                        ///< Whether this is the primary monitor.
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
            CursorConfinement,
            RelativeCursor,
            CursorWarping,
            FileDrop,
            ExclusiveFullscreen,
            OcclusionReporting,
            Count ///< Enumerator count; not a capability.
        };

        /// @brief Backend capability flags and declarative-region limits.
        struct Capabilities
        {
            std::uint64_t flags = 0;                      ///< Bits indexed by Capability.
            std::uint32_t maximumCustomChromeRegions = 0; ///< Maximum copied chrome rectangles.
            std::uint32_t maximumPointerInputRegions = 0; ///< Maximum copied pointer rectangles.

            /// @brief Returns whether a known capability bit is present.
            [[nodiscard]] constexpr bool supports(Capability capability) const noexcept
            {
                const auto index = static_cast<std::uint8_t>(capability);
                return index < static_cast<std::uint8_t>(Capability::Count) && (flags & (std::uint64_t{1} << index)) != 0;
            }
        };

        /// @brief Origin of a close request.
        enum class CloseRequestSource
        {
            User,         ///< Native user action.
            Programmatic, ///< Window::requestClose().
            System        ///< Thread or operating-system request.
        };

        /// @brief Sticky request that the application explicitly close the window.
        struct CloseRequestedEvent
        {
            CloseRequestSource source = CloseRequestSource::User; ///< Request source.
        };
        /// @brief Unexpected native destruction observed before controlled finalization.
        struct ClosedEvent
        {
        };
        /// @brief Visibility transition.
        struct VisibilityChangedEvent
        {
            bool visible = false; ///< New visibility.
        };
        /// @brief Logical client-position transition.
        struct MovedEvent
        {
            ScreenPosition position; ///< New physical client top-left.
        };
        /// @brief Logical client-size transition.
        struct ClientSizeChangedEvent
        {
            LogicalSize size; ///< New client extent.
        };
        /// @brief Physical drawable-size transition.
        struct FramebufferSizeChangedEvent
        {
            PixelSize size; ///< New physical extent.
        };
        /// @brief Keyboard-focus transition.
        struct FocusChangedEvent
        {
            bool focused = false; ///< New focus state.
        };
        /// @brief Presentation-state transition.
        struct PresentationStateChangedEvent
        {
            PresentationState state = PresentationState::Normal; ///< New state.
        };

        /// @brief DPI/content-scale transition.
        struct ContentScaleChangedEvent
        {
            ContentScale previousScale; ///< Previous scale.
            ContentScale scale;         ///< New scale.
            Dpi previousDpi;            ///< Previous DPI.
            Dpi dpi;                    ///< New DPI.
            PixelSize framebufferSize;  ///< New physical client extent.
        };

        /// @brief Current-monitor transition.
        struct MonitorChangedEvent
        {
            MonitorId previousMonitor; ///< Previous monitor.
            MonitorId monitor;         ///< New monitor.
        };

        /// @brief Window-mode transition.
        struct ModeChangedEvent
        {
            WindowMode previousMode = WindowMode::Windowed; ///< Previous mode.
            WindowMode mode = WindowMode::Windowed;         ///< New mode.
        };

        /// @brief Owner relationship transition.
        struct OwnerChangedEvent
        {
            WindowId previousOwner; ///< Previous owner.
            WindowId owner;         ///< New owner.
        };

        /// @brief Notification that desktop display configuration changed.
        struct DisplayConfigurationChangedEvent
        {
        };
        /// @brief Cursor entry/leave transition.
        struct CursorPresenceChangedEvent
        {
            bool insideClientArea = false; ///< New presence state.
        };

        /// @brief One native file-drop operation and its owning payload.
        struct FilesDroppedEvent
        {
            std::optional<LogicalPosition> clientPosition; ///< Logical drop position when available.
            std::vector<FileSystem::Types::Path> paths; ///< Native FileSystem paths.
        };

        /// @brief Backend-reported occlusion transition.
        struct OcclusionChangedEvent
        {
            bool occluded = false; ///< New occlusion state.
        };
        /// @brief Native request for client redraw.
        struct RedrawRequestedEvent
        {
        };

        /// @brief Tagged payload for one portable Window event.
        /// @note Explicit close tears down event storage without an event. Unexpected native
        /// destruction retains state and queues ClosedEvent for controlled finalization.
        using EventData = std::variant<
            CloseRequestedEvent,
            ClosedEvent,
            VisibilityChangedEvent,
            MovedEvent,
            ClientSizeChangedEvent,
            FramebufferSizeChangedEvent,
            FocusChangedEvent,
            PresentationStateChangedEvent,
            ContentScaleChangedEvent,
            MonitorChangedEvent,
            ModeChangedEvent,
            OwnerChangedEvent,
            DisplayConfigurationChangedEvent,
            CursorPresenceChangedEvent,
            FilesDroppedEvent,
            OcclusionChangedEvent,
            RedrawRequestedEvent>;

        /// @brief One queued event with a per-open monotonic sequence.
        struct Event
        {
            std::uint64_t sequence = 0; ///< Strictly increasing retained-event identity.
            EventData data;             ///< Typed payload.

            /// @brief Returns the payload when it has EventType, otherwise nullptr.
            template <typename EventType> [[nodiscard]] EventType *getIf() noexcept
            {
                return std::get_if<EventType>(&data);
            }

            /// @brief Returns the payload when it has EventType, otherwise nullptr.
            template <typename EventType> [[nodiscard]] const EventType *getIf() const noexcept
            {
                return std::get_if<EventType>(&data);
            }
        };

        /// @brief Ownership kind of fixed event storage.
        enum class EventStorageKind
        {
            Internal, ///< Storage allocated once by open().
            External  ///< Storage borrowed exclusively until close().
        };

        /// @brief Snapshot of one Window event queue.
        struct EventQueueInfo
        {
            EventStorageKind storage = EventStorageKind::Internal; ///< Storage ownership.
            std::size_t capacity = 0;                              ///< Fixed slot count.
            std::size_t pendingEvents = 0;                         ///< Currently retained events.
            std::uint64_t droppedEvents = 0;                       ///< Cumulative drops for this open lifetime.
        };

        /// @brief Result of one calling-thread native event pump.
        struct EventPumpResult
        {
            IO::Types::Status status;        ///< Pump status; earlier events may still be queued.
            std::size_t eventsQueued = 0;    ///< Routed insertions or coalesces in this call.
            std::uint64_t eventsDropped = 0; ///< New drops in this call.
            bool timedOut = false;           ///< Whether a finite wait elapsed without input.
        };

        /// @brief Capability query result.
        struct CapabilitiesResult
        {
            IO::Types::Status status;  ///< Query status.
            Capabilities capabilities; ///< Meaningful on success.
        };

        /// @brief Physical screen-position query/conversion result.
        struct ScreenPositionResult
        {
            IO::Types::Status status; ///< Operation status.
            ScreenPosition position;  ///< Meaningful on success.
        };

        /// @brief Logical client-position conversion result.
        struct LogicalPositionResult
        {
            IO::Types::Status status;
            LogicalPosition position;
        };

        /// @brief Materialized monitor enumeration result.
        struct MonitorListResult
        {
            IO::Types::Status status;          ///< Enumeration status.
            std::vector<MonitorInfo> monitors; ///< Complete on success.
        };

        /// @brief Single monitor query result.
        struct MonitorInfoResult
        {
            IO::Types::Status status; ///< Query status.
            MonitorInfo monitor;      ///< Meaningful on success.
        };

        /// @brief Materialized display-mode enumeration result.
        struct DisplayModeListResult
        {
            IO::Types::Status status;              ///< Enumeration status.
            std::vector<DisplayMode> displayModes; ///< Complete on success.
        };

        /// @brief Single display-mode query result.
        struct DisplayModeResult
        {
            IO::Types::Status status; ///< Query status.
            DisplayMode displayMode;  ///< Meaningful on success.
        };
    } // namespace Types

    /// @brief Returns cached backend/environment capabilities.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::CapabilitiesResult getCapabilities() noexcept;
    /// @brief Returns whether the backend advertises a known capability.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT bool supports(Types::Capability capability) noexcept;
    /// @brief Non-blockingly pumps events for every Window owned by the calling thread.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::EventPumpResult pollEvents() noexcept;
    /// @brief Waits for and pumps calling-thread Window events.
    /// @param timeout Zero, finite non-negative, or kWaitForever.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::EventPumpResult waitEvents(std::chrono::milliseconds timeout = kWaitForever) noexcept;
    /// @brief Enumerates current desktop monitors.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::MonitorListResult getMonitors() noexcept;
    /// @brief Returns the current primary monitor.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::MonitorInfoResult getPrimaryMonitor() noexcept;
    /// @brief Resolves a current process-local monitor identity.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::MonitorInfoResult getMonitor(Types::MonitorId monitor) noexcept;
    /// @brief Enumerates supported display modes for a monitor.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::DisplayModeListResult getDisplayModes(Types::MonitorId monitor) noexcept;
    /// @brief Returns the active display mode for a monitor.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::DisplayModeResult getCurrentDisplayMode(Types::MonitorId monitor) noexcept;
    /// @brief Returns the connected target's DisplayConfig preferred/native mode.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::DisplayModeResult getPreferredDisplayMode(Types::MonitorId monitor) noexcept;

    /// @brief Non-copyable, non-movable RAII owner of one native top-level desktop window.
    /// @details Except wakeEventWait(), open-object operations require the opening thread. Cached
    /// getters do not query the backend and are not synchronized. The stable object address and
    /// thread affinity last until explicit close or destruction.
    class GAMEWIP_WINDOW_EXPORT Window final
    {
    public:
        /// @brief Constructs a closed object without allocating.
        Window() noexcept;
        Window(const Window &) = delete;
        Window &operator=(const Window &) = delete;
        Window(Window &&) = delete;
        Window &operator=(Window &&) = delete;
        /// @brief Performs non-throwing cleanup, transferring wrong-thread native work to the owner dispatcher.
        ~Window() noexcept;

        /// @brief Opens with kDefaultEventQueueCapacity internal slots.
        [[nodiscard]] IO::Types::Status open(const Types::Description &description = {}) noexcept;
        /// @brief Opens with a positive fixed internal capacity.
        [[nodiscard]] IO::Types::Status open(const Types::Description &description, std::size_t eventQueueCapacity) noexcept;
        /// @brief Opens with non-empty caller-owned slots borrowed exclusively until close().
        /// @warning Storage must remain alive and unmoved until close().
        [[nodiscard]] IO::Types::Status open(const Types::Description &description, std::span<Types::Event> eventStorage) noexcept;

        /// @brief Returns whether a live native Window exists.
        [[nodiscard]] bool isOpen() const noexcept;
        /// @brief Returns open, closed, or unexpected-native-destruction pending-finalize state.
        [[nodiscard]] Types::LifetimeState lifetimeState() const noexcept;
        /// @brief Destroys or finalizes native state and releases event storage; repeated calls succeed.
        [[nodiscard]] IO::Types::Status close() noexcept;

        /// @brief Returns the current open-lifetime identity or an invalid ID.
        [[nodiscard]] Types::WindowId id() const noexcept;
        /// @brief Returns the cached owner identity or invalid for no owner.
        [[nodiscard]] Types::WindowId ownerId() const noexcept;
        /// @brief Returns whether the open object belongs to the calling thread.
        [[nodiscard]] bool isOwnedByCurrentThread() const noexcept;
        /// @brief Returns backend support adjusted for the current window state.
        [[nodiscard]] bool supports(Types::Capability capability) const noexcept;
        /// @brief Changes to an existing same-thread owner, or invalid to remove ownership.
        [[nodiscard]] IO::Types::Status setOwner(Types::WindowId owner) noexcept;

        /// @brief Returns sticky close-request state.
        [[nodiscard]] bool closeRequested() const noexcept;
        /// @brief Sets sticky state and queues a Programmatic request where possible.
        [[nodiscard]] IO::Types::Status requestClose() noexcept;
        /// @brief Clears an unaccepted sticky close request.
        [[nodiscard]] IO::Types::Status clearCloseRequest() noexcept;

        /// @brief Moves the oldest queued event into outEvent.
        [[nodiscard]] bool popEvent(Types::Event &outEvent) noexcept;
        /// @brief Moves up to destination.size() oldest events in FIFO order.
        [[nodiscard]] std::size_t popEvents(std::span<Types::Event> destination) noexcept;
        /// @brief Discards all pending events without clearing sticky state or drop counters.
        void clearEvents() noexcept;
        /// @brief Returns current fixed-storage statistics.
        [[nodiscard]] Types::EventQueueInfo eventQueueInfo() const noexcept;
        /// @brief Resets the cumulative dropped-event count.
        void clearDroppedEventCount() noexcept;
        /// @brief Wakes waitEvents() on the owning thread; intentionally cross-thread safe.
        [[nodiscard]] IO::Types::Status wakeEventWait() const noexcept;

        /// @brief Returns a view invalidated by setTitle(), close(), or destruction.
        [[nodiscard]] std::string_view title() const noexcept;
        [[nodiscard]] Types::LogicalSize clientSize() const noexcept;                 ///< Cached logical client extent.
        [[nodiscard]] Types::PixelSize framebufferSize() const noexcept;              ///< Cached physical client extent.
        [[nodiscard]] Types::ScreenPosition clientPosition() const noexcept;          ///< Cached physical screen position.
        [[nodiscard]] Types::ScreenRect frameRect() const noexcept;                   ///< Cached physical outer frame.
        [[nodiscard]] Types::Insets frameInsets() const noexcept;                     ///< Cached logical frame insets.
        [[nodiscard]] Types::ContentScale contentScale() const noexcept;              ///< Cached content scale.
        [[nodiscard]] Types::Dpi effectiveDpi() const noexcept;                       ///< Cached effective DPI.
        [[nodiscard]] Types::DpiResizePolicy dpiResizePolicy() const noexcept;        ///< Cached future DPI-transition policy.
        [[nodiscard]] Types::MonitorId currentMonitor() const noexcept;               ///< Cached monitor identity.
        [[nodiscard]] Types::WindowMode mode() const noexcept;                        ///< Cached window mode.
        [[nodiscard]] Types::FullscreenInfo fullscreenInfo() const noexcept;          ///< Cached fullscreen detail.
        [[nodiscard]] Types::PresentationState presentationState() const noexcept;    ///< Cached presentation.
        [[nodiscard]] Types::DecorationMode decorationMode() const noexcept;          ///< Cached decorations.
        [[nodiscard]] Types::WindowControls windowControls() const noexcept;          ///< Cached controls.
        [[nodiscard]] Types::SizeLimits sizeLimits() const noexcept;                  ///< Cached size limits.
        [[nodiscard]] std::optional<Types::AspectRatio> aspectRatio() const noexcept; ///< Cached ratio.
        [[nodiscard]] Types::CursorMode cursorMode() const noexcept;                  ///< Cached cursor mode.
        [[nodiscard]] Types::CursorShape cursorShape() const noexcept;                ///< Cached cursor shape.
        [[nodiscard]] Types::PointerInputMode pointerInputMode() const noexcept;      ///< Cached pointer policy.
        [[nodiscard]] std::size_t pointerInputRegionCount() const noexcept;           ///< Copied pointer rectangles.
        [[nodiscard]] Types::BackdropEffect backdropEffect() const noexcept;          ///< Cached backdrop.
        [[nodiscard]] float opacity() const noexcept;                                 ///< Cached opacity.
        [[nodiscard]] bool isVisible() const noexcept;                                ///< Cached visibility.
        [[nodiscard]] bool isFocused() const noexcept;                                ///< Cached focus.
        [[nodiscard]] bool isMinimized() const noexcept;                              ///< Cached minimized state.
        [[nodiscard]] bool isMaximized() const noexcept;                              ///< Cached maximized state.
        [[nodiscard]] bool isOccluded() const noexcept;                               ///< Cached occlusion state.
        [[nodiscard]] bool isCursorInside() const noexcept;                           ///< Cached cursor presence.
        [[nodiscard]] bool isResizable() const noexcept;                              ///< Cached resizability.
        [[nodiscard]] bool isFocusable() const noexcept;                              ///< Cached focusability.
        [[nodiscard]] bool isUserInteractionEnabled() const noexcept;                 ///< Cached interaction state.
        [[nodiscard]] bool isAlwaysOnTop() const noexcept;                            ///< Cached topmost state.
        [[nodiscard]] bool acceptsFileDrops() const noexcept;                         ///< Cached drop state.
        [[nodiscard]] bool hasTransparentFramebuffer() const noexcept;                ///< Creation-time transparency.

        /// @brief Replaces the copied UTF-8 title after validation and native conversion.
        [[nodiscard]] IO::Types::Status setTitle(std::string_view utf8Title) noexcept;
        /// @brief Copies one or more tightly packed RGBA8 icon candidates into native icons.
        [[nodiscard]] IO::Types::Status setIcon(std::span<const Types::IconImageView> images) noexcept;
        /// @brief Restores the platform-default window icon.
        [[nodiscard]] IO::Types::Status clearIcon() noexcept;
        /// @brief Changes the positive logical client extent within current limits.
        [[nodiscard]] IO::Types::Status setClientSize(Types::LogicalSize size) noexcept;
        /// @brief Changes the physical virtual-screen position of the client top-left.
        [[nodiscard]] IO::Types::Status setClientPosition(Types::ScreenPosition position) noexcept;
        /// @brief Atomically requests physical placement and logical client extent.
        [[nodiscard]] IO::Types::Status setClientRect(Types::ScreenPosition position, Types::LogicalSize size) noexcept;
        /// @brief Centers the frame on monitor, or the current monitor when invalid.
        [[nodiscard]] IO::Types::Status centerOn(Types::MonitorId monitor = {}) noexcept;
        /// @brief Replaces logical client-size constraints and clamps the current size if needed.
        [[nodiscard]] IO::Types::Status setSizeLimits(const Types::SizeLimits &limits) noexcept;
        /// @brief Replaces or clears the positive interactive resize ratio.
        [[nodiscard]] IO::Types::Status setAspectRatio(std::optional<Types::AspectRatio> aspectRatio) noexcept;
        /// @brief Converts a logical client point to a physical virtual-screen point.
        [[nodiscard]] Types::ScreenPositionResult clientToScreen(Types::LogicalPosition position) const noexcept;
        /// @brief Converts a physical virtual-screen point to a logical client point.
        [[nodiscard]] Types::LogicalPositionResult screenToClient(Types::ScreenPosition position) const noexcept;
        /// @brief Selects how future DPI changes resize the client.
        [[nodiscard]] IO::Types::Status setDpiResizePolicy(Types::DpiResizePolicy policy) noexcept;
        /// @brief Makes the window visible without changing its mode.
        [[nodiscard]] IO::Types::Status show() noexcept;
        /// @brief Hides the window without destroying it.
        [[nodiscard]] IO::Types::Status hide() noexcept;
        /// @brief Requests keyboard focus subject to platform foreground policy.
        [[nodiscard]] IO::Types::Status requestFocus() noexcept;
        /// @brief Requests a taskbar/native attention indication.
        [[nodiscard]] IO::Types::Status requestAttention() noexcept;
        /// @brief Requests minimized presentation when the control is enabled.
        [[nodiscard]] IO::Types::Status minimize() noexcept;
        /// @brief Requests maximized presentation when resizing and the control are enabled.
        [[nodiscard]] IO::Types::Status maximize() noexcept;
        /// @brief Restores normal presentation from minimized or maximized state.
        [[nodiscard]] IO::Types::Status restore() noexcept;
        /// @brief Transactionally changes windowed, borderless, or exclusive mode.
        [[nodiscard]] IO::Types::Status setMode(const Types::ModeRequest &request) noexcept;
        /// @brief Enables or disables interactive resizing.
        [[nodiscard]] IO::Types::Status setResizable(bool resizable) noexcept;
        /// @brief Replaces the decoration policy and refreshes native non-client layout.
        [[nodiscard]] IO::Types::Status setDecorationMode(Types::DecorationMode mode) noexcept;
        /// @brief Replaces standard close, minimize, and maximize control availability.
        [[nodiscard]] IO::Types::Status setWindowControls(const Types::WindowControls &controls) noexcept;
        /// @brief Enables or disables native activation and focus acquisition.
        [[nodiscard]] IO::Types::Status setFocusable(bool focusable) noexcept;
        /// @brief Enables or disables native mouse and keyboard interaction.
        [[nodiscard]] IO::Types::Status setUserInteractionEnabled(bool enabled) noexcept;
        /// @brief Changes the topmost-window policy.
        [[nodiscard]] IO::Types::Status setAlwaysOnTop(bool alwaysOnTop) noexcept;
        /// @brief Sets global opacity in the inclusive range [0, 1].
        [[nodiscard]] IO::Types::Status setOpacity(float opacity) noexcept;
        /// @brief Changes the optional platform backdrop treatment.
        [[nodiscard]] IO::Types::Status setBackdropEffect(Types::BackdropEffect effect) noexcept;
        /// @brief Enables or disables native file-drop acceptance.
        [[nodiscard]] IO::Types::Status setFileDropEnabled(bool enabled) noexcept;
        /// @brief Copies declarative custom-chrome rectangles for native hit testing.
        [[nodiscard]] IO::Types::Status setCustomChromeLayout(const Types::CustomChromeLayout &layout) noexcept;
        /// @brief Removes all copied custom-chrome rectangles.
        [[nodiscard]] IO::Types::Status clearCustomChromeLayout() noexcept;
        /// @brief Copies a pointer hit-test mode and its logical client rectangles.
        [[nodiscard]] IO::Types::Status setPointerInputLayout(const Types::PointerInputLayout &layout) noexcept;
        /// @brief Replaces the visible, hidden, confined, or relative cursor policy.
        [[nodiscard]] IO::Types::Status setCursorMode(Types::CursorMode mode) noexcept;
        /// @brief Replaces the standard cursor shape used over the client area.
        [[nodiscard]] IO::Types::Status setCursorShape(Types::CursorShape shape) noexcept;
        /// @brief Warps the cursor to a logical client position.
        [[nodiscard]] IO::Types::Status setCursorPosition(Types::LogicalPosition clientPosition) noexcept;
        /// @brief Queries the current cursor position in logical client coordinates.
        [[nodiscard]] Types::LogicalPositionResult cursorPosition() const noexcept;

    private:
        friend struct Detail::WindowAccess;
        std::unique_ptr<Detail::WindowState> state_;
    };
} // namespace GameWIP::Window
