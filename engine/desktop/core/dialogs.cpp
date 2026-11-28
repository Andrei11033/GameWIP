/// @file dialogs.cpp
/// @brief Portable semantic validation and forwarding for one-shot dialogs.

#include "desktop/dialogs.h"

#include "desktop/internal/window_state.h"
#include "desktop/platform/dialogs.h"
#include "desktop/platform/window.h"

#include <new>
#include <optional>
#include <utility>

namespace GameWIP::Desktop::Dialogs
{
    namespace
    {
        using GameWIP::Status::Types::Code;
        using GameWIP::Status::Types::Result;
        using GameWIP::Status::Types::Status;

        /// @brief Builds a shared status from one portable error code.
        [[nodiscard]] Status error(Code code) noexcept
        {
            return Status(code);
        }

        // Owner and text validation

        /// @brief Rejects embedded NUL characters before text reaches a native dialog API.
        [[nodiscard]] Status validateText(std::string_view text) noexcept
        {
            return text.contains('\0') ? error(Code::InvalidArgument) : Status{};
        }

        /// @brief Validates optional Window ownership and enforces owner-thread dialog calls.
        [[nodiscard]] Status validateOwner(Window *owner) noexcept
        {
            if(owner == nullptr)
            {
                return {};
            }
            Internal::WindowState *state = Internal::WindowAccess::state(*owner);

            if(state == nullptr || !Platform::hasLiveNativeWindow(*state))
            {
                return error(Code::NotOpen);
            }
            return Platform::ownedByCurrentThread(*state) ? Status{} : error(Code::ResourceBusy);
        }

        // File-filter validation

        /// @brief Returns whether an extension is one native-filter-safe filename component.
        [[nodiscard]] bool validExtensionSyntax(std::string_view extension) noexcept
        {
            return !extension.empty() && !extension.starts_with('.') && !extension.contains('*') && !extension.contains('?') &&
                   !extension.contains('/') && !extension.contains('\\') && !extension.contains('\0');
        }

        /// @brief Applies the additional delimiter rule used by native filter construction.
        [[nodiscard]] bool validFilterExtensionSyntax(std::string_view extension) noexcept
        {
            return validExtensionSyntax(extension) && !extension.contains(';');
        }

        /// @brief Validates filter text and the selected filter index before native conversion.
        [[nodiscard]] Status validateFilters(
            std::span<const Types::Dialogs::File::Filter> filters,
            std::optional<std::size_t> preferredFilterIndex) noexcept
        {
            if(preferredFilterIndex && *preferredFilterIndex >= filters.size())
            {
                return error(Code::InvalidArgument);
            }

            for(const Types::Dialogs::File::Filter &filter : filters)
            {
                Status status = validateText(filter.name);

                if(!status.ok())
                {
                    return status;
                }

                for(std::string_view extension : filter.extensions)
                {
                    status = validateText(extension);

                    if(!status.ok())
                    {
                        return status;
                    }

                    if(!validFilterExtensionSyntax(extension))
                    {
                        return error(Code::InvalidArgument);
                    }
                }
            }

            return {};
        }

        // Message and prompt validation

        /// @brief Validates the portable severity values accepted by every dialog backend.
        [[nodiscard]] bool validSeverity(Types::Dialogs::Severity severity) noexcept
        {
            using Types::Dialogs::Severity;
            return severity == Severity::None || severity == Severity::Information || severity == Severity::Warning || severity == Severity::Error;
        }

        /// @brief Validates the supported message-button combinations.
        [[nodiscard]] bool validMessageButtons(Types::Dialogs::Message::Buttons buttons) noexcept
        {
            using Types::Dialogs::Message::Buttons;
            return buttons == Buttons::Ok || buttons == Buttons::OkCancel || buttons == Buttons::YesNo || buttons == Buttons::YesNoCancel ||
                   buttons == Buttons::RetryCancel;
        }

