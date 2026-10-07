/// @file win32_accessibility.cpp
/// @brief Snapshot-backed UI Automation tree, patterns, transport, and event delivery.

#include "desktop/platform/win32/internal/win32_accessibility_provider.h"
#include "unicode/unicode.h"
#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cwchar>
#include <limits>
#include <new>

namespace GameWIP::Desktop::Detail::Platform
{
    namespace
    {
        constexpr HRESULT kUnavailable = static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE);
        constexpr HRESULT kUnsupported = static_cast<HRESULT>(UIA_E_NOTSUPPORTED);
        constexpr double kNoScroll = -1.0;
        // Selection2=10034 in Microsoft's UIAutomation IDL; older MinGW SDKs omit the named constant.
        constexpr PATTERNID kSelection2PatternId = 10034;
        // ------------------------------------------------------------
        // Geometry and native semantic mapping
        // ------------------------------------------------------------

        A::Rect intersection(A::Rect a, A::Rect b) noexcept
        {
            const double right = std::min(a.x + a.width, b.x + b.width), bottom = std::min(a.y + a.height, b.y + b.height);
            a.x = std::max(a.x, b.x);
            a.y = std::max(a.y, b.y);
            a.width = std::max(0.0, right - a.x);
            a.height = std::max(0.0, bottom - a.y);
            return a;
        }

        A::Rect transformed(const A::Geometry &geometry) noexcept
        {
            const auto &r = geometry.localBounds;
            const auto &t = geometry.toWindow;
            const std::array xs{r.x, r.x + r.width, r.x, r.x + r.width};
            const std::array ys{r.y, r.y, r.y + r.height, r.y + r.height};
            double left = std::numeric_limits<double>::max(), top = left, right = -left, bottom = -left;
            for (unsigned i = 0; i < 4; ++i)
            {
                const double x = t.m11 * xs[i] + t.m21 * ys[i] + t.tx, y = t.m12 * xs[i] + t.m22 * ys[i] + t.ty;
                left = std::min(left, x);
                right = std::max(right, x);
                top = std::min(top, y);
                bottom = std::max(bottom, y);
            }
            return {left, top, right - left, bottom - top};
        }

        int controlType(A::Role role) noexcept
        {
            switch (role)
            {
            case A::Role::Button:
            case A::Role::ToggleButton:
                return UIA_ButtonControlTypeId;
            case A::Role::CheckBox:
                return UIA_CheckBoxControlTypeId;
            case A::Role::RadioButton:
                return UIA_RadioButtonControlTypeId;
            case A::Role::Link:
                return UIA_HyperlinkControlTypeId;
            case A::Role::Image:
                return UIA_ImageControlTypeId;
            case A::Role::StaticText:
                return UIA_TextControlTypeId;
            case A::Role::TextField:
            case A::Role::PasswordField:
            case A::Role::SearchField:
                return UIA_EditControlTypeId;
            case A::Role::Slider:
                return UIA_SliderControlTypeId;
            case A::Role::ProgressBar:
                return UIA_ProgressBarControlTypeId;
            case A::Role::ScrollBar:
                return UIA_ScrollBarControlTypeId;
            case A::Role::List:
                return UIA_ListControlTypeId;
            case A::Role::ListItem:
                return UIA_ListItemControlTypeId;
            case A::Role::Tree:
                return UIA_TreeControlTypeId;
            case A::Role::TreeItem:
                return UIA_TreeItemControlTypeId;
            case A::Role::Table:
                return UIA_TableControlTypeId;
            case A::Role::Cell:
                return UIA_DataItemControlTypeId;
            case A::Role::Row:
                return UIA_DataItemControlTypeId;
            case A::Role::ColumnHeader:
            case A::Role::RowHeader:
                return UIA_HeaderItemControlTypeId;
            case A::Role::Tab:
                return UIA_TabItemControlTypeId;
            case A::Role::TabList:
                return UIA_TabControlTypeId;
            case A::Role::ComboBox:
                return UIA_ComboBoxControlTypeId;
            case A::Role::SpinButton:
                return UIA_SpinnerControlTypeId;
            case A::Role::Calendar:
            case A::Role::DateInput:
                return UIA_CalendarControlTypeId;
            case A::Role::Menu:
                return UIA_MenuControlTypeId;
            case A::Role::MenuBar:
                return UIA_MenuBarControlTypeId;
            case A::Role::MenuItem:
                return UIA_MenuItemControlTypeId;
            case A::Role::Toolbar:
                return UIA_ToolBarControlTypeId;
            case A::Role::Separator:
                return UIA_SeparatorControlTypeId;
            case A::Role::Status:
                return UIA_StatusBarControlTypeId;
            case A::Role::Tooltip:
                return UIA_ToolTipControlTypeId;
            case A::Role::Application:
            case A::Role::Window:
            case A::Role::Dialog:
                return UIA_WindowControlTypeId;
            case A::Role::Document:
            case A::Role::Article:
                return UIA_DocumentControlTypeId;
            case A::Role::Group:
            case A::Role::Section:
            case A::Role::Form:
            case A::Role::Landmark:
            case A::Role::Navigation:
                return UIA_GroupControlTypeId;
            case A::Role::Pane:
            case A::Role::TabPanel:
            case A::Role::ScrollArea:
                return UIA_PaneControlTypeId;
            case A::Role::Unknown:
            case A::Role::TimeInput:
            case A::Role::Alert:
            case A::Role::Video:
            case A::Role::Audio:
            case A::Role::Canvas:
            case A::Role::Custom:
            case A::Role::Count:
                return UIA_CustomControlTypeId;
            }
            return UIA_CustomControlTypeId;
        }

        bool patternSupported(const A::Node &n, PATTERNID id) noexcept
        {
            switch (id)
            {
            case UIA_InvokePatternId:
                return n.actions.contains(A::ActionKind::Invoke);
            case UIA_ValuePatternId:
                return !valueHidden(n) && !n.rangeValue && (!n.value.empty() || n.actions.contains(A::ActionKind::SetValue));
            case UIA_RangeValuePatternId:
                return !valueHidden(n) && n.rangeValue.has_value();
            case UIA_TogglePatternId:
                return n.actions.contains(A::ActionKind::Toggle) || n.role == A::Role::CheckBox || n.role == A::Role::ToggleButton;
            case UIA_ExpandCollapsePatternId:
                return n.states.contains(A::State::Expandable) || n.actions.contains(A::ActionKind::Expand) ||
                       n.actions.contains(A::ActionKind::Collapse);
            case UIA_SelectionPatternId:
            case kSelection2PatternId:
                return n.selection.has_value();
            case UIA_SelectionItemPatternId:
                return n.states.contains(A::State::Selectable);
            case UIA_ScrollPatternId:
                return n.scroll.has_value();
            case UIA_ScrollItemPatternId:
                return n.actions.contains(A::ActionKind::ScrollTo);
            case UIA_GridPatternId:
            case UIA_TablePatternId:
                return n.collection && n.role == A::Role::Table;
            case UIA_GridItemPatternId:
            case UIA_TableItemPatternId:
                return n.collection && (n.role == A::Role::Cell || n.role == A::Role::RowHeader || n.role == A::Role::ColumnHeader);
            case UIA_VirtualizedItemPatternId:
                return n.virtualization && n.virtualization->virtualized && n.virtualization->canRealize;
            case UIA_ItemContainerPatternId:
                return n.virtualization.has_value() || n.selection.has_value() || n.role == A::Role::Table;
            case UIA_TextPatternId:
            case UIA_TextPattern2Id:
                return n.text.has_value() && !textHidden(n);
            default:
                return false;
            }
        }

        HRESULT fragment(const PublishedSnapshot &snapshot, A::NodeId id, IRawElementProviderFragment **out) noexcept
        {
            auto *provider = providerFor(snapshot, id);
            return provider ? provider->QueryInterface(__uuidof(IRawElementProviderFragment), reinterpret_cast<void **>(out)) : S_OK;
        }

        const A::TextAnnotation *annotationFor(const UiaQuery &q, std::size_t &source, std::size_t &index) noexcept
        {
            if (!q)
                return nullptr;
            const auto &sources = static_cast<const UiaSnapshot *>(q.snapshot->native.get())->annotationSources;
            const auto reference = sources[q.index];
            source = reference.first;
            index = reference.second;
            if (source >= q.snapshot->storage.nodes.size())
                return nullptr;
            const auto &node = q.snapshot->storage.nodes[source];
            return node.text && !textHidden(node) ? &node.text->annotations[index] : nullptr;
        }

        bool annotationSupported(const UiaQuery &q) noexcept
        {
            std::size_t source = 0, index = 0;
            return annotationFor(q, source, index) != nullptr;
        }

        HRESULT boolean(VARIANT *out, bool value) noexcept
        {
            out->vt = VT_BOOL;
            out->boolVal = value ? VARIANT_TRUE : VARIANT_FALSE;
            return S_OK;
        }

        HRESULT integer(VARIANT *out, int value) noexcept
        {
            out->vt = VT_I4;
            out->lVal = value;
            return S_OK;
        }

