/// @file accessibility_text_ranges_tests.inl
/// @brief Native UI Automation text-range tests.

/// ------------------------------------------------------------
/// Native text ranges and document queries
/// ------------------------------------------------------------

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

    // Publish a rich document with text, annotation, and fragment metadata.
    Tree tree;
    const std::string_view text = "e\xCC\x81 \xF0\x9F\x99\x82\nlast";
    const std::array<A::TextAnnotation, 1> annotations{{{{0, 3}, A::TextAnnotationKind::Language, "fr-FR", 0}}};
    const std::array<A::TextFragment, 1> fragments{{{{0, static_cast<std::uint32_t>(text.size())}, {0, 0, 100, 20}}}};
    tree.nodes[1].role = A::Role::Document;
    tree.nodes[1].text = A::TextContent{text, A::TextDirection::LeftToRight, "en-US", annotations, fragments, {}, 0};
    tree.nodes[1].actions.flags = action(A::ActionKind::SetTextSelection) | action(A::ActionKind::ScrollTo);
    check(context, "rich text publishes", bridge.publish(tree.view()).ok());
    const Com<IRawElementProviderSimple> provider(
        static_cast<IRawElementProviderSimple *>(Desktop::Internal::TestHooks::accessibilityProvider(window, 2)));

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

    // Check UTF-16 exposure and grapheme-safe range operations.
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

    // Verify typed selection transport and read-only state.
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

    // Search text and attributes without splitting graphemes.
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

    // Search for partial graphemes and text attributes.
    clone.put();
    search = SysAllocString(L"e");
    equal(context, "partial combining-grapheme search", S_OK, range->FindText(search, FALSE, FALSE, clone.put()));
    SysFreeString(search);
    check(context, "search never returns a split grapheme", !clone);

    attribute.vt = VT_BOOL;
    attribute.boolVal = VARIANT_TRUE;
    equal(context, "read-only attribute search", S_OK, range->FindAttribute(UIA_IsReadOnlyAttributeId, attribute, FALSE, clone.put()));
    check(context, "attribute search creates a range", static_cast<bool>(clone));

    // Verify scroll requests and bounded endpoint movement.
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

    // Replacing or closing a snapshot invalidates retained native ranges.
    check(context, "replace text snapshot", bridge.publish(tree.view(2)).ok());
    equal(context, "old immutable ranges unavailable after replacement", kUnavailable, range->GetText(-1, &wide));
    clone.put();
    range.put();
    equal(context, "released pool slots reusable", S_OK, patternProvider->get_DocumentRange(range.put()));
    check(context, "text close", window.close().ok());
    equal(context, "retained text range unavailable after close", kUnavailable, range->GetText(-1, &wide));
}
