#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace GameWIP::Input
{
    class InputState;
}

namespace GameWIP::Platform::Win32
{
    inline constexpr std::size_t defaultWindowEventQueueCapacity = 256; // Default cap for queued window events.

    namespace Internal
    {
        struct WindowMessageAccess; // Lets the Win32 callback forward native messages into private Window handlers.
    }

    /// @brief Display mode used by the Win32 window.
    enum class WindowMode
    {
        Windowed,             // Decorated window with a normal border and title bar.
        BorderlessFullscreen, // Borderless window covering the monitor without changing display mode.
        Fullscreen            // Exclusive fullscreen mode that changes the display mode.
    };

    /// @brief Combined cursor visibility and confinement policy.
    enum class CursorMode
    {
        FreeVisible,     // Cursor is visible and unconstrained.
        FreeHidden,      // Cursor is hidden and unconstrained.
        ConfinedVisible, // Cursor is visible and clipped to the client area.
        ConfinedHidden   // Cursor is hidden and clipped to the client area.
    };

    /// @brief Engine-level role that controls allowed window behavior.
    enum class WindowRole
    {
        MainGame,          // Primary game window; owns gameplay input and fullscreen.
        SecondaryGameView, // Render-only game view for extra cameras or monitors.
        Tool,              // Interactive editor/tool window.
        Debug              // Render-only diagnostic window.
    };

    /// @brief Result code returned by window operations.
    enum class WindowResult
    {
        Success,                  // The operation completed successfully.
        NotCreated,               // The operation needs a native window, but none exists.
        InvalidDescription,       // The window description is not usable.
        InvalidSize,              // A requested width or height is zero or negative.
        InvalidMonitor,           // A monitor argument is empty or stale.
        InvalidDisplayMode,       // A display mode is incomplete or unsupported by the target monitor.
        Win32CallFailed,          // A Win32 API call failed; check getLastWin32Error().
        MissingWindowedPlacement, // Windowed placement was needed but was never saved.
        MissingDisplayMode,       // Display-mode state was needed but was never saved.
        ModeChangeFailed,         // A window-mode transition failed.
        OperationNotAllowed       // The requested operation is blocked by the window role.
    };

    /// @brief Native data needed by platform-specific render/input backends.
    struct WindowInfo
    {
        void *instance = nullptr; // Non-owning HINSTANCE stored as an opaque pointer.
        void *handle = nullptr;   // Non-owning HWND stored as an opaque pointer.
    };

    /// @brief Queued window event type.
    enum class WindowEventType
    {
        CloseRequested, // The OS or user requested the window to close.
        Destroyed,      // The native window was destroyed.
        Resized,        // The client area changed size.
        Moved,          // The window moved on screen.
        Focused,        // The window gained focus.
        LostFocus,      // The window lost focus.
        Minimized,      // The window became minimized.
        Maximized,      // The window became maximized.
        Restored,       // The window returned from minimized state.
        ModeChanged,    // The engine window mode changed.
        MonitorChanged, // The window moved to a different monitor.
        DisplayChanged, // The monitor or display-mode configuration changed.
        DpiChanged,     // The window DPI changed.
        CursorEntered,  // The cursor entered the client area.
        CursorLeft,     // The cursor left the client area.
        FileDropped,    // A file was dropped onto the window.
        Suspended,      // The app lost foreground activation.
        Resumed,        // The app gained foreground activation.
        Occluded,       // The window became hidden/minimized; true present occlusion should come from the renderer.
        Visible         // The window became visible again after being hidden/occluded.
    };

    /// @brief Data for one queued window event.
    struct WindowEvent
    {
        WindowEventType type = WindowEventType::Resized; // Kind of event.
        int width = 0;                                   // Client width for size-related events.
        int height = 0;                                  // Client height for size-related events.
        int x = 0;                                       // Screen x position for move-related events.
        int y = 0;                                       // Screen y position for move-related events.
        unsigned int dpi = 0;                            // DPI value for DPI-related events.
        WindowMode mode = WindowMode::Windowed;          // Window mode for mode-related events.
        std::string filePath;                            // UTF-8 path for file drop events.
    };

