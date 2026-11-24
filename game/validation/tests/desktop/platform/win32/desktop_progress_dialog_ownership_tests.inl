/// @file desktop_progress_dialog_ownership_tests.inl
/// @brief ProgressDialog ownership and exceptional lifetime tests.

#if DESKTOP_INTERNAL_TEST_HOOKS
/// ------------------------------------------------------------
/// Ownership and exceptional lifetime
/// ------------------------------------------------------------

void testProgressDialogOwnership(TestSupport::Context &context)
{
    Desktop::Types::Description windowDescription;
    windowDescription.activation.visible = false;
    Desktop::Window owner;
    if(!context.expectTrue("ProgressDialog owner Window opens", owner.open(windowDescription, Desktop::Events::owned(8)).ok()))
    {
        return;
    }

    static_cast<void>(context.expectTrue("ProgressDialog owner starts interaction-enabled", nativeInteractionEnabled(owner)));

    auto description = progressDescription();
    description.owner = &owner;
    description.blocksOwner = false;
    Desktop::ProgressDialog nonblocking;
    if(!context.expectTrue("nonblocking owned ProgressDialog opens", nonblocking.open(description).ok()))
    {
        static_cast<void>(owner.close());
        return;
    }
    static_cast<void>(context.expectTrue("blocksOwner false leaves owner enabled", nativeInteractionEnabled(owner)));
    static_cast<void>(context.expectTrue("owned ProgressDialog participates in event pump", Desktop::Events::poll().status.ok()));
    static_cast<void>(nonblocking.close());

    // Keep a shared owner blocked until its last modal ProgressDialog closes.
    description.blocksOwner = true;
    Desktop::ProgressDialog first;
    Desktop::ProgressDialog second;
    static_cast<void>(context.expectTrue("first blocking ProgressDialog opens", first.open(description).ok()));
    static_cast<void>(context.expectFalse("first blocker disables native owner", nativeInteractionEnabled(owner)));
    static_cast<void>(context.expectTrue("second blocking ProgressDialog opens", second.open(description).ok()));
    static_cast<void>(context.expectFalse("two blockers keep native owner disabled", nativeInteractionEnabled(owner)));
    static_cast<void>(context.expectTrue("first blocker closes", first.close().ok()));
    static_cast<void>(context.expectFalse("owner remains disabled until last blocker", nativeInteractionEnabled(owner)));
    static_cast<void>(context.expectTrue("second blocker closes", second.close().ok()));
    static_cast<void>(context.expectTrue("last blocker restores requested owner state", nativeInteractionEnabled(owner)));

    static_cast<void>(context.expectTrue("blockers reopen for reverse order", first.open(description).ok() && second.open(description).ok()));
    static_cast<void>(context.expectTrue("second blocker closes first", second.close().ok()));
    static_cast<void>(context.expectFalse("reverse close order retains block", nativeInteractionEnabled(owner)));
    static_cast<void>(context.expectTrue("first blocker closes last", first.close().ok()));
    static_cast<void>(context.expectTrue("reverse close order restores owner", nativeInteractionEnabled(owner)));

    // Preserve requested interaction state across blocking changes.
    static_cast<void>(context.expectTrue("owner can be pre-disabled", owner.setUserInteractionEnabled(false).ok()));
    static_cast<void>(context.expectFalse("pre-disabled owner is natively disabled", nativeInteractionEnabled(owner)));
    static_cast<void>(context.expectTrue("blocker opens over pre-disabled owner", first.open(description).ok()));
    static_cast<void>(context.expectTrue("pre-disabled blocker closes", first.close().ok()));
    static_cast<void>(context.expectFalse("last blocker preserves pre-disabled state", nativeInteractionEnabled(owner)));
    static_cast<void>(context.expectTrue("owner requested state re-enables", owner.setUserInteractionEnabled(true).ok()));

    static_cast<void>(context.expectTrue("blocker opens for requested-state update", first.open(description).ok()));
    static_cast<void>(context.expectTrue("application may request disabled while blocked", owner.setUserInteractionEnabled(false).ok()));
    static_cast<void>(context.expectFalse("disabled request remains effectively blocked", nativeInteractionEnabled(owner)));
    static_cast<void>(context.expectTrue("application may request enabled while blocked", owner.setUserInteractionEnabled(true).ok()));
    static_cast<void>(context.expectFalse("enabled request remains effectively blocked", nativeInteractionEnabled(owner)));
    static_cast<void>(context.expectTrue("requested-state blocker closes", first.close().ok()));
    static_cast<void>(context.expectTrue("last blocker restores latest requested state", nativeInteractionEnabled(owner)));

    // Restore ownership after native loss and keep ownerless dialogs independent.
    auto ownerlessDescription = progressDescription();
    ownerlessDescription.blocksOwner = true;
    Desktop::ProgressDialog ownerless;
    static_cast<void>(context.expectTrue("ownerless blocksOwner ProgressDialog opens", ownerless.open(ownerlessDescription).ok()));
    static_cast<void>(context.expectTrue("ownerless blocksOwner has no effect on unrelated Window", nativeInteractionEnabled(owner)));
    static_cast<void>(ownerless.close());

    Desktop::ProgressDialog lostOwnedProgress;
    static_cast<void>(context.expectTrue("owner starts enabled before owned ProgressDialog native loss", nativeInteractionEnabled(owner)));
    static_cast<void>(context.expectTrue("owned blocking ProgressDialog opens for native loss", lostOwnedProgress.open(description).ok()));
    static_cast<void>(context.expectFalse("owned blocking ProgressDialog disables owner", nativeInteractionEnabled(owner)));
    static_cast<void>(context.expectTrue(
        "unexpected owned ProgressDialog destruction succeeds",
        Win32TestHooks::destroyNativeProgressDialog(lostOwnedProgress).ok()));
    static_cast<void>(context.expectFalse("owned native ProgressDialog loss clears isOpen", lostOwnedProgress.isOpen()));
    static_cast<void>(context.expectEq(
        "owned native ProgressDialog loss removes active registry entry",
        std::size_t{0},
        Win32TestHooks::activeProgressDialogCount()));
    static_cast<void>(context.expectFalse(
        "owned native ProgressDialog loss removes blocker contribution",
        Win32TestHooks::inspectProgressDialog(lostOwnedProgress).blockingOwner));
    static_cast<void>(context.expectFalse("owner restoration remains deferred until dispatch", nativeInteractionEnabled(owner)));
    static_cast<void>(context.expectTrue("owner restoration dispatch succeeds", Desktop::Events::poll().status.ok()));
    static_cast<void>(context.expectTrue("live owner is restored after ProgressDialog native loss", nativeInteractionEnabled(owner)));
    static_cast<void>(context.expectTrue("ProgressDialog closes after owned native loss", lostOwnedProgress.close().ok()));

    // Retry a failed restore wake after overlapping blockers are removed.
    static_cast<void>(context.expectTrue("first blocker opens for failed restore wake", first.open(description).ok()));
    static_cast<void>(context.expectTrue("second blocker opens for failed restore wake", second.open(description).ok()));
    static_cast<void>(context.expectTrue("blocked owner accepts a deferred disabled request", owner.setUserInteractionEnabled(false).ok()));
    Desktop::Internal::TestHooks::failNext(Desktop::Internal::TestHooks::FailurePoint::ProgressOwnerRestoreWake);
    static_cast<void>(context.expectTrue(
        "unexpected ProgressDialog loss survives failed owner-restore wake",
        Win32TestHooks::destroyNativeProgressDialog(first).ok()));
    const auto failedWakePump = Desktop::Events::poll();
    static_cast<void>(context.expectFalse("failed owner-restore wake is reported by the next pump", failedWakePump.status.ok()));
    static_cast<void>(context.expectFalse("overlapping blocker retains effective disabled state after failed wake", nativeInteractionEnabled(owner)));
    static_cast<void>(context.expectTrue("blocked owner accepts a deferred enabled request", owner.setUserInteractionEnabled(true).ok()));
    static_cast<void>(context.expectTrue("last overlapping blocker closes after failed wake", second.close().ok()));
    static_cast<void>(context.expectTrue("failed wake retry restores latest requested owner state", nativeInteractionEnabled(owner)));
    static_cast<void>(context.expectTrue("destroyed ProgressDialog finalizes after failed wake", first.close().ok()));

    // Verify normal and unexpected owner destruction.
    static_cast<void>(context.expectTrue("owned ProgressDialog opens for normal owner close", first.open(description).ok()));
    static_cast<void>(context.expectTrue("owner closes after finalizing ProgressDialog", owner.close().ok()));
    static_cast<void>(context.expectFalse("normal owner close removes ProgressDialog presentation", first.isOpen()));
    static_cast<void>(
        context.expectEq("normal owner close removes active registry entry", std::size_t{0}, Win32TestHooks::activeProgressDialogCount()));
    static_cast<void>(context.expectTrue("ProgressDialog close remains safe after owner close", first.close().ok()));

    Desktop::Window unexpectedOwner;
    static_cast<void>(context.expectTrue("unexpected-loss owner opens", unexpectedOwner.open(windowDescription, Desktop::Events::owned(4)).ok()));
    description.owner = &unexpectedOwner;
    static_cast<void>(context.expectTrue("ProgressDialog opens for unexpected owner loss", first.open(description).ok()));
    static_cast<void>(
        context.expectTrue("unexpected native owner destruction succeeds", Desktop::Internal::TestHooks::destroyNativeWindow(unexpectedOwner).ok()));
    static_cast<void>(context.expectFalse("unexpected owner loss removes ProgressDialog presentation", first.isOpen()));
    static_cast<void>(
        context.expectEq("unexpected owner loss removes active registry entry", std::size_t{0}, Win32TestHooks::activeProgressDialogCount()));
    static_cast<void>(context.expectTrue("unexpected owner-loss restoration dispatch succeeds", Desktop::Events::poll().status.ok()));
    static_cast<void>(context.expectFalse("unexpected owner loss does not restore a dying native owner", nativeInteractionEnabled(unexpectedOwner)));
    static_cast<void>(context.expectTrue("ProgressDialog closes after unexpected owner loss", first.close().ok()));
    static_cast<void>(unexpectedOwner.close());
}

