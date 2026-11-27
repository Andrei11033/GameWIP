/// @file win32_file_dialogs.cpp
/// @brief Win32 shell file dialogs.

#include "desktop/internal/results.h"
#include "desktop/platform/dialogs.h"

#include "desktop/internal/desktop_test_hooks.h"
#include "desktop/internal/window_state.h"
#include "desktop/platform/win32/internal/test_hooks.h"
#include "desktop/platform/win32/internal/win32_com.h"
#include "desktop/platform/win32/internal/win32_dialog_helpers.h"
#include "desktop/platform/win32/internal/win32_window_backend.h"
#include "desktop/platform/win32/internal/win32_unicode.h"

#include <commctrl.h>
#include <objbase.h>
#include <shobjidl.h>

#include <limits>
#include <new>
#include <optional>
#include <span>
#include <string_view>
#include <string>
#include <utility>
#include <vector>

namespace GameWIP::Desktop::Platform
{
    namespace Win32TestHooks = GameWIP::Desktop::Internal::TestHooks::Win32;

    /// ------------------------------------------------------------
    /// File-dialog preparation
    /// ------------------------------------------------------------

    namespace
    {
        using ErrorCode = GameWIP::Status::Types::Code;
        using Win32::convertText;
        using Win32::ownerHandle;

        struct FileDialogOperation
        {
            GameWIP::Status::Types::Status status;
            Types::Dialogs::Outcome outcome = Types::Dialogs::Outcome::Cancelled;
            FileSystem::Types::Path path;
            std::optional<std::size_t> selectedFilterIndex;
        };

        struct FileDialogListOperation
        {
            GameWIP::Status::Types::Status status;
            Types::Dialogs::Outcome outcome = Types::Dialogs::Outcome::Cancelled;
            std::vector<FileSystem::Types::Path> paths;
            std::optional<std::size_t> selectedFilterIndex;
        };

        [[nodiscard]] GameWIP::Status::Types::Result<Types::Dialogs::File::Selection> sharedResult(FileDialogOperation operation)
        {
            Types::Dialogs::File::Selection selection{operation.outcome, std::move(operation.path), operation.selectedFilterIndex};
            const bool hasPartialValue =
                selection.outcome == Types::Dialogs::Outcome::Accepted || !selection.path.empty() || selection.selectedFilterIndex.has_value();
            return Internal::resultWithPartial(std::move(operation.status), std::move(selection), hasPartialValue);
        }

        [[nodiscard]] GameWIP::Status::Types::Result<Types::Dialogs::File::SelectionList> sharedResult(FileDialogListOperation operation)
        {
            Types::Dialogs::File::SelectionList selection{operation.outcome, std::move(operation.paths), operation.selectedFilterIndex};
            const bool hasPartialValue =
                selection.outcome == Types::Dialogs::Outcome::Accepted || !selection.paths.empty() || selection.selectedFilterIndex.has_value();
            return Internal::resultWithPartial(std::move(operation.status), std::move(selection), hasPartialValue);
        }

        /// @brief Releases a string allocated by a Shell COM API.
        class CoTaskMemString final
        {
        public:
            ~CoTaskMemString() noexcept
            {
                CoTaskMemFree(value);
            }

            PWSTR value = nullptr;
        };

        /// @brief Resolves and applies a suggested filesystem directory to a native dialog.
        [[nodiscard]] GameWIP::Status::Types::Status setSuggestedDirectory(IFileDialog &dialog, const FileSystem::Types::Path &directory) noexcept
        {
            if(directory.empty())
            {
                return GameWIP::Status::Types::Status{};
            }

            try
            {
                const auto &native = directory.native();

                if(std::wstring_view(native).contains(L'\0'))
                {
                    return GameWIP::Desktop::Internal::statusFor(ErrorCode::InvalidArgument);
                }
                ComPtr<IShellItem> item;
                const HRESULT created = SHCreateItemFromParsingName(native.c_str(), nullptr, IID_PPV_ARGS(item.put()));

                if(FAILED(created))
                {
                    return statusFromHResult(created);
                }

                if(!item)
                {
                    return GameWIP::Desktop::Internal::statusFor(ErrorCode::NativeFailure);
                }
                const HRESULT applied = dialog.SetDefaultFolder(item.get());
                return SUCCEEDED(applied) ? GameWIP::Status::Types::Status{} : statusFromHResult(applied);
            }
            catch(const std::bad_alloc &)
            {
                return GameWIP::Desktop::Internal::statusFor(ErrorCode::OutOfMemory);
            }
            catch(...)
            {
                return GameWIP::Desktop::Internal::statusFor(ErrorCode::Unknown);
            }
        }

