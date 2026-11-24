/// @file accessibility_text_attributes_tests.inl
/// @brief Native text attribute tests.

/// ------------------------------------------------------------
/// Effective text attributes
/// ------------------------------------------------------------

void testTextAttributes(Context &context)
{
    Desktop::Window window;

    if(!open(context, window))
    {
        return;
    }

    auto bridge = window.accessibility();
    check(context, "text attribute bridge enables", bridge.enable().ok());

    // Publish annotated text and read effective native attributes.
    Tree tree;
    std::array<A::TextAnnotation, 2> annotations{
        {{{0, 1}, A::TextAnnotationKind::Emphasis, "bold"}, {{1, 2}, A::TextAnnotationKind::Emphasis, "bold"}}};
    auto &content = tree.nodes[1].text.emplace(A::TextContent{"ab", A::TextDirection::LeftToRight, "en-US", annotations});
    check(context, "adjacent bold annotations publish", bridge.publish(tree.view()).ok());
    const Com<IRawElementProviderSimple> provider(
        static_cast<IRawElementProviderSimple *>(Desktop::Internal::TestHooks::accessibilityProvider(window, 2)));

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

    // Adjacent equivalent emphasis stays uniform; differing values are mixed.
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

    // Language annotations compare by their resolved native culture.
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

    // Preserve portable link metadata when UI Automation cannot expose a target range.
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
