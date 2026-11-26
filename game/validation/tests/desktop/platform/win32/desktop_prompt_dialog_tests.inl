/// @file desktop_prompt_dialog_tests.inl
/// @brief Prompt dialog validation tests.

#if DESKTOP_INTERNAL_TEST_HOOKS

/// ------------------------------------------------------------
/// Prompt dialogs
/// ------------------------------------------------------------

void testPromptDialogs(TestSupport::Context &context)
{
    Win32TestHooks::resetDialogState();

    using StatusCode = GameWIP::Status::Types::Code;
    using PromptDialogs::Button;
    using PromptDialogs::ButtonId;
    using PromptDialogs::Option;
    using PromptDialogs::OptionId;

    // Validate required buttons and portable button identifiers.
    PromptDialogs::Description description;
    static_cast<void>(
        context.expectEq("Prompt requires a button", StatusCode::InvalidArgument, Desktop::Dialogs::showPrompt(description).status().code()));

    std::array<Button, 3> buttons{
        Button{ButtonId{1}, "Proceed", "Use recommended settings"},
        Button{ButtonId{2}, "Cancel", {}},
        Button{ButtonId{std::numeric_limits<std::uint32_t>::max()}, "Advanced", "Choose manually"}};
    description.buttons = buttons;

    buttons[0].id = {};
    static_cast<void>(
        context.expectEq("Prompt rejects zero button ID", StatusCode::InvalidArgument, Desktop::Dialogs::showPrompt(description).status().code()));
    buttons[0].id = ButtonId{1};
    buttons[1].id = ButtonId{1};
    static_cast<void>(context.expectEq(
        "Prompt rejects duplicate button ID",
        StatusCode::InvalidArgument,
        Desktop::Dialogs::showPrompt(description).status().code()));
    buttons[1].id = ButtonId{2};
    buttons[0].label = {};
    static_cast<void>(context.expectEq(
        "Prompt rejects empty button label",
        StatusCode::InvalidArgument,
        Desktop::Dialogs::showPrompt(description).status().code()));
    buttons[0].label = std::string_view{"bad\0label", 9};
    static_cast<void>(
        context.expectEq("Prompt rejects button-label null", StatusCode::InvalidArgument, Desktop::Dialogs::showPrompt(description).status().code()));
    buttons[0].label = std::string_view{"\xC3", 1};
    Win32TestHooks::completeNextPromptDialog({});
    static_cast<void>(context.expectEq(
        "Prompt rejects malformed button label",
        StatusCode::EncodingFailed,
        Desktop::Dialogs::showPrompt(description).status().code()));
    buttons[0].label = "Proceed";
    buttons[0].description = std::string_view{"bad\0description", 15};
    static_cast<void>(context.expectEq(
        "Prompt rejects button-description null",
        StatusCode::InvalidArgument,
        Desktop::Dialogs::showPrompt(description).status().code()));
    buttons[0].description = std::string_view{"\xF0\x28\x8C\x28", 4};
    Win32TestHooks::completeNextPromptDialog({});
    static_cast<void>(context.expectEq(
        "Prompt rejects malformed button description",
        StatusCode::EncodingFailed,
        Desktop::Dialogs::showPrompt(description).status().code()));
    buttons[0].description = "Use recommended settings";

    description.defaultButton = ButtonId{99};
    static_cast<void>(context.expectEq(
        "Prompt default button must reference a button",
        StatusCode::InvalidArgument,
        Desktop::Dialogs::showPrompt(description).status().code()));
    description.defaultButton = {};
    description.cancelButton = ButtonId{99};
    static_cast<void>(context.expectEq(
        "Prompt cancel button must reference a button",
        StatusCode::InvalidArgument,
        Desktop::Dialogs::showPrompt(description).status().code()));
    description.cancelButton = {};

    // Validate option identifiers, labels, and default selection.
    std::array<Option, 2> options{Option{OptionId{1}, "Fast"}, Option{OptionId{std::numeric_limits<std::uint32_t>::max()}, "Thorough"}};
    description.options = options;
    options[0].id = {};
    static_cast<void>(
        context.expectEq("Prompt rejects zero option ID", StatusCode::InvalidArgument, Desktop::Dialogs::showPrompt(description).status().code()));
    options[0].id = OptionId{1};
    options[1].id = OptionId{1};
    static_cast<void>(context.expectEq(
        "Prompt rejects duplicate option ID",
        StatusCode::InvalidArgument,
        Desktop::Dialogs::showPrompt(description).status().code()));
    options[1].id = OptionId{std::numeric_limits<std::uint32_t>::max()};
    options[0].label = {};
    static_cast<void>(context.expectEq(
        "Prompt rejects empty option label",
        StatusCode::InvalidArgument,
        Desktop::Dialogs::showPrompt(description).status().code()));
    options[0].label = std::string_view{"bad\0option", 10};
    static_cast<void>(
        context.expectEq("Prompt rejects option-label null", StatusCode::InvalidArgument, Desktop::Dialogs::showPrompt(description).status().code()));
    options[0].label = std::string_view{"\xC3", 1};
    Win32TestHooks::completeNextPromptDialog({});
    static_cast<void>(context.expectEq(
        "Prompt rejects malformed option label",
        StatusCode::EncodingFailed,
        Desktop::Dialogs::showPrompt(description).status().code()));
    options[0].label = "Fast";
    description.defaultOption = OptionId{99};
    static_cast<void>(context.expectEq(
        "Prompt default option must reference an option",
        StatusCode::InvalidArgument,
        Desktop::Dialogs::showPrompt(description).status().code()));
    description.defaultOption = {};
    description.options = {};
    description.defaultOption = OptionId{1};
    static_cast<void>(context.expectEq(
        "Prompt default option is invalid without options",
        StatusCode::InvalidArgument,
        Desktop::Dialogs::showPrompt(description).status().code()));
    description.options = options;
    description.defaultOption = {};

    // Validate optional checkbox and text fields.
    description.checkBox = PromptDialogs::CheckBox{};
    static_cast<void>(
        context.expectEq("Prompt checkbox requires a label", StatusCode::InvalidArgument, Desktop::Dialogs::showPrompt(description).status().code()));
    description.checkBox = PromptDialogs::CheckBox{std::string_view{"bad\0check", 9}, false};
    static_cast<void>(
        context.expectEq("Prompt checkbox rejects null", StatusCode::InvalidArgument, Desktop::Dialogs::showPrompt(description).status().code()));
    description.checkBox = PromptDialogs::CheckBox{std::string_view{"\xC3", 1}, false};
    Win32TestHooks::completeNextPromptDialog({});
    static_cast<void>(context.expectEq(
        "Prompt checkbox rejects malformed UTF-8",
        StatusCode::EncodingFailed,
        Desktop::Dialogs::showPrompt(description).status().code()));
    description.checkBox.reset();

    const auto rejectText = [&](std::string_view name, std::string_view PromptDialogs::Description::*member)
    {
        description.*member = std::string_view{"bad\0text", 8};
        static_cast<void>(context.expectEq(
            std::format("Prompt {} rejects null", name),
            StatusCode::InvalidArgument,
            Desktop::Dialogs::showPrompt(description).status().code()));
        description.*member = std::string_view{"\xC3", 1};
        Win32TestHooks::completeNextPromptDialog({});
        static_cast<void>(context.expectEq(
            std::format("Prompt {} rejects malformed UTF-8", name),
            StatusCode::EncodingFailed,
            Desktop::Dialogs::showPrompt(description).status().code()));
        description.*member = {};
    };

    rejectText("title", &PromptDialogs::Description::title);
    rejectText("heading", &PromptDialogs::Description::heading);
    rejectText("message", &PromptDialogs::Description::message);
    rejectText("details", &PromptDialogs::Description::details);
    rejectText("supplemental text", &PromptDialogs::Description::supplementalText);

    // Verify portable selections map to native identifiers and return intact.
    description.title = "Operation";
    description.heading = "Choose a path";
    description.message = "Portable IDs include native collisions";
    description.severity = DialogTypes::Severity::Warning;
    description.defaultButton = buttons[2].id;
    description.cancelButton = buttons[1].id;
    description.defaultOption = options[0].id;
    description.details = "Detailed explanation";
    description.supplementalText = "Supplemental note";
    description.checkBox = PromptDialogs::CheckBox{"Remember this", true};
    Win32TestHooks::completeNextPromptDialog({.buttonIndex = 0, .optionIndex = 1, .checkBoxChecked = false, .dismissed = false});
    const auto accepted = Desktop::Dialogs::showPrompt(description);
    if(!context.expectTrue("rich Prompt succeeds", accepted.ok()))
    {
        return;
    }
    static_cast<void>(context.expectEq("rich Prompt accepts selected action", DialogTypes::Outcome::Accepted, accepted.value().outcome));
    static_cast<void>(context.expectEq("portable button ID round trips", buttons[0].id, accepted.value().button));
    static_cast<void>(context.expectEq("portable option ID round trips", options[1].id, accepted.value().option));
    static_cast<void>(context.expectEq("Prompt final checkbox state returns", std::optional<bool>{false}, accepted.value().checkBoxChecked));

    const auto &snapshot = Win32TestHooks::lastPromptDialogSnapshot();
    static_cast<void>(context.expectEq(
        "Prompt descriptions compose command-link text",
        std::wstring{L"Proceed\nUse recommended settings"},
        snapshot.buttonText[0]));
    static_cast<void>(context.expectEq("undescribed Prompt button remains label only", std::wstring{L"Cancel"}, snapshot.buttonText[1]));
    static_cast<void>(context.expectTrue("any description enables command-link behavior", snapshot.commandLinks));
    static_cast<void>(context.expectEq("Prompt button native IDs are backend owned", std::vector<int>{1000, 1001, 1002}, snapshot.nativeButtonIds));
    static_cast<void>(context.expectEq("Prompt option native IDs follow backend range", std::vector<int>{1003, 1004}, snapshot.nativeOptionIds));
    static_cast<void>(context.expectEq("large portable default maps by position", 1002, snapshot.nativeDefaultButton));
    static_cast<void>(context.expectEq("portable default option maps by position", 1003, snapshot.nativeDefaultOption));
    static_cast<void>(context.expectEq("Prompt details propagate", std::wstring{L"Detailed explanation"}, snapshot.details));
    static_cast<void>(context.expectEq("Prompt supplemental text propagates", std::wstring{L"Supplemental note"}, snapshot.supplementalText));
    static_cast<void>(context.expectEq("Prompt checkbox label propagates", std::wstring{L"Remember this"}, snapshot.checkBoxLabel));
    static_cast<void>(context.expectTrue("Prompt checkbox initial state propagates", snapshot.checkBoxInitiallyChecked));

    Win32TestHooks::completeNextPromptDialog({.buttonIndex = 1, .optionIndex = 0, .checkBoxChecked = true, .dismissed = false});
    const auto explicitCancel = Desktop::Dialogs::showPrompt(description);
    if(!context.expectTrue("configured Prompt cancellation succeeds", explicitCancel.ok()))
    {
        return;
    }
    static_cast<void>(context.expectEq("configured cancel action is cancelled", DialogTypes::Outcome::Cancelled, explicitCancel.value().outcome));
    static_cast<void>(context.expectEq("configured cancel ID round trips", buttons[1].id, explicitCancel.value().button));
    static_cast<void>(context.expectEq("cancelled Prompt retains final option", options[0].id, explicitCancel.value().option));
    static_cast<void>(context.expectEq("cancelled Prompt retains final checkbox", std::optional<bool>{true}, explicitCancel.value().checkBoxChecked));
    Win32TestHooks::completeNextPromptDialog({.optionIndex = 1, .checkBoxChecked = false, .dismissed = true});
    const auto dismissed = Desktop::Dialogs::showPrompt(description);
    if(!context.expectTrue("Prompt dismissal succeeds", dismissed.ok()))
    {
        return;
    }
    static_cast<void>(context.expectEq("Prompt dismissal is cancelled", DialogTypes::Outcome::Cancelled, dismissed.value().outcome));
    static_cast<void>(context.expectEq("Prompt dismissal uses semantic cancel ID", buttons[1].id, dismissed.value().button));

    description.cancelButton = {};
    description.defaultButton = {};
    description.options = {};
    description.defaultOption = {};
    description.checkBox.reset();
    Win32TestHooks::completeNextPromptDialog({.dismissed = true});
    const auto noSemanticCancel = Desktop::Dialogs::showPrompt(description);
    if(!context.expectTrue("Prompt dismissal without a semantic cancel succeeds", noSemanticCancel.ok()))
    {
        return;
    }
    static_cast<void>(context.expectFalse("Prompt dismissal without cancel has no ID", noSemanticCancel.value().button.has_value()));
    static_cast<void>(context.expectFalse("Prompt with no option selection has no option", noSemanticCancel.value().option.has_value()));
    static_cast<void>(context.expectFalse("Prompt without checkbox returns nullopt", noSemanticCancel.value().checkBoxChecked.has_value()));

    // Malformed native selections and backend failures return no selection.
    Win32TestHooks::completeNextPromptDialog({.buttonIndex = 0, .optionIndex = description.options.size() + 1, .dismissed = false});
    const auto malformedOption = Desktop::Dialogs::showPrompt(description);
    static_cast<void>(context.expectEq("malformed Prompt option reports native failure", StatusCode::NativeFailure, malformedOption.status().code()));
    static_cast<void>(context.expectFalse("malformed Prompt option has no selection", malformedOption.hasValue()));

    Win32TestHooks::completeNextPromptDialog({.buttonIndex = 0, .optionIndex = (std::numeric_limits<std::size_t>::max)(), .dismissed = false});
    const auto maximumMalformedOption = Desktop::Dialogs::showPrompt(description);
    static_cast<void>(context.expectEq(
        "maximum malformed Prompt option reports native failure",
        StatusCode::NativeFailure,
        maximumMalformedOption.status().code()));
    static_cast<void>(context.expectFalse("maximum malformed Prompt option has no selection", maximumMalformedOption.hasValue()));

    description.options = options;
    description.defaultOption = {};
    Win32TestHooks::completeNextPromptDialog({.buttonIndex = 2, .dismissed = false});
    const auto largeId = Desktop::Dialogs::showPrompt(description);
    if(!context.expectTrue("large uint32 Prompt ID selection succeeds", largeId.ok()))
    {
        return;
    }
    static_cast<void>(context.expectEq("large uint32 Prompt ID is accepted", buttons[2].id, largeId.value().button));

    Win32TestHooks::completeNextPromptDialog({.buttonIndex = 0, .dismissed = false});
    Desktop::Internal::TestHooks::failNext(Desktop::Internal::TestHooks::FailurePoint::Allocation);
    auto failed = Desktop::Dialogs::showPrompt(description);
    static_cast<void>(context.expectEq("Prompt allocation failure maps", StatusCode::OutOfMemory, failed.status().code()));
    static_cast<void>(context.expectFalse("failed Prompt has no selection", failed.hasValue()));

    Win32TestHooks::completeNextPromptDialog({.buttonIndex = 0, .dismissed = false});
    Desktop::Internal::TestHooks::failNext(Desktop::Internal::TestHooks::FailurePoint::DialogNativeOperation);
    failed = Desktop::Dialogs::showPrompt(description);
    static_cast<void>(context.expectEq("Prompt native failure maps", StatusCode::NativeFailure, failed.status().code()));
    static_cast<void>(context.expectFalse("native-failed Prompt has no selection", failed.hasValue()));

    Win32TestHooks::completeNextPromptDialog({.buttonIndex = 0, .dismissed = false});
    Desktop::Internal::TestHooks::failNext(Desktop::Internal::TestHooks::FailurePoint::DialogResult);
    failed = Desktop::Dialogs::showPrompt(description);
    static_cast<void>(context.expectEq("Prompt invalid native result maps", StatusCode::NativeFailure, failed.status().code()));
    static_cast<void>(context.expectFalse("invalid native Prompt result has no selection", failed.hasValue()));
}

#endif
