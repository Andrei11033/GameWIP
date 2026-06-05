#include "input/input.h"
#include "input/internal/input_state_access.h"

#include <algorithm>
#include <array>
#include <limits>

namespace GameWIP::Input
{
    namespace
    {
        // Control helpers

        /// @brief Returns true when a control is a button.
        /// @param control Control to test.
        /// @return True if the control is a button.
        bool isButtonControl(InputControl control)
        {
            return control.controlType == InputControlType::Button;
        }

        /// @brief Returns true when a control is an axis.
        /// @param control Control to test.
        /// @return True if the control is an axis.
        bool isAxisControl(InputControl control)
        {
            return control.controlType == InputControlType::Axis;
        }

        /// @brief Returns true when a control is a wheel.
        /// @param control Control to test.
        /// @return True if the control is a wheel.
        bool isWheelControl(InputControl control)
        {
            return control.controlType == InputControlType::Wheel;
        }

        /// @brief Finds where a control belongs in a sorted compact control list.
        auto findControl(std::vector<InputControl> &controls, InputControl control)
        {
            return std::lower_bound(controls.begin(), controls.end(), control);
        }

        /// @brief Finds where a control belongs in a sorted compact control list.
        auto findControl(const std::vector<InputControl> &controls, InputControl control)
        {
            return std::lower_bound(controls.begin(), controls.end(), control);
        }

        /// @brief Returns whether a lower-bound result points at a matching control.
        template <typename Iterator> bool isMatchingControl(Iterator entry, Iterator end, InputControl control)
        {
            return entry != end && *entry == control;
        }

        /// @brief Returns whether a control is stored in a sorted compact control list.
        /// @param controls Control list to search.
        /// @param control Control to search for.
        /// @return True if the control exists in the list.
        bool containsControl(const std::vector<InputControl> &controls, InputControl control)
        {
            auto entry = findControl(controls, control);
            return isMatchingControl(entry, controls.end(), control);
        }

        /// @brief Adds a control to a sorted compact list if it is not already present.
        /// @param controls Control list to update.
        /// @param control Control to add.
        void addUniqueControl(std::vector<InputControl> &controls, InputControl control)
        {
            auto entry = findControl(controls, control);
            if (!isMatchingControl(entry, controls.end(), control))
            {
                controls.insert(entry, control);
            }
        }

        /// @brief Removes a control from a sorted compact list.
        /// @param controls Control list to update.
        /// @param control Control to remove.
        void removeControl(std::vector<InputControl> &controls, InputControl control)
        {
            auto entry = findControl(controls, control);
            if (isMatchingControl(entry, controls.end(), control))
            {
                controls.erase(entry);
            }
        }

        /// @brief Finds where a control/value pair belongs in a sorted compact value list.
        /// @param values Control/value list to search.
        /// @param control Control to search for.
        /// @return Iterator to the matching entry, or values.end().
        auto findControlValue(std::vector<std::pair<InputControl, float>> &values, InputControl control)
        {
            return std::lower_bound(
                values.begin(),
                values.end(),
                control,
                [](const std::pair<InputControl, float> &entry, InputControl target)
                {
                    return entry.first < target;
                });
        }

        /// @brief Finds where a control/value pair belongs in a sorted compact value list.
        /// @param values Control/value list to search.
        /// @param control Control to search for.
        /// @return Iterator to the matching entry, or values.end().
        auto findControlValue(const std::vector<std::pair<InputControl, float>> &values, InputControl control)
        {
            return std::lower_bound(
                values.begin(),
                values.end(),
                control,
                [](const std::pair<InputControl, float> &entry, InputControl target)
                {
                    return entry.first < target;
                });
        }

        /// @brief Returns whether a lower-bound result points at a matching control/value pair.
        template <typename Iterator> bool isMatchingControlValue(Iterator entry, Iterator end, InputControl control)
        {
            return entry != end && entry->first == control;
        }

        /// @brief Stores or removes a value in a sorted compact value list.
        /// @param values Control/value list to update.
        /// @param control Control to update.
        /// @param value Value to store, or zero to remove.
        void setControlValue(std::vector<std::pair<InputControl, float>> &values, InputControl control, float value)
        {
            auto entry = findControlValue(values, control);
            if (value == 0.0f)
            {
                if (isMatchingControlValue(entry, values.end(), control))
                {
                    values.erase(entry);
                }
                return;
            }

            if (isMatchingControlValue(entry, values.end(), control))
            {
                entry->second = value;
            }
            else
            {
                values.insert(entry, {control, value});
            }
        }

        /// @brief Returns a stored control value, or zero when absent.
        /// @param values Control/value list to search.
        /// @param control Control to search for.
        /// @return Stored value, or 0 if the control is absent.
        float getControlValue(const std::vector<std::pair<InputControl, float>> &values, InputControl control)
        {
            auto entry = findControlValue(values, control);
            return isMatchingControlValue(entry, values.end(), control) ? entry->second : 0.0f;
        }

        auto findDeviceEntry(std::vector<InputDeviceInfo> &devices, InputDeviceRef device)
        {
            return std::lower_bound(
                devices.begin(),
                devices.end(),
                device,
                [](const InputDeviceInfo &entry, InputDeviceRef target)
                {
                    return entry.device < target;
                });
        }

