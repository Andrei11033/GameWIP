/// @file win32_accessibility.cpp
/// @brief Manages snapshot-backed UI Automation providers and navigation.

#include "desktop/platform/win32/internal/win32_accessibility_provider.h"
#include "desktop/platform/win32/internal/win32_accessibility_helpers.h"

#include <array>
#include <bit>
#include <cmath>

namespace GameWIP::Desktop::Platform
{
    /// ------------------------------------------------------------
    /// Native state and snapshot queries
    /// ------------------------------------------------------------

    UiaSnapshot::~UiaSnapshot()
    {
        for(auto *provider : providers)
        {
            if(provider)
            {
                provider->Release();
            }
        }
    }

    UiaState::~UiaState()
    {
        for(auto [id, provider] : providers)
        {
            static_cast<void>(id);
            provider->Release();
        }
    }

    UiaQuery queryNode(const std::weak_ptr<AccessibilityRuntime> &runtime, A::NodeId id) noexcept
    {
        UiaQuery result;
        result.runtime = runtime.lock();

        if(!result.runtime || !result.runtime->live.load())
        {
            return result;
        }
        result.snapshot = result.runtime->snapshot.load();

        if(!result.snapshot)
        {
            return result;
        }
        result.index = result.snapshot->findIndex(id);

        if(result.index < result.snapshot->storage.nodes.size())
        {
            result.node = &result.snapshot->storage.nodes[result.index];
        }
        AccessibilityHostGeometry host;

        if(result.runtime->readGeometry(host))
        {
            result.host = host;
        }

        if(!result.runtime->live.load())
        {
            result.node = nullptr;
        }

        return result;
    }

    IRawElementProviderSimple *providerFor(const PublishedSnapshot &snapshot, A::NodeId id) noexcept
    {
        const auto index = snapshot.findIndex(id);
        const auto *native = static_cast<const UiaSnapshot *>(snapshot.native.get());
        return native && index < native->providers.size() ? native->providers[index] : nullptr;
    }

    /// ------------------------------------------------------------
    /// Content exposure and cached bounds
    /// ------------------------------------------------------------

    bool valueHidden(const A::Node &n) noexcept
    {
        return n.states.contains(A::State::Sensitive) || n.states.contains(A::State::Password) || n.role == A::Role::PasswordField ||
               n.exposure == A::Exposure::RedactValue || n.exposure == A::Exposure::RedactContent;
    }

    bool textHidden(const A::Node &n) noexcept
    {
        return n.states.contains(A::State::Sensitive) || n.states.contains(A::State::Password) || n.role == A::Role::PasswordField ||
               n.exposure == A::Exposure::RedactText || n.exposure == A::Exposure::RedactContent;
    }

    bool contentReadOnly(const A::Node &node) noexcept
    {
        return node.states.contains(A::State::ReadOnly) ||
               (!node.states.contains(A::State::Editable) && !node.actions.contains(A::ActionKind::SetValue));
    }

    UiaRect clientRect(const UiaQuery &query, A::Rect rect) noexcept
    {
        if(!query.host || !query.host->visible)
        {
            return {};
        }
        const auto &host = *query.host;
        const auto &layout = static_cast<const UiaSnapshot *>(query.snapshot->native.get())->layout[query.index];

        if(!layout.visible)
        {
            return {};
        }

        if(layout.clip)
        {
            rect = intersection(rect, *layout.clip);
        }
        rect = intersection(rect, {0, 0, host.width, host.height});
        return {host.x + rect.x * host.scaleX, host.y + rect.y * host.scaleY, rect.width * host.scaleX, rect.height * host.scaleY};
    }

    UiaRect nodeBounds(const UiaQuery &query) noexcept
    {
        auto rect = static_cast<const UiaSnapshot *>(query.snapshot->native.get())->layout[query.index].bounds;

        if(query.node->id == query.snapshot->storage.root && !query.node->geometry)
        {
            if(!query.host)
            {
                return {};
            }
            rect = {0, 0, query.host->width, query.host->height};
        }

        return clientRect(query, rect);
    }

    /// ------------------------------------------------------------
    /// UI Automation value and array helpers
    /// ------------------------------------------------------------

    HRESULT stringValue(std::u16string_view text, BSTR *out) noexcept
    {
        if(!out)
        {
            return E_POINTER;
        }
        *out = SysAllocStringLen(reinterpret_cast<const OLECHAR *>(text.data()), static_cast<UINT>(text.size()));
        return *out ? S_OK : E_OUTOFMEMORY;
    }

