#include "filesystem/filesystem.h"

namespace GameWIP::FileSystem
{

    namespace
    {

    }

    Types::PathResult getCurrentDirectory() noexcept;

    IO::Types::Status setCurrentDirectory(const Types::Path &path) noexcept;

    Types::PathResult parentPath(const Types::Path &path) noexcept
    {
        try
        {
            return {.status = IO::successStatus(), .path = path.parent_path()};
        }
        catch (const std::bad_alloc &)
        {
            return {.status = IO::makeStatus(IO::Types::ErrorCode::OutOfMemory)};
        }
        catch (...)
        {
            return {.status = IO::makeStatus(IO::Types::ErrorCode::InvalidArgument)};
        }
    }

    Types::PathResult filename(const Types::Path &path) noexcept
    {
        try
        {
            return {.status = IO::successStatus(), .path = path.filename()};
        }
        catch (const std::bad_alloc &)
        {
            return {.status = IO::makeStatus(IO::Types::ErrorCode::OutOfMemory)};
        }
        catch (...)
        {
            return {.status = IO::makeStatus(IO::Types::ErrorCode::InvalidArgument)};
        }
    }

    Types::PathResult stem(const Types::Path &path) noexcept
    {
        try
        {
            return {.status = IO::successStatus(), .path = path.stem()};
        }
        catch (const std::bad_alloc &)
        {
            return {.status = IO::makeStatus(IO::Types::ErrorCode::OutOfMemory)};
        }
        catch (...)
        {
            return {.status = IO::makeStatus(IO::Types::ErrorCode::InvalidArgument)};
        }
    }

    Types::PathResult extension(const Types::Path &path) noexcept
    {
        try
        {
            return {.status = IO::successStatus(), .path = path.extension()};
        }
        catch (const std::bad_alloc &)
        {
            return {.status = IO::makeStatus(IO::Types::ErrorCode::OutOfMemory)};
        }
        catch (...)
        {
            return {.status = IO::makeStatus(IO::Types::ErrorCode::InvalidArgument)};
        }
    }

    Types::PathResult replaceExtension(const Types::Path &path, const Types::Path &newExtension) noexcept
    {
        try
        {
            Types::Path result = path;
            result.replace_extension(newExtension);

            return {.status = IO::successStatus(), .path = result};
        }
        catch (const std::bad_alloc &)
        {
            return {.status = IO::makeStatus(IO::Types::ErrorCode::OutOfMemory)};
        }
        catch (...)
        {
            return {.status = IO::makeStatus(IO::Types::ErrorCode::InvalidArgument)};
        }
    }

    Types::PathResult joinPath(const Types::Path &left, const Types::Path &right) noexcept
    {
        try
        {
            Types::Path result = left;
            result /= right;

            return {.status = IO::successStatus(), .path = result};
        }
        catch (const std::bad_alloc &)
        {
            return {.status = IO::makeStatus(IO::Types::ErrorCode::OutOfMemory)};
        }
        catch (...)
        {
            return {.status = IO::makeStatus(IO::Types::ErrorCode::InvalidArgument)};
        }
    }

    Types::BoolResult isAbsolutePath(const Types::Path &path) noexcept
    {
        try
        {
            return {.status = IO::successStatus(), .value = path.is_absolute()};
        }
        catch (const std::bad_alloc &)
        {
            return {.status = IO::makeStatus(IO::Types::ErrorCode::OutOfMemory)};
        }
        catch (...)
        {
            return {.status = IO::makeStatus(IO::Types::ErrorCode::InvalidArgument)};
        }
    }

    Types::BoolResult isRelativePath(const Types::Path &path) noexcept
    {
        try
        {
            return {.status = IO::successStatus(), .value = path.is_relative()};
        }
        catch (const std::bad_alloc &)
        {
            return {.status = IO::makeStatus(IO::Types::ErrorCode::OutOfMemory)};
        }
        catch (...)
        {
            return {.status = IO::makeStatus(IO::Types::ErrorCode::InvalidArgument)};
        }
    }

    Types::PathResult absolutePath(const Types::Path &path) noexcept
    {
        try
        {
            return {.status = IO::successStatus(), .path = std::filesystem::absolute(path)};
        }
        catch (const std::bad_alloc &)
        {
            return {.status = IO::makeStatus(IO::Types::ErrorCode::OutOfMemory)};
        }
        catch (...)
        {
            return {.status = IO::makeStatus(IO::Types::ErrorCode::InvalidArgument)};
        }
    }

    Types::PathResult canonicalPath(const Types::Path &path) noexcept
    {
        try
        {
            return {.status = IO::successStatus(), .path = std::filesystem::canonical(path)};
        }
        catch (const std::bad_alloc &)
        {
            return {.status = IO::makeStatus(IO::Types::ErrorCode::OutOfMemory)};
        }
        catch (...)
        {
            return {.status = IO::makeStatus(IO::Types::ErrorCode::InvalidArgument)};
        }
    }

    Types::PathResult weaklyCanonicalPath(const Types::Path &path) noexcept
    {
        try
        {
            return {.status = IO::successStatus(), .path = std::filesystem::weakly_canonical(path)};
        }
        catch (const std::bad_alloc &)
        {
            return {.status = IO::makeStatus(IO::Types::ErrorCode::OutOfMemory)};
        }
        catch (...)
        {
            return {.status = IO::makeStatus(IO::Types::ErrorCode::InvalidArgument)};
        }
    }

    Types::PathResult getTemporaryDirectoryPath() noexcept
    {
        try
        {
            return {.status = IO::successStatus(), .path = std::filesystem::temp_directory_path()};
        }
        catch (const std::bad_alloc &)
        {
            return {.status = IO::makeStatus(IO::Types::ErrorCode::OutOfMemory)};
        }
        catch (...)
        {
            return {.status = IO::makeStatus(IO::Types::ErrorCode::InvalidArgument)};
        }
    }

    Types::PathResult pathFromUtf8(std::string_view utf8Path) noexcept
    {
        try
        {
            const char8_t *u8Data = reinterpret_cast<const char8_t *>(utf8Path.data());
            std::u8string_view u8View{u8Data, utf8Path.size()};
            Types::Path result{u8View};

            return {.status = IO::successStatus(), .path = result};
        }
        catch (const std::bad_alloc &)
        {
            return {.status = IO::makeStatus(IO::Types::ErrorCode::OutOfMemory)};
        }
        catch (...)
        {
            return {.status = IO::makeStatus(IO::Types::ErrorCode::EncodingFailed)};
        }
    }

    Types::Utf8PathResult pathToUtf8(const Types::Path &path) noexcept
    {
        try
        {
            std::u8string u8String = path.u8string();
            std::string utf8{u8String.begin(), u8String.end()};

            return {.status = IO::successStatus(), .utf8 = utf8};
        }
        catch (const std::bad_alloc &)
        {
            return {.status = IO::makeStatus(IO::Types::ErrorCode::OutOfMemory)};
        }
        catch (...)
        {
            return {.status = IO::makeStatus(IO::Types::ErrorCode::EncodingFailed)};
        }
    }
} // namespace GameWIP::FileSystem
