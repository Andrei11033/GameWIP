/// @file desktop_progress_dialog_tests.inl
/// @brief ProgressDialog validation and native lifecycle tests.

#if DESKTOP_INTERNAL_TEST_HOOKS
/// ------------------------------------------------------------
/// Validation and native lifecycle
/// ------------------------------------------------------------

void testProgressDialogValidation(TestSupport::Context &context)
{
    // Check closed behavior and reject invalid descriptions.
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
    description.presentationMode = static_cast<ProgressTypes::Mode>(255);
    static_cast<void>(context.expectEq("ProgressDialog rejects invalid Mode", ErrorCode::InvalidArgument, progress.open(description).code));
    description.presentationMode = ProgressTypes::Mode::Determinate;

    for(const double invalid : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(), -0.001, 1.001})
    {
        description.progress = invalid;
        static_cast<void>(
            context.expectEq("ProgressDialog rejects invalid initial progress", ErrorCode::InvalidArgument, progress.open(description).code));
    }

    // Reject malformed text in each description field.
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

    // Verify native range conversion and open rollback.
    for(const double boundary : {0.0, 1.0})
    {
        description = progressDescription();
        description.progress = boundary;
        if(!context.expectTrue("ProgressDialog accepts progress boundary", progress.open(description).ok()))
        {
            return;
        }

        static_cast<void>(context.expectEq(
            "ProgressDialog boundary reaches native range",
            static_cast<int>(boundary * 10000.0),
            Win32TestHooks::inspectProgressDialog(progress).position));
        static_cast<void>(context.expectTrue("ProgressDialog closes after boundary", progress.close().ok()));
    }

    description = progressDescription();
    Desktop::Internal::TestHooks::failNext(Desktop::Internal::TestHooks::FailurePoint::Allocation);
    static_cast<void>(context.expectEq("ProgressDialog allocation failure maps", ErrorCode::OutOfMemory, progress.open(description).code));
    Desktop::Internal::TestHooks::failNext(Desktop::Internal::TestHooks::FailurePoint::ProgressClassRegistration);
    static_cast<void>(context.expectEq("ProgressDialog class registration failure maps", ErrorCode::OpenFailed, progress.open(description).code));
    Desktop::Internal::TestHooks::failNext(Desktop::Internal::TestHooks::FailurePoint::NativeCreation);
    static_cast<void>(context.expectEq("ProgressDialog native creation failure maps", ErrorCode::OpenFailed, progress.open(description).code));
    static_cast<void>(context.expectFalse("failed ProgressDialog opens remain reusable", progress.isOpen()));
    static_cast<void>(context.expectEq(
        "failed ProgressDialog opens release class references",
        std::size_t{0},
        Win32TestHooks::progressDialogClassReferenceCount()));
    static_cast<void>(context.expectTrue("ProgressDialog reuses object after failed open", progress.open(description).ok()));
    static_cast<void>(progress.close());

    // Check owner binding and thread affinity.
    Desktop::Window closedOwner;
    description.owner = &closedOwner;
    static_cast<void>(context.expectEq("ProgressDialog closed owner is NotOpen", ErrorCode::NotOpen, progress.open(description).code));
    Desktop::Types::Description ownerDescription;
    ownerDescription.activation.visible = false;
    if(!context.expectTrue("ProgressDialog validation owner opens", closedOwner.open(ownerDescription, Desktop::Events::owned(4)).ok()))
    {
        return;
    }
    ErrorCode foreignOwnerCode = ErrorCode::Unknown;
    std::thread foreign(
        [&]
        {
            foreignOwnerCode = progress.open(description).code;
        });
    foreign.join();
    static_cast<void>(context.expectEq("ProgressDialog foreign-thread owner is busy", ErrorCode::ResourceBusy, foreignOwnerCode));
    Desktop::Internal::TestHooks::failNext(Desktop::Internal::TestHooks::FailurePoint::ProgressOwnerBlocking);
    static_cast<void>(context.expectEq("ProgressDialog owner-blocking failure maps", ErrorCode::NativeFailure, progress.open(description).code));
    static_cast<void>(context.expectTrue("failed owner blocking restores owner interaction", nativeInteractionEnabled(closedOwner)));
    static_cast<void>(closedOwner.close());
}

