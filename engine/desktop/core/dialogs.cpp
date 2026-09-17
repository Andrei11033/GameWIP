/// @file dialogs.cpp
/// @brief Portable semantic validation and forwarding for one-shot dialogs.

#include "desktop/dialogs.h"

#include "desktop/internal/dialogs_platform.h"
#include "desktop/internal/window_platform.h"
#include "desktop/internal/window_state.h"

#include <new>

namespace GameWIP::Desktop::Dialogs
{
    namespace
    {
        using IO::Types::ErrorCode;

        [[nodiscard]] IO::Types::Status error(ErrorCode code) noexcept
        {
            return IO::makeStatus(code);
        }

        [[nodiscard]] IO::Types::Status validateText(std::string_view text) noexcept
        {
            return text.contains('\0') ? error(ErrorCode::InvalidArgument) : IO::successStatus();
        }

        [[nodiscard]] IO::Types::Status validateOwner(Window *owner) noexcept
        {
            if (owner == nullptr)
            {
                return IO::successStatus();
            }
            Detail::WindowState *state = Detail::WindowAccess::state(*owner);
            if (state == nullptr || !Detail::Platform::hasLiveNativeWindow(*state))
            {
                return error(ErrorCode::NotOpen);
            }
            return Detail::Platform::ownedByCurrentThread(*state) ? IO::successStatus() : error(ErrorCode::ResourceBusy);
        }

        [[nodiscard]] bool validExtensionSyntax(std::string_view extension) noexcept
        {
            return !extension.empty() && !extension.starts_with('.') && !extension.contains('*') && !extension.contains('?') &&
                   !extension.contains('/') && !extension.contains('\\') && !extension.contains('\0');
        }

        [[nodiscard]] bool validFilterExtensionSyntax(std::string_view extension) noexcept
        {
            return validExtensionSyntax(extension) && !extension.contains(';');
        }

        [[nodiscard]] IO::Types::Status validateFilters(
            std::span<const Types::Dialogs::File::Filter> filters,
            std::optional<std::size_t> preferredFilterIndex) noexcept
        {
            if (preferredFilterIndex && *preferredFilterIndex >= filters.size())
            {
                return error(ErrorCode::InvalidArgument);
            }
            for (const Types::Dialogs::File::Filter &filter : filters)
            {
                IO::Types::Status status = validateText(filter.name);
                if (!status.ok())
                {
                    return status;
                }
                for (std::string_view extension : filter.extensions)
                {
                    status = validateText(extension);
                    if (!status.ok())
                    {
                        return status;
                    }
                    if (!validFilterExtensionSyntax(extension))
                    {
                        return error(ErrorCode::InvalidArgument);
                    }
                }
            }
            return IO::successStatus();
        }

        [[nodiscard]] bool validSeverity(Types::Dialogs::Severity severity) noexcept
        {
            using Severity = Types::Dialogs::Severity;
            return severity == Severity::None || severity == Severity::Information || severity == Severity::Warning || severity == Severity::Error;
        }

        [[nodiscard]] bool validMessageButtons(Types::Dialogs::Message::Buttons buttons) noexcept
        {
            using Buttons = Types::Dialogs::Message::Buttons;
            return buttons == Buttons::Ok || buttons == Buttons::OkCancel || buttons == Buttons::YesNo || buttons == Buttons::YesNoCancel ||
                   buttons == Buttons::RetryCancel;
        }

        [[nodiscard]] bool messageContainsButton(Types::Dialogs::Message::Buttons buttons, Types::Dialogs::Message::Button button) noexcept
        {
            using Button = Types::Dialogs::Message::Button;
            using Buttons = Types::Dialogs::Message::Buttons;
            switch (buttons)
            {
            case Buttons::Ok:
                return button == Button::Ok;
            case Buttons::OkCancel:
                return button == Button::Ok || button == Button::Cancel;
            case Buttons::YesNo:
                return button == Button::Yes || button == Button::No;
            case Buttons::YesNoCancel:
                return button == Button::Yes || button == Button::No || button == Button::Cancel;
            case Buttons::RetryCancel:
                return button == Button::Retry || button == Button::Cancel;
            }
            return false;
        }

