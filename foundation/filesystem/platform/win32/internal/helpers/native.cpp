/// @file native.cpp
/// @brief Win32 FileSystem native handle ownership and cleanup.

#include "filesystem/platform/win32/internal/helpers.h"
#include "base/platform/win32/dynamic_library.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <utility>

namespace GameWIP::FileSystem::Platform::Win32
{
    /// ------------------------------------------------------------
    /// Native handle ownership
    /// ------------------------------------------------------------

    UniqueHandle::UniqueHandle(HANDLE handle) noexcept
        : handle_(handle)
    {
    }

    UniqueHandle::UniqueHandle(UniqueHandle &&other) noexcept
        : handle_(std::exchange(other.handle_, INVALID_HANDLE_VALUE))
    {
    }

    UniqueHandle &UniqueHandle::operator=(UniqueHandle &&other) noexcept
    {
        if(this != &other)
        {
            close();
            handle_ = std::exchange(other.handle_, INVALID_HANDLE_VALUE);
        }

        return *this;
    }

    UniqueHandle::~UniqueHandle() noexcept
    {
        close();
    }

    HANDLE UniqueHandle::get() const noexcept
    {
        return handle_;
    }

    bool UniqueHandle::isValid() const noexcept
    {
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
    }

    HANDLE UniqueHandle::release() noexcept
    {
        return std::exchange(handle_, INVALID_HANDLE_VALUE);
    }

    void UniqueHandle::close() noexcept
    {
        if(isValid())
        {
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
        }
    }

    UniqueFindHandle::UniqueFindHandle(HANDLE handle) noexcept
        : handle_(handle)
    {
    }

    UniqueFindHandle::UniqueFindHandle(UniqueFindHandle &&other) noexcept
        : handle_(std::exchange(other.handle_, INVALID_HANDLE_VALUE))
    {
    }

    UniqueFindHandle &UniqueFindHandle::operator=(UniqueFindHandle &&other) noexcept
    {
        if(this != &other)
        {
            close();
            handle_ = std::exchange(other.handle_, INVALID_HANDLE_VALUE);
        }

        return *this;
    }

    UniqueFindHandle::~UniqueFindHandle() noexcept
    {
        close();
    }

    HANDLE UniqueFindHandle::get() const noexcept
    {
        return handle_;
    }

    bool UniqueFindHandle::isValid() const noexcept
    {
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
    }

    HANDLE UniqueFindHandle::release() noexcept
    {
        return std::exchange(handle_, INVALID_HANDLE_VALUE);
    }

    void UniqueFindHandle::close() noexcept
    {
        if(isValid())
        {
            FindClose(handle_);
            handle_ = INVALID_HANDLE_VALUE;
        }
    }

    /// ------------------------------------------------------------
    /// Native API lookup
    /// ------------------------------------------------------------

    const NtApi &ntApi() noexcept
    {
        static const NtApi api = []
        {
            NtApi result{};
            HMODULE module = GetModuleHandleW(L"ntdll.dll");

            if(module == nullptr)
            {
                module = LoadLibraryW(L"ntdll.dll");
            }

            if(module != nullptr)
            {
                result.createFile = GameWIP::Base::Platform::Win32::loadProcedure<NtApi::NtCreateFileFunction>(module, "NtCreateFile");
                result.setInformationFile = GameWIP::Base::Platform::Win32::loadProcedure<NtApi::NtSetInformationFileFunction>(module, "NtSetInformationFile");
                result.ntStatusToDosError =
                    GameWIP::Base::Platform::Win32::loadProcedure<NtApi::RtlNtStatusToDosErrorFunction>(module, "RtlNtStatusToDosError");
            }

            return result;
        }();

        return api;
    }

    /// ------------------------------------------------------------
    /// Handle state helpers
    /// ------------------------------------------------------------

    HANDLE nativeHandle(const Internal::FileState &state) noexcept
    {
        return static_cast<HANDLE>(state.nativeHandle);
    }

    HANDLE nativeHandle(const Internal::FileLockState &state) noexcept
    {
        return static_cast<HANDLE>(state.nativeHandle);
    }

    bool isValidHandle(HANDLE handle) noexcept
    {
        return handle != nullptr && handle != INVALID_HANDLE_VALUE;
    }

    void clearNativeHandle(Internal::FileState &state) noexcept
    {
        state.nativeHandle = nullptr;
        state.readable = false;
        state.writable = false;
        state.appendMode = false;
        state.flushOnClose = IO::Types::FlushMode::None;
    }

    IO::Types::Status closeNativeHandle(void *&nativeHandleValue) noexcept
    {
        HANDLE handle = static_cast<HANDLE>(nativeHandleValue);

        if(!isValidHandle(handle))
        {
            nativeHandleValue = nullptr;
            return IO::successStatus();
        }

        if(CloseHandle(handle) == FALSE)
        {
            return makeLastErrorStatus(ErrorCode::CloseFailed);
        }

        nativeHandleValue = nullptr;
        return IO::successStatus();
    }

    /// ------------------------------------------------------------
    /// Test-only synchronization state
    /// ------------------------------------------------------------

#if FILESYSTEM_INTERNAL_TEST_HOOKS
    static std::atomic_bool forceFileUnlockFailure = false;

