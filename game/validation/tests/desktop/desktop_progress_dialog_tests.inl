/// @file desktop_progress_dialog_tests.inl
/// @brief ProgressDialog validation, native presentation, blocking, and cleanup tests.

#if DESKTOP_INTERNAL_TEST_HOOKS
namespace ProgressTypes = Desktop::Types::Dialogs::Progress;

[[nodiscard]] bool nativeInteractionEnabled(const Desktop::Window &window)
{
    const auto handle = Desktop::Native::Win32::getHandle(window);
    return handle.status.ok() && handle.handle.window != nullptr && IsWindowEnabled(static_cast<HWND>(handle.handle.window)) != FALSE;
}

ProgressTypes::Description progressDescription()
{
    return {
        .title = "Progress title",
        .heading = "Progress heading",
        .message = "Progress message",
        .mode = ProgressTypes::Mode::Determinate,
        .progress = 0.25,
        .cancelable = true,
        .blocksOwner = true};
}

void testProgressDialogValidation(TestSupport::Context &context)
{
    Desktop::ProgressDialog progress;
    static_cast<void>(context.expectFalse("default ProgressDialog is closed", progress.isOpen()));
    static_cast<void>(context.expectFalse("closed ProgressDialog has no owner thread", progress.ownedByCurrentThread()));
    static_cast<void>(context.expectFalse("closed ProgressDialog has no cancellation", progress.hasCancelRequest()));
    progress.clearCancelRequest();
    static_cast<void>(context.expectTrue("closing a closed ProgressDialog succeeds", progress.close().ok()));
    static_cast<void>(context.expectEq("closed ProgressDialog title mutation is NotOpen", ErrorCode::NotOpen, progress.setTitle("title").code));
    static_cast<void>(context.expectEq("closed ProgressDialog heading mutation is NotOpen", ErrorCode::NotOpen, progress.setHeading("heading").code));
    static_cast<void>(context.expectEq("closed ProgressDialog message mutation is NotOpen", ErrorCode::NotOpen, progress.setMessage("message").code));
    static_cast<void>(context.expectEq(
        "closed ProgressDialog mode mutation is NotOpen",
        ErrorCode::NotOpen,
        progress.setMode(ProgressTypes::Mode::Determinate).code));
    static_cast<void>(context.expectEq("closed ProgressDialog value mutation is NotOpen", ErrorCode::NotOpen, progress.setProgress(0.5).code));

    auto description = progressDescription();
    description.mode = static_cast<ProgressTypes::Mode>(255);
    static_cast<void>(context.expectEq("ProgressDialog rejects invalid Mode", ErrorCode::InvalidArgument, progress.open(description).code));
    description.mode = ProgressTypes::Mode::Determinate;
    for (const double invalid : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(), -0.001, 1.001})
    {
        description.progress = invalid;
        static_cast<void>(
            context.expectEq("ProgressDialog rejects invalid initial progress", ErrorCode::InvalidArgument, progress.open(description).code));
    }

    const auto rejectText = [&](std::string_view name, std::string_view ProgressTypes::Description::*member)
    {
        description = progressDescription();
        description.*member = std::string_view{"bad\0text", 8};
        static_cast<void>(
            context.expectEq(std::format("ProgressDialog {} rejects null", name), ErrorCode::InvalidArgument, progress.open(description).code));
        description.*member = std::string_view{"\xF0\x28\x8C\x28", 4};
        static_cast<void>(context.expectEq(
            std::format("ProgressDialog {} rejects malformed UTF-8", name),
            ErrorCode::EncodingFailed,
            progress.open(description).code));
        static_cast<void>(context.expectFalse("failed ProgressDialog text conversion leaves closed", progress.isOpen()));
    };
    rejectText("title", &ProgressTypes::Description::title);
    rejectText("heading", &ProgressTypes::Description::heading);
    rejectText("message", &ProgressTypes::Description::message);

    for (const double boundary : {0.0, 1.0})
    {
        description = progressDescription();
        description.progress = boundary;
        static_cast<void>(context.expectTrue("ProgressDialog accepts progress boundary", progress.open(description).ok()));
        static_cast<void>(context.expectEq(
            "ProgressDialog boundary reaches native range",
            static_cast<int>(boundary * 10000.0),
            Desktop::TestHooks::inspectProgressDialog(progress).position));
        static_cast<void>(context.expectTrue("ProgressDialog closes after boundary", progress.close().ok()));
    }

    description = progressDescription();
    Desktop::TestHooks::failNext(Desktop::TestHooks::FailurePoint::Allocation);
    static_cast<void>(context.expectEq("ProgressDialog allocation failure maps", ErrorCode::OutOfMemory, progress.open(description).code));
    Desktop::TestHooks::failNext(Desktop::TestHooks::FailurePoint::ProgressClassRegistration);
    static_cast<void>(context.expectEq("ProgressDialog class registration failure maps", ErrorCode::OpenFailed, progress.open(description).code));
    Desktop::TestHooks::failNext(Desktop::TestHooks::FailurePoint::NativeCreation);
    static_cast<void>(context.expectEq("ProgressDialog native creation failure maps", ErrorCode::OpenFailed, progress.open(description).code));
    static_cast<void>(context.expectFalse("failed ProgressDialog opens remain reusable", progress.isOpen()));
    static_cast<void>(context.expectEq(
        "failed ProgressDialog opens release class references",
        std::size_t{0},
        Desktop::TestHooks::progressDialogClassReferenceCount()));
    static_cast<void>(context.expectTrue("ProgressDialog reuses object after failed open", progress.open(description).ok()));
    static_cast<void>(progress.close());

    Desktop::Window closedOwner;
    description.owner = &closedOwner;
    static_cast<void>(context.expectEq("ProgressDialog closed owner is NotOpen", ErrorCode::NotOpen, progress.open(description).code));
    Desktop::Types::Description ownerDescription;
    ownerDescription.visible = false;
    static_cast<void>(context.expectTrue("ProgressDialog validation owner opens", closedOwner.open(ownerDescription, 4).ok()));
    ErrorCode foreignOwnerCode = ErrorCode::Unknown;
    std::thread foreign(
        [&]
        {
            foreignOwnerCode = progress.open(description).code;
        });
    foreign.join();
    static_cast<void>(context.expectEq("ProgressDialog foreign-thread owner is busy", ErrorCode::ResourceBusy, foreignOwnerCode));
    Desktop::TestHooks::failNext(Desktop::TestHooks::FailurePoint::ProgressOwnerBlocking);
    static_cast<void>(context.expectEq("ProgressDialog owner-blocking failure maps", ErrorCode::NativeFailure, progress.open(description).code));
    static_cast<void>(context.expectTrue("failed owner blocking restores owner interaction", nativeInteractionEnabled(closedOwner)));
    static_cast<void>(closedOwner.close());
}

