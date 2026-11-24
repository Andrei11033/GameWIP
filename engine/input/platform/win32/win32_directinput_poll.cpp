/// @file win32_directinput_poll.cpp
/// @brief Win32 DirectInput device polling.

#include "input/platform/win32/internal/win32_attachment_state.h"
#include "input/platform/win32/internal/device_helpers.h"
#include "input/platform/win32/internal/win32_keyboard_mapping.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <new>
#include <span>
#include <type_traits>

namespace GameWIP::Input::Platform::Win32
{
    namespace
    {
        using DirectInputDevice = IDirectInputDevice8W;

        template <typename T> [[nodiscard]] bool readNativeValue(std::span<const std::byte> bytes, std::size_t offset, T &value) noexcept
        {
            static_assert(std::is_trivially_copyable_v<T>);

            if(offset > bytes.size() || sizeof(T) > bytes.size() - offset)
            {
                return false;
            }

            std::array<std::byte, sizeof(T)> valueBytes{};
            std::ranges::copy(bytes.subspan(offset, sizeof(T)), valueBytes.begin());
            value = std::bit_cast<T>(valueBytes);
            return true;
        }

        /// ------------------------------------------------------------
        /// Keyboard mapping and device acquisition
        /// ------------------------------------------------------------

        [[nodiscard]] std::uint32_t keyboardUsageForDirectInputKey(std::uint32_t key) noexcept
        {
            bool extended = false;
            bool pause = false;
            unsigned int scanCode = key;

            switch(key)
            {
            case 0x9C: // Numpad Enter
            case 0x9D: // Right Control
                scanCode = key & 0x7FU;
                extended = true;
                break;
            case 0xB5: // Numpad Divide
            case 0xB7: // Print Screen
            case 0xB8: // Right Alt
            case 0xC7: // Home
            case 0xC8: // Up
            case 0xC9: // Page Up
            case 0xCB: // Left
            case 0xCD: // Right
            case 0xCF: // End
            case 0xD0: // Down
            case 0xD1: // Page Down
            case 0xD2: // Insert
            case 0xD3: // Delete
            case 0xDB: // Left Windows
            case 0xDC: // Right Windows
            case 0xDD: // Application
                scanCode = key & 0x7FU;
                extended = true;
                break;
            case 0xC5: // Pause
                pause = true;
                scanCode = 0x46;
                extended = true;
                break;
            default:
                break;
            }

            return keyboardUsageForScanCode(scanCode, extended, pause);
        }

        [[nodiscard]] bool acquireDevice(DirectInputDevice &device) noexcept
        {
            return SUCCEEDED(device.Acquire());
        }

        [[nodiscard]] bool deviceNeedsReacquisition(HRESULT result) noexcept
        {
            return result == DIERR_INPUTLOST || result == DIERR_NOTACQUIRED;
        }
    } // namespace

    /// ------------------------------------------------------------
    /// Device polling
    /// ------------------------------------------------------------

