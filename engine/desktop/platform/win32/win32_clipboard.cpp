/// @file win32_clipboard.cpp
/// @brief Win32 Clipboard service backend and native data-transfer conversion.

#include "desktop/internal/clipboard_platform.h"
#include "desktop/internal/desktop_test_hooks.h"
#include "desktop/platform/win32/internal/win32_data_transfer.h"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <limits>
#include <new>
#include <span>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <variant>

namespace GameWIP::Desktop::Detail::Platform
{
    namespace
    {
        using ErrorCode = IO::Types::ErrorCode;
        namespace Transfer = Types::DataTransfer;
        namespace ClipboardTypes = Types::Clipboard;

        constexpr auto kRetryInterval = std::chrono::milliseconds{5};

#if defined(__clang__)
#pragma clang unsafe_buffer_usage begin
#endif
        template <typename Value> [[nodiscard]] std::span<Value> mutableNativeSpan(void *data, std::size_t count) noexcept
        {
            return {static_cast<Value *>(data), count};
        }

#if defined(__clang__)
#pragma clang unsafe_buffer_usage end
#endif

        [[nodiscard]] IO::Types::Status status(ErrorCode code, DWORD nativeCode = 0) noexcept
        {
            return IO::makeStatus(code, static_cast<std::int64_t>(nativeCode));
        }

        template <typename Result> [[nodiscard]] Result failure(ErrorCode code, DWORD nativeCode = 0) noexcept
        {
            Result result;
            result.status = status(code, nativeCode);
            return result;
        }

        [[nodiscard]] bool validTimeout(std::chrono::milliseconds timeout) noexcept
        {
            return timeout.count() >= 0;
        }

