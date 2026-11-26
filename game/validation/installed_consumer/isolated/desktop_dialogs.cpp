/// @file desktop_dialogs.cpp
/// @brief Isolated installed-consumer check for desktop/dialogs.h.

#include "desktop/dialogs.h"

#include <cstddef>
#include <span>

bool consumeDesktopDialogs()
{
    namespace DialogTypes = GameWIP::Desktop::Types::Dialogs;

    const DialogTypes::File::Filter filter{"GameWIP project", {}};
    const DialogTypes::File::OpenDescription openDescription{.filters = std::span{&filter, std::size_t{1}}};
    const DialogTypes::File::Selection fileSelection;
    const DialogTypes::Message::Description message;
    const DialogTypes::Prompt::ButtonId buttonId{7};
    const DialogTypes::Progress::Description progressDescription;
    GameWIP::Desktop::ProgressDialog progress;

    return openDescription.filters.size() == 1 && fileSelection.outcome == DialogTypes::Outcome::Cancelled &&
           message.buttons == DialogTypes::Message::Buttons::Ok && buttonId.isValid() &&
           progressDescription.presentationMode == DialogTypes::Progress::Mode::Indeterminate && !progress.isOpen() && progress.close().ok();
}
