/// @file path_text.h
/// @brief Shared UTF-8 filesystem-path conversions for validation tests.

#pragma once

#include "filesystem/path.h"

#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace GameWIP::Test::Internal
{
    /// ------------------------------------------------------------
    /// UTF-8 path conversion
    /// ------------------------------------------------------------

    template <typename Context>
    [[nodiscard]] std::string pathToUtf8(Context &context, const std::filesystem::path &path, std::string_view failureName)
    {
        GameWIP::FileSystem::Types::Utf8PathResult result = GameWIP::FileSystem::pathToUtf8(path);

        if(!result.status.ok())
        {
            context.fail(failureName, std::format("code={} nativeCode={}", static_cast<int>(result.status.code), result.status.nativeCode));
            return {};
        }

        return std::move(result.utf8);
    }

    template <typename Context>
    [[nodiscard]] std::filesystem::path pathFromUtf8(Context &context, std::string_view text, std::string_view failureName)
    {
        GameWIP::FileSystem::Types::PathResult result = GameWIP::FileSystem::pathFromUtf8(text);

        if(!result.status.ok())
        {
            context.fail(failureName, std::format("code={} nativeCode={}", static_cast<int>(result.status.code), result.status.nativeCode));
            return {};
        }

        return std::move(result.path);
    }
} // namespace GameWIP::Test::Internal
