/// @file win32.h
/// @brief Explicit Win32 native-handle adapter for GameWIP Window.

#pragma once

#include "io/io.h"
#include "window/window_export.h"

#include <windows.h>

namespace GameWIP::Window
{
    class ChildSurface;
    class Window;
} // namespace GameWIP::Window

/// @brief Deliberate Win32 interoperability for the portable Window owner.
namespace GameWIP::Window::Native::Win32
{
    /// @brief Non-owning native handle pair.
    /// @warning Valid only until the Window closes or reopens. Consumers must not destroy either
    /// handle or mutate native ownership.
    struct HandleView
    {
        HINSTANCE instance = nullptr; ///< Module instance used for the native window.
        HWND window = nullptr;        ///< Owned native top-level window handle.
    };

    /// @brief Result of a native handle query.
    struct HandleResult
    {
        IO::Types::Status status; ///< Success, NotOpen, or ResourceBusy.
        HandleView handle;        ///< Non-owning handles on success.
    };

    /// @brief Returns non-owning Win32 handles for an open Window on its owner thread.
    /// @param window Window whose native handles are requested.
    /// @return Query status and non-owning handles on success.
    /// @warning Native use must not race Window close.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT HandleResult getHandle(const GameWIP::Window::Window &window) noexcept;

    /// @brief Returns the borrowed Win32 host handle for an open ChildSurface on its owner thread.
    /// @param surface ChildSurface whose native host handle is requested.
    /// @return Query status and non-owning handles on success.
    /// @warning Native use must not race ChildSurface close. Consumers must not destroy, reparent,
    /// subclass, or overwrite GameWIP-owned state on the returned host HWND.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT HandleResult getHandle(const GameWIP::Window::ChildSurface &surface) noexcept;
} // namespace GameWIP::Window::Native::Win32
