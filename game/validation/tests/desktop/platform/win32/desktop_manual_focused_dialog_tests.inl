/// @file desktop_manual_focused_dialog_tests.inl
/// @brief Focused overwrite and live-DPI dialog checks.

/// ------------------------------------------------------------
/// Focused dialog checks
/// ------------------------------------------------------------

/// @brief Reproducible counterparts of the guided overwrite and live DPI checks.
void testManualFocusedDialogs(TestSupport::Context &context)
{
    const bool saveSelected = manualCaseSelected("save-overwrite-dpi");
    const bool progressSelected = manualCaseSelected("progress-dpi");

    if(!saveSelected && !progressSelected)
    {
        return;
    }

    Desktop::Window owner;

    if(!openManualWindow(context, owner, "GameWIP focused dialog validation"))
    {
        return;
    }

    if(saveSelected)
    {
        // Prepare an existing file for overwrite confirmation.
        const TestSupport::ScopedTemporaryDirectory fixtures("desktop-save-overwrite");
        const auto path = fixtures.path() / "overwrite-check.gamewip";
        constexpr std::string_view marker = "Existing file must remain unchanged by the path picker.\n";

        if(!context.expectTrue("overwrite fixture creates", fixtures.status().ok() && TestSupport::writeTextFile(path, marker).ok()))
        {
            return;
        }
        const std::array extensions{std::string_view{"gamewip"}};
        const std::array filters{FileDialogs::Filter{"GameWIP validation", extensions}};
        FileDialogs::SaveDescription description;
        description.owner = &owner;
        description.title = "GameWIP overwrite and monitor scaling validation";
        description.filters = filters;
        description.preferredFilterIndex = 0;
        description.suggestedDirectory = fixtures.path();
        description.suggestedFileName = "overwrite-check";
        description.suggestedExtension = "gamewip";
        constexpr std::string_view instruction =
            "Move the Save picker across differently scaled monitors if available. Then Save the existing overwrite-check.gamewip: choose No, verify "
            "the picker stays open, Save again and choose Yes. The shared Windows picker can pause during DPI relayout.";

        // Run the picker and collect the manual result.
        if(manualStatusWindow != nullptr)
        {
            manualStatusWindow->setScenario("save-overwrite-dpi", instruction);
            manualStatusWindow->refresh(owner);
        }
        context.manual(instruction);
        const auto result = Desktop::Dialogs::saveFile(description);
        const auto answer = recordManualCheck(
            context,
            owner,
            "save-overwrite-dpi",
            "Did both overwrite confirmation paths work, and did the picker stay sharp, correctly laid out and responsive after changing monitors? "
            "Skip if you could not exercise the requested paths.");

        if(answer == TestSupport::Types::Reporting::ManualAnswer::Yes)
        {
            // Confirm the accepted path and preserve the existing file.
            static_cast<void>(context.expectTrue(
                "overwrite picker accepts exact fixture and filter",
                result.ok() && result.value().outcome == DialogTypes::Outcome::Accepted && result.value().path == path &&
                    result.value().selectedFilterIndex == std::optional<std::size_t>{0}));
            const auto unchanged = TestSupport::readTextFile(path);
            static_cast<void>(
                context.expectTrue("overwrite path selection preserves existing file bytes", unchanged.ok() && unchanged.value() == marker));
        }
    }

    if(progressSelected)
    {
        // Open a non-blocking progress dialog for live DPI movement.
        Desktop::ProgressDialog progress;
        Desktop::Types::Dialogs::Progress::Description description;
        description.title = "GameWIP live progress scaling validation";
        description.heading = "Move between differently scaled monitors";
        description.message = "Progress updates while you drag. Check sharp text and clipping, then press Cancel.";
        description.presentationMode = Desktop::Types::Dialogs::Progress::Mode::Determinate;
        description.cancelable = true;
        description.blocksOwner = false;

        if(!requireManualStatus(context, "focused progress opens", progress.open(description)))
        {
            return;
        }
        const auto started = std::chrono::steady_clock::now();
        bool updateFailed = false;

        // Continue progress updates while the user moves and cancels the dialog.
        const auto answer = recordManualCheck(
            context,
            owner,
            "progress-dpi",
            "Move the progress dialog between differently scaled monitors while progress updates. Does it adjust promptly without clipping or "
            "freezing? Press Cancel, then confirm.",
            [&]
            {
                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();

                if(!updateFailed && !progress.hasCancelRequest())
                {
                    const auto updated = progress.setProgress(static_cast<double>(elapsed % 5000) / 5000.0);

                    if(!updated.ok())
                    {
                        updateFailed = true;
                        context.fail("live progress update", manualStatusDetails(updated.status()));
                    }
                }

                if(manualStatusWindow != nullptr)
                {
                    manualStatusWindow->setObservation(
                        progress.hasCancelRequest() ? "Cancel request received; waiting for your answer."
                                                    : "Live progress updates; move the dialog, then press Cancel.");
                }
            });

        if(answer == TestSupport::Types::Reporting::ManualAnswer::Yes)
        {
            static_cast<void>(context.expectTrue("focused progress cancellation is sticky", progress.hasCancelRequest()));
        }

        static_cast<void>(requireManualStatus(context, "focused progress closes", progress.close()));
    }

    static_cast<void>(owner.close());
}
