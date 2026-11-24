/// @file win32_drag_drop_source.cpp
/// @brief Win32 OLE source objects and synchronous drag operation.

#include "desktop/platform/drag_drop.h"
#include "desktop/internal/desktop_test_hooks.h"
#include "desktop/platform/win32/internal/test_hooks.h"
#include "desktop/platform/win32/internal/win32_data_transfer.h"
#include "desktop/platform/win32/internal/win32_drag_drop.h"
#include "desktop/platform/win32/internal/win32_window_backend.h"

#include <ole2.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <new>
#include <span>
#include <unordered_set>
#include <vector>

namespace GameWIP::Desktop::Platform
{
    namespace DD = Types::DragDrop;

    namespace
    {
        thread_local bool sourceDragActive = false;

        /// ------------------------------------------------------------
        /// Source validation and native effects
        /// ------------------------------------------------------------

        /// @brief Validates the effect reported by OLE after a source drag completes.
        [[nodiscard]] DD::Result droppedSourceResult(DWORD performed, DD::Effect allowed) noexcept
        {
            constexpr DWORD knownEffects = DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK;
            DD::Result result;
            const DWORD native = performed & knownEffects;
            const DD::Effect effect = portableEffects(performed);

            if((performed & ~knownEffects) != 0 || (native & (native - 1U)) != 0 || (effect & allowed) != effect)
            {
                result.status = IO::makeStatus(IO::Types::ErrorCode::NativeFailure, E_UNEXPECTED);
            }
            else
            {
                result.status = IO::successStatus();
                result.outcome = native == 0 ? DD::Outcome::Cancelled : DD::Outcome::Dropped;
                result.effect = effect;
            }

            return result;
        }

        /// @brief Tests whether an effect mask contains only supported nonzero effects.
        [[nodiscard]] bool validEffects(DD::Effect effects) noexcept
        {
            constexpr auto all = DD::Effect::Copy | DD::Effect::Move | DD::Effect::Link;
            return effects != DD::Effect::None && (effects & all) == effects;
        }

        /// @brief Materializes and validates every source item before entering OLE.
        [[nodiscard]] IO::Types::Status prepareSource(const DD::Description &description, std::vector<DataTransfer::PreparedItem> &prepared) noexcept
        {
            if(description.items.empty() || !validEffects(description.effects.allowed) ||
               static_cast<unsigned>(description.triggerButton) > static_cast<unsigned>(DD::TriggerButton::Middle))
            {
                return IO::makeStatus(IO::Types::ErrorCode::InvalidArgument);
            }

            try
            {
                prepared.reserve(description.items.size());
                std::unordered_set<CLIPFORMAT> formats;

                for(const auto &item : description.items)
                {
                    if(Internal::consumeFailure(GameWIP::Desktop::Internal::TestHooks::FailurePoint::DragDropPreparation))
                    {
                        return IO::makeStatus(IO::Types::ErrorCode::OutOfMemory);
                    }

                    DataTransfer::PreparedItem value;
                    IO::Types::Status status = DataTransfer::prepare(item, value);

                    if(!status.ok())
                    {
                        return status;
                    }

                    if(!formats.insert(value.format).second)
                    {
                        return IO::makeStatus(IO::Types::ErrorCode::InvalidArgument);
                    }

                    prepared.push_back(std::move(value));
                }

                return IO::successStatus();
            }
            catch(const std::bad_alloc &)
            {
                return IO::makeStatus(IO::Types::ErrorCode::OutOfMemory);
            }
            catch(...)
            {
                return IO::makeStatus(IO::Types::ErrorCode::Unknown);
            }
        }

        /// ------------------------------------------------------------
        /// OLE lifetime
        /// ------------------------------------------------------------

        /// @brief Owns the calling apartment's OLE initialization lease.
        class OleLease final
        {
        public:
            [[nodiscard]] IO::Types::Status acquire() noexcept
            {
                if(Internal::consumeFailure(GameWIP::Desktop::Internal::TestHooks::FailurePoint::DragDropOleInitialization))
                {
                    return IO::makeStatus(IO::Types::ErrorCode::OpenFailed);
                }

                const HRESULT hr = OleInitialize(nullptr);

                if(hr == S_OK || hr == S_FALSE)
                {
                    held_ = true;
                    return IO::successStatus();
                }

                return IO::makeStatus(hr == RPC_E_CHANGED_MODE ? IO::Types::ErrorCode::ResourceBusy : IO::Types::ErrorCode::OpenFailed, hr);
            }

            ~OleLease() noexcept
            {
                if(held_)
                {
                    OleUninitialize();
                }
            }

