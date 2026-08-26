/// @file cursor.cpp
/// @brief Portable custom native cursor resource and validation implementation.

#include "window/cursor.h"

#include <limits>

namespace GameWIP::Window
{
    namespace
    {
        using IO::Types::ErrorCode;

        [[nodiscard]] IO::Types::Status error(ErrorCode code) noexcept
        {
            return IO::makeStatus(code);
        }

        [[nodiscard]] constexpr bool multiplicationWouldOverflow(std::size_t lhs, std::size_t rhs) noexcept
        {
            return rhs != 0 && lhs > std::numeric_limits<std::size_t>::max() / rhs;
        }

        [[nodiscard]] IO::Types::Status validateVariants(std::span<const Types::Cursor::ImageView> variants) noexcept
        {
            if (variants.empty())
                return error(ErrorCode::InvalidArgument);

            constexpr auto nativeMaximum = static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max());

            constexpr std::size_t channels = 4;

            for (std::size_t i = 0; i < variants.size(); ++i)
            {
                const auto &variant = variants[i];

                // Basic image contract.
                if (variant.size.width == 0 || variant.size.height == 0 || variant.size.width > nativeMaximum ||
                    variant.size.height > nativeMaximum || variant.hotspot.x >= variant.size.width || variant.hotspot.y >= variant.size.height ||
                    variant.intendedDpi == 0)
                {
                    return error(ErrorCode::InvalidArgument);
                }

                const std::size_t width = static_cast<std::size_t>(variant.size.width);
                const std::size_t height = static_cast<std::size_t>(variant.size.height);

                // Resolve and validate row stride.
                if (multiplicationWouldOverflow(width, channels))
                    return error(ErrorCode::InvalidArgument);

                const std::size_t packedRowBytes = width * channels;
                const std::size_t resolvedStride = variant.rowStrideBytes == 0 ? packedRowBytes : variant.rowStrideBytes;

                if (resolvedStride < packedRowBytes)
                    return error(ErrorCode::InvalidArgument);

                // Validate complete pixel payload size.
                if (multiplicationWouldOverflow(resolvedStride, height))
                    return error(ErrorCode::InvalidArgument);

                const std::size_t requiredBytes = resolvedStride * height;

                if (variant.rgba8.size() != requiredBytes)
                    return error(ErrorCode::InvalidArgument);

                // Each DPI may have only one variant.
                for (std::size_t previous = 0; previous < i; ++previous)
                {
                    if (variants[previous].intendedDpi == variant.intendedDpi)
                    {
                        return error(ErrorCode::InvalidArgument);
                    }
                }
            }

            return IO::successStatus();
        }
    } // namespace

    Cursor::Cursor() noexcept = default;
    Cursor::Cursor(const Cursor &) noexcept = default;
    Cursor &Cursor::operator=(const Cursor &) noexcept = default;
    Cursor::Cursor(Cursor &&) noexcept = default;
    Cursor &Cursor::operator=(Cursor &&) noexcept = default;
    Cursor::~Cursor() noexcept = default;

    bool Cursor::isValid() const noexcept
    {
        return static_cast<bool>(state_);
    }
} // namespace GameWIP::Window
