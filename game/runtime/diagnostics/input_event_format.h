/// @file input_event_format.h
/// @brief Names and formats shared by runtime Input diagnostics.
#pragma once

#include "input/types.h"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>

namespace GameWIP::Game::RuntimeDiagnostics::InputEventFormat
{
    namespace Input = GameWIP::Input;

    /// ------------------------------------------------------------
    /// Control names and values
    /// ------------------------------------------------------------

    [[nodiscard]] std::string controlName(Input::Types::StandardId id);
    [[nodiscard]] std::string controlName(const Input::Types::HidControlId &id);
    [[nodiscard]] std::string value(const Input::Types::Value &value);

    /// ------------------------------------------------------------
    /// Device summaries
    /// ------------------------------------------------------------

    [[nodiscard]] std::size_t standardControlCount(const Input::Types::DeviceInfo &device);
    [[nodiscard]] std::string backendSummary(std::span<const Input::Types::SourceInfo> sources);

    /// ------------------------------------------------------------
    /// Event labels
    /// ------------------------------------------------------------

    [[nodiscard]] std::string_view eventKind(Input::Types::EventKind kind) noexcept;
    [[nodiscard]] std::string_view eventCause(Input::Types::EventCause cause) noexcept;

    /// ------------------------------------------------------------
    /// Device and value labels
    /// ------------------------------------------------------------

    [[nodiscard]] std::string_view valueKind(Input::Types::ValueKind kind) noexcept;
    [[nodiscard]] std::string_view reportingMode(Input::Types::ReportingMode mode) noexcept;
    [[nodiscard]] std::string_view backend(Input::Types::BackendKind kind) noexcept;
    [[nodiscard]] std::string_view deviceOrigin(Input::Types::DeviceOrigin origin) noexcept;
    [[nodiscard]] std::string_view deviceKind(const Input::Types::DeviceInfo &device) noexcept;
    [[nodiscard]] std::string_view identityConfidence(Input::Types::IdentityConfidence confidence) noexcept;
    [[nodiscard]] std::string_view standardDomain(Input::Types::StandardDomain domain) noexcept;
    [[nodiscard]] std::string_view numericStorage(Input::Types::NumericStorage storage) noexcept;
    [[nodiscard]] std::string_view unit(Input::Types::Unit unit) noexcept;
    [[nodiscard]] std::string_view valueProvenance(Input::Types::ValueProvenance provenance) noexcept;
} // namespace GameWIP::Game::RuntimeDiagnostics::InputEventFormat
