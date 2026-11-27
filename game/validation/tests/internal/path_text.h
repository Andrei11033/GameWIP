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
        auto result = GameWIP::FileSystem::pathToUtf8(path);

        if(!result.ok())
        {
            const GameWIP::Status::Types::Diagnostic *diagnostic = result.status().diagnostic();
            context.fail(
                failureName,
                std::format("code={} detail={}", static_cast<int>(result.status().code()), diagnostic == nullptr ? "" : diagnostic->message));
            return {};
        }

        return std::move(result).value();
    }

    template <typename Context>
    [[nodiscard]] std::filesystem::path pathFromUtf8(Context &context, std::string_view text, std::string_view failureName)
    {
        auto result = GameWIP::FileSystem::pathFromUtf8(text);

        if(!result.ok())
        {
            const GameWIP::Status::Types::Diagnostic *diagnostic = result.status().diagnostic();
            context.fail(
                failureName,
                std::format("code={} detail={}", static_cast<int>(result.status().code()), diagnostic == nullptr ? "" : diagnostic->message));
            return {};
        }

        return std::move(result).value();
    }
} // namespace GameWIP::Test::Internal
