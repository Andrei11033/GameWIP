/// @file shell_helpers.h
/// @brief Shared private helpers for portable shell resources.

#pragma once

#include "desktop/internal/shell_state.h"
#include "desktop/shell.h"

#include <optional>
#include <string_view>
#include <thread>

namespace GameWIP::Desktop::Internal
{
    // Text and value validation

    /// @brief Validates shell text and optionally requires a non-empty value.
    [[nodiscard]] bool validText(std::string_view text, bool required = false) noexcept;

    /// @brief Validates optional progress state before native shell publication.
    [[nodiscard]] IO::Types::Status validateProgress(const std::optional<Types::Shell::Progress> &progress) noexcept;

    /// @brief Validates and deep-copies an icon view into retained shell state.
    [[nodiscard]] IO::Types::Status copyIcon(Types::IconImageView view, OwnedIconImage &out) noexcept;

    // Queue ownership

    /// @brief Binds one shell resource to an owner-thread queue.
    [[nodiscard]] IO::Types::Status bindQueue(ShellEventQueue &queue, std::thread::id owner, ShellEventQueueState *&outQueue) noexcept;

    /// @brief Checks native liveness before enforcing shell-resource owner-thread access.
    [[nodiscard]] IO::Types::Status requireOwnedShellResource(bool live, bool owned) noexcept;

    /// @brief Drops one resource binding and clears its non-owning queue pointer.
    void unbindQueue(ShellEventQueueState *&queue) noexcept;
} // namespace GameWIP::Desktop::Internal
