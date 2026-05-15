#pragma once

namespace GameWIP
{
struct QueuedWindowEvent // Compact event representation used by the internal ring buffer.
{
    WindowEventType type = WindowEventType::Resized;
    int width = 0;
    int height = 0;
    int x = 0;
    int y = 0;
    unsigned int dpi = 0;
    WindowMode mode = WindowMode::Windowed;
    std::size_t filePayloadIndex = static_cast<std::size_t>(-1);
};

struct WindowEventQueue
{
    static constexpr std::size_t invalidFilePayloadIndex = static_cast<std::size_t>(-1);

    std::vector<QueuedWindowEvent> events;
    std::vector<std::string> fileDropPayloads;
    std::vector<std::size_t> freeFileDropPayloadIndices;
    std::size_t head = 0;
    std::size_t count = 0;
    std::size_t queuedFileDropPayloadCount = 0;

    void init(std::size_t capacity)
    {
        events.assign(capacity, QueuedWindowEvent{});
        fileDropPayloads.clear();
        freeFileDropPayloadIndices.clear();
        queuedFileDropPayloadCount = 0;
        head = 0;
        count = 0;
    }

    bool empty() const
    {
        return count == 0;
    }

    std::size_t size() const
    {
        return count;
    }

    std::size_t capacity() const
    {
        return events.size();
    }

    QueuedWindowEvent &eventAt(std::size_t index)
    {
        return events[(head + index) % capacity()];
    }

    const QueuedWindowEvent &eventAt(std::size_t index) const
    {
        return events[(head + index) % capacity()];
    }

    void clear()
    {
        head = 0;
        count = 0;
        std::vector<std::string>().swap(fileDropPayloads);
        std::vector<std::size_t>().swap(freeFileDropPayloadIndices);
        queuedFileDropPayloadCount = 0;
    }

    void releaseFileDropPayload(const QueuedWindowEvent &event)
    {
        if (event.type != WindowEventType::FileDropped || event.filePayloadIndex == invalidFilePayloadIndex)
        {
            return;
        }

        if (event.filePayloadIndex < fileDropPayloads.size())
        {
            std::string().swap(fileDropPayloads[event.filePayloadIndex]);
        }

        if (queuedFileDropPayloadCount > 0)
        {
            --queuedFileDropPayloadCount;
        }

        if (queuedFileDropPayloadCount == 0)
        {
            std::vector<std::string>().swap(fileDropPayloads);
            std::vector<std::size_t>().swap(freeFileDropPayloadIndices);
        }
        else if (event.filePayloadIndex < fileDropPayloads.size())
        {
            freeFileDropPayloadIndices.push_back(event.filePayloadIndex);
        }
    }

    void moveToPublicEvent(const QueuedWindowEvent &queuedEvent, WindowEvent &outEvent)
    {
        outEvent = {};
        std::string().swap(outEvent.filePath);
        outEvent.type = queuedEvent.type;
        outEvent.width = queuedEvent.width;
        outEvent.height = queuedEvent.height;
        outEvent.x = queuedEvent.x;
        outEvent.y = queuedEvent.y;
        outEvent.dpi = queuedEvent.dpi;
        outEvent.mode = queuedEvent.mode;

        if (queuedEvent.type == WindowEventType::FileDropped &&
            queuedEvent.filePayloadIndex != invalidFilePayloadIndex &&
            queuedEvent.filePayloadIndex < fileDropPayloads.size())
        {
            outEvent.filePath = std::move(fileDropPayloads[queuedEvent.filePayloadIndex]);
        }
    }

    bool popFront(WindowEvent &outEvent)
    {
        if (empty())
        {
            return false;
        }

        QueuedWindowEvent queuedEvent = eventAt(0);
        moveToPublicEvent(queuedEvent, outEvent);
        releaseFileDropPayload(queuedEvent);
        head = (head + 1) % capacity();
        --count;
        return true;
    }

    void discardFront()
    {
        if (empty())
        {
            return;
        }

        releaseFileDropPayload(eventAt(0));
        head = (head + 1) % capacity();
        --count;
    }

    void pushBack(WindowEvent &&event)
    {
        QueuedWindowEvent queuedEvent{
            .type = event.type,
            .width = event.width,
            .height = event.height,
            .x = event.x,
            .y = event.y,
            .dpi = event.dpi,
            .mode = event.mode,
            .filePayloadIndex = invalidFilePayloadIndex};

        if (event.type == WindowEventType::FileDropped)
        {
            if (!freeFileDropPayloadIndices.empty())
            {
                queuedEvent.filePayloadIndex = freeFileDropPayloadIndices.back();
                freeFileDropPayloadIndices.pop_back();
                fileDropPayloads[queuedEvent.filePayloadIndex] = std::move(event.filePath);
            }
            else
            {
                queuedEvent.filePayloadIndex = fileDropPayloads.size();
                fileDropPayloads.push_back(std::move(event.filePath));
            }
            ++queuedFileDropPayloadCount;
        }

        events[(head + count) % capacity()] = queuedEvent;
        ++count;
    }

