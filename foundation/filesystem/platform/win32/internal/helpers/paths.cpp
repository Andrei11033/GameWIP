/// @file paths.cpp
/// @brief Win32 FileSystem path parsing and normalization.

#include "filesystem/platform/win32/internal/helpers.h"

namespace GameWIP::FileSystem::Platform::Win32
{
    /// ------------------------------------------------------------
    /// Path syntax helpers
    /// ------------------------------------------------------------

    /// @brief Recognizes either separator accepted by Windows path parsing.
    [[nodiscard]] bool isSeparator(wchar_t value) noexcept
    {
        return value == L'\\' || value == L'/';
    }

    /// @brief Performs locale-independent ASCII case folding for path prefixes.
    [[nodiscard]] wchar_t asciiUpper(wchar_t value) noexcept
    {
        if(value >= L'a' && value <= L'z')
        {
            return static_cast<wchar_t>(value - (L'a' - L'A'));
        }
        return value;
    }

    /// @brief Compares an ASCII Windows path prefix without locale dependence.
    [[nodiscard]] bool startsWithInsensitive(std::wstring_view text, std::wstring_view prefix) noexcept
    {
        if(text.size() < prefix.size())
        {
            return false;
        }

        for(std::size_t index = 0; index < prefix.size(); ++index)
        {
            if(asciiUpper(text[index]) != asciiUpper(prefix[index]))
            {
                return false;
            }
        }

        return true;
    }

    /// @brief Finds the next Windows path separator at or after offset.
    [[nodiscard]] std::size_t findSeparator(std::wstring_view text, std::size_t offset) noexcept
    {
        for(std::size_t index = offset; index < text.size(); ++index)
        {
            if(isSeparator(text[index]))
            {
                return index;
            }
        }

        return std::wstring_view::npos;
    }

    /// ------------------------------------------------------------
    /// Path normalization and parsing
    /// ------------------------------------------------------------

    /// @brief Detects embedded nulls that Win32 APIs would otherwise truncate.
    [[nodiscard]] bool containsEmbeddedNull(std::wstring_view text) noexcept
    {
        return std::find(text.begin(), text.end(), L'\0') != text.end();
    }

    /// @brief Adds the extended-length prefix while preserving UNC path semantics.
    [[nodiscard]] std::wstring asExtendedLengthPath(std::wstring path)
    {
        if(startsWithInsensitive(path, L"\\\\?\\") || startsWithInsensitive(path, L"\\\\.\\"))
        {
            return path;
        }

        if(path.size() >= 2 && isSeparator(path[0]) && isSeparator(path[1]))
        {
            path.erase(0, 2);
            return L"\\\\?\\UNC\\" + path;
        }
        return L"\\\\?\\" + path;
    }

    /// @brief Converts a non-empty path to normalized absolute UTF-16 extended form.
    [[nodiscard]] WidePathResult absoluteNativePath(const Types::Path &path)
    {
        const std::wstring nativePath = path.wstring();

        if(nativePath.empty() || containsEmbeddedNull(nativePath))
        {
            return {.status = IO::makeStatus(ErrorCode::InvalidArgument)};
        }

        DWORD requiredLength = GetFullPathNameW(nativePath.c_str(), 0, nullptr, nullptr);

        if(requiredLength == 0)
        {
            return {.status = makeLastErrorStatus(ErrorCode::StatFailed)};
        }

        std::wstring buffer(requiredLength, L'\0');
        DWORD copiedLength = GetFullPathNameW(nativePath.c_str(), requiredLength, buffer.data(), nullptr);

        if(copiedLength == 0)
        {
            return {.status = makeLastErrorStatus(ErrorCode::StatFailed)};
        }

        if(copiedLength >= requiredLength)
        {
            buffer.assign(copiedLength + 1, L'\0');
            copiedLength = GetFullPathNameW(nativePath.c_str(), static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);

            if(copiedLength == 0 || copiedLength >= buffer.size())
            {
                return {.status = makeLastErrorStatus(ErrorCode::StatFailed)};
            }
        }

        buffer.resize(copiedLength);
        return {.status = IO::successStatus(), .path = asExtendedLengthPath(std::move(buffer))};
    }

