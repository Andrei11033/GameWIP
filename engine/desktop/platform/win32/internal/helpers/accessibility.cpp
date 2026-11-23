/// @file accessibility.cpp
/// @brief Shared UI Automation projection helpers for Desktop's Win32 backend.

#include "desktop/platform/win32/internal/win32_accessibility_helpers.h"

#include <algorithm>
#include <array>
#include <limits>

namespace GameWIP::Desktop::Platform
{
    /// ------------------------------------------------------------
    /// Geometry and native semantic mapping
    /// ------------------------------------------------------------

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

        for(unsigned i = 0; i < 4; ++i)
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
        switch(role)
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
        switch(id)
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
        if(!q)
        {
            return nullptr;
        }

        const auto &sources = static_cast<const UiaSnapshot *>(q.snapshot->native.get())->annotationSources;
        const auto reference = sources[q.index];
        source = reference.first;
        index = reference.second;

        if(source >= q.snapshot->storage.nodes.size())
        {
            return nullptr;
        }
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

        if(SUCCEEDED(hr))
        {
            out->vt = VT_BSTR;
        }

        return hr;
    }

    std::span<const A::NodeId> relation(const A::Node &node, A::RelationKind kind) noexcept
    {
        for(const auto &r : node.relations)
        {
            if(r.kind == kind)
            {
                return r.targets;
            }
        }

        return {};
    }

    bool descendant(const PublishedSnapshot &snapshot, const A::Node &node, A::NodeId ancestor) noexcept
    {
        const auto nodeIndex = snapshot.findIndex(node.id), ancestorIndex = snapshot.findIndex(ancestor);

        if(nodeIndex >= snapshot.storage.nodes.size() || ancestorIndex >= snapshot.storage.nodes.size())
        {
            return false;
        }
        const auto &layout = static_cast<const UiaSnapshot *>(snapshot.native.get())->layout;
        return layout[nodeIndex].ordinal > layout[ancestorIndex].ordinal && layout[nodeIndex].ordinal < layout[ancestorIndex].subtreeEnd;
    }

} // namespace GameWIP::Desktop::Platform
