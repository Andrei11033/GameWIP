/// @file desktop_clipboard_failure_tests.inl
/// @brief Clipboard contention, transaction, and failure tests.

/// ------------------------------------------------------------
/// Clipboard contention and failures
/// ------------------------------------------------------------

void testClipboardFixtureContention(TestSupport::Context &context)
{
    // Hold the native Clipboard on another thread and verify bounded retries.
    for(const bool releaseSoon : {true, false})
    {
        std::atomic<bool> ready = false;
        std::atomic<bool> release = false;
        bool locked = false;
        const std::jthread holder(
            [&]
            {
                const HWND owner = CreateWindowExW(
                    0,
                    L"STATIC",
                    L"GameWIP Clipboard Fixture Contention",
                    WS_POPUP,
                    0,
                    0,
                    0,
                    0,
                    nullptr,
                    nullptr,
                    GetModuleHandleW(nullptr),
                    nullptr);
                locked = owner != nullptr && openNativeClipboardFixture(owner);
                ready.store(true, std::memory_order_release);
                ready.notify_one();

                if(locked)
                {
                    if(releaseSoon)
                    {
                        Sleep(30);
                    }
                    else
                    {
                        release.wait(false, std::memory_order_acquire);
                    }
                    CloseClipboard();
                }

                if(owner != nullptr)
                {
                    DestroyWindow(owner);
                }
            });
        ready.wait(false, std::memory_order_acquire);

        if(context.expectTrue("contention fixture acquires clipboard on separate thread", locked))
        {
            const auto started = std::chrono::steady_clock::now();
            const bool opened = openNativeClipboardFixture(nullptr, &context, std::chrono::milliseconds{80});
            static_cast<void>(context.expectEq(
                releaseSoon ? "fixture retries until foreign owner releases" : "fixture reports bounded access failure",
                releaseSoon,
                opened));

            if(opened)
            {
                CloseClipboard();
            }
            static_cast<void>(
                context.expectTrue("fixture retry stays bounded", std::chrono::steady_clock::now() - started < std::chrono::seconds{1}));
        }
        release.store(true, std::memory_order_release);
        release.notify_one();
    }
}

