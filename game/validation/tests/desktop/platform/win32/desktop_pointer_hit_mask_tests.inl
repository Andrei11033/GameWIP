/// @file desktop_pointer_hit_mask_tests.inl
/// @brief Renderer pointer hit-mask tests.

/// ------------------------------------------------------------
/// Pointer hit masks
/// ------------------------------------------------------------

#if DESKTOP_INTERNAL_TEST_HOOKS
void testPointerHitMask(TestSupport::Context &context)
{
    namespace Feedback = Desktop::Renderer;
    namespace FeedbackTypes = Desktop::Types::Renderer;
    using MaskWord = FeedbackTypes::PointerHitMaskWord;
    constexpr std::size_t bitsPerWord = std::numeric_limits<MaskWord>::digits;

    Desktop::Types::Description description;
    description.title = "Window pointer-mask validation";
    description.windowed.clientSize = {241, 161};
    description.activation.visible = false;

    Desktop::Window window;
    static_cast<void>(context.expectTrue("pointer-mask fixture opens", window.open(description, Desktop::Events::owned(8)).ok()));

    if(!window.isOpen())
    {
        return;
    }

    static_cast<void>(
        context.expectFalse("unused Window has no renderer-integration sidecar", Desktop::Internal::TestHooks::hasRendererIntegrationState(window)));

    static_cast<void>(context.expectEq(
        "unadvertised native HitMask bridge is unsupported",
        ErrorCode::Unsupported,
        Feedback::beginPointerHitMaskUpdate(window).status.code));
    static_cast<void>(context.expectFalse(
        "unsupported renderer operation remains allocation-free",
        Desktop::Internal::TestHooks::hasRendererIntegrationState(window)));
    Desktop::Internal::TestHooks::enablePointerHitMaskBridge(window);
    static_cast<void>(
        context.expectTrue("renderer sidecar is allocated on first enabled use", Desktop::Internal::TestHooks::hasRendererIntegrationState(window)));

    Desktop::Types::PixelSize size = window.framebufferSize();

    if(size.width % bitsPerWord == 0)
    {
        static_cast<void>(window.setClientSize({description.windowed.clientSize.width + 1, description.windowed.clientSize.height}));
        size = window.framebufferSize();
    }
    const std::size_t wordsPerRow = static_cast<std::size_t>(size.width) / bitsPerWord + (size.width % bitsPerWord != 0 ? 1U : 0U);
    const std::size_t wordCount = Feedback::requiredPointerHitMaskWords(size);
    std::vector<MaskWord> words(wordCount);
    static_cast<void>(context.expectFalse("pointer-mask fixture has nonempty storage", words.empty()));

    if(words.empty())
    {
        return;
    }
    words.front() = MaskWord{1};

    const std::size_t lastX = static_cast<std::size_t>(size.width) - 1U;
    const std::size_t lastWord = (static_cast<std::size_t>(size.height) - 1U) * wordsPerRow + lastX / bitsPerWord;
    const unsigned int lastBit = static_cast<unsigned int>(lastX % bitsPerWord);
    words[lastWord] |= MaskWord{1} << lastBit;

    const Desktop::Types::LogicalPosition rowSample{0, static_cast<std::int32_t>(window.clientSize().height / 2U)};
    const std::uint64_t sampledY = static_cast<std::uint64_t>(rowSample.y) * size.height / window.clientSize().height;
    words[static_cast<std::size_t>(sampledY) * wordsPerRow] |= MaskWord{1};

    const auto first = Feedback::beginPointerHitMaskUpdate(window);
    static_cast<void>(context.expectTrue("Window creates a nonzero mask generation", first.status.ok() && first->generation != 0));
    static_cast<void>(context.expectEq("target snapshots framebuffer size", size, first->framebufferSize));
    static_cast<void>(context.expectEq("target reports exact word count", wordCount, first->requiredWordCount));
    Desktop::Internal::TestHooks::failNext(Desktop::Internal::TestHooks::FailurePoint::Allocation);
    static_cast<void>(context.expectEq(
        "first mask allocation failure is reported",
        ErrorCode::OutOfMemory,
        Feedback::publishPointerHitMask(window, first->generation, words).code));
    static_cast<void>(
        context.expectEq("failed first publication keeps no mask", std::size_t{0}, Desktop::Internal::TestHooks::pointerHitMaskWordCount(window)));

    static_cast<void>(context.expectTrue("first mask publication succeeds", Feedback::publishPointerHitMask(window, first->generation, words).ok()));
    static_cast<void>(context.expectTrue("active mask is reported", Feedback::hasPointerHitMask(window)));
    static_cast<void>(context.expectEq("mask stores exact word count", wordCount, Desktop::Internal::TestHooks::pointerHitMaskWordCount(window)));
    static_cast<void>(
        context.expectEq("mask stores first physical pixel", MaskWord{1}, Desktop::Internal::TestHooks::pointerHitMaskWord(window, 0) & MaskWord{1}));
    static_cast<void>(context.expectTrue(
        "mask stores last physical pixel",
        (Desktop::Internal::TestHooks::pointerHitMaskWord(window, lastWord) & (MaskWord{1} << lastBit)) != 0));
    static_cast<void>(context.expectTrue(
        "first logical position samples the first interactive pixel",
        Desktop::Internal::TestHooks::pointerHitMaskAccepts(window, {0, 0})));
    static_cast<void>(context.expectTrue(
        "row-local mask lookup samples an independently padded row",
        Desktop::Internal::TestHooks::pointerHitMaskAccepts(window, rowSample)));
    static_cast<void>(
        context.expectTrue("out-of-range sampling uses interactive fallback", Desktop::Internal::TestHooks::pointerHitMaskAccepts(window, {-1, -1})));

    const void *storage = Desktop::Internal::TestHooks::pointerHitMaskStorage(window);
    words.front() = MaskWord{2};
    const auto stale = Feedback::beginPointerHitMaskUpdate(window);
    const auto newer = Feedback::beginPointerHitMaskUpdate(window);
    static_cast<void>(context.expectTrue(
        "Window generations increase monotonically",
        stale.status.ok() && newer.status.ok() && newer->generation > stale->generation));
    static_cast<void>(context.expectEq(
        "superseded outstanding update is interrupted",
        ErrorCode::Interrupted,
        Feedback::publishPointerHitMask(window, stale->generation, words).code));
    static_cast<void>(context.expectTrue("newer same-size mask succeeds", Feedback::publishPointerHitMask(window, newer->generation, words).ok()));
    static_cast<void>(context.expectEq("same-size publication reuses storage", storage, Desktop::Internal::TestHooks::pointerHitMaskStorage(window)));
    static_cast<void>(
        context.expectEq("newer publication updates revision", newer->generation, Desktop::Internal::TestHooks::pointerHitMaskGeneration(window)));
    static_cast<void>(context.expectEq(
        "stale publication is rejected",
        ErrorCode::Interrupted,
        Feedback::publishPointerHitMask(window, newer->generation, words).code));
    static_cast<void>(
        context.expectEq("stale publication preserves active data", MaskWord{2}, Desktop::Internal::TestHooks::pointerHitMaskWord(window, 0)));

    const unsigned int validBitsInLastWord = static_cast<unsigned int>(size.width % bitsPerWord);

    if(validBitsInLastWord != 0)
    {
        std::vector<MaskWord> invalidPadding = words;
        invalidPadding[wordsPerRow - 1U] |= MaskWord{1} << validBitsInLastWord;
        const auto padding = Feedback::beginPointerHitMaskUpdate(window);
        static_cast<void>(context.expectEq(
            "set row-padding bits are rejected",
            ErrorCode::InvalidArgument,
            Feedback::publishPointerHitMask(window, padding->generation, invalidPadding).code));
        static_cast<void>(context.expectEq(
            "invalid row-padding bits preserve revision",
            newer->generation,
            Desktop::Internal::TestHooks::pointerHitMaskGeneration(window)));
    }

    const Desktop::Types::ScreenPosition originalPosition = window.clientPosition();
    static_cast<void>(window.setClientPosition({originalPosition.x + 1, originalPosition.y + 1}));
    static_cast<void>(context.expectEq(
        "movement preserves active mask revision",
        newer->generation,
        Desktop::Internal::TestHooks::pointerHitMaskGeneration(window)));
    const auto beforeResize = Feedback::beginPointerHitMaskUpdate(window);
    static_cast<void>(window.setClientSize({window.clientSize().width + 1, window.clientSize().height}));
    static_cast<void>(context.expectEq(
        "framebuffer resize invalidates active mask",
        std::uint64_t{0},
        Desktop::Internal::TestHooks::pointerHitMaskGeneration(window)));
    static_cast<void>(context.expectFalse("resize removes active-mask state", Feedback::hasPointerHitMask(window)));
    static_cast<void>(context.expectEq(
        "pre-resize update is interrupted",
        ErrorCode::Interrupted,
        Feedback::publishPointerHitMask(window, beforeResize->generation, words).code));
    static_cast<void>(context.expectTrue("mask clear is idempotent", Feedback::clearPointerHitMask(window).ok()));

    const auto beforeClose = Feedback::beginPointerHitMaskUpdate(window);
    static_cast<void>(context.expectTrue("pointer-mask fixture closes", window.close().ok()));
    static_cast<void>(context.expectTrue("pointer-mask fixture reopens", window.open(description, Desktop::Events::owned(8)).ok()));

    if(window.isOpen())
    {
        Desktop::Internal::TestHooks::enablePointerHitMaskBridge(window);
        static_cast<void>(context.expectEq(
            "pre-close generation stays stale after reopen",
            ErrorCode::Interrupted,
            Feedback::publishPointerHitMask(window, beforeClose->generation, words).code));
        Desktop::Internal::TestHooks::setPointerHitMaskGeneration(window, std::numeric_limits<std::uint64_t>::max() - 1U);
        const auto lastGeneration = Feedback::beginPointerHitMaskUpdate(window);
        static_cast<void>(context.expectEq(
            "last representable generation is deterministic",
            std::numeric_limits<std::uint64_t>::max(),
            lastGeneration->generation));
        static_cast<void>(
            context.expectEq("generation overflow never wraps", ErrorCode::ResourceBusy, Feedback::beginPointerHitMaskUpdate(window).status.code));
        static_cast<void>(window.close());
    }
}
#endif

/// ------------------------------------------------------------
/// Renderer occlusion feedback
/// ------------------------------------------------------------
