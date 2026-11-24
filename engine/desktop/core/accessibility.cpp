/// @file accessibility.cpp
/// @brief Accessibility snapshot publication and facade operations.

#include "desktop/internal/accessibility_state.h"
#include "desktop/platform/window.h"
#include "desktop/internal/desktop_test_hooks.h"
#include "desktop/internal/helpers/accessibility_publication.h"
#include "unicode/unicode.h"

#include <algorithm>
#include <memory>
#include <new>
#include <thread>
#include <utility>

namespace GameWIP::Desktop::Accessibility
{
    namespace
    {
        namespace A = Types::Accessibility;

        /// ------------------------------------------------------------
        /// Active runtime lookup
        /// ------------------------------------------------------------

        std::shared_ptr<Internal::AccessibilityRuntime> runtimeFor(const Window *window) noexcept
        {
            auto *state = window ? Internal::WindowAccess::accessibilityState(*window) : nullptr;
            return state ? state->runtime.load() : nullptr;
        }

    } // namespace

    /// ------------------------------------------------------------
    /// Bridge activation
    /// ------------------------------------------------------------

    Facade::Facade(Window &window) noexcept
        : window_(&window)
    {
    }

    IO::Types::Status Facade::enable(const A::Options &options) noexcept
    {
        // Validate the Window, thread, capability, and limits before allocating.
        if(!window_->isOpen())
        {
            return IO::makeStatus(IO::Types::ErrorCode::NotOpen);
        }

        if(!window_->ownedByCurrentThread())
        {
            return IO::makeStatus(IO::Types::ErrorCode::ResourceBusy);
        }

        if(!window_->supports(Types::Capability::Accessibility))
        {
            return IO::makeStatus(IO::Types::ErrorCode::Unsupported);
        }

        if(enabled())
        {
            return IO::makeStatus(IO::Types::ErrorCode::AlreadyOpen);
        }

        if(validate({}, options.limits).issue == A::ValidationIssue::InvalidLimits)
        {
            return IO::makeStatus(IO::Types::ErrorCode::InvalidArgument);
        }

        if(Internal::consumeFailure(Internal::TestHooks::FailurePoint::Allocation))
        {
            return IO::makeStatus(IO::Types::ErrorCode::OutOfMemory);
        }

        try
        {
            // Allocate bounded transport storage for this activation.
            auto runtime = std::make_shared<Internal::AccessibilityRuntime>();
            runtime->limits = options.limits;
            runtime->ownerThread = std::this_thread::get_id();
            runtime->actions.resize(options.limits.maximumActionQueue);
            runtime->notifications.resize(options.limits.maximumNotificationQueue);

            auto &state = Internal::WindowAccess::accessibilityStateOwner(*window_);

            if(!state)
            {
                state.reset(new Internal::AccessibilityState());
            }

            auto *windowState = Internal::WindowAccess::state(*window_);
            auto status = Platform::enableAccessibility(*windowState, runtime);

            if(!status.ok())
            {
                return status;
            }

            // Publish the runtime only after the native provider is ready.
            state->runtime.store(runtime);
            windowState->accessibility = state.get();
            Internal::publishAccessibilityGeometry(*windowState);
            return {};
        }
        catch(const std::bad_alloc &)
        {
            return IO::makeStatus(IO::Types::ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return IO::makeStatus(IO::Types::ErrorCode::SizeLimitExceeded);
        }
    }

    IO::Types::Status Facade::disable() noexcept
    {
        const auto runtime = runtimeFor(window_);

        if(!runtime || !runtime->live.load())
        {
            return {};
        }

        if(runtime->ownerThread != std::this_thread::get_id())
        {
            return IO::makeStatus(IO::Types::ErrorCode::ResourceBusy);
        }

        if(auto *state = Internal::WindowAccess::state(*window_))
        {
            Internal::closeAccessibility(*state);
        }

        return {};
    }

    bool Facade::enabled() const noexcept
    {
        const auto runtime = runtimeFor(window_);
        return runtime && runtime->live.load();
    }

    A::Features Facade::features() const noexcept
    {
        const auto runtime = runtimeFor(window_);
        return runtime && runtime->live.load() ? runtime->features : A::Features{};
    }

    /// ------------------------------------------------------------
    /// Snapshot publication and inspection
    /// ------------------------------------------------------------

    IO::Types::Status Facade::publish(const A::SnapshotView &snapshot) noexcept
    {
        const auto runtime = runtimeFor(window_);

        if(!runtime || !runtime->live.load())
        {
            return IO::makeStatus(IO::Types::ErrorCode::NotOpen);
        }

        // Validate before allocating retained storage.
        const auto validation = validate(snapshot, runtime->limits);

        if(!validation.ok())
        {
            return validation.status;
        }

        if(Internal::consumeFailure(Internal::TestHooks::FailurePoint::Allocation))
        {
            return IO::makeStatus(IO::Types::ErrorCode::OutOfMemory);
        }

        try
        {
            // Build owned nodes, indexes, and native text caches off-lock.
            auto candidate = std::make_shared<Internal::PublishedSnapshot>();
            candidate->storage.generation = snapshot.generation;
            candidate->storage.root = snapshot.root;
            candidate->storage.owned.reserve(snapshot.nodes.size());
            candidate->storage.nodes.reserve(snapshot.nodes.size());
            candidate->index.reserve(snapshot.nodes.size());
            candidate->textCache.resize(snapshot.nodes.size());

            for(std::size_t i = 0; i < snapshot.nodes.size(); ++i)
            {
                candidate->storage.owned.push_back(std::make_shared<Internal::OwnedAccessibilityNode>(snapshot.nodes[i]));
                candidate->storage.nodes.push_back(candidate->storage.owned.back()->node);
                candidate->index.emplace_back(snapshot.nodes[i].id, i);

                if(snapshot.nodes[i].states.contains(A::State::Focused))
                {
                    candidate->focusedNode = snapshot.nodes[i].id;
                }
                Internal::Helpers::indexAccessibilityText(snapshot.nodes[i], candidate->textCache[i]);
            }

            std::ranges::sort(candidate->index);

            auto *state = Internal::WindowAccess::accessibilityState(*window_);
            std::lock_guard publication(state->publicationMutex);

            // Recheck activation and generation while serializing publication.
            if(!runtime->live.load())
            {
                return IO::makeStatus(IO::Types::ErrorCode::NotOpen);
            }

            if(snapshot.generation <= state->lastGeneration)
            {
                return IO::makeStatus(IO::Types::ErrorCode::InvalidArgument);
            }
            const auto before = runtime->snapshot.load();

            if(before && before->storage.root != snapshot.root)
            {
                return IO::makeStatus(IO::Types::ErrorCode::InvalidArgument);
            }

            const auto status = Platform::prepareAccessibilitySnapshot(runtime, *candidate);

            if(!status.ok())
            {
                return status;
            }

            // Publish the prepared tree, then queue its committed-generation diff.
            if(!runtime->live.load())
            {
                return IO::makeStatus(IO::Types::ErrorCode::NotOpen);
            }
            runtime->snapshot.store(candidate);
            state->lastGeneration = snapshot.generation;
            Internal::Helpers::queueAccessibilityDiff(*runtime, before, candidate);
            Platform::wakeAccessibility(*runtime);
            return {};
        }
        catch(const std::bad_alloc &)
        {
            return IO::makeStatus(IO::Types::ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return IO::makeStatus(IO::Types::ErrorCode::SizeLimitExceeded);
        }
    }

    IO::Types::Status Facade::publish(const SnapshotBuilder &snapshot) noexcept
    {
        return publish(snapshot.view());
    }

    A::SnapshotInfo Facade::snapshotInfo() const noexcept
    {
        return readSnapshot().info();
    }

    SnapshotReader Facade::readSnapshot() const noexcept
    {
        const auto runtime = runtimeFor(window_);
        return SnapshotReader(runtime && runtime->live.load() ? runtime->snapshot.load() : nullptr);
    }

    IO::Types::Status Facade::copySnapshot(SnapshotBuilder &destination) const noexcept
    {
        const auto reader = readSnapshot();

        if(!reader.isValid())
        {
            return IO::makeStatus(IO::Types::ErrorCode::NotOpen);
        }

        // Build a complete replacement before changing the destination.
        SnapshotBuilder copy(destination.limits());
        auto status = copy.setGeneration(reader.info().generation);

        if(!status.ok())
        {
            return status;
        }

        status = copy.setRoot(reader.info().root);

        if(!status.ok())
        {
            return status;
        }

        for(const auto &node : reader.view().nodes)
        {
            status = copy.addNode(node);

            if(!status.ok())
            {
                return status;
            }
        }

        destination = std::move(copy);
        return {};
    }

    /// ------------------------------------------------------------
    /// Application actions and announcements
    /// ------------------------------------------------------------

    bool Facade::popAction(A::ActionRequest &outAction) noexcept
    {
        const auto runtime = runtimeFor(window_);

        if(!runtime || runtime->ownerThread != std::this_thread::get_id())
        {
            return false;
        }
        std::lock_guard lock(runtime->actionMutex);

        if(!runtime->live.load() || runtime->actionCount == 0)
        {
            return false;
        }
        auto &slot = runtime->actions[runtime->actionHead];
        outAction = std::move(slot);
        slot = {};
        runtime->actionHead = (runtime->actionHead + 1) % runtime->actions.size();
        --runtime->actionCount;
        return true;
    }

    A::QueueInfo Facade::queueInfo() const noexcept
    {
        const auto runtime = runtimeFor(window_);

        if(!runtime)
        {
            return {};
        }

        A::QueueInfo result;
        {
            std::lock_guard lock(runtime->actionMutex);
            result.actions = {
                static_cast<std::uint32_t>(runtime->actions.size()),
                static_cast<std::uint32_t>(runtime->actionCount),
                runtime->rejected};
        }

        {
            std::lock_guard lock(runtime->notificationMutex);
            result.notifications = {
                static_cast<std::uint32_t>(runtime->notifications.size()),
                static_cast<std::uint32_t>(runtime->notificationCount),
                runtime->collapsed,
                runtime->invalidationPending};
        }

        return result;
    }

    IO::Types::Status Facade::announce(const A::AnnouncementView &announcement) noexcept
    {
        const auto runtime = runtimeFor(window_);

        if(!runtime || !runtime->live.load())
        {
            return IO::makeStatus(IO::Types::ErrorCode::NotOpen);
        }

        if(!runtime->features.supports(A::Feature::Announcements))
        {
            return IO::makeStatus(IO::Types::ErrorCode::Unsupported);
        }

        // Validate announcement bounds and text before resolving its target.
        if(announcement.priority > A::AnnouncementPriority::High ||
           announcement.text.size() + announcement.language.size() > runtime->limits.maximumAnnouncementBytes)
        {
            return IO::makeStatus(IO::Types::ErrorCode::InvalidArgument);
        }

        if(Unicode::Utf8::validate(announcement.text).outcome != Unicode::Types::ValidationOutcome::Valid ||
           Unicode::Utf8::validate(announcement.language).outcome != Unicode::Types::ValidationOutcome::Valid)
        {
            return IO::makeStatus(IO::Types::ErrorCode::EncodingFailed);
        }

        // Resolve the requested node against the current generation.
        const auto snapshot = runtime->snapshot.load();
        A::NodeId id = announcement.node;
        if(id == 0 && snapshot)
        {
            id = snapshot->storage.root;
        }
        const A::Node *node = snapshot ? snapshot->find(id) : nullptr;

        if(node == nullptr)
        {
            return IO::makeStatus(IO::Types::ErrorCode::InvalidArgument);
        }

        if(!announcement.language.empty() && announcement.language != node->language && !runtime->features.supports(A::Feature::AnnouncementLanguage))
        {
            return IO::makeStatus(IO::Types::ErrorCode::Unsupported);
        }

        try
        {
            // Retain announcement text until owner-thread delivery.
            Internal::AccessibilityPendingNotification pending{
                {A::NotificationKind::LiveRegionChanged, snapshot->storage.generation, id},
                {},
                snapshot,
                Internal::Helpers::toUtf16(announcement.text),
                announcement.priority};
            std::lock_guard lock(runtime->notificationMutex);

            if(!runtime->live.load())
            {
                return IO::makeStatus(IO::Types::ErrorCode::NotOpen);
            }

            if(runtime->notificationCount == runtime->notifications.size())
            {
                return IO::makeStatus(IO::Types::ErrorCode::SizeLimitExceeded);
            }
            runtime->notifications[(runtime->notificationHead + runtime->notificationCount++) % runtime->notifications.size()] = std::move(pending);
            Platform::wakeAccessibility(*runtime);
            return {};
        }
        catch(...)
        {
            return IO::makeStatus(IO::Types::ErrorCode::OutOfMemory);
        }
    }
} // namespace GameWIP::Desktop::Accessibility
