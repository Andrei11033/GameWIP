/// @file desktop_dialogs.cpp
/// @brief Isolated installed-consumer check for desktop/dialogs.h.

#include "desktop/dialogs.h"

bool consumeDesktopDialogs()
{
    namespace DialogTypes = GameWIP::Desktop::Types::Dialogs;

    const DialogTypes::File::Filter filter{"GameWIP project", {}};
    const DialogTypes::File::OpenDescription openDescription{.filters = std::span{&filter, std::size_t{1}}};
    const DialogTypes::File::Result fileResult;
    const DialogTypes::Message::Description message;
    const DialogTypes::Prompt::ButtonId buttonId{7};
    const DialogTypes::Progress::Description progressDescription;
    GameWIP::Desktop::ProgressDialog progress;

    return openDescription.filters.size() == 1 && fileResult.outcome == DialogTypes::Outcome::Cancelled &&
           message.buttons == DialogTypes::Message::Buttons::Ok && buttonId.isValid() &&
           progressDescription.mode == DialogTypes::Progress::Mode::Indeterminate && !progress.isOpen() && progress.close().ok();
}
