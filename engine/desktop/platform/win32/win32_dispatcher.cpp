/// @file win32_dispatcher.cpp
/// @brief Win32 per-thread event queues, registries, and deferred cleanup.

#include "desktop/platform/win32/internal/win32_window_backend.h"
#include "desktop/internal/accessibility_state.h"
#include "desktop/internal/child_surface_state.h"
#include "desktop/internal/drag_drop_state.h"
#include "desktop/internal/progress_dialog_state.h"
#include "desktop/internal/window_state.h"
#include "desktop/platform/child_surface.h"
#include "desktop/platform/dialogs.h"
#include "desktop/platform/drag_drop.h"
#include "desktop/platform/win32/internal/test_hooks.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace GameWIP::Desktop::Platform
{
    /// ------------------------------------------------------------
    /// Process and thread registries
    /// ------------------------------------------------------------

    namespace
    {
        std::mutex windowRegistryMutex;
        std::unordered_map<std::uint64_t, WindowState *> windowRegistry;
        std::atomic_uint64_t nextWindowId{1};

        std::mutex dispatcherRegistryMutex;
        std::unordered_map<DWORD, Dispatcher *> dispatcherRegistry;

        thread_local Dispatcher threadDispatcher{GetCurrentThreadId()};
    } // namespace

    /// ------------------------------------------------------------
    /// Window identity registry
    /// ------------------------------------------------------------

    /// @brief Assigns a nonzero portable identity and publishes it in the process registry.
    void registerWindowId(WindowState &state)
    {
        std::uint64_t value = nextWindowId.fetch_add(1, std::memory_order_relaxed);

        if(value == 0)
        {
            value = nextWindowId.fetch_add(1, std::memory_order_relaxed);
        }
        std::scoped_lock lock(windowRegistryMutex);
        windowRegistry.emplace(value, &state);
        state.id = {value};
    }

    /// @brief Removes a Window identity and clears owners that pointed at it.
    void unregisterWindowId(WindowState &state) noexcept
    {
        std::scoped_lock lock(windowRegistryMutex);
        const Types::WindowId removed = state.id;
        windowRegistry.erase(state.id.value);
        state.id = {};
        state.owner = Types::WindowId::invalid();

        if(!removed.isValid())
        {
            return;
        }

        for(const auto &[id, candidate] : windowRegistry)
        {
            static_cast<void>(id);

            if(candidate != nullptr && candidate->owner == removed)
            {
                candidate->owner = Types::WindowId::invalid();

                if(candidate->platform && candidate->platform->handle != nullptr)
                {
                    SetLastError(ERROR_SUCCESS);
                    const LONG_PTR previous = SetWindowLongPtrW(candidate->platform->handle, GWLP_HWNDPARENT, 0);
                    const DWORD error = GetLastError();

                    if(previous == 0 && error != ERROR_SUCCESS)
                    {
                        recordPumpFailure(statusFromWin32(IO::Types::ErrorCode::NativeFailure, error, "clear destroyed window owner"));
                    }

                    if(IO::Types::Status styleStatus = applyStyle(*candidate); !styleStatus.ok())
                    {
                        recordPumpFailure(std::move(styleStatus));
                    }
                }
                routeEvent(*candidate, Types::Events::OwnerChanged{removed, std::nullopt});
            }
        }
    }

    /// ------------------------------------------------------------
    /// Event routing and dispatcher state
    /// ------------------------------------------------------------

    Dispatcher &dispatcher() noexcept
    {
        return threadDispatcher;
    }

    namespace
    {
        struct RegisteredMessage
        {
            UINT value = 0;
            DWORD error = ERROR_SUCCESS;
        };

        struct ProgressOwnerRestoreMessageState
        {
            std::once_flag registration;
            std::mutex publicationMutex;
            std::atomic<UINT> value{0};
            std::atomic<DWORD> error{ERROR_SUCCESS};
            std::atomic_bool attempted{false};
        };

        /// @brief Registers a process-wide message and retains its native failure code.
        [[nodiscard]] RegisteredMessage registerMessage(const wchar_t *name) noexcept
        {
            SetLastError(ERROR_SUCCESS);
            const UINT value = RegisterWindowMessageW(name);

            if(value != 0)
            {
                return {.value = value, .error = ERROR_SUCCESS};
            }

            const DWORD error = GetLastError();
            return {.value = 0, .error = error == ERROR_SUCCESS ? ERROR_FUNCTION_FAILED : error};
        }

        /// @brief Lazily returns the message used to wake event waits.
        [[nodiscard]] const RegisteredMessage &wakeMessageInfo() noexcept
        {
            static const RegisteredMessage info = registerMessage(L"GameWIP.Window.WakeEventWait");
            return info;
        }

        /// @brief Returns thread-local state for the progress-owner restoration message.
        [[nodiscard]] ProgressOwnerRestoreMessageState &progressOwnerRestoreMessageState() noexcept
        {
            static ProgressOwnerRestoreMessageState state;
            return state;
        }
    } // namespace

    UINT wakeMessage() noexcept
    {
        return wakeMessageInfo().value;
    }

    DWORD wakeMessageError() noexcept
    {
        return wakeMessageInfo().error;
    }

    /// @brief Wakes an owner dispatcher through its thread message queue.
    IO::Types::Status postWakeMessage(DWORD threadId, std::string_view operation) noexcept
    {
        const UINT message = wakeMessage();

        if(message == 0)
        {
            return statusFromWin32(IO::Types::ErrorCode::Interrupted, wakeMessageError(), operation);
        }

        SetLastError(ERROR_SUCCESS);

        if(PostThreadMessageW(threadId, message, 0, 0) == FALSE)
        {
            return statusFromWin32(IO::Types::ErrorCode::Interrupted, GetLastError(), operation);
        }

        return IO::successStatus();
    }

    /// @brief Publishes the progress-owner restoration message once per process.
    UINT ensureProgressOwnerRestoreMessage() noexcept
    {
        ProgressOwnerRestoreMessageState &state = progressOwnerRestoreMessageState();
        try
        {
            std::scoped_lock lock(state.publicationMutex);
            try
            {
                std::call_once(
                    state.registration,
                    [&state]
                    {
                        const RegisteredMessage info = registerMessage(L"GameWIP.Desktop.ProgressOwnerRestore");
                        state.error.store(info.error, std::memory_order_relaxed);
                        state.value.store(info.value, std::memory_order_release);
                        state.attempted.store(true, std::memory_order_release);
                    });
            }
            catch(...)
            {
                state.error.store(ERROR_FUNCTION_FAILED, std::memory_order_relaxed);
                state.value.store(0, std::memory_order_release);
                state.attempted.store(true, std::memory_order_release);
            }
        }
        catch(...)
        {
            state.error.store(ERROR_FUNCTION_FAILED, std::memory_order_relaxed);
            state.value.store(0, std::memory_order_release);
            state.attempted.store(true, std::memory_order_release);
        }

        return state.value.load(std::memory_order_acquire);
    }

    /// @brief Reads the published progress-owner restoration message identifier.
    UINT registeredProgressOwnerRestoreMessage() noexcept
    {
        return progressOwnerRestoreMessageState().value.load(std::memory_order_acquire);
    }

    /// @brief Reads the native error from progress-owner message registration.
    DWORD progressOwnerRestoreMessageError() noexcept
    {
        return progressOwnerRestoreMessageState().error.load(std::memory_order_acquire);
    }

    /// @brief Queries a native window value with test-hook injection and error translation.
    IO::Types::Status queryWindowLong(HWND window, int index, LONG_PTR &value, const char *operation) noexcept
    {
#if DESKTOP_INTERNAL_TEST_HOOKS
        if((index == GWL_STYLE || index == GWL_EXSTYLE) &&
           Internal::consumeFailure(GameWIP::Desktop::Internal::TestHooks::FailurePoint::WindowStyleQuery))
        {
            value = 0;
            return statusFromWin32(IO::Types::ErrorCode::NativeFailure, ERROR_GEN_FAILURE, operation);
        }

#endif
        SetLastError(ERROR_SUCCESS);
        value = GetWindowLongPtrW(window, index);
        const DWORD nativeCode = GetLastError();

        if(value == 0 && nativeCode != ERROR_SUCCESS)
        {
            return statusFromWin32(IO::Types::ErrorCode::NativeFailure, nativeCode, operation);
        }

        return IO::successStatus();
    }

    /// @brief Enqueues a Window event and updates the active pump accounting.
    void routeEvent(WindowState &state, Types::Events::Payload data) noexcept
    {
        const std::uint64_t droppedBefore = state.droppedEvents;
        const EnqueueResult result = enqueueEvent(state, std::move(data));
        Dispatcher &current = dispatcher();

        if(current.activeResult != nullptr)
        {
            current.activeResult->eventsDropped += state.droppedEvents - droppedBefore;

            if(result != EnqueueResult::Dropped)
            {
                ++current.activeResult->eventsQueued;
            }
        }
    }

    /// @brief Publishes the first pump failure while retaining deferred failures for later polling.
    void recordPumpFailure(IO::Types::Status status) noexcept
    {
        Dispatcher &current = dispatcher();

        if(current.activeResult != nullptr && current.activeResult->status.ok())
        {
            current.activeResult->status = std::move(status);
            return;
        }

        if(current.activeResult == nullptr && !current.deferredPumpFailure)
        {
            current.deferredPumpFailure = std::move(status);
        }
    }

    /// @brief Registers an open Window with the current dispatcher and message queue.
    void registerOpenState(WindowState &state)
    {
        Dispatcher &current = dispatcher();
        {
            std::scoped_lock lock(dispatcherRegistryMutex);
            dispatcherRegistry[current.threadId] = &current;
        }
        MSG message{};
        PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
        current.windows.push_back(&state);
    }

    /// @brief Removes an open Window and releases thread-local display-color resources when idle.
    void unregisterOpenState(WindowState &state) noexcept
    {
        Dispatcher &current = dispatcher();
        current.windows.erase(std::remove(current.windows.begin(), current.windows.end(), &state), current.windows.end());

        if(current.windows.empty())
        {
            releaseDisplayColorResources();
        }
    }

    /// @brief Tests whether the calling thread currently owns any open top-level Window.
    bool hasOpenWindowsOnCurrentThread() noexcept
    {
        std::scoped_lock lock(dispatcherRegistryMutex);
        const auto found = dispatcherRegistry.find(GetCurrentThreadId());
        return found != dispatcherRegistry.end() && found->second != nullptr && !found->second->windows.empty();
    }

    /// @brief Registers an open child surface with the current dispatcher.
    void registerOpenChildSurface(ChildSurfaceState &state)
    {
        Dispatcher &current = dispatcher();
        {
            std::scoped_lock lock(dispatcherRegistryMutex);
            dispatcherRegistry[current.threadId] = &current;
        }
        MSG message{};
        PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
        current.childSurfaces.push_back(&state);
    }

    /// @brief Removes an open child surface from the current dispatcher.
    void unregisterOpenChildSurface(ChildSurfaceState &state) noexcept
    {
        Dispatcher &current = dispatcher();
        current.childSurfaces.erase(std::remove(current.childSurfaces.begin(), current.childSurfaces.end(), &state), current.childSurfaces.end());
    }

    /// @brief Registers an open progress dialog with the current dispatcher.
    void registerOpenProgressDialog(ProgressDialogState &state)
    {
        Dispatcher &current = dispatcher();
        {
            std::scoped_lock lock(dispatcherRegistryMutex);
            dispatcherRegistry[current.threadId] = &current;
        }
        MSG message{};
        PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);

        if(!current.progressDialogs)
        {
            auto progressDialogs = std::make_unique<std::vector<ProgressDialogState *>>();
            progressDialogs->push_back(&state);
            current.progressDialogs = std::move(progressDialogs);
            return;
        }
        current.progressDialogs->push_back(&state);
    }

    /// @brief Removes an open progress dialog from the current dispatcher.
    void unregisterOpenProgressDialog(ProgressDialogState &state) noexcept
    {
        Dispatcher &current = dispatcher();

        if(!current.progressDialogs)
        {
            return;
        }
        auto &states = *current.progressDialogs;
        states.erase(std::remove(states.begin(), states.end(), &state), states.end());

        if(states.empty())
        {
            current.progressDialogs.reset();
        }
    }

    /// @brief Enqueues a child-surface event and updates the active pump accounting.
    void routeChildSurfaceEvent(ChildSurfaceState &state, Types::ChildSurface::Events::Payload data) noexcept
    {
        const std::uint64_t droppedBefore = state.droppedEvents;
        const ChildSurfaceEnqueueResult result = enqueueChildSurfaceEvent(state, data);
        Dispatcher &current = dispatcher();

        if(current.activeResult != nullptr)
        {
            current.activeResult->eventsDropped += state.droppedEvents - droppedBefore;

            if(result != ChildSurfaceEnqueueResult::Dropped)
            {
                ++current.activeResult->eventsQueued;
            }
        }
    }

    /// @brief Refreshes child screen rectangles after a parent Window moves.
    void refreshChildSurfaceScreenRectsForParent(Types::WindowId parentId) noexcept
    {
        if(!parentId.isValid())
        {
            return;
        }

        for(ChildSurfaceState *child : dispatcher().childSurfaces)
        {
            if(child != nullptr && child->parentId == parentId)
            {
                refreshChildSurfaceScreenRect(*child);
            }
        }
    }

    /// @brief Retries cleanup deferred from foreign threads before the next pump.
    void pruneAbandonedStates(Dispatcher &current) noexcept
    {
        restorePendingProgressOwners(current);
        std::unique_ptr<ProgressDialogState> progressCleanup;
        {
            std::scoped_lock lock(current.deferredMutex);
            progressCleanup = std::move(current.deferredProgressDialogCleanupHead);
        }

        while(progressCleanup)
        {
            if(!closeProgressBestEffort(*progressCleanup))
            {
                ProgressDialogState *tail = progressCleanup.get();

                while(tail->deferredCleanupNext != nullptr)
                {
                    tail = tail->deferredCleanupNext;
                }
                {
                    std::scoped_lock lock(current.deferredMutex);
                    tail->deferredCleanupNext = current.deferredProgressDialogCleanupHead.release();
                    current.deferredProgressDialogCleanupHead = std::move(progressCleanup);
                }
                // Cleanup is requeued; this post only wakes the owner for another attempt.
                static_cast<void>(postWakeMessage(current.threadId, "PostThreadMessageW wake"));
                break;
            }
            std::unique_ptr<ProgressDialogState> next{progressCleanup->deferredCleanupNext};
            progressCleanup->deferredCleanupNext = nullptr;
            progressCleanup = std::move(next);
        }

        std::unique_ptr<WindowState> cleanup;
        {
            std::scoped_lock lock(current.deferredMutex);
            cleanup = std::move(current.deferredCleanupHead);
        }

        while(cleanup)
        {
            std::unique_ptr<WindowState> next{cleanup->deferredCleanupNext};
            cleanup->deferredCleanupNext = nullptr;
            closeBestEffort(*cleanup);
            cleanup = std::move(next);
        }

        std::unique_ptr<ChildSurfaceState> childCleanup;
        {
            std::scoped_lock lock(current.deferredMutex);
            childCleanup = std::move(current.deferredChildCleanupHead);
        }

        while(childCleanup)
        {
            std::unique_ptr<ChildSurfaceState> next{childCleanup->deferredCleanupNext};
            childCleanup->deferredCleanupNext = nullptr;
            closeChildSurfaceBestEffort(*childCleanup);
            childCleanup = std::move(next);
        }

        std::unique_ptr<DragDropState> dragDropCleanup;
        {
            std::scoped_lock lock(current.deferredMutex);
            dragDropCleanup = std::move(current.deferredDragDropCleanupHead);
        }

        while(dragDropCleanup)
        {
            if(!closeDragDropTargetBestEffort(*dragDropCleanup))
            {
                static_cast<void>(deferDragDropCleanupToOwner(dragDropCleanup));
                break;
            }
            std::unique_ptr<DragDropState> next{dragDropCleanup->deferredCleanupNext};
            dragDropCleanup->deferredCleanupNext = nullptr;
            dragDropCleanup = std::move(next);
        }
    }

    Dispatcher::Dispatcher(DWORD owningThreadId) noexcept
        : threadId(owningThreadId)
    {
    }

    Dispatcher::~Dispatcher() noexcept
    {
        // Thread-exit cleanup cannot safely enter DXGI Release. Normal query and
        // final-window paths release eagerly; this exceptional path leaves the
        // remaining reference for process reclamation.
        abandonDisplayColorResources();

        // Keep the registry locked through shutdown. A concurrent wrong-thread
        // destructor either transfers before this point or waits until every
        // owner-thread-affine native resource has been released.
        std::scoped_lock registryLock(dispatcherRegistryMutex);
        std::unique_ptr<ProgressDialogState> deferredProgress;
        {
            std::scoped_lock lock(deferredMutex);
            deferredProgress = std::move(deferredProgressDialogCleanupHead);
        }

        while(deferredProgress)
        {
            std::unique_ptr<ProgressDialogState> next{deferredProgress->deferredCleanupNext};
            deferredProgress->deferredCleanupNext = nullptr;
            finalizeProgressForDispatcherExit(*deferredProgress);
            deferredProgress = std::move(next);
        }

        while(progressDialogs && !progressDialogs->empty())
        {
            ProgressDialogState *state = progressDialogs->back();

            if(state == nullptr)
            {
                progressDialogs->pop_back();
                continue;
            }
            finalizeProgressForDispatcherExit(*state);
        }

        std::unique_ptr<DragDropState> deferredDragDrop;
        {
            std::scoped_lock lock(deferredMutex);
            deferredDragDrop = std::move(deferredDragDropCleanupHead);
        }

        while(deferredDragDrop)
        {
            std::unique_ptr<DragDropState> next{deferredDragDrop->deferredCleanupNext};
            deferredDragDrop->deferredCleanupNext = nullptr;
            finalizeDragDropTargetForDispatcherExit(*deferredDragDrop);
            deferredDragDrop = std::move(next);
        }

        while(dragDropTargets && !dragDropTargets->empty())
        {
            DragDropState *state = dragDropTargets->back();

            if(state == nullptr)
            {
                dragDropTargets->pop_back();
                continue;
            }
            finalizeDragDropTargetForDispatcherExit(*state);
        }

        const auto found = dispatcherRegistry.find(threadId);

        if(found != dispatcherRegistry.end() && found->second == this)
        {
            dispatcherRegistry.erase(found);
        }

        std::unique_ptr<WindowState> deferred;
        {
            std::scoped_lock lock(deferredMutex);
            deferred = std::move(deferredCleanupHead);
        }

        while(deferred)
        {
            std::unique_ptr<WindowState> next{deferred->deferredCleanupNext};
            deferred->deferredCleanupNext = nullptr;
            closeBestEffort(*deferred);
            deferred = std::move(next);
        }

        std::unique_ptr<ChildSurfaceState> deferredChild;
        {
            std::scoped_lock lock(deferredMutex);
            deferredChild = std::move(deferredChildCleanupHead);
        }

        while(deferredChild)
        {
            std::unique_ptr<ChildSurfaceState> next{deferredChild->deferredCleanupNext};
            deferredChild->deferredCleanupNext = nullptr;
            closeChildSurfaceBestEffort(*deferredChild);
            deferredChild = std::move(next);
        }

        while(!childSurfaces.empty())
        {
            ChildSurfaceState *state = childSurfaces.back();

            if(state == nullptr)
            {
                childSurfaces.pop_back();
                continue;
            }
            closeChildSurfaceBestEffort(*state);
            state->clearRetainedEvents();
        }

        while(!windows.empty())
        {
            WindowState *state = windows.back();

            if(state == nullptr)
            {
                windows.pop_back();
                continue;
            }
            closeBestEffort(*state);
            state->clearRetainedEvents();
        }
    }

    /// @brief Resolves a portable Window identity under the registry mutex.
    WindowState *resolveWindowId(Types::WindowId id) noexcept
    {
        if(!id.isValid())
        {
            return nullptr;
        }

        std::scoped_lock lock(windowRegistryMutex);
        const auto found = windowRegistry.find(id.value);
        return found == windowRegistry.end() ? nullptr : found->second;
    }

    /// ------------------------------------------------------------
    /// Deferred owner-thread cleanup
    /// ------------------------------------------------------------

    // Deferred ownership is committed under deferredMutex before this best-effort wake. A failed
    // wake cannot lose the cleanup obligation; the owner processes the durable queue on its next
    // pump or during thread-exit cleanup.
    /// @brief Transfers a wrong-thread Window to its owner dispatcher for cleanup.
    bool deferCleanupToOwner(std::unique_ptr<WindowState> &state) noexcept
    {
        if(!state || !state->platform)
        {
            return true;
        }

        std::scoped_lock registryLock(dispatcherRegistryMutex);
        const auto found = dispatcherRegistry.find(state->platform->ownerThreadId);
        Dispatcher *owner = found == dispatcherRegistry.end() ? nullptr : found->second;

        if(owner == nullptr)
        {
            return false;
        }

        {
            std::scoped_lock lock(owner->deferredMutex);
            state->deferredCleanupNext = owner->deferredCleanupHead.release();
            owner->deferredCleanupHead = std::move(state);
        }
        static_cast<void>(postWakeMessage(owner->threadId, "PostThreadMessageW wake"));
        return true;
    }

    /// @brief Transfers a wrong-thread child surface to its owner dispatcher.
    bool deferChildSurfaceCleanupToOwner(std::unique_ptr<ChildSurfaceState> &state) noexcept
    {
        if(!state || !state->platform)
        {
            return false;
        }

        std::scoped_lock registryLock(dispatcherRegistryMutex);
        const auto found = dispatcherRegistry.find(state->platform->ownerThreadId);
        Dispatcher *owner = found == dispatcherRegistry.end() ? nullptr : found->second;

        if(owner == nullptr)
        {
            return false;
        }

        {
            std::scoped_lock lock(owner->deferredMutex);
            state->deferredCleanupNext = owner->deferredChildCleanupHead.release();
            owner->deferredChildCleanupHead = std::move(state);
        }
        static_cast<void>(postWakeMessage(owner->threadId, "PostThreadMessageW wake"));
        return true;
    }

    /// @brief Transfers a wrong-thread progress dialog to its owner dispatcher.
    bool deferProgressCleanupToOwner(std::unique_ptr<ProgressDialogState> &state) noexcept
    {
        if(!state || !state->platform)
        {
            return false;
        }

        if(progressOwnerNativeThreadId(*state) == GetCurrentThreadId())
        {
            Dispatcher &owner = dispatcher();
            ProgressDialogState *tail = state.get();

            while(tail->deferredCleanupNext != nullptr)
            {
                tail = tail->deferredCleanupNext;
            }
            {
                std::scoped_lock lock(owner.deferredMutex);
                tail->deferredCleanupNext = owner.deferredProgressDialogCleanupHead.release();
                owner.deferredProgressDialogCleanupHead = std::move(state);
            }
            static_cast<void>(postWakeMessage(owner.threadId, "PostThreadMessageW wake"));
            return true;
        }

        std::scoped_lock registryLock(dispatcherRegistryMutex);
        const auto found = dispatcherRegistry.find(progressOwnerNativeThreadId(*state));
        Dispatcher *owner = found == dispatcherRegistry.end() ? nullptr : found->second;

        if(owner == nullptr)
        {
            return false;
        }

        ProgressDialogState *tail = state.get();

        while(tail->deferredCleanupNext != nullptr)
        {
            tail = tail->deferredCleanupNext;
        }
        {
            std::scoped_lock lock(owner->deferredMutex);
            tail->deferredCleanupNext = owner->deferredProgressDialogCleanupHead.release();
            owner->deferredProgressDialogCleanupHead = std::move(state);
        }
        static_cast<void>(postWakeMessage(owner->threadId, "PostThreadMessageW wake"));
        return true;
    }

    /// @brief Transfers a wrong-thread OLE target to its owner dispatcher.
    bool deferDragDropCleanupToOwner(std::unique_ptr<DragDropState> &state) noexcept
    {
        if(!state || state->ownerNativeThreadId == 0)
        {
            return false;
        }

        std::scoped_lock registryLock(dispatcherRegistryMutex);
        const auto found = dispatcherRegistry.find(static_cast<DWORD>(state->ownerNativeThreadId));
        Dispatcher *owner = found == dispatcherRegistry.end() ? nullptr : found->second;

        if(owner == nullptr)
        {
            return false;
        }

        DragDropState *tail = state.get();

        while(tail->deferredCleanupNext != nullptr)
        {
            tail = tail->deferredCleanupNext;
        }
        {
            std::scoped_lock lock(owner->deferredMutex);
            tail->deferredCleanupNext = owner->deferredDragDropCleanupHead.release();
            owner->deferredDragDropCleanupHead = std::move(state);
        }
        static_cast<void>(postWakeMessage(owner->threadId, "PostThreadMessageW wake"));
        return true;
    }
} // namespace GameWIP::Desktop::Platform

#if DESKTOP_INTERNAL_TEST_HOOKS
namespace GameWIP::Desktop::Internal::TestHooks::Platform
{
    using namespace GameWIP::Desktop::Platform;
    /// @brief Reports whether progress-owner message registration has been attempted.
    bool progressOwnerRestoreMessageRegistrationAttempted() noexcept
    {
        return progressOwnerRestoreMessageState().attempted.load(std::memory_order_acquire);
    }
} // namespace GameWIP::Desktop::Internal::TestHooks::Platform
#endif
