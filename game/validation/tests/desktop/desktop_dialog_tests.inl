/// @file desktop_dialog_tests.inl
/// @brief Deterministic native file, folder, Message, and Prompt validation.

namespace DialogTypes = Desktop::Types::Dialogs;
namespace FileDialogs = DialogTypes::File;
namespace MessageDialogs = DialogTypes::Message;
namespace PromptDialogs = DialogTypes::Prompt;

#if DESKTOP_INTERNAL_TEST_HOOKS
void armFileDialog(
    Desktop::TestHooks::FileDialogOperation operation,
    bool accepted = false,
    std::vector<GameWIP::FileSystem::Types::Path> paths = {},
    std::optional<std::size_t> nativeFilterIndex = std::nullopt)
{
    Desktop::TestHooks::completeNextFileDialog(operation, {.accepted = accepted, .paths = std::move(paths), .nativeFilterIndex = nativeFilterIndex});
}

void testFileAndFolderDialogs(TestSupport::Context &context)
{
    using FileOperation = Desktop::TestHooks::FileDialogOperation;

    const auto invalidExtension = [&](std::string_view extension)
    {
        const std::array extensions{extension};
        const std::array filters{FileDialogs::Filter{"Files", extensions}};
        FileDialogs::OpenDescription description{.filters = filters};
        static_cast<void>(context.expectEq(
            std::format("extension '{}' is invalid", extension),
            ErrorCode::InvalidArgument,
            Desktop::Dialogs::openFile(description).status.code));
    };
    invalidExtension("");
    invalidExtension(".png");
    invalidExtension("*");
    invalidExtension("?");
    invalidExtension("a/b");
    invalidExtension("a\\b");
    invalidExtension(std::string_view{"a\0b", 3});

    const std::array pngExtensions{std::string_view{"png"}};
    const std::array projectExtensions{std::string_view{"gamewip"}, std::string_view{"tar.gz"}};
    const std::array<FileDialogs::Filter, 3> filters{
        FileDialogs::Filter{"Images", pngExtensions},
        FileDialogs::Filter{"Projects", projectExtensions},
        FileDialogs::Filter{"All files", {}}};
    FileDialogs::OpenDescription open{
        .title = "Open UTF-8 \xCE\xA9",
        .filters = filters,
        .preferredFilterIndex = 1,
        .suggestedDirectory = GameWIP::FileSystem::Types::Path{L"C:\\GameWIP Suggested"}};
    armFileDialog(FileOperation::OpenFile);
    const auto cancelled = Desktop::Dialogs::openFile(open);
    static_cast<void>(context.expectTrue("file cancellation is successful", cancelled.status.ok()));
    static_cast<void>(context.expectEq("file cancellation is a domain outcome", DialogTypes::Outcome::Cancelled, cancelled.outcome));
    static_cast<void>(context.expectTrue("cancelled file path is empty", cancelled.path.empty()));
    static_cast<void>(context.expectFalse("cancelled file has no selected filter", cancelled.selectedFilterIndex.has_value()));
    const auto &snapshot = Desktop::TestHooks::lastFileDialogSnapshot();
    static_cast<void>(
        context.expectEq("file filter names preserve order", std::vector<std::wstring>{L"Images", L"Projects", L"All files"}, snapshot.filterNames));
    static_cast<void>(context.expectEq(
        "portable extensions become ordered native patterns",
        std::vector<std::wstring>{L"*.png", L"*.gamewip;*.tar.gz", L"*.*"},
        snapshot.filterPatterns));
    static_cast<void>(context.expectEq("preferred filter remains zero based", std::optional<std::size_t>{1}, snapshot.preferredFilterIndex));
    static_cast<void>(context.expectEq("suggested directory reaches the native boundary", open.suggestedDirectory, snapshot.suggestedDirectory));

    open.preferredFilterIndex.reset();
    armFileDialog(FileOperation::OpenFile, true, {GameWIP::FileSystem::Types::Path{L"C:\\chosen.png"}}, 1);
    const auto accepted = Desktop::Dialogs::openFile(open);
    static_cast<void>(context.expectTrue("single file acceptance succeeds", accepted.status.ok()));
    static_cast<void>(context.expectEq("single file acceptance is accepted", DialogTypes::Outcome::Accepted, accepted.outcome));
    static_cast<void>(context.expectEq("single file path is exact", GameWIP::FileSystem::Types::Path{L"C:\\chosen.png"}, accepted.path));
    static_cast<void>(context.expectEq("native one-based filter maps to portable zero", std::optional<std::size_t>{0}, accepted.selectedFilterIndex));
    static_cast<void>(
        context.expectFalse("absent preferred filter remains absent", Desktop::TestHooks::lastFileDialogSnapshot().preferredFilterIndex.has_value()));

    open.preferredFilterIndex = filters.size();
    static_cast<void>(
        context.expectEq("out-of-range preferred filter is invalid", ErrorCode::InvalidArgument, Desktop::Dialogs::openFile(open).status.code));
    open.preferredFilterIndex = 0;
    armFileDialog(FileOperation::OpenFile, true, {GameWIP::FileSystem::Types::Path{L"C:\\chosen.png"}}, 99);
    static_cast<void>(
        context.expectFalse("out-of-range native filter is not invented", Desktop::Dialogs::openFile(open).selectedFilterIndex.has_value()));

    FileDialogs::OpenDescription allFiles;
    const std::array<FileDialogs::Filter, 1> allFilesFilter{FileDialogs::Filter{"Everything", {}}};
    allFiles.filters = allFilesFilter;
    armFileDialog(FileOperation::OpenFile);
    static_cast<void>(Desktop::Dialogs::openFile(allFiles));
    static_cast<void>(context.expectEq(
        "empty extension span means all files",
        std::vector<std::wstring>{L"*.*"},
        Desktop::TestHooks::lastFileDialogSnapshot().filterPatterns));

    const std::array malformedExtension{std::string_view{"\xC3", 1}};
    const std::array malformedExtensionFilter{FileDialogs::Filter{"Malformed", malformedExtension}};
    allFiles.filters = malformedExtensionFilter;
    armFileDialog(FileOperation::OpenFile);
    static_cast<void>(context.expectEq(
        "malformed filter extension fails at conversion boundary",
        ErrorCode::EncodingFailed,
        Desktop::Dialogs::openFile(allFiles).status.code));
    const std::array malformedNameFilter{FileDialogs::Filter{std::string_view{"\xC3", 1}, pngExtensions}};
    allFiles.filters = malformedNameFilter;
    armFileDialog(FileOperation::OpenFile);
    static_cast<void>(context.expectEq(
        "malformed filter name fails at conversion boundary",
        ErrorCode::EncodingFailed,
        Desktop::Dialogs::openFile(allFiles).status.code));
    const std::array nullNameFilter{FileDialogs::Filter{std::string_view{"bad\0name", 8}, pngExtensions}};
    allFiles.filters = nullNameFilter;
    static_cast<void>(
        context.expectEq("filter name rejects embedded null", ErrorCode::InvalidArgument, Desktop::Dialogs::openFile(allFiles).status.code));

    allFiles = {};
    allFiles.title = std::string_view{"bad\0title", 9};
    static_cast<void>(
        context.expectEq("file title rejects embedded null", ErrorCode::InvalidArgument, Desktop::Dialogs::openFile(allFiles).status.code));
    allFiles.title = std::string_view{"\xF0\x28\x8C\x28", 4};
    armFileDialog(FileOperation::OpenFile);
    static_cast<void>(context.expectEq(
        "file title rejects malformed UTF-8 at conversion",
        ErrorCode::EncodingFailed,
        Desktop::Dialogs::openFile(allFiles).status.code));

    FileDialogs::SaveDescription save;
    const auto invalidSuggestedExtension = [&](std::string_view extension)
    {
        save.suggestedExtension = extension;
        static_cast<void>(context.expectEq(
            std::format("suggested extension '{}' is invalid", extension),
            ErrorCode::InvalidArgument,
            Desktop::Dialogs::saveFile(save).status.code));
    };
    invalidSuggestedExtension(".png");
    invalidSuggestedExtension("*.png");
    invalidSuggestedExtension("a/b");
    invalidSuggestedExtension("a\\b");
    invalidSuggestedExtension(std::string_view{"a\0b", 3});
    save.suggestedExtension = {};
    armFileDialog(FileOperation::SaveFile);
    static_cast<void>(context.expectTrue("empty suggested extension is accepted", Desktop::Dialogs::saveFile(save).status.ok()));
    save.suggestedExtension = "png";
    save.suggestedFileName = "project";
    save.title = "Save project";
    armFileDialog(FileOperation::SaveFile, true, {GameWIP::FileSystem::Types::Path{L"C:\\authoritative.custom"}});
    const auto saved = Desktop::Dialogs::saveFile(save);
    static_cast<void>(
        context.expectEq("accepted save path is authoritative", GameWIP::FileSystem::Types::Path{L"C:\\authoritative.custom"}, saved.path));
    static_cast<void>(context.expectEq(
        "save file name reaches native boundary",
        std::wstring{L"project"},
        Desktop::TestHooks::lastFileDialogSnapshot().suggestedFileName));
    static_cast<void>(context.expectEq(
        "save extension reaches native boundary",
        std::wstring{L"png"},
        Desktop::TestHooks::lastFileDialogSnapshot().suggestedExtension));
    save.suggestedFileName = std::string_view{"bad\0name", 8};
    static_cast<void>(context.expectEq("suggested file name rejects null", ErrorCode::InvalidArgument, Desktop::Dialogs::saveFile(save).status.code));
    save.suggestedFileName = std::string_view{"\xC3", 1};
    armFileDialog(FileOperation::SaveFile);
    static_cast<void>(
        context.expectEq("suggested file name rejects malformed UTF-8", ErrorCode::EncodingFailed, Desktop::Dialogs::saveFile(save).status.code));
    save.suggestedFileName = {};
    save.suggestedExtension = std::string_view{"\xC3", 1};
    armFileDialog(FileOperation::SaveFile);
    static_cast<void>(
        context.expectEq("suggested extension rejects malformed UTF-8", ErrorCode::EncodingFailed, Desktop::Dialogs::saveFile(save).status.code));

    open.preferredFilterIndex.reset();
    armFileDialog(
        FileOperation::OpenFiles,
        true,
        {GameWIP::FileSystem::Types::Path{L"C:\\one.txt"}, GameWIP::FileSystem::Types::Path{L"C:\\two.txt"}},
        2);
    const auto openedFiles = Desktop::Dialogs::openFiles(open);
    static_cast<void>(context.expectEq(
        "multiple file order is preserved",
        std::vector<GameWIP::FileSystem::Types::Path>{L"C:\\one.txt", L"C:\\two.txt"},
        openedFiles.paths));
    static_cast<void>(
        context.expectEq("multiple file selected filter is zero based", std::optional<std::size_t>{1}, openedFiles.selectedFilterIndex));
    armFileDialog(FileOperation::OpenFiles, true);
    const auto emptyAcceptedFiles = Desktop::Dialogs::openFiles(open);
    static_cast<void>(context.expectEq("accepted empty multiple result fails", ErrorCode::NativeFailure, emptyAcceptedFiles.status.code));
    static_cast<void>(context.expectTrue("failed empty multiple result remains defaulted", emptyAcceptedFiles.paths.empty()));
    armFileDialog(FileOperation::OpenFiles);
    const auto cancelledFiles = Desktop::Dialogs::openFiles(open);
    static_cast<void>(
        context.expectTrue("multiple cancellation succeeds with empty paths", cancelledFiles.status.ok() && cancelledFiles.paths.empty()));

    FileDialogs::FolderDescription folder{.title = "Choose folder", .suggestedDirectory = GameWIP::FileSystem::Types::Path{L"C:\\Folder"}};
    armFileDialog(FileOperation::SelectFolder, true, {GameWIP::FileSystem::Types::Path{L"C:\\Folder"}}, 1);
    const auto selectedFolder = Desktop::Dialogs::selectFolder(folder);
    static_cast<void>(context.expectTrue("single folder acceptance succeeds", selectedFolder.status.ok()));
    static_cast<void>(context.expectFalse("folder result never invents selected filter", selectedFolder.selectedFilterIndex.has_value()));
    armFileDialog(
        FileOperation::SelectFolders,
        true,
        {GameWIP::FileSystem::Types::Path{L"C:\\FolderA"}, GameWIP::FileSystem::Types::Path{L"C:\\FolderB"}},
        1);
    const auto selectedFolders = Desktop::Dialogs::selectFolders(folder);
    static_cast<void>(context.expectEq(
        "multiple folder order is preserved",
        std::vector<GameWIP::FileSystem::Types::Path>{L"C:\\FolderA", L"C:\\FolderB"},
        selectedFolders.paths));
    static_cast<void>(context.expectFalse("folder list never invents selected filter", selectedFolders.selectedFilterIndex.has_value()));
    armFileDialog(FileOperation::SelectFolder);
    const auto cancelledFolder = Desktop::Dialogs::selectFolder(folder);
    static_cast<void>(context.expectTrue("folder cancellation is successful and empty", cancelledFolder.status.ok() && cancelledFolder.path.empty()));

    folder.suggestedDirectory = GameWIP::FileSystem::Types::Path{std::wstring{L"C:\\bad\0folder", 13}};
    armFileDialog(FileOperation::SelectFolder);
    static_cast<void>(
        context.expectEq("suggested directory rejects native null", ErrorCode::InvalidArgument, Desktop::Dialogs::selectFolder(folder).status.code));
    folder.suggestedDirectory = GameWIP::FileSystem::Types::Path{L"C:\\MissingSuggestion"};
    armFileDialog(FileOperation::SelectFolder);
    Desktop::TestHooks::failNext(Desktop::TestHooks::FailurePoint::DialogSuggestedDirectory);
    static_cast<void>(
        context.expectEq("suggested directory native failure maps", ErrorCode::NativeFailure, Desktop::Dialogs::selectFolder(folder).status.code));

    Desktop::TestHooks::resetFailures();
    armFileDialog(FileOperation::OpenFile);
    Desktop::TestHooks::failNext(Desktop::TestHooks::FailurePoint::Allocation);
    static_cast<void>(context.expectEq("file dialog allocation failure maps", ErrorCode::OutOfMemory, Desktop::Dialogs::openFile({}).status.code));
    armFileDialog(FileOperation::OpenFile);
    Desktop::TestHooks::failNext(Desktop::TestHooks::FailurePoint::DialogConversion);
    static_cast<void>(
        context.expectEq("injected file conversion failure maps", ErrorCode::EncodingFailed, Desktop::Dialogs::openFile({}).status.code));
    armFileDialog(FileOperation::OpenFile);
    Desktop::TestHooks::failNext(Desktop::TestHooks::FailurePoint::DialogNativeOperation);
    static_cast<void>(context.expectEq("file native failure maps", ErrorCode::NativeFailure, Desktop::Dialogs::openFile({}).status.code));
    armFileDialog(FileOperation::OpenFile, true, {GameWIP::FileSystem::Types::Path{L"C:\\result.txt"}});
    Desktop::TestHooks::failNext(Desktop::TestHooks::FailurePoint::DialogResult);
    const auto failedResult = Desktop::Dialogs::openFile({});
    static_cast<void>(context.expectEq("file result failure maps", ErrorCode::NativeFailure, failedResult.status.code));
    static_cast<void>(context.expectTrue("failed file result remains defaulted", failedResult.path.empty() && !failedResult.selectedFilterIndex));

    Desktop::Window owner;
    FileDialogs::OpenDescription owned{.owner = &owner};
    static_cast<void>(context.expectEq("closed file-dialog owner is NotOpen", ErrorCode::NotOpen, Desktop::Dialogs::openFile(owned).status.code));
    Desktop::Types::Description ownerDescription;
    ownerDescription.visible = false;
    static_cast<void>(context.expectTrue("file-dialog owner opens", owner.open(ownerDescription, 4).ok()));
    armFileDialog(FileOperation::OpenFile);
    static_cast<void>(context.expectTrue("current-thread owner is accepted", Desktop::Dialogs::openFile(owned).status.ok()));
    ErrorCode foreignOwnerCode = ErrorCode::Unknown;
    std::thread foreignOwner(
        [&]
        {
            foreignOwnerCode = Desktop::Dialogs::openFile(owned).status.code;
        });
    foreignOwner.join();
    static_cast<void>(context.expectEq("foreign-thread file-dialog owner is busy", ErrorCode::ResourceBusy, foreignOwnerCode));
    static_cast<void>(owner.close());
    armFileDialog(FileOperation::OpenFile);
    static_cast<void>(context.expectTrue("ownerless file dialog stays on caller thread", Desktop::Dialogs::openFile({}).status.ok()));
}