        auto findDeviceEntry(const std::vector<InputDeviceInfo> &devices, InputDeviceRef device)
        {
            return std::lower_bound(
                devices.begin(),
                devices.end(),
                device,
                [](const InputDeviceInfo &entry, InputDeviceRef target)
                {
                    return entry.device < target;
                });
        }

        bool isMatchingDevice(
            std::vector<InputDeviceInfo>::const_iterator entry,
            std::vector<InputDeviceInfo>::const_iterator end,
            InputDeviceRef device)
        {
            return entry != end && entry->device == device;
        }

        bool isMatchingDevice(std::vector<InputDeviceInfo>::iterator entry, std::vector<InputDeviceInfo>::iterator end, InputDeviceRef device)
        {
            return entry != end && entry->device == device;
        }

        bool hasDeviceIndex(const std::vector<InputDeviceInfo> &devices, InputDeviceType deviceType, DeviceIndex deviceIndex)
        {
            return std::any_of(
                devices.begin(),
                devices.end(),
                [deviceType, deviceIndex](const InputDeviceInfo &device)
                {
                    return device.device.deviceType == deviceType && device.device.deviceIndex == deviceIndex;
                });
        }

        bool isSameNativeIdentity(const InputDeviceInfo &left, const InputDeviceInfo &right)
        {
            return !left.nativeIdentity.empty() && left.nativeIdentityHash != 0 && left.nativeIdentityHash == right.nativeIdentityHash;
        }

        bool hasBackendFeed(const InputDeviceInfo &device, InputDeviceBackend backend)
        {
            switch (backend)
            {
            case InputDeviceBackend::BuiltIn:
                return device.hasBuiltInFeed;
            case InputDeviceBackend::XInput:
                return device.hasXInputFeed;
            case InputDeviceBackend::RawInputHID:
                return device.hasHidFeed;
            }

            return false;
        }

        InputDeviceBackend choosePrimaryBackend(const InputDeviceInfo &device)
        {
            if (device.hasBuiltInFeed)
            {
                return InputDeviceBackend::BuiltIn;
            }

            if (device.hasXInputFeed)
            {
                return InputDeviceBackend::XInput;
            }

            if (device.hasHidFeed)
            {
                return InputDeviceBackend::RawInputHID;
            }

            return device.primaryBackend;
        }

        std::string makeBackendName(const InputDeviceInfo &device)
        {
            std::string name;
            if (device.hasBuiltInFeed)
            {
                name += "BuiltIn";
            }

            if (device.hasXInputFeed)
            {
                if (!name.empty())
                {
                    name += "+";
                }
                name += "XInput";
            }

            if (device.hasHidFeed)
            {
                if (!name.empty())
                {
                    name += "+";
                }
                name += "RawInputHID";
            }

            return name.empty() ? "None" : name;
        }

        void refreshDeviceSummary(InputDeviceInfo &device)
        {
            device.connected = device.hasBuiltInFeed || device.hasXInputFeed || device.hasHidFeed;
            device.primaryBackend = choosePrimaryBackend(device);
            device.backend = device.primaryBackend;
            device.backendName = makeBackendName(device);

            if (device.primaryBackend == InputDeviceBackend::XInput && !device.xInputNativeIdentity.empty())
            {
                device.nativeIdentity = device.xInputNativeIdentity;
                device.nativeIdentityHash = device.xInputNativeIdentityHash;
            }
            else if (device.primaryBackend == InputDeviceBackend::RawInputHID && !device.hidNativeIdentity.empty())
            {
                device.nativeIdentity = device.hidNativeIdentity;
                device.nativeIdentityHash = device.hidNativeIdentityHash;
            }
        }

        void attachBackendInfo(InputDeviceInfo &target, const InputDeviceInfo &source)
        {
            target.deviceType = source.deviceType;
            target.canonical = source.canonical;

            if (!source.displayName.empty() && (target.displayName.empty() || source.backend == InputDeviceBackend::RawInputHID))
            {
                target.displayName = source.displayName;
            }

            if (target.vendorId == 0 || source.backend == InputDeviceBackend::RawInputHID)
            {
                target.vendorId = source.vendorId;
            }

            if (target.productId == 0 || source.backend == InputDeviceBackend::RawInputHID)
            {
                target.productId = source.productId;
            }

            switch (source.backend)
            {
            case InputDeviceBackend::BuiltIn:
                target.hasBuiltInFeed = source.connected || source.hasBuiltInFeed;
                break;
            case InputDeviceBackend::XInput:
                target.hasXInputFeed = source.connected || source.hasXInputFeed;
                target.xInputNativeIdentity = !source.xInputNativeIdentity.empty() ? source.xInputNativeIdentity : source.nativeIdentity;
                target.xInputNativeIdentityHash = source.xInputNativeIdentityHash != 0 ? source.xInputNativeIdentityHash : source.nativeIdentityHash;
                break;
            case InputDeviceBackend::RawInputHID:
                target.hasHidFeed = source.connected || source.hasHidFeed;
                target.hidNativeIdentity = !source.hidNativeIdentity.empty() ? source.hidNativeIdentity : source.nativeIdentity;
                target.hidNativeIdentityHash = source.hidNativeIdentityHash != 0 ? source.hidNativeIdentityHash : source.nativeIdentityHash;
                break;
            }

            if (!source.suppressionReason.empty())
            {
                target.suppressionReason = source.suppressionReason;
            }

            refreshDeviceSummary(target);
        }

