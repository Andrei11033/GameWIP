/// @file shell.cpp
/// @brief Portable shell queues, jump lists, and registration forwarding.

#include "desktop/internal/results.h"
#include "desktop/shell.h"

#include "desktop/internal/shell_helpers.h"
#include "desktop/internal/shell_state.h"
#include "desktop/platform/shell.h"

#include <algorithm>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <span>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>

namespace GameWIP::Desktop::Internal
{
    /// ------------------------------------------------------------
    /// Queue and launch validation helpers
    /// ------------------------------------------------------------

    namespace
    {
        using ErrorCode = GameWIP::Status::Types::Code;

        /// @brief Builds a portable status from a single error code.
        [[nodiscard]] GameWIP::Status::Types::Status error(ErrorCode code) noexcept
        {
            return GameWIP::Desktop::Internal::statusFor(code);
        }

        /// @brief Returns whether the current thread owns an open shell event queue.
        [[nodiscard]] bool ownedByCurrentThread(const ShellEventQueueState &state) noexcept
        {
            return state.open && state.ownerThread == std::this_thread::get_id();
        }

        /// @brief Maps a logical queue position to its fixed-storage ring slot.
        [[nodiscard]] std::size_t physicalIndex(const ShellEventQueueState &state, std::size_t logicalIndex) noexcept
        {
            return (state.eventHead + logicalIndex) % state.eventStorage.size();
        }

        /// @brief Clears queue payloads while the caller holds the queue mutex.
        void clearEventsUnlocked(ShellEventQueueState &state) noexcept
        {
            for(std::size_t index = 0; index < state.eventCount; ++index)
            {
                state.eventStorage[physicalIndex(state, index)] = {};
            }
            state.eventHead = 0;
            state.eventCount = 0;
        }

        /// @brief Returns whether a launch action contains a target-substitution placeholder.
        [[nodiscard]] bool containsPlaceholder(const Types::Shell::LaunchActionView &action) noexcept
        {
            for(const Types::Shell::LaunchArgumentView &argument : action.arguments)
            {
                if(std::holds_alternative<Types::Shell::LaunchPlaceholder>(argument))
                {
                    return true;
                }
            }

            return false;
        }

        /// @brief Validates launch targets and arguments before shell registration or publication.
        [[nodiscard]] GameWIP::Status::Types::Status validateLaunchAction(const Types::Shell::LaunchActionView &action, bool allowPlaceholders) noexcept
        {
            if(action.executable.path.get().empty() || (!allowPlaceholders && containsPlaceholder(action)))
            {
                return error(ErrorCode::InvalidArgument);
            }

            for(const Types::Shell::LaunchArgumentView &argument : action.arguments)
            {
                if(const auto *literal = std::get_if<Types::Shell::LiteralLaunchArgumentView>(&argument))
                {
                    if(!validText(literal->text))
                    {
                        return error(ErrorCode::InvalidArgument);
                    }
                }
                else if(const auto *target = std::get_if<Types::Shell::TargetLaunchArgumentView>(&argument))
                {
                    if(const auto *path = std::get_if<Types::Shell::PathTargetView>(&target->target); path != nullptr && path->path.get().empty())
                    {
                        return error(ErrorCode::InvalidArgument);
                    }

                    if(const auto *uri = std::get_if<Types::Shell::UriTargetView>(&target->target); uri != nullptr && !validText(uri->uri, true))
                    {
                        return error(ErrorCode::InvalidArgument);
                    }

                    if(std::holds_alternative<std::monostate>(target->target))
                    {
                        return error(ErrorCode::InvalidArgument);
                    }
                }
            }

            return GameWIP::Status::Types::Status{};
        }

        /// @brief Validates one jump-list task shared by direct and categorized entries.
        [[nodiscard]] GameWIP::Status::Types::Status validateJumpListTask(const Types::JumpLists::Task &task) noexcept
        {
            if(!validText(task.title, true) || !validText(task.description))
            {
                return error(ErrorCode::InvalidArgument);
            }

            return validateLaunchAction(task.action, false);
        }

