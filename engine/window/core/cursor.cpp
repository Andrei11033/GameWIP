/// @file cursor.cpp
/// @brief Portable custom native cursor resource and validation implementation.

#include "window/cursor.h"

#include "window/internal/cursor_platform.h"
#include "window/internal/cursor_selection.h"
#include "window/internal/cursor_state.h"
#include "window/internal/window_platform.h"
#include "window/internal/window_state.h"
#include "window/internal/window_test_hooks.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <utility>
#include <vector>

namespace GameWIP::Window
{
    // ------------------------------------------------------------
    // Cursor validation and resource state
    // ------------------------------------------------------------
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

    // ------------------------------------------------------------
    // Shared cursor lifecycle
    // ------------------------------------------------------------
    Cursor::Cursor() noexcept = default;
    Cursor::Cursor(const Cursor &) noexcept = default;
    Cursor &Cursor::operator=(const Cursor &) noexcept = default;
    Cursor::Cursor(Cursor &&) noexcept = default;
    Cursor &Cursor::operator=(Cursor &&) noexcept = default;
    Cursor::~Cursor() noexcept = default;

    Cursor::Cursor(std::shared_ptr<const Detail::CursorState> state) noexcept
        : state_(std::move(state))
    {
    }

    bool Cursor::isValid() const noexcept
    {
        return static_cast<bool>(state_);
    }

    Detail::CursorState::CursorState(std::vector<NativeCursorVariant> nativeVariants) noexcept
        : variants(std::move(nativeVariants))
    {
    }

    Detail::CursorState::~CursorState() noexcept
    {
        Detail::Platform::destroyNativeCursorVariants(variants);
    }

    const Detail::NativeCursorVariant &Detail::CursorState::variantForDpi(std::uint32_t dpi) const noexcept
    {
        std::size_t best = 0;
        for (std::size_t index = 1; index < variants.size(); ++index)
        {
            if (isBetterDpiCandidate(variants[index].intendedDpi, variants[best].intendedDpi, dpi))
                best = index;
        }
        return variants[best];
    }

    Cursor Detail::CursorAccess::make(std::shared_ptr<const CursorState> state) noexcept
    {
        return Cursor(std::move(state));
    }

    const std::shared_ptr<const Detail::CursorState> &Detail::CursorAccess::state(const Cursor &cursor) noexcept
    {
        return cursor.state_;
    }

    // ------------------------------------------------------------
    // Cursor creation and Window binding
    // ------------------------------------------------------------
    Types::Cursor::CreateResult createCursor(const Types::Cursor::ImageView &image) noexcept
    {
        return createCursor(std::span{&image, std::size_t{1}});
    }

    Types::Cursor::CreateResult createCursor(std::span<const Types::Cursor::ImageView> variants) noexcept
    {
        IO::Types::Status status = validateVariants(variants);
        if (!status.ok())
            return {.status = std::move(status)};

        std::vector<Detail::NativeCursorVariant> nativeVariants;
        try
        {
            if (Detail::consumeFailure(TestHooks::FailurePoint::Allocation))
                throw std::bad_alloc{};
            nativeVariants.reserve(variants.size());
            status = Detail::Platform::createNativeCursorVariants(variants, nativeVariants);
            if (!status.ok())
            {
                Detail::Platform::destroyNativeCursorVariants(nativeVariants);
                return {.status = std::move(status)};
            }

            if (Detail::consumeFailure(TestHooks::FailurePoint::CursorStateAllocation))
                throw std::bad_alloc{};
            auto state = std::make_shared<Detail::CursorState>(std::move(nativeVariants));
            return {.status = IO::successStatus(), .cursor = Detail::CursorAccess::make(std::move(state))};
        }
        catch (const std::bad_alloc &)
        {
            Detail::Platform::destroyNativeCursorVariants(nativeVariants);
            return {.status = error(ErrorCode::OutOfMemory)};
        }
        catch (...)
        {
            Detail::Platform::destroyNativeCursorVariants(nativeVariants);
            return {.status = error(ErrorCode::Unknown)};
        }
    }

    IO::Types::Status setCursor(Window &window, const Cursor &cursor) noexcept
    {
        Detail::WindowState *state = Detail::WindowAccess::state(window);
        if (state == nullptr || !state->platform || !Detail::Platform::hasLiveNativeWindow(*state))
            return error(ErrorCode::NotOpen);
        if (!Detail::Platform::isOwnedByCurrentThread(*state))
            return error(ErrorCode::ResourceBusy);
        if (!cursor.isValid())
            return error(ErrorCode::InvalidArgument);
        return Detail::Platform::setCustomCursor(*state, Detail::CursorAccess::state(cursor));
    }

    bool hasCustomCursor(const Window &window) noexcept
    {
        const Detail::WindowState *state = Detail::WindowAccess::state(window);
        return state != nullptr && state->platform && Detail::Platform::hasLiveNativeWindow(*state) &&
               Detail::Platform::isOwnedByCurrentThread(*state) && Detail::Platform::hasCustomCursor(*state);
    }
} // namespace GameWIP::Window