        /// @brief Owns UTF-16 filter storage referenced by COMDLG_FILTERSPEC entries.
        struct NativeFilters
        {
            std::vector<std::wstring> names;
            std::vector<std::wstring> patterns;
            std::vector<COMDLG_FILTERSPEC> specifications;
        };

        /// @brief Converts portable filter names/extensions while retaining stable COM pointers.
        [[nodiscard]] GameWIP::Status::Types::Status makeFilters(
            std::span<const Types::Dialogs::File::Filter> filters,
            NativeFilters &native) noexcept
        {
            if(filters.size() > std::numeric_limits<UINT>::max())
            {
                return GameWIP::Desktop::Internal::statusFor(ErrorCode::InvalidArgument);
            }

            try
            {
                native.names.reserve(filters.size());
                native.patterns.reserve(filters.size());

                for(const auto &filter : filters)
                {
                    std::wstring name;
                    GameWIP::Status::Types::Status status = convertText(filter.name, name);

                    if(!status.ok())
                    {
                        return status;
                    }
                    native.names.push_back(std::move(name));

                    std::wstring pattern;

                    if(filter.extensions.empty())
                    {
                        pattern = L"*.*";
                    }
                    else
                    {
                        for(std::size_t index = 0; index < filter.extensions.size(); ++index)
                        {
                            std::wstring extension;
                            status = convertText(filter.extensions[index], extension);

                            if(!status.ok())
                            {
                                return status;
                            }

                            if(index != 0)
                            {
                                pattern.push_back(L';');
                            }
                            pattern.append(L"*.");
                            pattern.append(extension);
                        }
                    }
                    native.patterns.push_back(std::move(pattern));
                }

                native.specifications.reserve(filters.size());

                for(std::size_t index = 0; index < filters.size(); ++index)
                {
                    native.specifications.push_back({native.names[index].c_str(), native.patterns[index].c_str()});
                }

                return GameWIP::Status::Types::Status{};
            }
            catch(const std::bad_alloc &)
            {
                return GameWIP::Desktop::Internal::statusFor(ErrorCode::OutOfMemory);
            }
            catch(...)
            {
                return GameWIP::Desktop::Internal::statusFor(ErrorCode::Unknown);
            }
        }

        /// @brief Applies shared title, directory, filter, and native-option configuration.
        [[nodiscard]] GameWIP::Status::Types::Status configureCommon(
            IFileDialog &dialog,
            std::string_view title,
            const FileSystem::Types::Path &suggestedDirectory,
            std::span<const Types::Dialogs::File::Filter> filters,
            std::optional<std::size_t> preferredFilterIndex,
            DWORD requestedOptions,
            NativeFilters &nativeFilters) noexcept
        {
            FILEOPENDIALOGOPTIONS options{};
            HRESULT result = dialog.GetOptions(&options);

            if(FAILED(result))
            {
                return statusFromHResult(result);
            }

            result = dialog.SetOptions(static_cast<FILEOPENDIALOGOPTIONS>(static_cast<DWORD>(options) | requestedOptions | FOS_FORCEFILESYSTEM));

            if(FAILED(result))
            {
                return statusFromHResult(result);
            }

            if(!title.empty())
            {
                std::wstring wideTitle;
                GameWIP::Status::Types::Status status = convertText(title, wideTitle);

                if(!status.ok())
                {
                    return status;
                }

                result = dialog.SetTitle(wideTitle.c_str());

                if(FAILED(result))
                {
                    return statusFromHResult(result);
                }
            }

            GameWIP::Status::Types::Status status = setSuggestedDirectory(dialog, suggestedDirectory);

            if(!status.ok())
            {
                return status;
            }

            status = makeFilters(filters, nativeFilters);

            if(!status.ok())
            {
                return status;
            }

            if(!nativeFilters.specifications.empty())
            {
                result = dialog.SetFileTypes(static_cast<UINT>(nativeFilters.specifications.size()), nativeFilters.specifications.data());

                if(FAILED(result))
                {
                    return statusFromHResult(result);
                }

                if(preferredFilterIndex)
                {
                    result = dialog.SetFileTypeIndex(static_cast<UINT>(*preferredFilterIndex + 1));

                    if(FAILED(result))
                    {
                        return statusFromHResult(result);
                    }
                }
            }

            return GameWIP::Status::Types::Status{};
        }

