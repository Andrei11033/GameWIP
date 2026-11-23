/// @file win32_clipboard.cpp
/// @brief Win32 Clipboard service backend and native data-transfer conversion.

#include "desktop/platform/clipboard.h"
#include "desktop/internal/desktop_test_hooks.h"
#include "desktop/platform/win32/internal/win32_data_transfer.h"
#include "desktop/platform/win32/internal/win32_window_backend.h"
#include "desktop/platform/win32/internal/win32_unicode.h"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <new>
#include <span>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <variant>

namespace GameWIP::Desktop::Platform
{
    /// ------------------------------------------------------------
    /// Clipboard ownership and conversion helpers
    /// ------------------------------------------------------------

    namespace
    {
        using IO::Types::ErrorCode;
        namespace Transfer = Types::DataTransfer;
        namespace ClipboardTypes = Types::Clipboard;

        constexpr auto kRetryInterval = std::chrono::milliseconds{5};

#if defined(__clang__)
#pragma clang unsafe_buffer_usage begin
#endif
        /// @brief Views checked native clipboard storage as writable typed bytes.
        template <typename Value> [[nodiscard]] std::span<Value> mutableNativeSpan(void *data, std::size_t count) noexcept
        {
            return {static_cast<Value *>(data), count};
        }

#if defined(__clang__)
#pragma clang unsafe_buffer_usage end
#endif

        /// @brief Converts a portable error and optional Win32 code into a Clipboard status.
        [[nodiscard]] IO::Types::Status status(ErrorCode code, DWORD nativeCode = 0) noexcept
        {
            return IO::makeStatus(code, static_cast<std::int64_t>(nativeCode));
        }

        /// @brief Builds a failed result with its payload left at the deterministic default.
        template <typename Result> [[nodiscard]] Result failure(ErrorCode code, DWORD nativeCode = 0) noexcept
        {
            Result result;
            result.status = status(code, nativeCode);
            return result;
        }

        /// @brief Rejects negative access timeouts before entering retry logic.
        [[nodiscard]] bool validTimeout(std::chrono::milliseconds timeout) noexcept
        {
            return timeout.count() >= 0;
        }

        /// @brief Owns a movable GMEM_MOVEABLE allocation until Clipboard publication succeeds.
        class GlobalMemory final
        {
        public:
            GlobalMemory() = default;
            GlobalMemory(const GlobalMemory &) = delete;
            GlobalMemory &operator=(const GlobalMemory &) = delete;
            GlobalMemory(GlobalMemory &&other) noexcept
                : value_(std::exchange(other.value_, nullptr))
            {
            }

            GlobalMemory &operator=(GlobalMemory &&other) noexcept
            {
                if(this != &other)
                {
                    reset();
                    value_ = std::exchange(other.value_, nullptr);
                }

                return *this;
            }

            ~GlobalMemory() noexcept
            {
                reset();
            }

            [[nodiscard]] bool allocate(std::size_t size) noexcept
            {
                value_ = ::GlobalAlloc(GMEM_MOVEABLE, size);
                return value_ != nullptr;
            }

            [[nodiscard]] HGLOBAL get() const noexcept
            {
                return value_;
            }

            [[nodiscard]] HGLOBAL release() noexcept
            {
                return std::exchange(value_, nullptr);
            }

        private:
            void reset() noexcept
            {
                if(value_ != nullptr)
                {
                    static_cast<void>(::GlobalFree(value_));
                }
                value_ = nullptr;
            }
            HGLOBAL value_ = nullptr;
        };

        /// @brief Releases one temporary lock on a global clipboard allocation.
        class GlobalLock final
        {
        public:
            explicit GlobalLock(HGLOBAL memory) noexcept
                : memory_(memory)
                , value_(::GlobalLock(memory))
            {
            }

            GlobalLock(const GlobalLock &) = delete;
            GlobalLock &operator=(const GlobalLock &) = delete;

            ~GlobalLock() noexcept
            {
                if(value_ != nullptr)
                {
                    static_cast<void>(::GlobalUnlock(memory_));
                }
            }

            [[nodiscard]] void *get() const noexcept
            {
                return value_;
            }

