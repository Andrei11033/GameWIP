/// @file window_test.cpp
/// @brief Deterministic portable and hidden-native checks for Window.

#include "validation/tests/window/window_test.h"

#include "test_support/test_support.h"
#include "window/renderer.h"
#include "window/native/win32.h"
#include "window/window.h"

#include <shellapi.h>
#include <shlobj.h>

#ifndef INTERNAL_WINDOW_TEST_HOOKS
#define INTERNAL_WINDOW_TEST_HOOKS 0
#endif

#if INTERNAL_WINDOW_TEST_HOOKS
#include "window/internal/window_test_hooks.h"
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <format>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
    namespace IO = GameWIP::IO;
    namespace TestSupport = GameWIP::TestSupport;
    namespace Window = GameWIP::Window;
    using ErrorCode = IO::Types::ErrorCode;

    static_assert(!std::is_move_constructible_v<Window::Window>);
    static_assert(!std::is_move_assignable_v<Window::Window>);
    static_assert(!std::is_copy_constructible_v<Window::Window>);
    static_assert(!std::is_copy_assignable_v<Window::Window>);
    static_assert(noexcept(Window::pollEvents()));
    static_assert(noexcept(Window::waitEvents()));
    static_assert(noexcept(std::declval<Window::Window &>().close()));
    static_assert(std::is_same_v<decltype(Window::Types::FilesDroppedEvent{}.paths)::value_type, GameWIP::FileSystem::Types::Path>);

    template <typename Payload> [[nodiscard]] bool consumeEventOfType(Window::Window &window)
    {
        bool found = false;
        Window::Types::Event event;
        while (window.popEvent(event))
        {
            found = found || event.getIf<Payload>() != nullptr;
        }
        return found;
    }

    void testPassiveValuesAndClosedState(TestSupport::Context &context)
    {
        static_cast<void>(context.expectFalse("default window id is invalid", Window::Types::WindowId{}.valid()));
        static_cast<void>(context.expectTrue("nonzero window id is valid", Window::Types::WindowId{4}.valid()));
        static_cast<void>(context.expectFalse("default monitor id is invalid", Window::Types::MonitorId{}.valid()));
        static_cast<void>(context.expectTrue(
            "geometry values compare structurally",
            Window::Types::LogicalRect{{-4, 8}, {10, 12}} == Window::Types::LogicalRect{{-4, 8}, {10, 12}}));
        static_cast<void>(context.expectEq(
            "hit-mask words round up one bit per pixel",
            std::size_t{2},
            Window::Renderer::requiredPointerHitMaskWords({9, 8})));
        static_cast<void>(context.expectEq(
            "empty hit-mask extent is invalid",
            std::size_t{0},
            Window::Renderer::requiredPointerHitMaskWords({0, 8})));
#if INTERNAL_WINDOW_TEST_HOOKS
        static_cast<void>(context.expectEq(
            "60000/1001 refresh rounds to millihertz",
            std::uint32_t{59'940},
            Window::TestHooks::refreshRateMillihertz(60'000, 1'001)));
        static_cast<void>(context.expectEq(
            "unknown rational refresh remains zero",
            std::uint32_t{0},
            Window::TestHooks::refreshRateMillihertz(60'000, 0)));
        static_cast<void>(context.expectEq(
            "rational refresh conversion saturates",
            std::numeric_limits<std::uint32_t>::max(),
            Window::TestHooks::refreshRateMillihertz(std::numeric_limits<std::uint32_t>::max(), 1)));
        const Window::Types::DisplayMode exactMode{{1920, 1080}, 60'000, 32, false};
        static_cast<void>(context.expectTrue(
            "exclusive comparator accepts an exact native mode",
            Window::TestHooks::exactNativeDisplayModeMatches(exactMode, 1920, 1080, 60, 32, false)));
        Window::Types::DisplayMode fractionalMode = exactMode;
        fractionalMode.refreshRateMillihertz = 59'940;
        static_cast<void>(context.expectFalse(
            "exclusive comparator rejects a nearest integer-Hz mode",
            Window::TestHooks::exactNativeDisplayModeMatches(fractionalMode, 1920, 1080, 60, 32, false)));
        const Window::TestHooks::DpiTransitionResult preserveLogical =
            Window::TestHooks::calculateDpiTransition(
                {800, 600},
                {800, 600},
                144,
                Window::Types::DpiResizePolicy::PreserveLogicalClientSize);
        static_cast<void>(
            context.expectEq("logical-size DPI policy preserves logical extent", Window::Types::LogicalSize{800, 600}, preserveLogical.logicalSize));
        static_cast<void>(context.expectEq(
            "logical-size DPI policy scales framebuffer",
            Window::Types::PixelSize{1200, 900},
            preserveLogical.framebufferSize));
        const Window::TestHooks::DpiTransitionResult preservePhysical =
            Window::TestHooks::calculateDpiTransition(
                {800, 600},
                {800, 600},
                144,
                Window::Types::DpiResizePolicy::PreservePhysicalClientSize);
        static_cast<void>(context.expectEq(
            "physical-size DPI policy recalculates logical extent",
            Window::Types::LogicalSize{533, 400},
            preservePhysical.logicalSize));
        static_cast<void>(context.expectEq(
            "physical-size DPI policy preserves framebuffer",
            Window::Types::PixelSize{800, 600},
            preservePhysical.framebufferSize));
#endif

        const Window::Types::CapabilitiesResult capabilities = Window::getCapabilities();
        static_cast<void>(context.expectTrue("capability query succeeds", capabilities.status.ok()));
        static_cast<void>(
            context.expectTrue("Win32 supports multiple windows", capabilities.capabilities.supports(Window::Types::Capability::MultipleWindows)));
        static_cast<void>(context.expectFalse("Count is not a capability", capabilities.capabilities.supports(Window::Types::Capability::Count)));

        const Window::Types::EventPumpResult idlePoll = Window::pollEvents();
        static_cast<void>(context.expectTrue("poll with no windows is a successful no-op", idlePoll.status.ok()));
        const Window::Types::EventPumpResult badWait = Window::waitEvents(std::chrono::milliseconds{-2});
        static_cast<void>(context.expectEq("timeout below forever sentinel is invalid", ErrorCode::InvalidArgument, badWait.status.code));

        Window::Window closed;
        static_cast<void>(context.expectFalse("default Window is closed", closed.isOpen()));
        static_cast<void>(
            context.expectEq("default lifetime is Closed", Window::Types::LifetimeState::Closed, closed.lifetimeState()));
        static_cast<void>(context.expectEq("closed id is invalid", Window::Types::WindowId{}, closed.id()));
        static_cast<void>(context.expectEq("closed title is empty", std::string_view{}, closed.title()));
        static_cast<void>(context.expectEq("closed operation reports NotOpen", ErrorCode::NotOpen, closed.setTitle("unused").code));
        static_cast<void>(context.expectTrue("repeated close on closed Window succeeds", closed.close().ok()));
    }

    void testDescriptionValidation(TestSupport::Context &context)
    {
        const auto expectInvalid = [&context](std::string_view name, Window::Types::Description description)
        {
            Window::Window window;
            const IO::Types::Status status = window.open(description);
            static_cast<void>(context.expectEq(name, ErrorCode::InvalidArgument, status.code));
            static_cast<void>(context.expectFalse("invalid description leaves Window closed", window.isOpen()));
        };

        Window::Types::Description description;
        description.clientSize = {};
        expectInvalid("zero client size is invalid", description);

        description = {};
        description.title.assign("bad\0title", 9);
        expectInvalid("embedded title NUL is invalid", description);

        description = {};
        description.title.assign("\xC0\xAF", 2);
        expectInvalid("overlong UTF-8 is invalid", description);

        description = {};
        description.opacity = 1.1F;
        expectInvalid("opacity above one is invalid", description);

        description = {};
        description.sizeLimits.minimum = Window::Types::LogicalSize{900, 700};
        description.sizeLimits.maximum = Window::Types::LogicalSize{800, 600};
        expectInvalid("inverted size limits are invalid", description);

        description = {};
        description.aspectRatio = Window::Types::AspectRatio{16, 0};
        expectInvalid("zero aspect denominator is invalid", description);

        description = {};
        description.mode.displayMode = Window::Types::DisplayMode{{1920, 1080}, 60'000, 32, false};
        expectInvalid("display mode outside exclusive mode is invalid", description);

        description = {};
        description.pointerInputMode = Window::Types::PointerInputMode::AcceptRegions;
        expectInvalid("initial region mode without layout is invalid", description);

        description = {};
        description.cursorMode = static_cast<Window::Types::CursorMode>(99);
        expectInvalid("unknown enum is invalid", description);

        description = {};
        description.dpiResizePolicy = static_cast<Window::Types::DpiResizePolicy>(99);
        expectInvalid("unknown DPI resize policy is invalid", description);

        description = {};
        description.resizable = false;
        description.controls.maximizable = true;
        expectInvalid("maximize requires resize at creation", description);

        description = {};
        description.placement.monitor = {std::numeric_limits<std::uint64_t>::max()};
        expectInvalid("unknown placement monitor is invalid", description);

        description = {};
        description.mode.monitor = {std::numeric_limits<std::uint64_t>::max()};
        expectInvalid("unknown mode monitor is invalid", description);

        description = {};
        description.owner = {std::numeric_limits<std::uint64_t>::max()};
        expectInvalid("unknown owner is invalid", description);

        description = {};
        description.requestFocus = true;
        expectInvalid("hidden initial focus request is invalid", description);

        description = {};
        description.presentation = Window::Types::PresentationState::Minimized;
        expectInvalid("hidden non-normal presentation is invalid", description);

        description = {};
        description.visible = true;
        description.requestFocus = true;
        description.focusable = false;
        expectInvalid("focus request on non-focusable Window is invalid", description);

        description = {};
        description.clientSize.width = std::numeric_limits<std::uint32_t>::max();
        expectInvalid("client extent outside native signed range is invalid", description);

        Window::Window window;
        static_cast<void>(context.expectEq("zero internal queue capacity is invalid", ErrorCode::InvalidArgument, window.open({}, 0).code));
        std::span<Window::Types::Event> empty;
        static_cast<void>(context.expectEq("empty external queue is invalid", ErrorCode::InvalidArgument, window.open({}, empty).code));
    }

#if INTERNAL_WINDOW_TEST_HOOKS
    void testFixedEventQueue(TestSupport::Context &context)
    {
        std::array<Window::Types::Event, 3> storage;
        Window::Window window;
        static_cast<void>(context.expectTrue("portable queue hook opens", Window::TestHooks::openPortable(window, storage).ok()));
        static_cast<void>(
            context.expectEq("hook queue reports external storage", Window::Types::EventStorageKind::External, window.eventQueueInfo().storage));
        static_cast<void>(context.expectEq("native open rejects existing hook state", ErrorCode::AlreadyOpen, window.open({}).code));

        static_cast<void>(Window::TestHooks::enqueue(window, Window::Types::MovedEvent{{1, 2}}));
        static_cast<void>(Window::TestHooks::enqueue(window, Window::Types::MovedEvent{{3, 4}}));
        static_cast<void>(context.expectEq("compatible movement coalesces", std::size_t{1}, window.eventQueueInfo().pendingEvents));

        static_cast<void>(Window::TestHooks::enqueue(window, Window::Types::FocusChangedEvent{true}));
        static_cast<void>(Window::TestHooks::enqueue(window, Window::Types::MovedEvent{{5, 6}}));
        static_cast<void>(context.expectEq("noncoalescible event is a barrier", std::size_t{3}, window.eventQueueInfo().pendingEvents));

        static_cast<void>(Window::TestHooks::enqueue(window, Window::Types::RedrawRequestedEvent{}));
        static_cast<void>(context.expectEq("full queue evicts oldest coalescible event", std::uint64_t{1}, window.eventQueueInfo().droppedEvents));

        Window::Types::Event first;
        Window::Types::Event second;
        Window::Types::Event third;
        static_cast<void>(context.expectTrue("first retained event pops", window.popEvent(first)));
        static_cast<void>(context.expectTrue("focus barrier remains", first.getIf<Window::Types::FocusChangedEvent>() != nullptr));
        static_cast<void>(context.expectTrue("second retained event pops", window.popEvent(second)));
        const auto *moved = second.getIf<Window::Types::MovedEvent>();
        static_cast<void>(context.expectTrue("post-barrier movement remains", moved != nullptr));
        if (moved != nullptr)
            static_cast<void>(context.expectEq("coalesced movement keeps latest payload", Window::Types::ScreenPosition{5, 6}, moved->position));
        static_cast<void>(context.expectTrue("third retained event pops", window.popEvent(third)));
        static_cast<void>(context.expectTrue("new noncoalescible event remains", third.getIf<Window::Types::RedrawRequestedEvent>() != nullptr));
        static_cast<void>(
            context.expectTrue("retained sequences are increasing", first.sequence < second.sequence && second.sequence < third.sequence));

        window.clearDroppedEventCount();
        static_cast<void>(context.expectEq("drop count clears", std::uint64_t{0}, window.eventQueueInfo().droppedEvents));
        static_cast<void>(context.expectTrue("hook queue closes", window.close().ok()));

        std::array<Window::Types::Event, 1> payloadStorage;
        Window::Window payloadWindow;
        static_cast<void>(Window::TestHooks::openPortable(payloadWindow, payloadStorage));
        Window::Types::FilesDroppedEvent dropped;
        dropped.paths.emplace_back("retained-until-close.txt");
        static_cast<void>(Window::TestHooks::enqueue(payloadWindow, std::move(dropped)));
        static_cast<void>(payloadWindow.close());
        static_cast<void>(context.expectTrue(
            "close releases payloads from borrowed event slots",
            payloadStorage[0].getIf<Window::Types::FilesDroppedEvent>() == nullptr));
    }

    void testStickyClose(TestSupport::Context &context)
    {
        std::array<Window::Types::Event, 4> storage;
        Window::Window source;
        static_cast<void>(Window::TestHooks::openPortable(source, storage));
        static_cast<void>(Window::TestHooks::requestClose(source, Window::Types::CloseRequestSource::User));
        static_cast<void>(Window::TestHooks::requestClose(source, Window::Types::CloseRequestSource::System));
        static_cast<void>(context.expectTrue("close request is sticky", source.closeRequested()));
        static_cast<void>(context.expectEq("repeated close request emits once", std::size_t{1}, source.eventQueueInfo().pendingEvents));

        Window::Types::Event event;
        static_cast<void>(context.expectTrue("queue remains readable", source.popEvent(event)));
        const auto *close = event.getIf<Window::Types::CloseRequestedEvent>();
        static_cast<void>(context.expectTrue("typed close payload remains", close != nullptr));
        if (close != nullptr)
            static_cast<void>(context.expectEq("first close source wins", Window::Types::CloseRequestSource::User, close->source));
        static_cast<void>(source.close());
    }

    void testFailureInjection(TestSupport::Context &context)
    {
        using FailurePoint = Window::TestHooks::FailurePoint;
        Window::TestHooks::resetFailures();

        Window::Types::Description description;
        description.title = "Window failure validation";
        description.clientSize = {280, 180};
        description.visible = false;

        const auto expectFailedOpen = [&context, &description](std::string_view name, FailurePoint point, ErrorCode expected)
        {
            Window::Window candidate;
            Window::TestHooks::failNext(point);
            const IO::Types::Status status = candidate.open(description, 8);
            static_cast<void>(context.expectEq(name, expected, status.code));
            static_cast<void>(context.expectFalse("failed open rolls back native ownership", candidate.isOpen()));
            Window::TestHooks::resetFailures();
        };

        expectFailedOpen("allocation failure is translated", FailurePoint::Allocation, ErrorCode::OutOfMemory);
        expectFailedOpen("dispatcher failure is translated", FailurePoint::Dispatcher, ErrorCode::OpenFailed);
        expectFailedOpen("native creation failure is translated", FailurePoint::NativeCreation, ErrorCode::OpenFailed);
        expectFailedOpen("partial native open rolls back", FailurePoint::PartialOpen, ErrorCode::NativeFailure);

        Window::Window window;
        static_cast<void>(context.expectTrue("open succeeds after injected rollback", window.open(description, 16).ok()));
        if (!window.isOpen())
            return;

        const std::string originalTitle(window.title());
        Window::TestHooks::failNext(FailurePoint::TitleConversion);
        static_cast<void>(context.expectEq("title conversion failure is translated", ErrorCode::EncodingFailed, window.setTitle("changed").code));
        static_cast<void>(context.expectEq("failed title update preserves cache", std::string_view{originalTitle}, window.title()));

        const std::array pointerRegions{Window::Types::LogicalRect{{0, 0}, {16, 16}}};
        Window::TestHooks::failNext(FailurePoint::RegionCopy);
        const Window::Types::CustomChromeLayout chromeLayout{.draggableRegions = pointerRegions};
        static_cast<void>(
            context.expectEq("region copy failure is translated", ErrorCode::OutOfMemory, window.setCustomChromeLayout(chromeLayout).code));

        const std::array<std::byte, 4> pixel{std::byte{0x40}, std::byte{0x80}, std::byte{0xC0}, std::byte{0xFF}};
        const std::array iconImages{Window::Types::IconImageView{{1, 1}, pixel}};
        Window::TestHooks::failNext(FailurePoint::IconConversion);
        static_cast<void>(context.expectEq("icon conversion failure is translated", ErrorCode::NativeFailure, window.setIcon(iconImages).code));
        static_cast<void>(context.expectTrue("icon operation recovers after failure", window.setIcon(iconImages).ok()));
        static_cast<void>(window.clearIcon());

        Window::TestHooks::failNext(FailurePoint::Cursor);
        static_cast<void>(context.expectEq(
            "cursor failure is translated",
            ErrorCode::NativeFailure,
            window.setCursorMode(Window::Types::CursorMode::Confined).code));
        static_cast<void>(context.expectEq("failed cursor update rolls back cache", Window::Types::CursorMode::Normal, window.cursorMode()));

        Window::TestHooks::failNext(FailurePoint::MonitorQuery);
        static_cast<void>(context.expectEq("monitor query failure is translated", ErrorCode::StatFailed, Window::getPrimaryMonitor().status.code));
        Window::TestHooks::failNext(FailurePoint::DisplayEnumeration);
        static_cast<void>(context.expectEq("display enumeration failure is translated", ErrorCode::StatFailed, Window::getMonitors().status.code));

        const Window::Types::MonitorId monitor = window.currentMonitor();
        Window::TestHooks::failNext(FailurePoint::FullscreenPartial);
        static_cast<void>(context.expectEq(
            "partial fullscreen failure is translated",
            ErrorCode::NativeFailure,
            window.setMode({.mode = Window::Types::WindowMode::BorderlessFullscreen, .monitor = monitor}).code));
        static_cast<void>(context.expectEq("partial fullscreen failure restores mode", Window::Types::WindowMode::Windowed, window.mode()));

        static_cast<void>(context.expectTrue(
            "borderless mode opens restoration boundary",
            window.setMode({.mode = Window::Types::WindowMode::BorderlessFullscreen, .monitor = monitor}).ok()));
        Window::TestHooks::failNext(FailurePoint::DisplayRestoration);
        static_cast<void>(context.expectEq("display restoration failure is translated", ErrorCode::NativeFailure, window.setMode({}).code));
        static_cast<void>(
            context.expectEq("failed display restoration preserves previous mode", Window::Types::WindowMode::BorderlessFullscreen, window.mode()));
        static_cast<void>(context.expectTrue("display restoration retry succeeds", window.setMode({}).ok()));

        Window::TestHooks::failNext(FailurePoint::EventPump);
        static_cast<void>(context.expectEq("event pump failure is translated", ErrorCode::NativeFailure, Window::pollEvents().status.code));
        static_cast<void>(context.expectTrue("event pump recovers after failure", Window::pollEvents().status.ok()));

        Window::TestHooks::failNext(FailurePoint::Close);
        static_cast<void>(context.expectEq("close failure is translated", ErrorCode::CloseFailed, window.close().code));
        static_cast<void>(context.expectTrue("failed close preserves ownership", window.isOpen()));
        static_cast<void>(context.expectTrue("close retry succeeds", window.close().ok()));
        Window::TestHooks::resetFailures();
    }

    void testThreadingContracts(TestSupport::Context &context)
    {
        Window::Types::Description description;
        description.title = "Window threading validation";
        description.clientSize = {240, 160};
        description.visible = false;

        Window::Window window;
        static_cast<void>(context.expectTrue("threading fixture opens", window.open(description, 8).ok()));
        if (!window.isOpen())
            return;

        ErrorCode mutationCode = ErrorCode::Success;
        ErrorCode closeCode = ErrorCode::Success;
        ErrorCode wakeCode = ErrorCode::Unknown;
        ErrorCode nativeHandleCode = ErrorCode::Success;
        std::thread worker(
            [&window, &mutationCode, &closeCode, &wakeCode, &nativeHandleCode]
            {
                mutationCode = window.setTitle("wrong-thread mutation").code;
                closeCode = window.close().code;
                wakeCode = window.wakeEventWait().code;
                nativeHandleCode = Window::Native::Win32::getHandle(window).status.code;
            });
        worker.join();

        static_cast<void>(context.expectEq("wrong-thread mutation is rejected", ErrorCode::ResourceBusy, mutationCode));
        static_cast<void>(context.expectEq("wrong-thread close is rejected", ErrorCode::ResourceBusy, closeCode));
        static_cast<void>(context.expectEq("wake is intentionally cross-thread safe", ErrorCode::Success, wakeCode));
        static_cast<void>(context.expectEq("wrong-thread native handle is rejected", ErrorCode::ResourceBusy, nativeHandleCode));
        static_cast<void>(context.expectEq("wrong-thread mutation preserves title", std::string_view{description.title}, window.title()));
        static_cast<void>(context.expectTrue("wrong-thread close preserves ownership", window.isOpen()));

        const Window::Types::EventPumpResult wakeResult = Window::waitEvents(std::chrono::milliseconds{100});
        static_cast<void>(context.expectTrue("owner pump receives cross-thread wake", wakeResult.status.ok() && !wakeResult.timedOut));
        static_cast<void>(
            context.expectEq("reentrant event pump is rejected", ErrorCode::ResourceBusy, Window::TestHooks::pumpReentrantly().status.code));
        static_cast<void>(context.expectTrue("threading fixture closes", window.close().ok()));
    }

    void testExceptionalLifetime(TestSupport::Context &context)
    {
        Window::Types::Description description;
        description.title = "Window exceptional lifetime validation";
        description.clientSize = {240, 160};
        description.visible = false;

        Window::Window unexpected;
        static_cast<void>(context.expectTrue("unexpected-destruction fixture opens", unexpected.open(description, 1).ok()));
        if (unexpected.isOpen())
        {
            static_cast<void>(Window::TestHooks::enqueue(unexpected, Window::Types::RedrawRequestedEvent{}));
            static_cast<void>(
                context.expectTrue("test hook destroys native HWND unexpectedly", Window::TestHooks::destroyNativeWindow(unexpected).ok()));
            static_cast<void>(context.expectFalse("unexpectedly destroyed HWND is not open", unexpected.isOpen()));
            static_cast<void>(context.expectEq(
                "unexpected destruction enters pending finalize",
                Window::Types::LifetimeState::NativeDestroyedPendingFinalize,
                unexpected.lifetimeState()));
            static_cast<void>(
                context.expectEq("mutation while pending finalize reports NotOpen", ErrorCode::NotOpen, unexpected.setTitle("unused").code));
            static_cast<void>(
                context.expectEq("reopen before finalization is rejected", ErrorCode::AlreadyOpen, unexpected.open(description).code));
            static_cast<void>(context.expectEq(
                "native handle is unavailable while pending finalize",
                ErrorCode::NotOpen,
                Window::Native::Win32::getHandle(unexpected).status.code));
            Window::Types::Event event;
            static_cast<void>(context.expectTrue("pending finalize retains a terminal event", unexpected.popEvent(event)));
            static_cast<void>(
                context.expectTrue("terminal event is typed ClosedEvent", event.getIf<Window::Types::ClosedEvent>() != nullptr));
            static_cast<void>(context.expectTrue("controlled finalization succeeds", unexpected.close().ok()));
            static_cast<void>(context.expectEq(
                "controlled finalization reaches Closed",
                Window::Types::LifetimeState::Closed,
                unexpected.lifetimeState()));
            static_cast<void>(context.expectTrue("Window reopens after finalization", unexpected.open(description, 4).ok()));
            static_cast<void>(context.expectTrue("reopened Window closes normally", unexpected.close().ok()));
        }

        auto deferred = std::make_unique<Window::Window>();
        static_cast<void>(context.expectTrue("deferred-destruction fixture opens", deferred->open(description, 4).ok()));
        HWND deferredHandle = nullptr;
        if (deferred->isOpen())
            deferredHandle = Window::Native::Win32::getHandle(*deferred).handle.window;
        std::thread destroyer(
            [owned = std::move(deferred)]() mutable
            {
                owned.reset();
            });
        destroyer.join();
        static_cast<void>(context.expectTrue("wrong-thread destructor leaves cleanup queued", IsWindow(deferredHandle) != FALSE));
        static_cast<void>(context.expectTrue("owner pump drains deferred cleanup", Window::pollEvents().status.ok()));
        static_cast<void>(context.expectFalse("deferred owner cleanup destroys HWND", IsWindow(deferredHandle) != FALSE));

        std::unique_ptr<Window::Window> survivingObject;
        HWND ownerExitHandle = nullptr;
        std::thread ownerThread(
            [&]
            {
                auto owned = std::make_unique<Window::Window>();
                if (owned->open(description, 4).ok())
                    ownerExitHandle = Window::Native::Win32::getHandle(*owned).handle.window;
                survivingObject = std::move(owned);
            });
        ownerThread.join();
        static_cast<void>(context.expectTrue("owner-exit fixture created a native HWND", ownerExitHandle != nullptr));
        static_cast<void>(context.expectFalse("thread-local dispatcher destroys HWND on owner exit", IsWindow(ownerExitHandle) != FALSE));
        if (survivingObject)
        {
            static_cast<void>(context.expectEq(
                "portable object observes Closed after owner exit",
                Window::Types::LifetimeState::Closed,
                survivingObject->lifetimeState()));
            survivingObject.reset();
        }
    }

    void testPointerHitMask(TestSupport::Context &context)
    {
        namespace Feedback = Window::Renderer;
        Window::Types::Description description;
        description.title = "Window pointer-mask validation";
        description.clientSize = {241, 161};
        description.visible = false;

        Window::Window window;
        static_cast<void>(context.expectTrue("pointer-mask fixture opens", window.open(description, 8).ok()));
        if (!window.isOpen())
            return;

        static_cast<void>(context.expectEq(
            "unadvertised native HitMask bridge is unsupported",
            ErrorCode::Unsupported,
            Feedback::beginPointerHitMaskUpdate(window).status.code));
        Window::TestHooks::enablePointerHitMaskBridge(window);

        Window::Types::PixelSize size = window.framebufferSize();
        if ((static_cast<std::size_t>(size.width) * size.height) % 64U == 0)
        {
            static_cast<void>(window.setClientSize({description.clientSize.width + 1, description.clientSize.height}));
            size = window.framebufferSize();
        }
        const std::size_t wordCount = Feedback::requiredPointerHitMaskWords(size);
        std::vector<std::uint64_t> words(wordCount);
        words.front() = 1;
        const std::size_t lastPixel = static_cast<std::size_t>(size.width) * size.height - 1U;
        words[lastPixel / 64U] |= std::uint64_t{1} << (lastPixel % 64U);

        const Feedback::PointerHitMaskTargetResult first = Feedback::beginPointerHitMaskUpdate(window);
        static_cast<void>(context.expectTrue("Window creates a nonzero mask generation", first.status.ok() && first.target.generation != 0));
        static_cast<void>(context.expectEq("target snapshots framebuffer size", size, first.target.framebufferSize));
        static_cast<void>(context.expectEq("target reports exact word count", wordCount, first.target.requiredWordCount));
        Window::TestHooks::failNext(Window::TestHooks::FailurePoint::Allocation);
        static_cast<void>(context.expectEq(
            "first mask allocation failure is reported",
            ErrorCode::OutOfMemory,
            Feedback::publishPointerHitMask(window, first.target.generation, words).code));
        static_cast<void>(
            context.expectEq("failed first publication keeps no mask", std::size_t{0}, Window::TestHooks::pointerHitMaskWordCount(window)));

        static_cast<void>(
            context.expectTrue("first mask publication succeeds", Feedback::publishPointerHitMask(window, first.target.generation, words).ok()));
        static_cast<void>(context.expectTrue("active mask is reported", Feedback::hasPointerHitMask(window)));
        static_cast<void>(context.expectEq(
            "mask stores exact word count",
            wordCount,
            Window::TestHooks::pointerHitMaskWordCount(window)));
        static_cast<void>(context.expectEq(
            "mask stores first physical pixel",
            std::uint64_t{1},
            Window::TestHooks::pointerHitMaskWord(window, 0) & 1U));
        static_cast<void>(context.expectTrue(
            "mask stores last physical pixel",
            (Window::TestHooks::pointerHitMaskWord(window, lastPixel / 64U) & (std::uint64_t{1} << (lastPixel % 64U))) != 0));
        static_cast<void>(context.expectTrue(
            "first logical position samples the first interactive pixel",
            Window::TestHooks::pointerHitMaskAccepts(window, {0, 0})));
        static_cast<void>(context.expectTrue(
            "out-of-range sampling uses interactive fallback",
            Window::TestHooks::pointerHitMaskAccepts(window, {-1, -1})));

        const void *storage = Window::TestHooks::pointerHitMaskStorage(window);
        words.front() = 2;
        const Feedback::PointerHitMaskTargetResult stale = Feedback::beginPointerHitMaskUpdate(window);
        const Feedback::PointerHitMaskTargetResult newer = Feedback::beginPointerHitMaskUpdate(window);
        static_cast<void>(context.expectTrue(
            "Window generations increase monotonically",
            stale.status.ok() && newer.status.ok() && newer.target.generation > stale.target.generation));
        static_cast<void>(context.expectEq(
            "superseded outstanding update is interrupted",
            ErrorCode::Interrupted,
            Feedback::publishPointerHitMask(window, stale.target.generation, words).code));
        static_cast<void>(
            context.expectTrue("newer same-size mask succeeds", Feedback::publishPointerHitMask(window, newer.target.generation, words).ok()));
        static_cast<void>(
            context.expectEq("same-size publication reuses storage", storage, Window::TestHooks::pointerHitMaskStorage(window)));
        static_cast<void>(context.expectEq(
            "newer publication updates revision",
            newer.target.generation,
            Window::TestHooks::pointerHitMaskGeneration(window)));
        static_cast<void>(context.expectEq(
            "stale publication is rejected",
            ErrorCode::Interrupted,
            Feedback::publishPointerHitMask(window, newer.target.generation, words).code));
        static_cast<void>(context.expectEq(
            "stale publication preserves active data",
            std::uint64_t{2},
            Window::TestHooks::pointerHitMaskWord(window, 0)));

        if (lastPixel % 64U != 63U)
        {
            std::vector<std::uint64_t> invalidTrailing = words;
            invalidTrailing.back() |= std::uint64_t{1} << 63U;
            const Feedback::PointerHitMaskTargetResult trailing = Feedback::beginPointerHitMaskUpdate(window);
            static_cast<void>(context.expectEq(
                "set trailing bits are rejected",
                ErrorCode::InvalidArgument,
                Feedback::publishPointerHitMask(window, trailing.target.generation, invalidTrailing).code));
            static_cast<void>(context.expectEq(
                "invalid trailing bits preserve revision",
                newer.target.generation,
                Window::TestHooks::pointerHitMaskGeneration(window)));
        }

        const Window::Types::ScreenPosition originalPosition = window.clientPosition();
        static_cast<void>(window.setClientPosition({originalPosition.x + 1, originalPosition.y + 1}));
        static_cast<void>(context.expectEq(
            "movement preserves active mask revision",
            newer.target.generation,
            Window::TestHooks::pointerHitMaskGeneration(window)));
        const Feedback::PointerHitMaskTargetResult beforeResize = Feedback::beginPointerHitMaskUpdate(window);
        static_cast<void>(window.setClientSize({window.clientSize().width + 1, window.clientSize().height}));
        static_cast<void>(context.expectEq(
            "framebuffer resize invalidates active mask",
            std::uint64_t{0},
            Window::TestHooks::pointerHitMaskGeneration(window)));
        static_cast<void>(context.expectFalse("resize removes active-mask state", Feedback::hasPointerHitMask(window)));
        static_cast<void>(context.expectEq(
            "pre-resize update is interrupted",
            ErrorCode::Interrupted,
            Feedback::publishPointerHitMask(window, beforeResize.target.generation, words).code));
        static_cast<void>(context.expectTrue("mask clear is idempotent", Feedback::clearPointerHitMask(window).ok()));

        const Feedback::PointerHitMaskTargetResult beforeClose = Feedback::beginPointerHitMaskUpdate(window);
        static_cast<void>(context.expectTrue("pointer-mask fixture closes", window.close().ok()));
        static_cast<void>(context.expectTrue("pointer-mask fixture reopens", window.open(description, 8).ok()));
        if (window.isOpen())
        {
            Window::TestHooks::enablePointerHitMaskBridge(window);
            static_cast<void>(context.expectEq(
                "pre-close generation stays stale after reopen",
                ErrorCode::Interrupted,
                Feedback::publishPointerHitMask(window, beforeClose.target.generation, words).code));
            Window::TestHooks::setPointerHitMaskGeneration(window, std::numeric_limits<std::uint64_t>::max() - 1U);
            const Feedback::PointerHitMaskTargetResult lastGeneration = Feedback::beginPointerHitMaskUpdate(window);
            static_cast<void>(context.expectEq(
                "last representable generation is deterministic",
                std::numeric_limits<std::uint64_t>::max(),
                lastGeneration.target.generation));
            static_cast<void>(context.expectEq(
                "generation overflow never wraps",
                ErrorCode::ResourceBusy,
                Feedback::beginPointerHitMaskUpdate(window).status.code));
            static_cast<void>(window.close());
        }
    }
#endif

    void testNativeEventTranslation(TestSupport::Context &context)
    {
        Window::Types::Description description;
        description.title = "Window native event validation";
        description.clientSize = {260, 170};
        description.visible = false;
        description.acceptsFileDrops = true;

        Window::Window window;
        static_cast<void>(context.expectTrue("native event fixture opens", window.open(description, 64).ok()));
        if (!window.isOpen())
            return;

        const Window::Native::Win32::HandleResult handle = Window::Native::Win32::getHandle(window);
        static_cast<void>(context.expectTrue("native event fixture exposes HWND", handle.status.ok() && handle.handle.window != nullptr));
        if (!handle.status.ok() || handle.handle.window == nullptr)
        {
            static_cast<void>(window.close());
            return;
        }

        window.clearEvents();
        static_cast<void>(window.setClientSize({300, 210}));
        static_cast<void>(context.expectTrue(
            "native resize translates to ClientSizeChangedEvent",
            consumeEventOfType<Window::Types::ClientSizeChangedEvent>(window)));

        window.clearEvents();
        static_cast<void>(window.setMode({.mode = Window::Types::WindowMode::BorderlessFullscreen, .monitor = window.currentMonitor()}));
        static_cast<void>(
            context.expectTrue("fullscreen transition translates to ModeChangedEvent", consumeEventOfType<Window::Types::ModeChangedEvent>(window)));
#if INTERNAL_WINDOW_TEST_HOOKS
        window.clearEvents();
        static_cast<void>(context.expectTrue(
            "synthetic monitor removal recovery succeeds",
            Window::TestHooks::simulateFullscreenMonitorRemoval(window).ok()));
        static_cast<void>(
            context.expectEq("monitor removal recovers Windowed mode", Window::Types::WindowMode::Windowed, window.mode()));
        static_cast<void>(context.expectFalse(
            "monitor removal clears fullscreen monitor",
            window.fullscreenInfo().monitor.valid()));
        Window::Types::Event recoveryEvent;
        static_cast<void>(context.expectTrue("recovery queues display event first", window.popEvent(recoveryEvent)));
        static_cast<void>(context.expectTrue(
            "first recovery event is display configuration",
            recoveryEvent.getIf<Window::Types::DisplayConfigurationChangedEvent>() != nullptr));
        static_cast<void>(context.expectTrue("recovery queues mode event second", window.popEvent(recoveryEvent)));
        static_cast<void>(context.expectTrue(
            "second recovery event is mode change",
            recoveryEvent.getIf<Window::Types::ModeChangedEvent>() != nullptr));
        window.clearEvents();
        static_cast<void>(window.setMode({.mode = Window::Types::WindowMode::BorderlessFullscreen, .monitor = window.currentMonitor()}));
#endif
        window.clearEvents();
        static_cast<void>(window.setMode({}));
        static_cast<void>(
            context.expectTrue("windowed restoration translates to ModeChangedEvent", consumeEventOfType<Window::Types::ModeChangedEvent>(window)));

        static_cast<void>(SendMessageW(handle.handle.window, WM_SHOWWINDOW, FALSE, 0));
        window.clearEvents();
        static_cast<void>(SendMessageW(handle.handle.window, WM_SHOWWINDOW, TRUE, 0));
        static_cast<void>(context.expectTrue("WM_SHOWWINDOW updates visibility cache", window.isVisible()));
        static_cast<void>(context.expectTrue(
            "WM_SHOWWINDOW translates to VisibilityChangedEvent",
            consumeEventOfType<Window::Types::VisibilityChangedEvent>(window)));
        window.clearEvents();
        static_cast<void>(SendMessageW(handle.handle.window, WM_SHOWWINDOW, FALSE, 0));
        static_cast<void>(context.expectFalse("synthetic hide restores visibility cache", window.isVisible()));
        static_cast<void>(consumeEventOfType<Window::Types::VisibilityChangedEvent>(window));

        window.clearEvents();
        static_cast<void>(SendMessageW(handle.handle.window, WM_SIZE, SIZE_MINIMIZED, MAKELPARAM(300, 210)));
        static_cast<void>(context.expectTrue("WM_SIZE minimize updates presentation cache", window.isMinimized()));
        static_cast<void>(context.expectTrue(
            "WM_SIZE translates to PresentationStateChangedEvent",
            consumeEventOfType<Window::Types::PresentationStateChangedEvent>(window)));
        window.clearEvents();
        static_cast<void>(SendMessageW(handle.handle.window, WM_SIZE, SIZE_RESTORED, MAKELPARAM(300, 210)));
        static_cast<void>(context.expectFalse("synthetic restore resets minimized cache", window.isMinimized()));
        static_cast<void>(consumeEventOfType<Window::Types::PresentationStateChangedEvent>(window));

        static_cast<void>(SendMessageW(handle.handle.window, WM_KILLFOCUS, 0, 0));
        window.clearEvents();
        static_cast<void>(SendMessageW(handle.handle.window, WM_SETFOCUS, 0, 0));
        static_cast<void>(context.expectTrue("WM_SETFOCUS updates cache", window.isFocused()));
        static_cast<void>(
            context.expectTrue("WM_SETFOCUS translates to FocusChangedEvent", consumeEventOfType<Window::Types::FocusChangedEvent>(window)));
        window.clearEvents();
        static_cast<void>(SendMessageW(handle.handle.window, WM_KILLFOCUS, 0, 0));
        static_cast<void>(context.expectFalse("WM_KILLFOCUS updates cache", window.isFocused()));
        static_cast<void>(
            context.expectTrue("WM_KILLFOCUS translates to FocusChangedEvent", consumeEventOfType<Window::Types::FocusChangedEvent>(window)));

        window.clearEvents();
        static_cast<void>(SendMessageW(handle.handle.window, WM_MOUSEMOVE, 0, MAKELPARAM(2, 2)));
        static_cast<void>(context.expectTrue("WM_MOUSEMOVE updates cursor-presence cache", window.isCursorInside()));
        static_cast<void>(context.expectTrue(
            "WM_MOUSEMOVE translates to CursorPresenceChangedEvent",
            consumeEventOfType<Window::Types::CursorPresenceChangedEvent>(window)));
        window.clearEvents();
        static_cast<void>(SendMessageW(handle.handle.window, WM_MOUSELEAVE, 0, 0));
        static_cast<void>(context.expectFalse("WM_MOUSELEAVE updates cursor-presence cache", window.isCursorInside()));
        static_cast<void>(context.expectTrue(
            "WM_MOUSELEAVE translates to CursorPresenceChangedEvent",
            consumeEventOfType<Window::Types::CursorPresenceChangedEvent>(window)));

        window.clearEvents();
        static_cast<void>(SendMessageW(handle.handle.window, WM_DISPLAYCHANGE, 32, MAKELPARAM(1920, 1080)));
        static_cast<void>(context.expectTrue(
            "WM_DISPLAYCHANGE translates to DisplayConfigurationChangedEvent",
            consumeEventOfType<Window::Types::DisplayConfigurationChangedEvent>(window)));

        window.clearEvents();
        const std::wstring droppedPath = L"C:/GameWIP/window-drop-test.txt";
        const std::size_t dropBytes = sizeof(DROPFILES) + (droppedPath.size() + 2) * sizeof(wchar_t);
        HGLOBAL dropMemory = GlobalAlloc(GHND, dropBytes);
        static_cast<void>(context.expectTrue("synthetic drop payload allocates", dropMemory != nullptr));
        if (dropMemory != nullptr)
        {
            auto *drop = static_cast<DROPFILES *>(GlobalLock(dropMemory));
            static_cast<void>(context.expectTrue("synthetic drop payload locks", drop != nullptr));
            if (drop != nullptr)
            {
                drop->pFiles = sizeof(DROPFILES);
                drop->pt = {4, 6};
                drop->fNC = FALSE;
                drop->fWide = TRUE;
                auto *path = reinterpret_cast<wchar_t *>(reinterpret_cast<std::byte *>(drop) + sizeof(DROPFILES));
                std::copy(droppedPath.begin(), droppedPath.end(), path);
                path[droppedPath.size()] = wchar_t{};
                path[droppedPath.size() + 1] = wchar_t{};
                static_cast<void>(GlobalUnlock(dropMemory));
                static_cast<void>(SendMessageW(handle.handle.window, WM_DROPFILES, reinterpret_cast<WPARAM>(dropMemory), 0));
                dropMemory = nullptr;
                static_cast<void>(
                    context.expectTrue("WM_DROPFILES translates to FilesDroppedEvent", consumeEventOfType<Window::Types::FilesDroppedEvent>(window)));
            }
            if (dropMemory != nullptr)
                static_cast<void>(GlobalFree(dropMemory));
        }

        window.clearEvents();
        static_cast<void>(SendMessageW(handle.handle.window, WM_PAINT, 0, 0));
        static_cast<void>(
            context.expectTrue("WM_PAINT translates to RedrawRequestedEvent", consumeEventOfType<Window::Types::RedrawRequestedEvent>(window)));

        window.clearEvents();
        static_cast<void>(SendMessageW(handle.handle.window, WM_CLOSE, 0, 0));
        static_cast<void>(context.expectTrue("WM_CLOSE sets sticky close intent", window.closeRequested()));
        static_cast<void>(
            context.expectTrue("WM_CLOSE translates to CloseRequestedEvent", consumeEventOfType<Window::Types::CloseRequestedEvent>(window)));
        static_cast<void>(window.clearCloseRequest());
        static_cast<void>(context.expectTrue("native event fixture closes", window.close().ok()));
    }

    void testRendererOcclusionFeedback(TestSupport::Context &context)
    {
        namespace Feedback = Window::Renderer;
        using Capability = Window::Types::Capability;

        Window::Window closed;
        static_cast<void>(
            context.expectEq("closed Window rejects provider attachment", ErrorCode::NotOpen, Feedback::attachOcclusionProvider(closed).code));
        static_cast<void>(
            context.expectEq("closed Window rejects occlusion report", ErrorCode::NotOpen, Feedback::reportOcclusion(closed, true).code));
        static_cast<void>(
            context.expectEq("closed Window rejects provider detach", ErrorCode::NotOpen, Feedback::detachOcclusionProvider(closed).code));

        Window::Types::Description description;
        description.title = "Window renderer feedback validation";
        description.clientSize = {240, 160};
        description.visible = false;

        Window::Window window;
        static_cast<void>(context.expectTrue("renderer feedback fixture opens", window.open(description, 8).ok()));
        if (!window.isOpen())
            return;

        static_cast<void>(context.expectFalse("global occlusion support needs a provider", Window::supports(Capability::OcclusionReporting)));
        static_cast<void>(context.expectFalse("Window initially lacks occlusion provider", window.supports(Capability::OcclusionReporting)));
        static_cast<void>(context.expectEq("report before provider is rejected", ErrorCode::NotOpen, Feedback::reportOcclusion(window, true).code));
        static_cast<void>(context.expectTrue("occlusion provider attaches", Feedback::attachOcclusionProvider(window).ok()));
        static_cast<void>(context.expectTrue("attached Window advertises occlusion reporting", window.supports(Capability::OcclusionReporting)));
        static_cast<void>(context.expectEq("second provider is rejected", ErrorCode::AlreadyOpen, Feedback::attachOcclusionProvider(window).code));

        window.clearEvents();
        static_cast<void>(context.expectTrue("occluded report succeeds", Feedback::reportOcclusion(window, true).ok()));
        static_cast<void>(context.expectTrue("occluded report updates cache", window.isOccluded()));
        Window::Types::Event event;
        static_cast<void>(context.expectTrue("occluded transition queues event", window.popEvent(event)));
        const auto *occludedEvent = event.getIf<Window::Types::OcclusionChangedEvent>();
        static_cast<void>(context.expectTrue("occluded event has typed payload", occludedEvent != nullptr));
        if (occludedEvent != nullptr)
            static_cast<void>(context.expectTrue("occluded event carries true", occludedEvent->occluded));

        static_cast<void>(context.expectTrue("duplicate occlusion report succeeds", Feedback::reportOcclusion(window, true).ok()));
        static_cast<void>(context.expectEq("duplicate report does not queue event", std::size_t{0}, window.eventQueueInfo().pendingEvents));

        ErrorCode wrongThreadCode = ErrorCode::Success;
        std::thread worker(
            [&window, &wrongThreadCode]
            {
                wrongThreadCode = Window::Renderer::reportOcclusion(window, false).code;
            });
        worker.join();
        static_cast<void>(context.expectEq("wrong-thread renderer feedback is rejected", ErrorCode::ResourceBusy, wrongThreadCode));
        static_cast<void>(context.expectTrue("wrong-thread report preserves cache", window.isOccluded()));

        window.clearEvents();
        static_cast<void>(context.expectTrue("provider detaches", Feedback::detachOcclusionProvider(window).ok()));
        static_cast<void>(context.expectFalse("detach disables per-window capability", window.supports(Capability::OcclusionReporting)));
        static_cast<void>(context.expectFalse("detach resets occlusion cache", window.isOccluded()));
        static_cast<void>(context.expectTrue("detach queues final false transition", window.popEvent(event)));
        const auto *visibleEvent = event.getIf<Window::Types::OcclusionChangedEvent>();
        static_cast<void>(context.expectTrue("detach event has typed payload", visibleEvent != nullptr));
        if (visibleEvent != nullptr)
            static_cast<void>(context.expectFalse("detach event carries false", visibleEvent->occluded));
        static_cast<void>(context.expectEq("report after detach is rejected", ErrorCode::NotOpen, Feedback::reportOcclusion(window, false).code));
        static_cast<void>(context.expectTrue("repeated detach succeeds", Feedback::detachOcclusionProvider(window).ok()));

        static_cast<void>(context.expectTrue("provider reattaches", Feedback::attachOcclusionProvider(window).ok()));
        static_cast<void>(context.expectTrue("feedback fixture closes with provider attached", window.close().ok()));

        Window::Window overflow;
        static_cast<void>(context.expectTrue("occlusion overflow fixture opens", overflow.open(description, 1).ok()));
        if (overflow.isOpen())
        {
            static_cast<void>(Feedback::attachOcclusionProvider(overflow));
            static_cast<void>(Feedback::reportOcclusion(overflow, true));
            static_cast<void>(Feedback::reportOcclusion(overflow, false));
            static_cast<void>(context.expectFalse("dropped transition still updates cache", overflow.isOccluded()));
            static_cast<void>(
                context.expectEq("full queue counts dropped occlusion event", std::uint64_t{1}, overflow.eventQueueInfo().droppedEvents));
            static_cast<void>(overflow.close());
        }
    }

    void testHiddenNativeWindow(TestSupport::Context &context)
    {
        Window::Types::Description description;
        description.title = "GameWIP Window validation";
        description.clientSize = {320, 200};
        description.visible = false;

        Window::Window owner;
        const IO::Types::Status openStatus = owner.open(description, 32);
        static_cast<void>(context.expectTrue("hidden native Window opens", openStatus.ok()));
        if (!openStatus.ok())
            return;
        static_cast<void>(context.expectTrue("open Window has an id", owner.id().valid()));
        static_cast<void>(
            context.expectEq("open Window reports Open lifetime", Window::Types::LifetimeState::Open, owner.lifetimeState()));
        static_cast<void>(context.expectEq("title cache matches", std::string_view{description.title}, owner.title()));
        static_cast<void>(
            context.expectEq("internal event storage is reported", Window::Types::EventStorageKind::Internal, owner.eventQueueInfo().storage));

        const Window::Native::Win32::HandleResult handle = Window::Native::Win32::getHandle(owner);
        static_cast<void>(context.expectTrue("native handle adapter succeeds", handle.status.ok()));
        static_cast<void>(context.expectTrue("native HWND is non-null", handle.handle.window != nullptr));

        static_cast<void>(context.expectTrue("UTF-8 title update succeeds", owner.setTitle("Fenêtre GameWIP").ok()));
        static_cast<void>(context.expectTrue("logical client resize succeeds", owner.setClientSize({360, 240}).ok()));
        static_cast<void>(context.expectEq("client-size cache reports applied resize", Window::Types::LogicalSize{360, 240}, owner.clientSize()));
        const Window::Types::LogicalSize beforePolicyChange = owner.clientSize();
        static_cast<void>(context.expectTrue(
            "DPI resize policy changes for future transitions",
            owner.setDpiResizePolicy(Window::Types::DpiResizePolicy::PreservePhysicalClientSize).ok()));
        static_cast<void>(context.expectEq(
            "DPI policy setter does not resize immediately",
            beforePolicyChange,
            owner.clientSize()));
        static_cast<void>(context.expectEq(
            "unknown runtime DPI policy is invalid",
            ErrorCode::InvalidArgument,
            owner.setDpiResizePolicy(static_cast<Window::Types::DpiResizePolicy>(99)).code));
        static_cast<void>(context.expectTrue(
            "default DPI policy restores",
            owner.setDpiResizePolicy(Window::Types::DpiResizePolicy::PreserveLogicalClientSize).ok()));

        const Window::Types::LogicalPosition localPoint{12, 18};
        const Window::Types::ScreenPositionResult screenPoint = owner.clientToScreen(localPoint);
        static_cast<void>(context.expectTrue("client-to-screen conversion succeeds", screenPoint.status.ok()));
        if (screenPoint.status.ok())
        {
            const Window::Types::LogicalPositionResult roundTrip = owner.screenToClient(screenPoint.position);
            static_cast<void>(context.expectTrue("screen-to-client conversion succeeds", roundTrip.status.ok()));
            if (roundTrip.status.ok())
                static_cast<void>(context.expectEq("coordinate conversion round trips", localPoint, roundTrip.position));
        }

        static_cast<void>(context.expectTrue(
            "borderless fullscreen transition succeeds",
            owner.setMode({.mode = Window::Types::WindowMode::BorderlessFullscreen, .monitor = owner.currentMonitor()}).ok()));
        static_cast<void>(context.expectEq("borderless mode is cached", Window::Types::WindowMode::BorderlessFullscreen, owner.mode()));
        static_cast<void>(context.expectTrue("windowed placement restores", owner.setMode({}).ok()));
        static_cast<void>(context.expectEq("windowed mode is cached", Window::Types::WindowMode::Windowed, owner.mode()));

        static_cast<void>(
            context.expectTrue("runtime borderless decorations succeed", owner.setDecorationMode(Window::Types::DecorationMode::Borderless).ok()));
        static_cast<void>(context.expectTrue("system decorations restore", owner.setDecorationMode(Window::Types::DecorationMode::System).ok()));
        static_cast<void>(context.expectTrue(
            "click-through pointer policy succeeds",
            owner.setPointerInputLayout({.mode = Window::Types::PointerInputMode::ClickThrough}).ok()));
        const LONG_PTR clickThroughStyle = GetWindowLongPtrW(handle.handle.window, GWL_EXSTYLE);
        static_cast<void>(context.expectTrue(
            "click-through uses documented layered hit-testing styles",
            (clickThroughStyle & WS_EX_LAYERED) != 0 && (clickThroughStyle & WS_EX_TRANSPARENT) != 0));
        static_cast<void>(context.expectTrue("normal pointer policy restores", owner.setPointerInputLayout({}).ok()));
        const LONG_PTR normalPointerStyle = GetWindowLongPtrW(handle.handle.window, GWL_EXSTYLE);
        static_cast<void>(context.expectFalse(
            "normal pointer policy removes transparent hit-testing style",
            (normalPointerStyle & WS_EX_TRANSPARENT) != 0));
        const std::array<Window::Types::LogicalRect, 1> pointerRegions{{{{0, 0}, {10, 10}}}};
        static_cast<void>(context.expectEq(
            "rectangular pointer routing remains Unsupported",
            ErrorCode::Unsupported,
            owner.setPointerInputLayout(
                     {.mode = Window::Types::PointerInputMode::AcceptRegions, .regions = pointerRegions})
                .code));
        static_cast<void>(context.expectEq(
            "unsupported region request preserves pointer mode",
            Window::Types::PointerInputMode::Normal,
            owner.pointerInputMode()));
        static_cast<void>(context.expectTrue("opacity update succeeds", owner.setOpacity(0.8F).ok()));
        static_cast<void>(context.expectTrue("opacity restores", owner.setOpacity(1.0F).ok()));
        static_cast<void>(context.expectTrue("file-drop enable succeeds", owner.setFileDropEnabled(true).ok()));
        static_cast<void>(context.expectTrue("file-drop disable succeeds", owner.setFileDropEnabled(false).ok()));
        static_cast<void>(context.expectTrue("interaction disable succeeds", owner.setUserInteractionEnabled(false).ok()));
        static_cast<void>(context.expectTrue("interaction re-enable succeeds", owner.setUserInteractionEnabled(true).ok()));

        static_cast<void>(context.expectEq(
            "resize cannot be disabled while maximize remains enabled",
            ErrorCode::InvalidArgument,
            owner.setResizable(false).code));
        Window::Types::WindowControls controls = owner.windowControls();
        controls.maximizable = false;
        static_cast<void>(context.expectTrue("maximize is explicitly disabled", owner.setWindowControls(controls).ok()));
        static_cast<void>(context.expectTrue("resize can then be disabled", owner.setResizable(false).ok()));
        controls.maximizable = true;
        static_cast<void>(context.expectEq(
            "maximize cannot be enabled while resize is disabled",
            ErrorCode::InvalidArgument,
            owner.setWindowControls(controls).code));
        controls.maximizable = false;
        controls.closable = false;
        controls.minimizable = false;
        static_cast<void>(context.expectTrue(
            "close and minimize remain independent from resize",
            owner.setWindowControls(controls).ok()));
        static_cast<void>(context.expectTrue("resize can be re-enabled explicitly", owner.setResizable(true).ok()));
        controls.maximizable = true;
        controls.closable = true;
        controls.minimizable = true;
        static_cast<void>(context.expectTrue("maximize restores after resize", owner.setWindowControls(controls).ok()));

        const Window::Types::Capabilities capabilities = Window::getCapabilities().capabilities;
        static_cast<void>(context.expectFalse(
            "Win32 does not advertise cross-application pointer regions",
            capabilities.supports(Window::Types::Capability::PointerRegions)));
        static_cast<void>(context.expectEq(
            "unsupported pointer regions expose zero native limit",
            std::uint32_t{0},
            capabilities.maximumPointerInputRegions));
        if (capabilities.supports(Window::Types::Capability::SystemBackdrop))
        {
            static_cast<void>(context.expectTrue(
                "runtime-supported system backdrop applies",
                owner.setBackdropEffect(Window::Types::BackdropEffect::Automatic).ok()));
            static_cast<void>(context.expectTrue(
                "system backdrop clears",
                owner.setBackdropEffect(Window::Types::BackdropEffect::None).ok()));
        }
        else
        {
            static_cast<void>(context.expectEq(
                "unsupported system backdrop is rejected",
                ErrorCode::Unsupported,
                owner.setBackdropEffect(Window::Types::BackdropEffect::Automatic).code));
        }

        const std::array<std::byte, 4> redPixel{std::byte{0xFF}, std::byte{0x00}, std::byte{0x00}, std::byte{0xFF}};
        const std::array iconImages{Window::Types::IconImageView{{1, 1}, redPixel}};
        static_cast<void>(context.expectTrue("RGBA icon copy succeeds", owner.setIcon(iconImages).ok()));
        static_cast<void>(context.expectTrue("icon clears", owner.clearIcon().ok()));

        static_cast<void>(context.expectTrue("programmatic close request succeeds", owner.requestClose().ok()));
        static_cast<void>(context.expectTrue("programmatic close request is sticky", owner.closeRequested()));
        static_cast<void>(context.expectTrue("clear close request succeeds", owner.clearCloseRequest().ok()));
        static_cast<void>(context.expectFalse("close request clears", owner.closeRequested()));

        Window::Types::Description childDescription = description;
        childDescription.owner = owner.id();
        Window::Window child;
        static_cast<void>(context.expectTrue("same-thread owned Window opens", child.open(childDescription, 16).ok()));
        if (child.isOpen())
        {
            static_cast<void>(context.expectEq("owner identity is cached", owner.id(), child.ownerId()));
            const HWND childHandle = Window::Native::Win32::getHandle(child).handle.window;
            static_cast<void>(context.expectFalse(
                "owned Window has no independent taskbar style",
                (GetWindowLongPtrW(childHandle, GWL_EXSTYLE) & WS_EX_APPWINDOW) != 0));
            child.clearEvents();
            static_cast<void>(context.expectTrue("owner can be cleared at runtime", child.setOwner({}).ok()));
            static_cast<void>(context.expectTrue(
                "owner removal restores independent taskbar style",
                (GetWindowLongPtrW(childHandle, GWL_EXSTYLE) & WS_EX_APPWINDOW) != 0));
            static_cast<void>(
                context.expectTrue("owner clear translates to OwnerChangedEvent", consumeEventOfType<Window::Types::OwnerChangedEvent>(child)));
            child.clearEvents();
            static_cast<void>(context.expectTrue("owner can be restored at runtime", child.setOwner(owner.id()).ok()));
            static_cast<void>(context.expectFalse(
                "owner restoration removes independent taskbar style",
                (GetWindowLongPtrW(childHandle, GWL_EXSTYLE) & WS_EX_APPWINDOW) != 0));
            static_cast<void>(
                context.expectTrue("owner restore translates to OwnerChangedEvent", consumeEventOfType<Window::Types::OwnerChangedEvent>(child)));
        }
        static_cast<void>(context.expectTrue("owner closes", owner.close().ok()));
        if (child.isOpen())
        {
            static_cast<void>(context.expectFalse("closing owner clears child owner identity", child.ownerId().valid()));
            static_cast<void>(context.expectTrue(
                "closing owner restores child taskbar style",
                (GetWindowLongPtrW(Window::Native::Win32::getHandle(child).handle.window, GWL_EXSTYLE) & WS_EX_APPWINDOW) != 0));
            static_cast<void>(context.expectTrue("child closes after owner", child.close().ok()));
        }
        static_cast<void>(context.expectFalse("closed owner reports closed", owner.isOpen()));
        static_cast<void>(context.expectTrue("repeated native close succeeds", owner.close().ok()));

        Window::Types::Description alphaDescription = description;
        alphaDescription.title = "Transparent framebuffer capability validation";
        alphaDescription.transparentFramebuffer = true;
        Window::Window alphaWindow;
        const IO::Types::Status alphaStatus = alphaWindow.open(alphaDescription, 4);
        if (capabilities.supports(Window::Types::Capability::TransparentFramebuffer))
        {
            static_cast<void>(context.expectTrue("runtime-supported transparent framebuffer opens", alphaStatus.ok()));
            if (alphaWindow.isOpen())
                static_cast<void>(alphaWindow.close());
        }
        else
        {
            static_cast<void>(context.expectEq(
                "unsupported transparent framebuffer is rejected before open",
                ErrorCode::Unsupported,
                alphaStatus.code));
            static_cast<void>(context.expectFalse("unsupported alpha request creates no HWND", alphaWindow.isOpen()));
        }
    }

    void testMonitors(TestSupport::Context &context)
    {
        const Window::Types::MonitorListResult monitors = Window::getMonitors();
        static_cast<void>(context.expectTrue("monitor enumeration succeeds", monitors.status.ok()));
        static_cast<void>(context.expectFalse("monitor enumeration is nonempty", monitors.monitors.empty()));
        const Window::Types::MonitorInfoResult primary = Window::getPrimaryMonitor();
        static_cast<void>(context.expectTrue("primary monitor query succeeds", primary.status.ok()));
        if (!primary.status.ok())
            return;
        static_cast<void>(context.expectTrue("primary monitor id is valid", primary.monitor.id.valid()));
        static_cast<void>(context.expectTrue("primary monitor is marked primary", primary.monitor.primary));
        static_cast<void>(context.expectTrue("monitor identity resolves", Window::getMonitor(primary.monitor.id).status.ok()));
        const Window::Types::DisplayModeListResult modes = Window::getDisplayModes(primary.monitor.id);
        static_cast<void>(context.expectTrue("display-mode enumeration succeeds", modes.status.ok()));
        static_cast<void>(context.expectFalse("display-mode enumeration is nonempty", modes.displayModes.empty()));
        static_cast<void>(context.expectTrue("current display mode succeeds", Window::getCurrentDisplayMode(primary.monitor.id).status.ok()));
        const Window::Types::DisplayModeResult preferred = Window::getPreferredDisplayMode(primary.monitor.id);
        static_cast<void>(context.expectTrue("DisplayConfig preferred mode succeeds", preferred.status.ok()));
        if (preferred.status.ok())
        {
            static_cast<void>(context.expectTrue(
                "preferred mode has a physical resolution",
                preferred.displayMode.resolution.width != 0 && preferred.displayMode.resolution.height != 0));
        }
        static_cast<void>(context.expectEq("invalid monitor lookup is rejected", ErrorCode::InvalidArgument, Window::getMonitor({}).status.code));
    }
} // namespace

namespace GameWIP::Test
{
    int runWindowTests(int, char **, const WindowTestOptions &options)
    {
        TestSupport::Types::ReportOptions reportOptions;
        reportOptions.writeConsole = true;
        reportOptions.consoleVerbosity =
            options.verboseConsole ? TestSupport::Types::ConsoleVerbosity::Full : TestSupport::Types::ConsoleVerbosity::Minimal;
        reportOptions.writeReport = options.writeReport;
        reportOptions.appendReport = options.appendReport;
        reportOptions.reportPath = options.reportPath;

        TestSupport::Runner runner(reportOptions);
        runner.runSuite("Window passive values and closed state", testPassiveValuesAndClosedState);
        runner.runSuite("Window description validation", testDescriptionValidation);
#if INTERNAL_WINDOW_TEST_HOOKS
        runner.runSuite("Window fixed event queue", testFixedEventQueue);
        runner.runSuite("Window sticky close", testStickyClose);
        runner.runSuite("Window deterministic failure paths", testFailureInjection);
        runner.runSuite("Window threading contracts", testThreadingContracts);
        runner.runSuite("Window exceptional lifetime", testExceptionalLifetime);
        runner.runSuite("Window packed pointer hit mask", testPointerHitMask);
#else
        runner.runSuite(
            "Window fixed event queue",
            [](TestSupport::Context &context)
            {
                context.skip("Window queue hooks", "INTERNAL_WINDOW_TEST_HOOKS is disabled");
            });
#endif
        runner.runSuite("Window hidden native lifecycle", testHiddenNativeWindow);
        runner.runSuite("Window native event translation", testNativeEventTranslation);
        runner.runSuite("Window renderer occlusion feedback", testRendererOcclusionFeedback);
        runner.runSuite("Window monitors and display modes", testMonitors);

        const TestSupport::Types::Summary result = runner.result();
        runner.summary(std::format("Window library self-tests passed={} failed={} skipped={}", result.passed, result.failed, result.skipped));
        return runner.exitCode();
    }
} // namespace GameWIP::Test
