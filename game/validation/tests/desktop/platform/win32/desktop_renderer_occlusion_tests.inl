/// @file desktop_renderer_occlusion_tests.inl
/// @brief Renderer occlusion feedback tests.

/// ------------------------------------------------------------
/// Renderer occlusion feedback
/// ------------------------------------------------------------

void testRendererOcclusionFeedback(TestSupport::Context &context)
{
    namespace Feedback = Desktop::Renderer;
    using Desktop::Types::Capability;

    // Reject provider operations while the window is closed.
    Desktop::Window closed;
    static_cast<void>(
        context.expectEq("closed Window rejects provider attachment", ErrorCode::NotOpen, Feedback::attachOcclusionProvider(closed).status().code()));
    static_cast<void>(context.expectEq("closed Window rejects occlusion report", ErrorCode::NotOpen, Feedback::reportOcclusion(closed, true).status().code()));
    static_cast<void>(context.expectEq("closed Window rejects provider detach", ErrorCode::NotOpen, Feedback::detachOcclusionProvider(closed).status().code()));

    Desktop::Types::Description description;
    description.title = "Window renderer feedback validation";
    description.windowed.clientSize = {240, 160};
    description.activation.visible = false;

    Desktop::Window window;
    static_cast<void>(context.expectTrue("renderer feedback fixture opens", window.open(description, Desktop::Events::owned(8)).ok()));

    if(!window.isOpen())
    {
        return;
    }

    // Attach one provider and verify capability and event reporting.
    static_cast<void>(context.expectFalse(
        "occlusion feedback works without concurrent presentation publication",
        Feedback::concurrentPresentationReadsEnabled(window)));

    static_cast<void>(context.expectTrue("global occlusion reporting is supported", Desktop::supports(Capability::OcclusionReporting)));
    static_cast<void>(context.expectTrue("Window reports backend occlusion capability", window.supports(Capability::OcclusionReporting)));
    static_cast<void>(context.expectFalse("Window initially lacks occlusion provider", Feedback::hasOcclusionProvider(window)));
    static_cast<void>(context.expectEq("report before provider is rejected", ErrorCode::NotOpen, Feedback::reportOcclusion(window, true).status().code()));
    static_cast<void>(context.expectTrue("occlusion provider attaches", Feedback::attachOcclusionProvider(window).ok()));
    static_cast<void>(context.expectTrue("attached Window reports its provider", Feedback::hasOcclusionProvider(window)));
    static_cast<void>(context.expectEq("second provider is rejected", ErrorCode::AlreadyOpen, Feedback::attachOcclusionProvider(window).status().code()));

    window.clearEvents();
    static_cast<void>(context.expectTrue("occluded report succeeds", Feedback::reportOcclusion(window, true).ok()));
    static_cast<void>(context.expectTrue("occluded report updates cache", window.occluded()));
    Desktop::Types::Event event;
    static_cast<void>(context.expectTrue("occluded transition queues event", window.popEvent(event)));
    const auto *occludedEvent = event.getIf<Desktop::Types::Events::OcclusionChanged>();
    static_cast<void>(context.expectTrue("occluded event has typed payload", occludedEvent != nullptr));

    if(occludedEvent != nullptr)
    {
        static_cast<void>(context.expectTrue("occluded event carries true", occludedEvent->occluded));
    }

    static_cast<void>(context.expectTrue("duplicate occlusion report succeeds", Feedback::reportOcclusion(window, true).ok()));
    static_cast<void>(context.expectEq("duplicate report does not queue event", std::size_t{0}, window.eventQueueInfo().pendingEvents));

    // Reject cross-thread reports without changing the cached state.
    ErrorCode wrongThreadCode = ErrorCode::Success;
    std::thread worker(
        [&window, &wrongThreadCode]
        {
            wrongThreadCode = Desktop::Renderer::reportOcclusion(window, false).status().code();
        });
    worker.join();
    static_cast<void>(context.expectEq("wrong-thread renderer feedback is rejected", ErrorCode::ResourceBusy, wrongThreadCode));
    static_cast<void>(context.expectTrue("wrong-thread report preserves cache", window.occluded()));

    // Detach, reattach, and verify concurrent reads from the publication.
    window.clearEvents();
    static_cast<void>(context.expectTrue("provider detaches", Feedback::detachOcclusionProvider(window).ok()));
    static_cast<void>(context.expectTrue("detach preserves backend capability", window.supports(Capability::OcclusionReporting)));
    static_cast<void>(context.expectFalse("detach clears provider state", Feedback::hasOcclusionProvider(window)));
    static_cast<void>(context.expectFalse("detach resets occlusion cache", window.occluded()));
    static_cast<void>(context.expectTrue("detach queues final false transition", window.popEvent(event)));
    const auto *visibleEvent = event.getIf<Desktop::Types::Events::OcclusionChanged>();
    static_cast<void>(context.expectTrue("detach event has typed payload", visibleEvent != nullptr));

    if(visibleEvent != nullptr)
    {
        static_cast<void>(context.expectFalse("detach event carries false", visibleEvent->occluded));
    }
    static_cast<void>(context.expectEq("report after detach is rejected", ErrorCode::NotOpen, Feedback::reportOcclusion(window, false).status().code()));
    static_cast<void>(context.expectTrue("repeated detach succeeds", Feedback::detachOcclusionProvider(window).ok()));

    static_cast<void>(context.expectTrue("provider reattaches", Feedback::attachOcclusionProvider(window).ok()));
    static_cast<void>(context.expectTrue("concurrent presentation reads enable", Feedback::enableConcurrentPresentationReads(window).ok()));
    static_cast<void>(context.expectTrue("enabled occlusion report succeeds", Feedback::reportOcclusion(window, true).ok()));
    bool concurrentlyOccluded = false;
    std::thread occlusionReader(
        [&]
        {
            concurrentlyOccluded = window.occluded();
        });
    occlusionReader.join();
    static_cast<void>(context.expectTrue("enabled occlusion publication is readable concurrently", concurrentlyOccluded));
    static_cast<void>(context.expectTrue("feedback fixture closes with provider attached", window.close().ok()));

    // Confirm queue overflow still updates state and records the dropped event.
    Desktop::Window overflow;
    static_cast<void>(context.expectTrue("occlusion overflow fixture opens", overflow.open(description, Desktop::Events::owned(1)).ok()));

    if(overflow.isOpen())
    {
        static_cast<void>(Feedback::attachOcclusionProvider(overflow));
        static_cast<void>(Feedback::reportOcclusion(overflow, true));
        static_cast<void>(Feedback::reportOcclusion(overflow, false));
        static_cast<void>(context.expectFalse("dropped transition still updates cache", overflow.occluded()));
        static_cast<void>(context.expectEq("full queue counts dropped occlusion event", std::uint64_t{1}, overflow.eventQueueInfo().droppedEvents));
        static_cast<void>(overflow.close());
    }
}
