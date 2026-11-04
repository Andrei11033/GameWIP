/// @file attachment_stub.cpp
/// @brief Reports that the opt-in Win32 attachment is unavailable on macOS.

#include "input/win32.h"

#include <new>

namespace GameWIP::Input::Platform::Win32
{
    using IO::Types::ErrorCode;

    struct Attachment::Impl
    {
    };

    Attachment::Attachment() noexcept
        : impl_(std::unique_ptr<Impl>(new (std::nothrow) Impl{}))
    {
    }

    Attachment::~Attachment() noexcept
    {
        static_cast<void>(detach());
    }

    IO::Types::Status Attachment::attach(System &, void *) noexcept
    {
        return IO::makeStatus(ErrorCode::Unsupported);
    }

    IO::Types::Status Attachment::detach() noexcept
    {
        return IO::successStatus();
    }

    bool Attachment::isAttached() const noexcept
    {
        return false;
    }

    Types::SurfaceId Attachment::surfaceId() const noexcept
    {
        return {};
    }

    IO::Types::Status Attachment::handleMessage(std::uint32_t, std::uint64_t, std::int64_t, bool &outHandled) noexcept
    {
        outHandled = false;
        return IO::makeStatus(ErrorCode::Unsupported);
    }

    IO::Types::Status Attachment::poll() noexcept
    {
        return impl_ == nullptr ? IO::makeStatus(ErrorCode::OutOfMemory) : IO::makeStatus(ErrorCode::NotOpen);
    }
} // namespace GameWIP::Input::Platform::Win32
