/// @file accessibility_state.h
/// @brief Immutable semantic storage and lifetime-safe accessibility transport.

#pragma once

#include "desktop/accessibility.h"

#include <atomic>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>
#include <unordered_set>

namespace GameWIP::Desktop::Detail
{
    namespace A = Types::Accessibility;
    struct WindowState;

    // ------------------------------------------------------------
    // Owned and published semantic storage
    // ------------------------------------------------------------

    struct OwnedAccessibilityNode
    {
        A::Node node;
        std::deque<std::string> strings;
        std::vector<A::NodeId> children;
        std::vector<A::Relation> relations;
        std::deque<std::vector<A::NodeId>> relationTargets;
        std::vector<A::Extension> extensions;
        std::deque<std::vector<A::ExtensionValue>> extensionLists;
        std::vector<A::TextAnnotation> annotations;
        std::vector<A::TextFragment> fragments;
        std::vector<A::TextRange> textSelection;
        std::vector<A::NodeId> selectedNodes;

        explicit OwnedAccessibilityNode(const A::Node &source);
        std::string_view copyString(std::string_view source);
        A::ExtensionValue copyExtension(const A::ExtensionValue &source);
    };

    struct SnapshotBuilderState
    {
        A::Generation generation = 0;
        A::NodeId root = 0;
        std::vector<std::shared_ptr<OwnedAccessibilityNode>> owned;
        std::vector<A::Node> nodes;
        std::unordered_set<A::NodeId> nodeIds;
        std::uint64_t textBytes = 0;

        [[nodiscard]] A::SnapshotView view() const noexcept
        {
            return {generation, root, nodes};
        }
    };

    struct AccessibilityTextCache
    {
        std::u16string name, description, help, value, language, accessKey, shortcut, text, textLanguage;
        std::vector<std::u16string> annotationValues;
        std::vector<std::uint32_t> scalars;
        std::vector<std::uint32_t> utf16Offsets;
        std::vector<std::uint32_t> graphemes;
        std::vector<std::uint32_t> words;
        std::vector<std::uint32_t> lines;
        std::vector<std::uint32_t> formats;
    };

    struct AccessibilityNativeSnapshot
    {
        virtual ~AccessibilityNativeSnapshot() = default;
    };
    struct AccessibilityNativeState
    {
        virtual ~AccessibilityNativeState() = default;
    };

    struct PublishedSnapshot
    {
        SnapshotBuilderState storage;
        A::NodeId focusedNode = 0;
        std::vector<std::pair<A::NodeId, std::size_t>> index;
        std::vector<AccessibilityTextCache> textCache;
        std::shared_ptr<AccessibilityNativeSnapshot> native;

        [[nodiscard]] const A::Node *find(A::NodeId id) const noexcept;
        [[nodiscard]] std::size_t findIndex(A::NodeId id) const noexcept;
    };

    // ------------------------------------------------------------
    // Host state and bounded runtime transport
    // ------------------------------------------------------------

    struct AccessibilityHostGeometry
    {
        double x = 0, y = 0, width = 0, height = 0, scaleX = 1, scaleY = 1;
        bool visible = false, focused = false;
    };

    struct AccessibilityPendingNotification
    {
        A::Notification notification;
        std::shared_ptr<const PublishedSnapshot> before, after;
        std::u16string announcement;
        A::AnnouncementPriority priority = A::AnnouncementPriority::Normal;
    };

    struct AccessibilityRuntime
    {
        A::Limits limits;
        A::Features features;
        std::thread::id ownerThread;
        std::uint32_t nativeThread = 0;

        std::atomic<bool> live{true};
        std::atomic<bool> hostEnabled{true}; ///< Actual native owner interaction, including transient modal owner blocking.
        std::atomic<std::uintptr_t> nativeWindow{0};

        std::atomic<std::shared_ptr<const PublishedSnapshot>> snapshot;
        std::shared_ptr<AccessibilityNativeState> native;

        std::mutex actionMutex, notificationMutex;
        std::vector<A::ActionRequest> actions;
        std::size_t actionHead = 0, actionCount = 0;
        A::RequestId nextRequest = 1;
        std::uint64_t rejected = 0;

        std::vector<AccessibilityPendingNotification> notifications;
        std::size_t notificationHead = 0, notificationCount = 0;
        std::uint64_t collapsed = 0;
        bool invalidationPending = false;

        std::atomic<std::uint64_t> geometrySequence{0};
        std::atomic<double> x{0}, y{0}, width{0}, height{0}, scaleX{1}, scaleY{1};
        std::atomic<bool> visible{false}, focused{false};

        /// @brief Publishes one owner-thread geometry update with a sequence protecting compound reads.
        void publishGeometry(const AccessibilityHostGeometry &geometry) noexcept;
        /// @brief Attempts a bounded coherent read without blocking the native client on an owner update.
        [[nodiscard]] bool readGeometry(AccessibilityHostGeometry &geometry) const noexcept;
    };

    struct AccessibilityState
    {
        std::atomic<std::shared_ptr<AccessibilityRuntime>> runtime;
        std::mutex publicationMutex;
        A::Generation lastGeneration = 0;
    };

    // ------------------------------------------------------------
    // Window integration and native backend boundary
    // ------------------------------------------------------------

    [[nodiscard]] A::ActionResult submitAccessibilityAction(AccessibilityRuntime &runtime, A::ActionRequest request) noexcept;
    void closeAccessibility(WindowState &window) noexcept;
    void publishAccessibilityGeometry(WindowState &window) noexcept;
    void invalidateAccessibilityHost(WindowState &window, bool focusGained = false) noexcept;
    void drainAccessibilityNotifications(WindowState &window) noexcept;

    namespace Platform
    {
        [[nodiscard]] IO::Types::Status enableAccessibility(WindowState &window, const std::shared_ptr<AccessibilityRuntime> &runtime) noexcept;
        [[nodiscard]] IO::Types::Status prepareAccessibilitySnapshot(
            const std::shared_ptr<AccessibilityRuntime> &runtime,
            PublishedSnapshot &snapshot) noexcept;
        void detachAccessibility(AccessibilityRuntime &runtime) noexcept;
        void wakeAccessibility(const AccessibilityRuntime &runtime) noexcept;
        void deliverAccessibilityNotification(AccessibilityRuntime &runtime, const AccessibilityPendingNotification &notification) noexcept;
    } // namespace Platform
} // namespace GameWIP::Desktop::Detail
