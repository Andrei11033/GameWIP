/// @file win32_drag_drop.cpp
/// @brief Win32 OLE drop-target lifecycle and callbacks.

#include "desktop/internal/results.h"
#include "desktop/platform/drag_drop.h"
#include "desktop/internal/desktop_test_hooks.h"
#include "desktop/internal/window_state.h"
#include "desktop/platform/win32/internal/test_hooks.h"
#include "desktop/platform/win32/internal/win32_data_transfer.h"
#include "desktop/platform/win32/internal/win32_drag_drop.h"
#include "desktop/platform/win32/internal/win32_window_backend.h"

#include <ole2.h>

#include <algorithm>
#include <atomic>
#include <new>
#include <unordered_set>

namespace GameWIP::Desktop::Platform
{
    /// ------------------------------------------------------------
    /// Native target state
    /// ------------------------------------------------------------

    /// @brief Thread-affine native OLE target resources for one Window.
    struct DragDropData
    {
        HWND window = nullptr;
        DWORD ownerThreadId = 0;
        IDropTarget *target = nullptr;
        bool registered = false;
        bool oleHeld = false;
    };

    namespace
    {
        /// ------------------------------------------------------------
        /// Target region and callback helpers
        /// ------------------------------------------------------------

        namespace DD = Types::DragDrop;
        namespace Transfer = Types::DataTransfer;

        /// @brief Consumes the materialization failure hook used by drag/drop tests.
        [[nodiscard]] GameWIP::Status::Types::Status materializationStatus() noexcept
        {
            return Internal::consumeFailure(GameWIP::Desktop::Internal::TestHooks::FailurePoint::DragDropMaterialization)
                       ? GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::ReadFailed)
                       : GameWIP::Status::Types::Status{};
        }

        /// @brief Tests whether one native format is accepted by a drop region.
        [[nodiscard]] bool accepts(const DragDropRegion &region, CLIPFORMAT offered) noexcept
        {
            return std::ranges::find(region.nativeFormats, static_cast<std::uint32_t>(offered)) != region.nativeFormats.end();
        }

        /// @brief Converts a screen-pixel OLE point to Window-local logical units.
        [[nodiscard]] Types::LogicalPosition clientPosition(const DragDropState &state, POINTL screen) noexcept
        {
            POINT p{screen.x, screen.y};
            ScreenToClient(state.platform ? state.platform->window : nullptr, &p);
            const UINT dpi = state.platform ? dpiForWindow(state.platform->window) : kBaselineDpi;
            return {physicalToLogical(p.x, dpi), physicalToLogical(p.y, dpi)};
        }

        /// @brief Selects the last matching region at a point for the offered formats.
        [[nodiscard]] const DragDropRegion *regionAt(
            const DragDropState &state,
            Types::LogicalPosition point,
            const std::vector<CLIPFORMAT> &offered) noexcept
        {
            const DragDropRegion *result = nullptr;

            for(const auto &region : state.regions)
            {
                const bool inside = !region.rect || pointInRect(point, *region.rect);
                const bool acceptedFormat = std::ranges::any_of(
                    offered,
                    [&](CLIPFORMAT offeredFormat)
                    {
                        return accepts(region, offeredFormat);
                    });

                if(inside && acceptedFormat)
                {
                    result = &region;
                }
            }

            return result;
        }

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"
#endif

        /// ------------------------------------------------------------
        /// Target callbacks
        /// ------------------------------------------------------------

        /// @brief OLE drop target that translates callbacks into portable events.
        class DropTarget final : public IDropTarget
        {
        public:
            explicit DropTarget(DragDropState &state) noexcept
                : state_(&state)
            {
            }
            HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) noexcept override
            {
                if(!out)
                {
                    return E_POINTER;
                }
                *out = nullptr;

                if(id == IID_IUnknown || id == IID_IDropTarget)
                {
                    *out = static_cast<IDropTarget *>(this);
                }
                else
                {
                    return E_NOINTERFACE;
                }

                AddRef();
                return S_OK;
            }