        /// @brief Validates shared file/URI registration fields and launch actions.
        [[nodiscard]] GameWIP::Status::Types::Status validateRegistration(
            std::string_view ownerKey,
            std::string_view identity,
            std::string_view displayName,
            const std::optional<Types::Shell::LaunchActionView> &defaultAction,
            std::span<const Types::Registration::Verb> verbs) noexcept
        {
            if(!validText(ownerKey, true) || !validText(identity, true) || !validText(displayName, true))
            {
                return error(ErrorCode::InvalidArgument);
            }

            if(defaultAction.has_value())
            {
                GameWIP::Status::Types::Status status = validateLaunchAction(*defaultAction, true);

                if(!status.ok())
                {
                    return status;
                }
            }

            for(const Types::Registration::Verb &verb : verbs)
            {
                if(!validText(verb.name, true) || !validText(verb.label, true))
                {
                    return error(ErrorCode::InvalidArgument);
                }

                GameWIP::Status::Types::Status status = validateLaunchAction(verb.action, true);

                if(!status.ok())
                {
                    return status;
                }
            }

            return GameWIP::Status::Types::Status{};
        }
    } // namespace

    /// @brief Appends one native shell callback payload under the queue lock.
    void publishShellEvent(ShellEventQueueState &queue, Types::Shell::Events::Payload payload) noexcept
    {
        std::lock_guard lock(queue.mutex);

        if(!queue.open || queue.eventStorage.empty())
        {
            return;
        }

        if(queue.eventCount == queue.eventStorage.size())
        {
            ++queue.droppedEvents;
            return;
        }

        try
        {
            const std::size_t slot = physicalIndex(queue, queue.eventCount);
            queue.eventStorage[slot] = Types::Shell::Event{queue.nextSequence++, std::move(payload)};
            ++queue.eventCount;
        }
        catch(...)
        {
            ++queue.droppedEvents;
        }
    }
} // namespace GameWIP::Desktop::Internal

namespace GameWIP::Desktop
{
    namespace
    {
        using ErrorCode = GameWIP::Status::Types::Code;

        /// @brief Builds a portable status from a single error code.
        [[nodiscard]] GameWIP::Status::Types::Status error(ErrorCode code) noexcept
        {
            return GameWIP::Desktop::Internal::statusFor(code);
        }
    } // namespace

    /// ------------------------------------------------------------
    /// Jump lists
    /// ------------------------------------------------------------

