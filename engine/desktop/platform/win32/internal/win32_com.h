/// @file win32_com.h
/// @brief Shared COM ownership and apartment helpers for Desktop's Win32 backends.

#pragma once

#include "io/status.h"

#include <objbase.h>

namespace GameWIP::Desktop::Detail::Platform
{
    /// ------------------------------------------------------------
    /// COM ownership
    /// ------------------------------------------------------------

    /// @brief Maps common COM outcomes to the portable status vocabulary.
    [[nodiscard]] IO::Types::Status statusFromHResult(HRESULT result) noexcept;

    /// @brief Owns one COM interface pointer and releases it at scope exit.
    template <typename Interface> class ComPtr final
    {
    public:
        ComPtr() noexcept = default;
        ComPtr(const ComPtr &) = delete;
        ComPtr &operator=(const ComPtr &) = delete;
        ~ComPtr() noexcept
        {
            reset();
        }

        [[nodiscard]] Interface *get() const noexcept
        {
            return value_;
        }

        [[nodiscard]] Interface **put() noexcept
        {
            reset();
            return &value_;
        }

        Interface *operator->() const noexcept
        {
            return value_;
        }

        explicit operator bool() const noexcept
        {
            return value_ != nullptr;
        }

        void reset() noexcept
        {
            if(value_ != nullptr)
            {
                value_->Release();
                value_ = nullptr;
            }
        }

    private:
        Interface *value_ = nullptr;
    };

    /// ------------------------------------------------------------
    /// STA apartment lease
    /// ------------------------------------------------------------

    /// @brief Ensures the calling thread has a compatible COM apartment for shell APIs.
    class ApartmentLease final
    {
    public:
        ApartmentLease() noexcept;
        ~ApartmentLease() noexcept;

        [[nodiscard]] const IO::Types::Status &status() const noexcept;

    private:
        IO::Types::Status status_;
        bool ownsInitialization_ = false;
    };
} // namespace GameWIP::Desktop::Detail::Platform
