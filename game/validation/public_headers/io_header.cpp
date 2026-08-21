/// @file io_header.cpp
/// @brief IO public-header self-containment compile check.
///
/// This translation unit intentionally includes only `io/io.h` first. This proves
/// the installed public header can be parsed without relying on include order
/// from another GameWIP header.

#include "io/io.h"

#include <cstddef>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

static_assert(noexcept(std::declval<GameWIP::IO::Reader &>().read(std::declval<std::span<std::byte>>())));
static_assert(noexcept(std::declval<GameWIP::IO::Reader &>().close()));
static_assert(noexcept(std::declval<GameWIP::IO::Reader &>().position()));
static_assert(noexcept(std::declval<GameWIP::IO::Reader &>().size()));
static_assert(noexcept(std::declval<GameWIP::IO::Reader &>().seek(0, GameWIP::IO::Types::SeekOrigin::Begin)));
static_assert(noexcept(std::declval<GameWIP::IO::Writer &>().write(std::declval<std::span<const std::byte>>())));
static_assert(noexcept(std::declval<GameWIP::IO::Writer &>().flush()));
static_assert(noexcept(std::declval<GameWIP::IO::Writer &>().close()));
static_assert(noexcept(std::declval<GameWIP::IO::Writer &>().position()));
static_assert(noexcept(std::declval<GameWIP::IO::Writer &>().seek(0, GameWIP::IO::Types::SeekOrigin::Begin)));
static_assert(noexcept(GameWIP::IO::readAllBytes(std::declval<GameWIP::IO::Reader &>())));
static_assert(noexcept(GameWIP::IO::readAllText(std::declval<GameWIP::IO::Reader &>())));
static_assert(noexcept(GameWIP::IO::writeAllBytes(std::declval<GameWIP::IO::Writer &>(), std::declval<std::span<const std::byte>>())));
static_assert(noexcept(GameWIP::IO::writeAllText(std::declval<GameWIP::IO::Writer &>(), std::declval<std::string_view>())));
static_assert(noexcept(std::declval<GameWIP::IO::MemoryWriter &>().reserve(0)));
static_assert(noexcept(std::declval<const GameWIP::IO::MemoryWriter &>().copyText()));
static_assert(std::is_same_v<decltype(std::declval<const GameWIP::IO::MemoryWriter &>().copyText()), GameWIP::IO::Types::CopyTextResult>);
static_assert(std::is_nothrow_move_constructible_v<GameWIP::IO::Types::Status>);
static_assert(std::is_nothrow_move_assignable_v<GameWIP::IO::Types::Status>);