    GameWIP::Status::Types::Result<void> JumpLists::publish(const Types::JumpLists::Description &description) noexcept
    {
        try
        {
            // Validate direct tasks and category entries.
            for(const Types::JumpLists::Task &task : description.tasks)
            {
                GameWIP::Status::Types::Status status = Internal::validateJumpListTask(task);

                if(!status.ok())
                {
                    return status;
                }
            }

            for(const Types::JumpLists::Category &category : description.categories)
            {
                if(!Internal::validText(category.label, true) || category.tasks.empty())
                {
                    return error(ErrorCode::InvalidArgument);
                }

                for(const Types::JumpLists::Task &task : category.tasks)
                {
                    GameWIP::Status::Types::Status status = Internal::validateJumpListTask(task);

                    if(!status.ok())
                    {
                        return status;
                    }
                }
            }

            // Validate recent targets before native publication.
            for(const Types::Shell::TargetView &target : description.recentItems)
            {
                if(const auto *uri = std::get_if<Types::Shell::UriTargetView>(&target); uri != nullptr && !Internal::validText(uri->uri, true))
                {
                    return error(ErrorCode::InvalidArgument);
                }

                if(std::holds_alternative<std::monostate>(target))
                {
                    return error(ErrorCode::InvalidArgument);
                }
            }

            // Publish only after the complete description passes validation.
            return Platform::publishJumpLists(description);
        }
        catch(const std::bad_alloc &)
        {
            return error(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return error(ErrorCode::Unknown);
        }
    }

    /// ------------------------------------------------------------
    /// File and URI registration
    /// ------------------------------------------------------------

    Types::Registration::Result Registration::registerFileExtension(const Types::Registration::FileExtensionDescription &description) noexcept
    {
        GameWIP::Status::Types::Status status = Internal::validateRegistration(
            description.ownerKey,
            description.extension,
            description.displayName,
            description.defaultAction,
            description.verbs);

        if(status.ok() && !Internal::validText(description.contentType))
        {
            status = error(ErrorCode::InvalidArgument);
        }

        if(!status.ok())
        {
            return Types::Registration::Result::failure(std::move(status));
        }
        return Platform::registerFileExtension(description);
    }

    Types::Registration::Result Registration::registerUriScheme(const Types::Registration::UriSchemeDescription &description) noexcept
    {
        GameWIP::Status::Types::Status status = Internal::validateRegistration(
            description.ownerKey,
            description.scheme,
            description.displayName,
            description.defaultAction,
            description.verbs);

        if(!status.ok())
        {
            return Types::Registration::Result::failure(std::move(status));
        }
        return Platform::registerUriScheme(description);
    }

    Types::Registration::Result Registration::unregisterFileExtension(std::string_view ownerKey, std::string_view extension) noexcept
    {
        if(!Internal::validText(ownerKey, true) || !Internal::validText(extension, true))
        {
            return Types::Registration::Result::failure(error(ErrorCode::InvalidArgument));
        }
        return Platform::unregisterFileExtension(ownerKey, extension);
    }

    Types::Registration::Result Registration::unregisterUriScheme(std::string_view ownerKey, std::string_view scheme) noexcept
    {
        if(!Internal::validText(ownerKey, true) || !Internal::validText(scheme, true))
        {
            return Types::Registration::Result::failure(error(ErrorCode::InvalidArgument));
        }
        return Platform::unregisterUriScheme(ownerKey, scheme);
    }

    /// ------------------------------------------------------------
    /// Shell event queue
    /// ------------------------------------------------------------

    ShellEventQueue::ShellEventQueue() noexcept = default;

    ShellEventQueue::~ShellEventQueue() noexcept
    {
        if(state_ != nullptr)
        {
            static_cast<void>(close());
            state_.reset();
        }
    }

    GameWIP::Status::Types::Result<void> ShellEventQueue::open() noexcept
    {
        return open(Types::Shell::QueueConfiguration::owned());
    }

    GameWIP::Status::Types::Result<void> ShellEventQueue::open(Types::Shell::QueueConfiguration queue) noexcept
    {
        // Validate storage configuration and reject duplicate opens.
        const auto *ownedQueue = std::get_if<Types::Shell::QueueConfiguration::Owned>(&queue.storage);
        const auto *borrowedQueue = std::get_if<Types::Shell::QueueConfiguration::Borrowed>(&queue.storage);

        if((ownedQueue != nullptr && ownedQueue->capacity == 0) || (borrowedQueue != nullptr && borrowedQueue->storage.empty()))
        {
            return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::InvalidArgument);
        }

        if(state_ != nullptr)
        {
            return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::AlreadyOpen);
        }

        try
        {
            // Set up owned or caller-provided event storage.
            auto candidate = std::make_unique<Internal::ShellEventQueueState>();

            if(ownedQueue != nullptr)
            {
                if(ownedQueue->capacity > candidate->internalEvents.max_size())
                {
                    return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::InvalidArgument);
                }
                candidate->internalEvents.resize(ownedQueue->capacity);
                candidate->eventStorage = candidate->internalEvents;
                candidate->storageKind = Types::Events::StorageKind::Internal;
            }
            else
            {
                candidate->eventStorage = borrowedQueue->storage;
                candidate->storageKind = Types::Events::StorageKind::External;
            }

