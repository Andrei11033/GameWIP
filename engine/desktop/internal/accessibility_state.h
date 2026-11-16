/// @file accessibility_state.h
/// @brief Immutable semantic storage and lifetime-safe accessibility transport.

#pragma once

#include "desktop/accessibility.h"

#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

namespace GameWIP::Desktop::Detail
{
    namespace A = Types::Accessibility;
    struct WindowState;

    /// ------------------------------------------------------------
    /// Owned and published semantic storage
    /// ------------------------------------------------------------

    /// @brief Deep-owned payload storage backing one borrowed Node value.
    /// @details Deques keep nested string/list views stable while recursive copies append storage.
    /// Each owned node stays at a stable heap address; its views expire with that owner.
    struct OwnedAccessibilityNode
    {
        // Borrowed node value
        A::Node node;

        // Stable owners for nested payloads
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

        /// @brief Copies the active node payloads after the caller has checked copy budgets.
        /// @details May throw on allocation; checked builder/publication entry points translate failures.
        explicit OwnedAccessibilityNode(const A::Node &source);
        /// @brief Retains string bytes and returns a view unaffected by later appends.
        std::string_view copyString(std::string_view source);
        /// @brief Recursively owns the active extension payload without retaining caller views.
        A::ExtensionValue copyExtension(const A::ExtensionValue &source);
    };

    /// @brief Authoring metadata and stable owners behind the contiguous borrowed node view.
    struct SnapshotBuilderState
    {
        // Snapshot identity
        A::Generation generation = 0;
        A::NodeId root = 0;

        // Owned nodes and the borrowed view
        std::vector<std::shared_ptr<OwnedAccessibilityNode>> owned;
        std::vector<A::Node> nodes;

        // Validation indexes and copy budget
        std::unordered_set<A::NodeId> nodeIds;
        std::uint64_t textBytes = 0;

        [[nodiscard]] A::SnapshotView view() const noexcept
        {
            return {generation, root, nodes};
        }
    };

    /// @brief Publication-time native strings and byte-offset indexes for immutable text queries.
    struct AccessibilityTextCache
    {
        // UTF-16 properties
        std::u16string name, description, help, value, language, accessKey, shortcut, text, textLanguage;
        std::vector<std::u16string> annotationValues;

        // Text indexes and boundaries
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

    /// @brief Immutable owned tree, ID index, Unicode caches, and prepared native metadata.
    /// @details Shared retention keeps all node views alive independently of the Window activation.
    struct PublishedSnapshot
    {
        // Owned tree and focused node
        SnapshotBuilderState storage;
        A::NodeId focusedNode = 0;

        // Lookup, text, and native metadata
        std::vector<std::pair<A::NodeId, std::size_t>> index;
        std::vector<AccessibilityTextCache> textCache;
        std::shared_ptr<AccessibilityNativeSnapshot> native;

        /// @brief Uses the sorted ID index to return a retained node, or nullptr for an unknown ID.
        [[nodiscard]] const A::Node *find(A::NodeId id) const noexcept;
        /// @brief Returns the node's storage index, or storage.nodes.size() when absent.
        [[nodiscard]] std::size_t findIndex(A::NodeId id) const noexcept;
    };

#if defined(__cpp_lib_atomic_shared_ptr) && __cpp_lib_atomic_shared_ptr >= 201711L
    template <typename Value> using AtomicSharedPointer = std::atomic<std::shared_ptr<Value>>;
#else
    /// @brief Portable atomic shared-pointer wrapper for standard libraries without the C++20 specialization.
    template <typename Value> class AtomicSharedPointer
    {
    public:
        [[nodiscard]] std::shared_ptr<Value> load(std::memory_order order = std::memory_order_seq_cst) const noexcept
        {
            return std::atomic_load_explicit(&value_, order);
        }

        void store(std::shared_ptr<Value> value, std::memory_order order = std::memory_order_seq_cst) noexcept
        {
            std::atomic_store_explicit(&value_, std::move(value), order);
        }

    private:
        std::shared_ptr<Value> value_;
    };
#endif

    /// ------------------------------------------------------------
    /// Host state and bounded runtime transport
    /// ------------------------------------------------------------

    /// @brief Coherent physical screen origin/client extent and logical-to-pixel scale.
    struct AccessibilityHostGeometry
    {
        // Logical bounds and scale
        double x = 0, y = 0, width = 0, height = 0, scaleX = 1, scaleY = 1;

