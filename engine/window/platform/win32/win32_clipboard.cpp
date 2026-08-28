/// @file win32_clipboard.cpp
/// @brief Win32 Clipboard service backend and native data-transfer conversion.

#include "window/internal/clipboard_platform.h"
#include "window/internal/window_test_hooks.h"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <limits>
#include <new>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <variant>

namespace GameWIP::Window::Detail::Platform
{
    namespace
    {
        using ErrorCode = IO::Types::ErrorCode;
        namespace Transfer = Types::DataTransfer;
        namespace ClipboardTypes = Types::Clipboard;

        constexpr auto kRetryInterval = std::chrono::milliseconds{5};

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

        [[nodiscard]] bool checkedAdd(std::size_t left, std::size_t right, std::size_t &result) noexcept
        {
            if (left > std::numeric_limits<std::size_t>::max() - right)
                return false;
            result = left + right;
            return true;
        }

        [[nodiscard]] bool checkedMultiply(std::size_t left, std::size_t right, std::size_t &result) noexcept
        {
            if (left != 0 && right > std::numeric_limits<std::size_t>::max() / left)
                return false;
            result = left * right;
            return true;
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

        [[nodiscard]] bool isValidWide(std::wstring_view text, DWORD &nativeCode) noexcept
        {
            nativeCode = ERROR_SUCCESS;
            if (text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            {
                nativeCode = ERROR_INSUFFICIENT_BUFFER;
                return false;
            }
            if (text.empty())
                return true;
            if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr) <= 0)
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
            std::memcpy(lock.get(), bytes.data(), bytes.size());
            return IO::successStatus();
        }

        [[nodiscard]] IO::Types::Status prepareText(const Transfer::TextView &item, PreparedItem &prepared)
        {
            if (Detail::consumeFailure(TestHooks::FailurePoint::ClipboardAllocation))
                return status(ErrorCode::OutOfMemory);
            if (Detail::consumeFailure(TestHooks::FailurePoint::ClipboardTextConversion))
                return status(ErrorCode::EncodingFailed);
            if (item.text.find('\0') != std::string_view::npos)
                return status(ErrorCode::InvalidArgument);
            std::wstring wide;
            DWORD nativeCode = 0;
            if (!utf8ToWide(item.text, wide, nativeCode))
                return status(ErrorCode::InvalidArgument, nativeCode);
            std::size_t units = 0;
            std::size_t bytes = 0;
            if (!checkedAdd(wide.size(), 1, units) || !checkedMultiply(units, sizeof(wchar_t), bytes))
                return status(ErrorCode::SizeLimitExceeded);
            if (!prepared.memory.allocate(bytes))
                return status(ErrorCode::OutOfMemory, GetLastError());
            GlobalLock lock(prepared.memory.get());
            if (lock.get() == nullptr)
                return status(ErrorCode::LockFailed, GetLastError());
            if (!wide.empty())
                std::memcpy(lock.get(), wide.data(), wide.size() * sizeof(wchar_t));
            static_cast<wchar_t *>(lock.get())[wide.size()] = L'\0';
            prepared.format = CF_UNICODETEXT;
            return IO::successStatus();
        }

