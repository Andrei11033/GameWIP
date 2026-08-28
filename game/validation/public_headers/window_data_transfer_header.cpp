/// @file window_data_transfer_header.cpp
/// @brief Standalone public-header compilation check for window/data_transfer.h.

#include "window/data_transfer.h"

#include <type_traits>

static_assert(std::is_same_v<GameWIP::Window::Types::DataTransfer::Payload::value_type, GameWIP::Window::Types::DataTransfer::Item>);
