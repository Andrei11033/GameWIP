/// @file desktop_manual_dialog_tests.inl
/// @brief Guided visible validation for native dialogs and ProgressDialog.

namespace ManualProgressTypes = Desktop::Types::Dialogs::Progress;

void testManualDialogs(TestSupport::Context &context, const GameWIP::Test::DesktopTestOptions &options)
{
    if (!beginManualSuite(context, options, "Desktop native dialogs"))
    {
        return;
    }

    Desktop::Window owner;
    if (!openManualWindow(context, owner, "GameWIP dialog validation owner"))
    {
        return;
    }

    const std::array textExtensions{std::string_view{"txt"}, std::string_view{"md"}};
    const std::array projectExtensions{std::string_view{"gamewip"}};
    const std::array<FileDialogs::Filter, 2> filters{
        FileDialogs::Filter{"Text", textExtensions},
        FileDialogs::Filter{"GameWIP project", projectExtensions}};
    FileDialogs::OpenDescription open{
        .owner = &owner,
        .title = "Choose one file for GameWIP validation",
        .filters = filters,
        .preferredFilterIndex = 0};

    context.info("The next dialog must open one existing file; select one file to continue.");
    const auto oneFile = Desktop::Dialogs::openFile(open);
    static_cast<void>(context.expectTrue("manual open-one status succeeds", oneFile.status.ok()));
    static_cast<void>(context.expectEq("manual open-one accepts one file", DialogTypes::Outcome::Accepted, oneFile.outcome));
    recordManualCheck(context, owner, "open one file", "Did the native picker show the title, ordered filters, and selected file normally?");

    context.info("The next dialog must open multiple existing files; select at least two files to continue.");
    const auto manyFiles = Desktop::Dialogs::openFiles(open);
    static_cast<void>(context.expectTrue("manual open-many status succeeds", manyFiles.status.ok()));
    static_cast<void>(context.expectTrue(
        "manual open-many accepts multiple files",
        manyFiles.outcome == DialogTypes::Outcome::Accepted && manyFiles.paths.size() >= 2));
    recordManualCheck(context, owner, "open multiple files", "Did multi-selection preserve the selected native file order and filter behavior?");

    FileDialogs::SaveDescription save{
        .owner = &owner,
        .title = "Choose a save path for GameWIP validation",
        .filters = filters,
        .preferredFilterIndex = 1,
        .suggestedFileName = "dialog-validation",
        .suggestedExtension = "gamewip"};
    context.info("The next dialog must choose a save path. The file need not be created by GameWIP.");
    const auto saved = Desktop::Dialogs::saveFile(save);
    static_cast<void>(context.expectTrue("manual save status succeeds", saved.status.ok()));
    static_cast<void>(context.expectEq("manual save accepts a path", DialogTypes::Outcome::Accepted, saved.outcome));
    recordManualCheck(
        context,
        owner,
        "save one file",
        "Were the suggested name, extension, selected filter, overwrite behavior, and returned path native and coherent?");

    FileDialogs::FolderDescription folder{.owner = &owner, .title = "Choose one folder for GameWIP validation"};
    context.info("The next dialog must select one existing folder.");
    const auto oneFolder = Desktop::Dialogs::selectFolder(folder);
    static_cast<void>(context.expectTrue("manual select-folder status succeeds", oneFolder.status.ok()));
    static_cast<void>(context.expectEq("manual select-folder accepts", DialogTypes::Outcome::Accepted, oneFolder.outcome));
    recordManualCheck(
        context,
        owner,
        "select one folder",
        "Did the native folder picker accept exactly one existing folder without showing a fabricated file filter?");

    folder.title = "Choose multiple folders for GameWIP validation";
    context.info("The next dialog must select at least two existing folders.");
    const auto manyFolders = Desktop::Dialogs::selectFolders(folder);
    static_cast<void>(context.expectTrue("manual select-folders status succeeds", manyFolders.status.ok()));
    static_cast<void>(context.expectTrue(
        "manual select-folders accepts multiple folders",
        manyFolders.outcome == DialogTypes::Outcome::Accepted && manyFolders.paths.size() >= 2));
    recordManualCheck(context, owner, "select multiple folders", "Did native folder multi-selection work and preserve the selected order?");

    struct MessageCase
    {
        MessageDialogs::Buttons buttons;
        DialogTypes::Severity severity;
        std::string_view label;
    };
    constexpr std::array messageCases{
        MessageCase{MessageDialogs::Buttons::Ok, DialogTypes::Severity::None, "OK / none"},
        MessageCase{MessageDialogs::Buttons::OkCancel, DialogTypes::Severity::Information, "OK-Cancel / information"},
        MessageCase{MessageDialogs::Buttons::YesNo, DialogTypes::Severity::Warning, "Yes-No / warning"},
        MessageCase{MessageDialogs::Buttons::YesNoCancel, DialogTypes::Severity::Error, "Yes-No-Cancel / error"},
        MessageCase{MessageDialogs::Buttons::RetryCancel, DialogTypes::Severity::Information, "Retry-Cancel / information"}};
    for (const MessageCase &testCase : messageCases)
    {
        MessageDialogs::Description message{
            .owner = &owner,
            .title = "GameWIP Message validation",
            .message = testCase.label,
            .buttons = testCase.buttons,
            .severity = testCase.severity};
        context.info(std::format("The next Message checks {}. Choose any non-cancel button.", testCase.label));
        const auto result = Desktop::Dialogs::showMessage(message);
        static_cast<void>(context.expectTrue("manual Message status succeeds", result.status.ok()));
        recordManualCheck(
            context,
            owner,
            std::format("Message {}", testCase.label),
            "Did the native Task Dialog show the expected button set, severity treatment, owner modality, and normal keyboard/close behavior?");
    }

    const std::array<PromptDialogs::Button, 3> promptButtons{
        PromptDialogs::Button{{1}, "Install", "Install the selected package now"},
        PromptDialogs::Button{{2}, "Later", "Keep the package for a later session"},
        PromptDialogs::Button{{3}, "Cancel", {}}};
    const std::array<PromptDialogs::Option, 3> promptOptions{
        PromptDialogs::Option{{1}, "Current user"},
        PromptDialogs::Option{{2}, "All users"},
        PromptDialogs::Option{{3}, "Portable"}};
    PromptDialogs::Description prompt{
        .owner = &owner,
        .title = "GameWIP Prompt validation",
        .heading = "Choose installation behavior",
        .message = "Validate custom semantic choices and portable secondary content.",
        .severity = DialogTypes::Severity::Information,
        .buttons = promptButtons,
        .defaultButton = {1},
        .cancelButton = {3},
        .options = promptOptions,
        .defaultOption = {1},
        .details = "Expanded details must remain readable and must not replace the primary message.",
        .supplementalText = "Supplemental text is lower-priority information.",
        .checkBox = PromptDialogs::CheckBox{"Remember this choice", true}};
    context.info("The next Prompt should show command-link descriptions, radio options, details, supplemental text, and a checked checkbox.");
    const auto prompted = Desktop::Dialogs::showPrompt(prompt);
    static_cast<void>(context.expectTrue("manual Prompt status succeeds", prompted.status.ok()));
    recordManualCheck(
        context,
        owner,
        "rich semantic Prompt",
        "Were all custom buttons, secondary descriptions, mutually-exclusive options, details, supplemental text, checkbox, severity, and owner "
        "relationship visible and usable?");

    ManualProgressTypes::Description progressDescription{
        .owner = &owner,
        .title = "GameWIP progress validation",
        .heading = "Determinate operation",
        .message = "Watch title, heading, message, mode, and value update.",
        .mode = ManualProgressTypes::Mode::Determinate,
        .progress = 0.1,
        .cancelable = true,
        .blocksOwner = true};
    Desktop::ProgressDialog progress;
    static_cast<void>(context.expectTrue("manual determinate ProgressDialog opens", progress.open(progressDescription).ok()));
    std::size_t updateStep = 0;
    recordManualCheck(
        context,
        owner,
        "determinate ProgressDialog live updates",
        "While this question waits, does the progress window remain responsive, block owner interaction, and show changing title, heading, message, "
        "and determinate value?",
        [&]
        {
            ++updateStep;
            const double value = static_cast<double>(updateStep % 101) / 100.0;
            static_cast<void>(progress.setProgress(value));
            if (updateStep == 10)
            {
                static_cast<void>(progress.setTitle("GameWIP progress title updated"));
                static_cast<void>(progress.setHeading("Heading updated live"));
                static_cast<void>(progress.setMessage("Message updated live"));
            }
        });
    recordManualCheck(
        context,
        owner,
        "ProgressDialog cancellation request",
        "Click Cancel or the progress window close button. Does the dialog stay open while the application reports a sticky cancellation request?",
        [&]
        {
            if (manualStatusWindow != nullptr)
            {
                manualStatusWindow->setObservation(std::format("cancelRequested={} open={}", progress.hasCancelRequest(), progress.isOpen()));
            }
        });
    static_cast<void>(context.expectTrue("manual cancel gesture sets sticky request", progress.hasCancelRequest()));
    progress.clearCancelRequest();
    static_cast<void>(
        context.expectTrue("manual ProgressDialog switches to indeterminate", progress.setMode(ManualProgressTypes::Mode::Indeterminate).ok()));
    recordManualCheck(
        context,
        owner,
        "indeterminate ProgressDialog",
        "Does the same progress window switch live to a native indeterminate/marquee presentation?");
    static_cast<void>(progress.close());

    progressDescription.cancelable = false;
    progressDescription.blocksOwner = false;
    progressDescription.heading = "Noncancelable, nonblocking";
    static_cast<void>(context.expectTrue("manual noncancelable ProgressDialog opens", progress.open(progressDescription).ok()));
    recordManualCheck(
        context,
        owner,
        "noncancelable close behavior and blocksOwner false",
        "Can the owner still be used, is there no Cancel control, and does clicking the progress window close button leave it open without "
        "requesting cancellation?");
    static_cast<void>(context.expectTrue("manual noncancelable close leaves dialog open", progress.isOpen()));
    static_cast<void>(context.expectFalse("manual noncancelable close leaves cancellation clear", progress.hasCancelRequest()));
    static_cast<void>(progress.close());

    progressDescription.cancelable = true;
    progressDescription.blocksOwner = true;
    Desktop::ProgressDialog blockerOne;
    Desktop::ProgressDialog blockerTwo;
    static_cast<void>(context.expectTrue(
        "manual overlapping ProgressDialogs open",
        blockerOne.open(progressDescription).ok() && blockerTwo.open(progressDescription).ok()));
    static_cast<void>(blockerOne.close());
    recordManualCheck(
        context,
        owner,
        "overlapping ProgressDialog blockers",
        "With the first blocker closed and the second still visible, does the owner remain blocked until the final blocker closes?");
    static_cast<void>(blockerTwo.close());

    progressDescription.owner = nullptr;
    progressDescription.blocksOwner = true;
    Desktop::ProgressDialog ownerless;
    static_cast<void>(context.expectTrue("manual ownerless ProgressDialog opens", ownerless.open(progressDescription).ok()));
    recordManualCheck(
        context,
        owner,
        "ownerless ProgressDialog and DPI",
        "Is the ownerless progress window independent and responsive? If practical, move it between differently scaled displays and verify crisp "
        "relayout without clipping.");
    static_cast<void>(ownerless.close());
    static_cast<void>(owner.close());
}