        // Host visibility and focus
        bool visible = false, focused = false;
    };

    /// @brief Retains both diff generations and any copied announcement until owner-thread delivery.
    struct AccessibilityPendingNotification
    {
        // Notification and retained semantic generations
        A::Notification notification;
        std::shared_ptr<const PublishedSnapshot> before, after;

        // Owned announcement text and delivery priority
        std::u16string announcement;
        A::AnnouncementPriority priority = A::AnnouncementPriority::Normal;
    };

    /// @brief Shared per-activation provider state with a close gate and bounded transport.
    /// @details Native readers retain immutable snapshots. Each queue has its own mutex; native
    /// notification delivery releases the transport lock before entering the platform client.
    struct AccessibilityRuntime
    {
        // Validation and owner-thread policy
        A::Limits limits;
        A::Features features;
        std::thread::id ownerThread;
        std::uint32_t nativeThread = 0;

        // Activation and native host state
        std::atomic<bool> live{true};
        std::atomic<bool> hostEnabled{true}; ///< Actual native owner interaction, including transient modal owner blocking.
        std::atomic<std::uintptr_t> nativeWindow{0};

        // Published semantic snapshot
        AtomicSharedPointer<const PublishedSnapshot> snapshot;
        std::shared_ptr<AccessibilityNativeState> native;

        // Request and notification synchronization
        std::mutex actionMutex, notificationMutex;

        // Pending actions
        std::vector<A::ActionRequest> actions;
        std::size_t actionHead = 0, actionCount = 0;
        A::RequestId nextRequest = 1;
        std::uint64_t rejected = 0;

        // Pending notifications
        std::vector<AccessibilityPendingNotification> notifications;
        std::size_t notificationHead = 0, notificationCount = 0;
        std::uint64_t collapsed = 0;
        bool invalidationPending = false;

        // Atomically published host geometry
        std::atomic<std::uint64_t> geometrySequence{0};
        std::atomic<double> x{0}, y{0}, width{0}, height{0}, scaleX{1}, scaleY{1};
        std::atomic<bool> visible{false}, focused{false};

        /// @brief Publishes one owner-thread geometry update with a sequence protecting compound reads.
        void publishGeometry(const AccessibilityHostGeometry &geometry) noexcept;
        /// @brief Attempts a bounded coherent read without blocking the native client on an owner update.
        [[nodiscard]] bool readGeometry(AccessibilityHostGeometry &geometry) const noexcept;
    };

    /// @brief Object-lifetime publication serialization and generation history across activations.
    struct AccessibilityState
    {
        // Active bridge
        AtomicSharedPointer<AccessibilityRuntime> runtime;

        // Publication order across activations
        std::mutex publicationMutex;
        A::Generation lastGeneration = 0;
    };

    /// ------------------------------------------------------------
    /// Window integration and native backend boundary
    /// ------------------------------------------------------------

    /// @brief Validates and queues an owning native action; acceptance does not execute application work.
    [[nodiscard]] A::ActionResult submitAccessibilityAction(AccessibilityRuntime &runtime, A::ActionRequest request) noexcept;
    /// @brief Gates the active bridge, detaches native providers, and clears snapshot/transport retention.
    void closeAccessibility(WindowState &window) noexcept;
    /// @brief Queues a latest-tree invalidation for host changes without advancing semantic generation.
    void invalidateAccessibilityHost(WindowState &window, bool focusGained = false) noexcept;
    /// @brief Delivers pending notifications on the owner thread without holding transport locks across native calls.
    void drainAccessibilityNotifications(WindowState &window) noexcept;

    namespace Platform
    {
        [[nodiscard]] IO::Types::Status enableAccessibility(WindowState &window, const std::shared_ptr<AccessibilityRuntime> &runtime) noexcept;
        [[nodiscard]] IO::Types::Status prepareAccessibilitySnapshot(
            const std::shared_ptr<AccessibilityRuntime> &runtime,
            PublishedSnapshot &snapshot) noexcept;
        void detachAccessibility(AccessibilityRuntime &runtime) noexcept;
        void wakeAccessibility(const AccessibilityRuntime &runtime) noexcept;
        void deliverAccessibilityNotification(AccessibilityRuntime &runtime, const AccessibilityPendingNotification &event) noexcept;
    } // namespace Platform
} // namespace GameWIP::Desktop::Detail
