/// @file desktop_clipboard_support.inl
/// @brief Shared fixtures and helpers for Desktop clipboard tests.

/// ------------------------------------------------------------
/// Clipboard test support
/// ------------------------------------------------------------

namespace Clipboard = Desktop::Clipboard;
namespace ClipboardTypes = Desktop::Types::Clipboard;
namespace Transfer = Desktop::Types::DataTransfer;
using StatusCode = GameWIP::Status::Types::Code;

#if defined(__clang__)
#pragma clang unsafe_buffer_usage begin
#endif
std::span<std::byte> nativeClipboardBytes(void *data, std::size_t size) noexcept
{
    return {static_cast<std::byte *>(data), size};
}

std::span<const std::byte> nativeClipboardBytes(const void *data, std::size_t size) noexcept
{
    return {static_cast<const std::byte *>(data), size};
}

#if defined(__clang__)
#pragma clang unsafe_buffer_usage end
#endif

// External clipboard owners can briefly hold access while our malformed-format fixtures publish.
bool openNativeClipboardFixture(
    HWND owner,
    TestSupport::Context *context = nullptr,
    std::chrono::milliseconds timeout = std::chrono::milliseconds{250})
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    DWORD error = ERROR_SUCCESS;
    std::size_t attempts = 0;
    do
    {
        ++attempts;

        if(OpenClipboard(owner) != FALSE)
        {
            return true;
        }
        error = GetLastError();

        if(std::chrono::steady_clock::now() >= deadline)
        {
            break;
        }
        Sleep(1);
    } while(true);

    if(context != nullptr)
    {
        context->info(std::format("Clipboard fixture: OpenClipboard failed, native={}, attempts={}, timeout={}ms", error, attempts, timeout.count()));
    }

    return false;
}

bool publishNativeClipboardData(TestSupport::Context &context, UINT format, std::span<const std::byte> bytes)
{
    HWND owner =
        CreateWindowExW(0, L"STATIC", L"GameWIP Clipboard Test Owner", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);

    if(owner == nullptr)
    {
        context.info(std::format("Clipboard fixture: CreateWindowExW failed, native={}", GetLastError()));
    }

    if(owner == nullptr || !openNativeClipboardFixture(owner, &context))
    {
        if(owner != nullptr)
        {
            static_cast<void>(DestroyWindow(owner));
        }

        return false;
    }

    const auto diagnose = [&](const char *call, bool success)
    {
        if(!success)
        {
            context.info(std::format("Clipboard fixture: {} failed, native={}, format={}", call, GetLastError(), format));
        }

        return success;
    };
    bool success = diagnose("EmptyClipboard", EmptyClipboard() != FALSE);
    HGLOBAL memory = success ? GlobalAlloc(GMEM_MOVEABLE, bytes.size()) : nullptr;
    success = success && diagnose("GlobalAlloc", memory != nullptr);

    if(success && !bytes.empty())
    {
        void *destination = GlobalLock(memory);
        success = diagnose("GlobalLock", destination != nullptr);

        if(success)
        {
            std::ranges::copy(bytes, nativeClipboardBytes(destination, bytes.size()).begin());
        }

        if(destination != nullptr)
        {
            static_cast<void>(GlobalUnlock(memory));
        }
    }

    if(success)
    {
        success = diagnose("SetClipboardData", SetClipboardData(format, memory) != nullptr);

        if(success)
        {
            memory = nullptr;
        }
    }

    if(memory != nullptr)
    {
        static_cast<void>(GlobalFree(memory));
    }
    success = diagnose("CloseClipboard", CloseClipboard() != FALSE) && success;
    static_cast<void>(DestroyWindow(owner));
    return success;
}

bool readNativeDibV5Header(TestSupport::Context &context, BITMAPV5HEADER &header)
{
    if(!openNativeClipboardFixture(nullptr, &context))
    {
        return false;
    }

    const HGLOBAL memory = static_cast<HGLOBAL>(GetClipboardData(CF_DIBV5));

    if(memory == nullptr)
    {
        context.info(std::format("Clipboard fixture: GetClipboardData(CF_DIBV5) failed, native={}", GetLastError()));
    }
    const bool largeEnough = memory != nullptr && GlobalSize(memory) >= sizeof(header);
    const void *data = largeEnough ? GlobalLock(memory) : nullptr;

    if(data != nullptr)
    {
        std::ranges::copy(nativeClipboardBytes(data, sizeof(header)), std::as_writable_bytes(std::span{&header, 1}).begin());
    }

    if(data != nullptr)
    {
        static_cast<void>(GlobalUnlock(memory));
    }
    const bool closed = CloseClipboard() != FALSE;

    if(!closed)
    {
        context.info(std::format("Clipboard fixture: CloseClipboard failed, native={}", GetLastError()));
    }

    return data != nullptr && closed;
}
