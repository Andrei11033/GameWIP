/// @file shell_jump_lists.h
/// @brief Portable jump-list and recent-item publication for GameWIP Desktop.

#pragma once

#include "desktop/desktop_export.h"
#include "desktop/shell_types.h"
#include "io/status.h"

#include <span>
#include <string_view>

namespace GameWIP::Desktop::Types::JumpLists
{
    /// @brief One caller-defined jump-list task.
    /// @details The title, description, and launch action are borrowed until publish returns.
    /// Launch actions must not contain target-substitution placeholders.
    struct Task
    {
        std::string_view title;         ///< Nonempty UTF-8 task title.
        std::string_view description;   ///< Optional UTF-8 task description.
        Shell::LaunchActionView action; ///< Structured executable and argument launch action.
    };

    /// @brief One caller-defined jump-list category.
    /// @details The category label and task span are borrowed until publish returns.
    struct Category
    {
        std::string_view label;      ///< Nonempty UTF-8 category label.
        std::span<const Task> tasks; ///< Nonempty category task set.
    };

    /// @brief Complete borrowed jump-list publication snapshot.
    /// @details The snapshot replaces the current application jump-list content. An empty
    /// description clears published tasks, categories, and recent items.
    struct Description
    {
        std::span<const Task> tasks;                    ///< Application-defined tasks.
        std::span<const Category> categories;           ///< Nonempty categories with copied task sets.
        std::span<const Shell::TargetView> recentItems; ///< Filesystem paths or URIs for recent-item publication.
    };
} // namespace GameWIP::Desktop::Types::JumpLists

/// @brief Stateless jump-list and recent-item publication operations.
namespace GameWIP::Desktop::JumpLists
{
    /// @brief Atomically replaces the current application jump-list and recent-item publication.
    /// @details All strings, launch actions, targets, categories, and tasks are copied before the
    /// operation returns. Jump-list launch actions reject target placeholders because no selected
    /// target set exists in this context. The operation does not change the default application
    /// or register file associations. The operation is process-level; concurrent calls are
    /// serialized by the implementation and each successful call replaces the previous snapshot.
    /// @param description Complete borrowed jump-list and recent-item snapshot.
    /// @return Success, or a validation, capability, size, allocation, or native failure. Failure leaves the previous publication unchanged.
    [[nodiscard]] DESKTOP_EXPORT IO::Types::Status publish(const Types::JumpLists::Description &description) noexcept;
} // namespace GameWIP::Desktop::JumpLists
