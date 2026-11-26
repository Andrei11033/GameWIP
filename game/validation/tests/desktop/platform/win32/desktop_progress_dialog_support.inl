/// @file desktop_progress_dialog_support.inl
/// @brief Shared helpers for ProgressDialog validation.

/// ------------------------------------------------------------
/// ProgressDialog test support
/// ------------------------------------------------------------

#if DESKTOP_INTERNAL_TEST_HOOKS
namespace ProgressTypes = Desktop::Types::Dialogs::Progress;

[[nodiscard]] bool nativeInteractionEnabled(const Desktop::Window &window)
{
    const auto handle = Desktop::Native::Win32::getHandle(window);
    return handle.ok() && handle.value().window != nullptr && IsWindowEnabled(static_cast<HWND>(handle.value().window)) != FALSE;
}

ProgressTypes::Description progressDescription()
{
    return {
        .title = "Progress title",
        .heading = "Progress heading",
        .message = "Progress message",
        .presentationMode = ProgressTypes::Mode::Determinate,
        .progress = 0.25,
        .cancelable = true,
        .blocksOwner = true};
}
#endif
