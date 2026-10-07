/// @file accessibility_snapshot.cpp
/// @brief Owned authoring storage and indexed immutable snapshot inspection.

#include "desktop/internal/accessibility_state.h"
#include <algorithm>
#include <limits>
#include <new>

namespace GameWIP::Desktop::Detail
{
    // ------------------------------------------------------------
    // Owned payload copies
    // ------------------------------------------------------------

    std::string_view OwnedAccessibilityNode::copyString(std::string_view source)
    {
        if (source.empty())
            return {};
        strings.emplace_back(source);
        return strings.back();
    }

    A::ExtensionValue OwnedAccessibilityNode::copyExtension(const A::ExtensionValue &source)
    {
        A::ExtensionValue result;
        result.kind = source.kind;
        switch (source.kind)
        {
        case A::ExtensionValueKind::Boolean:
            result.booleanValue = source.booleanValue;
            break;
        case A::ExtensionValueKind::SignedInteger:
            result.signedValue = source.signedValue;
            break;
        case A::ExtensionValueKind::UnsignedInteger:
            result.unsignedValue = source.unsignedValue;
            break;
        case A::ExtensionValueKind::Double:
            result.doubleValue = source.doubleValue;
            break;
        case A::ExtensionValueKind::Utf8:
            result.utf8Value = copyString(source.utf8Value);
            break;
        case A::ExtensionValueKind::NodeReference:
            result.nodeValue = source.nodeValue;
            break;
        case A::ExtensionValueKind::List:
        {
            extensionLists.emplace_back();
            auto &items = extensionLists.back();
            items.reserve(source.listValue.size());
            for (const auto &item : source.listValue)
                items.push_back(copyExtension(item));
            result.listValue = items;
            break;
        }
        }
        return result;
    }

    OwnedAccessibilityNode::OwnedAccessibilityNode(const A::Node &source)
        : node(source)
    {
        node.name = copyString(source.name);
        node.description = copyString(source.description);
        node.helpText = copyString(source.helpText);
        node.value = copyString(source.value);
        node.language = copyString(source.language);
        node.accessKey = copyString(source.accessKey);
        node.keyboardShortcut = copyString(source.keyboardShortcut);
        children.assign(source.children.begin(), source.children.end());
        node.children = children;
        relations.reserve(source.relations.size());
        for (const auto &relation : source.relations)
        {
            relationTargets.emplace_back(relation.targets.begin(), relation.targets.end());
            relations.push_back({relation.kind, relationTargets.back()});
        }
        node.relations = relations;
        extensions.reserve(source.extensions.size());
        for (const auto &extension : source.extensions)
            extensions.push_back({copyString(extension.namespaceName), copyString(extension.name), copyExtension(extension.value)});
        node.extensions = extensions;
        if (source.selection)
        {
            selectedNodes.assign(source.selection->selectedNodes.begin(), source.selection->selectedNodes.end());
            node.selection->selectedNodes = selectedNodes;
        }
        if (source.text)
        {
            node.text->utf8 = copyString(source.text->utf8);
            node.text->language = copyString(source.text->language);
            annotations.assign(source.text->annotations.begin(), source.text->annotations.end());
            for (auto &annotation : annotations)
                annotation.value = copyString(annotation.value);
            fragments.assign(source.text->fragments.begin(), source.text->fragments.end());
            textSelection.assign(source.text->selection.begin(), source.text->selection.end());
            node.text->annotations = annotations;
            node.text->fragments = fragments;
            node.text->selection = textSelection;
        }
    }

    // ------------------------------------------------------------
    // Published snapshot lookup
    // ------------------------------------------------------------

    std::size_t PublishedSnapshot::findIndex(A::NodeId id) const noexcept
    {
        const auto found = std::lower_bound(
            index.begin(),
            index.end(),
            id,
            [](const auto &item, auto key)
            {
                return item.first < key;
            });
        return found != index.end() && found->first == id ? found->second : storage.nodes.size();
    }

