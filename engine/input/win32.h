/// @file win32.h
/// @brief Opt-in Win32 attachment for the generic GameWIP Input system.
#pragma once

#include "input/system.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace GameWIP::Input::Internal::Win32
{
    struct AttachmentState;
}

namespace GameWIP::Input::Platform::Win32
{
    /// @brief Connects an Input system to a caller-owned Win32 window.
    /// @details Available on Windows; the caller owns the window
    /// and forwards its messages; the attachment does not create or subclass the
    /// window or start a worker thread. It enables available Raw Input, HID,
    /// XInput, GameInput, DirectInput, and window-message providers by default.
    /// Confidently identified sources are grouped while controls remain individually
    /// addressable; raw reports can be retained for opaque HID devices.
    class Attachment final
    {
    public:
        /// @brief Creates a detached Win32 attachment.
        INPUT_EXPORT Attachment() noexcept;

        /// @brief Detaches the native source as best-effort cleanup.
        /// @details The destructor does not report a detach failure. Call detach explicitly when the status matters.
        INPUT_EXPORT ~Attachment() noexcept;

        Attachment(const Attachment &) = delete;

        Attachment &operator=(const Attachment &) = delete;

        Attachment(Attachment &&) = delete;

        Attachment &operator=(Attachment &&) = delete;

        // ------------------------------------------------------------
        // Attachment lifecycle
        // ------------------------------------------------------------

        /// @brief Registers the caller-owned window for Windows input delivery.
        /// @param system Open owner-thread-affine System to receive native values.
        /// @param windowHandle HWND represented as an opaque pointer.
        /// @return Operation status.
        [[nodiscard]] INPUT_EXPORT GameWIP::Status::Types::Result<void> attach(System &system, void *windowHandle) noexcept;

        /// @brief Detaches the current native source.
        /// @return Operation status.
        [[nodiscard]] INPUT_EXPORT GameWIP::Status::Types::Result<void> detach() noexcept;

        /// @brief Reports whether a native source is attached.
        /// @return `true` while the attachment owns a live System connection.
        [[nodiscard]] INPUT_EXPORT bool isAttached() const noexcept;

        // ------------------------------------------------------------
        // Backend status
        // ------------------------------------------------------------

        /// @brief Copies startup status for the Win32 input backends.
        /// @return One status per backend on success.
        [[nodiscard]] INPUT_EXPORT GameWIP::Status::Types::Result<std::vector<Types::BackendInfo>> backends() const noexcept;

        // ------------------------------------------------------------
        // Window message and text surface
        // ------------------------------------------------------------

        /// @brief Returns the text-input surface associated with the attached window.
        /// @return Surface identity while attached, or an invalid ID otherwise.
        [[nodiscard]] INPUT_EXPORT Types::SurfaceId surfaceId() const noexcept;

        /// @brief Forwards one window message to the native input backend.
        /// @param message Win32 message identifier.
        /// @param wParam Message word parameter.
        /// @param lParam Message long parameter.
        /// @return Message status and whether the backend consumed it.
        [[nodiscard]] INPUT_EXPORT GameWIP::Status::Types::Result<bool> handleMessage(
            std::uint32_t message,
            std::uint64_t wParam,
            std::int64_t lParam) noexcept;

        // ------------------------------------------------------------
        // Provider polling
        // ------------------------------------------------------------

        /// @brief Polls initialized native providers and publishes observations.
        /// @details System::capture polls providers before publishing its frame.
        /// Provider failures do not prevent remaining providers from polling; the first failure is returned.
        /// @return Operation status.
        [[nodiscard]] INPUT_EXPORT GameWIP::Status::Types::Result<void> poll() noexcept;

    private:
        [[nodiscard]] GameWIP::Status::Types::Status attachStatus(System &system, void *windowHandle) noexcept;
        [[nodiscard]] GameWIP::Status::Types::Status detachStatus() noexcept;
        [[nodiscard]] GameWIP::Status::Types::Status backendsStatus(std::vector<Types::BackendInfo> &outBackends) const noexcept;
        [[nodiscard]] GameWIP::Status::Types::Status handleMessageStatus(
            std::uint32_t message,
            std::uint64_t wParam,
            std::int64_t lParam,
            bool &outHandled) noexcept;
        [[nodiscard]] GameWIP::Status::Types::Status pollStatus() noexcept;

        static void beforeCapture(::GameWIP::Input::Internal::SystemState &state, void *context) noexcept;
        static void closeSystem(::GameWIP::Input::Internal::SystemState &state, void *context) noexcept;
        static GameWIP::Status::Types::Status pollNative(::GameWIP::Input::Internal::Win32::AttachmentState &implementation) noexcept;

        std::unique_ptr<::GameWIP::Input::Internal::Win32::AttachmentState> impl_{};
    };
} // namespace GameWIP::Input::Platform::Win32
