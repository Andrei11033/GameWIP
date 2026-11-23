/// @file desktop_file_dialog_tests.inl
/// @brief File and folder dialog validation tests.

namespace DialogTypes = Desktop::Types::Dialogs;
namespace FileDialogs = DialogTypes::File;
namespace MessageDialogs = DialogTypes::Message;
namespace PromptDialogs = DialogTypes::Prompt;

#if DESKTOP_INTERNAL_TEST_HOOKS

/// ------------------------------------------------------------
/// File and folder dialogs
/// ------------------------------------------------------------

void armFileDialog(
    Win32TestHooks::FileDialogOperation operation,
    bool accepted = false,
    std::vector<GameWIP::FileSystem::Types::Path> paths = {},
    std::optional<std::size_t> nativeFilterIndex = std::nullopt)
{
    Win32TestHooks::completeNextFileDialog(operation, {.accepted = accepted, .paths = std::move(paths), .nativeFilterIndex = nativeFilterIndex});
}

void testFileAndFolderDialogs(TestSupport::Context &context)
{
    Win32TestHooks::resetDialogState();

    using FileOperation = Win32TestHooks::FileDialogOperation;

    const auto invalidExtension = [&](std::string_view extension)
    {
        const std::array extensions{extension};
        const std::array filters{FileDialogs::Filter{"Files", extensions}};
        const FileDialogs::OpenDescription description{.filters = filters};
        static_cast<void>(context.expectEq(
            std::format("extension '{}' is invalid", extension),
            ErrorCode::InvalidArgument,
            Desktop::Dialogs::openFile(description).status.code));
    };

    // Reject unsafe extension patterns before invoking the native dialog.
    invalidExtension("");
    invalidExtension(".png");
    invalidExtension("*");
    invalidExtension("?");
    invalidExtension("a/b");
    invalidExtension("a\\b");
    invalidExtension(std::string_view{"a\0b", 3});

    // Preserve filter order and map selection to a portable zero-based index.
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

    const auto &snapshot = Win32TestHooks::lastFileDialogSnapshot();
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
        context.expectFalse("absent preferred filter remains absent", Win32TestHooks::lastFileDialogSnapshot().preferredFilterIndex.has_value()));

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
        Win32TestHooks::lastFileDialogSnapshot().filterPatterns));

    // Reject malformed text and pattern injection at the conversion boundary.
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

    const std::array semicolonExtension{std::string_view{"png;jpg"}};
    const std::array semicolonFilter{FileDialogs::Filter{"Injected", semicolonExtension}};
    allFiles.filters = semicolonFilter;
    static_cast<void>(context.expectEq(
        "filter extension rejects semicolon injection",
        ErrorCode::InvalidArgument,
        Desktop::Dialogs::openFile(allFiles).status.code));

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

    // Keep save paths authoritative and validate optional name fields.
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
        Win32TestHooks::lastFileDialogSnapshot().suggestedFileName));
    static_cast<void>(context.expectEq(
        "save extension reaches native boundary",
        std::wstring{L"png"},
        Win32TestHooks::lastFileDialogSnapshot().suggestedExtension));
    save.suggestedExtension = "png;jpg";
    armFileDialog(FileOperation::SaveFile);
    static_cast<void>(context.expectTrue("suggested extension semicolon remains portable", Desktop::Dialogs::saveFile(save).status.ok()));
    static_cast<void>(context.expectEq(
        "semicolon suggested extension reaches native boundary unchanged",
        std::wstring{L"png;jpg"},
        Win32TestHooks::lastFileDialogSnapshot().suggestedExtension));
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

    // Preserve native ordering and cancellation results for multiple selections.
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

    // Preserve folder selections and reject invalid suggested paths.
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
    Desktop::Internal::TestHooks::failNext(Desktop::Internal::TestHooks::FailurePoint::DialogSuggestedDirectory);
    static_cast<void>(
        context.expectEq("suggested directory native failure maps", ErrorCode::NativeFailure, Desktop::Dialogs::selectFolder(folder).status.code));

    // Map allocation, conversion, native, and result failures.
    Desktop::Internal::TestHooks::resetFailures();
    armFileDialog(FileOperation::OpenFile);
    Desktop::Internal::TestHooks::failNext(Desktop::Internal::TestHooks::FailurePoint::Allocation);
    static_cast<void>(context.expectEq("file dialog allocation failure maps", ErrorCode::OutOfMemory, Desktop::Dialogs::openFile({}).status.code));
    armFileDialog(FileOperation::OpenFile);
    Desktop::Internal::TestHooks::failNext(Desktop::Internal::TestHooks::FailurePoint::DialogConversion);
    static_cast<void>(
        context.expectEq("injected file conversion failure maps", ErrorCode::EncodingFailed, Desktop::Dialogs::openFile({}).status.code));
    armFileDialog(FileOperation::OpenFile);
    Desktop::Internal::TestHooks::failNext(Desktop::Internal::TestHooks::FailurePoint::DialogNativeOperation);
    static_cast<void>(context.expectEq("file native failure maps", ErrorCode::NativeFailure, Desktop::Dialogs::openFile({}).status.code));
    armFileDialog(FileOperation::OpenFile, true, {GameWIP::FileSystem::Types::Path{L"C:\\result.txt"}});
    Desktop::Internal::TestHooks::failNext(Desktop::Internal::TestHooks::FailurePoint::DialogResult);
    const auto failedResult = Desktop::Dialogs::openFile({});
    static_cast<void>(context.expectEq("file result failure maps", ErrorCode::NativeFailure, failedResult.status.code));
    static_cast<void>(context.expectTrue("failed file result remains defaulted", failedResult.path.empty() && !failedResult.selectedFilterIndex));

    // Enforce owner lifetime and thread affinity.
    Desktop::Window owner;
    FileDialogs::OpenDescription owned{.owner = &owner};
    static_cast<void>(context.expectEq("closed file-dialog owner is NotOpen", ErrorCode::NotOpen, Desktop::Dialogs::openFile(owned).status.code));
    Desktop::Types::Description ownerDescription;
    ownerDescription.activation.visible = false;
    static_cast<void>(context.expectTrue("file-dialog owner opens", owner.open(ownerDescription, Desktop::Events::owned(4)).ok()));
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

#endif
