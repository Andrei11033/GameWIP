/// @file renderer_feedback.cpp
/// @brief Owner-thread renderer feedback state transitions.

#include "window/integration/renderer_feedback.h"

#include "window/internal/window_state.h"

namespace GameWIP::Window::Integration::Renderer
{
    namespace
    {
        [[nodiscard]] IO::Types::Status requireOwner(Window &window, Detail::WindowState *&state) noexcept
        {
            state = Detail::WindowAccess::state(window);
            if (state == nullptr || !window.isOpen())
                return IO::makeStatus(IO::Types::ErrorCode::NotOpen);
            if (!window.isOwnedByCurrentThread())
                return IO::makeStatus(IO::Types::ErrorCode::ResourceBusy);
            return IO::successStatus();
        }
    } // namespace

    IO::Types::Status attachOcclusionProvider(Window &window) noexcept
    {
        Detail::WindowState *state = nullptr;
        IO::Types::Status status = requireOwner(window, state);
        if (!status.ok())
            return status;
        if (state->occlusionProviderAttached)
            return IO::makeStatus(IO::Types::ErrorCode::AlreadyOpen);
        state->occlusionProviderAttached = true;
        state->occluded = false;
        return IO::successStatus();
    }

    IO::Types::Status reportOcclusion(Window &window, bool occluded) noexcept
    {
        Detail::WindowState *state = nullptr;
        IO::Types::Status status = requireOwner(window, state);
        if (!status.ok())
            return status;
        if (!state->occlusionProviderAttached)
            return IO::makeStatus(IO::Types::ErrorCode::NotOpen);
        if (state->occluded == occluded)
            return IO::successStatus();

        state->occluded = occluded;
        static_cast<void>(Detail::enqueueEvent(*state, Types::OcclusionChangedEvent{occluded}));
        return IO::successStatus();
    }

    IO::Types::Status detachOcclusionProvider(Window &window) noexcept
    {
        Detail::WindowState *state = nullptr;
        IO::Types::Status status = requireOwner(window, state);
        if (!status.ok())
            return status;
        if (!state->occlusionProviderAttached)
            return IO::successStatus();

        state->occlusionProviderAttached = false;
        if (state->occluded)
        {
            state->occluded = false;
            static_cast<void>(Detail::enqueueEvent(*state, Types::OcclusionChangedEvent{false}));
        }
        return IO::successStatus();
    }
} // namespace GameWIP::Window::Integration::Renderer
