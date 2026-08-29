/// @file desktop_events_header.cpp
/// @brief Verifies the source-tree focused Window events header.

#include "desktop/events.h"

#include <type_traits>

static_assert(std::is_same_v<decltype(GameWIP::Desktop::Types::Events::FilesDropped{}.paths)::value_type, GameWIP::FileSystem::Types::Path>);
static_assert(noexcept(GameWIP::Desktop::Events::poll()));
static_assert(noexcept(GameWIP::Desktop::Events::wait()));