    HRESULT nodeArray(const PublishedSnapshot &snapshot, std::span<const A::NodeId> ids, SAFEARRAY **out) noexcept
    {
        if(!out)
        {
            return E_POINTER;
        }
        *out = nullptr;
        auto *array = SafeArrayCreateVector(VT_UNKNOWN, 0, static_cast<ULONG>(ids.size()));

        if(!array)
        {
            return E_OUTOFMEMORY;
        }

        for(std::size_t i = 0; i < ids.size(); ++i)
        {
            LONG index = static_cast<LONG>(i);
            IUnknown *provider = providerFor(snapshot, ids[i]);
            const auto hr = SafeArrayPutElement(array, &index, provider);

            if(FAILED(hr))
            {
                SafeArrayDestroy(array);
                return hr;
            }
        }
        *out = array;
        return S_OK;
    }

    /// ------------------------------------------------------------
    /// Native action transport
    /// ------------------------------------------------------------

    HRESULT action(const UiaQuery &q, A::ActionRequest request) noexcept
    {
        if(!q || !q.runtime->live.load())
        {
            return kUnavailable;
        }

        request.node = q.node->id;
        request.observedGeneration = q.snapshot->storage.generation;

        switch(submitAccessibilityAction(*q.runtime, std::move(request)).acceptance)
        {
        case A::ActionAcceptance::Accepted:
            return S_OK;
        case A::ActionAcceptance::Closed:
            return kUnavailable;
        case A::ActionAcceptance::Unsupported:
            return kUnsupported;
        case A::ActionAcceptance::Invalid:
            return E_INVALIDARG;
        case A::ActionAcceptance::QueueFull:
            return HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA);
        case A::ActionAcceptance::Rejected:
            return static_cast<HRESULT>(UIA_E_ELEMENTNOTENABLED);
        }