void testDialogApartmentContracts(TestSupport::Context &context)
{
    HRESULT freshBefore = S_OK;
    HRESULT freshAfter = S_OK;
    IO::Types::Status freshStatus;
    std::thread fresh(
        [&]
        {
            APTTYPE type = APTTYPE_CURRENT;
            APTTYPEQUALIFIER qualifier = APTTYPEQUALIFIER_NONE;
            freshBefore = CoGetApartmentType(&type, &qualifier);
            freshStatus = Desktop::TestHooks::testDialogApartment();
            freshAfter = CoGetApartmentType(&type, &qualifier);
        });
    fresh.join();
    static_cast<void>(context.expectEq("fresh dialog thread starts without COM", CO_E_NOTINITIALIZED, freshBefore));
    static_cast<void>(context.expectTrue("fresh dialog thread receives compatible STA lease", freshStatus.ok()));
    static_cast<void>(context.expectEq("Desktop balances only its COM initialization", CO_E_NOTINITIALIZED, freshAfter));

    IO::Types::Status staStatus;
    HRESULT staStillInitialized = E_FAIL;
    std::thread sta(
        [&]
        {
            const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            if (SUCCEEDED(initialized))
            {
                staStatus = Desktop::TestHooks::testDialogApartment();
                APTTYPE type = APTTYPE_CURRENT;
                APTTYPEQUALIFIER qualifier = APTTYPEQUALIFIER_NONE;
                staStillInitialized = CoGetApartmentType(&type, &qualifier);
                CoUninitialize();
            }
        });
    sta.join();
    static_cast<void>(context.expectTrue("existing STA is reused", staStatus.ok()));
    static_cast<void>(context.expectTrue("Desktop does not uninitialize caller STA", SUCCEEDED(staStillInitialized)));

    IO::Types::Status mtaStatus;
    std::thread mta(
        [&]
        {
            const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            if (SUCCEEDED(initialized))
            {
                mtaStatus = Desktop::TestHooks::testDialogApartment();
                CoUninitialize();
            }
        });
    mta.join();
    static_cast<void>(context.expectEq("incompatible MTA is ResourceBusy", ErrorCode::ResourceBusy, mtaStatus.code));
}

