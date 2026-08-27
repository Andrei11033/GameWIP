/// @file window_test_hooks.h
/// @brief Source-tree-only deterministic hooks for Window validation.
/// @warning This header is not installed and must not be used by production consumers.

#pragma once

#include "window/cursor.h"
#include "window/description.h"
#include "window/display_info.h"
#include "window/events.h"
#include "window/renderer_bridge.h"

#include <array>

#ifndef WINDOW_INTERNAL_TEST_HOOKS
#define WINDOW_INTERNAL_TEST_HOOKS 0
#endif

namespace GameWIP::Window::TestHooks
{
    struct DpiTransitionResult
    {
        Types::LogicalSize logicalSize;
        Types::PixelSize framebufferSize;
    };

    struct DisplayColorSnapshot
    {
        Types::Display::ColorSpace activeColorSpace = Types::Display::ColorSpace::Unknown;
        bool wideColorGamutSupported = false;
        bool hdrSupported = false;
        bool hdrEnabled = false;
        std::uint32_t bitsPerColorChannel = 0;
        float minimumLuminanceNits = 0.0F;
        float maximumLuminanceNits = 0.0F;
        float maximumFullFrameLuminanceNits = 0.0F;
        std::uint32_t sdrWhiteLevelMilli80Nits = 0;
    };

    struct CustomCursorNativeSnapshot
    {
        Types::Cursor::PixelPosition hotspot;
        std::array<std::byte, 4> firstBgraPixel{};
        bool valid = false;
    };

    enum class FailurePoint
    {
        None,
        Allocation,
        Dispatcher,
        NativeCreation,
        PartialOpen,
        TitleConversion,
        RegionCopy,
        IconConversion,
        Cursor,
        SystemCursorLoad,
        CursorBinding,
        CursorStateAllocation,
        MonitorQuery,
        DisplayEnumeration,
        DisplayColorQuery,
        FullscreenPartial,
        DisplayRestoration,
        Close,
        EventPump
    };

#if WINDOW_INTERNAL_TEST_HOOKS
    GAMEWIP_WINDOW_EXPORT void failNext(FailurePoint point) noexcept;
    GAMEWIP_WINDOW_EXPORT void resetFailures() noexcept;
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::Events::PumpResult pumpReentrantly() noexcept;
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT IO::Types::Status openPortable(Window &window, std::span<Types::Event> storage) noexcept;
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT IO::Types::Status enqueue(Window &window, Types::Events::Payload data) noexcept;
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT IO::Types::Status requestClose(Window &window, Types::Events::CloseRequestSource source) noexcept;
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT IO::Types::Status destroyNativeWindow(Window &window) noexcept;
    GAMEWIP_WINDOW_EXPORT void enablePointerHitMaskBridge(Window &window) noexcept;
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT bool hasRendererIntegrationState(const Window &window) noexcept;
    GAMEWIP_WINDOW_EXPORT void setPointerHitMaskGeneration(Window &window, std::uint64_t generation) noexcept;
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT bool pointerHitMaskAccepts(const Window &window, Types::LogicalPosition position) noexcept;
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT IO::Types::Status simulateFullscreenMonitorRemoval(Window &window) noexcept;
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT std::uint64_t pointerHitMaskGeneration(const Window &window) noexcept;
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT std::size_t pointerHitMaskWordCount(const Window &window) noexcept;
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::Renderer::PointerHitMaskWord pointerHitMaskWord(const Window &window, std::size_t index) noexcept;
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT const void *pointerHitMaskStorage(const Window &window) noexcept;
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT std::uint32_t refreshRateMillihertz(std::uint32_t numerator, std::uint32_t denominator) noexcept;
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::Display::ColorInfo makeDisplayColorInfo(
        Types::Display::MonitorId monitor,
        const DisplayColorSnapshot &snapshot) noexcept;
    GAMEWIP_WINDOW_EXPORT void simulateDisplayColorConfigurationChange() noexcept;
    GAMEWIP_WINDOW_EXPORT void makeNextDisplayColorMetadataUnavailable() noexcept;
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT bool exactNativeDisplayModeMatches(
        const Types::Display::Mode &requested,
        std::uint32_t width,
        std::uint32_t height,
        std::uint32_t frequencyHertz,
        std::uint16_t bitsPerPixel,
        bool interlaced) noexcept;
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT DpiTransitionResult calculateDpiTransition(
        Types::LogicalSize logicalSize,
        Types::PixelSize framebufferSize,
        std::uint32_t newDpi,
        Types::DpiResizePolicy policy) noexcept;
    GAMEWIP_WINDOW_EXPORT void failCursorNativeCreationAfter(std::size_t successfulVariants) noexcept;
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT std::size_t customCursorVariantCount(const Cursor &cursor) noexcept;
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT std::uint32_t customCursorBindingDpi(const Window &window) noexcept;
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT std::size_t createdCustomCursorCount() noexcept;
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT std::size_t destroyedCustomCursorCount() noexcept;
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT CustomCursorNativeSnapshot inspectCustomCursorVariant(const Cursor &cursor, std::size_t index) noexcept;
#endif
} // namespace GameWIP::Window::TestHooks

namespace GameWIP::Window::Detail
{
#if WINDOW_INTERNAL_TEST_HOOKS
    [[nodiscard]] bool consumeFailure(TestHooks::FailurePoint point) noexcept;
    [[nodiscard]] bool consumeCursorNativeCreationFailure() noexcept;
    void recordCustomCursorCreated() noexcept;
    void recordCustomCursorDestroyed() noexcept;
#else
    [[nodiscard]] constexpr bool consumeFailure(TestHooks::FailurePoint) noexcept
    {
        return false;
    }
    [[nodiscard]] constexpr bool consumeCursorNativeCreationFailure() noexcept
    {
        return false;
    }
    constexpr void recordCustomCursorCreated() noexcept {}
    constexpr void recordCustomCursorDestroyed() noexcept {}
#endif
} // namespace GameWIP::Window::Detail
