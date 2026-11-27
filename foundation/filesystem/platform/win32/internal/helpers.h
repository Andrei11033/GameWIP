/// @file helpers.h
/// @brief Private shared helpers for the Win32 FileSystem backend.

#pragma once

#include "filesystem/internal/helpers.h"
#include "filesystem/internal/platform.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winternl.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace GameWIP::FileSystem::Platform::Win32
{
    using Internal::includeEntryKind;
    using Internal::isValidFileShare;
    using Internal::isValidSymlinkPolicy;
    using ErrorCode = Internal::ErrorCode;
    using Status = GameWIP::Status::Types::Status;
    template <typename T> using Result = GameWIP::Status::Types::Result<T>;

    /// ------------------------------------------------------------
    /// Native access and sharing
    /// ------------------------------------------------------------

    inline constexpr DWORD kShareAll = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
    inline constexpr DWORD kShareWithoutDelete = kShareAll & ~static_cast<DWORD>(FILE_SHARE_DELETE);
    inline constexpr DWORD kAttributesWithoutReadOnly = ~static_cast<DWORD>(FILE_ATTRIBUTE_READONLY);
    inline constexpr ACCESS_MASK kQueryAccess = FILE_READ_ATTRIBUTES | SYNCHRONIZE;
    inline constexpr ACCESS_MASK kDirectoryTraversalAccess = FILE_READ_ATTRIBUTES | FILE_TRAVERSE | SYNCHRONIZE;
    inline constexpr ULONG kOpenOptionsBase = FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_FOR_BACKUP_INTENT;
    inline constexpr std::int64_t kUnixEpochAsWindowsFileTime = 116'444'736'000'000'000LL;

    /// ------------------------------------------------------------
    /// Native ownership
    /// ------------------------------------------------------------

    class UniqueHandle final
    {
    public:
        UniqueHandle() noexcept = default;
        explicit UniqueHandle(HANDLE handle) noexcept;
        UniqueHandle(const UniqueHandle &) = delete;
        UniqueHandle &operator=(const UniqueHandle &) = delete;
        UniqueHandle(UniqueHandle &&other) noexcept;
        UniqueHandle &operator=(UniqueHandle &&other) noexcept;
        ~UniqueHandle() noexcept;

        [[nodiscard]] HANDLE get() const noexcept;
        [[nodiscard]] bool isValid() const noexcept;
        [[nodiscard]] HANDLE release() noexcept;

    private:
        void close() noexcept;

        HANDLE handle_ = INVALID_HANDLE_VALUE;
    };

    class UniqueFindHandle final
    {
    public:
        UniqueFindHandle() noexcept = default;
        explicit UniqueFindHandle(HANDLE handle) noexcept;
        UniqueFindHandle(const UniqueFindHandle &) = delete;
        UniqueFindHandle &operator=(const UniqueFindHandle &) = delete;
        UniqueFindHandle(UniqueFindHandle &&other) noexcept;
        UniqueFindHandle &operator=(UniqueFindHandle &&other) noexcept;
        ~UniqueFindHandle() noexcept;

        [[nodiscard]] HANDLE get() const noexcept;
        [[nodiscard]] bool isValid() const noexcept;
        [[nodiscard]] HANDLE release() noexcept;

    private:
        void close() noexcept;

        HANDLE handle_ = INVALID_HANDLE_VALUE;
    };

    struct NtApi
    {
        using NtCreateFileFunction =
            NTSTATUS(NTAPI *)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK, PLARGE_INTEGER, ULONG, ULONG, ULONG, ULONG, PVOID, ULONG);
        using NtSetInformationFileFunction = NTSTATUS(NTAPI *)(HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG, FILE_INFORMATION_CLASS);
        using RtlNtStatusToDosErrorFunction = ULONG(NTAPI *)(NTSTATUS);

        NtCreateFileFunction createFile = nullptr;
        NtSetInformationFileFunction setInformationFile = nullptr;
        RtlNtStatusToDosErrorFunction ntStatusToDosError = nullptr;
    };

    /// ------------------------------------------------------------
    /// Native operation results
    /// ------------------------------------------------------------

    struct ParsedPath
    {
        std::wstring root;
        std::vector<std::wstring> components;
    };

    struct HandleInfo
    {
        UniqueHandle handle;
        ULONG_PTR information = 0;
    };

    struct OpenPath
    {
        UniqueHandle handle;
        Types::EntryInfo info{};
    };

    struct StablePath
    {
        std::vector<UniqueHandle> handles;
        Types::EntryInfo info{};
    };

    template <typename T> [[nodiscard]] Result<T> failure(Status status) noexcept
    {
        return Result<T>::failure(std::move(status));
    }

    template <typename T> [[nodiscard]] Result<T> failure(ErrorCode code) noexcept
    {
        return Result<T>::failure(Status(code));
    }

    [[nodiscard]] inline Result<void> success() noexcept
    {
        return Result<void>::success();
    }

    [[nodiscard]] inline Result<void> failure(ErrorCode code) noexcept
    {
        return Result<void>::failure(Status(code));
    }

    enum class ExistingEntryRule
    {
        MustExistRegularFile,
        MayCreateRegularFile
    };

    struct NativeOpenRequest
    {
        // Native creation
        DWORD desiredAccess = 0;
        DWORD creationDisposition = OPEN_EXISTING;
        DWORD flagsAndAttributes = FILE_ATTRIBUTE_NORMAL;

        // Portable policy
        Types::File::Access access = Types::File::Access::ReadWrite;
        IO::Types::FlushMode flushOnClose = IO::Types::FlushMode::None;
        Types::File::Share share = Types::File::Share::All;
        Types::SymlinkPolicy symlinkPolicy = Types::SymlinkPolicy::FollowAll;
        ExistingEntryRule existingEntryRule = ExistingEntryRule::MustExistRegularFile;

        // Effective stream state
        bool readable = false;
        bool writable = false;
        bool appendMode = false;
    };

    /// ------------------------------------------------------------
    /// Status and option helpers
    /// ------------------------------------------------------------

    [[nodiscard]] Status makeWin32Status(DWORD error, ErrorCode fallback) noexcept;
    [[nodiscard]] Status makeLastErrorStatus(ErrorCode fallback) noexcept;
    [[nodiscard]] Status makeDiagnosticStatus(ErrorCode code, std::int64_t nativeCode, std::string_view message) noexcept;
    [[nodiscard]] Status makeNtStatus(NTSTATUS status, ErrorCode fallback) noexcept;
    [[nodiscard]] Status symlinkPolicyRejectedStatus() noexcept;
    [[nodiscard]] DWORD nativeShareMode(Types::File::Share share) noexcept;

    /// ------------------------------------------------------------
    /// Native handle helpers
    /// ------------------------------------------------------------

    [[nodiscard]] HANDLE nativeHandle(const Internal::FileState &state) noexcept;
    [[nodiscard]] HANDLE nativeHandle(const Internal::FileLockState &state) noexcept;
    [[nodiscard]] bool isValidHandle(HANDLE handle) noexcept;
    void clearNativeHandle(Internal::FileState &state) noexcept;
    [[nodiscard]] Result<void> closeNativeHandle(void *&nativeHandle) noexcept;
    [[nodiscard]] const NtApi &ntApi() noexcept;

    /// ------------------------------------------------------------
    /// Path parsing and conversion
    /// ------------------------------------------------------------

    [[nodiscard]] bool isSeparator(wchar_t value) noexcept;
    [[nodiscard]] wchar_t asciiUpper(wchar_t value) noexcept;
    [[nodiscard]] bool startsWithInsensitive(std::wstring_view text, std::wstring_view prefix) noexcept;
    [[nodiscard]] std::size_t findSeparator(std::wstring_view text, std::size_t offset) noexcept;
    [[nodiscard]] bool containsEmbeddedNull(std::wstring_view text) noexcept;
    [[nodiscard]] std::wstring asExtendedLengthPath(std::wstring path);
    [[nodiscard]] Result<std::wstring> absoluteNativePath(const Types::Path &path);
    [[nodiscard]] bool appendComponent(std::vector<std::wstring> &components, std::wstring_view component);
    [[nodiscard]] bool splitComponents(std::wstring_view text, std::vector<std::wstring> &components);
    [[nodiscard]] std::size_t uncRootEnd(std::wstring_view text, std::size_t serverOffset) noexcept;
    [[nodiscard]] Result<ParsedPath> parseAbsolutePath(std::wstring_view fullPath);

    /// ------------------------------------------------------------
    /// Native path opening
    /// ------------------------------------------------------------

    [[nodiscard]] Result<HandleInfo> openFullPathFollowAll(const std::wstring &path);
    [[nodiscard]] Result<HandleInfo> openRootDirectory(
        const std::wstring &root,
        DWORD shareMode = kShareAll,
        ACCESS_MASK desiredAccess = kDirectoryTraversalAccess,
        bool doNotFollow = true,
        ErrorCode fallback = ErrorCode::StatFailed);
    [[nodiscard]] Result<HandleInfo> openChildNative(
        HANDLE parent,
        const std::wstring &name,
        ACCESS_MASK desiredAccess,
        DWORD shareMode,
        ULONG createDisposition,
        ULONG openOptions,
        ULONG fileAttributes,
        ErrorCode fallback);
    [[nodiscard]] Result<HandleInfo> openChild(
        HANDLE parent,
        const std::wstring &name,
        bool doNotFollow,
        bool requireDirectory,
        DWORD shareMode = kShareAll);

    /// ------------------------------------------------------------
    /// Entry metadata queries
    /// ------------------------------------------------------------

    [[nodiscard]] Result<Types::FileTime> fileTimeToSystemTimePoint(LARGE_INTEGER fileTime);
    [[nodiscard]] bool isNameSurrogateReparsePoint(const FILE_ATTRIBUTE_TAG_INFO &attributes) noexcept;
    [[nodiscard]] Result<Types::EntryInfo> queryHandleInfo(HANDLE handle);
    [[nodiscard]] Result<Types::EntryInfo> queryStrictPath(const ParsedPath &path, Types::SymlinkPolicy symlinkPolicy);
    [[nodiscard]] Result<Types::EntryInfo> queryEntryImpl(const Types::Path &path, Types::SymlinkPolicy symlinkPolicy);

    /// ------------------------------------------------------------
    /// Stable path resolution
    /// ------------------------------------------------------------

    [[nodiscard]] Result<HandleInfo> openParentStrict(
        const ParsedPath &path,
        DWORD shareMode = kShareAll,
        std::vector<UniqueHandle> *heldHandles = nullptr);
    [[nodiscard]] Result<OpenPath> openExistingPath(
        const Types::Path &path,
        Types::SymlinkPolicy symlinkPolicy,
        ACCESS_MASK desiredAccess,
        DWORD shareMode,
        bool allowDirectory,
        ErrorCode fallback);
    [[nodiscard]] Result<StablePath> stabilizeExistingPath(const Types::Path &path, Types::SymlinkPolicy symlinkPolicy, bool requireDirectory);

    /// ------------------------------------------------------------
    /// File operations
    /// ------------------------------------------------------------

    [[nodiscard]] Result<void> validateExistingEntry(const Types::Path &path, const NativeOpenRequest &request);
    [[nodiscard]] Result<void> assignFileState(std::unique_ptr<Internal::FileState> &state, UniqueHandle handle, const NativeOpenRequest &request);
    [[nodiscard]] Result<void> seekNativeHandle(HANDLE handle, std::int64_t offset, IO::Types::SeekOrigin origin);
    [[nodiscard]] Result<std::uint64_t> nativePosition(HANDLE handle);
    [[nodiscard]] Result<void> truncateNativeHandle(HANDLE handle, std::uint64_t sizeBytes);
    [[nodiscard]] Result<void> openNativeFileStrict(
        std::unique_ptr<Internal::FileState> &state,
        const Types::Path &path,
        const NativeOpenRequest &request);
    [[nodiscard]] Result<void> openNativeFile(std::unique_ptr<Internal::FileState> &state, const Types::Path &path, const NativeOpenRequest &request);
    [[nodiscard]] Result<std::uint64_t> nativeFileSize(HANDLE handle);
    [[nodiscard]] Result<void> unlockNativeFile(Internal::FileLockState &state) noexcept;

