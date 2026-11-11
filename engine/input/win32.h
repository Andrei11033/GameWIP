/// @file win32.h
/// @brief Opt-in Win32 attachment for the generic GameWIP Input system.
#pragma once

#include "input/system.h"

#include <memory>

namespace GameWIP::Input::Platform::Win32
{
    /// @brief Connects an Input system to a caller-owned Win32 window.
    /// @details Available on Windows; the caller owns the window
    /// and forwards its messages; the attachment does not create or subclass the
    /// window or start a worker thread. It registers generic and discovered HID
    /// collections, groups confidently identified sources, exposes decoded
    /// controls individually, and can retain raw reports for opaque HID devices.
    class INPUT_EXPORT Attachment final
    {
    public:
        /// @brief Creates a detached Win32 attachment.
        Attachment() noexcept;

        /// @brief Detaches the native source as best-effort cleanup.
        /// @details The destructor does not report a detach failure. Call detach explicitly when the status matters.
        ~Attachment() noexcept;

        Attachment(const Attachment &) = delete;

        Attachment &operator=(const Attachment &) = delete;

        Attachment(Attachment &&) = delete;

        Attachment &operator=(Attachment &&) = delete;

        /// @brief Registers the caller-owned window for Windows input delivery.
        /// @param system Open owner-thread-affine System to receive native values.
        /// @param windowHandle HWND represented as an opaque pointer.
        /// @return Success, InvalidArgument, NotOpen, ResourceBusy, AlreadyOpen,
        /// Unsupported, NativeFailure, or OutOfMemory.
        [[nodiscard]] IO::Types::Status attach(System &system, void *windowHandle) noexcept;

        /// @brief Detaches the current native source.
        /// @return Success, ResourceBusy, or OutOfMemory.
        [[nodiscard]] IO::Types::Status detach() noexcept;

        /// @brief Reports whether a native source is attached.
        /// @return `true` while the attachment owns a live System connection.
        [[nodiscard]] bool isAttached() const noexcept;

        /// @brief Returns the text-input surface associated with the attached window.
        /// @return Surface identity while attached, or an invalid ID otherwise.
        [[nodiscard]] Types::SurfaceId surfaceId() const noexcept;

        /// @brief Forwards one window message to the native input backend.
        /// @param message Win32 message identifier.
        /// @param wParam Message word parameter.
        /// @param lParam Message long parameter.
        /// @param outHandled Receives whether the backend consumed the message.
        /// @return Success, NotOpen, ResourceBusy, InvalidArgument, Unsupported, NativeFailure, or OutOfMemory.
        [[nodiscard]] IO::Types::Status handleMessage(std::uint32_t message, std::uint64_t wParam, std::int64_t lParam, bool &outHandled) noexcept;

        /// @brief Polls XInput gamepads and publishes changed observations.
        /// @details System::capture performs the same poll before publishing its frame.
        /// @return Success, NotOpen, ResourceBusy, Unsupported, or OutOfMemory.
        [[nodiscard]] IO::Types::Status poll() noexcept;

        /// @brief Opaque implementation declaration reserved for the library binary.
        struct Impl;

    private:
        static void beforeCapture(System::Impl &state, void *context) noexcept;
        static void afterCapture(System::Impl &state, void *context) noexcept;
        static void closeSystem(System::Impl &state, void *context) noexcept;
        static IO::Types::Status pollNative(Impl &implementation) noexcept;

        std::unique_ptr<Impl> impl_{};
    };
} // namespace GameWIP::Input::Platform::Win32
