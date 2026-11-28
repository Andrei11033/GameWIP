/// @file path_text.h
/// @brief Private UTF-8 path formatting for TestSupport diagnostics.

#pragma once

#include <filesystem>
#include <string>

namespace GameWIP::TestSupport::Internal
{
    /// @brief Returns a filesystem path as UTF-8 text.
    [[nodiscard]] std::string pathText(const std::filesystem::path &path);
} // namespace GameWIP::TestSupport::Internal