    /// @brief Configuration used when creating a native Win32 window.
    struct WindowDescription
    {
        std::string title;                                                // Initial window title.
        int width = 0;                                                    // Initial client-area width.
        int height = 0;                                                   // Initial client-area height.
        WindowMode mode = WindowMode::Windowed;                           // Initial window mode.
        WindowRole role = WindowRole::MainGame;                           // Role used for input and fullscreen policy.
        bool resizable = true;                                            // Whether the user can resize the window.
        std::size_t eventQueueCapacity = defaultWindowEventQueueCapacity; // Maximum queued window events; must be positive.
    };

    /// @brief Simple rectangle type used by native monitor information.
    struct Rect
    {
        int left = 0;   // Left screen coordinate.
        int top = 0;    // Top screen coordinate.
        int right = 0;  // Right screen coordinate.
        int bottom = 0; // Bottom screen coordinate.
    };

    /// @brief Describes one connected monitor.
    struct MonitorInfo
    {
        void *handle = nullptr; // HMONITOR stored as an opaque pointer.
        Rect workArea = {};     // Usable area excluding taskbars and docked bars.
        Rect monitorArea = {};  // Full monitor rectangle.
        bool isPrimary = false; // Whether this is the primary monitor.
        std::string deviceName; // Win32 display device name encoded as UTF-8.
    };

    /// @brief Describes one supported display mode.
    struct DisplayMode
    {
        int width = 0;        // Display width in physical pixels.
        int height = 0;       // Display height in physical pixels.
        int refreshRate = 0;  // Refresh rate in hertz.
        int bitsPerPixel = 0; // Color depth in bits per pixel.
    };

    /// @brief Native Win32 window wrapper.
    class Window
    {
    public:
        // Lifecycle

        /// @brief Creates an empty Window wrapper with no native handle.
        Window() = default;

        /// @brief Window is non-copyable.
        Window(const Window &) = delete;

        /// @brief Window is non-copyable.
        Window &operator=(const Window &) = delete;

        /// @brief Window is non-movable.
        Window(Window &&) = delete;

        /// @brief Window is non-movable.
        Window &operator=(Window &&) = delete;

        /// @brief Destroys the native window.
        ~Window();

        /// @brief Creates the native Win32 window.
        /// @param description Window title and requested size.
        /// @return Result code for the create operation.
        WindowResult create(const WindowDescription &description);

        /// @brief Destroys the native window.
        /// @return Result code for the destroy operation.
        WindowResult destroy();

        // Events

        /// @brief Processes pending Win32 messages.
        /// @param inputState Input state updated from forwarded input messages.
        void pollEvents(Input::InputState &inputState);

        /// @brief Pops the oldest pending window event.
        /// @param outEvent Receives the event when one exists.
        /// @return True if an event was popped.
        bool popEvent(WindowEvent &outEvent);

        /// @brief Clears all pending window events.
        void clearEvents();

        /// @brief Returns whether the program has requested shutdown.
        /// @return True if the game loop should exit.
        bool shouldClose() const;

        /// @brief Marks the window as wanting to close.
        void requestClose();

        // Size and state

        /// @brief Returns current client-area width.
        /// @return The client-area width.
        int getClientWidth() const;

        /// @brief Returns current client-area height.
        /// @return The client-area height.
        int getClientHeight() const;

        /// @brief Returns the window DPI.
        /// @return Window DPI, or 96 if not created.
        unsigned int getDpi() const;

        /// @brief Returns whether the client area size changed.
        /// @return True if the size changed.
        bool wasClientSizeChanged() const;

        /// @brief Clears the client-size-changed flag.
        void clearClientSizeChanged();

        /// @brief Returns native window data.
        /// @return Native window info, or null pointers if not created.
        WindowInfo getInfo() const;

        /// @brief Returns whether the window is focused.
        /// @return True if focused.
        bool isFocused() const;

        /// @brief Returns whether the window is minimized.
        /// @return True if minimized.
        bool isMinimized() const;

        // Size and title

