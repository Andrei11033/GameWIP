/// @file accessibility_test.cpp
/// @brief Portable contract and snapshot-backed native accessibility regression tests.

#include "validation/tests/desktop/accessibility_test.h"
#include "desktop/accessibility.h"
#include "desktop/dialogs.h"
#include "desktop/internal/desktop_test_hooks.h"
#include "desktop/native/win32.h"
#include <uiautomation.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <limits>
#include <thread>
#include <type_traits>

namespace
{
    namespace Desktop = GameWIP::Desktop;
    namespace A = Desktop::Types::Accessibility;
    namespace Bridge = Desktop::Accessibility;
    using Context = GameWIP::TestSupport::Context;
    using Error = GameWIP::IO::Types::ErrorCode;
    constexpr HRESULT kUnavailable = static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE);
    static_assert(!std::is_copy_constructible_v<Bridge::SnapshotBuilder>);
    static_assert(std::is_nothrow_move_constructible_v<Bridge::SnapshotBuilder>);
    static_assert(std::is_nothrow_copy_constructible_v<Bridge::SnapshotReader>);
    static_assert(noexcept(std::declval<Bridge::Facade &>().publish(A::SnapshotView{})));

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
        description.visible = false;
        const auto status = window.open(description);
        return context.expectTrue("hidden accessibility Window opens", status.ok());
    }
    // ------------------------------------------------------------
    // Portable snapshot contracts
    // ------------------------------------------------------------

    void testValidation(Context &context)
    {
        Tree tree;

        const auto expectIssue = [&](A::ValidationIssue issue)
        {
            equal(context, "deterministic validation issue", issue, Bridge::validate(tree.view()).issue);
        };
        check(context, "complete tree validates", Bridge::validate(tree.view()).ok());
        equal(context, "zero generation rejects", A::ValidationIssue::ZeroGeneration, Bridge::validate(tree.view(0)).issue);
        A::Limits limits;
        limits.maximumActionQueue = 0;
        equal(context, "zero capacity rejects", A::ValidationIssue::InvalidLimits, Bridge::validate(tree.view(), limits).issue);
        tree.nodes[1].id = 1;
        expectIssue(A::ValidationIssue::DuplicateNodeId);
        tree.nodes[1].id = 2;
        tree.nodes[1].parent = 0;
        expectIssue(A::ValidationIssue::ParentMismatch);
        tree.nodes[1].parent = 1;
        tree.children[0] = 3;
        expectIssue(A::ValidationIssue::MissingNode);
        tree.children[0] = 2;
        tree.nodes[1].role = A::Role::Count;
        expectIssue(A::ValidationIssue::InvalidRole);
        tree.nodes[1].role = A::Role::Button;
        tree.nodes[1].states.flags |= std::uint64_t{1} << 63;
        expectIssue(A::ValidationIssue::InvalidState);
        tree.nodes[1].states.flags &= ~(std::uint64_t{1} << 63);
        tree.nodes[1].name = std::string_view("\xC0\xAF", 2);
        expectIssue(A::ValidationIssue::InvalidText);
        tree.nodes[1].name = "Run";
        auto &geometry = tree.nodes[1].geometry.emplace();
        geometry.toWindow.m11 = 0;
        expectIssue(A::ValidationIssue::InvalidGeometry);
        geometry.hitTestable = false;
        check(context, "singular decorative geometry allowed", Bridge::validate(tree.view()).ok());
        tree.nodes[1].geometry.reset();
        tree.nodes[1].rangeValue = A::RangeValue{2, 0, 1};
        expectIssue(A::ValidationIssue::InvalidValue);
        tree.nodes[1].rangeValue.reset();
        auto &scroll = tree.nodes[1].scroll.emplace();
        scroll.verticalPercent = std::numeric_limits<double>::quiet_NaN();
        expectIssue(A::ValidationIssue::InvalidValue);
        tree.nodes[1].scroll.reset();
        const std::array<A::TextRange, 1> split{{{0, 1}}};
        auto &text = tree.nodes[1].text.emplace(A::TextContent{"e\xCC\x81", A::TextDirection::LeftToRight, "en-US", {}, {}, split});
        expectIssue(A::ValidationIssue::InvalidTextRange);
        const std::array<A::TextRange, 1> whole{{{0, 3}}};
        text.selection = whole;
        check(context, "whole combining grapheme validates", Bridge::validate(tree.view()).ok());
        text.caret = 2;
        expectIssue(A::ValidationIssue::InvalidTextRange);
        text.caret.reset();
        const std::array<A::TextRange, 2> multiple{{{0, 0}, {3, 3}}};
        text.selection = multiple;
        expectIssue(A::ValidationIssue::InvalidSelection);
        tree.nodes[1].selection = A::SelectionInfo{.multiSelectable = true};
        check(context, "multiple text selections require explicit policy", Bridge::validate(tree.view()).ok());
        tree.nodes[1].selection.reset();
        tree.nodes[1].text.reset();
        tree.nodes[1].virtualization = A::VirtualizationInfo{true, false, true, 0, 1};
        expectIssue(A::ValidationIssue::InvalidVirtualization);
        tree.nodes[1].actions.flags |= action(A::ActionKind::Realize);
        check(context, "virtualization requires copied Realize action", Bridge::validate(tree.view()).ok());
        tree.nodes[1].virtualization.reset();
        const std::array<A::NodeId, 1> targets{77};
        const std::array<A::Relation, 1> relations{{{A::RelationKind::LabelledBy, targets}}};
        tree.nodes[1].relations = relations;
        expectIssue(A::ValidationIssue::MissingNode);
        tree.nodes[1].relations = {};
        A::ExtensionValue value;
        value.kind = A::ExtensionValueKind::List;
        value.listValue = {&value, 1};
        const A::Extension extension{"example.vendor", "recursive", value};
        tree.nodes[1].extensions = {&extension, 1};
        expectIssue(A::ValidationIssue::LimitExceeded);
        tree.nodes[1].extensions = {};
        limits = {};
        limits.maximumTextBytesPerNode = 1;
        equal(context, "text budgets are cumulative", A::ValidationIssue::LimitExceeded, Bridge::validate(tree.view(), limits).issue);
    }

    void testBuilder(Context &context)
    {
        Bridge::SnapshotBuilder builder;
        equal(context, "empty authoring state", std::size_t{0}, builder.nodeCount());
        equal(context, "zero root fails", Error::InvalidArgument, builder.setRoot(0).code);
        check(context, "builder generation", builder.setGeneration(1).ok());
        check(context, "builder root", builder.setRoot(1).ok());
        std::string name = "Original";
        std::array<A::NodeId, 1> children{2};
        std::string extensionText = "owned";
        A::ExtensionValue item;
        item.utf8Value = extensionText;
        A::ExtensionValue list;
        list.kind = A::ExtensionValueKind::List;
        list.listValue = {&item, 1};
        const A::Extension extension{"example.vendor", "property", list};
        A::Node root;
        root.id = 1;
        root.children = children;
        root.name = name;
        root.extensions = {&extension, 1};
        check(context, "builder copies borrowed node", builder.addNode(root).ok());
        name.assign("Changed");
        extensionText.assign("other");
        children[0] = 99;
        const auto copy = builder.view().nodes.front();
        equal(context, "name owns bytes", std::string_view("Original"), copy.name);
        equal(context, "children own IDs", A::NodeId{2}, copy.children.front());
        equal(context, "nested extensions own text", std::string_view("owned"), copy.extensions[0].value.listValue[0].utf8Value);
        A::Node child;
        child.id = 2;
        child.parent = 1;
        check(context, "child adds", builder.addNode(child).ok());
        equal(context, "duplicate builder node fails", Error::InvalidArgument, builder.addNode(child).code);
        check(context, "owned tree validates", Bridge::validate(builder.view()).ok());
        Bridge::SnapshotBuilder moved(std::move(builder));
        equal(context, "move retains tree", std::size_t{2}, moved.nodeCount());
        moved.clear();
        equal(context, "clear resets identity", A::Generation{0}, moved.view().generation);
    }

    void testLifetime(Context &context)
    {
        Desktop::Window window;
        auto bridge = window.accessibility();
        Tree tree;
        check(context, "closed bridge starts disabled", !bridge.enabled());
        equal(context, "closed enable", Error::NotOpen, bridge.enable().code);
        if (!open(context, window))
        {
            return;
        }
        check(context, "explicit enable", bridge.enable().ok());
        equal(context, "duplicate enable", Error::AlreadyOpen, bridge.enable().code);
        check(context, "publish complete snapshot", bridge.publish(tree.view()).ok());
        const auto retained = bridge.readSnapshot();
        check(context, "retained reader", retained.isValid());
        tree.nodes[1].name = "New";
        check(context, "newer generation replaces", bridge.publish(tree.view(2)).ok());
        equal(context, "retained reader immutable", std::string_view("Run"), retained.find(2)->name);
        equal(context, "stale publish rejected", Error::InvalidArgument, bridge.publish(tree.view(1)).code);
        Bridge::SnapshotBuilder copied;
        check(context, "explicit owning copy", bridge.copySnapshot(copied).ok());
        equal(context, "copy keeps generation", A::Generation{2}, copied.view().generation);
        std::atomic<bool> published{false}, wrongThreadRejected{false};
        std::thread worker(
            [&]
            {
                published = bridge.publish(tree.view(3)).ok();
                wrongThreadRejected = bridge.disable().code == Error::ResourceBusy;
            });
        worker.join();
        check(context, "worker publishes", published.load());
        check(context, "foreign lifecycle rejected", wrongThreadRejected.load());
#if DESKTOP_INTERNAL_TEST_HOOKS
        Desktop::TestHooks::failNext(Desktop::TestHooks::FailurePoint::Allocation);
        equal(context, "publication allocation failure", Error::OutOfMemory, bridge.publish(tree.view(4)).code);
        equal(context, "failed publication preserves tree", A::Generation{3}, bridge.snapshotInfo().generation);
#endif
        check(context, "close gates bridge", window.close().ok());
        check(context, "close clears active tree", !bridge.readSnapshot().isValid());
        check(context, "retained portable reader survives close", retained.find(2) != nullptr);
        if (!open(context, window))
        {
            return;
        }
        check(context, "reopen is explicitly disabled", !bridge.enabled());
        check(context, "reenable", bridge.enable().ok());
        equal(context, "reopen cannot reuse old generation", Error::InvalidArgument, bridge.publish(tree.view(3)).code);
        check(context, "fresh generation after reopen", bridge.publish(tree.view(4)).ok());
        check(context, "disable idempotent", bridge.disable().ok() && bridge.disable().ok());
        equal(context, "disabled publish", Error::NotOpen, bridge.publish(tree.view(5)).code);
        check(context, "close reopened Window", window.close().ok());
    }

    // ------------------------------------------------------------
    // Native COM fixtures
    // ------------------------------------------------------------

    template <typename T> struct Com
    {
        T *value = nullptr;
        Com() = default;

        explicit Com(T *source)
            : value(source)
        {
        }

        ~Com()
        {
            if (value)
            {
                value->Release();
            }
        }
        Com(const Com &) = delete;
        Com &operator=(const Com &) = delete;

        T **put()
        {
            if (value)
            {
                value->Release();
            }
            value = nullptr;
            return &value;
        }

        explicit operator bool() const
        {
            return value != nullptr;
        }

        T *operator->() const
        {
            return value;
        }
    };

    template <typename T> bool pattern(Context &context, IRawElementProviderSimple *provider, PATTERNID id, Com<T> &out)
    {
        Com<IUnknown> unknown;
        if (!context.expectTrue("pattern lookup succeeds", SUCCEEDED(provider->GetPatternProvider(id, unknown.put())) && static_cast<bool>(unknown)))
        {
            return false;
        }
        return context.expectTrue(
            "pattern has correct interface",
            SUCCEEDED(unknown->QueryInterface(__uuidof(T), reinterpret_cast<void **>(out.put()))));
    }
