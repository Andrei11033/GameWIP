/// @file win32_data_transfer.cpp
/// @brief Shared Win32 conversion between portable DataTransfer values and HGLOBAL wire formats.

#include "desktop/platform/win32/internal/win32_data_transfer.h"
#include "desktop/platform/win32/internal/win32_window_backend.h"

#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <array>
#include <limits>
#include <new>
#include <string>
#include <variant>

namespace GameWIP::Desktop::Detail::Platform::DataTransfer
{
    // ------------------------------------------------------------
    // Checked native storage and ownership
    // ------------------------------------------------------------

    namespace
    {
        using ErrorCode = IO::Types::ErrorCode;
        namespace Transfer = Types::DataTransfer;
        [[nodiscard]] IO::Types::Status failure(ErrorCode code, DWORD native = 0) noexcept
        {
            return IO::makeStatus(code, native);
        }
        [[nodiscard]] bool multiply(std::size_t a, std::size_t b, std::size_t &out) noexcept
        {
            if (a && b > std::numeric_limits<std::size_t>::max() / a)
                return false;
            out = a * b;
            return true;
        }
        [[nodiscard]] bool add(std::size_t a, std::size_t b, std::size_t &out) noexcept
        {
            if (a > std::numeric_limits<std::size_t>::max() - b)
                return false;
            out = a + b;
            return true;
        }
        template <class T> [[nodiscard]] std::span<T> span(void *p, std::size_t n) noexcept
        {
#if defined(__clang__)
#pragma clang unsafe_buffer_usage begin
#endif
            return {static_cast<T *>(p), n};
#if defined(__clang__)
#pragma clang unsafe_buffer_usage end
#endif
        }
        template <class T> [[nodiscard]] std::span<const T> span(const void *p, std::size_t n) noexcept
        {
#if defined(__clang__)
#pragma clang unsafe_buffer_usage begin
#endif
            return {static_cast<const T *>(p), n};
#if defined(__clang__)
#pragma clang unsafe_buffer_usage end
#endif
        }

        class GlobalMemoryLock final
        {
        public:
            explicit GlobalMemoryLock(HGLOBAL global) noexcept
                : global_(global)
                , memory_(GlobalLock(global))
            {
            }
            ~GlobalMemoryLock() noexcept
            {
                if (memory_ != nullptr)
                    GlobalUnlock(global_);
            }
            GlobalMemoryLock(const GlobalMemoryLock &) = delete;
            GlobalMemoryLock &operator=(const GlobalMemoryLock &) = delete;
            [[nodiscard]] void *get() const noexcept
            {
                return memory_;
            }

        private:
            HGLOBAL global_ = nullptr;
            void *memory_ = nullptr;
        };

        template <class Interface> class ComOwner final
        {
        public:
            ~ComOwner() noexcept
            {
                if (value_ != nullptr)
                    value_->Release();
            }
            [[nodiscard]] Interface **out() noexcept
            {
                return &value_;
            }
            [[nodiscard]] Interface *operator->() const noexcept
            {
                return value_;
            }
            [[nodiscard]] explicit operator bool() const noexcept
            {
                return value_ != nullptr;
            }

        private:
            Interface *value_ = nullptr;
        };

        class TargetDeviceOwner final
        {
        public:
            explicit TargetDeviceOwner(FORMATETC &format) noexcept
                : format_(format)
            {
            }
            ~TargetDeviceOwner() noexcept
            {
                if (format_.ptd != nullptr)
                    CoTaskMemFree(format_.ptd);
                format_.ptd = nullptr;
            }

        private:
            FORMATETC &format_;
        };
        class Medium final
        {
        public:
            ~Medium() noexcept
            {
                if (valid_)
                    ReleaseStgMedium(&value_);
            }
            STGMEDIUM *out() noexcept
            {
                return &value_;
            }
            HGLOBAL global() const noexcept
            {
                return valid_ && value_.tymed == TYMED_HGLOBAL ? value_.hGlobal : nullptr;
            }
            void acquired() noexcept
            {
                valid_ = true;
            }

