/// @file notification_center.cpp
/// @brief Portable NotificationCenter lifecycle and publication.

#include "desktop/shell_notifications.h"

#include "base/checked_arithmetic.h"
#include "desktop/internal/shell_helpers.h"
#include "desktop/internal/shell_state.h"
#include "desktop/platform/shell.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace GameWIP::Desktop::Internal
{
    /// ------------------------------------------------------------
    /// Notification description helpers
    /// ------------------------------------------------------------

    namespace
    {
        using IO::Types::ErrorCode;

        [[nodiscard]] IO::Types::Status error(ErrorCode code) noexcept
        {
            return IO::makeStatus(code);
        }

        /// @brief Validates and deep-copies notification text, actions, inputs, media, and scheduling.
        [[nodiscard]] IO::Types::Status copyNotificationDescription(
            const Types::Notifications::Description &description,
            OwnedNotificationDescription &out) noexcept
        {
            // Validate text and scheduling before allocating retained data.
            if(!validText(description.title, true) || !validText(description.body) || !validText(description.group) || !validText(description.badge))
            {
                return error(ErrorCode::InvalidArgument);
            }

            IO::Types::Status status = validateProgress(description.progress);

            if(!status.ok())
            {
                return status;
            }

            if(description.schedule.has_value() &&
               (description.schedule->occurrenceCount == 0 ||
                (description.schedule->occurrenceCount > 1 && description.schedule->repeatInterval <= std::chrono::milliseconds::zero())))
            {
                return error(ErrorCode::InvalidArgument);
            }

            try
            {
                std::unordered_set<std::uint32_t> actionIds;
                std::unordered_set<std::uint32_t> inputIds;

                // Copy scalar settings and reserve the owned payloads.
                out.title = description.title;
                out.body = description.body;
                out.group = description.group;
                out.badge = description.badge;
                out.sound = description.sound;
                out.urgency = description.urgency;
                out.schedule = description.schedule;
                out.progress = description.progress;
                out.actions.clear();
                out.inputs.clear();
                out.media.clear();
                out.actions.reserve(description.actions.size());
                out.inputs.reserve(description.inputs.size());
                out.media.reserve(description.media.size());

                // Validate and copy actions and inputs.
                for(const Types::Notifications::Action &action : description.actions)
                {
                    if(!action.id.isValid() || !validText(action.label, true) || !actionIds.insert(action.id.value).second)
                    {
                        return error(ErrorCode::InvalidArgument);
                    }
                    out.actions.push_back({action.id, std::string(action.label)});
                }

                for(const Types::Notifications::Input &input : description.inputs)
                {
                    if(!input.id.isValid() || !validText(input.placeholder) || !validText(input.initialValue) ||
                       !inputIds.insert(input.id.value).second)
                    {
                        return error(ErrorCode::InvalidArgument);
                    }
                    out.inputs.push_back({input.id, std::string(input.placeholder), std::string(input.initialValue), input.secure});
                }

                // Validate media and pack images into tightly sized rows.
                for(const Types::Notifications::MediaView &media : description.media)
                {
                    OwnedNotificationMedia owned;

                    if(const auto *image = std::get_if<Types::DataTransfer::ImageView>(&media))
                    {
                        const std::size_t packedRowBytes = static_cast<std::size_t>(image->size.width) * 4U;
                        const std::size_t rowStride = image->rowStrideBytes.value_or(packedRowBytes);

                        if(image->size.width == 0 || image->size.height == 0 || rowStride < packedRowBytes ||
                           GameWIP::Base::wouldMultiplyOverflow(static_cast<std::size_t>(image->size.height), rowStride) ||
                           image->rgba8.size() != static_cast<std::size_t>(image->size.height) * rowStride)
                        {
                            return error(ErrorCode::InvalidArgument);
                        }
                        owned.size = image->size;
                        owned.rgba8.resize(static_cast<std::size_t>(image->size.width) * static_cast<std::size_t>(image->size.height) * 4U);
                        const auto sourceRowBytes = static_cast<std::vector<std::byte>::difference_type>(rowStride);
                        const auto destinationRowBytes = static_cast<std::vector<std::byte>::difference_type>(packedRowBytes);

                        for(std::uint32_t row = 0; row < image->size.height; ++row)
                        {
                            std::copy_n(
                                image->rgba8.begin() + static_cast<std::vector<std::byte>::difference_type>(row) * sourceRowBytes,
                                packedRowBytes,
                                owned.rgba8.begin() + static_cast<std::vector<std::byte>::difference_type>(row) * destinationRowBytes);
                        }
                    }
                    else
                    {
                        const auto &uri = std::get<Types::Notifications::UriMediaView>(media);

                        if(!validText(uri.uri, true))
                        {
                            return error(ErrorCode::InvalidArgument);
                        }
                        owned.uri = true;
                        owned.uriText = uri.uri;
                    }

                    out.media.push_back(std::move(owned));
                }

                return IO::successStatus();
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
    } // namespace
} // namespace GameWIP::Desktop::Internal

namespace GameWIP::Desktop
{
    /// ------------------------------------------------------------
    /// Notification ownership checks
    /// ------------------------------------------------------------

    namespace
    {
        using Internal::NotificationCenterState;
        using IO::Types::ErrorCode;

        /// @brief Builds a portable status from a single error code.
        [[nodiscard]] IO::Types::Status error(ErrorCode code) noexcept
        {
            return IO::makeStatus(code);
        }

        /// @brief Validates an open notification center and its owner-thread access.
        [[nodiscard]] IO::Types::Status requireOwnedNotification(NotificationCenterState *state, bool owned) noexcept
        {
            return Internal::requireOwnedShellResource(state != nullptr && state->open && state->platform != nullptr, owned);
        }
    } // namespace

    /// ------------------------------------------------------------
    /// Notification integration
    /// ------------------------------------------------------------

    NotificationCenter::NotificationCenter() noexcept = default;

    NotificationCenter::~NotificationCenter() noexcept
    {
        if(state_ == nullptr)
        {
            return;
        }

        if(ownedByCurrentThread())
        {
            static_cast<void>(close());
        }
        else
        {
            Platform::closeNotificationCenterBestEffort(*state_);
            Internal::unbindQueue(state_->eventQueue);
            state_.reset();
        }
    }

    IO::Types::Status NotificationCenter::open() noexcept
    {
        if(state_ != nullptr)
        {
            return error(ErrorCode::AlreadyOpen);
        }

        try
        {
            // Open the native center before publishing its state to the wrapper.
            auto candidate = std::make_unique<Internal::NotificationCenterState>();
            candidate->ownerThread = std::this_thread::get_id();
            candidate->open = true;
            const IO::Types::Status status = Platform::openNotificationCenter(*candidate);

            if(!status.ok())
            {
                Platform::closeNotificationCenterBestEffort(*candidate);
                return status;
            }
            state_ = std::move(candidate);
            return IO::successStatus();
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

    IO::Types::Status NotificationCenter::open(ShellEventQueue &eventQueue) noexcept
    {
        if(state_ != nullptr)
        {
            return error(ErrorCode::AlreadyOpen);
        }

        try
        {
            // Bind the event queue before opening the native center.
            auto candidate = std::make_unique<Internal::NotificationCenterState>();
            candidate->ownerThread = std::this_thread::get_id();
            IO::Types::Status status = Internal::bindQueue(eventQueue, candidate->ownerThread, candidate->eventQueue);

            if(!status.ok())
            {
                return status;
            }
            candidate->open = true;
            status = Platform::openNotificationCenter(*candidate);

            if(!status.ok())
            {
                Internal::unbindQueue(candidate->eventQueue);
                Platform::closeNotificationCenterBestEffort(*candidate);
                return status;
            }

            // Publish only after both queue and native setup succeed.
            state_ = std::move(candidate);
            return IO::successStatus();
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

    bool NotificationCenter::isOpen() const noexcept
    {
        return state_ != nullptr && state_->open && state_->platform != nullptr;
    }

    bool NotificationCenter::ownedByCurrentThread() const noexcept
    {
        return state_ != nullptr && state_->ownerThread == std::this_thread::get_id();
    }

    bool NotificationCenter::hasEventQueue() const noexcept
    {
        return state_ != nullptr && state_->eventQueue != nullptr;
    }

    IO::Types::Status NotificationCenter::close() noexcept
    {
        if(state_ == nullptr)
        {
            return IO::successStatus();
        }

        if(!ownedByCurrentThread())
        {
            return error(ErrorCode::ResourceBusy);
        }

        const IO::Types::Status status = Platform::closeNotificationCenter(*state_);

        if(!status.ok())
        {
            return status;
        }

        Internal::unbindQueue(state_->eventQueue);
        state_->notifications.clear();
        state_.reset();
        return IO::successStatus();
    }

    Types::Result<Types::Shell::NotificationId> NotificationCenter::publish(const Types::Notifications::Description &description) noexcept
    {
        Types::Result<Types::Shell::NotificationId> result{error(ErrorCode::NotOpen), std::nullopt};
        IO::Types::Status status = requireOwnedNotification(Internal::NotificationCenterAccess::state(*this), ownedByCurrentThread());

        if(!status.ok())
        {
            result.status = std::move(status);
            return result;
        }

        // Interactive notifications need a queue for their responses.
        if((!description.actions.empty() || !description.inputs.empty()) && state_->eventQueue == nullptr)
        {
            result.status = error(ErrorCode::InvalidArgument);
            return result;
        }

        std::uint64_t publishedId = 0;
        bool nativePublished = false;

        try
        {
            // Validate and own the description before assigning a native ID.
            Internal::OwnedNotificationDescription owned;
            status = Internal::copyNotificationDescription(description, owned);

            if(!status.ok())
            {
                result.status = std::move(status);
                return result;
            }
            std::uint64_t id = state_->nextId++;

            if(id == 0)
            {
                id = state_->nextId++;
            }

            while(id == 0 || state_->notifications.contains(id))
            {
                id = state_->nextId++;

                if(id == 0)
                {
                    id = state_->nextId++;
                }
            }

            // Publish natively, then retain the description for future updates.
            status = Platform::publishNotification(*state_, id, owned);

            if(!status.ok())
            {
                result.status = std::move(status);
                return result;
            }
            publishedId = id;
            nativePublished = true;
            state_->notifications.emplace(id, std::move(owned));
            result.status = IO::successStatus();
            result.value = Types::Shell::NotificationId{id};
            return result;
        }
        catch(const std::bad_alloc &)
        {
            if(nativePublished)
            {
                static_cast<void>(Platform::dismissNotification(*state_, publishedId));
            }

            result.status = error(ErrorCode::OutOfMemory);
            return result;
        }
        catch(...)
        {
            if(nativePublished)
            {
                static_cast<void>(Platform::dismissNotification(*state_, publishedId));
            }

            result.status = error(ErrorCode::Unknown);
            return result;
        }
    }

    IO::Types::Status NotificationCenter::update(Types::Shell::NotificationId id, const Types::Notifications::Description &description) noexcept
    {
        IO::Types::Status status = requireOwnedNotification(Internal::NotificationCenterAccess::state(*this), ownedByCurrentThread());

        if(!status.ok())
        {
            return status;
        }

        if(!id.isValid())
        {
            return error(ErrorCode::InvalidArgument);
        }

        if(!state_->notifications.contains(id.value))
        {
            return error(ErrorCode::NotFound);
        }

        if((!description.actions.empty() || !description.inputs.empty()) && state_->eventQueue == nullptr)
        {
            return error(ErrorCode::InvalidArgument);
        }

        bool nativeUpdated = false;
        std::optional<Internal::OwnedNotificationDescription> previous;

        try
        {
            // Keep the current description available for rollback.
            previous = state_->notifications.at(id.value);
            Internal::OwnedNotificationDescription owned;
            status = Internal::copyNotificationDescription(description, owned);

            if(!status.ok())
            {
                return status;
            }

            // Update the native notification before replacing the retained copy.
            status = Platform::updateNotification(*state_, id.value, owned);

            if(!status.ok())
            {
                return status;
            }
            nativeUpdated = true;
            state_->notifications[id.value] = std::move(owned);
            return IO::successStatus();
        }
        catch(const std::bad_alloc &)
        {
            if(nativeUpdated && previous.has_value())
            {
                static_cast<void>(Platform::updateNotification(*state_, id.value, *previous));
            }

            return error(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            if(nativeUpdated && previous.has_value())
            {
                static_cast<void>(Platform::updateNotification(*state_, id.value, *previous));
            }

            return error(ErrorCode::Unknown);
        }
    }

    IO::Types::Status NotificationCenter::dismiss(Types::Shell::NotificationId id) noexcept
    {
        IO::Types::Status status = requireOwnedNotification(Internal::NotificationCenterAccess::state(*this), ownedByCurrentThread());

        if(!status.ok())
        {
            return status;
        }

        if(!id.isValid())
        {
            return error(ErrorCode::InvalidArgument);
        }

        if(!state_->notifications.contains(id.value))
        {
            return error(ErrorCode::NotFound);
        }

        status = Platform::dismissNotification(*state_, id.value);

        if(status.ok())
        {
            state_->notifications.erase(id.value);
        }

        return status;
    }
} // namespace GameWIP::Desktop
