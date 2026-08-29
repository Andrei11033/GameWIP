/// @file desktop_data_transfer.cpp
/// @brief Isolated installed-consumer check for desktop/data_transfer.h.

#include "desktop/data_transfer.h"

void consumeWindowDataTransfer()
{
    const GameWIP::Desktop::Types::DataTransfer::Format format{};
    static_cast<void>(format);
}
