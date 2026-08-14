#include "window/types.h"

static_assert(!GameWIP::Window::Types::WindowId{}.isValid());
static_assert(GameWIP::Window::Types::WindowId{1}.isValid());
static_assert(GameWIP::Window::Types::LogicalSize{1, 2} == GameWIP::Window::Types::LogicalSize{1, 2});