        HRESULT number(VARIANT *out, double value) noexcept
        {
            out->vt = VT_R8;
            out->dblVal = value;
            return S_OK;
        }

        HRESULT string(VARIANT *out, std::u16string_view value) noexcept
        {
            const auto hr = stringValue(value, &out->bstrVal);
            if (SUCCEEDED(hr))
                out->vt = VT_BSTR;
            return hr;
        }

        std::span<const A::NodeId> relation(const A::Node &node, A::RelationKind kind) noexcept
        {
            for (const auto &r : node.relations)
                if (r.kind == kind)
                    return r.targets;
            return {};
        }

        bool descendant(const PublishedSnapshot &snapshot, const A::Node &node, A::NodeId ancestor) noexcept
        {
            const auto nodeIndex = snapshot.findIndex(node.id), ancestorIndex = snapshot.findIndex(ancestor);
            if (nodeIndex >= snapshot.storage.nodes.size() || ancestorIndex >= snapshot.storage.nodes.size())
                return false;
            const auto &layout = static_cast<const UiaSnapshot *>(snapshot.native.get())->layout;
            return layout[nodeIndex].ordinal > layout[ancestorIndex].ordinal && layout[nodeIndex].ordinal < layout[ancestorIndex].subtreeEnd;
        }
        // ------------------------------------------------------------
        // Native property notifications
        // ------------------------------------------------------------

