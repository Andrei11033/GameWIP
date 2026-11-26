/// @file win32_data_transfer.h
/// @brief Shared private Win32 wire-format preparation and materialization.

#pragma once

#include "desktop/data_transfer.h"
#include "status/result.h"

#include <windows.h>
#include <objidl.h>

#include <cstddef>
#include <vector>

namespace GameWIP::Desktop::Platform::DataTransfer
{
    /// @brief Native clipboard payload prepared for HGLOBAL publication.
    struct PreparedItem
    {
        CLIPFORMAT format = 0;
        std::vector<std::byte> bytes;
    };

    /// @brief Portable format paired with the Win32 clipboard format identity.
    struct FormatIdentity
    {
        Types::DataTransfer::Format portable;
        CLIPFORMAT native = 0;
    };

    /// @brief Converts one portable item into its native clipboard wire format.
    [[nodiscard]] GameWIP::Status::Types::Status prepare(const Types::DataTransfer::ItemView &, PreparedItem &) noexcept;
    /// @brief Allocates and copies one prepared payload into movable global memory.
    [[nodiscard]] GameWIP::Status::Types::Status copyToGlobal(const PreparedItem &, HGLOBAL &) noexcept;
    /// @brief Enumerates supported HGLOBAL formats exposed by a foreign data object.
    [[nodiscard]] GameWIP::Status::Types::Status formats(IDataObject &, std::vector<FormatIdentity> &) noexcept;
    /// @brief Decodes one HGLOBAL payload into the requested portable item kind.
    [[nodiscard]] GameWIP::Status::Types::Status materializeGlobal(HGLOBAL, const Types::DataTransfer::Format &, Types::DataTransfer::Item &) noexcept;
    /// @brief Requests and decodes one portable item from a foreign data object.
    [[nodiscard]] GameWIP::Status::Types::Status materialize(IDataObject &, const Types::DataTransfer::Format &, Types::DataTransfer::Item &) noexcept;
    /// @brief Resolves a portable format to its Win32 clipboard identifier.
    [[nodiscard]] CLIPFORMAT nativeFormat(const Types::DataTransfer::Format &, GameWIP::Status::Types::Status &) noexcept;
    /// @brief Compares portable formats using the native identity of custom formats.
    [[nodiscard]] bool equivalent(const Types::DataTransfer::Format &, const Types::DataTransfer::Format &) noexcept;
} // namespace GameWIP::Desktop::Platform::DataTransfer
