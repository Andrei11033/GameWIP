/// @file accessibility_contract_tests.inl
/// @brief Portable snapshot contract tests for the Desktop accessibility suite.

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
    tree.nodes[1].states.flags |= std::uint64_t{1} << 63U;
    expectIssue(A::ValidationIssue::InvalidState);
    tree.nodes[1].states.flags &= ~(std::uint64_t{1} << 63U);
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
    A::ExtensionValue value = A::ExtensionValue::list({});
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
    const A::ExtensionValue item = A::ExtensionValue::utf8(extensionText);
    const std::array<A::ExtensionValue, 1> items{item};
    const A::ExtensionValue list = A::ExtensionValue::list(items);
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

    if(!open(context, window))
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

    if(!open(context, window))
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