    const A::Node *PublishedSnapshot::find(A::NodeId id) const noexcept
    {
        const auto found = findIndex(id);
        return found < storage.nodes.size() ? &storage.nodes[found] : nullptr;
    }
} // namespace GameWIP::Desktop::Detail

namespace GameWIP::Desktop::Accessibility
{
    namespace
    {
        namespace A = Types::Accessibility;

        bool boundedNode(const A::Node &node, const A::Limits &limits, std::uint64_t &bytes)
        {
            std::uint64_t nodeBytes = 0, values = 0;
            const auto string = [&](std::string_view text)
            {
                if (text.size() > limits.maximumTextBytesPerNode)
                    return false;
                nodeBytes += text.size();
                return nodeBytes <= limits.maximumTextBytesPerNode;
            };
            for (auto text : {node.name, node.description, node.helpText, node.value, node.language, node.accessKey, node.keyboardShortcut})
                if (!string(text))
                    return false;
            if (node.children.size() > limits.maximumChildrenPerNode || node.relations.size() > limits.maximumRelationsPerNode ||
                node.extensions.size() > limits.maximumExtensionsPerNode)
                return false;
            std::uint64_t targets = 0;
            for (const auto &relation : node.relations)
                if ((targets += relation.targets.size()) > limits.maximumRelationTargetsPerNode)
                    return false;
            const auto extension = [&](const auto &self, const A::ExtensionValue &value, std::uint32_t depth) -> bool
            {
                if (depth > limits.maximumExtensionDepth || ++values > limits.maximumExtensionValuesPerNode)
                    return false;
                if (value.kind == A::ExtensionValueKind::Utf8)
                    return string(value.utf8Value);
                if (value.kind == A::ExtensionValueKind::List)
                {
                    if (value.listValue.size() > limits.maximumExtensionListItems)
                        return false;
                    for (const auto &item : value.listValue)
                        if (!self(self, item, depth + 1))
                            return false;
                }
                return value.kind <= A::ExtensionValueKind::List;
            };
            for (const auto &value : node.extensions)
                if (!string(value.namespaceName) || !string(value.name) || !extension(extension, value.value, 0))
                    return false;
            if (node.selection && node.selection->selectedNodes.size() > limits.maximumNodes)
                return false;
            if (node.text)
            {
                const auto &text = *node.text;
                if (text.annotations.size() > limits.maximumTextAnnotationsPerNode || text.fragments.size() > limits.maximumTextFragmentsPerNode ||
                    text.selection.size() > limits.maximumTextSelectionsPerNode || !string(text.utf8) || !string(text.language))
                    return false;
                for (const auto &annotation : text.annotations)
                    if (!string(annotation.value))
                        return false;
            }
            bytes += nodeBytes;
            return bytes <= limits.maximumTotalTextBytes;
        }
    } // namespace

    // ------------------------------------------------------------
    // Snapshot authoring
    // ------------------------------------------------------------

    SnapshotBuilder::SnapshotBuilder() noexcept = default;

    SnapshotBuilder::SnapshotBuilder(const A::Limits &limits) noexcept
        : limits_(limits)
    {
    }
    SnapshotBuilder::~SnapshotBuilder() noexcept = default;
    SnapshotBuilder::SnapshotBuilder(SnapshotBuilder &&) noexcept = default;
    SnapshotBuilder &SnapshotBuilder::operator=(SnapshotBuilder &&) noexcept = default;

    void SnapshotBuilder::clear() noexcept
    {
        state_.reset();
    }

    const A::Limits &SnapshotBuilder::limits() const noexcept
    {
        return limits_;
    }

    IO::Types::Status SnapshotBuilder::setGeneration(A::Generation generation) noexcept
    {
        if (generation == 0)
            return IO::makeStatus(IO::Types::ErrorCode::InvalidArgument);
        try
        {
            if (!state_)
                state_ = std::make_unique<Detail::SnapshotBuilderState>();
            state_->generation = generation;
            return {};
        }
        catch (...)
        {
            return IO::makeStatus(IO::Types::ErrorCode::OutOfMemory);
        }
    }

