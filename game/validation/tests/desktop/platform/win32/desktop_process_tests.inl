/// @file desktop_process_tests.inl
/// @brief Process-isolated Desktop child modes and shutdown tests.

/// ------------------------------------------------------------
/// Process-isolated child modes
/// ------------------------------------------------------------

inline constexpr wchar_t kManualTestAppUserModelId[] = L"GameWIP.Validation.DesktopManualTests";
inline constexpr std::string_view kStandaloneColorChildArgument = "--desktop-test-child=standalone-color-shutdown";
inline constexpr std::string_view kWindowColorChildArgument = "--desktop-test-child=window-color-shutdown";
inline constexpr std::string_view kOwnerExitColorChildArgument = "--desktop-test-child=owner-exit-color-shutdown";
inline constexpr std::string_view kOwnerExitDragDropChildArgument = "--desktop-test-child=owner-exit-drag-drop-shutdown";
inline constexpr std::string_view kOwnerExitDragDropFailureChildArgument = "--desktop-test-child=owner-exit-drag-drop-revocation-failure";
inline constexpr std::string_view kOwnerExitProgressChildArgument = "--desktop-test-child=owner-exit-progress-shutdown";
inline constexpr std::string_view kOwnerExitProgressFailureChildArgument = "--desktop-test-child=owner-exit-progress-close-failure";
inline constexpr std::string_view kDeferredPumpFailureWaitChildArgument = "--desktop-test-child=deferred-pump-failure-wait";
#if DESKTOP_INTERNAL_TEST_HOOKS
inline constexpr std::string_view kProgressRestoreMessageLazinessChildArgument = "--desktop-test-child=progress-restore-message-laziness";
#endif

static_assert(!std::is_move_constructible_v<Desktop::Window>);
static_assert(!std::is_move_assignable_v<Desktop::Window>);
static_assert(!std::is_copy_constructible_v<Desktop::Window>);
static_assert(!std::is_copy_assignable_v<Desktop::Window>);
static_assert(!std::is_move_constructible_v<Desktop::ChildSurface>);
static_assert(!std::is_move_assignable_v<Desktop::ChildSurface>);
static_assert(!std::is_copy_constructible_v<Desktop::ChildSurface>);
static_assert(!std::is_copy_assignable_v<Desktop::ChildSurface>);
static_assert(!std::is_move_constructible_v<Desktop::ProgressDialog>);
static_assert(!std::is_copy_constructible_v<Desktop::ProgressDialog>);
static_assert(noexcept(Desktop::Events::poll()));
static_assert(noexcept(Desktop::Events::wait()));
static_assert(noexcept(std::declval<Desktop::Window &>().close()));
static_assert(noexcept(Desktop::Display::getColorInfo(Desktop::Types::Display::MonitorId::invalid())));
static_assert(noexcept(Desktop::Display::getColorInfo(std::declval<const Desktop::Window &>())));
static_assert(std::is_same_v<decltype(Desktop::Types::Events::FilesDropped{}.paths)::value_type, GameWIP::FileSystem::Types::Path>);

[[nodiscard]] int runStandaloneColorShutdownChild() noexcept
{
    const auto primary = Desktop::Display::getPrimaryMonitor();
    if(!primary.ok())
    {
        return 2;
    }
    return Desktop::Display::getColorInfo(primary->id).ok() ? 0 : 3;
}

[[nodiscard]] int runWindowColorShutdownChild() noexcept
{
    const auto primary = Desktop::Display::getPrimaryMonitor();
    if(!primary.ok() || !Desktop::Display::getColorInfo(primary->id).ok())
    {
        return 2;
    }
    Desktop::Types::Description description;
    description.title = "Desktop color shutdown child";
    description.windowed.clientSize = {160, 100};
    description.activation.visible = false;
    Desktop::Window window;

    if(!window.open(description, Desktop::Events::owned(4)).ok())
    {
        return 3;
    }

    if(!Desktop::Display::getColorInfo(window).ok())
    {
        return 4;
    }
    const auto handle = Desktop::Native::Win32::getHandle(window);
    if(!handle.status.ok() || handle->window == nullptr || PostMessageW(static_cast<HWND>(handle->window), WM_CLOSE, 0, 0) == FALSE)
    {
        return 5;
    }

    if(!Desktop::Events::poll().status.ok() || !window.hasCloseRequest())
    {
        return 6;
    }
    return window.close().ok() ? 0 : 7;
}

