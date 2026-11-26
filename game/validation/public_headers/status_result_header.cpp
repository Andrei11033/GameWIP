/// @file status_result_header.cpp
/// @brief Verifies that the Result header is self-contained.

#include "status/result.h"

#include <type_traits>

static_assert(!std::is_default_constructible_v<GameWIP::Status::Types::Result<int>>);
