/// @file process_state.cpp
/// @brief Process-global environment and coordination guard implementation for TestSupport.

#include "test_support/process.h"
#include "test_support/internal/process_state.h"
#include "test_support/platform/backend.h"
#include "test_support/internal/status.h"

#include <new>
#include <string_view>
#include <system_error>
#include <utility>

namespace GameWIP::TestSupport::Internal
{
    namespace
    {
        std::recursive_mutex processStateMutexInstance;

        [[nodiscard]] Types::Status restoreEnvironmentVariable(
            std::string_view name,
            const std::optional<std::string> &expectedValue,
            const std::optional<std::string> &previousValue) noexcept
        {
            try
            {
                const Platform::EnvironmentReadResult current = Platform::readEnvironmentVariable(name);

                if(!current.ok())
                {
                    return current.status();
                }

                if(current.value() != expectedValue)
                {
                    return failureStatus(GameWIP::Status::Types::Code::ProcessStateConflict);
                }

                if(previousValue)
                {
                    return Platform::setEnvironmentVariableValue(name, *previousValue).status();
                }
                return Platform::unsetEnvironmentVariableValue(name).status();
            }
            catch(const std::bad_alloc &)
            {
                return failureStatus(GameWIP::Status::Types::Code::OutOfMemory);
            }
            catch(...)
            {
                return failureStatus(GameWIP::Status::Types::Code::EnvironmentFailed);
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
            status_ = Types::Status(GameWIP::Status::Types::Code::OutOfMemory);
        }
        catch(const std::system_error &error)
        {
            status_ = Internal::failureStatus(GameWIP::Status::Types::Code::NativeFailure, Internal::nativeCode(error.code()));
        }
        catch(...)
        {
            status_ = Types::Status(GameWIP::Status::Types::Code::NativeFailure);
        }
    }

    Types::Status ScopedProcessState::status() const noexcept
    {
        return status_;
    }

    Types::Status ScopedProcessState::restorationStatus() const noexcept
    {
        return restorationStatus_;
    }

    void ScopedProcessState::recordRestorationFailure(Types::Status status) noexcept
    {
        if(!status.ok() && restorationStatus_.ok())
        {
            restorationStatus_ = std::move(status);
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

            if(!readResult.ok())
            {
                status_ = readResult.status();
                return;
            }

            previousValue_ = readResult.value();
            expectedValue_ = value.empty() ? std::nullopt : std::optional<std::string>{std::string(value)};
            status_ = Platform::setEnvironmentVariableValue(name_, value).status();
        }
        catch(const std::bad_alloc &)
        {
            status_ = Types::Status(GameWIP::Status::Types::Code::OutOfMemory);
        }
        catch(...)
        {
            status_ = Types::Status(GameWIP::Status::Types::Code::EnvironmentFailed);
        }
    }

    ScopedEnvironmentVariable::~ScopedEnvironmentVariable() noexcept
    {
        static_cast<void>(restore());
    }

    Types::Status ScopedEnvironmentVariable::status() const noexcept
    {
        return status_;
    }

    Types::Result<void> ScopedEnvironmentVariable::restore() noexcept
    {
        if(!status_.ok())
        {
            return Types::Result<void>::failure(status_);
        }

        if(restored_)
        {
            return restorationStatus_.ok() ? Types::Result<void>::success() : Types::Result<void>::failure(restorationStatus_);
        }

        restorationStatus_ = Internal::restoreEnvironmentVariable(name_, expectedValue_, previousValue_);
        restored_ = restorationStatus_.ok();

        if(!restorationStatus_.ok())
        {
            processState_->recordRestorationFailure(restorationStatus_);
        }

        return restorationStatus_.ok() ? Types::Result<void>::success() : Types::Result<void>::failure(restorationStatus_);
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

            if(!readResult.ok())
            {
                status_ = readResult.status();
                return;
            }

            previousValue_ = readResult.value();
            expectedValue_ = std::nullopt;
            status_ = Platform::unsetEnvironmentVariableValue(name_).status();
        }
        catch(const std::bad_alloc &)
        {
            status_ = Types::Status(GameWIP::Status::Types::Code::OutOfMemory);
        }
        catch(...)
        {
            status_ = Types::Status(GameWIP::Status::Types::Code::EnvironmentFailed);
        }
    }

    ScopedUnsetEnvironmentVariable::~ScopedUnsetEnvironmentVariable() noexcept
    {
        static_cast<void>(restore());
    }

    Types::Status ScopedUnsetEnvironmentVariable::status() const noexcept
    {
        return status_;
    }

    Types::Result<void> ScopedUnsetEnvironmentVariable::restore() noexcept
    {
        if(!status_.ok())
        {
            return Types::Result<void>::failure(status_);
        }

        if(restored_)
        {
            return restorationStatus_.ok() ? Types::Result<void>::success() : Types::Result<void>::failure(restorationStatus_);
        }

        restorationStatus_ = Internal::restoreEnvironmentVariable(name_, expectedValue_, previousValue_);
        restored_ = restorationStatus_.ok();

        if(!restorationStatus_.ok())
        {
            processState_->recordRestorationFailure(restorationStatus_);
        }

        return restorationStatus_.ok() ? Types::Result<void>::success() : Types::Result<void>::failure(restorationStatus_);
    }
} // namespace GameWIP::TestSupport
