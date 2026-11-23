/// @file format_scratch.cpp
/// @brief Win32 thread-local formatting scratch storage.

#include "logger/platform/backend.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <new>

namespace
{
#if defined(__MINGW32__)
    /// ------------------------------------------------------------
    /// MinGW FLS storage
    /// ------------------------------------------------------------

    void NTAPI destroyFormatScratch(void *value)
    {
        delete static_cast<GameWIP::Logger::Internal::FormatScratchStorage *>(value);
    }

    /// @brief Owns the process-wide FLS index while its callback code remains loaded.
    class FormatScratchSlot final
    {
    public:
        FormatScratchSlot() noexcept
            : value_(FlsAlloc(destroyFormatScratch))
        {
        }

        ~FormatScratchSlot() noexcept
        {
            if(value_ != FLS_OUT_OF_INDEXES)
            {
                static_cast<void>(FlsFree(value_));
            }
        }

        FormatScratchSlot(const FormatScratchSlot &) = delete;
        FormatScratchSlot &operator=(const FormatScratchSlot &) = delete;

        [[nodiscard]] DWORD value() const noexcept
        {
            return value_;
        }

    private:
        DWORD value_ = FLS_OUT_OF_INDEXES;
    };

    DWORD formatScratchSlot()
    {
        static FormatScratchSlot slot;
        return slot.value();
    }

    GameWIP::Logger::Internal::FormatScratchStorage &formatScratchStorageForThreadFls()
    {
        const DWORD slot = formatScratchSlot();

        if(slot == FLS_OUT_OF_INDEXES)
        {
            throw std::bad_alloc();
        }
        auto *storage = static_cast<GameWIP::Logger::Internal::FormatScratchStorage *>(FlsGetValue(slot));

        // Create storage lazily and register its thread-exit cleanup.
        if(storage == nullptr)
        {
            storage = new GameWIP::Logger::Internal::FormatScratchStorage();

            if(!FlsSetValue(slot, storage))
            {
                delete storage;
                throw std::bad_alloc();
            }
        }

        return *storage;
    }

    GameWIP::Logger::Internal::FormatScratchStorage *findFormatScratchStorageForThreadFls() noexcept
    {
        const DWORD slot = formatScratchSlot();
        if(slot == FLS_OUT_OF_INDEXES)
        {
            return nullptr;
        }

        return static_cast<GameWIP::Logger::Internal::FormatScratchStorage *>(FlsGetValue(slot));
    }
#endif

#if !defined(__MINGW32__)
    GameWIP::Logger::Internal::FormatScratchStorage &formatScratchStorageForThreadLocal()
    {
        thread_local GameWIP::Logger::Internal::FormatScratchStorage storage;
        return storage;
    }
#endif
} // namespace

namespace GameWIP::Logger::Platform
{
    Internal::FormatScratchStorage &formatScratchStorageForThread()
    {
#if defined(__MINGW32__)
        return formatScratchStorageForThreadFls();
#else
        return formatScratchStorageForThreadLocal();
#endif
    }

    Internal::FormatScratchStorage *findFormatScratchStorageForThread() noexcept
    {
#if defined(__MINGW32__)
        return findFormatScratchStorageForThreadFls();
#else
        return &formatScratchStorageForThreadLocal();
#endif
    }
} // namespace GameWIP::Logger::Platform
