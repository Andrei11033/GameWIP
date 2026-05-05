#include "input.h"

#include <algorithm>

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

        /// @brief Returns whether a control is stored in a compact control list.
        /// @param controls Control list to search.
        /// @param control Control to search for.
        /// @return True if the control exists in the list.
        bool containsControl(const std::vector<InputControl> &controls, InputControl control)
        {
            return std::find(controls.begin(), controls.end(), control) != controls.end();
        }

        /// @brief Adds a control to a compact list if it is not already present.
        /// @param controls Control list to update.
        /// @param control Control to add.
        void addUniqueControl(std::vector<InputControl> &controls, InputControl control)
        {
            if (!containsControl(controls, control))
            {
                controls.push_back(control);
            }
        }

        /// @brief Removes a control from a compact list.
        /// @param controls Control list to update.
        /// @param control Control to remove.
        void removeControl(std::vector<InputControl> &controls, InputControl control)
        {
            auto entry = std::find(controls.begin(), controls.end(), control);
            if (entry != controls.end())
            {
                controls.erase(entry);
            }
        }

        /// @brief Finds a control/value pair in a compact value list.
        /// @param values Control/value list to search.
        /// @param control Control to search for.
        /// @return Iterator to the matching entry, or values.end().
        auto findControlValue(std::vector<std::pair<InputControl, float>> &values, InputControl control)
        {
            return std::find_if(
                values.begin(),
                values.end(),
                [control](const std::pair<InputControl, float> &entry)
                {
                    return entry.first == control;
                });
        }

        /// @brief Finds a control/value pair in a compact value list.
        /// @param values Control/value list to search.
        /// @param control Control to search for.
        /// @return Iterator to the matching entry, or values.end().
        auto findControlValue(const std::vector<std::pair<InputControl, float>> &values, InputControl control)
        {
            return std::find_if(
                values.begin(),
                values.end(),
                [control](const std::pair<InputControl, float> &entry)
                {
                    return entry.first == control;
                });
        }

        /// @brief Stores or removes a value in a compact value list.
        /// @param values Control/value list to update.
        /// @param control Control to update.
        /// @param value Value to store, or zero to remove.
        void setControlValue(std::vector<std::pair<InputControl, float>> &values, InputControl control, float value)
        {
            auto entry = findControlValue(values, control);
            if (value == 0.0f)
            {
                if (entry != values.end())
                {
                    values.erase(entry);
                }
                return;
            }

            if (entry != values.end())
            {
                entry->second = value;
            }
            else
            {
                values.push_back({control, value});
            }
        }

        /// @brief Returns a stored control value, or zero when absent.
        /// @param values Control/value list to search.
        /// @param control Control to search for.
        /// @return Stored value, or 0 if the control is absent.
        float getControlValue(const std::vector<std::pair<InputControl, float>> &values, InputControl control)
        {
            auto entry = findControlValue(values, control);
            return entry != values.end() ? entry->second : 0.0f;
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
                activations.push_back(InputActivation{
                    control,
                    InputActivationType::ButtonReleased,
                    0.0f});
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

        auto entry = std::find_if(
            connectedDevices.begin(),
            connectedDevices.end(),
            [deviceType, deviceIndex](const InputDeviceId &deviceId)
            {
                return deviceId.deviceType == deviceType &&
                       deviceId.deviceIndex == deviceIndex;
            });

        return entry != connectedDevices.end();
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

        activations.push_back(InputActivation{
            control,
            isDown ? InputActivationType::ButtonPressed : InputActivationType::ButtonReleased,
            isDown ? 1.0f : 0.0f});
    }

    void InputState::setAxisInternal(InputControl control, float value)
    {
        if (!isAxisControl(control))
        {
            return;
        }

        float previousValue = getAxis(control);
        setControlValue(axisValues, control, value);

        if (previousValue <= 0.0f && value > 0.0f)
        {
            activations.push_back(InputActivation{control, InputActivationType::AxisPositive, value});
        }
        else if (previousValue >= 0.0f && value < 0.0f)
        {
            activations.push_back(InputActivation{control, InputActivationType::AxisNegative, value});
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
        activations.push_back(InputActivation{
            control,
            amount > 0.0f ? InputActivationType::WheelPositive : InputActivationType::WheelNegative,
            amount});
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
        auto entry = std::find_if(
            connectedDevices.begin(),
            connectedDevices.end(),
            [deviceType, deviceIndex](const InputDeviceId &deviceId)
            {
                return deviceId.deviceType == deviceType &&
                       deviceId.deviceIndex == deviceIndex;
            });

        if (connected)
        {
            if (entry == connectedDevices.end())
            {
                connectedDevices.push_back(InputDeviceId{deviceType, deviceIndex});
            }
        }
        else if (entry != connectedDevices.end())
        {
            connectedDevices.erase(entry);
        }
    }

}