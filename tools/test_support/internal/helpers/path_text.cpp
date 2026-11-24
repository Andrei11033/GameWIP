/// @file path_text.cpp
/// @brief UTF-8 path formatting for TestSupport diagnostics.

#include "test_support/internal/path_text.h"

namespace GameWIP::TestSupport::Internal
{
    std::string pathText(const std::filesystem::path &path)
    {
        const std::u8string encoded = path.u8string();
        return std::string{reinterpret_cast<const char *>(encoded.data()), encoded.size()};
    }
} // namespace GameWIP::TestSupport::Internal