void testProgressDialogNativeLifecycle(TestSupport::Context &context)
{
    // Check initial native state and live text updates.
    Desktop::ProgressDialog progress;
    auto description = progressDescription();
    if(!context.expectTrue("ownerless ProgressDialog opens", progress.open(description).ok()))
    {
        return;
    }

    static_cast<void>(context.expectTrue("open ProgressDialog reports live", progress.isOpen()));
    static_cast<void>(context.expectTrue("opening thread owns ProgressDialog", progress.ownedByCurrentThread()));
    static_cast<void>(context.expectEq("open ProgressDialog is registered", std::size_t{1}, Win32TestHooks::activeProgressDialogCount()));
    static_cast<void>(
        context.expectEq("open ProgressDialog holds one class reference", std::size_t{1}, Win32TestHooks::progressDialogClassReferenceCount()));
    static_cast<void>(context.expectEq("second ProgressDialog open is AlreadyOpen", ErrorCode::AlreadyOpen, progress.open(description).code));
    const auto initial = Win32TestHooks::inspectProgressDialog(progress);
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
    const auto updated = Win32TestHooks::inspectProgressDialog(progress);
    static_cast<void>(context.expectEq("updated title reaches native window", std::wstring{L"Updated title"}, updated.title));
    static_cast<void>(context.expectEq("updated heading reaches native control", std::wstring{L"Updated heading"}, updated.heading));
    static_cast<void>(context.expectEq("updated message reaches native control", std::wstring{L"Updated message"}, updated.message));
    static_cast<void>(
        context.expectEq("live title rejects embedded null", ErrorCode::InvalidArgument, progress.setTitle(std::string_view{"a\0b", 3}).code));
    static_cast<void>(
        context.expectEq("live heading rejects malformed UTF-8", ErrorCode::EncodingFailed, progress.setHeading(std::string_view{"\xC3", 1}).code));
    Desktop::Internal::TestHooks::failNext(Desktop::Internal::TestHooks::FailurePoint::ProgressMutation);
    static_cast<void>(context.expectEq("native text failure maps", ErrorCode::NativeFailure, progress.setMessage("not applied").code));
    static_cast<void>(context.expectEq(
        "failed text mutation preserves native text",
        std::wstring{L"Updated message"},
        Win32TestHooks::inspectProgressDialog(progress).message));

    // Check mode changes and native progress-range mapping.
    Desktop::Internal::TestHooks::failNext(Desktop::Internal::TestHooks::FailurePoint::WindowStyleQuery);
    static_cast<void>(context.expectEq(
        "ProgressDialog style query failure is returned before mode mutation",
        ErrorCode::NativeFailure,
        progress.setMode(ProgressTypes::Mode::Indeterminate).code));
    static_cast<void>(
        context.expectFalse("failed ProgressDialog style query keeps determinate mode", Win32TestHooks::inspectProgressDialog(progress).marquee));
    static_cast<void>(context.expectTrue("ProgressDialog switches to indeterminate", progress.setMode(ProgressTypes::Mode::Indeterminate).ok()));
    static_cast<void>(context.expectTrue("indeterminate ProgressDialog enables marquee", Win32TestHooks::inspectProgressDialog(progress).marquee));
    static_cast<void>(context.expectTrue("indeterminate ProgressDialog retains numeric updates", progress.setProgress(0.75).ok()));
    static_cast<void>(context.expectTrue("ProgressDialog switches back to determinate", progress.setMode(ProgressTypes::Mode::Determinate).ok()));
    static_cast<void>(
        context.expectEq("switching back exposes retained numeric value", 7500, Win32TestHooks::inspectProgressDialog(progress).position));
    static_cast<void>(context.expectTrue("ProgressDialog accepts live zero", progress.setProgress(0.0).ok()));
    static_cast<void>(context.expectEq("live zero reaches native control", 0, Win32TestHooks::inspectProgressDialog(progress).position));
    static_cast<void>(context.expectTrue("ProgressDialog accepts live one", progress.setProgress(1.0).ok()));
    static_cast<void>(context.expectEq("live one reaches native control", 10000, Win32TestHooks::inspectProgressDialog(progress).position));
    static_cast<void>(context.expectTrue("ProgressDialog rounds native numeric values", progress.setProgress(0.33335).ok()));
    static_cast<void>(
        context.expectEq("ProgressDialog native rounding is consistent", 3334, Win32TestHooks::inspectProgressDialog(progress).position));
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

    Desktop::Internal::TestHooks::failNext(Desktop::Internal::TestHooks::FailurePoint::ProgressMutation);
    static_cast<void>(context.expectEq("native progress failure maps", ErrorCode::NativeFailure, progress.setProgress(0.5).code));
    static_cast<void>(
        context.expectEq("failed progress mutation preserves native position", 3334, Win32TestHooks::inspectProgressDialog(progress).position));
    Desktop::Internal::TestHooks::failNext(Desktop::Internal::TestHooks::FailurePoint::ProgressMutation);
    static_cast<void>(
        context.expectEq("native mode failure maps", ErrorCode::NativeFailure, progress.setMode(ProgressTypes::Mode::Indeterminate).code));
    static_cast<void>(context.expectFalse("failed mode mutation rolls native style back", Win32TestHooks::inspectProgressDialog(progress).marquee));

    // Check cancellation and DPI relayout.
    static_cast<void>(context.expectFalse("new ProgressDialog has no cancel request", progress.hasCancelRequest()));
    static_cast<void>(context.expectTrue("native Cancel request is simulated", Win32TestHooks::requestProgressDialogCancel(progress).ok()));
    static_cast<void>(context.expectTrue("Cancel control sets sticky request", progress.hasCancelRequest()));
    static_cast<void>(context.expectTrue("Cancel request does not close presentation", progress.isOpen()));
    progress.clearCancelRequest();
    static_cast<void>(context.expectFalse("clear resets sticky cancellation", progress.hasCancelRequest()));
    static_cast<void>(context.expectTrue("native close gesture is simulated", Win32TestHooks::requestProgressDialogClose(progress).ok()));
    static_cast<void>(context.expectTrue("cancelable close gesture sets sticky request", progress.hasCancelRequest()));
    static_cast<void>(context.expectTrue("cancelable close gesture leaves dialog open", progress.isOpen()));

    const Win32TestHooks::NativePixelRect suggested{.x = 40, .y = 60, .width = 520, .height = 260};
    static_cast<void>(context.expectTrue(
        "ProgressDialog DPI transition is simulated",
        Win32TestHooks::simulateProgressDialogDpiChange(progress, suggested, 144).ok()));
    const auto dpiChanged = Win32TestHooks::inspectProgressDialog(progress);
    static_cast<void>(context.expectEq("WM_DPICHANGED consumes suggested bounds", suggested, dpiChanged.windowBounds));
    static_cast<void>(context.expectTrue(
        "WM_DPICHANGED relayout keeps progress control nonempty",
        dpiChanged.progressBounds.width > 0 && dpiChanged.progressBounds.height > 0));

    // Verify close retry and lifetime reset.
    Desktop::Internal::TestHooks::failNext(Desktop::Internal::TestHooks::FailurePoint::Close);
    static_cast<void>(context.expectEq("ProgressDialog native close failure is reported", ErrorCode::CloseFailed, progress.close().code));
    static_cast<void>(context.expectTrue("failed native close retains retryable presentation", progress.isOpen()));
    static_cast<void>(context.expectEq("failed native close retains active registry", std::size_t{1}, Win32TestHooks::activeProgressDialogCount()));
    static_cast<void>(context.expectTrue("ProgressDialog native close retry succeeds", progress.close().ok()));
    static_cast<void>(context.expectFalse("closed ProgressDialog clears open state", progress.isOpen()));
    static_cast<void>(context.expectFalse("closed ProgressDialog clears owner identity", progress.ownedByCurrentThread()));
    static_cast<void>(context.expectFalse("successful close clears cancellation lifetime", progress.hasCancelRequest()));
    static_cast<void>(context.expectEq("closed ProgressDialog unregisters", std::size_t{0}, Win32TestHooks::activeProgressDialogCount()));
    static_cast<void>(context.expectEq("closed ProgressDialog releases class", std::size_t{0}, Win32TestHooks::progressDialogClassReferenceCount()));

    if(!context.expectTrue("ProgressDialog reopens after close", progress.open(description).ok()))
    {
        return;
    }

    static_cast<void>(context.expectFalse("reopened ProgressDialog starts without cancellation", progress.hasCancelRequest()));
    static_cast<void>(progress.close());

    // Check the noncancelable presentation contract.
    description.cancelable = false;
    if(!context.expectTrue("noncancelable ProgressDialog opens", progress.open(description).ok()))
    {
        return;
    }
    static_cast<void>(
        context.expectFalse("noncancelable ProgressDialog has no Cancel control", Win32TestHooks::inspectProgressDialog(progress).cancelControl));
    static_cast<void>(context.expectEq(
        "noncancelable ProgressDialog cannot receive Cancel click",
        ErrorCode::Unsupported,
        Win32TestHooks::requestProgressDialogCancel(progress).code));
    static_cast<void>(context.expectTrue("noncancelable close gesture is ignored", Win32TestHooks::requestProgressDialogClose(progress).ok()));
    static_cast<void>(context.expectTrue("noncancelable close gesture leaves presentation open", progress.isOpen()));
    static_cast<void>(context.expectFalse("noncancelable close gesture does not set request", progress.hasCancelRequest()));
    static_cast<void>(progress.close());
}

/// ------------------------------------------------------------
#endif