        [[nodiscard]] IO::Types::Status prepareFiles(const Transfer::FileListView &item, PreparedItem &prepared)
        {
            if (Detail::consumeFailure(TestHooks::FailurePoint::ClipboardAllocation))
                return status(ErrorCode::OutOfMemory);
            if (Detail::consumeFailure(TestHooks::FailurePoint::ClipboardPathConversion))
                return status(ErrorCode::EncodingFailed);
            if (item.paths.empty())
                return status(ErrorCode::InvalidArgument);
            std::vector<std::wstring> paths;
            paths.reserve(item.paths.size());
            std::size_t totalUnits = 1;
            for (const FileSystem::Types::Path &path : item.paths)
            {
                if (!path.is_absolute())
                    return status(ErrorCode::InvalidArgument);
                std::wstring native = path.native();
                if (native.find(L'\0') != std::wstring::npos)
                    return status(ErrorCode::InvalidArgument);
                std::size_t withTerminator = 0;
                if (!checkedAdd(native.size(), 1, withTerminator) || !checkedAdd(totalUnits, withTerminator, totalUnits))
                    return status(ErrorCode::SizeLimitExceeded);
                paths.push_back(std::move(native));
            }
            std::size_t pathBytes = 0;
            std::size_t totalBytes = 0;
            if (!checkedMultiply(totalUnits, sizeof(wchar_t), pathBytes) || !checkedAdd(sizeof(DROPFILES), pathBytes, totalBytes))
                return status(ErrorCode::SizeLimitExceeded);
            if (totalBytes > static_cast<std::size_t>(std::numeric_limits<UINT>::max()))
                return status(ErrorCode::SizeLimitExceeded);
            if (!prepared.memory.allocate(totalBytes))
                return status(ErrorCode::OutOfMemory, GetLastError());
            GlobalLock lock(prepared.memory.get());
            if (lock.get() == nullptr)
                return status(ErrorCode::LockFailed, GetLastError());
            auto *drop = static_cast<DROPFILES *>(lock.get());
            *drop = {};
            drop->pFiles = sizeof(DROPFILES);
            drop->fWide = TRUE;
            auto *destination = reinterpret_cast<wchar_t *>(static_cast<std::byte *>(lock.get()) + sizeof(DROPFILES));
            for (const std::wstring &path : paths)
            {
                if (!path.empty())
                    std::memcpy(destination, path.data(), path.size() * sizeof(wchar_t));
                destination += path.size();
                *destination++ = L'\0';
            }
            *destination = L'\0';
            prepared.format = CF_HDROP;
            return IO::successStatus();
        }

        [[nodiscard]] IO::Types::Status validateImage(const Transfer::ImageView &item, std::size_t &packedRow, std::size_t &stride) noexcept
        {
            if (item.size.width == 0 || item.size.height == 0 ||
                !checkedMultiply(static_cast<std::size_t>(item.size.width), std::size_t{4}, packedRow))
                return status(ErrorCode::InvalidArgument);
            stride = item.rowStrideBytes == 0 ? packedRow : item.rowStrideBytes;
            if (stride < packedRow)
                return status(ErrorCode::InvalidArgument);
            std::size_t expected = 0;
            if (!checkedMultiply(stride, static_cast<std::size_t>(item.size.height), expected))
                return status(ErrorCode::SizeLimitExceeded);
            return item.rgba8.size() == expected ? IO::successStatus() : status(ErrorCode::InvalidArgument);
        }

