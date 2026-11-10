/// @file accessibility_retained_tests.inl
/// @brief Externally retained accessibility object lifetime tests.

// ------------------------------------------------------------
// Externally retained lifetime
// ------------------------------------------------------------

void testRetainedObjects(Context &context)
{
    auto window = std::make_unique<Desktop::Window>();

    if(!open(context, *window))
    {
        return;
    }
    auto bridge = window->accessibility();
    check(context, "retained-object bridge enables", bridge.enable().ok());

    Tree tree;
    tree.nodes[1].text = A::TextContent{"Retained document"};
    check(context, "retained-object tree publishes", bridge.publish(tree.view()).ok());

    const auto reader = bridge.readSnapshot();
    Com<IRawElementProviderSimple> provider(static_cast<IRawElementProviderSimple *>(Desktop::TestHooks::accessibilityProvider(*window, 2)));

    if(!context.expectTrue("retained-object provider exists", static_cast<bool>(provider)))
    {
        return;
    }

    Com<ITextProvider> text;

    if(!pattern(context, provider.value, UIA_TextPatternId, text))
    {
        return;
    }

    Com<ITextRangeProvider> range;
    check(context, "retained native range exists", SUCCEEDED(text->get_DocumentRange(range.put())) && range);

    window.reset();
    const auto *retained = reader.find(2);
    check(context, "portable reader outlives C++ Window", retained && retained->text && retained->text->utf8 == "Retained document");
    VARIANT name{};
    equal(context, "retained native node unavailable after object destruction", kUnavailable, provider->GetPropertyValue(UIA_NamePropertyId, &name));

    if(range)
    {
        BSTR content = nullptr;
        equal(context, "retained native range unavailable after object destruction", kUnavailable, range->GetText(-1, &content));
        range.put(); // The pool remains alive through this last externally retained range release.
    }
}
