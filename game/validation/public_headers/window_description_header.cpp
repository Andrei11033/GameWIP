#include "window/description.h"

static_assert(GameWIP::Window::Types::Controls{}.closable);
static_assert(GameWIP::Window::Types::ModeRequest{}.mode == GameWIP::Window::Types::Mode::Windowed);