            ULONG STDMETHODCALLTYPE AddRef() noexcept override
            {
                return ++references_;
            }

            ULONG STDMETHODCALLTYPE Release() noexcept override
            {
                const ULONG count = --references_;

                if(!count)
                {
                    delete this;
                }

                return count;
            }

            HRESULT STDMETHODCALLTYPE DragEnter(IDataObject *object, DWORD, POINTL point, DWORD *effect) noexcept override
            {
                if(!effect || !object)
                {
                    return E_INVALIDARG;
                }

                if(state_ == nullptr)
                {
                    *effect = DROPEFFECT_NONE;
                    return S_OK;
                }

                clearSession();
                try
                {
                    // Snapshot offered formats before selecting a region and effect.
                    std::vector<DataTransfer::FormatIdentity> offered;
                    auto status = DataTransfer::formats(*object, offered);

                    if(!status.ok())
                    {
                        *effect = DROPEFFECT_NONE;
                        return S_OK;
                    }
                    auto portable = std::make_shared<std::vector<Transfer::Format>>();
                    portable->reserve(offered.size());
                    nativeFormats_.reserve(offered.size());

                    for(auto &format : offered)
                    {
                        portable->push_back(std::move(format.portable));
                        nativeFormats_.push_back(format.native);
                    }
                    formats_ = std::move(portable);
                    sourceEffects_ = portableEffects(*effect);
                    session_ = allocateDragDropSessionId(*state_);
                    const auto pos = clientPosition(*state_, point);
                    active_ = regionAt(*state_, pos, nativeFormats_);
                    const auto selected =
                        active_ ? Internal::negotiateDragDropEffect(sourceEffects_, active_->effects.allowed, active_->effects.preferred)
                                : DD::Effect::None;
                    currentRegion_ = active_ ? active_->id : DD::RegionId{};
                    routeDragDropEvent(
                        *state_,
                        DD::Events::Entered{session_, pos, active_ ? std::optional<DD::RegionId>{active_->id} : std::nullopt, selected, formats_});
                    *effect = nativeEffects(selected);
                    return S_OK;
                }
                catch(...)
                {
                    clearSession();
                    *effect = DROPEFFECT_NONE;
                    return S_OK;
                }
            }
            HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL point, DWORD *effect) noexcept override
            {
                if(!effect || !formats_ || state_ == nullptr)
                {
                    if(effect)
                    {
                        *effect = DROPEFFECT_NONE;
                    }

                    return S_OK;
                }
                const auto pos = clientPosition(*state_, point);
                const auto *next = regionAt(*state_, pos, nativeFormats_);
                const auto region = next ? next->id : DD::RegionId{};
                const auto selected =
                    next ? Internal::negotiateDragDropEffect(sourceEffects_, next->effects.allowed, next->effects.preferred) : DD::Effect::None;
                routeDragDropEvent(
                    *state_,
                    DD::Events::Moved{
                        session_,
                        pos,
                        currentRegion_.isValid() ? std::optional<DD::RegionId>{currentRegion_} : std::nullopt,
                        next ? std::optional<DD::RegionId>{region} : std::nullopt,
                        selected,
                        formats_});
                active_ = next;
                currentRegion_ = region;
                *effect = nativeEffects(selected);
                return S_OK;
            }

            HRESULT STDMETHODCALLTYPE DragLeave() noexcept override
            {
                if(state_ == nullptr)
                {
                    clearSession();
                    return S_OK;
                }

                if(session_.isValid())
                {
                    routeDragDropEvent(*state_, DD::Events::Left{session_});
                }
                clearSession();
                return S_OK;
            }

            HRESULT STDMETHODCALLTYPE Drop(IDataObject *object, DWORD, POINTL point, DWORD *effect) noexcept override
            {
                if(!effect || !object || !formats_ || state_ == nullptr)
                {
                    if(effect)
                    {
                        *effect = DROPEFFECT_NONE;
                    }
                    clearSession();
                    return S_OK;
                }
                const auto pos = clientPosition(*state_, point);
                const auto *region = regionAt(*state_, pos, nativeFormats_);
                const auto selected =
                    region ? Internal::negotiateDragDropEffect(sourceEffects_, region->effects.allowed, region->effects.preferred) : DD::Effect::None;

                if(selected == DD::Effect::None)
                {
                    *effect = DROPEFFECT_NONE;
                    clearSession();
                    return S_OK;
                }

                try
                {
                    // Materialize only the formats accepted by the selected region.
                    Transfer::Payload payload;

                    for(std::size_t index = 0; index < region->formats.size(); ++index)
                    {
                        if(std::ranges::find(nativeFormats_, static_cast<CLIPFORMAT>(region->nativeFormats[index])) != nativeFormats_.end())
                        {
                            if(!materializationStatus().ok())
                            {
                                *effect = DROPEFFECT_NONE;
                                clearSession();
                                return S_OK;
                            }
                            Transfer::Item item;
                            auto status = DataTransfer::materialize(*object, region->formats[index], item);

                            if(!status.ok())
                            {
                                *effect = DROPEFFECT_NONE;
                                clearSession();
                                return S_OK;
                            }
                            payload.push_back(std::move(item));
                        }
                    }

                    if(payload.empty())
                    {
                        *effect = DROPEFFECT_NONE;
                        clearSession();
                        return S_OK;
                    }

                    // Queue the terminal drop before clearing the callback session.
                    routeDragDropEvent(*state_, DD::Events::Dropped{session_, pos, region->id, selected, std::move(payload)}, true);
                    *effect = nativeEffects(selected);
                    clearSession();
                    return S_OK;
                }
                catch(...)
                {
                    *effect = DROPEFFECT_NONE;
                    clearSession();
                    return S_OK;
                }
            }
            void detach() noexcept
            {
                state_ = nullptr;
                clearSession();
            }

        private:
            /// @brief Clears the active OLE session without emitting a new event.
            void clearSession() noexcept
            {
                formats_.reset();
                nativeFormats_.clear();
                session_ = {};
                currentRegion_ = {};
                active_ = nullptr;
                sourceEffects_ = DD::Effect::None;
            }
            std::atomic<ULONG> references_{1};
            DragDropState *state_ = nullptr;
            DD::SessionId session_;
            DD::RegionId currentRegion_;
            DD::Effect sourceEffects_ = DD::Effect::None;
            const DragDropRegion *active_ = nullptr;
            DD::FormatSnapshot formats_;
            std::vector<CLIPFORMAT> nativeFormats_;
        };

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

    } // namespace

    /// ------------------------------------------------------------
    /// Target lifecycle
    /// ------------------------------------------------------------

    /// @brief Releases native target resources only after apartment-affine close has completed.
    void DragDropDataDeleter::operator()(DragDropData *data) const noexcept
    {
        // Apartment-affine cleanup is explicit in closeDragDropTarget(). A record that
        // still owns OLE resources must be retained, never finalized by this deleter.
        if(data == nullptr)
        {
            return;
        }

        if(data->registered)
        {
            return;
        }

        if(data->target != nullptr)
        {
            data->target->Release();
        }

        if(data->oleHeld)
        {
            OleUninitialize();
        }
        delete data;
    }

    namespace
    {
        /// @brief Finds the registered drop target associated with a Window.
        [[nodiscard]] DragDropState *targetForWindow(const WindowState &window) noexcept
        {
            const auto &targets = dispatcher().dragDropTargets;

            if(!targets)
            {
                return nullptr;
            }

            const auto found = std::ranges::find_if(
                *targets,
                [&](const DragDropState *target)
                {
                    return target != nullptr && target->windowId == window.id;
                });
            return found == targets->end() ? nullptr : *found;
        }

        /// @brief Removes a target from the current dispatcher's ownership list.
        void unregisterTarget(DragDropState &state) noexcept
        {
            auto &targets = dispatcher().dragDropTargets;

            if(!targets)
            {
                return;
            }
            targets->erase(std::remove(targets->begin(), targets->end(), &state), targets->end());

            if(targets->empty())
            {
                targets.reset();
            }
        }
    } // namespace

    /// @brief Resolves portable region formats to unique native clipboard identities.
    GameWIP::Status::Types::Status prepareDragDropRegions(std::vector<DragDropRegion> &regions) noexcept
    {
        try
        {
            for(auto &region : regions)
            {
                std::unordered_set<CLIPFORMAT> identities;
                std::vector<std::uint32_t> normalized;
                normalized.reserve(region.formats.size());

                for(const auto &format : region.formats)
                {
                    GameWIP::Status::Types::Status status;
                    const CLIPFORMAT native = DataTransfer::nativeFormat(format, status);

                    if(!status.ok())
                    {
                        return status;
                    }

                    if(!identities.insert(native).second)
                    {
                        return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::InvalidArgument);
                    }
                    normalized.push_back(native);
                }
                region.nativeFormats = std::move(normalized);
            }

            return GameWIP::Status::Types::Status{};
        }
        catch(const std::bad_alloc &)
        {
            return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::OutOfMemory);
        }
        catch(...)
        {
            return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::Unknown);
        }
    }

    /// @brief Initializes OLE and registers a Window's native drop target.
    GameWIP::Status::Types::Status openDragDropTarget(DragDropState &state, WindowState &window) noexcept
    {
        try
        {
            // Reserve dispatcher ownership before registering callbacks with OLE.
            Dispatcher &current = dispatcher();

            if(targetForWindow(window) != nullptr)
            {
                return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::ResourceBusy);
            }

            if(!current.dragDropTargets)
            {
                current.dragDropTargets = std::make_unique<std::vector<DragDropState *>>();
            }
            current.dragDropTargets->push_back(&state);
            auto data = std::unique_ptr<DragDropData, DragDropDataDeleter>(new DragDropData{});
            data->window = window.platform->handle;
            data->ownerThreadId = window.platform->ownerThreadId;
            state.ownerNativeThreadId = data->ownerThreadId;

            if(Internal::consumeFailure(GameWIP::Desktop::Internal::TestHooks::FailurePoint::DragDropOleInitialization))
            {
                unregisterTarget(state);
                return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::OpenFailed);
            }
            const HRESULT init = OleInitialize(nullptr);

            if(init != S_OK && init != S_FALSE)
            {
                unregisterTarget(state);
                return GameWIP::Desktop::Internal::statusFor(
                    init == RPC_E_CHANGED_MODE ? GameWIP::Status::Types::Code::ResourceBusy : GameWIP::Status::Types::Code::OpenFailed,
                    init,
                    GameWIP::Desktop::Internal::NativeCodeDomain::HResult);
            }
            data->oleHeld = true;

            data->target = new DropTarget(state);
            const HRESULT hr = Internal::consumeFailure(GameWIP::Desktop::Internal::TestHooks::FailurePoint::DragDropRegistration)
                                   ? E_FAIL
                                   : RegisterDragDrop(data->window, data->target);

            if(FAILED(hr))
            {
                data->target->Release();
                data->target = nullptr;
                OleUninitialize();
                data->oleHeld = false;
                unregisterTarget(state);
                return GameWIP::Desktop::Internal::statusFor(
                    hr == DRAGDROP_E_ALREADYREGISTERED ? GameWIP::Status::Types::Code::ResourceBusy : GameWIP::Status::Types::Code::OpenFailed,
                    hr,
                    GameWIP::Desktop::Internal::NativeCodeDomain::HResult);
            }
            data->registered = true;

            // Publish target ownership only after OLE accepts registration.
            state.platform = std::move(data);
            return GameWIP::Status::Types::Status{};
        }
        catch(const std::bad_alloc &)
        {
            unregisterTarget(state);
            return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::OutOfMemory);
        }
        catch(...)
        {
            unregisterTarget(state);
            return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::Unknown);
        }
    }

    /// @brief Revokes the native drop target and releases its OLE apartment lease.
    CloseResult closeDragDropTarget(DragDropState &state) noexcept
    {
        if(!state.platform)
        {
            return Internal::result(GameWIP::Status::Types::Status{}, true);
        }

        if(state.platform->ownerThreadId != GetCurrentThreadId())
        {
            return Internal::result(GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::ResourceBusy), false);
        }

        // Keep the target attached if OLE cannot revoke it safely.
        HRESULT hr = S_OK;

        if(state.platform->registered)
        {
            hr = (Internal::consumeFailure(GameWIP::Desktop::Internal::TestHooks::FailurePoint::DragDropRevocation) ||
                  Internal::consumeDragDropRevocationFailure())
                     ? E_FAIL
                     : RevokeDragDrop(state.platform->window);
        }

        if(FAILED(hr) && hr != DRAGDROP_E_NOTREGISTERED)
        {
            return Internal::result(
                GameWIP::Desktop::Internal::statusFor(
                    GameWIP::Status::Types::Code::CloseFailed,
                    hr,
                    GameWIP::Desktop::Internal::NativeCodeDomain::HResult),
                false);
        }

        state.platform->registered = false;

        // Detach callbacks before releasing the target and its apartment lease.
        if(state.platform->target)
        {
            static_cast<DropTarget *>(state.platform->target)->detach();
            state.platform->target->Release();
            state.platform->target = nullptr;
        }

        if(state.platform->oleHeld)
        {
            OleUninitialize();
            state.platform->oleHeld = false;
        }
        unregisterTarget(state);
        state.platform.reset();
        return Internal::result(GameWIP::Status::Types::Status{}, true);
    }

    /// @brief Attempts drop-target close without reporting teardown failures.
    bool closeDragDropTargetBestEffort(DragDropState &state) noexcept
    {
        if(!state.platform)
        {
            return true;
        }
        if(state.platform->ownerThreadId != GetCurrentThreadId())
        {
            return false;
        }
        const CloseResult result = closeDragDropTarget(state);
        return result.hasValue() && result.value();
    }

    /// @brief Detaches portable state when the owner dispatcher is exiting.
    void finalizeDragDropTargetForDispatcherExit(DragDropState &state) noexcept
    {
        constexpr std::size_t retryCount = 4;

        for(std::size_t attempt = 0; attempt < retryCount; ++attempt)
        {
            const CloseResult result = closeDragDropTarget(state);

            if(result.hasValue() && result.value())
            {
                state.window = nullptr;
                state.nativeDestroyedPendingFinalize = true;
                return;
            }
        }

        // A dispatcher cannot transfer apartment-affine work after its thread exits.
        // If native revocation persistently fails, detach portable state while the owner
        // apartment is still alive and leave the registered reference owned by OLE. The
        // target then rejects callbacks safely, and apartment/process teardown owns the
        // remaining native registration rather than a leaked GameWIP state allocation.
        if(state.platform)
        {
            if(state.platform->target != nullptr)
            {
                static_cast<DropTarget *>(state.platform->target)->detach();
                state.platform->target->Release();
                state.platform->target = nullptr;
            }
            state.platform->registered = false;
            state.platform->oleHeld = false;
            unregisterTarget(state);
            state.platform.reset();
        }
        state.window = nullptr;
        state.nativeDestroyedPendingFinalize = true;
    }

    /// @brief Tests whether the current thread owns the target's OLE apartment.
    bool dragDropTargetOwnedByCurrentThread(const DragDropState &state) noexcept
    {
        return state.ownerNativeThreadId != 0 && state.ownerNativeThreadId == GetCurrentThreadId();
    }

    /// @brief Tests whether the registered drop target still has a live HWND.
    bool hasLiveDragDropTarget(const DragDropState &state) noexcept
    {
        return state.platform && state.platform->registered && IsWindow(state.platform->window);
    }

    /// @brief Tests whether native target storage remains attached to the state.
    bool hasNativeDragDropResources(const DragDropState &state) noexcept
    {
        return state.platform != nullptr;
    }

    /// @brief Tests whether a Window currently owns a drop target.
    bool hasDragDropTarget(const WindowState &window) noexcept
    {
        return targetForWindow(window) != nullptr;
    }

    /// @brief Enqueues one drag/drop event and accounts for queue pressure during pumping.
    void routeDragDropEvent(DragDropState &state, DD::Events::Payload data, bool terminal) noexcept
    {
        const std::uint64_t droppedBefore = state.droppedEvents;
        const bool queued = enqueueDragDropEvent(state, std::move(data), terminal);
        Dispatcher &current = dispatcher();

        if(current.activePumpInfo != nullptr)
        {
            current.activePumpInfo->eventsDropped += state.droppedEvents - droppedBefore;

            if(queued)
            {
                ++current.activePumpInfo->eventsQueued;
            }
        }
    }

    /// @brief Closes or marks a Window's target during native Window destruction.
    bool windowClosingDragDrop(WindowState &window, bool nativeDestroyed) noexcept
    {
        DragDropState *target = targetForWindow(window);

        if(target == nullptr)
        {
            return true;
        }
        const CloseResult closed = closeDragDropTarget(*target);

        if((closed.hasValue() && closed.value()) || nativeDestroyed)
        {
            target->window = nullptr;
            target->nativeDestroyedPendingFinalize = true;
        }

        return closed.hasValue() && closed.value();
    }

} // namespace GameWIP::Desktop::Platform

