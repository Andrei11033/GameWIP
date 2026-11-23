/// @file cursor.cpp
/// @brief Portable custom native cursor resource and validation implementation.

#include "desktop/cursor.h"

#include "base/checked_arithmetic.h"
#include "desktop/internal/desktop_test_hooks.h"
#include "desktop/internal/cursor_selection.h"
#include "desktop/internal/cursor_state.h"
#include "desktop/internal/window_state.h"
#include "desktop/platform/cursor.h"
#include "desktop/platform/window.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <utility>
#include <vector>

namespace GameWIP::Desktop
{
    /// ------------------------------------------------------------
    /// Cursor validation and resource state
    /// ------------------------------------------------------------

    namespace
    {
        using IO::Types::ErrorCode;

        /// @brief Builds a portable status from a single error code.
        [[nodiscard]] IO::Types::Status error(ErrorCode code) noexcept
        {
            return IO::makeStatus(code);
        }

        /// @brief Validates cursor dimensions, pixel storage, DPI uniqueness, and hotspot bounds.
        [[nodiscard]] IO::Types::Status validateVariants(std::span<const Types::Cursor::ImageView> variants) noexcept
        {
            if(variants.empty())
            {
                return error(ErrorCode::InvalidArgument);
            }

            constexpr auto nativeMaximum = static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max());
            constexpr std::size_t channels = 4;

            for(std::size_t i = 0; i < variants.size(); ++i)
            {
                const auto &variant = variants[i];

                // Validate dimensions, row stride, and complete pixel payload before creating native resources.
                if(variant.size.width == 0 || variant.size.height == 0 || variant.size.width > nativeMaximum || variant.size.height > nativeMaximum ||
                   variant.hotspot.x >= variant.size.width || variant.hotspot.y >= variant.size.height || variant.intendedDpi == 0)
                {
                    return error(ErrorCode::InvalidArgument);
                }
                const std::size_t width = static_cast<std::size_t>(variant.size.width);
                const std::size_t height = static_cast<std::size_t>(variant.size.height);

                if(GameWIP::Base::wouldMultiplyOverflow(width, channels))
                {
                    return error(ErrorCode::InvalidArgument);
                }
                const std::size_t packedRowBytes = width * channels;
                const std::size_t resolvedStride = variant.rowStrideBytes.value_or(packedRowBytes);

                if(resolvedStride < packedRowBytes)
                {
                    return error(ErrorCode::InvalidArgument);
                }

                if(GameWIP::Base::wouldMultiplyOverflow(resolvedStride, height))
                {
                    return error(ErrorCode::InvalidArgument);
                }

                const std::size_t requiredBytes = resolvedStride * height;

                if(variant.rgba8.size() != requiredBytes)
                {
                    return error(ErrorCode::InvalidArgument);
                }
                // Duplicate DPIs would make native variant selection depend on input order.
                for(std::size_t previous = 0; previous < i; ++previous)
                {
                    if(variants[previous].intendedDpi == variant.intendedDpi)
                    {
                        return error(ErrorCode::InvalidArgument);
                    }
                }
            }

