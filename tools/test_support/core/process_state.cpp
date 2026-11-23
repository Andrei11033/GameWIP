/// @file process_state.cpp
/// @brief Process-global environment and coordination guard implementation for TestSupport.

#include "test_support/process.h"
#include "test_support/internal/process_state.h"
#include "test_support/platform/backend.h"
#include "test_support/internal/status.h"

#include <new>
#include <string_view>
#include <system_error>

namespace GameWIP::TestSupport::Internal
{
    namespace
    {
        std::recursive_mutex processStateMutexInstance;

        [[nodiscard]] Types::InfrastructureStatus restoreEnvironmentVariable(
            std::string_view name,
            const std::optional<std::string> &expectedValue,
            const std::optional<std::string> &previousValue) noexcept
        {
            try
            {
                const Platform::EnvironmentReadResult current = Platform::readEnvironmentVariable(name);

                if(!current.status.ok())
                {
                    return current.status;
                }

                if(current.value != expectedValue)
                {
                    return failureStatus(Types::InfrastructureError::ProcessStateConflict);
                }

                if(previousValue)
                {
                    return Platform::setEnvironmentVariableValue(name, *previousValue);
                }
                return Platform::unsetEnvironmentVariableValue(name);
            }
            catch(const std::bad_alloc &)
            {
                return failureStatus(Types::InfrastructureError::OutOfMemory);
            }
            catch(...)
            {
                return failureStatus(Types::InfrastructureError::EnvironmentFailed);
            }
        }
    } // namespace

    std::recursive_mutex &processStateMutex() noexcept
    {
        return processStateMutexInstance;
    }
} // namespace GameWIP::TestSupport::Internal

namespace GameWIP::TestSupport
{
    ScopedProcessState::ScopedProcessState() noexcept
    {
        try
        {
            lock_ = std::unique_lock<std::recursive_mutex>(Internal::processStateMutex());
        }
        catch(const std::bad_alloc &)
        {
            status_ = {.error = Types::InfrastructureError::OutOfMemory};
        }
        catch(const std::system_error &error)
        {
            status_ = {
                .error = Types::InfrastructureError::PlatformFailure,
                .nativeCode = static_cast<std::uint64_t>(static_cast<std::uint32_t>(error.code().value()))};
        }
        catch(...)
        {
            status_ = {.error = Types::InfrastructureError::PlatformFailure};
        }
    }

    Types::InfrastructureStatus ScopedProcessState::status() const noexcept
    {
        return status_;
    }

    Types::InfrastructureStatus ScopedProcessState::restorationStatus() const noexcept
    {
        return restorationStatus_;
    }

    void ScopedProcessState::recordRestorationFailure(Types::InfrastructureStatus status) noexcept
    {
        if(!status.ok() && restorationStatus_.ok())
        {
            restorationStatus_ = status;
        }
    }

    /// ------------------------------------------------------------
    /// Scoped set/restore environment variable
    /// ------------------------------------------------------------

    ScopedEnvironmentVariable::ScopedEnvironmentVariable(ScopedProcessState &processState, std::string_view name, std::string_view value) noexcept
        : processState_(&processState)
    {
        try
        {
            if(!processState.status().ok())
            {
                status_ = processState.status();
                return;
            }
            name_.assign(name);
            const Platform::EnvironmentReadResult readResult = Platform::readEnvironmentVariable(name_);

            if(!readResult.status.ok())
            {
                status_ = readResult.status;
                return;
            }
            previousValue_ = readResult.value;
            expectedValue_ = value.empty() ? std::nullopt : std::optional<std::string>{std::string(value)};
            status_ = Platform::setEnvironmentVariableValue(name_, value);
        }
        catch(const std::bad_alloc &)
        {
            status_ = {.error = Types::InfrastructureError::OutOfMemory};
        }
        catch(...)
        {
            status_ = {.error = Types::InfrastructureError::EnvironmentFailed};
        }
    }

    ScopedEnvironmentVariable::~ScopedEnvironmentVariable() noexcept
    {
        static_cast<void>(restore());
    }

    Types::InfrastructureStatus ScopedEnvironmentVariable::status() const noexcept
    {
        return status_;
    }

    Types::InfrastructureStatus ScopedEnvironmentVariable::restore() noexcept
    {
        if(!status_.ok())
        {
            return status_;
        }

        if(restored_)
        {
            return restorationStatus_;
        }

        restorationStatus_ = Internal::restoreEnvironmentVariable(name_, expectedValue_, previousValue_);
        restored_ = restorationStatus_.ok();

        if(!restorationStatus_.ok())
        {
            processState_->recordRestorationFailure(restorationStatus_);
        }

        return restorationStatus_;
    }

    /// ------------------------------------------------------------
    /// Scoped unset/restore environment variable
    /// ------------------------------------------------------------

    ScopedUnsetEnvironmentVariable::ScopedUnsetEnvironmentVariable(ScopedProcessState &processState, std::string_view name) noexcept
        : processState_(&processState)
    {
        try
        {
            if(!processState.status().ok())
            {
                status_ = processState.status();
                return;
            }
            name_.assign(name);
            const Platform::EnvironmentReadResult readResult = Platform::readEnvironmentVariable(name_);

            if(!readResult.status.ok())
            {
                status_ = readResult.status;
                return;
            }
            previousValue_ = readResult.value;
            expectedValue_ = std::nullopt;
            status_ = Platform::unsetEnvironmentVariableValue(name_);
        }
        catch(const std::bad_alloc &)
        {
            status_ = {.error = Types::InfrastructureError::OutOfMemory};
        }
        catch(...)
        {
            status_ = {.error = Types::InfrastructureError::EnvironmentFailed};
        }
    }

    ScopedUnsetEnvironmentVariable::~ScopedUnsetEnvironmentVariable() noexcept
    {
        static_cast<void>(restore());
    }

    Types::InfrastructureStatus ScopedUnsetEnvironmentVariable::status() const noexcept
    {
        return status_;
    }

    Types::InfrastructureStatus ScopedUnsetEnvironmentVariable::restore() noexcept
    {
        if(!status_.ok())
        {
            return status_;
        }

        if(restored_)
        {
            return restorationStatus_;
        }

        restorationStatus_ = Internal::restoreEnvironmentVariable(name_, expectedValue_, previousValue_);
        restored_ = restorationStatus_.ok();

        if(!restorationStatus_.ok())
        {
            processState_->recordRestorationFailure(restorationStatus_);
        }

        return restorationStatus_;
    }
} // namespace GameWIP::TestSupport