            void release() noexcept
            {
                if(held_)
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

        /// @brief Creates a span over COM-provided FORMATETC storage.
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

        /// ------------------------------------------------------------
        /// Source format enumeration
        /// ------------------------------------------------------------

        /// @brief COM enumerator over the formats exposed by the source data object.
        class FormatEnumerator final : public IEnumFORMATETC
        {
        public:
            explicit FormatEnumerator(std::vector<FORMATETC> formats)
                : formats_(std::move(formats))
            {
            }
            HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) noexcept override
            {
                if(!out)
                {
                    return E_POINTER;
                }
                *out = nullptr;

                if(id == IID_IUnknown || id == IID_IEnumFORMATETC)
                {
                    *out = static_cast<IEnumFORMATETC *>(this);
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
                return ++refs_;
            }

            ULONG STDMETHODCALLTYPE Release() noexcept override
            {
                auto n = --refs_;

                if(!n)
                {
                    delete this;
                }

                return n;
            }

            HRESULT STDMETHODCALLTYPE Next(ULONG count, FORMATETC *out, ULONG *fetched) noexcept override
            {
                if(fetched)
                {
                    *fetched = 0;
                }

                if(!out || (!fetched && count != 1))
                {
                    return E_INVALIDARG;
                }
                auto destination = nativeSpan(out, static_cast<std::size_t>(count));
                ULONG n = 0;

                while(n < count && index_ < formats_.size())
                {
                    destination[n++] = formats_[index_++];
                }

                if(fetched)
                {
                    *fetched = n;
                }

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
                if(!out)
                {
                    return E_POINTER;
                }
                *out = nullptr;
                try
                {
                    auto *copy = new FormatEnumerator(formats_);
                    copy->index_ = index_;
                    *out = copy;
                    return S_OK;
                }
                catch(...)
                {
                    return E_OUTOFMEMORY;
                }
            }

        private:
            std::atomic<ULONG> refs_{1};
            std::vector<FORMATETC> formats_;
            std::size_t index_ = 0;
        };

        /// ------------------------------------------------------------
        /// Source data object
        /// ------------------------------------------------------------

        /// @brief COM data object that serves prepared HGLOBAL payloads to OLE.
        class DataObject final : public IDataObject
        {
        public:
            explicit DataObject(std::vector<DataTransfer::PreparedItem> items)
                : items_(std::move(items))
            {
            }
            HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) noexcept override
            {
                if(!out)
                {
                    return E_POINTER;
                }
                *out = nullptr;

                if(id == IID_IUnknown || id == IID_IDataObject)
                {
                    *out = static_cast<IDataObject *>(this);
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
                return ++refs_;
            }

            ULONG STDMETHODCALLTYPE Release() noexcept override
            {
                auto n = --refs_;

                if(!n)
                {
                    delete this;
                }

                return n;
            }

            HRESULT STDMETHODCALLTYPE GetData(FORMATETC *format, STGMEDIUM *medium) noexcept override
            {
                if(!format || !medium)
                {
                    return E_INVALIDARG;
                }

                if(format->dwAspect != DVASPECT_CONTENT)
                {
                    return DV_E_DVASPECT;
                }

                if(format->ptd != nullptr)
                {
                    return DV_E_DVTARGETDEVICE;
                }

                if(format->lindex != -1)
                {
                    return DV_E_LINDEX;
                }

                if(!(format->tymed & TYMED_HGLOBAL))
                {
                    return DV_E_TYMED;
                }

                auto found = std::ranges::find_if(
                    items_,
                    [&](const auto &i)
                    {
                        return i.format == format->cfFormat;
                    });

                if(found == items_.end())
                {
                    return DV_E_FORMATETC;
                }

                HGLOBAL global = nullptr;
                auto status = DataTransfer::copyToGlobal(*found, global);

                if(!status.ok())
                {
                    return status.code == IO::Types::ErrorCode::OutOfMemory ? E_OUTOFMEMORY : E_FAIL;
                }

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
                if(!f)
                {
                    return E_INVALIDARG;
                }

                if(f->dwAspect != DVASPECT_CONTENT)
                {
                    return DV_E_DVASPECT;
                }

                if(f->ptd != nullptr)
                {
                    return DV_E_DVTARGETDEVICE;
                }

                if(f->lindex != -1)
                {
                    return DV_E_LINDEX;
                }

                if(!(f->tymed & TYMED_HGLOBAL))
                {
                    return DV_E_TYMED;
                }

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
                if(out)
                {
                    out->ptd = nullptr;
                }

                return E_NOTIMPL;
            }

            HRESULT STDMETHODCALLTYPE SetData(FORMATETC *, STGMEDIUM *, BOOL) noexcept override
            {
                return E_NOTIMPL;
            }

            HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD direction, IEnumFORMATETC **out) noexcept override
            {
                if(!out)
                {
                    return E_POINTER;
                }
                *out = nullptr;

                if(direction != DATADIR_GET)
                {
                    return E_NOTIMPL;
                }

                try
                {
                    std::vector<FORMATETC> formats;
                    formats.reserve(items_.size());

                    for(const auto &i : items_)
                    {
                        formats.push_back({i.format, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL});
                    }
                    *out = new FormatEnumerator(std::move(formats));
                    return S_OK;
                }
                catch(...)
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

        /// ------------------------------------------------------------
        /// Source termination
        /// ------------------------------------------------------------

        /// @brief COM source policy that ends a drag on escape or trigger release.
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
                if(!out)
                {
                    return E_POINTER;
                }
                *out = nullptr;

                if(id == IID_IUnknown || id == IID_IDropSource)
                {
                    *out = static_cast<IDropSource *>(this);
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
                return ++refs_;
            }

            ULONG STDMETHODCALLTYPE Release() noexcept override
            {
                auto n = --refs_;

                if(!n)
                {
                    delete this;
                }

                return n;
            }

            HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escape, DWORD keys) noexcept override
            {
                if(escape)
                {
                    return DRAGDROP_S_CANCEL;
                }

                if(!(keys & mask_))
                {
                    return DRAGDROP_S_DROP;
                }

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

    /// ------------------------------------------------------------
    /// Synchronous source drag
    /// ------------------------------------------------------------

    /// @brief Runs one synchronous OLE source drag and translates its outcome.
    DD::Result beginNativeDrag(WindowState &window, const DD::Description &description) noexcept
    {
        DD::Result result;

        // Reject invalid or nested source requests before preparing payload data.
        if(sourceDragActive)
        {
            result.status = IO::makeStatus(IO::Types::ErrorCode::ResourceBusy);
            return result;
        }

        if(description.items.empty() || !validEffects(description.effects.allowed) ||
           static_cast<unsigned>(description.triggerButton) > static_cast<unsigned>(DD::TriggerButton::Middle))
        {
            result.status = IO::makeStatus(IO::Types::ErrorCode::InvalidArgument);
            return result;
        }
        const int key = description.triggerButton == DD::TriggerButton::Left    ? VK_LBUTTON
                        : description.triggerButton == DD::TriggerButton::Right ? VK_RBUTTON
                                                                                : VK_MBUTTON;

        if((static_cast<std::uint16_t>(GetAsyncKeyState(key)) & 0x8000U) == 0)
        {
            result.status = IO::makeStatus(IO::Types::ErrorCode::InvalidArgument);
            return result;
        }

        std::vector<DataTransfer::PreparedItem> prepared;
        result.status = prepareSource(description, prepared);

        if(!result.status.ok())
        {
            return result;
        }

        try
        {
            // Keep the OLE apartment and data objects alive for the synchronous drag.
            OleLease ole;
            result.status = ole.acquire();

            if(!result.status.ok())
            {
                return result;
            }

            auto *object = new DataObject(std::move(prepared));
            DropSource *source = nullptr;
            try
            {
                source = new DropSource(description.triggerButton);
            }
            catch(...)
            {
                object->Release();
                throw;
            }

            sourceDragActive = true;
            DWORD performed = DROPEFFECT_NONE;
            const HRESULT hr = DoDragDrop(object, source, nativeEffects(description.effects.allowed), &performed);
            sourceDragActive = false;
            source->Release();
            object->Release();

            // Translate OLE's outcome only after confirming the source Window survived.
            if(!window.platform || !IsWindow(window.platform->handle))
            {
                result.status = IO::makeStatus(IO::Types::ErrorCode::NativeFailure);
                return result;
            }

            if(hr == DRAGDROP_S_CANCEL)
            {
                result.status = IO::successStatus();
                result.outcome = DD::Outcome::Cancelled;
                result.effect = DD::Effect::None;
            }
            else if(hr == DRAGDROP_S_DROP)
            {
                result = droppedSourceResult(performed, description.effects.allowed);
            }
            else
            {
                result.status = IO::makeStatus(IO::Types::ErrorCode::NativeFailure, hr);
            }

            return result;
        }
        catch(const std::bad_alloc &)
        {
            sourceDragActive = false;
            result.status = IO::makeStatus(IO::Types::ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            sourceDragActive = false;
            result.status = IO::makeStatus(IO::Types::ErrorCode::Unknown);
        }

        return result;
    }
} // namespace GameWIP::Desktop::Platform

namespace GameWIP::Desktop::Internal::Win32
{
    using namespace GameWIP::Desktop::Platform;

    /// ------------------------------------------------------------
    /// Source test hooks
    /// ------------------------------------------------------------

    /// @brief Validates and materializes source data without starting OLE.
    IO::Types::Status prepareDragDropSource(const DD::Description &description) noexcept
    {
        std::vector<DataTransfer::PreparedItem> prepared;
        return prepareSource(description, prepared);
    }

    /// @brief Acquires one test OLE initialization lease.
    IO::Types::Status testDragDropOleInitialization() noexcept
    {
        OleLease lease;
        return lease.acquire();
    }

    /// @brief Checks native effect validation for a completed source drag.
    DD::Result testDroppedDragDropSourceResult(DD::Effect performed, DD::Effect allowed) noexcept
    {
        return droppedSourceResult(nativeEffects(performed), allowed);
    }

    /// @brief Checks IDataObject queries and format enumeration.
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

            if(enumerated)
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

                if(clone != nullptr)
                {
                    clone->Release();
                }
                enumerator->Release();
                enumerationContracts = next && exhausted && reset && clonePosition;
            }
            object->Release();
            return queryContracts && unsupported && enumerationContracts;
        }
        catch(...)
        {
            return false;
        }
    }
} // namespace GameWIP::Desktop::Internal::Win32