        private:
            HGLOBAL memory_ = nullptr;
            void *value_ = nullptr;
        };

        /// @brief Acquires Clipboard ownership with bounded retry and releases it on scope exit.
        class ClipboardSession final
        {
        public:
            ClipboardSession() = default;
            ClipboardSession(const ClipboardSession &) = delete;
            ClipboardSession &operator=(const ClipboardSession &) = delete;
            ~ClipboardSession() noexcept
            {
                if(open_)
                {
                    static_cast<void>(CloseClipboard());
                }
            }

            [[nodiscard]] IO::Types::Status open(HWND owner, std::chrono::milliseconds timeout) noexcept
            {
                const auto started = std::chrono::steady_clock::now();
                const auto maximumWait =
                    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::time_point::max() - started);
                const auto deadline = timeout >= maximumWait ? std::chrono::steady_clock::time_point::max() : started + timeout;

                if(Internal::consumeFailure(GameWIP::Desktop::Internal::TestHooks::FailurePoint::ClipboardAccess))
                {
                    return status(ErrorCode::ResourceBusy, ERROR_BUSY);
                }

                for(;;)
                {
                    SetLastError(ERROR_SUCCESS);

                    if(OpenClipboard(owner) != FALSE)
                    {
                        open_ = true;
                        return IO::successStatus();
                    }
                    const DWORD nativeCode = GetLastError();

                    if(timeout == Clipboard::kNoWait || std::chrono::steady_clock::now() >= deadline)
                    {
                        return status(ErrorCode::ResourceBusy, nativeCode);
                    }
                    const auto remaining = deadline - std::chrono::steady_clock::now();

                    if(remaining <= std::chrono::steady_clock::duration::zero())
                    {
                        return status(ErrorCode::ResourceBusy, nativeCode);
                    }
                    std::this_thread::sleep_for(std::min(std::chrono::duration_cast<std::chrono::steady_clock::duration>(kRetryInterval), remaining));
                }
            }

            [[nodiscard]] IO::Types::Status close() noexcept
            {
                if(!open_)
                {
                    return IO::successStatus();
                }
                open_ = false;
                SetLastError(ERROR_SUCCESS);
                const bool injected = Internal::consumeFailure(GameWIP::Desktop::Internal::TestHooks::FailurePoint::ClipboardClose);

                if(CloseClipboard() == FALSE)
                {
                    return status(ErrorCode::CloseFailed, GetLastError());
                }

                if(injected)
                {
                    return status(ErrorCode::CloseFailed, ERROR_GEN_FAILURE);
                }

                return IO::successStatus();
            }

        private:
            bool open_ = false;
        };

        /// @brief Provides a process-local hidden HWND for Clipboard ownership publication.
        class PublicationOwner final
        {
        public:
            PublicationOwner() noexcept
            {
                handle_ = CreateWindowExW(
                    0,
                    L"STATIC",
                    L"GameWIP Clipboard Owner",
                    WS_POPUP,
                    0,
                    0,
                    0,
                    0,
                    nullptr,
                    nullptr,
                    GetModuleHandleW(nullptr),
                    nullptr);
            }
            PublicationOwner(const PublicationOwner &) = delete;
            PublicationOwner &operator=(const PublicationOwner &) = delete;
            ~PublicationOwner() noexcept
            {
                if(handle_ != nullptr)
                {
                    static_cast<void>(DestroyWindow(handle_));
                }
            }
            [[nodiscard]] HWND get() const noexcept
            {
                return handle_;
            }

        private:
            HWND handle_ = nullptr;
        };

        /// @brief Owns one prepared native format and its publication buffer.
        struct PreparedItem
        {
            UINT format = 0;
            GlobalMemory memory;
        };

