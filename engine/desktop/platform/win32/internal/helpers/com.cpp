/// @file com.cpp
/// @brief COM status mapping and apartment ownership for Desktop's Win32 backend.

#include "desktop/platform/win32/internal/win32_com.h"

namespace GameWIP::Desktop::Platform
{
    /// ------------------------------------------------------------
    /// COM status mapping
    /// ------------------------------------------------------------

    IO::Types::Status statusFromHResult(HRESULT result) noexcept
    {
        using IO::Types::ErrorCode;

        ErrorCode code = ErrorCode::NativeFailure;

        if(result == E_OUTOFMEMORY)
        {
            code = ErrorCode::OutOfMemory;
        }
        else if(result == E_INVALIDARG)
        {
            code = ErrorCode::InvalidArgument;
        }
        else if(result == E_ACCESSDENIED)
        {
            code = ErrorCode::PermissionDenied;
        }
        else if(result == RPC_E_CHANGED_MODE)
        {
            code = ErrorCode::ResourceBusy;
        }
        else if(result == E_NOTIMPL || result == HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED))
        {
            code = ErrorCode::Unsupported;
        }

        return IO::makeStatus(code, static_cast<std::int64_t>(result));
    }

    /// ------------------------------------------------------------
    /// STA apartment lease
    /// ------------------------------------------------------------

    ApartmentLease::ApartmentLease() noexcept
    {
        APTTYPE type = APTTYPE_CURRENT;
        APTTYPEQUALIFIER qualifier = APTTYPEQUALIFIER_NONE;
        HRESULT result = CoGetApartmentType(&type, &qualifier);

        if(SUCCEEDED(result))
        {
            if(type == APTTYPE_STA || type == APTTYPE_MAINSTA)
            {
                status_ = IO::successStatus();
                return;
            }

            if(type != APTTYPE_MTA || qualifier != APTTYPEQUALIFIER_IMPLICIT_MTA)
            {
                status_ = IO::makeStatus(IO::Types::ErrorCode::ResourceBusy, static_cast<std::int64_t>(RPC_E_CHANGED_MODE));
                return;
            }

            // The implicit process MTA allows this uninitialized thread to enter an STA.
        }
        else if(result != CO_E_NOTINITIALIZED)
        {
            status_ = statusFromHResult(result);
            return;
        }

        result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

        if(SUCCEEDED(result))
        {
            ownsInitialization_ = true;
            status_ = IO::successStatus();
        }
        else
        {
            status_ = statusFromHResult(result);
        }
    }

    ApartmentLease::~ApartmentLease() noexcept
    {
        if(ownsInitialization_)
        {
            CoUninitialize();
        }
    }

    const IO::Types::Status &ApartmentLease::status() const noexcept
    {
        return status_;
    }
} // namespace GameWIP::Desktop::Platform
