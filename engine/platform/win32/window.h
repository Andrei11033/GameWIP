#pragma once

#include "input/input.h"

#include <string>
#include <string_view>
#include <vector>

namespace GameWIP::Platform::Win32
{
    namespace Internal
    {
        struct WindowMessageAccess; // Lets the Win32 callback forward native messages into private Window handlers.
    }

    enum class WindowMode // Display mode used by the Win32 window.
    {
        Windowed,             // Decorated window with a normal border and title bar.
        BorderlessFullscreen, // Borderless window covering the monitor without changing display mode.
        Fullscreen            // Exclusive fullscreen mode that changes the display mode.
    };

    enum class CursorMode // Combined cursor visibility and confinement policy.
    {
        FreeVisible,     // Cursor is visible and unconstrained.
        FreeHidden,      // Cursor is hidden and unconstrained.
        ConfinedVisible, // Cursor is visible and clipped to the client area.
        ConfinedHidden   // Cursor is hidden and clipped to the client area.
    };

    enum class WindowResult // Result code returned by window operations.
    {
        Success,                  // The operation completed successfully.
        NotCreated,               // The operation needs a native window, but none exists.
        InvalidDescription,       // The window description is not usable.
        InvalidSize,              // A requested width or height is zero or negative.
        InvalidMonitor,           // A monitor argument is empty or stale.
        Win32CallFailed,          // A Win32 API call failed; check getLastWin32Error().
        MissingWindowedPlacement, // Windowed placement was needed but was never saved.
        MissingDisplayMode,       // Display-mode state was needed but was never saved.
        ModeChangeFailed          // A window-mode transition failed.
    };

    struct WindowInfo // Native data needed by platform-specific render/input backends.
    {
        void *instance = nullptr; // Non-owning HINSTANCE stored as an opaque pointer.
        void *handle = nullptr;   // Non-owning HWND stored as an opaque pointer.
    };

    enum class WindowEventType // Queued window event type.
    {
        CloseRequested, // The OS or user requested the window to close.
        Resized,        // The client area changed size.
        Moved,          // The window moved on screen.
        Focused,        // The window gained focus.
        LostFocus,      // The window lost focus.
        Minimized,      // The window became minimized.
        Restored,       // The window returned from minimized state.
        DisplayChanged, // The monitor or display-mode configuration changed.
        DpiChanged      // The window DPI changed.
    };

    struct WindowEvent // Data for one queued window event.
    {
        WindowEventType type = WindowEventType::Resized; // Kind of event.
        int width = 0;                                   // Client width for size-related events.
        int height = 0;                                  // Client height for size-related events.
        int x = 0;                                       // Screen x position for move-related events.
        int y = 0;                                       // Screen y position for move-related events.
        unsigned int dpi = 0;                            // DPI value for DPI-related events.
    };

    struct WindowDescription // Configuration used when creating a native Win32 window.
    {
        std::string title;                      // Initial window title.
        int width;                              // Initial client-area width.
        int height;                             // Initial client-area height.
        WindowMode mode = WindowMode::Windowed; // Initial window mode.
        bool resizable = true;                  // Whether the user can resize the window.
    };

    struct Rect // Simple rectangle type used by native monitor information.
    {
        int left = 0;   // Left screen coordinate.
        int top = 0;    // Top screen coordinate.
        int right = 0;  // Right screen coordinate.
        int bottom = 0; // Bottom screen coordinate.
    };

    struct MonitorInfo // Describes one connected monitor.
    {
        void *handle = nullptr; // HMONITOR stored as an opaque pointer.
        Rect workArea = {};     // Usable area excluding taskbars and docked bars.
        Rect monitorArea = {};  // Full monitor rectangle.
        bool isPrimary = false; // Whether this is the primary monitor.
        std::string deviceName; // Win32 display device name encoded as UTF-8.
    };