        [[nodiscard]] IO::Types::Status prepareImage(const Transfer::ImageView &item, PreparedItem &prepared)
        {
            if (Detail::consumeFailure(TestHooks::FailurePoint::ClipboardAllocation))
                return status(ErrorCode::OutOfMemory);
            if (Detail::consumeFailure(TestHooks::FailurePoint::ClipboardImagePreparation))
                return status(ErrorCode::EncodingFailed);
            std::size_t packedRow = 0;
            std::size_t stride = 0;
            IO::Types::Status result = validateImage(item, packedRow, stride);
            if (!result.ok())
                return result;
            if (item.size.height > static_cast<std::uint32_t>(std::numeric_limits<LONG>::max()) ||
                item.size.width > static_cast<std::uint32_t>(std::numeric_limits<LONG>::max()))
                return status(ErrorCode::SizeLimitExceeded);
            std::size_t pixelBytes = 0;
            std::size_t totalBytes = 0;
            if (!checkedMultiply(packedRow, static_cast<std::size_t>(item.size.height), pixelBytes) ||
                !checkedAdd(sizeof(BITMAPV5HEADER), pixelBytes, totalBytes))
                return status(ErrorCode::SizeLimitExceeded);
            if (pixelBytes > static_cast<std::size_t>(std::numeric_limits<DWORD>::max()))
                return status(ErrorCode::SizeLimitExceeded);
            if (!prepared.memory.allocate(totalBytes))
                return status(ErrorCode::OutOfMemory, GetLastError());
            GlobalLock lock(prepared.memory.get());
            if (lock.get() == nullptr)
                return status(ErrorCode::LockFailed, GetLastError());
            auto *header = static_cast<BITMAPV5HEADER *>(lock.get());
            *header = {};
            header->bV5Size = sizeof(BITMAPV5HEADER);
            header->bV5Width = static_cast<LONG>(item.size.width);
            header->bV5Height = -static_cast<LONG>(item.size.height);
            header->bV5Planes = 1;
            header->bV5BitCount = 32;
            header->bV5Compression = BI_BITFIELDS;
            header->bV5SizeImage = static_cast<DWORD>(pixelBytes);
            header->bV5RedMask = 0x00FF0000U;
            header->bV5GreenMask = 0x0000FF00U;
            header->bV5BlueMask = 0x000000FFU;
            header->bV5AlphaMask = 0xFF000000U;
            header->bV5CSType = 0x73524742U; // LCS_sRGB without the SDK's multi-character literal warning.
            auto *destination = static_cast<std::byte *>(lock.get()) + sizeof(BITMAPV5HEADER);
            for (std::size_t y = 0; y < item.size.height; ++y)
            {
                const auto *source = item.rgba8.data() + y * stride;
                for (std::size_t x = 0; x < item.size.width; ++x)
                {
                    destination[x * 4] = source[x * 4 + 2];
                    destination[x * 4 + 1] = source[x * 4 + 1];
                    destination[x * 4 + 2] = source[x * 4];
                    destination[x * 4 + 3] = source[x * 4 + 3];
                }
                destination += packedRow;
            }
            prepared.format = CF_DIBV5;
            return IO::successStatus();
        }

        [[nodiscard]] IO::Types::Status prepareCustom(const Transfer::CustomView &item, PreparedItem &prepared)
        {
            if (Detail::consumeFailure(TestHooks::FailurePoint::ClipboardAllocation))
                return status(ErrorCode::OutOfMemory);
            if (item.formatName.empty() || item.formatName.find('\0') != std::string_view::npos)
                return status(ErrorCode::InvalidArgument);
            std::wstring wide;
            DWORD nativeCode = 0;
            if (!utf8ToWide(item.formatName, wide, nativeCode))
                return status(ErrorCode::InvalidArgument, nativeCode);
            SetLastError(ERROR_SUCCESS);
            if (Detail::consumeFailure(TestHooks::FailurePoint::ClipboardRegistration))
                return status(ErrorCode::NativeFailure, ERROR_GEN_FAILURE);
            prepared.format = RegisterClipboardFormatW(wide.c_str());
            if (prepared.format == 0)
                return status(ErrorCode::NativeFailure, GetLastError());
            return allocateAndCopy(item.bytes, prepared.memory);
        }