namespace GameWIP::Desktop::Internal::Win32
{
    using namespace GameWIP::Desktop::Platform;

    GameWIP::Status::Types::Status testDragDropMaterialization() noexcept
    {
        return materializationStatus();
    }

    Types::Events::PumpResult testRouteDragDropDuringPump(DragDropState &state, DD::Events::Payload data, bool terminal) noexcept
    {
        Types::Events::PumpInfo info;
        Dispatcher &current = dispatcher();
        Types::Events::PumpInfo *previousInfo = current.activePumpInfo;
        GameWIP::Status::Types::Status *previousStatus = current.activePumpStatus;
        current.activePumpInfo = &info;
        current.activePumpStatus = nullptr;
        routeDragDropEvent(state, std::move(data), terminal);
        current.activePumpInfo = previousInfo;
        current.activePumpStatus = previousStatus;
        return Types::Events::PumpResult::success(std::move(info));
    }

    DD::RegionId testMatchDragDropRegion(
        const DragDropState &state,
        Types::LogicalPosition position,
        std::span<const Transfer::FormatView> offered) noexcept
    {
        try
        {
            std::vector<CLIPFORMAT> native;
            native.reserve(offered.size());

            for(const auto &view : offered)
            {
                Transfer::Format format{view.kind(), std::string(view.customName())};
                GameWIP::Status::Types::Status status;
                const CLIPFORMAT identity = DataTransfer::nativeFormat(format, status);

                if(!status.ok())
                {
                    return {};
                }
                native.push_back(identity);
            }
            const DragDropRegion *matched = regionAt(state, position, native);
            return matched == nullptr ? DD::RegionId{} : matched->id;
        }
        catch(...)
        {
            return {};
        }
    }

    std::size_t testActiveDragDropTargetCount() noexcept
    {
        const auto &targets = dispatcher().dragDropTargets;
        return targets ? targets->size() : 0;
    }

    std::size_t testDeferredDragDropTargetCount() noexcept
    {
        Dispatcher &current = dispatcher();
        std::scoped_lock lock(current.deferredMutex);
        std::size_t count = 0;

        for(DragDropState *state = current.deferredDragDropCleanupHead.get(); state != nullptr; state = state->deferredCleanupNext)
        {
            ++count;
        }

        return count;
    }
} // namespace GameWIP::Desktop::Internal::Win32