        /// @brief Sets the client-area size.
        /// @param width New width.
        /// @param height New height.
        /// @note In exclusive fullscreen, a previously requested display mode remains authoritative; use setFullscreenDisplayMode to change it.
        /// @return Result code for the operation.
        WindowResult setClientSize(int width, int height);

        /// @brief Sets the window title.
        /// @param title New title.
        /// @return Result code for the operation.
        WindowResult setTitle(std::string_view title);

        /// @brief Sets the minimum client-area size.
        /// @param width Minimum width.
        /// @param height Minimum height.
        void setMinClientSize(int width, int height);

        /// @brief Sets the maximum client-area size.
        /// @param width Maximum width.
        /// @param height Maximum height.
        void setMaxClientSize(int width, int height);

        /// @brief Returns the client-area size constraints.
        /// @param outMinWidth Minimum width.
        /// @param outMinHeight Minimum height.
        /// @param outMaxWidth Maximum width.
        /// @param outMaxHeight Maximum height.
        void getClientSizeConstraints(int &outMinWidth, int &outMinHeight, int &outMaxWidth, int &outMaxHeight) const;

        // Window mode

        /// @brief Returns the current window mode.
        /// @return The window mode.
        WindowMode getMode() const;

        /// @brief Returns the window role.
        /// @return The role assigned at creation time.
        WindowRole getRole() const;

        /// @brief Sets the mode of the window.
        /// @param mode The new mode.
        /// @return Result code for the mode operation.
        WindowResult setMode(WindowMode mode);

        // Errors

        /// @brief Returns the last explicit operation result.
        /// @return Last result code.
        WindowResult getLastResult() const;

        /// @brief Returns the last Win32 error from an explicit operation.
        /// @return Win32 error code, or 0 if none.
        unsigned long getLastWin32Error() const;

        /// @brief Returns whether a passive handler recorded an error.
        /// @return True if an error is available.
        bool hasAsyncError() const;

        /// @brief Returns the last passive handler result.
        /// @return Last async result code, or Success if none.
        WindowResult getLastAsyncResult() const;

        /// @brief Returns the last Win32 error from a passive handler.
        /// @return Win32 error code, or 0 if none.
        unsigned long getLastAsyncWin32Error() const;

        /// @brief Clears the stored passive message-handler error.
        void clearAsyncError();

        // Cursor

        /// @brief Shows or hides the cursor.
        /// @param visible True to show, false to hide.
        void setCursorVisible(bool visible);

        /// @brief Returns whether the cursor is visible.
        /// @return True if visible.
        bool isCursorVisible() const;

        /// @brief Confines or releases the cursor.
        /// @param confined True to confine, false to release.
        /// @return Result code for the operation.
        WindowResult setCursorConfined(bool confined);

        /// @brief Returns whether the cursor is confined.
        /// @return True if confined.
        bool isCursorConfined() const;

        /// @brief Applies a cursor visibility and confinement policy.
        /// @param mode Cursor mode to apply.
        /// @return Result code for the operation.
        WindowResult setCursorMode(CursorMode mode);

        /// @brief Returns the cursor visibility and confinement policy.
        /// @return Current cursor mode.
        CursorMode getCursorMode() const;

        // Monitor and display info

        /// @brief Returns all available monitors.
        /// @param outMonitors Monitor information.
        /// @return Result code for the query.
        WindowResult getMonitors(std::vector<MonitorInfo> &outMonitors);

        /// @brief Returns the monitor containing the window.
        /// @param outMonitor Monitor information.
        /// @return Result code for the query.
        WindowResult getCurrentMonitor(MonitorInfo &outMonitor);

        /// @brief Returns supported display modes for a monitor.
        /// @param monitor Monitor to query.
        /// @param outModes Supported display modes.
        /// @return Result code for the query.
        WindowResult getDisplayModes(const MonitorInfo &monitor, std::vector<DisplayMode> &outModes);

        /// @brief Returns the window's current display mode.
        /// @param outMode Current display mode.
        /// @return Result code for the query.
        WindowResult getCurrentDisplayMode(DisplayMode &outMode);