void testProgressDialogNativeLifecycle(TestSupport::Context &context)
{
    Desktop::ProgressDialog progress;
    auto description = progressDescription();
    static_cast<void>(context.expectTrue("ownerless ProgressDialog opens", progress.open(description).ok()));
    static_cast<void>(context.expectTrue("open ProgressDialog reports live", progress.isOpen()));
    static_cast<void>(context.expectTrue("opening thread owns ProgressDialog", progress.ownedByCurrentThread()));
    static_cast<void>(context.expectEq("open ProgressDialog is registered", std::size_t{1}, Desktop::TestHooks::activeProgressDialogCount()));
    static_cast<void>(
        context.expectEq("open ProgressDialog holds one class reference", std::size_t{1}, Desktop::TestHooks::progressDialogClassReferenceCount()));
    static_cast<void>(context.expectEq("second ProgressDialog open is AlreadyOpen", ErrorCode::AlreadyOpen, progress.open(description).code));
    const auto initial = Desktop::TestHooks::inspectProgressDialog(progress);
    static_cast<void>(context.expectTrue("ProgressDialog owns a native window", initial.nativeWindow));
    static_cast<void>(context.expectTrue("cancelable ProgressDialog owns a Cancel control", initial.cancelControl));
    static_cast<void>(context.expectEq("ProgressDialog native range starts at zero", 0, initial.rangeMinimum));
    static_cast<void>(context.expectEq("ProgressDialog native range ends at 10000", 10000, initial.rangeMaximum));
    static_cast<void>(context.expectEq("ProgressDialog initial value uses native range", 2500, initial.position));
    static_cast<void>(context.expectFalse("determinate ProgressDialog is not marquee", initial.marquee));
    static_cast<void>(context.expectEq("ProgressDialog initial title propagates", std::wstring{L"Progress title"}, initial.title));
    static_cast<void>(context.expectEq("ProgressDialog initial heading propagates", std::wstring{L"Progress heading"}, initial.heading));
    static_cast<void>(context.expectEq("ProgressDialog initial message propagates", std::wstring{L"Progress message"}, initial.message));
    static_cast<void>(context.expectTrue("ownerless ProgressDialog keeps event polling active", Desktop::Events::poll().status.ok()));
    static_cast<void>(context.expectTrue(
        "ownerless ProgressDialog keeps bounded event waiting active",
        Desktop::Events::wait(std::chrono::milliseconds{0}).status.ok()));

    static_cast<void>(context.expectTrue("ProgressDialog title updates live", progress.setTitle("Updated title").ok()));
    static_cast<void>(context.expectTrue("ProgressDialog heading updates live", progress.setHeading("Updated heading").ok()));
    static_cast<void>(context.expectTrue("ProgressDialog message updates live", progress.setMessage("Updated message").ok()));
    auto updated = Desktop::TestHooks::inspectProgressDialog(progress);
    static_cast<void>(context.expectEq("updated title reaches native window", std::wstring{L"Updated title"}, updated.title));
    static_cast<void>(context.expectEq("updated heading reaches native control", std::wstring{L"Updated heading"}, updated.heading));
    static_cast<void>(context.expectEq("updated message reaches native control", std::wstring{L"Updated message"}, updated.message));
    static_cast<void>(
        context.expectEq("live title rejects embedded null", ErrorCode::InvalidArgument, progress.setTitle(std::string_view{"a\0b", 3}).code));
    static_cast<void>(
        context.expectEq("live heading rejects malformed UTF-8", ErrorCode::EncodingFailed, progress.setHeading(std::string_view{"\xC3", 1}).code));
    Desktop::TestHooks::failNext(Desktop::TestHooks::FailurePoint::ProgressMutation);
    static_cast<void>(context.expectEq("native text failure maps", ErrorCode::NativeFailure, progress.setMessage("not applied").code));
    static_cast<void>(context.expectEq(
        "failed text mutation preserves native text",
        std::wstring{L"Updated message"},
        Desktop::TestHooks::inspectProgressDialog(progress).message));

    Desktop::TestHooks::failNext(Desktop::TestHooks::FailurePoint::WindowStyleQuery);
    static_cast<void>(context.expectEq(
        "ProgressDialog style query failure is returned before mode mutation",
        ErrorCode::NativeFailure,
        progress.setMode(ProgressTypes::Mode::Indeterminate).code));
    static_cast<void>(
        context.expectFalse("failed ProgressDialog style query keeps determinate mode", Desktop::TestHooks::inspectProgressDialog(progress).marquee));
    static_cast<void>(context.expectTrue("ProgressDialog switches to indeterminate", progress.setMode(ProgressTypes::Mode::Indeterminate).ok()));
    static_cast<void>(
        context.expectTrue("indeterminate ProgressDialog enables marquee", Desktop::TestHooks::inspectProgressDialog(progress).marquee));
    static_cast<void>(context.expectTrue("indeterminate ProgressDialog retains numeric updates", progress.setProgress(0.75).ok()));
    static_cast<void>(context.expectTrue("ProgressDialog switches back to determinate", progress.setMode(ProgressTypes::Mode::Determinate).ok()));
    static_cast<void>(
        context.expectEq("switching back exposes retained numeric value", 7500, Desktop::TestHooks::inspectProgressDialog(progress).position));
    static_cast<void>(context.expectTrue("ProgressDialog accepts live zero", progress.setProgress(0.0).ok()));
    static_cast<void>(context.expectEq("live zero reaches native control", 0, Desktop::TestHooks::inspectProgressDialog(progress).position));
    static_cast<void>(context.expectTrue("ProgressDialog accepts live one", progress.setProgress(1.0).ok()));
    static_cast<void>(context.expectEq("live one reaches native control", 10000, Desktop::TestHooks::inspectProgressDialog(progress).position));
    static_cast<void>(context.expectTrue("ProgressDialog rounds native numeric values", progress.setProgress(0.33335).ok()));
    static_cast<void>(
        context.expectEq("ProgressDialog native rounding is consistent", 3334, Desktop::TestHooks::inspectProgressDialog(progress).position));
    static_cast<void>(context.expectEq(
        "live ProgressDialog rejects NaN",
        ErrorCode::InvalidArgument,
        progress.setProgress(std::numeric_limits<double>::quiet_NaN()).code));
    static_cast<void>(context.expectEq(
        "live ProgressDialog rejects infinity",
        ErrorCode::InvalidArgument,
        progress.setProgress(std::numeric_limits<double>::infinity()).code));
    static_cast<void>(context.expectEq("live ProgressDialog rejects negative", ErrorCode::InvalidArgument, progress.setProgress(-0.1).code));
    static_cast<void>(context.expectEq("live ProgressDialog rejects values above one", ErrorCode::InvalidArgument, progress.setProgress(1.1).code));
    static_cast<void>(context.expectEq(
        "live ProgressDialog rejects invalid Mode",
        ErrorCode::InvalidArgument,
        progress.setMode(static_cast<ProgressTypes::Mode>(255)).code));

    Desktop::TestHooks::failNext(Desktop::TestHooks::FailurePoint::ProgressMutation);
    static_cast<void>(context.expectEq("native progress failure maps", ErrorCode::NativeFailure, progress.setProgress(0.5).code));
    static_cast<void>(
        context.expectEq("failed progress mutation preserves native position", 3334, Desktop::TestHooks::inspectProgressDialog(progress).position));
    Desktop::TestHooks::failNext(Desktop::TestHooks::FailurePoint::ProgressMutation);
    static_cast<void>(
        context.expectEq("native mode failure maps", ErrorCode::NativeFailure, progress.setMode(ProgressTypes::Mode::Indeterminate).code));
    static_cast<void>(
        context.expectFalse("failed mode mutation rolls native style back", Desktop::TestHooks::inspectProgressDialog(progress).marquee));

    static_cast<void>(context.expectFalse("new ProgressDialog has no cancel request", progress.hasCancelRequest()));
    static_cast<void>(context.expectTrue("native Cancel request is simulated", Desktop::TestHooks::requestProgressDialogCancel(progress).ok()));
    static_cast<void>(context.expectTrue("Cancel control sets sticky request", progress.hasCancelRequest()));
    static_cast<void>(context.expectTrue("Cancel request does not close presentation", progress.isOpen()));
    progress.clearCancelRequest();
    static_cast<void>(context.expectFalse("clear resets sticky cancellation", progress.hasCancelRequest()));
    static_cast<void>(context.expectTrue("native close gesture is simulated", Desktop::TestHooks::requestProgressDialogClose(progress).ok()));
    static_cast<void>(context.expectTrue("cancelable close gesture sets sticky request", progress.hasCancelRequest()));
    static_cast<void>(context.expectTrue("cancelable close gesture leaves dialog open", progress.isOpen()));

    const Desktop::TestHooks::NativePixelRect suggested{.x = 40, .y = 60, .width = 520, .height = 260};
    static_cast<void>(context.expectTrue(
        "ProgressDialog DPI transition is simulated",
        Desktop::TestHooks::simulateProgressDialogDpiChange(progress, suggested, 144).ok()));
    const auto dpiChanged = Desktop::TestHooks::inspectProgressDialog(progress);
    static_cast<void>(context.expectEq("WM_DPICHANGED consumes suggested bounds", suggested, dpiChanged.windowBounds));
    static_cast<void>(context.expectTrue(
        "WM_DPICHANGED relayout keeps progress control nonempty",
        dpiChanged.progressBounds.width > 0 && dpiChanged.progressBounds.height > 0));

    Desktop::TestHooks::failNext(Desktop::TestHooks::FailurePoint::Close);
    static_cast<void>(context.expectEq("ProgressDialog native close failure is reported", ErrorCode::CloseFailed, progress.close().code));
    static_cast<void>(context.expectTrue("failed native close retains retryable presentation", progress.isOpen()));
    static_cast<void>(
        context.expectEq("failed native close retains active registry", std::size_t{1}, Desktop::TestHooks::activeProgressDialogCount()));
    static_cast<void>(context.expectTrue("ProgressDialog native close retry succeeds", progress.close().ok()));
    static_cast<void>(context.expectFalse("closed ProgressDialog clears open state", progress.isOpen()));
    static_cast<void>(context.expectFalse("closed ProgressDialog clears owner identity", progress.ownedByCurrentThread()));
    static_cast<void>(context.expectFalse("successful close clears cancellation lifetime", progress.hasCancelRequest()));
    static_cast<void>(context.expectEq("closed ProgressDialog unregisters", std::size_t{0}, Desktop::TestHooks::activeProgressDialogCount()));
    static_cast<void>(
        context.expectEq("closed ProgressDialog releases class", std::size_t{0}, Desktop::TestHooks::progressDialogClassReferenceCount()));

    static_cast<void>(context.expectTrue("ProgressDialog reopens after close", progress.open(description).ok()));
    static_cast<void>(context.expectFalse("reopened ProgressDialog starts without cancellation", progress.hasCancelRequest()));
    static_cast<void>(progress.close());

    description.cancelable = false;
    static_cast<void>(context.expectTrue("noncancelable ProgressDialog opens", progress.open(description).ok()));
    static_cast<void>(
        context.expectFalse("noncancelable ProgressDialog has no Cancel control", Desktop::TestHooks::inspectProgressDialog(progress).cancelControl));
    static_cast<void>(context.expectEq(
        "noncancelable ProgressDialog cannot receive Cancel click",
        ErrorCode::Unsupported,
        Desktop::TestHooks::requestProgressDialogCancel(progress).code));
    static_cast<void>(context.expectTrue("noncancelable close gesture is ignored", Desktop::TestHooks::requestProgressDialogClose(progress).ok()));
    static_cast<void>(context.expectTrue("noncancelable close gesture leaves presentation open", progress.isOpen()));
    static_cast<void>(context.expectFalse("noncancelable close gesture does not set request", progress.hasCancelRequest()));
    static_cast<void>(progress.close());
}