        /// @brief Returns whether a message-button set contains the requested result.
        [[nodiscard]] bool messageContainsButton(Types::Dialogs::Message::Buttons buttons, Types::Dialogs::Message::Button button) noexcept
        {
            using Types::Dialogs::Message::Button;
            using Types::Dialogs::Message::Buttons;

            switch(buttons)
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

        /// @brief Returns whether a prompt button identifier is unique within the description.
        [[nodiscard]] bool containsButton(std::span<const Types::Dialogs::Prompt::Button> buttons, Types::Dialogs::Prompt::ButtonId id) noexcept
        {
            for(const auto &button : buttons)
            {
                if(button.id == id)
                {
                    return true;
                }
            }

            return false;
        }

        /// @brief Returns whether a prompt option identifier is unique within the description.
        [[nodiscard]] bool containsOption(std::span<const Types::Dialogs::Prompt::Option> options, Types::Dialogs::Prompt::OptionId id) noexcept
        {
            for(const auto &option : options)
            {
                if(option.id == id)
                {
                    return true;
                }
            }

            return false;
        }

        /// @brief Validates prompt text, identifiers, defaults, and optional checkbox state.
        [[nodiscard]] Status validatePrompt(const Types::Dialogs::Prompt::Description &description) noexcept
        {
            if(!validSeverity(description.severity) || description.buttons.empty())
            {
                return error(Code::InvalidArgument);
            }

            for(std::string_view text :
                {description.title, description.heading, description.message, description.details, description.supplementalText})
            {
                Status status = validateText(text);

                if(!status.ok())
                {
                    return status;
                }
            }

            for(std::size_t index = 0; index < description.buttons.size(); ++index)
            {
                const auto &button = description.buttons[index];

                if(!button.id.isValid() || button.label.empty())
                {
                    return error(Code::InvalidArgument);
                }

                for(std::size_t previous = 0; previous < index; ++previous)
                {
                    if(description.buttons[previous].id == button.id)
                    {
                        return error(Code::InvalidArgument);
                    }
                }

                for(std::string_view text : {button.label, button.description})
                {
                    Status status = validateText(text);

                    if(!status.ok())
                    {
                        return status;
                    }
                }
            }

            for(std::size_t index = 0; index < description.options.size(); ++index)
            {
                const auto &option = description.options[index];

                if(!option.id.isValid() || option.label.empty())
                {
                    return error(Code::InvalidArgument);
                }

                for(std::size_t previous = 0; previous < index; ++previous)
                {
                    if(description.options[previous].id == option.id)
                    {
                        return error(Code::InvalidArgument);
                    }
                }

                Status status = validateText(option.label);

                if(!status.ok())
                {
                    return status;
                }
            }

            if((description.defaultButton && !containsButton(description.buttons, *description.defaultButton)) ||
               (description.cancelButton && !containsButton(description.buttons, *description.cancelButton)) ||
               (description.defaultOption && !containsOption(description.options, *description.defaultOption)))
            {
                return error(Code::InvalidArgument);
            }

            if(description.checkBox)
            {
                if(description.checkBox->label.empty())
                {
                    return error(Code::InvalidArgument);
                }

                Status status = validateText(description.checkBox->label);

                if(!status.ok())
                {
                    return status;
                }
            }

            return {};
        }

    } // namespace

    /// ------------------------------------------------------------
    /// File dialogs
    /// ------------------------------------------------------------

    Result<Types::Dialogs::File::Selection> openFile(const Types::Dialogs::File::OpenDescription &description) noexcept
    {
        try
        {
            Status status = validateOwner(description.owner);
            if(status.ok())
            {
                status = validateText(description.title);
            }
            if(status.ok())
            {
                status = validateFilters(description.filters, description.preferredFilterIndex);
            }
            if(!status.ok())
            {
                return Result<Types::Dialogs::File::Selection>::failure(std::move(status));
            }

            return Platform::openFile(description);
        }
        catch(const std::bad_alloc &)
        {
            return Result<Types::Dialogs::File::Selection>::failure(Status(Code::OutOfMemory));
        }
        catch(...)
        {
            return Result<Types::Dialogs::File::Selection>::failure(Status(Code::Unknown));
        }
    }

    Result<Types::Dialogs::File::SelectionList> openFiles(const Types::Dialogs::File::OpenDescription &description) noexcept
    {
        try
        {
            Status status = validateOwner(description.owner);
            if(status.ok())
            {
                status = validateText(description.title);
            }
            if(status.ok())
            {
                status = validateFilters(description.filters, description.preferredFilterIndex);
            }
            if(!status.ok())
            {
                return Result<Types::Dialogs::File::SelectionList>::failure(std::move(status));
            }

            return Platform::openFiles(description);
        }
        catch(const std::bad_alloc &)
        {
            return Result<Types::Dialogs::File::SelectionList>::failure(Status(Code::OutOfMemory));
        }
        catch(...)
        {
            return Result<Types::Dialogs::File::SelectionList>::failure(Status(Code::Unknown));
        }
    }