        [[nodiscard]] bool containsButton(std::span<const Types::Dialogs::Prompt::Button> buttons, Types::Dialogs::Prompt::ButtonId id) noexcept
        {
            for (const auto &button : buttons)
            {
                if (button.id == id)
                {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] bool containsOption(std::span<const Types::Dialogs::Prompt::Option> options, Types::Dialogs::Prompt::OptionId id) noexcept
        {
            for (const auto &option : options)
            {
                if (option.id == id)
                {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] IO::Types::Status validatePrompt(const Types::Dialogs::Prompt::Description &description) noexcept
        {
            if (!validSeverity(description.severity) || description.buttons.empty())
            {
                return error(ErrorCode::InvalidArgument);
            }

            for (std::string_view text :
                 {description.title, description.heading, description.message, description.details, description.supplementalText})
            {
                IO::Types::Status status = validateText(text);
                if (!status.ok())
                {
                    return status;
                }
            }

            for (std::size_t index = 0; index < description.buttons.size(); ++index)
            {
                const auto &button = description.buttons[index];
                if (!button.id.isValid() || button.label.empty())
                {
                    return error(ErrorCode::InvalidArgument);
                }
                for (std::size_t previous = 0; previous < index; ++previous)
                {
                    if (description.buttons[previous].id == button.id)
                    {
                        return error(ErrorCode::InvalidArgument);
                    }
                }
                for (std::string_view text : {button.label, button.description})
                {
                    IO::Types::Status status = validateText(text);
                    if (!status.ok())
                    {
                        return status;
                    }
                }
            }

            for (std::size_t index = 0; index < description.options.size(); ++index)
            {
                const auto &option = description.options[index];
                if (!option.id.isValid() || option.label.empty())
                {
                    return error(ErrorCode::InvalidArgument);
                }
                for (std::size_t previous = 0; previous < index; ++previous)
                {
                    if (description.options[previous].id == option.id)
                    {
                        return error(ErrorCode::InvalidArgument);
                    }
                }
                IO::Types::Status status = validateText(option.label);
                if (!status.ok())
                {
                    return status;
                }
            }

            if ((description.defaultButton.isValid() && !containsButton(description.buttons, description.defaultButton)) ||
                (description.cancelButton.isValid() && !containsButton(description.buttons, description.cancelButton)) ||
                (description.defaultOption.isValid() && !containsOption(description.options, description.defaultOption)))
            {
                return error(ErrorCode::InvalidArgument);
            }
            if (description.checkBox)
            {
                if (description.checkBox->label.empty())
                {
                    return error(ErrorCode::InvalidArgument);
                }
                IO::Types::Status status = validateText(description.checkBox->label);
                if (!status.ok())
                {
                    return status;
                }
            }
            return IO::successStatus();
        }
    } // namespace

    Types::Dialogs::File::Result openFile(const Types::Dialogs::File::OpenDescription &description) noexcept
    {
        Types::Dialogs::File::Result result;
        try
        {
            result.status = validateOwner(description.owner);
            if (result.status.ok())
            {
                result.status = validateText(description.title);
            }
            if (result.status.ok())
            {
                result.status = validateFilters(description.filters, description.preferredFilterIndex);
            }
            return result.status.ok() ? Detail::Platform::openFile(description) : result;
        }
        catch (const std::bad_alloc &)
        {
            result.status = error(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            result.status = error(ErrorCode::Unknown);
        }
        return result;
    }

    Types::Dialogs::File::ListResult openFiles(const Types::Dialogs::File::OpenDescription &description) noexcept
    {
        Types::Dialogs::File::ListResult result;
        try
        {
            result.status = validateOwner(description.owner);
            if (result.status.ok())
            {
                result.status = validateText(description.title);
            }
            if (result.status.ok())
            {
                result.status = validateFilters(description.filters, description.preferredFilterIndex);
            }
            return result.status.ok() ? Detail::Platform::openFiles(description) : result;
        }
        catch (const std::bad_alloc &)
        {
            result.status = error(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            result.status = error(ErrorCode::Unknown);
        }
        return result;
    }

    Types::Dialogs::File::Result saveFile(const Types::Dialogs::File::SaveDescription &description) noexcept
    {
        Types::Dialogs::File::Result result;
        try
        {
            result.status = validateOwner(description.owner);
            if (result.status.ok())
            {
                result.status = validateText(description.title);
            }
            if (result.status.ok())
            {
                result.status = validateText(description.suggestedFileName);
            }
            if (result.status.ok())
            {
                result.status = validateFilters(description.filters, description.preferredFilterIndex);
            }
            if (result.status.ok() && !description.suggestedExtension.empty())
            {
                result.status = validateText(description.suggestedExtension);
                if (result.status.ok() && !validExtensionSyntax(description.suggestedExtension))
                {
                    result.status = error(ErrorCode::InvalidArgument);
                }
            }
            return result.status.ok() ? Detail::Platform::saveFile(description) : result;
        }
        catch (const std::bad_alloc &)
        {
            result.status = error(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            result.status = error(ErrorCode::Unknown);
        }
        return result;
    }

    Types::Dialogs::File::Result selectFolder(const Types::Dialogs::File::FolderDescription &description) noexcept
    {
        Types::Dialogs::File::Result result;
        try
        {
            result.status = validateOwner(description.owner);
            if (result.status.ok())
            {
                result.status = validateText(description.title);
            }
            return result.status.ok() ? Detail::Platform::selectFolder(description) : result;
        }
        catch (const std::bad_alloc &)
        {
            result.status = error(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            result.status = error(ErrorCode::Unknown);
        }
        return result;
    }

    Types::Dialogs::File::ListResult selectFolders(const Types::Dialogs::File::FolderDescription &description) noexcept
    {
        Types::Dialogs::File::ListResult result;
        try
        {
            result.status = validateOwner(description.owner);
            if (result.status.ok())
            {
                result.status = validateText(description.title);
            }
            return result.status.ok() ? Detail::Platform::selectFolders(description) : result;
        }
        catch (const std::bad_alloc &)
        {
            result.status = error(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            result.status = error(ErrorCode::Unknown);
        }
        return result;
    }

    Types::Dialogs::Message::Result showMessage(const Types::Dialogs::Message::Description &description) noexcept
    {
        Types::Dialogs::Message::Result result;
        try
        {
            result.status = validateOwner(description.owner);
            if (result.status.ok() && (!validSeverity(description.severity) || !validMessageButtons(description.buttons) ||
                                       (description.defaultButton != Types::Dialogs::Message::Button::None &&
                                        !messageContainsButton(description.buttons, description.defaultButton))))
            {
                result.status = error(ErrorCode::InvalidArgument);
            }
            if (result.status.ok())
            {
                result.status = validateText(description.title);
            }
            if (result.status.ok())
            {
                result.status = validateText(description.message);
            }
            return result.status.ok() ? Detail::Platform::showMessage(description) : result;
        }
        catch (const std::bad_alloc &)
        {
            result.status = error(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            result.status = error(ErrorCode::Unknown);
        }
        return result;
    }

    Types::Dialogs::Prompt::Result showPrompt(const Types::Dialogs::Prompt::Description &description) noexcept
    {
        Types::Dialogs::Prompt::Result result;
        try
        {
            result.status = validateOwner(description.owner);
            if (result.status.ok())
            {
                result.status = validatePrompt(description);
            }
            return result.status.ok() ? Detail::Platform::showPrompt(description) : result;
        }
        catch (const std::bad_alloc &)
        {
            result.status = error(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            result.status = error(ErrorCode::Unknown);
        }
        return result;
    }
} // namespace GameWIP::Desktop::Dialogs