    IO::Types::Status SnapshotBuilder::setRoot(A::NodeId root) noexcept
    {
        if (root == 0)
            return IO::makeStatus(IO::Types::ErrorCode::InvalidArgument);
        try
        {
            if (!state_)
                state_ = std::make_unique<Detail::SnapshotBuilderState>();
            state_->root = root;
            return {};
        }
        catch (...)
        {
            return IO::makeStatus(IO::Types::ErrorCode::OutOfMemory);
        }
    }

    IO::Types::Status SnapshotBuilder::addNode(const A::Node &node) noexcept
    {
        if (validate({}, limits_).issue == A::ValidationIssue::InvalidLimits || node.id == 0)
            return IO::makeStatus(IO::Types::ErrorCode::InvalidArgument);
        try
        {
            if (state_ && state_->nodes.size() >= limits_.maximumNodes)
                return IO::makeStatus(IO::Types::ErrorCode::SizeLimitExceeded);
            if (state_ && state_->nodeIds.contains(node.id))
                return IO::makeStatus(IO::Types::ErrorCode::InvalidArgument);

            std::uint64_t bytes = state_ ? state_->textBytes : 0;
            if (!boundedNode(node, limits_, bytes))
                return IO::makeStatus(IO::Types::ErrorCode::SizeLimitExceeded);

            auto owned = std::make_shared<Detail::OwnedAccessibilityNode>(node);
            if (!state_)
                state_ = std::make_unique<Detail::SnapshotBuilderState>();
            if (state_->nodes.size() == state_->nodes.capacity())
            {
                const auto capacity =
                    std::min<std::size_t>(limits_.maximumNodes, std::max<std::size_t>(state_->nodes.size() + 1, state_->nodes.capacity() * 2));
                state_->owned.reserve(capacity);
                state_->nodes.reserve(capacity);
            }

            state_->nodeIds.insert(node.id);
            state_->owned.push_back(std::move(owned));
            state_->nodes.push_back(state_->owned.back()->node);
            state_->textBytes = bytes;
            return {};
        }
        catch (const std::bad_alloc &)
        {
            return IO::makeStatus(IO::Types::ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return IO::makeStatus(IO::Types::ErrorCode::SizeLimitExceeded);
        }
    }

    std::size_t SnapshotBuilder::nodeCount() const noexcept
    {
        return state_ ? state_->nodes.size() : 0;
    }

    A::SnapshotView SnapshotBuilder::view() const noexcept
    {
        return state_ ? state_->view() : A::SnapshotView{};
    }
    // ------------------------------------------------------------
    // Immutable snapshot inspection
    // ------------------------------------------------------------

    SnapshotReader::SnapshotReader(std::shared_ptr<const Detail::PublishedSnapshot> snapshot) noexcept
        : snapshot_(std::move(snapshot))
    {
    }
    SnapshotReader::~SnapshotReader() noexcept = default;

    bool SnapshotReader::isValid() const noexcept
    {
        return snapshot_ != nullptr;
    }

    A::SnapshotInfo SnapshotReader::info() const noexcept
    {
        return snapshot_
                   ? A::SnapshotInfo{true, snapshot_->storage.generation, snapshot_->storage.root, static_cast<std::uint32_t>(snapshot_->storage.nodes.size())}
                   : A::SnapshotInfo{};
    }

    A::SnapshotView SnapshotReader::view() const noexcept
    {
        return snapshot_ ? snapshot_->storage.view() : A::SnapshotView{};
    }

    const A::Node *SnapshotReader::find(A::NodeId node) const noexcept
    {
        return snapshot_ ? snapshot_->find(node) : nullptr;
    }
} // namespace GameWIP::Desktop::Accessibility
