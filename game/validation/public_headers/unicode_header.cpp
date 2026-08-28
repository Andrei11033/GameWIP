/// @file unicode_header.cpp
/// @brief Unicode public-header self-containment compile check.
///
/// This translation unit intentionally includes only `unicode/unicode.h` first. This proves
/// the installed public header can be parsed without relying on include order from another
/// GameWIP header.

#include "unicode/unicode.h"

#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

namespace Unicode = GameWIP::Unicode;

static_assert(Unicode::Utf8::kMaximumScalarBytes == 4);
static_assert(Unicode::Utf16::kMaximumScalarCodeUnits == 2);
static_assert(Unicode::isScalarValue(U'\0'));
static_assert(!Unicode::isScalarValue(static_cast<char32_t>(0xD800)));
static_assert(Unicode::Utf16::isHighSurrogate(static_cast<char16_t>(0xD800)));
static_assert(Unicode::Utf16::isLowSurrogate(static_cast<char16_t>(0xDC00)));
static_assert(Unicode::Utf16::isSurrogate(static_cast<char16_t>(0xDFFF)));

static_assert(noexcept(Unicode::getStandardVersion()));
static_assert(noexcept(Unicode::Utf8::decodeScalar(std::declval<std::string_view>())));
static_assert(noexcept(Unicode::Utf8::encodeScalar(U'A')));
static_assert(noexcept(Unicode::Utf8::validate(std::declval<std::string_view>())));
static_assert(noexcept(Unicode::Utf8::measureToUtf16(std::declval<std::string_view>())));
static_assert(noexcept(Unicode::Utf8::convertToUtf16(std::declval<std::string_view>(), std::declval<std::span<char16_t>>())));
static_assert(noexcept(Unicode::Utf8::nextCodePointBoundary(std::declval<std::string_view>(), 0)));
static_assert(noexcept(Unicode::Utf8::previousCodePointBoundary(std::declval<std::string_view>(), 0)));
static_assert(noexcept(Unicode::Utf8::nextGraphemeBoundary(std::declval<std::string_view>(), 0)));
static_assert(noexcept(Unicode::Utf8::previousGraphemeBoundary(std::declval<std::string_view>(), 0)));
static_assert(
    noexcept(std::declval<Unicode::Utf8::GraphemeCursor &>().reset(std::declval<std::string_view>(), std::declval<std::span<std::size_t>>())));
static_assert(noexcept(std::declval<Unicode::Utf8::GraphemeCursor &>().clear()));
static_assert(noexcept(std::declval<const Unicode::Utf8::GraphemeCursor &>().ready()));
static_assert(noexcept(std::declval<const Unicode::Utf8::GraphemeCursor &>().byteOffset()));
static_assert(noexcept(std::declval<const Unicode::Utf8::GraphemeCursor &>().boundaryCount()));
static_assert(noexcept(std::declval<Unicode::Utf8::GraphemeCursor &>().seek(0)));
static_assert(noexcept(std::declval<Unicode::Utf8::GraphemeCursor &>().next()));
static_assert(noexcept(std::declval<Unicode::Utf8::GraphemeCursor &>().previous()));
static_assert(noexcept(std::declval<Unicode::Utf8::GraphemeCursor &>().discardAfterCurrent()));
static_assert(noexcept(Unicode::Utf16::decodeScalar(std::declval<std::span<const char16_t>>())));
static_assert(noexcept(Unicode::Utf16::encodeScalar(U'A')));
static_assert(noexcept(Unicode::Utf16::validate(std::declval<std::span<const char16_t>>())));
static_assert(noexcept(Unicode::Utf16::measureToUtf8(std::declval<std::span<const char16_t>>())));
static_assert(noexcept(Unicode::Utf16::convertToUtf8(std::declval<std::span<const char16_t>>(), std::declval<std::span<char>>())));

static_assert(std::is_same_v<decltype(Unicode::getStandardVersion()), Unicode::Types::Version>);
static_assert(std::is_same_v<decltype(Unicode::Utf8::decodeScalar({})), Unicode::Types::Utf8::DecodeResult>);
static_assert(std::is_same_v<decltype(Unicode::Utf16::decodeScalar({})), Unicode::Types::Utf16::DecodeResult>);
