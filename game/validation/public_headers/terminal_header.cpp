/// @file terminal_header.cpp
/// @brief Terminal public-header self-containment compile check.
///
/// This translation unit intentionally includes only `terminal/terminal.h` first. This proves
/// the installed public header can be parsed without relying on include order
/// from another GameWIP header.

#include "terminal/terminal.h"

#include <span>
#include <string_view>
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
    static_assert(noexcept(std::declval<const Terminal::Session &>().getInputCapabilities()));
    static_assert(noexcept(std::declval<const Terminal::Session &>().getOutputCapabilities()));
    static_assert(noexcept(std::declval<Terminal::Session &>().prepareOutput()));
    static_assert(noexcept(std::declval<const Terminal::Session &>().getTerminalSize()));
    static_assert(noexcept(std::declval<Terminal::Session &>().writeText(std::declval<std::string_view>())));
    static_assert(noexcept(std::declval<Terminal::Session &>().writeLine(std::declval<std::string_view>())));
    static_assert(noexcept(std::declval<Terminal::Session &>().writeBytes(std::declval<std::span<const std::byte>>())));
    static_assert(noexcept(std::declval<Terminal::Session &>().writeSegments(std::declval<std::span<const Terminal::Types::Output::Segment>>())));
    static_assert(noexcept(std::declval<Terminal::Session &>().template print<int>(std::declval<std::format_string<int>>(), std::declval<int &&>())));
    static_assert(
        noexcept(std::declval<Terminal::Session &>().template println<int>(std::declval<std::format_string<int>>(), std::declval<int &&>())));
    static_assert(noexcept(std::declval<Terminal::Session &>().flush()));
    static_assert(noexcept(std::declval<Terminal::Session &>().resetStyle()));
    static_assert(noexcept(std::declval<Terminal::Session &>().moveCursor(Terminal::Types::Cursor::MoveDirection::Up)));
    static_assert(noexcept(std::declval<Terminal::Session &>().setCursorPosition(Terminal::Types::Cursor::Position{})));
    static_assert(noexcept(std::declval<Terminal::Session &>().getCursorPosition()));
    static_assert(noexcept(std::declval<Terminal::Session &>().saveCursorPosition()));
    static_assert(noexcept(std::declval<Terminal::Session &>().restoreCursorPosition()));
    static_assert(noexcept(std::declval<Terminal::Session &>().setCursorVisible(false)));
    static_assert(noexcept(std::declval<Terminal::Session &>().clear()));
    static_assert(noexcept(std::declval<Terminal::Session &>().scroll(Terminal::Types::Output::ScrollDirection::Up)));
    static_assert(noexcept(std::declval<Terminal::Session &>().enterAlternateScreen()));
    static_assert(noexcept(std::declval<Terminal::Session &>().leaveAlternateScreen()));
    static_assert(noexcept(std::declval<Terminal::Session &>().setTitle(std::declval<std::string_view>())));
    static_assert(noexcept(std::declval<Terminal::Session &>().ringBell()));

    static_assert(std::is_nothrow_default_constructible_v<Terminal::AlternateScreenScope>);
    static_assert(std::is_nothrow_move_constructible_v<Terminal::AlternateScreenScope>);
    static_assert(std::is_nothrow_move_assignable_v<Terminal::AlternateScreenScope>);
    static_assert(std::is_nothrow_destructible_v<Terminal::AlternateScreenScope>);
    static_assert(noexcept(std::declval<Terminal::AlternateScreenScope &>().leave()));
    static_assert(std::is_nothrow_default_constructible_v<Terminal::CursorHiddenScope>);
    static_assert(std::is_nothrow_move_constructible_v<Terminal::CursorHiddenScope>);
    static_assert(std::is_nothrow_move_assignable_v<Terminal::CursorHiddenScope>);
    static_assert(std::is_nothrow_destructible_v<Terminal::CursorHiddenScope>);
    static_assert(noexcept(std::declval<Terminal::CursorHiddenScope &>().restore()));

    static_assert(std::is_nothrow_default_constructible_v<Terminal::OutputBuffer>);
    static_assert(noexcept(std::declval<Terminal::OutputBuffer &>().setLineEnding(Terminal::Types::Output::LineEnding::Lf)));
    static_assert(noexcept(std::declval<Terminal::OutputBuffer &>().reserve(16)));
    static_assert(noexcept(std::declval<Terminal::OutputBuffer &>().appendText(std::declval<std::string_view>())));
    static_assert(noexcept(std::declval<Terminal::OutputBuffer &>().appendLine(std::declval<std::string_view>())));
    static_assert(
        noexcept(std::declval<Terminal::OutputBuffer &>().template print<int>(std::declval<std::format_string<int>>(), std::declval<int &&>())));
    static_assert(
        noexcept(std::declval<Terminal::OutputBuffer &>().template println<int>(std::declval<std::format_string<int>>(), std::declval<int &&>())));
    static_assert(noexcept(std::declval<const Terminal::OutputBuffer &>().writeTo()));
    static_assert(noexcept(std::declval<Terminal::OutputBuffer &>().flushTo()));

    static_assert(noexcept(Terminal::readEvent()));
    static_assert(noexcept(Terminal::readText()));
    static_assert(noexcept(Terminal::readLine()));
    static_assert(noexcept(Terminal::readBytes(std::declval<std::span<std::byte>>())));
} // namespace

static_assert(noexcept(GameWIP::Terminal::writeText(std::string_view{})), "direct writeText is noexcept");
static_assert(noexcept(GameWIP::Terminal::writeLine(std::string_view{})), "direct writeLine is noexcept");
static_assert(noexcept(GameWIP::Terminal::writeBytes(std::span<const std::byte>{})), "direct writeBytes is noexcept");
static_assert(noexcept(GameWIP::Terminal::flush()), "direct flush is noexcept");