        void addUniqueControlInfo(std::vector<InputControlInfo> &controls, const InputControlInfo &control)
        {
            auto entry = std::find_if(
                controls.begin(),
                controls.end(),
                [&control](const InputControlInfo &candidate)
                {
                    return candidate.control == control.control;
                });

            if (entry == controls.end())
            {
                controls.push_back(control);
            }
            else if (!control.displayName.empty())
            {
                *entry = control;
            }
        }

        void mergeControlInfo(std::vector<InputControlInfo> &target, std::span<const InputControlInfo> source)
        {
            for (const InputControlInfo &control : source)
            {
                addUniqueControlInfo(target, control);
            }
        }

        void remapControls(std::vector<InputControlInfo> &controls, InputDeviceRef from, InputDeviceRef to)
        {
            for (InputControlInfo &control : controls)
            {
                if (control.control.deviceType == from.deviceType && control.control.deviceIndex == from.deviceIndex)
                {
                    control.control.deviceType = to.deviceType;
                    control.control.deviceIndex = to.deviceIndex;
                }
            }
        }

        InputControlInfo makeButtonControlInfo(InputControl control, const char *name)
        {
            return InputControlInfo{.control = control, .displayName = name, .minimumValue = 0.0f, .maximumValue = 1.0f, .relative = false};
        }

        InputControlInfo makeAxisControlInfo(InputControl control, const char *name, bool relative)
        {
            return InputControlInfo{
                .control = control,
                .displayName = name,
                .minimumValue = relative ? -1.0f : 0.0f,
                .maximumValue = 1.0f,
                .relative = relative};
        }

        std::vector<InputControlInfo> makeKeyboardControls()
        {
            constexpr std::array<std::pair<ControlCode, const char *>, 55> keys{
                {{KeyboardControlCode::A, "A"},
                 {KeyboardControlCode::B, "B"},
                 {KeyboardControlCode::C, "C"},
                 {KeyboardControlCode::D, "D"},
                 {KeyboardControlCode::E, "E"},
                 {KeyboardControlCode::F, "F"},
                 {KeyboardControlCode::G, "G"},
                 {KeyboardControlCode::H, "H"},
                 {KeyboardControlCode::I, "I"},
                 {KeyboardControlCode::J, "J"},
                 {KeyboardControlCode::K, "K"},
                 {KeyboardControlCode::L, "L"},
                 {KeyboardControlCode::M, "M"},
                 {KeyboardControlCode::N, "N"},
                 {KeyboardControlCode::O, "O"},
                 {KeyboardControlCode::P, "P"},
                 {KeyboardControlCode::Q, "Q"},
                 {KeyboardControlCode::R, "R"},
                 {KeyboardControlCode::S, "S"},
                 {KeyboardControlCode::T, "T"},
                 {KeyboardControlCode::U, "U"},
                 {KeyboardControlCode::V, "V"},
                 {KeyboardControlCode::W, "W"},
                 {KeyboardControlCode::X, "X"},
                 {KeyboardControlCode::Y, "Y"},
                 {KeyboardControlCode::Z, "Z"},
                 {KeyboardControlCode::Digit0, "0"},
                 {KeyboardControlCode::Digit1, "1"},
                 {KeyboardControlCode::Digit2, "2"},
                 {KeyboardControlCode::Digit3, "3"},
                 {KeyboardControlCode::Digit4, "4"},
                 {KeyboardControlCode::Digit5, "5"},
                 {KeyboardControlCode::Digit6, "6"},
                 {KeyboardControlCode::Digit7, "7"},
                 {KeyboardControlCode::Digit8, "8"},
                 {KeyboardControlCode::Digit9, "9"},
                 {KeyboardControlCode::Escape, "Escape"},
                 {KeyboardControlCode::Enter, "Enter"},
                 {KeyboardControlCode::Space, "Space"},
                 {KeyboardControlCode::Tab, "Tab"},
                 {KeyboardControlCode::Backspace, "Backspace"},
                 {KeyboardControlCode::LeftArrow, "Left Arrow"},
                 {KeyboardControlCode::RightArrow, "Right Arrow"},
                 {KeyboardControlCode::UpArrow, "Up Arrow"},
                 {KeyboardControlCode::DownArrow, "Down Arrow"},
                 {KeyboardControlCode::LeftControl, "Left Ctrl"},
                 {KeyboardControlCode::RightControl, "Right Ctrl"},
                 {KeyboardControlCode::LeftShift, "Left Shift"},
                 {KeyboardControlCode::RightShift, "Right Shift"},
                 {KeyboardControlCode::LeftAlt, "Left Alt"},
                 {KeyboardControlCode::RightAlt, "Right Alt"},
                 {KeyboardControlCode::LeftSuper, "Left Super"},
                 {KeyboardControlCode::RightSuper, "Right Super"},
                 {KeyboardControlCode::F1, "F1"},
                 {KeyboardControlCode::F2, "F2"}}};

            std::vector<InputControlInfo> controls;
            controls.reserve(keys.size());
            for (const auto &[code, name] : keys)
            {
                controls.push_back(makeButtonControlInfo(makeKeyboardKey(code), name));
            }

            return controls;
        }

