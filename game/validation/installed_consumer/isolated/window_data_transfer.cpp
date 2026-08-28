/// @file window_data_transfer.cpp
/// @brief Isolated installed-consumer check for window/data_transfer.h.

#include "window/data_transfer.h"

void consumeWindowDataTransfer()
{
    const GameWIP::Window::Types::DataTransfer::Format format{};
    static_cast<void>(format);
}