    /// @brief Normalizes one path component into a traversal vector without allowing upward escape.
    [[nodiscard]] bool appendComponent(std::vector<std::wstring> &components, std::wstring_view component)
    {
        if(component.empty() || component == L".")
        {
            return true;
        }

        if(component == L"..")
        {
            return false;
        }

        if(component.size() > std::numeric_limits<USHORT>::max() / sizeof(wchar_t))
        {
            return false;
        }
        components.emplace_back(component);
        return true;
    }

    /// @brief Splits a path suffix into normalized handle-traversal components.
    [[nodiscard]] bool splitComponents(std::wstring_view text, std::vector<std::wstring> &components)
    {
        std::size_t offset = 0;

        while(offset < text.size())
        {
            while(offset < text.size() && isSeparator(text[offset]))
            {
                ++offset;
            }

            if(offset >= text.size())
            {
                break;
            }

            const std::size_t separator = findSeparator(text, offset);
            const std::size_t end = separator == std::wstring_view::npos ? text.size() : separator;

            if(!appendComponent(components, text.substr(offset, end - offset)))
            {
                return false;
            }
            offset = end;
        }

        return true;
    }

    /// @brief Finds the end of a UNC server/share root or returns npos for malformed input.
    [[nodiscard]] std::size_t uncRootEnd(std::wstring_view text, std::size_t serverOffset) noexcept
    {
        const std::size_t serverEnd = findSeparator(text, serverOffset);

        if(serverEnd == std::wstring_view::npos || serverEnd == serverOffset)
        {
            return std::wstring_view::npos;
        }
        const std::size_t shareOffset = serverEnd + 1;
        const std::size_t shareEnd = findSeparator(text, shareOffset);

        if(shareEnd == shareOffset)
        {
            return std::wstring_view::npos;
        }

        if(shareEnd == std::wstring_view::npos)
        {
            return text.size();
        }
        return shareEnd + 1;
    }

    /// @brief Separates a normalized absolute drive/UNC path into root and safe components.
    [[nodiscard]] ParsedPathResult parseAbsolutePath(std::wstring_view fullPath)
    {
        if(fullPath.empty() || containsEmbeddedNull(fullPath))
        {
            return {.status = IO::makeStatus(ErrorCode::InvalidArgument)};
        }

        if(startsWithInsensitive(fullPath, L"\\\\.\\"))
        {
            return {.status = IO::makeStatus(ErrorCode::Unsupported, 0, "Win32 device paths are not supported by strict traversal")};
        }

        std::size_t rootEnd = std::wstring_view::npos;

        if(startsWithInsensitive(fullPath, L"\\\\?\\UNC\\"))
        {
            rootEnd = uncRootEnd(fullPath, 8);
        }
        else if(startsWithInsensitive(fullPath, L"\\\\?\\"))
        {
            if(fullPath.size() >= 7 && fullPath[5] == L':' && isSeparator(fullPath[6]))
            {
                rootEnd = 7;
            }
            else
            {
                const std::size_t deviceEnd = findSeparator(fullPath, 4);

                if(deviceEnd != std::wstring_view::npos)
                {
                    rootEnd = deviceEnd + 1;
                }
            }
        }
        else if(fullPath.size() >= 2 && isSeparator(fullPath[0]) && isSeparator(fullPath[1]))
        {
            rootEnd = uncRootEnd(fullPath, 2);
        }
        else if(fullPath.size() >= 3 && fullPath[1] == L':' && isSeparator(fullPath[2]))
        {
            rootEnd = 3;
        }

        if(rootEnd == std::wstring_view::npos || rootEnd == 0 || rootEnd > fullPath.size())
        {
            return {.status = IO::makeStatus(ErrorCode::InvalidArgument)};
        }

        ParsedPathResult result{.status = IO::successStatus(), .root = std::wstring(fullPath.substr(0, rootEnd))};

        if(!splitComponents(fullPath.substr(rootEnd), result.components))
        {
            return {.status = IO::makeStatus(ErrorCode::InvalidArgument)};
        }

        return result;
    }

} // namespace GameWIP::FileSystem::Platform::Win32