void testProgressDialogExceptionalLifetime(TestSupport::Context &context)
{
    // Finalize unexpected native loss and retry class release.
    const auto description = progressDescription();
    Desktop::ProgressDialog progress;
    static_cast<void>(context.expectTrue("ProgressDialog opens for native loss", progress.open(description).ok()));
    static_cast<void>(
        context.expectTrue("unexpected ProgressDialog destruction succeeds", Win32TestHooks::destroyNativeProgressDialog(progress).ok()));
    static_cast<void>(context.expectFalse("native ProgressDialog loss clears isOpen", progress.isOpen()));
    const auto lost = Win32TestHooks::inspectProgressDialog(progress);
    static_cast<void>(context.expectTrue("native loss retains pending-finalize state", lost.nativeDestroyedPendingFinalize));
    static_cast<void>(context.expectFalse("native loss removes active presentation", lost.registered));
    static_cast<void>(
        context.expectEq("active registry contains only live presentations", std::size_t{0}, Win32TestHooks::activeProgressDialogCount()));
    static_cast<void>(context.expectTrue("close finalizes unexpected native loss", progress.close().ok()));
    static_cast<void>(context.expectTrue("ProgressDialog reopens after native loss finalization", progress.open(description).ok()));
    static_cast<void>(progress.close());

    static_cast<void>(context.expectTrue("ProgressDialog opens for class-release failure", progress.open(description).ok()));
    Desktop::Internal::TestHooks::failNext(Desktop::Internal::TestHooks::FailurePoint::ProgressClassRelease);
    static_cast<void>(context.expectEq("class-release failure is reported", ErrorCode::CloseFailed, progress.close().code));
    static_cast<void>(context.expectFalse("class-release failure has no active presentation", progress.isOpen()));
    static_cast<void>(context.expectEq(
        "class-release failure retains accurate reference count",
        std::size_t{1},
        Win32TestHooks::progressDialogClassReferenceCount()));
    static_cast<void>(
        context.expectEq("class-release failure does not retain active registry entry", std::size_t{0}, Win32TestHooks::activeProgressDialogCount()));
    static_cast<void>(context.expectTrue("class-release retry succeeds", progress.close().ok()));
    static_cast<void>(context.expectEq("class-release retry clears reference", std::size_t{0}, Win32TestHooks::progressDialogClassReferenceCount()));

    // Reject foreign-thread access without disturbing the presentation.
    static_cast<void>(context.expectTrue("ProgressDialog opens for foreign-thread contract", progress.open(description).ok()));
    bool foreignOpen = true;
    bool foreignOwned = true;
    bool foreignCancel = true;
    ErrorCode foreignTitle = ErrorCode::Unknown;
    ErrorCode foreignHeading = ErrorCode::Unknown;
    ErrorCode foreignMessage = ErrorCode::Unknown;
    ErrorCode foreignMode = ErrorCode::Unknown;
    ErrorCode foreignProgress = ErrorCode::Unknown;
    ErrorCode foreignClose = ErrorCode::Unknown;
    std::thread foreign(
        [&]
        {
            foreignOpen = progress.isOpen();
            foreignOwned = progress.ownedByCurrentThread();
            foreignCancel = progress.hasCancelRequest();
            progress.clearCancelRequest();
            foreignTitle = progress.setTitle("foreign").code;
            foreignHeading = progress.setHeading("foreign").code;
            foreignMessage = progress.setMessage("foreign").code;
            foreignMode = progress.setMode(ProgressTypes::Mode::Indeterminate).code;
            foreignProgress = progress.setProgress(0.5).code;
            foreignClose = progress.close().code;
        });
    foreign.join();
    static_cast<void>(context.expectFalse("foreign thread does not report ProgressDialog open", foreignOpen));
    static_cast<void>(context.expectFalse("foreign thread does not own ProgressDialog", foreignOwned));
    static_cast<void>(context.expectFalse("foreign thread cannot query owner cancellation", foreignCancel));

    for(const ErrorCode code : {foreignTitle, foreignHeading, foreignMessage, foreignMode, foreignProgress, foreignClose})
    {
        static_cast<void>(context.expectEq("foreign ProgressDialog operation is ResourceBusy", ErrorCode::ResourceBusy, code));
    }
    static_cast<void>(context.expectTrue("foreign operations preserve native ProgressDialog", progress.isOpen()));
    static_cast<void>(progress.close());

    // Drain deferred destructors and retain the remaining chain after failure.
    auto deferredOne = std::make_unique<Desktop::ProgressDialog>();
    static_cast<void>(context.expectTrue("deferred ProgressDialog opens", deferredOne->open(description).ok()));
    std::thread destroyForeign(
        [owned = std::move(deferredOne)] mutable
        {
            owned.reset();
        });
    destroyForeign.join();
    static_cast<void>(context.expectEq("wrong-thread destructor transfers cleanup", std::size_t{1}, Win32TestHooks::deferredProgressDialogCount()));
    static_cast<void>(
        context.expectEq("transferred cleanup remains active until owner pump", std::size_t{1}, Win32TestHooks::activeProgressDialogCount()));
    static_cast<void>(context.expectTrue("owner pump finalizes transferred ProgressDialog", Desktop::Events::poll().status.ok()));
    static_cast<void>(context.expectEq("owner pump clears transferred cleanup", std::size_t{0}, Win32TestHooks::deferredProgressDialogCount()));
    static_cast<void>(
        context.expectEq("owner pump unregisters transferred ProgressDialog", std::size_t{0}, Win32TestHooks::activeProgressDialogCount()));

    auto deferredFirst = std::make_unique<Desktop::ProgressDialog>();
    auto deferredSecond = std::make_unique<Desktop::ProgressDialog>();
    static_cast<void>(context.expectTrue("first cleanup-chain ProgressDialog opens", deferredFirst->open(description).ok()));
    static_cast<void>(context.expectTrue("second cleanup-chain ProgressDialog opens", deferredSecond->open(description).ok()));
    std::thread destroyChain(
        [first = std::move(deferredFirst), second = std::move(deferredSecond)] mutable
        {
            first.reset();
            second.reset();
        });
    destroyChain.join();
    static_cast<void>(context.expectEq("deferred cleanup chain retains both states", std::size_t{2}, Win32TestHooks::deferredProgressDialogCount()));
    Desktop::Internal::TestHooks::failNext(Desktop::Internal::TestHooks::FailurePoint::Close);
    static_cast<void>(Desktop::Events::poll());
    static_cast<void>(
        context.expectEq("one cleanup failure preserves complete later chain", std::size_t{2}, Win32TestHooks::deferredProgressDialogCount()));
    static_cast<void>(Desktop::Events::poll());
    static_cast<void>(context.expectEq("deferred cleanup chain succeeds on retry", std::size_t{0}, Win32TestHooks::deferredProgressDialogCount()));
    static_cast<void>(context.expectEq("cleanup-chain retry removes all presentations", std::size_t{0}, Win32TestHooks::activeProgressDialogCount()));
}
#endif
