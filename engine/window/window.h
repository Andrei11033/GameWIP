#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace GameWIP::Input
{
    class InputDeviceRegistry;
    class InputState;
} // namespace GameWIP::Input

namespace GameWIP
{
    namespace WindowTypes
    {
        inline constexpr std::size_t defaultEventQueueCapacity = 256;

        enum class Mode
        {
            Windowed,
            BorderlessFullscreen,
            Fullscreen
        };

        enum class CursorMode
        {
            FreeVisible,
            FreeHidden,
            ConfinedVisible,
            ConfinedHidden
        };

        enum class Role
        {
            MainGame,
            SecondaryGameView,
            Tool,
            Debug
        };

        enum class Result
        {
            Success,
            NotCreated,
            InvalidDescription,
            InvalidSize,
            InvalidMonitor,
            InvalidDisplayMode,
            PlatformCallFailed,
            MissingWindowedPlacement,
            MissingDisplayMode,
            ModeChangeFailed,
            OperationNotAllowed
        };

        struct Info
        {
            void *instance = nullptr;
            void *handle = nullptr;
        };

        enum class EventType
        {
            CloseRequested,
            Destroyed,
            Resized,
            Moved,
            Focused,
            LostFocus,
            Minimized,
            Maximized,
            Restored,
            ModeChanged,
            MonitorChanged,
            DisplayChanged,
            DpiChanged,
            CursorEntered,
            CursorLeft,
            FileDropped,
            Suspended,
            Resumed,
            Occluded,
            Visible
        };

        struct Event
        {
            EventType type = EventType::Resized;
            int width = 0;
            int height = 0;
            int x = 0;
            int y = 0;
            unsigned int dpi = 0;
            Mode mode = Mode::Windowed;
            std::string filePath;
        };

        struct Description
        {
            std::string title;
            int width = 0;
            int height = 0;
            Mode mode = Mode::Windowed;
            Role role = Role::MainGame;
            bool resizable = true;
            std::size_t eventQueueCapacity = defaultEventQueueCapacity;
        };

        struct Rect
        {
            int left = 0;
            int top = 0;
            int right = 0;
            int bottom = 0;
        };

        struct MonitorInfo
        {
            void *handle = nullptr;
            Rect workArea = {};
            Rect monitorArea = {};
            bool isPrimary = false;
            std::string deviceName;
        };

        struct DisplayMode
        {
            int width = 0;
            int height = 0;
            int refreshRate = 0;
            int bitsPerPixel = 0;
        };
    } // namespace WindowTypes

    namespace WindowInternal
    {
        struct MessageAccess;
    }

    /// @brief Native platform window wrapper.
    class Window
    {
    public:
        using Mode = WindowTypes::Mode;
        using CursorMode = WindowTypes::CursorMode;
        using Role = WindowTypes::Role;
        using Result = WindowTypes::Result;
        using Info = WindowTypes::Info;
        using EventType = WindowTypes::EventType;
        using Event = WindowTypes::Event;
        using Description = WindowTypes::Description;
        using Rect = WindowTypes::Rect;
        using MonitorInfo = WindowTypes::MonitorInfo;
        using DisplayMode = WindowTypes::DisplayMode;

        inline static constexpr std::size_t defaultEventQueueCapacity = WindowTypes::defaultEventQueueCapacity;

        Window() = default;
        Window(const Window &) = delete;
        Window &operator=(const Window &) = delete;
        Window(Window &&) = delete;
        Window &operator=(Window &&) = delete;
        ~Window();

        Result create(const Description &description);
        Result destroy();

        void pollEvents(Input::InputState &inputState, Input::InputDeviceRegistry &inputDevices);
        bool popEvent(Event &outEvent);
        void clearEvents();
        bool shouldClose() const;
        void requestClose();

        int getClientWidth() const;
        int getClientHeight() const;
        unsigned int getDpi() const;
        bool wasClientSizeChanged() const;
        void clearClientSizeChanged();
        Info getInfo() const;
        bool isFocused() const;
        bool isMinimized() const;

