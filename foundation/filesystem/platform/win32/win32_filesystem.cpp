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
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <span>
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

        /// @brief Native sharing used for metadata traversal that must not block ordinary owners.
        constexpr DWORD kShareAll = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
        /// @brief Minimum native access required for stable metadata queries.
        constexpr ACCESS_MASK kQueryAccess = FILE_READ_ATTRIBUTES | SYNCHRONIZE;
        /// @brief Native access required while traversing directory components by handle.
        constexpr ACCESS_MASK kDirectoryTraversalAccess = FILE_READ_ATTRIBUTES | FILE_TRAVERSE | SYNCHRONIZE;
        /// @brief Native options shared by synchronous handle-relative opens.
        constexpr ULONG kOpenOptionsBase = FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_FOR_BACKUP_INTENT;
        /// @brief Tick offset from the Windows 1601 epoch to the Unix 1970 epoch.
        constexpr std::int64_t kUnixEpochAsWindowsFileTime = 116'444'736'000'000'000LL;

#if INTERNAL_FILESYSTEM_TEST_HOOKS
        /// Persistent failure injection used to validate destructor cleanup after UnlockFileEx failure.
        std::atomic_bool forceFileUnlockFailure = false;

        struct MovePauseHook
        {
            std::mutex mutex;
            std::condition_variable condition;
            bool pauseAfterValidation = false;
            bool pauseAfterCommit = false;
            bool reached = false;
            bool released = false;
        };

        MovePauseHook movePauseHook;

        void pauseMoveIfArmed(bool afterCommit)
        {
            std::unique_lock lock(movePauseHook.mutex);
            bool &armed = afterCommit ? movePauseHook.pauseAfterCommit : movePauseHook.pauseAfterValidation;
            if (!armed)
            {
                return;
            }
            movePauseHook.reached = true;
            movePauseHook.condition.notify_all();
            movePauseHook.condition.wait(
                lock,
                []
                {
                    return movePauseHook.released;
                });
            armed = false;
            movePauseHook.reached = false;
            movePauseHook.released = false;
        }