void testClipboardMultiFormatAndFailures(TestSupport::Context &context)
{
    // Verify ordered multi-format publication and transaction state.
    const std::array firstBytes{std::byte{0x01}, std::byte{0x02}};
    const std::array secondBytes{std::byte{0x03}, std::byte{0x04}};
    const std::array<Transfer::ItemView, 3> items{
        Transfer::CustomView{"GameWIP.Validation.First", firstBytes},
        Transfer::TextView{"multi-format"},
        Transfer::CustomView{"GameWIP.Validation.Second", secondBytes}};
    const auto write = Clipboard::write(items);
    if(context.expectTrue("several custom formats plus text publish", write.ok()))
    {
        static_cast<void>(context.expectEq("all multi-format items publish", items.size(), write.value().formatsPublished));
    }
    const auto formats = Clipboard::getFormats();
    static_cast<void>(context.expectTrue("multi-format enumeration succeeds", formats.ok()));
    static_cast<void>(
        context.expectTrue("multi-format enumeration contains three portable entries", formats.ok() && formats.value().size() >= items.size()));

    if(formats.ok() && formats.value().size() >= items.size())
    {
        static_cast<void>(context.expectEq(
            "first caller format keeps priority",
            Transfer::Format{Transfer::FormatKind::Custom, "GameWIP.Validation.First"},
            formats.value()[0]));
        static_cast<void>(context.expectEq("text keeps caller priority", Transfer::Format{Transfer::FormatKind::Text, {}}, formats.value()[1]));
        static_cast<void>(context.expectEq(
            "second custom keeps caller priority",
            Transfer::Format{Transfer::FormatKind::Custom, "GameWIP.Validation.Second"},
            formats.value()[2]));
    }

    const std::array<Transfer::ItemView, 2> duplicateText{Transfer::TextView{"one"}, Transfer::TextView{"two"}};
    static_cast<void>(
        context.expectEq("duplicate standard format is rejected", StatusCode::InvalidArgument, Clipboard::write(duplicateText).status().code()));
    const std::array<Transfer::ItemView, 2> duplicateCustom{
        Transfer::CustomView{"GameWIP.Validation.Case", firstBytes},
        Transfer::CustomView{"gamewip.validation.case", secondBytes}};
    static_cast<void>(context.expectEq(
        "duplicate native custom identity is rejected",
        StatusCode::InvalidArgument,
        Clipboard::write(duplicateCustom).status().code()));

    static_cast<void>(Clipboard::writeText("preserve me"));
    const auto invalidImage = Transfer::ImageView::packed({2, 2}, firstBytes);
    const std::array<Transfer::ItemView, 2> invalidItems{Transfer::TextView{"replacement"}, invalidImage};
    const auto invalidWrite = Clipboard::write(invalidItems);
    static_cast<void>(context.expectEq("late preparation validation fails", StatusCode::InvalidArgument, invalidWrite.status().code()));
    if(context.expectTrue("preparation failure retains progress", invalidWrite.hasValue()))
    {
        static_cast<void>(
            context.expectEq("preparation failure remains NotStarted", ClipboardTypes::CommitState::NotStarted, invalidWrite.value().commitState));
    }
    const auto preservedText = Clipboard::readText();
    if(context.expectTrue("preparation failure leaves prior text readable", preservedText.ok()))
    {
        static_cast<void>(context.expectEq("preparation failure preserves old clipboard", std::string{"preserve me"}, preservedText.value()));
    }

#if DESKTOP_INTERNAL_TEST_HOOKS
    // Inject access, allocation, publication, and close failures.
    Desktop::Internal::TestHooks::resetFailures();
    Desktop::Internal::TestHooks::failNext(Desktop::Internal::TestHooks::FailurePoint::ClipboardAccess);
    static_cast<void>(
        context.expectEq("access busy is deterministic", StatusCode::ResourceBusy, Clipboard::readText(Clipboard::kNoWait).status().code()));
    Desktop::Internal::TestHooks::failNext(Desktop::Internal::TestHooks::FailurePoint::ClipboardAccess);
    static_cast<void>(
        context.expectEq("convenience overload shares access semantics", StatusCode::ResourceBusy, Clipboard::readText().status().code()));

    Desktop::Internal::TestHooks::failNext(Desktop::Internal::TestHooks::FailurePoint::ClipboardAllocation);
    const auto allocationFailure = Clipboard::writeText("allocation");
    static_cast<void>(context.expectEq("allocation failure translates", StatusCode::OutOfMemory, allocationFailure.status().code()));
    if(context.expectTrue("allocation failure retains progress", allocationFailure.hasValue()))
    {
        static_cast<void>(
            context.expectEq("allocation failure does not start", ClipboardTypes::CommitState::NotStarted, allocationFailure.value().commitState));
    }

    Desktop::Internal::TestHooks::failNext(Desktop::Internal::TestHooks::FailurePoint::ClipboardOwnerCreation);
    static_cast<void>(context.expectEq("helper owner failure translates", StatusCode::OpenFailed, Clipboard::writeText("owner").status().code()));

    static_cast<void>(Clipboard::write(items));
    Desktop::Internal::TestHooks::failClipboardEnumerationAfter(1);
    const auto partialEnumeration = Clipboard::getFormats();
    static_cast<void>(context.expectEq("partial enumeration reports read failure", StatusCode::ReadFailed, partialEnumeration.status().code()));
    static_cast<void>(context.expectTrue("partial enumeration preserves a failed payload", partialEnumeration.hasValue()));

    Desktop::Internal::TestHooks::failNext(Desktop::Internal::TestHooks::FailurePoint::ClipboardRead);
    static_cast<void>(context.expectEq("native read failure translates", StatusCode::ReadFailed, Clipboard::readText().status().code()));

    Desktop::Internal::TestHooks::failNext(Desktop::Internal::TestHooks::FailurePoint::ClipboardClear);
    const auto clearFailure = Clipboard::write(items);
    if(context.expectTrue("native clear failure retains progress", clearFailure.hasValue()))
    {
        static_cast<void>(
            context.expectEq("native clear failure does not start", ClipboardTypes::CommitState::NotStarted, clearFailure.value().commitState));
        static_cast<void>(context.expectEq("native clear failure publishes zero", std::size_t{0}, clearFailure.value().formatsPublished));
    }

    Desktop::Internal::TestHooks::failClipboardPublicationAt(0);
    const auto firstFailure = Clipboard::write(items);
    if(context.expectTrue("first publication failure retains progress", firstFailure.hasValue()))
    {
        static_cast<void>(
            context.expectEq("first publication failure reports Cleared", ClipboardTypes::CommitState::Cleared, firstFailure.value().commitState));
        static_cast<void>(context.expectEq("first publication failure count is zero", std::size_t{0}, firstFailure.value().formatsPublished));
    }

    Desktop::Internal::TestHooks::failClipboardPublicationAt(2);
    const auto partialFailure = Clipboard::write(items);
    if(context.expectTrue("partial publication failure retains progress", partialFailure.hasValue()))
    {
        static_cast<void>(context.expectEq(
            "later publication failure is partial",
            ClipboardTypes::CommitState::PartiallyPublished,
            partialFailure.value().commitState));
        static_cast<void>(context.expectEq("partial publication reports exact prefix", std::size_t{2}, partialFailure.value().formatsPublished));
    }

    Desktop::Internal::TestHooks::failNext(Desktop::Internal::TestHooks::FailurePoint::ClipboardClose);
    const auto closeAfterWrite = Clipboard::writeText("committed despite close failure");
    static_cast<void>(context.expectEq("close failure after full publication is reported", StatusCode::CloseFailed, closeAfterWrite.status().code()));
    if(context.expectTrue("close failure retains publication progress", closeAfterWrite.hasValue()))
    {
        static_cast<void>(
            context.expectEq("close failure preserves Published", ClipboardTypes::CommitState::Published, closeAfterWrite.value().commitState));
        static_cast<void>(context.expectEq("close failure preserves publication count", std::size_t{1}, closeAfterWrite.value().formatsPublished));
    }
    const auto committedText = Clipboard::readText();
    if(context.expectTrue("published side effect remains readable", committedText.ok()))
    {
        static_cast<void>(
            context.expectEq("published side effect survives close failure", std::string{"committed despite close failure"}, committedText.value()));
    }

    Desktop::Internal::TestHooks::failNext(Desktop::Internal::TestHooks::FailurePoint::ClipboardClose);
    const auto closeAfterClear = Clipboard::clear();
    static_cast<void>(context.expectEq("close failure after clear is reported", StatusCode::CloseFailed, closeAfterClear.status().code()));
    if(context.expectTrue("close failure retains clear progress", closeAfterClear.hasValue()))
    {
        static_cast<void>(context.expectTrue("close failure preserves cleared side effect", closeAfterClear.value().cleared));
    }
    Desktop::Internal::TestHooks::resetFailures();
#endif

    // Verify finite timeouts while another thread owns the Clipboard.
    std::atomic_bool holderReady = false;
    std::atomic_bool releaseHolder = false;
    std::atomic_bool holderOpened = false;
    std::thread holder(
        [&]
        {
            HWND owner = CreateWindowExW(
                0,
                L"STATIC",
                L"GameWIP Clipboard Contention",
                WS_POPUP,
                0,
                0,
                0,
                0,
                nullptr,
                nullptr,
                GetModuleHandleW(nullptr),
                nullptr);
            holderOpened.store(owner != nullptr && OpenClipboard(owner) != FALSE);
            holderReady.store(true);

            while(!releaseHolder.load())
            {
                std::this_thread::yield();
            }

            if(holderOpened.load())
            {
                static_cast<void>(CloseClipboard());
            }

            if(owner != nullptr)
            {
                static_cast<void>(DestroyWindow(owner));
            }
        });

    while(!holderReady.load())
    {
        std::this_thread::yield();
    }

    if(holderOpened.load())
    {
        const auto before = std::chrono::steady_clock::now();
        const auto boundedBusy = Clipboard::getFormats(std::chrono::milliseconds{20});
        const auto elapsed = std::chrono::steady_clock::now() - before;
        static_cast<void>(context.expectEq("finite timeout reports contention", StatusCode::ResourceBusy, boundedBusy.status().code()));
        static_cast<void>(context.expectTrue("finite timeout is bounded without busy spin", elapsed < std::chrono::seconds{1}));
    }
    else
    {
        context.skip("finite Clipboard contention timeout", "native holder could not acquire Clipboard");
    }
    releaseHolder.store(true);
    holder.join();

    // Confirm ordinary threads can use the Clipboard.
    std::atomic<StatusCode> threadResult{StatusCode::Unknown};
    std::thread ordinaryThread(
        [&]
        {
            threadResult.store(Clipboard::writeText("ordinary thread").status().code());
        });
    ordinaryThread.join();
    static_cast<void>(context.expectEq("ordinary non-Window thread may use Clipboard", StatusCode::Success, threadResult.load()));
    static_cast<void>(Clipboard::clear());
}
