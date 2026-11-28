/// @file accessibility_text_semantics_tests.inl
/// @brief Text selection and editability tests.

/// ------------------------------------------------------------
/// Text selection and editability
/// ------------------------------------------------------------

void testTextSemantics(Context &context)
{
    Desktop::Window window;

    if(!open(context, window))
    {
        return;
    }

    auto bridge = window.accessibility();
    check(context, "text semantics bridge enables", bridge.enable().ok());

    // Publish editable text without a submitted selection or caret.
    Tree tree;
    const std::array<A::TextFragment, 1> fragments{{{{0, 2}, {0, 0, 100, 20}}}};
    tree.nodes[1].role = A::Role::TextField;
    tree.nodes[1].value = "ab";
    auto &content = tree.nodes[1].text.emplace(A::TextContent{"ab", A::TextDirection::LeftToRight, "en-US", {}, fragments});
    tree.nodes[1].actions.flags = action(A::ActionKind::SetTextSelection);
    check(context, "text without selection or caret publishes", bridge.publish(tree.view()).ok());

    Desktop::Internal::TestHooks::PresentationPublicationSnapshot host;
    host.clientSize = {200, 100};
    host.visible = true;
    Desktop::Internal::TestHooks::applyPresentationPublicationSnapshot(window, host);
    const Com<IRawElementProviderSimple> provider(
        static_cast<IRawElementProviderSimple *>(Desktop::Internal::TestHooks::accessibilityProvider(window, 2)));

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

    // Verify explicit caret reporting and its degenerate geometry.
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

    // Verify immutable selection reporting and editability policy.
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
        // Submitted control state
        std::uint64_t states;
        std::uint64_t actions;

        // Expected UI Automation state
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
