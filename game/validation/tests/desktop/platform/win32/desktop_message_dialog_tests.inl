/// @file desktop_message_dialog_tests.inl
/// @brief Message dialog validation tests.

#if DESKTOP_INTERNAL_TEST_HOOKS

/// ------------------------------------------------------------
/// Message dialogs
/// ------------------------------------------------------------

void testMessageDialogs(TestSupport::Context &context)
{
    Win32TestHooks::resetDialogState();

    using StatusCode = GameWIP::Status::Types::Code;
    using MessageDialogs::Button;
    using MessageDialogs::Buttons;

    struct ButtonSetCase
    {
        Buttons buttons;
        std::array<Button, 3> members;
        std::size_t memberCount;
        bool hasCancel;
    };
    constexpr std::array cases{
        ButtonSetCase{Buttons::Ok, {Button::Ok, Button::None, Button::None}, 1, false},
        ButtonSetCase{Buttons::OkCancel, {Button::Ok, Button::Cancel, Button::None}, 2, true},
        ButtonSetCase{Buttons::YesNo, {Button::Yes, Button::No, Button::None}, 2, false},
        ButtonSetCase{Buttons::YesNoCancel, {Button::Yes, Button::No, Button::Cancel}, 3, true},
        ButtonSetCase{Buttons::RetryCancel, {Button::Retry, Button::Cancel, Button::None}, 2, true}};

    // Validate defaults, selections, dismissal, and semantic cancel behavior.
    for(const ButtonSetCase &testCase : cases)
    {
        MessageDialogs::Description description{.title = "Message", .message = "Body", .buttons = testCase.buttons};
        Win32TestHooks::completeNextMessageDialog({.button = testCase.members[0], .dismissed = false});
        const auto nativeDefault = Desktop::Dialogs::showMessage(description);
        if(!context.expectTrue("Message native default succeeds", nativeDefault.ok()))
        {
            continue;
        }
        static_cast<void>(context.expectEq("Message selection is accepted", DialogTypes::Outcome::Accepted, nativeDefault.value().outcome));
        static_cast<void>(context.expectEq("Message selection round trips", testCase.members[0], nativeDefault.value().button));
        static_cast<void>(
            context.expectEq("Message default None reaches backend", Button::None, Win32TestHooks::lastMessageDialogSnapshot().defaultButton));

        for(std::size_t index = 0; index < testCase.memberCount; ++index)
        {
            description.defaultButton = testCase.members[index];
            Win32TestHooks::completeNextMessageDialog({.button = testCase.members[index], .dismissed = false});
            const auto selected = Desktop::Dialogs::showMessage(description);
            const bool cancel = testCase.members[index] == Button::Cancel;
            if(!context.expectTrue("valid explicit Message default succeeds", selected.ok()))
            {
                continue;
            }
            static_cast<void>(context.expectEq(
                "Message button maps to exact outcome",
                cancel ? DialogTypes::Outcome::Cancelled : DialogTypes::Outcome::Accepted,
                selected.value().outcome));
            static_cast<void>(context.expectEq("Message button maps exactly", testCase.members[index], selected.value().button));
        }

        description.defaultButton = Button::None;
        Win32TestHooks::completeNextMessageDialog({.dismissed = true});
        const auto dismissed = Desktop::Dialogs::showMessage(description);
        if(!context.expectTrue("Message native dismissal succeeds", dismissed.ok()))
        {
            continue;
        }
        static_cast<void>(context.expectEq("Message native dismissal is cancelled", DialogTypes::Outcome::Cancelled, dismissed.value().outcome));
        static_cast<void>(context.expectEq(
            "Message dismissal reports semantic cancel only when available",
            testCase.hasCancel ? Button::Cancel : Button::None,
            dismissed.value().button));
    }

    // Reject invalid enums and default-button combinations.
    MessageDialogs::Description description;
    description.buttons = Buttons::Ok;
    description.defaultButton = Button::Yes;
    static_cast<void>(context.expectEq(
        "Message default must belong to set",
        StatusCode::InvalidArgument,
        Desktop::Dialogs::showMessage(description).status().code()));
    description.defaultButton = static_cast<Button>(255);
    static_cast<void>(context.expectEq(
        "invalid Message default enum is rejected",
        StatusCode::InvalidArgument,
        Desktop::Dialogs::showMessage(description).status().code()));
    description.defaultButton = Button::None;
    description.buttons = static_cast<Buttons>(255);
    static_cast<void>(context.expectEq(
        "invalid Message Buttons enum is rejected",
        StatusCode::InvalidArgument,
        Desktop::Dialogs::showMessage(description).status().code()));
    description.buttons = Buttons::Ok;
    description.severity = static_cast<DialogTypes::Severity>(255);
    static_cast<void>(context.expectEq(
        "invalid Message Severity enum is rejected",
        StatusCode::InvalidArgument,
        Desktop::Dialogs::showMessage(description).status().code()));

    // Forward every portable severity to the native boundary.
    constexpr std::array severities{
        DialogTypes::Severity::None,
        DialogTypes::Severity::Information,
        DialogTypes::Severity::Warning,
        DialogTypes::Severity::Error};

    for(const DialogTypes::Severity severity : severities)
    {
        description.severity = severity;
        Win32TestHooks::completeNextMessageDialog({.button = Button::Ok, .dismissed = false});
        static_cast<void>(context.expectTrue("every Message Severity succeeds", Desktop::Dialogs::showMessage(description).ok()));
        static_cast<void>(context.expectEq("Message Severity reaches backend", severity, Win32TestHooks::lastMessageDialogSnapshot().severity));
    }

    // Reject invalid text and report failures without selections.
    description.severity = DialogTypes::Severity::None;
    description.title = std::string_view{"bad\0title", 9};
    static_cast<void>(context.expectEq(
        "Message title rejects embedded null",
        StatusCode::InvalidArgument,
        Desktop::Dialogs::showMessage(description).status().code()));
    description.title = std::string_view{"\xC3", 1};
    Win32TestHooks::completeNextMessageDialog({.button = Button::Ok, .dismissed = false});
    static_cast<void>(context.expectEq(
        "Message title rejects malformed UTF-8",
        StatusCode::EncodingFailed,
        Desktop::Dialogs::showMessage(description).status().code()));
    description.title = {};
    description.message = std::string_view{"bad\0message", 11};
    static_cast<void>(context.expectEq(
        "Message text rejects embedded null",
        StatusCode::InvalidArgument,
        Desktop::Dialogs::showMessage(description).status().code()));
    description.message = std::string_view{"\xC3", 1};
    Win32TestHooks::completeNextMessageDialog({.button = Button::Ok, .dismissed = false});
    static_cast<void>(context.expectEq(
        "Message malformed UTF-8 maps at conversion",
        StatusCode::EncodingFailed,
        Desktop::Dialogs::showMessage(description).status().code()));

    description.message = "failure";
    Win32TestHooks::completeNextMessageDialog({.button = Button::Ok, .dismissed = false});
    Desktop::Internal::TestHooks::failNext(Desktop::Internal::TestHooks::FailurePoint::Allocation);
    auto failed = Desktop::Dialogs::showMessage(description);
    static_cast<void>(context.expectEq("Message allocation failure maps", StatusCode::OutOfMemory, failed.status().code()));
    static_cast<void>(context.expectFalse("failed Message has no selection", failed.hasValue()));
    Win32TestHooks::completeNextMessageDialog({.button = Button::Ok, .dismissed = false});
    Desktop::Internal::TestHooks::failNext(Desktop::Internal::TestHooks::FailurePoint::DialogNativeOperation);
    failed = Desktop::Dialogs::showMessage(description);
    static_cast<void>(context.expectEq("Message native failure maps", StatusCode::NativeFailure, failed.status().code()));
    static_cast<void>(context.expectFalse("native-failed Message has no selection", failed.hasValue()));
    Win32TestHooks::completeNextMessageDialog({.button = Button::Ok, .dismissed = false});
    Desktop::Internal::TestHooks::failNext(Desktop::Internal::TestHooks::FailurePoint::DialogResult);
    failed = Desktop::Dialogs::showMessage(description);
    static_cast<void>(context.expectEq("Message unknown native result maps", StatusCode::NativeFailure, failed.status().code()));
    static_cast<void>(context.expectFalse("invalid native Message result has no selection", failed.hasValue()));
}

#endif
