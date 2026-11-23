/// @file runtime_input.h
/// @brief Win32 attachment owned by one runtime Input session.

#pragma once

#include "desktop/window.h"
#include "input/frame.h"
#include "input/native/desktop_win32.h"
#include "input/system.h"
#include "io/status.h"
#include "runtime/diagnostics/runtime_diagnostics.h"
#include "runtime/options/runtime_options.h"

namespace GameWIP::Game::Runtime
{
    namespace Desktop = GameWIP::Desktop;
    namespace IO = GameWIP::IO;
    namespace Input = GameWIP::Input;
    namespace RuntimeDiagnostics = GameWIP::Game::RuntimeDiagnostics;

    /// ------------------------------------------------------------
    /// Runtime Input session
    /// ------------------------------------------------------------

    struct RuntimeInput
    {
        // Input session
        Input::System system;
        Input::Types::Frame frame;

        // Win32 attachment
        Input::Platform::Win32::Attachment attachment;

        // Backend selection
        void applyBackendSelection(const Options &options, Input::Types::Configuration &configuration) const;

        // Window attachment and backend reporting
        [[nodiscard]] constexpr bool usesWindowAttachment() const noexcept
        {
            return true;
        }
        [[nodiscard]] IO::Types::Status attachToWindow(Desktop::Window &window) noexcept;
        void logBackends();

        // Session cleanup
        [[nodiscard]] bool finish();
    };
} // namespace GameWIP::Game::Runtime