#endif

        using NtCreateFileFunction =
            NTSTATUS(NTAPI *)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK, PLARGE_INTEGER, ULONG, ULONG, ULONG, ULONG, PVOID, ULONG);
        using NtSetInformationFileFunction = NTSTATUS(NTAPI *)(HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG, FILE_INFORMATION_CLASS);
        using RtlNtStatusToDosErrorFunction = ULONG(NTAPI *)(NTSTATUS);

        /// @brief Lazily resolved NT entry points needed for race-resistant relative traversal.
        struct NtApi
        {
            NtCreateFileFunction createFile = nullptr;
            NtSetInformationFileFunction setInformationFile = nullptr;
            RtlNtStatusToDosErrorFunction ntStatusToDosError = nullptr;
        };

        /// @brief Move-only owner for a CloseHandle-compatible native handle.
        class UniqueHandle final
        {
        public:
            UniqueHandle() = default;

            /// @brief Takes ownership of one CloseHandle-compatible value.
            explicit UniqueHandle(HANDLE handle) noexcept
                : handle_(handle)
            {
            }

            UniqueHandle(const UniqueHandle &) = delete;
            UniqueHandle &operator=(const UniqueHandle &) = delete;

            /// @brief Transfers native ownership from another wrapper.
            UniqueHandle(UniqueHandle &&other) noexcept
                : handle_(std::exchange(other.handle_, INVALID_HANDLE_VALUE))
            {
            }

            /// @brief Closes current ownership before taking another wrapper's handle.
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

            /// @brief Returns the owned handle without transferring it.
            [[nodiscard]] HANDLE get() const noexcept
            {
                return handle_;
            }

            /// @brief Returns whether the wrapper owns a closeable handle.
            [[nodiscard]] bool isValid() const noexcept
            {
                return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
            }

            /// @brief Releases native ownership to the caller.
            [[nodiscard]] HANDLE release() noexcept
            {
                return std::exchange(handle_, INVALID_HANDLE_VALUE);
            }

        private:
            /// @brief Closes owned state and restores the invalid sentinel.
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

        /// @brief Move-only owner for a FindFirstFileW enumeration handle.
        class UniqueFindHandle final
        {
        public:
            UniqueFindHandle() = default;

            /// @brief Takes ownership of one FindClose-compatible value.
            explicit UniqueFindHandle(HANDLE handle) noexcept
                : handle_(handle)
            {
            }

            UniqueFindHandle(const UniqueFindHandle &) = delete;
            UniqueFindHandle &operator=(const UniqueFindHandle &) = delete;

            /// @brief Transfers enumeration ownership from another wrapper.
            UniqueFindHandle(UniqueFindHandle &&other) noexcept
                : handle_(std::exchange(other.handle_, INVALID_HANDLE_VALUE))
            {
            }

            /// @brief Closes current enumeration before taking another wrapper's handle.
            UniqueFindHandle &operator=(UniqueFindHandle &&other) noexcept
            {
                if (this != &other)
                {
                    close();
                    handle_ = std::exchange(other.handle_, INVALID_HANDLE_VALUE);
                }
                return *this;
            }

            ~UniqueFindHandle()
            {
                close();
            }

            /// @brief Returns whether an enumeration handle is active.
            [[nodiscard]] bool isValid() const noexcept
            {
                return handle_ != INVALID_HANDLE_VALUE;
            }

            /// @brief Returns the active enumeration handle without transferring it.
            [[nodiscard]] HANDLE get() const noexcept
            {
                return handle_;
            }

            /// @brief Releases enumeration ownership to another owner.
            [[nodiscard]] HANDLE release() noexcept
            {
                return std::exchange(handle_, INVALID_HANDLE_VALUE);
            }

        private:
            /// @brief Ends the enumeration and restores the invalid sentinel.
            void close() noexcept
            {
                if (isValid())
                {
                    FindClose(handle_);
                    handle_ = INVALID_HANDLE_VALUE;
                }
            }

            HANDLE handle_ = INVALID_HANDLE_VALUE;
        };

        /// @brief UTF-16 path conversion result with portable failure status.
        struct WidePathResult
        {
            IO::Types::Status status;
            std::wstring path;
        };

        /// @brief Absolute native root and normalized components for handle-relative traversal.
        struct ParsedPathResult
        {
            IO::Types::Status status;
            std::wstring root;
            std::vector<std::wstring> components;
        };

        /// @brief Native open result with NT information status when available.
        struct HandleResult
        {
            IO::Types::Status status;
            UniqueHandle handle;
            ULONG_PTR information = 0;
        };

        /// @brief Converted system-clock timestamp with overflow status.
        struct TimeResult
        {
            IO::Types::Status status;
            Types::FileTime time{};
        };

        /// @brief Stable opened path and metadata returned by existing-entry resolution.
        struct OpenPathResult
        {
            IO::Types::Status status;
            UniqueHandle handle;
            Types::EntryInfo info{};
        };

        /// @brief Chain of owned traversal handles keeping every resolved component stable.
        struct StablePathResult
        {
            IO::Types::Status status;
            std::vector<UniqueHandle> handles;
            Types::EntryInfo info{};
        };

        /// @brief Recognizes either separator accepted by Windows path parsing.
        [[nodiscard]] bool isSeparator(wchar_t value) noexcept
        {
            return value == L'\\' || value == L'/';
        }

        /// @brief Performs locale-independent ASCII case folding for path prefixes.
        [[nodiscard]] wchar_t asciiUpper(wchar_t value) noexcept
        {
            if (value >= L'a' && value <= L'z')
            {
                return static_cast<wchar_t>(value - (L'a' - L'A'));
            }
            return value;
        }

        /// @brief Compares an ASCII Windows path prefix without locale dependence.
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

        /// @brief Finds the next Windows path separator at or after offset.
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

        /// @brief Maps common Win32 errors to portable IO status while preserving native detail.
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

        /// @brief Maps the calling thread's current GetLastError value.
        [[nodiscard]] IO::Types::Status makeLastErrorStatus(ErrorCode fallback)
        {
            return makeWin32Status(GetLastError(), fallback);
        }

        /// @brief Rejects file-sharing bits outside the supported mask.
        [[nodiscard]] bool isValidFileShare(Types::FileShare share) noexcept
        {
            return (static_cast<std::uint8_t>(share) & ~static_cast<std::uint8_t>(Types::FileShare::All)) == 0;
        }

        /// @brief Validates symlink-policy values before backend traversal.
        [[nodiscard]] bool isValidSymlinkPolicy(Types::SymlinkPolicy policy) noexcept
        {
            switch (policy)
            {
            case Types::SymlinkPolicy::DoNotFollow:
            case Types::SymlinkPolicy::FollowFinal:
            case Types::SymlinkPolicy::FollowAll:
                return true;
            }

            return false;
        }

        /// @brief Converts portable sharing flags to CreateFile/NtCreateFile flags.
        [[nodiscard]] DWORD nativeShareMode(Types::FileShare share) noexcept
        {
            DWORD result = 0;
            if ((share & Types::FileShare::Read) == Types::FileShare::Read)
            {
                result |= FILE_SHARE_READ;
            }
            if ((share & Types::FileShare::Write) == Types::FileShare::Write)
            {
                result |= FILE_SHARE_WRITE;
            }
            if ((share & Types::FileShare::Delete) == Types::FileShare::Delete)
            {
                result |= FILE_SHARE_DELETE;
            }
            return result;
        }

        /// @brief Recovers the typed native handle from file state.
        [[nodiscard]] HANDLE nativeHandle(const Detail::FileState &state) noexcept
        {
            return static_cast<HANDLE>(state.nativeHandle);
        }

        /// @brief Recovers the typed native handle from lock state.
        [[nodiscard]] HANDLE nativeHandle(const Detail::FileLockState &state) noexcept
        {
            return static_cast<HANDLE>(state.nativeHandle);
        }

        /// @brief Rejects null and INVALID_HANDLE_VALUE handles.
        [[nodiscard]] bool isValidHandle(HANDLE handle) noexcept
        {
            return handle != nullptr && handle != INVALID_HANDLE_VALUE;
        }

        /// @brief Resets all file-state fields after native ownership is released.
        void clearNativeHandle(Detail::FileState &state) noexcept
        {
            state.nativeHandle = nullptr;
            state.readable = false;
            state.writable = false;
            state.appendMode = false;
            state.flushOnClose = IO::Types::FlushMode::None;
        }

        /// @brief Closes an erased native handle and clears ownership only on success.
        [[nodiscard]] IO::Types::Status closeNativeHandle(void *&nativeHandle) noexcept
        {
            HANDLE handle = static_cast<HANDLE>(nativeHandle);
            if (!isValidHandle(handle))
            {
                nativeHandle = nullptr;
                return IO::successStatus();
            }

            if (CloseHandle(handle) == FALSE)
            {
                return makeLastErrorStatus(ErrorCode::CloseFailed);
            }

            nativeHandle = nullptr;
            return IO::successStatus();
        }

        /// @brief Resolves and caches required ntdll functions without throwing.
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
                    result.setInformationFile = reinterpret_cast<NtSetInformationFileFunction>(GetProcAddress(module, "NtSetInformationFile"));
                    result.ntStatusToDosError = reinterpret_cast<RtlNtStatusToDosErrorFunction>(GetProcAddress(module, "RtlNtStatusToDosError"));
                }
                return result;
            }();

            return api;
        }

        /// @brief Converts NTSTATUS through RtlNtStatusToDosError when available.
        [[nodiscard]] IO::Types::Status makeNtStatus(NTSTATUS status, ErrorCode fallback)
        {
            const NtApi &api = ntApi();
            if (api.ntStatusToDosError == nullptr)
            {
                return IO::makeStatus(fallback, static_cast<std::int64_t>(status), "NTSTATUS conversion is unavailable");
            }

            return makeWin32Status(api.ntStatusToDosError(status), fallback);
        }

        /// @brief Returns the stable failure used when traversal encounters a forbidden symlink.
        [[nodiscard]] IO::Types::Status symlinkPolicyRejectedStatus()
        {
            return IO::makeStatus(ErrorCode::PermissionDenied, 0, "path rejected by symlink policy");
        }

        /// @brief Detects embedded nulls that Win32 APIs would otherwise truncate.
        [[nodiscard]] bool containsEmbeddedNull(std::wstring_view text) noexcept
        {
            return std::find(text.begin(), text.end(), L'\0') != text.end();
        }

        /// @brief Adds the extended-length prefix while preserving UNC path semantics.
        [[nodiscard]] std::wstring asExtendedLengthPath(std::wstring path)
        {
            if (startsWithInsensitive(path, L"\\\\?\\") || startsWithInsensitive(path, L"\\\\.\\"))
            {
                return path;
            }
            if (path.size() >= 2 && isSeparator(path[0]) && isSeparator(path[1]))
            {
                path.erase(0, 2);
                return L"\\\\?\\UNC\\" + path;
            }
            return L"\\\\?\\" + path;
        }

        /// @brief Converts a non-empty path to normalized absolute UTF-16 extended form.
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
            return {.status = IO::successStatus(), .path = asExtendedLengthPath(std::move(buffer))};
        }

        /// @brief Normalizes one path component into a traversal vector without allowing upward escape.
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

        /// @brief Splits a path suffix into normalized handle-traversal components.
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

        /// @brief Finds the end of a UNC server/share root or returns npos for malformed input.
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

        /// @brief Separates a normalized absolute drive/UNC path into root and safe components.
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

        /// @brief Opens a complete path using ordinary Win32 symlink-following semantics.
        [[nodiscard]] HandleResult openFullPathFollowAll(const std::wstring &path)
        {
            UniqueHandle handle{CreateFileW(path.c_str(), kQueryAccess, kShareAll, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr)};

            if (!handle.isValid())
            {
                return {.status = makeLastErrorStatus(ErrorCode::StatFailed)};
            }
            return {.status = IO::successStatus(), .handle = std::move(handle)};
        }

        /// @brief Opens a parsed drive or UNC root for handle-relative traversal.
        [[nodiscard]] HandleResult openRootDirectory(const std::wstring &root, DWORD shareMode = kShareAll)
        {
            UniqueHandle handle{CreateFileW(
                root.c_str(),
                kDirectoryTraversalAccess,
                shareMode,
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

        /// @brief Calls NtCreateFile relative to a parent handle with explicit reparse behavior.
        [[nodiscard]] HandleResult openChildNative(
            HANDLE parent,
            const std::wstring &name,
            ACCESS_MASK desiredAccess,
            ULONG shareAccess,
            ULONG createDisposition,
            ULONG createOptions,
            ULONG fileAttributes,
            ErrorCode fallback)
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

            const NTSTATUS status = api.createFile(
                &rawHandle,
                desiredAccess | SYNCHRONIZE,
                &attributes,
                &ioStatus,
                nullptr,
                fileAttributes,
                shareAccess,
                createDisposition,
                createOptions,
                nullptr,
                0);

            if (!NT_SUCCESS(status))
            {
                return {.status = makeNtStatus(status, fallback)};
            }

            return {.status = IO::successStatus(), .handle = UniqueHandle{rawHandle}, .information = ioStatus.Information};
        }

        /// @brief Opens one child and rejects reparse points when the policy requires it.
        [[nodiscard]] HandleResult openChild(
            HANDLE parent,
            const std::wstring &name,
            bool doNotFollow,
            bool forTraversal,
            DWORD shareMode = kShareAll)
        {
            ULONG openOptions = kOpenOptionsBase;
            if (forTraversal)
            {
                openOptions |= FILE_DIRECTORY_FILE;
            }
            if (doNotFollow)
            {
                openOptions |= FILE_OPEN_REPARSE_POINT;
            }

            return openChildNative(
                parent,
                name,
                forTraversal ? kDirectoryTraversalAccess : kQueryAccess,
                shareMode,
                FILE_OPEN,
                openOptions,
                FILE_ATTRIBUTE_NORMAL,
                ErrorCode::StatFailed);
        }

        /// @brief Converts Windows 100-nanosecond ticks to system_clock with overflow checks.
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

        /// @brief Detects name-surrogate reparse points, including symlinks and mount points.
        [[nodiscard]] bool isNameSurrogateReparsePoint(const FILE_ATTRIBUTE_TAG_INFO &attributes) noexcept
        {
            return (attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 && IsReparseTagNameSurrogate(attributes.ReparseTag);
        }

        /// @brief Collects portable entry metadata from one already-stable native handle.
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

        /// @brief Traverses parsed components by handle and applies final/intermediate symlink policy.
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

        /// @brief Selects ordinary or strict metadata resolution for one public path query.
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

        /// @brief Traverses and retains the parent directory required for a race-resistant final open.
        [[nodiscard]] HandleResult openParentStrict(
            const ParsedPathResult &path,
            DWORD shareMode = kShareAll,
            std::vector<UniqueHandle> *heldHandles = nullptr)
        {
            if (path.components.empty())
            {
                return {.status = IO::makeStatus(ErrorCode::InvalidArgument)};
            }

            HandleResult parent = openRootDirectory(path.root, shareMode);
            if (!parent.status.ok())
            {
                return parent;
            }
            if (heldHandles != nullptr)
            {
                heldHandles->push_back(UniqueHandle{});
                heldHandles->back() = std::move(parent.handle);
                parent.handle = UniqueHandle{};
            }

            HANDLE currentParent = heldHandles != nullptr ? heldHandles->back().get() : parent.handle.get();
            for (std::size_t index = 0; index + 1 < path.components.size(); ++index)
            {
                HandleResult child = openChild(currentParent, path.components[index], true, true, shareMode);
                if (!child.status.ok())
                {
                    return child;
                }

                EntryQueryResult childInfo = queryHandleInfo(child.handle.get());
                if (!childInfo.status.ok())
                {
                    return {.status = childInfo.status};
                }
                if (childInfo.info.kind == Types::EntryKind::Symlink)
                {
                    return {.status = symlinkPolicyRejectedStatus()};
                }
                if (childInfo.info.kind != Types::EntryKind::Directory)
                {
                    return {.status = IO::makeStatus(ErrorCode::NotDirectory)};
                }

                if (heldHandles != nullptr)
                {
                    heldHandles->push_back(std::move(child.handle));
                    currentParent = heldHandles->back().get();
                }
                else
                {
                    parent.handle = std::move(child.handle);
                    currentParent = parent.handle.get();
                }
            }

            if (heldHandles != nullptr)
            {
                return {.status = IO::successStatus(), .handle = UniqueHandle{}};
            }
            return parent;
        }

        /// @brief Opens an existing final entry with requested access and symlink behavior.
        [[nodiscard]] OpenPathResult openExistingPath(
            const Types::Path &path,
            Types::SymlinkPolicy symlinkPolicy,
            ACCESS_MASK desiredAccess,
            DWORD shareMode,
            bool allowDirectory,
            ErrorCode fallback)
        {
            const WidePathResult absolutePath = absoluteNativePath(path);
            if (!absolutePath.status.ok())
            {
                return {.status = absolutePath.status};
            }

            if (symlinkPolicy == Types::SymlinkPolicy::FollowAll)
            {
                DWORD flags = FILE_ATTRIBUTE_NORMAL;
                if (allowDirectory)
                {
                    flags |= FILE_FLAG_BACKUP_SEMANTICS;
                }

                UniqueHandle handle{CreateFileW(absolutePath.path.c_str(), desiredAccess, shareMode, nullptr, OPEN_EXISTING, flags, nullptr)};
                if (!handle.isValid())
                {
                    return {.status = makeLastErrorStatus(fallback)};
                }

                EntryQueryResult info = queryHandleInfo(handle.get());
                if (!info.status.ok())
                {
                    return {.status = info.status};
                }
                if (!allowDirectory && info.info.kind == Types::EntryKind::Directory)
                {
                    return {.status = IO::makeStatus(ErrorCode::IsDirectory)};
                }
                return {.status = IO::successStatus(), .handle = std::move(handle), .info = info.info};
            }

            const ParsedPathResult parsedPath = parseAbsolutePath(absolutePath.path);
            if (!parsedPath.status.ok())
            {
                return {.status = parsedPath.status};
            }
            if (parsedPath.components.empty())
            {
                return {.status = allowDirectory ? IO::successStatus() : IO::makeStatus(ErrorCode::InvalidArgument)};
            }

            HandleResult parent = openParentStrict(parsedPath, shareMode);
            if (!parent.status.ok())
            {
                return {.status = parent.status};
            }

            ULONG openOptions = kOpenOptionsBase;
            if (!allowDirectory)
            {
                openOptions |= FILE_NON_DIRECTORY_FILE;
            }
            if (symlinkPolicy == Types::SymlinkPolicy::DoNotFollow)
            {
                openOptions |= FILE_OPEN_REPARSE_POINT;
            }

            HandleResult finalHandle = openChildNative(
                parent.handle.get(),
                parsedPath.components.back(),
                desiredAccess,
                shareMode,
                FILE_OPEN,
                openOptions,
                FILE_ATTRIBUTE_NORMAL,
                fallback);
            if (!finalHandle.status.ok())
            {
                return {.status = finalHandle.status};
            }

            EntryQueryResult info = queryHandleInfo(finalHandle.handle.get());
            if (!info.status.ok())
            {
                return {.status = info.status};
            }
            if (!allowDirectory && info.info.kind == Types::EntryKind::Directory)
            {
                return {.status = IO::makeStatus(ErrorCode::IsDirectory)};
            }

            return {.status = IO::successStatus(), .handle = std::move(finalHandle.handle), .info = info.info};
        }

        /// @brief Retains every traversal handle so a multi-step operation cannot swap path components.
        [[nodiscard]] StablePathResult stabilizeExistingPath(const Types::Path &path, Types::SymlinkPolicy symlinkPolicy, bool requireDirectory)
        {
            const WidePathResult absolutePath = absoluteNativePath(path);
            if (!absolutePath.status.ok())
            {
                return {.status = absolutePath.status};
            }

            if (symlinkPolicy == Types::SymlinkPolicy::FollowAll)
            {
                OpenPathResult opened =
                    openExistingPath(path, symlinkPolicy, kQueryAccess, kShareAll & ~FILE_SHARE_DELETE, true, ErrorCode::StatFailed);
                if (!opened.status.ok())
                {
                    return {.status = opened.status};
                }
                if (requireDirectory && opened.info.kind != Types::EntryKind::Directory)
                {
                    return {.status = IO::makeStatus(ErrorCode::NotDirectory)};
                }

                std::vector<UniqueHandle> handles;
                handles.push_back(std::move(opened.handle));
                return {.status = IO::successStatus(), .handles = std::move(handles), .info = opened.info};
            }

            const ParsedPathResult parsedPath = parseAbsolutePath(absolutePath.path);
            if (!parsedPath.status.ok())
            {
                return {.status = parsedPath.status};
            }

            std::vector<UniqueHandle> handles;
            const DWORD stableShare = kShareAll & ~FILE_SHARE_DELETE;
            if (parsedPath.components.empty())
            {
                HandleResult root = openRootDirectory(parsedPath.root, stableShare);
                if (!root.status.ok())
                {
                    return {.status = root.status};
                }
                EntryQueryResult info = queryHandleInfo(root.handle.get());
                if (!info.status.ok())
                {
                    return {.status = info.status};
                }
                handles.push_back(std::move(root.handle));
                return {.status = IO::successStatus(), .handles = std::move(handles), .info = info.info};
            }

            HandleResult parent = openParentStrict(parsedPath, stableShare, &handles);
            if (!parent.status.ok())
            {
                return {.status = parent.status};
            }

            const bool doNotFollowFinal = symlinkPolicy == Types::SymlinkPolicy::DoNotFollow;
            HandleResult finalHandle = openChild(handles.back().get(), parsedPath.components.back(), doNotFollowFinal, requireDirectory, stableShare);
            if (!finalHandle.status.ok())
            {
                return {.status = finalHandle.status};
            }
            EntryQueryResult info = queryHandleInfo(finalHandle.handle.get());
            if (!info.status.ok())
            {
                return {.status = info.status};
            }
            if (requireDirectory && info.info.kind != Types::EntryKind::Directory)
            {
                return {.status = IO::makeStatus(ErrorCode::NotDirectory)};
            }

            handles.push_back(std::move(finalHandle.handle));
            return {.status = IO::successStatus(), .handles = std::move(handles), .info = info.info};
        }

        /// @brief Existing-target requirement used by the unified native open path.
        enum class ExistingEntryRule
        {
            MustExistRegularFile,
            MayCreateRegularFile
        };

        /// @brief Fully validated access, sharing, creation, and type constraints for one native open.
        struct NativeOpenRequest
        {
            DWORD desiredAccess = 0;
            DWORD creationDisposition = OPEN_EXISTING;
            DWORD flagsAndAttributes = FILE_ATTRIBUTE_NORMAL;
            Types::FileAccess access = Types::FileAccess::ReadWrite;
            IO::Types::FlushMode flushOnClose = IO::Types::FlushMode::None;
            Types::FileShare share = Types::FileShare::All;
            Types::SymlinkPolicy symlinkPolicy = Types::SymlinkPolicy::FollowAll;
            ExistingEntryRule existingEntryRule = ExistingEntryRule::MustExistRegularFile;
            bool readable = false;
            bool writable = false;
            bool appendMode = false;
        };

        /// @brief Enforces an open request's existing-entry kind and replacement rules.
        [[nodiscard]] IO::Types::Status validateExistingEntry(const Types::Path &path, const NativeOpenRequest &request)
        {
            const EntryQueryResult entry = queryEntryImpl(path, request.symlinkPolicy);
            if (!entry.status.ok())
            {
                if (entry.status.code == ErrorCode::NotFound && request.existingEntryRule == ExistingEntryRule::MayCreateRegularFile)
                {
                    return IO::successStatus();
                }

                return entry.status;
            }

            if (request.creationDisposition == CREATE_NEW)
            {
                return IO::makeStatus(ErrorCode::AlreadyExists);
            }

            if (entry.info.kind == Types::EntryKind::Directory)
            {
                return IO::makeStatus(ErrorCode::IsDirectory);
            }
            if (entry.info.kind != Types::EntryKind::RegularFile)
            {
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }

            return IO::successStatus();
        }

        /// @brief Publishes a successfully opened native handle into platform-neutral file state.
        [[nodiscard]] IO::Types::Status assignFileState(
            std::unique_ptr<Detail::FileState> &state,
            UniqueHandle handle,
            const NativeOpenRequest &request)
        {
            auto newState = std::make_unique<Detail::FileState>();
            newState->nativeHandle = handle.release();
            newState->access = request.access;
            newState->flushOnClose = request.flushOnClose;
            newState->readable = request.readable;
            newState->writable = request.writable;
            newState->appendMode = request.appendMode;

            state = std::move(newState);
            return IO::successStatus();
        }

        /// @brief Moves a native file pointer using one validated seek origin.
        [[nodiscard]] IO::Types::Status seekNativeHandle(HANDLE handle, std::int64_t offset, IO::Types::SeekOrigin origin);
        /// @brief Queries the current unsigned native file position.
        [[nodiscard]] IO::Types::PositionResult nativePosition(HANDLE handle);

        /// @brief Resizes a handle while restoring its original file pointer when possible.
        [[nodiscard]] IO::Types::Status truncateNativeHandle(HANDLE handle, std::uint64_t sizeBytes)
        {
            if (sizeBytes > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
            {
                return IO::makeStatus(ErrorCode::SizeLimitExceeded);
            }

            const IO::Types::PositionResult originalPosition = nativePosition(handle);

            LARGE_INTEGER target{};
            target.QuadPart = static_cast<LONGLONG>(sizeBytes);
            if (SetFilePointerEx(handle, target, nullptr, FILE_BEGIN) == FALSE)
            {
                return makeLastErrorStatus(ErrorCode::ResizeFailed);
            }
            if (SetEndOfFile(handle) == FALSE)
            {
                return makeLastErrorStatus(ErrorCode::ResizeFailed);
            }

            if (originalPosition.status.ok() && originalPosition.position <= sizeBytes)
            {
                static_cast<void>(seekNativeHandle(handle, static_cast<std::int64_t>(originalPosition.position), IO::Types::SeekOrigin::Begin));
            }
            return IO::successStatus();
        }

        /// @brief Performs a handle-relative final open for non-FollowAll policies.
        [[nodiscard]] IO::Types::Status openNativeFileStrict(
            std::unique_ptr<Detail::FileState> &state,
            const Types::Path &path,
            const NativeOpenRequest &request)
        {
            const WidePathResult absolutePath = absoluteNativePath(path);
            if (!absolutePath.status.ok())
            {
                return absolutePath.status;
            }

            const ParsedPathResult parsedPath = parseAbsolutePath(absolutePath.path);
            if (!parsedPath.status.ok())
            {
                return parsedPath.status;
            }
            if (parsedPath.components.empty())
            {
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }

            HandleResult parent = openParentStrict(parsedPath);
            if (!parent.status.ok())
            {
                return parent.status;
            }

            ULONG openOptions = kOpenOptionsBase | FILE_NON_DIRECTORY_FILE;
            if (request.symlinkPolicy == Types::SymlinkPolicy::DoNotFollow)
            {
                openOptions |= FILE_OPEN_REPARSE_POINT;
            }

            const auto openFinal = [&](ULONG createDisposition) -> HandleResult
            {
                return openChildNative(
                    parent.handle.get(),
                    parsedPath.components.back(),
                    request.desiredAccess,
                    nativeShareMode(request.share),
                    createDisposition,
                    openOptions,
                    FILE_ATTRIBUTE_NORMAL,
                    ErrorCode::OpenFailed);
            };

            bool truncateAfterOpen = false;
            HandleResult finalHandle{.status = IO::makeStatus(ErrorCode::InvalidArgument)};
            switch (request.creationDisposition)
            {
            case CREATE_NEW:
                finalHandle = openFinal(FILE_CREATE);
                break;
            case OPEN_EXISTING:
                finalHandle = openFinal(FILE_OPEN);
                break;
            case OPEN_ALWAYS:
                finalHandle = openFinal(FILE_OPEN_IF);
                break;
            case TRUNCATE_EXISTING:
                finalHandle = openFinal(FILE_OPEN);
                truncateAfterOpen = true;
                break;
            case CREATE_ALWAYS:
                finalHandle = openFinal(FILE_CREATE);
                if (!finalHandle.status.ok() && finalHandle.status.code == ErrorCode::AlreadyExists)
                {
                    finalHandle = openFinal(FILE_OPEN);
                    truncateAfterOpen = true;
                }
                break;
            default:
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }

            if (!finalHandle.status.ok())
            {
                return finalHandle.status;
            }

            const EntryQueryResult info = queryHandleInfo(finalHandle.handle.get());
            if (!info.status.ok())
            {
                return info.status;
            }
            if (info.info.kind == Types::EntryKind::Directory)
            {
                return IO::makeStatus(ErrorCode::IsDirectory);
            }
            if (info.info.kind != Types::EntryKind::RegularFile)
            {
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }

            if (truncateAfterOpen)
            {
                const IO::Types::Status truncateStatus = truncateNativeHandle(finalHandle.handle.get(), 0);
                if (!truncateStatus.ok())
                {
                    return truncateStatus;
                }
            }

            return assignFileState(state, std::move(finalHandle.handle), request);
        }

        /// @brief Executes one validated native open and initializes FileState on success.
        [[nodiscard]] IO::Types::Status openNativeFile(
            std::unique_ptr<Detail::FileState> &state,
            const Types::Path &path,
            const NativeOpenRequest &request)
        {
            if (path.empty() || !isValidFileShare(request.share) || !isValidSymlinkPolicy(request.symlinkPolicy) ||
                !IO::isValidFlushMode(request.flushOnClose))
            {
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }

            if (request.symlinkPolicy != Types::SymlinkPolicy::FollowAll)
            {
                return openNativeFileStrict(state, path, request);
            }

            const IO::Types::Status existingStatus = validateExistingEntry(path, request);
            if (!existingStatus.ok())
            {
                return existingStatus;
            }

            const WidePathResult absolutePath = absoluteNativePath(path);
            if (!absolutePath.status.ok())
            {
                return absolutePath.status;
            }

            UniqueHandle handle{CreateFileW(
                absolutePath.path.c_str(),
                request.desiredAccess,
                nativeShareMode(request.share),
                nullptr,
                request.creationDisposition,
                request.flagsAndAttributes,
                nullptr)};

            if (!handle.isValid())
            {
                return makeLastErrorStatus(ErrorCode::OpenFailed);
            }

            return assignFileState(state, std::move(handle), request);
        }

        /// @brief Implements native seek after validating origin and signed offset semantics.
        [[nodiscard]] IO::Types::Status seekNativeHandle(HANDLE handle, std::int64_t offset, IO::Types::SeekOrigin origin)
        {
            DWORD moveMethod = FILE_BEGIN;
            switch (origin)
            {
            case IO::Types::SeekOrigin::Begin:
                moveMethod = FILE_BEGIN;
                break;
            case IO::Types::SeekOrigin::Current:
                moveMethod = FILE_CURRENT;
                break;
            case IO::Types::SeekOrigin::End:
                moveMethod = FILE_END;
                break;
            default:
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }

            LARGE_INTEGER distance{};
            distance.QuadPart = offset;
            if (SetFilePointerEx(handle, distance, nullptr, moveMethod) == FALSE)
            {
                return makeLastErrorStatus(ErrorCode::SeekFailed);
            }

            return IO::successStatus();
        }

        /// @brief Returns the current native file pointer without changing it.
        [[nodiscard]] IO::Types::PositionResult nativePosition(HANDLE handle)
        {
            LARGE_INTEGER distance{};
            LARGE_INTEGER position{};
            if (SetFilePointerEx(handle, distance, &position, FILE_CURRENT) == FALSE)
            {
                return {.status = makeLastErrorStatus(ErrorCode::SeekFailed)};
            }
            if (position.QuadPart < 0)
            {
                return {.status = IO::makeStatus(ErrorCode::SizeLimitExceeded)};
            }

            return {.status = IO::successStatus(), .position = static_cast<std::uint64_t>(position.QuadPart)};
        }

        /// @brief Returns the unsigned file size for one open native handle.
        [[nodiscard]] IO::Types::SizeResult nativeFileSize(HANDLE handle)
        {
            LARGE_INTEGER size{};
            if (GetFileSizeEx(handle, &size) == FALSE)
            {
                return {.status = makeLastErrorStatus(ErrorCode::StatFailed)};
            }
            if (size.QuadPart < 0)
            {
                return {.status = IO::makeStatus(ErrorCode::SizeLimitExceeded)};
            }

            return {.status = IO::successStatus(), .sizeBytes = static_cast<std::uint64_t>(size.QuadPart)};
        }

        /// @brief Releases a whole-file native lock and decrements shared active-lock state.
        [[nodiscard]] IO::Types::Status unlockNativeFile(Detail::FileLockState &state) noexcept
        {
            if (!state.active)
            {
                return IO::successStatus();
            }

#if INTERNAL_FILESYSTEM_TEST_HOOKS
            if (forceFileUnlockFailure.load(std::memory_order_acquire))
            {
                return IO::makeStatus(ErrorCode::UnlockFailed);
            }
#endif

            OVERLAPPED overlapped{};
            if (UnlockFileEx(nativeHandle(state), 0, MAXDWORD, MAXDWORD, &overlapped) == FALSE)
            {
                return makeLastErrorStatus(ErrorCode::UnlockFailed);
            }

            state.active = false;
            if (state.activeLocks && *state.activeLocks > 0)
            {
                --(*state.activeLocks);
            }

            return IO::successStatus();
        }

        /// @brief Filters synthetic dot entries from Win32 directory enumeration.
        [[nodiscard]] bool isDotDirectoryEntry(std::wstring_view name) noexcept
        {
            return name == L"." || name == L"..";
        }

        /// @brief Appends a wildcard component to an extended-length directory path.
        [[nodiscard]] std::wstring searchWildcardPath(const std::wstring &path)
        {
            std::wstring result = path;
            if (!result.empty() && !isSeparator(result.back()))
            {
                result.push_back(L'\\');
            }
            result.push_back(L'*');
            return result;
        }

        /// @brief Marks an open stable handle for POSIX-style deletion when supported.
        [[nodiscard]] IO::Types::Status markHandleForDeletion(HANDLE handle, ErrorCode fallback) noexcept
        {
            FILE_DISPOSITION_INFO disposition{};
            disposition.DeleteFile = TRUE;
            if (SetFileInformationByHandle(handle, FileDispositionInfo, &disposition, sizeof(disposition)) == FALSE)
            {
                return makeLastErrorStatus(fallback);
            }
            return IO::successStatus();
        }

        /// @brief Updates the read-only attribute through an already-stable handle.
        [[nodiscard]] IO::Types::Status setHandleReadOnly(HANDLE handle, bool readOnly) noexcept
        {
            FILE_BASIC_INFO basicInfo{};
            if (GetFileInformationByHandleEx(handle, FileBasicInfo, &basicInfo, sizeof(basicInfo)) == FALSE)
            {
                return makeLastErrorStatus(ErrorCode::StatFailed);
            }

            if (readOnly)
            {
                basicInfo.FileAttributes |= FILE_ATTRIBUTE_READONLY;
            }
            else
            {
                basicInfo.FileAttributes &= ~FILE_ATTRIBUTE_READONLY;
                if (basicInfo.FileAttributes == 0)
                {
                    basicInfo.FileAttributes = FILE_ATTRIBUTE_NORMAL;
                }
            }

            if (SetFileInformationByHandle(handle, FileBasicInfo, &basicInfo, sizeof(basicInfo)) == FALSE)
            {
                return makeLastErrorStatus(ErrorCode::StatFailed);
            }
            return IO::successStatus();
        }

        /// @brief Copies portable timestamps and read-only state between stable handles.
        [[nodiscard]] IO::Types::Status copyHandleBasicMetadata(HANDLE source, HANDLE destination) noexcept
        {
            FILE_BASIC_INFO sourceInfo{};
            if (GetFileInformationByHandleEx(source, FileBasicInfo, &sourceInfo, sizeof(sourceInfo)) == FALSE)
            {
                return makeLastErrorStatus(ErrorCode::StatFailed);
            }

            FILE_BASIC_INFO destinationInfo{};
            if (GetFileInformationByHandleEx(destination, FileBasicInfo, &destinationInfo, sizeof(destinationInfo)) == FALSE)
            {
                return makeLastErrorStatus(ErrorCode::StatFailed);
            }

            destinationInfo.LastWriteTime = sourceInfo.LastWriteTime;
            if ((sourceInfo.FileAttributes & FILE_ATTRIBUTE_READONLY) != 0)
            {
                destinationInfo.FileAttributes |= FILE_ATTRIBUTE_READONLY;
            }
            else
            {
                destinationInfo.FileAttributes &= ~FILE_ATTRIBUTE_READONLY;
                if (destinationInfo.FileAttributes == 0)
                {
                    destinationInfo.FileAttributes = FILE_ATTRIBUTE_NORMAL;
                }
            }

            if (SetFileInformationByHandle(destination, FileBasicInfo, &destinationInfo, sizeof(destinationInfo)) == FALSE)
            {
                return makeLastErrorStatus(ErrorCode::StatFailed);
            }
            return IO::successStatus();
        }

        /// @brief Creates one final directory through a stable parent handle.
        [[nodiscard]] IO::Types::Status createDirectoryStrict(const Types::Path &path, const Types::CreateDirectoryOptions &options)
        {
            const WidePathResult absolutePath = absoluteNativePath(path);
            if (!absolutePath.status.ok())
            {
                return absolutePath.status;
            }

            const ParsedPathResult parsedPath = parseAbsolutePath(absolutePath.path);
            if (!parsedPath.status.ok())
            {
                return parsedPath.status;
            }
            if (parsedPath.components.empty())
            {
                return options.succeedIfAlreadyExists ? IO::successStatus() : IO::makeStatus(ErrorCode::AlreadyExists);
            }

            const EntryQueryResult existing = queryStrictPath(parsedPath, options.symlinkPolicy);
            if (existing.status.ok())
            {
                if (existing.info.kind == Types::EntryKind::Directory)
                {
                    return options.succeedIfAlreadyExists ? IO::successStatus() : IO::makeStatus(ErrorCode::AlreadyExists);
                }
                return IO::makeStatus(ErrorCode::AlreadyExists);
            }
            if (existing.status.code != ErrorCode::NotFound)
            {
                return existing.status;
            }

            HandleResult parent = openParentStrict(parsedPath);
            if (!parent.status.ok())
            {
                return parent.status;
            }

            HandleResult created = openChildNative(
                parent.handle.get(),
                parsedPath.components.back(),
                kDirectoryTraversalAccess,
                kShareAll,
                FILE_CREATE,
                kOpenOptionsBase | FILE_DIRECTORY_FILE,
                FILE_ATTRIBUTE_DIRECTORY,
                ErrorCode::DirectoryCreateFailed);
            return created.status;
        }

        /// @brief Creates missing components one at a time while retaining strict traversal handles.
        [[nodiscard]] IO::Types::Status createDirectoriesStrict(const Types::Path &path, const Types::CreateDirectoryOptions &options)
        {
            const WidePathResult absolutePath = absoluteNativePath(path);
            if (!absolutePath.status.ok())
            {
                return absolutePath.status;
            }

            const ParsedPathResult parsedPath = parseAbsolutePath(absolutePath.path);
            if (!parsedPath.status.ok())
            {
                return parsedPath.status;
            }
            if (parsedPath.components.empty())
            {
                return options.succeedIfAlreadyExists ? IO::successStatus() : IO::makeStatus(ErrorCode::AlreadyExists);
            }

            HandleResult current = openRootDirectory(parsedPath.root);
            if (!current.status.ok())
            {
                return current.status;
            }

            bool createdAny = false;
            for (std::size_t index = 0; index < parsedPath.components.size(); ++index)
            {
                const bool finalComponent = index + 1 == parsedPath.components.size();
                const bool followThisComponent = finalComponent && options.symlinkPolicy == Types::SymlinkPolicy::FollowFinal;
                HandleResult child = openChild(current.handle.get(), parsedPath.components[index], !followThisComponent, true);
                if (!child.status.ok())
                {
                    if (child.status.code != ErrorCode::NotFound)
                    {
                        return child.status;
                    }

                    child = openChildNative(
                        current.handle.get(),
                        parsedPath.components[index],
                        kDirectoryTraversalAccess,
                        kShareAll,
                        FILE_CREATE,
                        kOpenOptionsBase | FILE_DIRECTORY_FILE,
                        FILE_ATTRIBUTE_DIRECTORY,
                        ErrorCode::DirectoryCreateFailed);
                    if (!child.status.ok())
                    {
                        return child.status;
                    }
                    createdAny = true;
                }

                EntryQueryResult childInfo = queryHandleInfo(child.handle.get());
                if (!childInfo.status.ok())
                {
                    return childInfo.status;
                }
                if (childInfo.info.kind == Types::EntryKind::Symlink && !followThisComponent)
                {
                    return symlinkPolicyRejectedStatus();
                }
                if (childInfo.info.kind != Types::EntryKind::Directory)
                {
                    return IO::makeStatus(ErrorCode::AlreadyExists);
                }

                current.handle = std::move(child.handle);
            }

            return (createdAny || options.succeedIfAlreadyExists) ? IO::successStatus() : IO::makeStatus(ErrorCode::AlreadyExists);
        }

        /// @brief Applies public directory-list kind filters to one queried entry.
        [[nodiscard]] bool includeEntryKind(Types::EntryKind kind, const Types::ListDirectoryOptions &options) noexcept
        {
            switch (kind)
            {
            case Types::EntryKind::RegularFile:
                return options.includeFiles;
            case Types::EntryKind::Directory:
                return options.includeDirectories;
            case Types::EntryKind::Symlink:
                return options.includeSymlinks;
            case Types::EntryKind::Other:
                return options.includeOther;
            }
            return false;
        }

        /// Converts one native enumeration record into a platform-neutral directory entry.
        [[nodiscard]] DirectoryCursorNextResult directoryCursorEntry(Detail::DirectoryCursorState &state, const WIN32_FIND_DATAW &findData)
        {
            const std::wstring childName{findData.cFileName};
            const Types::Path childPath = state.directoryPath / std::filesystem::path{childName};
            if (state.stableHandles.empty())
            {
                return {.status = IO::makeStatus(ErrorCode::DirectoryListFailed)};
            }

            // Query relative to the retained directory handle. This preserves strict traversal and
            // avoids reopening the complete ancestry once per child.
            const HANDLE parentHandle = static_cast<HANDLE>(state.stableHandles.back());
            HandleResult childHandle = openChild(parentHandle, childName, state.symlinkPolicy == Types::SymlinkPolicy::DoNotFollow, false);
            if (!childHandle.status.ok())
            {
                return {.status = childHandle.status};
            }
            const EntryQueryResult child = queryHandleInfo(childHandle.handle.get());
            if (!child.status.ok())
            {
                return {.status = child.status};
            }

            return {
                .status = IO::successStatus(),
                .entry = Types::DirectoryEntry{.path = childPath, .info = child.info},
                .hidden = (findData.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0,
                .hasEntry = true};
        }

        /// Starts native enumeration after the caller has stabilized the directory path.
        [[nodiscard]] DirectoryCursorOpenResult startDirectoryCursor(std::unique_ptr<Detail::DirectoryCursorState> state)
        {
            const WidePathResult absolutePath = absoluteNativePath(state->directoryPath);
            if (!absolutePath.status.ok())
            {
                return {.status = absolutePath.status};
            }

            WIN32_FIND_DATAW findData{};
            UniqueFindHandle findHandle{FindFirstFileW(searchWildcardPath(absolutePath.path).c_str(), &findData)};
            if (!findHandle.isValid())
            {
                const DWORD error = GetLastError();
                if (error == ERROR_FILE_NOT_FOUND)
                {
                    state->finished = true;
                }
                else
                {
                    return {.status = makeWin32Status(error, ErrorCode::DirectoryListFailed)};
                }
            }
            else
            {
                while (isDotDirectoryEntry(findData.cFileName))
                {
                    if (FindNextFileW(findHandle.get(), &findData) != FALSE)
                    {
                        continue;
                    }

                    const DWORD error = GetLastError();
                    if (error != ERROR_NO_MORE_FILES)
                    {
                        return {.status = makeWin32Status(error, ErrorCode::DirectoryListFailed)};
                    }
                    state->finished = true;
                    break;
                }

                if (!state->finished)
                {
                    DirectoryCursorNextResult first = directoryCursorEntry(*state, findData);
                    if (!first.status.ok())
                    {
                        return {.status = first.status};
                    }
                    state->bufferedEntry = std::move(first.entry);
                    state->bufferedEntryHidden = first.hidden;
                    state->hasBufferedEntry = true;
                }
            }

            if (findHandle.isValid())
            {
                state->nativeFindHandle = findHandle.release();
            }
            return {.status = IO::successStatus(), .state = std::move(state)};
        }

        /// Opens a root cursor while retaining the complete path traversal chain.
        [[nodiscard]] DirectoryCursorOpenResult openDirectoryCursorImpl(const Types::Path &path, Types::SymlinkPolicy symlinkPolicy)
        {
            StablePathResult stablePath = stabilizeExistingPath(path, symlinkPolicy, true);
            if (!stablePath.status.ok())
            {
                return {.status = stablePath.status};
            }

            auto state = std::make_unique<Detail::DirectoryCursorState>();
            state->directoryPath = path;
            state->symlinkPolicy = symlinkPolicy;
            state->stableHandles.reserve(stablePath.handles.size());
            for (UniqueHandle &handle : stablePath.handles)
            {
                state->stableHandles.push_back(handle.release());
            }
            return startDirectoryCursor(std::move(state));
        }

        /// Opens a child from its stabilized parent so active nested cursors retain one new handle per level.
        [[nodiscard]] DirectoryCursorOpenResult openChildDirectoryCursorImpl(
            const Detail::DirectoryCursorState &parent,
            const Types::Path &path,
            Types::SymlinkPolicy symlinkPolicy)
        {
            if (symlinkPolicy != Types::SymlinkPolicy::DoNotFollow || parent.stableHandles.empty())
            {
                return {.status = IO::makeStatus(ErrorCode::InvalidArgument)};
            }

            const std::wstring childName = path.filename().wstring();
            if (childName.empty())
            {
                return {.status = IO::makeStatus(ErrorCode::InvalidArgument)};
            }

            const HANDLE parentHandle = static_cast<HANDLE>(parent.stableHandles.back());
            HandleResult childHandle = openChild(parentHandle, childName, true, true, kShareAll & ~FILE_SHARE_DELETE);
            if (!childHandle.status.ok())
            {
                return {.status = childHandle.status};
            }

            auto state = std::make_unique<Detail::DirectoryCursorState>();
            state->directoryPath = path;
            state->symlinkPolicy = symlinkPolicy;
            state->stableHandles.push_back(childHandle.handle.release());
            return startDirectoryCursor(std::move(state));
        }

        /// Advances one cursor without collecting sibling entries.
        [[nodiscard]] DirectoryCursorNextResult readDirectoryCursorImpl(Detail::DirectoryCursorState &state)
        {
            if (state.hasBufferedEntry)
            {
                state.hasBufferedEntry = false;
                return {
                    .status = IO::successStatus(),
                    .entry = std::move(state.bufferedEntry),
                    .hidden = state.bufferedEntryHidden,
                    .hasEntry = true};
            }
            if (state.finished)
            {
                return {.status = IO::successStatus()};
            }

            const HANDLE findHandle = static_cast<HANDLE>(state.nativeFindHandle);
            WIN32_FIND_DATAW findData{};
            while (FindNextFileW(findHandle, &findData) != FALSE)
            {
                if (!isDotDirectoryEntry(findData.cFileName))
                {
                    return directoryCursorEntry(state, findData);
                }
            }

            const DWORD error = GetLastError();
            if (error == ERROR_NO_MORE_FILES)
            {
                state.finished = true;
                return {.status = IO::successStatus()};
            }
            return {.status = makeWin32Status(error, ErrorCode::DirectoryListFailed)};
        }

        /// Enumerates direct children and applies public filtering and count policy.
        [[nodiscard]] Types::ListDirectoryResult listDirectoryImpl(const Types::Path &path, const Types::ListDirectoryOptions &options)
        {
            DirectoryCursorOpenResult opened = openDirectoryCursorImpl(path, options.symlinkPolicy);
            if (!opened.status.ok())
            {
                return {.status = opened.status};
            }

            Types::ListDirectoryResult result{.status = IO::successStatus()};
            while (true)
            {
                DirectoryCursorNextResult next = readDirectoryCursorImpl(*opened.state);
                if (!next.status.ok())
                {
                    result.status = next.status;
                    return result;
                }
                if (!next.hasEntry)
                {
                    return result;
                }
                if ((!options.includeHidden && next.hidden) || !includeEntryKind(next.entry.info.kind, options))
                {
                    continue;
                }
                if (result.entries.size() >= options.maxEntries)
                {
                    result.status = IO::makeStatus(ErrorCode::SizeLimitExceeded);
                    return result;
                }
                result.entries.push_back(std::move(next.entry));
            }
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

    IO::Types::Status openReader(
        std::unique_ptr<Detail::FileState> &state,
        const Types::Path &path,
        const Types::FileReaderOpenOptions &options) noexcept
    {
        try
        {
            const NativeOpenRequest request{
                .desiredAccess = FILE_GENERIC_READ,
                .creationDisposition = OPEN_EXISTING,
                .flagsAndAttributes = FILE_ATTRIBUTE_NORMAL,
                .access = Types::FileAccess::Read,
                .flushOnClose = IO::Types::FlushMode::None,
                .share = options.share,
                .symlinkPolicy = options.symlinkPolicy,
                .existingEntryRule = ExistingEntryRule::MustExistRegularFile,
                .readable = true,
                .writable = false,
                .appendMode = false};
            return openNativeFile(state, path, request);
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

    IO::Types::Status openWriter(
        std::unique_ptr<Detail::FileState> &state,
        const Types::Path &path,
        const Types::FileWriterOpenOptions &options) noexcept
    {
        try
        {
            DWORD creationDisposition = CREATE_ALWAYS;
            bool appendMode = false;
            ExistingEntryRule existingRule = ExistingEntryRule::MayCreateRegularFile;

            switch (options.mode)
            {
            case Types::FileWriterMode::CreateNew:
                creationDisposition = CREATE_NEW;
                break;
            case Types::FileWriterMode::CreateOrTruncate:
                creationDisposition = CREATE_ALWAYS;
                break;
            case Types::FileWriterMode::TruncateExisting:
                creationDisposition = TRUNCATE_EXISTING;
                existingRule = ExistingEntryRule::MustExistRegularFile;
                break;
            case Types::FileWriterMode::OpenOrCreate:
                creationDisposition = OPEN_ALWAYS;
                break;
            case Types::FileWriterMode::AppendOrCreate:
                creationDisposition = OPEN_ALWAYS;
                appendMode = true;
                break;
            case Types::FileWriterMode::AppendExisting:
                creationDisposition = OPEN_EXISTING;
                existingRule = ExistingEntryRule::MustExistRegularFile;
                appendMode = true;
                break;
            default:
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }

            const NativeOpenRequest request{
                .desiredAccess = appendMode ? static_cast<DWORD>(FILE_APPEND_DATA | FILE_READ_ATTRIBUTES | SYNCHRONIZE)
                                            : static_cast<DWORD>(FILE_GENERIC_WRITE | FILE_READ_ATTRIBUTES),
                .creationDisposition = creationDisposition,
                .flagsAndAttributes = FILE_ATTRIBUTE_NORMAL,
                .access = Types::FileAccess::Write,
                .flushOnClose = options.flushOnClose,
                .share = options.share,
                .symlinkPolicy = options.symlinkPolicy,
                .existingEntryRule = existingRule,
                .readable = false,
                .writable = true,
                .appendMode = appendMode};
            return openNativeFile(state, path, request);
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

    IO::Types::Status openFile(std::unique_ptr<Detail::FileState> &state, const Types::Path &path, const Types::FileOpenOptions &options) noexcept
    {
        try
        {
            DWORD creationDisposition = OPEN_EXISTING;
            ExistingEntryRule existingRule = ExistingEntryRule::MustExistRegularFile;

            switch (options.mode)
            {
            case Types::FileOpenMode::OpenExisting:
                creationDisposition = OPEN_EXISTING;
                break;
            case Types::FileOpenMode::CreateNew:
                creationDisposition = CREATE_NEW;
                existingRule = ExistingEntryRule::MayCreateRegularFile;
                break;
            case Types::FileOpenMode::OpenOrCreate:
                creationDisposition = OPEN_ALWAYS;
                existingRule = ExistingEntryRule::MayCreateRegularFile;
                break;
            case Types::FileOpenMode::TruncateExisting:
                creationDisposition = TRUNCATE_EXISTING;
                break;
            case Types::FileOpenMode::CreateOrTruncate:
                creationDisposition = CREATE_ALWAYS;
                existingRule = ExistingEntryRule::MayCreateRegularFile;
                break;
            default:
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }

            DWORD desiredAccess = 0;
            bool readable = false;
            bool writable = false;
            switch (options.access)
            {
            case Types::FileAccess::Read:
                desiredAccess = FILE_GENERIC_READ;
                readable = true;
                break;
            case Types::FileAccess::Write:
                desiredAccess = FILE_GENERIC_WRITE | FILE_READ_ATTRIBUTES;
                writable = true;
                break;
            case Types::FileAccess::ReadWrite:
                desiredAccess = FILE_GENERIC_READ | FILE_GENERIC_WRITE | FILE_READ_ATTRIBUTES;
                readable = true;
                writable = true;
                break;
            default:
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }

            const NativeOpenRequest request{
                .desiredAccess = desiredAccess,
                .creationDisposition = creationDisposition,
                .flagsAndAttributes = FILE_ATTRIBUTE_NORMAL,
                .access = options.access,
                .flushOnClose = options.flushOnClose,
                .share = options.share,
                .symlinkPolicy = options.symlinkPolicy,
                .existingEntryRule = existingRule,
                .readable = readable,
                .writable = writable,
                .appendMode = false};
            const IO::Types::Status openStatus = openNativeFile(state, path, request);
            if (!openStatus.ok())
            {
                return openStatus;
            }

            if (options.initialPosition == Types::FileInitialPosition::End)
            {
                const IO::Types::Status seekStatus = seekFile(*state, 0, IO::Types::SeekOrigin::End);
                if (!seekStatus.ok())
                {
                    static_cast<void>(closeFile(*state));
                    state.reset();
                    return seekStatus;
                }
            }
            else if (options.initialPosition != Types::FileInitialPosition::Beginning)
            {
                static_cast<void>(closeFile(*state));
                state.reset();
                return IO::makeStatus(ErrorCode::InvalidArgument);
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

    IO::Types::ReadResult readFile(Detail::FileState &state, std::span<std::byte> destination) noexcept
    {
        if (!state.readable)
        {
            return {.status = IO::makeStatus(ErrorCode::PermissionDenied)};
        }

        try
        {
            if (destination.empty())
            {
                const IO::Types::PositionResult position = filePosition(state);
                const IO::Types::SizeResult size = fileSize(state);
                return {
                    .status = position.status.ok() ? size.status : position.status,
                    .bytesRead = 0,
                    .endOfStream = position.status.ok() && size.status.ok() && position.position >= size.sizeBytes};
            }

            const auto requestSize =
                static_cast<DWORD>(std::min<std::size_t>(destination.size(), static_cast<std::size_t>(std::numeric_limits<DWORD>::max())));
            DWORD bytesRead = 0;
            if (ReadFile(nativeHandle(state), destination.data(), requestSize, &bytesRead, nullptr) == FALSE)
            {
                return {.status = makeLastErrorStatus(ErrorCode::ReadFailed)};
            }

            return {.status = IO::successStatus(), .bytesRead = bytesRead, .endOfStream = bytesRead < requestSize};
        }
        catch (...)
        {
            return {.status = IO::makeStatus(ErrorCode::Unknown)};
        }
    }

    IO::Types::WriteResult writeFile(Detail::FileState &state, std::span<const std::byte> bytes) noexcept
    {
        if (!state.writable)
        {
            return {.status = IO::makeStatus(ErrorCode::PermissionDenied)};
        }

        try
        {
            if (bytes.empty())
            {
                return {.status = IO::successStatus(), .bytesWritten = 0};
            }

            const auto requestSize =
                static_cast<DWORD>(std::min<std::size_t>(bytes.size(), static_cast<std::size_t>(std::numeric_limits<DWORD>::max())));
            DWORD bytesWritten = 0;
            if (WriteFile(nativeHandle(state), bytes.data(), requestSize, &bytesWritten, nullptr) == FALSE)
            {
                return {.status = makeLastErrorStatus(ErrorCode::WriteFailed), .bytesWritten = bytesWritten};
            }

            return {.status = IO::successStatus(), .bytesWritten = bytesWritten};
        }
        catch (...)
        {
            return {.status = IO::makeStatus(ErrorCode::Unknown)};
        }
    }

    IO::Types::Status flushFile(Detail::FileState &state, IO::Types::FlushMode mode) noexcept
    {
        if (!IO::isValidFlushMode(mode))
        {
            return IO::makeStatus(ErrorCode::InvalidArgument);
        }
        if (mode == IO::Types::FlushMode::None)
        {
            return IO::successStatus();
        }
        if (FlushFileBuffers(nativeHandle(state)) == FALSE)
        {
            return makeLastErrorStatus(ErrorCode::FlushFailed);
        }

        return IO::successStatus();
    }

    IO::Types::Status closeFile(Detail::FileState &state) noexcept
    {
        if (state.activeLocks && *state.activeLocks > 0)
        {
            return IO::makeStatus(ErrorCode::ResourceBusy);
        }

        if (state.writable)
        {
            const IO::Types::Status flushStatus = flushFile(state, state.flushOnClose);
            if (!flushStatus.ok())
            {
                return flushStatus;
            }
        }

        const IO::Types::Status closeStatus = closeNativeHandle(state.nativeHandle);
        if (closeStatus.ok())
        {
            clearNativeHandle(state);
        }
        return closeStatus;
    }

    IO::Types::PositionResult filePosition(const Detail::FileState &state) noexcept
    {
        if (state.appendMode)
        {
            return {.status = IO::makeStatus(ErrorCode::NotSeekable)};
        }
        return nativePosition(nativeHandle(state));
    }

    IO::Types::SizeResult fileSize(const Detail::FileState &state) noexcept
    {
        return nativeFileSize(nativeHandle(state));
    }

    IO::Types::Status seekFile(Detail::FileState &state, std::int64_t offset, IO::Types::SeekOrigin origin) noexcept
    {
        if (state.appendMode)
        {
            return IO::makeStatus(ErrorCode::NotSeekable);
        }
        return seekNativeHandle(nativeHandle(state), offset, origin);
    }

    IO::Types::Status resizeFile(Detail::FileState &state, std::uint64_t sizeBytes) noexcept
    {
        if (!state.writable)
        {
            return IO::makeStatus(ErrorCode::PermissionDenied);
        }
        if (sizeBytes > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        {
            return IO::makeStatus(ErrorCode::SizeLimitExceeded);
        }

        // Preserve the caller's position when possible; a shrink below it naturally leaves the native pointer at the new end.
        const IO::Types::PositionResult originalPosition = state.appendMode ? IO::Types::PositionResult{} : filePosition(state);

        LARGE_INTEGER target{};
        target.QuadPart = static_cast<LONGLONG>(sizeBytes);
        if (SetFilePointerEx(nativeHandle(state), target, nullptr, FILE_BEGIN) == FALSE)
        {
            return makeLastErrorStatus(ErrorCode::ResizeFailed);
        }
        if (SetEndOfFile(nativeHandle(state)) == FALSE)
        {
            return makeLastErrorStatus(ErrorCode::ResizeFailed);
        }

        if (!state.appendMode && originalPosition.status.ok() && originalPosition.position <= sizeBytes)
        {
            static_cast<void>(
                seekNativeHandle(nativeHandle(state), static_cast<std::int64_t>(originalPosition.position), IO::Types::SeekOrigin::Begin));
        }

        return IO::successStatus();
    }

    NativeLockResult tryLockFile(Detail::FileState &state, Types::FileLockMode mode) noexcept
    {
        try
        {
            DWORD flags = LOCKFILE_FAIL_IMMEDIATELY;
            switch (mode)
            {
            case Types::FileLockMode::Shared:
                break;
            case Types::FileLockMode::Exclusive:
                flags |= LOCKFILE_EXCLUSIVE_LOCK;
                break;
            default:
                return {.status = IO::makeStatus(ErrorCode::InvalidArgument)};
            }

            if (!state.activeLocks)
            {
                state.activeLocks = std::make_shared<std::uint32_t>(0);
            }
            auto lockState = std::make_unique<Detail::FileLockState>();

            OVERLAPPED overlapped{};
            if (LockFileEx(nativeHandle(state), flags, 0, MAXDWORD, MAXDWORD, &overlapped) == FALSE)
            {
                const DWORD error = GetLastError();
                if (error == ERROR_LOCK_VIOLATION || error == ERROR_SHARING_VIOLATION)
                {
                    return {.status = IO::successStatus(), .outcome = Types::LockOutcome::WouldBlock};
                }
                return {.status = makeWin32Status(error, ErrorCode::LockFailed)};
            }

            // The lock must own an independent handle so it can remain valid after the originating public wrapper is destroyed.
            HANDLE duplicatedHandleRaw = INVALID_HANDLE_VALUE;
            if (DuplicateHandle(
                    GetCurrentProcess(),
                    nativeHandle(state),
                    GetCurrentProcess(),
                    &duplicatedHandleRaw,
                    0,
                    FALSE,
                    DUPLICATE_SAME_ACCESS) == FALSE)
            {
                const DWORD duplicateError = GetLastError();
                OVERLAPPED unlockOverlapped{};
                static_cast<void>(UnlockFileEx(nativeHandle(state), 0, MAXDWORD, MAXDWORD, &unlockOverlapped));
                return {.status = makeWin32Status(duplicateError, ErrorCode::LockFailed)};
            }
            UniqueHandle duplicatedHandle{duplicatedHandleRaw};

            lockState->nativeHandle = duplicatedHandle.release();
            lockState->activeLocks = state.activeLocks;
            lockState->active = true;
            lockState->exclusive = mode == Types::FileLockMode::Exclusive;
            ++(*lockState->activeLocks);

            return {.status = IO::successStatus(), .outcome = Types::LockOutcome::Acquired, .state = std::move(lockState)};
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

    IO::Types::Status unlockFile(Detail::FileLockState &state) noexcept
    {
        return unlockNativeFile(state);
    }

    IO::Types::Status createDirectory(const Types::Path &path, const Types::CreateDirectoryOptions &options) noexcept
    {
        try
        {
            if (path.empty() || !isValidSymlinkPolicy(options.symlinkPolicy))
            {
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }

            if (options.symlinkPolicy != Types::SymlinkPolicy::FollowAll)
            {
                return createDirectoryStrict(path, options);
            }

            const EntryQueryResult existing = queryEntryImpl(path, Types::SymlinkPolicy::FollowAll);
            if (existing.status.ok())
            {
                if (existing.info.kind == Types::EntryKind::Directory)
                {
                    return options.succeedIfAlreadyExists ? IO::successStatus() : IO::makeStatus(ErrorCode::AlreadyExists);
                }
                return IO::makeStatus(ErrorCode::AlreadyExists);
            }
            if (existing.status.code != ErrorCode::NotFound)
            {
                return existing.status;
            }

            const WidePathResult nativePath = absoluteNativePath(path);
            if (!nativePath.status.ok())
            {
                return nativePath.status;
            }
            if (CreateDirectoryW(nativePath.path.c_str(), nullptr) == FALSE)
            {
                return makeLastErrorStatus(ErrorCode::DirectoryCreateFailed);
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

    IO::Types::Status createDirectories(const Types::Path &path, const Types::CreateDirectoryOptions &options) noexcept
    {
        try
        {
            if (path.empty() || !isValidSymlinkPolicy(options.symlinkPolicy))
            {
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }

            if (options.symlinkPolicy != Types::SymlinkPolicy::FollowAll)
            {
                return createDirectoriesStrict(path, options);
            }

            const WidePathResult absolutePath = absoluteNativePath(path);
            if (!absolutePath.status.ok())
            {
                return absolutePath.status;
            }
            const ParsedPathResult parsedPath = parseAbsolutePath(absolutePath.path);
            if (!parsedPath.status.ok())
            {
                return parsedPath.status;
            }
            if (parsedPath.components.empty())
            {
                return options.succeedIfAlreadyExists ? IO::successStatus() : IO::makeStatus(ErrorCode::AlreadyExists);
            }

            Types::Path current = std::filesystem::path{parsedPath.root};
            bool createdAny = false;
            for (const std::wstring &component : parsedPath.components)
            {
                current /= std::filesystem::path{component};
                const EntryQueryResult existing = queryEntryImpl(current, Types::SymlinkPolicy::FollowAll);
                if (existing.status.ok())
                {
                    if (existing.info.kind != Types::EntryKind::Directory)
                    {
                        return IO::makeStatus(ErrorCode::AlreadyExists);
                    }
                    continue;
                }
                if (existing.status.code != ErrorCode::NotFound)
                {
                    return existing.status;
                }

                const WidePathResult nativeCurrent = absoluteNativePath(current);
                if (!nativeCurrent.status.ok())
                {
                    return nativeCurrent.status;
                }
                if (CreateDirectoryW(nativeCurrent.path.c_str(), nullptr) == FALSE)
                {
                    return makeLastErrorStatus(ErrorCode::DirectoryCreateFailed);
                }
                createdAny = true;
            }

            return (createdAny || options.succeedIfAlreadyExists) ? IO::successStatus() : IO::makeStatus(ErrorCode::AlreadyExists);
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

    Types::ListDirectoryResult listDirectory(const Types::Path &path, const Types::ListDirectoryOptions &options) noexcept
    {
        try
        {
            if (path.empty() || !isValidSymlinkPolicy(options.symlinkPolicy))
            {
                return {.status = IO::makeStatus(ErrorCode::InvalidArgument)};
            }
            return listDirectoryImpl(path, options);
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

    DirectoryCursorOpenResult openDirectoryCursor(const Types::Path &path, Types::SymlinkPolicy symlinkPolicy) noexcept
    {
        try
        {
            if (path.empty() || !isValidSymlinkPolicy(symlinkPolicy))
            {
                return {.status = IO::makeStatus(ErrorCode::InvalidArgument)};
            }
            return openDirectoryCursorImpl(path, symlinkPolicy);
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

    DirectoryCursorNextResult readDirectoryCursor(Detail::DirectoryCursorState &state) noexcept
    {
        try
        {
            return readDirectoryCursorImpl(state);
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

    DirectoryCursorOpenResult openChildDirectoryCursor(
        const Detail::DirectoryCursorState &parent,
        const Types::Path &path,
        Types::SymlinkPolicy symlinkPolicy) noexcept
    {
        try
        {
            if (path.empty() || !isValidSymlinkPolicy(symlinkPolicy))
            {
                return {.status = IO::makeStatus(ErrorCode::InvalidArgument)};
            }
            return openChildDirectoryCursorImpl(parent, path, symlinkPolicy);
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

    IO::Types::Status setReadOnly(const Types::Path &path, bool readOnly, Types::SymlinkPolicy symlinkPolicy) noexcept
    {
        try
        {
            if (path.empty() || !isValidSymlinkPolicy(symlinkPolicy))
            {
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }

            OpenPathResult opened =
                openExistingPath(path, symlinkPolicy, FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES, kShareAll, true, ErrorCode::StatFailed);
            if (!opened.status.ok())
            {
                return opened.status;
            }

            return setHandleReadOnly(opened.handle.get(), readOnly);
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

    IO::Types::Status copyBasicMetadata(const Types::Path &from, const Types::Path &to, Types::SymlinkPolicy symlinkPolicy) noexcept
    {
        try
        {
            if (from.empty() || to.empty() || !isValidSymlinkPolicy(symlinkPolicy))
            {
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }

            OpenPathResult source = openExistingPath(from, symlinkPolicy, FILE_READ_ATTRIBUTES, kShareAll, false, ErrorCode::StatFailed);
            if (!source.status.ok())
            {
                return source.status;
            }
            if (source.info.kind != Types::EntryKind::RegularFile)
            {
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }

            OpenPathResult destination = openExistingPath(
                to,
                Types::SymlinkPolicy::DoNotFollow,
                FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES,
                kShareAll,
                false,
                ErrorCode::StatFailed);
            if (!destination.status.ok())
            {
                return destination.status;
            }
            if (destination.info.kind != Types::EntryKind::RegularFile)
            {
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }

            return copyHandleBasicMetadata(source.handle.get(), destination.handle.get());
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

    IO::Types::Status removeFile(const Types::Path &path, const Types::RemoveOptions &options) noexcept
    {
        try
        {
            if (path.empty() || !isValidSymlinkPolicy(options.symlinkPolicy))
            {
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }

            OpenPathResult opened =
                openExistingPath(path, options.symlinkPolicy, DELETE | FILE_READ_ATTRIBUTES, kShareAll, true, ErrorCode::RemoveFailed);
            if (!opened.status.ok())
            {
                if (opened.status.code == ErrorCode::NotFound && options.succeedIfMissing)
                {
                    return IO::successStatus();
                }
                return opened.status;
            }
            if (opened.info.kind == Types::EntryKind::Directory)
            {
                return IO::makeStatus(ErrorCode::IsDirectory);
            }

            return markHandleForDeletion(opened.handle.get(), ErrorCode::RemoveFailed);
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

    IO::Types::Status removeEmptyDirectory(const Types::Path &path, const Types::RemoveOptions &options) noexcept
    {
        try
        {
            if (path.empty() || !isValidSymlinkPolicy(options.symlinkPolicy))
            {
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }

            OpenPathResult opened =
                openExistingPath(path, options.symlinkPolicy, DELETE | FILE_READ_ATTRIBUTES, kShareAll, true, ErrorCode::RemoveFailed);
            if (!opened.status.ok())
            {
                if (opened.status.code == ErrorCode::NotFound && options.succeedIfMissing)
                {
                    return IO::successStatus();
                }
                return opened.status;
            }
            if (opened.info.kind != Types::EntryKind::Directory)
            {
                return IO::makeStatus(ErrorCode::NotDirectory);
            }

            return markHandleForDeletion(opened.handle.get(), ErrorCode::RemoveFailed);
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

    IO::Types::Status movePath(
        const Types::Path &from,
        const Types::Path &to,
        Types::ReplaceMode replaceMode,
        Types::SymlinkPolicy symlinkPolicy) noexcept
    {
        try
        {
            if (from.empty() || to.empty() || !isValidSymlinkPolicy(symlinkPolicy))
            {
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }

            DWORD flags = MOVEFILE_WRITE_THROUGH;
            switch (replaceMode)
            {
            case Types::ReplaceMode::FailIfExists:
                break;
            case Types::ReplaceMode::ReplaceExisting:
                flags |= MOVEFILE_REPLACE_EXISTING;
                break;
            default:
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }

            if (symlinkPolicy != Types::SymlinkPolicy::FollowAll)
            {
                OpenPathResult source = openExistingPath(from, symlinkPolicy, DELETE | FILE_READ_ATTRIBUTES, kShareAll, true, ErrorCode::MoveFailed);
                if (!source.status.ok())
                {
                    return source.status;
                }

                const WidePathResult nativeTo = absoluteNativePath(to);
                if (!nativeTo.status.ok())
                {
                    return nativeTo.status;
                }
                const ParsedPathResult parsedTo = parseAbsolutePath(nativeTo.path);
                if (!parsedTo.status.ok())
                {
                    return parsedTo.status;
                }
                if (parsedTo.components.empty())
                {
                    return IO::makeStatus(ErrorCode::InvalidArgument);
                }

                HandleResult destinationParent = openParentStrict(parsedTo);
                if (!destinationParent.status.ok())
                {
                    return destinationParent.status;
                }

#if INTERNAL_FILESYSTEM_TEST_HOOKS
                pauseMoveIfArmed(false);
#endif

                // Keep the validated parent handle authoritative through commit. An absolute name
                // would re-resolve every ancestor and reopen the validation-to-rename race.
                const std::wstring &fileName = parsedTo.components.back();
                const DWORD fileNameBytes = static_cast<DWORD>(fileName.size() * sizeof(wchar_t));
                std::vector<std::byte> renameBuffer(sizeof(FILE_RENAME_INFO) + fileNameBytes + sizeof(wchar_t));
                auto *renameInfo = reinterpret_cast<FILE_RENAME_INFO *>(renameBuffer.data());
                renameInfo->ReplaceIfExists = replaceMode == Types::ReplaceMode::ReplaceExisting;
                renameInfo->RootDirectory = destinationParent.handle.get();
                renameInfo->FileNameLength = fileNameBytes;
                std::copy(fileName.begin(), fileName.end(), renameInfo->FileName);

                const NtApi &api = ntApi();
                if (api.setInformationFile == nullptr)
                {
                    return IO::makeStatus(ErrorCode::Unsupported, 0, "NtSetInformationFile is unavailable");
                }
                IO_STATUS_BLOCK renameStatus{};
                const NTSTATUS status = api.setInformationFile(
                    source.handle.get(),
                    &renameStatus,
                    renameInfo,
                    static_cast<ULONG>(renameBuffer.size()),
                    FileRenameInformation);
                if (!NT_SUCCESS(status))
                {
                    return makeNtStatus(status, ErrorCode::MoveFailed);
                }
#if INTERNAL_FILESYSTEM_TEST_HOOKS
                pauseMoveIfArmed(true);
#endif
                return IO::successStatus();
            }

            const WidePathResult nativeFrom = absoluteNativePath(from);
            if (!nativeFrom.status.ok())
            {
                return nativeFrom.status;
            }
            const WidePathResult nativeTo = absoluteNativePath(to);
            if (!nativeTo.status.ok())
            {
                return nativeTo.status;
            }

            if (MoveFileExW(nativeFrom.path.c_str(), nativeTo.path.c_str(), flags) == FALSE)
            {
                return makeLastErrorStatus(ErrorCode::MoveFailed);
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

    IO::Types::Status flushDirectory(const Types::Path &path) noexcept
    {
        try
        {
            if (path.empty())
            {
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }

            OpenPathResult opened = openExistingPath(
                path,
                Types::SymlinkPolicy::FollowAll,
                FILE_GENERIC_READ | FILE_GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                true,
                ErrorCode::FlushFailed);
            if (!opened.status.ok())
            {
                return opened.status;
            }
            if (opened.info.kind != Types::EntryKind::Directory)
            {
                return IO::makeStatus(ErrorCode::NotDirectory);
            }
            if (FlushFileBuffers(opened.handle.get()) == FALSE)
            {
                return makeLastErrorStatus(ErrorCode::FlushFailed);
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

    Types::BoolResult isHidden(const Types::Path &path) noexcept
    {
        try
        {
            const WidePathResult nativePath = absoluteNativePath(path);
            if (!nativePath.status.ok())
            {
                return {.status = nativePath.status, .value = false};
            }

            const DWORD attributes = GetFileAttributesW(nativePath.path.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES)
            {
                return {.status = makeLastErrorStatus(ErrorCode::StatFailed), .value = false};
            }

            return {.status = IO::successStatus(), .value = (attributes & FILE_ATTRIBUTE_HIDDEN) != 0};
        }
        catch (const std::bad_alloc &)
        {
            return {.status = IO::makeStatus(ErrorCode::OutOfMemory), .value = false};
        }
        catch (...)
        {
            return {.status = IO::makeStatus(ErrorCode::Unknown), .value = false};
        }
    }

#if INTERNAL_FILESYSTEM_TEST_HOOKS
    namespace TestHooks
    {
        void setFileUnlockFailure(bool enabled) noexcept
        {
            forceFileUnlockFailure.store(enabled, std::memory_order_release);
        }

        void armMoveDestinationValidatedPause() noexcept
        {
            std::lock_guard lock(movePauseHook.mutex);
            movePauseHook.pauseAfterValidation = true;
            movePauseHook.pauseAfterCommit = false;
            movePauseHook.reached = false;
            movePauseHook.released = false;
        }

        void armMoveCommittedPause() noexcept
        {
            std::lock_guard lock(movePauseHook.mutex);
            movePauseHook.pauseAfterValidation = false;
            movePauseHook.pauseAfterCommit = true;
            movePauseHook.reached = false;
            movePauseHook.released = false;
        }

        bool waitForMovePause(std::chrono::milliseconds timeout) noexcept
        {
            std::unique_lock lock(movePauseHook.mutex);
            return movePauseHook.condition.wait_for(
                lock,
                timeout,
                []
                {
                    return movePauseHook.reached;
                });
        }

        void releaseMovePause() noexcept
        {
            std::lock_guard lock(movePauseHook.mutex);
            movePauseHook.released = true;
            movePauseHook.condition.notify_all();
        }

        void reset() noexcept
        {
            forceFileUnlockFailure.store(false, std::memory_order_release);
            std::lock_guard lock(movePauseHook.mutex);
            movePauseHook.pauseAfterValidation = false;
            movePauseHook.pauseAfterCommit = false;
            movePauseHook.reached = false;
            movePauseHook.released = true;
            movePauseHook.condition.notify_all();
        }
    } // namespace TestHooks
#endif
} // namespace GameWIP::FileSystem::Detail::Platform

namespace GameWIP::FileSystem::Detail
{
    namespace
    {
        /// @brief Validates erased native handles before destructor cleanup.
        [[nodiscard]] bool isValidNativeHandle(void *nativeHandle) noexcept
        {
            const HANDLE handle = static_cast<HANDLE>(nativeHandle);
            return handle != nullptr && handle != INVALID_HANDLE_VALUE;
        }
    } // namespace

    FileState::FileState() = default;

    FileState::~FileState() noexcept
    {
        if (isValidNativeHandle(nativeHandle))
        {
            if (writable && IO::isValidFlushMode(flushOnClose) && flushOnClose != IO::Types::FlushMode::None)
            {
                static_cast<void>(FlushFileBuffers(static_cast<HANDLE>(nativeHandle)));
            }
            static_cast<void>(CloseHandle(static_cast<HANDLE>(nativeHandle)));
            nativeHandle = nullptr;
        }
    }

    FileLockState::~FileLockState() noexcept
    {
        if (active && isValidNativeHandle(nativeHandle))
        {
            static_cast<void>(Platform::unlockFile(*this));
        }

        if (isValidNativeHandle(nativeHandle))
        {
            const bool closed = CloseHandle(static_cast<HANDLE>(nativeHandle)) != FALSE;
            if (closed)
            {
                nativeHandle = nullptr;
                if (active)
                {
                    active = false;
                    if (activeLocks && *activeLocks > 0)
                    {
                        --(*activeLocks);
                    }
                }
            }
        }
    }

    DirectoryCursorState::~DirectoryCursorState() noexcept
    {
        const HANDLE findHandle = static_cast<HANDLE>(nativeFindHandle);
        if (findHandle != nullptr && findHandle != INVALID_HANDLE_VALUE)
        {
            static_cast<void>(FindClose(findHandle));
            nativeFindHandle = nullptr;
        }

        for (void *handleValue : stableHandles)
        {
            const HANDLE handle = static_cast<HANDLE>(handleValue);
            if (handle != nullptr && handle != INVALID_HANDLE_VALUE)
            {
                static_cast<void>(CloseHandle(handle));
            }
        }
        stableHandles.clear();
    }
} // namespace GameWIP::FileSystem::Detail
