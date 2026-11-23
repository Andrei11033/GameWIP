/// @file handles.cpp
/// @brief Shared FileSystem handle-state operations.

#include "filesystem/internal/helpers/handles.h"

namespace GameWIP::FileSystem::Internal
{
    bool stateIsOpen(const std::unique_ptr<FileState> &state) noexcept
    {
        return state != nullptr && state->nativeHandle != nullptr;
    }

    IO::Types::Status closeFileState(std::unique_ptr<FileState> &state) noexcept
    {
        if(!stateIsOpen(state))
        {
            state.reset();
            return IO::successStatus();
        }

        IO::Types::Status status = Platform::closeFile(*state);

        if(status.ok())
        {
            state.reset();
        }

        return status;
    }
} // namespace GameWIP::FileSystem::Internal
