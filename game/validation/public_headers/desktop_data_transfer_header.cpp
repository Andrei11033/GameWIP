/// @file desktop_data_transfer_header.cpp
/// @brief Standalone public-header compilation check for desktop/data_transfer.h.

#include "desktop/data_transfer.h"

#include <type_traits>

static_assert(std::is_same_v<GameWIP::Desktop::Types::DataTransfer::Payload::value_type, GameWIP::Desktop::Types::DataTransfer::Item>);