        [[nodiscard]] bool utf8ToWide(std::string_view text, std::wstring &output, DWORD &nativeCode)
        {
            nativeCode = ERROR_SUCCESS;
            if (text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            {
                nativeCode = ERROR_INSUFFICIENT_BUFFER;
                return false;
            }
            if (text.empty())
            {
                output.clear();
                return true;
            }
            const int sourceLength = static_cast<int>(text.size());
            const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), sourceLength, nullptr, 0);
            if (required <= 0)
            {
                nativeCode = GetLastError();
                return false;
            }
            output.resize(static_cast<std::size_t>(required));
            if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), sourceLength, output.data(), required) != required)
            {
                nativeCode = GetLastError();
                return false;
            }
            return true;
        }

        [[nodiscard]] bool wideToUtf8(std::wstring_view text, std::string &output, DWORD &nativeCode)
        {
            nativeCode = ERROR_SUCCESS;
            if (text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            {
                nativeCode = ERROR_INSUFFICIENT_BUFFER;
                return false;
            }
            if (text.empty())
            {
                output.clear();
                return true;
            }
            const int sourceLength = static_cast<int>(text.size());
            const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), sourceLength, nullptr, 0, nullptr, nullptr);
            if (required <= 0)
            {
                nativeCode = GetLastError();
                return false;
            }
            output.resize(static_cast<std::size_t>(required));
            if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), sourceLength, output.data(), required, nullptr, nullptr) != required)
            {
                nativeCode = GetLastError();
                return false;
            }
            return true;
        }

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
                if (this != &other)
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
                if (value_ != nullptr)
                    static_cast<void>(::GlobalFree(value_));
                value_ = nullptr;
            }
            HGLOBAL value_ = nullptr;
        };

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
                if (value_ != nullptr)
                    static_cast<void>(::GlobalUnlock(memory_));
            }
            [[nodiscard]] void *get() const noexcept
            {
                return value_;
            }

        private:
            HGLOBAL memory_ = nullptr;
            void *value_ = nullptr;
        };

        class ClipboardSession final
        {
        public:
            ClipboardSession() = default;
            ClipboardSession(const ClipboardSession &) = delete;
            ClipboardSession &operator=(const ClipboardSession &) = delete;
            ~ClipboardSession() noexcept
            {
                if (open_)
                    static_cast<void>(CloseClipboard());
            }

            [[nodiscard]] IO::Types::Status open(HWND owner, std::chrono::milliseconds timeout) noexcept
            {
                const auto started = std::chrono::steady_clock::now();
                const auto maximumWait =
                    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::time_point::max() - started);
                const auto deadline = timeout >= maximumWait ? std::chrono::steady_clock::time_point::max() : started + timeout;
                if (Detail::consumeFailure(TestHooks::FailurePoint::ClipboardAccess))
                    return status(ErrorCode::ResourceBusy, ERROR_BUSY);
                for (;;)
                {
                    SetLastError(ERROR_SUCCESS);
                    if (OpenClipboard(owner) != FALSE)
                    {
                        open_ = true;
                        return IO::successStatus();
                    }
                    const DWORD nativeCode = GetLastError();
                    if (timeout == Clipboard::kNoWait || std::chrono::steady_clock::now() >= deadline)
                        return status(ErrorCode::ResourceBusy, nativeCode);
                    const auto remaining = deadline - std::chrono::steady_clock::now();
                    if (remaining <= std::chrono::steady_clock::duration::zero())
                        return status(ErrorCode::ResourceBusy, nativeCode);
                    std::this_thread::sleep_for(std::min(std::chrono::duration_cast<std::chrono::steady_clock::duration>(kRetryInterval), remaining));
                }
            }

            [[nodiscard]] IO::Types::Status close() noexcept
            {
                if (!open_)
                    return IO::successStatus();
                open_ = false;
                SetLastError(ERROR_SUCCESS);
                const bool injected = Detail::consumeFailure(TestHooks::FailurePoint::ClipboardClose);
                if (CloseClipboard() == FALSE)
                    return status(ErrorCode::CloseFailed, GetLastError());
                if (injected)
                    return status(ErrorCode::CloseFailed, ERROR_GEN_FAILURE);
                return IO::successStatus();
            }

        private:
            bool open_ = false;
        };

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
                if (handle_ != nullptr)
                    static_cast<void>(DestroyWindow(handle_));
            }
            [[nodiscard]] HWND get() const noexcept
            {
                return handle_;
            }

        private:
            HWND handle_ = nullptr;
        };

        struct PreparedItem
        {
            UINT format = 0;
            GlobalMemory memory;
        };

        [[nodiscard]] IO::Types::Status validateFormatView(Transfer::FormatView format) noexcept
        {
            switch (format.kind)
            {
            case Transfer::FormatKind::Text:
            case Transfer::FormatKind::FileList:
            case Transfer::FormatKind::Image:
                return format.customName.empty() ? IO::successStatus() : status(ErrorCode::InvalidArgument);
            case Transfer::FormatKind::Custom:
                if (format.customName.empty() || format.customName.find('\0') != std::string_view::npos)
                    return status(ErrorCode::InvalidArgument);
                try
                {
                    std::wstring wide;
                    DWORD nativeCode = 0;
                    return utf8ToWide(format.customName, wide, nativeCode) ? IO::successStatus() : status(ErrorCode::InvalidArgument, nativeCode);
                }
                catch (const std::bad_alloc &)
                {
                    return status(ErrorCode::OutOfMemory);
                }
                catch (...)
                {
                    return status(ErrorCode::Unknown);
                }
            }
            return status(ErrorCode::InvalidArgument);
        }

        [[nodiscard]] bool registeredName(UINT format, std::wstring &name, DWORD &nativeCode)
        {
            if (format < 0xC000U)
                return false;
            std::array<wchar_t, 256> buffer{};
            SetLastError(ERROR_SUCCESS);
            const int length = GetClipboardFormatNameW(format, buffer.data(), static_cast<int>(buffer.size()));
            if (length <= 0)
            {
                nativeCode = GetLastError();
                return false;
            }
            name.assign(buffer.data(), static_cast<std::size_t>(length));
            return true;
        }

        [[nodiscard]] UINT findRegisteredFormat(std::wstring_view requested, DWORD &nativeCode)
        {
            nativeCode = ERROR_SUCCESS;
            UINT current = 0;
            for (;;)
            {
                if (Detail::consumeFailure(TestHooks::FailurePoint::ClipboardEnumeration))
                {
                    nativeCode = ERROR_GEN_FAILURE;
                    return 0;
                }
                SetLastError(ERROR_SUCCESS);
                current = EnumClipboardFormats(current);
                if (current == 0)
                {
                    nativeCode = GetLastError();
                    return 0;
                }
                if (current < 0xC000U)
                    continue;
                std::wstring candidate;
                DWORD nameCode = 0;
                if (!registeredName(current, candidate, nameCode))
                {
                    nativeCode = nameCode;
                    return 0;
                }
                if (CompareStringOrdinal(
                        candidate.data(),
                        static_cast<int>(candidate.size()),
                        requested.data(),
                        static_cast<int>(requested.size()),
                        TRUE) == CSTR_EQUAL)
                    return current;
            }
        }

        [[nodiscard]] IO::Types::Status allocateAndCopy(std::span<const std::byte> bytes, GlobalMemory &memory) noexcept
        {
            // A zero-byte GMEM_MOVEABLE allocation is a discarded handle that SetClipboardData
            // rejects. A one-byte substitute would corrupt the opaque extent, while nullptr would
            // opt into delayed rendering (deliberately outside this feature's producer contract).
            if (bytes.empty())
                return status(ErrorCode::Unsupported);
            if (!memory.allocate(bytes.size()))
                return status(ErrorCode::OutOfMemory, GetLastError());
            if (bytes.empty())
                return IO::successStatus();
            GlobalLock lock(memory.get());
            if (lock.get() == nullptr)
                return status(ErrorCode::LockFailed, GetLastError());
            const auto destination = mutableNativeSpan<std::byte>(lock.get(), bytes.size());
            std::ranges::copy(bytes, destination.begin());
            return IO::successStatus();
        }

        [[nodiscard]] IO::Types::Status prepareItem(const Transfer::ItemView &item, PreparedItem &prepared)
        {
            if (Detail::consumeFailure(TestHooks::FailurePoint::ClipboardAllocation))
                return status(ErrorCode::OutOfMemory);
            if ((std::holds_alternative<Transfer::TextView>(item) && Detail::consumeFailure(TestHooks::FailurePoint::ClipboardTextConversion)) ||
                (std::holds_alternative<Transfer::FileListView>(item) && Detail::consumeFailure(TestHooks::FailurePoint::ClipboardPathConversion)) ||
                (std::holds_alternative<Transfer::ImageView>(item) && Detail::consumeFailure(TestHooks::FailurePoint::ClipboardImagePreparation)))
                return status(ErrorCode::EncodingFailed);
            if (std::holds_alternative<Transfer::CustomView>(item) && Detail::consumeFailure(TestHooks::FailurePoint::ClipboardRegistration))
                return status(ErrorCode::NativeFailure, ERROR_GEN_FAILURE);
            DataTransfer::PreparedItem shared;
            IO::Types::Status result = DataTransfer::prepare(item, shared);
            if (!result.ok())
                return result;
            prepared.format = shared.format;
            return allocateAndCopy(shared.bytes, prepared.memory);
        }

        [[nodiscard]] IO::Types::Status materializeClipboardItem(UINT nativeFormat, const Transfer::Format &format, Transfer::Item &item) noexcept
        {
            SetLastError(ERROR_SUCCESS);
            HGLOBAL memory = static_cast<HGLOBAL>(GetClipboardData(nativeFormat));
            if (!memory)
                return status(ErrorCode::ReadFailed, GetLastError());
            return DataTransfer::materializeGlobal(memory, format, item);
        }

        [[nodiscard]] IO::Types::Status closePreservingPrimary(ClipboardSession &session, IO::Types::Status primary) noexcept
        {
            IO::Types::Status cleanup = session.close();
            return primary.ok() ? std::move(cleanup) : std::move(primary);
        }

        [[nodiscard]] bool isTextFormat(UINT format) noexcept
        {
            return format == CF_TEXT || format == CF_OEMTEXT || format == CF_UNICODETEXT;
        }

        [[nodiscard]] bool isImageFormat(UINT format) noexcept
        {
            return format == CF_BITMAP || format == CF_DIB || format == CF_DIBV5;
        }

    } // namespace

    Types::Clipboard::FormatResult clipboardHasFormat(Transfer::FormatView format, std::chrono::milliseconds timeout) noexcept
    {
        if (!validTimeout(timeout))
            return failure<ClipboardTypes::FormatResult>(ErrorCode::InvalidArgument);
        IO::Types::Status validation = validateFormatView(format);
        if (!validation.ok())
            return {.status = std::move(validation), .available = false};
        try
        {
            ClipboardSession session;
            IO::Types::Status openStatus = session.open(nullptr, timeout);
            if (!openStatus.ok())
                return {.status = std::move(openStatus), .available = false};
            bool available = false;
            IO::Types::Status result = IO::successStatus();
            switch (format.kind)
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
                if (!utf8ToWide(format.customName, name, nativeCode))
                    result = status(ErrorCode::InvalidArgument, nativeCode);
                else
                {
                    const UINT found = findRegisteredFormat(name, nativeCode);
                    available = found != 0;
                    if (found == 0 && nativeCode != ERROR_SUCCESS)
                        result = status(ErrorCode::ReadFailed, nativeCode);
                }
                break;
            }
            }
            result = closePreservingPrimary(session, std::move(result));
            return {.status = std::move(result), .available = available};
        }
        catch (const std::bad_alloc &)
        {
            return failure<ClipboardTypes::FormatResult>(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return failure<ClipboardTypes::FormatResult>(ErrorCode::Unknown);
        }
    }

    Types::Clipboard::FormatsResult clipboardGetFormats(std::chrono::milliseconds timeout) noexcept
    {
        if (!validTimeout(timeout))
            return failure<ClipboardTypes::FormatsResult>(ErrorCode::InvalidArgument);
        ClipboardTypes::FormatsResult result;
        try
        {
            ClipboardSession session;
            result.status = session.open(nullptr, timeout);
            if (!result.status.ok())
                return result;
            bool textSeen = false;
            bool filesSeen = false;
            bool imageSeen = false;
            UINT current = 0;
            for (;;)
            {
                if (Detail::consumeFailure(TestHooks::FailurePoint::ClipboardEnumeration) ||
                    Detail::consumeClipboardEnumerationFailure(result.formats.size()))
                {
                    result.status = status(ErrorCode::ReadFailed, ERROR_GEN_FAILURE);
                    break;
                }
                SetLastError(ERROR_SUCCESS);
                current = EnumClipboardFormats(current);
                if (current == 0)
                {
                    const DWORD nativeCode = GetLastError();
                    if (nativeCode != ERROR_SUCCESS)
                        result.status = status(ErrorCode::ReadFailed, nativeCode);
                    break;
                }
                if (isTextFormat(current))
                {
                    if (!textSeen)
                    {
                        result.formats.push_back({Transfer::FormatKind::Text, {}});
                        textSeen = true;
                    }
                }
                else if (current == CF_HDROP)
                {
                    if (!filesSeen)
                    {
                        result.formats.push_back({Transfer::FormatKind::FileList, {}});
                        filesSeen = true;
                    }
                }
                else if (isImageFormat(current))
                {
                    if (!imageSeen)
                    {
                        result.formats.push_back({Transfer::FormatKind::Image, {}});
                        imageSeen = true;
                    }
                }
                else if (current >= 0xC000U)
                {
                    std::wstring wide;
                    DWORD nativeCode = 0;
                    if (!registeredName(current, wide, nativeCode))
                    {
                        result.status = status(ErrorCode::ReadFailed, nativeCode);
                        break;
                    }
                    std::string name;
                    if (!wideToUtf8(wide, name, nativeCode))
                    {
                        result.status = status(ErrorCode::EncodingFailed, nativeCode);
                        break;
                    }
                    result.formats.push_back({Transfer::FormatKind::Custom, std::move(name)});
                }
            }
            result.status = closePreservingPrimary(session, std::move(result.status));
        }
        catch (const std::bad_alloc &)
        {
            result.status = status(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            result.status = status(ErrorCode::Unknown);
        }
        return result;
    }

    Types::Clipboard::TextResult clipboardReadText(std::chrono::milliseconds timeout) noexcept
    {
        if (!validTimeout(timeout))
            return failure<ClipboardTypes::TextResult>(ErrorCode::InvalidArgument);
        ClipboardTypes::TextResult result;
        try
        {
            ClipboardSession session;
            result.status = session.open(nullptr, timeout);
            if (!result.status.ok())
                return result;
            if (IsClipboardFormatAvailable(CF_UNICODETEXT) == FALSE)
                result.status = status(ErrorCode::NotFound);
            else if (Detail::consumeFailure(TestHooks::FailurePoint::ClipboardRead))
                result.status = status(ErrorCode::ReadFailed, ERROR_GEN_FAILURE);
            else
            {
                Transfer::Item item;
                result.status = materializeClipboardItem(CF_UNICODETEXT, {Transfer::FormatKind::Text, {}}, item);
                if (result.status.ok())
                    result.text = std::move(std::get<Transfer::Text>(item).text);
            }
            result.status = closePreservingPrimary(session, std::move(result.status));
        }
        catch (const std::bad_alloc &)
        {
            result.status = status(ErrorCode::OutOfMemory);
            result.text.clear();
        }
        catch (...)
        {
            result.status = status(ErrorCode::Unknown);
            result.text.clear();
        }
        return result;
    }

    Types::Clipboard::FileListResult clipboardReadFiles(std::chrono::milliseconds timeout) noexcept
    {
        if (!validTimeout(timeout))
            return failure<ClipboardTypes::FileListResult>(ErrorCode::InvalidArgument);
        ClipboardTypes::FileListResult result;
        try
        {
            ClipboardSession session;
            result.status = session.open(nullptr, timeout);
            if (!result.status.ok())
                return result;
            if (IsClipboardFormatAvailable(CF_HDROP) == FALSE)
                result.status = status(ErrorCode::NotFound);
            else if (Detail::consumeFailure(TestHooks::FailurePoint::ClipboardRead))
                result.status = status(ErrorCode::ReadFailed, ERROR_GEN_FAILURE);
            else
            {
                Transfer::Item item;
                result.status = materializeClipboardItem(CF_HDROP, {Transfer::FormatKind::FileList, {}}, item);
                if (result.status.ok())
                    result.paths = std::move(std::get<Transfer::FileList>(item).paths);
            }
            result.status = closePreservingPrimary(session, std::move(result.status));
        }
        catch (const std::bad_alloc &)
        {
            result.status = status(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            result.status = status(ErrorCode::Unknown);
        }
        return result;
    }

    Types::Clipboard::ImageResult clipboardReadImage(std::chrono::milliseconds timeout) noexcept
    {
        if (!validTimeout(timeout))
            return failure<ClipboardTypes::ImageResult>(ErrorCode::InvalidArgument);
        ClipboardTypes::ImageResult result;
        try
        {
            ClipboardSession session;
            result.status = session.open(nullptr, timeout);
            if (!result.status.ok())
                return result;
            const UINT format = IsClipboardFormatAvailable(CF_DIBV5) != FALSE ? CF_DIBV5 : IsClipboardFormatAvailable(CF_DIB) != FALSE ? CF_DIB : 0;
            if (format == 0)
                result.status = IsClipboardFormatAvailable(CF_BITMAP) != FALSE ? status(ErrorCode::Unsupported) : status(ErrorCode::NotFound);
            else if (Detail::consumeFailure(TestHooks::FailurePoint::ClipboardRead))
                result.status = status(ErrorCode::ReadFailed, ERROR_GEN_FAILURE);
            else
            {
                Transfer::Item item;
                result.status = materializeClipboardItem(format, {Transfer::FormatKind::Image, {}}, item);
                if (result.status.ok())
                    result.image = std::move(std::get<Transfer::Image>(item));
            }
            result.status = closePreservingPrimary(session, std::move(result.status));
        }
        catch (const std::bad_alloc &)
        {
            result.status = status(ErrorCode::OutOfMemory);
            result.image = {};
        }
        catch (...)
        {
            result.status = status(ErrorCode::Unknown);
            result.image = {};
        }
        return result;
    }

    Types::Clipboard::CustomDataResult clipboardReadCustomData(std::string_view formatName, std::chrono::milliseconds timeout) noexcept
    {
        if (!validTimeout(timeout))
            return failure<ClipboardTypes::CustomDataResult>(ErrorCode::InvalidArgument);
        IO::Types::Status validation = validateFormatView({Transfer::FormatKind::Custom, formatName});
        if (!validation.ok())
            return {.status = std::move(validation), .bytes = {}};
        ClipboardTypes::CustomDataResult result;
        try
        {
            std::wstring wide;
            DWORD nativeCode = 0;
            if (!utf8ToWide(formatName, wide, nativeCode))
                return failure<ClipboardTypes::CustomDataResult>(ErrorCode::InvalidArgument, nativeCode);
            ClipboardSession session;
            result.status = session.open(nullptr, timeout);
            if (!result.status.ok())
                return result;
            const UINT format = findRegisteredFormat(wide, nativeCode);
            if (format == 0)
                result.status = nativeCode == ERROR_SUCCESS ? status(ErrorCode::NotFound) : status(ErrorCode::ReadFailed, nativeCode);
            else if (Detail::consumeFailure(TestHooks::FailurePoint::ClipboardRead))
                result.status = status(ErrorCode::ReadFailed, ERROR_GEN_FAILURE);
            else
            {
                Transfer::Item item;
                result.status = materializeClipboardItem(format, {Transfer::FormatKind::Custom, std::string(formatName)}, item);
                if (result.status.ok())
                    result.bytes = std::move(std::get<Transfer::CustomData>(item).bytes);
            }
            result.status = closePreservingPrimary(session, std::move(result.status));
        }
        catch (const std::bad_alloc &)
        {
            result.status = status(ErrorCode::OutOfMemory);
            result.bytes.clear();
        }
        catch (...)
        {
            result.status = status(ErrorCode::Unknown);
            result.bytes.clear();
        }
        return result;
    }

    Types::Clipboard::WriteResult clipboardWrite(std::span<const Transfer::ItemView> items, std::chrono::milliseconds timeout) noexcept
    {
        ClipboardTypes::WriteResult result;
        if (!validTimeout(timeout) || items.empty())
        {
            result.status = status(ErrorCode::InvalidArgument);
            return result;
        }
        try
        {
            std::vector<PreparedItem> prepared;
            prepared.reserve(items.size());
            std::unordered_set<UINT> identities;
            identities.reserve(items.size());
            for (const Transfer::ItemView &item : items)
            {
                PreparedItem native;
                result.status = prepareItem(item, native);
                if (!result.status.ok())
                    return result;
                if (!identities.insert(native.format).second)
                {
                    result.status = status(ErrorCode::InvalidArgument);
                    return result;
                }
                prepared.push_back(std::move(native));
            }

            if (Detail::consumeFailure(TestHooks::FailurePoint::ClipboardOwnerCreation))
            {
                result.status = status(ErrorCode::OpenFailed, ERROR_GEN_FAILURE);
                return result;
            }
            PublicationOwner owner;
            if (owner.get() == nullptr)
            {
                result.status = status(ErrorCode::OpenFailed, GetLastError());
                return result;
            }
            ClipboardSession session;
            result.status = session.open(owner.get(), timeout);
            if (!result.status.ok())
                return result;
            SetLastError(ERROR_SUCCESS);
            if (Detail::consumeFailure(TestHooks::FailurePoint::ClipboardClear) || EmptyClipboard() == FALSE)
            {
                result.status = status(ErrorCode::WriteFailed, GetLastError());
                result.status = closePreservingPrimary(session, std::move(result.status));
                return result;
            }
            result.commitState = ClipboardTypes::CommitState::Cleared;
            for (std::size_t index = 0; index < prepared.size(); ++index)
            {
                PreparedItem &item = prepared[index];
                SetLastError(ERROR_SUCCESS);
                if (Detail::consumeClipboardPublicationFailure(index) || SetClipboardData(item.format, item.memory.get()) == nullptr)
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
        catch (const std::bad_alloc &)
        {
            result.status = status(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            result.status = status(ErrorCode::Unknown);
        }
        return result;
    }

    Types::Clipboard::ClearResult clipboardClear(std::chrono::milliseconds timeout) noexcept
    {
        ClipboardTypes::ClearResult result;
        if (!validTimeout(timeout))
        {
            result.status = status(ErrorCode::InvalidArgument);
            return result;
        }
        if (Detail::consumeFailure(TestHooks::FailurePoint::ClipboardOwnerCreation))
        {
            result.status = status(ErrorCode::OpenFailed, ERROR_GEN_FAILURE);
            return result;
        }
        PublicationOwner owner;
        if (owner.get() == nullptr)
        {
            result.status = status(ErrorCode::OpenFailed, GetLastError());
            return result;
        }
        ClipboardSession session;
        result.status = session.open(owner.get(), timeout);
        if (!result.status.ok())
            return result;
        SetLastError(ERROR_SUCCESS);
        if (Detail::consumeFailure(TestHooks::FailurePoint::ClipboardClear) || EmptyClipboard() == FALSE)
            result.status = status(ErrorCode::WriteFailed, GetLastError());
        else
        {
            result.status = IO::successStatus();
            result.cleared = true;
        }
        result.status = closePreservingPrimary(session, std::move(result.status));
        return result;
    }
} // namespace GameWIP::Desktop::Detail::Platform
