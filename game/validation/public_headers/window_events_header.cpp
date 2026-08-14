#include "window/events.h"

#include <type_traits>

static_assert(std::is_same_v<decltype(GameWIP::Window::Types::Events::FilesDropped{}.paths)::value_type, GameWIP::FileSystem::Types::Path>);
static_assert(noexcept(GameWIP::Window::Events::poll()));
static_assert(noexcept(GameWIP::Window::Events::wait()));
