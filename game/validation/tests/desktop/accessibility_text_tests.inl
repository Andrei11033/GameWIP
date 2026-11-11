/// @file accessibility_text_tests.inl
/// @brief Native UI Automation text-range and attribute tests.

// ------------------------------------------------------------
// Native text ranges and attributes
// ------------------------------------------------------------

void testText(Context &context)
{
    Desktop::Window window;

    if(!open(context, window))
    {
        return;
    }
    auto bridge = window.accessibility();
    A::Options options;
    options.limits.maximumNativeTextRanges = 2;
    check(context, "text pool enables", bridge.enable(options).ok());

    Tree tree;
    const std::string_view text = "e\xCC\x81 \xF0\x9F\x99\x82\nlast";
    const std::array<A::TextAnnotation, 1> annotations{{{{0, 3}, A::TextAnnotationKind::Language, "fr-FR", 0}}};
    const std::array<A::TextFragment, 1> fragments{{{{0, static_cast<std::uint32_t>(text.size())}, {0, 0, 100, 20}}}};
    tree.nodes[1].role = A::Role::Document;
    tree.nodes[1].text = A::TextContent{text, A::TextDirection::LeftToRight, "en-US", annotations, fragments, {}, 0};
    tree.nodes[1].actions.flags = action(A::ActionKind::SetTextSelection) | action(A::ActionKind::ScrollTo);
    check(context, "rich text publishes", bridge.publish(tree.view()).ok());
    Com<IRawElementProviderSimple> provider(static_cast<IRawElementProviderSimple *>(Desktop::TestHooks::accessibilityProvider(window, 2)));

    if(!context.expectTrue("text provider exists", static_cast<bool>(provider)))
    {
        return;
    }

    Com<ITextProvider2> patternProvider;

    if(!pattern(context, provider.value, UIA_TextPattern2Id, patternProvider))
    {
        return;
    }

    Com<ITextRangeProvider> range, clone, overflow;
    equal(context, "document range", S_OK, patternProvider->get_DocumentRange(range.put()));

    if(!range)
    {
        return;
    }

    BSTR wide = nullptr;
    equal(context, "text full query", S_OK, range->GetText(-1, &wide));
    check(context, "text preserves combining and nonBMP scalars", std::wstring_view(wide, SysStringLen(wide)) == L"e\u0301 \U0001F642\nlast");
    SysFreeString(wide);

    equal(context, "clone claims second pool slot", S_OK, range->Clone(clone.put()));
    equal(context, "pool pressure rejects newest range", HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA), range->Clone(overflow.put()));

    if(clone)
    {
        BOOL same = FALSE;
        equal(context, "range comparison", S_OK, range->Compare(clone.value, &same));
        check(context, "clone initially equal", same != FALSE);
    }

    equal(context, "character expands by grapheme", S_OK, range->ExpandToEnclosingUnit(TextUnit_Character));
    equal(context, "first grapheme query", S_OK, range->GetText(-1, &wide));
    equal(context, "combining scalar kept", UINT{2}, SysStringLen(wide));
    SysFreeString(wide);
    int moved = 0;
    equal(context, "character movement", S_OK, range->Move(TextUnit_Character, 2, &moved));
    equal(context, "grapheme movement count", 2, moved);
    equal(context, "emoji range query", S_OK, range->GetText(-1, &wide));
    check(context, "emoji remains intact", std::wstring_view(wide, SysStringLen(wide)) == L"\U0001F642");
    SysFreeString(wide);
    equal(context, "UTF16 cap does not split surrogate", S_OK, range->GetText(1, &wide));
    equal(context, "split surrogate suppressed", UINT{0}, SysStringLen(wide));
    SysFreeString(wide);

    equal(context, "selection transports typed range", S_OK, range->Select());
    A::ActionRequest request;
    check(context, "range action pops", bridge.popAction(request));
    check(context, "UTF8 grapheme offsets transported", request.range && request.range->begin == 4 && request.range->end == 8);

    VARIANT attribute{};
    equal(context, "read-only attribute", S_OK, range->GetAttributeValue(UIA_IsReadOnlyAttributeId, &attribute));
    check(context, "document content is read-only", attribute.vt == VT_BOOL && attribute.boolVal == VARIANT_TRUE);
    VariantClear(&attribute);

    Com<IUnknown> textEdit;
    equal(context, "TextEdit availability query", S_OK, provider->GetPatternProvider(UIA_TextEditPatternId, textEdit.put()));
    check(context, "TextEdit is not advertised", !textEdit);

    clone.put();
    equal(context, "restore complete document unit", S_OK, range->ExpandToEnclosingUnit(TextUnit_Document));
    BSTR search = SysAllocString(L"LAST");
    equal(context, "case-insensitive native text search", S_OK, range->FindText(search, FALSE, TRUE, clone.put()));
    SysFreeString(search);

    if(context.expectTrue("text search creates a retained range", static_cast<bool>(clone)))
    {
        equal(context, "matched text read", S_OK, clone->GetText(-1, &wide));
        equal(context, "case search preserves original text", std::wstring_view(L"last"), std::wstring_view(wide, SysStringLen(wide)));
        SysFreeString(wide);
    }

    clone.put();
    search = SysAllocString(L"e");
    equal(context, "partial combining-grapheme search", S_OK, range->FindText(search, FALSE, FALSE, clone.put()));
    SysFreeString(search);
    check(context, "search never returns a split grapheme", !clone);

    attribute.vt = VT_BOOL;
    attribute.boolVal = VARIANT_TRUE;
    equal(context, "read-only attribute search", S_OK, range->FindAttribute(UIA_IsReadOnlyAttributeId, attribute, FALSE, clone.put()));
    check(context, "attribute search creates a range", static_cast<bool>(clone));

    clone.put();
    equal(context, "range scroll transports without executing", S_OK, range->ScrollIntoView(FALSE));
    check(context, "range scroll request drains", bridge.popAction(request));
    check(context, "range scroll alignment is typed", request.action == A::ActionKind::ScrollTo && request.range && request.point.y == 1);

    equal(
        context,
        "bounded endpoint saturation",
        S_OK,
        range->MoveEndpointByUnit(TextPatternRangeEndpoint_Start, TextUnit_Character, INT_MAX, &moved));
    check(context, "endpoint movement count is bounded", moved > 0 && moved < INT_MAX);

    check(context, "replace text snapshot", bridge.publish(tree.view(2)).ok());
    equal(context, "old immutable ranges unavailable after replacement", kUnavailable, range->GetText(-1, &wide));
    clone.put();
    range.put();
    equal(context, "released pool slots reusable", S_OK, patternProvider->get_DocumentRange(range.put()));
    check(context, "text close", window.close().ok());
    equal(context, "retained text range unavailable after close", kUnavailable, range->GetText(-1, &wide));
}

