/// @file
/// @brief Verifies that the Terminal style public header is self-contained and preserves its noexcept type contracts.

#include "terminal/style.h"

#include <type_traits>

static_assert(noexcept(GameWIP::Terminal::defaultColor()));
static_assert(noexcept(GameWIP::Terminal::basicColor(GameWIP::Terminal::Types::Style::BasicColor::White)));
static_assert(std::is_same_v<decltype(GameWIP::Terminal::defaultColor()), GameWIP::Terminal::Types::Style::Color>);