        [[nodiscard]] IO::Types::Status prepareItem(const Transfer::ItemView &item, PreparedItem &prepared)
        {
            return std::visit(
                [&](const auto &value) -> IO::Types::Status
                {
                    using Value = std::remove_cvref_t<decltype(value)>;
                    if constexpr (std::is_same_v<Value, Transfer::TextView>)
                        return prepareText(value, prepared);
                    else if constexpr (std::is_same_v<Value, Transfer::FileListView>)
                        return prepareFiles(value, prepared);
                    else if constexpr (std::is_same_v<Value, Transfer::ImageView>)
                        return prepareImage(value, prepared);
                    else
                        return prepareCustom(value, prepared);
                },
                item);
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

        [[nodiscard]] std::size_t dibRowStride(std::size_t width, std::size_t bitsPerPixel, bool &valid) noexcept
        {
            std::size_t bits = 0;
            std::size_t rounded = 0;
            valid = checkedMultiply(width, bitsPerPixel, bits) && checkedAdd(bits, 31, rounded);
            return valid ? (rounded / 32) * 4 : 0;
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
                SetLastError(ERROR_SUCCESS);
                HGLOBAL memory = static_cast<HGLOBAL>(GetClipboardData(CF_UNICODETEXT));
                if (memory == nullptr)
                    result.status = status(ErrorCode::ReadFailed, GetLastError());
                else
                {
                    const std::size_t byteSize = GlobalSize(memory);
                    GlobalLock lock(memory);
                    if (lock.get() == nullptr && byteSize != 0)
                        result.status = status(ErrorCode::LockFailed, GetLastError());
                    else if (byteSize < sizeof(wchar_t) || byteSize % sizeof(wchar_t) != 0)
                        result.status = status(ErrorCode::EncodingFailed);
                    else
                    {
                        const auto *text = static_cast<const wchar_t *>(lock.get());
                        const std::size_t capacity = byteSize / sizeof(wchar_t);
                        const auto *terminator = std::find(text, text + capacity, L'\0');
                        if (terminator == text + capacity)
                            result.status = status(ErrorCode::EncodingFailed);
                        else
                        {
                            DWORD nativeCode = 0;
                            if (!wideToUtf8({text, static_cast<std::size_t>(terminator - text)}, result.text, nativeCode))
                                result.status = status(ErrorCode::EncodingFailed, nativeCode);
                            else
                                result.status = IO::successStatus();
                        }
                    }
                }
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
                SetLastError(ERROR_SUCCESS);
                HDROP drop = static_cast<HDROP>(GetClipboardData(CF_HDROP));
                if (drop == nullptr)
                    result.status = status(ErrorCode::ReadFailed, GetLastError());
                else
                {
                    const UINT count = DragQueryFileW(drop, 0xFFFFFFFFU, nullptr, 0);
                    if (count == 0)
                        result.status = status(ErrorCode::EncodingFailed);
                    else
                        result.status = IO::successStatus();
                    result.paths.reserve(count);
                    for (UINT index = 0; index < count; ++index)
                    {
                        const UINT length = DragQueryFileW(drop, index, nullptr, 0);
                        if (length == 0)
                        {
                            result.status = status(ErrorCode::EncodingFailed);
                            break;
                        }
                        std::wstring path(static_cast<std::size_t>(length) + 1, L'\0');
                        if (DragQueryFileW(drop, index, path.data(), length + 1) != length)
                        {
                            result.status = status(ErrorCode::ReadFailed, GetLastError());
                            break;
                        }
                        path.resize(length);
                        DWORD nativeCode = 0;
                        if (!isValidWide(path, nativeCode))
                        {
                            result.status = status(ErrorCode::EncodingFailed, nativeCode);
                            break;
                        }
                        result.paths.emplace_back(std::move(path));
                    }
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
                SetLastError(ERROR_SUCCESS);
                HGLOBAL memory = static_cast<HGLOBAL>(GetClipboardData(format));
                if (memory == nullptr)
                    result.status = status(ErrorCode::ReadFailed, GetLastError());
                else
                {
                    const std::size_t totalBytes = GlobalSize(memory);
                    GlobalLock lock(memory);
                    if (lock.get() == nullptr)
                        result.status = status(ErrorCode::LockFailed, GetLastError());
                    else if (totalBytes < sizeof(BITMAPINFOHEADER))
                        result.status = status(ErrorCode::ReadFailed);
                    else
                    {
                        const auto *header = static_cast<const BITMAPINFOHEADER *>(lock.get());
                        const bool dimensionsValid = header->biSize >= sizeof(BITMAPINFOHEADER) && header->biSize <= totalBytes &&
                                                     header->biWidth > 0 && header->biHeight != 0 && header->biHeight != LONG_MIN;
                        const bool encodingSupported =
                            header->biPlanes == 1 &&
                            ((header->biBitCount == 24 && header->biCompression == BI_RGB) ||
                             (header->biBitCount == 32 && (header->biCompression == BI_RGB || header->biCompression == BI_BITFIELDS)));
                        if (!dimensionsValid)
                            result.status = status(ErrorCode::ReadFailed);
                        else if (!encodingSupported)
                            result.status = status(ErrorCode::Unsupported);
                        else
                        {
                            const std::size_t width = static_cast<std::size_t>(header->biWidth);
                            const std::size_t height = static_cast<std::size_t>(header->biHeight < 0 ? -header->biHeight : header->biHeight);
                            bool strideValid = false;
                            const std::size_t sourceStride = dibRowStride(width, header->biBitCount, strideValid);
                            std::size_t sourceBytes = 0;
                            std::size_t packedRow = 0;
                            std::size_t outputBytes = 0;
                            std::size_t pixelOffset = header->biSize;
                            bool explicitAlpha = false;
                            bool masksSupported = true;
                            if (header->biCompression == BI_BITFIELDS && header->biSize == sizeof(BITMAPINFOHEADER))
                            {
                                std::size_t masksEnd = 0;
                                strideValid = strideValid && checkedAdd(pixelOffset, 3 * sizeof(DWORD), masksEnd) && masksEnd <= totalBytes;
                                if (strideValid)
                                {
                                    const auto *masks = reinterpret_cast<const DWORD *>(static_cast<const std::byte *>(lock.get()) + pixelOffset);
                                    masksSupported = masks[0] == 0x00FF0000U && masks[1] == 0x0000FF00U && masks[2] == 0x000000FFU;
                                    pixelOffset = masksEnd;
                                }
                            }
                            else if (header->biCompression == BI_BITFIELDS)
                            {
                                if (header->biSize < sizeof(BITMAPV4HEADER))
                                    masksSupported = false;
                                else
                                {
                                    const auto *extended = reinterpret_cast<const BITMAPV4HEADER *>(header);
                                    masksSupported = extended->bV4RedMask == 0x00FF0000U && extended->bV4GreenMask == 0x0000FF00U &&
                                                     extended->bV4BlueMask == 0x000000FFU &&
                                                     (extended->bV4AlphaMask == 0 || extended->bV4AlphaMask == 0xFF000000U);
                                    explicitAlpha = extended->bV4AlphaMask == 0xFF000000U;
                                }
                            }
                            if (!masksSupported)
                                result.status = status(ErrorCode::Unsupported);
                            else if (
                                !strideValid || !checkedMultiply(sourceStride, height, sourceBytes) ||
                                !checkedMultiply(width, std::size_t{4}, packedRow) || !checkedMultiply(packedRow, height, outputBytes) ||
                                pixelOffset > totalBytes || sourceBytes > totalBytes - pixelOffset)
                                result.status = status(ErrorCode::ReadFailed);
                            else
                            {
                                result.image.size = {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
                                result.image.rgba8.resize(outputBytes);
                                const auto *pixels = static_cast<const std::byte *>(lock.get()) + pixelOffset;
                                for (std::size_t y = 0; y < height; ++y)
                                {
                                    const std::size_t sourceY = header->biHeight < 0 ? y : height - 1 - y;
                                    const auto *source = pixels + sourceY * sourceStride;
                                    auto *destination = result.image.rgba8.data() + y * packedRow;
                                    for (std::size_t x = 0; x < width; ++x)
                                    {
                                        const std::size_t sourcePixel = x * (header->biBitCount / 8);
                                        destination[x * 4] = source[sourcePixel + 2];
                                        destination[x * 4 + 1] = source[sourcePixel + 1];
                                        destination[x * 4 + 2] = source[sourcePixel];
                                        destination[x * 4 + 3] = explicitAlpha ? source[sourcePixel + 3] : std::byte{0xFF};
                                    }
                                }
                                result.status = IO::successStatus();
                            }
                        }
                    }
                }
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
                SetLastError(ERROR_SUCCESS);
                HGLOBAL memory = static_cast<HGLOBAL>(GetClipboardData(format));
                if (memory == nullptr)
                    result.status = status(ErrorCode::ReadFailed, GetLastError());
                else
                {
                    const std::size_t byteSize = GlobalSize(memory);
                    if (byteSize == 0)
                        result.status = IO::successStatus();
                    else
                    {
                        GlobalLock lock(memory);
                        if (lock.get() == nullptr)
                            result.status = status(ErrorCode::LockFailed, GetLastError());
                        else
                        {
                            result.bytes.resize(byteSize);
                            std::memcpy(result.bytes.data(), lock.get(), byteSize);
                            result.status = IO::successStatus();
                        }
                    }
                }
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
} // namespace GameWIP::Window::Detail::Platform
