/// @file terminal_header.cpp
/// @brief Terminal public-header self-containment compile check.
///
/// This translation unit intentionally includes only `terminal/terminal.h` first. This proves
/// the installed public header can be parsed without relying on include order
/// from another GameWIP header.

#include "terminal/terminal.h"

#include <span>
#include <type_traits>
#include <utility>

namespace
{
    namespace Terminal = GameWIP::Terminal;

    static_assert(std::is_move_constructible_v<Terminal::Session>);
    static_assert(!std::is_move_assignable_v<Terminal::Session>);
    static_assert(!std::is_copy_constructible_v<Terminal::Session>);
    static_assert(!std::is_copy_assignable_v<Terminal::Session>);

    static_assert(noexcept(std::declval<const Terminal::Session &>().isOpen()));
    static_assert(noexcept(std::declval<Terminal::Session &>().open()));
    static_assert(noexcept(std::declval<Terminal::Session &>().close()));
    static_assert(noexcept(std::declval<Terminal::Session &>().readEvent()));
    static_assert(noexcept(std::declval<Terminal::Session &>().readText()));
    static_assert(noexcept(std::declval<Terminal::Session &>().readLine()));
    static_assert(noexcept(std::declval<Terminal::Session &>().readBytes(std::declval<std::span<std::byte>>())));
    static_assert(noexcept(Terminal::readEvent()));
    static_assert(noexcept(Terminal::readText()));
    static_assert(noexcept(Terminal::readLine()));
    static_assert(noexcept(Terminal::readBytes(std::declval<std::span<std::byte>>())));
} // namespace
