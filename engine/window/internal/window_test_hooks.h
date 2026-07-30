/// @file window_test_hooks.h
/// @brief Source-tree-only deterministic hooks for Window validation.
/// @warning This header is not installed and must not be used by production consumers.

#pragma once

#include "window/window.h"

#ifndef INTERNAL_WINDOW_TEST_HOOKS
/// @brief Build-interface switch enabling source-tree Window validation hooks.
#define INTERNAL_WINDOW_TEST_HOOKS 0
#endif

namespace GameWIP::Window::TestHooks
{
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