        std::vector<InputControlInfo> makeMouseControls()
        {
            std::vector<InputControlInfo> controls;
            controls.reserve(9);
            controls.push_back(makeButtonControlInfo(makeMouseButton(MouseButton::Left), "Left Button"));
            controls.push_back(makeButtonControlInfo(makeMouseButton(MouseButton::Right), "Right Button"));
            controls.push_back(makeButtonControlInfo(makeMouseButton(MouseButton::Middle), "Middle Button"));
            controls.push_back(makeButtonControlInfo(makeMouseButton(MouseButton::X1), "X1 Button"));
            controls.push_back(makeButtonControlInfo(makeMouseButton(MouseButton::X2), "X2 Button"));
            controls.push_back(makeAxisControlInfo(makeMouseAxis(MouseAxis::DeltaX), "Delta X", true));
            controls.push_back(makeAxisControlInfo(makeMouseAxis(MouseAxis::DeltaY), "Delta Y", true));
            controls.push_back(
                InputControlInfo{
                    .control = makeMouseWheel(MouseWheel::Vertical),
                    .displayName = "Vertical Wheel",
                    .minimumValue = -1.0f,
                    .maximumValue = 1.0f,
                    .relative = true});
            controls.push_back(
                InputControlInfo{
                    .control = makeMouseWheel(MouseWheel::Horizontal),
                    .displayName = "Horizontal Wheel",
                    .minimumValue = -1.0f,
                    .maximumValue = 1.0f,
                    .relative = true});
            return controls;
        }

        // Text helpers

        /// @brief Appends one Unicode codepoint as UTF-8.
        /// @param text UTF-8 text buffer to update.
        /// @param codepoint Unicode codepoint to append.
        void appendUtf8Codepoint(std::string &text, char32_t codepoint)
        {
            if (codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF))
            {
                codepoint = 0xFFFD;
            }

            if (codepoint <= 0x7F)
            {
                text.push_back(static_cast<char>(codepoint));
                return;
            }

