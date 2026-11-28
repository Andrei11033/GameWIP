/// @file handles.cpp
/// @brief Shared FileSystem handle-state operations.

#include "filesystem/internal/helpers/handles.h"

namespace GameWIP::FileSystem::Internal
{
    bool stateIsOpen(const std::unique_ptr<FileState> &state) noexcept
    {
        return state != nullptr && state->nativeHandle != nullptr;
    }

    GameWIP::Status::Types::Result<void> closeFileState(std::unique_ptr<FileState> &state) noexcept
    {
        if(!stateIsOpen(state))
        {
            state.reset();
            return GameWIP::Status::Types::Result<void>::success();
        }

        auto result = Platform::closeFile(*state);

        if(result.ok())
        {
            state.reset();
        }

        return result;
    }
} // namespace GameWIP::FileSystem::Internal
