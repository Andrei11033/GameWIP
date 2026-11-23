/// @file win32_accessibility_patterns.cpp
/// @brief Implements UI Automation control patterns for the Win32 backend.

#include "desktop/platform/win32/internal/win32_accessibility_helpers.h"
#include "desktop/platform/win32/internal/win32_accessibility_provider.h"
#include "unicode/unicode.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cwchar>
#include <limits>

namespace GameWIP::Desktop::Platform
{
    /// ------------------------------------------------------------
    /// Invoke, value, toggle, and expansion patterns
    /// ------------------------------------------------------------

    HRESULT NodeProvider::Invoke()
    {
        return request(A::ActionKind::Invoke);
    }

    HRESULT NodeProvider::SetValue(LPCWSTR value)
    {
        if(!value)
        {
            return E_INVALIDARG;
        }

        const auto q = query();

        if(!q)
        {
            return kUnavailable;
        }

        if(valueHidden(*q.node))
        {
            return kUnsupported;
        }

        // UIA supplies a readable NUL-terminated string; bound the native scan before conversion/allocation.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
#endif
        const auto size = ::wcsnlen(value, static_cast<std::size_t>(q.runtime->limits.maximumActionTextBytes) + 1);
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
        if(size > q.runtime->limits.maximumActionTextBytes)
        {
            return E_INVALIDARG;
        }
        const std::u16string_view wide(reinterpret_cast<const char16_t *>(value), size);
        const auto measured = Unicode::Utf16::measureToUtf8(wide);

        if(measured.outcome != Unicode::Types::MeasureOutcome::Measured || measured.requiredBytes > q.runtime->limits.maximumActionTextBytes)
        {
            return E_INVALIDARG;
        }

        try
        {
            A::ActionRequest request;
            request.action = A::ActionKind::SetValue;
            request.text.resize(measured.requiredBytes);
            const auto converted = Unicode::Utf16::convertToUtf8(wide, request.text);

            if(converted.outcome != Unicode::Types::ConversionOutcome::Converted)
            {
                return E_INVALIDARG;
            }
            return action(q, std::move(request));
        }
        catch(...)
        {
            return E_OUTOFMEMORY;
        }
    }

    HRESULT NodeProvider::get_Value(BSTR *out)
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

