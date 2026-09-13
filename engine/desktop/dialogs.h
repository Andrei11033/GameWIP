/// @file dialogs.h
/// @brief Native file, folder, message, prompt, and operation-progress dialogs.

#pragma once

#include "desktop/desktop_export.h"
#include "filesystem/path.h"
#include "io/status.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace GameWIP::Desktop
{
    class Window;

    namespace Detail
    {
        struct ProgressDialogAccess;
        struct ProgressDialogState;
    } // namespace Detail
} // namespace GameWIP::Desktop

namespace GameWIP::Desktop::Types::Dialogs
{
    /// @brief Domain outcome of a successfully executed native dialog.
    enum class Outcome : std::uint8_t
    {
        Accepted, ///< The dialog completed successfully with a non-cancel semantic selection.
        Cancelled ///< The user dismissed or cancelled the dialog without an operation failure.
    };

    /// @brief Semantic severity used to select a native dialog icon.
    enum class Severity : std::uint8_t
    {
        None,        ///< No semantic severity or severity icon.
        Information, ///< Informational content.
        Warning,     ///< Content that warrants caution.
        Error        ///< Content describing an error or failed operation.
    };
} // namespace GameWIP::Desktop::Types::Dialogs

namespace GameWIP::Desktop::Types::Dialogs::File
{
    /// @brief One portable file-type filter.
    struct Filter
    {
        std::string_view name;                        ///< Display name shown for the filter.
        std::span<const std::string_view> extensions; ///< Extensions without a leading period, wildcard, or path separator; empty means all files.
    };

    /// @brief Description shared by single- and multiple-file open dialogs.
    struct OpenDescription
    {
        Window *owner = nullptr;                         ///< Optional live owner Window on the calling thread.
        std::string_view title;                          ///< UTF-8 dialog title; empty selects backend wording.
        std::span<const Filter> filters;                 ///< Ordered, call-scoped file filters; empty permits all files.
        std::optional<std::size_t> preferredFilterIndex; ///< Optional zero-based initial filter index.
        FileSystem::Types::Path suggestedDirectory;      ///< Optional initial directory suggestion.
    };

    /// @brief Description of a save-file dialog.
    struct SaveDescription
    {
        Window *owner = nullptr;                         ///< Optional live owner Window on the calling thread.
        std::string_view title;                          ///< UTF-8 dialog title; empty selects backend wording.
        std::span<const Filter> filters;                 ///< Ordered, call-scoped file filters; empty permits all files.
        std::optional<std::size_t> preferredFilterIndex; ///< Optional zero-based initial filter index.
        FileSystem::Types::Path suggestedDirectory;      ///< Optional initial directory suggestion.
        std::string_view suggestedFileName;              ///< Optional UTF-8 initial file name.
        std::string_view suggestedExtension;             ///< Optional default extension without a leading period.
    };

    /// @brief Description shared by single- and multiple-folder selection.
    struct FolderDescription
    {
        Window *owner = nullptr;                    ///< Optional live owner Window on the calling thread.
        std::string_view title;                     ///< UTF-8 dialog title; empty selects backend wording.
        FileSystem::Types::Path suggestedDirectory; ///< Optional initial directory suggestion.
    };

    /// @brief Result containing at most one native path.
    struct Result
    {
        IO::Types::Status status;                       ///< Operation status; cancellation is reported separately.
        Outcome outcome = Outcome::Cancelled;           ///< User outcome when @c status is successful.
        FileSystem::Types::Path path;                   ///< Selected path when @c outcome is Accepted.
        std::optional<std::size_t> selectedFilterIndex; ///< Zero-based selected filter index when the dialog reports one.
    };

    /// @brief Result containing an ordered list of native paths.
    struct ListResult
    {
        IO::Types::Status status;                       ///< Operation status; cancellation is reported separately.
        Outcome outcome = Outcome::Cancelled;           ///< User outcome when @c status is successful.
        std::vector<FileSystem::Types::Path> paths;     ///< Selected paths in backend order when accepted.
        std::optional<std::size_t> selectedFilterIndex; ///< Zero-based selected filter index when the dialog reports one.
    };
} // namespace GameWIP::Desktop::Types::Dialogs::File

namespace GameWIP::Desktop::Dialogs
{
    /// @brief Shows a synchronous native single-file open dialog.
    /// @param description Call-scoped owner, text, filter, and initial-directory settings.
    /// @return Operation status, user outcome, selected path, and selected filter.
    [[nodiscard]] GAMEWIP_DESKTOP_EXPORT Types::Dialogs::File::Result openFile(const Types::Dialogs::File::OpenDescription &description) noexcept;

