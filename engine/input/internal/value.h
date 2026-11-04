/// @file value.h
/// @brief Private value helpers shared by the portable Input implementation.
#pragma once

#include "input/types.h"

namespace GameWIP::Input::Internal
{
    /// @brief Returns the public kind corresponding to a stored value alternative.
    /// @param value Value to classify.
    /// @return Matching ValueKind.
    [[nodiscard]] Types::ValueKind valueKind(const Types::Value &value) noexcept;

    /// @brief Creates the typed neutral value for a public value kind.
    /// @param kind Value kind to initialize.
    /// @return Neutral value of the requested kind, or monostate for ValueKind::None.
    [[nodiscard]] Types::Value zeroValue(Types::ValueKind kind) noexcept;

    /// @brief Checks whether a value has the requested public kind.
    /// @param value Value to inspect.
    /// @param kind Expected value kind.
    /// @return `true` when the alternative and kind match.
    [[nodiscard]] bool hasValueKind(const Types::Value &value, Types::ValueKind kind) noexcept;

    /// @brief Extracts one numeric component from a scalar or vector value.
    /// @param value Value to inspect.
    /// @param index Component index.
    /// @return Numeric component, or no value when the kind or index is unsupported.
    [[nodiscard]] std::optional<Types::Scalar> component(const Types::Value &value, std::size_t index) noexcept;

    /// @brief Adds one relative value to an existing value when both are compatible.
    /// @param current Accumulated value.
    /// @param delta Relative value to add.
    /// @return `true` when the values were added, otherwise `false`.
    [[nodiscard]] bool addRelative(Types::Value &current, const Types::Value &delta) noexcept;
} // namespace GameWIP::Input::Internal
