/// @file accessibility_control_tests.inl
/// @brief UI Automation control-pattern and grid tests.

// ------------------------------------------------------------
// Control patterns and grid membership
// ------------------------------------------------------------

void testControlPatterns(Context &context)
{
    Desktop::Window window;

    if(!open(context, window))
    {
        return;
    }
    auto bridge = window.accessibility();
    check(context, "control bridge enables", bridge.enable().ok());
    std::array<A::Node, 8> nodes{};
    const std::array<A::NodeId, 5> rootChildren{2, 4, 5, 6, 7};
    const std::array<A::NodeId, 1> cellChildren{3}, textChildren{8}, selected{3};

    for(std::size_t i = 0; i < nodes.size(); ++i)
    {
        nodes[i].id = i + 1;
        nodes[i].parent = i == 0 ? 0 : 1;
        nodes[i].actions.flags = (std::uint64_t{1} << static_cast<unsigned>(A::ActionKind::Count)) - 1;
    }
    nodes[0].children = rootChildren;
    nodes[1].role = A::Role::Table;
    nodes[1].children = cellChildren;
    nodes[1].collection = A::CollectionInfo{.rowCount = 2, .columnCount = 2};
    nodes[1].selection = A::SelectionInfo{selected, 3, true, false};
    nodes[2].parent = 2;
    nodes[2].role = A::Role::Cell;
    nodes[2].states.flags = state(A::State::Selectable) | state(A::State::Selected);
    nodes[2].collection = A::CollectionInfo{.rowIndex = 1, .columnIndex = 0, .columnSpan = 2};
    nodes[2].virtualization = A::VirtualizationInfo{true, false, true};
    nodes[3].role = A::Role::Slider;
    nodes[3].rangeValue = A::RangeValue{5, 0, 10, 1, 5};
    nodes[3].scroll = A::ScrollInfo{true, false, 10, 0, 25, 100};
    nodes[4].role = A::Role::CheckBox;
    nodes[4].states.flags = state(A::State::Mixed);
    nodes[5].role = A::Role::TreeItem;
    nodes[5].states.flags = state(A::State::Expandable);
    const A::TextAnnotation annotation{{0, 5}, A::TextAnnotationKind::Spelling, "spelling", 8};
    nodes[6].role = A::Role::Document;
    nodes[6].children = textChildren;
    nodes[6].text = A::TextContent{"spell", A::TextDirection::LeftToRight, "en-US", {&annotation, 1}};
    nodes[7].parent = 7;
    check(context, "all typed controls publish", bridge.publish({1, 1, nodes}).ok());

    const auto pop = [&](A::ActionKind kind)
    {
        A::ActionRequest request;
        check(context, "native request drains", bridge.popAction(request));
        equal(context, "typed native request", kind, request.action);
        return request;
    };
    Com<IRawElementProviderSimple> table(static_cast<IRawElementProviderSimple *>(Desktop::TestHooks::accessibilityProvider(window, 2)));
    Com<IRawElementProviderSimple> cell(static_cast<IRawElementProviderSimple *>(Desktop::TestHooks::accessibilityProvider(window, 3)));
    Com<IRawElementProviderSimple> slider(static_cast<IRawElementProviderSimple *>(Desktop::TestHooks::accessibilityProvider(window, 4)));
    Com<IRawElementProviderSimple> toggle(static_cast<IRawElementProviderSimple *>(Desktop::TestHooks::accessibilityProvider(window, 5)));
    Com<IRawElementProviderSimple> expand(static_cast<IRawElementProviderSimple *>(Desktop::TestHooks::accessibilityProvider(window, 6)));
    Com<IRawElementProviderSimple> annotationNode(static_cast<IRawElementProviderSimple *>(Desktop::TestHooks::accessibilityProvider(window, 8)));

    if(!context.expectTrue("all control providers exist", table && cell && slider && toggle && expand && annotationNode))
    {
        return;
    }
    Com<IGridProvider> grid;

    if(pattern(context, table.value, UIA_GridPatternId, grid))
    {
        int rows = 0;
        equal(context, "grid row count", S_OK, grid->get_RowCount(&rows));
        equal(context, "grid typed rows", 2, rows);
        Com<IRawElementProviderSimple> item;
        equal(context, "spanned grid item", S_OK, grid->GetItem(1, 1, item.put()));
        check(context, "spanned item identity", item.value == cell.value);
        item.put();
        equal(context, "known missing grid item", S_OK, grid->GetItem(0, 0, item.put()));
        check(context, "partial tree does not invent items", !item);
    }
    Com<IGridItemProvider> gridItem;

    if(pattern(context, cell.value, UIA_GridItemPatternId, gridItem))
    {
        Com<IRawElementProviderSimple> container;
        equal(context, "containing grid", S_OK, gridItem->get_ContainingGrid(container.put()));
        check(context, "grid container identity", container.value == table.value);
    }
    Com<ITableProvider> tablePattern;

    if(pattern(context, table.value, UIA_TablePatternId, tablePattern))
    {
        SAFEARRAY *headers = nullptr;
        equal(context, "table headers", S_OK, tablePattern->GetRowHeaders(&headers));

        if(headers)
        {
            SafeArrayDestroy(headers);
        }
    }
    Com<ITableItemProvider> tableItem;
    check(context, "table-item pattern", pattern(context, cell.value, UIA_TableItemPatternId, tableItem));
    Com<ISelectionProvider2> selection;

    if(pattern(context, table.value, 10034, selection))
    {
        int count = 0;
        equal(context, "Selection2 item count", S_OK, selection->get_ItemCount(&count));
        equal(context, "Selection2 copied count", 1, count);
        Com<IRawElementProviderSimple> active;
        equal(context, "Selection2 active item", S_OK, selection->get_CurrentSelectedItem(active.put()));
        check(context, "Selection2 active identity", active.value == cell.value);
    }
    Com<ISelectionItemProvider> selectedItem;

    if(pattern(context, cell.value, UIA_SelectionItemPatternId, selectedItem))
    {
        equal(context, "add selection transport", S_OK, selectedItem->AddToSelection());
        equal(context, "explicit additive selection", A::SelectionOperation::Add, pop(A::ActionKind::Select).selectionOperation);
        equal(context, "remove selection transport", S_OK, selectedItem->RemoveFromSelection());
        pop(A::ActionKind::Deselect);
    }
    Com<IRangeValueProvider> range;

    if(pattern(context, slider.value, UIA_RangeValuePatternId, range))
    {
        double value = 0;
        equal(context, "numeric current value", S_OK, range->get_Value(&value));
        equal(context, "numeric snapshot value", 5.0, value);
        equal(context, "numeric range rejects invalid destination", E_INVALIDARG, range->SetValue(11));
        equal(context, "numeric setter transport", S_OK, range->SetValue(7));
        equal(context, "numeric typed payload", 7.0, pop(A::ActionKind::SetValue).value);
    }
    Com<IScrollProvider> scroll;

    if(pattern(context, slider.value, UIA_ScrollPatternId, scroll))
    {
        double percent = 0;
        equal(context, "unsupported axis query", S_OK, scroll->get_VerticalScrollPercent(&percent));
        equal(context, "UIA no-scroll sentinel", -1.0, percent);
        equal(context, "relative scroll transport", S_OK, scroll->Scroll(ScrollAmount_LargeIncrement, ScrollAmount_NoAmount));
        equal(context, "relative scroll uses steps", A::ScrollUnit::Step, pop(A::ActionKind::ScrollBy).scroll.unit);
        equal(context, "percent scroll transport", S_OK, scroll->SetScrollPercent(50, -1));
        equal(context, "absolute scroll uses percentages", A::ScrollUnit::Percent, pop(A::ActionKind::ScrollTo).scroll.unit);
    }
    Com<IScrollItemProvider> scrollItem;

    if(pattern(context, cell.value, UIA_ScrollItemPatternId, scrollItem))
    {
        equal(context, "scroll item transport", S_OK, scrollItem->ScrollIntoView());
        pop(A::ActionKind::ScrollTo);
    }
    Com<IToggleProvider> togglePattern;

    if(pattern(context, toggle.value, UIA_TogglePatternId, togglePattern))
    {
        ToggleState value = ToggleState_Off;
        equal(context, "mixed toggle state query", S_OK, togglePattern->get_ToggleState(&value));
        equal(context, "mixed toggle state", ToggleState_Indeterminate, value);
        equal(context, "toggle transport", S_OK, togglePattern->Toggle());
        pop(A::ActionKind::Toggle);
    }
    Com<IExpandCollapseProvider> expandPattern;

    if(pattern(context, expand.value, UIA_ExpandCollapsePatternId, expandPattern))
    {
        equal(context, "expand transport", S_OK, expandPattern->Expand());
        pop(A::ActionKind::Expand);
        equal(context, "collapse transport", S_OK, expandPattern->Collapse());
        pop(A::ActionKind::Collapse);
    }
    Com<IVirtualizedItemProvider> virtualPattern;

    if(pattern(context, cell.value, UIA_VirtualizedItemPatternId, virtualPattern))
    {
        equal(context, "realization transports only", S_OK, virtualPattern->Realize());
        pop(A::ActionKind::Realize);
        const auto reader = bridge.readSnapshot();
        const auto *publishedCell = reader.find(3);
        check(
            context,
            "realization does not mutate snapshot",
            publishedCell && publishedCell->virtualization && !publishedCell->virtualization->realized);
    }
    Com<IItemContainerProvider> items;

    if(pattern(context, table.value, UIA_ItemContainerPatternId, items))
    {
        Com<IRawElementProviderSimple> item;
        VARIANT filter{};
        equal(context, "item container local search", S_OK, items->FindItemByProperty(nullptr, 0, filter, item.put()));
        check(context, "item-container identity", item.value == cell.value);
    }
    Com<IAnnotationProvider> annotationPattern;

    if(pattern(context, annotationNode.value, UIA_AnnotationPatternId, annotationPattern))
    {
        int type = 0;
        equal(context, "annotation type query", S_OK, annotationPattern->get_AnnotationTypeId(&type));
        equal(context, "spelling annotation type", AnnotationType_SpellingError, type);
    }
    Com<ITextChildProvider> textChild;

    if(pattern(context, annotationNode.value, UIA_TextChildPatternId, textChild))
    {
        Com<ITextRangeProvider> textRange;
        equal(context, "text child associated range", S_OK, textChild->get_TextRange(textRange.put()));
        BSTR text = nullptr;

        if(textRange)
        {
            equal(context, "text child range text", S_OK, textRange->GetText(-1, &text));
            check(context, "annotation range text", std::wstring_view(text, SysStringLen(text)) == L"spell");
            SysFreeString(text);
        }
    }
    check(context, "control bridge closes", window.close().ok());
}

