/// @file win32_drag_drop.cpp
/// @brief Lazy OLE target/source implementation for native data drag and drop.

#include "desktop/internal/drag_drop_platform.h"
#include "desktop/internal/window_state.h"
#include "desktop/platform/win32/internal/win32_data_transfer.h"
#include "desktop/platform/win32/internal/win32_window_backend.h"

#include <ole2.h>

#include <algorithm>
#include <atomic>
#include <new>
#include <unordered_set>

namespace GameWIP::Desktop::Detail::Platform
{
    // ------------------------------------------------------------
    // Native target state
    // ------------------------------------------------------------

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
        // ------------------------------------------------------------
        // Native effect conversion and target policy
        // ------------------------------------------------------------

        namespace DD = Types::DragDrop;
        namespace Transfer = Types::DataTransfer;
        thread_local bool sourceDragActive = false;

        [[nodiscard]] DWORD nativeEffects(DD::Effect effects) noexcept
        {
            DWORD result = 0;
            if ((effects & DD::Effect::Copy) != DD::Effect::None)
                result |= DROPEFFECT_COPY;
            if ((effects & DD::Effect::Move) != DD::Effect::None)
                result |= DROPEFFECT_MOVE;
            if ((effects & DD::Effect::Link) != DD::Effect::None)
                result |= DROPEFFECT_LINK;
            return result;
        }
        [[nodiscard]] DD::Effect portableEffects(DWORD effects) noexcept
        {
            DD::Effect result = DD::Effect::None;
            if (effects & DROPEFFECT_COPY)
                result |= DD::Effect::Copy;
            if (effects & DROPEFFECT_MOVE)
                result |= DD::Effect::Move;
            if (effects & DROPEFFECT_LINK)
                result |= DD::Effect::Link;
            return result;
        }
        [[nodiscard]] DD::Result droppedSourceResult(DWORD performed, DD::Effect allowed) noexcept
        {
            constexpr DWORD knownEffects = DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK;
            DD::Result result;
            const DWORD native = performed & knownEffects;
            const DD::Effect effect = portableEffects(performed);
            if ((performed & ~knownEffects) != 0 || (native & (native - 1U)) != 0 || (effect & allowed) != effect)
                result.status = IO::makeStatus(IO::Types::ErrorCode::NativeFailure, E_UNEXPECTED);
            else
            {
                result.status = IO::successStatus();
                result.outcome = native == 0 ? DD::Outcome::Cancelled : DD::Outcome::Dropped;
                result.effect = effect;
            }
            return result;
        }
        [[nodiscard]] bool validEffects(DD::Effect effects) noexcept
        {
            constexpr auto all = DD::Effect::Copy | DD::Effect::Move | DD::Effect::Link;
            return effects != DD::Effect::None && (effects & all) == effects;
        }
        [[nodiscard]] IO::Types::Status prepareSource(const DD::Description &description, std::vector<DataTransfer::PreparedItem> &prepared) noexcept
        {
            if (description.items.empty() || !validEffects(description.allowedEffects) ||
                static_cast<unsigned>(description.triggerButton) > static_cast<unsigned>(DD::TriggerButton::Middle))
                return IO::makeStatus(IO::Types::ErrorCode::InvalidArgument);
            try
            {
                prepared.reserve(description.items.size());
                std::unordered_set<CLIPFORMAT> formats;
                for (const auto &item : description.items)
                {
                    if (Detail::consumeFailure(TestHooks::FailurePoint::DragDropPreparation))
                        return IO::makeStatus(IO::Types::ErrorCode::OutOfMemory);
                    DataTransfer::PreparedItem value;
                    IO::Types::Status status = DataTransfer::prepare(item, value);
                    if (!status.ok())
                        return status;
                    if (!formats.insert(value.format).second)
                        return IO::makeStatus(IO::Types::ErrorCode::InvalidArgument);
                    prepared.push_back(std::move(value));
                }
                return IO::successStatus();
            }
            catch (const std::bad_alloc &)
            {
                return IO::makeStatus(IO::Types::ErrorCode::OutOfMemory);
            }
            catch (...)
            {
                return IO::makeStatus(IO::Types::ErrorCode::Unknown);
            }
        }
        [[nodiscard]] IO::Types::Status materializationStatus() noexcept
        {
            return Detail::consumeFailure(TestHooks::FailurePoint::DragDropMaterialization) ? IO::makeStatus(IO::Types::ErrorCode::ReadFailed)
                                                                                            : IO::successStatus();
        }
        [[nodiscard]] bool accepts(const DragDropRegion &region, CLIPFORMAT offered) noexcept
        {
            return std::ranges::find(region.nativeFormats, static_cast<std::uint32_t>(offered)) != region.nativeFormats.end();
        }
        [[nodiscard]] Types::LogicalPosition clientPosition(const DragDropState &state, POINTL screen) noexcept
        {
            POINT p{screen.x, screen.y};
            ScreenToClient(state.platform ? state.platform->window : nullptr, &p);
            const UINT dpi = state.platform ? dpiForWindow(state.platform->window) : kBaselineDpi;
            return {physicalToLogical(p.x, dpi), physicalToLogical(p.y, dpi)};
        }
        [[nodiscard]] const DragDropRegion *regionAt(
            const DragDropState &state,
            Types::LogicalPosition point,
            const std::vector<CLIPFORMAT> &offered) noexcept
        {
            const DragDropRegion *result = nullptr;
            for (const auto &region : state.regions)
            {
                const bool inside = !region.rect || pointInRect(point, *region.rect);
                const bool acceptedFormat = std::ranges::any_of(
                    offered,
                    [&](CLIPFORMAT offeredFormat)
                    {
                        return accepts(region, offeredFormat);
                    });
                if (inside && acceptedFormat)
                    result = &region;
            }
            return result;
        }