        /// @brief Reads a native one-based filter index into the portable zero-based result.
        [[nodiscard]] GameWIP::Status::Types::Status selectedFilter(
            IFileDialog &dialog,
            std::size_t filterCount,
            std::optional<std::size_t> &selected) noexcept
        {
            selected.reset();

            if(filterCount == 0)
            {
                return GameWIP::Status::Types::Status{};
            }

            UINT nativeIndex = 0;
            const HRESULT result = dialog.GetFileTypeIndex(&nativeIndex);

            if(FAILED(result))
            {
                return statusFromHResult(result);
            }

            if(nativeIndex >= 1 && nativeIndex <= filterCount)
            {
                selected = static_cast<std::size_t>(nativeIndex - 1);
            }

            return GameWIP::Status::Types::Status{};
        }

#if DESKTOP_INTERNAL_TEST_HOOKS
        /// @brief Reuses native preparation rules for deterministic test-hook dialog responses.
        [[nodiscard]] GameWIP::Status::Types::Status prepareSimulatedFileDialog(
            Win32TestHooks::FileDialogOperation operation,
            std::string_view title,
            std::span<const Types::Dialogs::File::Filter> filters,
            std::optional<std::size_t> preferredFilterIndex,
            const FileSystem::Types::Path &suggestedDirectory,
            std::string_view suggestedFileName,
            std::string_view suggestedExtension) noexcept
        {
            if(Internal::consumeFailure(GameWIP::Desktop::Internal::TestHooks::FailurePoint::Allocation))
            {
                return GameWIP::Desktop::Internal::statusFor(ErrorCode::OutOfMemory);
            }

            try
            {
                Win32TestHooks::FileDialogSnapshot snapshot;
                snapshot.operation = operation;
                snapshot.preferredFilterIndex = preferredFilterIndex;
                snapshot.suggestedDirectory = suggestedDirectory;
                GameWIP::Status::Types::Status status = convertText(title, snapshot.title);

                if(!status.ok())
                {
                    return status;
                }
                NativeFilters nativeFilters;
                status = makeFilters(filters, nativeFilters);

                if(!status.ok())
                {
                    return status;
                }
                snapshot.filterNames = std::move(nativeFilters.names);
                snapshot.filterPatterns = std::move(nativeFilters.patterns);
                status = convertText(suggestedFileName, snapshot.suggestedFileName);

                if(!status.ok())
                {
                    return status;
                }

                status = convertText(suggestedExtension, snapshot.suggestedExtension);

                if(!status.ok())
                {
                    return status;
                }

                if(!suggestedDirectory.empty() && std::wstring_view(suggestedDirectory.native()).contains(L'\0'))
                {
                    return GameWIP::Desktop::Internal::statusFor(ErrorCode::InvalidArgument);
                }

                if(Internal::consumeFailure(GameWIP::Desktop::Internal::TestHooks::FailurePoint::DialogSuggestedDirectory))
                {
                    return GameWIP::Desktop::Internal::statusFor(ErrorCode::NativeFailure);
                }
                Internal::recordFileDialogSnapshot(std::move(snapshot));
                return GameWIP::Status::Types::Status{};
            }
            catch(const std::bad_alloc &)
            {
                return GameWIP::Desktop::Internal::statusFor(ErrorCode::OutOfMemory);
            }
            catch(...)
            {
                return GameWIP::Desktop::Internal::statusFor(ErrorCode::Unknown);
            }
        }