        Result setClientSize(int width, int height);
        Result setTitle(std::string_view title);
        void setMinClientSize(int width, int height);
        void setMaxClientSize(int width, int height);
        void getClientSizeConstraints(int &outMinWidth, int &outMinHeight, int &outMaxWidth, int &outMaxHeight) const;

        Mode getMode() const;
        Role getRole() const;
        Result setMode(Mode mode);

        Result getLastResult() const;
        unsigned long getLastPlatformError() const;
        bool hasAsyncError() const;
        Result getLastAsyncResult() const;
        unsigned long getLastAsyncPlatformError() const;
        void clearAsyncError();

        void setCursorVisible(bool visible);
        bool isCursorVisible() const;
        Result setCursorConfined(bool confined);
        bool isCursorConfined() const;
        Result setCursorMode(CursorMode mode);
        CursorMode getCursorMode() const;

        Result getMonitors(std::vector<MonitorInfo> &outMonitors);
        Result getCurrentMonitor(MonitorInfo &outMonitor);
        Result getDisplayModes(const MonitorInfo &monitor, std::vector<DisplayMode> &outModes);
        Result getCurrentDisplayMode(DisplayMode &outMode);
        Result setFullscreenDisplayMode(const MonitorInfo &monitor, const DisplayMode &mode);
        Result getFullscreenDisplayMode(MonitorInfo &outMonitor, DisplayMode &outMode);

    private:
        friend struct WindowInternal::MessageAccess;

        struct NativeWindow;
        NativeWindow *nativeWindow = nullptr;

        Result lastResult = Result::Success;
        unsigned long lastPlatformError = 0;
        Result lastAsyncResult = Result::Success;
        unsigned long lastAsyncPlatformError = 0;
        bool asyncErrorRecorded = false;

        Result recordResult(Result result, unsigned long win32Error = 0);
        void recordAsyncError(Result result, unsigned long win32Error = 0);

        void pushEvent(Event event);
        bool shouldSuppressEvent(EventType type) const;

        void handleResize(int width, int height);
        void handleMove(int x, int y);
        void handleFocusChange(bool focused);
        void handleActivationChange(bool active);
        void handleMinimizeChange(bool minimized);
        void handleMaximizeChange(bool maximized);
        void handleVisibilityChange(bool visible);
        bool shouldHandleCursorEnter() const;
        bool handleCursorEnter();
        void handleCursorTrackingFailure(unsigned long win32Error);
        void handleCursorLeave();
        void handleFileDrop(std::string_view filePath);
        void handleDestroyed();
        void updateCurrentMonitor();
        void handleDpiChange(unsigned int dpi, int suggestedLeft, int suggestedTop, int suggestedRight, int suggestedBottom);
        void handleDisplayChange();
        void invalidateMonitorCache();
        void handleGetMinMaxInfo(void *minMaxInfo);
        void *getNativeCursorHandle() const;

        Result saveWindowedPlacement();
        Result restoreDisplayMode();
        Result restoreWindowedPlacement();
        Result applyWindowedMode();
        Result applyBorderlessFullscreenMode();
        Result applyFullscreenMode();

        struct FullscreenModeSnapshot;
        struct FullscreenModeTarget;

        FullscreenModeSnapshot captureFullscreenModeSnapshot() const;
        Result restoreFullscreenModeSnapshot(const FullscreenModeSnapshot &snapshot);
        void rollbackFullscreenMode(Mode previousMode, const FullscreenModeSnapshot &snapshot);
        Result resolveFullscreenModeTarget(const FullscreenModeSnapshot &previousState, FullscreenModeTarget &outTarget);
        Result applyFullscreenModeTarget(const FullscreenModeTarget &target, Mode previousMode, const FullscreenModeSnapshot &previousState);
        void storeFullscreenModeTarget(const FullscreenModeTarget &target);

        Result releaseCursorConfinement(unsigned long *outWin32Error = nullptr);
        void updateCursorConfinement();
    };
} // namespace GameWIP