void testTextSemantics(Context &context)
{
    Desktop::Window window;

    if(!open(context, window))
    {
        return;
    }
    auto bridge = window.accessibility();
    check(context, "text semantics bridge enables", bridge.enable().ok());

    Tree tree;
    const std::array<A::TextFragment, 1> fragments{{{{0, 2}, {0, 0, 100, 20}}}};
    tree.nodes[1].role = A::Role::TextField;
    tree.nodes[1].value = "ab";
    auto &content = tree.nodes[1].text.emplace(A::TextContent{"ab", A::TextDirection::LeftToRight, "en-US", {}, fragments});
    tree.nodes[1].actions.flags = action(A::ActionKind::SetTextSelection);
    check(context, "text without selection or caret publishes", bridge.publish(tree.view()).ok());

    Desktop::TestHooks::PresentationPublicationSnapshot host;
    host.clientSize = {200, 100};
    host.visible = true;
    Desktop::TestHooks::applyPresentationPublicationSnapshot(window, host);
    Com<IRawElementProviderSimple> provider(static_cast<IRawElementProviderSimple *>(Desktop::TestHooks::accessibilityProvider(window, 2)));

    if(!context.expectTrue("text semantics provider exists", static_cast<bool>(provider)))
    {
        return;
    }
    Com<ITextProvider2> text;
    Com<IValueProvider> value;

    if(!pattern(context, provider.value, UIA_TextPattern2Id, text) || !pattern(context, provider.value, UIA_ValuePatternId, value))
    {
        return;
    }

    SAFEARRAY *selection = nullptr;
    equal(context, "missing selection query", S_OK, text->GetSelection(&selection));
    check(context, "missing selection and caret do not invent an insertion point", selection == nullptr);

    if(selection)
    {
        SafeArrayDestroy(selection);
    }

    content.caret = 1;
    check(context, "explicit caret publishes", bridge.publish(tree.view(2)).ok());
    equal(context, "explicit caret selection query", S_OK, text->GetSelection(&selection));

    if(context.expectTrue("explicit caret supplies one selection range", selection && selection->rgsabound[0].cElements == 1))
    {
        Com<IUnknown> item;
        LONG index = 0;
        equal(context, "caret selection element", S_OK, SafeArrayGetElement(selection, &index, static_cast<void *>(item.put())));
        Com<ITextRangeProvider> caret;

        if(item && SUCCEEDED(item->QueryInterface(__uuidof(ITextRangeProvider), reinterpret_cast<void **>(caret.put()))))
        {
            Com<ITextRangeProvider> document;
            equal(context, "caret comparison document", S_OK, text->get_DocumentRange(document.put()));
            int distance = 0;

            if(document)
            {
                equal(
                    context,
                    "caret start position",
                    S_OK,
                    caret->CompareEndpoints(TextPatternRangeEndpoint_Start, document.value, TextPatternRangeEndpoint_Start, &distance));
                equal(context, "caret keeps submitted UTF16 offset", 1, distance);
            }
            SAFEARRAY *rectangles = nullptr;
            equal(context, "caret rectangle query", S_OK, caret->GetBoundingRectangles(&rectangles));
            check(context, "degenerate range has no fragment rectangles", rectangles && rectangles->rgsabound[0].cElements == 0);

            if(rectangles)
            {
                SafeArrayDestroy(rectangles);
            }

            if(document)
            {
                equal(context, "nondegenerate rectangle query", S_OK, document->GetBoundingRectangles(&rectangles));
                check(context, "document retains its visible fragment rectangle", rectangles && rectangles->rgsabound[0].cElements == 4);

                if(rectangles)
                {
                    SafeArrayDestroy(rectangles);
                }
            }
        }
        else
        {
            check(context, "caret selection exposes a text range", false);
        }
    }

    if(selection)
    {
        SafeArrayDestroy(selection);
    }

    const std::array<A::TextRange, 1> selected{{{0, 1}}};
    content.selection = selected;
    content.caret.reset();
    tree.nodes[1].actions.flags = 0;
    check(context, "selection without mutation support publishes", bridge.publish(tree.view(3)).ok());
    equal(context, "immutable selection query", S_OK, text->GetSelection(&selection));
    check(context, "submitted selection is readable without a selection action", selection && selection->rgsabound[0].cElements == 1);

    if(selection)
    {
        SafeArrayDestroy(selection);
    }

    SupportedTextSelection supported = SupportedTextSelection_None;
    equal(context, "immutable selection policy query", S_OK, text->get_SupportedTextSelection(&supported));
    equal(context, "submitted selection advertises single selection", SupportedTextSelection_Single, supported);

    struct EditabilityCase
    {
        std::uint64_t states;
        std::uint64_t actions;
        bool readOnly;
    };
    const std::array cases{
        EditabilityCase{0, 0, true},
        EditabilityCase{state(A::State::Editable), 0, false},
        EditabilityCase{0, action(A::ActionKind::SetValue), false},
        EditabilityCase{state(A::State::ReadOnly) | state(A::State::Editable), action(A::ActionKind::SetValue), true}};
    A::Generation generation = 4;

    for(const auto &entry : cases)
    {
        tree.nodes[1].states.flags = entry.states;
        tree.nodes[1].actions.flags = entry.actions;
        check(context, "editability snapshot publishes", bridge.publish(tree.view(generation++)).ok());
        Com<ITextRangeProvider> document;
        equal(context, "editability document range", S_OK, text->get_DocumentRange(document.put()));

        if(!document)
        {
            continue;
        }
        VARIANT attribute{};
        equal(context, "text editability query", S_OK, document->GetAttributeValue(UIA_IsReadOnlyAttributeId, &attribute));
        check(
            context,
            "text read-only state reflects submitted editability",
            attribute.vt == VT_BOOL && (attribute.boolVal != VARIANT_FALSE) == entry.readOnly);
        VariantClear(&attribute);
        BOOL readOnly = FALSE;
        equal(context, "value editability query", S_OK, value->get_IsReadOnly(&readOnly));
        equal(context, "value and text editability agree", entry.readOnly, readOnly != FALSE);
        equal(context, "value editability property query", S_OK, provider->GetPropertyValue(UIA_ValueIsReadOnlyPropertyId, &attribute));
        check(context, "value property agrees with pattern", attribute.vt == VT_BOOL && (attribute.boolVal != VARIANT_FALSE) == entry.readOnly);
        VariantClear(&attribute);
    }
    check(context, "text semantics close", window.close().ok());
}

