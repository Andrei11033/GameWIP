/// @file environment_block.cpp
/// @brief Win32 child environment block construction for TestSupport.

#include "test_support/platform/win32/internal/child_process.h"
#include "test_support/platform/win32/internal/text.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <cwchar>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace GameWIP::TestSupport::Internal::Win32
{
    namespace
    {
        /// ------------------------------------------------------------
        /// Child environment entries
        /// ------------------------------------------------------------

        [[nodiscard]] int compareEnvironmentNames(std::wstring_view left, std::wstring_view right) noexcept
        {
            if(left.empty())
            {
                return right.empty() ? CSTR_EQUAL : CSTR_LESS_THAN;
            }

            if(right.empty())
            {
                return CSTR_GREATER_THAN;
            }

            constexpr std::size_t maximumLength = static_cast<std::size_t>(std::numeric_limits<int>::max());

            if(left.size() > maximumLength || right.size() > maximumLength)
            {
                return 0;
            }

            return CompareStringOrdinal(left.data(), static_cast<int>(left.size()), right.data(), static_cast<int>(right.size()), TRUE);
        }

        [[nodiscard]] std::wstring_view environmentEntryName(std::wstring_view entry) noexcept
        {
            if(!entry.empty() && entry.front() == L'=')
            {
                const std::size_t secondEquals = entry.find(L'=', 1);
                return secondEquals == std::wstring_view::npos ? entry : entry.substr(0, secondEquals);
            }

            const std::size_t equals = entry.find(L'=');
            return equals == std::wstring_view::npos ? entry : entry.substr(0, equals);
        }

        [[nodiscard]] bool sameEnvironmentName(std::wstring_view left, std::wstring_view right)
        {
            return compareEnvironmentNames(left, right) == CSTR_EQUAL;
        }

        void applyEnvironmentOverride(std::vector<std::wstring> &entries, const Types::Process::EnvironmentOverride &variable)
        {
            if(variable.name.empty() || variable.name.contains('='))
            {
                throw std::invalid_argument("Child-process environment names must be non-empty and cannot contain '='");
            }

            const std::wstring variableName = Internal::Win32::utf8ToWide(variable.name);
            const auto matchesName = [&variableName](const std::wstring &entry)
            {
                return sameEnvironmentName(environmentEntryName(entry), variableName);
            };

            if(!variable.value)
            {
                entries.erase(std::remove_if(entries.begin(), entries.end(), matchesName), entries.end());
                return;
            }

            const std::wstring replacement = variableName + L"=" + Internal::Win32::utf8ToWide(*variable.value);
            const auto existing = std::find_if(entries.begin(), entries.end(), matchesName);

            if(existing != entries.end())
            {
                *existing = replacement;
            }
            else
            {
                entries.push_back(replacement);
            }
        }

        [[nodiscard]] std::vector<std::wstring> inheritedEnvironmentEntries()
        {
            std::vector<std::wstring> entries;
            LPWCH environmentStrings = GetEnvironmentStringsW();

            if(environmentStrings == nullptr)
            {
                throw NativeInfrastructureFailure(Types::InfrastructureError::ProcessSetupFailed, GetLastError());
            }

            try
            {
                // The returned environment block is documented as double-NUL-terminated and has no separate length API.
#if defined(__clang__)
#pragma clang unsafe_buffer_usage begin
#endif
                for(LPWCH current = environmentStrings; *current != L'\0'; current += std::wcslen(current) + 1)
                {
                    entries.emplace_back(current);
                }
#if defined(__clang__)
#pragma clang unsafe_buffer_usage end
#endif
            }
            catch(...)
            {
                static_cast<void>(FreeEnvironmentStringsW(environmentStrings));
                throw;
            }

            if(FreeEnvironmentStringsW(environmentStrings) == FALSE)
            {
                throw NativeInfrastructureFailure(Types::InfrastructureError::ProcessSetupFailed, GetLastError());
            }

            return entries;
        }

    } // namespace
    /// ------------------------------------------------------------
    /// Child environment block construction
    /// ------------------------------------------------------------

    std::wstring buildEnvironmentBlock(const Types::Process::Options &options)
    {
        // Start from the parent environment only when inheritance is enabled.
        std::vector<std::wstring> entries = options.inheritParentEnvironment ? inheritedEnvironmentEntries() : std::vector<std::wstring>{};

        // Apply overrides in caller order.
        for(const Types::Process::EnvironmentOverride &variable : options.environmentOverrides)
        {
            applyEnvironmentOverride(entries, variable);
        }

        // Win32 expects a sorted, double-NUL-terminated environment block.
        std::sort(
            entries.begin(),
            entries.end(),
            [](const std::wstring &left, const std::wstring &right)
            {
                return compareEnvironmentNames(environmentEntryName(left), environmentEntryName(right)) == CSTR_LESS_THAN;
            });

        std::wstring block;

        for(const std::wstring &entry : entries)
        {
            block += entry;
            block.push_back(L'\0');
        }
        block.push_back(L'\0');

        if(entries.empty())
        {
            block.push_back(L'\0');
        }

        return block;
    }
} // namespace GameWIP::TestSupport::Internal::Win32