        private:
            STGMEDIUM value_{};
            bool valid_ = false;
        };
        [[nodiscard]] IO::Types::Status getGlobal(IDataObject &object, CLIPFORMAT format, Medium &medium) noexcept
        {
            FORMATETC request{format, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
            const HRESULT result = object.GetData(&request, medium.out());
            if (FAILED(result))
                return failure(ErrorCode::ReadFailed, static_cast<DWORD>(result));
            medium.acquired();
            return medium.global() ? IO::successStatus() : failure(ErrorCode::ReadFailed);
        }
    } // namespace

    // ------------------------------------------------------------
    // Native format identity
    // ------------------------------------------------------------

    CLIPFORMAT nativeFormat(const Transfer::Format &format, IO::Types::Status &result) noexcept
    {
        try
        {
            result = IO::successStatus();
            switch (format.kind)
            {
            case Transfer::FormatKind::Text:
                return CF_UNICODETEXT;
            case Transfer::FormatKind::FileList:
                return CF_HDROP;
            case Transfer::FormatKind::Image:
                return CF_DIBV5;
            case Transfer::FormatKind::Custom:
            {
                if (format.customName.empty() || format.customName.find('\0') != std::string::npos)
                {
                    result = failure(ErrorCode::InvalidArgument);
                    return 0;
                }
                std::wstring wide;
                DWORD code = 0;
                if (!utf8ToUtf16(format.customName, wide, code))
                {
                    result = failure(ErrorCode::InvalidArgument, code);
                    return 0;
                }
                const UINT value = RegisterClipboardFormatW(wide.c_str());
                if (!value)
                    result = failure(ErrorCode::NativeFailure, GetLastError());
                return static_cast<CLIPFORMAT>(value);
            }
            }
            result = failure(ErrorCode::InvalidArgument);
            return 0;
        }
        catch (const std::bad_alloc &)
        {
            result = failure(ErrorCode::OutOfMemory);
            return 0;
        }
        catch (...)
        {
            result = failure(ErrorCode::Unknown);
            return 0;
        }
    }

    bool equivalent(const Transfer::Format &a, const Transfer::Format &b) noexcept
    {
        if (a.kind != b.kind)
            return false;
        if (a.kind != Transfer::FormatKind::Custom)
            return true;
        IO::Types::Status leftStatus;
        IO::Types::Status rightStatus;
        const CLIPFORMAT left = nativeFormat(a, leftStatus);
        const CLIPFORMAT right = nativeFormat(b, rightStatus);
        return leftStatus.ok() && rightStatus.ok() && left == right;
    }

    // ------------------------------------------------------------
    // Source preparation
    // ------------------------------------------------------------