            if (codepoint <= 0x7FF)
            {
                text.push_back(static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F)));
                text.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
                return;
            }

            if (codepoint <= 0xFFFF)
            {
                text.push_back(static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F)));
                text.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
                text.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
                return;
            }

            if (codepoint <= 0x10FFFF)
            {
                text.push_back(static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07)));
                text.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
                text.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
                text.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
            }
        }
    } // namespace

    InputDeviceRegistry::InputDeviceRegistry()
    {
        devices.reserve(8);
        addBuiltInDevices();
    }

    std::span<const InputDeviceInfo> InputDeviceRegistry::getDevices() const
    {
        return devices;
    }

    const InputDeviceInfo *InputDeviceRegistry::findDevice(InputDeviceRef device) const
    {
        auto entry = findDeviceEntry(devices, device);
        return isMatchingDevice(entry, devices.end(), device) ? &(*entry) : nullptr;
    }

    const InputControlInfo *InputDeviceRegistry::findControl(InputControl control) const
    {
        const InputDeviceInfo *device = findDevice(InputDeviceRef{control.deviceType, control.deviceIndex});
        if (device == nullptr)
        {
            return nullptr;
        }

        auto controlEntry = std::find_if(
            device->controls.begin(),
            device->controls.end(),
            [control](const InputControlInfo &controlInfo)
            {
                return controlInfo.control == control;
            });

        return controlEntry != device->controls.end() ? &(*controlEntry) : nullptr;
    }

    bool InputDeviceRegistry::shouldFeedDevice(InputDeviceRef device) const
    {
        const InputDeviceInfo *deviceInfo = findDevice(device);
        return deviceInfo != nullptr && deviceInfo->connected && deviceInfo->canonical;
    }

    bool InputDeviceRegistry::shouldFeedDeviceBackend(InputDeviceRef device, InputDeviceBackend backend) const
    {
        const InputDeviceInfo *deviceInfo = findDevice(device);
        return deviceInfo != nullptr && deviceInfo->connected && deviceInfo->canonical && hasBackendFeed(*deviceInfo, backend) &&
               deviceInfo->primaryBackend == backend;
    }

    bool InputDeviceRegistry::hasConnectedNativeGamepad() const
    {
        return std::any_of(
            devices.begin(),
            devices.end(),
            [](const InputDeviceInfo &device)
            {
                return device.connected && device.canonical && device.deviceType == InputDeviceType::Gamepad && device.hasHidFeed;
            });
    }

    void InputDeviceRegistry::addBuiltInDevices()
    {
        InputDeviceInfo keyboard{};
        keyboard.device = InputDeviceRef{InputDeviceType::Keyboard, 0};
        keyboard.backend = InputDeviceBackend::BuiltIn;
        keyboard.primaryBackend = InputDeviceBackend::BuiltIn;
        keyboard.deviceType = InputDeviceType::Keyboard;
        keyboard.displayName = "Keyboard";
        keyboard.backendName = "BuiltIn";
        keyboard.nativeIdentity = "builtin:keyboard";
        keyboard.nativeIdentityHash = 1;
        keyboard.connected = true;
        keyboard.canonical = true;
        keyboard.hasBuiltInFeed = true;
        keyboard.controls = makeKeyboardControls();
        upsertDevice(keyboard);

        InputDeviceInfo mouse{};
        mouse.device = InputDeviceRef{InputDeviceType::Mouse, 0};
        mouse.backend = InputDeviceBackend::BuiltIn;
        mouse.primaryBackend = InputDeviceBackend::BuiltIn;
        mouse.deviceType = InputDeviceType::Mouse;
        mouse.displayName = "Mouse";
        mouse.backendName = "BuiltIn";
        mouse.nativeIdentity = "builtin:mouse";
        mouse.nativeIdentityHash = 2;
        mouse.connected = true;
        mouse.canonical = true;
        mouse.hasBuiltInFeed = true;
        mouse.controls = makeMouseControls();
        upsertDevice(mouse);
    }

    InputDeviceRef InputDeviceRegistry::upsertDevice(const InputDeviceInfo &deviceInfo)
    {
        InputDeviceInfo nextDevice = deviceInfo;
        const InputDeviceRef requestedDevice = nextDevice.device;
        if (nextDevice.device.deviceType != nextDevice.deviceType)
        {
            nextDevice.device.deviceType = nextDevice.deviceType;
        }
        attachBackendInfo(nextDevice, deviceInfo);
        mergeControlInfo(nextDevice.controls, deviceInfo.controls);

        auto nativeEntry = std::find_if(
            devices.begin(),
            devices.end(),
            [&nextDevice](const InputDeviceInfo &candidate)
            {
                return isSameNativeIdentity(candidate, nextDevice);
            });

        if (nativeEntry != devices.end())
        {
            remapControls(nextDevice.controls, requestedDevice, nativeEntry->device);
            attachBackendInfo(*nativeEntry, nextDevice);
            mergeControlInfo(nativeEntry->controls, nextDevice.controls);
            const InputDeviceRef device = nativeEntry->device;
            std::sort(
                devices.begin(),
                devices.end(),
                [](const InputDeviceInfo &left, const InputDeviceInfo &right)
                {
                    return left.device < right.device;
                });
            return device;
        }

        if (hasDeviceIndex(devices, nextDevice.device.deviceType, nextDevice.device.deviceIndex))
        {
            nextDevice.device.deviceIndex = allocateDeviceIndex(nextDevice.device.deviceType);
            remapControls(nextDevice.controls, requestedDevice, nextDevice.device);
        }

        auto deviceEntry = findDeviceEntry(devices, nextDevice.device);
        if (isMatchingDevice(deviceEntry, devices.end(), nextDevice.device))
        {
            *deviceEntry = nextDevice;
        }
        else
        {
            devices.insert(deviceEntry, nextDevice);
        }

        return nextDevice.device;
    }

    InputDeviceRef InputDeviceRegistry::mergeDeviceBackend(InputDeviceRef device, const InputDeviceInfo &backendInfo)
    {
        auto entry = findDeviceEntry(devices, device);
        if (!isMatchingDevice(entry, devices.end(), device))
        {
            return upsertDevice(backendInfo);
        }

        attachBackendInfo(*entry, backendInfo);
        mergeControlInfo(entry->controls, backendInfo.controls);
        return entry->device;
    }

    void InputDeviceRegistry::setDeviceConnected(InputDeviceRef device, bool connected)
    {
        auto entry = findDeviceEntry(devices, device);
        if (isMatchingDevice(entry, devices.end(), device))
        {
            entry->connected = connected;
            if (!connected)
            {
                entry->hasXInputFeed = false;
                entry->hasHidFeed = false;
            }
            refreshDeviceSummary(*entry);
        }
    }

    void InputDeviceRegistry::setDeviceBackendConnected(InputDeviceRef device, InputDeviceBackend backend, bool connected)
    {
        auto entry = findDeviceEntry(devices, device);
        if (isMatchingDevice(entry, devices.end(), device))
        {
            switch (backend)
            {
            case InputDeviceBackend::BuiltIn:
                entry->hasBuiltInFeed = connected;
                break;
            case InputDeviceBackend::XInput:
                entry->hasXInputFeed = connected;
                break;
            case InputDeviceBackend::RawInputHID:
                entry->hasHidFeed = connected;
                break;
            }

            refreshDeviceSummary(*entry);
        }
    }

    void InputDeviceRegistry::setDeviceCanonical(InputDeviceRef device, bool canonical)
    {
        auto entry = findDeviceEntry(devices, device);
        if (isMatchingDevice(entry, devices.end(), device))
        {
            entry->canonical = canonical;
        }
    }

    void InputDeviceRegistry::replaceDeviceControls(InputDeviceRef device, std::span<const InputControlInfo> controls)
    {
        auto entry = findDeviceEntry(devices, device);
        if (isMatchingDevice(entry, devices.end(), device))
        {
            entry->controls.assign(controls.begin(), controls.end());
        }
    }

    void InputDeviceRegistry::mergeDeviceControls(InputDeviceRef device, std::span<const InputControlInfo> controls)
    {
        auto entry = findDeviceEntry(devices, device);
        if (isMatchingDevice(entry, devices.end(), device))
        {
            mergeControlInfo(entry->controls, controls);
        }
    }

    void InputDeviceRegistry::clearBackend(InputDeviceBackend backend)
    {
        if (backend == InputDeviceBackend::BuiltIn)
        {
            return;
        }

        for (InputDeviceInfo &device : devices)
        {
            switch (backend)
            {
            case InputDeviceBackend::BuiltIn:
                break;
            case InputDeviceBackend::XInput:
                device.hasXInputFeed = false;
                device.xInputNativeIdentity.clear();
                device.xInputNativeIdentityHash = 0;
                break;
            case InputDeviceBackend::RawInputHID:
                device.hasHidFeed = false;
                device.hidNativeIdentity.clear();
                device.hidNativeIdentityHash = 0;
                break;
            }

            refreshDeviceSummary(device);
        }

        devices.erase(
            std::remove_if(
                devices.begin(),
                devices.end(),
                [](const InputDeviceInfo &device)
                {
                    return !device.hasBuiltInFeed && !device.hasXInputFeed && !device.hasHidFeed;
                }),
            devices.end());
    }

    DeviceIndex InputDeviceRegistry::allocateDeviceIndex(InputDeviceType deviceType) const
    {
        for (std::uint16_t candidate = 0; candidate <= std::numeric_limits<DeviceIndex>::max(); ++candidate)
        {
            const DeviceIndex deviceIndex = static_cast<DeviceIndex>(candidate);
            if (!hasDeviceIndex(devices, deviceType, deviceIndex))
            {
                return deviceIndex;
            }
        }

        return std::numeric_limits<DeviceIndex>::max();
    }

    InputState::InputState()
    {
        currentButtons.reserve(32);
        pressedButtons.reserve(16);
        releasedButtons.reserve(16);
        axisValues.reserve(16);
        wheelDeltas.reserve(2);
        connectedDevices.reserve(4);
        activations.reserve(32);
        textInputUtf8.reserve(32);
    }

    void InputState::clear(bool emitReleaseActivations)
    {
        activations.clear();
        pressedButtons.clear();
        releasedButtons.clear();

        if (emitReleaseActivations)
        {
            for (InputControl control : currentButtons)
            {
                addUniqueControl(releasedButtons, control);
                activations.push_back(InputActivation{control, InputActivationType::ButtonReleased, 0.0f, 1.0f});
            }
        }

        currentButtons.clear();
        axisValues.clear();
        wheelDeltas.clear();
        textInputUtf8.clear();
        pendingTextHighSurrogate = 0;
        mouseDeltaX = 0;
        mouseDeltaY = 0;
        ++clearGeneration;
        clearMousePositionInternal();
    }

    void InputState::advanceFrame()
    {
        pressedButtons.clear();
        releasedButtons.clear();
        wheelDeltas.clear();
        activations.clear();
        textInputUtf8.clear();
        mouseDeltaX = 0;
        mouseDeltaY = 0;
    }

    bool InputState::isButtonDown(InputControl control) const
    {
        if (!isButtonControl(control))
        {
            return false;
        }

        return containsControl(currentButtons, control);
    }

    bool InputState::wasButtonPressed(InputControl control) const
    {
        if (!isButtonControl(control))
        {
            return false;
        }

        return containsControl(pressedButtons, control);
    }

    bool InputState::wasButtonReleased(InputControl control) const
    {
        if (!isButtonControl(control))
        {
            return false;
        }

        return containsControl(releasedButtons, control);
    }

    std::span<const InputControl> InputState::getCurrentButtonView() const
    {
        return currentButtons;
    }

    std::span<const std::pair<InputControl, float>> InputState::getAxisValueView() const
    {
        return axisValues;
    }

    float InputState::getAxis(InputControl control) const
    {
        if (!isAxisControl(control))
        {
            return 0.0f;
        }

        return getControlValue(axisValues, control);
    }

    int InputState::getMouseDeltaX() const
    {
        return mouseDeltaX;
    }

    int InputState::getMouseDeltaY() const
    {
        return mouseDeltaY;
    }

    bool InputState::hasMousePosition() const
    {
        return mousePositionKnown;
    }

    int InputState::getMouseX() const
    {
        return mouseX;
    }

    int InputState::getMouseY() const
    {
        return mouseY;
    }

    float InputState::getWheelDelta(InputControl control) const
    {
        if (!isWheelControl(control))
        {
            return 0.0f;
        }

        return getControlValue(wheelDeltas, control);
    }

    bool InputState::isDeviceConnected(InputDeviceType deviceType, DeviceIndex deviceIndex) const
    {
        if ((deviceType == InputDeviceType::Keyboard || deviceType == InputDeviceType::Mouse) && deviceIndex == 0)
        {
            return true;
        }

        InputDeviceId targetDeviceId{deviceType, deviceIndex};
        auto entry = std::lower_bound(
            connectedDevices.begin(),
            connectedDevices.end(),
            targetDeviceId,
            [](const InputDeviceId &left, const InputDeviceId &right)
            {
                if (left.deviceType != right.deviceType)
                {
                    return static_cast<int>(left.deviceType) < static_cast<int>(right.deviceType);
                }

                return left.deviceIndex < right.deviceIndex;
            });

        return entry != connectedDevices.end() && entry->deviceType == deviceType && entry->deviceIndex == deviceIndex;
    }

    bool InputState::tryGetFirstActivation(InputActivation &outActivation) const
    {
        if (activations.empty())
        {
            outActivation = {};
            return false;
        }

        outActivation = activations.front();
        return true;
    }

    void InputState::getActivations(std::vector<InputActivation> &outActivations) const
    {
        outActivations = activations;
    }

    std::span<const InputActivation> InputState::getActivationView() const
    {
        return activations;
    }

    bool InputState::hasTextInput() const
    {
        return !textInputUtf8.empty();
    }

    std::string_view InputState::getTextInputUtf8() const
    {
        return textInputUtf8;
    }

    void InputState::setButtonInternal(InputControl control, bool isDown)
    {
        if (!isButtonControl(control))
        {
            return;
        }

        bool wasDown = containsControl(currentButtons, control);

        if (isDown == wasDown)
        {
            return;
        }

        if (isDown)
        {
            addUniqueControl(currentButtons, control);
            addUniqueControl(pressedButtons, control);
        }
        else
        {
            removeControl(currentButtons, control);
            addUniqueControl(releasedButtons, control);
        }

        activations.push_back(
            InputActivation{
                control,
                isDown ? InputActivationType::ButtonPressed : InputActivationType::ButtonReleased,
                isDown ? 1.0f : 0.0f,
                wasDown ? 1.0f : 0.0f});
    }

    void InputState::setAxisInternal(InputControl control, float value)
    {
        if (!isAxisControl(control))
        {
            return;
        }

        float previousValue = getAxis(control);
        if (previousValue == value)
        {
            return;
        }

        setControlValue(axisValues, control, value);

        if (previousValue <= 0.0f && value > 0.0f)
        {
            activations.push_back(InputActivation{control, InputActivationType::AxisPositive, value, previousValue});
        }
        else if (previousValue >= 0.0f && value < 0.0f)
        {
            activations.push_back(InputActivation{control, InputActivationType::AxisNegative, value, previousValue});
        }
        else
        {
            activations.push_back(InputActivation{control, InputActivationType::AxisChanged, value, previousValue});
        }
    }

    void InputState::addMouseDeltaInternal(int deltaX, int deltaY)
    {
        mouseDeltaX += deltaX;
        mouseDeltaY += deltaY;
    }

    void InputState::setMousePositionInternal(int x, int y)
    {
        mouseX = x;
        mouseY = y;
        mousePositionKnown = true;
    }

    void InputState::clearMousePositionInternal()
    {
        mouseX = 0;
        mouseY = 0;
        mousePositionKnown = false;
    }

    void InputState::addWheelDeltaInternal(InputControl control, float amount)
    {
        if (!isWheelControl(control) || amount == 0.0f)
        {
            return;
        }

        float newAmount = getControlValue(wheelDeltas, control) + amount;
        setControlValue(wheelDeltas, control, newAmount);
        activations.push_back(
            InputActivation{
                control,
                amount > 0.0f ? InputActivationType::WheelPositive : InputActivationType::WheelNegative,
                amount,
                newAmount - amount});
    }

    void InputState::addTextUtf8Internal(std::string_view text)
    {
        textInputUtf8.append(text);
    }

    void InputState::addTextCodepointInternal(char32_t codepoint)
    {
        appendUtf8Codepoint(textInputUtf8, codepoint);
    }

    char16_t InputState::getPendingTextHighSurrogateInternal() const
    {
        return pendingTextHighSurrogate;
    }

    void InputState::setPendingTextHighSurrogateInternal(char16_t codeUnit)
    {
        pendingTextHighSurrogate = codeUnit;
    }

    void InputState::clearTextCompositionInternal()
    {
        pendingTextHighSurrogate = 0;
    }

    void InputState::setDeviceConnectedInternal(InputDeviceType deviceType, DeviceIndex deviceIndex, bool connected)
    {
        InputDeviceId targetDeviceId{deviceType, deviceIndex};
        auto entry = std::lower_bound(
            connectedDevices.begin(),
            connectedDevices.end(),
            targetDeviceId,
            [](const InputDeviceId &left, const InputDeviceId &right)
            {
                if (left.deviceType != right.deviceType)
                {
                    return static_cast<int>(left.deviceType) < static_cast<int>(right.deviceType);
                }

                return left.deviceIndex < right.deviceIndex;
            });
        bool found = entry != connectedDevices.end() && entry->deviceType == deviceType && entry->deviceIndex == deviceIndex;

        if (connected)
        {
            if (!found)
            {
                connectedDevices.insert(entry, targetDeviceId);
            }
        }
        else if (found)
        {
            connectedDevices.erase(entry);
        }
    }

    void InputState::clearDeviceInternal(InputDeviceRef device)
    {
        for (std::size_t controlIndex = 0; controlIndex < currentButtons.size();)
        {
            InputControl control = currentButtons[controlIndex];
            if (control.deviceType == device.deviceType && control.deviceIndex == device.deviceIndex)
            {
                addUniqueControl(releasedButtons, control);
                activations.push_back(InputActivation{control, InputActivationType::ButtonReleased, 0.0f, 1.0f});
                currentButtons.erase(currentButtons.begin() + controlIndex);
            }
            else
            {
                ++controlIndex;
            }
        }

        for (std::size_t valueIndex = 0; valueIndex < axisValues.size();)
        {
            const InputControl control = axisValues[valueIndex].first;
            if (control.deviceType == device.deviceType && control.deviceIndex == device.deviceIndex)
            {
                axisValues.erase(axisValues.begin() + valueIndex);
            }
            else
            {
                ++valueIndex;
            }
        }
    }

} // namespace GameWIP::Input

