#pragma once

#include "input/input.h"

#include <cstdint>
#include <string_view>

namespace GameWIP::Input::Internal
{
    /// @brief Shared bridge that lets platform input backends feed private InputState mutation helpers.
    struct InputStateAccess
    {
        static void setButton(InputState &inputState, InputControl control, bool isDown);
        static void setAxis(InputState &inputState, InputControl control, float value);
        static void addMouseDelta(InputState &inputState, int deltaX, int deltaY);
        static void setMousePosition(InputState &inputState, int x, int y);
        static void clearMousePosition(InputState &inputState);
        static void addWheelDelta(InputState &inputState, InputControl control, float amount);
        static void addTextUtf8(InputState &inputState, std::string_view text);
        static void addTextCodepoint(InputState &inputState, char32_t codepoint);
        static char16_t getPendingTextHighSurrogate(const InputState &inputState);
        static void setPendingTextHighSurrogate(InputState &inputState, char16_t codeUnit);
        static void clearTextComposition(InputState &inputState);
        static void setDeviceConnected(InputState &inputState, InputDeviceType deviceType, DeviceIndex deviceIndex, bool connected);
        static void clearDevice(InputState &inputState, InputDeviceRef device);
        static std::uint64_t getClearGeneration(const InputState &inputState);
    };

    /// @brief Shared bridge that lets platform input backends feed private InputDeviceRegistry mutation helpers.
    struct InputDeviceRegistryAccess
    {
        static InputDeviceRef upsertDevice(InputDeviceRegistry &registry, const InputDeviceInfo &deviceInfo);
        static void setDeviceConnected(InputDeviceRegistry &registry, InputDeviceRef device, bool connected);
        static void setDeviceCanonical(InputDeviceRegistry &registry, InputDeviceRef device, bool canonical);
        static void replaceDeviceControls(InputDeviceRegistry &registry, InputDeviceRef device, std::span<const InputControlInfo> controls);
        static void clearBackend(InputDeviceRegistry &registry, InputDeviceBackend backend);
        static bool shouldFeedDevice(const InputDeviceRegistry &registry, InputDeviceRef device);
        static bool shouldFeedDeviceBackend(const InputDeviceRegistry &registry, InputDeviceRef device, InputDeviceBackend backend);
        static InputDeviceRef mergeDeviceBackend(InputDeviceRegistry &registry, InputDeviceRef device, const InputDeviceInfo &backendInfo);
        static void setDeviceBackendConnected(InputDeviceRegistry &registry, InputDeviceRef device, InputDeviceBackend backend, bool connected);
        static void mergeDeviceControls(InputDeviceRegistry &registry, InputDeviceRef device, std::span<const InputControlInfo> controls);
    };
} // namespace GameWIP::Input::Internal