#if DESKTOP_INTERNAL_TEST_HOOKS
    void drain(Desktop::Window &window)
    {
        A::Notification notification;
        while (Desktop::TestHooks::popAccessibilityNotification(window, notification))
        {
        }
    }
    // ------------------------------------------------------------
    // Bounded transport and publication races
    // ------------------------------------------------------------

    void testQueues(Context &context)
    {
        Desktop::Window window;
        if (!open(context, window))
        {
            return;
        }
        A::Options options;
        options.limits.maximumActionQueue = 2;
        options.limits.maximumNotificationQueue = 2;
        auto bridge = window.accessibility();
        check(context, "small bounded bridge enables", bridge.enable(options).ok());
        Tree tree;
        check(context, "queue tree publishes", bridge.publish(tree.view()).ok());
        drain(window);

        const auto submit = [&](A::ActionKind kind)
        {
            A::ActionRequest request;
            request.node = 2;
            request.action = kind;
            return Desktop::TestHooks::submitAccessibilityAction(window, std::move(request));
        };
        const auto first = submit(A::ActionKind::Focus), replacement = submit(A::ActionKind::Focus);
        check(
            context,
            "focus tail coalesces",
            first.acceptance == A::ActionAcceptance::Accepted && replacement.requestId > first.requestId && bridge.queueInfo().actions.pending == 1);
        equal(context, "durable Invoke accepted", A::ActionAcceptance::Accepted, submit(A::ActionKind::Invoke).acceptance);
        equal(context, "coalescing cannot cross durable barrier", A::ActionAcceptance::QueueFull, submit(A::ActionKind::Focus).acceptance);
        equal(context, "newest overflow rejected", std::uint64_t{1}, bridge.queueInfo().actions.rejected);
        A::ActionRequest request;
        check(context, "pop coalesced action", bridge.popAction(request));
        equal(context, "latest request ID", replacement.requestId, request.requestId);
        A::Node root;
        root.id = 1;
        const std::array<A::Node, 1> nodes{root};
        check(context, "node removal publishes", bridge.publish({2, 1, nodes}).ok());
        check(context, "already queued action remains deliverable", bridge.popAction(request));
        equal(context, "stale observed generation delivered", A::Generation{1}, request.observedGeneration);
        equal(context, "stale durable kind retained", A::ActionKind::Invoke, request.action);
        check(context, "queue empty", !bridge.popAction(request));
        drain(window);
        check(context, "unchanged full tree restored", bridge.publish(tree.view(3)).ok());
        drain(window);
        check(context, "identical payload newer generation publishes", bridge.publish(tree.view(4)).ok());
        equal(context, "identical payload produces no notifications", std::uint32_t{0}, bridge.queueInfo().notifications.pending);
        tree.nodes[1].name = "Changed";
        tree.nodes[1].description = "Changed too";
        tree.nodes[1].value = "Third change";
        check(context, "pressure still commits snapshot", bridge.publish(tree.view(5)).ok());
        check(context, "pressure collapses to invalidation", bridge.queueInfo().notifications.invalidationPending);
        A::Notification notification;
        check(context, "invalidation pops", Desktop::TestHooks::popAccessibilityNotification(window, notification));
        equal(context, "single invalidation category", A::NotificationKind::TreeInvalidated, notification.kind);
        equal(context, "committed latest generation", A::Generation{5}, notification.generation);
        tree.nodes[1].states.flags |= state(A::State::Focused);
        check(context, "semantic focus publishes", bridge.publish(tree.view(6)).ok());
        drain(window);
        const auto handles = Desktop::Native::Win32::getHandle(window);
        if (handles.status.ok())
        {
            // Deterministic native message boundary, independent of desktop activation policy.
            SendMessageW(handles.handle.window, WM_KILLFOCUS, 0, 0);
            drain(window);
            SendMessageW(handles.handle.window, WM_SETFOCUS, 0, 0);
            bool focusEvent = false;
            while (Desktop::TestHooks::popAccessibilityNotification(window, notification))
            {
                focusEvent = focusEvent || (notification.kind == A::NotificationKind::FocusChanged && notification.node == 2);
            }
            check(context, "host focus regain notifies semantic focused node", focusEvent);
        }
        check(context, "event pump succeeds", Desktop::Events::poll().status.ok());
        check(context, "small bridge closes", window.close().ok());
    }

    void testCapacityAndInteraction(Context &context)
    {
        Desktop::Window window;
        if (!open(context, window))
        {
            return;
        }
        auto bridge = window.accessibility();
        A::Options options;
        options.limits.maximumNodes = 2;
        options.limits.maximumNativeProviders = 2;
        options.limits.maximumNotificationQueue = 3;
        check(context, "bounded provider registry enables", bridge.enable(options).ok());
        Tree tree;
        tree.nodes[0].language = "en-US";
        check(context, "bounded registry initial publication", bridge.publish(tree.view()).ok());
        Com<IRawElementProviderSimple> retained(static_cast<IRawElementProviderSimple *>(Desktop::TestHooks::accessibilityProvider(window, 2)));
        if (!context.expectTrue("registry retains initial provider", static_cast<bool>(retained)))
        {
            return;
        }
        tree.children[0] = 3;
        tree.nodes[1].id = 3;
        equal(context, "new native identity rejects at lifetime bound", Error::SizeLimitExceeded, bridge.publish(tree.view(2)).code);
        equal(context, "provider pressure preserves committed generation", A::Generation{1}, bridge.snapshotInfo().generation);
        check(context, "provider pressure preserves old semantic identity", bridge.readSnapshot().find(2) != nullptr);
        tree.children[0] = 2;
        tree.nodes[1].id = 2;
        check(context, "failed preparation does not consume generation", bridge.publish(tree.view(2)).ok());
        drain(window);
        if (bridge.features().supports(A::Feature::Announcements))
        {
            check(context, "default-locale announcement queues", bridge.announce({1, "Queued speech"}).ok());
            equal(context, "announcement language support is explicit", Error::Unsupported, bridge.announce({1, "Bonjour", "fr-FR"}).code);
            check(context, "announcement matching source language queues", bridge.announce({1, "Queued speech", "en-US"}).ok());
            tree.nodes[0].name = "Updated root";
            tree.nodes[1].name = "Updated button";
            check(context, "notification pressure commits despite announcements", bridge.publish(tree.view(3)).ok());
            check(context, "full notification queue collapses to invalidation", bridge.queueInfo().notifications.invalidationPending);
            drain(window);
            check(
                context,
                "announcement accepted behind invalidation",
                bridge.publish(tree.view(4)).ok() && bridge.announce({1, "Retained speech"}).ok());
            tree.nodes[0].name = "Root again";
            check(context, "diff appended beside announcement", bridge.publish(tree.view(5)).ok());
            A::Notification notification;
            check(context, "queued announcement remains first", Desktop::TestHooks::popAccessibilityNotification(window, notification));
            equal(context, "explicit announcement not silently lost without pressure", A::NotificationKind::LiveRegionChanged, notification.kind);
        }
        Com<IInvokeProvider> invoke;
        if (!pattern(context, retained.value, UIA_InvokePatternId, invoke))
        {
            return;
        }
        check(context, "explicit host interaction disable", window.setUserInteractionEnabled(false).ok());
        equal(context, "disabled host rejects native action", static_cast<HRESULT>(UIA_E_ELEMENTNOTENABLED), invoke->Invoke());
        VARIANT enabled{};
        check(context, "disabled host property query", SUCCEEDED(retained->GetPropertyValue(UIA_IsEnabledPropertyId, &enabled)));
        check(context, "disabled host reports native disabled", enabled.vt == VT_BOOL && enabled.boolVal == VARIANT_FALSE);
        VariantClear(&enabled);
        check(context, "host interaction restore", window.setUserInteractionEnabled(true).ok());
        Desktop::ProgressDialog blocker;
        Desktop::Types::Dialogs::Progress::Description progress;
        progress.owner = &window;
        progress.title = "Accessibility interaction regression";
        if (context.expectTrue("modeless owner blocker opens", blocker.open(progress).ok()))
        {
            equal(context, "modal owner block rejects accessibility action", static_cast<HRESULT>(UIA_E_ELEMENTNOTENABLED), invoke->Invoke());
            check(context, "owner blocker closes", blocker.close().ok());
            static_cast<void>(Desktop::Events::poll());
            equal(context, "unblocked host accepts action", S_OK, invoke->Invoke());
            A::ActionRequest request;
            check(context, "unblocked accepted action drains", bridge.popAction(request));
        }
        check(context, "capacity interaction Window closes", window.close().ok());
    }

    void testClosePublicationRace(Context &context)
    {
        Desktop::Window window;
        if (!open(context, window))
        {
            return;
        }
        auto bridge = window.accessibility();
        check(context, "publication race bridge enables", bridge.enable().ok());
        Tree tree;
        check(context, "publication race initial tree", bridge.publish(tree.view()).ok());
        const auto retained = bridge.readSnapshot();
        const std::string largeText(std::size_t{512} * 1024, 'x');
        tree.nodes[1].text = A::TextContent{largeText};
        std::atomic<bool> started{false};
        Error result = Error::InvalidArgument;
        std::thread publisher(
            [&]
            {
                started = true;
                result = bridge.publish(tree.view(2)).code;
            });
        while (!started.load())
        {
            std::this_thread::yield();
        }
        check(context, "close gates in-flight publication", window.close().ok());
        publisher.join();
        check(context, "concurrent publication either commits before close or observes gate", result == Error::Success || result == Error::NotOpen);
        check(context, "in-flight publication never resurrects active snapshot", !bridge.snapshotInfo().available);
        equal(context, "retained reader survives concurrent close", std::string_view("Run"), retained.find(2)->name);
        if (!open(context, window))
        {
            return;
        }
        check(context, "race Window reenable", bridge.enable().ok());
        tree.nodes[1].text.reset();
        check(context, "fresh activation after race publishes", bridge.publish(tree.view(3)).ok());
        check(context, "race Window final close", window.close().ok());
    }
    // ------------------------------------------------------------
    // Native node providers
    // ------------------------------------------------------------

    void testProviders(Context &context)
    {
        Desktop::Window window;
        if (!open(context, window))
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
        if (!context.expectTrue("stable providers exist", root && button))
        {
            return;
        }
        VARIANT value{};
        equal(context, "native name query", S_OK, button->GetPropertyValue(UIA_NamePropertyId, &value));
        check(context, "native name UTF16", value.vt == VT_BSTR && std::wstring_view(value.bstrVal, SysStringLen(value.bstrVal)) == L"Run");
        VariantClear(&value);
        Com<IInvokeProvider> invoke;
        if (pattern(context, button.value, UIA_InvokePatternId, invoke))
        {
            equal(context, "Invoke is accepted transport", S_OK, invoke->Invoke());
            A::ActionRequest request;
            check(context, "native action available", bridge.popAction(request));
            equal(context, "native action kind", A::ActionKind::Invoke, request.action);
            equal(context, "native generation", A::Generation{1}, request.observedGeneration);
        }
        Com<IValueProvider> stringPattern;
        if (pattern(context, button.value, UIA_ValuePatternId, stringPattern))
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
        if (fragment)
        {
            Com<IRawElementProviderFragment> parent;
            equal(context, "parent navigation", S_OK, fragment->Navigate(NavigateDirection_Parent, parent.put()));
            check(context, "parent exists", static_cast<bool>(parent));
            SAFEARRAY *runtimeId = nullptr;
            equal(context, "runtime identity", S_OK, fragment->GetRuntimeId(&runtimeId));
            if (runtimeId)
            {
                SafeArrayDestroy(runtimeId);
            }
        }
        const auto handles = Desktop::Native::Win32::getHandle(window);
        if (handles.status.ok())
        {
            check(context, "WM_GETOBJECT publishes UIA provider", SendMessageW(handles.handle.window, WM_GETOBJECT, 0, UiaRootObjectId) != 0);
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
        if (!open(context, window))
        {
            return;
        }
        check(context, "native reopen enable", bridge.enable().ok());
        check(context, "fresh native publish", bridge.publish(tree.view(4)).ok());
        equal(context, "old root never revives across reopen", kUnavailable, root->GetPropertyValue(UIA_NamePropertyId, &value));
        VariantClear(&value);
        check(context, "native reopen closes", window.close().ok());
    }
    // ------------------------------------------------------------
    // Native text ranges and attributes
    // ------------------------------------------------------------

    void testText(Context &context)
    {
        Desktop::Window window;
        if (!open(context, window))
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
        if (!context.expectTrue("text provider exists", static_cast<bool>(provider)))
        {
            return;
        }
        Com<ITextProvider2> patternProvider;
        if (!pattern(context, provider.value, UIA_TextPattern2Id, patternProvider))
        {
            return;
        }
        Com<ITextRangeProvider> range, clone, overflow;
        equal(context, "document range", S_OK, patternProvider->get_DocumentRange(range.put()));
        if (!range)
        {
            return;
        }
        BSTR wide = nullptr;
        equal(context, "text full query", S_OK, range->GetText(-1, &wide));
        check(context, "text preserves combining and nonBMP scalars", std::wstring_view(wide, SysStringLen(wide)) == L"e\u0301 \U0001F642\nlast");
        SysFreeString(wide);
        equal(context, "clone claims second pool slot", S_OK, range->Clone(clone.put()));
        equal(context, "pool pressure rejects newest range", HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA), range->Clone(overflow.put()));
        if (clone)
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
        check(context, "emoji remains intact", SysStringLen(wide) == 2 && wide[0] == 0xD83D && wide[1] == 0xDE42);
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
        if (context.expectTrue("text search creates a retained range", static_cast<bool>(clone)))
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
        if (!open(context, window))
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
        if (!context.expectTrue("text semantics provider exists", static_cast<bool>(provider)))
        {
            return;
        }
        Com<ITextProvider2> text;
        Com<IValueProvider> value;
        if (!pattern(context, provider.value, UIA_TextPattern2Id, text) || !pattern(context, provider.value, UIA_ValuePatternId, value))
        {
            return;
        }

        SAFEARRAY *selection = nullptr;
        equal(context, "missing selection query", S_OK, text->GetSelection(&selection));
        check(context, "missing selection and caret do not invent an insertion point", selection == nullptr);
        if (selection)
        {
            SafeArrayDestroy(selection);
        }

        content.caret = 1;
        check(context, "explicit caret publishes", bridge.publish(tree.view(2)).ok());
        equal(context, "explicit caret selection query", S_OK, text->GetSelection(&selection));
        if (context.expectTrue("explicit caret supplies one selection range", selection && selection->rgsabound[0].cElements == 1))
        {
            Com<IUnknown> item;
            LONG index = 0;
            equal(context, "caret selection element", S_OK, SafeArrayGetElement(selection, &index, static_cast<void *>(item.put())));
            Com<ITextRangeProvider> caret;
            if (item && SUCCEEDED(item->QueryInterface(__uuidof(ITextRangeProvider), reinterpret_cast<void **>(caret.put()))))
            {
                Com<ITextRangeProvider> document;
                equal(context, "caret comparison document", S_OK, text->get_DocumentRange(document.put()));
                int distance = 0;
                if (document)
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
                if (rectangles)
                {
                    SafeArrayDestroy(rectangles);
                }
                if (document)
                {
                    equal(context, "nondegenerate rectangle query", S_OK, document->GetBoundingRectangles(&rectangles));
                    check(context, "document retains its visible fragment rectangle", rectangles && rectangles->rgsabound[0].cElements == 4);
                    if (rectangles)
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
        if (selection)
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
        if (selection)
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
        for (const auto &entry : cases)
        {
            tree.nodes[1].states.flags = entry.states;
            tree.nodes[1].actions.flags = entry.actions;
            check(context, "editability snapshot publishes", bridge.publish(tree.view(generation++)).ok());
            Com<ITextRangeProvider> document;
            equal(context, "editability document range", S_OK, text->get_DocumentRange(document.put()));
            if (!document)
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
        if (!open(context, window))
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
        if (!context.expectTrue("attribute provider exists", static_cast<bool>(provider)))
        {
            return;
        }
        Com<ITextProvider> text;
        if (!pattern(context, provider.value, UIA_TextPatternId, text))
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
            if (!document)
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
        content.annotations = {annotations.data(), 1};
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

    // ------------------------------------------------------------
    // Externally retained lifetime
    // ------------------------------------------------------------

    void testRetainedObjects(Context &context)
    {
        auto window = std::make_unique<Desktop::Window>();
        if (!open(context, *window))
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
        if (!context.expectTrue("retained-object provider exists", static_cast<bool>(provider)))
        {
            return;
        }
        Com<ITextProvider> text;
        if (!pattern(context, provider.value, UIA_TextPatternId, text))
        {
            return;
        }
        Com<ITextRangeProvider> range;
        check(context, "retained native range exists", SUCCEEDED(text->get_DocumentRange(range.put())) && range);
        window.reset();
        const auto *retained = reader.find(2);
        check(context, "portable reader outlives C++ Window", retained && retained->text && retained->text->utf8 == "Retained document");
        VARIANT name{};
        equal(
            context,
            "retained native node unavailable after object destruction",
            kUnavailable,
            provider->GetPropertyValue(UIA_NamePropertyId, &name));
        if (range)
        {
            BSTR content = nullptr;
            equal(context, "retained native range unavailable after object destruction", kUnavailable, range->GetText(-1, &content));
            range.put(); // The pool remains alive through this last externally retained range release.
        }
    }
    // ------------------------------------------------------------
    // Control patterns and grid membership
    // ------------------------------------------------------------

    void testControlPatterns(Context &context)
    {
        Desktop::Window window;
        if (!open(context, window))
        {
            return;
        }
        auto bridge = window.accessibility();
        check(context, "control bridge enables", bridge.enable().ok());
        std::array<A::Node, 8> nodes{};
        const std::array<A::NodeId, 5> rootChildren{2, 4, 5, 6, 7};
        const std::array<A::NodeId, 1> cellChildren{3}, textChildren{8}, selected{3};
        for (std::size_t i = 0; i < nodes.size(); ++i)
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
        if (!context.expectTrue("all control providers exist", table && cell && slider && toggle && expand && annotationNode))
        {
            return;
        }
        Com<IGridProvider> grid;
        if (pattern(context, table.value, UIA_GridPatternId, grid))
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
        if (pattern(context, cell.value, UIA_GridItemPatternId, gridItem))
        {
            Com<IRawElementProviderSimple> container;
            equal(context, "containing grid", S_OK, gridItem->get_ContainingGrid(container.put()));
            check(context, "grid container identity", container.value == table.value);
        }
        Com<ITableProvider> tablePattern;
        if (pattern(context, table.value, UIA_TablePatternId, tablePattern))
        {
            SAFEARRAY *headers = nullptr;
            equal(context, "table headers", S_OK, tablePattern->GetRowHeaders(&headers));
            if (headers)
            {
                SafeArrayDestroy(headers);
            }
        }
        Com<ITableItemProvider> tableItem;
        check(context, "table-item pattern", pattern(context, cell.value, UIA_TableItemPatternId, tableItem));
        Com<ISelectionProvider2> selection;
        if (pattern(context, table.value, 10034, selection))
        {
            int count = 0;
            equal(context, "Selection2 item count", S_OK, selection->get_ItemCount(&count));
            equal(context, "Selection2 copied count", 1, count);
            Com<IRawElementProviderSimple> active;
            equal(context, "Selection2 active item", S_OK, selection->get_CurrentSelectedItem(active.put()));
            check(context, "Selection2 active identity", active.value == cell.value);
        }
        Com<ISelectionItemProvider> selectedItem;
        if (pattern(context, cell.value, UIA_SelectionItemPatternId, selectedItem))
        {
            equal(context, "add selection transport", S_OK, selectedItem->AddToSelection());
            equal(context, "explicit additive selection", A::SelectionOperation::Add, pop(A::ActionKind::Select).selectionOperation);
            equal(context, "remove selection transport", S_OK, selectedItem->RemoveFromSelection());
            pop(A::ActionKind::Deselect);
        }
        Com<IRangeValueProvider> range;
        if (pattern(context, slider.value, UIA_RangeValuePatternId, range))
        {
            double value = 0;
            equal(context, "numeric current value", S_OK, range->get_Value(&value));
            equal(context, "numeric snapshot value", 5.0, value);
            equal(context, "numeric range rejects invalid destination", E_INVALIDARG, range->SetValue(11));
            equal(context, "numeric setter transport", S_OK, range->SetValue(7));
            equal(context, "numeric typed payload", 7.0, pop(A::ActionKind::SetValue).value);
        }
        Com<IScrollProvider> scroll;
        if (pattern(context, slider.value, UIA_ScrollPatternId, scroll))
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
        if (pattern(context, cell.value, UIA_ScrollItemPatternId, scrollItem))
        {
            equal(context, "scroll item transport", S_OK, scrollItem->ScrollIntoView());
            pop(A::ActionKind::ScrollTo);
        }
        Com<IToggleProvider> togglePattern;
        if (pattern(context, toggle.value, UIA_TogglePatternId, togglePattern))
        {
            ToggleState value = ToggleState_Off;
            equal(context, "mixed toggle state query", S_OK, togglePattern->get_ToggleState(&value));
            equal(context, "mixed toggle state", ToggleState_Indeterminate, value);
            equal(context, "toggle transport", S_OK, togglePattern->Toggle());
            pop(A::ActionKind::Toggle);
        }
        Com<IExpandCollapseProvider> expandPattern;
        if (pattern(context, expand.value, UIA_ExpandCollapsePatternId, expandPattern))
        {
            equal(context, "expand transport", S_OK, expandPattern->Expand());
            pop(A::ActionKind::Expand);
            equal(context, "collapse transport", S_OK, expandPattern->Collapse());
            pop(A::ActionKind::Collapse);
        }
        Com<IVirtualizedItemProvider> virtualPattern;
        if (pattern(context, cell.value, UIA_VirtualizedItemPatternId, virtualPattern))
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
        if (pattern(context, table.value, UIA_ItemContainerPatternId, items))
        {
            Com<IRawElementProviderSimple> item;
            VARIANT filter{};
            equal(context, "item container local search", S_OK, items->FindItemByProperty(nullptr, 0, filter, item.put()));
            check(context, "item-container identity", item.value == cell.value);
        }
        Com<IAnnotationProvider> annotationPattern;
        if (pattern(context, annotationNode.value, UIA_AnnotationPatternId, annotationPattern))
        {
            int type = 0;
            equal(context, "annotation type query", S_OK, annotationPattern->get_AnnotationTypeId(&type));
            equal(context, "spelling annotation type", AnnotationType_SpellingError, type);
        }
        Com<ITextChildProvider> textChild;
        if (pattern(context, annotationNode.value, UIA_TextChildPatternId, textChild))
        {
            Com<ITextRangeProvider> textRange;
            equal(context, "text child associated range", S_OK, textChild->get_TextRange(textRange.put()));
            BSTR text = nullptr;
            if (textRange)
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
        if (!open(context, window))
        {
            return;
        }
        auto bridge = window.accessibility();
        check(context, "grid lookup bridge enables", bridge.enable().ok());
        std::array<A::Node, 6> nodes{};
        const std::array<A::NodeId, 2> rootChildren{2, 4};
        const std::array<A::NodeId, 1> outerCells{3}, innerRows{5}, innerCells{6};
        for (std::size_t i = 0; i < nodes.size(); ++i)
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
        if (!context.expectTrue("nested grid providers exist", outer && cell && inner && nestedCell))
        {
            return;
        }
        Com<IGridProvider> grid, nestedGrid;
        if (!pattern(context, outer.value, UIA_GridPatternId, grid) || !pattern(context, inner.value, UIA_GridPatternId, nestedGrid))
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

    // ------------------------------------------------------------
    // Host geometry, cleanup, and concurrency
    // ------------------------------------------------------------

    void testGeometryAndExceptionalLifetime(Context &context)
    {
        auto window = std::make_unique<Desktop::Window>();
        if (!open(context, *window))
        {
            return;
        }
        auto bridge = window->accessibility();
        check(context, "geometry bridge enables", bridge.enable().ok());
        Tree tree;
        tree.nodes[1].geometry = A::Geometry{{1, 2, 10, 20}, {0, 1, -1, 0, 50, 40}, A::Rect{30, 42, 10, 8}};
        check(context, "transformed geometry publishes", bridge.publish(tree.view()).ok());
        Desktop::TestHooks::PresentationPublicationSnapshot host;
        host.clientSize = {200, 100};
        host.contentScale = {2, 1.5F};
        host.visible = true;
        Desktop::TestHooks::applyPresentationPublicationSnapshot(*window, host);
        const auto origin = window->clientPosition();
        Com<IRawElementProviderSimple> provider(static_cast<IRawElementProviderSimple *>(Desktop::TestHooks::accessibilityProvider(*window, 2)));
        Com<IRawElementProviderSimple> root(static_cast<IRawElementProviderSimple *>(Desktop::TestHooks::accessibilityProvider(*window, 1)));
        if (!context.expectTrue("geometry providers exist", provider && root))
        {
            return;
        }
        Com<IRawElementProviderFragment> fragment;
        check(
            context,
            "geometry fragment",
            SUCCEEDED(provider->QueryInterface(__uuidof(IRawElementProviderFragment), reinterpret_cast<void **>(fragment.put()))));
        if (fragment)
        {
            UiaRect bounds{};
            equal(context, "geometry query", S_OK, fragment->get_BoundingRectangle(&bounds));
            equal(context, "physical clipped x", static_cast<double>(origin.x) + 60, bounds.left);
            equal(context, "physical clipped y", static_cast<double>(origin.y) + 63, bounds.top);
            equal(context, "physical clipped width", 20.0, bounds.width);
            equal(context, "physical clipped height", 12.0, bounds.height);
        }
        Com<IRawElementProviderFragmentRoot> fragmentRoot;
        check(
            context,
            "geometry root",
            SUCCEEDED(root->QueryInterface(__uuidof(IRawElementProviderFragmentRoot), reinterpret_cast<void **>(fragmentRoot.put()))));
        if (fragmentRoot)
        {
            Com<IRawElementProviderFragment> hit;
            equal(context, "inverse affine hit test", S_OK, fragmentRoot->ElementProviderFromPoint(origin.x + 70.0, origin.y + 67.5, hit.put()));
            Com<IRawElementProviderSimple> hitSimple;
            if (hit)
            {
                check(
                    context,
                    "hit exposes simple identity",
                    SUCCEEDED(hit->QueryInterface(__uuidof(IRawElementProviderSimple), reinterpret_cast<void **>(hitSimple.put()))));
            }
            check(context, "hit finds transformed child", hitSimple.value == provider.value);
        }
        check(context, "unexpected native destruction", Desktop::TestHooks::destroyNativeWindow(*window).ok());
        VARIANT value{};
        equal(
            context,
            "native destruction immediately gates retained provider",
            kUnavailable,
            provider->GetPropertyValue(UIA_NamePropertyId, &value));
        check(context, "native destruction clears active snapshot", !bridge.snapshotInfo().available);
        check(context, "unexpected destruction finalizes", window->close().ok());
        if (!open(context, *window))
        {
            return;
        }
        check(context, "exceptional lifetime reenable", bridge.enable().ok());
        check(context, "exceptional lifetime fresh publish", bridge.publish(tree.view(2)).ok());
        Com<IRawElementProviderSimple> retained(static_cast<IRawElementProviderSimple *>(Desktop::TestHooks::accessibilityProvider(*window, 1)));
        std::thread destroyer(
            [owned = std::move(window)]() mutable
            {
                owned.reset();
            });
        destroyer.join();
        equal(context, "wrong-thread destructor gates before native cleanup", kUnavailable, retained->GetPropertyValue(UIA_NamePropertyId, &value));
        check(context, "owner pump drains deferred native cleanup", Desktop::Events::poll().status.ok());
    }

    void testConcurrentQueries(Context &context)
    {
        Desktop::Window window;
        if (!open(context, window))
        {
            return;
        }
        auto bridge = window.accessibility();
        check(context, "concurrent bridge enables", bridge.enable().ok());
        Tree tree;
        check(context, "concurrent initial snapshot", bridge.publish(tree.view()).ok());
        Com<IRawElementProviderSimple> provider(static_cast<IRawElementProviderSimple *>(Desktop::TestHooks::accessibilityProvider(window, 2)));
        if (!context.expectTrue("concurrent provider exists", static_cast<bool>(provider)))
        {
            return;
        }
        std::atomic<bool> failed{false}, stop{false};
        std::thread client(
            [&]
            {
                while (!stop.load())
                {
                    VARIANT value{};
                    const auto hr = provider->GetPropertyValue(UIA_NamePropertyId, &value);
                    if (hr != S_OK && hr != kUnavailable)
                    {
                        failed = true;
                    }
                    if (hr == S_OK && (value.vt != VT_BSTR || SysStringLen(value.bstrVal) == 0))
                    {
                        failed = true;
                    }
                    VariantClear(&value);
                }
            });
        std::thread publisher(
            [&]
            {
                for (A::Generation generation = 2; generation < 100; ++generation)
                {
                    tree.nodes[1].name = (generation & 1U) != 0 ? "Odd" : "Even";
                    if (!bridge.publish(tree.view(generation)).ok())
                    {
                        failed = true;
                    }
                }
            });
        publisher.join();
        check(context, "close while native queries active", window.close().ok());
        stop = true;
        client.join();
        check(context, "queries observe complete generations or unavailable", !failed.load());
    }
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
        description.visible = false;
        if (!window.open(description).ok())
        {
            return 1;
        }
        auto bridge = window.accessibility();
        if (!bridge.enable().ok())
        {
            return 2;
        }
        Tree tree;
        tree.nodes[1].text = A::TextContent{"Smoke text"};
        if (!bridge.publish(tree.view()).ok())
        {
            return 3;
        }
        const auto handles = Desktop::Native::Win32::getHandle(window);
        if (!handles.status.ok())
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
                if (FAILED(initialized))
                {
                    done = true;
                    return;
                }
                const auto runClient = [&]() -> int
                {
                    Com<IUIAutomation> automation;
                    if (FAILED(CoCreateInstance(
                            CLSID_CUIAutomation,
                            nullptr,
                            CLSCTX_INPROC_SERVER,
                            __uuidof(IUIAutomation),
                            reinterpret_cast<void **>(automation.put()))))
                    {
                        return 6;
                    }
                    Com<IUIAutomationElement> root, child;
                    if (FAILED(automation->ElementFromHandle(handles.handle.window, root.put())) || !root)
                    {
                        return 7;
                    }
                    BSTR name = nullptr;
                    const auto named = root->get_CurrentName(&name);
                    const bool correctName = SUCCEEDED(named) && std::wstring_view(name, SysStringLen(name)) == L"Example";
                    SysFreeString(name);
                    if (!correctName)
                    {
                        return 8;
                    }
                    VARIANT id{};
                    id.vt = VT_BSTR;
                    id.bstrVal = SysAllocString(L"2");
                    if (!id.bstrVal)
                    {
                        return 9;
                    }
                    Com<IUIAutomationCondition> condition;
                    const auto filtered = automation->CreatePropertyCondition(UIA_AutomationIdPropertyId, id, condition.put());
                    VariantClear(&id);
                    if (FAILED(filtered) || !condition || FAILED(root->FindFirst(TreeScope_Children, condition.value, child.put())) || !child)
                    {
                        return 10;
                    }
                    Com<IUnknown> unknown;
                    if (FAILED(child->GetCurrentPattern(UIA_InvokePatternId, unknown.put())) || !unknown)
                    {
                        return 11;
                    }
                    Com<IUIAutomationInvokePattern> invoke;
                    if (FAILED(unknown->QueryInterface(__uuidof(IUIAutomationInvokePattern), reinterpret_cast<void **>(invoke.put()))) ||
                        FAILED(invoke->Invoke()))
                    {
                        return 12;
                    }
                    unknown.put();
                    if (FAILED(child->GetCurrentPattern(UIA_TextPatternId, unknown.put())) || !unknown)
                    {
                        return 13;
                    }
                    Com<IUIAutomationTextPattern> text;
                    Com<IUIAutomationTextRange> range;
                    if (FAILED(unknown->QueryInterface(__uuidof(IUIAutomationTextPattern), reinterpret_cast<void **>(text.put()))) ||
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
        while (!done.load())
        {
            static_cast<void>(Desktop::Events::wait(std::chrono::milliseconds{10}));
            A::ActionRequest request;
            while (bridge.popAction(request))
            {
                invoked = invoked || request.action == A::ActionKind::Invoke;
            }
        }
        client.join();
        A::ActionRequest request;
        while (bridge.popAction(request))
        {
            invoked = invoked || request.action == A::ActionKind::Invoke;
        }
        if (!window.close().ok())
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