        /// @brief Validates portable format identity before native clipboard lookup.
        [[nodiscard]] IO::Types::Status validateFormatView(Transfer::FormatView format) noexcept
        {
            switch(format.kind())
            {
            case Transfer::FormatKind::Text:
            case Transfer::FormatKind::FileList:
            case Transfer::FormatKind::Image:
                return format.customName().empty() ? IO::successStatus() : status(ErrorCode::InvalidArgument);
            case Transfer::FormatKind::Custom:
                if(format.customName().empty() || format.customName().contains('\0'))
                {
                    return status(ErrorCode::InvalidArgument);
                }

                try
                {
                    std::wstring wide;
                    DWORD nativeCode = 0;
                    return utf8ToUtf16(format.customName(), wide, nativeCode)
                               ? IO::successStatus()
                               : status(unicodeConversionError(nativeCode, ErrorCode::InvalidArgument), nativeCode);
                }
                catch(const std::bad_alloc &)
                {
                    return status(ErrorCode::OutOfMemory);
                }
                catch(...)
                {
                    return status(ErrorCode::Unknown);
                }
            }

            return status(ErrorCode::InvalidArgument);
        }

        /// @brief Reads a registered format name while preserving native enumeration errors.
        [[nodiscard]] bool registeredName(UINT format, std::wstring &name, DWORD &nativeCode)
        {
            if(format < 0xC000U)
            {
                return false;
            }

            std::array<wchar_t, 256> buffer{};
            SetLastError(ERROR_SUCCESS);
            const int length = GetClipboardFormatNameW(format, buffer.data(), static_cast<int>(buffer.size()));

            if(length <= 0)
            {
                nativeCode = GetLastError();
                return false;
            }
            name.assign(buffer.data(), static_cast<std::size_t>(length));
            return true;
        }

        /// @brief Enumerates registered clipboard formats using ordinal case-insensitive matching.
        [[nodiscard]] UINT findRegisteredFormat(std::wstring_view requested, DWORD &nativeCode)
        {
            nativeCode = ERROR_SUCCESS;
            UINT current = 0;

            for(;;)
            {
                if(Internal::consumeFailure(GameWIP::Desktop::Internal::TestHooks::FailurePoint::ClipboardEnumeration))
                {
                    nativeCode = ERROR_GEN_FAILURE;
                    return 0;
                }
                SetLastError(ERROR_SUCCESS);
                current = EnumClipboardFormats(current);

                if(current == 0)
                {
                    nativeCode = GetLastError();
                    return 0;
                }

                if(current < 0xC000U)
                {
                    continue;
                }
                std::wstring candidate;
                DWORD nameCode = 0;

                if(!registeredName(current, candidate, nameCode))
                {
                    nativeCode = nameCode;
                    return 0;
                }

                if(CompareStringOrdinal(
                       candidate.data(),
                       static_cast<int>(candidate.size()),
                       requested.data(),
                       static_cast<int>(requested.size()),
                       TRUE) == CSTR_EQUAL)
                {
                    return current;
                }
            }
        }

        /// @brief Allocates and copies one prepared payload into transferable global memory.
        [[nodiscard]] IO::Types::Status allocateAndCopy(std::span<const std::byte> bytes, GlobalMemory &memory) noexcept
        {
            // A zero-byte GMEM_MOVEABLE allocation is a discarded handle that SetClipboardData
            // rejects. A one-byte substitute would corrupt the opaque extent, while nullptr would
            // opt into delayed rendering (deliberately outside this feature's producer contract).
            if(bytes.empty())
            {
                return status(ErrorCode::Unsupported);
            }

            if(!memory.allocate(bytes.size()))
            {
                return status(ErrorCode::OutOfMemory, GetLastError());
            }
            GlobalLock lock(memory.get());

            if(lock.get() == nullptr)
            {
                return status(ErrorCode::LockFailed, GetLastError());
            }
            const auto destination = mutableNativeSpan<std::byte>(lock.get(), bytes.size());
            std::ranges::copy(bytes, destination.begin());
            return IO::successStatus();
        }

        /// @brief Converts one portable item and owns its native clipboard storage.
        [[nodiscard]] IO::Types::Status prepareItem(const Transfer::ItemView &item, PreparedItem &prepared)
        {
            if(Internal::consumeFailure(GameWIP::Desktop::Internal::TestHooks::FailurePoint::ClipboardAllocation))
            {
                return status(ErrorCode::OutOfMemory);
            }

            DataTransfer::PreparedItem shared;
            IO::Types::Status result = DataTransfer::prepare(item, shared);

            if(!result.ok())
            {
                return result;
            }

            prepared.format = shared.format;
            return allocateAndCopy(shared.bytes, prepared.memory);
        }

