/// @file desktop_renderer_tests.inl
/// @brief Window renderer-integration validation cases.

#if DESKTOP_INTERNAL_TEST_HOOKS
void testPointerHitMask(TestSupport::Context &context)
{
    namespace Feedback = Desktop::Renderer;
    namespace FeedbackTypes = Desktop::Types::Renderer;
    using MaskWord = FeedbackTypes::PointerHitMaskWord;
    constexpr std::size_t bitsPerWord = std::numeric_limits<MaskWord>::digits;

    Desktop::Types::Description description;
    description.title = "Window pointer-mask validation";
    description.clientSize = {241, 161};
    description.visible = false;

    Desktop::Window window;
    static_cast<void>(context.expectTrue("pointer-mask fixture opens", window.open(description, 8).ok()));
    if (!window.isOpen())
        return;

    static_cast<void>(
        context.expectFalse("unused Window has no renderer-integration sidecar", Desktop::TestHooks::hasRendererIntegrationState(window)));

    static_cast<void>(context.expectEq(
        "unadvertised native HitMask bridge is unsupported",
        ErrorCode::Unsupported,
        Feedback::beginPointerHitMaskUpdate(window).status.code));
    static_cast<void>(
        context.expectFalse("unsupported renderer operation remains allocation-free", Desktop::TestHooks::hasRendererIntegrationState(window)));
    Desktop::TestHooks::enablePointerHitMaskBridge(window);
    static_cast<void>(
        context.expectTrue("renderer sidecar is allocated on first enabled use", Desktop::TestHooks::hasRendererIntegrationState(window)));

    Desktop::Types::PixelSize size = window.framebufferSize();
    if (size.width % bitsPerWord == 0)
    {
        static_cast<void>(window.setClientSize({description.clientSize.width + 1, description.clientSize.height}));
        size = window.framebufferSize();
    }
    const std::size_t wordsPerRow = static_cast<std::size_t>(size.width) / bitsPerWord + (size.width % bitsPerWord != 0 ? 1U : 0U);
    const std::size_t wordCount = Feedback::requiredPointerHitMaskWords(size);
    std::vector<MaskWord> words(wordCount);
    static_cast<void>(context.expectFalse("pointer-mask fixture has nonempty storage", words.empty()));
    if (words.empty())
        return;
    words.front() = MaskWord{1};

    const std::size_t lastX = static_cast<std::size_t>(size.width) - 1U;
    const std::size_t lastWord = (static_cast<std::size_t>(size.height) - 1U) * wordsPerRow + lastX / bitsPerWord;
    const unsigned int lastBit = static_cast<unsigned int>(lastX % bitsPerWord);
    words[lastWord] |= MaskWord{1} << lastBit;

    const Desktop::Types::LogicalPosition rowSample{0, static_cast<std::int32_t>(window.clientSize().height / 2U)};
    const std::uint64_t sampledY = static_cast<std::uint64_t>(rowSample.y) * size.height / window.clientSize().height;
    words[static_cast<std::size_t>(sampledY) * wordsPerRow] |= MaskWord{1};

    const FeedbackTypes::PointerHitMaskResult first = Feedback::beginPointerHitMaskUpdate(window);
    static_cast<void>(context.expectTrue("Window creates a nonzero mask generation", first.status.ok() && first.target.generation != 0));
    static_cast<void>(context.expectEq("target snapshots framebuffer size", size, first.target.framebufferSize));
    static_cast<void>(context.expectEq("target reports exact word count", wordCount, first.target.requiredWordCount));
    Desktop::TestHooks::failNext(Desktop::TestHooks::FailurePoint::Allocation);
    static_cast<void>(context.expectEq(
        "first mask allocation failure is reported",
        ErrorCode::OutOfMemory,
        Feedback::publishPointerHitMask(window, first.target.generation, words).code));
    static_cast<void>(
        context.expectEq("failed first publication keeps no mask", std::size_t{0}, Desktop::TestHooks::pointerHitMaskWordCount(window)));

    static_cast<void>(
        context.expectTrue("first mask publication succeeds", Feedback::publishPointerHitMask(window, first.target.generation, words).ok()));
    static_cast<void>(context.expectTrue("active mask is reported", Feedback::hasPointerHitMask(window)));
    static_cast<void>(context.expectEq("mask stores exact word count", wordCount, Desktop::TestHooks::pointerHitMaskWordCount(window)));
    static_cast<void>(
        context.expectEq("mask stores first physical pixel", MaskWord{1}, Desktop::TestHooks::pointerHitMaskWord(window, 0) & MaskWord{1}));
    static_cast<void>(context.expectTrue(
        "mask stores last physical pixel",
        (Desktop::TestHooks::pointerHitMaskWord(window, lastWord) & (MaskWord{1} << lastBit)) != 0));
    static_cast<void>(
        context.expectTrue("first logical position samples the first interactive pixel", Desktop::TestHooks::pointerHitMaskAccepts(window, {0, 0})));
    static_cast<void>(context.expectTrue(
        "row-local mask lookup samples an independently padded row",
        Desktop::TestHooks::pointerHitMaskAccepts(window, rowSample)));
    static_cast<void>(
        context.expectTrue("out-of-range sampling uses interactive fallback", Desktop::TestHooks::pointerHitMaskAccepts(window, {-1, -1})));

    const void *storage = Desktop::TestHooks::pointerHitMaskStorage(window);
    words.front() = MaskWord{2};
    const FeedbackTypes::PointerHitMaskResult stale = Feedback::beginPointerHitMaskUpdate(window);
    const FeedbackTypes::PointerHitMaskResult newer = Feedback::beginPointerHitMaskUpdate(window);
    static_cast<void>(context.expectTrue(
        "Window generations increase monotonically",
        stale.status.ok() && newer.status.ok() && newer.target.generation > stale.target.generation));
    static_cast<void>(context.expectEq(
        "superseded outstanding update is interrupted",
        ErrorCode::Interrupted,
        Feedback::publishPointerHitMask(window, stale.target.generation, words).code));
    static_cast<void>(
        context.expectTrue("newer same-size mask succeeds", Feedback::publishPointerHitMask(window, newer.target.generation, words).ok()));
    static_cast<void>(context.expectEq("same-size publication reuses storage", storage, Desktop::TestHooks::pointerHitMaskStorage(window)));
    static_cast<void>(
        context.expectEq("newer publication updates revision", newer.target.generation, Desktop::TestHooks::pointerHitMaskGeneration(window)));
    static_cast<void>(context.expectEq(
        "stale publication is rejected",
        ErrorCode::Interrupted,
        Feedback::publishPointerHitMask(window, newer.target.generation, words).code));
    static_cast<void>(context.expectEq("stale publication preserves active data", MaskWord{2}, Desktop::TestHooks::pointerHitMaskWord(window, 0)));

    const unsigned int validBitsInLastWord = static_cast<unsigned int>(size.width % bitsPerWord);
    if (validBitsInLastWord != 0)
    {
        std::vector<MaskWord> invalidPadding = words;
        invalidPadding[wordsPerRow - 1U] |= MaskWord{1} << validBitsInLastWord;
        const FeedbackTypes::PointerHitMaskResult padding = Feedback::beginPointerHitMaskUpdate(window);
        static_cast<void>(context.expectEq(
            "set row-padding bits are rejected",
            ErrorCode::InvalidArgument,
            Feedback::publishPointerHitMask(window, padding.target.generation, invalidPadding).code));
        static_cast<void>(context.expectEq(
            "invalid row-padding bits preserve revision",
            newer.target.generation,
            Desktop::TestHooks::pointerHitMaskGeneration(window)));
    }

    const Desktop::Types::ScreenPosition originalPosition = window.clientPosition();
    static_cast<void>(window.setClientPosition({originalPosition.x + 1, originalPosition.y + 1}));
    static_cast<void>(
        context.expectEq("movement preserves active mask revision", newer.target.generation, Desktop::TestHooks::pointerHitMaskGeneration(window)));
    const FeedbackTypes::PointerHitMaskResult beforeResize = Feedback::beginPointerHitMaskUpdate(window);
    static_cast<void>(window.setClientSize({window.clientSize().width + 1, window.clientSize().height}));
    static_cast<void>(
        context.expectEq("framebuffer resize invalidates active mask", std::uint64_t{0}, Desktop::TestHooks::pointerHitMaskGeneration(window)));
    static_cast<void>(context.expectFalse("resize removes active-mask state", Feedback::hasPointerHitMask(window)));
    static_cast<void>(context.expectEq(
        "pre-resize update is interrupted",
        ErrorCode::Interrupted,
        Feedback::publishPointerHitMask(window, beforeResize.target.generation, words).code));
    static_cast<void>(context.expectTrue("mask clear is idempotent", Feedback::clearPointerHitMask(window).ok()));

    const FeedbackTypes::PointerHitMaskResult beforeClose = Feedback::beginPointerHitMaskUpdate(window);
    static_cast<void>(context.expectTrue("pointer-mask fixture closes", window.close().ok()));
    static_cast<void>(context.expectTrue("pointer-mask fixture reopens", window.open(description, 8).ok()));
    if (window.isOpen())
    {
        Desktop::TestHooks::enablePointerHitMaskBridge(window);
        static_cast<void>(context.expectEq(
            "pre-close generation stays stale after reopen",
            ErrorCode::Interrupted,
            Feedback::publishPointerHitMask(window, beforeClose.target.generation, words).code));
        Desktop::TestHooks::setPointerHitMaskGeneration(window, std::numeric_limits<std::uint64_t>::max() - 1U);
        const FeedbackTypes::PointerHitMaskResult lastGeneration = Feedback::beginPointerHitMaskUpdate(window);
        static_cast<void>(context.expectEq(
            "last representable generation is deterministic",
            std::numeric_limits<std::uint64_t>::max(),
            lastGeneration.target.generation));
        static_cast<void>(
            context.expectEq("generation overflow never wraps", ErrorCode::ResourceBusy, Feedback::beginPointerHitMaskUpdate(window).status.code));
        static_cast<void>(window.close());
    }
}
#endif

