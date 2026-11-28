/// @file input_control_helpers.h
/// @brief Private metadata helpers shared by Debug and Trace Input reports.

#pragma once

#include "input/system.h"
#include "input/types.h"
#include "logger/logging.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace GameWIP::Game::RuntimeDiagnostics::InputControlEvents::Internal
{
    namespace Logger = GameWIP::Logger;
    namespace Types = GameWIP::Input::Types;

    // Control lookup and classification

    [[nodiscard]] const Types::DeviceInfo *findDevice(const GameWIP::Input::System &input, Types::DeviceId id);
    [[nodiscard]] const Types::ElementInfo *findElement(const Types::DeviceInfo *device, Types::ElementId id);
    [[nodiscard]] std::string controlName(const Types::ElementInfo *element, Types::ElementId id);
    [[nodiscard]] std::string_view controlCategory(const Types::ElementInfo *element);
    [[nodiscard]] bool isEventDrivenDiscreteControl(const Types::ElementInfo *element) noexcept;
    [[nodiscard]] bool isDerivedGamepadAxis(const Types::ElementInfo *element) noexcept;
    [[nodiscard]] bool isPointerPosition(const Types::ElementInfo *element) noexcept;

    // Structured event metadata

    [[nodiscard]] Logger::Record eventRecord(const Types::Event &event, std::uint64_t captureSequence);
    void addDeviceFields(Logger::Record &record, const Types::DeviceInfo *device, Types::DeviceId id);
} // namespace GameWIP::Game::RuntimeDiagnostics::InputControlEvents::Internal
