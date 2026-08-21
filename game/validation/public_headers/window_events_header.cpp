/// @file window_events_header.cpp
/// @brief Verifies the source-tree focused Window events header.

#include "window/events.h"

#include <type_traits>

static_assert(std::is_same_v<decltype(GameWIP::Window::Types::Events::FilesDropped{}.paths)::value_type, GameWIP::FileSystem::Types::Path>);
static_assert(noexcept(GameWIP::Window::Events::poll()));
static_assert(noexcept(GameWIP::Window::Events::wait()));