[[nodiscard]] int runOwnerExitColorShutdownChild() noexcept
{
    std::unique_ptr<Desktop::Window> survivingWindow;
    int workerResult = 0;
    std::thread owner(
        [&]
        {
            auto window = std::make_unique<Desktop::Window>();
            Desktop::Types::Description description;
            description.title = "Desktop owner-exit color shutdown child";
            description.windowed.clientSize = {160, 100};
            description.activation.visible = false;

            if(!window->open(description, Desktop::Events::owned(4)).ok())
            {
                workerResult = 2;
                return;
            }

            if(!Desktop::Display::getColorInfo(*window).status.ok())
            {
                workerResult = 3;
                return;
            }
            survivingWindow = std::move(window);
        });
    owner.join();

    if(workerResult != 0)
    {
        return workerResult;
    }

    if(!survivingWindow || survivingWindow->lifetimeState() != Desktop::Types::LifetimeState::Closed)
    {
        return 4;
    }
    survivingWindow.reset();
    return 0;
}

[[nodiscard]] int runOwnerExitDragDropShutdownChild(bool forceRepeatedRevocationFailure) noexcept
{
    std::unique_ptr<Desktop::Window> survivingWindow;
    std::unique_ptr<Desktop::DragDropTarget> survivingTarget;
    Desktop::Types::WindowId retainedWindowId;
    int workerResult = 0;
    std::thread owner(
        [&]
        {
            auto window = std::make_unique<Desktop::Window>();
            auto target = std::make_unique<Desktop::DragDropTarget>();
            Desktop::Types::Description description;
            description.title = "Desktop owner-exit DragDrop shutdown child";
            description.windowed.clientSize = {160, 100};
            description.activation.visible = false;

            if(!window->open(description, Desktop::Events::owned(4)).ok())
            {
                workerResult = 2;
                return;
            }
            namespace DD = Desktop::Types::DragDrop;
            namespace Transfer = Desktop::Types::DataTransfer;
            std::array formats{Transfer::FormatView::text()};
            std::array regions{DD::RegionDescription{DD::RegionId{1}, std::nullopt, formats, DD::EffectPolicy::copy()}};

            if(!target->open(*window, DD::TargetDescription{regions}, DD::QueueConfiguration::owned(2)).ok())
            {
                workerResult = 3;
                return;
            }

#if DESKTOP_INTERNAL_TEST_HOOKS
            if(forceRepeatedRevocationFailure)
            {
                Desktop::Internal::TestHooks::failDragDropRevocations(64);
            }
#else
            static_cast<void>(forceRepeatedRevocationFailure);
#endif
            retainedWindowId = window->id();
            survivingWindow = std::move(window);
            survivingTarget = std::move(target);
        });
    owner.join();

    if(workerResult != 0)
    {
        return workerResult;
    }

    if(!survivingWindow || !survivingTarget)
    {
        return 4;
    }

    if(survivingTarget->isOpen() || survivingTarget->lifetimeState() != Desktop::Types::LifetimeState::NativeDestroyedPendingFinalize ||
       survivingTarget->windowId() != retainedWindowId)
    {
        return 5;
    }

    if(survivingWindow->lifetimeState() != Desktop::Types::LifetimeState::Closed)
    {
        return 6;
    }

    if(!survivingTarget->close().ok())
    {
        return 7;
    }
    survivingTarget.reset();
    survivingWindow.reset();
    return 0;
}

[[nodiscard]] int runOwnerExitProgressShutdownChild(bool forceCloseFailure) noexcept
{
    std::unique_ptr<Desktop::ProgressDialog> survivingProgress;
    int workerResult = 0;
    std::thread owner(
        [&]
        {
            auto progress = std::make_unique<Desktop::ProgressDialog>();
            Desktop::Types::Dialogs::Progress::Description description;
            description.title = "Desktop owner-exit ProgressDialog child";
            description.message = "Dispatcher teardown must finalize this presentation";

            if(!progress->open(description).ok())
            {
                workerResult = 2;
                return;
            }

#if DESKTOP_INTERNAL_TEST_HOOKS
            if(forceCloseFailure)
            {
                Desktop::Internal::TestHooks::failNext(Desktop::Internal::TestHooks::FailurePoint::Close);
            }
#else
            static_cast<void>(forceCloseFailure);
#endif
            survivingProgress = std::move(progress);
        });
    owner.join();

    if(workerResult != 0)
    {
        return workerResult;
    }

    if(!survivingProgress || survivingProgress->ownedByCurrentThread() || survivingProgress->isOpen())
    {
        return 3;
    }
    survivingProgress.reset();
    return 0;
}

