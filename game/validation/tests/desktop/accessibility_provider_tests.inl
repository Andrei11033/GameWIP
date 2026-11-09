/// @file accessibility_provider_tests.inl
/// @brief Native UI Automation provider tests.

// ------------------------------------------------------------
// Native node providers
// ------------------------------------------------------------

void testProviders(Context &context)
{
    Desktop::Window window;

    if(!open(context, window))
    {
        return;
    }
    auto bridge = window.accessibility();
    check(context, "native bridge enables", bridge.enable().ok());
    Tree tree;
    tree.nodes[1].value = std::string_view("a\0b", 3);
    check(context, "native tree publishes", bridge.publish(tree.view()).ok());
    Com<IRawElementProviderSimple> root(static_cast<IRawElementProviderSimple *>(Desktop::TestHooks::accessibilityProvider(window, 1)));
    Com<IRawElementProviderSimple> button(static_cast<IRawElementProviderSimple *>(Desktop::TestHooks::accessibilityProvider(window, 2)));

    if(!context.expectTrue("stable providers exist", root && button))
    {
        return;
    }
    VARIANT value{};
    equal(context, "native name query", S_OK, button->GetPropertyValue(UIA_NamePropertyId, &value));
    check(context, "native name UTF16", value.vt == VT_BSTR && std::wstring_view(value.bstrVal, SysStringLen(value.bstrVal)) == L"Run");
    VariantClear(&value);
    Com<IInvokeProvider> invoke;

    if(pattern(context, button.value, UIA_InvokePatternId, invoke))
    {
        equal(context, "Invoke is accepted transport", S_OK, invoke->Invoke());
        A::ActionRequest request;
        check(context, "native action available", bridge.popAction(request));
        equal(context, "native action kind", A::ActionKind::Invoke, request.action);
        equal(context, "native generation", A::Generation{1}, request.observedGeneration);
    }
    Com<IValueProvider> stringPattern;

    if(pattern(context, button.value, UIA_ValuePatternId, stringPattern))
    {
        BSTR text = nullptr;
        equal(context, "value read", S_OK, stringPattern->get_Value(&text));
        equal(context, "embedded null preserved", UINT{3}, SysStringLen(text));
        SysFreeString(text);
        equal(context, "UTF16 SetValue accepted", S_OK, stringPattern->SetValue(L"new \U0001F642"));
        A::ActionRequest request;
        check(context, "value action pops", bridge.popAction(request));
        equal(context, "UTF8 native input", std::string("new \xF0\x9F\x99\x82"), request.text);
    }
    Com<IRawElementProviderFragment> fragment;
    check(
        context,
        "fragment interface",
        SUCCEEDED(button->QueryInterface(__uuidof(IRawElementProviderFragment), reinterpret_cast<void **>(fragment.put()))));

    if(fragment)
    {
        Com<IRawElementProviderFragment> parent;
        equal(context, "parent navigation", S_OK, fragment->Navigate(NavigateDirection_Parent, parent.put()));
        check(context, "parent exists", static_cast<bool>(parent));
        SAFEARRAY *runtimeId = nullptr;
        equal(context, "runtime identity", S_OK, fragment->GetRuntimeId(&runtimeId));

        if(runtimeId)
        {
            SafeArrayDestroy(runtimeId);
        }
    }
    const auto handles = Desktop::Native::Win32::getHandle(window);

    if(handles.status.ok())
    {
        check(context, "WM_GETOBJECT publishes UIA provider", SendMessageW(handles->window, WM_GETOBJECT, 0, UiaRootObjectId) != 0);
    }
    tree.nodes[1].states.flags |= state(A::State::Password);
    check(context, "redaction publishes", bridge.publish(tree.view(2)).ok());
    equal(context, "redacted value property", S_OK, button->GetPropertyValue(UIA_ValueValuePropertyId, &value));
    check(context, "password value is empty", value.vt == VT_BSTR && SysStringLen(value.bstrVal) == 0);
    VariantClear(&value);
    Com<IUnknown> hidden;
    equal(context, "password pattern lookup succeeds", S_OK, button->GetPatternProvider(UIA_ValuePatternId, hidden.put()));
    check(context, "password pattern is not exposed", !hidden);
    const std::array<A::Node, 1> remaining{A::Node{.id = 1}};
    check(context, "remove native node", bridge.publish({3, 1, remaining}).ok());
    equal(context, "retained removed provider unavailable", kUnavailable, button->GetPropertyValue(UIA_NamePropertyId, &value));
    VariantClear(&value);
    check(context, "native close", window.close().ok());
    equal(context, "retained root unavailable after close", kUnavailable, root->GetPropertyValue(UIA_NamePropertyId, &value));

    if(!open(context, window))
    {
        return;
    }
    check(context, "native reopen enable", bridge.enable().ok());
    check(context, "fresh native publish", bridge.publish(tree.view(4)).ok());
    equal(context, "old root never revives across reopen", kUnavailable, root->GetPropertyValue(UIA_NamePropertyId, &value));
    VariantClear(&value);
    check(context, "native reopen closes", window.close().ok());
}