    /// @brief Shows a synchronous native multiple-file open dialog.
    /// @param description Call-scoped owner, text, filter, and initial-directory settings.
    /// @return Operation status, user outcome, selected paths, and selected filter.
    [[nodiscard]] GAMEWIP_DESKTOP_EXPORT Types::Dialogs::File::ListResult openFiles(
        const Types::Dialogs::File::OpenDescription &description) noexcept;

    /// @brief Shows a synchronous native save-file dialog.
    /// @param description Call-scoped owner, text, filter, and save-name settings.
    /// @return Operation status, user outcome, selected path, and selected filter.
    [[nodiscard]] GAMEWIP_DESKTOP_EXPORT Types::Dialogs::File::Result saveFile(const Types::Dialogs::File::SaveDescription &description) noexcept;

    /// @brief Shows a synchronous native single-folder selection dialog.
    /// @param description Call-scoped owner, title, and initial-directory settings.
    /// @return Operation status, user outcome, and selected folder path.
    [[nodiscard]] GAMEWIP_DESKTOP_EXPORT Types::Dialogs::File::Result selectFolder(
        const Types::Dialogs::File::FolderDescription &description) noexcept;

    /// @brief Shows a synchronous native multiple-folder selection dialog.
    /// @param description Call-scoped owner, title, and initial-directory settings.
    /// @return Operation status, user outcome, and selected folder paths.
    [[nodiscard]] GAMEWIP_DESKTOP_EXPORT Types::Dialogs::File::ListResult selectFolders(
        const Types::Dialogs::File::FolderDescription &description) noexcept;
} // namespace GameWIP::Desktop::Dialogs

namespace GameWIP::Desktop::Types::Dialogs::Message
{
    /// @brief Portable fixed button combinations for a message dialog.
    enum class Buttons : std::uint8_t
    {
        Ok,          ///< One affirmative OK button.
        OkCancel,    ///< OK and Cancel buttons.
        YesNo,       ///< Yes and No buttons.
        YesNoCancel, ///< Yes, No, and Cancel buttons.
        RetryCancel  ///< Retry and Cancel buttons.
    };

    /// @brief Portable identity of a message-dialog button.
    enum class Button : std::uint8_t
    {
        None,   ///< No button or no explicit default button.
        Ok,     ///< Affirmative OK button.
        Cancel, ///< Cancellation button or dismissal affordance.
        Yes,    ///< Affirmative Yes button.
        No,     ///< Negative No button.
        Retry   ///< Retry button.
    };

    /// @brief Call-scoped description of a fixed-button message dialog.
    struct Description
    {
        Window *owner = nullptr;             ///< Optional live owner Window on the calling thread.
        std::string_view title;              ///< UTF-8 dialog title.
        std::string_view message;            ///< UTF-8 primary message.
        Buttons buttons = Buttons::Ok;       ///< Fixed button combination to present.
        Button defaultButton = Button::None; ///< Optional default button; must belong to @c buttons.
        Severity severity = Severity::None;  ///< Semantic severity used for native presentation.
    };

    /// @brief Result of a message-dialog operation.
    struct Result
    {
        IO::Types::Status status;             ///< Operation status; cancellation is reported separately.
        Outcome outcome = Outcome::Cancelled; ///< User outcome when @c status is successful.
        Button button = Button::None;         ///< Selected button, or Cancel/None when dismissed as documented.
    };
} // namespace GameWIP::Desktop::Types::Dialogs::Message

namespace GameWIP::Desktop::Dialogs
{
    /// @brief Shows a synchronous native message with a fixed portable button set.
    /// @param description Call-scoped owner, text, severity, and button settings.
    /// @return Operation status, user outcome, and selected portable button.
    [[nodiscard]] GAMEWIP_DESKTOP_EXPORT Types::Dialogs::Message::Result showMessage(
        const Types::Dialogs::Message::Description &description) noexcept;
} // namespace GameWIP::Desktop::Dialogs

namespace GameWIP::Desktop::Types::Dialogs::Prompt
{
    /// @brief Opaque caller-defined prompt-button identity.
    struct ButtonId
    {
        std::uint32_t value = 0; ///< Caller-defined nonzero identity; zero is invalid.