void testProgressDialogOwnership(TestSupport::Context &context)
{
    Desktop::Types::Description windowDescription;
    windowDescription.visible = false;
    Desktop::Window owner;
    static_cast<void>(context.expectTrue("ProgressDialog owner Window opens", owner.open(windowDescription, 8).ok()));
    static_cast<void>(context.expectTrue("ProgressDialog owner starts interaction-enabled", nativeInteractionEnabled(owner)));

    auto description = progressDescription();
    description.owner = &owner;
    description.blocksOwner = false;
    Desktop::ProgressDialog nonblocking;
    static_cast<void>(context.expectTrue("nonblocking owned ProgressDialog opens", nonblocking.open(description).ok()));
    static_cast<void>(context.expectTrue("blocksOwner false leaves owner enabled", nativeInteractionEnabled(owner)));
    static_cast<void>(context.expectTrue("owned ProgressDialog participates in event pump", Desktop::Events::poll().status.ok()));
    static_cast<void>(nonblocking.close());

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
        Desktop::TestHooks::destroyNativeProgressDialog(lostOwnedProgress).ok()));
    static_cast<void>(context.expectFalse("owned native ProgressDialog loss clears isOpen", lostOwnedProgress.isOpen()));
    static_cast<void>(context.expectEq(
        "owned native ProgressDialog loss removes active registry entry",
        std::size_t{0},
        Desktop::TestHooks::activeProgressDialogCount()));
    static_cast<void>(context.expectFalse(
        "owned native ProgressDialog loss removes blocker contribution",
        Desktop::TestHooks::inspectProgressDialog(lostOwnedProgress).blockingOwner));
    static_cast<void>(context.expectFalse("owner restoration remains deferred until dispatch", nativeInteractionEnabled(owner)));
    static_cast<void>(context.expectTrue("owner restoration dispatch succeeds", Desktop::Events::poll().status.ok()));
    static_cast<void>(context.expectTrue("live owner is restored after ProgressDialog native loss", nativeInteractionEnabled(owner)));
    static_cast<void>(context.expectTrue("ProgressDialog closes after owned native loss", lostOwnedProgress.close().ok()));

    static_cast<void>(context.expectTrue("first blocker opens for failed restore wake", first.open(description).ok()));
    static_cast<void>(context.expectTrue("second blocker opens for failed restore wake", second.open(description).ok()));
    static_cast<void>(context.expectTrue("blocked owner accepts a deferred disabled request", owner.setUserInteractionEnabled(false).ok()));
    Desktop::TestHooks::failNext(Desktop::TestHooks::FailurePoint::ProgressOwnerRestoreWake);
    static_cast<void>(context.expectTrue(
        "unexpected ProgressDialog loss survives failed owner-restore wake",
        Desktop::TestHooks::destroyNativeProgressDialog(first).ok()));
    const auto failedWakePump = Desktop::Events::poll();
    static_cast<void>(context.expectFalse("failed owner-restore wake is reported by the next pump", failedWakePump.status.ok()));
    static_cast<void>(context.expectFalse("overlapping blocker retains effective disabled state after failed wake", nativeInteractionEnabled(owner)));
    static_cast<void>(context.expectTrue("blocked owner accepts a deferred enabled request", owner.setUserInteractionEnabled(true).ok()));
    static_cast<void>(context.expectTrue("last overlapping blocker closes after failed wake", second.close().ok()));
    static_cast<void>(context.expectTrue("failed wake retry restores latest requested owner state", nativeInteractionEnabled(owner)));
    static_cast<void>(context.expectTrue("destroyed ProgressDialog finalizes after failed wake", first.close().ok()));

    static_cast<void>(context.expectTrue("owned ProgressDialog opens for normal owner close", first.open(description).ok()));
    static_cast<void>(context.expectTrue("owner closes after finalizing ProgressDialog", owner.close().ok()));
    static_cast<void>(context.expectFalse("normal owner close removes ProgressDialog presentation", first.isOpen()));
    static_cast<void>(
        context.expectEq("normal owner close removes active registry entry", std::size_t{0}, Desktop::TestHooks::activeProgressDialogCount()));
    static_cast<void>(context.expectTrue("ProgressDialog close remains safe after owner close", first.close().ok()));

    Desktop::Window unexpectedOwner;
    static_cast<void>(context.expectTrue("unexpected-loss owner opens", unexpectedOwner.open(windowDescription, 4).ok()));
    description.owner = &unexpectedOwner;
    static_cast<void>(context.expectTrue("ProgressDialog opens for unexpected owner loss", first.open(description).ok()));
    static_cast<void>(
        context.expectTrue("unexpected native owner destruction succeeds", Desktop::TestHooks::destroyNativeWindow(unexpectedOwner).ok()));
    static_cast<void>(context.expectFalse("unexpected owner loss removes ProgressDialog presentation", first.isOpen()));
    static_cast<void>(
        context.expectEq("unexpected owner loss removes active registry entry", std::size_t{0}, Desktop::TestHooks::activeProgressDialogCount()));
    static_cast<void>(context.expectTrue("unexpected owner-loss restoration dispatch succeeds", Desktop::Events::poll().status.ok()));
    static_cast<void>(context.expectFalse("unexpected owner loss does not restore a dying native owner", nativeInteractionEnabled(unexpectedOwner)));
    static_cast<void>(context.expectTrue("ProgressDialog closes after unexpected owner loss", first.close().ok()));
    static_cast<void>(unexpectedOwner.close());
}

