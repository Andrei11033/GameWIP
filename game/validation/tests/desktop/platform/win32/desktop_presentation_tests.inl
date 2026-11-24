/// @file desktop_presentation_tests.inl
/// @brief Renderer presentation publication tests.

/// ------------------------------------------------------------
/// Presentation publication
/// ------------------------------------------------------------

void testPresentationPublication(TestSupport::Context &context)
{
    using Snapshot = Desktop::Internal::TestHooks::PresentationPublicationSnapshot;
    constexpr Snapshot first{
        .clientSize = {0x11111111U, 0x22222222U},
        .framebufferSize = {0x33333333U, 0x44444444U},
        .contentScale = {1.25F, 2.5F},
        .dpi = {120.0F, 240.0F},
        .monitor = {101},
        .presentation = Desktop::Types::PresentationState::Minimized,
        .visible = true,
        .interactiveMoveResizeActive = true,
        .occluded = true};
    constexpr Snapshot second{
        .clientSize = {0xAAAAAAAAU, 0xBBBBBBBBU},
        .framebufferSize = {0xCCCCCCCCU, 0xDDDDDDDDU},
        .contentScale = {3.75F, 4.5F},
        .dpi = {360.0F, 480.0F},
        .monitor = {202},
        .presentation = Desktop::Types::PresentationState::Maximized,
        .visible = false,
        .interactiveMoveResizeActive = false,
        .occluded = false};

    // A closed window has no publication and rejects publication setup.
    Desktop::Window closed;
    static_cast<void>(context.expectEq(
        "default Window allocates no presentation publication",
        static_cast<const void *>(nullptr),
        Desktop::Internal::TestHooks::presentationPublicationStorage(closed)));
    static_cast<void>(context.expectEq(
        "closed Window rejects concurrent presentation reads",
        ErrorCode::NotOpen,
        Desktop::Renderer::enableConcurrentPresentationReads(closed).code));

    Desktop::Types::Description description;
    description.title = "Window presentation publication validation";
    description.windowed.clientSize = {320, 200};
    description.activation.visible = false;
    Desktop::Window window;
    static_cast<void>(context.expectTrue("publication fixture opens", window.open(description, Desktop::Events::owned(4)).ok()));

    if(!window.isOpen())
    {
        return;
    }

    // Confirm ordinary getters and lazy sidecar allocation before enabling reads.
    static_cast<void>(
        context.expectFalse("default Window has no presentation publication", Desktop::Renderer::concurrentPresentationReadsEnabled(window)));
    static_cast<void>(context.expectEq(
        "default Window has no presentation storage",
        static_cast<const void *>(nullptr),
        Desktop::Internal::TestHooks::presentationPublicationStorage(window)));
    static_cast<void>(
        context.expectFalse("publication does not allocate renderer integration", Desktop::Internal::TestHooks::hasRendererIntegrationState(window)));

    Desktop::Internal::TestHooks::applyPresentationPublicationSnapshot(window, first);
    static_cast<void>(context.expectEq("ordinary getter reads authoritative client size", first.clientSize, window.clientSize()));
    static_cast<void>(context.expectEq("ordinary getter reads authoritative monitor", first.monitor, window.currentMonitor()));
    static_cast<void>(context.expectTrue("ordinary getter reads authoritative visibility", window.visible()));
    static_cast<void>(context.expectFalse("disabled publication keeps authoritative occlusion default", window.occluded()));

    Desktop::Internal::TestHooks::failNext(Desktop::Internal::TestHooks::FailurePoint::Allocation);
    static_cast<void>(context.expectEq(
        "presentation publication allocation failure is reported",
        ErrorCode::OutOfMemory,
        Desktop::Renderer::enableConcurrentPresentationReads(window).code));
    static_cast<void>(context.expectFalse(
        "allocation failure leaves presentation publication disabled",
        Desktop::Renderer::concurrentPresentationReadsEnabled(window)));

    ErrorCode wrongThreadCode = ErrorCode::Success;
    std::thread wrongThread(
        [&]
        {
            wrongThreadCode = Desktop::Renderer::enableConcurrentPresentationReads(window).code;
        });
    wrongThread.join();
    static_cast<void>(context.expectEq("wrong-thread enable is rejected", ErrorCode::ResourceBusy, wrongThreadCode));

    static_cast<void>(
        context.expectTrue("owner enables concurrent presentation reads", Desktop::Renderer::enableConcurrentPresentationReads(window).ok()));
    const void *publicationStorage = Desktop::Internal::TestHooks::presentationPublicationStorage(window);
    static_cast<void>(context.expectTrue("enabled presentation publication has storage", publicationStorage != nullptr));
    static_cast<void>(context.expectEq("enable immediately publishes client size", first.clientSize, window.clientSize()));
    static_cast<void>(context.expectEq("enable immediately publishes current monitor", first.monitor, window.currentMonitor()));
    static_cast<void>(context.expectTrue("repeated enable succeeds", Desktop::Renderer::enableConcurrentPresentationReads(window).ok()));
    static_cast<void>(context.expectEq(
        "repeated enable preserves publication storage",
        publicationStorage,
        Desktop::Internal::TestHooks::presentationPublicationStorage(window)));

    // Read snapshots concurrently while the owner publishes alternating states.
    Desktop::Internal::TestHooks::applyPresentationPublicationSnapshot(window, first);
    std::atomic_bool stop = false;
    std::atomic_bool invalid = false;
    std::atomic_size_t reads = 0;
    std::thread reader(
        [&]
        {
            while(!stop.load(std::memory_order_acquire))
            {
                const auto client = window.clientSize();
                const auto framebuffer = window.framebufferSize();
                const auto scale = window.contentScale();
                const auto dpi = window.effectiveDpi();
                const auto monitor = window.currentMonitor();
                const auto presentation = window.presentationState();

                if((client != first.clientSize && client != second.clientSize) ||
                   (framebuffer != first.framebufferSize && framebuffer != second.framebufferSize) ||
                   (scale != first.contentScale && scale != second.contentScale) || (dpi != first.dpi && dpi != second.dpi) ||
                   (monitor != first.monitor && monitor != second.monitor) ||
                   (presentation != first.presentation && presentation != second.presentation))
                {
                    invalid.store(true, std::memory_order_relaxed);
                }
                static_cast<void>(window.minimized());
                static_cast<void>(window.maximized());
                static_cast<void>(window.visible());
                static_cast<void>(window.interactiveMoveResizeActive());
                static_cast<void>(window.occluded());

                if(reads.fetch_add(1, std::memory_order_release) == 0)
                {
                    reads.notify_one();
                }
            }
        });

    // Ensure the reader runs before a short publisher loop can finish its time slice.
    reads.wait(0, std::memory_order_acquire);

    for(std::size_t iteration = 0; iteration < 200'000; ++iteration)
    {
        Desktop::Internal::TestHooks::applyPresentationPublicationSnapshot(window, (iteration & 1U) == 0 ? second : first);
    }
    stop.store(true, std::memory_order_release);
    reader.join();

    static_cast<void>(context.expectTrue("render thread completed publication reads", reads.load(std::memory_order_relaxed) != 0));
    static_cast<void>(context.expectFalse("compound presentation publications never tear", invalid.load(std::memory_order_relaxed)));

    Desktop::Internal::TestHooks::applyPresentationPublicationSnapshot(window, first);
    static_cast<void>(context.expectEq("published client size is visible", first.clientSize, window.clientSize()));
    static_cast<void>(context.expectEq("published framebuffer size is visible", first.framebufferSize, window.framebufferSize()));
    static_cast<void>(context.expectEq("published content scale is visible", first.contentScale, window.contentScale()));
    static_cast<void>(context.expectEq("published DPI is visible", first.dpi, window.effectiveDpi()));
    static_cast<void>(context.expectEq("published monitor is visible", first.monitor, window.currentMonitor()));
    static_cast<void>(context.expectTrue("published minimized state is visible", window.minimized()));
    static_cast<void>(context.expectFalse("published minimized state is not maximized", window.maximized()));
    static_cast<void>(context.expectTrue("published visibility is visible", window.visible()));
    static_cast<void>(context.expectTrue("published interactive state is visible", window.interactiveMoveResizeActive()));
    static_cast<void>(context.expectTrue("published occlusion is visible", window.occluded()));

    // Verify close, failed reopen, and successful reopen preserve the sidecar contract.
    static_cast<void>(context.expectTrue("portable publication fixture closes", window.close().ok()));
    static_cast<void>(context.expectEq("close clears client size", Desktop::Types::LogicalSize{}, window.clientSize()));
    static_cast<void>(context.expectEq("close clears framebuffer size", Desktop::Types::PixelSize{}, window.framebufferSize()));
    static_cast<void>(context.expectFalse("close clears current monitor", window.currentMonitor().isValid()));
    static_cast<void>(context.expectFalse("close clears visibility", window.visible()));
    static_cast<void>(context.expectFalse("close clears interactive state", window.interactiveMoveResizeActive()));
    static_cast<void>(context.expectFalse("close clears occlusion", window.occluded()));
    static_cast<void>(
        context.expectTrue("close retains concurrent presentation enablement", Desktop::Renderer::concurrentPresentationReadsEnabled(window)));
    static_cast<void>(context.expectEq(
        "close retains presentation allocation",
        publicationStorage,
        Desktop::Internal::TestHooks::presentationPublicationStorage(window)));

    Desktop::Internal::TestHooks::failNext(Desktop::Internal::TestHooks::FailurePoint::NativeCreation);
    static_cast<void>(context.expectEq("failed reopen is reported", ErrorCode::OpenFailed, window.open(description, Desktop::Events::owned(4)).code));
    static_cast<void>(context.expectEq("failed reopen exposes closed client size", Desktop::Types::LogicalSize{}, window.clientSize()));
    static_cast<void>(context.expectFalse("failed reopen exposes no candidate monitor", window.currentMonitor().isValid()));
    static_cast<void>(context.expectEq(
        "failed reopen retains presentation allocation",
        publicationStorage,
        Desktop::Internal::TestHooks::presentationPublicationStorage(window)));

    static_cast<void>(context.expectTrue("publication fixture reopens", window.open(description, Desktop::Events::owned(4)).ok()));
    static_cast<void>(
        context.expectTrue("reopen automatically retains concurrent reads", Desktop::Renderer::concurrentPresentationReadsEnabled(window)));
    static_cast<void>(context.expectEq(
        "reopen reuses presentation allocation",
        publicationStorage,
        Desktop::Internal::TestHooks::presentationPublicationStorage(window)));
    static_cast<void>(context.expectTrue("reopen publishes a valid current monitor", window.currentMonitor().isValid()));
    static_cast<void>(context.expectFalse("reopen does not leak occlusion", window.occluded()));
    static_cast<void>(window.close());
}
