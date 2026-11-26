/// @file status_status_header.cpp
/// @brief Verifies that the Status header is self-contained.

#include "status/status.h"

#include <type_traits>
#include <utility>

using Status = GameWIP::Status::Types::Status;
using StatusCode = GameWIP::Status::Types::Code;

static_assert(std::is_same_v<decltype(std::declval<const Status &>().code()), StatusCode>);
static_assert(noexcept(std::declval<const Status &>().ok()));
