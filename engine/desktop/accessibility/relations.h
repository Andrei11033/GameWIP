/// @file relations.h
/// @brief Portable relations and extension types.

#pragma once

#include "desktop/accessibility/identity.h"

#include <cstdint>
#include <span>
#include <string_view>

namespace GameWIP::Desktop::Types::Accessibility
{
    /// ------------------------------------------------------------
    /// Relations and extensions
    /// ------------------------------------------------------------

    /// @brief Relationship kinds between semantic nodes.
    enum class RelationKind : std::uint8_t
    {
        LabelledBy,
        DescribedBy,
        ControllerFor,
        ControlledBy,
        FlowsTo,
        FlowsFrom,
        Details,
        ErrorMessage,
        MemberOf,
        RowHeaders,
        ColumnHeaders
    };

    /// @brief One directed relationship from a node to one or more other nodes.
    struct Relation
    {
        RelationKind kind = RelationKind::LabelledBy; ///< At most one relation of each kind per node.
        std::span<const NodeId> targets;              ///< Borrowed non-empty distinct existing-node targets, preserving application order.
    };

    /// @brief Bounded typed value for a namespaced extension property.
    enum class ExtensionValueKind : std::uint8_t
    {
        Boolean,
        SignedInteger,
        UnsignedInteger,
        Double,
        Utf8,
        NodeReference,
        List
    };

    /// @brief Allocation-free view of a supported extension value.
    struct ExtensionValue
    {
        ExtensionValueKind kind = ExtensionValueKind::Utf8; ///< Selects the sole active payload; inactive fields are ignored and not copied.
        bool booleanValue = false;                          ///< Boolean payload.
        std::int64_t signedValue = 0;                       ///< Signed-integer payload.
        std::uint64_t unsignedValue = 0;                    ///< Unsigned-integer payload.
        double doubleValue = 0.0;                           ///< Finite floating-point payload.
        NodeId nodeValue = kInvalidNodeId;                  ///< Existing nonzero semantic node-reference payload.
        std::string_view utf8Value;                         ///< Borrowed strict UTF-8 payload.
        std::span<const ExtensionValue> listValue;          ///< Borrowed bounded recursively typed list payload.

        /// @brief Constructs a boolean extension value.
        [[nodiscard]] static constexpr ExtensionValue boolean(bool value) noexcept
        {
            return {ExtensionValueKind::Boolean, value, {}, {}, {}, kInvalidNodeId, {}, {}};
        }

        /// @brief Constructs a signed-integer extension value.
        [[nodiscard]] static constexpr ExtensionValue signedInteger(std::int64_t value) noexcept
        {
            return {ExtensionValueKind::SignedInteger, {}, value, {}, {}, kInvalidNodeId, {}, {}};
        }

        /// @brief Constructs an unsigned-integer extension value.
        [[nodiscard]] static constexpr ExtensionValue unsignedInteger(std::uint64_t value) noexcept
        {
            return {ExtensionValueKind::UnsignedInteger, {}, {}, value, {}, kInvalidNodeId, {}, {}};
        }

        /// @brief Constructs a finite floating-point extension value.
        [[nodiscard]] static constexpr ExtensionValue real(double value) noexcept
        {
            return {ExtensionValueKind::Double, {}, {}, {}, value, kInvalidNodeId, {}, {}};
        }

        /// @brief Constructs a borrowed UTF-8 extension value.
        [[nodiscard]] static constexpr ExtensionValue utf8(std::string_view value) noexcept
        {
            return {ExtensionValueKind::Utf8, {}, {}, {}, {}, kInvalidNodeId, value, {}};
        }

        /// @brief Constructs a node-reference extension value.
        [[nodiscard]] static constexpr ExtensionValue nodeReference(NodeId value) noexcept
        {
            return {ExtensionValueKind::NodeReference, {}, {}, {}, {}, value, {}, {}};
        }

        /// @brief Constructs a borrowed recursive extension list.
        [[nodiscard]] static constexpr ExtensionValue list(std::span<const ExtensionValue> value) noexcept
        {
            return {ExtensionValueKind::List, {}, {}, {}, {}, kInvalidNodeId, {}, value};
        }
    };

    /// @brief One namespaced, bounded extension property.
    struct Extension
    {
        std::string_view namespaceName; ///< Non-empty strict UTF-8 namespace owned by the application/vendor.
        std::string_view name;          ///< Non-empty strict UTF-8 key; namespace/key pairs are unique per node.
        ExtensionValue value;           ///< Bounded typed portable payload; native backends need not register custom properties.
    };
} // namespace GameWIP::Desktop::Types::Accessibility