        /// @brief Reports whether this identity may be used by a prompt button.
        /// @return true when @c value is nonzero; otherwise false.
        [[nodiscard]] constexpr bool isValid() const noexcept
        {
            return value != 0;
        }
        /// @brief Compares two portable button identities.
        /// @param left First identity.
        /// @param right Second identity.
        /// @return true when both identities contain the same value.
        friend constexpr bool operator==(ButtonId left, ButtonId right) noexcept = default;
    };

    /// @brief Opaque caller-defined prompt-option identity.
    struct OptionId
    {
        std::uint32_t value = 0; ///< Caller-defined nonzero identity; zero is invalid.

        /// @brief Reports whether this identity may be used by a prompt option.
        /// @return true when @c value is nonzero; otherwise false.
        [[nodiscard]] constexpr bool isValid() const noexcept
        {
            return value != 0;
        }
        /// @brief Compares two portable option identities.
        /// @param left First identity.
        /// @param right Second identity.
        /// @return true when both identities contain the same value.
        friend constexpr bool operator==(OptionId left, OptionId right) noexcept = default;
    };

    /// @brief One caller-defined prompt action.
    struct Button
    {
        ButtonId id;                  ///< Unique nonzero identity within the prompt.
        std::string_view label;       ///< UTF-8 action label.
        std::string_view description; ///< Optional UTF-8 explanatory secondary text associated with the action.
    };

    /// @brief One mutually exclusive prompt option.
    struct Option
    {
        OptionId id;            ///< Unique nonzero identity within the prompt.
        std::string_view label; ///< UTF-8 option label.
    };

    /// @brief Optional prompt checkbox and its initial state.
    struct CheckBox
    {
        std::string_view label; ///< Nonempty UTF-8 checkbox label.
        bool checked = false;   ///< Initial checked state.
    };

    /// @brief Call-scoped description of a caller-defined prompt.
    struct Description
    {
        Window *owner = nullptr;            ///< Optional live owner Window on the calling thread.
        std::string_view title;             ///< UTF-8 dialog title.
        std::string_view heading;           ///< UTF-8 prominent heading.
        std::string_view message;           ///< UTF-8 primary message.
        Severity severity = Severity::None; ///< Semantic severity used for native presentation.
        std::span<const Button> buttons;    ///< Non-empty, call-scoped actions with unique valid IDs.
        ButtonId defaultButton;             ///< Optional default action; zero or an ID in @c buttons.
        ButtonId cancelButton;              ///< Optional cancellation action; zero or an ID in @c buttons.
        std::span<const Option> options;    ///< Call-scoped mutually exclusive options with unique valid IDs.
        OptionId defaultOption;             ///< Optional initial option; zero or an ID in @c options.
        std::string_view details;           ///< Optional UTF-8 expandable details.
        std::string_view supplementalText;  ///< Optional UTF-8 supplemental text.
        std::optional<CheckBox> checkBox;   ///< Optional checkbox description and initial state.
    };

    /// @brief Result of a caller-defined prompt operation.
    struct Result
    {
        IO::Types::Status status;             ///< Operation status; cancellation is reported separately.
        Outcome outcome = Outcome::Cancelled; ///< User outcome when @c status is successful.
        ButtonId button;                      ///< Selected action identity, or zero after dismissal without one.
        OptionId option;                      ///< Final selected option identity, or zero when none is selected.
        std::optional<bool> checkBoxChecked;  ///< Final checkbox state when a checkbox was presented.
    };
} // namespace GameWIP::Desktop::Types::Dialogs::Prompt

namespace GameWIP::Desktop::Dialogs
{
    /// @brief Shows a synchronous native prompt with caller-defined choices.
    /// @param description Call-scoped owner, content, actions, options, and checkbox settings.
    /// @return Operation status, user outcome, selected identities, and final checkbox state.
    [[nodiscard]] GAMEWIP_DESKTOP_EXPORT Types::Dialogs::Prompt::Result showPrompt(const Types::Dialogs::Prompt::Description &description) noexcept;
} // namespace GameWIP::Desktop::Dialogs

namespace GameWIP::Desktop::Types::Dialogs::Progress
{
    /// @brief Visual mode of an operation-progress presentation.
    enum class Mode : std::uint8_t
    {
        Determinate,  ///< Displays the retained numeric progress value.
        Indeterminate ///< Displays ongoing activity without a numeric completion estimate.
    };

