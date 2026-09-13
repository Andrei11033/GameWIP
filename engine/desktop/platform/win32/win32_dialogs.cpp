/// @file win32_dialogs.cpp
/// @brief Win32 shell file dialogs and TaskDialog one-shot presentations.

#include "desktop/internal/dialogs_platform.h"

#include "desktop/internal/desktop_test_hooks.h"
#include "desktop/internal/window_state.h"
#include "desktop/platform/win32/internal/win32_window_backend.h"

#include <commctrl.h>
#include <objbase.h>
#include <shobjidl.h>

#include <limits>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace GameWIP::Desktop::Detail::Platform
{
    namespace
    {
        using IO::Types::ErrorCode;

        template <typename Interface> class ComPtr final
        {
        public:
            ComPtr() noexcept = default;
            ComPtr(const ComPtr &) = delete;
            ComPtr &operator=(const ComPtr &) = delete;
            ~ComPtr() noexcept
            {
                reset();
            }

            [[nodiscard]] Interface *get() const noexcept
            {
                return value_;
            }
            [[nodiscard]] Interface **put() noexcept
            {
                reset();
                return &value_;
            }
            Interface *operator->() const noexcept
            {
                return value_;
            }
            explicit operator bool() const noexcept
            {
                return value_ != nullptr;
            }
            void reset() noexcept
            {
                if (value_ != nullptr)
                {
                    value_->Release();
                    value_ = nullptr;
                }
            }

        private:
            Interface *value_ = nullptr;
        };

        class CoTaskMemString final
        {
        public:
            ~CoTaskMemString() noexcept
            {
                CoTaskMemFree(value);
            }
            PWSTR value = nullptr;
        };

        [[nodiscard]] IO::Types::Status statusFromHResult(HRESULT result) noexcept
        {
            ErrorCode code = ErrorCode::NativeFailure;
            if (result == E_OUTOFMEMORY)
            {
                code = ErrorCode::OutOfMemory;
            }
            else if (result == E_INVALIDARG)
            {
                code = ErrorCode::InvalidArgument;
            }
            else if (result == E_ACCESSDENIED)
            {
                code = ErrorCode::PermissionDenied;
            }
            else if (result == RPC_E_CHANGED_MODE)
            {
                code = ErrorCode::ResourceBusy;
            }
            else if (result == E_NOTIMPL || result == HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED))
            {
                code = ErrorCode::Unsupported;
            }
            return IO::makeStatus(code, static_cast<std::int64_t>(result));
        }

        class ApartmentLease final
        {
        public:
            ApartmentLease() noexcept
            {
                APTTYPE type = APTTYPE_CURRENT;
                APTTYPEQUALIFIER qualifier = APTTYPEQUALIFIER_NONE;
                HRESULT result = CoGetApartmentType(&type, &qualifier);
                if (SUCCEEDED(result))
                {
                    if (type == APTTYPE_STA || type == APTTYPE_MAINSTA)
                    {
                        status_ = IO::successStatus();
                    }
                    else
                    {
                        status_ = IO::makeStatus(ErrorCode::ResourceBusy, static_cast<std::int64_t>(RPC_E_CHANGED_MODE));
                    }
                    return;
                }
                if (result != CO_E_NOTINITIALIZED)
                {
                    status_ = statusFromHResult(result);
                    return;
                }

                result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
                if (SUCCEEDED(result))
                {
                    ownsInitialization_ = true;
                    status_ = IO::successStatus();
                }
                else
                {
                    status_ = statusFromHResult(result);
                }
            }

            ~ApartmentLease() noexcept
            {
                if (ownsInitialization_)
                {
                    CoUninitialize();
                }
            }

            [[nodiscard]] const IO::Types::Status &status() const noexcept
            {
                return status_;
            }

        private:
            IO::Types::Status status_;
            bool ownsInitialization_ = false;
        };

        [[nodiscard]] HWND ownerHandle(Window *owner) noexcept
        {
            if (owner == nullptr)
            {
                return nullptr;
            }
            WindowState *state = WindowAccess::state(*owner);
            return state != nullptr ? static_cast<HWND>(nativeHandle(*state).window) : nullptr;
        }

        [[nodiscard]] IO::Types::Status convertText(std::string_view text, std::wstring &output) noexcept
        {
            try
            {
#if DESKTOP_INTERNAL_TEST_HOOKS
                if (Detail::consumeFailure(TestHooks::FailurePoint::DialogConversion))
                {
                    return IO::makeStatus(ErrorCode::EncodingFailed);
                }
#endif
                DWORD nativeCode = ERROR_SUCCESS;
                if (!utf8ToUtf16(text, output, nativeCode))
                {
                    return IO::makeStatus(ErrorCode::EncodingFailed, nativeCode);
                }
                return IO::successStatus();
            }
            catch (const std::bad_alloc &)
            {
                return IO::makeStatus(ErrorCode::OutOfMemory);
            }
            catch (...)
            {
                return IO::makeStatus(ErrorCode::Unknown);
            }
        }

        [[nodiscard]] IO::Types::Status setSuggestedDirectory(IFileDialog &dialog, const FileSystem::Types::Path &directory) noexcept
        {
            if (directory.empty())
            {
                return IO::successStatus();
            }
            try
            {
                const auto &native = directory.native();
                if (std::wstring_view(native).contains(L'\0'))
                {
                    return IO::makeStatus(ErrorCode::InvalidArgument);
                }
                ComPtr<IShellItem> item;
                const HRESULT created = SHCreateItemFromParsingName(native.c_str(), nullptr, IID_PPV_ARGS(item.put()));
                if (FAILED(created))
                {
                    return statusFromHResult(created);
                }
                if (!item)
                {
                    return IO::makeStatus(ErrorCode::NativeFailure);
                }
                const HRESULT applied = dialog.SetDefaultFolder(item.get());
                return SUCCEEDED(applied) ? IO::successStatus() : statusFromHResult(applied);
            }
            catch (const std::bad_alloc &)
            {
                return IO::makeStatus(ErrorCode::OutOfMemory);
            }
            catch (...)
            {
                return IO::makeStatus(ErrorCode::Unknown);
            }
        }

        struct NativeFilters
        {
            std::vector<std::wstring> names;
            std::vector<std::wstring> patterns;
            std::vector<COMDLG_FILTERSPEC> specifications;
        };

        [[nodiscard]] IO::Types::Status makeFilters(std::span<const Types::Dialogs::File::Filter> filters, NativeFilters &native) noexcept
        {
            if (filters.size() > std::numeric_limits<UINT>::max())
            {
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }
            try
            {
                native.names.reserve(filters.size());
                native.patterns.reserve(filters.size());
                for (const auto &filter : filters)
                {
                    std::wstring name;
                    IO::Types::Status status = convertText(filter.name, name);
                    if (!status.ok())
                    {
                        return status;
                    }
                    native.names.push_back(std::move(name));

                    std::wstring pattern;
                    if (filter.extensions.empty())
                    {
                        pattern = L"*.*";
                    }
                    else
                    {
                        for (std::size_t index = 0; index < filter.extensions.size(); ++index)
                        {
                            std::wstring extension;
                            status = convertText(filter.extensions[index], extension);
                            if (!status.ok())
                            {
                                return status;
                            }
                            if (index != 0)
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
                for (std::size_t index = 0; index < filters.size(); ++index)
                {
                    native.specifications.push_back({native.names[index].c_str(), native.patterns[index].c_str()});
                }
                return IO::successStatus();
            }
            catch (const std::bad_alloc &)
            {
                return IO::makeStatus(ErrorCode::OutOfMemory);
            }
            catch (...)
            {
                return IO::makeStatus(ErrorCode::Unknown);
            }
        }

        [[nodiscard]] IO::Types::Status configureCommon(
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
            if (FAILED(result))
            {
                return statusFromHResult(result);
            }
            result = dialog.SetOptions(static_cast<FILEOPENDIALOGOPTIONS>(static_cast<DWORD>(options) | requestedOptions | FOS_FORCEFILESYSTEM));
            if (FAILED(result))
            {
                return statusFromHResult(result);
            }

            if (!title.empty())
            {
                std::wstring wideTitle;
                IO::Types::Status status = convertText(title, wideTitle);
                if (!status.ok())
                {
                    return status;
                }
                result = dialog.SetTitle(wideTitle.c_str());
                if (FAILED(result))
                {
                    return statusFromHResult(result);
                }
            }

            IO::Types::Status status = setSuggestedDirectory(dialog, suggestedDirectory);
            if (!status.ok())
            {
                return status;
            }
            status = makeFilters(filters, nativeFilters);
            if (!status.ok())
            {
                return status;
            }
            if (!nativeFilters.specifications.empty())
            {
                result = dialog.SetFileTypes(static_cast<UINT>(nativeFilters.specifications.size()), nativeFilters.specifications.data());
                if (FAILED(result))
                {
                    return statusFromHResult(result);
                }
                if (preferredFilterIndex)
                {
                    result = dialog.SetFileTypeIndex(static_cast<UINT>(*preferredFilterIndex + 1));
                    if (FAILED(result))
                    {
                        return statusFromHResult(result);
                    }
                }
            }
            return IO::successStatus();
        }

        [[nodiscard]] IO::Types::Status selectedFilter(IFileDialog &dialog, std::size_t filterCount, std::optional<std::size_t> &selected) noexcept
        {
            selected.reset();
            if (filterCount == 0)
            {
                return IO::successStatus();
            }
            UINT nativeIndex = 0;
            const HRESULT result = dialog.GetFileTypeIndex(&nativeIndex);
            if (FAILED(result))
            {
                return statusFromHResult(result);
            }
            if (nativeIndex >= 1 && nativeIndex <= filterCount)
            {
                selected = static_cast<std::size_t>(nativeIndex - 1);
            }
            return IO::successStatus();
        }

#if DESKTOP_INTERNAL_TEST_HOOKS
        [[nodiscard]] IO::Types::Status prepareSimulatedFileDialog(
            TestHooks::FileDialogOperation operation,
            std::string_view title,
            std::span<const Types::Dialogs::File::Filter> filters,
            std::optional<std::size_t> preferredFilterIndex,
            const FileSystem::Types::Path &suggestedDirectory,
            std::string_view suggestedFileName,
            std::string_view suggestedExtension) noexcept
        {
            if (Detail::consumeFailure(TestHooks::FailurePoint::Allocation))
            {
                return IO::makeStatus(ErrorCode::OutOfMemory);
            }

            try
            {
                TestHooks::FileDialogSnapshot snapshot;
                snapshot.operation = operation;
                snapshot.preferredFilterIndex = preferredFilterIndex;
                snapshot.suggestedDirectory = suggestedDirectory;
                IO::Types::Status status = convertText(title, snapshot.title);
                if (!status.ok())
                {
                    return status;
                }

                NativeFilters nativeFilters;
                status = makeFilters(filters, nativeFilters);
                if (!status.ok())
                {
                    return status;
                }
                snapshot.filterNames = std::move(nativeFilters.names);
                snapshot.filterPatterns = std::move(nativeFilters.patterns);
                status = convertText(suggestedFileName, snapshot.suggestedFileName);
                if (!status.ok())
                {
                    return status;
                }
                status = convertText(suggestedExtension, snapshot.suggestedExtension);
                if (!status.ok())
                {
                    return status;
                }
                if (!suggestedDirectory.empty() && std::wstring_view(suggestedDirectory.native()).contains(L'\0'))
                {
                    return IO::makeStatus(ErrorCode::InvalidArgument);
                }
                if (Detail::consumeFailure(TestHooks::FailurePoint::DialogSuggestedDirectory))
                {
                    return IO::makeStatus(ErrorCode::NativeFailure);
                }
                Detail::recordFileDialogSnapshot(std::move(snapshot));
                return IO::successStatus();
            }
            catch (const std::bad_alloc &)
            {
                return IO::makeStatus(ErrorCode::OutOfMemory);
            }
            catch (...)
            {
                return IO::makeStatus(ErrorCode::Unknown);
            }
        }

        [[nodiscard]] Types::Dialogs::File::Result completeSimulatedSingleFileDialog(
            TestHooks::FileDialogOperation operation,
            const Types::Dialogs::File::OpenDescription &description,
            TestHooks::FileDialogResponse response,
            bool folder) noexcept
        {
            Types::Dialogs::File::Result output;
            output.status = prepareSimulatedFileDialog(
                operation,
                description.title,
                description.filters,
                description.preferredFilterIndex,
                description.suggestedDirectory,
                {},
                {});
            if (!output.status.ok())
            {
                return output;
            }
            if (Detail::consumeFailure(TestHooks::FailurePoint::DialogNativeOperation))
            {
                output.status = IO::makeStatus(ErrorCode::NativeFailure);
                return output;
            }
            if (!response.accepted)
            {
                output.status = IO::successStatus();
                return output;
            }
            if (Detail::consumeFailure(TestHooks::FailurePoint::DialogResult) || response.paths.size() != 1)
            {
                output.status = IO::makeStatus(ErrorCode::NativeFailure);
                return output;
            }
            output.path = std::move(response.paths.front());
            if (!folder && !description.filters.empty() && response.nativeFilterIndex && *response.nativeFilterIndex >= 1 &&
                *response.nativeFilterIndex <= description.filters.size())
            {
                output.selectedFilterIndex = *response.nativeFilterIndex - 1;
            }
            output.status = IO::successStatus();
            output.outcome = Types::Dialogs::Outcome::Accepted;
            return output;
        }

        [[nodiscard]] Types::Dialogs::File::ListResult completeSimulatedMultipleFileDialog(
            TestHooks::FileDialogOperation operation,
            const Types::Dialogs::File::OpenDescription &description,
            TestHooks::FileDialogResponse response,
            bool folder) noexcept
        {
            Types::Dialogs::File::ListResult output;
            output.status = prepareSimulatedFileDialog(
                operation,
                description.title,
                description.filters,
                description.preferredFilterIndex,
                description.suggestedDirectory,
                {},
                {});
            if (!output.status.ok())
            {
                return output;
            }
            if (Detail::consumeFailure(TestHooks::FailurePoint::DialogNativeOperation))
            {
                output.status = IO::makeStatus(ErrorCode::NativeFailure);
                return output;
            }
            if (!response.accepted)
            {
                output.status = IO::successStatus();
                return output;
            }
            if (Detail::consumeFailure(TestHooks::FailurePoint::DialogResult) || response.paths.empty())
            {
                output.status = IO::makeStatus(ErrorCode::NativeFailure);
                return output;
            }
            output.paths = std::move(response.paths);
            if (!folder && !description.filters.empty() && response.nativeFilterIndex && *response.nativeFilterIndex >= 1 &&
                *response.nativeFilterIndex <= description.filters.size())
            {
                output.selectedFilterIndex = *response.nativeFilterIndex - 1;
            }
            output.status = IO::successStatus();
            output.outcome = Types::Dialogs::Outcome::Accepted;
            return output;
        }

        [[nodiscard]] Types::Dialogs::File::Result completeSimulatedSaveFileDialog(
            const Types::Dialogs::File::SaveDescription &description,
            TestHooks::FileDialogResponse response) noexcept
        {
            Types::Dialogs::File::Result output;
            output.status = prepareSimulatedFileDialog(
                TestHooks::FileDialogOperation::SaveFile,
                description.title,
                description.filters,
                description.preferredFilterIndex,
                description.suggestedDirectory,
                description.suggestedFileName,
                description.suggestedExtension);
            if (!output.status.ok())
            {
                return output;
            }
            if (Detail::consumeFailure(TestHooks::FailurePoint::DialogNativeOperation))
            {
                output.status = IO::makeStatus(ErrorCode::NativeFailure);
                return output;
            }
            if (!response.accepted)
            {
                output.status = IO::successStatus();
                return output;
            }
            if (Detail::consumeFailure(TestHooks::FailurePoint::DialogResult) || response.paths.size() != 1)
            {
                output.status = IO::makeStatus(ErrorCode::NativeFailure);
                return output;
            }
            output.path = std::move(response.paths.front());
            if (!description.filters.empty() && response.nativeFilterIndex && *response.nativeFilterIndex >= 1 &&
                *response.nativeFilterIndex <= description.filters.size())
            {
                output.selectedFilterIndex = *response.nativeFilterIndex - 1;
            }
            output.status = IO::successStatus();
            output.outcome = Types::Dialogs::Outcome::Accepted;
            return output;
        }
#endif

        [[nodiscard]] IO::Types::Status extractPath(IShellItem &item, FileSystem::Types::Path &path) noexcept
        {
            CoTaskMemString native;
            const HRESULT result = item.GetDisplayName(SIGDN_FILESYSPATH, &native.value);
            if (FAILED(result))
            {
                return statusFromHResult(result);
            }
            if (native.value == nullptr)
            {
                return IO::makeStatus(ErrorCode::NativeFailure);
            }
            try
            {
                FileSystem::Types::Path candidate(native.value);
                path = std::move(candidate);
                return IO::successStatus();
            }
            catch (const std::bad_alloc &)
            {
                return IO::makeStatus(ErrorCode::OutOfMemory);
            }
            catch (...)
            {
                return IO::makeStatus(ErrorCode::Unknown);
            }
        }

        [[nodiscard]] bool isCancellation(HRESULT result) noexcept
        {
            return result == HRESULT_FROM_WIN32(ERROR_CANCELLED);
        }

        [[nodiscard]] Types::Dialogs::File::Result singleOpen(const Types::Dialogs::File::OpenDescription &description, bool folder) noexcept
        {
            Types::Dialogs::File::Result output;
            ApartmentLease apartment;
            if (!apartment.status().ok())
            {
                output.status = apartment.status();
                return output;
            }

            ComPtr<IFileOpenDialog> dialog;
            HRESULT result = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.put()));
            if (FAILED(result))
            {
                output.status = statusFromHResult(result);
                return output;
            }
            if (!dialog)
            {
                output.status = IO::makeStatus(ErrorCode::NativeFailure);
                return output;
            }

            NativeFilters filters;
            DWORD options = FOS_PATHMUSTEXIST;
            if (folder)
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
            if (!output.status.ok())
            {
                return output;
            }

            result = dialog->Show(ownerHandle(description.owner));
            if (isCancellation(result))
            {
                output.status = IO::successStatus();
                return output;
            }
            if (FAILED(result))
            {
                output.status = statusFromHResult(result);
                return output;
            }
            ComPtr<IShellItem> item;
            result = dialog->GetResult(item.put());
            if (FAILED(result))
            {
                output.status = statusFromHResult(result);
                return output;
            }
            if (!item)
            {
                output.status = IO::makeStatus(ErrorCode::NativeFailure);
                return output;
            }
            output.status = extractPath(*item.get(), output.path);
            if (!output.status.ok())
            {
                output.path.clear();
                return output;
            }
            if (!folder)
            {
                output.status = selectedFilter(*dialog.get(), description.filters.size(), output.selectedFilterIndex);
                if (!output.status.ok())
                {
                    output.path.clear();
                    output.selectedFilterIndex.reset();
                    return output;
                }
            }
            output.outcome = Types::Dialogs::Outcome::Accepted;
            return output;
        }

        [[nodiscard]] Types::Dialogs::File::ListResult multipleOpen(const Types::Dialogs::File::OpenDescription &description, bool folder) noexcept
        {
            Types::Dialogs::File::ListResult output;
            ApartmentLease apartment;
            if (!apartment.status().ok())
            {
                output.status = apartment.status();
                return output;
            }

            ComPtr<IFileOpenDialog> dialog;
            HRESULT result = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.put()));
            if (FAILED(result))
            {
                output.status = statusFromHResult(result);
                return output;
            }
            if (!dialog)
            {
                output.status = IO::makeStatus(ErrorCode::NativeFailure);
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
            if (!output.status.ok())
            {
                return output;
            }

            result = dialog->Show(ownerHandle(description.owner));
            if (isCancellation(result))
            {
                output.status = IO::successStatus();
                return output;
            }
            if (FAILED(result))
            {
                output.status = statusFromHResult(result);
                return output;
            }

            ComPtr<IShellItemArray> items;
            result = dialog->GetResults(items.put());
            if (FAILED(result))
            {
                output.status = statusFromHResult(result);
                return output;
            }
            if (!items)
            {
                output.status = IO::makeStatus(ErrorCode::NativeFailure);
                return output;
            }
            DWORD count = 0;
            result = items->GetCount(&count);
            if (FAILED(result))
            {
                output.status = statusFromHResult(result);
                return output;
            }
            if (count == 0)
            {
                output.status = IO::makeStatus(ErrorCode::NativeFailure);
                return output;
            }
            try
            {
                std::vector<FileSystem::Types::Path> paths;
                paths.reserve(count);
                for (DWORD index = 0; index < count; ++index)
                {
                    ComPtr<IShellItem> item;
                    result = items->GetItemAt(index, item.put());
                    if (FAILED(result))
                    {
                        output.status = statusFromHResult(result);
                        return output;
                    }
                    if (!item)
                    {
                        output.status = IO::makeStatus(ErrorCode::NativeFailure);
                        return output;
                    }
                    FileSystem::Types::Path path;
                    output.status = extractPath(*item.get(), path);
                    if (!output.status.ok())
                    {
                        return output;
                    }
                    paths.push_back(std::move(path));
                }
                if (!folder)
                {
                    output.status = selectedFilter(*dialog.get(), description.filters.size(), output.selectedFilterIndex);
                    if (!output.status.ok())
                    {
                        output.selectedFilterIndex.reset();
                        return output;
                    }
                }
                output.paths = std::move(paths);
                output.outcome = Types::Dialogs::Outcome::Accepted;
                return output;
            }
            catch (const std::bad_alloc &)
            {
                output.status = IO::makeStatus(ErrorCode::OutOfMemory);
            }
            catch (...)
            {
                output.status = IO::makeStatus(ErrorCode::Unknown);
            }
            return output;
        }

        [[nodiscard]] PCWSTR severityIcon(Types::Dialogs::Severity severity) noexcept
        {
            using Severity = Types::Dialogs::Severity;
            switch (severity)
            {
            case Severity::Information:
                return TD_INFORMATION_ICON;
            case Severity::Warning:
                return TD_WARNING_ICON;
            case Severity::Error:
                return TD_ERROR_ICON;
            case Severity::None:
                return nullptr;
            }
            return nullptr;
        }

        [[nodiscard]] int nativeMessageButton(Types::Dialogs::Message::Button button) noexcept
        {
            using Button = Types::Dialogs::Message::Button;
            switch (button)
            {
            case Button::Ok:
                return IDOK;
            case Button::Cancel:
                return IDCANCEL;
            case Button::Yes:
                return IDYES;
            case Button::No:
                return IDNO;
            case Button::Retry:
                return IDRETRY;
            case Button::None:
                return 0;
            }
            return 0;
        }

        [[nodiscard]] Types::Dialogs::Message::Button portableMessageButton(int button) noexcept
        {
            using Button = Types::Dialogs::Message::Button;
            switch (button)
            {
            case IDOK:
                return Button::Ok;
            case IDCANCEL:
                return Button::Cancel;
            case IDYES:
                return Button::Yes;
            case IDNO:
                return Button::No;
            case IDRETRY:
                return Button::Retry;
            default:
                return Button::None;
            }
        }

        [[nodiscard]] bool messageHasCancel(Types::Dialogs::Message::Buttons buttons) noexcept
        {
            using Buttons = Types::Dialogs::Message::Buttons;
            return buttons == Buttons::OkCancel || buttons == Buttons::YesNoCancel || buttons == Buttons::RetryCancel;
        }
    } // namespace

    Types::Dialogs::File::Result openFile(const Types::Dialogs::File::OpenDescription &description) noexcept
    {
        try
        {
#if DESKTOP_INTERNAL_TEST_HOOKS
            TestHooks::FileDialogResponse response;
            if (Detail::consumeFileDialogResponse(TestHooks::FileDialogOperation::OpenFile, response))
            {
                return completeSimulatedSingleFileDialog(TestHooks::FileDialogOperation::OpenFile, description, std::move(response), false);
            }
#endif
            return singleOpen(description, false);
        }
        catch (const std::bad_alloc &)
        {
            Types::Dialogs::File::Result result;
            result.status = IO::makeStatus(ErrorCode::OutOfMemory);
            return result;
        }
        catch (...)
        {
            Types::Dialogs::File::Result result;
            result.status = IO::makeStatus(ErrorCode::Unknown);
            return result;
        }
    }

    Types::Dialogs::File::ListResult openFiles(const Types::Dialogs::File::OpenDescription &description) noexcept
    {
        try
        {
#if DESKTOP_INTERNAL_TEST_HOOKS
            TestHooks::FileDialogResponse response;
            if (Detail::consumeFileDialogResponse(TestHooks::FileDialogOperation::OpenFiles, response))
            {
                return completeSimulatedMultipleFileDialog(TestHooks::FileDialogOperation::OpenFiles, description, std::move(response), false);
            }
#endif
            return multipleOpen(description, false);
        }
        catch (const std::bad_alloc &)
        {
            Types::Dialogs::File::ListResult result;
            result.status = IO::makeStatus(ErrorCode::OutOfMemory);
            return result;
        }
        catch (...)
        {
            Types::Dialogs::File::ListResult result;
            result.status = IO::makeStatus(ErrorCode::Unknown);
            return result;
        }
    }

    Types::Dialogs::File::Result saveFile(const Types::Dialogs::File::SaveDescription &description) noexcept
    {
        Types::Dialogs::File::Result output;
        try
        {
#if DESKTOP_INTERNAL_TEST_HOOKS
            TestHooks::FileDialogResponse response;
            if (Detail::consumeFileDialogResponse(TestHooks::FileDialogOperation::SaveFile, response))
            {
                return completeSimulatedSaveFileDialog(description, std::move(response));
            }
#endif
            ApartmentLease apartment;
            if (!apartment.status().ok())
            {
                output.status = apartment.status();
                return output;
            }
            ComPtr<IFileSaveDialog> dialog;
            HRESULT result = CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.put()));
            if (FAILED(result))
            {
                output.status = statusFromHResult(result);
                return output;
            }
            if (!dialog)
            {
                output.status = IO::makeStatus(ErrorCode::NativeFailure);
                return output;
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
            if (!output.status.ok())
            {
                return output;
            }
            std::wstring fileName;
            if (!description.suggestedFileName.empty())
            {
                output.status = convertText(description.suggestedFileName, fileName);
                if (!output.status.ok())
                {
                    return output;
                }
                result = dialog->SetFileName(fileName.c_str());
                if (FAILED(result))
                {
                    output.status = statusFromHResult(result);
                    return output;
                }
            }
            std::wstring extension;
            if (!description.suggestedExtension.empty())
            {
                output.status = convertText(description.suggestedExtension, extension);
                if (!output.status.ok())
                {
                    return output;
                }
                result = dialog->SetDefaultExtension(extension.c_str());
                if (FAILED(result))
                {
                    output.status = statusFromHResult(result);
                    return output;
                }
            }
            result = dialog->Show(ownerHandle(description.owner));
            if (isCancellation(result))
            {
                output.status = IO::successStatus();
                return output;
            }
            if (FAILED(result))
            {
                output.status = statusFromHResult(result);
                return output;
            }
            ComPtr<IShellItem> item;
            result = dialog->GetResult(item.put());
            if (FAILED(result))
            {
                output.status = statusFromHResult(result);
                return output;
            }
            if (!item)
            {
                output.status = IO::makeStatus(ErrorCode::NativeFailure);
                return output;
            }
            output.status = extractPath(*item.get(), output.path);
            if (!output.status.ok())
            {
                output.path.clear();
                return output;
            }
            output.status = selectedFilter(*dialog.get(), description.filters.size(), output.selectedFilterIndex);
            if (!output.status.ok())
            {
                output.path.clear();
                output.selectedFilterIndex.reset();
                return output;
            }
            output.outcome = Types::Dialogs::Outcome::Accepted;
            return output;
        }
        catch (const std::bad_alloc &)
        {
            output.status = IO::makeStatus(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            output.status = IO::makeStatus(ErrorCode::Unknown);
        }
        return output;
    }

    Types::Dialogs::File::Result selectFolder(const Types::Dialogs::File::FolderDescription &description) noexcept
    {
        try
        {
            Types::Dialogs::File::OpenDescription translated{
                .owner = description.owner,
                .title = description.title,
                .suggestedDirectory = description.suggestedDirectory};
#if DESKTOP_INTERNAL_TEST_HOOKS
            TestHooks::FileDialogResponse response;
            if (Detail::consumeFileDialogResponse(TestHooks::FileDialogOperation::SelectFolder, response))
            {
                return completeSimulatedSingleFileDialog(TestHooks::FileDialogOperation::SelectFolder, translated, std::move(response), true);
            }
#endif
            return singleOpen(translated, true);
        }
        catch (const std::bad_alloc &)
        {
            Types::Dialogs::File::Result result;
            result.status = IO::makeStatus(ErrorCode::OutOfMemory);
            return result;
        }
        catch (...)
        {
            Types::Dialogs::File::Result result;
            result.status = IO::makeStatus(ErrorCode::Unknown);
            return result;
        }
    }

    Types::Dialogs::File::ListResult selectFolders(const Types::Dialogs::File::FolderDescription &description) noexcept
    {
        try
        {
            Types::Dialogs::File::OpenDescription translated{
                .owner = description.owner,
                .title = description.title,
                .suggestedDirectory = description.suggestedDirectory};
#if DESKTOP_INTERNAL_TEST_HOOKS
            TestHooks::FileDialogResponse response;
            if (Detail::consumeFileDialogResponse(TestHooks::FileDialogOperation::SelectFolders, response))
            {
                return completeSimulatedMultipleFileDialog(TestHooks::FileDialogOperation::SelectFolders, translated, std::move(response), true);
            }
#endif
            return multipleOpen(translated, true);
        }
        catch (const std::bad_alloc &)
        {
            Types::Dialogs::File::ListResult result;
            result.status = IO::makeStatus(ErrorCode::OutOfMemory);
            return result;
        }
        catch (...)
        {
            Types::Dialogs::File::ListResult result;
            result.status = IO::makeStatus(ErrorCode::Unknown);
            return result;
        }
    }

    Types::Dialogs::Message::Result showMessage(const Types::Dialogs::Message::Description &description) noexcept
    {
        Types::Dialogs::Message::Result output;
        try
        {
#if DESKTOP_INTERNAL_TEST_HOOKS
            TestHooks::MessageDialogResponse simulatedResponse;
            const bool simulated = Detail::consumeMessageDialogResponse(simulatedResponse);
            if (simulated && Detail::consumeFailure(TestHooks::FailurePoint::Allocation))
            {
                output.status = IO::makeStatus(ErrorCode::OutOfMemory);
                return output;
            }
#endif
            std::wstring title;
            std::wstring message;
            output.status = convertText(description.title, title);
            if (!output.status.ok())
            {
                return output;
            }
            output.status = convertText(description.message, message);
            if (!output.status.ok())
            {
                return output;
            }

            TASKDIALOGCONFIG config{};
            config.cbSize = sizeof(config);
            config.hwndParent = ownerHandle(description.owner);
            config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION;
            config.pszWindowTitle = title.c_str();
            config.pszContent = message.c_str();
            config.pszMainIcon = severityIcon(description.severity);
            using Buttons = Types::Dialogs::Message::Buttons;
            switch (description.buttons)
            {
            case Buttons::Ok:
                config.dwCommonButtons = TDCBF_OK_BUTTON;
                break;
            case Buttons::OkCancel:
                config.dwCommonButtons = static_cast<TASKDIALOG_COMMON_BUTTON_FLAGS>(TDCBF_OK_BUTTON | TDCBF_CANCEL_BUTTON);
                break;
            case Buttons::YesNo:
                config.dwCommonButtons = static_cast<TASKDIALOG_COMMON_BUTTON_FLAGS>(TDCBF_YES_BUTTON | TDCBF_NO_BUTTON);
                break;
            case Buttons::YesNoCancel:
                config.dwCommonButtons = static_cast<TASKDIALOG_COMMON_BUTTON_FLAGS>(TDCBF_YES_BUTTON | TDCBF_NO_BUTTON | TDCBF_CANCEL_BUTTON);
                break;
            case Buttons::RetryCancel:
                config.dwCommonButtons = static_cast<TASKDIALOG_COMMON_BUTTON_FLAGS>(TDCBF_RETRY_BUTTON | TDCBF_CANCEL_BUTTON);
                break;
            }
            config.nDefaultButton = nativeMessageButton(description.defaultButton);

            int selected = 0;
            HRESULT result = S_OK;
#if DESKTOP_INTERNAL_TEST_HOOKS
            if (simulated)
            {
                Detail::recordMessageDialogSnapshot(
                    {.title = title,
                     .message = message,
                     .buttons = description.buttons,
                     .defaultButton = description.defaultButton,
                     .severity = description.severity});
                if (Detail::consumeFailure(TestHooks::FailurePoint::DialogNativeOperation))
                {
                    result = E_FAIL;
                }
                else
                {
                    selected = simulatedResponse.dismissed ? IDCANCEL : nativeMessageButton(simulatedResponse.button);
                    if (Detail::consumeFailure(TestHooks::FailurePoint::DialogResult))
                    {
                        selected = std::numeric_limits<int>::min();
                    }
                }
            }
            else
#endif
            {
                result = TaskDialogIndirect(&config, &selected, nullptr, nullptr);
            }
            if (FAILED(result))
            {
                output.status = statusFromHResult(result);
                return output;
            }
            output.status = IO::successStatus();
            if (selected == IDCANCEL)
            {
                output.outcome = Types::Dialogs::Outcome::Cancelled;
                output.button =
                    messageHasCancel(description.buttons) ? Types::Dialogs::Message::Button::Cancel : Types::Dialogs::Message::Button::None;
            }
            else
            {
                output.button = portableMessageButton(selected);
                if (output.button == Types::Dialogs::Message::Button::None)
                {
                    output = {};
                    output.status = IO::makeStatus(ErrorCode::NativeFailure);
                    return output;
                }
                output.outcome = Types::Dialogs::Outcome::Accepted;
            }
        }
        catch (const std::bad_alloc &)
        {
            output.status = IO::makeStatus(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            output.status = IO::makeStatus(ErrorCode::Unknown);
        }
        return output;
    }

    Types::Dialogs::Prompt::Result showPrompt(const Types::Dialogs::Prompt::Description &description) noexcept
    {
        Types::Dialogs::Prompt::Result output;
        try
        {
#if DESKTOP_INTERNAL_TEST_HOOKS
            TestHooks::PromptDialogResponse simulatedResponse;
            const bool simulated = Detail::consumePromptDialogResponse(simulatedResponse);
            if (simulated && Detail::consumeFailure(TestHooks::FailurePoint::Allocation))
            {
                output.status = IO::makeStatus(ErrorCode::OutOfMemory);
                return output;
            }
#endif
            constexpr int nativeBase = 1000;
            if (description.buttons.size() > static_cast<std::size_t>(std::numeric_limits<int>::max() - nativeBase) ||
                description.options.size() > static_cast<std::size_t>(std::numeric_limits<int>::max() - nativeBase) - description.buttons.size())
            {
                output.status = IO::makeStatus(ErrorCode::InvalidArgument);
                return output;
            }

            std::wstring title;
            std::wstring heading;
            std::wstring message;
            std::wstring details;
            std::wstring footer;
            std::wstring verification;
            for (auto pair :
                 {std::pair{description.title, &title},
                  std::pair{description.heading, &heading},
                  std::pair{description.message, &message},
                  std::pair{description.details, &details},
                  std::pair{description.supplementalText, &footer}})
            {
                output.status = convertText(pair.first, *pair.second);
                if (!output.status.ok())
                {
                    return output;
                }
            }
            if (description.checkBox)
            {
                output.status = convertText(description.checkBox->label, verification);
                if (!output.status.ok())
                {
                    return output;
                }
            }

            bool commandLinks = false;
            for (const auto &button : description.buttons)
            {
                commandLinks = commandLinks || !button.description.empty();
            }

            std::vector<std::wstring> buttonText;
            std::vector<TASKDIALOG_BUTTON> nativeButtons;
            buttonText.reserve(description.buttons.size());
            nativeButtons.reserve(description.buttons.size());
            for (std::size_t index = 0; index < description.buttons.size(); ++index)
            {
                std::wstring label;
                std::wstring detail;
                output.status = convertText(description.buttons[index].label, label);
                if (!output.status.ok())
                {
                    return output;
                }
                output.status = convertText(description.buttons[index].description, detail);
                if (!output.status.ok())
                {
                    return output;
                }
                if (!detail.empty())
                {
                    label.push_back(L'\n');
                    label.append(detail);
                }
                buttonText.push_back(std::move(label));
            }
            for (std::size_t index = 0; index < buttonText.size(); ++index)
            {
                nativeButtons.push_back({nativeBase + static_cast<int>(index), buttonText[index].c_str()});
            }

            std::vector<std::wstring> optionText;
            std::vector<TASKDIALOG_BUTTON> nativeOptions;
            optionText.reserve(description.options.size());
            nativeOptions.reserve(description.options.size());
            for (const auto &option : description.options)
            {
                std::wstring label;
                output.status = convertText(option.label, label);
                if (!output.status.ok())
                {
                    return output;
                }
                optionText.push_back(std::move(label));
            }
            const int optionBase = nativeBase + static_cast<int>(description.buttons.size());
            for (std::size_t index = 0; index < optionText.size(); ++index)
            {
                nativeOptions.push_back({optionBase + static_cast<int>(index), optionText[index].c_str()});
            }

            TASKDIALOGCONFIG config{};
            config.cbSize = sizeof(config);
            config.hwndParent = ownerHandle(description.owner);
            DWORD taskFlags = TDF_ALLOW_DIALOG_CANCELLATION;
            if (commandLinks)
            {
                taskFlags |= TDF_USE_COMMAND_LINKS;
            }
            if (!description.defaultOption.isValid())
            {
                taskFlags |= TDF_NO_DEFAULT_RADIO_BUTTON;
            }
            if (description.checkBox && description.checkBox->checked)
            {
                taskFlags |= TDF_VERIFICATION_FLAG_CHECKED;
            }
            config.dwFlags = static_cast<TASKDIALOG_FLAGS>(taskFlags);
            config.pszWindowTitle = title.c_str();
            config.pszMainInstruction = heading.c_str();
            config.pszContent = message.c_str();
            config.pszExpandedInformation = details.c_str();
            config.pszFooter = footer.c_str();
            config.pszMainIcon = severityIcon(description.severity);
            config.cButtons = static_cast<UINT>(nativeButtons.size());
            config.pButtons = nativeButtons.data();
            config.cRadioButtons = static_cast<UINT>(nativeOptions.size());
            config.pRadioButtons = nativeOptions.data();
            config.pszVerificationText = description.checkBox ? verification.c_str() : nullptr;

            for (std::size_t index = 0; index < description.buttons.size(); ++index)
            {
                if (description.buttons[index].id == description.defaultButton)
                {
                    config.nDefaultButton = nativeBase + static_cast<int>(index);
                }
            }
            for (std::size_t index = 0; index < description.options.size(); ++index)
            {
                if (description.options[index].id == description.defaultOption)
                {
                    config.nDefaultRadioButton = optionBase + static_cast<int>(index);
                }
            }

            int selectedButton = IDCANCEL;
            int selectedOption = 0;
            BOOL checked = FALSE;
            HRESULT result = S_OK;
#if DESKTOP_INTERNAL_TEST_HOOKS
            if (simulated)
            {
                TestHooks::PromptDialogSnapshot snapshot;
                snapshot.title = title;
                snapshot.heading = heading;
                snapshot.message = message;
                snapshot.details = details;
                snapshot.supplementalText = footer;
                snapshot.checkBoxLabel = verification;
                snapshot.buttonText = buttonText;
                snapshot.optionText = optionText;
                snapshot.nativeDefaultButton = config.nDefaultButton;
                snapshot.nativeDefaultOption = config.nDefaultRadioButton;
                snapshot.commandLinks = commandLinks;
                snapshot.checkBoxInitiallyChecked = description.checkBox && description.checkBox->checked;
                snapshot.severity = description.severity;
                snapshot.nativeButtonIds.reserve(nativeButtons.size());
                snapshot.nativeOptionIds.reserve(nativeOptions.size());
                for (const TASKDIALOG_BUTTON &button : nativeButtons)
                {
                    snapshot.nativeButtonIds.push_back(button.nButtonID);
                }
                for (const TASKDIALOG_BUTTON &option : nativeOptions)
                {
                    snapshot.nativeOptionIds.push_back(option.nButtonID);
                }
                Detail::recordPromptDialogSnapshot(std::move(snapshot));

                if (Detail::consumeFailure(TestHooks::FailurePoint::DialogNativeOperation))
                {
                    result = E_FAIL;
                }
                else
                {
                    if (!simulatedResponse.dismissed && simulatedResponse.buttonIndex && *simulatedResponse.buttonIndex < description.buttons.size())
                    {
                        selectedButton = nativeBase + static_cast<int>(*simulatedResponse.buttonIndex);
                    }
                    else if (!simulatedResponse.dismissed)
                    {
                        selectedButton = std::numeric_limits<int>::min();
                    }
                    if (simulatedResponse.optionIndex && *simulatedResponse.optionIndex < description.options.size())
                    {
                        selectedOption = optionBase + static_cast<int>(*simulatedResponse.optionIndex);
                    }
                    checked = simulatedResponse.checkBoxChecked ? TRUE : FALSE;
                    if (Detail::consumeFailure(TestHooks::FailurePoint::DialogResult))
                    {
                        selectedButton = std::numeric_limits<int>::min();
                    }
                }
            }
            else
#endif
            {
                result = TaskDialogIndirect(&config, &selectedButton, &selectedOption, &checked);
            }
            if (FAILED(result))
            {
                output.status = statusFromHResult(result);
                return output;
            }

            output.status = IO::successStatus();
            if (selectedOption != 0 && (selectedOption < optionBase || selectedOption >= optionBase + static_cast<int>(description.options.size())))
            {
                output.status = IO::makeStatus(ErrorCode::NativeFailure);
                return output;
            }
            if (selectedOption >= optionBase && selectedOption < optionBase + static_cast<int>(description.options.size()))
            {
                output.option = description.options[static_cast<std::size_t>(selectedOption - optionBase)].id;
            }
            if (description.checkBox)
            {
                output.checkBoxChecked = checked != FALSE;
            }

            if (selectedButton == IDCANCEL)
            {
                output.outcome = Types::Dialogs::Outcome::Cancelled;
                output.button = description.cancelButton;
                return output;
            }
            if (selectedButton >= nativeBase && selectedButton < nativeBase + static_cast<int>(description.buttons.size()))
            {
                output.button = description.buttons[static_cast<std::size_t>(selectedButton - nativeBase)].id;
                output.outcome = output.button == description.cancelButton && description.cancelButton.isValid() ? Types::Dialogs::Outcome::Cancelled
                                                                                                                 : Types::Dialogs::Outcome::Accepted;
                return output;
            }

            output = {};
            output.status = IO::makeStatus(ErrorCode::NativeFailure);
        }
        catch (const std::bad_alloc &)
        {
            output = {};
            output.status = IO::makeStatus(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            output = {};
            output.status = IO::makeStatus(ErrorCode::Unknown);
        }
        return output;
    }

#if DESKTOP_INTERNAL_TEST_HOOKS
    IO::Types::Status testDialogApartment() noexcept
    {
        ApartmentLease apartment;
        const IO::Types::Status &status = apartment.status();
        return status.ok() ? IO::successStatus() : IO::makeStatus(status.code, status.nativeCode);
    }
#endif
} // namespace GameWIP::Desktop::Detail::Platform
