/// @file session_state.h
/// @brief Private state and status helpers shared by Terminal Session modules.

#pragma once

#include "terminal/session.h"
#include "terminal/output.h"
#include "terminal/platform/backend.h"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace GameWIP::Terminal::Internal
{
    /// ------------------------------------------------------------
    /// Input ownership
    /// ------------------------------------------------------------

    struct PreparedInputOwnership
    {
        Types::Input::Capabilities capabilities{};
        Platform::InputModeSnapshot previousMode{};
        bool hasPreviousMode = false;
        bool claimed = false;
        bool directClaimed = false;
    };

    [[nodiscard]] IO::Types::Status invalidArgumentStatus() noexcept;
    [[nodiscard]] IO::Types::Status notOpenStatus() noexcept;
} // namespace GameWIP::Terminal::Internal

namespace GameWIP::Terminal
{
    struct Session::State
    {
        enum class PersistentOutputState : std::uint8_t
        {
            CursorHidden,
            AlternateScreen
        };

        // Lifecycle coordination
        // Operations retain the open binding through a lightweight count without holding this mutex while backend
        // work or user formatters execute. Lifecycle mutation closes the admission gate and waits for the count.
        std::mutex lifecycleMutex;
        std::condition_variable lifecycleCondition;
        bool lifecycleMutation = false;
        std::size_t activeOperations = 0;

        // Input and output synchronization
        std::mutex inputOperationMutex;
        std::mutex persistentOutputMutex;

        // Active session binding
        std::atomic_bool open = false;
        Types::SessionOptions options{};
        Internal::PreparedInputOwnership ownership{};

        // Retain Unicode boundary capacity across line reads.
        std::vector<std::size_t> lineBoundaryStorage;

        // Persistent output restoration
        CursorHiddenScope cursorHiddenScope;
        AlternateScreenScope alternateScreenScope;
        std::array<PersistentOutputState, 2> outputRestoreOrder{};
        std::size_t outputRestoreCount = 0;

        class Operation final
        {
        public:
            explicit Operation(State &requested)
                : previous_(current_)
            {
                bool reentrant = false;

                for(const Operation *operation = current_; operation != nullptr; operation = operation->previous_)
                {
                    if(operation->state_ == &requested)
                    {
                        reentrant = true;
                        break;
                    }
                }

                std::unique_lock lock(requested.lifecycleMutex);

                if(!reentrant)
                {
                    requested.lifecycleCondition.wait(
                        lock,
                        [&requested]
                        {
                            return !requested.lifecycleMutation;
                        });
                }

                if(!requested.open.load(std::memory_order_acquire))
                {
                    return;
                }
                ++requested.activeOperations;
                state_ = &requested;
                current_ = this;
            }

            Operation(const Operation &) = delete;
            Operation &operator=(const Operation &) = delete;

            ~Operation()
            {
                if(state_ == nullptr)
                {
                    return;
                }

                current_ = previous_;
                std::lock_guard lock(state_->lifecycleMutex);
                --state_->activeOperations;

                if(state_->activeOperations == 0)
                {
                    state_->lifecycleCondition.notify_all();
                }
            }

            [[nodiscard]] static bool activeOnCurrentThread(const State &state) noexcept
            {
                for(const Operation *operation = current_; operation != nullptr; operation = operation->previous_)
                {
                    if(operation->state_ == &state)
                    {
                        return true;
                    }
                }

                return false;
            }

        private:
            inline static thread_local Operation *current_ = nullptr;
            State *state_ = nullptr;
            Operation *previous_ = nullptr;
        };

        class Mutation final
        {
        public:
            explicit Mutation(State &state)
                : state_(state)
                , lock_(state.lifecycleMutex)
            {
                state_.lifecycleCondition.wait(
                    lock_,
                    [this]
                    {
                        return !state_.lifecycleMutation;
                    });
                state_.lifecycleMutation = true;
                try
                {
                    state_.lifecycleCondition.wait(
                        lock_,
                        [this]
                        {
                            return state_.activeOperations == 0;
                        });
                }
                catch(...)
                {
                    state_.lifecycleMutation = false;
                    state_.lifecycleCondition.notify_all();
                    throw;
                }
            }

            Mutation(const Mutation &) = delete;
            Mutation &operator=(const Mutation &) = delete;

            ~Mutation()
            {
                state_.lifecycleMutation = false;
                lock_.unlock();
                state_.lifecycleCondition.notify_all();
            }

        private:
            State &state_;
            std::unique_lock<std::mutex> lock_;
        };
    };
} // namespace GameWIP::Terminal