#if FILESYSTEM_INTERNAL_TEST_HOOKS
    [[nodiscard]] bool fileUnlockFailureForced() noexcept;
    void pauseMoveIfArmed(bool afterCommit);
#endif

    /// ------------------------------------------------------------
    /// Directory operations
    /// ------------------------------------------------------------

    [[nodiscard]] bool isDotDirectoryEntry(std::wstring_view name) noexcept;
    [[nodiscard]] std::wstring searchWildcardPath(const std::wstring &path);
    [[nodiscard]] Result<void> markHandleForDeletion(HANDLE handle, ErrorCode fallback) noexcept;
    [[nodiscard]] Result<void> setHandleReadOnly(HANDLE handle, bool readOnly) noexcept;
    [[nodiscard]] Result<void> copyHandleBasicMetadata(HANDLE source, HANDLE destination) noexcept;
    [[nodiscard]] Result<void> createDirectoryStrict(const Types::Path &path, const Types::Directory::CreateOptions &options);
    [[nodiscard]] Result<void> createDirectoriesStrict(const Types::Path &path, const Types::Directory::CreateOptions &options);
    [[nodiscard]] Result<Platform::DirectoryCursorEntry> directoryCursorEntry(
        Internal::DirectoryCursorState &state,
        const WIN32_FIND_DATAW &findData);
    [[nodiscard]] Result<std::unique_ptr<Internal::DirectoryCursorState>> startDirectoryCursor(std::unique_ptr<Internal::DirectoryCursorState> state);
    [[nodiscard]] Result<std::unique_ptr<Internal::DirectoryCursorState>> openDirectoryCursorImpl(
        const Types::Path &path,
        Types::SymlinkPolicy symlinkPolicy);
    [[nodiscard]] Result<std::unique_ptr<Internal::DirectoryCursorState>> openChildDirectoryCursorImpl(
        const Internal::DirectoryCursorState &parent,
        const Types::Path &path,
        Types::SymlinkPolicy symlinkPolicy);
    [[nodiscard]] Result<std::optional<Platform::DirectoryCursorEntry>> readDirectoryCursorImpl(Internal::DirectoryCursorState &state);
    [[nodiscard]] Result<std::vector<Types::Directory::Entry>> listDirectoryImpl(
        const Types::Path &path,
        const Types::Directory::ListOptions &options);
} // namespace GameWIP::FileSystem::Platform::Win32