void testTextAttributes(Context &context)
{
    Desktop::Window window;

    if(!open(context, window))
    {
        return;
    }
    auto bridge = window.accessibility();
    check(context, "text attribute bridge enables", bridge.enable().ok());

    Tree tree;
    std::array<A::TextAnnotation, 2> annotations{
        {{{0, 1}, A::TextAnnotationKind::Emphasis, "bold"}, {{1, 2}, A::TextAnnotationKind::Emphasis, "bold"}}};
    auto &content = tree.nodes[1].text.emplace(A::TextContent{"ab", A::TextDirection::LeftToRight, "en-US", annotations});
    check(context, "adjacent bold annotations publish", bridge.publish(tree.view()).ok());
    Com<IRawElementProviderSimple> provider(static_cast<IRawElementProviderSimple *>(Desktop::TestHooks::accessibilityProvider(window, 2)));

    if(!context.expectTrue("attribute provider exists", static_cast<bool>(provider)))
    {
        return;
    }

    Com<ITextProvider> text;

    if(!pattern(context, provider.value, UIA_TextPatternId, text))
    {
        return;
    }

    Com<IUnknown> mixed, unsupported;
    equal(context, "mixed attribute sentinel", S_OK, UiaGetReservedMixedAttributeValue(mixed.put()));
    equal(context, "unsupported attribute sentinel", S_OK, UiaGetReservedNotSupportedValue(unsupported.put()));

    const auto read = [&](TEXTATTRIBUTEID id, auto verify)
    {
        Com<ITextRangeProvider> document;
        equal(context, "attribute document range", S_OK, text->get_DocumentRange(document.put()));

        if(!document)
        {
            return;
        }

        VARIANT attribute{};
        equal(context, "effective attribute query", S_OK, document->GetAttributeValue(id, &attribute));
        verify(attribute);
        VariantClear(&attribute);
    };

    read(
        UIA_FontWeightAttributeId,
        [&](const VARIANT &attribute)
        {
            check(context, "adjacent equal bold values remain uniform", attribute.vt == VT_I4 && attribute.lVal == 700);
        });

    annotations[1].value = "italic";
    check(context, "different effective formatting publishes", bridge.publish(tree.view(2)).ok());
    read(
        UIA_FontWeightAttributeId,
        [&](const VARIANT &attribute)
        {
            check(context, "different effective weights are mixed", attribute.vt == VT_UNKNOWN && attribute.punkVal == mixed.value);
        });

    annotations[0] = {{0, 2}, A::TextAnnotationKind::Emphasis, "bold"};
    check(context, "overlapping independent attributes publish", bridge.publish(tree.view(3)).ok());
    read(
        UIA_FontWeightAttributeId,
        [&](const VARIANT &attribute)
        {
            check(context, "italic does not interfere with uniform bold", attribute.vt == VT_I4 && attribute.lVal == 700);
        });
    read(
        UIA_IsItalicAttributeId,
        [&](const VARIANT &attribute)
        {
            check(context, "partial italic is mixed", attribute.vt == VT_UNKNOWN && attribute.punkVal == mixed.value);
        });

    annotations = {{{{0, 1}, A::TextAnnotationKind::Language, "fr-FR"}, {{1, 2}, A::TextAnnotationKind::Language, "fr-FR"}}};
    check(context, "adjacent language annotations publish", bridge.publish(tree.view(4)).ok());
    read(
        UIA_CultureAttributeId,
        [&](const VARIANT &attribute)
        {
            check(
                context,
                "adjacent equal language values remain uniform",
                attribute.vt == VT_I4 && attribute.lVal == static_cast<LONG>(LocaleNameToLCID(L"fr-FR", 0)));
        });

    annotations[0].range = {0, 2};
    annotations[1].value = "fr-fr";
    check(context, "overlapping equivalent languages publish", bridge.publish(tree.view(5)).ok());
    read(
        UIA_CultureAttributeId,
        [&](const VARIANT &attribute)
        {
            check(
                context,
                "equivalent native cultures remain uniform",
                attribute.vt == VT_I4 && attribute.lVal == static_cast<LONG>(LocaleNameToLCID(L"fr-FR", 0)));
        });

    annotations[0] = {{0, 2}, A::TextAnnotationKind::Link, "target", 1};
    content.annotations = std::span(annotations).first<1>();
    check(context, "portable link association publishes", bridge.publish(tree.view(6)).ok());
    read(
        UIA_LinkAttributeId,
        [&](const VARIANT &attribute)
        {
            check(
                context,
                "link without a destination text range is unsupported",
                attribute.vt == VT_UNKNOWN && attribute.punkVal == unsupported.value);
        });

    const auto reader = bridge.readSnapshot();
    const auto *link = reader.find(2);
    check(
        context,
        "portable link target remains available",
        link && link->text && link->text->annotations.size() == 1 && link->text->annotations[0].target == 1);
    check(context, "text attribute close", window.close().ok());
}