        /// @brief Sets the exclusive fullscreen display mode.
        /// @param monitor Target monitor.
        /// @param mode Display mode to set.
        /// @return Result code for the operation.
        WindowResult setFullscreenDisplayMode(const MonitorInfo &monitor, const DisplayMode &mode);

        /// @brief Returns the requested fullscreen display mode.
        /// @param outMonitor Fullscreen monitor.
        /// @param outMode Fullscreen display mode.
        /// @return Result code for the operation.
        WindowResult getFullscreenDisplayMode(MonitorInfo &outMonitor, DisplayMode &outMode);

    private:
        // Native bridge

        /// @brief Grants Win32 callback access to private handlers.
        friend struct Internal::WindowMessageAccess;

        /// @brief Win32-specific window state.
        struct NativeWindow;

        /// @brief Owned native window state.
        NativeWindow *nativeWindow = nullptr;

        /// @brief Last explicit operation result.
        WindowResult lastResult = WindowResult::Success;

        /// @brief Last explicit operation Win32 error.
        unsigned long lastWin32Error = 0;

        /// @brief Last passive message-handler result.
        WindowResult lastAsyncResult = WindowResult::Success;

        /// @brief Last passive message-handler Win32 error.
        unsigned long lastAsyncWin32Error = 0;

        /// @brief True if an unconsumed passive error exists.
        bool asyncErrorRecorded = false;

        // Result helpers

        /// @brief Stores and returns an operation result.
        /// @param result Result code to store.
        /// @param win32Error Optional Win32 GetLastError value.
        /// @return The result argument.
        WindowResult recordResult(WindowResult result, unsigned long win32Error = 0);

        /// @brief Stores a passive message-handler error.
        /// @param result Result code to store.
        /// @param win32Error Optional Win32 error code.
        void recordAsyncError(WindowResult result, unsigned long win32Error = 0);

        // Event helpers

        /// @brief Queues a window event.
        /// @param event Event to queue.
        void pushEvent(WindowEvent event);

        /// @brief Checks whether an event should be filtered before queueing.
        /// @param type Event type to check.
        /// @return True when the event should be suppressed.
        bool shouldSuppressEvent(WindowEventType type) const;

        // Message handlers

        /// @brief Updates cached client size after a native resize message.
        /// @param width New client-area width.
        /// @param height New client-area height.
        void handleResize(int width, int height);

        /// @brief Refreshes cursor confinement after the native window moves.
        /// @param x New screen x position.
        /// @param y New screen y position.
        void handleMove(int x, int y);

        /// @brief Updates focus state and handles fullscreen focus transitions.
        /// @param focused True when the window gained focus.
        void handleFocusChange(bool focused);

        /// @brief Updates app activation state after foreground activation changes.
        /// @param active True when the app resumed foreground activation.
        void handleActivationChange(bool active);

        /// @brief Updates minimized state after a native size message.
        /// @param minimized True when the window is minimized.
        void handleMinimizeChange(bool minimized);

        /// @brief Updates maximized state after a native size message.
        /// @param maximized True when the window is maximized.
        void handleMaximizeChange(bool maximized);

        /// @brief Updates visible/occluded state after native visibility messages.
        /// @param visible True when the window is visible.
        void handleVisibilityChange(bool visible);

        /// @brief Returns whether cursor-enter handling is needed.
        /// @return True if the cursor is not currently considered inside the client area.
        bool shouldHandleCursorEnter() const;

        /// @brief Queues a cursor-enter event.
        /// @return True if cursor entered the client area.
        bool handleCursorEnter();

        /// @brief Rolls back cursor-enter state after mouse-leave tracking fails.
        /// @param win32Error Win32 error reported by TrackMouseEvent.
        void handleCursorTrackingFailure(unsigned long win32Error);

        /// @brief Queues a cursor-left event.
        void handleCursorLeave();

        /// @brief Handles a file drop event.
        /// @param filePath UTF-8 path of dropped file.
        void handleFileDrop(std::string_view filePath);

        /// @brief Handles native window destruction.
        void handleDestroyed();