    struct MovePauseHook
    {
        std::mutex mutex;
        std::condition_variable condition;
        bool pauseAfterValidation = false;
        bool pauseAfterCommit = false;
        bool reached = false;
        bool released = false;
    };

    static MovePauseHook movePauseHook;

    bool fileUnlockFailureForced() noexcept
    {
        return forceFileUnlockFailure.load(std::memory_order_acquire);
    }

    void pauseMoveIfArmed(bool afterCommit)
    {
        std::unique_lock lock(movePauseHook.mutex);
        bool &armed = afterCommit ? movePauseHook.pauseAfterCommit : movePauseHook.pauseAfterValidation;

        if(!armed)
        {
            return;
        }

        movePauseHook.reached = true;
        movePauseHook.condition.notify_all();
        movePauseHook.condition.wait(
            lock,
            []
            {
                return movePauseHook.released;
            });
        armed = false;
        movePauseHook.reached = false;
        movePauseHook.released = false;
    }

#endif
} // namespace GameWIP::FileSystem::Platform::Win32

#if FILESYSTEM_INTERNAL_TEST_HOOKS
namespace GameWIP::FileSystem::Internal::TestHooks::Win32
{
    namespace Backend = GameWIP::FileSystem::Platform::Win32;

    void setFileUnlockFailure(bool enabled) noexcept
    {
        Backend::forceFileUnlockFailure.store(enabled, std::memory_order_release);
    }

    void armMoveDestinationValidatedPause() noexcept
    {
        std::lock_guard lock(Backend::movePauseHook.mutex);
        Backend::movePauseHook.pauseAfterValidation = true;
        Backend::movePauseHook.pauseAfterCommit = false;
        Backend::movePauseHook.reached = false;
        Backend::movePauseHook.released = false;
    }

    void armMoveCommittedPause() noexcept
    {
        std::lock_guard lock(Backend::movePauseHook.mutex);
        Backend::movePauseHook.pauseAfterValidation = false;
        Backend::movePauseHook.pauseAfterCommit = true;
        Backend::movePauseHook.reached = false;
        Backend::movePauseHook.released = false;
    }

    bool waitForMovePause(std::chrono::milliseconds timeout) noexcept
    {
        std::unique_lock lock(Backend::movePauseHook.mutex);
        return Backend::movePauseHook.condition.wait_for(
            lock,
            timeout,
            []
            {
                return Backend::movePauseHook.reached;
            });
    }

    void releaseMovePause() noexcept
    {
        std::lock_guard lock(Backend::movePauseHook.mutex);
        Backend::movePauseHook.released = true;
        Backend::movePauseHook.condition.notify_all();
    }

    void reset() noexcept
    {
        Backend::forceFileUnlockFailure.store(false, std::memory_order_release);
        std::lock_guard lock(Backend::movePauseHook.mutex);
        Backend::movePauseHook.pauseAfterValidation = false;
        Backend::movePauseHook.pauseAfterCommit = false;
        Backend::movePauseHook.reached = false;
        Backend::movePauseHook.released = true;
        Backend::movePauseHook.condition.notify_all();
    }
} // namespace GameWIP::FileSystem::Internal::TestHooks::Win32
#endif

namespace GameWIP::FileSystem::Internal
{
    namespace
    {
        [[nodiscard]] bool isValidNativeHandle(void *nativeHandle) noexcept
        {
            const HANDLE handle = static_cast<HANDLE>(nativeHandle);
            return handle != nullptr && handle != INVALID_HANDLE_VALUE;
        }
    } // namespace

    FileState::FileState() = default;

    /// ------------------------------------------------------------
    /// Native state cleanup
    /// ------------------------------------------------------------

    FileState::~FileState() noexcept
    {
        if(isValidNativeHandle(nativeHandle))
        {
            if(writable && IO::isValidFlushMode(flushOnClose) && flushOnClose != IO::Types::FlushMode::None)
            {
                static_cast<void>(FlushFileBuffers(static_cast<HANDLE>(nativeHandle)));
            }

            static_cast<void>(CloseHandle(static_cast<HANDLE>(nativeHandle)));
            nativeHandle = nullptr;
        }
    }

    FileLockState::~FileLockState() noexcept
    {
        if(active && isValidNativeHandle(nativeHandle))
        {
            static_cast<void>(Platform::unlockFile(*this));
        }

        if(isValidNativeHandle(nativeHandle))
        {
            const bool closed = CloseHandle(static_cast<HANDLE>(nativeHandle)) != FALSE;

            if(closed)
            {
                nativeHandle = nullptr;

                if(active)
                {
                    active = false;

                    if(activeLocks && *activeLocks > 0)
                    {
                        --(*activeLocks);
                    }
                }
            }
        }
    }

    DirectoryCursorState::~DirectoryCursorState() noexcept
    {
        const HANDLE findHandle = static_cast<HANDLE>(nativeFindHandle);

        if(findHandle != nullptr && findHandle != INVALID_HANDLE_VALUE)
        {
            static_cast<void>(FindClose(findHandle));
            nativeFindHandle = nullptr;
        }

        for(void *handleValue : stableHandles)
        {
            const HANDLE handle = static_cast<HANDLE>(handleValue);

            if(handle != nullptr && handle != INVALID_HANDLE_VALUE)
            {
                static_cast<void>(CloseHandle(handle));
            }
        }
        stableHandles.clear();
    }
} // namespace GameWIP::FileSystem::Internal