        /// @brief Completes one deterministic single-file response after shared validation.
        [[nodiscard]] FileDialogOperation completeSimulatedSingleFileDialog(
            Win32TestHooks::FileDialogOperation operation,
            const Types::Dialogs::File::OpenDescription &description,
            Win32TestHooks::FileDialogResponse response,
            bool folder) noexcept
        {
            FileDialogOperation output;
            output.status = prepareSimulatedFileDialog(
                operation,
                description.title,
                description.filters,
                description.preferredFilterIndex,
                description.suggestedDirectory,
                {},
                {});

            if(!output.status.ok())
            {
                return output;
            }

            if(Internal::consumeFailure(GameWIP::Desktop::Internal::TestHooks::FailurePoint::DialogNativeOperation))
            {
                output.status = GameWIP::Desktop::Internal::statusFor(ErrorCode::NativeFailure);
                return output;
            }

            if(!response.accepted)
            {
                output.status = GameWIP::Status::Types::Status{};
                return output;
            }

            if(Internal::consumeFailure(GameWIP::Desktop::Internal::TestHooks::FailurePoint::DialogResult) || response.paths.size() != 1)
            {
                output.status = GameWIP::Desktop::Internal::statusFor(ErrorCode::NativeFailure);
                return output;
            }
            output.path = std::move(response.paths.front());

            if(!folder && !description.filters.empty() && response.nativeFilterIndex && *response.nativeFilterIndex >= 1 &&
               *response.nativeFilterIndex <= description.filters.size())
            {
                output.selectedFilterIndex = *response.nativeFilterIndex - 1;
            }
            output.status = GameWIP::Status::Types::Status{};
            output.outcome = Types::Dialogs::Outcome::Accepted;
            return output;
        }

        /// @brief Completes a deterministic multi-selection or folder response.
        [[nodiscard]] FileDialogListOperation completeSimulatedMultipleFileDialog(
            Win32TestHooks::FileDialogOperation operation,
            const Types::Dialogs::File::OpenDescription &description,
            Win32TestHooks::FileDialogResponse response,
            bool folder) noexcept
        {
            FileDialogListOperation output;
            output.status = prepareSimulatedFileDialog(
                operation,
                description.title,
                description.filters,
                description.preferredFilterIndex,
                description.suggestedDirectory,
                {},
                {});

            if(!output.status.ok())
            {
                return output;
            }

            if(Internal::consumeFailure(GameWIP::Desktop::Internal::TestHooks::FailurePoint::DialogNativeOperation))
            {
                output.status = GameWIP::Desktop::Internal::statusFor(ErrorCode::NativeFailure);
                return output;
            }

            if(!response.accepted)
            {
                output.status = GameWIP::Status::Types::Status{};
                return output;
            }

            if(Internal::consumeFailure(GameWIP::Desktop::Internal::TestHooks::FailurePoint::DialogResult) || response.paths.empty())
            {
                output.status = GameWIP::Desktop::Internal::statusFor(ErrorCode::NativeFailure);
                return output;
            }
            output.paths = std::move(response.paths);

            if(!folder && !description.filters.empty() && response.nativeFilterIndex && *response.nativeFilterIndex >= 1 &&
               *response.nativeFilterIndex <= description.filters.size())
            {
                output.selectedFilterIndex = *response.nativeFilterIndex - 1;
            }
            output.status = GameWIP::Status::Types::Status{};
            output.outcome = Types::Dialogs::Outcome::Accepted;
            return output;
        }