[[nodiscard]] int runDeferredPumpFailureWaitChild() noexcept
{
#if !DESKTOP_INTERNAL_TEST_HOOKS
    return 6;
#else
    Desktop::Window owner;
    Desktop::Types::Description windowDescription;
    windowDescription.title = "Deferred pump failure owner";
    windowDescription.windowed.clientSize = {160, 100};
    windowDescription.activation.visible = false;

    if(!owner.open(windowDescription, Desktop::Events::owned(4)).ok())
    {
        return 2;
    }
    Desktop::ProgressDialog progress;
    Desktop::Types::Dialogs::Progress::Description progressDescription;
    progressDescription.title = "Deferred pump failure progress";
    progressDescription.message = "The wait must return the already-recorded error.";
    progressDescription.owner = &owner;
    progressDescription.blocksOwner = true;

    if(!progress.open(progressDescription).ok())
    {
        static_cast<void>(owner.close());
        return 3;
    }

    Desktop::Internal::TestHooks::failNext(Desktop::Internal::TestHooks::FailurePoint::ProgressOwnerRestoreWake);

    if(!Win32TestHooks::destroyNativeProgressDialog(progress).ok())
    {
        static_cast<void>(progress.close());
        static_cast<void>(owner.close());
        return 4;
    }
    const Desktop::Types::Events::PumpResult waitResult = Desktop::Events::wait();
    static_cast<void>(progress.close());
    static_cast<void>(owner.close());
    return waitResult.status.ok() ? 5 : 0;
#endif
}

#if DESKTOP_INTERNAL_TEST_HOOKS
[[nodiscard]] int runProgressRestoreMessageLazinessChild() noexcept
{
    if(Win32TestHooks::progressOwnerRestoreMessageRegistrationAttempted())
    {
        return 2;
    }
    Desktop::Window window;
    Desktop::Types::Description description;
    description.title = "Progress restore registration laziness";
    description.windowed.clientSize = {160, 100};
    description.activation.visible = false;

    if(!window.open(description, Desktop::Events::owned(4)).ok())
    {
        return 3;
    }

    if(!Desktop::Events::poll().status.ok())
    {
        return 4;
    }

    if(!window.close().ok())
    {
        return 5;
    }

    return Win32TestHooks::progressOwnerRestoreMessageRegistrationAttempted() ? 6 : 0;
}

#endif

void testDesktopProcessShutdown(TestSupport::Context &context, const std::filesystem::path &executablePath)
{
    constexpr std::array childArguments{
        std::string_view{"--desktop-test-child=accessibility-client"},
        kStandaloneColorChildArgument,
        kWindowColorChildArgument,
        kOwnerExitColorChildArgument,
        kOwnerExitDragDropChildArgument,
        kOwnerExitDragDropFailureChildArgument,
        kOwnerExitProgressChildArgument,
        kOwnerExitProgressFailureChildArgument,
        kDeferredPumpFailureWaitChildArgument,
#if DESKTOP_INTERNAL_TEST_HOOKS
        kProgressRestoreMessageLazinessChildArgument,
#endif
    };

    for(const std::string_view argument : childArguments)
    {
        TestSupport::Types::Process::Options child;
        child.executablePath = executablePath;
        child.arguments = {std::string(argument)};
        child.waitTimeout = std::chrono::seconds(30);
        child.captureOutput = true;
        const TestSupport::Types::Process::Result result = TestSupport::runChildProcess(child);
        const std::string name = std::format("{} exits cleanly", argument);

        if(!result.status.ok())
        {
            context.fail(name, TestSupport::formatInfrastructureStatus(result.status));
            continue;
        }
        static_cast<void>(
            context.expectEq(std::format("{} reports an exact exit", argument), TestSupport::Types::Process::Outcome::Exited, result.outcome));
        static_cast<void>(context.expectEq(name, std::uint32_t{0}, result.exitCode));
    }
}
