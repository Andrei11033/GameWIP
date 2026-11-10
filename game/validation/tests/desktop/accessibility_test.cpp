/// @file accessibility_test.cpp
/// @brief Accessibility fixtures, process-isolated checks, and suite registration.

#include "validation/tests/desktop/accessibility_test.h"

#include "desktop/accessibility.h"
#include "desktop/dialogs.h"
#include "desktop/internal/desktop_test_hooks.h"
#include "desktop/native/win32.h"

#include <uiautomation.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>

namespace
{
    namespace Desktop = GameWIP::Desktop;
    namespace A = Desktop::Types::Accessibility;
    namespace Bridge = Desktop::Accessibility;
    using GameWIP::TestSupport::Context;
    using Error = GameWIP::IO::Types::ErrorCode;
    constexpr HRESULT kUnavailable = static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE);

    static_assert(!std::is_copy_constructible_v<Bridge::SnapshotBuilder>);
    static_assert(std::is_nothrow_move_constructible_v<Bridge::SnapshotBuilder>);
    static_assert(std::is_nothrow_copy_constructible_v<Bridge::SnapshotReader>);
    static_assert(noexcept(std::declval<Bridge::Facade &>().publish(A::SnapshotView{})));

    // ------------------------------------------------------------
    // Shared fixtures and assertions
    // ------------------------------------------------------------

    void check(Context &context, std::string_view name, bool result)
    {
        static_cast<void>(context.expectTrue(name, result));
    }

    template <typename T, typename U> void equal(Context &context, std::string_view name, const T &expected, const U &actual)
    {
        static_cast<void>(context.expectEq(name, expected, actual));
    }

    constexpr std::uint64_t state(A::State flag)
    {
        return static_cast<std::uint64_t>(flag);
    }

    constexpr std::uint64_t action(A::ActionKind kind)
    {
        return std::uint64_t{1} << static_cast<unsigned>(kind);
    }

    struct Tree
    {
        std::array<A::NodeId, 1> children{2};
        std::array<A::Node, 2> nodes{};

        Tree()
        {
            nodes[0].id = 1;
            nodes[0].role = A::Role::Window;
            nodes[0].children = children;
            nodes[0].name = "Example";
            nodes[1].id = 2;
            nodes[1].parent = 1;
            nodes[1].role = A::Role::Button;
            nodes[1].name = "Run";
            nodes[1].states.flags = state(A::State::Focusable);
            nodes[1].actions.flags = action(A::ActionKind::Focus) | action(A::ActionKind::Invoke) | action(A::ActionKind::SetValue);
        }

        A::SnapshotView view(A::Generation generation = 1)
        {
            return {generation, 1, nodes};
        }
    };

    bool open(Context &context, Desktop::Window &window)
    {
        Desktop::Types::Description description;
        description.activation.visible = false;
        const auto status = window.open(description);
        return context.expectTrue("hidden accessibility Window opens", status.ok());
    }

    // ------------------------------------------------------------
    // Test suites
    // ------------------------------------------------------------

#include "validation/tests/desktop/accessibility_contract_tests.inl"
#include "validation/tests/desktop/accessibility_com_fixtures.inl"
#if DESKTOP_INTERNAL_TEST_HOOKS
#include "validation/tests/desktop/accessibility_transport_tests.inl"
#include "validation/tests/desktop/accessibility_provider_tests.inl"
#include "validation/tests/desktop/accessibility_text_tests.inl"
#include "validation/tests/desktop/accessibility_retained_tests.inl"
#include "validation/tests/desktop/accessibility_control_tests.inl"
#include "validation/tests/desktop/accessibility_geometry_tests.inl"
#endif
} // namespace

namespace GameWIP::Test
{
    // ------------------------------------------------------------
    // Process-isolated UIA client and suite registration
    // ------------------------------------------------------------