            return IO::successStatus();
        }
    } // namespace

    /// ------------------------------------------------------------
    /// Shared cursor lifecycle
    /// ------------------------------------------------------------

    Cursor::Cursor() noexcept = default;
    Cursor::Cursor(const Cursor &) noexcept = default;
    Cursor &Cursor::operator=(const Cursor &) noexcept = default;
    Cursor::Cursor(Cursor &&) noexcept = default;
    Cursor &Cursor::operator=(Cursor &&) noexcept = default;
    Cursor::~Cursor() noexcept = default;

    /// @brief Creates a portable cursor handle that shares immutable native variants.
    Cursor::Cursor(std::shared_ptr<const Internal::CursorState> state) noexcept
        : state_(std::move(state))
    {
    }

    bool Cursor::isValid() const noexcept
    {
        return static_cast<bool>(state_);
    }

    Internal::CursorState::CursorState(std::vector<NativeCursorVariant> nativeVariants) noexcept
        : variants(std::move(nativeVariants))
    {
    }

    Internal::CursorState::~CursorState() noexcept
    {
        Platform::destroyNativeCursorVariants(variants);
    }

    /// @brief Selects the nearest retained native cursor variant for the requested DPI.
    const Internal::NativeCursorVariant &Internal::CursorState::variantForDpi(std::uint32_t dpi) const noexcept
    {
        std::size_t best = 0;

        for(std::size_t index = 1; index < variants.size(); ++index)
        {
            if(isBetterDpiCandidate(variants[index].intendedDpi, variants[best].intendedDpi, dpi))
            {
                best = index;
            }
        }

        return variants[best];
    }

    /// @brief Reconstructs the public Cursor wrapper around validated immutable state.
    Cursor Internal::CursorAccess::make(std::shared_ptr<const CursorState> state) noexcept
    {
        return Cursor(std::move(state));
    }

    /// @brief Exposes the private shared state to the desktop implementation.
    const std::shared_ptr<const Internal::CursorState> &Internal::CursorAccess::state(const Cursor &cursor) noexcept
    {
        return cursor.state_;
    }

    /// ------------------------------------------------------------
    /// Cursor creation and Window binding
    /// ------------------------------------------------------------

    Types::Result<Cursor> createCursor(const Types::Cursor::ImageView &image) noexcept
    {
        return createCursor(std::span{&image, std::size_t{1}});
    }

    Types::Result<Cursor> createCursor(std::span<const Types::Cursor::ImageView> variants) noexcept
    {
        // Validate every image before creating native resources.
        IO::Types::Status status = validateVariants(variants);
        if(!status.ok())
        {
            return {.status = std::move(status)};
        }

        std::vector<Internal::NativeCursorVariant> nativeVariants;

        try
        {
            // Create native variants and retain ownership locally until state creation succeeds.
            if(Internal::consumeFailure(Internal::TestHooks::FailurePoint::Allocation))
            {
                throw std::bad_alloc{};
            }
            nativeVariants.reserve(variants.size());
            status = Platform::createNativeCursorVariants(variants, nativeVariants);

            if(!status.ok())
            {
                Platform::destroyNativeCursorVariants(nativeVariants);
                return {.status = std::move(status)};
            }

            if(Internal::consumeFailure(Internal::TestHooks::FailurePoint::CursorStateAllocation))
            {
                throw std::bad_alloc{};
            }

            // Publish the immutable resource state to the public Cursor wrapper.
            auto state = std::make_shared<Internal::CursorState>(std::move(nativeVariants));
            return {.status = IO::successStatus(), .value = Internal::CursorAccess::make(std::move(state))};
        }
        catch(const std::bad_alloc &)
        {
            Platform::destroyNativeCursorVariants(nativeVariants);
            return {.status = error(ErrorCode::OutOfMemory)};
        }
        catch(...)
        {
            Platform::destroyNativeCursorVariants(nativeVariants);
            return {.status = error(ErrorCode::Unknown)};
        }
    }

    IO::Types::Status setCursor(Window &window, const Cursor &cursor) noexcept
    {
        Internal::WindowState *state = Internal::WindowAccess::state(window);

        if(state == nullptr || !state->platform || !Platform::hasLiveNativeWindow(*state))
        {
            return error(ErrorCode::NotOpen);
        }

        if(!Platform::ownedByCurrentThread(*state))
        {
            return error(ErrorCode::ResourceBusy);
        }

        if(!cursor.isValid())
        {
            return error(ErrorCode::InvalidArgument);
        }
        return Platform::setCustomCursor(*state, Internal::CursorAccess::state(cursor));
    }

    bool hasCustomCursor(const Window &window) noexcept
    {
        const Internal::WindowState *state = Internal::WindowAccess::state(window);
        return state != nullptr && state->platform && Platform::hasLiveNativeWindow(*state) && Platform::ownedByCurrentThread(*state) &&
               Platform::hasCustomCursor(*state);
    }
} // namespace GameWIP::Desktop