        /// @brief Updates the current monitor.
        void updateCurrentMonitor();

        /// @brief Handles a DPI change.
        /// @param dpi New DPI value.
        /// @param suggestedLeft Suggested window left.
        /// @param suggestedTop Suggested window top.
        /// @param suggestedRight Suggested window right.
        /// @param suggestedBottom Suggested window bottom.
        void handleDpiChange(unsigned int dpi, int suggestedLeft, int suggestedTop, int suggestedRight, int suggestedBottom);

        /// @brief Handles monitor or display-mode changes reported by Win32.
        void handleDisplayChange();

        /// @brief Invalidates monitor and display-mode cache.
        void invalidateMonitorCache();

        /// @brief Handles the GETMINMAXINFO message.
        /// @param minMaxInfo Pointer to MINMAXINFO structure.
        void handleGetMinMaxInfo(void *minMaxInfo);

        // Mode helpers

        /// @brief Saves the decorated window rectangle and style before fullscreen changes.
        /// @return Result code for the save operation.
        WindowResult saveWindowedPlacement();

        /// @brief Restores the desktop display mode after exclusive fullscreen.
        /// @return Result code for the restore operation.
        WindowResult restoreDisplayMode();

        /// @brief Restores the saved decorated window rectangle and style.
        /// @return Result code for the restore operation.
        WindowResult restoreWindowedPlacement();

        /// @brief Applies normal decorated window mode.
        /// @return Result code for the mode operation.
        WindowResult applyWindowedMode();

        /// @brief Applies borderless fullscreen mode.
        /// @return Result code for the mode operation.
        WindowResult applyBorderlessFullscreenMode();

        /// @brief Applies exclusive fullscreen mode.
        /// @return Result code for the mode operation.
        WindowResult applyFullscreenMode();

        /// @brief Snapshot of fullscreen mode state used for rollback.
        struct FullscreenModeSnapshot;

        /// @brief Target fullscreen mode configuration to apply.
        struct FullscreenModeTarget;

        /// @brief Captures the current fullscreen mode state for potential rollback.
        /// @return Snapshot of current fullscreen state.
        FullscreenModeSnapshot captureFullscreenModeSnapshot() const;

        /// @brief Restores a previously captured fullscreen mode snapshot.
        /// @param snapshot Snapshot to restore.
        /// @return Result code for the restore operation.
        WindowResult restoreFullscreenModeSnapshot(const FullscreenModeSnapshot &snapshot);

        /// @brief Rolls back fullscreen mode to a previous state.
        /// @param previousMode Previous window mode to restore.
        /// @param snapshot Previous fullscreen state snapshot.
        void rollbackFullscreenMode(WindowMode previousMode, const FullscreenModeSnapshot &snapshot);

        /// @brief Resolves the target fullscreen mode configuration.
        /// @param previousState Current fullscreen state snapshot.
        /// @param outTarget Receives the resolved target configuration.
        /// @return Result code for the resolution operation.
        WindowResult resolveFullscreenModeTarget(const FullscreenModeSnapshot &previousState, FullscreenModeTarget &outTarget);

        /// @brief Applies the fullscreen mode target configuration.
        /// @param target Target fullscreen mode configuration.
        /// @param previousMode Previous window mode for rollback if needed.
        /// @param previousState Previous fullscreen state snapshot for rollback if needed.
        /// @return Result code for the apply operation.
        WindowResult applyFullscreenModeTarget(const FullscreenModeTarget &target, WindowMode previousMode, const FullscreenModeSnapshot &previousState);

        /// @brief Stores the fullscreen mode target for later use.
        /// @param target Fullscreen mode target to store.
        void storeFullscreenModeTarget(const FullscreenModeTarget &target);

        // Cursor helpers

        /// @brief Releases an active cursor clip.
        /// @param outWin32Error Win32 error if release fails.
        /// @return Result code for the operation.
        WindowResult releaseCursorConfinement(unsigned long *outWin32Error = nullptr);

        /// @brief Updates cursor clipping based on window state.
        void updateCursorConfinement();
    };
}