namespace GameWIP::Input::Internal
{
    void InputStateAccess::setButton(InputState &inputState, InputControl control, bool isDown)
    {
        inputState.setButtonInternal(control, isDown);
    }

    void InputStateAccess::setAxis(InputState &inputState, InputControl control, float value)
    {
        inputState.setAxisInternal(control, value);
    }

    void InputStateAccess::addMouseDelta(InputState &inputState, int deltaX, int deltaY)
    {
        inputState.addMouseDeltaInternal(deltaX, deltaY);
    }

    void InputStateAccess::setMousePosition(InputState &inputState, int x, int y)
    {
        inputState.setMousePositionInternal(x, y);
    }

    void InputStateAccess::clearMousePosition(InputState &inputState)
    {
        inputState.clearMousePositionInternal();
    }

    void InputStateAccess::addWheelDelta(InputState &inputState, InputControl control, float amount)
    {
        inputState.addWheelDeltaInternal(control, amount);
    }

    void InputStateAccess::addTextUtf8(InputState &inputState, std::string_view text)
    {
        inputState.addTextUtf8Internal(text);
    }

    void InputStateAccess::addTextCodepoint(InputState &inputState, char32_t codepoint)
    {
        inputState.addTextCodepointInternal(codepoint);
    }

    char16_t InputStateAccess::getPendingTextHighSurrogate(const InputState &inputState)
    {
        return inputState.getPendingTextHighSurrogateInternal();
    }

