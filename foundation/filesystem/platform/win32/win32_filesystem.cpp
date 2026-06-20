/// @file win32_filesystem.cpp
/// @brief Win32 backend for the FileSystem library.

#include "filesystem/internal/filesystem_platform.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winternl.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace GameWIP::FileSystem::Detail::Platform
{
    namespace
    {
        using ErrorCode = IO::Types::ErrorCode;

        constexpr DWORD kShareAll = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
        constexpr ACCESS_MASK kQueryAccess = FILE_READ_ATTRIBUTES | SYNCHRONIZE;
        constexpr ACCESS_MASK kDirectoryTraversalAccess = FILE_READ_ATTRIBUTES | FILE_TRAVERSE | SYNCHRONIZE;
        constexpr ULONG kOpenOptionsBase = FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_FOR_BACKUP_INTENT;
        constexpr std::int64_t kUnixEpochAsWindowsFileTime = 116'444'736'000'000'000LL;

        using NtCreateFileFunction =
            NTSTATUS(NTAPI *)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK, PLARGE_INTEGER, ULONG, ULONG, ULONG, ULONG, PVOID, ULONG);
        using RtlNtStatusToDosErrorFunction = ULONG(NTAPI *)(NTSTATUS);

        struct NtApi
        {
            NtCreateFileFunction createFile = nullptr;
            RtlNtStatusToDosErrorFunction ntStatusToDosError = nullptr;
        };

        class UniqueHandle final
        {
        public:
            UniqueHandle() = default;

            explicit UniqueHandle(HANDLE handle) noexcept
                : handle_(handle)
            {
            }

            UniqueHandle(const UniqueHandle &) = delete;
            UniqueHandle &operator=(const UniqueHandle &) = delete;

            UniqueHandle(UniqueHandle &&other) noexcept
                : handle_(std::exchange(other.handle_, INVALID_HANDLE_VALUE))
            {
            }

            UniqueHandle &operator=(UniqueHandle &&other) noexcept
            {
                if (this != &other)
                {
                    close();
                    handle_ = std::exchange(other.handle_, INVALID_HANDLE_VALUE);
                }
                return *this;
            }

            ~UniqueHandle()
            {
                close();
            }

            [[nodiscard]] HANDLE get() const noexcept
            {
                return handle_;
            }

            [[nodiscard]] bool isValid() const noexcept
            {
                return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
            }

        private:
            void close() noexcept
            {
                if (isValid())
                {
                    CloseHandle(handle_);
                    handle_ = INVALID_HANDLE_VALUE;
                }
            }

            HANDLE handle_ = INVALID_HANDLE_VALUE;
        };

        struct WidePathResult
        {
            IO::Types::Status status;
            std::wstring path;
        };

        struct ParsedPathResult
        {
            IO::Types::Status status;
            std::wstring root;
            std::vector<std::wstring> components;
        };

        struct HandleResult
        {
            IO::Types::Status status;
            UniqueHandle handle;
        };

        struct TimeResult
        {
            IO::Types::Status status;
            Types::FileTime time{};
        };

        [[nodiscard]] bool isSeparator(wchar_t value) noexcept
        {
            return value == L'\\' || value == L'/';
        }

        [[nodiscard]] wchar_t asciiUpper(wchar_t value) noexcept
        {
            if (value >= L'a' && value <= L'z')
            {
                return static_cast<wchar_t>(value - (L'a' - L'A'));
            }
            return value;
        }

        [[nodiscard]] bool startsWithInsensitive(std::wstring_view text, std::wstring_view prefix) noexcept
        {
            if (text.size() < prefix.size())
            {
                return false;
            }

            for (std::size_t index = 0; index < prefix.size(); ++index)
            {
                if (asciiUpper(text[index]) != asciiUpper(prefix[index]))
                {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] std::size_t findSeparator(std::wstring_view text, std::size_t offset) noexcept
        {
            for (std::size_t index = offset; index < text.size(); ++index)
            {
                if (isSeparator(text[index]))
                {
                    return index;
                }
            }
            return std::wstring_view::npos;
        }

        [[nodiscard]] IO::Types::Status makeWin32Status(DWORD error, ErrorCode fallback)
        {
            if (error == ERROR_SUCCESS)
            {
                return IO::successStatus();
            }

            ErrorCode code = fallback;
            switch (error)
            {
            case ERROR_FILE_NOT_FOUND:
            case ERROR_PATH_NOT_FOUND:
                code = ErrorCode::NotFound;
                break;
            case ERROR_DIRECTORY:
                code = ErrorCode::NotDirectory;
                break;
            case ERROR_ACCESS_DENIED:
            case ERROR_PRIVILEGE_NOT_HELD:
                code = ErrorCode::PermissionDenied;
                break;
            case ERROR_ALREADY_EXISTS:
            case ERROR_FILE_EXISTS:
                code = ErrorCode::AlreadyExists;
                break;
            case ERROR_FILENAME_EXCED_RANGE:
                code = ErrorCode::PathTooLong;
                break;
            case ERROR_NOT_ENOUGH_MEMORY:
            case ERROR_OUTOFMEMORY:
                code = ErrorCode::OutOfMemory;
                break;
            case ERROR_SHARING_VIOLATION:
            case ERROR_LOCK_VIOLATION:
                code = ErrorCode::ResourceBusy;
                break;
            case ERROR_DISK_FULL:
            case ERROR_HANDLE_DISK_FULL:
                code = ErrorCode::StorageFull;
                break;
            case ERROR_DIR_NOT_EMPTY:
                code = ErrorCode::DirectoryNotEmpty;
                break;
            case ERROR_BAD_PATHNAME:
            case ERROR_INVALID_NAME:
            case ERROR_INVALID_PARAMETER:
                code = ErrorCode::InvalidArgument;
                break;
            default:
                break;
            }

            return IO::makeStatus(code, static_cast<std::int64_t>(error), std::system_category().message(static_cast<int>(error)));
        }

        [[nodiscard]] IO::Types::Status makeLastErrorStatus(ErrorCode fallback)
        {
            return makeWin32Status(GetLastError(), fallback);
        }

        [[nodiscard]] const NtApi &ntApi() noexcept
        {
            static const NtApi api = []
            {
                NtApi result{};
                HMODULE module = GetModuleHandleW(L"ntdll.dll");
                if (module == nullptr)
                {
                    module = LoadLibraryW(L"ntdll.dll");
                }
                if (module != nullptr)
                {
                    result.createFile = reinterpret_cast<NtCreateFileFunction>(GetProcAddress(module, "NtCreateFile"));
                    result.ntStatusToDosError = reinterpret_cast<RtlNtStatusToDosErrorFunction>(GetProcAddress(module, "RtlNtStatusToDosError"));
                }
                return result;
            }();

            return api;
        }

        [[nodiscard]] IO::Types::Status makeNtStatus(NTSTATUS status, ErrorCode fallback)
        {
            const NtApi &api = ntApi();
            if (api.ntStatusToDosError == nullptr)
            {
                return IO::makeStatus(fallback, static_cast<std::int64_t>(status), "NTSTATUS conversion is unavailable");
            }

            return makeWin32Status(api.ntStatusToDosError(status), fallback);
        }

        [[nodiscard]] IO::Types::Status symlinkPolicyRejectedStatus()
        {
            return IO::makeStatus(ErrorCode::PermissionDenied, 0, "path rejected by symlink policy");
        }

        [[nodiscard]] bool containsEmbeddedNull(std::wstring_view text) noexcept
        {
            return std::find(text.begin(), text.end(), L'\0') != text.end();
        }

        [[nodiscard]] WidePathResult absoluteNativePath(const Types::Path &path)
        {
            const std::wstring nativePath = path.wstring();
            if (nativePath.empty() || containsEmbeddedNull(nativePath))
            {
                return {.status = IO::makeStatus(ErrorCode::InvalidArgument)};
            }

            DWORD requiredLength = GetFullPathNameW(nativePath.c_str(), 0, nullptr, nullptr);
            if (requiredLength == 0)
            {
                return {.status = makeLastErrorStatus(ErrorCode::StatFailed)};
            }

            std::wstring buffer(requiredLength, L'\0');
            DWORD copiedLength = GetFullPathNameW(nativePath.c_str(), requiredLength, buffer.data(), nullptr);
            if (copiedLength == 0)
            {
                return {.status = makeLastErrorStatus(ErrorCode::StatFailed)};
            }
            if (copiedLength >= requiredLength)
            {
                buffer.assign(copiedLength + 1, L'\0');
                copiedLength = GetFullPathNameW(nativePath.c_str(), static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
                if (copiedLength == 0 || copiedLength >= buffer.size())
                {
                    return {.status = makeLastErrorStatus(ErrorCode::StatFailed)};
                }
            }

            buffer.resize(copiedLength);
            return {.status = IO::successStatus(), .path = std::move(buffer)};
        }

        [[nodiscard]] bool appendComponent(std::vector<std::wstring> &components, std::wstring_view component)
        {
            if (component.empty() || component == L".")
            {
                return true;
            }
            if (component == L"..")
            {
                return false;
            }
            if (component.size() > std::numeric_limits<USHORT>::max() / sizeof(wchar_t))
            {
                return false;
            }

            components.emplace_back(component);
            return true;
        }

        [[nodiscard]] bool splitComponents(std::wstring_view text, std::vector<std::wstring> &components)
        {
            std::size_t offset = 0;
            while (offset < text.size())
            {
                while (offset < text.size() && isSeparator(text[offset]))
                {
                    ++offset;
                }
                if (offset >= text.size())
                {
                    break;
                }

                const std::size_t separator = findSeparator(text, offset);
                const std::size_t end = separator == std::wstring_view::npos ? text.size() : separator;
                if (!appendComponent(components, text.substr(offset, end - offset)))
                {
                    return false;
                }

                offset = end;
            }
            return true;
        }

        [[nodiscard]] std::size_t uncRootEnd(std::wstring_view text, std::size_t serverOffset) noexcept
        {
            const std::size_t serverEnd = findSeparator(text, serverOffset);
            if (serverEnd == std::wstring_view::npos || serverEnd == serverOffset)
            {
                return std::wstring_view::npos;
            }

            const std::size_t shareOffset = serverEnd + 1;
            const std::size_t shareEnd = findSeparator(text, shareOffset);
            if (shareEnd == shareOffset)
            {
                return std::wstring_view::npos;
            }
            if (shareEnd == std::wstring_view::npos)
            {
                return text.size();
            }

            return shareEnd + 1;
        }

        [[nodiscard]] ParsedPathResult parseAbsolutePath(std::wstring_view fullPath)
        {
            if (fullPath.empty() || containsEmbeddedNull(fullPath))
            {
                return {.status = IO::makeStatus(ErrorCode::InvalidArgument)};
            }
            if (startsWithInsensitive(fullPath, L"\\\\.\\"))
            {
                return {.status = IO::makeStatus(ErrorCode::Unsupported, 0, "Win32 device paths are not supported by strict traversal")};
            }

            std::size_t rootEnd = std::wstring_view::npos;
            if (startsWithInsensitive(fullPath, L"\\\\?\\UNC\\"))
            {
                rootEnd = uncRootEnd(fullPath, 8);
            }
            else if (startsWithInsensitive(fullPath, L"\\\\?\\"))
            {
                if (fullPath.size() >= 7 && fullPath[5] == L':' && isSeparator(fullPath[6]))
                {
                    rootEnd = 7;
                }
                else
                {
                    const std::size_t deviceEnd = findSeparator(fullPath, 4);
                    if (deviceEnd != std::wstring_view::npos)
                    {
                        rootEnd = deviceEnd + 1;
                    }
                }
            }
            else if (fullPath.size() >= 2 && isSeparator(fullPath[0]) && isSeparator(fullPath[1]))
            {
                rootEnd = uncRootEnd(fullPath, 2);
            }
            else if (fullPath.size() >= 3 && fullPath[1] == L':' && isSeparator(fullPath[2]))
            {
                rootEnd = 3;
            }

            if (rootEnd == std::wstring_view::npos || rootEnd == 0 || rootEnd > fullPath.size())
            {
                return {.status = IO::makeStatus(ErrorCode::InvalidArgument)};
            }

            ParsedPathResult result{.status = IO::successStatus(), .root = std::wstring(fullPath.substr(0, rootEnd))};
            if (!splitComponents(fullPath.substr(rootEnd), result.components))
            {
                return {.status = IO::makeStatus(ErrorCode::InvalidArgument)};
            }
            return result;
        }

        [[nodiscard]] HandleResult openFullPathFollowAll(const std::wstring &path)
        {
            UniqueHandle handle{CreateFileW(path.c_str(), kQueryAccess, kShareAll, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr)};

            if (!handle.isValid())
            {
                return {.status = makeLastErrorStatus(ErrorCode::StatFailed)};
            }
            return {.status = IO::successStatus(), .handle = std::move(handle)};
        }

        [[nodiscard]] HandleResult openRootDirectory(const std::wstring &root)
        {
            UniqueHandle handle{CreateFileW(
                root.c_str(),
                kDirectoryTraversalAccess,
                kShareAll,
                nullptr,
                OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
                nullptr)};

            if (!handle.isValid())
            {
                return {.status = makeLastErrorStatus(ErrorCode::StatFailed)};
            }
            return {.status = IO::successStatus(), .handle = std::move(handle)};
        }

        [[nodiscard]] HandleResult openChild(HANDLE parent, const std::wstring &name, bool doNotFollow, bool forTraversal)
        {
            const NtApi &api = ntApi();
            if (api.createFile == nullptr)
            {
                return {.status = IO::makeStatus(ErrorCode::Unsupported, 0, "NtCreateFile is unavailable")};
            }

            UNICODE_STRING objectName{};
            objectName.Length = static_cast<USHORT>(name.size() * sizeof(wchar_t));
            objectName.MaximumLength = objectName.Length;
            objectName.Buffer = const_cast<PWSTR>(name.data());

            OBJECT_ATTRIBUTES attributes{};
            InitializeObjectAttributes(&attributes, &objectName, OBJ_CASE_INSENSITIVE, parent, nullptr);

            IO_STATUS_BLOCK ioStatus{};
            HANDLE rawHandle = INVALID_HANDLE_VALUE;
            ULONG openOptions = kOpenOptionsBase;
            if (doNotFollow)
            {
                openOptions |= FILE_OPEN_REPARSE_POINT;
            }

            const NTSTATUS status = api.createFile(
                &rawHandle,
                forTraversal ? kDirectoryTraversalAccess : kQueryAccess,
                &attributes,
                &ioStatus,
                nullptr,
                FILE_ATTRIBUTE_NORMAL,
                kShareAll,
                FILE_OPEN,
                openOptions,
                nullptr,
                0);

            if (!NT_SUCCESS(status))
            {
                return {.status = makeNtStatus(status, ErrorCode::StatFailed)};
            }

            return {.status = IO::successStatus(), .handle = UniqueHandle{rawHandle}};
        }

        [[nodiscard]] TimeResult fileTimeToSystemTimePoint(LARGE_INTEGER fileTime)
        {
            const __int128 ticksSinceUnixEpoch = static_cast<__int128>(fileTime.QuadPart) - static_cast<__int128>(kUnixEpochAsWindowsFileTime);
            const __int128 nanoseconds = ticksSinceUnixEpoch * 100;
            if (nanoseconds > std::numeric_limits<std::int64_t>::max() || nanoseconds < std::numeric_limits<std::int64_t>::min())
            {
                return {.status = IO::makeStatus(ErrorCode::SizeLimitExceeded)};
            }

            const auto duration =
                std::chrono::duration_cast<Types::FileTime::duration>(std::chrono::nanoseconds{static_cast<std::int64_t>(nanoseconds)});
            return {.status = IO::successStatus(), .time = Types::FileTime{duration}};
        }

        [[nodiscard]] bool isNameSurrogateReparsePoint(const FILE_ATTRIBUTE_TAG_INFO &attributes) noexcept
        {
            return (attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 && IsReparseTagNameSurrogate(attributes.ReparseTag);
        }

        [[nodiscard]] EntryQueryResult queryHandleInfo(HANDLE handle)
        {
            FILE_ATTRIBUTE_TAG_INFO attributeInfo{};
            if (!GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &attributeInfo, sizeof(attributeInfo)))
            {
                return {.status = makeLastErrorStatus(ErrorCode::StatFailed)};
            }

            FILE_STANDARD_INFO standardInfo{};
            if (!GetFileInformationByHandleEx(handle, FileStandardInfo, &standardInfo, sizeof(standardInfo)))
            {
                return {.status = makeLastErrorStatus(ErrorCode::StatFailed)};
            }

            FILE_BASIC_INFO basicInfo{};
            if (!GetFileInformationByHandleEx(handle, FileBasicInfo, &basicInfo, sizeof(basicInfo)))
            {
                return {.status = makeLastErrorStatus(ErrorCode::StatFailed)};
            }

            EntryQueryResult result{.status = IO::successStatus()};
            if (isNameSurrogateReparsePoint(attributeInfo))
            {
                result.info.kind = Types::EntryKind::Symlink;
            }
            else if (standardInfo.Directory != FALSE || (attributeInfo.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
            {
                result.info.kind = Types::EntryKind::Directory;
            }
            else if ((attributeInfo.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
            {
                result.info.kind = Types::EntryKind::Other;
            }
            else
            {
                result.info.kind = Types::EntryKind::RegularFile;
            }

            if (result.info.kind == Types::EntryKind::RegularFile)
            {
                if (standardInfo.EndOfFile.QuadPart < 0)
                {
                    return {.status = IO::makeStatus(ErrorCode::SizeLimitExceeded)};
                }

                result.info.sizeBytes = static_cast<std::uint64_t>(standardInfo.EndOfFile.QuadPart);
                result.info.hasSize = true;
            }

            const TimeResult lastWriteTime = fileTimeToSystemTimePoint(basicInfo.LastWriteTime);
            if (!lastWriteTime.status.ok())
            {
                return {.status = lastWriteTime.status};
            }
            result.info.lastWriteTime = lastWriteTime.time;
            result.info.hasLastWriteTime = true;
            result.info.readOnly = (basicInfo.FileAttributes & FILE_ATTRIBUTE_READONLY) != 0;
            return result;
        }

        [[nodiscard]] EntryQueryResult queryStrictPath(const ParsedPathResult &path, Types::SymlinkPolicy symlinkPolicy)
        {
            HandleResult parent = openRootDirectory(path.root);
            if (!parent.status.ok())
            {
                return {.status = parent.status};
            }

            if (path.components.empty())
            {
                return queryHandleInfo(parent.handle.get());
            }

            for (std::size_t index = 0; index + 1 < path.components.size(); ++index)
            {
                HandleResult child = openChild(parent.handle.get(), path.components[index], true, true);
                if (!child.status.ok())
                {
                    return {.status = child.status};
                }

                EntryQueryResult childInfo = queryHandleInfo(child.handle.get());
                if (!childInfo.status.ok())
                {
                    return childInfo;
                }
                if (childInfo.info.kind == Types::EntryKind::Symlink)
                {
                    return {.status = symlinkPolicyRejectedStatus()};
                }
                if (childInfo.info.kind != Types::EntryKind::Directory)
                {
                    return {.status = IO::makeStatus(ErrorCode::NotDirectory)};
                }

                parent.handle = std::move(child.handle);
            }

            const bool doNotFollowFinal = symlinkPolicy == Types::SymlinkPolicy::DoNotFollow;
            HandleResult finalHandle = openChild(parent.handle.get(), path.components.back(), doNotFollowFinal, false);
            if (!finalHandle.status.ok())
            {
                return {.status = finalHandle.status};
            }

            return queryHandleInfo(finalHandle.handle.get());
        }

        [[nodiscard]] EntryQueryResult queryEntryImpl(const Types::Path &path, Types::SymlinkPolicy symlinkPolicy)
        {
            switch (symlinkPolicy)
            {
            case Types::SymlinkPolicy::DoNotFollow:
            case Types::SymlinkPolicy::FollowFinal:
            case Types::SymlinkPolicy::FollowAll:
                break;
            default:
                return {.status = IO::makeStatus(ErrorCode::InvalidArgument)};
            }

            const WidePathResult absolutePath = absoluteNativePath(path);
            if (!absolutePath.status.ok())
            {
                return {.status = absolutePath.status};
            }

            if (symlinkPolicy == Types::SymlinkPolicy::FollowAll)
            {
                HandleResult handle = openFullPathFollowAll(absolutePath.path);
                if (!handle.status.ok())
                {
                    return {.status = handle.status};
                }
                return queryHandleInfo(handle.handle.get());
            }

            const ParsedPathResult parsedPath = parseAbsolutePath(absolutePath.path);
            if (!parsedPath.status.ok())
            {
                return {.status = parsedPath.status};
            }

            return queryStrictPath(parsedPath, symlinkPolicy);
        }
    } // namespace

    EntryQueryResult queryEntry(const Types::Path &path, Types::SymlinkPolicy symlinkPolicy) noexcept
    {
        try
        {
            return queryEntryImpl(path, symlinkPolicy);
        }
        catch (const std::bad_alloc &)
        {
            return {.status = IO::makeStatus(ErrorCode::OutOfMemory)};
        }
        catch (...)
        {
            return {.status = IO::makeStatus(ErrorCode::Unknown)};
        }
    }
} // namespace GameWIP::FileSystem::Detail::Platform