    int runAccessibilityClientChild()
    {
        Desktop::Window window;
        Desktop::Types::Description description;
        description.activation.visible = false;

        if(!window.open(description).ok())
        {
            return 1;
        }
        auto bridge = window.accessibility();

        if(!bridge.enable().ok())
        {
            return 2;
        }
        Tree tree;
        tree.nodes[1].text = A::TextContent{"Smoke text"};

        if(!bridge.publish(tree.view()).ok())
        {
            return 3;
        }
        const auto handles = Desktop::Native::Win32::getHandle(window);

        if(!handles.status.ok())
        {
            return 4;
        }
        std::atomic<bool> done{false};
        std::atomic<int> clientResult{5};
        bool invoked = false;
        std::thread client(
            [&]
            {
                const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

                if(FAILED(initialized))
                {
                    done = true;
                    return;
                }
                const auto runClient = [&] -> int
                {
                    Com<IUIAutomation> automation;

                    if(FAILED(CoCreateInstance(
                           CLSID_CUIAutomation,
                           nullptr,
                           CLSCTX_INPROC_SERVER,
                           __uuidof(IUIAutomation),
                           reinterpret_cast<void **>(automation.put()))))
                    {
                        return 6;
                    }
                    Com<IUIAutomationElement> root, child;

                    if(FAILED(automation->ElementFromHandle(handles->window, root.put())) || !root)
                    {
                        return 7;
                    }
                    BSTR name = nullptr;
                    const auto named = root->get_CurrentName(&name);
                    const bool correctName = SUCCEEDED(named) && std::wstring_view(name, SysStringLen(name)) == L"Example";
                    SysFreeString(name);

                    if(!correctName)
                    {
                        return 8;
                    }
                    VARIANT id{};
                    id.vt = VT_BSTR;
                    id.bstrVal = SysAllocString(L"2");

                    if(!id.bstrVal)
                    {
                        return 9;
                    }
                    Com<IUIAutomationCondition> condition;
                    const auto filtered = automation->CreatePropertyCondition(UIA_AutomationIdPropertyId, id, condition.put());
                    VariantClear(&id);

                    if(FAILED(filtered) || !condition || FAILED(root->FindFirst(TreeScope_Children, condition.value, child.put())) || !child)
                    {
                        return 10;
                    }
                    Com<IUnknown> unknown;

                    if(FAILED(child->GetCurrentPattern(UIA_InvokePatternId, unknown.put())) || !unknown)
                    {
                        return 11;
                    }
                    Com<IUIAutomationInvokePattern> invoke;

                    if(FAILED(unknown->QueryInterface(__uuidof(IUIAutomationInvokePattern), reinterpret_cast<void **>(invoke.put()))) ||
                       FAILED(invoke->Invoke()))
                    {
                        return 12;
                    }
                    unknown.put();

                    if(FAILED(child->GetCurrentPattern(UIA_TextPatternId, unknown.put())) || !unknown)
                    {
                        return 13;
                    }
                    Com<IUIAutomationTextPattern> text;
                    Com<IUIAutomationTextRange> range;

                    if(FAILED(unknown->QueryInterface(__uuidof(IUIAutomationTextPattern), reinterpret_cast<void **>(text.put()))) ||
                       FAILED(text->get_DocumentRange(range.put())) || !range)
                    {
                        return 14;
                    }
                    BSTR content = nullptr;
                    const auto read = range->GetText(-1, &content);
                    const bool correctText = SUCCEEDED(read) && std::wstring_view(content, SysStringLen(content)) == L"Smoke text";
                    SysFreeString(content);
                    return correctText ? 0 : 15;
                };
                clientResult = runClient();
                CoUninitialize();
                done = true;
            });

        while(!done.load())
        {
            static_cast<void>(Desktop::Events::wait(std::chrono::milliseconds{10}));
            A::ActionRequest request;

            while(bridge.popAction(request))
            {
                invoked = invoked || request.action == A::ActionKind::Invoke;
            }
        }
        client.join();
        A::ActionRequest request;

        while(bridge.popAction(request))
        {
            invoked = invoked || request.action == A::ActionKind::Invoke;
        }

        if(!window.close().ok())
        {
            return 16;
        }

        return clientResult.load() != 0 ? clientResult.load() : invoked ? 0 : 17;
    }

    void runAccessibilityTests(TestSupport::Runner &runner)
    {
        runner.runSuite("Desktop accessibility validation", testValidation);
        runner.runSuite("Desktop accessibility owning authoring", testBuilder);
        runner.runSuite("Desktop accessibility publication and lifetime", testLifetime);
#if DESKTOP_INTERNAL_TEST_HOOKS
        runner.runSuite("Desktop accessibility bounded transport", testQueues);
        runner.runSuite("Desktop accessibility capacity and native interaction", testCapacityAndInteraction);
        runner.runSuite("Desktop accessibility close-publication race", testClosePublicationRace);
        runner.runSuite("Desktop accessibility UIA providers", testProviders);
        runner.runSuite("Desktop accessibility read-only text", testText);
        runner.runSuite("Desktop accessibility text selection and editability", testTextSemantics);
        runner.runSuite("Desktop accessibility effective text attributes", testTextAttributes);
        runner.runSuite("Desktop accessibility externally retained objects", testRetainedObjects);
        runner.runSuite("Desktop accessibility standard control patterns", testControlPatterns);
        runner.runSuite("Desktop accessibility nested grid lookup", testGridLookup);
        runner.runSuite("Desktop accessibility geometry and exceptional lifetime", testGeometryAndExceptionalLifetime);
        runner.runSuite("Desktop accessibility concurrent native queries", testConcurrentQueries);
#endif
    }
} // namespace GameWIP::Test
