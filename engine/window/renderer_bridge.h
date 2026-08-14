/// @file renderer_bridge.h
/// @brief Optional display-color queries and the renderer-to-Window bridge.

#pragma once

#include "window/window.h"

namespace GameWIP::Window::Types
{
    /// @brief Current operating-system display-color classification.
    enum class DisplayColorSpace
    {
        Unknown,        ///< The active display-color mode could not be classified.
        Srgb,           ///< Standard dynamic range using the sRGB/BT.709 color space.
        WideColorGamut, ///< Advanced-color SDR with a wider-than-sRGB gamut.
        Hdr10Pq         ///< HDR output using BT.2100/ST.2084 (PQ).
    };

    /// @brief Current native color capabilities and state for one monitor.
    struct DisplayColorInfo
    {
        MonitorId monitor;                                               ///< Queried process-local monitor identity.
        DisplayColorSpace activeColorSpace = DisplayColorSpace::Unknown; ///< Current operating-system display mode.
        bool wideColorGamutSupported = false;                            ///< Whether advanced wider-gamut output is supported.
        bool hdrSupported = false;                                       ///< Whether HDR output is supported.
        bool hdrEnabled = false;                                         ///< Whether HDR is currently enabled.
        std::uint16_t bitsPerColorChannel = 0;                           ///< Active wire precision, or zero when unavailable.
        float minimumLuminanceNits = 0.0F;                               ///< Minimum panel luminance, or zero when unavailable.
        float maximumLuminanceNits = 0.0F;                               ///< Peak panel luminance, or zero when unavailable.
        float maximumFullFrameLuminanceNits = 0.0F;                      ///< Full-frame panel luminance, or zero when unavailable.
        float sdrWhiteLevelNits = 0.0F;                                  ///< Current SDR white level, or zero when unavailable.
    };

    /// @brief Status and current display-color information.
    struct DisplayColorInfoResult
    {
        IO::Types::Status status; ///< Query status.
        DisplayColorInfo info;    ///< Current display-color facts on success.
    };
} // namespace GameWIP::Window::Types

/// @brief Window-side bridge for renderer-owned presentation state and integration data.
namespace GameWIP::Window::Renderer
{
    /// @brief Word used by the canonical dense pointer-hit-mask representation.
    using PointerHitMaskWord = std::uint32_t;
    /// @brief Window-owned target for one asynchronous packed-mask update.
    struct PointerHitMaskTarget
    {
        std::uint64_t generation = 0;      ///< Nonzero generation created by Window.
        Types::PixelSize framebufferSize;  ///< Coherent physical framebuffer snapshot.
        std::size_t requiredWordCount = 0; ///< Exact number of packed PointerHitMaskWord values.
    };

    /// @brief Status and target returned when beginning a packed-mask update.
    struct PointerHitMaskTargetResult
    {
        IO::Types::Status status;    ///< Begin-update status.
        PointerHitMaskTarget target; ///< New publication target on success.
    };

    /// @brief Attaches the sole authoritative occlusion provider.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT IO::Types::Status attachOcclusionProvider(Window &window) noexcept;
    /// @brief Publishes an owner-thread occlusion transition.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT IO::Types::Status reportOcclusion(Window &window, bool occluded) noexcept;
    /// @brief Detaches the occlusion provider and resets occlusion to false.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT IO::Types::Status detachOcclusionProvider(Window &window) noexcept;

    /// @brief Returns ceil(width / 32) * height, or zero for an empty/overflowing extent.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT std::size_t requiredPointerHitMaskWords(Types::PixelSize framebufferSize) noexcept;

    /// @brief Begins the newest packed physical-framebuffer pointer-mask update.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT PointerHitMaskTargetResult beginPointerHitMaskUpdate(Window &window) noexcept;

    /// @brief Publishes a completed packed physical-framebuffer pointer mask.
    /// @details Rows are independently packed left to right with one physical-framebuffer pixel
    /// per bit, least-significant bit first within each PointerHitMaskWord. Zero passes input through
    /// and one accepts input. Unused bits in the final word of every row must be zero. Publication is
    /// owner-thread only. Only the newest Window-created generation is accepted; stale work returns
    /// Interrupted. Same-size updates reuse active storage, and the previous mask remains active on
    /// every failure.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT IO::Types::Status publishPointerHitMask(
        Window &window,
        std::uint64_t generation,
        std::span<const PointerHitMaskWord> words) noexcept;

    /// @brief Clears the persistent active mask on the owner thread.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT IO::Types::Status clearPointerHitMask(Window &window) noexcept;

    /// @brief Reports whether the current native lifetime has a valid active mask.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT bool hasPointerHitMask(const Window &window) noexcept;

    /// @brief Queries current operating-system color facts for one connected monitor.
    /// @details This does not inspect or configure a renderer swapchain. Optional numeric
    /// metadata remains zero when the operating system cannot report it reliably.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::DisplayColorInfoResult getDisplayColorInfo(Types::MonitorId monitor) noexcept;

    /// @brief Queries color facts for the monitor currently associated with an open Window.
    /// @details This owner-thread operation reads the cached current monitor and then performs
    /// the same native query as getDisplayColorInfo().
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::DisplayColorInfoResult getWindowDisplayColorInfo(const Window &window) noexcept;
} // namespace GameWIP::Window::Renderer