            // Publish the queue after its storage and owner thread are ready.
            candidate->ownerThread = std::this_thread::get_id();
            candidate->open = true;
            state_ = std::move(candidate);
            return GameWIP::Status::Types::Status{};
        }
        catch(const std::bad_alloc &)
        {
            return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::OutOfMemory);
        }
        catch(...)
        {
            return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::Unknown);
        }
    }

    bool ShellEventQueue::isOpen() const noexcept
    {
        return state_ != nullptr && state_->open;
    }

    bool ShellEventQueue::ownedByCurrentThread() const noexcept
    {
        return state_ != nullptr && Internal::ownedByCurrentThread(*state_);
    }

    GameWIP::Status::Types::Result<void> ShellEventQueue::close() noexcept
    {
        if(state_ == nullptr || !state_->open)
        {
            return GameWIP::Status::Types::Status{};
        }

        if(!ownedByCurrentThread())
        {
            return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::ResourceBusy);
        }

        {
            std::lock_guard lock(state_->mutex);

            // Keep bound resources from outliving their queue.
            if(state_->boundResourceCount != 0)
            {
                return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::ResourceBusy);
            }

            // Clear retained events and release storage before closing the queue.
            Internal::clearEventsUnlocked(*state_);
            state_->eventStorage = {};
            state_->internalEvents.clear();
            state_->open = false;
            state_->ownerThread = {};
        }

        state_.reset();
        return GameWIP::Status::Types::Status{};
    }

    bool ShellEventQueue::popEvent(Types::Shell::Event &outEvent) noexcept
    {
        if(state_ == nullptr || !ownedByCurrentThread())
        {
            return false;
        }
        std::lock_guard lock(state_->mutex);

        if(!state_->open || state_->eventCount == 0)
        {
            return false;
        }

        try
        {
            outEvent = std::move(state_->eventStorage[state_->eventHead]);
            state_->eventStorage[state_->eventHead] = {};
            state_->eventHead = (state_->eventHead + 1) % state_->eventStorage.size();
            --state_->eventCount;
            return true;
        }
        catch(...)
        {
            return false;
        }
    }

    std::size_t ShellEventQueue::popEvents(std::span<Types::Shell::Event> destination) noexcept
    {
        if(state_ == nullptr || destination.empty() || !ownedByCurrentThread())
        {
            return 0;
        }
        std::lock_guard lock(state_->mutex);

        if(!state_->open)
        {
            return 0;
        }
        const std::size_t count = std::min(destination.size(), state_->eventCount);
        std::size_t copied = 0;

        try
        {
            // Drain events in FIFO order until the destination fills or assignment fails.
            for(; copied < count; ++copied)
            {
                destination[copied] = std::move(state_->eventStorage[state_->eventHead]);
                state_->eventStorage[state_->eventHead] = {};
                state_->eventHead = (state_->eventHead + 1) % state_->eventStorage.size();
                --state_->eventCount;
            }
        }
        catch(...)
        {
            return copied;
        }

        return copied;
    }

    void ShellEventQueue::clearEvents() noexcept
    {
        if(state_ == nullptr || !ownedByCurrentThread())
        {
            return;
        }
        std::lock_guard lock(state_->mutex);

        if(state_->open)
        {
            Internal::clearEventsUnlocked(*state_);
        }
    }

    Types::Events::QueueInfo ShellEventQueue::eventQueueInfo() const noexcept
    {
        if(state_ == nullptr || !state_->open || !ownedByCurrentThread())
        {
            return {};
        }
        std::lock_guard lock(state_->mutex);
        return Types::Events::QueueInfo{state_->storageKind, state_->eventStorage.size(), state_->eventCount, state_->droppedEvents};
    }

    void ShellEventQueue::clearDroppedEventCount() noexcept
    {
        if(state_ == nullptr || !ownedByCurrentThread())
        {
            return;
        }
        std::lock_guard lock(state_->mutex);

        if(state_->open)
        {
            state_->droppedEvents = 0;
        }
    }
} // namespace GameWIP::Desktop

namespace GameWIP::Desktop::Shell
{
    /// ------------------------------------------------------------
    /// Capability queries
    /// ------------------------------------------------------------

    GameWIP::Status::Types::Result<Types::Shell::Capabilities> getCapabilities() noexcept
    {
        return Platform::getShellCapabilities();
    }

    bool supports(Types::Shell::Capability capability) noexcept
    {
        const auto result = getCapabilities();
        return result.ok() && result.value().supports(capability);
    }
} // namespace GameWIP::Desktop::Shell