        return E_FAIL;
    }

    /// ------------------------------------------------------------
    /// Provider identity and interfaces
    /// ------------------------------------------------------------

    NodeProvider::NodeProvider(const std::shared_ptr<AccessibilityRuntime> &runtime, A::NodeId id, bool root) noexcept
        : runtime_(runtime)
        , id_(id)
        , root_(root)
        , text_(*this)
    {
    }

    UiaQuery NodeProvider::query() const noexcept
    {
        return queryNode(runtime_, id_);
    }

    HRESULT NodeProvider::QueryInterface(REFIID iid, void **out)
    {
        if(!out)
        {
            return E_POINTER;
        }
        *out = nullptr;

        if(IsEqualIID(iid, IID_IUnknown) || IsEqualIID(iid, __uuidof(IRawElementProviderSimple)))
        {
            *out = static_cast<IRawElementProviderSimple *>(this);
        }
#define DESKTOP_UIA_INTERFACE(Type) else if(IsEqualIID(iid, __uuidof(Type))) *out = static_cast<Type *>(this)
        DESKTOP_UIA_INTERFACE(IRawElementProviderFragment);
        else if(root_ && IsEqualIID(iid, __uuidof(IRawElementProviderFragmentRoot)))
        {
            *out = static_cast<IRawElementProviderFragmentRoot *>(this);
        }
        DESKTOP_UIA_INTERFACE(IRawElementProviderAdviseEvents);
        DESKTOP_UIA_INTERFACE(IInvokeProvider);
        DESKTOP_UIA_INTERFACE(IValueProvider);
        DESKTOP_UIA_INTERFACE(IRangeValueProvider);
        DESKTOP_UIA_INTERFACE(IToggleProvider);
        DESKTOP_UIA_INTERFACE(IExpandCollapseProvider);
        DESKTOP_UIA_INTERFACE(ISelectionProvider);
        DESKTOP_UIA_INTERFACE(ISelectionProvider2);
        DESKTOP_UIA_INTERFACE(ISelectionItemProvider);
        DESKTOP_UIA_INTERFACE(IScrollProvider);
        DESKTOP_UIA_INTERFACE(IScrollItemProvider);
        DESKTOP_UIA_INTERFACE(IGridProvider);
        DESKTOP_UIA_INTERFACE(IGridItemProvider);
        DESKTOP_UIA_INTERFACE(ITableProvider);
        DESKTOP_UIA_INTERFACE(ITableItemProvider);
        DESKTOP_UIA_INTERFACE(IVirtualizedItemProvider);
        DESKTOP_UIA_INTERFACE(IItemContainerProvider);
        DESKTOP_UIA_INTERFACE(IAnnotationProvider);
        DESKTOP_UIA_INTERFACE(ITextChildProvider);
        else if(IsEqualIID(iid, __uuidof(ITextProvider)) || IsEqualIID(iid, __uuidof(ITextProvider2)))
        {
            *out = static_cast<ITextProvider2 *>(&text_);
        }
#undef DESKTOP_UIA_INTERFACE
        if(!*out)
        {
            return E_NOINTERFACE;
        }

        AddRef();
        return S_OK;
    }

    ULONG NodeProvider::AddRef()
    {
        return references_.fetch_add(1) + 1;
    }

    ULONG NodeProvider::Release()
    {
        const auto remaining = references_.fetch_sub(1) - 1;

        if(remaining == 0)
        {
            delete this;
        }

        return remaining;
    }

    HRESULT NodeProvider::get_ProviderOptions(ProviderOptions *out)
    {
        if(!out)
        {
            return E_POINTER;
        }
        *out = static_cast<ProviderOptions>(
            static_cast<std::uint32_t>(ProviderOptions_ServerSideProvider) | static_cast<std::uint32_t>(ProviderOptions_ProviderOwnsSetFocus) |
            static_cast<std::uint32_t>(ProviderOptions_UseComThreading));
        return query() ? S_OK : kUnavailable;
    }

    HRESULT NodeProvider::GetPatternProvider(PATTERNID id, IUnknown **out)
    {
        if(!out)
        {
            return E_POINTER;
        }
        *out = nullptr;
        const auto q = query();

        if(!q)
        {
            return kUnavailable;
        }

        if(!(id == UIA_AnnotationPatternId || id == UIA_TextChildPatternId ? annotationSupported(q) : patternSupported(*q.node, id)))
        {
            return S_OK;
        }

        switch(id)
        {
        case UIA_InvokePatternId:
            *out = static_cast<IInvokeProvider *>(this);
            break;
        case UIA_ValuePatternId:
            *out = static_cast<IValueProvider *>(this);
            break;
        case UIA_RangeValuePatternId:
            *out = static_cast<IRangeValueProvider *>(this);
            break;
        case UIA_TogglePatternId:
            *out = static_cast<IToggleProvider *>(this);
            break;
        case UIA_ExpandCollapsePatternId:
            *out = static_cast<IExpandCollapseProvider *>(this);
            break;
        case UIA_SelectionPatternId:
            *out = static_cast<ISelectionProvider *>(this);
            break;
        case kSelection2PatternId:
            *out = static_cast<ISelectionProvider2 *>(this);
            break;
        case UIA_SelectionItemPatternId:
            *out = static_cast<ISelectionItemProvider *>(this);
            break;
        case UIA_ScrollPatternId:
            *out = static_cast<IScrollProvider *>(this);
            break;
        case UIA_ScrollItemPatternId:
            *out = static_cast<IScrollItemProvider *>(this);
            break;
        case UIA_GridPatternId:
            *out = static_cast<IGridProvider *>(this);
            break;
        case UIA_GridItemPatternId:
            *out = static_cast<IGridItemProvider *>(this);
            break;
        case UIA_TablePatternId:
            *out = static_cast<ITableProvider *>(this);
            break;
        case UIA_TableItemPatternId:
            *out = static_cast<ITableItemProvider *>(this);
            break;
        case UIA_VirtualizedItemPatternId:
            *out = static_cast<IVirtualizedItemProvider *>(this);
            break;
        case UIA_ItemContainerPatternId:
            *out = static_cast<IItemContainerProvider *>(this);
            break;
        case UIA_AnnotationPatternId:
            *out = static_cast<IAnnotationProvider *>(this);
            break;
        case UIA_TextChildPatternId:
            *out = static_cast<ITextChildProvider *>(this);
            break;
        case UIA_TextPatternId:
        case UIA_TextPattern2Id:
            *out = static_cast<ITextProvider2 *>(&text_);
            break;
        default:
            break;
        }

        if(*out)
        {
            (*out)->AddRef();
        }

        return S_OK;
    }

    HRESULT NodeProvider::GetPropertyValue(PROPERTYID id, VARIANT *out)
    {
        return property(query(), id, out);
    }

    HRESULT NodeProvider::get_HostRawElementProvider(IRawElementProviderSimple **out)
    {
        if(!out)
        {
            return E_POINTER;
        }
        *out = nullptr;
        const auto q = query();

        if(!q)
        {
            return kUnavailable;
        }

        return root_ ? UiaHostProviderFromHwnd(reinterpret_cast<HWND>(q.runtime->nativeWindow.load()), out) : S_OK;
    }

    /// ------------------------------------------------------------
    /// Fragment navigation and geometry
    /// ------------------------------------------------------------

    HRESULT NodeProvider::Navigate(NavigateDirection direction, IRawElementProviderFragment **out)
    {
        if(!out)
        {
            return E_POINTER;
        }
        *out = nullptr;
        const auto q = query();

        if(!q)
        {
            return kUnavailable;
        }

        A::NodeId target = 0;

        switch(direction)
        {
        case NavigateDirection_Parent:
            target = q.node->parent;
            break;
        case NavigateDirection_FirstChild:
            if(!q.node->children.empty())
            {
                target = q.node->children.front();
            }
            break;
        case NavigateDirection_LastChild:
            if(!q.node->children.empty())
            {
                target = q.node->children.back();
            }
            break;
        case NavigateDirection_NextSibling:
        case NavigateDirection_PreviousSibling:
            if(const auto *parent = q.snapshot->find(q.node->parent))
            {
                const auto children = parent->children;

                for(std::size_t i = 0; i < children.size(); ++i)
                {
                    if(children[i] == id_)
                    {
                        if(direction == NavigateDirection_NextSibling && i + 1 < children.size())
                        {
                            target = children[i + 1];
                        }

                        if(direction == NavigateDirection_PreviousSibling && i != 0)
                        {
                            target = children[i - 1];
                        }
                        break;
                    }
                }
            }
            break;
        default:
            return E_INVALIDARG;
        }

        return fragment(*q.snapshot, target, out);
    }

    HRESULT NodeProvider::GetRuntimeId(SAFEARRAY **out)
    {
        if(!out)
        {
            return E_POINTER;
        }
        *out = nullptr;

        if(!query())
        {
            return kUnavailable;
        }

        if(root_)
        {
            return S_OK;
        }

        auto *array = SafeArrayCreateVector(VT_I4, 0, 3);

        if(!array)
        {
            return E_OUTOFMEMORY;
        }
        const std::array<LONG, 3> values{
            UiaAppendRuntimeId,
            std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(id_)),
            std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(id_ >> 32U))};

        for(LONG i = 0; i < 3; ++i)
        {
            LONG value = values[static_cast<std::size_t>(i)];
            const auto hr = SafeArrayPutElement(array, &i, &value);

            if(FAILED(hr))
            {
                SafeArrayDestroy(array);
                return hr;
            }
        }
        *out = array;
        return S_OK;
    }

    HRESULT NodeProvider::get_BoundingRectangle(UiaRect *out)
    {
        if(!out)
        {
            return E_POINTER;
        }
        *out = {};
        const auto q = query();

        if(!q)
        {
            return kUnavailable;
        }

        *out = nodeBounds(q);
        return S_OK;
    }

    HRESULT NodeProvider::GetEmbeddedFragmentRoots(SAFEARRAY **out)
    {
        if(!out)
        {
            return E_POINTER;
        }
        *out = nullptr;
        return query() ? S_OK : kUnavailable;
    }

    HRESULT NodeProvider::request(A::ActionKind kind, A::SelectionOperation operation) noexcept
    {
        A::ActionRequest request;
        request.action = kind;
        request.selectionOperation = operation;
        return action(query(), std::move(request));
    }

    HRESULT NodeProvider::SetFocus()
    {
        return request(A::ActionKind::Focus);
    }

    HRESULT NodeProvider::get_FragmentRoot(IRawElementProviderFragmentRoot **out)
    {
        if(!out)
        {
            return E_POINTER;
        }
        *out = nullptr;
        const auto q = query();

        if(!q)
        {
            return kUnavailable;
        }

        auto *provider = providerFor(*q.snapshot, q.snapshot->storage.root);
        return provider ? provider->QueryInterface(__uuidof(IRawElementProviderFragmentRoot), reinterpret_cast<void **>(out)) : kUnavailable;
    }

    HRESULT NodeProvider::ElementProviderFromPoint(double x, double y, IRawElementProviderFragment **out)
    {
        if(!out)
        {
            return E_POINTER;
        }
        *out = nullptr;
        const auto q = query();

        if(!q)
        {
            return kUnavailable;
        }

        if(!std::isfinite(x) || !std::isfinite(y))
        {
            return E_INVALIDARG;
        }

        // Reject points outside the visible host bounds.
        const auto rootBounds = nodeBounds(q);

        if(x < rootBounds.left || y < rootBounds.top || x >= rootBounds.left + rootBounds.width || y >= rootBounds.top + rootBounds.height)
        {
            return S_OK;
        }

        if(!q.host || q.host->scaleX <= 0 || q.host->scaleY <= 0)
        {
            return S_OK;
        }

        // Convert the screen point to client coordinates.
        const auto &host = *q.host;
        const double clientX = (x - host.x) / host.scaleX, clientY = (y - host.y) / host.scaleY;
        const auto &layout = static_cast<const UiaSnapshot *>(q.snapshot->native.get())->layout;
        A::NodeId best = q.snapshot->storage.root;
        std::uint32_t bestOrder = 0, bestOrdinal = 0;

        // Select the topmost visible node whose transformed bounds contain the point.
        for(std::size_t i = 0; i < q.snapshot->storage.nodes.size(); ++i)
        {
            const auto &node = q.snapshot->storage.nodes[i];

            if(!node.geometry || !node.geometry->hitTestable || !layout[i].visible)
            {
                continue;
            }
            const auto &clip = layout[i].clip;

            if(clip && (clientX < clip->x || clientY < clip->y || clientX >= clip->x + clip->width || clientY >= clip->y + clip->height))
            {
                continue;
            }
            const auto &t = node.geometry->toWindow;
            const auto &r = node.geometry->localBounds;

            // Map the client point through the inverse node transform.
            const double determinant = t.m11 * t.m22 - t.m12 * t.m21;
            const double localX = (t.m22 * (clientX - t.tx) - t.m21 * (clientY - t.ty)) / determinant;
            const double localY = (-t.m12 * (clientX - t.tx) + t.m11 * (clientY - t.ty)) / determinant;

            if(localX < r.x || localY < r.y || localX >= r.x + r.width || localY >= r.y + r.height)
            {
                continue;
            }

            if(layout[i].order > bestOrder || (layout[i].order == bestOrder && layout[i].ordinal >= bestOrdinal))
            {
                best = node.id;
                bestOrder = layout[i].order;
                bestOrdinal = layout[i].ordinal;
            }
        }

        return fragment(*q.snapshot, best, out);
    }

    HRESULT NodeProvider::GetFocus(IRawElementProviderFragment **out)
    {
        if(!out)
        {
            return E_POINTER;
        }
        *out = nullptr;
        const auto q = query();

        if(!q)
        {
            return kUnavailable;
        }

        if(!q.host || !q.host->focused)
        {
            return S_OK;
        }

        return q.snapshot->focusedNode != 0 ? fragment(*q.snapshot, q.snapshot->focusedNode, out) : S_OK;
    }

    HRESULT NodeProvider::AdviseEventAdded(EVENTID id, SAFEARRAY *properties)
    {
        static_cast<void>(id);
        static_cast<void>(properties);
        const auto q = query();

        if(!q)
        {
            return kUnavailable;
        }

        static_cast<UiaState *>(q.runtime->native.get())->listeners.fetch_add(1);
        return S_OK;
    }

    HRESULT NodeProvider::AdviseEventRemoved(EVENTID id, SAFEARRAY *properties)
    {
        static_cast<void>(id);
        static_cast<void>(properties);
        const auto q = query();

        if(!q)
        {
            return kUnavailable;
        }

        auto &listeners = static_cast<UiaState *>(q.runtime->native.get())->listeners;
        auto count = listeners.load();

        for(unsigned i = 0; i < 8 && count != 0; ++i)
        {
            if(listeners.compare_exchange_weak(count, count - 1))
            {
                break;
            }
        }

        return S_OK;
    }

} // namespace GameWIP::Desktop::Platform

#if DESKTOP_INTERNAL_TEST_HOOKS
namespace GameWIP::Desktop::Internal::TestHooks
{
    void *accessibilityProvider(Window &window, Types::Accessibility::NodeId node) noexcept
    {
        const auto *state = Internal::WindowAccess::accessibilityState(window);
        const auto runtime = state ? state->runtime.load() : nullptr;
        const auto snapshot = runtime && runtime->live.load() ? runtime->snapshot.load() : nullptr;
        auto *provider = snapshot ? Platform::providerFor(*snapshot, node) : nullptr;

        if(provider)
        {
            provider->AddRef();
        }

        return provider;
    }
} // namespace GameWIP::Desktop::Internal::TestHooks
#endif