    void InputStateAccess::setPendingTextHighSurrogate(InputState &inputState, char16_t codeUnit)
    {
        inputState.setPendingTextHighSurrogateInternal(codeUnit);
    }

    void InputStateAccess::clearTextComposition(InputState &inputState)
    {
        inputState.clearTextCompositionInternal();
    }

    void InputStateAccess::setDeviceConnected(InputState &inputState, InputDeviceType deviceType, DeviceIndex deviceIndex, bool connected)
    {
        inputState.setDeviceConnectedInternal(deviceType, deviceIndex, connected);
    }

    void InputStateAccess::clearDevice(InputState &inputState, InputDeviceRef device)
    {
        inputState.clearDeviceInternal(device);
    }

    std::uint64_t InputStateAccess::getClearGeneration(const InputState &inputState)
    {
        return inputState.clearGeneration;
    }

    InputDeviceRef InputDeviceRegistryAccess::upsertDevice(InputDeviceRegistry &registry, const InputDeviceInfo &deviceInfo)
    {
        return registry.upsertDevice(deviceInfo);
    }

    void InputDeviceRegistryAccess::setDeviceConnected(InputDeviceRegistry &registry, InputDeviceRef device, bool connected)
    {
        registry.setDeviceConnected(device, connected);
    }

    void InputDeviceRegistryAccess::setDeviceCanonical(InputDeviceRegistry &registry, InputDeviceRef device, bool canonical)
    {
        registry.setDeviceCanonical(device, canonical);
    }