        /// @brief Completes a deterministic save response while preserving filter selection.
        [[nodiscard]] FileDialogOperation completeSimulatedSaveFileDialog(
            const Types::Dialogs::File::SaveDescription &description,
            Win32TestHooks::FileDialogResponse response) noexcept
        {
            FileDialogOperation output;
            output.status = prepareSimulatedFileDialog(
                Win32TestHooks::FileDialogOperation::SaveFile,
                description.title,
                description.filters,
                description.preferredFilterIndex,
                description.suggestedDirectory,
                description.suggestedFileName,
                description.suggestedExtension);

            if(!output.status.ok())
            {
                return output;
            }

            if(Internal::consumeFailure(GameWIP::Desktop::Internal::TestHooks::FailurePoint::DialogNativeOperation))
            {
                output.status = GameWIP::Desktop::Internal::statusFor(ErrorCode::NativeFailure);
                return output;
            }

            if(!response.accepted)
            {
                output.status = GameWIP::Status::Types::Status{};
                return output;
            }

            if(Internal::consumeFailure(GameWIP::Desktop::Internal::TestHooks::FailurePoint::DialogResult) || response.paths.size() != 1)
            {
                output.status = GameWIP::Desktop::Internal::statusFor(ErrorCode::NativeFailure);
                return output;
            }
            output.path = std::move(response.paths.front());

            if(!description.filters.empty() && response.nativeFilterIndex && *response.nativeFilterIndex >= 1 &&
               *response.nativeFilterIndex <= description.filters.size())
            {
                output.selectedFilterIndex = *response.nativeFilterIndex - 1;
            }
            output.status = GameWIP::Status::Types::Status{};
            output.outcome = Types::Dialogs::Outcome::Accepted;
            return output;
        }

#endif

        /// @brief Extracts one Shell item filesystem path into the portable Path type.
        [[nodiscard]] GameWIP::Status::Types::Status extractPath(IShellItem &item, FileSystem::Types::Path &path) noexcept
        {
            CoTaskMemString native;
            const HRESULT result = item.GetDisplayName(SIGDN_FILESYSPATH, &native.value);

            if(FAILED(result))
            {
                return statusFromHResult(result);
            }

            if(native.value == nullptr)
            {
                return GameWIP::Desktop::Internal::statusFor(ErrorCode::NativeFailure);
            }

            try
            {
                FileSystem::Types::Path candidate(native.value);
                path = std::move(candidate);
                return GameWIP::Status::Types::Status{};
            }
            catch(const std::bad_alloc &)
            {
                return GameWIP::Desktop::Internal::statusFor(ErrorCode::OutOfMemory);
            }
            catch(...)
            {
                return GameWIP::Desktop::Internal::statusFor(ErrorCode::Unknown);
            }
        }

        /// @brief Recognizes the native cancellation result as a successful cancel outcome.
        [[nodiscard]] bool isCancellation(HRESULT result) noexcept
        {
            return result == HRESULT_FROM_WIN32(ERROR_CANCELLED);
        }

        /// @brief Presents one native open or folder-selection dialog and maps its result.
        [[nodiscard]] FileDialogOperation singleOpen(const Types::Dialogs::File::OpenDescription &description, bool folder) noexcept
        {
            FileDialogOperation output;
            ApartmentLease apartment;

            if(!apartment.status().ok())
            {
                output.status = apartment.status();
                return output;
            }

            ComPtr<IFileOpenDialog> dialog;
            HRESULT result = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.put()));

            if(FAILED(result))
            {
                output.status = statusFromHResult(result);
                return output;
            }

            if(!dialog)
            {
                output.status = GameWIP::Desktop::Internal::statusFor(ErrorCode::NativeFailure);
                return output;
            }

            NativeFilters filters;
            DWORD options = FOS_PATHMUSTEXIST;

            if(folder)
            {
                options |= FOS_PICKFOLDERS;
            }
            else
            {
                options |= FOS_FILEMUSTEXIST;
            }
            output.status = configureCommon(
                *dialog.get(),
                description.title,
                description.suggestedDirectory,
                description.filters,
                description.preferredFilterIndex,
                options,
                filters);

            if(!output.status.ok())
            {
                return output;
            }

            result = dialog->Show(ownerHandle(description.owner));

            if(isCancellation(result))
            {
                output.status = GameWIP::Status::Types::Status{};
                return output;
            }

            if(FAILED(result))
            {
                output.status = statusFromHResult(result);
                return output;
            }