        /// @brief Materializes one native clipboard handle through the shared transfer decoder.
        [[nodiscard]] IO::Types::Status materializeClipboardItem(UINT nativeFormat, const Transfer::Format &format, Transfer::Item &item) noexcept
        {
            SetLastError(ERROR_SUCCESS);
            HGLOBAL memory = static_cast<HGLOBAL>(GetClipboardData(nativeFormat));

            if(!memory)
            {
                return status(ErrorCode::ReadFailed, GetLastError());
            }
            return DataTransfer::materializeGlobal(memory, format, item);
        }

        /// @brief Closes Clipboard access without hiding an earlier read/write failure.
        [[nodiscard]] IO::Types::Status closePreservingPrimary(ClipboardSession &session, IO::Types::Status primary) noexcept
        {
            IO::Types::Status cleanup = session.close();
            return primary.ok() ? std::move(cleanup) : std::move(primary);
        }

        /// @brief Returns whether a native format is one of Win32's text encodings.
        [[nodiscard]] bool isTextFormat(UINT format) noexcept
        {
            return format == CF_TEXT || format == CF_OEMTEXT || format == CF_UNICODETEXT;
        }

        /// @brief Returns whether a native format carries a bitmap or DIB image.
        [[nodiscard]] bool isImageFormat(UINT format) noexcept
        {
            return format == CF_BITMAP || format == CF_DIB || format == CF_DIBV5;
        }

        /// @brief Reports the injected native read failure used by validation.
        [[nodiscard]] bool clipboardReadShouldFail() noexcept
        {
            return Internal::consumeFailure(GameWIP::Desktop::Internal::TestHooks::FailurePoint::ClipboardRead);
        }

        /// @brief Reads one Clipboard payload after applying the shared read-failure hook.
        [[nodiscard]] IO::Types::Status readClipboardItem(UINT nativeFormat, const Transfer::Format &format, Transfer::Item &item) noexcept
        {
            if(clipboardReadShouldFail())
            {
                return status(ErrorCode::ReadFailed, ERROR_GEN_FAILURE);
            }

            return materializeClipboardItem(nativeFormat, format, item);
        }

