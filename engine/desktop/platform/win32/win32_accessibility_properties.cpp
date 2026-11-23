/// @file win32_accessibility_properties.cpp
/// @brief Projects the accessibility snapshot into UI Automation properties.

#include "desktop/platform/win32/internal/win32_accessibility_provider.h"
#include "desktop/platform/win32/internal/win32_accessibility_helpers.h"

#include <array>
#include <charconv>

namespace GameWIP::Desktop::Platform
{
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
            // Identity and accessible text

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

            // Element state and geometry

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

            // Value state

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
            // Selection state

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
            // Scrolling and table headers

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

            // Collection position and relationships

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

            // Pattern availability and annotations

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

} // namespace GameWIP::Desktop::Platform
