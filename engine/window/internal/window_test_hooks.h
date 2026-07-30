/// @file window_test_hooks.h
/// @brief Source-tree-only deterministic hooks for Window validation.
/// @warning This header is not installed and must not be used by production consumers.

#pragma once

#include "window/renderer.h"

#ifndef INTERNAL_WINDOW_TEST_HOOKS
/// @brief Build-interface switch enabling source-tree Window validation hooks.
#define INTERNAL_WINDOW_TEST_HOOKS 0
#endif

namespace GameWIP::Window::TestHooks
{
    /// @brief Deterministic result of applying one DPI resize policy.
    struct DpiTransitionResult
    {
        Types::LogicalSize logicalSize;   ///< Resulting logical client extent.
        Types::PixelSize framebufferSize; ///< Resulting physical framebuffer extent.
    };

    /// @brief Backend-neutral display facts used for deterministic conversion validation.
    struct DisplayColorSnapshot
    {
        Types::DisplayColorSpace activeColorSpace = Types::DisplayColorSpace::Unknown; ///< Injected active mode.
        bool wideColorGamutSupported = false;                                          ///< Injected WCG support.
        bool hdrSupported = false;                                                     ///< Injected HDR support.
        bool hdrEnabled = false;                                                       ///< Injected HDR enablement.
        std::uint32_t bitsPerColorChannel = 0;                                         ///< Injected unsaturated precision.
        float minimumLuminanceNits = 0.0F;                                             ///< Injected minimum luminance.
        float maximumLuminanceNits = 0.0F;                                             ///< Injected peak luminance.
        float maximumFullFrameLuminanceNits = 0.0F;                                    ///< Injected full-frame luminance.
        std::uint32_t sdrWhiteLevelMilli80Nits = 0;                                    ///< Injected native SDR-white units.
    };

    /// @brief One-shot production boundary that can be failed by validation builds.
    enum class FailurePoint
    {
        None,               ///< No armed failure.
        Allocation,         ///< Portable state or event-storage allocation.
        Dispatcher,         ///< Owning-thread dispatcher registration.
        NativeCreation,     ///< Native top-level window creation.
        PartialOpen,        ///< Open after native creation and identity registration.
        TitleConversion,    ///< UTF-8 title conversion.
        RegionCopy,         ///< Custom-chrome or pointer-region ownership copy.
        IconConversion,     ///< RGBA icon conversion and native icon creation.
        Cursor,             ///< Native cursor application.
        MonitorQuery,       ///< Native monitor metadata query.
        DisplayEnumeration, ///< Monitor or display-mode enumeration.
        DisplayColorQuery,  ///< Native display-color metadata query.
        FullscreenPartial,  ///< Fullscreen transaction after style mutation.
        DisplayRestoration, ///< Fullscreen display restoration.
        Close,              ///< Native close before ownership release.
        EventPump           ///< Owning-thread native event pump.
    };

#if INTERNAL_WINDOW_TEST_HOOKS
    /// @brief Arms one failure point on the calling thread.
    /// @details The point is consumed by the next matching production operation. Arming another
    /// point replaces the previous one.
    GAMEWIP_WINDOW_EXPORT void failNext(FailurePoint point) noexcept;

    /// @brief Clears any unconsumed failure point on the calling thread.
    GAMEWIP_WINDOW_EXPORT void resetFailures() noexcept;

    /// @brief Calls the production pump while its owning-thread reentrancy guard is active.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::EventPumpResult pumpReentrantly() noexcept;

    /// @brief Creates a backend-free queue fixture using caller-owned fixed event storage.
    /// @details The storage remains exclusively borrowed until close() or destruction. Native-only
    /// operations still report NotOpen. This hook exists only for portable queue validation.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT IO::Types::Status openPortable(Window &window, std::span<Types::Event> storage) noexcept;

    /// @brief Routes one typed event through the production fixed-capacity queue algorithm.
    /// @warning The event's state payload is not applied to cached Window properties.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT IO::Types::Status enqueue(Window &window, Types::EventData data) noexcept;

    /// @brief Applies the production sticky close-request path to a portable hook state.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT IO::Types::Status requestClose(Window &window, Types::CloseRequestSource source) noexcept;

    /// @brief Destroys the live HWND without entering the explicit-close path.
    /// @details Exercises unexpected native destruction and pending finalization.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT IO::Types::Status destroyNativeWindow(Window &window) noexcept;
    /// @brief Enables the backend-independent HitMask bridge without advertising native support.
    GAMEWIP_WINDOW_EXPORT void enablePointerHitMaskBridge(Window &window) noexcept;
    /// @brief Sets the persistent generation for deterministic overflow validation.
    GAMEWIP_WINDOW_EXPORT void setPointerHitMaskGeneration(Window &window, std::uint64_t generation) noexcept;
    /// @brief Samples the production packed-mask helper in logical client coordinates.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT bool pointerHitMaskAccepts(const Window &window, Types::LogicalPosition position) noexcept;

    /// @brief Exercises fullscreen-target removal recovery without changing display topology.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT IO::Types::Status simulateFullscreenMonitorRemoval(Window &window) noexcept;

    /// @brief Returns the active packed pointer-mask generation.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT std::uint64_t pointerHitMaskGeneration(const Window &window) noexcept;
    /// @brief Returns the active packed pointer-mask word count.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT std::size_t pointerHitMaskWordCount(const Window &window) noexcept;
    /// @brief Returns one active packed pointer-mask word, or zero outside the active range.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT std::uint64_t pointerHitMaskWord(const Window &window, std::size_t index) noexcept;
    /// @brief Returns the active vector storage address for reuse validation.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT const void *pointerHitMaskStorage(const Window &window) noexcept;

    /// @brief Applies the production rational-refresh conversion.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT std::uint32_t refreshRateMillihertz(std::uint32_t numerator, std::uint32_t denominator) noexcept;

    /// @brief Converts backend-neutral display facts through the production result policy.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::DisplayColorInfo makeDisplayColorInfo(
        Types::MonitorId monitor,
        const DisplayColorSnapshot &snapshot) noexcept;

    /// @brief Makes the next owner-thread pump observe a native display-color transition.
    GAMEWIP_WINDOW_EXPORT void simulateDisplayColorConfigurationChange() noexcept;

    /// @brief Makes the next valid query succeed with unavailable optional color metadata.
    GAMEWIP_WINDOW_EXPORT void makeNextDisplayColorMetadataUnavailable() noexcept;

    /// @brief Applies the exact native exclusive-mode comparator without switching displays.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT bool exactNativeDisplayModeMatches(
        const Types::DisplayMode &requested,
        std::uint32_t width,
        std::uint32_t height,
        std::uint32_t frequencyHertz,
        std::uint16_t bitsPerPixel,
        bool interlaced) noexcept;

    /// @brief Applies the production DPI resize-policy size calculation.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT DpiTransitionResult calculateDpiTransition(
        Types::LogicalSize logicalSize,
        Types::PixelSize framebufferSize,
        std::uint32_t newDpi,
        Types::DpiResizePolicy policy) noexcept;
#endif
} // namespace GameWIP::Window::TestHooks

namespace GameWIP::Window::Detail
{
    /// @brief Consumes a matching one-shot validation failure on the calling thread.
#if INTERNAL_WINDOW_TEST_HOOKS
    [[nodiscard]] bool consumeFailure(TestHooks::FailurePoint point) noexcept;
#else
    [[nodiscard]] constexpr bool consumeFailure(TestHooks::FailurePoint) noexcept
    {
        return false;
    }
#endif
} // namespace GameWIP::Window::Detail