    void InputDeviceRegistryAccess::replaceDeviceControls(
        InputDeviceRegistry &registry,
        InputDeviceRef device,
        std::span<const InputControlInfo> controls)
    {
        registry.replaceDeviceControls(device, controls);
    }

    void InputDeviceRegistryAccess::clearBackend(InputDeviceRegistry &registry, InputDeviceBackend backend)
    {
        registry.clearBackend(backend);
    }

    bool InputDeviceRegistryAccess::shouldFeedDevice(const InputDeviceRegistry &registry, InputDeviceRef device)
    {
        return registry.shouldFeedDevice(device);
    }

    bool InputDeviceRegistryAccess::shouldFeedDeviceBackend(const InputDeviceRegistry &registry, InputDeviceRef device, InputDeviceBackend backend)
    {
        return registry.shouldFeedDeviceBackend(device, backend);
    }

    InputDeviceRef InputDeviceRegistryAccess::mergeDeviceBackend(
        InputDeviceRegistry &registry,
        InputDeviceRef device,
        const InputDeviceInfo &backendInfo)
    {
        return registry.mergeDeviceBackend(device, backendInfo);
    }

    void InputDeviceRegistryAccess::setDeviceBackendConnected(
        InputDeviceRegistry &registry,
        InputDeviceRef device,
        InputDeviceBackend backend,
        bool connected)
    {
        registry.setDeviceBackendConnected(device, backend, connected);
    }

    void InputDeviceRegistryAccess::mergeDeviceControls(
        InputDeviceRegistry &registry,
        InputDeviceRef device,
        std::span<const InputControlInfo> controls)
    {
        registry.mergeDeviceControls(device, controls);
    }
} // namespace GameWIP::Input::Internal