void testGridLookup(Context &context)
{
    Desktop::Window window;

    if(!open(context, window))
    {
        return;
    }
    auto bridge = window.accessibility();
    check(context, "grid lookup bridge enables", bridge.enable().ok());
    std::array<A::Node, 6> nodes{};
    const std::array<A::NodeId, 2> rootChildren{2, 4};
    const std::array<A::NodeId, 1> outerCells{3}, innerRows{5}, innerCells{6};

    for(std::size_t i = 0; i < nodes.size(); ++i)
    {
        nodes[i].id = i + 1;
    }
    nodes[0].role = A::Role::Table;
    nodes[0].children = rootChildren;
    nodes[0].collection = A::CollectionInfo{.rowCount = 2, .columnCount = 2};
    nodes[1].parent = 1;
    nodes[1].role = A::Role::Row;
    nodes[1].children = outerCells;
    nodes[1].collection = A::CollectionInfo{.positionInSet = 1, .setSize = 2};
    nodes[2].parent = 2;
    nodes[2].role = A::Role::Cell;
    nodes[2].collection = A::CollectionInfo{};
    nodes[3].parent = 1;
    nodes[3].role = A::Role::Table;
    nodes[3].children = innerRows;
    nodes[3].collection = A::CollectionInfo{.rowCount = 1, .columnCount = 2};
    nodes[4].parent = 4;
    nodes[4].role = A::Role::Row;
    nodes[4].children = innerCells;
    nodes[4].collection = A::CollectionInfo{.positionInSet = 1, .setSize = 1};
    nodes[5].parent = 5;
    nodes[5].role = A::Role::Cell;
    nodes[5].collection = A::CollectionInfo{.columnIndex = 1};
    check(context, "nested grids publish", bridge.publish({1, 1, nodes}).ok());
    Com<IRawElementProviderSimple> outer(static_cast<IRawElementProviderSimple *>(Desktop::TestHooks::accessibilityProvider(window, 1)));
    Com<IRawElementProviderSimple> cell(static_cast<IRawElementProviderSimple *>(Desktop::TestHooks::accessibilityProvider(window, 3)));
    Com<IRawElementProviderSimple> inner(static_cast<IRawElementProviderSimple *>(Desktop::TestHooks::accessibilityProvider(window, 4)));
    Com<IRawElementProviderSimple> nestedCell(static_cast<IRawElementProviderSimple *>(Desktop::TestHooks::accessibilityProvider(window, 6)));

    if(!context.expectTrue("nested grid providers exist", outer && cell && inner && nestedCell))
    {
        return;
    }
    Com<IGridProvider> grid, nestedGrid;

    if(!pattern(context, outer.value, UIA_GridPatternId, grid) || !pattern(context, inner.value, UIA_GridPatternId, nestedGrid))
    {
        return;
    }
    Com<IRawElementProviderSimple> result;
    equal(context, "outer grid cell lookup", S_OK, grid->GetItem(0, 0, result.put()));
    check(context, "collection metadata on a row does not turn it into a cell", result.value == cell.value);
    equal(context, "outer grid missing cell lookup", S_OK, grid->GetItem(0, 1, result.put()));
    check(context, "outer grid excludes nested table cells", !result);
    equal(context, "nested grid cell lookup", S_OK, nestedGrid->GetItem(0, 1, result.put()));
    check(context, "nested grid retains its own cell", result.value == nestedCell.value);
    std::ranges::reverse(nodes);
    check(context, "shuffled node storage publishes", bridge.publish({2, 1, nodes}).ok());
    equal(context, "shuffled grid cell lookup", S_OK, grid->GetItem(0, 0, result.put()));
    check(context, "grid lookup follows semantic membership across storage orders", result.value == cell.value);
    check(context, "grid lookup close", window.close().ok());
}
