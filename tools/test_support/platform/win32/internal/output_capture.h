/// @file output_capture.h
/// @brief Private Win32 child-process capture helper for TestSupport.

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "test_support/types.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace GameWIP::TestSupport::Internal::Win32
{
    /// ------------------------------------------------------------
    /// Captured child output
    /// ------------------------------------------------------------

    void readCapturedOutput(
        HANDLE outputReadHandle,
        HANDLE outputDoneHandle,
        std::size_t captureLimit,
        std::string &outputBytes,
        bool &outputTruncated,
        Types::Status &outputStatus) noexcept;
} // namespace GameWIP::TestSupport::Internal::Win32