        return stringValue(valueHidden(*q.node) ? std::u16string_view{} : q.text().value, out);
    }

    HRESULT NodeProvider::get_IsReadOnly(BOOL *out)
    {
        if(!out)
        {
            return E_POINTER;
        }
        *out = TRUE;
        const auto q = query();

        if(!q)
        {
            return kUnavailable;
        }

        *out = contentReadOnly(*q.node);
        return S_OK;
    }

    HRESULT NodeProvider::SetValue(double value)
    {
        const auto q = query();

        if(!q)
        {
            return kUnavailable;
        }

        if(!q.node->rangeValue || valueHidden(*q.node))
        {
            return kUnsupported;
        }

        A::ActionRequest request;
        request.action = A::ActionKind::SetValue;
        request.value = value;
        return action(q, std::move(request));
    }

    HRESULT NodeProvider::rangeValue(double A::RangeValue::*member, double *out) noexcept
    {
        if(!out)
        {
            return E_POINTER;
        }
        *out = 0;
        const auto q = query();

        if(!q)
        {
            return kUnavailable;
        }

        if(!q.node->rangeValue || valueHidden(*q.node))
        {
            return kUnsupported;
        }
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
        if(!out)
        {
            return E_POINTER;
        }
        *out = ToggleState_Off;
        const auto q = query();

        if(!q)
        {
            return kUnavailable;
        }

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
        if(!out)
        {
            return E_POINTER;
        }
        *out = ExpandCollapseState_LeafNode;
        const auto q = query();

        if(!q)
        {
            return kUnavailable;
        }

        if(patternSupported(*q.node, UIA_ExpandCollapsePatternId))
        {
            *out = q.node->states.contains(A::State::Expanded) ? ExpandCollapseState_Expanded : ExpandCollapseState_Collapsed;
        }

        return S_OK;
    }

    /// ------------------------------------------------------------
    /// Selection patterns
    /// ------------------------------------------------------------

    HRESULT NodeProvider::GetSelection(SAFEARRAY **out)
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

        if(!q.node->selection)
        {
            return kUnsupported;
        }
        return nodeArray(*q.snapshot, q.node->selection->selectedNodes, out);
    }

    HRESULT NodeProvider::get_CanSelectMultiple(BOOL *out)
    {
        if(!out)
        {
            return E_POINTER;
        }
        *out = FALSE;
        const auto q = query();

        if(!q)
        {
            return kUnavailable;
        }

        if(!q.node->selection)
        {
            return kUnsupported;
        }
        *out = q.node->selection->multiSelectable;
        return S_OK;
    }

    HRESULT NodeProvider::get_IsSelectionRequired(BOOL *out)
    {
        if(!out)
        {
            return E_POINTER;
        }
        *out = FALSE;
        const auto q = query();

        if(!q)
        {
            return kUnavailable;
        }

        if(!q.node->selection)
        {
            return kUnsupported;
        }
        *out = q.node->selection->selectionRequired;
        return S_OK;
    }

    HRESULT NodeProvider::Select()
    {
        return request(A::ActionKind::Select);
    }

    HRESULT NodeProvider::selectedItem(unsigned kind, IRawElementProviderSimple **out) noexcept
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

        if(!q.node->selection)
        {
            return kUnsupported;
        }
        const auto &selection = *q.node->selection;
        A::NodeId selected = selection.activeNode;

        if(kind < 2 && !selection.selectedNodes.empty())
        {
            selected = kind == 0 ? selection.selectedNodes.front() : selection.selectedNodes.back();
        }
        *out = providerFor(*q.snapshot, selected);

        if(*out)
        {
            (*out)->AddRef();
        }

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
        if(!out)
        {
            return E_POINTER;
        }
        *out = 0;
        const auto q = query();

        if(!q)
        {
            return kUnavailable;
        }

        if(!q.node->selection)
        {
            return kUnsupported;
        }
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
        if(!out)
        {
            return E_POINTER;
        }
        *out = FALSE;
        const auto q = query();

        if(!q)
        {
            return kUnavailable;
        }

        *out = q.node->states.contains(A::State::Selected);
        return S_OK;
    }

    HRESULT NodeProvider::ancestor(bool selection, IRawElementProviderSimple **out) noexcept
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

        const auto &layout = static_cast<const UiaSnapshot *>(q.snapshot->native.get())->layout[q.index];
        *out = providerFor(*q.snapshot, selection ? layout.selectionContainer : layout.containingGrid);

        if(*out)
        {
            (*out)->AddRef();
        }

        return S_OK;
    }

    HRESULT NodeProvider::get_SelectionContainer(IRawElementProviderSimple **out)
    {
        return ancestor(true, out);
    }

    /// ------------------------------------------------------------
    /// Scroll patterns
    /// ------------------------------------------------------------

    HRESULT NodeProvider::Scroll(ScrollAmount horizontal, ScrollAmount vertical)
    {
        const auto delta = [](ScrollAmount amount)
        {
            switch(amount)
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

        if(horizontal < ScrollAmount_LargeDecrement || horizontal > ScrollAmount_SmallIncrement || vertical < ScrollAmount_LargeDecrement ||
           vertical > ScrollAmount_SmallIncrement)
        {
            return E_INVALIDARG;
        }

        const auto q = query();

        if(!q)
        {
            return kUnavailable;
        }

        if(!q.node->scroll)
        {
            return kUnsupported;
        }

        if((!q.node->scroll->horizontalScrollable && horizontal != ScrollAmount_NoAmount) ||
           (!q.node->scroll->verticalScrollable && vertical != ScrollAmount_NoAmount))
        {
            return kUnsupported;
        }

        A::ActionRequest request;
        request.action = A::ActionKind::ScrollBy;
        request.scroll = {delta(horizontal), delta(vertical), false, A::ScrollUnit::Step};
        return action(q, std::move(request));
    }

    HRESULT NodeProvider::SetScrollPercent(double horizontal, double vertical)
    {
        const auto q = query();

        if(!q)
        {
            return kUnavailable;
        }

        if(!q.node->scroll)
        {
            return kUnsupported;
        }

        if(!std::isfinite(horizontal) || !std::isfinite(vertical) || horizontal < -1 || horizontal > 100 || vertical < -1 || vertical > 100 ||
           (horizontal < 0 && horizontal != kNoScroll) || (vertical < 0 && vertical != kNoScroll))
        {
            return E_INVALIDARG;
        }

        if((!q.node->scroll->horizontalScrollable && horizontal != kNoScroll) || (!q.node->scroll->verticalScrollable && vertical != kNoScroll))
        {
            return kUnsupported;
        }

        A::ActionRequest request;
        request.action = A::ActionKind::ScrollTo;
        request.scroll = {horizontal, vertical, true, A::ScrollUnit::Percent};
        return action(q, std::move(request));
    }

    HRESULT NodeProvider::scrollValue(double A::ScrollInfo::*member, double *out, bool horizontal) noexcept
    {
        if(!out)
        {
            return E_POINTER;
        }
        *out = 0;
        const auto q = query();

        if(!q)
        {
            return kUnavailable;
        }

        if(!q.node->scroll)
        {
            return kUnsupported;
        }
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
        if(!out)
        {
            return E_POINTER;
        }
        *out = FALSE;
        const auto q = query();

        if(!q)
        {
            return kUnavailable;
        }

        if(!q.node->scroll)
        {
            return kUnsupported;
        }
        *out = q.node->scroll->horizontalScrollable;
        return S_OK;
    }

    HRESULT NodeProvider::get_VerticallyScrollable(BOOL *out)
    {
        if(!out)
        {
            return E_POINTER;
        }
        *out = FALSE;
        const auto q = query();

        if(!q)
        {
            return kUnavailable;
        }

        if(!q.node->scroll)
        {
            return kUnsupported;
        }
        *out = q.node->scroll->verticalScrollable;
        return S_OK;
    }

    HRESULT NodeProvider::ScrollIntoView()
    {
        return request(A::ActionKind::ScrollTo);
    }

    /// ------------------------------------------------------------
    /// Grid and table patterns
    /// ------------------------------------------------------------

    HRESULT NodeProvider::GetItem(int row, int column, IRawElementProviderSimple **out)
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

        if(!q.node->collection)
        {
            return kUnsupported;
        }
        const auto &c = *q.node->collection;

        if(row < 0 || column < 0 || static_cast<std::uint32_t>(row) >= c.rowCount || static_cast<std::uint32_t>(column) >= c.columnCount)
        {
            return E_INVALIDARG;
        }

        const auto &nodes = q.snapshot->storage.nodes;
        const auto &layout = static_cast<const UiaSnapshot *>(q.snapshot->native.get())->layout;

        for(std::size_t i = 0; i < nodes.size(); ++i)
        {
            if(const auto &n = nodes[i]; n.collection && patternSupported(n, UIA_GridItemPatternId) && layout[i].containingGrid == id_)
            {
                const auto &item = *n.collection;

                if(static_cast<std::uint32_t>(row) >= item.rowIndex && static_cast<std::uint32_t>(row) - item.rowIndex < item.rowSpan &&
                   static_cast<std::uint32_t>(column) >= item.columnIndex && static_cast<std::uint32_t>(column) - item.columnIndex < item.columnSpan)
                {
                    *out = providerFor(*q.snapshot, n.id);

                    if(*out)
                    {
                        (*out)->AddRef();
                    }

                    return S_OK;
                }
            }
        }

        return S_OK; // A partial virtualized snapshot does not invent unrealized providers.
    }

    HRESULT NodeProvider::collectionValue(std::uint32_t A::CollectionInfo::*member, int *out) noexcept
    {
        if(!out)
        {
            return E_POINTER;
        }
        *out = 0;
        const auto q = query();

        if(!q)
        {
            return kUnavailable;
        }

        if(!q.node->collection)
        {
            return kUnsupported;
        }
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
        if(!out)
        {
            return E_POINTER;
        }
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

    /// ------------------------------------------------------------
    /// Virtualization and known-item lookup
    /// ------------------------------------------------------------

    HRESULT NodeProvider::Realize()
    {
        return request(A::ActionKind::Realize);
    }

    HRESULT NodeProvider::FindItemByProperty(IRawElementProviderSimple *start, PROPERTYID id, VARIANT value, IRawElementProviderSimple **out)
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

        if(id != 0 && id != UIA_NamePropertyId && id != UIA_ControlTypePropertyId && id != UIA_SelectionItemIsSelectedPropertyId &&
           id != UIA_AutomationIdPropertyId)
        {
            return kUnsupported;
        }

        if((id == UIA_NamePropertyId || id == UIA_AutomationIdPropertyId) && value.vt != VT_BSTR)
        {
            return E_INVALIDARG;
        }

        if(id == UIA_ControlTypePropertyId && value.vt != VT_I4)
        {
            return E_INVALIDARG;
        }

        if(id == UIA_SelectionItemIsSelectedPropertyId && value.vt != VT_BOOL)
        {
            return E_INVALIDARG;
        }

        bool past = start == nullptr;
        const auto &layout = static_cast<const UiaSnapshot *>(q.snapshot->native.get())->layout;
        std::uint32_t startOrdinal = 0;

        if(start)
        {
            bool found = false;

            for(std::size_t i = 0; i < q.snapshot->storage.nodes.size(); ++i)
            {
                if(providerFor(*q.snapshot, q.snapshot->storage.nodes[i].id) == start)
                {
                    startOrdinal = layout[i].ordinal;
                    found = true;
                    break;
                }
            }

            if(!found)
            {
                return E_INVALIDARG;
            }
            past = true;
        }
        A::NodeId best = 0;
        std::uint32_t bestOrdinal = std::numeric_limits<std::uint32_t>::max();

        for(std::size_t i = 0; i < q.snapshot->storage.nodes.size(); ++i)
        {
            const auto &n = q.snapshot->storage.nodes[i];

            if(!past || !descendant(*q.snapshot, n, id_) || (start && layout[i].ordinal <= startOrdinal))
            {
                continue;
            }
            bool matches = id == 0;

            if(id == UIA_ControlTypePropertyId)
            {
                matches = controlType(n.role) == value.lVal;
            }
            else if(id == UIA_SelectionItemIsSelectedPropertyId)
            {
                matches = n.states.contains(A::State::Selected) == (value.boolVal != VARIANT_FALSE);
            }
            else if(id == UIA_NamePropertyId)
            {
                const auto &name = q.snapshot->textCache[i].name;
                matches = n.exposure != A::Exposure::RedactContent && name.size() == SysStringLen(value.bstrVal) &&
                          std::equal(name.begin(), name.end(), reinterpret_cast<const char16_t *>(value.bstrVal));
            }
            else if(id == UIA_AutomationIdPropertyId)
            {
                std::array<char, 24> digits{};
                const auto converted = std::to_chars(digits.begin(), digits.end(), n.id);
                const auto length = static_cast<std::size_t>(converted.ptr - digits.data());
                const std::wstring_view supplied(value.bstrVal, SysStringLen(value.bstrVal));
                matches = supplied.size() == length;

                for(std::size_t j = 0; matches && j < supplied.size(); ++j)
                {
                    matches = supplied[j] == static_cast<wchar_t>(static_cast<unsigned char>(digits[j]));
                }
            }

            if(matches && layout[i].ordinal < bestOrdinal)
            {
                best = n.id;
                bestOrdinal = layout[i].ordinal;
            }
        }

        if(best != 0)
        {
            *out = providerFor(*q.snapshot, best);

            if(*out)
            {
                (*out)->AddRef();
            }
        }

        return S_OK;
    }

    /// ------------------------------------------------------------
    /// Annotation and text-child patterns
    /// ------------------------------------------------------------

    HRESULT NodeProvider::get_AnnotationTypeId(int *out)
    {
        if(!out)
        {
            return E_POINTER;
        }
        *out = AnnotationType_Unknown;
        const auto q = query();

        if(!q)
        {
            return kUnavailable;
        }

        std::size_t source = 0, index = 0;
        const auto *annotation = annotationFor(q, source, index);

        if(!annotation)
        {
            return kUnsupported;
        }

        if(annotation->kind == A::TextAnnotationKind::Spelling)
        {
            *out = AnnotationType_SpellingError;
        }
        else if(annotation->kind == A::TextAnnotationKind::Emphasis)
        {
            *out = AnnotationType_Highlighted;
        }

        return S_OK;
    }

    HRESULT NodeProvider::get_AnnotationTypeName(BSTR *out)
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

        std::size_t source = 0, index = 0;

        if(!annotationFor(q, source, index))
        {
            return kUnsupported;
        }
        return stringValue(q.snapshot->textCache[source].annotationValues[index], out);
    }

    HRESULT NodeProvider::get_Author(BSTR *out)
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

        return annotationSupported(q) ? stringValue({}, out) : kUnsupported;
    }

    HRESULT NodeProvider::get_DateTime(BSTR *out)
    {
        return get_Author(out);
    }

    HRESULT NodeProvider::get_Target(IRawElementProviderSimple **out)
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

        std::size_t source = 0, index = 0;

        if(!annotationFor(q, source, index))
        {
            return kUnsupported;
        }
        *out = providerFor(*q.snapshot, q.snapshot->storage.nodes[source].id);

        if(*out)
        {
            (*out)->AddRef();
        }

        return S_OK;
    }

    HRESULT NodeProvider::get_TextContainer(IRawElementProviderSimple **out)
    {
        return get_Target(out);
    }

    HRESULT NodeProvider::get_TextRange(ITextRangeProvider **out)
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

        std::size_t source = 0, index = 0;
        const auto *annotation = annotationFor(q, source, index);

        if(!annotation)
        {
            return kUnsupported;
        }

        UiaQuery sourceQuery{q.runtime, q.snapshot, &q.snapshot->storage.nodes[source], source};
        return static_cast<UiaState *>(q.runtime->native.get())->range(sourceQuery, annotation->range, out);
    }

} // namespace GameWIP::Desktop::Platform