            ComPtr<IShellItem> item;
            result = dialog->GetResult(item.put());

            if(FAILED(result))
            {
                output.status = statusFromHResult(result);
                return output;
            }

            if(!item)
            {
                output.status = GameWIP::Desktop::Internal::statusFor(ErrorCode::NativeFailure);
                return output;
            }

            output.status = extractPath(*item.get(), output.path);

            if(!output.status.ok())
            {
                output.path.clear();
                return output;
            }

            if(!folder)
            {
                output.status = selectedFilter(*dialog.get(), description.filters.size(), output.selectedFilterIndex);

                if(!output.status.ok())
                {
                    output.path.clear();
                    output.selectedFilterIndex.reset();
                    return output;
                }
            }
            output.outcome = Types::Dialogs::Outcome::Accepted;
            return output;
        }

        /// @brief Presents a native multi-open or multi-folder dialog and maps its result.
        [[nodiscard]] FileDialogListOperation multipleOpen(const Types::Dialogs::File::OpenDescription &description, bool folder) noexcept
        {
            FileDialogListOperation output;
            ApartmentLease apartment;

            if(!apartment.status().ok())
            {
                output.status = apartment.status();
                return output;
            }

            ComPtr<IFileOpenDialog> dialog;
            HRESULT result = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.put()));

            if(FAILED(result))
            {
                output.status = statusFromHResult(result);
                return output;
            }

            if(!dialog)
            {
                output.status = GameWIP::Desktop::Internal::statusFor(ErrorCode::NativeFailure);
                return output;
            }

            NativeFilters filters;
            DWORD options = FOS_PATHMUSTEXIST | FOS_ALLOWMULTISELECT;
            options |= folder ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST;
            output.status = configureCommon(
                *dialog.get(),
                description.title,
                description.suggestedDirectory,
                description.filters,
                description.preferredFilterIndex,
                options,
                filters);

            if(!output.status.ok())
            {
                return output;
            }

            result = dialog->Show(ownerHandle(description.owner));

            if(isCancellation(result))
            {
                output.status = GameWIP::Status::Types::Status{};
                return output;
            }

            if(FAILED(result))
            {
                output.status = statusFromHResult(result);
                return output;
            }

            ComPtr<IShellItemArray> items;
            result = dialog->GetResults(items.put());

            if(FAILED(result))
            {
                output.status = statusFromHResult(result);
                return output;
            }

            if(!items)
            {
                output.status = GameWIP::Desktop::Internal::statusFor(ErrorCode::NativeFailure);
                return output;
            }

            DWORD count = 0;
            result = items->GetCount(&count);

            if(FAILED(result))
            {
                output.status = statusFromHResult(result);
                return output;
            }

            if(count == 0)
            {
                output.status = GameWIP::Desktop::Internal::statusFor(ErrorCode::NativeFailure);
                return output;
            }

            try
            {
                std::vector<FileSystem::Types::Path> paths;
                paths.reserve(count);

                for(DWORD index = 0; index < count; ++index)
                {
                    ComPtr<IShellItem> item;
                    result = items->GetItemAt(index, item.put());

                    if(FAILED(result))
                    {
                        output.status = statusFromHResult(result);
                        return output;
                    }

                    if(!item)
                    {
                        output.status = GameWIP::Desktop::Internal::statusFor(ErrorCode::NativeFailure);
                        return output;
                    }
                    FileSystem::Types::Path path;
                    output.status = extractPath(*item.get(), path);

                    if(!output.status.ok())
                    {
                        return output;
                    }
                    paths.push_back(std::move(path));
                }

                if(!folder)
                {
                    output.status = selectedFilter(*dialog.get(), description.filters.size(), output.selectedFilterIndex);

                    if(!output.status.ok())
                    {
                        output.selectedFilterIndex.reset();
                        return output;
                    }
                }
                output.paths = std::move(paths);
                output.outcome = Types::Dialogs::Outcome::Accepted;
                return output;
            }
            catch(const std::bad_alloc &)
            {
                output.status = GameWIP::Desktop::Internal::statusFor(ErrorCode::OutOfMemory);
            }
            catch(...)
            {
                output.status = GameWIP::Desktop::Internal::statusFor(ErrorCode::Unknown);
            }

            return output;
        }

    } // namespace

    /// ------------------------------------------------------------
    /// File and folder dialogs
    /// ------------------------------------------------------------

    FileDialogResult openFile(const Types::Dialogs::File::OpenDescription &description) noexcept
    {
        try
        {
#if DESKTOP_INTERNAL_TEST_HOOKS
            Win32TestHooks::FileDialogResponse response;

            if(Internal::consumeFileDialogResponse(Win32TestHooks::FileDialogOperation::OpenFile, response))
            {
                return sharedResult(
                    completeSimulatedSingleFileDialog(Win32TestHooks::FileDialogOperation::OpenFile, description, std::move(response), false));
            }
#endif
            return sharedResult(singleOpen(description, false));
        }
        catch(const std::bad_alloc &)
        {
            return FileDialogResult::failure(GameWIP::Desktop::Internal::statusFor(ErrorCode::OutOfMemory));
        }
        catch(...)
        {
            return FileDialogResult::failure(GameWIP::Desktop::Internal::statusFor(ErrorCode::Unknown));
        }
    }

    FileDialogListResult openFiles(const Types::Dialogs::File::OpenDescription &description) noexcept
    {
        try
        {
#if DESKTOP_INTERNAL_TEST_HOOKS
            Win32TestHooks::FileDialogResponse response;

            if(Internal::consumeFileDialogResponse(Win32TestHooks::FileDialogOperation::OpenFiles, response))
            {
                return sharedResult(
                    completeSimulatedMultipleFileDialog(Win32TestHooks::FileDialogOperation::OpenFiles, description, std::move(response), false));
            }
#endif
            return sharedResult(multipleOpen(description, false));
        }
        catch(const std::bad_alloc &)
        {
            return FileDialogListResult::failure(GameWIP::Desktop::Internal::statusFor(ErrorCode::OutOfMemory));
        }
        catch(...)
        {
            return FileDialogListResult::failure(GameWIP::Desktop::Internal::statusFor(ErrorCode::Unknown));
        }
    }

    FileDialogResult saveFile(const Types::Dialogs::File::SaveDescription &description) noexcept
    {
        FileDialogOperation output;
        try
        {
#if DESKTOP_INTERNAL_TEST_HOOKS
            Win32TestHooks::FileDialogResponse response;

            if(Internal::consumeFileDialogResponse(Win32TestHooks::FileDialogOperation::SaveFile, response))
            {
                return sharedResult(completeSimulatedSaveFileDialog(description, std::move(response)));
            }
#endif
            ApartmentLease apartment;

            if(!apartment.status().ok())
            {
                output.status = apartment.status();
                return sharedResult(std::move(output));
            }
            ComPtr<IFileSaveDialog> dialog;
            HRESULT result = CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.put()));

            if(FAILED(result))
            {
                output.status = statusFromHResult(result);
                return sharedResult(std::move(output));
            }

            if(!dialog)
            {
                output.status = GameWIP::Desktop::Internal::statusFor(ErrorCode::NativeFailure);
                return sharedResult(std::move(output));
            }
            NativeFilters filters;
            output.status = configureCommon(
                *dialog.get(),
                description.title,
                description.suggestedDirectory,
                description.filters,
                description.preferredFilterIndex,
                FOS_PATHMUSTEXIST | FOS_OVERWRITEPROMPT,
                filters);

            if(!output.status.ok())
            {
                return sharedResult(std::move(output));
            }
            std::wstring fileName;

            if(!description.suggestedFileName.empty())
            {
                output.status = convertText(description.suggestedFileName, fileName);

                if(!output.status.ok())
                {
                    return sharedResult(std::move(output));
                }

                result = dialog->SetFileName(fileName.c_str());

                if(FAILED(result))
                {
                    output.status = statusFromHResult(result);
                    return sharedResult(std::move(output));
                }
            }

            std::wstring extension;

            if(!description.suggestedExtension.empty())
            {
                output.status = convertText(description.suggestedExtension, extension);

                if(!output.status.ok())
                {
                    return sharedResult(std::move(output));
                }

                result = dialog->SetDefaultExtension(extension.c_str());

                if(FAILED(result))
                {
                    output.status = statusFromHResult(result);
                    return sharedResult(std::move(output));
                }
            }

            result = dialog->Show(ownerHandle(description.owner));

            if(isCancellation(result))
            {
                output.status = GameWIP::Status::Types::Status{};
                return sharedResult(std::move(output));
            }

            if(FAILED(result))
            {
                output.status = statusFromHResult(result);
                return sharedResult(std::move(output));
            }
            ComPtr<IShellItem> item;
            result = dialog->GetResult(item.put());

            if(FAILED(result))
            {
                output.status = statusFromHResult(result);
                return sharedResult(std::move(output));
            }

            if(!item)
            {
                output.status = GameWIP::Desktop::Internal::statusFor(ErrorCode::NativeFailure);
                return sharedResult(std::move(output));
            }

            output.status = extractPath(*item.get(), output.path);

            if(!output.status.ok())
            {
                output.path.clear();
                return sharedResult(std::move(output));
            }

            output.status = selectedFilter(*dialog.get(), description.filters.size(), output.selectedFilterIndex);

            if(!output.status.ok())
            {
                output.path.clear();
                output.selectedFilterIndex.reset();
                return sharedResult(std::move(output));
            }
            output.outcome = Types::Dialogs::Outcome::Accepted;
            return sharedResult(std::move(output));
        }
        catch(const std::bad_alloc &)
        {
            output.status = GameWIP::Desktop::Internal::statusFor(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            output.status = GameWIP::Desktop::Internal::statusFor(ErrorCode::Unknown);
        }

        return sharedResult(std::move(output));
    }

    FileDialogResult selectFolder(const Types::Dialogs::File::FolderDescription &description) noexcept
    {
        try
        {
            Types::Dialogs::File::OpenDescription translated{
                .owner = description.owner,
                .title = description.title,
                .suggestedDirectory = description.suggestedDirectory};
#if DESKTOP_INTERNAL_TEST_HOOKS
            Win32TestHooks::FileDialogResponse response;

            if(Internal::consumeFileDialogResponse(Win32TestHooks::FileDialogOperation::SelectFolder, response))
            {
                return sharedResult(
                    completeSimulatedSingleFileDialog(Win32TestHooks::FileDialogOperation::SelectFolder, translated, std::move(response), true));
            }
#endif
            return sharedResult(singleOpen(translated, true));
        }
        catch(const std::bad_alloc &)
        {
            return FileDialogResult::failure(GameWIP::Desktop::Internal::statusFor(ErrorCode::OutOfMemory));
        }
        catch(...)
        {
            return FileDialogResult::failure(GameWIP::Desktop::Internal::statusFor(ErrorCode::Unknown));
        }
    }

    FileDialogListResult selectFolders(const Types::Dialogs::File::FolderDescription &description) noexcept
    {
        try
        {
            Types::Dialogs::File::OpenDescription translated{
                .owner = description.owner,
                .title = description.title,
                .suggestedDirectory = description.suggestedDirectory};
#if DESKTOP_INTERNAL_TEST_HOOKS
            Win32TestHooks::FileDialogResponse response;

            if(Internal::consumeFileDialogResponse(Win32TestHooks::FileDialogOperation::SelectFolders, response))
            {
                return sharedResult(
                    completeSimulatedMultipleFileDialog(Win32TestHooks::FileDialogOperation::SelectFolders, translated, std::move(response), true));
            }
#endif
            return sharedResult(multipleOpen(translated, true));
        }
        catch(const std::bad_alloc &)
        {
            return FileDialogListResult::failure(GameWIP::Desktop::Internal::statusFor(ErrorCode::OutOfMemory));
        }
        catch(...)
        {
            return FileDialogListResult::failure(GameWIP::Desktop::Internal::statusFor(ErrorCode::Unknown));
        }
    }

} // namespace GameWIP::Desktop::Platform