    IO::Types::Status prepare(const Transfer::ItemView &item, PreparedItem &out) noexcept
    {
        try
        {
            return std::visit(
                [&](const auto &value) -> IO::Types::Status
                {
                    using T = std::remove_cvref_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, Transfer::TextView>)
                    {
                        if (value.text.find('\0') != std::string_view::npos)
                            return failure(ErrorCode::InvalidArgument);
                        std::wstring wide;
                        DWORD code = 0;
                        if (!utf8ToUtf16(value.text, wide, code))
                            return failure(ErrorCode::InvalidArgument, code);
                        std::size_t units = 0, bytes = 0;
                        if (!add(wide.size(), 1, units) || !multiply(units, sizeof(wchar_t), bytes))
                            return failure(ErrorCode::SizeLimitExceeded);
                        out.format = CF_UNICODETEXT;
                        out.bytes.assign(bytes, std::byte{});
                        std::ranges::copy(std::as_bytes(std::span{wide}), out.bytes.begin());
                        return IO::successStatus();
                    }
                    else if constexpr (std::is_same_v<T, Transfer::FileListView>)
                    {
                        if (value.paths.empty())
                            return failure(ErrorCode::InvalidArgument);
                        std::size_t units = 1;
                        for (const auto &path : value.paths)
                        {
                            if (!path.is_absolute() || path.native().find(L'\0') != std::wstring::npos)
                                return failure(ErrorCode::InvalidArgument);
                            std::string validated;
                            DWORD code = 0;
                            if (!utf16ToUtf8(path.native(), validated, code))
                                return failure(ErrorCode::InvalidArgument, code);
                            std::size_t next = 0;
                            if (!add(units, path.native().size() + 1, next))
                                return failure(ErrorCode::SizeLimitExceeded);
                            units = next;
                        }
                        std::size_t pathBytes = 0, total = 0;
                        if (!multiply(units, sizeof(wchar_t), pathBytes) || !add(sizeof(DROPFILES), pathBytes, total) || total > UINT_MAX)
                            return failure(ErrorCode::SizeLimitExceeded);
                        out.format = CF_HDROP;
                        out.bytes.assign(total, std::byte{});
                        DROPFILES header{};
                        header.pFiles = sizeof(DROPFILES);
                        header.fWide = TRUE;
                        std::ranges::copy(std::as_bytes(std::span{&header, std::size_t{1}}), out.bytes.begin());
                        std::size_t offset = sizeof(DROPFILES);
                        for (const auto &path : value.paths)
                        {
                            auto bytes = std::as_bytes(std::span{path.native()});
                            std::ranges::copy(bytes, out.bytes.begin() + static_cast<std::ptrdiff_t>(offset));
                            offset += bytes.size() + sizeof(wchar_t);
                        }
                        return IO::successStatus();
                    }
                    else if constexpr (std::is_same_v<T, Transfer::ImageView>)
                    {
                        std::size_t row = 0, inputBytes = 0;
                        if (!value.size.width || !value.size.height || !multiply(value.size.width, 4, row))
                            return failure(ErrorCode::InvalidArgument);
                        const std::size_t stride = value.rowStrideBytes ? value.rowStrideBytes : row;
                        if (stride < row || !multiply(stride, value.size.height, inputBytes) || inputBytes != value.rgba8.size())
                            return failure(ErrorCode::InvalidArgument);
                        std::size_t pixels = 0, total = 0;
                        if (!multiply(row, value.size.height, pixels) || !add(sizeof(BITMAPV5HEADER), pixels, total) ||
                            pixels > std::numeric_limits<DWORD>::max() || value.size.width > static_cast<std::uint32_t>(LONG_MAX) ||
                            value.size.height > static_cast<std::uint32_t>(LONG_MAX))
                            return failure(ErrorCode::SizeLimitExceeded);
                        out.format = CF_DIBV5;
                        out.bytes.assign(total, std::byte{});
                        BITMAPV5HEADER h{};
                        h.bV5Size = sizeof(h);
                        h.bV5Width = static_cast<LONG>(value.size.width);
                        h.bV5Height = -static_cast<LONG>(value.size.height);
                        h.bV5Planes = 1;
                        h.bV5BitCount = 32;
                        h.bV5Compression = BI_BITFIELDS;
                        h.bV5SizeImage = static_cast<DWORD>(pixels);
                        h.bV5RedMask = 0x00FF0000;
                        h.bV5GreenMask = 0x0000FF00;
                        h.bV5BlueMask = 0x000000FF;
                        h.bV5AlphaMask = 0xFF000000;
                        h.bV5CSType = 0x73524742;
                        std::ranges::copy(std::as_bytes(std::span{&h, std::size_t{1}}), out.bytes.begin());
                        for (std::size_t y = 0; y < value.size.height; ++y)
                            for (std::size_t x = 0; x < value.size.width; ++x)
                            {
                                const auto s = value.rgba8.subspan(y * stride + x * 4, 4);
                                auto d = std::span{out.bytes}.subspan(sizeof(h) + y * row + x * 4, 4);
                                d[0] = s[2];
                                d[1] = s[1];
                                d[2] = s[0];
                                d[3] = s[3];
                            }
                        return IO::successStatus();
                    }
                    else
                    {
                        Transfer::Format f{Transfer::FormatKind::Custom, std::string(value.formatName)};
                        IO::Types::Status result;
                        out.format = nativeFormat(f, result);
                        if (!result.ok())
                            return result;
                        if (value.bytes.empty())
                            return failure(ErrorCode::Unsupported);
                        out.bytes.assign(value.bytes.begin(), value.bytes.end());
                        return IO::successStatus();
                    }
                },
                item);
        }
        catch (const std::bad_alloc &)
        {
            return failure(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return failure(ErrorCode::Unknown);
        }
    }

