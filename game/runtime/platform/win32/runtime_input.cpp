/// @file runtime_input.cpp
/// @brief Connects the runtime Input session to Win32 services.

#include "runtime/platform/win32/runtime_input.h"

#include "runtime/diagnostics/runtime_diagnostics.h"
#include "runtime/platform/win32/input_backends.h"

#include <cstddef>
#include <utility>
#include <vector>

namespace GameWIP::Game::Runtime
{
    namespace
    {
        /// @brief Detaches the runtime's Win32 source before the generic session closes.
        [[nodiscard]] IO::Types::Status detachInput(void *context) noexcept
        {
            auto *attachment = static_cast<Input::Platform::Win32::Attachment *>(context);
            if(attachment == nullptr || !attachment->isAttached())
            {
                return IO::successStatus();
            }

            return attachment->detach();
        }
    } // namespace

    void RuntimeInput::applyBackendSelection(const Options &options, Input::Types::Configuration &configuration) const
    {
        if(!options.inputBackendsConfigured)
        {
            return;
        }

        std::vector<Input::Types::BackendKind> enabledBackends;
        enabledBackends.reserve(options.inputBackendCount);
        for(std::size_t index = 0; index < options.inputBackendCount; ++index)
        {
            enabledBackends.push_back(options.inputBackends[index]);
        }
        configuration.enabledBackends = std::move(enabledBackends);
    }

    IO::Types::Status RuntimeInput::attachToWindow(Desktop::Window &window) noexcept
    {
        return Input::Platform::Win32::attachToWindow(system, attachment, window);
    }

    void RuntimeInput::logBackends()
    {
        RuntimeDiagnostics::Win32::logInputBackends(attachment);
    }

    bool RuntimeInput::finish()
    {
        return RuntimeDiagnostics::finishInputSystemWithDetach(system, &detachInput, &attachment);
    }
} // namespace GameWIP::Game::Runtime
