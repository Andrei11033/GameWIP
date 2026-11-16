/// @file win32_accessibility.cpp
/// @brief Snapshot-backed UI Automation providers and property projection.

#include "desktop/platform/win32/internal/win32_accessibility_provider.h"
#include "desktop/platform/win32/internal/win32_accessibility_helpers.h"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>

namespace GameWIP::Desktop::Detail::Platform
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
            } // namespace GameWIP::Desktop::Detail::Platform
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
    /// Native property projection
    /// ------------------------------------------------------------

    HRESULT property(const UiaQuery &q, PROPERTYID id, VARIANT *out) noexcept
    {
        if(!out)
        {
            return E_POINTER;
        }
        VariantInit(out);

        if(!q)
        {
            return kUnavailable;
        }

        const auto &n = *q.node;
        const bool redact = n.exposure == A::Exposure::RedactContent;

        switch(id)
        {
        case UIA_ControlTypePropertyId:
            return integer(out, controlType(n.role));
        case UIA_NamePropertyId:
            return string(out, redact ? std::u16string_view{} : q.text().name);
        case UIA_FullDescriptionPropertyId:
            return string(out, redact ? std::u16string_view{} : q.text().description);
        case UIA_HelpTextPropertyId:
            return string(out, redact ? std::u16string_view{} : q.text().help);
        case UIA_AccessKeyPropertyId:
            return string(out, redact ? std::u16string_view{} : q.text().accessKey);
        case UIA_AcceleratorKeyPropertyId:
            return string(out, redact ? std::u16string_view{} : q.text().shortcut);
        case UIA_AutomationIdPropertyId:
        {
            std::array<char, 24> digits{};
            std::array<char16_t, 24> wide{};
            const auto result = std::to_chars(digits.begin(), digits.end(), n.id);
            const auto length = static_cast<std::size_t>(result.ptr - digits.data());

            for(std::size_t i = 0; i < length; ++i)
            {
                wide[i] = static_cast<char16_t>(digits[i]);
            }

            return string(out, {wide.data(), length});
        }

        case UIA_FrameworkIdPropertyId:
            return string(out, u"GameWIP.Desktop");
        case UIA_IsControlElementPropertyId:
        case UIA_IsContentElementPropertyId:
            return boolean(out, true);
        case UIA_IsEnabledPropertyId:
            return boolean(out, q.runtime->hostEnabled.load() && !n.states.contains(A::State::Disabled));
        case UIA_IsKeyboardFocusablePropertyId:
            return boolean(out, n.states.contains(A::State::Focusable));
        case UIA_HasKeyboardFocusPropertyId:
        {
            return boolean(out, q.host && q.host->focused && n.states.contains(A::State::Focused));
        }

        case UIA_IsPasswordPropertyId:
            return boolean(out, n.states.contains(A::State::Password) || n.role == A::Role::PasswordField);
        case UIA_IsRequiredForFormPropertyId:
            return boolean(out, n.states.contains(A::State::Required));
        case UIA_IsDataValidForFormPropertyId:
            return boolean(out, !n.states.contains(A::State::Invalid));
        case UIA_IsOffscreenPropertyId:
        {
            const auto bounds = nodeBounds(q);
            return boolean(out, n.states.contains(A::State::Offscreen) || bounds.width <= 0 || bounds.height <= 0);
        }

        case UIA_NativeWindowHandlePropertyId:
            return integer(out, n.id == q.snapshot->storage.root ? static_cast<int>(q.runtime->nativeWindow.load() & 0xFFFFFFFFU) : 0);
        case UIA_CulturePropertyId:
            return integer(
                out,
                redact || q.text().language.empty() ? 0
                                                    : static_cast<int>(LocaleNameToLCID(reinterpret_cast<LPCWSTR>(q.text().language.c_str()), 0)));
        case UIA_BoundingRectanglePropertyId:
        {
            const auto r = nodeBounds(q);
            const std::array values{r.left, r.top, r.width, r.height};
            auto *array = SafeArrayCreateVector(VT_R8, 0, 4);

            if(!array)
            {
                return E_OUTOFMEMORY;
            }

            for(LONG i = 0; i < 4; ++i)
            {
                double value = values[static_cast<std::size_t>(i)];
                const auto hr = SafeArrayPutElement(array, &i, &value);

                if(FAILED(hr))
                {
                    SafeArrayDestroy(array);
                    return hr;
                }
            }
            out->vt = VT_ARRAY | VT_R8;
            out->parray = array;
            return S_OK;
        }

        case UIA_ValueValuePropertyId:
            return string(out, valueHidden(n) ? std::u16string_view{} : q.text().value);
        case UIA_ValueIsReadOnlyPropertyId:
        case UIA_RangeValueIsReadOnlyPropertyId:
            return boolean(out, contentReadOnly(n));
        case UIA_RangeValueValuePropertyId:
            if(n.rangeValue && !valueHidden(n))
            {
                return number(out, n.rangeValue->value);
            }
            break;
        case UIA_RangeValueMinimumPropertyId:
            if(n.rangeValue && !valueHidden(n))
            {
                return number(out, n.rangeValue->minimum);
            }
            break;
        case UIA_RangeValueMaximumPropertyId:
            if(n.rangeValue && !valueHidden(n))
            {
                return number(out, n.rangeValue->maximum);
            }
            break;
        case UIA_RangeValueSmallChangePropertyId:
            if(n.rangeValue && !valueHidden(n))
            {
                return number(out, n.rangeValue->smallChange);
            }
            break;
        case UIA_RangeValueLargeChangePropertyId:
            if(n.rangeValue && !valueHidden(n))
            {
                return number(out, n.rangeValue->largeChange);
            }
            break;
        case UIA_ToggleToggleStatePropertyId:
            return integer(
                out,
                n.states.contains(A::State::Mixed)                                               ? ToggleState_Indeterminate
                : (n.states.contains(A::State::Checked) || n.states.contains(A::State::Pressed)) ? ToggleState_On
                                                                                                 : ToggleState_Off);
        case UIA_ExpandCollapseExpandCollapseStatePropertyId:
            return integer(
                out,
                !patternSupported(n, UIA_ExpandCollapsePatternId) ? ExpandCollapseState_LeafNode
                : n.states.contains(A::State::Expanded)           ? ExpandCollapseState_Expanded
                                                                  : ExpandCollapseState_Collapsed);
        case UIA_SelectionItemIsSelectedPropertyId:
            return boolean(out, n.states.contains(A::State::Selected));
        case UIA_SelectionCanSelectMultiplePropertyId:
            if(n.selection)
            {
                return boolean(out, n.selection->multiSelectable);
            }
            break;
        case UIA_SelectionIsSelectionRequiredPropertyId:
            if(n.selection)
            {
                return boolean(out, n.selection->selectionRequired);
            }
            break;
        case UIA_SelectionSelectionPropertyId:
            if(n.selection)
            {
                const auto hr = nodeArray(*q.snapshot, n.selection->selectedNodes, &out->parray);

                if(SUCCEEDED(hr))
                {
                    out->vt = VT_ARRAY | VT_UNKNOWN;
                }

                return hr;
            }
            break;
        case UIA_SelectionItemSelectionContainerPropertyId:
        case UIA_GridItemContainingGridPropertyId:
        {
            const auto &layout = static_cast<const UiaSnapshot *>(q.snapshot->native.get())->layout[q.index];
            out->punkVal =
                providerFor(*q.snapshot, id == UIA_SelectionItemSelectionContainerPropertyId ? layout.selectionContainer : layout.containingGrid);

            if(out->punkVal)
            {
                out->punkVal->AddRef();
            }
            out->vt = VT_UNKNOWN;
            break;
        }
        case UIA_Selection2ItemCountPropertyId:
            if(n.selection)
            {
                return integer(out, static_cast<int>(n.selection->selectedNodes.size()));
            }
            break;
        case UIA_Selection2FirstSelectedItemPropertyId:
        case UIA_Selection2LastSelectedItemPropertyId:
        case UIA_Selection2CurrentSelectedItemPropertyId:
            if(n.selection)
            {
                A::NodeId selected = n.selection->activeNode;

                if(id != UIA_Selection2CurrentSelectedItemPropertyId && !n.selection->selectedNodes.empty())
                {
                    selected =
                        id == UIA_Selection2FirstSelectedItemPropertyId ? n.selection->selectedNodes.front() : n.selection->selectedNodes.back();
                }
                out->punkVal = providerFor(*q.snapshot, selected);

                if(out->punkVal)
                {
                    out->punkVal->AddRef();
                }
                out->vt = VT_UNKNOWN;
            }
            break;
        case UIA_ScrollHorizontalScrollPercentPropertyId:
            if(n.scroll)
            {
                return number(out, n.scroll->horizontalScrollable ? n.scroll->horizontalPercent : kNoScroll);
            }
            break;
        case UIA_ScrollVerticalScrollPercentPropertyId:
            if(n.scroll)
            {
                return number(out, n.scroll->verticalScrollable ? n.scroll->verticalPercent : kNoScroll);
            }
            break;
        case UIA_ScrollHorizontalViewSizePropertyId:
            if(n.scroll)
            {
                return number(out, n.scroll->horizontalViewSize);
            }
            break;
        case UIA_ScrollVerticalViewSizePropertyId:
            if(n.scroll)
            {
                return number(out, n.scroll->verticalViewSize);
            }
            break;
        case UIA_ScrollHorizontallyScrollablePropertyId:
            if(n.scroll)
            {
                return boolean(out, n.scroll->horizontalScrollable);
            }
            break;
        case UIA_ScrollVerticallyScrollablePropertyId:
            if(n.scroll)
            {
                return boolean(out, n.scroll->verticalScrollable);
            }
            break;
        case UIA_TableRowOrColumnMajorPropertyId:
            if(n.collection)
            {
                return integer(out, RowOrColumnMajor_RowMajor);
            }
            break;
        case UIA_TableRowHeadersPropertyId:
        case UIA_TableItemRowHeaderItemsPropertyId:
        case UIA_TableColumnHeadersPropertyId:
        case UIA_TableItemColumnHeaderItemsPropertyId:
        {
            const auto hr = nodeArray(
                *q.snapshot,
                relation(
                    n,
                    id == UIA_TableRowHeadersPropertyId || id == UIA_TableItemRowHeaderItemsPropertyId ? A::RelationKind::RowHeaders
                                                                                                       : A::RelationKind::ColumnHeaders),
                &out->parray);

            if(SUCCEEDED(hr))
            {
                out->vt = VT_ARRAY | VT_UNKNOWN;
            }

            return hr;
        }

        case UIA_PositionInSetPropertyId:
            if(n.collection)
            {
                return integer(out, static_cast<int>(n.collection->positionInSet));
            }
            break;
        case UIA_SizeOfSetPropertyId:
            if(n.collection)
            {
                return integer(out, static_cast<int>(n.collection->setSize));
            }
            break;
        case UIA_LevelPropertyId:
            if(n.collection)
            {
                return integer(out, static_cast<int>(n.collection->level));
            }
            break;
        case UIA_GridRowCountPropertyId:
            if(n.collection)
            {
                return integer(out, static_cast<int>(n.collection->rowCount));
            }
            break;
        case UIA_GridColumnCountPropertyId:
            if(n.collection)
            {
                return integer(out, static_cast<int>(n.collection->columnCount));
            }
            break;
        case UIA_GridItemRowPropertyId:
            if(n.collection)
            {
                return integer(out, static_cast<int>(n.collection->rowIndex));
            }
            break;
        case UIA_GridItemColumnPropertyId:
            if(n.collection)
            {
                return integer(out, static_cast<int>(n.collection->columnIndex));
            }
            break;
        case UIA_GridItemRowSpanPropertyId:
            if(n.collection)
            {
                return integer(out, static_cast<int>(n.collection->rowSpan));
            }
            break;
        case UIA_GridItemColumnSpanPropertyId:
            if(n.collection)
            {
                return integer(out, static_cast<int>(n.collection->columnSpan));
            }
            break;
        case UIA_LabeledByPropertyId:
        {
            const auto ids = relation(n, A::RelationKind::LabelledBy);

            if(!ids.empty())
            {
                out->punkVal = providerFor(*q.snapshot, ids.front());

                if(out->punkVal)
                {
                    out->punkVal->AddRef();
                }
                out->vt = VT_UNKNOWN;
            }
            break;
        }
        case UIA_DescribedByPropertyId:
        case UIA_ControllerForPropertyId:
        case UIA_FlowsToPropertyId:
        case UIA_FlowsFromPropertyId:
        {
            const auto kind = id == UIA_DescribedByPropertyId     ? A::RelationKind::DescribedBy
                              : id == UIA_ControllerForPropertyId ? A::RelationKind::ControllerFor
                              : id == UIA_FlowsToPropertyId       ? A::RelationKind::FlowsTo
                                                                  : A::RelationKind::FlowsFrom;
            const auto hr = nodeArray(*q.snapshot, relation(n, kind), &out->parray);

            if(SUCCEEDED(hr))
            {
                out->vt = VT_ARRAY | VT_UNKNOWN;
            }

            return hr;
        }

        case UIA_IsInvokePatternAvailablePropertyId:
            return boolean(out, patternSupported(n, UIA_InvokePatternId));
        case UIA_IsValuePatternAvailablePropertyId:
            return boolean(out, patternSupported(n, UIA_ValuePatternId));
        case UIA_IsRangeValuePatternAvailablePropertyId:
            return boolean(out, patternSupported(n, UIA_RangeValuePatternId));
        case UIA_IsTogglePatternAvailablePropertyId:
            return boolean(out, patternSupported(n, UIA_TogglePatternId));
        case UIA_IsExpandCollapsePatternAvailablePropertyId:
            return boolean(out, patternSupported(n, UIA_ExpandCollapsePatternId));
        case UIA_IsSelectionPatternAvailablePropertyId:
            return boolean(out, patternSupported(n, UIA_SelectionPatternId));
        case UIA_IsSelectionItemPatternAvailablePropertyId:
            return boolean(out, patternSupported(n, UIA_SelectionItemPatternId));
        case UIA_IsScrollPatternAvailablePropertyId:
            return boolean(out, patternSupported(n, UIA_ScrollPatternId));
        case UIA_IsScrollItemPatternAvailablePropertyId:
            return boolean(out, patternSupported(n, UIA_ScrollItemPatternId));
        case UIA_IsGridPatternAvailablePropertyId:
            return boolean(out, patternSupported(n, UIA_GridPatternId));
        case UIA_IsGridItemPatternAvailablePropertyId:
            return boolean(out, patternSupported(n, UIA_GridItemPatternId));
        case UIA_IsTablePatternAvailablePropertyId:
            return boolean(out, patternSupported(n, UIA_TablePatternId));
        case UIA_IsTableItemPatternAvailablePropertyId:
            return boolean(out, patternSupported(n, UIA_TableItemPatternId));
        case UIA_IsTextPatternAvailablePropertyId:
            return boolean(out, patternSupported(n, UIA_TextPatternId));
        case UIA_IsTextPattern2AvailablePropertyId:
            return boolean(out, patternSupported(n, UIA_TextPattern2Id));
        case UIA_IsVirtualizedItemPatternAvailablePropertyId:
            return boolean(out, patternSupported(n, UIA_VirtualizedItemPatternId));
        case UIA_IsItemContainerPatternAvailablePropertyId:
            return boolean(out, patternSupported(n, UIA_ItemContainerPatternId));
        case UIA_IsAnnotationPatternAvailablePropertyId:
        case UIA_IsTextChildPatternAvailablePropertyId:
            return boolean(out, annotationSupported(q));
        case UIA_IsSelectionPattern2AvailablePropertyId:
            return boolean(out, n.selection.has_value());
        case UIA_LiveSettingPropertyId:
            return integer(out, n.role == A::Role::Alert ? 2 : n.role == A::Role::Status ? 1 : 0);
        case UIA_AnnotationAnnotationTypeIdPropertyId:
        case UIA_AnnotationAnnotationTypeNamePropertyId:
        case UIA_AnnotationAuthorPropertyId:
        case UIA_AnnotationDateTimePropertyId:
        case UIA_AnnotationTargetPropertyId:
        {
            std::size_t source = 0, annotationIndex = 0;
            const auto *annotation = annotationFor(q, source, annotationIndex);

            if(!annotation)
            {
                break;
            }

            if(id == UIA_AnnotationAnnotationTypeIdPropertyId)
            {
                return integer(
                    out,
                    annotation->kind == A::TextAnnotationKind::Spelling   ? AnnotationType_SpellingError
                    : annotation->kind == A::TextAnnotationKind::Emphasis ? AnnotationType_Highlighted
                                                                          : AnnotationType_Unknown);
            }

            if(id == UIA_AnnotationAnnotationTypeNamePropertyId)
            {
                return string(out, q.snapshot->textCache[source].annotationValues[annotationIndex]);
            }

            if(id == UIA_AnnotationTargetPropertyId)
            {
                out->punkVal = providerFor(*q.snapshot, q.snapshot->storage.nodes[source].id);

                if(out->punkVal)
                {
                    out->punkVal->AddRef();
                }
                out->vt = VT_UNKNOWN;
                break;
            }

            return string(out, {});
        }

        default:
            break;
        }

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

        const auto rootBounds = nodeBounds(q);

        if(x < rootBounds.left || y < rootBounds.top || x >= rootBounds.left + rootBounds.width || y >= rootBounds.top + rootBounds.height)
        {
            return S_OK;
        }

        if(!q.host || q.host->scaleX <= 0 || q.host->scaleY <= 0)
        {
            return S_OK;
        }
        const auto &host = *q.host;
        const double clientX = (x - host.x) / host.scaleX, clientY = (y - host.y) / host.scaleY;
        const auto &layout = static_cast<const UiaSnapshot *>(q.snapshot->native.get())->layout;
        A::NodeId best = q.snapshot->storage.root;
        std::uint32_t bestOrder = 0, bestOrdinal = 0;

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

} // namespace GameWIP::Desktop::Detail::Platform

#if DESKTOP_INTERNAL_TEST_HOOKS
namespace GameWIP::Desktop::TestHooks
{
    void *accessibilityProvider(Window &window, Types::Accessibility::NodeId node) noexcept
    {
        const auto *state = Detail::WindowAccess::accessibilityState(window);
        const auto runtime = state ? state->runtime.load() : nullptr;
        const auto snapshot = runtime && runtime->live.load() ? runtime->snapshot.load() : nullptr;
        auto *provider = snapshot ? Detail::Platform::providerFor(*snapshot, node) : nullptr;

        if(provider)
        {
            provider->AddRef();
        }

        return provider;
    }
} // namespace GameWIP::Desktop::TestHooks
#endif