    /// @brief Initial settings for a modeless operation-progress presentation.
    struct Description
    {
        Window *owner = nullptr;         ///< Borrowed owner; its Window object must outlive this open ProgressDialog lifetime.
        std::string_view title;          ///< UTF-8 window title, borrowed only during open().
        std::string_view heading;        ///< UTF-8 prominent heading, borrowed only during open().
        std::string_view message;        ///< UTF-8 operation message, borrowed only during open().
        Mode mode = Mode::Indeterminate; ///< Initial visual progress mode.
        double progress = 0.0;           ///< Initial normalized value in the inclusive range [0, 1].
        bool cancelable = false;         ///< Whether user cancellation requests are exposed.
        bool blocksOwner = true;         ///< Whether a supplied owner is interaction-blocked while open.
    };
} // namespace GameWIP::Desktop::Types::Dialogs::Progress

namespace GameWIP::Desktop
{
    /// @brief Owner-thread-bound modeless native operation-progress presentation.
    class GAMEWIP_DESKTOP_EXPORT ProgressDialog final
    {
    public:
        /// @brief Constructs a closed progress-dialog owner.
        ProgressDialog() noexcept;
        ProgressDialog(const ProgressDialog &) = delete;
        ProgressDialog &operator=(const ProgressDialog &) = delete;
        ProgressDialog(ProgressDialog &&) = delete;
        ProgressDialog &operator=(ProgressDialog &&) = delete;
        /// @brief Best-effort closes the native presentation and releases retained state.
        /// @details Wrong-thread destruction transfers cleanup to the opening thread when its dispatcher remains available.
        ~ProgressDialog() noexcept;

        /// @brief Opens a modeless native progress presentation on the calling thread.
        /// @details All subsequent operations except ownedByCurrentThread() require the successful opening thread. The caller must continue pumping
        /// Desktop events.
        /// @param description Initial owner, text, mode, progress, cancellation, and blocking settings.
        /// @return Success, or a status explaining why no presentation was opened.
        [[nodiscard]] IO::Types::Status open(const Types::Dialogs::Progress::Description &description) noexcept;
        /// @brief Returns whether a native presentation is live on its owner thread.
        /// @return true only when called by the opening thread while the native presentation remains live.
        [[nodiscard]] bool isOpen() const noexcept;
        /// @brief Safely reports whether the caller owns the retained lifetime.
        /// @return true when the calling thread is the thread that successfully opened the retained lifetime.
        [[nodiscard]] bool ownedByCurrentThread() const noexcept;
        /// @brief Replaces the native window title.
        /// @param title Strict UTF-8 title with no embedded U+0000.
        /// @return Success, or a validation, ownership, lifetime, or native-operation failure.
        [[nodiscard]] IO::Types::Status setTitle(std::string_view title) noexcept;
        /// @brief Replaces the native heading.
        /// @param heading Strict UTF-8 heading with no embedded U+0000.
        /// @return Success, or a validation, ownership, lifetime, or native-operation failure.
        [[nodiscard]] IO::Types::Status setHeading(std::string_view heading) noexcept;
        /// @brief Replaces the native message.
        /// @param message Strict UTF-8 message with no embedded U+0000.
        /// @return Success, or a validation, ownership, lifetime, or native-operation failure.
        [[nodiscard]] IO::Types::Status setMessage(std::string_view message) noexcept;
        /// @brief Switches between determinate and indeterminate presentation.
        /// @param mode New visual progress mode.
        /// @return Success, or a validation, ownership, lifetime, or native-operation failure.
        [[nodiscard]] IO::Types::Status setMode(Types::Dialogs::Progress::Mode mode) noexcept;
        /// @brief Retains a normalized numeric progress value and displays it when determinate.
        /// @param progress New finite value in the inclusive range [0, 1].
        /// @return Success, or a validation, ownership, lifetime, or native-operation failure.
        [[nodiscard]] IO::Types::Status setProgress(double progress) noexcept;
        /// @brief Returns the sticky owner-thread cancellation request.
        /// @return true when cancellation was requested on the opening thread and has not been cleared.
        [[nodiscard]] bool hasCancelRequest() const noexcept;
        /// @brief Clears the sticky owner-thread cancellation request.
        /// @details A call from another thread or without retained state has no effect.
        void clearCancelRequest() noexcept;
        /// @brief Closes and finalizes the retained progress lifetime.
        /// @details A successful close permits the same object to be opened again. Failed native cleanup is retained for retry.
        /// @return Success when closed or already closed; ResourceBusy on another thread; otherwise the cleanup failure.
        [[nodiscard]] IO::Types::Status close() noexcept;

    private:
        friend struct Detail::ProgressDialogAccess;
        std::unique_ptr<Detail::ProgressDialogState> state_;
        std::atomic_uint64_t ownerThreadToken_{0};
    };
} // namespace GameWIP::Desktop
