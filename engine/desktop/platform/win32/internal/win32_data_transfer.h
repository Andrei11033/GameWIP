/// @file win32_data_transfer.h
/// @brief Shared private Win32 wire-format preparation and materialization.

#pragma once

#include "desktop/data_transfer.h"
#include "io/status.h"

#include <windows.h>
#include <objidl.h>

#include <cstddef>
#include <vector>

namespace GameWIP::Desktop::Detail::Platform::DataTransfer
{
    struct PreparedItem
    {
        CLIPFORMAT format = 0;
        std::vector<std::byte> bytes;
    };

    struct FormatIdentity
    {
        Types::DataTransfer::Format portable;
        CLIPFORMAT native = 0;
    };

    [[nodiscard]] IO::Types::Status prepare(const Types::DataTransfer::ItemView &, PreparedItem &) noexcept;
    [[nodiscard]] IO::Types::Status copyToGlobal(const PreparedItem &, HGLOBAL &) noexcept;
    [[nodiscard]] IO::Types::Status formats(IDataObject &, std::vector<FormatIdentity> &) noexcept;
    [[nodiscard]] IO::Types::Status materializeGlobal(HGLOBAL, const Types::DataTransfer::Format &, Types::DataTransfer::Item &) noexcept;
    [[nodiscard]] IO::Types::Status materialize(IDataObject &, const Types::DataTransfer::Format &, Types::DataTransfer::Item &) noexcept;
    [[nodiscard]] CLIPFORMAT nativeFormat(const Types::DataTransfer::Format &, IO::Types::Status &) noexcept;
    [[nodiscard]] bool equivalent(const Types::DataTransfer::Format &, const Types::DataTransfer::Format &) noexcept;
} // namespace GameWIP::Desktop::Detail::Platform::DataTransfer
