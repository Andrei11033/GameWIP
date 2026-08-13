#include "terminal/style.h"

#include <type_traits>

static_assert(noexcept(GameWIP::Terminal::defaultColor()));
static_assert(noexcept(GameWIP::Terminal::basicColor(GameWIP::Terminal::Types::Style::BasicColor::White)));
static_assert(std::is_same_v<decltype(GameWIP::Terminal::defaultColor()), GameWIP::Terminal::Types::Style::Color>);