    IO::Types::Status copyToGlobal(const PreparedItem &item, HGLOBAL &output) noexcept
    {
        output = GlobalAlloc(GMEM_MOVEABLE, item.bytes.size());
        if (!output)
            return failure(ErrorCode::OutOfMemory, GetLastError());
        GlobalMemoryLock lock(output);
        if (!lock.get())
        {
            const DWORD code = GetLastError();
            GlobalFree(output);
            output = nullptr;
            return failure(ErrorCode::LockFailed, code);
        }
        std::ranges::copy(item.bytes, span<std::byte>(lock.get(), item.bytes.size()).begin());
        return IO::successStatus();
    }

    // ------------------------------------------------------------
    // Foreign format enumeration
    // ------------------------------------------------------------

    IO::Types::Status formats(IDataObject &object, std::vector<FormatIdentity> &out) noexcept
    {
        try
        {
            ComOwner<IEnumFORMATETC> enumerator;
            HRESULT hr = object.EnumFormatEtc(DATADIR_GET, enumerator.out());
            if (FAILED(hr) || !enumerator)
                return failure(ErrorCode::ReadFailed, static_cast<DWORD>(hr));
            std::size_t seen = 0;
            IO::Types::Status result = IO::successStatus();
            for (;;)
            {
                FORMATETC f{};
                ULONG read = 0;
                hr = enumerator->Next(1, &f, &read);
                TargetDeviceOwner targetDevice(f);
                if (hr != S_OK)
                {
                    if (FAILED(hr))
                        result = failure(ErrorCode::ReadFailed, static_cast<DWORD>(hr));
                    break;
                }
                if (++seen > 256)
                {
                    result = failure(ErrorCode::SizeLimitExceeded);
                    break;
                }
                if (f.dwAspect != DVASPECT_CONTENT || f.lindex != -1 || !(f.tymed & TYMED_HGLOBAL))
                    continue;
                FormatIdentity value;
                if (f.cfFormat == CF_UNICODETEXT)
                {
                    value.portable.kind = Transfer::FormatKind::Text;
                    value.native = CF_UNICODETEXT;
                }
                else if (f.cfFormat == CF_HDROP)
                {
                    value.portable.kind = Transfer::FormatKind::FileList;
                    value.native = CF_HDROP;
                }
                else if (f.cfFormat == CF_DIBV5 || f.cfFormat == CF_DIB)
                {
                    value.portable.kind = Transfer::FormatKind::Image;
                    value.native = CF_DIBV5;
                }
                else if (f.cfFormat >= 0xC000)
                {
                    std::array<wchar_t, 256> name{};
                    int n = GetClipboardFormatNameW(f.cfFormat, name.data(), static_cast<int>(name.size()));
                    if (n <= 0)
                        continue;
                    value.portable.kind = Transfer::FormatKind::Custom;
                    value.native = f.cfFormat;
                    DWORD code = 0;
                    if (!utf16ToUtf8({name.data(), static_cast<std::size_t>(n)}, value.portable.customName, code))
                    {
                        result = failure(ErrorCode::EncodingFailed, code);
                        break;
                    }
                }
                else
                    continue;
                if (std::ranges::none_of(
                        out,
                        [&](const auto &old)
                        {
                            return old.native == value.native;
                        }))
                    out.push_back(std::move(value));
            }
            return result;
        }
        catch (const std::bad_alloc &)
        {
            return failure(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return failure(ErrorCode::Unknown);
        }
    }

    // ------------------------------------------------------------
    // Target materialization
    // ------------------------------------------------------------

    IO::Types::Status materializeGlobal(HGLOBAL global, const Transfer::Format &format, Transfer::Item &out) noexcept
    {
        try
        {
            if (!global)
                return failure(ErrorCode::ReadFailed);
            const std::size_t size = GlobalSize(global);
            if (format.kind == Transfer::FormatKind::Custom && size == 0)
            {
                out = Transfer::CustomData{format.customName, {}};
                return IO::successStatus();
            }
            GlobalMemoryLock lock(global);
            const void *raw = lock.get();
            if (!raw)
                return failure(ErrorCode::LockFailed, GetLastError());
            if (format.kind == Transfer::FormatKind::Text)
            {
                if (size < sizeof(wchar_t) || size % sizeof(wchar_t))
                    return failure(ErrorCode::EncodingFailed);
                auto wide = span<const wchar_t>(raw, size / sizeof(wchar_t));
                auto end = std::ranges::find(wide, L'\0');
                if (end == wide.end())
                    return failure(ErrorCode::EncodingFailed);
                Transfer::Text text;
                DWORD code = 0;
                if (!utf16ToUtf8({wide.data(), static_cast<std::size_t>(end - wide.begin())}, text.text, code))
                    return failure(ErrorCode::EncodingFailed, code);
                out = std::move(text);
            }
            else if (format.kind == Transfer::FormatKind::FileList)
            {
                const UINT count = DragQueryFileW(static_cast<HDROP>(global), 0xFFFFFFFF, nullptr, 0);
                if (!count)
                    return failure(ErrorCode::EncodingFailed);
                Transfer::FileList list;
                list.paths.reserve(count);
                for (UINT i = 0; i < count; ++i)
                {
                    UINT n = DragQueryFileW(static_cast<HDROP>(global), i, nullptr, 0);
                    if (!n)
                        return failure(ErrorCode::EncodingFailed);
                    std::wstring path(n + 1, L'\0');
                    if (DragQueryFileW(static_cast<HDROP>(global), i, path.data(), n + 1) != n)
                        return failure(ErrorCode::ReadFailed);
                    path.resize(n);
                    std::string validated;
                    DWORD code = 0;
                    if (!utf16ToUtf8(path, validated, code))
                        return failure(ErrorCode::EncodingFailed, code);
                    list.paths.emplace_back(std::move(path));
                }
                out = std::move(list);
            }
            else if (format.kind == Transfer::FormatKind::Custom)
            {
                Transfer::CustomData data;
                data.formatName = format.customName;
                data.bytes.assign(span<const std::byte>(raw, size).begin(), span<const std::byte>(raw, size).end());
                out = std::move(data);
            }
            else
            {
                if (size < sizeof(BITMAPINFOHEADER))
                    return failure(ErrorCode::ReadFailed);
                BITMAPINFOHEADER h{};
                std::ranges::copy(span<const std::byte>(raw, sizeof(h)), std::as_writable_bytes(std::span{&h, std::size_t{1}}).begin());
                const bool dimensionsValid =
                    h.biSize >= sizeof(BITMAPINFOHEADER) && h.biSize <= size && h.biWidth > 0 && h.biHeight != 0 && h.biHeight != LONG_MIN;
                const bool encodingSupported =
                    h.biPlanes == 1 && ((h.biBitCount == 24 && h.biCompression == BI_RGB) ||
                                        (h.biBitCount == 32 && (h.biCompression == BI_RGB || h.biCompression == BI_BITFIELDS)));
                if (!dimensionsValid)
                    return failure(ErrorCode::ReadFailed);
                if (!encodingSupported)
                    return failure(ErrorCode::Unsupported);
                const std::size_t w = static_cast<std::size_t>(h.biWidth),
                                  height = h.biHeight < 0 ? static_cast<std::size_t>(-h.biHeight) : static_cast<std::size_t>(h.biHeight);
                std::size_t bits = 0, rounded = 0, row = 0, bytes = 0, packed = 0, pixelRow = 0;
                std::size_t pixelOffset = h.biSize;
                bool explicitAlpha = false;
                bool masksSupported = true;
                if (h.biCompression == BI_BITFIELDS && h.biSize == sizeof(BITMAPINFOHEADER))
                {
                    std::size_t masksEnd = 0;
                    if (!add(pixelOffset, 3 * sizeof(DWORD), masksEnd) || masksEnd > size)
                        masksSupported = false;
                    else
                    {
                        std::array<DWORD, 3> masks{};
                        std::ranges::copy(
                            span<const std::byte>(raw, size).subspan(pixelOffset, sizeof(masks)),
                            std::as_writable_bytes(std::span{masks}).begin());
                        masksSupported = masks[0] == 0x00FF0000U && masks[1] == 0x0000FF00U && masks[2] == 0x000000FFU;
                        pixelOffset = masksEnd;
                    }
                }
                else if (h.biCompression == BI_BITFIELDS)
                {
                    if (h.biSize < sizeof(BITMAPV4HEADER))
                        masksSupported = false;
                    else
                    {
                        BITMAPV4HEADER extended{};
                        std::ranges::copy(
                            span<const std::byte>(raw, sizeof(extended)),
                            std::as_writable_bytes(std::span{&extended, std::size_t{1}}).begin());
                        masksSupported = extended.bV4RedMask == 0x00FF0000U && extended.bV4GreenMask == 0x0000FF00U &&
                                         extended.bV4BlueMask == 0x000000FFU && (extended.bV4AlphaMask == 0 || extended.bV4AlphaMask == 0xFF000000U);
                        explicitAlpha = extended.bV4AlphaMask == 0xFF000000U;
                    }
                }
                if (!masksSupported)
                    return failure(ErrorCode::Unsupported);
                if (!multiply(w, h.biBitCount, bits) || !add(bits, 31, rounded) || !multiply((rounded / 32), 4, row) ||
                    !multiply(row, height, bytes) || !multiply(w, 4, pixelRow) || !multiply(pixelRow, height, packed) || pixelOffset > size ||
                    bytes > size - pixelOffset)
                    return failure(ErrorCode::ReadFailed);
                Transfer::Image image;
                image.size = {static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(height)};
                image.rgba8.resize(packed);
                auto src = span<const std::byte>(raw, size).subspan(pixelOffset, bytes);
                for (std::size_t y = 0; y < height; ++y)
                {
                    auto s = src.subspan((h.biHeight < 0 ? y : height - 1 - y) * row, row);
                    auto d = std::span{image.rgba8}.subspan(y * pixelRow, pixelRow);
                    for (std::size_t x = 0; x < w; ++x)
                    {
                        auto p = x * (h.biBitCount / 8);
                        d[x * 4] = s[p + 2];
                        d[x * 4 + 1] = s[p + 1];
                        d[x * 4 + 2] = s[p];
                        d[x * 4 + 3] = explicitAlpha ? s[p + 3] : std::byte{0xFF};
                    }
                }
                out = std::move(image);
            }
            return IO::successStatus();
        }
        catch (const std::bad_alloc &)
        {
            return failure(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return failure(ErrorCode::Unknown);
        }
    }

    IO::Types::Status materialize(IDataObject &object, const Transfer::Format &format, Transfer::Item &out) noexcept
    {
        IO::Types::Status result;
        CLIPFORMAT native = nativeFormat(format, result);
        if (!result.ok())
            return result;
        if (format.kind == Transfer::FormatKind::Image)
        {
            FORMATETC v5{CF_DIBV5, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
            if (object.QueryGetData(&v5) != S_OK)
                native = CF_DIB;
        }
        Medium medium;
        result = getGlobal(object, native, medium);
        return result.ok() ? materializeGlobal(medium.global(), format, out) : result;
    }
} // namespace GameWIP::Desktop::Detail::Platform::DataTransfer