void testMessageDialogs(TestSupport::Context &context)
{
    using Button = MessageDialogs::Button;
    using Buttons = MessageDialogs::Buttons;

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

    for (const ButtonSetCase &testCase : cases)
    {
        MessageDialogs::Description description{.title = "Message", .message = "Body", .buttons = testCase.buttons};
        Desktop::TestHooks::completeNextMessageDialog({.button = testCase.members[0], .dismissed = false});
        const auto nativeDefault = Desktop::Dialogs::showMessage(description);
        static_cast<void>(context.expectTrue("Message native default succeeds", nativeDefault.status.ok()));
        static_cast<void>(context.expectEq("Message selection is accepted", DialogTypes::Outcome::Accepted, nativeDefault.outcome));
        static_cast<void>(context.expectEq("Message selection round trips", testCase.members[0], nativeDefault.button));
        static_cast<void>(
            context.expectEq("Message default None reaches backend", Button::None, Desktop::TestHooks::lastMessageDialogSnapshot().defaultButton));

        for (std::size_t index = 0; index < testCase.memberCount; ++index)
        {
            description.defaultButton = testCase.members[index];
            Desktop::TestHooks::completeNextMessageDialog({.button = testCase.members[index], .dismissed = false});
            const auto selected = Desktop::Dialogs::showMessage(description);
            const bool cancel = testCase.members[index] == Button::Cancel;
            static_cast<void>(context.expectTrue("valid explicit Message default succeeds", selected.status.ok()));
            static_cast<void>(context.expectEq(
                "Message button maps to exact outcome",
                cancel ? DialogTypes::Outcome::Cancelled : DialogTypes::Outcome::Accepted,
                selected.outcome));
            static_cast<void>(context.expectEq("Message button maps exactly", testCase.members[index], selected.button));
        }

        description.defaultButton = Button::None;
        Desktop::TestHooks::completeNextMessageDialog({.dismissed = true});
        const auto dismissed = Desktop::Dialogs::showMessage(description);
        static_cast<void>(context.expectTrue("Message native dismissal succeeds", dismissed.status.ok()));
        static_cast<void>(context.expectEq("Message native dismissal is cancelled", DialogTypes::Outcome::Cancelled, dismissed.outcome));
        static_cast<void>(context.expectEq(
            "Message dismissal reports semantic cancel only when available",
            testCase.hasCancel ? Button::Cancel : Button::None,
            dismissed.button));
    }

    MessageDialogs::Description description;
    description.buttons = Buttons::Ok;
    description.defaultButton = Button::Yes;
    static_cast<void>(
        context.expectEq("Message default must belong to set", ErrorCode::InvalidArgument, Desktop::Dialogs::showMessage(description).status.code));
    description.defaultButton = static_cast<Button>(255);
    static_cast<void>(context.expectEq(
        "invalid Message default enum is rejected",
        ErrorCode::InvalidArgument,
        Desktop::Dialogs::showMessage(description).status.code));
    description.defaultButton = Button::None;
    description.buttons = static_cast<Buttons>(255);
    static_cast<void>(context.expectEq(
        "invalid Message Buttons enum is rejected",
        ErrorCode::InvalidArgument,
        Desktop::Dialogs::showMessage(description).status.code));
    description.buttons = Buttons::Ok;
    description.severity = static_cast<DialogTypes::Severity>(255);
    static_cast<void>(context.expectEq(
        "invalid Message Severity enum is rejected",
        ErrorCode::InvalidArgument,
        Desktop::Dialogs::showMessage(description).status.code));

    constexpr std::array severities{
        DialogTypes::Severity::None,
        DialogTypes::Severity::Information,
        DialogTypes::Severity::Warning,
        DialogTypes::Severity::Error};
    for (DialogTypes::Severity severity : severities)
    {
        description.severity = severity;
        Desktop::TestHooks::completeNextMessageDialog({.button = Button::Ok, .dismissed = false});
        static_cast<void>(context.expectTrue("every Message Severity succeeds", Desktop::Dialogs::showMessage(description).status.ok()));
        static_cast<void>(context.expectEq("Message Severity reaches backend", severity, Desktop::TestHooks::lastMessageDialogSnapshot().severity));
    }

    description.severity = DialogTypes::Severity::None;
    description.title = std::string_view{"bad\0title", 9};
    static_cast<void>(
        context.expectEq("Message title rejects embedded null", ErrorCode::InvalidArgument, Desktop::Dialogs::showMessage(description).status.code));
    description.title = std::string_view{"\xC3", 1};
    Desktop::TestHooks::completeNextMessageDialog({.button = Button::Ok, .dismissed = false});
    static_cast<void>(
        context.expectEq("Message title rejects malformed UTF-8", ErrorCode::EncodingFailed, Desktop::Dialogs::showMessage(description).status.code));
    description.title = {};
    description.message = std::string_view{"bad\0message", 11};
    static_cast<void>(
        context.expectEq("Message text rejects embedded null", ErrorCode::InvalidArgument, Desktop::Dialogs::showMessage(description).status.code));
    description.message = std::string_view{"\xC3", 1};
    Desktop::TestHooks::completeNextMessageDialog({.button = Button::Ok, .dismissed = false});
    static_cast<void>(context.expectEq(
        "Message malformed UTF-8 maps at conversion",
        ErrorCode::EncodingFailed,
        Desktop::Dialogs::showMessage(description).status.code));

    description.message = "failure";
    Desktop::TestHooks::completeNextMessageDialog({.button = Button::Ok, .dismissed = false});
    Desktop::TestHooks::failNext(Desktop::TestHooks::FailurePoint::Allocation);
    auto failed = Desktop::Dialogs::showMessage(description);
    static_cast<void>(context.expectEq("Message allocation failure maps", ErrorCode::OutOfMemory, failed.status.code));
    static_cast<void>(context.expectEq("failed Message outcome remains default", DialogTypes::Outcome::Cancelled, failed.outcome));
    static_cast<void>(context.expectEq("failed Message button remains default", Button::None, failed.button));
    Desktop::TestHooks::completeNextMessageDialog({.button = Button::Ok, .dismissed = false});
    Desktop::TestHooks::failNext(Desktop::TestHooks::FailurePoint::DialogNativeOperation);
    failed = Desktop::Dialogs::showMessage(description);
    static_cast<void>(context.expectEq("Message native failure maps", ErrorCode::NativeFailure, failed.status.code));
    static_cast<void>(context.expectEq("native-failed Message remains defaulted", Button::None, failed.button));
    Desktop::TestHooks::completeNextMessageDialog({.button = Button::Ok, .dismissed = false});
    Desktop::TestHooks::failNext(Desktop::TestHooks::FailurePoint::DialogResult);
    failed = Desktop::Dialogs::showMessage(description);
    static_cast<void>(context.expectEq("Message unknown native result maps", ErrorCode::NativeFailure, failed.status.code));
}