    struct DisplayMode // Describes one supported display mode.
    {
        int width = 0;        // Display width in physical pixels.
        int height = 0;       // Display height in physical pixels.
        int refreshRate = 0;  // Refresh rate in hertz.
        int bitsPerPixel = 0; // Color depth in bits per pixel.
    };

    class Window // Owns one native Win32 window and exposes engine-friendly window controls.
    {
    public:
        // Lifecycle

        /// @brief Creates an empty Window wrapper with no native handle.
        Window() = default;

        /// @brief Native window handles are unique, so Window cannot be copied.
        Window(const Window &) = delete;

        /// @brief Native window handles are unique, so Window cannot be copy-assigned.
        Window &operator=(const Window &) = delete;

        /// @brief Native window handles are unique, so Window cannot be moved.
        Window(Window &&) = delete;

        /// @brief Native window handles are unique, so Window cannot be move-assigned.
        Window &operator=(Window &&) = delete;

        /// @brief Destroys the native window if it is still alive.
        ~Window();

        /// @brief Creates the native Win32 window.
        /// @param description Window title and requested size.
        /// @return Result code for the create operation.
        WindowResult create(const WindowDescription &description);

        /// @brief Destroys the native window and clears owned native state.
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

        /// @brief Returns the DPI currently associated with the window.
        /// @return Window DPI, or 96 if no native window exists.
        unsigned int getDpi() const;

        /// @brief Returns whether the client area size changed since the flag was last cleared.
        /// @return True if the client size changed.
        bool wasClientSizeChanged() const;

        /// @brief Clears the client-size-changed flag after the resize has been handled.
        void clearClientSizeChanged();

        /// @brief Returns native window data for platform-specific backends.
        /// @return Native window info, or null pointers if the window is not created.
        WindowInfo getInfo() const;

        /// @brief Returns whether the window is currently focused.
        /// @return True if the window is focused, false otherwise.
        bool isFocused() const;

        /// @brief Returns whether the window is currently minimized.
        /// @return True if the window is minimized, false otherwise.
        bool isMinimized() const;

        // Size and title

        /// @brief Sets the requested client-area size.
        /// This also updates the game resolution used when entering exclusive fullscreen.
        /// @param width The new width.
        /// @param height The new height.
        /// @return Result code for the resize operation.
        WindowResult setClientSize(int width, int height);

        /// @brief Sets the native window title from UTF-8 text.
        /// @param title The new title.
        /// @return Result code for the title operation.
        WindowResult setTitle(std::string_view title);

        /// @brief Sets the minimum requested client-area size for window resizing.
        /// @param width The minimum client width.
        /// @param height The minimum client height.
        void setMinClientSize(int width, int height);

        /// @brief Sets the maximum requested client-area size for window resizing.
        /// @param width The maximum client width.
        /// @param height The maximum client height.
        void setMaxClientSize(int width, int height);

        /// @brief Gets the current client-area size constraints.
        /// @param outMinWidth Receives the minimum client width.
        /// @param outMinHeight Receives the minimum client height.
        /// @param outMaxWidth Receives the maximum client width.
        /// @param outMaxHeight Receives the maximum client height.
        void getClientSizeConstraints(int &outMinWidth, int &outMinHeight, int &outMaxWidth, int &outMaxHeight) const;

        // Window mode

        /// @brief Returns the current window mode.
        /// @return The window mode.
        WindowMode getMode() const;

        /// @brief Sets the mode of the window.
        /// @param mode The new mode.
        /// @return Result code for the mode operation.
        WindowResult setMode(WindowMode mode);

        // Errors

        /// @brief Returns the last window operation result.
        /// @return The last stored result code.
        WindowResult getLastResult() const;

        /// @brief Returns the last Win32 GetLastError code recorded by a failed operation.
        /// @return Win32 error code, or 0 if none was recorded.
        unsigned long getLastWin32Error() const;

        // Cursor

        /// @brief Shows or hides the cursor while it is over the game window.
        /// @param visible True to show the cursor, false to hide it.
        void setCursorVisible(bool visible);