void testProgressDialogExceptionalLifetime(TestSupport::Context &context)
{
    auto description = progressDescription();
    Desktop::ProgressDialog progress;
    static_cast<void>(context.expectTrue("ProgressDialog opens for native loss", progress.open(description).ok()));
    static_cast<void>(
        context.expectTrue("unexpected ProgressDialog destruction succeeds", Desktop::TestHooks::destroyNativeProgressDialog(progress).ok()));
    static_cast<void>(context.expectFalse("native ProgressDialog loss clears isOpen", progress.isOpen()));
    const auto lost = Desktop::TestHooks::inspectProgressDialog(progress);
    static_cast<void>(context.expectTrue("native loss retains pending-finalize state", lost.nativeDestroyedPendingFinalize));
    static_cast<void>(context.expectFalse("native loss removes active presentation", lost.registered));
    static_cast<void>(
        context.expectEq("active registry contains only live presentations", std::size_t{0}, Desktop::TestHooks::activeProgressDialogCount()));
    static_cast<void>(context.expectTrue("close finalizes unexpected native loss", progress.close().ok()));
    static_cast<void>(context.expectTrue("ProgressDialog reopens after native loss finalization", progress.open(description).ok()));
    static_cast<void>(progress.close());

    static_cast<void>(context.expectTrue("ProgressDialog opens for class-release failure", progress.open(description).ok()));
    Desktop::TestHooks::failNext(Desktop::TestHooks::FailurePoint::ProgressClassRelease);
    static_cast<void>(context.expectEq("class-release failure is reported", ErrorCode::CloseFailed, progress.close().code));
    static_cast<void>(context.expectFalse("class-release failure has no active presentation", progress.isOpen()));
    static_cast<void>(context.expectEq(
        "class-release failure retains accurate reference count",
        std::size_t{1},
        Desktop::TestHooks::progressDialogClassReferenceCount()));
    static_cast<void>(context.expectEq(
        "class-release failure does not retain active registry entry",
        std::size_t{0},
        Desktop::TestHooks::activeProgressDialogCount()));
    static_cast<void>(context.expectTrue("class-release retry succeeds", progress.close().ok()));
    static_cast<void>(
        context.expectEq("class-release retry clears reference", std::size_t{0}, Desktop::TestHooks::progressDialogClassReferenceCount()));

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
    for (const ErrorCode code : {foreignTitle, foreignHeading, foreignMessage, foreignMode, foreignProgress, foreignClose})
    {
        static_cast<void>(context.expectEq("foreign ProgressDialog operation is ResourceBusy", ErrorCode::ResourceBusy, code));
    }
    static_cast<void>(context.expectTrue("foreign operations preserve native ProgressDialog", progress.isOpen()));
    static_cast<void>(progress.close());

    auto deferredOne = std::make_unique<Desktop::ProgressDialog>();
    static_cast<void>(context.expectTrue("deferred ProgressDialog opens", deferredOne->open(description).ok()));
    std::thread destroyForeign(
        [owned = std::move(deferredOne)]() mutable
        {
            owned.reset();
        });
    destroyForeign.join();
    static_cast<void>(
        context.expectEq("wrong-thread destructor transfers cleanup", std::size_t{1}, Desktop::TestHooks::deferredProgressDialogCount()));
    static_cast<void>(
        context.expectEq("transferred cleanup remains active until owner pump", std::size_t{1}, Desktop::TestHooks::activeProgressDialogCount()));
    static_cast<void>(context.expectTrue("owner pump finalizes transferred ProgressDialog", Desktop::Events::poll().status.ok()));
    static_cast<void>(context.expectEq("owner pump clears transferred cleanup", std::size_t{0}, Desktop::TestHooks::deferredProgressDialogCount()));
    static_cast<void>(
        context.expectEq("owner pump unregisters transferred ProgressDialog", std::size_t{0}, Desktop::TestHooks::activeProgressDialogCount()));

    auto deferredFirst = std::make_unique<Desktop::ProgressDialog>();
    auto deferredSecond = std::make_unique<Desktop::ProgressDialog>();
    static_cast<void>(context.expectTrue("first cleanup-chain ProgressDialog opens", deferredFirst->open(description).ok()));
    static_cast<void>(context.expectTrue("second cleanup-chain ProgressDialog opens", deferredSecond->open(description).ok()));
    std::thread destroyChain(
        [first = std::move(deferredFirst), second = std::move(deferredSecond)]() mutable
        {
            first.reset();
            second.reset();
        });
    destroyChain.join();
    static_cast<void>(
        context.expectEq("deferred cleanup chain retains both states", std::size_t{2}, Desktop::TestHooks::deferredProgressDialogCount()));
    Desktop::TestHooks::failNext(Desktop::TestHooks::FailurePoint::Close);
    static_cast<void>(Desktop::Events::poll());
    static_cast<void>(
        context.expectEq("one cleanup failure preserves complete later chain", std::size_t{2}, Desktop::TestHooks::deferredProgressDialogCount()));
    static_cast<void>(Desktop::Events::poll());
    static_cast<void>(
        context.expectEq("deferred cleanup chain succeeds on retry", std::size_t{0}, Desktop::TestHooks::deferredProgressDialogCount()));
    static_cast<void>(
        context.expectEq("cleanup-chain retry removes all presentations", std::size_t{0}, Desktop::TestHooks::activeProgressDialogCount()));
}
#endif