    IO::Types::Status pollDirectInput(Attachment::Impl &attachment, System::Impl &state) noexcept
    {
        try
        {
            // Poll connected devices and reacquire after focus or device loss.
            for(DeviceBinding &binding : attachment.devices)
            {
                if((binding.role != DeviceRole::DirectInputKeyboard && binding.role != DeviceRole::DirectInputMouse &&
                    binding.role != DeviceRole::DirectInputController) ||
                   !binding.connected || binding.nativeDevice == nullptr)
                {
                    continue;
                }

                auto *device = static_cast<DirectInputDevice *>(binding.nativeDevice);
                HRESULT result = device->Poll();
                if(deviceNeedsReacquisition(result))
                {
                    if(!acquireDevice(*device))
                    {
                        continue;
                    }
                    result = device->Poll();
                }

                if(FAILED(result) && result != DI_NOEFFECT)
                {
                    continue;
                }

                if(binding.role == DeviceRole::DirectInputKeyboard)
                {
                    // Publish the current keyboard state as standard key controls.
                    std::array<BYTE, 256> keys{};
                    result = device->GetDeviceState(static_cast<DWORD>(keys.size()), keys.data());
                    if(FAILED(result))
                    {
                        if(deviceNeedsReacquisition(result))
                        {
                            (void)acquireDevice(*device);
                        }
                        continue;
                    }

                    for(std::size_t key = 0; key < keys.size(); ++key)
                    {
                        const std::uint32_t usage = keyboardUsageForDirectInputKey(static_cast<std::uint32_t>(key));
                        if(usage == 0)
                        {
                            continue;
                        }
                        const bool pressed = (keys[key] & 0x80U) != 0;
                        if(!pressed && !binding.directInputKeyboardState[key])
                        {
                            continue;
                        }
                        binding.directInputKeyboardState[key] = pressed ? 1U : 0U;
                        const Types::StandardId standard{Types::StandardDomain::Keyboard, usage};
                        if(ElementBinding *element = standardElement(binding, standard); element != nullptr)
                        {
                            publish(state, element->id, pressed, false);
                        }
                    }
                    continue;
                }

                if(binding.role == DeviceRole::DirectInputMouse)
                {
                    // Publish standard buttons, relative movement, wheel, and extra buttons.
                    DIMOUSESTATE2 mouse{};
                    result = device->GetDeviceState(sizeof(mouse), &mouse);
                    if(FAILED(result))
                    {
                        if(deviceNeedsReacquisition(result))
                        {
                            (void)acquireDevice(*device);
                        }
                        continue;
                    }

                    constexpr std::array<Types::StandardId, 5> buttons{
                        Standard::Mouse::Left,
                        Standard::Mouse::Right,
                        Standard::Mouse::Middle,
                        Standard::Mouse::X1,
                        Standard::Mouse::X2,
                    };
                    const auto mouseButtons = std::to_array(mouse.rgbButtons);
                    for(std::size_t index = 0; index < buttons.size(); ++index)
                    {
                        if(ElementBinding *element = standardElement(binding, buttons[index]); element != nullptr)
                        {
                            publish(state, element->id, (mouseButtons.at(index) & 0x80U) != 0, false);
                        }
                    }

                    if(ElementBinding *movement = findRole(binding, ElementRole::MouseMovement); movement != nullptr)
                    {
                        publish(state, movement->id, Types::Vector2I64{mouse.lX, mouse.lY}, true);
                    }

                    if(ElementBinding *wheel = findRole(binding, ElementRole::MouseWheel); wheel != nullptr)
                    {
                        publish(state, wheel->id, Types::Vector2F32{static_cast<float>(mouse.lZ), 0.0F}, true);
                    }
                    for(const ElementBinding &element : binding.elements)
                    {
                        if(element.role == ElementRole::NativeButton && element.nativeIndex >= offsetof(DIMOUSESTATE2, rgbButtons) &&
                           element.nativeIndex < offsetof(DIMOUSESTATE2, rgbButtons) + sizeof(mouse.rgbButtons))
                        {
                            const std::size_t index = element.nativeIndex - offsetof(DIMOUSESTATE2, rgbButtons);
                            publish(state, element.id, (mouseButtons.at(index) & 0x80U) != 0, false);
                        }
                    }
                    continue;
                }

                // Publish raw controller axes, buttons, and POV switch positions.
                DIJOYSTATE2 joystick{};
                result = device->GetDeviceState(sizeof(joystick), &joystick);
                if(FAILED(result))
                {
                    if(deviceNeedsReacquisition(result))
                    {
                        (void)acquireDevice(*device);
                    }
                    continue;
                }

                const std::span<const std::byte> reportBytes = std::as_bytes(std::span{&joystick, std::size_t{1}});
                for(const ElementBinding &element : binding.elements)
                {
                    if(element.role == ElementRole::NativeAxis)
                    {
                        LONG value = 0;
                        if(readNativeValue(reportBytes, element.nativeIndex, value))
                        {
                            publish(state, element.id, static_cast<std::int64_t>(value), false);
                        }
                    }
                    else if(element.role == ElementRole::NativeButton)
                    {
                        BYTE value = 0;
                        if(readNativeValue(reportBytes, element.nativeIndex, value))
                        {
                            publish(state, element.id, (value & 0x80U) != 0, false);
                        }
                    }
                    else if(element.role == ElementRole::NativeSwitch)
                    {
                        DWORD value = 0;
                        if(readNativeValue(reportBytes, element.nativeIndex, value))
                        {
                            publish(state, element.id, static_cast<std::uint64_t>(value), false);
                        }
                    }
                }
            }

            return IO::successStatus();
        }
        catch(const std::bad_alloc &)
        {
            ++state.health.totalBackendFailures;
            state.pendingDiscontinuity = true;
            return status(IO::Types::ErrorCode::OutOfMemory);
        }
    }
} // namespace GameWIP::Input::Platform::Win32