        /// @brief Returns whether the window wants the cursor visible.
        /// @return True if cursor visibility is enabled.
        bool isCursorVisible() const;

        /// @brief Confines or releases the cursor to the window client area.
        /// @param confined True to confine the cursor, false to release it.
        void setCursorConfined(bool confined);

        /// @brief Returns whether the window wants the cursor confined to the client area.
        /// @return True if cursor confinement is enabled.
        bool isCursorConfined() const;

        /// @brief Applies a combined cursor visibility and confinement policy.
        /// @param mode The cursor mode to apply.
        void setCursorMode(CursorMode mode);

        /// @brief Returns the current combined cursor visibility and confinement policy.
        /// @return The current cursor mode.
        CursorMode getCursorMode() const;

        // Monitor and display info

        /// @brief Returns all currently available monitors.
        /// @param outMonitors Receives monitor information.
        /// @return Result code for the monitor query.
        WindowResult getMonitors(std::vector<MonitorInfo> &outMonitors);

        /// @brief Returns the monitor containing the window, or the nearest monitor.
        /// @param outMonitor Receives current monitor information.
        /// @return Result code for the monitor query.
        WindowResult getCurrentMonitor(MonitorInfo &outMonitor);

        /// @brief Returns supported display modes for one monitor.
        /// @param monitor Monitor to query.
        /// @param outModes Receives supported display modes.
        /// @return Result code for the display-mode query.
        WindowResult getDisplayModes(const MonitorInfo &monitor, std::vector<DisplayMode> &outModes);

        /// @brief Returns the current display mode for the window's monitor.
        /// @param outMode Receives the current display mode.
        /// @return Result code for the display-mode query.
        WindowResult getCurrentDisplayMode(DisplayMode &outMode);

    private:
        // Native bridge

        /// @brief Grants the Win32 callback access to private message handlers.
        friend struct Internal::WindowMessageAccess;

        /// @brief Win32-specific state hidden from users of the window API.
        struct NativeWindow;

        /// @brief Owned native window state; null before create and after destroy.
        NativeWindow *nativeWindow = nullptr;

        /// @brief Last operation result stored even if nativeWindow is destroyed.
        WindowResult lastResult = WindowResult::Success;

        /// @brief Last Win32 GetLastError value stored even if nativeWindow is destroyed.
        unsigned long lastWin32Error = 0;

        // Result helpers

        /// @brief Stores and returns an operation result.
        /// @param result Result code to store.
        /// @param win32Error Optional Win32 GetLastError value.
        /// @return The result argument.
        WindowResult recordResult(WindowResult result, unsigned long win32Error = 0);

        // Event helpers

        /// @brief Queues a window event for the next popEvent call.
        /// @param event Event to queue.
        void pushEvent(const WindowEvent &event);

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

        /// @brief Updates minimized state after a native size message.
        /// @param minimized True when the window is minimized.
        void handleMinimizeChange(bool minimized);

        /// @brief Handles a DPI change suggested by Win32.
        /// @param dpi New DPI value.
        /// @param suggestedLeft Suggested outer window left.
        /// @param suggestedTop Suggested outer window top.
        /// @param suggestedRight Suggested outer window right.
        /// @param suggestedBottom Suggested outer window bottom.
        void handleDpiChange(unsigned int dpi, int suggestedLeft, int suggestedTop, int suggestedRight, int suggestedBottom);

        /// @brief Handles monitor or display-mode changes reported by Win32.
        void handleDisplayChange();

        /// @brief Invalidates cached monitor and display-mode information.
        void invalidateMonitorCache();

        /// @brief Handles WM_GETMINMAXINFO without exposing Win32 types in the public header.
        /// @param minMaxInfo Pointer to a MINMAXINFO structure.
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

        // Cursor helpers

        /// @brief Releases an active cursor clip.
        /// @return Result code for the cursor operation.
        WindowResult releaseCursorConfinement();

        /// @brief Applies or releases cursor clipping based on current window state.
        void updateCursorConfinement();
    };
}
