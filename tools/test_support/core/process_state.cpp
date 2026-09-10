/// @file process_state.cpp
/// @brief Process-global environment guard implementation for TestSupport.

#include "test_support/process.h"
#include "test_support/internal/test_support_platform.h"

#include <mutex>
#include <new>
#include <utility>

namespace GameWIP::TestSupport
{
    namespace
    {
        std::mutex environmentMutex;

        [[nodiscard]] Types::InfrastructureStatus failureStatus(Types::InfrastructureError error, std::uint64_t code = 0) noexcept
        {
            return Types::InfrastructureStatus{.error = error, .nativeCode = code};
        }

        void restoreEnvironmentVariable(std::string_view name, const std::optional<std::string> &previousValue) noexcept
        {
            try
            {
                std::lock_guard lock(environmentMutex);
                if (previousValue)
                {
                    static_cast<void>(Detail::Platform::setEnvironmentVariableValue(name, *previousValue));
                }
                else
                {
                    static_cast<void>(Detail::Platform::unsetEnvironmentVariableValue(name));
                }
            }
            catch (...) // NOLINT(bugprone-empty-catch) -- Best-effort restoration cannot propagate from RAII destruction.
            {
            }
        }
    } // namespace

    // ------------------------------------------------------------
    // Scoped set/restore environment variable
    // ------------------------------------------------------------

    ScopedEnvironmentVariable::ScopedEnvironmentVariable(std::string_view name, std::string_view value) noexcept
    {
        try
        {
            name_.assign(name);

            // Environment variables are process-global, so guards serialize read/modify/write
            // pairs to avoid restoring a value captured from an overlapping mutation.
            std::lock_guard lock(environmentMutex);
            Detail::Platform::EnvironmentReadResult readResult = Detail::Platform::readEnvironmentVariable(name_);
            if (!readResult.status.ok())
            {
                status_ = readResult.status;
                return;
            }
            previousValue_ = std::move(readResult.value);
            status_ = Detail::Platform::setEnvironmentVariableValue(name_, value);
        }
        catch (const std::bad_alloc &)
        {
            status_ = failureStatus(Types::InfrastructureError::OutOfMemory);
        }
        catch (...)
        {
            status_ = failureStatus(Types::InfrastructureError::EnvironmentFailed);
        }
    }

    ScopedEnvironmentVariable::~ScopedEnvironmentVariable() noexcept
    {
        if (!status_.ok())
        {
            return;
        }

        restoreEnvironmentVariable(name_, previousValue_);
    }

    Types::InfrastructureStatus ScopedEnvironmentVariable::status() const noexcept
    {
        return status_;
    }

    // ------------------------------------------------------------
    // Scoped unset/restore environment variable
    // ------------------------------------------------------------

    ScopedUnsetEnvironmentVariable::ScopedUnsetEnvironmentVariable(std::string_view name) noexcept
    {
        try
        {
            name_.assign(name);

            // Environment variables are process-global, so guards serialize read/modify/write
            // pairs to avoid restoring a value captured from an overlapping mutation.
            std::lock_guard lock(environmentMutex);
            Detail::Platform::EnvironmentReadResult readResult = Detail::Platform::readEnvironmentVariable(name_);
            if (!readResult.status.ok())
            {
                status_ = readResult.status;
                return;
            }
            previousValue_ = std::move(readResult.value);
            status_ = Detail::Platform::unsetEnvironmentVariableValue(name_);
        }
        catch (const std::bad_alloc &)
        {
            status_ = failureStatus(Types::InfrastructureError::OutOfMemory);
        }
        catch (...)
        {
            status_ = failureStatus(Types::InfrastructureError::EnvironmentFailed);
        }
    }

    ScopedUnsetEnvironmentVariable::~ScopedUnsetEnvironmentVariable() noexcept
    {
        if (!status_.ok())
        {
            return;
        }

        restoreEnvironmentVariable(name_, previousValue_);
    }

    Types::InfrastructureStatus ScopedUnsetEnvironmentVariable::status() const noexcept
    {
        return status_;
    }
} // namespace GameWIP::TestSupport