        /// @brief Opens Clipboard once and transfers a decoded payload only after successful close.
        template <typename Payload, typename Read>
        [[nodiscard]] Types::Result<Payload> readClipboardWithSession(std::chrono::milliseconds timeout, Read &&read) noexcept
        {
            if(!validTimeout(timeout))
            {
                return failure<Types::Result<Payload>>(ErrorCode::InvalidArgument);
            }

            Types::Result<Payload> result;
            Payload payload{};

            try
            {
                // Hold Clipboard access while the format-specific reader materializes its value.
                ClipboardSession session;
                result.status = session.open(nullptr, timeout);

                if(!result.status.ok())
                {
                    return result;
                }

                result.status = read(payload);

                // Do not expose data unless both the read and Clipboard close succeed.
                result.status = closePreservingPrimary(session, std::move(result.status));

                if(result.status.ok())
                {
                    result.value = std::move(payload);
                }
            }
            catch(const std::bad_alloc &)
            {
                result.status = status(ErrorCode::OutOfMemory);
            }
            catch(...)
            {
                result.status = status(ErrorCode::Unknown);
            }

            return result;
        }

    } // namespace

    /// ------------------------------------------------------------
    /// Clipboard queries and reads
    /// ------------------------------------------------------------

    Types::Result<bool> clipboardHasFormat(Transfer::FormatView format, std::chrono::milliseconds timeout) noexcept
    {
        if(!validTimeout(timeout))
        {
            return failure<Types::Result<bool>>(ErrorCode::InvalidArgument);
        }

        IO::Types::Status validation = validateFormatView(format);
        if(!validation.ok())
        {
            return {.status = std::move(validation)};
        }

        return readClipboardWithSession<bool>(
            timeout,
            [format](bool &available)
            {
                IO::Types::Status result = IO::successStatus();

                switch(format.kind())
                {
                case Transfer::FormatKind::Text:
                    available = IsClipboardFormatAvailable(CF_UNICODETEXT) != FALSE;
                    break;
                case Transfer::FormatKind::FileList:
                    available = IsClipboardFormatAvailable(CF_HDROP) != FALSE;
                    break;
                case Transfer::FormatKind::Image:
                    available = IsClipboardFormatAvailable(CF_DIBV5) != FALSE || IsClipboardFormatAvailable(CF_DIB) != FALSE ||
                                IsClipboardFormatAvailable(CF_BITMAP) != FALSE;
                    break;
                case Transfer::FormatKind::Custom:
                {
                    std::wstring name;
                    DWORD nativeCode = 0;

                    if(!utf8ToUtf16(format.customName(), name, nativeCode))
                    {
                        result = status(unicodeConversionError(nativeCode, ErrorCode::InvalidArgument), nativeCode);
                    }
                    else
                    {
                        const UINT found = findRegisteredFormat(name, nativeCode);
                        available = found != 0;

                        if(found == 0 && nativeCode != ERROR_SUCCESS)
                        {
                            result = status(ErrorCode::ReadFailed, nativeCode);
                        }
                    }
                    break;
                }
                }
                return result;
            });
    }

    Types::Result<std::vector<Transfer::Format>> clipboardGetFormats(std::chrono::milliseconds timeout) noexcept
    {
        return readClipboardWithSession<std::vector<Transfer::Format>>(
            timeout,
            [](std::vector<Transfer::Format> &formats)
            {
                bool textSeen = false;
                bool filesSeen = false;
                bool imageSeen = false;
                UINT current = 0;
                IO::Types::Status result = IO::successStatus();

                for(;;)
                {
                    if(Internal::consumeFailure(GameWIP::Desktop::Internal::TestHooks::FailurePoint::ClipboardEnumeration) ||
                       Internal::consumeClipboardEnumerationFailure(formats.size()))
                    {
                        result = status(ErrorCode::ReadFailed, ERROR_GEN_FAILURE);
                        break;
                    }
                    SetLastError(ERROR_SUCCESS);
                    current = EnumClipboardFormats(current);

                    if(current == 0)
                    {
                        const DWORD nativeCode = GetLastError();

                        if(nativeCode != ERROR_SUCCESS)
                        {
                            result = status(ErrorCode::ReadFailed, nativeCode);
                        }
                        break;
                    }

                    if(isTextFormat(current))
                    {
                        if(!textSeen)
                        {
                            formats.push_back({Transfer::FormatKind::Text, {}});
                            textSeen = true;
                        }
                    }
                    else if(current == CF_HDROP)
                    {
                        if(!filesSeen)
                        {
                            formats.push_back({Transfer::FormatKind::FileList, {}});
                            filesSeen = true;
                        }
                    }
                    else if(isImageFormat(current))
                    {
                        if(!imageSeen)
                        {
                            formats.push_back({Transfer::FormatKind::Image, {}});
                            imageSeen = true;
                        }
                    }
                    else if(current >= 0xC000U)
                    {
                        std::wstring wide;
                        DWORD nativeCode = 0;

                        if(!registeredName(current, wide, nativeCode))
                        {
                            result = status(ErrorCode::ReadFailed, nativeCode);
                            break;
                        }
                        std::string name;

                        if(!utf16ToUtf8(wide, name, nativeCode))
                        {
                            result = status(unicodeConversionError(nativeCode, ErrorCode::EncodingFailed), nativeCode);
                            break;
                        }
                        formats.push_back({Transfer::FormatKind::Custom, std::move(name)});
                    }
                }
                return result;
            });
    }

    Types::Result<std::string> clipboardReadText(std::chrono::milliseconds timeout) noexcept
    {
        return readClipboardWithSession<std::string>(
            timeout,
            [](std::string &text)
            {
                if(IsClipboardFormatAvailable(CF_UNICODETEXT) == FALSE)
                {
                    return status(ErrorCode::NotFound);
                }

                Transfer::Item item;
                IO::Types::Status result = readClipboardItem(CF_UNICODETEXT, {Transfer::FormatKind::Text, {}}, item);

                if(result.ok())
                {
                    text = std::move(std::get<Transfer::Text>(item).text);
                }

                return result;
            });
    }

    Types::Result<std::vector<FileSystem::Types::Path>> clipboardReadFiles(std::chrono::milliseconds timeout) noexcept
    {
        return readClipboardWithSession<std::vector<FileSystem::Types::Path>>(
            timeout,
            [](std::vector<FileSystem::Types::Path> &paths)
            {
                if(IsClipboardFormatAvailable(CF_HDROP) == FALSE)
                {
                    return status(ErrorCode::NotFound);
                }

                Transfer::Item item;
                IO::Types::Status result = readClipboardItem(CF_HDROP, {Transfer::FormatKind::FileList, {}}, item);

                if(result.ok())
                {
                    paths = std::move(std::get<Transfer::FileList>(item).paths);
                }

                return result;
            });
    }

    Types::Result<Transfer::Image> clipboardReadImage(std::chrono::milliseconds timeout) noexcept
    {
        return readClipboardWithSession<Transfer::Image>(
            timeout,
            [](Transfer::Image &image)
            {
                const UINT format = IsClipboardFormatAvailable(CF_DIBV5) != FALSE ? CF_DIBV5
                                    : IsClipboardFormatAvailable(CF_DIB) != FALSE ? CF_DIB
                                                                                  : 0;

                if(format == 0)
                {
                    return IsClipboardFormatAvailable(CF_BITMAP) != FALSE ? status(ErrorCode::Unsupported) : status(ErrorCode::NotFound);
                }

                Transfer::Item item;
                IO::Types::Status result = readClipboardItem(format, {Transfer::FormatKind::Image, {}}, item);

                if(result.ok())
                {
                    image = std::move(std::get<Transfer::Image>(item));
                }

                return result;
            });
    }

    Types::Result<std::vector<std::byte>> clipboardReadCustomData(std::string_view formatName, std::chrono::milliseconds timeout) noexcept
    {
        if(!validTimeout(timeout))
        {
            return failure<Types::Result<std::vector<std::byte>>>(ErrorCode::InvalidArgument);
        }

        IO::Types::Status validation = validateFormatView(Transfer::FormatView::custom(formatName));

        if(!validation.ok())
        {
            return {.status = std::move(validation)};
        }

        try
        {
            std::wstring wide;
            DWORD nativeCode = 0;

            if(!utf8ToUtf16(formatName, wide, nativeCode))
            {
                return failure<Types::Result<std::vector<std::byte>>>(unicodeConversionError(nativeCode, ErrorCode::InvalidArgument), nativeCode);
            }
            return readClipboardWithSession<std::vector<std::byte>>(
                timeout,
                [&](std::vector<std::byte> &bytes)
                {
                    const UINT format = findRegisteredFormat(wide, nativeCode);

                    if(format == 0)
                    {
                        return nativeCode == ERROR_SUCCESS ? status(ErrorCode::NotFound) : status(ErrorCode::ReadFailed, nativeCode);
                    }

                    if(clipboardReadShouldFail())
                    {
                        return status(ErrorCode::ReadFailed, ERROR_GEN_FAILURE);
                    }

                    Transfer::Item item;
                    IO::Types::Status result = materializeClipboardItem(format, {Transfer::FormatKind::Custom, std::string(formatName)}, item);

                    if(result.ok())
                    {
                        bytes = std::move(std::get<Transfer::CustomData>(item).bytes);
                    }

                    return result;
                });
        }
        catch(const std::bad_alloc &)
        {
            return failure<Types::Result<std::vector<std::byte>>>(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return failure<Types::Result<std::vector<std::byte>>>(ErrorCode::Unknown);
        }
    }

    /// ------------------------------------------------------------
    /// Clipboard publication
    /// ------------------------------------------------------------

    Types::Clipboard::WriteResult clipboardWrite(std::span<const Transfer::ItemView> items, std::chrono::milliseconds timeout) noexcept
    {
        ClipboardTypes::WriteResult result;

        if(!validTimeout(timeout) || items.empty())
        {
            result.status = status(ErrorCode::InvalidArgument);
            return result;
        }

        try
        {
            // Convert every item before clearing the current Clipboard contents.
            std::vector<PreparedItem> prepared;
            prepared.reserve(items.size());
            std::unordered_set<UINT> identities;
            identities.reserve(items.size());

            for(const Transfer::ItemView &item : items)
            {
                PreparedItem native;
                result.status = prepareItem(item, native);

                if(!result.status.ok())
                {
                    return result;
                }

                if(!identities.insert(native.format).second)
                {
                    result.status = status(ErrorCode::InvalidArgument);
                    return result;
                }

                prepared.push_back(std::move(native));
            }

            // Create a native owner and acquire exclusive Clipboard access.
            if(Internal::consumeFailure(GameWIP::Desktop::Internal::TestHooks::FailurePoint::ClipboardOwnerCreation))
            {
                result.status = status(ErrorCode::OpenFailed, ERROR_GEN_FAILURE);
                return result;
            }
            PublicationOwner owner;

            if(owner.get() == nullptr)
            {
                result.status = status(ErrorCode::OpenFailed, GetLastError());
                return result;
            }
            ClipboardSession session;
            result.status = session.open(owner.get(), timeout);

            if(!result.status.ok())
            {
                return result;
            }
            SetLastError(ERROR_SUCCESS);

            if(Internal::consumeFailure(GameWIP::Desktop::Internal::TestHooks::FailurePoint::ClipboardClear) || EmptyClipboard() == FALSE)
            {
                result.status = status(ErrorCode::WriteFailed, GetLastError());
                result.status = closePreservingPrimary(session, std::move(result.status));
                return result;
            }
            result.commitState = ClipboardTypes::CommitState::Cleared;

            // Transfer each allocation to Windows only after its format is published.
            for(std::size_t index = 0; index < prepared.size(); ++index)
            {
                PreparedItem &item = prepared[index];
                SetLastError(ERROR_SUCCESS);

                if(Internal::consumeClipboardPublicationFailure(index) || SetClipboardData(item.format, item.memory.get()) == nullptr)
                {
                    result.status = status(ErrorCode::WriteFailed, GetLastError());
                    result.commitState =
                        result.formatsPublished == 0 ? ClipboardTypes::CommitState::Cleared : ClipboardTypes::CommitState::PartiallyPublished;
                    result.status = closePreservingPrimary(session, std::move(result.status));
                    return result;
                }
                static_cast<void>(item.memory.release());
                ++result.formatsPublished;
            }
            result.commitState = ClipboardTypes::CommitState::Published;
            result.status = closePreservingPrimary(session, IO::successStatus());
            return result;
        }
        catch(const std::bad_alloc &)
        {
            result.status = status(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            result.status = status(ErrorCode::Unknown);
        }

        return result;
    }

    Types::Clipboard::ClearResult clipboardClear(std::chrono::milliseconds timeout) noexcept
    {
        ClipboardTypes::ClearResult result;

        if(!validTimeout(timeout))
        {
            result.status = status(ErrorCode::InvalidArgument);
            return result;
        }

        if(Internal::consumeFailure(GameWIP::Desktop::Internal::TestHooks::FailurePoint::ClipboardOwnerCreation))
        {
            result.status = status(ErrorCode::OpenFailed, ERROR_GEN_FAILURE);
            return result;
        }

        PublicationOwner owner;

        if(owner.get() == nullptr)
        {
            result.status = status(ErrorCode::OpenFailed, GetLastError());
            return result;
        }

        ClipboardSession session;
        result.status = session.open(owner.get(), timeout);

        if(!result.status.ok())
        {
            return result;
        }

        SetLastError(ERROR_SUCCESS);

        if(Internal::consumeFailure(GameWIP::Desktop::Internal::TestHooks::FailurePoint::ClipboardClear) || EmptyClipboard() == FALSE)
        {
            result.status = status(ErrorCode::WriteFailed, GetLastError());
        }
        else
        {
            result.status = IO::successStatus();
            result.cleared = true;
        }
        result.status = closePreservingPrimary(session, std::move(result.status));
        return result;
    }
} // namespace GameWIP::Desktop::Platform
