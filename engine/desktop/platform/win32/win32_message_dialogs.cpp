/// @file win32_message_dialogs.cpp
/// @brief Win32 TaskDialog presentations.

#include "desktop/internal/results.h"
#include "desktop/platform/dialogs.h"

#include "desktop/internal/desktop_test_hooks.h"
#include "desktop/platform/win32/internal/test_hooks.h"
#include "desktop/platform/win32/internal/win32_com.h"
#include "desktop/platform/win32/internal/win32_dialog_helpers.h"

#include <commctrl.h>
#include <limits>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace GameWIP::Desktop::Platform
{
    namespace Win32TestHooks = GameWIP::Desktop::Internal::TestHooks::Win32;

    namespace
    {
        using ErrorCode = GameWIP::Status::Types::Code;
        using Win32::convertText;
        using Win32::ownerHandle;

        struct MessageDialogOperation
        {
            GameWIP::Status::Types::Status status;
            Types::Dialogs::Outcome outcome = Types::Dialogs::Outcome::Cancelled;
            Types::Dialogs::Message::Button button = Types::Dialogs::Message::Button::None;
        };

        struct PromptDialogOperation
        {
            GameWIP::Status::Types::Status status;
            Types::Dialogs::Outcome outcome = Types::Dialogs::Outcome::Cancelled;
            std::optional<Types::Dialogs::Prompt::ButtonId> button;
            std::optional<Types::Dialogs::Prompt::OptionId> option;
            std::optional<bool> checkBoxChecked;
        };

        [[nodiscard]] MessageDialogResult sharedResult(MessageDialogOperation operation)
        {
            Types::Dialogs::Message::Selection selection{operation.outcome, operation.button};
            const bool hasPartialValue = operation.outcome == Types::Dialogs::Outcome::Accepted ||
                                         operation.button != Types::Dialogs::Message::Button::None;
            return Internal::resultWithPartial(std::move(operation.status), std::move(selection), hasPartialValue);
        }

        [[nodiscard]] PromptDialogResult sharedResult(PromptDialogOperation operation)
        {
            Types::Dialogs::Prompt::Selection selection{
                operation.outcome, operation.button, operation.option, operation.checkBoxChecked};
            const bool hasPartialValue = operation.outcome == Types::Dialogs::Outcome::Accepted || operation.button.has_value() ||
                                         operation.option.has_value() || operation.checkBoxChecked.has_value();
            return Internal::resultWithPartial(std::move(operation.status), std::move(selection), hasPartialValue);
        }

        /// @brief Maps portable severity to the TaskDialog icon resource.
        [[nodiscard]] PCWSTR severityIcon(Types::Dialogs::Severity severity) noexcept
        {
            using Types::Dialogs::Severity;

            switch(severity)
            {
            case Severity::Information:
                return TD_INFORMATION_ICON;
            case Severity::Warning:
                return TD_WARNING_ICON;
            case Severity::Error:
                return TD_ERROR_ICON;
            case Severity::None:
                return nullptr;
            }

            return nullptr;
        }

        /// @brief Maps a portable message result to its TaskDialog button identifier.
        [[nodiscard]] int nativeMessageButton(Types::Dialogs::Message::Button button) noexcept
        {
            using Types::Dialogs::Message::Button;

            switch(button)
            {
            case Button::Ok:
                return IDOK;
            case Button::Cancel:
                return IDCANCEL;
            case Button::Yes:
                return IDYES;
            case Button::No:
                return IDNO;
            case Button::Retry:
                return IDRETRY;
            case Button::None:
                return 0;
            }

            return 0;
        }

        /// @brief Maps a native TaskDialog button identifier back to a portable result.
        [[nodiscard]] Types::Dialogs::Message::Button portableMessageButton(int button) noexcept
        {
            using Types::Dialogs::Message::Button;

            switch(button)
            {
            case IDOK:
                return Button::Ok;
            case IDCANCEL:
                return Button::Cancel;
            case IDYES:
                return Button::Yes;
            case IDNO:
                return Button::No;
            case IDRETRY:
                return Button::Retry;
            default:
                return Button::None;
            }
        }

        /// @brief Returns whether a message-button policy has a cancelable result.
        [[nodiscard]] bool messageHasCancel(Types::Dialogs::Message::Buttons buttons) noexcept
        {
            using Types::Dialogs::Message::Buttons;
            return buttons == Buttons::OkCancel || buttons == Buttons::YesNoCancel || buttons == Buttons::RetryCancel;
        }
    } // namespace

    /// ------------------------------------------------------------
    /// Message and prompt dialogs
    /// ------------------------------------------------------------

    MessageDialogResult showMessage(const Types::Dialogs::Message::Description &description) noexcept
    {
        MessageDialogOperation output;
        try
        {
#if DESKTOP_INTERNAL_TEST_HOOKS
            Win32TestHooks::MessageDialogResponse simulatedResponse;
            const bool simulated = Internal::consumeMessageDialogResponse(simulatedResponse);

            if(simulated && Internal::consumeFailure(GameWIP::Desktop::Internal::TestHooks::FailurePoint::Allocation))
            {
                output.status = GameWIP::Desktop::Internal::statusFor(ErrorCode::OutOfMemory);
                return sharedResult(std::move(output));
            }
#endif
            std::wstring title;
            std::wstring message;
            output.status = convertText(description.title, title);

            if(!output.status.ok())
            {
                return sharedResult(std::move(output));
            }

            output.status = convertText(description.message, message);

            if(!output.status.ok())
            {
                return sharedResult(std::move(output));
            }
            TASKDIALOGCONFIG config{};
            config.cbSize = sizeof(config);
            config.hwndParent = ownerHandle(description.owner);
            config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION;
            config.pszWindowTitle = title.c_str();
            config.pszContent = message.c_str();
            config.pszMainIcon = severityIcon(description.severity);
            using Types::Dialogs::Message::Buttons;

            switch(description.buttons)
            {
            case Buttons::Ok:
                config.dwCommonButtons = TDCBF_OK_BUTTON;
                break;
            case Buttons::OkCancel:
                config.dwCommonButtons =
                    static_cast<TASKDIALOG_COMMON_BUTTON_FLAGS>(static_cast<DWORD>(TDCBF_OK_BUTTON) | static_cast<DWORD>(TDCBF_CANCEL_BUTTON));
                break;
            case Buttons::YesNo:
                config.dwCommonButtons =
                    static_cast<TASKDIALOG_COMMON_BUTTON_FLAGS>(static_cast<DWORD>(TDCBF_YES_BUTTON) | static_cast<DWORD>(TDCBF_NO_BUTTON));
                break;
            case Buttons::YesNoCancel:
                config.dwCommonButtons = static_cast<TASKDIALOG_COMMON_BUTTON_FLAGS>(
                    static_cast<DWORD>(TDCBF_YES_BUTTON) | static_cast<DWORD>(TDCBF_NO_BUTTON) | static_cast<DWORD>(TDCBF_CANCEL_BUTTON));
                break;
            case Buttons::RetryCancel:
                config.dwCommonButtons =
                    static_cast<TASKDIALOG_COMMON_BUTTON_FLAGS>(static_cast<DWORD>(TDCBF_RETRY_BUTTON) | static_cast<DWORD>(TDCBF_CANCEL_BUTTON));
                break;
            }
            config.nDefaultButton = nativeMessageButton(description.defaultButton);

            int selected = 0;
            HRESULT result = S_OK;
#if DESKTOP_INTERNAL_TEST_HOOKS
            if(simulated)
            {
                Internal::recordMessageDialogSnapshot(
                    {.title = title,
                     .message = message,
                     .buttons = description.buttons,
                     .defaultButton = description.defaultButton,
                     .severity = description.severity});

                if(Internal::consumeFailure(GameWIP::Desktop::Internal::TestHooks::FailurePoint::DialogNativeOperation))
                {
                    result = E_FAIL;
                }
                else
                {
                    selected = simulatedResponse.dismissed ? IDCANCEL : nativeMessageButton(simulatedResponse.button);

                    if(Internal::consumeFailure(GameWIP::Desktop::Internal::TestHooks::FailurePoint::DialogResult))
                    {
                        selected = std::numeric_limits<int>::min();
                    }
                }
            }
            else
#endif
            {
                result = TaskDialogIndirect(&config, &selected, nullptr, nullptr);
            }

            if(FAILED(result))
            {
                output.status = statusFromHResult(result);
                return sharedResult(std::move(output));
            }

            output.status = GameWIP::Status::Types::Status{};

            if(selected == IDCANCEL)
            {
                output.outcome = Types::Dialogs::Outcome::Cancelled;
                output.button =
                    messageHasCancel(description.buttons) ? Types::Dialogs::Message::Button::Cancel : Types::Dialogs::Message::Button::None;
            }
            else
            {
                output.button = portableMessageButton(selected);

                if(output.button == Types::Dialogs::Message::Button::None)
                {
                    output = {};
                    output.status = GameWIP::Desktop::Internal::statusFor(ErrorCode::NativeFailure);
                    return sharedResult(std::move(output));
                }
                output.outcome = Types::Dialogs::Outcome::Accepted;
            }
        }
        catch(const std::bad_alloc &)
        {
            output.status = GameWIP::Desktop::Internal::statusFor(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            output.status = GameWIP::Desktop::Internal::statusFor(ErrorCode::Unknown);
        }

        return sharedResult(std::move(output));
    }

    PromptDialogResult showPrompt(const Types::Dialogs::Prompt::Description &description) noexcept
    {
        PromptDialogOperation output;
        try
        {
#if DESKTOP_INTERNAL_TEST_HOOKS
            Win32TestHooks::PromptDialogResponse simulatedResponse;
            const bool simulated = Internal::consumePromptDialogResponse(simulatedResponse);

            if(simulated && Internal::consumeFailure(GameWIP::Desktop::Internal::TestHooks::FailurePoint::Allocation))
            {
                output.status = GameWIP::Desktop::Internal::statusFor(ErrorCode::OutOfMemory);
                return sharedResult(std::move(output));
            }
#endif
            constexpr int nativeBase = 1000;

            if(description.buttons.size() > static_cast<std::size_t>(std::numeric_limits<int>::max() - nativeBase) ||
               description.options.size() > static_cast<std::size_t>(std::numeric_limits<int>::max() - nativeBase) - description.buttons.size())
            {
                output.status = GameWIP::Desktop::Internal::statusFor(ErrorCode::InvalidArgument);
                return sharedResult(std::move(output));
            }
            std::wstring title;
            std::wstring heading;
            std::wstring message;
            std::wstring details;
            std::wstring footer;
            std::wstring verification;

            for(auto pair :
                {std::pair{description.title, &title},
                 std::pair{description.heading, &heading},
                 std::pair{description.message, &message},
                 std::pair{description.details, &details},
                 std::pair{description.supplementalText, &footer}})
            {
                output.status = convertText(pair.first, *pair.second);

                if(!output.status.ok())
                {
                    return sharedResult(std::move(output));
                }
            }

            if(description.checkBox)
            {
                output.status = convertText(description.checkBox->label, verification);

                if(!output.status.ok())
                {
                    return sharedResult(std::move(output));
                }
            }

            bool commandLinks = false;

            for(const auto &button : description.buttons)
            {
                commandLinks = commandLinks || !button.description.empty();
            }

            std::vector<std::wstring> buttonText;
            std::vector<TASKDIALOG_BUTTON> nativeButtons;
            buttonText.reserve(description.buttons.size());
            nativeButtons.reserve(description.buttons.size());

            for(std::size_t index = 0; index < description.buttons.size(); ++index)
            {
                std::wstring label;
                std::wstring detail;
                output.status = convertText(description.buttons[index].label, label);

                if(!output.status.ok())
                {
                    return sharedResult(std::move(output));
                }

                output.status = convertText(description.buttons[index].description, detail);

                if(!output.status.ok())
                {
                    return sharedResult(std::move(output));
                }

                if(!detail.empty())
                {
                    label.push_back(L'\n');
                    label.append(detail);
                }
                buttonText.push_back(std::move(label));
            }

            for(std::size_t index = 0; index < buttonText.size(); ++index)
            {
                nativeButtons.push_back({nativeBase + static_cast<int>(index), buttonText[index].c_str()});
            }

            std::vector<std::wstring> optionText;
            std::vector<TASKDIALOG_BUTTON> nativeOptions;
            optionText.reserve(description.options.size());
            nativeOptions.reserve(description.options.size());

            for(const auto &option : description.options)
            {
                std::wstring label;
                output.status = convertText(option.label, label);

                if(!output.status.ok())
                {
                    return sharedResult(std::move(output));
                }
                optionText.push_back(std::move(label));
            }
            const int optionBase = nativeBase + static_cast<int>(description.buttons.size());

            for(std::size_t index = 0; index < optionText.size(); ++index)
            {
                nativeOptions.push_back({optionBase + static_cast<int>(index), optionText[index].c_str()});
            }

            TASKDIALOGCONFIG config{};
            config.cbSize = sizeof(config);
            config.hwndParent = ownerHandle(description.owner);
            DWORD taskFlags = TDF_ALLOW_DIALOG_CANCELLATION;

            if(commandLinks)
            {
                taskFlags |= TDF_USE_COMMAND_LINKS;
            }

            if(!description.defaultOption)
            {
                taskFlags |= TDF_NO_DEFAULT_RADIO_BUTTON;
            }

            if(description.checkBox && description.checkBox->checked)
            {
                taskFlags |= TDF_VERIFICATION_FLAG_CHECKED;
            }
            config.dwFlags = static_cast<TASKDIALOG_FLAGS>(taskFlags);
            config.pszWindowTitle = title.c_str();
            config.pszMainInstruction = heading.c_str();
            config.pszContent = message.c_str();
            config.pszExpandedInformation = details.c_str();
            config.pszFooter = footer.c_str();
            config.pszMainIcon = severityIcon(description.severity);
            config.cButtons = static_cast<UINT>(nativeButtons.size());
            config.pButtons = nativeButtons.data();
            config.cRadioButtons = static_cast<UINT>(nativeOptions.size());
            config.pRadioButtons = nativeOptions.data();
            config.pszVerificationText = description.checkBox ? verification.c_str() : nullptr;

            for(std::size_t index = 0; index < description.buttons.size(); ++index)
            {
                if(description.defaultButton && description.buttons[index].id == *description.defaultButton)
                {
                    config.nDefaultButton = nativeBase + static_cast<int>(index);
                }
            }

            for(std::size_t index = 0; index < description.options.size(); ++index)
            {
                if(description.defaultOption && description.options[index].id == *description.defaultOption)
                {
                    config.nDefaultRadioButton = optionBase + static_cast<int>(index);
                }
            }

            int selectedButton = IDCANCEL;
            int selectedOption = 0;
            BOOL checked = FALSE;
            HRESULT result = S_OK;
#if DESKTOP_INTERNAL_TEST_HOOKS
            if(simulated)
            {
                Win32TestHooks::PromptDialogSnapshot snapshot;
                snapshot.title = title;
                snapshot.heading = heading;
                snapshot.message = message;
                snapshot.details = details;
                snapshot.supplementalText = footer;
                snapshot.checkBoxLabel = verification;
                snapshot.buttonText = buttonText;
                snapshot.optionText = optionText;
                snapshot.nativeDefaultButton = config.nDefaultButton;
                snapshot.nativeDefaultOption = config.nDefaultRadioButton;
                snapshot.commandLinks = commandLinks;
                snapshot.checkBoxInitiallyChecked = description.checkBox && description.checkBox->checked;
                snapshot.severity = description.severity;
                snapshot.nativeButtonIds.reserve(nativeButtons.size());
                snapshot.nativeOptionIds.reserve(nativeOptions.size());

                for(const TASKDIALOG_BUTTON &button : nativeButtons)
                {
                    snapshot.nativeButtonIds.push_back(button.nButtonID);
                }

                for(const TASKDIALOG_BUTTON &option : nativeOptions)
                {
                    snapshot.nativeOptionIds.push_back(option.nButtonID);
                }
                Internal::recordPromptDialogSnapshot(std::move(snapshot));

                if(Internal::consumeFailure(GameWIP::Desktop::Internal::TestHooks::FailurePoint::DialogNativeOperation))
                {
                    result = E_FAIL;
                }
                else
                {
                    if(!simulatedResponse.dismissed && simulatedResponse.buttonIndex && *simulatedResponse.buttonIndex < description.buttons.size())
                    {
                        selectedButton = nativeBase + static_cast<int>(*simulatedResponse.buttonIndex);
                    }
                    else if(!simulatedResponse.dismissed)
                    {
                        selectedButton = std::numeric_limits<int>::min();
                    }

                    if(simulatedResponse.optionIndex)
                    {
                        const std::size_t maximumRepresentableOffset = static_cast<std::size_t>(std::numeric_limits<int>::max() - optionBase);
                        selectedOption = *simulatedResponse.optionIndex <= maximumRepresentableOffset
                                             ? optionBase + static_cast<int>(*simulatedResponse.optionIndex)
                                             : std::numeric_limits<int>::max();
                    }
                    checked = simulatedResponse.checkBoxChecked ? TRUE : FALSE;

                    if(Internal::consumeFailure(GameWIP::Desktop::Internal::TestHooks::FailurePoint::DialogResult))
                    {
                        selectedButton = std::numeric_limits<int>::min();
                    }
                }
            }
            else
#endif
            {
                result = TaskDialogIndirect(&config, &selectedButton, &selectedOption, &checked);
            }

            if(FAILED(result))
            {
                output.status = statusFromHResult(result);
                return sharedResult(std::move(output));
            }

            output.status = GameWIP::Status::Types::Status{};

            if(selectedOption != 0 && (selectedOption < optionBase || selectedOption >= optionBase + static_cast<int>(description.options.size())))
            {
                output.status = GameWIP::Desktop::Internal::statusFor(ErrorCode::NativeFailure);
                return sharedResult(std::move(output));
            }

            if(selectedOption >= optionBase && selectedOption < optionBase + static_cast<int>(description.options.size()))
            {
                output.option = description.options[static_cast<std::size_t>(selectedOption - optionBase)].id;
            }

            if(description.checkBox)
            {
                output.checkBoxChecked = checked != FALSE;
            }

            if(selectedButton == IDCANCEL)
            {
                output.outcome = Types::Dialogs::Outcome::Cancelled;
                output.button = description.cancelButton;
                return sharedResult(std::move(output));
            }

            if(selectedButton >= nativeBase && selectedButton < nativeBase + static_cast<int>(description.buttons.size()))
            {
                output.button = description.buttons[static_cast<std::size_t>(selectedButton - nativeBase)].id;
                output.outcome = description.cancelButton && output.button == description.cancelButton ? Types::Dialogs::Outcome::Cancelled
                                                                                                       : Types::Dialogs::Outcome::Accepted;
                return sharedResult(std::move(output));
            }
            output = {};
            output.status = GameWIP::Desktop::Internal::statusFor(ErrorCode::NativeFailure);
        }
        catch(const std::bad_alloc &)
        {
            output = {};
            output.status = GameWIP::Desktop::Internal::statusFor(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            output = {};
            output.status = GameWIP::Desktop::Internal::statusFor(ErrorCode::Unknown);
        }

        return sharedResult(std::move(output));
    }

} // namespace GameWIP::Desktop::Platform

#if DESKTOP_INTERNAL_TEST_HOOKS
namespace GameWIP::Desktop::Internal::Win32
{
    using namespace GameWIP::Desktop::Platform;

    GameWIP::Status::Types::Status testDialogApartment() noexcept
    {
        ApartmentLease apartment;
        const GameWIP::Status::Types::Status &status = apartment.status();
        return status.ok() ? GameWIP::Status::Types::Status{} : status;
    }
} // namespace GameWIP::Desktop::Internal::Win32
#endif
