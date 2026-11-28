/// @file terminal_output_types_header.cpp
/// @brief Verifies that the Terminal output-types header is self-contained.

#include "terminal/output_types.h"

#include <type_traits>

static_assert(std::is_default_constructible_v<GameWIP::Terminal::Types::Output::TextOptions>);
static_assert(std::is_default_constructible_v<GameWIP::Terminal::Types::Cursor::QueryOptions>);
