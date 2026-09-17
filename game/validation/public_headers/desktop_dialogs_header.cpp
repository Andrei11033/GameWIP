/// @file desktop_dialogs_header.cpp
/// @brief Standalone public-header compilation check for desktop/dialogs.h.

#include "desktop/dialogs.h"

#include <type_traits>
#include <utility>

static_assert(!std::is_copy_constructible_v<GameWIP::Desktop::ProgressDialog>);
static_assert(!std::is_move_constructible_v<GameWIP::Desktop::ProgressDialog>);
static_assert(noexcept(std::declval<GameWIP::Desktop::ProgressDialog &>().close()));
static_assert(!GameWIP::Desktop::Types::Dialogs::Prompt::ButtonId{}.isValid());