        void propertyEvent(
            const AccessibilityPendingNotification &event,
            IRawElementProviderSimple *provider,
            PROPERTYID id,
            const std::shared_ptr<AccessibilityRuntime> &runtime) noexcept
        {
            if (!event.before || !event.after)
                return;
            const auto beforeIndex = event.before->findIndex(event.notification.node), afterIndex = event.after->findIndex(event.notification.node);
            if (beforeIndex >= event.before->storage.nodes.size() || afterIndex >= event.after->storage.nodes.size())
                return;
            UiaQuery before{runtime, event.before, &event.before->storage.nodes[beforeIndex], beforeIndex};
            UiaQuery after{runtime, event.after, &event.after->storage.nodes[afterIndex], afterIndex};
            AccessibilityHostGeometry host;
            if (runtime->readGeometry(host))
                before.host = after.host = host;
            auto protectedBefore = *before.node;
            auto protectedAfter = *after.node;
            const auto current = runtime->snapshot.load();
            const auto *currentNode = current ? current->find(after.node->id) : nullptr;
            if (!currentNode)
                return;
            if (after.node->exposure == A::Exposure::RedactContent || currentNode->exposure == A::Exposure::RedactContent)
                protectedBefore.exposure = A::Exposure::RedactContent;
            if (currentNode->exposure == A::Exposure::RedactContent)
                protectedAfter.exposure = A::Exposure::RedactContent;
            if (valueHidden(*after.node) || valueHidden(*currentNode))
                protectedBefore.states.flags |= static_cast<std::uint64_t>(A::State::Sensitive);
            if (valueHidden(*currentNode))
                protectedAfter.states.flags |= static_cast<std::uint64_t>(A::State::Sensitive);
            before.node = &protectedBefore;
            after.node = &protectedAfter;
            VARIANT oldValue{}, newValue{};
            if (SUCCEEDED(property(before, id, &oldValue)) && SUCCEEDED(property(after, id, &newValue)))
                static_cast<void>(UiaRaiseAutomationPropertyChangedEvent(provider, id, oldValue, newValue));
            VariantClear(&oldValue);
            VariantClear(&newValue);
        }
    } // namespace

    // ------------------------------------------------------------
    // Native state and snapshot queries
    // ------------------------------------------------------------

    UiaSnapshot::~UiaSnapshot()
    {
        for (auto *provider : providers)
            if (provider)
                provider->Release();
    }

    UiaState::~UiaState()
    {
        for (auto [id, provider] : providers)
        {
            static_cast<void>(id);
            provider->Release();
        }
    }

    UiaQuery queryNode(const std::weak_ptr<AccessibilityRuntime> &runtime, A::NodeId id) noexcept
    {
        UiaQuery result;
        result.runtime = runtime.lock();
        if (!result.runtime || !result.runtime->live.load())
            return result;
        result.snapshot = result.runtime->snapshot.load();
        if (!result.snapshot)
            return result;
        result.index = result.snapshot->findIndex(id);
        if (result.index < result.snapshot->storage.nodes.size())
            result.node = &result.snapshot->storage.nodes[result.index];
        AccessibilityHostGeometry host;
        if (result.runtime->readGeometry(host))
            result.host = host;
        if (!result.runtime->live.load())
            result.node = nullptr;
        return result;
    }

    IRawElementProviderSimple *providerFor(const PublishedSnapshot &snapshot, A::NodeId id) noexcept
    {
        const auto index = snapshot.findIndex(id);
        const auto *native = static_cast<const UiaSnapshot *>(snapshot.native.get());
        return native && index < native->providers.size() ? native->providers[index] : nullptr;
    }
    // ------------------------------------------------------------
    // Content exposure and cached bounds
    // ------------------------------------------------------------

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
        if (!query.host || !query.host->visible)
            return {};
        const auto &host = *query.host;
        const auto &layout = static_cast<const UiaSnapshot *>(query.snapshot->native.get())->layout[query.index];
        if (!layout.visible)
            return {};
        if (layout.clip)
            rect = intersection(rect, *layout.clip);
        rect = intersection(rect, {0, 0, host.width, host.height});
        return {host.x + rect.x * host.scaleX, host.y + rect.y * host.scaleY, rect.width * host.scaleX, rect.height * host.scaleY};
    }

    UiaRect nodeBounds(const UiaQuery &query) noexcept
    {
        auto rect = static_cast<const UiaSnapshot *>(query.snapshot->native.get())->layout[query.index].bounds;
        if (query.node->id == query.snapshot->storage.root && !query.node->geometry)
        {
            if (!query.host)
                return {};
            rect = {0, 0, query.host->width, query.host->height};
        }
        return clientRect(query, rect);
    }

    HRESULT stringValue(std::u16string_view text, BSTR *out) noexcept
    {
        if (!out)
            return E_POINTER;
        *out = SysAllocStringLen(reinterpret_cast<const OLECHAR *>(text.data()), static_cast<UINT>(text.size()));
        return *out ? S_OK : E_OUTOFMEMORY;
    }

    HRESULT nodeArray(const PublishedSnapshot &snapshot, std::span<const A::NodeId> ids, SAFEARRAY **out) noexcept
    {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        auto *array = SafeArrayCreateVector(VT_UNKNOWN, 0, static_cast<ULONG>(ids.size()));
        if (!array)
            return E_OUTOFMEMORY;
        for (std::size_t i = 0; i < ids.size(); ++i)
        {
            LONG index = static_cast<LONG>(i);
            IUnknown *provider = providerFor(snapshot, ids[i]);
            const auto hr = SafeArrayPutElement(array, &index, provider);
            if (FAILED(hr))
            {
                SafeArrayDestroy(array);
                return hr;
            }
        }
        *out = array;
        return S_OK;
    }
    // ------------------------------------------------------------
    // Native property projection
    // ------------------------------------------------------------

    HRESULT property(const UiaQuery &q, PROPERTYID id, VARIANT *out) noexcept
    {
        if (!out)
            return E_POINTER;
        VariantInit(out);
        if (!q)
            return kUnavailable;
        const auto &n = *q.node;
        const bool redact = n.exposure == A::Exposure::RedactContent;
        switch (id)
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
            for (std::size_t i = 0; i < length; ++i)
                wide[i] = static_cast<char16_t>(digits[i]);
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
            if (!array)
                return E_OUTOFMEMORY;
            for (LONG i = 0; i < 4; ++i)
            {
                double value = values[static_cast<std::size_t>(i)];
                const auto hr = SafeArrayPutElement(array, &i, &value);
                if (FAILED(hr))
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
            if (n.rangeValue && !valueHidden(n))
                return number(out, n.rangeValue->value);
            break;
        case UIA_RangeValueMinimumPropertyId:
            if (n.rangeValue && !valueHidden(n))
                return number(out, n.rangeValue->minimum);
            break;
        case UIA_RangeValueMaximumPropertyId:
            if (n.rangeValue && !valueHidden(n))
                return number(out, n.rangeValue->maximum);
            break;
        case UIA_RangeValueSmallChangePropertyId:
            if (n.rangeValue && !valueHidden(n))
                return number(out, n.rangeValue->smallChange);
            break;
        case UIA_RangeValueLargeChangePropertyId:
            if (n.rangeValue && !valueHidden(n))
                return number(out, n.rangeValue->largeChange);
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
            if (n.selection)
                return boolean(out, n.selection->multiSelectable);
            break;
        case UIA_SelectionIsSelectionRequiredPropertyId:
            if (n.selection)
                return boolean(out, n.selection->selectionRequired);
            break;
        case UIA_SelectionSelectionPropertyId:
            if (n.selection)
            {
                const auto hr = nodeArray(*q.snapshot, n.selection->selectedNodes, &out->parray);
                if (SUCCEEDED(hr))
                    out->vt = VT_ARRAY | VT_UNKNOWN;
                return hr;
            }
            break;
        case UIA_SelectionItemSelectionContainerPropertyId:
        case UIA_GridItemContainingGridPropertyId:
        {
            const auto &layout = static_cast<const UiaSnapshot *>(q.snapshot->native.get())->layout[q.index];
            out->punkVal =
                providerFor(*q.snapshot, id == UIA_SelectionItemSelectionContainerPropertyId ? layout.selectionContainer : layout.containingGrid);
            if (out->punkVal)
                out->punkVal->AddRef();
            out->vt = VT_UNKNOWN;
            break;
        }
        case UIA_Selection2ItemCountPropertyId:
            if (n.selection)
                return integer(out, static_cast<int>(n.selection->selectedNodes.size()));
            break;
        case UIA_Selection2FirstSelectedItemPropertyId:
        case UIA_Selection2LastSelectedItemPropertyId:
        case UIA_Selection2CurrentSelectedItemPropertyId:
            if (n.selection)
            {
                A::NodeId selected = n.selection->activeNode;
                if (id != UIA_Selection2CurrentSelectedItemPropertyId && !n.selection->selectedNodes.empty())
                    selected =
                        id == UIA_Selection2FirstSelectedItemPropertyId ? n.selection->selectedNodes.front() : n.selection->selectedNodes.back();
                out->punkVal = providerFor(*q.snapshot, selected);
                if (out->punkVal)
                    out->punkVal->AddRef();
                out->vt = VT_UNKNOWN;
            }
            break;
        case UIA_ScrollHorizontalScrollPercentPropertyId:
            if (n.scroll)
                return number(out, n.scroll->horizontalScrollable ? n.scroll->horizontalPercent : kNoScroll);
            break;
        case UIA_ScrollVerticalScrollPercentPropertyId:
            if (n.scroll)
                return number(out, n.scroll->verticalScrollable ? n.scroll->verticalPercent : kNoScroll);
            break;
        case UIA_ScrollHorizontalViewSizePropertyId:
            if (n.scroll)
                return number(out, n.scroll->horizontalViewSize);
            break;
        case UIA_ScrollVerticalViewSizePropertyId:
            if (n.scroll)
                return number(out, n.scroll->verticalViewSize);
            break;
        case UIA_ScrollHorizontallyScrollablePropertyId:
            if (n.scroll)
                return boolean(out, n.scroll->horizontalScrollable);
            break;
        case UIA_ScrollVerticallyScrollablePropertyId:
            if (n.scroll)
                return boolean(out, n.scroll->verticalScrollable);
            break;
        case UIA_TableRowOrColumnMajorPropertyId:
            if (n.collection)
                return integer(out, RowOrColumnMajor_RowMajor);
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
            if (SUCCEEDED(hr))
                out->vt = VT_ARRAY | VT_UNKNOWN;
            return hr;
        }
        case UIA_PositionInSetPropertyId:
            if (n.collection)
                return integer(out, static_cast<int>(n.collection->positionInSet));
            break;
        case UIA_SizeOfSetPropertyId:
            if (n.collection)
                return integer(out, static_cast<int>(n.collection->setSize));
            break;
        case UIA_LevelPropertyId:
            if (n.collection)
                return integer(out, static_cast<int>(n.collection->level));
            break;
        case UIA_GridRowCountPropertyId:
            if (n.collection)
                return integer(out, static_cast<int>(n.collection->rowCount));
            break;
        case UIA_GridColumnCountPropertyId:
            if (n.collection)
                return integer(out, static_cast<int>(n.collection->columnCount));
            break;
        case UIA_GridItemRowPropertyId:
            if (n.collection)
                return integer(out, static_cast<int>(n.collection->rowIndex));
            break;
        case UIA_GridItemColumnPropertyId:
            if (n.collection)
                return integer(out, static_cast<int>(n.collection->columnIndex));
            break;
        case UIA_GridItemRowSpanPropertyId:
            if (n.collection)
                return integer(out, static_cast<int>(n.collection->rowSpan));
            break;
        case UIA_GridItemColumnSpanPropertyId:
            if (n.collection)
                return integer(out, static_cast<int>(n.collection->columnSpan));
            break;
        case UIA_LabeledByPropertyId:
        {
            const auto ids = relation(n, A::RelationKind::LabelledBy);
            if (!ids.empty())
            {
                out->punkVal = providerFor(*q.snapshot, ids.front());
                if (out->punkVal)
                    out->punkVal->AddRef();
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
            if (SUCCEEDED(hr))
                out->vt = VT_ARRAY | VT_UNKNOWN;
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
            return boolean(out, annotationSupported(q));
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
            if (!annotation)
                break;
            if (id == UIA_AnnotationAnnotationTypeIdPropertyId)
                return integer(
                    out,
                    annotation->kind == A::TextAnnotationKind::Spelling   ? AnnotationType_SpellingError
                    : annotation->kind == A::TextAnnotationKind::Emphasis ? AnnotationType_Highlighted
                                                                          : AnnotationType_Unknown);
            if (id == UIA_AnnotationAnnotationTypeNamePropertyId)
                return string(out, q.snapshot->textCache[source].annotationValues[annotationIndex]);
            if (id == UIA_AnnotationTargetPropertyId)
            {
                out->punkVal = providerFor(*q.snapshot, q.snapshot->storage.nodes[source].id);
                if (out->punkVal)
                    out->punkVal->AddRef();
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
    // ------------------------------------------------------------
    // Native action transport
    // ------------------------------------------------------------

    HRESULT action(const UiaQuery &q, A::ActionRequest request) noexcept
    {
        if (!q || !q.runtime->live.load())
            return kUnavailable;
        request.node = q.node->id;
        request.observedGeneration = q.snapshot->storage.generation;
        switch (submitAccessibilityAction(*q.runtime, std::move(request)).acceptance)
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

    // ------------------------------------------------------------
    // Provider identity and interfaces
    // ------------------------------------------------------------

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
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (IsEqualIID(iid, IID_IUnknown) || IsEqualIID(iid, __uuidof(IRawElementProviderSimple)))
            *out = static_cast<IRawElementProviderSimple *>(this);
#define DESKTOP_UIA_INTERFACE(Type) else if (IsEqualIID(iid, __uuidof(Type))) *out = static_cast<Type *>(this)
        DESKTOP_UIA_INTERFACE(IRawElementProviderFragment);
        else if (root_ && IsEqualIID(iid, __uuidof(IRawElementProviderFragmentRoot))) *out = static_cast<IRawElementProviderFragmentRoot *>(this);
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
        else if (IsEqualIID(iid, __uuidof(ITextProvider)) || IsEqualIID(iid, __uuidof(ITextProvider2))) *out = static_cast<ITextProvider2 *>(&text_);
#undef DESKTOP_UIA_INTERFACE
        if (!*out)
            return E_NOINTERFACE;
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
        if (remaining == 0)
            delete this;
        return remaining;
    }

    HRESULT NodeProvider::get_ProviderOptions(ProviderOptions *out)
    {
        if (!out)
            return E_POINTER;
        *out =
            static_cast<ProviderOptions>(ProviderOptions_ServerSideProvider | ProviderOptions_ProviderOwnsSetFocus | ProviderOptions_UseComThreading);
        return query() ? S_OK : kUnavailable;
    }

    HRESULT NodeProvider::GetPatternProvider(PATTERNID id, IUnknown **out)
    {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        const auto q = query();
        if (!q)
            return kUnavailable;
        if (!(id == UIA_AnnotationPatternId || id == UIA_TextChildPatternId ? annotationSupported(q) : patternSupported(*q.node, id)))
            return S_OK;
        switch (id)
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
        if (*out)
            (*out)->AddRef();
        return S_OK;
    }

    HRESULT NodeProvider::GetPropertyValue(PROPERTYID id, VARIANT *out)
    {
        return property(query(), id, out);
    }

    HRESULT NodeProvider::get_HostRawElementProvider(IRawElementProviderSimple **out)
    {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        const auto q = query();
        if (!q)
            return kUnavailable;
        return root_ ? UiaHostProviderFromHwnd(reinterpret_cast<HWND>(q.runtime->nativeWindow.load()), out) : S_OK;
    }
    // ------------------------------------------------------------
    // Fragment navigation and geometry
    // ------------------------------------------------------------

    HRESULT NodeProvider::Navigate(NavigateDirection direction, IRawElementProviderFragment **out)
    {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        const auto q = query();
        if (!q)
            return kUnavailable;
        A::NodeId target = 0;
        switch (direction)
        {
        case NavigateDirection_Parent:
            target = q.node->parent;
            break;
        case NavigateDirection_FirstChild:
            if (!q.node->children.empty())
                target = q.node->children.front();
            break;
        case NavigateDirection_LastChild:
            if (!q.node->children.empty())
                target = q.node->children.back();
            break;
        case NavigateDirection_NextSibling:
        case NavigateDirection_PreviousSibling:
            if (const auto *parent = q.snapshot->find(q.node->parent))
            {
                const auto children = parent->children;
                for (std::size_t i = 0; i < children.size(); ++i)
                    if (children[i] == id_)
                    {
                        if (direction == NavigateDirection_NextSibling && i + 1 < children.size())
                            target = children[i + 1];
                        if (direction == NavigateDirection_PreviousSibling && i != 0)
                            target = children[i - 1];
                        break;
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
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (!query())
            return kUnavailable;
        if (root_)
            return S_OK;
        auto *array = SafeArrayCreateVector(VT_I4, 0, 3);
        if (!array)
            return E_OUTOFMEMORY;
        const std::array<LONG, 3> values{
            UiaAppendRuntimeId,
            std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(id_)),
            std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(id_ >> 32))};
        for (LONG i = 0; i < 3; ++i)
        {
            LONG value = values[static_cast<std::size_t>(i)];
            const auto hr = SafeArrayPutElement(array, &i, &value);
            if (FAILED(hr))
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
        if (!out)
            return E_POINTER;
        *out = {};
        const auto q = query();
        if (!q)
            return kUnavailable;
        *out = nodeBounds(q);
        return S_OK;
    }

    HRESULT NodeProvider::GetEmbeddedFragmentRoots(SAFEARRAY **out)
    {
        if (!out)
            return E_POINTER;
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
        if (!out)
            return E_POINTER;
        *out = nullptr;
        const auto q = query();
        if (!q)
            return kUnavailable;
        auto *provider = providerFor(*q.snapshot, q.snapshot->storage.root);
        return provider ? provider->QueryInterface(__uuidof(IRawElementProviderFragmentRoot), reinterpret_cast<void **>(out)) : kUnavailable;
    }

    HRESULT NodeProvider::ElementProviderFromPoint(double x, double y, IRawElementProviderFragment **out)
    {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        const auto q = query();
        if (!q)
            return kUnavailable;
        if (!std::isfinite(x) || !std::isfinite(y))
            return E_INVALIDARG;
        const auto rootBounds = nodeBounds(q);
        if (x < rootBounds.left || y < rootBounds.top || x >= rootBounds.left + rootBounds.width || y >= rootBounds.top + rootBounds.height)
            return S_OK;
        if (!q.host || q.host->scaleX <= 0 || q.host->scaleY <= 0)
            return S_OK;
        const auto &host = *q.host;
        const double clientX = (x - host.x) / host.scaleX, clientY = (y - host.y) / host.scaleY;
        const auto &layout = static_cast<const UiaSnapshot *>(q.snapshot->native.get())->layout;
        A::NodeId best = q.snapshot->storage.root;
        std::uint32_t bestOrder = 0, bestOrdinal = 0;
        for (std::size_t i = 0; i < q.snapshot->storage.nodes.size(); ++i)
        {
            const auto &node = q.snapshot->storage.nodes[i];
            if (!node.geometry || !node.geometry->hitTestable || !layout[i].visible)
                continue;
            if (layout[i].clip && (clientX < layout[i].clip->x || clientY < layout[i].clip->y ||
                                   clientX >= layout[i].clip->x + layout[i].clip->width || clientY >= layout[i].clip->y + layout[i].clip->height))
                continue;
            const auto &t = node.geometry->toWindow;
            const auto &r = node.geometry->localBounds;
            const double determinant = t.m11 * t.m22 - t.m12 * t.m21;
            const double localX = (t.m22 * (clientX - t.tx) - t.m21 * (clientY - t.ty)) / determinant;
            const double localY = (-t.m12 * (clientX - t.tx) + t.m11 * (clientY - t.ty)) / determinant;
            if (localX < r.x || localY < r.y || localX >= r.x + r.width || localY >= r.y + r.height)
                continue;
            if (layout[i].order > bestOrder || (layout[i].order == bestOrder && layout[i].ordinal >= bestOrdinal))
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
        if (!out)
            return E_POINTER;
        *out = nullptr;
        const auto q = query();
        if (!q)
            return kUnavailable;
        if (!q.host || !q.host->focused)
            return S_OK;
        return q.snapshot->focusedNode != 0 ? fragment(*q.snapshot, q.snapshot->focusedNode, out) : S_OK;
    }

    HRESULT NodeProvider::AdviseEventAdded(EVENTID id, SAFEARRAY *properties)
    {
        static_cast<void>(id);
        static_cast<void>(properties);
        const auto q = query();
        if (!q)
            return kUnavailable;
        static_cast<UiaState *>(q.runtime->native.get())->listeners.fetch_add(1);
        return S_OK;
    }

    HRESULT NodeProvider::AdviseEventRemoved(EVENTID id, SAFEARRAY *properties)
    {
        static_cast<void>(id);
        static_cast<void>(properties);
        const auto q = query();
        if (!q)
            return kUnavailable;
        auto &listeners = static_cast<UiaState *>(q.runtime->native.get())->listeners;
        auto count = listeners.load();
        for (unsigned i = 0; i < 8 && count != 0; ++i)
            if (listeners.compare_exchange_weak(count, count - 1))
                break;
        return S_OK;
    }
    // ------------------------------------------------------------
    // Invoke, value, toggle, and expansion patterns
    // ------------------------------------------------------------

    HRESULT NodeProvider::Invoke()
    {
        return request(A::ActionKind::Invoke);
    }

    HRESULT NodeProvider::SetValue(LPCWSTR value)
    {
        if (!value)
            return E_INVALIDARG;
        const auto q = query();
        if (!q)
            return kUnavailable;
        if (valueHidden(*q.node))
            return kUnsupported;
        // UIA supplies a readable NUL-terminated string; bound the native scan before conversion/allocation.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
#endif
        const auto size = ::wcsnlen(value, static_cast<std::size_t>(q.runtime->limits.maximumActionTextBytes) + 1);
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
        if (size > q.runtime->limits.maximumActionTextBytes)
            return E_INVALIDARG;
        const std::u16string_view wide(reinterpret_cast<const char16_t *>(value), size);
        const auto measured = Unicode::Utf16::measureToUtf8(wide);
        if (measured.outcome != Unicode::Types::MeasureOutcome::Measured || measured.requiredBytes > q.runtime->limits.maximumActionTextBytes)
            return E_INVALIDARG;
        try
        {
            A::ActionRequest request;
            request.action = A::ActionKind::SetValue;
            request.text.resize(measured.requiredBytes);
            const auto converted = Unicode::Utf16::convertToUtf8(wide, request.text);
            if (converted.outcome != Unicode::Types::ConversionOutcome::Converted)
                return E_INVALIDARG;
            return action(q, std::move(request));
        }
        catch (...)
        {
            return E_OUTOFMEMORY;
        }
    }

    HRESULT NodeProvider::get_Value(BSTR *out)
    {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        const auto q = query();
        if (!q)
            return kUnavailable;
        return stringValue(valueHidden(*q.node) ? std::u16string_view{} : q.text().value, out);
    }

    HRESULT NodeProvider::get_IsReadOnly(BOOL *out)
    {
        if (!out)
            return E_POINTER;
        *out = TRUE;
        const auto q = query();
        if (!q)
            return kUnavailable;
        *out = contentReadOnly(*q.node);
        return S_OK;
    }

    HRESULT NodeProvider::SetValue(double value)
    {
        const auto q = query();
        if (!q)
            return kUnavailable;
        if (!q.node->rangeValue || valueHidden(*q.node))
            return kUnsupported;
        A::ActionRequest request;
        request.action = A::ActionKind::SetValue;
        request.value = value;
        return action(q, std::move(request));
    }

    HRESULT NodeProvider::rangeValue(double A::RangeValue::*member, double *out) noexcept
    {
        if (!out)
            return E_POINTER;
        *out = 0;
        const auto q = query();
        if (!q)
            return kUnavailable;
        if (!q.node->rangeValue || valueHidden(*q.node))
            return kUnsupported;
        *out = (*q.node->rangeValue).*member;
        return S_OK;
    }

    HRESULT NodeProvider::get_Value(double *out)
    {
        return rangeValue(&A::RangeValue::value, out);
    }

    HRESULT NodeProvider::get_Maximum(double *out)
    {
        return rangeValue(&A::RangeValue::maximum, out);
    }

    HRESULT NodeProvider::get_Minimum(double *out)
    {
        return rangeValue(&A::RangeValue::minimum, out);
    }

    HRESULT NodeProvider::get_LargeChange(double *out)
    {
        return rangeValue(&A::RangeValue::largeChange, out);
    }

    HRESULT NodeProvider::get_SmallChange(double *out)
    {
        return rangeValue(&A::RangeValue::smallChange, out);
    }

    HRESULT NodeProvider::Toggle()
    {
        return request(A::ActionKind::Toggle);
    }

    HRESULT NodeProvider::get_ToggleState(ToggleState *out)
    {
        if (!out)
            return E_POINTER;
        *out = ToggleState_Off;
        const auto q = query();
        if (!q)
            return kUnavailable;
        *out = q.node->states.contains(A::State::Mixed)                                                     ? ToggleState_Indeterminate
               : (q.node->states.contains(A::State::Checked) || q.node->states.contains(A::State::Pressed)) ? ToggleState_On
                                                                                                            : ToggleState_Off;
        return S_OK;
    }

    HRESULT NodeProvider::Expand()
    {
        return request(A::ActionKind::Expand);
    }

    HRESULT NodeProvider::Collapse()
    {
        return request(A::ActionKind::Collapse);
    }

    HRESULT NodeProvider::get_ExpandCollapseState(ExpandCollapseState *out)
    {
        if (!out)
            return E_POINTER;
        *out = ExpandCollapseState_LeafNode;
        const auto q = query();
        if (!q)
            return kUnavailable;
        if (patternSupported(*q.node, UIA_ExpandCollapsePatternId))
            *out = q.node->states.contains(A::State::Expanded) ? ExpandCollapseState_Expanded : ExpandCollapseState_Collapsed;
        return S_OK;
    }
    // ------------------------------------------------------------
    // Selection patterns
    // ------------------------------------------------------------

    HRESULT NodeProvider::GetSelection(SAFEARRAY **out)
    {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        const auto q = query();
        if (!q)
            return kUnavailable;
        if (!q.node->selection)
            return kUnsupported;
        return nodeArray(*q.snapshot, q.node->selection->selectedNodes, out);
    }

    HRESULT NodeProvider::get_CanSelectMultiple(BOOL *out)
    {
        if (!out)
            return E_POINTER;
        *out = FALSE;
        const auto q = query();
        if (!q)
            return kUnavailable;
        if (!q.node->selection)
            return kUnsupported;
        *out = q.node->selection->multiSelectable;
        return S_OK;
    }

    HRESULT NodeProvider::get_IsSelectionRequired(BOOL *out)
    {
        if (!out)
            return E_POINTER;
        *out = FALSE;
        const auto q = query();
        if (!q)
            return kUnavailable;
        if (!q.node->selection)
            return kUnsupported;
        *out = q.node->selection->selectionRequired;
        return S_OK;
    }

    HRESULT NodeProvider::Select()
    {
        return request(A::ActionKind::Select);
    }

    HRESULT NodeProvider::selectedItem(unsigned kind, IRawElementProviderSimple **out) noexcept
    {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        const auto q = query();
        if (!q)
            return kUnavailable;
        if (!q.node->selection)
            return kUnsupported;
        const auto &selection = *q.node->selection;
        A::NodeId selected = selection.activeNode;
        if (kind < 2 && !selection.selectedNodes.empty())
            selected = kind == 0 ? selection.selectedNodes.front() : selection.selectedNodes.back();
        *out = providerFor(*q.snapshot, selected);
        if (*out)
            (*out)->AddRef();
        return S_OK;
    }

    HRESULT NodeProvider::get_FirstSelectedItem(IRawElementProviderSimple **out)
    {
        return selectedItem(0, out);
    }

    HRESULT NodeProvider::get_LastSelectedItem(IRawElementProviderSimple **out)
    {
        return selectedItem(1, out);
    }

    HRESULT NodeProvider::get_CurrentSelectedItem(IRawElementProviderSimple **out)
    {
        return selectedItem(2, out);
    }

    HRESULT NodeProvider::get_ItemCount(int *out)
    {
        if (!out)
            return E_POINTER;
        *out = 0;
        const auto q = query();
        if (!q)
            return kUnavailable;
        if (!q.node->selection)
            return kUnsupported;
        *out = static_cast<int>(q.node->selection->selectedNodes.size());
        return S_OK;
    }

    HRESULT NodeProvider::AddToSelection()
    {
        return request(A::ActionKind::Select, A::SelectionOperation::Add);
    }

    HRESULT NodeProvider::RemoveFromSelection()
    {
        return request(A::ActionKind::Deselect, A::SelectionOperation::Remove);
    }

    HRESULT NodeProvider::get_IsSelected(BOOL *out)
    {
        if (!out)
            return E_POINTER;
        *out = FALSE;
        const auto q = query();
        if (!q)
            return kUnavailable;
        *out = q.node->states.contains(A::State::Selected);
        return S_OK;
    }

    HRESULT NodeProvider::ancestor(bool selection, IRawElementProviderSimple **out) noexcept
    {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        const auto q = query();
        if (!q)
            return kUnavailable;
        const auto &layout = static_cast<const UiaSnapshot *>(q.snapshot->native.get())->layout[q.index];
        *out = providerFor(*q.snapshot, selection ? layout.selectionContainer : layout.containingGrid);
        if (*out)
            (*out)->AddRef();
        return S_OK;
    }

    HRESULT NodeProvider::get_SelectionContainer(IRawElementProviderSimple **out)
    {
        return ancestor(true, out);
    }
    // ------------------------------------------------------------
    // Scroll patterns
    // ------------------------------------------------------------

    HRESULT NodeProvider::Scroll(ScrollAmount horizontal, ScrollAmount vertical)
    {
        const auto delta = [](ScrollAmount amount)
        {
            switch (amount)
            {
            case ScrollAmount_LargeDecrement:
                return -10.0;
            case ScrollAmount_SmallDecrement:
                return -1.0;
            case ScrollAmount_NoAmount:
                return 0.0;
            case ScrollAmount_LargeIncrement:
                return 10.0;
            case ScrollAmount_SmallIncrement:
                return 1.0;
            }
            return 0.0;
        };
        if (horizontal < ScrollAmount_LargeDecrement || horizontal > ScrollAmount_SmallIncrement || vertical < ScrollAmount_LargeDecrement ||
            vertical > ScrollAmount_SmallIncrement)
            return E_INVALIDARG;
        const auto q = query();
        if (!q)
            return kUnavailable;
        if (!q.node->scroll)
            return kUnsupported;
        if ((!q.node->scroll->horizontalScrollable && horizontal != ScrollAmount_NoAmount) ||
            (!q.node->scroll->verticalScrollable && vertical != ScrollAmount_NoAmount))
            return kUnsupported;
        A::ActionRequest request;
        request.action = A::ActionKind::ScrollBy;
        request.scroll = {delta(horizontal), delta(vertical), false, A::ScrollUnit::Step};
        return action(q, std::move(request));
    }

    HRESULT NodeProvider::SetScrollPercent(double horizontal, double vertical)
    {
        const auto q = query();
        if (!q)
            return kUnavailable;
        if (!q.node->scroll)
            return kUnsupported;
        if (!std::isfinite(horizontal) || !std::isfinite(vertical) || horizontal < -1 || horizontal > 100 || vertical < -1 || vertical > 100 ||
            (horizontal < 0 && horizontal != kNoScroll) || (vertical < 0 && vertical != kNoScroll))
            return E_INVALIDARG;
        if ((!q.node->scroll->horizontalScrollable && horizontal != kNoScroll) || (!q.node->scroll->verticalScrollable && vertical != kNoScroll))
            return kUnsupported;
        A::ActionRequest request;
        request.action = A::ActionKind::ScrollTo;
        request.scroll = {horizontal, vertical, true, A::ScrollUnit::Percent};
        return action(q, std::move(request));
    }

    HRESULT NodeProvider::scrollValue(double A::ScrollInfo::*member, double *out, bool horizontal) noexcept
    {
        if (!out)
            return E_POINTER;
        *out = 0;
        const auto q = query();
        if (!q)
            return kUnavailable;
        if (!q.node->scroll)
            return kUnsupported;
        const auto &s = *q.node->scroll;
        *out = (member == &A::ScrollInfo::horizontalPercent || member == &A::ScrollInfo::verticalPercent) &&
                       !(horizontal ? s.horizontalScrollable : s.verticalScrollable)
                   ? kNoScroll
                   : s.*member;
        return S_OK;
    }

    HRESULT NodeProvider::get_HorizontalScrollPercent(double *out)
    {
        return scrollValue(&A::ScrollInfo::horizontalPercent, out, true);
    }

    HRESULT NodeProvider::get_VerticalScrollPercent(double *out)
    {
        return scrollValue(&A::ScrollInfo::verticalPercent, out, false);
    }

    HRESULT NodeProvider::get_HorizontalViewSize(double *out)
    {
        return scrollValue(&A::ScrollInfo::horizontalViewSize, out, true);
    }

    HRESULT NodeProvider::get_VerticalViewSize(double *out)
    {
        return scrollValue(&A::ScrollInfo::verticalViewSize, out, false);
    }

    HRESULT NodeProvider::get_HorizontallyScrollable(BOOL *out)
    {
        if (!out)
            return E_POINTER;
        *out = FALSE;
        const auto q = query();
        if (!q)
            return kUnavailable;
        if (!q.node->scroll)
            return kUnsupported;
        *out = q.node->scroll->horizontalScrollable;
        return S_OK;
    }

    HRESULT NodeProvider::get_VerticallyScrollable(BOOL *out)
    {
        if (!out)
            return E_POINTER;
        *out = FALSE;
        const auto q = query();
        if (!q)
            return kUnavailable;
        if (!q.node->scroll)
            return kUnsupported;
        *out = q.node->scroll->verticalScrollable;
        return S_OK;
    }

    HRESULT NodeProvider::ScrollIntoView()
    {
        return request(A::ActionKind::ScrollTo);
    }
    // ------------------------------------------------------------
    // Grid and table patterns
    // ------------------------------------------------------------

    HRESULT NodeProvider::GetItem(int row, int column, IRawElementProviderSimple **out)
    {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        const auto q = query();
        if (!q)
            return kUnavailable;
        if (!q.node->collection)
            return kUnsupported;
        const auto &c = *q.node->collection;
        if (row < 0 || column < 0 || static_cast<std::uint32_t>(row) >= c.rowCount || static_cast<std::uint32_t>(column) >= c.columnCount)
            return E_INVALIDARG;
        const auto &nodes = q.snapshot->storage.nodes;
        const auto &layout = static_cast<const UiaSnapshot *>(q.snapshot->native.get())->layout;
        for (std::size_t i = 0; i < nodes.size(); ++i)
            if (const auto &n = nodes[i]; patternSupported(n, UIA_GridItemPatternId) && layout[i].containingGrid == id_)
            {
                const auto &item = *n.collection;
                if (static_cast<std::uint32_t>(row) >= item.rowIndex && static_cast<std::uint32_t>(row) - item.rowIndex < item.rowSpan &&
                    static_cast<std::uint32_t>(column) >= item.columnIndex && static_cast<std::uint32_t>(column) - item.columnIndex < item.columnSpan)
                {
                    *out = providerFor(*q.snapshot, n.id);
                    if (*out)
                        (*out)->AddRef();
                    return S_OK;
                }
            }
        return S_OK; // A partial virtualized snapshot does not invent unrealized providers.
    }

    HRESULT NodeProvider::collectionValue(std::uint32_t A::CollectionInfo::*member, int *out) noexcept
    {
        if (!out)
            return E_POINTER;
        *out = 0;
        const auto q = query();
        if (!q)
            return kUnavailable;
        if (!q.node->collection)
            return kUnsupported;
        *out = static_cast<int>((*q.node->collection).*member);
        return S_OK;
    }

    HRESULT NodeProvider::get_RowCount(int *out)
    {
        return collectionValue(&A::CollectionInfo::rowCount, out);
    }

    HRESULT NodeProvider::get_ColumnCount(int *out)
    {
        return collectionValue(&A::CollectionInfo::columnCount, out);
    }

    HRESULT NodeProvider::get_Row(int *out)
    {
        return collectionValue(&A::CollectionInfo::rowIndex, out);
    }

    HRESULT NodeProvider::get_Column(int *out)
    {
        return collectionValue(&A::CollectionInfo::columnIndex, out);
    }

    HRESULT NodeProvider::get_RowSpan(int *out)
    {
        return collectionValue(&A::CollectionInfo::rowSpan, out);
    }

    HRESULT NodeProvider::get_ColumnSpan(int *out)
    {
        return collectionValue(&A::CollectionInfo::columnSpan, out);
    }

    HRESULT NodeProvider::get_ContainingGrid(IRawElementProviderSimple **out)
    {
        return ancestor(false, out);
    }

    HRESULT NodeProvider::headers(A::RelationKind kind, SAFEARRAY **out) noexcept
    {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        const auto q = query();
        if (!q)
            return kUnavailable;
        return nodeArray(*q.snapshot, relation(*q.node, kind), out);
    }

    HRESULT NodeProvider::GetRowHeaders(SAFEARRAY **out)
    {
        return headers(A::RelationKind::RowHeaders, out);
    }

    HRESULT NodeProvider::GetColumnHeaders(SAFEARRAY **out)
    {
        return headers(A::RelationKind::ColumnHeaders, out);
    }

    HRESULT NodeProvider::get_RowOrColumnMajor(RowOrColumnMajor *out)
    {
        if (!out)
            return E_POINTER;
        *out = RowOrColumnMajor_RowMajor;
        return query() ? S_OK : kUnavailable;
    }

    HRESULT NodeProvider::GetRowHeaderItems(SAFEARRAY **out)
    {
        return headers(A::RelationKind::RowHeaders, out);
    }

    HRESULT NodeProvider::GetColumnHeaderItems(SAFEARRAY **out)
    {
        return headers(A::RelationKind::ColumnHeaders, out);
    }
    // ------------------------------------------------------------
    // Virtualization and known-item lookup
    // ------------------------------------------------------------

    HRESULT NodeProvider::Realize()
    {
        return request(A::ActionKind::Realize);
    }

    HRESULT NodeProvider::FindItemByProperty(IRawElementProviderSimple *start, PROPERTYID id, VARIANT value, IRawElementProviderSimple **out)
    {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        const auto q = query();
        if (!q)
            return kUnavailable;
        if (id != 0 && id != UIA_NamePropertyId && id != UIA_ControlTypePropertyId && id != UIA_SelectionItemIsSelectedPropertyId &&
            id != UIA_AutomationIdPropertyId)
            return kUnsupported;
        if ((id == UIA_NamePropertyId || id == UIA_AutomationIdPropertyId) && value.vt != VT_BSTR)
            return E_INVALIDARG;
        if (id == UIA_ControlTypePropertyId && value.vt != VT_I4)
            return E_INVALIDARG;
        if (id == UIA_SelectionItemIsSelectedPropertyId && value.vt != VT_BOOL)
            return E_INVALIDARG;
        bool past = start == nullptr;
        const auto &layout = static_cast<const UiaSnapshot *>(q.snapshot->native.get())->layout;
        std::uint32_t startOrdinal = 0;
        if (start)
        {
            bool found = false;
            for (std::size_t i = 0; i < q.snapshot->storage.nodes.size(); ++i)
                if (providerFor(*q.snapshot, q.snapshot->storage.nodes[i].id) == start)
                {
                    startOrdinal = layout[i].ordinal;
                    found = true;
                    break;
                }
            if (!found)
                return E_INVALIDARG;
            past = true;
        }
        A::NodeId best = 0;
        std::uint32_t bestOrdinal = std::numeric_limits<std::uint32_t>::max();
        for (std::size_t i = 0; i < q.snapshot->storage.nodes.size(); ++i)
        {
            const auto &n = q.snapshot->storage.nodes[i];
            if (!past || !descendant(*q.snapshot, n, id_) || (start && layout[i].ordinal <= startOrdinal))
                continue;
            bool matches = id == 0;
            if (id == UIA_ControlTypePropertyId)
                matches = controlType(n.role) == value.lVal;
            else if (id == UIA_SelectionItemIsSelectedPropertyId)
                matches = n.states.contains(A::State::Selected) == (value.boolVal != VARIANT_FALSE);
            else if (id == UIA_NamePropertyId)
            {
                const auto &name = q.snapshot->textCache[i].name;
                matches = n.exposure != A::Exposure::RedactContent && name.size() == SysStringLen(value.bstrVal) &&
                          std::equal(name.begin(), name.end(), reinterpret_cast<const char16_t *>(value.bstrVal));
            }
            else if (id == UIA_AutomationIdPropertyId)
            {
                std::array<char, 24> digits{};
                const auto converted = std::to_chars(digits.begin(), digits.end(), n.id);
                const auto length = static_cast<std::size_t>(converted.ptr - digits.data());
                const std::wstring_view supplied(value.bstrVal, SysStringLen(value.bstrVal));
                matches = supplied.size() == length;
                for (std::size_t j = 0; matches && j < supplied.size(); ++j)
                    matches = supplied[j] == digits[j];
            }
            if (matches && layout[i].ordinal < bestOrdinal)
            {
                best = n.id;
                bestOrdinal = layout[i].ordinal;
            }
        }
        if (best != 0)
        {
            *out = providerFor(*q.snapshot, best);
            if (*out)
                (*out)->AddRef();
        }
        return S_OK;
    }

    // ------------------------------------------------------------
    // Annotation and text-child patterns
    // ------------------------------------------------------------

    HRESULT NodeProvider::get_AnnotationTypeId(int *out)
    {
        if (!out)
            return E_POINTER;
        *out = AnnotationType_Unknown;
        const auto q = query();
        if (!q)
            return kUnavailable;
        std::size_t source = 0, index = 0;
        const auto *annotation = annotationFor(q, source, index);
        if (!annotation)
            return kUnsupported;
        if (annotation->kind == A::TextAnnotationKind::Spelling)
            *out = AnnotationType_SpellingError;
        else if (annotation->kind == A::TextAnnotationKind::Emphasis)
            *out = AnnotationType_Highlighted;
        return S_OK;
    }

    HRESULT NodeProvider::get_AnnotationTypeName(BSTR *out)
    {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        const auto q = query();
        if (!q)
            return kUnavailable;
        std::size_t source = 0, index = 0;
        if (!annotationFor(q, source, index))
            return kUnsupported;
        return stringValue(q.snapshot->textCache[source].annotationValues[index], out);
    }

    HRESULT NodeProvider::get_Author(BSTR *out)
    {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        const auto q = query();
        if (!q)
            return kUnavailable;
        return annotationSupported(q) ? stringValue({}, out) : kUnsupported;
    }

    HRESULT NodeProvider::get_DateTime(BSTR *out)
    {
        return get_Author(out);
    }

    HRESULT NodeProvider::get_Target(IRawElementProviderSimple **out)
    {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        const auto q = query();
        if (!q)
            return kUnavailable;
        std::size_t source = 0, index = 0;
        if (!annotationFor(q, source, index))
            return kUnsupported;
        *out = providerFor(*q.snapshot, q.snapshot->storage.nodes[source].id);
        if (*out)
            (*out)->AddRef();
        return S_OK;
    }

    HRESULT NodeProvider::get_TextContainer(IRawElementProviderSimple **out)
    {
        return get_Target(out);
    }

    HRESULT NodeProvider::get_TextRange(ITextRangeProvider **out)
    {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        const auto q = query();
        if (!q)
            return kUnavailable;
        std::size_t source = 0, index = 0;
        const auto *annotation = annotationFor(q, source, index);
        if (!annotation)
            return kUnsupported;
        UiaQuery sourceQuery{q.runtime, q.snapshot, &q.snapshot->storage.nodes[source], source};
        return static_cast<UiaState *>(q.runtime->native.get())->range(sourceQuery, annotation->range, out);
    }

    // ------------------------------------------------------------
    // Backend activation and transactional provider preparation
    // ------------------------------------------------------------

    IO::Types::Status enableAccessibility(WindowState &window, const std::shared_ptr<AccessibilityRuntime> &runtime) noexcept
    {
        try
        {
            const auto *data = static_cast<const WindowData *>(window.platform.get());
            if (!data || !data->handle)
                return IO::makeStatus(IO::Types::ErrorCode::NotOpen);
            auto native = std::make_shared<UiaState>();
            native->ranges.reserve(runtime->limits.maximumNativeTextRanges);
            for (std::uint32_t i = 0; i < runtime->limits.maximumNativeTextRanges; ++i)
                native->ranges.push_back(std::make_unique<TextRangeProvider>());
            if (const auto module = GetModuleHandleW(L"uiautomationcore.dll"))
                native->raiseNotification = std::bit_cast<RaiseNotification>(GetProcAddress(module, "UiaRaiseNotificationEvent"));
            runtime->nativeThread = data->ownerThreadId;
            runtime->hostEnabled.store(IsWindowEnabled(data->handle) != FALSE);
            runtime->nativeWindow.store(reinterpret_cast<std::uintptr_t>(data->handle));
            runtime->native = std::move(native);
            runtime->features.flags = (std::uint64_t{1} << static_cast<unsigned>(A::Feature::Count)) - 1;
            runtime->features.flags &= ~(std::uint64_t{1} << static_cast<unsigned>(A::Feature::AnnouncementLanguage));
            if (!static_cast<UiaState *>(runtime->native.get())->raiseNotification)
                runtime->features.flags &= ~(std::uint64_t{1} << static_cast<unsigned>(A::Feature::Announcements));
            return {};
        }
        catch (const std::bad_alloc &)
        {
            return IO::makeStatus(IO::Types::ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return IO::makeStatus(IO::Types::ErrorCode::SizeLimitExceeded);
        }
    }

    IO::Types::Status prepareAccessibilitySnapshot(const std::shared_ptr<AccessibilityRuntime> &runtime, PublishedSnapshot &snapshot) noexcept
    {
        auto &native = *static_cast<UiaState *>(runtime->native.get());
        try
        {
            std::size_t missing = 0;
            for (const auto &node : snapshot.storage.nodes)
                if (!native.providers.contains(node.id))
                    ++missing;
            if (missing > runtime->limits.maximumNativeProviders - native.providers.size())
                return IO::makeStatus(IO::Types::ErrorCode::SizeLimitExceeded);
            auto result = std::make_shared<UiaSnapshot>();
            result->providers.reserve(snapshot.storage.nodes.size());
            result->layout.resize(snapshot.storage.nodes.size());
            result->annotationSources.resize(snapshot.storage.nodes.size(), {snapshot.storage.nodes.size(), 0});
            for (std::size_t i = 0; i < snapshot.storage.nodes.size(); ++i)
                if (const auto &text = snapshot.storage.nodes[i].text)
                    for (std::size_t j = 0; j < text->annotations.size(); ++j)
                        if (const auto target = text->annotations[j].target; target != 0)
                        {
                            auto &reference = result->annotationSources[snapshot.findIndex(target)];
                            if (reference.first == snapshot.storage.nodes.size())
                                reference = {i, j};
                        }

            struct Additions
            {
                std::map<A::NodeId, NodeProvider *> values;
                ~Additions()
                {
                    for (auto [id, provider] : values)
                    {
                        static_cast<void>(id);
                        provider->Release();
                    }
                }
            } additions;
            try
            {
                for (const auto &node : snapshot.storage.nodes)
                    if (!native.providers.contains(node.id))
                    {
                        auto *provider = new NodeProvider(runtime, node.id, node.id == snapshot.storage.root);
                        try
                        {
                            additions.values.emplace(node.id, provider);
                        }
                        catch (...)
                        {
                            provider->Release();
                            throw;
                        }
                    }
            }
            catch (...)
            {
                throw;
            }

            std::vector<A::NodeId> pending;
            pending.reserve(snapshot.storage.nodes.size());
            pending.push_back(snapshot.storage.root);
            std::vector<std::size_t> traversal;
            traversal.reserve(snapshot.storage.nodes.size());
            std::uint32_t ordinal = 0;
            while (!pending.empty())
            {
                const auto id = pending.back();
                pending.pop_back();
                const auto index = snapshot.findIndex(id);
                const auto &node = snapshot.storage.nodes[index];
                auto &layout = result->layout[index];
                layout.ordinal = ordinal++;
                layout.subtreeEnd = ordinal;
                traversal.push_back(index);
                if (node.parent != 0)
                {
                    const auto parentIndex = snapshot.findIndex(node.parent);
                    const auto &parent = result->layout[parentIndex];
                    const auto &parentNode = snapshot.storage.nodes[parentIndex];
                    layout.clip = parent.clip;
                    layout.visible = parent.visible;
                    layout.selectionContainer = parentNode.selection ? parentNode.id : parent.selectionContainer;
                    layout.containingGrid = parentNode.role == A::Role::Table && parentNode.collection ? parentNode.id : parent.containingGrid;
                }
                layout.visible = layout.visible && !node.states.contains(A::State::Invisible);
                if (node.geometry)
                {
                    layout.bounds = transformed(*node.geometry);
                    layout.visible = layout.visible && node.geometry->visible;
                    layout.order = node.geometry->hitTestOrder;
                    if (node.geometry->clippingBounds)
                        layout.clip = layout.clip ? intersection(*layout.clip, *node.geometry->clippingBounds) : node.geometry->clippingBounds;
                }
                for (auto child = node.children.rbegin(); child != node.children.rend(); ++child)
                    pending.push_back(*child);
            }
            for (auto index = traversal.rbegin(); index != traversal.rend(); ++index)
                if (const auto parent = snapshot.storage.nodes[*index].parent; parent != 0)
                {
                    auto &layout = result->layout[snapshot.findIndex(parent)];
                    layout.subtreeEnd = std::max(layout.subtreeEnd, result->layout[*index].subtreeEnd);
                }
            // No fallible work after the identity registry merge: map node transfer does not allocate.
            native.providers.merge(additions.values);
            for (const auto &node : snapshot.storage.nodes)
            {
                auto *provider = static_cast<IRawElementProviderSimple *>(native.providers.find(node.id)->second);
                provider->AddRef();
                result->providers.push_back(provider);
            }

            snapshot.native = std::move(result);
            return {};
        }
        catch (const std::bad_alloc &)
        {
            return IO::makeStatus(IO::Types::ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return IO::makeStatus(IO::Types::ErrorCode::SizeLimitExceeded);
        }
    }
    // ------------------------------------------------------------
    // Native lifetime and notification delivery
    // ------------------------------------------------------------

    void detachAccessibility(AccessibilityRuntime &runtime) noexcept
    {
        auto *native = static_cast<UiaState *>(runtime.native.get());
        if (!native)
            return;
        if (runtime.nativeThread == GetCurrentThreadId())
            static_cast<void>(UiaReturnRawElementProvider(reinterpret_cast<HWND>(runtime.nativeWindow.load()), 0, 0, nullptr));
        for (auto [id, provider] : native->providers)
        {
            static_cast<void>(id);
            static_cast<void>(UiaDisconnectProvider(static_cast<IRawElementProviderSimple *>(provider)));
            provider->Release();
        }
        native->providers.clear();
    }

    void wakeAccessibility(const AccessibilityRuntime &runtime) noexcept
    {
        if (const auto message = wakeMessage())
            static_cast<void>(PostThreadMessageW(runtime.nativeThread, message, 0, 0));
    }

    bool accessibilityGetObject(WindowState &state, HWND hwnd, WPARAM wParam, LPARAM lParam, LRESULT &out) noexcept
    {
        if (static_cast<LONG>(lParam) != UiaRootObjectId || !state.accessibility)
            return false;
        const auto runtime = state.accessibility->runtime.load();
        if (!runtime || !runtime->live.load())
            return false;
        const auto snapshot = runtime->snapshot.load();
        if (!snapshot)
            return false;
        auto *provider = providerFor(*snapshot, snapshot->storage.root);
        if (!provider)
            return false;
        static_cast<WindowData *>(state.platform.get())->accessibilityExposed = true;
        out = UiaReturnRawElementProvider(hwnd, wParam, lParam, provider);
        return true;
    }

    void accessibilityWindowDestroyed(WindowState &state, HWND hwnd) noexcept
    {
        if (auto *data = static_cast<WindowData *>(state.platform.get()); data && data->accessibilityExposed)
        {
            static_cast<void>(UiaReturnRawElementProvider(hwnd, 0, 0, nullptr));
            data->accessibilityExposed = false;
        }
        closeAccessibility(state);
    }

    void deliverAccessibilityNotification(AccessibilityRuntime &runtime, const AccessibilityPendingNotification &event) noexcept
    {
        if (!runtime.live.load() || !event.after || !UiaClientsAreListening())
            return;
        auto *provider = providerFor(*event.after, event.notification.node);
        if (!provider)
            return;
        const auto *node = event.after->find(event.notification.node);
        if (!node)
            return;
        const auto currentSnapshot = runtime.snapshot.load();
        const auto *currentNode = currentSnapshot ? currentSnapshot->find(node->id) : nullptr;
        if (!currentNode)
            return;
        const auto &notification = event.notification;
        switch (notification.kind)
        {
        case A::NotificationKind::TreeInvalidated:
        case A::NotificationKind::StructureChanged:
            static_cast<void>(UiaRaiseStructureChangedEvent(provider, StructureChangeType_ChildrenInvalidated, nullptr, 0));
            break;
        case A::NotificationKind::FocusChanged:
        {
            AccessibilityHostGeometry host;
            if (runtime.readGeometry(host) && host.focused && currentNode->states.contains(A::State::Focused))
                static_cast<void>(UiaRaiseAutomationEvent(provider, UIA_AutomationFocusChangedEventId));
            break;
        }
        case A::NotificationKind::SelectionChanged:
            if (notification.property == A::PropertyKind::Text)
            {
                if (!textHidden(*node) && !textHidden(*currentNode))
                    static_cast<void>(UiaRaiseAutomationEvent(provider, UIA_Text_TextSelectionChangedEventId));
            }
            else if (node->selection)
                static_cast<void>(UiaRaiseAutomationEvent(provider, UIA_Selection_InvalidatedEventId));
            else if (node->states.contains(A::State::Selectable))
            {
                const auto containerId =
                    static_cast<const UiaSnapshot *>(event.after->native.get())->layout[event.after->findIndex(node->id)].selectionContainer;
                const auto *container = event.after->find(containerId);
                const auto eventId = !node->states.contains(A::State::Selected) ? UIA_SelectionItem_ElementRemovedFromSelectionEventId
                                     : container && container->selection && container->selection->multiSelectable
                                         ? UIA_SelectionItem_ElementAddedToSelectionEventId
                                         : UIA_SelectionItem_ElementSelectedEventId;
                static_cast<void>(UiaRaiseAutomationEvent(provider, eventId));
            }
            else
                static_cast<void>(UiaRaiseStructureChangedEvent(provider, StructureChangeType_ChildrenInvalidated, nullptr, 0));
            break;
        case A::NotificationKind::TextChanged:
            if (!textHidden(*node) && !textHidden(*currentNode))
                static_cast<void>(UiaRaiseAutomationEvent(provider, UIA_Text_TextChangedEventId));
            break;
        case A::NotificationKind::LiveRegionChanged:
        {
            auto *native = static_cast<UiaState *>(runtime.native.get());
            if (!native->raiseNotification || textHidden(*node) || textHidden(*currentNode))
                break;
            BSTR text = nullptr, activity = nullptr;
            if (SUCCEEDED(stringValue(event.announcement, &text)) && SUCCEEDED(stringValue(u"GameWIP.Announcement", &activity)))
                static_cast<void>(native->raiseNotification(
                    provider,
                    NotificationKind_Other,
                    event.priority == A::AnnouncementPriority::High  ? NotificationProcessing_ImportantAll
                    : event.priority == A::AnnouncementPriority::Low ? NotificationProcessing_MostRecent
                                                                     : NotificationProcessing_All,
                    text,
                    activity));
            SysFreeString(text);
            SysFreeString(activity);
            break;
        }
        case A::NotificationKind::PropertyChanged:
        {
            // Values exposed in property events are subject to the same redaction as direct queries.
            auto owningRuntime = static_cast<NodeProvider *>(provider)->query().runtime;
            if (!owningRuntime)
                break;
            switch (notification.property)
            {
            case A::PropertyKind::Name:
                propertyEvent(event, provider, UIA_NamePropertyId, owningRuntime);
                break;
            case A::PropertyKind::Description:
                propertyEvent(event, provider, UIA_FullDescriptionPropertyId, owningRuntime);
                break;
            case A::PropertyKind::Value:
                propertyEvent(event, provider, UIA_ValueValuePropertyId, owningRuntime);
                propertyEvent(event, provider, UIA_RangeValueValuePropertyId, owningRuntime);
                break;
            case A::PropertyKind::Geometry:
                propertyEvent(event, provider, UIA_BoundingRectanglePropertyId, owningRuntime);
                propertyEvent(event, provider, UIA_IsOffscreenPropertyId, owningRuntime);
                break;
            case A::PropertyKind::State:
                for (const auto id :
                     {UIA_IsEnabledPropertyId,
                      UIA_IsOffscreenPropertyId,
                      UIA_IsPasswordPropertyId,
                      UIA_ValueIsReadOnlyPropertyId,
                      UIA_RangeValueIsReadOnlyPropertyId,
                      UIA_IsValuePatternAvailablePropertyId,
                      UIA_IsRangeValuePatternAvailablePropertyId,
                      UIA_IsTextPatternAvailablePropertyId,
                      UIA_IsTextPattern2AvailablePropertyId,
                      UIA_IsKeyboardFocusablePropertyId,
                      UIA_HasKeyboardFocusPropertyId,
                      UIA_ToggleToggleStatePropertyId,
                      UIA_ExpandCollapseExpandCollapseStatePropertyId,
                      UIA_SelectionItemIsSelectedPropertyId,
                      UIA_IsRequiredForFormPropertyId,
                      UIA_IsDataValidForFormPropertyId})
                    propertyEvent(event, provider, id, owningRuntime);
                break;
            case A::PropertyKind::Range:
                for (const auto id :
                     {UIA_RangeValueMinimumPropertyId,
                      UIA_RangeValueMaximumPropertyId,
                      UIA_RangeValueSmallChangePropertyId,
                      UIA_RangeValueLargeChangePropertyId})
                    propertyEvent(event, provider, id, owningRuntime);
                break;
            case A::PropertyKind::Exposure:
                static_cast<void>(UiaRaiseStructureChangedEvent(provider, StructureChangeType_ChildrenInvalidated, nullptr, 0));
                break;
            case A::PropertyKind::Metadata:
                for (const auto id :
                     {UIA_HelpTextPropertyId,
                      UIA_CulturePropertyId,
                      UIA_AccessKeyPropertyId,
                      UIA_AcceleratorKeyPropertyId,
                      UIA_IsTextPatternAvailablePropertyId,
                      UIA_IsSelectionPatternAvailablePropertyId,
                      UIA_IsValuePatternAvailablePropertyId,
                      UIA_PositionInSetPropertyId,
                      UIA_SizeOfSetPropertyId,
                      UIA_LevelPropertyId,
                      UIA_GridRowCountPropertyId,
                      UIA_GridColumnCountPropertyId})
                    propertyEvent(event, provider, id, owningRuntime);
                static_cast<void>(UiaRaiseStructureChangedEvent(provider, StructureChangeType_ChildrenInvalidated, nullptr, 0));
                break;
            case A::PropertyKind::Scroll:
                for (const auto id :
                     {UIA_ScrollHorizontalScrollPercentPropertyId,
                      UIA_ScrollVerticalScrollPercentPropertyId,
                      UIA_ScrollHorizontalViewSizePropertyId,
                      UIA_ScrollVerticalViewSizePropertyId,
                      UIA_ScrollHorizontallyScrollablePropertyId,
                      UIA_ScrollVerticallyScrollablePropertyId})
                    propertyEvent(event, provider, id, owningRuntime);
                break;
            case A::PropertyKind::Selection:
            case A::PropertyKind::Text:
                break;
            }
            break;
        }
        }
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
        if (provider)
            provider->AddRef();
        return provider;
    }
} // namespace GameWIP::Desktop::TestHooks
#endif