        // ------------------------------------------------------------
        // OLE lifetime
        // ------------------------------------------------------------

        class OleLease final
        {
        public:
            [[nodiscard]] IO::Types::Status acquire() noexcept
            {
                if (Detail::consumeFailure(TestHooks::FailurePoint::DragDropOleInitialization))
                    return IO::makeStatus(IO::Types::ErrorCode::OpenFailed);
                const HRESULT hr = OleInitialize(nullptr);
                if (hr == S_OK || hr == S_FALSE)
                {
                    held_ = true;
                    return IO::successStatus();
                }
                return IO::makeStatus(hr == RPC_E_CHANGED_MODE ? IO::Types::ErrorCode::ResourceBusy : IO::Types::ErrorCode::OpenFailed, hr);
            }
            ~OleLease() noexcept
            {
                if (held_)
                    OleUninitialize();
            }
            void release() noexcept
            {
                if (held_)
                {
                    OleUninitialize();
                    held_ = false;
                }
            }

        private:
            bool held_ = false;
        };

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"
#endif
        // ------------------------------------------------------------
        // Target callbacks
        // ------------------------------------------------------------

        class DropTarget final : public IDropTarget
        {
        public:
            explicit DropTarget(DragDropState &state) noexcept
                : state_(&state)
            {
            }
            HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) noexcept override
            {
                if (!out)
                    return E_POINTER;
                *out = nullptr;
                if (id == IID_IUnknown || id == IID_IDropTarget)
                    *out = static_cast<IDropTarget *>(this);
                else
                    return E_NOINTERFACE;
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
                if (!count)
                    delete this;
                return count;
            }
            HRESULT STDMETHODCALLTYPE DragEnter(IDataObject *object, DWORD, POINTL point, DWORD *effect) noexcept override
            {
                if (!effect || !object)
                    return E_INVALIDARG;
                if (state_ == nullptr)
                {
                    *effect = DROPEFFECT_NONE;
                    return S_OK;
                }
                clearSession();
                try
                {
                    std::vector<DataTransfer::FormatIdentity> offered;
                    auto status = DataTransfer::formats(*object, offered);
                    if (!status.ok())
                    {
                        *effect = DROPEFFECT_NONE;
                        return S_OK;
                    }
                    auto portable = std::make_shared<std::vector<Transfer::Format>>();
                    portable->reserve(offered.size());
                    nativeFormats_.reserve(offered.size());
                    for (auto &format : offered)
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
                        active_ ? negotiateDragDropEffect(sourceEffects_, active_->allowedEffects, active_->preferredEffect) : DD::Effect::None;
                    currentRegion_ = active_ ? active_->id : DD::RegionId{};
                    routeDragDropEvent(*state_, DD::Events::Entered{session_, pos, currentRegion_, selected, formats_});
                    *effect = nativeEffects(selected);
                    return S_OK;
                }
                catch (...)
                {
                    clearSession();
                    *effect = DROPEFFECT_NONE;
                    return S_OK;
                }
            }
            HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL point, DWORD *effect) noexcept override
            {
                if (!effect || !formats_ || state_ == nullptr)
                {
                    if (effect)
                        *effect = DROPEFFECT_NONE;
                    return S_OK;
                }
                const auto pos = clientPosition(*state_, point);
                const auto *next = regionAt(*state_, pos, nativeFormats_);
                const auto region = next ? next->id : DD::RegionId{};
                const auto selected = next ? negotiateDragDropEffect(sourceEffects_, next->allowedEffects, next->preferredEffect) : DD::Effect::None;
                routeDragDropEvent(*state_, DD::Events::Moved{session_, pos, currentRegion_, region, selected, formats_});
                active_ = next;
                currentRegion_ = region;
                *effect = nativeEffects(selected);
                return S_OK;
            }
            HRESULT STDMETHODCALLTYPE DragLeave() noexcept override
            {
                if (state_ == nullptr)
                {
                    clearSession();
                    return S_OK;
                }
                if (session_.isValid())
                    routeDragDropEvent(*state_, DD::Events::Left{session_});
                clearSession();
                return S_OK;
            }
            HRESULT STDMETHODCALLTYPE Drop(IDataObject *object, DWORD, POINTL point, DWORD *effect) noexcept override
            {
                if (!effect || !object || !formats_ || state_ == nullptr)
                {
                    if (effect)
                        *effect = DROPEFFECT_NONE;
                    clearSession();
                    return S_OK;
                }
                const auto pos = clientPosition(*state_, point);
                const auto *region = regionAt(*state_, pos, nativeFormats_);
                const auto selected =
                    region ? negotiateDragDropEffect(sourceEffects_, region->allowedEffects, region->preferredEffect) : DD::Effect::None;
                if (selected == DD::Effect::None)
                {
                    *effect = DROPEFFECT_NONE;
                    clearSession();
                    return S_OK;
                }
                try
                {
                    Transfer::Payload payload;
                    for (std::size_t index = 0; index < region->formats.size(); ++index)
                        if (std::ranges::find(nativeFormats_, static_cast<CLIPFORMAT>(region->nativeFormats[index])) != nativeFormats_.end())
                        {
                            if (!materializationStatus().ok())
                            {
                                *effect = DROPEFFECT_NONE;
                                clearSession();
                                return S_OK;
                            }
                            Transfer::Item item;
                            auto status = DataTransfer::materialize(*object, region->formats[index], item);
                            if (!status.ok())
                            {
                                *effect = DROPEFFECT_NONE;
                                clearSession();
                                return S_OK;
                            }
                            payload.push_back(std::move(item));
                        }
                    if (payload.empty())
                    {
                        *effect = DROPEFFECT_NONE;
                        clearSession();
                        return S_OK;
                    }
                    routeDragDropEvent(*state_, DD::Events::Dropped{session_, pos, region->id, selected, std::move(payload)}, true);
                    *effect = nativeEffects(selected);
                    clearSession();
                    return S_OK;
                }
                catch (...)
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

        template <class T> [[nodiscard]] std::span<T> nativeSpan(T *data, std::size_t size) noexcept
        {
#if defined(__clang__)
#pragma clang unsafe_buffer_usage begin
#endif
            return {data, size};
#if defined(__clang__)
#pragma clang unsafe_buffer_usage end
#endif
        }

        // ------------------------------------------------------------
        // Source format enumeration
        // ------------------------------------------------------------

        class FormatEnumerator final : public IEnumFORMATETC
        {
        public:
            explicit FormatEnumerator(std::vector<FORMATETC> formats)
                : formats_(std::move(formats))
            {
            }
            HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) noexcept override
            {
                if (!out)
                    return E_POINTER;
                *out = nullptr;
                if (id == IID_IUnknown || id == IID_IEnumFORMATETC)
                    *out = static_cast<IEnumFORMATETC *>(this);
                else
                    return E_NOINTERFACE;
                AddRef();
                return S_OK;
            }
            ULONG STDMETHODCALLTYPE AddRef() noexcept override
            {
                return ++refs_;
            }
            ULONG STDMETHODCALLTYPE Release() noexcept override
            {
                auto n = --refs_;
                if (!n)
                    delete this;
                return n;
            }
            HRESULT STDMETHODCALLTYPE Next(ULONG count, FORMATETC *out, ULONG *fetched) noexcept override
            {
                if (fetched)
                    *fetched = 0;
                if (!out || (!fetched && count != 1))
                    return E_INVALIDARG;
                auto destination = nativeSpan(out, static_cast<std::size_t>(count));
                ULONG n = 0;
                while (n < count && index_ < formats_.size())
                    destination[n++] = formats_[index_++];
                if (fetched)
                    *fetched = n;
                return n == count ? S_OK : S_FALSE;
            }
            HRESULT STDMETHODCALLTYPE Skip(ULONG count) noexcept override
            {
                const std::size_t available = formats_.size() - index_;
                const std::size_t skipped = std::min<std::size_t>(available, count);
                index_ += skipped;
                return skipped == count ? S_OK : S_FALSE;
            }
            HRESULT STDMETHODCALLTYPE Reset() noexcept override
            {
                index_ = 0;
                return S_OK;
            }
            HRESULT STDMETHODCALLTYPE Clone(IEnumFORMATETC **out) noexcept override
            {
                if (!out)
                    return E_POINTER;
                *out = nullptr;
                try
                {
                    auto *copy = new FormatEnumerator(formats_);
                    copy->index_ = index_;
                    *out = copy;
                    return S_OK;
                }
                catch (...)
                {
                    return E_OUTOFMEMORY;
                }
            }

        private:
            std::atomic<ULONG> refs_{1};
            std::vector<FORMATETC> formats_;
            std::size_t index_ = 0;
        };

        // ------------------------------------------------------------
        // Source data object
        // ------------------------------------------------------------

        class DataObject final : public IDataObject
        {
        public:
            explicit DataObject(std::vector<DataTransfer::PreparedItem> items)
                : items_(std::move(items))
            {
            }
            HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) noexcept override
            {
                if (!out)
                    return E_POINTER;
                *out = nullptr;
                if (id == IID_IUnknown || id == IID_IDataObject)
                    *out = static_cast<IDataObject *>(this);
                else
                    return E_NOINTERFACE;
                AddRef();
                return S_OK;
            }
            ULONG STDMETHODCALLTYPE AddRef() noexcept override
            {
                return ++refs_;
            }
            ULONG STDMETHODCALLTYPE Release() noexcept override
            {
                auto n = --refs_;
                if (!n)
                    delete this;
                return n;
            }
            HRESULT STDMETHODCALLTYPE GetData(FORMATETC *format, STGMEDIUM *medium) noexcept override
            {
                if (!format || !medium)
                    return E_INVALIDARG;
                if (format->dwAspect != DVASPECT_CONTENT)
                    return DV_E_DVASPECT;
                if (format->ptd != nullptr)
                    return DV_E_DVTARGETDEVICE;
                if (format->lindex != -1)
                    return DV_E_LINDEX;
                if (!(format->tymed & TYMED_HGLOBAL))
                    return DV_E_TYMED;
                auto found = std::ranges::find_if(
                    items_,
                    [&](const auto &i)
                    {
                        return i.format == format->cfFormat;
                    });
                if (found == items_.end())
                    return DV_E_FORMATETC;
                HGLOBAL global = nullptr;
                auto status = DataTransfer::copyToGlobal(*found, global);
                if (!status.ok())
                    return status.code == IO::Types::ErrorCode::OutOfMemory ? E_OUTOFMEMORY : E_FAIL;
                medium->tymed = TYMED_HGLOBAL;
                medium->hGlobal = global;
                medium->pUnkForRelease = nullptr;
                return S_OK;
            }
            HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC *, STGMEDIUM *) noexcept override
            {
                return DATA_E_FORMATETC;
            }
            HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC *f) noexcept override
            {
                if (!f)
                    return E_INVALIDARG;
                if (f->dwAspect != DVASPECT_CONTENT)
                    return DV_E_DVASPECT;
                if (f->ptd != nullptr)
                    return DV_E_DVTARGETDEVICE;
                if (f->lindex != -1)
                    return DV_E_LINDEX;
                if (!(f->tymed & TYMED_HGLOBAL))
                    return DV_E_TYMED;
                return std::ranges::any_of(
                           items_,
                           [&](const auto &i)
                           {
                               return i.format == f->cfFormat && (f->tymed & TYMED_HGLOBAL);
                           })
                           ? S_OK
                           : DV_E_FORMATETC;
            }
            HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC *, FORMATETC *out) noexcept override
            {
                if (out)
                    out->ptd = nullptr;
                return E_NOTIMPL;
            }
            HRESULT STDMETHODCALLTYPE SetData(FORMATETC *, STGMEDIUM *, BOOL) noexcept override
            {
                return E_NOTIMPL;
            }
            HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD direction, IEnumFORMATETC **out) noexcept override
            {
                if (!out)
                    return E_POINTER;
                *out = nullptr;
                if (direction != DATADIR_GET)
                    return E_NOTIMPL;
                try
                {
                    std::vector<FORMATETC> formats;
                    formats.reserve(items_.size());
                    for (const auto &i : items_)
                        formats.push_back({i.format, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL});
                    *out = new FormatEnumerator(std::move(formats));
                    return S_OK;
                }
                catch (...)
                {
                    return E_OUTOFMEMORY;
                }
            }
            HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC *, DWORD, IAdviseSink *, DWORD *) noexcept override
            {
                return OLE_E_ADVISENOTSUPPORTED;
            }
            HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) noexcept override
            {
                return OLE_E_ADVISENOTSUPPORTED;
            }
            HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA **) noexcept override
            {
                return OLE_E_ADVISENOTSUPPORTED;
            }

        private:
            std::atomic<ULONG> refs_{1};
            std::vector<DataTransfer::PreparedItem> items_;
        };

        // ------------------------------------------------------------
        // Source termination
        // ------------------------------------------------------------

        class DropSource final : public IDropSource
        {
        public:
            explicit DropSource(DD::TriggerButton button)
                : mask_(
                      button == DD::TriggerButton::Left    ? MK_LBUTTON
                      : button == DD::TriggerButton::Right ? MK_RBUTTON
                                                           : MK_MBUTTON)
            {
            }
            HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) noexcept override
            {
                if (!out)
                    return E_POINTER;
                *out = nullptr;
                if (id == IID_IUnknown || id == IID_IDropSource)
                    *out = static_cast<IDropSource *>(this);
                else
                    return E_NOINTERFACE;
                AddRef();
                return S_OK;
            }
            ULONG STDMETHODCALLTYPE AddRef() noexcept override
            {
                return ++refs_;
            }
            ULONG STDMETHODCALLTYPE Release() noexcept override
            {
                auto n = --refs_;
                if (!n)
                    delete this;
                return n;
            }
            HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escape, DWORD keys) noexcept override
            {
                if (escape)
                    return DRAGDROP_S_CANCEL;
                if (!(keys & mask_))
                    return DRAGDROP_S_DROP;
                return S_OK;
            }
            HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) noexcept override
            {
                return DRAGDROP_S_USEDEFAULTCURSORS;
            }

        private:
            std::atomic<ULONG> refs_{1};
            DWORD mask_ = 0;
        };
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
    } // namespace

    // ------------------------------------------------------------
    // Target lifecycle
    // ------------------------------------------------------------

    void DragDropDataDeleter::operator()(DragDropData *data) const noexcept
    {
        // Apartment-affine cleanup is explicit in closeDragDropTarget(). A record that
        // still owns OLE resources must be retained, never finalized by this deleter.
        if (data == nullptr)
            return;
        if (data->registered)
            return;
        if (data->target != nullptr)
            data->target->Release();
        if (data->oleHeld)
            OleUninitialize();
        delete data;
    }

    namespace
    {
        [[nodiscard]] DragDropState *targetForWindow(const WindowState &window) noexcept
        {
            const auto &targets = dispatcher().dragDropTargets;
            if (!targets)
                return nullptr;
            const auto found = std::ranges::find_if(
                *targets,
                [&](const DragDropState *target)
                {
                    return target != nullptr && target->windowId == window.id;
                });
            return found == targets->end() ? nullptr : *found;
        }

        void unregisterTarget(DragDropState &state) noexcept
        {
            auto &targets = dispatcher().dragDropTargets;
            if (!targets)
                return;
            targets->erase(std::remove(targets->begin(), targets->end(), &state), targets->end());
            if (targets->empty())
                targets.reset();
        }
    } // namespace

    IO::Types::Status prepareDragDropRegions(std::vector<DragDropRegion> &regions) noexcept
    {
        try
        {
            for (auto &region : regions)
            {
                std::unordered_set<CLIPFORMAT> identities;
                std::vector<std::uint32_t> normalized;
                normalized.reserve(region.formats.size());
                for (const auto &format : region.formats)
                {
                    IO::Types::Status status;
                    const CLIPFORMAT native = DataTransfer::nativeFormat(format, status);
                    if (!status.ok())
                        return status;
                    if (!identities.insert(native).second)
                        return IO::makeStatus(IO::Types::ErrorCode::InvalidArgument);
                    normalized.push_back(native);
                }
                region.nativeFormats = std::move(normalized);
            }
            return IO::successStatus();
        }
        catch (const std::bad_alloc &)
        {
            return IO::makeStatus(IO::Types::ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return IO::makeStatus(IO::Types::ErrorCode::Unknown);
        }
    }

    IO::Types::Status openDragDropTarget(DragDropState &state, WindowState &window) noexcept
    {
        try
        {
            Dispatcher &current = dispatcher();
            if (targetForWindow(window) != nullptr)
                return IO::makeStatus(IO::Types::ErrorCode::ResourceBusy);
            if (!current.dragDropTargets)
                current.dragDropTargets = std::make_unique<std::vector<DragDropState *>>();
            current.dragDropTargets->push_back(&state);
            auto data = std::unique_ptr<DragDropData, DragDropDataDeleter>(new DragDropData{});
            data->window = window.platform->handle;
            data->ownerThreadId = window.platform->ownerThreadId;
            state.ownerNativeThreadId = data->ownerThreadId;
            if (Detail::consumeFailure(TestHooks::FailurePoint::DragDropOleInitialization))
            {
                unregisterTarget(state);
                return IO::makeStatus(IO::Types::ErrorCode::OpenFailed);
            }
            const HRESULT init = OleInitialize(nullptr);
            if (init != S_OK && init != S_FALSE)
            {
                unregisterTarget(state);
                return IO::makeStatus(init == RPC_E_CHANGED_MODE ? IO::Types::ErrorCode::ResourceBusy : IO::Types::ErrorCode::OpenFailed, init);
            }
            data->oleHeld = true;
            data->target = new DropTarget(state);
            const HRESULT hr =
                Detail::consumeFailure(TestHooks::FailurePoint::DragDropRegistration) ? E_FAIL : RegisterDragDrop(data->window, data->target);
            if (FAILED(hr))
            {
                data->target->Release();
                data->target = nullptr;
                OleUninitialize();
                data->oleHeld = false;
                unregisterTarget(state);
                return IO::makeStatus(hr == DRAGDROP_E_ALREADYREGISTERED ? IO::Types::ErrorCode::ResourceBusy : IO::Types::ErrorCode::OpenFailed, hr);
            }
            data->registered = true;
            state.platform = std::move(data);
            return IO::successStatus();
        }
        catch (const std::bad_alloc &)
        {
            unregisterTarget(state);
            return IO::makeStatus(IO::Types::ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            unregisterTarget(state);
            return IO::makeStatus(IO::Types::ErrorCode::Unknown);
        }
    }
    CloseResult closeDragDropTarget(DragDropState &state) noexcept
    {
        if (!state.platform)
            return {IO::successStatus(), true};
        if (state.platform->ownerThreadId != GetCurrentThreadId())
            return {IO::makeStatus(IO::Types::ErrorCode::ResourceBusy), false};
        HRESULT hr = S_OK;
        if (state.platform->registered)
            hr = (Detail::consumeFailure(TestHooks::FailurePoint::DragDropRevocation) || Detail::consumeDragDropRevocationFailure())
                     ? E_FAIL
                     : RevokeDragDrop(state.platform->window);
        if (FAILED(hr) && hr != DRAGDROP_E_NOTREGISTERED)
            return {IO::makeStatus(IO::Types::ErrorCode::CloseFailed, hr), false};
        state.platform->registered = false;
        if (state.platform->target)
        {
            static_cast<DropTarget *>(state.platform->target)->detach();
            state.platform->target->Release();
            state.platform->target = nullptr;
        }
        if (state.platform->oleHeld)
        {
            OleUninitialize();
            state.platform->oleHeld = false;
        }
        unregisterTarget(state);
        state.platform.reset();
        return {IO::successStatus(), true};
    }
    bool closeDragDropTargetBestEffort(DragDropState &state) noexcept
    {
        if (!state.platform)
            return true;
        return state.platform->ownerThreadId == GetCurrentThreadId() && closeDragDropTarget(state).resourceClosed;
    }

    void finalizeDragDropTargetForDispatcherExit(DragDropState &state) noexcept
    {
        constexpr std::size_t retryCount = 4;
        for (std::size_t attempt = 0; attempt < retryCount; ++attempt)
        {
            if (closeDragDropTarget(state).resourceClosed)
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
        if (state.platform)
        {
            if (state.platform->target != nullptr)
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
    bool dragDropTargetOwnedByCurrentThread(const DragDropState &state) noexcept
    {
        return state.ownerNativeThreadId != 0 && state.ownerNativeThreadId == GetCurrentThreadId();
    }
    bool hasLiveDragDropTarget(const DragDropState &state) noexcept
    {
        return state.platform && state.platform->registered && IsWindow(state.platform->window);
    }
    bool hasNativeDragDropResources(const DragDropState &state) noexcept
    {
        return state.platform != nullptr;
    }
    bool hasDragDropTarget(const WindowState &window) noexcept
    {
        return targetForWindow(window) != nullptr;
    }
    void routeDragDropEvent(DragDropState &state, DD::Events::Payload data, bool terminal) noexcept
    {
        const std::uint64_t droppedBefore = state.droppedEvents;
        const bool queued = enqueueDragDropEvent(state, std::move(data), terminal);
        Dispatcher &current = dispatcher();
        if (current.activeResult != nullptr)
        {
            current.activeResult->eventsDropped += state.droppedEvents - droppedBefore;
            if (queued)
                ++current.activeResult->eventsQueued;
        }
    }
    bool windowClosingDragDrop(WindowState &window, bool nativeDestroyed) noexcept
    {
        DragDropState *target = targetForWindow(window);
        if (target == nullptr)
            return true;
        const CloseResult closed = closeDragDropTarget(*target);
        if (closed.resourceClosed || nativeDestroyed)
        {
            target->window = nullptr;
            target->nativeDestroyedPendingFinalize = true;
        }
        return closed.resourceClosed;
    }

    // ------------------------------------------------------------
    // Source operation and validation hooks
    // ------------------------------------------------------------

    DD::Result beginNativeDrag(WindowState &window, const DD::Description &description) noexcept
    {
        DD::Result result;
        if (sourceDragActive)
        {
            result.status = IO::makeStatus(IO::Types::ErrorCode::ResourceBusy);
            return result;
        }
        if (description.items.empty() || !validEffects(description.allowedEffects) ||
            static_cast<unsigned>(description.triggerButton) > static_cast<unsigned>(DD::TriggerButton::Middle))
        {
            result.status = IO::makeStatus(IO::Types::ErrorCode::InvalidArgument);
            return result;
        }
        const int key = description.triggerButton == DD::TriggerButton::Left    ? VK_LBUTTON
                        : description.triggerButton == DD::TriggerButton::Right ? VK_RBUTTON
                                                                                : VK_MBUTTON;
        if ((GetAsyncKeyState(key) & 0x8000) == 0)
        {
            result.status = IO::makeStatus(IO::Types::ErrorCode::InvalidArgument);
            return result;
        }
        std::vector<DataTransfer::PreparedItem> prepared;
        result.status = prepareSource(description, prepared);
        if (!result.status.ok())
            return result;
        try
        {
            OleLease ole;
            result.status = ole.acquire();
            if (!result.status.ok())
                return result;
            auto *object = new DataObject(std::move(prepared));
            DropSource *source = nullptr;
            try
            {
                source = new DropSource(description.triggerButton);
            }
            catch (...)
            {
                object->Release();
                throw;
            }
            sourceDragActive = true;
            DWORD performed = DROPEFFECT_NONE;
            const HRESULT hr = DoDragDrop(object, source, nativeEffects(description.allowedEffects), &performed);
            sourceDragActive = false;
            source->Release();
            object->Release();
            if (!window.platform || !IsWindow(window.platform->handle))
            {
                result.status = IO::makeStatus(IO::Types::ErrorCode::NativeFailure);
                return result;
            }
            if (hr == DRAGDROP_S_CANCEL)
            {
                result.status = IO::successStatus();
                result.outcome = DD::Outcome::Cancelled;
                result.effect = DD::Effect::None;
            }
            else if (hr == DRAGDROP_S_DROP)
                result = droppedSourceResult(performed, description.allowedEffects);
            else
                result.status = IO::makeStatus(IO::Types::ErrorCode::NativeFailure, hr);
            return result;
        }
        catch (const std::bad_alloc &)
        {
            sourceDragActive = false;
            result.status = IO::makeStatus(IO::Types::ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            sourceDragActive = false;
            result.status = IO::makeStatus(IO::Types::ErrorCode::Unknown);
        }
        return result;
    }

    IO::Types::Status prepareDragDropSource(const DD::Description &description) noexcept
    {
        std::vector<DataTransfer::PreparedItem> prepared;
        return prepareSource(description, prepared);
    }

    IO::Types::Status testDragDropOleInitialization() noexcept
    {
        OleLease lease;
        return lease.acquire();
    }

    IO::Types::Status testDragDropMaterialization() noexcept
    {
        return materializationStatus();
    }

    DD::Result testDroppedDragDropSourceResult(DD::Effect performed, DD::Effect allowed) noexcept
    {
        return droppedSourceResult(nativeEffects(performed), allowed);
    }

    bool testDragDropComContracts() noexcept
    {
        try
        {
            std::vector<DataTransfer::PreparedItem> items{{CF_UNICODETEXT, {std::byte{}, std::byte{}}}};
            auto *object = new DataObject(std::move(items));
            FORMATETC valid{CF_UNICODETEXT, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
            FORMATETC invalidAspect = valid;
            invalidAspect.dwAspect = DVASPECT_THUMBNAIL;
            FORMATETC invalidIndex = valid;
            invalidIndex.lindex = 0;
            FORMATETC invalidMedium = valid;
            invalidMedium.tymed = TYMED_FILE;
            DVTARGETDEVICE device{};
            FORMATETC invalidDevice = valid;
            invalidDevice.ptd = &device;
            const bool queryContracts = object->QueryGetData(&valid) == S_OK && object->QueryGetData(&invalidAspect) == DV_E_DVASPECT &&
                                        object->QueryGetData(&invalidIndex) == DV_E_LINDEX && object->QueryGetData(&invalidMedium) == DV_E_TYMED &&
                                        object->QueryGetData(&invalidDevice) == DV_E_DVTARGETDEVICE;
            IEnumFORMATETC *enumerator = reinterpret_cast<IEnumFORMATETC *>(static_cast<std::uintptr_t>(1));
            const bool unsupported = object->EnumFormatEtc(DATADIR_SET, &enumerator) == E_NOTIMPL && enumerator == nullptr;
            const bool enumerated = object->EnumFormatEtc(DATADIR_GET, &enumerator) == S_OK && enumerator != nullptr;
            bool enumerationContracts = false;
            if (enumerated)
            {
                FORMATETC out{};
                ULONG fetched = 0;
                const bool next = enumerator->Next(1, &out, &fetched) == S_OK && fetched == 1 && out.cfFormat == CF_UNICODETEXT;
                const bool exhausted = enumerator->Skip(1) == S_FALSE;
                const bool reset = enumerator->Reset() == S_OK;
                IEnumFORMATETC *clone = nullptr;
                const bool cloned = enumerator->Clone(&clone) == S_OK && clone != nullptr;
                FORMATETC cloneOut{};
                const bool clonePosition = cloned && clone->Next(1, &cloneOut, nullptr) == S_OK && cloneOut.cfFormat == CF_UNICODETEXT;
                if (clone != nullptr)
                    clone->Release();
                enumerator->Release();
                enumerationContracts = next && exhausted && reset && clonePosition;
            }
            object->Release();
            return queryContracts && unsupported && enumerationContracts;
        }
        catch (...)
        {
            return false;
        }
    }

    Types::Events::PumpResult testRouteDragDropDuringPump(DragDropState &state, DD::Events::Payload data, bool terminal) noexcept
    {
        Types::Events::PumpResult result;
        Dispatcher &current = dispatcher();
        Types::Events::PumpResult *previous = current.activeResult;
        current.activeResult = &result;
        routeDragDropEvent(state, std::move(data), terminal);
        current.activeResult = previous;
        return result;
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
            for (const auto &view : offered)
            {
                Transfer::Format format{view.kind, std::string(view.customName)};
                IO::Types::Status status;
                const CLIPFORMAT identity = DataTransfer::nativeFormat(format, status);
                if (!status.ok())
                    return {};
                native.push_back(identity);
            }
            const DragDropRegion *matched = regionAt(state, position, native);
            return matched == nullptr ? DD::RegionId{} : matched->id;
        }
        catch (...)
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
        for (DragDropState *state = current.deferredDragDropCleanupHead.get(); state != nullptr; state = state->deferredCleanupNext)
            ++count;
        return count;
    }
} // namespace GameWIP::Desktop::Detail::Platform