void testPromptDialogs(TestSupport::Context &context)
{
    using Button = PromptDialogs::Button;
    using ButtonId = PromptDialogs::ButtonId;
    using Option = PromptDialogs::Option;
    using OptionId = PromptDialogs::OptionId;

    PromptDialogs::Description description;
    static_cast<void>(
        context.expectEq("Prompt requires a button", ErrorCode::InvalidArgument, Desktop::Dialogs::showPrompt(description).status.code));

    std::array<Button, 3> buttons{
        Button{ButtonId{1}, "Proceed", "Use recommended settings"},
        Button{ButtonId{2}, "Cancel", {}},
        Button{ButtonId{std::numeric_limits<std::uint32_t>::max()}, "Advanced", "Choose manually"}};
    description.buttons = buttons;

    buttons[0].id = {};
    static_cast<void>(
        context.expectEq("Prompt rejects zero button ID", ErrorCode::InvalidArgument, Desktop::Dialogs::showPrompt(description).status.code));
    buttons[0].id = ButtonId{1};
    buttons[1].id = ButtonId{1};
    static_cast<void>(
        context.expectEq("Prompt rejects duplicate button ID", ErrorCode::InvalidArgument, Desktop::Dialogs::showPrompt(description).status.code));
    buttons[1].id = ButtonId{2};
    buttons[0].label = {};
    static_cast<void>(
        context.expectEq("Prompt rejects empty button label", ErrorCode::InvalidArgument, Desktop::Dialogs::showPrompt(description).status.code));
    buttons[0].label = std::string_view{"bad\0label", 9};
    static_cast<void>(
        context.expectEq("Prompt rejects button-label null", ErrorCode::InvalidArgument, Desktop::Dialogs::showPrompt(description).status.code));
    buttons[0].label = std::string_view{"\xC3", 1};
    Desktop::TestHooks::completeNextPromptDialog({});
    static_cast<void>(
        context.expectEq("Prompt rejects malformed button label", ErrorCode::EncodingFailed, Desktop::Dialogs::showPrompt(description).status.code));
    buttons[0].label = "Proceed";
    buttons[0].description = std::string_view{"bad\0description", 15};
    static_cast<void>(context.expectEq(
        "Prompt rejects button-description null",
        ErrorCode::InvalidArgument,
        Desktop::Dialogs::showPrompt(description).status.code));
    buttons[0].description = std::string_view{"\xF0\x28\x8C\x28", 4};
    Desktop::TestHooks::completeNextPromptDialog({});
    static_cast<void>(context.expectEq(
        "Prompt rejects malformed button description",
        ErrorCode::EncodingFailed,
        Desktop::Dialogs::showPrompt(description).status.code));
    buttons[0].description = "Use recommended settings";

    description.defaultButton = ButtonId{99};
    static_cast<void>(context.expectEq(
        "Prompt default button must reference a button",
        ErrorCode::InvalidArgument,
        Desktop::Dialogs::showPrompt(description).status.code));
    description.defaultButton = {};
    description.cancelButton = ButtonId{99};
    static_cast<void>(context.expectEq(
        "Prompt cancel button must reference a button",
        ErrorCode::InvalidArgument,
        Desktop::Dialogs::showPrompt(description).status.code));
    description.cancelButton = {};

    std::array<Option, 2> options{Option{OptionId{1}, "Fast"}, Option{OptionId{std::numeric_limits<std::uint32_t>::max()}, "Thorough"}};
    description.options = options;
    options[0].id = {};
    static_cast<void>(
        context.expectEq("Prompt rejects zero option ID", ErrorCode::InvalidArgument, Desktop::Dialogs::showPrompt(description).status.code));
    options[0].id = OptionId{1};
    options[1].id = OptionId{1};
    static_cast<void>(
        context.expectEq("Prompt rejects duplicate option ID", ErrorCode::InvalidArgument, Desktop::Dialogs::showPrompt(description).status.code));
    options[1].id = OptionId{std::numeric_limits<std::uint32_t>::max()};
    options[0].label = {};
    static_cast<void>(
        context.expectEq("Prompt rejects empty option label", ErrorCode::InvalidArgument, Desktop::Dialogs::showPrompt(description).status.code));
    options[0].label = std::string_view{"bad\0option", 10};
    static_cast<void>(
        context.expectEq("Prompt rejects option-label null", ErrorCode::InvalidArgument, Desktop::Dialogs::showPrompt(description).status.code));
    options[0].label = std::string_view{"\xC3", 1};
    Desktop::TestHooks::completeNextPromptDialog({});
    static_cast<void>(
        context.expectEq("Prompt rejects malformed option label", ErrorCode::EncodingFailed, Desktop::Dialogs::showPrompt(description).status.code));
    options[0].label = "Fast";
    description.defaultOption = OptionId{99};
    static_cast<void>(context.expectEq(
        "Prompt default option must reference an option",
        ErrorCode::InvalidArgument,
        Desktop::Dialogs::showPrompt(description).status.code));
    description.defaultOption = {};
    description.options = {};
    description.defaultOption = OptionId{1};
    static_cast<void>(context.expectEq(
        "Prompt default option is invalid without options",
        ErrorCode::InvalidArgument,
        Desktop::Dialogs::showPrompt(description).status.code));
    description.options = options;
    description.defaultOption = {};

    description.checkBox = PromptDialogs::CheckBox{};
    static_cast<void>(
        context.expectEq("Prompt checkbox requires a label", ErrorCode::InvalidArgument, Desktop::Dialogs::showPrompt(description).status.code));
    description.checkBox = PromptDialogs::CheckBox{std::string_view{"bad\0check", 9}, false};
    static_cast<void>(
        context.expectEq("Prompt checkbox rejects null", ErrorCode::InvalidArgument, Desktop::Dialogs::showPrompt(description).status.code));
    description.checkBox = PromptDialogs::CheckBox{std::string_view{"\xC3", 1}, false};
    Desktop::TestHooks::completeNextPromptDialog({});
    static_cast<void>(context.expectEq(
        "Prompt checkbox rejects malformed UTF-8",
        ErrorCode::EncodingFailed,
        Desktop::Dialogs::showPrompt(description).status.code));
    description.checkBox.reset();

    const auto rejectText = [&](std::string_view name, std::string_view PromptDialogs::Description::*member)
    {
        description.*member = std::string_view{"bad\0text", 8};
        static_cast<void>(context.expectEq(
            std::format("Prompt {} rejects null", name),
            ErrorCode::InvalidArgument,
            Desktop::Dialogs::showPrompt(description).status.code));
        description.*member = std::string_view{"\xC3", 1};
        Desktop::TestHooks::completeNextPromptDialog({});
        static_cast<void>(context.expectEq(
            std::format("Prompt {} rejects malformed UTF-8", name),
            ErrorCode::EncodingFailed,
            Desktop::Dialogs::showPrompt(description).status.code));
        description.*member = {};
    };
    rejectText("title", &PromptDialogs::Description::title);
    rejectText("heading", &PromptDialogs::Description::heading);
    rejectText("message", &PromptDialogs::Description::message);
    rejectText("details", &PromptDialogs::Description::details);
    rejectText("supplemental text", &PromptDialogs::Description::supplementalText);

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
    Desktop::TestHooks::completeNextPromptDialog({.buttonIndex = 0, .optionIndex = 1, .checkBoxChecked = false, .dismissed = false});
    const auto accepted = Desktop::Dialogs::showPrompt(description);
    static_cast<void>(context.expectTrue("rich Prompt succeeds", accepted.status.ok()));
    static_cast<void>(context.expectEq("rich Prompt accepts selected action", DialogTypes::Outcome::Accepted, accepted.outcome));
    static_cast<void>(context.expectEq("portable button ID round trips", buttons[0].id, accepted.button));
    static_cast<void>(context.expectEq("portable option ID round trips", options[1].id, accepted.option));
    static_cast<void>(context.expectEq("Prompt final checkbox state returns", std::optional<bool>{false}, accepted.checkBoxChecked));
    const auto &snapshot = Desktop::TestHooks::lastPromptDialogSnapshot();
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

    Desktop::TestHooks::completeNextPromptDialog({.buttonIndex = 1, .optionIndex = 0, .checkBoxChecked = true, .dismissed = false});
    const auto explicitCancel = Desktop::Dialogs::showPrompt(description);
    static_cast<void>(context.expectEq("configured cancel action is cancelled", DialogTypes::Outcome::Cancelled, explicitCancel.outcome));
    static_cast<void>(context.expectEq("configured cancel ID round trips", buttons[1].id, explicitCancel.button));
    static_cast<void>(context.expectEq("cancelled Prompt retains final option", options[0].id, explicitCancel.option));
    static_cast<void>(context.expectEq("cancelled Prompt retains final checkbox", std::optional<bool>{true}, explicitCancel.checkBoxChecked));
    Desktop::TestHooks::completeNextPromptDialog({.optionIndex = 1, .checkBoxChecked = false, .dismissed = true});
    const auto dismissed = Desktop::Dialogs::showPrompt(description);
    static_cast<void>(context.expectEq("Prompt dismissal is cancelled", DialogTypes::Outcome::Cancelled, dismissed.outcome));
    static_cast<void>(context.expectEq("Prompt dismissal uses semantic cancel ID", buttons[1].id, dismissed.button));

    description.cancelButton = {};
    description.defaultButton = {};
    description.options = {};
    description.defaultOption = {};
    description.checkBox.reset();
    Desktop::TestHooks::completeNextPromptDialog({.dismissed = true});
    const auto noSemanticCancel = Desktop::Dialogs::showPrompt(description);
    static_cast<void>(context.expectFalse("Prompt dismissal without cancel has invalid ID", noSemanticCancel.button.isValid()));
    static_cast<void>(context.expectFalse("Prompt with no option selection has invalid option", noSemanticCancel.option.isValid()));
    static_cast<void>(context.expectFalse("Prompt without checkbox returns nullopt", noSemanticCancel.checkBoxChecked.has_value()));

    description.options = options;
    description.defaultOption = {};
    Desktop::TestHooks::completeNextPromptDialog({.buttonIndex = 2, .dismissed = false});
    const auto largeId = Desktop::Dialogs::showPrompt(description);
    static_cast<void>(context.expectEq("large uint32 Prompt ID is accepted", buttons[2].id, largeId.button));

    Desktop::TestHooks::completeNextPromptDialog({.buttonIndex = 0, .dismissed = false});
    Desktop::TestHooks::failNext(Desktop::TestHooks::FailurePoint::Allocation);
    auto failed = Desktop::Dialogs::showPrompt(description);
    static_cast<void>(context.expectEq("Prompt allocation failure maps", ErrorCode::OutOfMemory, failed.status.code));
    static_cast<void>(context.expectTrue(
        "failed Prompt domain fields remain defaulted",
        failed.outcome == DialogTypes::Outcome::Cancelled && !failed.button.isValid() && !failed.option.isValid() &&
            !failed.checkBoxChecked.has_value()));

    Desktop::TestHooks::completeNextPromptDialog({.buttonIndex = 0, .dismissed = false});
    Desktop::TestHooks::failNext(Desktop::TestHooks::FailurePoint::DialogNativeOperation);
    failed = Desktop::Dialogs::showPrompt(description);
    static_cast<void>(context.expectEq("Prompt native failure maps", ErrorCode::NativeFailure, failed.status.code));
    static_cast<void>(context.expectFalse("native-failed Prompt button remains invalid", failed.button.isValid()));

    Desktop::TestHooks::completeNextPromptDialog({.buttonIndex = 0, .dismissed = false});
    Desktop::TestHooks::failNext(Desktop::TestHooks::FailurePoint::DialogResult);
    failed = Desktop::Dialogs::showPrompt(description);
    static_cast<void>(context.expectEq("Prompt invalid native result maps", ErrorCode::NativeFailure, failed.status.code));
}
#endif