    Result<Types::Dialogs::File::Selection> saveFile(const Types::Dialogs::File::SaveDescription &description) noexcept
    {
        try
        {
            Status status = validateOwner(description.owner);
            if(status.ok())
            {
                status = validateText(description.title);
            }
            if(status.ok())
            {
                status = validateText(description.suggestedFileName);
            }
            if(status.ok())
            {
                status = validateFilters(description.filters, description.preferredFilterIndex);
            }

            // Validate the suggested extension after the shared filters.
            if(status.ok() && !description.suggestedExtension.empty())
            {
                status = validateText(description.suggestedExtension);
                if(status.ok() && !validExtensionSyntax(description.suggestedExtension))
                {
                    status = error(Code::InvalidArgument);
                }
            }
            if(!status.ok())
            {
                return Result<Types::Dialogs::File::Selection>::failure(std::move(status));
            }

            return Platform::saveFile(description);
        }
        catch(const std::bad_alloc &)
        {
            return Result<Types::Dialogs::File::Selection>::failure(Status(Code::OutOfMemory));
        }
        catch(...)
        {
            return Result<Types::Dialogs::File::Selection>::failure(Status(Code::Unknown));
        }
    }

    Result<Types::Dialogs::File::Selection> selectFolder(const Types::Dialogs::File::FolderDescription &description) noexcept
    {
        try
        {
            Status status = validateOwner(description.owner);
            if(status.ok())
            {
                status = validateText(description.title);
            }
            if(!status.ok())
            {
                return Result<Types::Dialogs::File::Selection>::failure(std::move(status));
            }

            return Platform::selectFolder(description);
        }
        catch(const std::bad_alloc &)
        {
            return Result<Types::Dialogs::File::Selection>::failure(Status(Code::OutOfMemory));
        }
        catch(...)
        {
            return Result<Types::Dialogs::File::Selection>::failure(Status(Code::Unknown));
        }
    }

    Result<Types::Dialogs::File::SelectionList> selectFolders(const Types::Dialogs::File::FolderDescription &description) noexcept
    {
        try
        {
            Status status = validateOwner(description.owner);
            if(status.ok())
            {
                status = validateText(description.title);
            }
            if(!status.ok())
            {
                return Result<Types::Dialogs::File::SelectionList>::failure(std::move(status));
            }

            return Platform::selectFolders(description);
        }
        catch(const std::bad_alloc &)
        {
            return Result<Types::Dialogs::File::SelectionList>::failure(Status(Code::OutOfMemory));
        }
        catch(...)
        {
            return Result<Types::Dialogs::File::SelectionList>::failure(Status(Code::Unknown));
        }
    }

    /// ------------------------------------------------------------
    /// Message and prompt dialogs
    /// ------------------------------------------------------------

    Result<Types::Dialogs::Message::Selection> showMessage(const Types::Dialogs::Message::Description &description) noexcept
    {
        try
        {
            // Validate owner access and the selected button combination.
            Status status = validateOwner(description.owner);

            if(status.ok() && (!validSeverity(description.severity) || !validMessageButtons(description.buttons) ||
                               (description.defaultButton != Types::Dialogs::Message::Button::None &&
                                !messageContainsButton(description.buttons, description.defaultButton))))
            {
                status = error(Code::InvalidArgument);
            }

            // Validate visible text before entering the native dialog.
            if(status.ok())
            {
                status = validateText(description.title);
            }

            if(status.ok())
            {
                status = validateText(description.message);
            }

            if(!status.ok())
            {
                return Result<Types::Dialogs::Message::Selection>::failure(std::move(status));
            }

            return Platform::showMessage(description);
        }
        catch(const std::bad_alloc &)
        {
            return Result<Types::Dialogs::Message::Selection>::failure(Status(Code::OutOfMemory));
        }
        catch(...)
        {
            return Result<Types::Dialogs::Message::Selection>::failure(Status(Code::Unknown));
        }
    }

    Result<Types::Dialogs::Prompt::Selection> showPrompt(const Types::Dialogs::Prompt::Description &description) noexcept
    {
        try
        {
            Status status = validateOwner(description.owner);

            if(status.ok())
            {
                status = validatePrompt(description);
            }

            if(!status.ok())
            {
                return Result<Types::Dialogs::Prompt::Selection>::failure(std::move(status));
            }

            return Platform::showPrompt(description);
        }
        catch(const std::bad_alloc &)
        {
            return Result<Types::Dialogs::Prompt::Selection>::failure(Status(Code::OutOfMemory));
        }
        catch(...)
        {
            return Result<Types::Dialogs::Prompt::Selection>::failure(Status(Code::Unknown));
        }
    }
} // namespace GameWIP::Desktop::Dialogs