    void removeAt(std::size_t index)
    {
        releaseFileDropPayload(eventAt(index));
        for (std::size_t i = index; i + 1 < count; ++i)
        {
            eventAt(i) = eventAt(i + 1);
        }
        --count;
    }

    std::size_t findOldestCoalescableEventIndex() const
    {
        for (std::size_t i = 0; i < count; ++i)
        {
            if (isWindowEventCoalesable(eventAt(i).type))
            {
                return i;
            }
        }
        return static_cast<std::size_t>(-1);
    }

    bool tryCoalesce(const WindowEvent &event)
    {
        for (std::size_t i = count; i-- > 0;)
        {
            const QueuedWindowEvent &queuedEvent = eventAt(i);
            if (!isWindowEventCoalesable(queuedEvent.type))
            {
                break;
            }
            if (queuedEvent.type == event.type)
            {
                QueuedWindowEvent &target = eventAt(i);
                target.width = event.width;
                target.height = event.height;
                target.x = event.x;
                target.y = event.y;
                target.dpi = event.dpi;
                target.mode = event.mode;
                return true;
            }
        }
        return false;
    }
};

struct Window::NativeWindow
{
    // === Native handles ===
    HINSTANCE instance = nullptr;
    HWND handle = nullptr;

    // === Window state ===
    WindowMode mode = WindowMode::Windowed;
    WindowRole role = WindowRole::MainGame;
    bool closeRequested = false;
    bool isFocused = false;
    bool isMinimized = false;
    bool isMaximized = false;
    bool isVisible = false;
    bool isSuspended = false;
    bool clientSizeChanged = false;
    bool suppressStartupEvents = false;

    // === Client size and DPI ===
    int clientWidth = 0;
    int clientHeight = 0;

    // The requested size is the game's target resolution; the live client size may differ after user resizing.
    int requestedClientWidth = 0;
    int requestedClientHeight = 0;
    unsigned int dpi = defaultDpi;

    // === Client size constraints ===
    int minClientWidth = 0;
    int minClientHeight = 0;
    int maxClientWidth = unlimitedClientSize;
    int maxClientHeight = unlimitedClientSize;

    // === Windowed mode state ===
    RECT windowedRect{};
    DWORD windowedStyle = 0;
    DWORD windowedExtendedStyle = 0;
    bool hasSavedWindowedPlacement = false;

    // === Exclusive fullscreen mode state ===
    wchar_t fullscreenDeviceName[CCHDEVICENAME]{};
    DEVMODEW savedDisplayMode{};
    MonitorInfo requestedFullscreenMonitor{};
    DisplayMode requestedFullscreenDisplayMode{};
    MonitorInfo activeFullscreenMonitor{};
    DisplayMode activeFullscreenDisplayMode{};
    RECT fullscreenRect{};
    bool hasRequestedFullscreenDisplayMode = false;
    bool activeFullscreenModeIsExact = false;
    bool hasSavedDisplayMode = false;

    // Focus loss temporarily restores the desktop mode while preserving the fullscreen request.
    bool fullscreenSuspended = false;
    int fullscreenWidth = 0;
    int fullscreenHeight = 0;

    // === Cursor state ===
    HCURSOR arrowCursor = nullptr;
    bool cursorVisible = true;
    bool cursorConfined = false;
    bool cursorClipApplied = false;
    bool cursorInClient = false;

    // === Monitor and display caches ===
    std::vector<MonitorInfo> monitorCache;
    std::unordered_map<std::string, std::vector<DisplayMode>> displayModeCache;
    HMONITOR currentMonitor = nullptr;

    // === Events queue ===
    WindowEventQueue events{};

    // === Scratch buffers for repeated conversions ===
    std::wstring utf16Scratch;

    // === Window title caching ===
    std::string title;
};

struct Window::FullscreenModeSnapshot
{
    wchar_t deviceName[CCHDEVICENAME]{};
    DEVMODEW savedDisplayMode{};
    MonitorInfo activeMonitor{};
    DisplayMode activeDisplayMode{};
    RECT rect{};
    bool hasSavedDisplayMode = false;
    bool activeModeIsExact = false;
    bool suspended = false;
    int width = 0;
    int height = 0;
};

struct Window::FullscreenModeTarget
{
    HMONITOR monitor = nullptr;
    MONITORINFOEXW monitorInfo{};
    wchar_t deviceName[CCHDEVICENAME]{};
    MonitorInfo publicMonitorInfo{};
    DEVMODEW savedDisplayMode{};
    DisplayMode displayMode{};
    bool savedModeIsFromDifferentDevice = false;
};
}