void testRendererOcclusionFeedback(TestSupport::Context &context)
{
    namespace Feedback = Desktop::Renderer;
    using Capability = Desktop::Types::Capability;

    Desktop::Window closed;
    static_cast<void>(
        context.expectEq("closed Window rejects provider attachment", ErrorCode::NotOpen, Feedback::attachOcclusionProvider(closed).code));
    static_cast<void>(context.expectEq("closed Window rejects occlusion report", ErrorCode::NotOpen, Feedback::reportOcclusion(closed, true).code));
    static_cast<void>(context.expectEq("closed Window rejects provider detach", ErrorCode::NotOpen, Feedback::detachOcclusionProvider(closed).code));

    Desktop::Types::Description description;
    description.title = "Window renderer feedback validation";
    description.clientSize = {240, 160};
    description.visible = false;

    Desktop::Window window;
    static_cast<void>(context.expectTrue("renderer feedback fixture opens", window.open(description, 8).ok()));
    if (!window.isOpen())
        return;

    static_cast<void>(context.expectTrue("global occlusion reporting is supported", Desktop::supports(Capability::OcclusionReporting)));
    static_cast<void>(context.expectTrue("Window reports backend occlusion capability", window.supports(Capability::OcclusionReporting)));
    static_cast<void>(context.expectFalse("Window initially lacks occlusion provider", Feedback::hasOcclusionProvider(window)));
    static_cast<void>(context.expectEq("report before provider is rejected", ErrorCode::NotOpen, Feedback::reportOcclusion(window, true).code));
    static_cast<void>(context.expectTrue("occlusion provider attaches", Feedback::attachOcclusionProvider(window).ok()));
    static_cast<void>(context.expectTrue("attached Window reports its provider", Feedback::hasOcclusionProvider(window)));
    static_cast<void>(context.expectEq("second provider is rejected", ErrorCode::AlreadyOpen, Feedback::attachOcclusionProvider(window).code));

    window.clearEvents();
    static_cast<void>(context.expectTrue("occluded report succeeds", Feedback::reportOcclusion(window, true).ok()));
    static_cast<void>(context.expectTrue("occluded report updates cache", window.occluded()));
    Desktop::Types::Event event;
    static_cast<void>(context.expectTrue("occluded transition queues event", window.popEvent(event)));
    const auto *occludedEvent = event.getIf<Desktop::Types::Events::OcclusionChanged>();
    static_cast<void>(context.expectTrue("occluded event has typed payload", occludedEvent != nullptr));
    if (occludedEvent != nullptr)
        static_cast<void>(context.expectTrue("occluded event carries true", occludedEvent->occluded));

    static_cast<void>(context.expectTrue("duplicate occlusion report succeeds", Feedback::reportOcclusion(window, true).ok()));
    static_cast<void>(context.expectEq("duplicate report does not queue event", std::size_t{0}, window.eventQueueInfo().pendingEvents));

    ErrorCode wrongThreadCode = ErrorCode::Success;
    std::thread worker(
        [&window, &wrongThreadCode]
        {
            wrongThreadCode = Desktop::Renderer::reportOcclusion(window, false).code;
        });
    worker.join();
    static_cast<void>(context.expectEq("wrong-thread renderer feedback is rejected", ErrorCode::ResourceBusy, wrongThreadCode));
    static_cast<void>(context.expectTrue("wrong-thread report preserves cache", window.occluded()));

    window.clearEvents();
    static_cast<void>(context.expectTrue("provider detaches", Feedback::detachOcclusionProvider(window).ok()));
    static_cast<void>(context.expectTrue("detach preserves backend capability", window.supports(Capability::OcclusionReporting)));
    static_cast<void>(context.expectFalse("detach clears provider state", Feedback::hasOcclusionProvider(window)));
    static_cast<void>(context.expectFalse("detach resets occlusion cache", window.occluded()));
    static_cast<void>(context.expectTrue("detach queues final false transition", window.popEvent(event)));
    const auto *visibleEvent = event.getIf<Desktop::Types::Events::OcclusionChanged>();
    static_cast<void>(context.expectTrue("detach event has typed payload", visibleEvent != nullptr));
    if (visibleEvent != nullptr)
        static_cast<void>(context.expectFalse("detach event carries false", visibleEvent->occluded));
    static_cast<void>(context.expectEq("report after detach is rejected", ErrorCode::NotOpen, Feedback::reportOcclusion(window, false).code));
    static_cast<void>(context.expectTrue("repeated detach succeeds", Feedback::detachOcclusionProvider(window).ok()));

    static_cast<void>(context.expectTrue("provider reattaches", Feedback::attachOcclusionProvider(window).ok()));
    static_cast<void>(context.expectTrue("feedback fixture closes with provider attached", window.close().ok()));

    Desktop::Window overflow;
    static_cast<void>(context.expectTrue("occlusion overflow fixture opens", overflow.open(description, 1).ok()));
    if (overflow.isOpen())
    {
        static_cast<void>(Feedback::attachOcclusionProvider(overflow));
        static_cast<void>(Feedback::reportOcclusion(overflow, true));
        static_cast<void>(Feedback::reportOcclusion(overflow, false));
        static_cast<void>(context.expectFalse("dropped transition still updates cache", overflow.occluded()));
        static_cast<void>(context.expectEq("full queue counts dropped occlusion event", std::uint64_t{1}, overflow.eventQueueInfo().droppedEvents));
        static_cast<void>(overflow.close());
    }
}
