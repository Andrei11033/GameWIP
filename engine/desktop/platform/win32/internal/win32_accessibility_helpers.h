/// @file win32_accessibility_helpers.h
/// @brief Shared UI Automation projection helpers for the Win32 backend.

#pragma once

#include "desktop/platform/win32/internal/win32_accessibility_provider.h"

#include <cstddef>
#include <span>
#include <string_view>

namespace GameWIP::Desktop::Platform
{
    /// ------------------------------------------------------------
    /// UI Automation projection
    /// ------------------------------------------------------------

    // Geometry
    [[nodiscard]] A::Rect intersection(A::Rect a, A::Rect b) noexcept;
    [[nodiscard]] A::Rect transformed(const A::Geometry &geometry) noexcept;

    // Native element mapping
    [[nodiscard]] int controlType(A::Role role) noexcept;
    [[nodiscard]] bool patternSupported(const A::Node &node, PATTERNID id) noexcept;

    // Provider lookup
    [[nodiscard]] HRESULT fragment(const PublishedSnapshot &snapshot, A::NodeId id, IRawElementProviderFragment **out) noexcept;

    // Accessible content
    [[nodiscard]] const A::TextAnnotation *annotationFor(const UiaQuery &query, std::size_t &source, std::size_t &index) noexcept;
    [[nodiscard]] bool annotationSupported(const UiaQuery &query) noexcept;

    // Native value conversion
    [[nodiscard]] HRESULT boolean(VARIANT *out, bool value) noexcept;
    [[nodiscard]] HRESULT integer(VARIANT *out, int value) noexcept;
    [[nodiscard]] HRESULT number(VARIANT *out, double value) noexcept;
    [[nodiscard]] HRESULT string(VARIANT *out, std::u16string_view value) noexcept;

    // Node relationships
    [[nodiscard]] std::span<const A::NodeId> relation(const A::Node &node, A::RelationKind kind) noexcept;
    [[nodiscard]] bool descendant(const PublishedSnapshot &snapshot, const A::Node &node, A::NodeId ancestor) noexcept;
} // namespace GameWIP::Desktop::Platform
